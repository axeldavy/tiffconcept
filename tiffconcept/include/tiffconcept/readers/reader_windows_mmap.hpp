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
#include "../reader_base.hpp"

namespace tiffconcept {
namespace windows_mmap_impl {

namespace detail {
    /// Custom deleter for memory-mapped view
    struct MmapViewDeleter {
        void operator()(void* ptr) const noexcept {
            if (ptr) {
                UnmapViewOfFile(ptr);
            }
        }
    };

    /// RAII wrapper for Windows file mapping handle
    class FileMappingHandle {
    private:
        HANDLE handle_{INVALID_HANDLE_VALUE};
        
    public:
        FileMappingHandle() noexcept = default;
        
        explicit FileMappingHandle(HANDLE handle) noexcept : handle_(handle) {}
        
        ~FileMappingHandle() noexcept {
            close();
        }
        
        FileMappingHandle(const FileMappingHandle&) = delete;
        FileMappingHandle& operator=(const FileMappingHandle&) = delete;
        
        FileMappingHandle(FileMappingHandle&& other) noexcept : handle_(other.handle_) {
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        
        FileMappingHandle& operator=(FileMappingHandle&& other) noexcept {
            if (this != &other) {
                close();
                handle_ = other.handle_;
                other.handle_ = INVALID_HANDLE_VALUE;
            }
            return *this;
        }
        
        void close() noexcept {
            if (handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
                handle_ = INVALID_HANDLE_VALUE;
            }
        }
        
        [[nodiscard]] HANDLE get() const noexcept { return handle_; }
        [[nodiscard]] bool is_valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }
        
        HANDLE release() noexcept {
            HANDLE h = handle_;
            handle_ = INVALID_HANDLE_VALUE;
            return h;
        }
    };

    // Access mode policies
    struct ReadOnlyAccess {
        static constexpr DWORD desired_access = GENERIC_READ;
        static constexpr DWORD share_mode = FILE_SHARE_READ;
        static constexpr DWORD page_protection = PAGE_READONLY;
        static constexpr DWORD map_access = FILE_MAP_READ;
        static constexpr bool can_read = true;
        static constexpr bool can_write = false;
    };

    struct ReadWriteAccess {
        static constexpr DWORD desired_access = GENERIC_READ | GENERIC_WRITE;
        static constexpr DWORD share_mode = FILE_SHARE_READ;
        static constexpr DWORD page_protection = PAGE_READWRITE;
        static constexpr DWORD map_access = FILE_MAP_ALL_ACCESS;
        static constexpr bool can_read = true;
        static constexpr bool can_write = true;
    };
} // namespace detail

/// Read-only view for memory-mapped regions (zero-copy, shared ownership)
class WindowsMmapReadView {
private:
    std::span<const std::byte> data_;
    std::shared_ptr<void> mmap_region_;

public:
    WindowsMmapReadView() noexcept = default;
    
    WindowsMmapReadView(std::span<const std::byte> data, std::shared_ptr<void> region) noexcept
        : data_(data), mmap_region_(std::move(region)) {}
    
    [[nodiscard]] std::span<const std::byte> data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    WindowsMmapReadView(WindowsMmapReadView&&) noexcept = default;
    WindowsMmapReadView& operator=(WindowsMmapReadView&&) noexcept = default;
    WindowsMmapReadView(const WindowsMmapReadView&) = delete;
    WindowsMmapReadView& operator=(const WindowsMmapReadView&) = delete;
};

static_assert(DataReadOnlyView<WindowsMmapReadView>, "WindowsMmapReadView must satisfy DataReadOnlyView concept");

/// Write-only view for memory-mapped regions - writes on flush
/// Supports readback when AccessPolicy::can_read is true
template <typename AccessPolicy>
class WindowsMmapWriteView {
private:
    std::span<std::byte> data_;
    std::shared_ptr<void> mmap_region_;

public:
    // Readback support depends on whether the mapping has read permissions
    static constexpr bool supports_inplace_readback = AccessPolicy::can_read;

    WindowsMmapWriteView() noexcept = default;
    
    WindowsMmapWriteView(std::span<std::byte> data, std::shared_ptr<void> region) noexcept
        : data_(data), mmap_region_(std::move(region)) {}
    
    [[nodiscard]] std::span<std::byte> data() noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    [[nodiscard]] Result<void> flush() noexcept {
        if (!data_.empty() && mmap_region_) {
            if (!FlushViewOfFile(data_.data(), data_.size())) {
                return Err(Error::Code::WriteError, "Failed to flush mapped view");
            }
        }
        return Ok();
    }
    
