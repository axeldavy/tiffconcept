#pragma once

#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../reader_base.hpp"

namespace tiffconcept {
namespace iocp_impl {

/// Read-only view that owns allocated buffer
/// Used for both sync and async read results
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
    
    // Move-only
    OwnedBufferReadView(OwnedBufferReadView&&) noexcept = default;
    OwnedBufferReadView& operator=(OwnedBufferReadView&&) noexcept = default;
    OwnedBufferReadView(const OwnedBufferReadView&) = delete;
    OwnedBufferReadView& operator=(const OwnedBufferReadView&) = delete;
};

static_assert(DataReadOnlyView<OwnedBufferReadView>, 
              "OwnedBufferReadView must satisfy DataReadOnlyView concept");

} // namespace iocp_impl

/// High-performance async file reader using Windows I/O Completion Ports (IOCP)
/// 
/// Design:
/// - Zero-copy async I/O with user-provided buffers
/// - Thread-safe: multiple threads can submit and poll concurrently
/// - IOCP-based: efficient multi-threaded completion notification
/// - Fallback: Provides sync read() methods for compatibility
/// 
/// Performance Characteristics:
/// - Best for: High-latency I/O (network shares, cloud) or massively parallel local I/O
/// - Concurrency: Unlimited pending operations (OS-managed queue)
/// - Latency: Near-optimal (single syscall for submission, efficient completion)
/// - Throughput: Saturates storage bandwidth with sufficient queue depth
/// 
/// Thread Safety:
/// - All methods are thread-safe and can be called concurrently
/// - IOCP handles distribution of completions to waiting threads
/// - Each completion delivered to exactly one thread
/// 
/// Requirements:
/// - Windows Vista or later (IOCP supported on all modern Windows)
/// - File must be opened with FILE_FLAG_OVERLAPPED
/// 
/// Usage Example:
/// @code
///   IOCPFileReader reader("file.tif");
///   
///   // Submit multiple reads
///   std::vector<Handle> handles;
///   std::vector<std::unique_ptr<std::byte[]>> buffers;
///   
///   for (auto& tile : tiles) {
///       auto buf = std::make_unique<std::byte[]>(tile.size);
///       auto h = reader.async_read_into(
///           std::span(buf.get(), tile.size),
///           tile.offset, tile.size
///       ).value();
///       handles.push_back(std::move(h));
///       buffers.push_back(std::move(buf));
///   }
///   
///   reader.submit_pending();  // No-op on Windows (immediate submission)
///   
///   // Process completions
///   while (reader.pending_operations() > 0) {
///       for (auto& [handle, result] : reader.wait_completions()) {
///           if (result) process(result.value().data());
///       }
///   }
/// @endcode
class IOCPFileReader {
public:
    using ReadViewType = iocp_impl::OwnedBufferReadView;
    
    static constexpr bool read_must_allocate = true;
    
    /// Configuration for IOCP setup
    struct Config {
        /// @brief Hint sequential access pattern
        /// May improve performance for sequential reads
        /// Most likely will have little positive effect
        /// for FastReader given its batched-ahead access pattern.
        bool use_sequential_scan = false;
        /// @brief Hint random access pattern
        /// Can improve significantly performance for bandwidth-bound IO,
        /// for instance if you only read a portion of the image on
        /// a network share (disables aggressive read-ahead).
        /// Negative performance impact will be small for FastReader,
        /// due to its batched-ahead access pattern.
        bool use_random_access = false;
    };

private:
    /// Context for a pending operation - just keeps OVERLAPPED alive until completion
    struct OperationContext {
        std::unique_ptr<OVERLAPPED> overlapped;  ///< OVERLAPPED structure (must persist)
    };

    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    HANDLE iocp_handle_{INVALID_HANDLE_VALUE};
    std::size_t size_{0};
    std::string path_;
    Config stored_config_;
    
    // Operation tracking
    mutable std::atomic<uint64_t> next_user_data_{1};
    mutable std::atomic<std::size_t> pending_ops_{0};
    
