#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include "reader_base.hpp"

namespace tiff {
namespace windows_impl {

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
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    std::size_t offset_{0};
    bool flushed_{false};

public:
    OwnedBufferWriteView() noexcept = default;
    
    OwnedBufferWriteView(std::span<std::byte> data, std::shared_ptr<std::byte[]> buffer, 
                         HANDLE handle, std::size_t offset) noexcept
        : data_(data), buffer_(std::move(buffer)), file_handle_(handle), offset_(offset) {}
    
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
        if (flushed_ || file_handle_ == INVALID_HANDLE_VALUE || data_.empty()) {
            return Ok();
        }
        
        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(offset_ & 0xFFFFFFFF);
        overlapped.OffsetHigh = static_cast<DWORD>(offset_ >> 32);
        
        DWORD bytes_written = 0;
        if (!WriteFile(file_handle_, data_.data(), static_cast<DWORD>(data_.size()), 
                      &bytes_written, &overlapped)) {
            DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                flushed_ = true;
                return Err(Error::Code::WriteError, "WriteFile failed");
            }
            
            if (!GetOverlappedResult(file_handle_, &overlapped, &bytes_written, TRUE)) {
                flushed_ = true;
                return Err(Error::Code::WriteError, "GetOverlappedResult failed");
            }
        }
        
        flushed_ = true;
        
        if (bytes_written != data_.size()) {
            return Err(Error::Code::WriteError, "Incomplete write");
        }
        
        return Ok();
    }
    
    OwnedBufferWriteView(OwnedBufferWriteView&& other) noexcept
        : data_(other.data_)
        , buffer_(std::move(other.buffer_))
        , file_handle_(other.file_handle_)
        , offset_(other.offset_)
        , flushed_(other.flushed_) {
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.flushed_ = true;
    }
    
    OwnedBufferWriteView& operator=(OwnedBufferWriteView&& other) noexcept {
        if (this != &other) {
            (void)flush();
            data_ = other.data_;
            buffer_ = std::move(other.buffer_);
            file_handle_ = other.file_handle_;
            offset_ = other.offset_;
            flushed_ = other.flushed_;
            other.file_handle_ = INVALID_HANDLE_VALUE;
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
    // Access mode policies for Windows ReadFile/WriteFile
    struct ReadOnlyAccess {
        static constexpr DWORD desired_access = GENERIC_READ;
        static constexpr DWORD share_mode = FILE_SHARE_READ;
        static constexpr DWORD creation_disposition = OPEN_EXISTING;
        static constexpr DWORD flags = FILE_ATTRIBUTE_NORMAL;
        static constexpr bool can_read = true;
        static constexpr bool can_write = false;
    };
    
    struct WriteOnlyAccess {
        static constexpr DWORD desired_access = GENERIC_WRITE;
        static constexpr DWORD share_mode = FILE_SHARE_READ;
        static constexpr DWORD creation_disposition = OPEN_ALWAYS;
        static constexpr DWORD flags = FILE_FLAG_OVERLAPPED;
        static constexpr bool can_read = false;
        static constexpr bool can_write = true;
    };
    
    struct ReadWriteAccess {
        static constexpr DWORD desired_access = GENERIC_READ | GENERIC_WRITE;
        static constexpr DWORD share_mode = FILE_SHARE_READ;
        static constexpr DWORD creation_disposition = OPEN_ALWAYS;
        static constexpr DWORD flags = FILE_FLAG_OVERLAPPED;
        static constexpr bool can_read = true;
        static constexpr bool can_write = true;
    };
} // namespace detail

} // namespace windows_impl

/// Base template for Windows ReadFile/WriteFile access
/// AccessPolicy determines read/write capabilities
template <typename AccessPolicy>
class WindowsFileBase {
protected:
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    std::size_t size_{0};
    std::string path_;

public:
    using ReadViewType = windows_impl::OwnedBufferReadView;
    using WriteViewType = windows_impl::OwnedBufferWriteView;
    
    WindowsFileBase() noexcept = default;
    
    explicit WindowsFileBase(std::string_view path) noexcept {
        (void)open(path);
    }
    
    ~WindowsFileBase() noexcept {
        close();
    }
    
    WindowsFileBase(const WindowsFileBase&) = delete;
    WindowsFileBase& operator=(const WindowsFileBase&) = delete;
    
    WindowsFileBase(WindowsFileBase&& other) noexcept
        : file_handle_(other.file_handle_)
        , size_(other.size_)
        , path_(std::move(other.path_)) {
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.size_ = 0;
    }
    
    WindowsFileBase& operator=(WindowsFileBase&& other) noexcept {
        if (this != &other) {
            close();
            file_handle_ = other.file_handle_;
            size_ = other.size_;
            path_ = std::move(other.path_);
            other.file_handle_ = INVALID_HANDLE_VALUE;
            other.size_ = 0;
        }
        return *this;
    }
    