    WindowsMmapWriteView(WindowsMmapWriteView&&) noexcept = default;
    WindowsMmapWriteView& operator=(WindowsMmapWriteView&&) noexcept = default;
    WindowsMmapWriteView(const WindowsMmapWriteView&) = delete;
    WindowsMmapWriteView& operator=(const WindowsMmapWriteView&) = delete;
};

static_assert(DataWriteOnlyView<WindowsMmapWriteView<detail::ReadWriteAccess>>, "WindowsMmapWriteView must satisfy DataWriteOnlyView concept");
static_assert(DataWriteViewWithReadback<WindowsMmapWriteView<detail::ReadWriteAccess>>, "WindowsMmapWriteView must support readback");

} // namespace windows_mmap_impl

/// Base template for Windows memory-mapped file access
/// AccessPolicy determines read/write capabilities
template <typename AccessPolicy>
class WindowsMmapFileBase {
protected:
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    windows_mmap_impl::detail::FileMappingHandle mapping_handle_;
    std::shared_ptr<void> base_mapping_;
    std::size_t size_{0};
    std::string path_;

public:
    using ReadViewType = windows_mmap_impl::WindowsMmapReadView;
    using WriteViewType = windows_mmap_impl::WindowsMmapWriteView<AccessPolicy>;

    static constexpr bool read_must_allocate = false;
    
    WindowsMmapFileBase() noexcept = default;
    
    explicit WindowsMmapFileBase(std::string_view path, bool create_if_missing = false) noexcept {
        (void)open(path, create_if_missing);
    }
    
    ~WindowsMmapFileBase() noexcept {
        close();
    }
    
    WindowsMmapFileBase(const WindowsMmapFileBase&) = delete;
    WindowsMmapFileBase& operator=(const WindowsMmapFileBase&) = delete;
    
    WindowsMmapFileBase(WindowsMmapFileBase&& other) noexcept
        : file_handle_(other.file_handle_)
        , mapping_handle_(std::move(other.mapping_handle_))
        , base_mapping_(std::move(other.base_mapping_))
        , size_(other.size_)
        , path_(std::move(other.path_)) {
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.size_ = 0;
    }
    
    WindowsMmapFileBase& operator=(WindowsMmapFileBase&& other) noexcept {
        if (this != &other) {
            close();
            file_handle_ = other.file_handle_;
            mapping_handle_ = std::move(other.mapping_handle_);
            base_mapping_ = std::move(other.base_mapping_);
            size_ = other.size_;
            path_ = std::move(other.path_);
            other.file_handle_ = INVALID_HANDLE_VALUE;
            other.size_ = 0;
        }
        return *this;
    }
    
    [[nodiscard]] Result<void> open(std::string_view path, bool create_if_missing = false) noexcept {
        close();
        
        path_ = path;
        
        // Convert to wide string for Windows API
        int wide_size = MultiByteToWideChar(CP_UTF8, 0, path_.c_str(), -1, nullptr, 0);
        if (wide_size == 0) {
            return Err(Error::Code::FileNotFound, "Failed to convert path: " + std::string(path));
        }
        
        std::wstring wide_path(wide_size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path_.c_str(), -1, &wide_path[0], wide_size);
        
        // Determine creation disposition
        DWORD creation = OPEN_EXISTING;
        if (create_if_missing && AccessPolicy::can_write) {
            creation = OPEN_ALWAYS;
        }
        
        // Open file
        file_handle_ = CreateFileW(
            wide_path.c_str(),
            AccessPolicy::desired_access,
            AccessPolicy::share_mode,
            nullptr,
            creation,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        
        if (file_handle_ == INVALID_HANDLE_VALUE) {
            return Err(Error::Code::FileNotFound, "Failed to open file: " + std::string(path));
        }
        
        // Get file size
        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(file_handle_, &file_size)) {
            close();
            return Err(Error::Code::ReadError, "Failed to get file size: " + std::string(path));
        }
        
        size_ = static_cast<std::size_t>(file_size.QuadPart);
        
        // Handle empty files
        if (size_ == 0) {
            return Ok();
        }
        
        // Create file mapping object
        HANDLE mapping = CreateFileMappingW(
            file_handle_,
            nullptr,
            AccessPolicy::page_protection,
            0, 0,  // Use entire file size
            nullptr
        );
        
        if (mapping == nullptr || mapping == INVALID_HANDLE_VALUE) {
            close();
            return Err(Error::Code::ReadError, "Failed to create file mapping: " + std::string(path));
        }
        
        mapping_handle_ = windows_mmap_impl::detail::FileMappingHandle(mapping);
        
        // Map the entire file into memory
        void* mapped_ptr = MapViewOfFile(
            mapping_handle_.get(),
            AccessPolicy::map_access,
            0, 0,  // Offset
            0      // Map entire file
        );
        
        if (mapped_ptr == nullptr) {
            close();
            return Err(Error::Code::ReadError, "Failed to map view of file: " + std::string(path));
        }
        
        // Store base mapping with custom deleter
        base_mapping_ = std::shared_ptr<void>(mapped_ptr, windows_mmap_impl::detail::MmapViewDeleter{});
        
        return Ok();
    }
    