    // Operation context storage (mutable for const methods)
    mutable std::mutex context_mutex_;
    mutable std::unordered_map<uint64_t, OperationContext> operation_contexts_;

public:
    IOCPFileReader() noexcept = default;
    
    /// Open file with default configuration
    explicit IOCPFileReader(std::string_view path) noexcept {
        Config default_config;
        (void)open(path, default_config);
    }
    
    /// Open file with custom configuration
    IOCPFileReader(std::string_view path, const Config& config) noexcept {
        (void)open(path, config);
    }

    IOCPFileReader(const Config& config) noexcept {
        stored_config_ = config;
    }
    
    ~IOCPFileReader() noexcept {
        close();
    }
    
    // Non-copyable
    IOCPFileReader(const IOCPFileReader&) = delete;
    IOCPFileReader& operator=(const IOCPFileReader&) = delete;
    
    // Movable
    IOCPFileReader(IOCPFileReader&& other) noexcept
        : file_handle_(other.file_handle_)
        , iocp_handle_(other.iocp_handle_)
        , size_(other.size_)
        , path_(std::move(other.path_))
        , stored_config_(other.stored_config_)
        , next_user_data_(other.next_user_data_.load())
        , pending_ops_(other.pending_ops_.load())
        , operation_contexts_(std::move(other.operation_contexts_)) {
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.iocp_handle_ = INVALID_HANDLE_VALUE;
        other.size_ = 0;
        other.next_user_data_.store(1);
        other.pending_ops_.store(0);
    }
    
    IOCPFileReader& operator=(IOCPFileReader&& other) noexcept {
        if (this != &other) {
            close();
            file_handle_ = other.file_handle_;
            iocp_handle_ = other.iocp_handle_;
            size_ = other.size_;
            path_ = std::move(other.path_);
            stored_config_ = other.stored_config_;
            next_user_data_.store(other.next_user_data_.load());
            pending_ops_.store(other.pending_ops_.load());
            operation_contexts_ = std::move(other.operation_contexts_);
            other.file_handle_ = INVALID_HANDLE_VALUE;
            other.iocp_handle_ = INVALID_HANDLE_VALUE;
            other.size_ = 0;
            other.next_user_data_.store(1);
            other.pending_ops_.store(0);
            other.operation_contexts_.clear();
        }
        return *this;
    }

    /// Open file with default configuration
    [[nodiscard]] Result<void> open(std::string_view path) noexcept {
        return open(path, stored_config_);
    }
    
