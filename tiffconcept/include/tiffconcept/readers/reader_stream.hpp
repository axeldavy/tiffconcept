#pragma once

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include "../reader_base.hpp"

namespace tiffconcept {
namespace stream_impl {

/// Read-only view that owns allocated buffer
class OwnedBufferReadView {
private:
    std::span<const std::byte> data_;
    std::shared_ptr<std::byte[]> buffer_;

public:
    OwnedBufferReadView() noexcept = default;
    
    OwnedBufferReadView(std::span<const std::byte> data, std::shared_ptr<std::byte[]> buffer) noexcept
        : data_(data), buffer_(std::move(buffer)) {}
    
    [[nodiscard]] std::span<const std::byte> data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    OwnedBufferReadView(OwnedBufferReadView&&) noexcept = default;
    OwnedBufferReadView& operator=(OwnedBufferReadView&&) noexcept = default;
    OwnedBufferReadView(const OwnedBufferReadView&) = delete;
    OwnedBufferReadView& operator=(const OwnedBufferReadView&) = delete;
};

static_assert(DataReadOnlyView<OwnedBufferReadView>, "OwnedBufferReadView must satisfy DataReadOnlyView concept");

/// Write-only view that writes on flush
class OwnedBufferWriteView {
private:
    std::span<std::byte> data_;
    std::shared_ptr<std::byte[]> buffer_;
    std::iostream* stream_{nullptr};
    std::mutex* mutex_{nullptr};
    std::size_t offset_{0};
    bool flushed_{false};

public:
    OwnedBufferWriteView() noexcept = default;
    
    OwnedBufferWriteView(std::span<std::byte> data, std::shared_ptr<std::byte[]> buffer,
                         std::iostream* stream, std::mutex* mutex, std::size_t offset) noexcept
        : data_(data), buffer_(std::move(buffer)), stream_(stream), mutex_(mutex), offset_(offset) {}
    
    ~OwnedBufferWriteView() noexcept {
        (void)flush();
    }

    // Since the buffer is allocated (and owned), we can support inplace readback
    // before flush() is called.
    static constexpr bool supports_inplace_readback = true;

    [[nodiscard]] std::span<std::byte> data() noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    [[nodiscard]] Result<void> flush() noexcept {
        if (flushed_ || !stream_ || !mutex_ || data_.empty()) {
            return Ok();
        }
        
        std::lock_guard<std::mutex> lock(*mutex_);
        
        stream_->seekp(static_cast<std::streamoff>(offset_), std::ios::beg);
        if (!*stream_) {
            flushed_ = true;
            return Err(Error::Code::WriteError, "Failed to seek to offset");
        }
        
        stream_->write(reinterpret_cast<const char*>(data_.data()), 
                      static_cast<std::streamsize>(data_.size()));
        if (!*stream_) {
            flushed_ = true;
            return Err(Error::Code::WriteError, "Failed to write to file");
        }
        
        stream_->flush();
        flushed_ = true;
        
        return Ok();
    }
    
    OwnedBufferWriteView(OwnedBufferWriteView&& other) noexcept
        : data_(other.data_)
        , buffer_(std::move(other.buffer_))
        , stream_(other.stream_)
        , mutex_(other.mutex_)
        , offset_(other.offset_)
        , flushed_(other.flushed_) {
        other.stream_ = nullptr;
        other.mutex_ = nullptr;
        other.flushed_ = true;
    }
    