    void close() noexcept {
        if (base_mapping_) {
            // Flush changes before unmapping if writable
            if constexpr (AccessPolicy::can_write) {
                FlushViewOfFile(base_mapping_.get(), size_);
            }
            base_mapping_.reset();
        }
        
        mapping_handle_.close();
        
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            if constexpr (AccessPolicy::can_write) {
                FlushFileBuffers(file_handle_);
            }
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
        }
        
        size_ = 0;
    }
    
    /// Zero-copy read (only available if can_read is true)
    [[nodiscard]] Result<ReadViewType> read(std::size_t offset, std::size_t size) const noexcept 
        requires (AccessPolicy::can_read) {
        if (!is_valid()) {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        if (size_ == 0) {
            return Ok(ReadViewType{});
        }
        
        if (offset >= size_) {
            return Err(Error::Code::OutOfBounds, "Read offset beyond file size");
        }
        
        std::size_t bytes_to_read = std::min(size, size_ - offset);
        
        const std::byte* base = static_cast<const std::byte*>(base_mapping_.get());
        std::span<const std::byte> data_span(base + offset, bytes_to_read);
        
        return Ok(windows_mmap_impl::WindowsMmapReadView(data_span, base_mapping_));
    }

    [[nodiscard]] Result<void> read_into(void* buffer, std::size_t offset, std::size_t size) const noexcept
        requires (AccessPolicy::can_read) {
        if (!is_valid()) {
            return Err(Error::Code::ReadError, "File not open");
        }

        if (size_ == 0) {
            return Ok();
        }

        if (offset >= size_) {
            return Err(Error::Code::OutOfBounds, "Read offset beyond file size");
        }

        std::size_t bytes_to_read = std::min(size, size_ - offset);

        const std::byte* base = static_cast<const std::byte*>(base_mapping_.get());
        std::memcpy(buffer, base + offset, bytes_to_read);

        return Ok();
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
        
        std::byte* base = static_cast<std::byte*>(base_mapping_.get());
        std::span<std::byte> data_span(base + offset, bytes_to_write);
        
        return Ok(windows_mmap_impl::WindowsMmapWriteView<AccessPolicy>(data_span, base_mapping_));
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
        
        // Unmap current mapping
        base_mapping_.reset();
        mapping_handle_.close();
        
        // Resize file
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(new_size);
        if (!SetFilePointerEx(file_handle_, li, nullptr, FILE_BEGIN)) {
            return Err(Error::Code::WriteError, "Failed to set file pointer for resize");
        }
        
        if (!SetEndOfFile(file_handle_)) {
            return Err(Error::Code::WriteError, "Failed to resize file");
        }
        
        size_ = new_size;
        
        // Remap if size > 0
        if (size_ > 0) {
            HANDLE mapping = CreateFileMappingW(
                file_handle_,
                nullptr,
                AccessPolicy::page_protection,
                0, 0,
                nullptr
            );
            
            if (mapping == nullptr || mapping == INVALID_HANDLE_VALUE) {
                return Err(Error::Code::WriteError, "Failed to remap after resize");
            }
            
            mapping_handle_ = windows_mmap_impl::detail::FileMappingHandle(mapping);
            
            void* mapped_ptr = MapViewOfFile(
                mapping_handle_.get(),
                AccessPolicy::map_access,
                0, 0,
                0
            );
            
            if (mapped_ptr == nullptr) {
                return Err(Error::Code::WriteError, "Failed to map view after resize");
            }
            
            base_mapping_ = std::shared_ptr<void>(mapped_ptr, windows_mmap_impl::detail::MmapViewDeleter{});
        }
        
        return Ok();
    }
    
    /// Flush writes to disk (only available if can_write is true)
    [[nodiscard]] Result<void> flush() noexcept 
        requires (AccessPolicy::can_write) {
        if (base_mapping_) {
            if (!FlushViewOfFile(base_mapping_.get(), size_)) {
                return Err(Error::Code::WriteError, "Failed to flush mapped view");
            }
        }
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            if (!FlushFileBuffers(file_handle_)) {
                return Err(Error::Code::WriteError, "Failed to flush file buffers");
            }
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

/// Windows memory-mapped file reader - zero-copy, thread-safe, read-only
using WindowsMmapFileReader = WindowsMmapFileBase<windows_mmap_impl::detail::ReadOnlyAccess>;

static_assert(RawReader<WindowsMmapFileReader>, "WindowsMmapFileReader must satisfy RawReader concept");

// Write-only mmap (write-only file) is not supported due to Windows mmap limitations

/// Windows memory-mapped file reader+writer - zero-copy, thread-safe, read-write
using WindowsMmapFileReadWriter = WindowsMmapFileBase<windows_mmap_impl::detail::ReadWriteAccess>;

static_assert(RawReader<WindowsMmapFileReadWriter>, "WindowsMmapFileReadWriter must satisfy RawReader concept");
static_assert(RawWriter<WindowsMmapFileReadWriter>, "WindowsMmapFileReadWriter must satisfy RawWriter concept");

} // namespace tiffconcept