    /// Open file and initialize IOCP
    [[nodiscard]] Result<void> open(std::string_view path, const Config& config) noexcept {
        close();
        
        path_ = path;
        
        // Build flags
        stored_config_ = config;
        DWORD flags = FILE_FLAG_OVERLAPPED;  // Required for async I/O
        if (config.use_sequential_scan) {
            flags |= FILE_FLAG_SEQUENTIAL_SCAN;
        }
        if (config.use_random_access) {
            flags |= FILE_FLAG_RANDOM_ACCESS;
        }
        
        // Open file
        file_handle_ = CreateFileA(
            path_.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            flags,
            nullptr
        );
        
        if (file_handle_ == INVALID_HANDLE_VALUE) [[unlikely]] {
            DWORD error = GetLastError();
            return Err(Error::Code::FileNotFound, 
                      "Failed to open file: '" + std::string(path) + 
                      "': " + format_windows_error(error));
        }
        
        // Get file size
        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(file_handle_, &file_size)) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return Err(Error::Code::ReadError, 
                      "Failed to get file size:  " + format_windows_error(error));
        }
        size_ = static_cast<std::size_t>(file_size.QuadPart);
        
        // Create I/O Completion Port
        iocp_handle_ = CreateIoCompletionPort(
            file_handle_,
            nullptr,  // Create new IOCP
            0,        // Completion key (not used, we use OVERLAPPED pointers)
            0         // max concurrent threads (0 = default, # of processors)
        );
        
        if (iocp_handle_ == nullptr) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return Err(Error::Code::ReadError, 
                      "Failed to create I/O Completion Port: " + format_windows_error(error));
        }
        
        return Ok();
    }
    
    /// Close file and cleanup IOCP
    void close() noexcept {
        if (iocp_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(iocp_handle_);
            iocp_handle_ = INVALID_HANDLE_VALUE;
        }
        
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            size_ = 0;
        }
        
        pending_ops_.store(0, std::memory_order_release);
        next_user_data_.store(1, std::memory_order_release);
        
        // Clean up pending contexts
        std::lock_guard lock(context_mutex_);
        operation_contexts_.clear();
    }
    
    // ========================================================================
    // RawReader Interface (Synchronous Operations)
    // ========================================================================
    
    [[nodiscard]] Result<std::size_t> hint_batch_size() const noexcept {
        // Windows doesn't have a specific block size like Linux st_blksize
        // Use a reasonable default for batching consecutive reads
        return 65536;  // 64 KB default
    }

    /// Synchronous read with allocation (fallback for compatibility)
    [[nodiscard]] Result<ReadViewType> read(std::size_t offset, std::size_t size) const noexcept {
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        if (offset >= size_) [[unlikely]] {
            return Err(Error::Code::OutOfBounds, "Read offset beyond file size");
        }
        
        std::size_t bytes_to_read = std::min(size, size_ - offset);
        
        // Allocate buffer
        auto buffer = std::shared_ptr<std::byte[]>(new std::byte[bytes_to_read]);
        
        // Setup OVERLAPPED for positioned read
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
        overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        
        if (!overlapped.hEvent) [[unlikely]] {
            DWORD error = GetLastError();
            return Err(Error::Code::ReadError,
                       "Failed to create event for synchronous read "
                       + format_windows_error(error));
        }
        
        DWORD bytes_read = 0;
        BOOL result = ReadFile(
            file_handle_,
            buffer.get(),
            static_cast<DWORD>(bytes_to_read),
            &bytes_read,
            &overlapped
        );
        
        if (!result && GetLastError() != ERROR_IO_PENDING) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(overlapped.hEvent);
            return Err(Error::Code::ReadError, 
                      "ReadFile failed: " + format_windows_error(error));
        }
        
        // Wait for completion
        if (!GetOverlappedResult(file_handle_, &overlapped, &bytes_read, TRUE)) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(overlapped.hEvent);
            return Err(Error::Code::ReadError, 
                      "GetOverlappedResult failed: " + format_windows_error(error));
        }
        
        CloseHandle(overlapped.hEvent);
        
        if (bytes_read < static_cast<DWORD>(bytes_to_read)) [[unlikely]] {
            return Err(Error::Code::UnexpectedEndOfFile, "Read returned fewer bytes than requested");
        }
        
        std::span<const std::byte> data_span(buffer.get(), bytes_read);
        return Ok(ReadViewType(data_span, buffer));
    }
    
    /// Synchronous read into user buffer
    [[nodiscard]] Result<void> read_into(void* dest_buffer, std::size_t offset, std::size_t size) const noexcept {
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        if (offset >= size_) [[unlikely]] {
            return Err(Error::Code::OutOfBounds, "Read offset beyond file size");
        }
        
        std::size_t bytes_to_read = std::min(size, size_ - offset);
        
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
        overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        
        if (!overlapped.hEvent) [[unlikely]] {
            DWORD error = GetLastError();
            return Err(Error::Code::ReadError,
                       "Failed to create event for synchronous read: "
                       + format_windows_error(error));
        }
        
        DWORD bytes_read = 0;
        BOOL result = ReadFile(
            file_handle_,
            dest_buffer,
            static_cast<DWORD>(bytes_to_read),
            &bytes_read,
            &overlapped
        );
        
        if (!result && GetLastError() != ERROR_IO_PENDING) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(overlapped.hEvent);
            return Err(Error::Code::ReadError, 
                      "ReadFile failed: " + format_windows_error(error));
        }
        
        if (!GetOverlappedResult(file_handle_, &overlapped, &bytes_read, TRUE)) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(overlapped.hEvent);
            return Err(Error::Code::ReadError, 
                      "GetOverlappedResult failed: " + format_windows_error(error));
        }
        
        CloseHandle(overlapped.hEvent);
        
        if (bytes_read < static_cast<DWORD>(bytes_to_read)) [[unlikely]] {
            return Err(Error::Code::UnexpectedEndOfFile, "Read returned fewer bytes than requested");
        }
        
        return Ok();
    }
    
    [[nodiscard]] Result<std::size_t> size() const noexcept {
        if (!is_valid()) {
            return Err(Error::Code::ReadError, "File not open");
        }
        return Ok(size_);
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        return file_handle_ != INVALID_HANDLE_VALUE && iocp_handle_ != INVALID_HANDLE_VALUE;
    }
    
    [[nodiscard]] std::string_view path() const noexcept {
        return path_;
    }
    
    // ========================================================================
    // AsyncRawReader Interface (Asynchronous Operations)
    // ========================================================================

    /// Create independent clone for parallel access
    [[nodiscard]] IOCPFileReader clone() const noexcept {
        if (!is_valid()) [[unlikely]] {
            return IOCPFileReader();
        }
        // Re-open same file with same config (stored during open)
        IOCPFileReader cloned(path_);
        return cloned;
    }

    /// Get available async operation slots
    [[nodiscard]] std::size_t available_async_ops() const noexcept {
        // IOCP has no practical limit on pending operations
        // Return a large number to indicate effectively unlimited
        return std::numeric_limits<std::size_t>::max();
    }
    
    /// Submit async read into user-provided buffer
    /// 
    /// The buffer MUST remain valid until completion is retrieved.
    /// Buffer is NOT copied - Windows reads directly into it.
    /// 
    /// @param buffer User-provided buffer (must outlive operation)
    /// @param offset File offset to read from
    /// @param size Number of bytes to read
    /// @return Handle for tracking this operation
    [[nodiscard]] Result<uint64_t> async_read_into(
        std::span<std::byte> buffer, 
        std::size_t offset, 
        std::size_t size) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        if (buffer.size() < size) [[unlikely]] {
            return Err(Error::Code::InvalidOperation, 
                      "Buffer too small for requested read size");
        }
        
        if (offset >= size_) [[unlikely]] {
            return Err(Error::Code::OutOfBounds, "Read offset beyond file size");
        }
        
        std::size_t bytes_to_read = std::min(size, size_ - offset);
        
        // Allocate unique handle for this operation
        uint64_t user_data = next_user_data_.fetch_add(1, std::memory_order_relaxed);
        if (user_data == 0) [[unlikely]] {
            // Wrapped around (extremely rare) - skip zero
            user_data = next_user_data_.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Allocate OVERLAPPED structure (must persist until completion)
        auto overlapped = std::make_unique<OVERLAPPED>();
        std::memset(overlapped.get(), 0, sizeof(OVERLAPPED));
        overlapped->Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        overlapped->OffsetHigh = static_cast<DWORD>(offset >> 32);

        OVERLAPPED* overlapped_ptr = overlapped.get();
        
        // Store operation context
        {
            std::lock_guard lock(context_mutex_);
            operation_contexts_[user_data] = OperationContext{
                std::move(overlapped)
            };
        }
        
        // Track pending operation
        pending_ops_.fetch_add(1, std::memory_order_release);
        
        // Submit read operation
        DWORD bytes_read = 0;
        BOOL result = ReadFile(
            file_handle_,
            buffer.data(),
            static_cast<DWORD>(bytes_to_read),
            &bytes_read,
            overlapped_ptr
        );
        
        DWORD error = GetLastError();
        
        // Check for errors (pending is OK)
        if (!result && error != ERROR_IO_PENDING) [[unlikely]] {
            // Operation failed - clean up
            pending_ops_.fetch_sub(1, std::memory_order_release);
            std::lock_guard lock(context_mutex_);
            operation_contexts_.erase(user_data);
            return Err(Error::Code::ReadError, 
                    "ReadFile failed: " + format_windows_error(error));
        }
        
        return Ok(uint64_t{user_data});
    }
    
    /// Poll for completed operations (non-blocking)
    /// @param completions Output vector for completions (appended, not cleared)
    /// @param max_completions Maximum completions to retrieve (0 = all available)
    /// @return Number of completions added to the vector
    Result<size_t> poll_completions(
        std::vector<std::pair<uint64_t, Result<std::size_t>>>& completions,
        std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        // Poll with zero timeout
        std::size_t init_count = completions.size();
        while (max_completions == 0 || (completions.size() - init_count) < max_completions) {
            DWORD bytes_transferred = 0;
            ULONG_PTR completion_key = 0;
            OVERLAPPED* overlapped = nullptr;
            
            BOOL result = GetQueuedCompletionStatus(
                iocp_handle_,
                &bytes_transferred,
                &completion_key,
                &overlapped,
                0  // Zero timeout = non-blocking
            );

            // Since we use a zero timeout, overlapped == nullptr means no completions available
            // in which case result is FALSE.
            if (overlapped == nullptr) {
                // No completions available
                break;
            }

            process_completion(completions, overlapped, result, bytes_transferred);
        }
        
        return completions.size() - init_count;
    }
    
    /// Wait for at least one completion (blocking)
    /// @param completions Output vector for completions (appended, not cleared)
    /// @param max_completions Maximum completions to retrieve (0 = all available)
    /// @return Number of completions added to the vector
    Result<size_t> wait_completions(
        std::vector<std::pair<uint64_t, Result<std::size_t>>>& completions,
        std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        // Wait for first completion (blocking)
        DWORD bytes_transferred = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED* overlapped = nullptr;
        
        BOOL result = GetQueuedCompletionStatus(
            iocp_handle_,
            &bytes_transferred,
            &completion_key,
            &overlapped,
            INFINITE
        );

        if (!result && overlapped == nullptr) {
            // This indicates a failure in waiting for completions
            DWORD error = GetLastError();
            return Err(Error::Code::ReadError, 
                      "GetQueuedCompletionStatus failed: " + format_windows_error(error));
        }
        
        std::size_t init_count = completions.size();
        if (overlapped != nullptr) {
            process_completion(completions, overlapped, result, bytes_transferred);
            
            // Also poll for any other completions that arrived
            while (max_completions == 0 || (completions.size() - init_count) < max_completions) {
                result = GetQueuedCompletionStatus(
                    iocp_handle_,
                    &bytes_transferred,
                    &completion_key,
                    &overlapped,
                    0  // Non-blocking
                );
                
                if (overlapped == nullptr) {
                    break;
                }

                process_completion(completions, overlapped, result, bytes_transferred);
            }
        }
        
        return completions.size() - init_count;
    }
    
    /// Wait for completions with timeout
    /// @param completions Output vector for completions (appended, not cleared)
    /// @param timeout Maximum time to wait
    /// @param max_completions Maximum completions to retrieve (0 = all available)
    /// @return Number of completions added to the vector
    Result<size_t> wait_completions_for(
        std::vector<std::pair<uint64_t, Result<std::size_t>>>& completions,
        std::chrono::milliseconds timeout, 
        std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        DWORD timeout_ms = static_cast<DWORD>(timeout.count());
        
        DWORD bytes_transferred = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED* overlapped = nullptr;
        
        BOOL result = GetQueuedCompletionStatus(
            iocp_handle_,
            &bytes_transferred,
            &completion_key,
            &overlapped,
            timeout_ms
        );

        if (!result && overlapped == nullptr) {
            // Timeout or failure
            DWORD error = GetLastError();
            if (error == WAIT_TIMEOUT) {
                // Timeout - no completions
                return 0;
            } else {
                return Err(Error::Code::ReadError, 
                          "GetQueuedCompletionStatus failed: " + format_windows_error(error));
            }
        }
        
        std::size_t init_count = completions.size();
        if (overlapped != nullptr) {
            process_completion(completions, overlapped, result, bytes_transferred);
            
            // Poll for any other completions
            while (max_completions == 0 || (completions.size() - init_count) < max_completions) {
                result = GetQueuedCompletionStatus(
                    iocp_handle_,
                    &bytes_transferred,
                    &completion_key,
                    &overlapped,
                    0
                );
                
                if (overlapped == nullptr) {
                    break;
                }

                process_completion(completions, overlapped, result, bytes_transferred);
            }
        }
        
        return completions.size() - init_count;
    }
    
    /// Get number of pending operations
    [[nodiscard]] std::size_t pending_operations() const noexcept {
        return pending_ops_.load(std::memory_order_acquire);
    }
    
    /// Force submission of queued operations (no-op on Windows)
    /// 
    /// Windows IOCP submits operations immediately, so this is a no-op.
    /// Provided for API compatibility with io_uring-based readers.
    [[nodiscard]] Result<void> flush_async_operations() const noexcept {
        // No-op on Windows - operations are submitted immediately
        return Ok();
    }