    OwnedBufferWriteView& operator=(OwnedBufferWriteView&& other) noexcept {
        if (this != &other) {
            (void)flush();
            data_ = other.data_;
            buffer_ = std::move(other.buffer_);
            stream_ = other.stream_;
            mutex_ = other.mutex_;
            offset_ = other.offset_;
            flushed_ = other.flushed_;
            other.stream_ = nullptr;
            other.mutex_ = nullptr;
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
    // Access mode policies for streams
    struct ReadOnlyAccess {
        static constexpr std::ios::openmode open_mode = std::ios::binary | std::ios::in;
        static constexpr bool can_read = true;
        static constexpr bool can_write = false;
    };
    
    struct WriteOnlyAccess {
        static constexpr std::ios::openmode open_mode = std::ios::binary | std::ios::in | std::ios::out;
        static constexpr bool can_read = false;
        static constexpr bool can_write = true;
    };
    
    struct ReadWriteAccess {
        static constexpr std::ios::openmode open_mode = std::ios::binary | std::ios::in | std::ios::out;
        static constexpr bool can_read = true;
        static constexpr bool can_write = true;
    };
} // namespace detail

} // namespace stream_impl

/// Base template for stream-based file access
/// AccessPolicy determines read/write capabilities
template <typename AccessPolicy>
class StreamFileBase {
protected:
    mutable std::fstream stream_;
    mutable std::mutex mutex_;
    std::size_t size_{0};
    std::string path_;

public:
    using ReadViewType = stream_impl::OwnedBufferReadView;
    using WriteViewType = stream_impl::OwnedBufferWriteView;

    static constexpr bool read_must_allocate = true;
    
    StreamFileBase() noexcept = default;
    
    explicit StreamFileBase(std::string_view path) noexcept {
        (void)open(path);
    }
    
    ~StreamFileBase() noexcept {
        close();
    }
    
    StreamFileBase(const StreamFileBase&) = delete;
    StreamFileBase& operator=(const StreamFileBase&) = delete;
    
    StreamFileBase(StreamFileBase&& other) noexcept
        : stream_(std::move(other.stream_))
        , size_(other.size_)
        , path_(std::move(other.path_)) {
        other.size_ = 0;
    }
    
    StreamFileBase& operator=(StreamFileBase&& other) noexcept {
        if (this != &other) {
            close();
            stream_ = std::move(other.stream_);
            size_ = other.size_;
            path_ = std::move(other.path_);
            other.size_ = 0;
        }
        return *this;
    }
    
    [[nodiscard]] Result<void> open(std::string_view path) noexcept {
        close();
        
        path_ = path;
        
        // For write access, create file if it doesn't exist
        if constexpr (AccessPolicy::can_write) {
            stream_.open(path_, AccessPolicy::open_mode);
            
            // If file doesn't exist, create it
            if (!stream_) {
                stream_.clear();
                stream_.open(path_, std::ios::binary | std::ios::out | std::ios::trunc);
                if (!stream_) {
                    return Err(Error::Code::WriteError, "Failed to create file: " + std::string(path));
                }
                stream_.close();
                stream_.open(path_, AccessPolicy::open_mode);
            }
        } else {
            stream_.open(path_, AccessPolicy::open_mode);
        }
        
        if (!stream_) {
            return Err(Error::Code::FileNotFound, "Failed to open file: " + std::string(path));
        }
        
        // Get file size
        stream_.seekg(0, std::ios::end);
        size_ = static_cast<std::size_t>(stream_.tellg());
        stream_.seekg(0, std::ios::beg);
        
        return Ok();
    }
    
    void close() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stream_.is_open()) {
            if constexpr (AccessPolicy::can_write) {
                stream_.flush();
            }
            stream_.close();
            size_ = 0;
        }
    }
    
    /// Thread-safe read (only available if can_read is true)
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
        
        // Lock for seek+read operations
        std::lock_guard<std::mutex> lock(mutex_);
        
        stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream_) {
            return Err(Error::Code::ReadError, "Failed to seek to offset");
        }
        
        stream_.read(reinterpret_cast<char*>(buffer.get()), static_cast<std::streamsize>(bytes_to_read));
        if (!stream_ && !stream_.eof()) {
            return Err(Error::Code::ReadError, "Failed to read from file");
        }
        
        std::size_t actual_read = static_cast<std::size_t>(stream_.gcount());
        std::span<const std::byte> data_span(buffer.get(), actual_read);
        
