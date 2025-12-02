#pragma once

#include <cstddef>
#include <span>
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <string_view>
#include <algorithm>
#include <cstring>
#include "reader_base.hpp"

namespace tiff {

/// Read-only view that owns allocated buffer
class OwnedBufferReadView {
private:
    std::span<const std::byte> data_;
    std::shared_ptr<std::byte[]> buffer_;  // Owns the data

public:
    OwnedBufferReadView() noexcept = default;
    
    OwnedBufferReadView(std::span<const std::byte> data, std::shared_ptr<std::byte[]> buffer) noexcept
        : data_(data), buffer_(std::move(buffer)) {}
    
    [[nodiscard]] std::span<const std::byte> data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    // Move-only
    OwnedBufferReadView(OwnedBufferReadView&&) noexcept = default;
    OwnedBufferReadView& operator=(OwnedBufferReadView&&) noexcept = default;
    OwnedBufferReadView(const OwnedBufferReadView&) = delete;
    OwnedBufferReadView& operator=(const OwnedBufferReadView&) = delete;
};

static_assert(DataReadOnlyView<OwnedBufferReadView>, "OwnedBufferReadView must satisfy DataReadOnlyView concept");

/// Write-only view that writes buffer on flush/destruction
class OwnedBufferWriteView {
private:
    std::span<std::byte> data_;
    std::shared_ptr<std::byte[]> buffer_;  // Owns the data
    int fd_{-1};
    std::size_t offset_{0};
    bool flushed_{false};

public:
    OwnedBufferWriteView() noexcept = default;
    
    OwnedBufferWriteView(std::span<std::byte> data, std::shared_ptr<std::byte[]> buffer, int fd, std::size_t offset) noexcept
        : data_(data), buffer_(std::move(buffer)), fd_(fd), offset_(offset) {}
    
    ~OwnedBufferWriteView() noexcept {
        (void)flush();  // Flush on destruction if not already done
    }

    // Since the buffer is allocated (and owned), we can support inplace readback
    // before flush() is called.
    static constexpr bool supports_inplace_readback = true;
    
    [[nodiscard]] std::span<std::byte> data() noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    // Write buffer to file using pwrite
    [[nodiscard]] Result<void> flush() noexcept {
        if (flushed_ || fd_ < 0 || data_.empty()) {
            return Ok();
        }
        
        ssize_t written = ::pwrite(fd_, data_.data(), data_.size(), static_cast<off_t>(offset_));
        flushed_ = true;
        
        if (written < 0) {
            return Err(Error::Code::WriteError, "pwrite failed: " + std::string(std::strerror(errno)));
        }
        
        if (static_cast<std::size_t>(written) != data_.size()) {
            return Err(Error::Code::WriteError, "pwrite incomplete");
        }
        
        return Ok();
    }
    
    // Move-only
    OwnedBufferWriteView(OwnedBufferWriteView&& other) noexcept
        : data_(other.data_)
        , buffer_(std::move(other.buffer_))
        , fd_(other.fd_)
        , offset_(other.offset_)
        , flushed_(other.flushed_) {
        other.fd_ = -1;
        other.flushed_ = true;  // Prevent double flush
    }
    
    OwnedBufferWriteView& operator=(OwnedBufferWriteView&& other) noexcept {
        if (this != &other) {
            (void)flush();
            data_ = other.data_;
            buffer_ = std::move(other.buffer_);
            fd_ = other.fd_;
            offset_ = other.offset_;
            flushed_ = other.flushed_;
            other.fd_ = -1;
            other.flushed_ = true;
        }
        return *this;
    }
    
    OwnedBufferWriteView(const OwnedBufferWriteView&) = delete;
    OwnedBufferWriteView& operator=(const OwnedBufferWriteView&) = delete;
};

static_assert(DataWriteOnlyView<OwnedBufferWriteView>, "OwnedBufferWriteView must satisfy DataWriteOnlyView concept");
static_assert(DataWriteViewWithReadback<OwnedBufferWriteView>, "OwnedBufferWriteView must support readback");


namespace detail {
    // Access mode policies for pread/pwrite
    struct ReadOnlyAccess {
        static constexpr int open_flags = O_RDONLY;
        static constexpr bool can_read = true;
        static constexpr bool can_write = false;
    };
    
    struct WriteOnlyAccess {
        static constexpr int open_flags = O_RDWR;
        static constexpr bool can_read = false;
        static constexpr bool can_write = true;
    };
    
    struct ReadWriteAccess {
        static constexpr int open_flags = O_RDWR;
        static constexpr bool can_read = true;
        static constexpr bool can_write = true;
    };
}

/// Base template for pread/pwrite file access
/// AccessPolicy determines read/write capabilities
template <typename AccessPolicy>
class PreadFileBase {
protected:
    int fd_{-1};
    std::size_t size_{0};
    std::string path_;

public:
    using ReadViewType = OwnedBufferReadView;
    using WriteViewType = OwnedBufferWriteView;
    
