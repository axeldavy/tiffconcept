#pragma once

#include <cstddef>
#include <span>
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string>
#include <string_view>
#include <algorithm>
#include <cstring>
#include <type_traits>
#include "reader_base.hpp"

namespace tiff {

namespace detail {
    struct MmapDeleter {
        std::size_t size;
        
        void operator()(void* ptr) const noexcept {
            if (ptr && ptr != MAP_FAILED) {
                munmap(ptr, size);
            }
        }
    };
    
    // Access mode policies
    struct ReadOnlyAccess {
        static constexpr int open_flags = O_RDONLY;
        static constexpr int prot_flags = PROT_READ;
        static constexpr bool can_read = true;
        static constexpr bool can_write = false;
    };
    
    struct WriteOnlyAccess {
        static constexpr int open_flags = O_WRONLY;
        static constexpr int prot_flags = PROT_WRITE;
        static constexpr bool can_read = false;
        static constexpr bool can_write = true;
    };
    
    struct ReadWriteAccess {
        static constexpr int open_flags = O_RDWR;
        static constexpr int prot_flags = PROT_READ | PROT_WRITE;
        static constexpr bool can_read = true;
        static constexpr bool can_write = true;
    };
}

/// Read-only view for memory-mapped files - shares mmap ownership
class MmapReadView {
private:
    std::span<const std::byte> data_;
    std::shared_ptr<void> mmap_handle_;

public:
    MmapReadView() noexcept = default;
    
    MmapReadView(std::span<const std::byte> data, std::shared_ptr<void> handle) noexcept
        : data_(data), mmap_handle_(std::move(handle)) {}
    
    [[nodiscard]] std::span<const std::byte> data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    MmapReadView(MmapReadView&&) noexcept = default;
    MmapReadView& operator=(MmapReadView&&) noexcept = default;
    MmapReadView(const MmapReadView&) = delete;
    MmapReadView& operator=(const MmapReadView&) = delete;
};

static_assert(DataReadOnlyView<MmapReadView>, "MmapReadView must satisfy DataReadOnlyView concept");

/// Write-only view for memory-mapped files - writes on flush or destructor
template <typename AccessPolicy>
class MmapWriteView {
private:
    std::span<std::byte> data_;
    std::shared_ptr<void> mmap_handle_;

public:
    // Readback support depends on whether the mmap has read permissions
    static constexpr bool supports_inplace_readback = AccessPolicy::can_read;
    MmapWriteView() noexcept = default;
    
    MmapWriteView(std::span<std::byte> data, std::shared_ptr<void> handle) noexcept
        : data_(data), mmap_handle_(std::move(handle)) {}
    
    [[nodiscard]] std::span<std::byte> data() noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    [[nodiscard]] Result<void> flush() noexcept {
        if (!data_.empty() && mmap_handle_) {
            if (msync(data_.data(), data_.size(), MS_SYNC) != 0) {
                return Err(Error::Code::WriteError, "Failed to sync mmap to disk");
            }
        }
        return Ok();
    }
    
    MmapWriteView(MmapWriteView&&) noexcept = default;
    MmapWriteView& operator=(MmapWriteView&&) noexcept = default;
    MmapWriteView(const MmapWriteView&) = delete;
    MmapWriteView& operator=(const MmapWriteView&) = delete;
};

static_assert(DataWriteOnlyView<MmapWriteView<detail::ReadWriteAccess>>, "MmapWriteView must satisfy DataWriteOnlyView concept");
static_assert(DataWriteOnlyView<MmapWriteView<detail::WriteOnlyAccess>>, "MmapWriteView must satisfy DataWriteOnlyView concept");
static_assert(DataWriteViewWithReadback<MmapWriteView<detail::ReadWriteAccess>>, "MmapWriteView must support readback");


/// Base template for memory-mapped file access
/// AccessPolicy determines read/write capabilities
template <typename AccessPolicy>
class MmapFileBase {
protected:
    int fd_{-1};
    std::size_t size_{0};
    std::string path_;
    std::shared_ptr<void> mmap_handle_;

public:
    using ReadViewType = MmapReadView;
    using WriteViewType = MmapWriteView<AccessPolicy>;
    
    MmapFileBase() noexcept = default;
    
    explicit MmapFileBase(std::string_view path, bool create_if_missing = false) noexcept {
        (void)open(path, create_if_missing);
    }
    
    ~MmapFileBase() noexcept {
        close();
    }
    
    MmapFileBase(const MmapFileBase&) = delete;
    MmapFileBase& operator=(const MmapFileBase&) = delete;
    
    MmapFileBase(MmapFileBase&& other) noexcept
        : fd_(other.fd_)
        , size_(other.size_)
        , path_(std::move(other.path_))
        , mmap_handle_(std::move(other.mmap_handle_)) {
        other.fd_ = -1;
        other.size_ = 0;
    }
    
    MmapFileBase& operator=(MmapFileBase&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            size_ = other.size_;
            path_ = std::move(other.path_);
            mmap_handle_ = std::move(other.mmap_handle_);
            other.fd_ = -1;
            other.size_ = 0;
        }
        return *this;
    }
    