private:
    static std::string format_windows_error(DWORD error_code) noexcept {
        char* message_buffer = nullptr;
        DWORD size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error_code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&message_buffer),
            0,
            nullptr
        );
        
        if (size == 0 || message_buffer == nullptr) {
            // FormatMessage failed, return numeric code
            return "Error " + std::to_string(error_code);
        }
        
        // Copy message and remove trailing newlines
        std::string result(message_buffer, size);
        LocalFree(message_buffer);
        
        // Trim trailing whitespace/newlines
        while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' ')) {
            result.pop_back();
        }
        
        // Append error code for reference
        result += " (Error " + std::to_string(error_code) + ")";
        
        return result;
    }
    
    /// Find user data from OVERLAPPED pointer
    [[nodiscard]] uint64_t find_user_data(OVERLAPPED* overlapped) const noexcept {
        std::lock_guard lock(context_mutex_);
        for (const auto& [user_data, ctx] : operation_contexts_) {
            if (ctx.overlapped.get() == overlapped) {
                return user_data;
            }
        }
        return 0;  // Not found (happens for sync operations)
    }

    /// remove a pending operation from the current list
    void remove_pending_operation(uint64_t user_data) const noexcept {
        std::lock_guard lock(context_mutex_);
        operation_contexts_.erase(user_data);
        // Decrement pending counter
        assert (user_data != 0);
        pending_ops_.fetch_sub(1, std::memory_order_release);
    }
    
    /// Process a completed operation
    void process_completion(std::vector<std::pair<uint64_t, Result<std::size_t>>>& completions,
                            OVERLAPPED* overlapped,
                            BOOL success,
                            DWORD bytes_transferred) const noexcept {
        
        // Find user data from OVERLAPPED
        uint64_t user_data = find_user_data(overlapped);

        if (user_data == 0) {
            // Sync operation or failure not associated with a known async operation
            return;
        }

        // Remove from pending operations
        remove_pending_operation(user_data);

        if (success) {
            completions.push_back({uint64_t{user_data}, Ok(static_cast<std::size_t>(bytes_transferred))});
        } else {
            DWORD error = GetLastError();
            completions.emplace_back(user_data, 
                Err(Error::Code::ReadError, 
                    "IOCP read failed: " + format_windows_error(error)));
        }
    }
};

static_assert(RawReader<IOCPFileReader>, 
              "IOCPFileReader must satisfy RawReader concept");
static_assert(AsyncRawReader<IOCPFileReader>, 
              "IOCPFileReader must satisfy AsyncRawReader concept");

} // namespace tiffconcept

#endif // _WIN32 || _WIN64