    [[nodiscard]] Result<void> open(std::string_view path) noexcept {
        close();
        
        path_ = path;
        
        // Convert to wide string for Windows API
        int wide_size = MultiByteToWideChar(CP_UTF8, 0, path_.c_str(), -1, nullptr, 0);
        if (wide_size == 0) {
            return Err(Error::Code::FileNotFound, "Failed to convert path: " + std::string(path));
        }
        
        std::wstring wide_path(wide_size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path_.c_str(), -1, &wide_path[0], wide_size);
        
        file_handle_ = CreateFileW(
            wide_path.c_str(),
            AccessPolicy::desired_access,
            AccessPolicy::share_mode,
            nullptr,
            AccessPolicy::creation_disposition,
            AccessPolicy::flags,
            nullptr
        );
        
        if (file_handle_ == INVALID_HANDLE_VALUE) {
            return Err(Error::Code::FileNotFound, "Failed to open file: " + std::string(path));
        }
        
        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(file_handle_, &file_size)) {
            close();
            return Err(Error::Code::ReadError, "Failed to get file size: " + std::string(path));
        }
        
        size_ = static_cast<std::size_t>(file_size.QuadPart);
        return Ok();
    }
    
    void close() noexcept {
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            if constexpr (AccessPolicy::can_write) {
                FlushFileBuffers(file_handle_);
            }
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            size_ = 0;
        }
    }
    
    /// Thread-safe read using ReadFile with OVERLAPPED (only available if can_read is true)
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
        
        // Setup OVERLAPPED structure for positioned read (thread-safe)
        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
        
        DWORD bytes_read = 0;
        if (!ReadFile(file_handle_, buffer.get(), static_cast<DWORD>(bytes_to_read), &bytes_read, &overlapped)) {
            DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                return Err(Error::Code::ReadError, "ReadFile failed with error: " + std::to_string(error));
            }
            
            // Wait for async operation to complete
            if (!GetOverlappedResult(file_handle_, &overlapped, &bytes_read, TRUE)) {
                return Err(Error::Code::ReadError, "GetOverlappedResult failed");
            }
        }
        
        std::span<const std::byte> data_span(buffer.get(), static_cast<std::size_t>(bytes_read));
        
        return Ok(windows_impl::OwnedBufferReadView(data_span, buffer));
    }
    
    /// Thread-safe write using WriteFile with OVERLAPPED (only available if can_write is true)
    [[nodiscard]] Result<WriteViewType> write(std::size_t offset, std::size_t size) noexcept 
        requires (AccessPolicy::can_write) {
        if (!is_valid()) {
            return Err(Error::Code::WriteError, "File not open");
        }
        
        // Allocate buffer for user to write into
        auto buffer = std::shared_ptr<std::byte[]>(new std::byte[size]);
        std::span<std::byte> data_span(buffer.get(), size);
        
        return Ok(windows_impl::OwnedBufferWriteView(data_span, buffer, file_handle_, offset));
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
        
        LARGE_INTEGER li;
        li.QuadPart = new_size;
        
        if (!SetFilePointerEx(file_handle_, li, nullptr, FILE_BEGIN)) {
            return Err(Error::Code::WriteError, "SetFilePointerEx failed");
        }
        
        if (!SetEndOfFile(file_handle_)) {
            return Err(Error::Code::WriteError, "SetEndOfFile failed");
        }
        
        size_ = new_size;
        return Ok();
    }
    
    /// Flush writes to disk (only available if can_write is true)
    [[nodiscard]] Result<void> flush() noexcept 
        requires (AccessPolicy::can_write) {
        if (!is_valid()) {
            return Err(Error::Code::WriteError, "File not open");
        }
        
        if (!FlushFileBuffers(file_handle_)) {
            return Err(Error::Code::WriteError, "FlushFileBuffers failed");
        }
        
        return Ok();
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        return file_handle_ != INVALID_HANDLE_VALUE;
    }
    
    [[nodiscard]] std::string_view path() const noexcept {
        return path_;
    }
};

/// Windows file reader using ReadFile with OVERLAPPED - thread-safe without locks, read-only
using WindowsFileReader = WindowsFileBase<windows_impl::detail::ReadOnlyAccess>;

static_assert(RawReader<WindowsFileReader>, "WindowsFileReader must satisfy RawReader concept");

/// Windows file writer using WriteFile with OVERLAPPED - thread-safe without locks, write-only
using WindowsFileWriter = WindowsFileBase<windows_impl::detail::WriteOnlyAccess>;

static_assert(RawWriter<WindowsFileWriter>, "WindowsFileWriter must satisfy RawWriter concept");

/// Windows file reader+writer using ReadFile/WriteFile with OVERLAPPED - thread-safe without locks, read-write
using WindowsFileReadWriter = WindowsFileBase<windows_impl::detail::ReadWriteAccess>;

static_assert(RawReader<WindowsFileReadWriter>, "WindowsFileReadWriter must satisfy RawReader concept");
static_assert(RawWriter<WindowsFileReadWriter>, "WindowsFileReadWriter must satisfy RawWriter concept");

} // namespace tiff