    [[nodiscard]] Result<void> open(std::string_view path, bool create_if_missing = false) noexcept {
        close();
        
        path_ = path;
        int flags = AccessPolicy::open_flags;
        if (create_if_missing && AccessPolicy::can_write) {
            flags |= O_CREAT;
        }
        
        fd_ = ::open(path_.c_str(), flags, 0644);
        
        if (fd_ < 0) {
            return Err(Error::Code::FileNotFound, 
                       "Failed to open file: " + std::string(path));
        }
        
        struct stat st;
        if (fstat(fd_, &st) != 0) {
            close();
            return Err(Error::Code::ReadError, 
                       "Failed to get file size: " + std::string(path));
        }
        
        size_ = static_cast<std::size_t>(st.st_size);
        
        if (size_ > 0) {
            void* addr = mmap(nullptr, size_, AccessPolicy::prot_flags, MAP_SHARED, fd_, 0);
            if (addr == MAP_FAILED) {
                close();
                return Err(Error::Code::ReadError, "Failed to mmap file: " + std::string(path));
            }
            
            // Advise kernel for sequential access (helps with read performance)
            if constexpr (AccessPolicy::can_read) {
                madvise(addr, size_, MADV_SEQUENTIAL);
            }
            
            mmap_handle_ = std::shared_ptr<void>(addr, detail::MmapDeleter{size_});
        }
        
        return Ok();
    }
    
    void close() noexcept {
        if (mmap_handle_) {
            // Sync changes before unmapping if writable
            if constexpr (AccessPolicy::can_write) {
                msync(mmap_handle_.get(), size_, MS_SYNC);
            }
            mmap_handle_.reset();
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
            size_ = 0;
        }
    }
    
    /// Zero-copy read (only available if can_read is true)
    [[nodiscard]] Result<ReadViewType> read(std::size_t offset, std::size_t size) const noexcept 
        requires (AccessPolicy::can_read) {
        if (!is_valid()) {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        if (offset >= size_) {
            return Err(Error::Code::OutOfBounds, "Read offset beyond file size");
        }
        
        std::size_t bytes_to_read = std::min(size, size_ - offset);
        
        auto* base = static_cast<const std::byte*>(mmap_handle_.get());
        std::span<const std::byte> data_span(base + offset, bytes_to_read);
        
        return Ok(MmapReadView(data_span, mmap_handle_));
    }
    
    /// Zero-copy write (only available if can_write is true)
    [[nodiscard]] Result<WriteViewType> write(std::size_t offset, std::size_t size) noexcept 
        requires (AccessPolicy::can_write) {
        if (!is_valid()) {
            return Err(Error::Code::WriteError, "File not open");
        }
        
        if (offset >= size_) {
            return Err(Error::Code::OutOfBounds, "Write offset beyond file size");
        }
        
        std::size_t bytes_to_write = std::min(size, size_ - offset);
        
        auto* base = static_cast<std::byte*>(mmap_handle_.get());
        std::span<std::byte> data_span(base + offset, bytes_to_write);
        
        return Ok(MmapWriteView<AccessPolicy>(data_span, mmap_handle_));
    }
    
    [[nodiscard]] Result<std::size_t> size() const noexcept {
        if (!is_valid()) {
            return Err(Error::Code::ReadError, "File not open");
        }
        return Ok(size_);
    }
    
    /// Resize file (only available if can_write is true)
    [[nodiscard]] Result<void> resize(std::size_t new_size) noexcept 
        requires (AccessPolicy::can_write) {
        if (!is_valid()) {
            return Err(Error::Code::WriteError, "File not open");
        }
        
        if (new_size == size_) {
            return Ok();
        }
        
        // Unmap current region
        mmap_handle_.reset();
        
        // Resize file
        if (ftruncate(fd_, static_cast<off_t>(new_size)) != 0) {
            return Err(Error::Code::WriteError, "Failed to resize file");
        }
        
        size_ = new_size;
        
        // Remap if size > 0
        if (size_ > 0) {
            void* addr = mmap(nullptr, size_, AccessPolicy::prot_flags, MAP_SHARED, fd_, 0);
            if (addr == MAP_FAILED) {
                return Err(Error::Code::WriteError, "Failed to remap file after resize");
            }
            
            mmap_handle_ = std::shared_ptr<void>(addr, detail::MmapDeleter{size_});
        }
        
        return Ok();
    }
    
    /// Flush writes to disk (only available if can_write is true)
    [[nodiscard]] Result<void> flush() noexcept 
        requires (AccessPolicy::can_write) {
        if (mmap_handle_) {
            if (msync(mmap_handle_.get(), size_, MS_SYNC) != 0) {
                return Err(Error::Code::WriteError, "Failed to flush mmap to disk");
            }
        }
        return Ok();
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        return fd_ >= 0 && (size_ == 0 || mmap_handle_ != nullptr);
    }
    
    [[nodiscard]] std::string_view path() const noexcept {
        return path_;
    }
};

/// Memory-mapped file reader (POSIX) - zero-copy, thread-safe, read-only
using MmapFileReader = MmapFileBase<detail::ReadOnlyAccess>;

static_assert(RawReader<MmapFileReader>, "MmapFileReader must satisfy RawReader concept");

/// Memory-mapped file writer (POSIX) - zero-copy, thread-safe, write-only
using MmapFileWriter = MmapFileBase<detail::WriteOnlyAccess>;

static_assert(RawWriter<MmapFileWriter>, "MmapFileWriter must satisfy RawWriter concept");

/// Memory-mapped file reader+writer (POSIX) - zero-copy, thread-safe, read-write
using MmapFileReadWriter = MmapFileBase<detail::ReadWriteAccess>;

static_assert(RawReader<MmapFileReadWriter>, "MmapFileReadWriter must satisfy RawReader concept");
static_assert(RawWriter<MmapFileReadWriter>, "MmapFileReadWriter must satisfy RawWriter concept");

} // namespace tiff