    PreadFileBase() noexcept = default;
    
    explicit PreadFileBase(std::string_view path, bool create_if_missing = false) noexcept {
        (void)open(path, create_if_missing);
    }
    
    ~PreadFileBase() noexcept {
        close();
    }
    
    PreadFileBase(const PreadFileBase&) = delete;
    PreadFileBase& operator=(const PreadFileBase&) = delete;
    
    PreadFileBase(PreadFileBase&& other) noexcept
        : fd_(other.fd_)
        , size_(other.size_)
        , path_(std::move(other.path_)) {
        other.fd_ = -1;
        other.size_ = 0;
    }
    
    PreadFileBase& operator=(PreadFileBase&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            size_ = other.size_;
            path_ = std::move(other.path_);
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
        return Ok();
    }
    
    void close() noexcept {
        if (fd_ >= 0) {
            if constexpr (AccessPolicy::can_write) {
                fsync(fd_);  // Ensure data is written for writable files
            }
            ::close(fd_);
            fd_ = -1;
            size_ = 0;
        }
    }
    
    /// Thread-safe read using pread (only available if can_read is true)
    [[nodiscard]] Result<ReadViewType> read(std::size_t offset, std::size_t size) const noexcept 
        requires (AccessPolicy::can_read) {
        if (!is_valid()) {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        if (offset >= size_) {
            return Err(Error::Code::OutOfBounds, "Read offset beyond file size");
        }
        
        std::size_t bytes_to_read = std::min(size, size_ - offset);
        
        // Allocate buffer
        auto buffer = std::shared_ptr<std::byte[]>(new std::byte[bytes_to_read]);
        
        ssize_t bytes_read = ::pread(fd_, buffer.get(), bytes_to_read, static_cast<off_t>(offset));
        
        if (bytes_read < 0) {
            return Err(Error::Code::ReadError, 
                       "pread failed: " + std::string(std::strerror(errno)));
        }
        
        std::span<const std::byte> data_span(buffer.get(), static_cast<std::size_t>(bytes_read));
        
        return Ok(OwnedBufferReadView(data_span, buffer));
    }
    
    /// Thread-safe write using pwrite (only available if can_write is true)
    [[nodiscard]] Result<WriteViewType> write(std::size_t offset, std::size_t size) noexcept 
        requires (AccessPolicy::can_write) {
        if (!is_valid()) {
            return Err(Error::Code::WriteError, "File not open");
        }
        
        if (offset > size_) {
            return Err(Error::Code::OutOfBounds, "Write offset beyond file size");
        }
        
        // Allocate buffer for user to write into
        auto buffer = std::shared_ptr<std::byte[]>(new std::byte[size]);
        std::span<std::byte> data_span(buffer.get(), size);
        
        return Ok(OwnedBufferWriteView(data_span, buffer, fd_, offset));
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
        
        if (ftruncate(fd_, static_cast<off_t>(new_size)) != 0) {
            return Err(Error::Code::WriteError, "Failed to resize file");
        }
        
        size_ = new_size;
        return Ok();
    }
    
    /// Flush writes to disk (only available if can_write is true)
    [[nodiscard]] Result<void> flush() noexcept 
        requires (AccessPolicy::can_write) {
        if (fd_ >= 0) {
            if (fsync(fd_) != 0) {
                return Err(Error::Code::WriteError, "Failed to sync file");
            }
        }
        return Ok();
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        return fd_ >= 0;
    }
    
    [[nodiscard]] std::string_view path() const noexcept {
        return path_;
    }
};

/// File reader using pread (POSIX) - thread-safe without locks, read-only
using PreadFileReader = PreadFileBase<detail::ReadOnlyAccess>;

static_assert(RawReader<PreadFileReader>, "PreadFileReader must satisfy RawReader concept");

/// File writer using pwrite (POSIX) - thread-safe without locks, write-only
using PwriteFileWriter = PreadFileBase<detail::WriteOnlyAccess>;

static_assert(RawWriter<PwriteFileWriter>, "PwriteFileWriter must satisfy RawWriter concept");

/// File reader+writer using pread/pwrite (POSIX) - thread-safe without locks, read-write
using PreadFileReadWriter = PreadFileBase<detail::ReadWriteAccess>;

static_assert(RawReader<PreadFileReadWriter>, "PreadFileReadWriter must satisfy RawReader concept");
static_assert(RawWriter<PreadFileReadWriter>, "PreadFileReadWriter must satisfy RawWriter concept");

} // namespace tiff