        return Ok(stream_impl::OwnedBufferReadView(data_span, buffer));
    }

    [[nodiscard]] Result<void> read_into(void* dest_buffer, std::size_t offset, std::size_t size) const noexcept 
        requires (AccessPolicy::can_read) {
        if (!is_valid()) {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        if (offset >= size_) {
            return Err(Error::Code::OutOfBounds, "Read offset beyond file size");
        }
        
        std::size_t bytes_to_read = std::min(size, size_ - offset);
        
        // Lock for seek+read operations
        std::lock_guard<std::mutex> lock(mutex_);
        
        stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream_) {
            return Err(Error::Code::ReadError, "Failed to seek to offset");
        }
        
        stream_.read(reinterpret_cast<char*>(dest_buffer), static_cast<std::streamsize>(bytes_to_read));
        if (!stream_ && !stream_.eof()) {
            return Err(Error::Code::ReadError, "Failed to read from file");
        }
        
        return Ok();
    }
    
    /// Thread-safe write (only available if can_write is true)
    [[nodiscard]] Result<WriteViewType> write(std::size_t offset, std::size_t size) noexcept 
        requires (AccessPolicy::can_write) {
        if (!is_valid()) {
            return Err(Error::Code::WriteError, "File not open");
        }

        if (offset > size_) {
            return Err(Error::Code::OutOfBounds, "Write offset beyond file size");
        }
        
        // Allocate buffer
        auto buffer = std::shared_ptr<std::byte[]>(new std::byte[size]);
        std::span<std::byte> data_span(buffer.get(), size);
        
        return Ok(stream_impl::OwnedBufferWriteView(data_span, buffer, &stream_, &mutex_, offset));
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
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (new_size < size_) {
            // Truncate by reopening
            stream_.close();
            stream_.open(path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
            if (!stream_) {
                return Err(Error::Code::WriteError, "Failed to reopen file for truncation");
            }
        }
        
        // Seek to new size and write a byte to extend file if needed
        stream_.seekp(static_cast<std::streamoff>(new_size > 0 ? new_size - 1 : 0), std::ios::beg);
        if (!stream_) {
            return Err(Error::Code::WriteError, "Failed to seek to new size");
        }
        
        if (new_size > size_) {
            char zero = 0;
            stream_.write(&zero, 1);
            if (!stream_) {
                return Err(Error::Code::WriteError, "Failed to extend file");
            }
        }
        
        stream_.flush();
        size_ = new_size;
        
        return Ok();
    }
    
    /// Flush writes to disk (only available if can_write is true)
    [[nodiscard]] Result<void> flush() noexcept 
        requires (AccessPolicy::can_write) {
        if (!is_valid()) {
            return Err(Error::Code::WriteError, "File not open");
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        stream_.flush();
        
        if (!stream_) {
            return Err(Error::Code::WriteError, "Failed to flush file");
        }
        
        return Ok();
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return stream_.is_open();
    }
    
    [[nodiscard]] std::string_view path() const noexcept {
        return path_;
    }
};

/// Portable file reader using std::fstream - thread-safe with mutex, read-only
using StreamFileReader = StreamFileBase<stream_impl::detail::ReadOnlyAccess>;

static_assert(RawReader<StreamFileReader>, "StreamFileReader must satisfy RawReader concept");

/// Portable file writer using std::fstream - thread-safe with mutex, write-only
using StreamFileWriter = StreamFileBase<stream_impl::detail::WriteOnlyAccess>;

static_assert(RawWriter<StreamFileWriter>, "StreamFileWriter must satisfy RawWriter concept");

/// Portable file reader+writer using std::fstream - thread-safe with mutex, read-write
using StreamFileReadWriter = StreamFileBase<stream_impl::detail::ReadWriteAccess>;

static_assert(RawReader<StreamFileReadWriter>, "StreamFileReadWriter must satisfy RawReader concept");
static_assert(RawWriter<StreamFileReadWriter>, "StreamFileReadWriter must satisfy RawWriter concept");

} // namespace tiffconcept