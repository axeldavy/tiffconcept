#pragma once

#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
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
    
    /// Non-copyable handle for async operations (satisfies AsyncRawReader concept)
    struct AsyncOperationHandle {
        uint64_t id;
        
        AsyncOperationHandle() noexcept : id(0) {}
        explicit AsyncOperationHandle(uint64_t id_) noexcept : id(id_) {}
        
        // Non-copyable
        AsyncOperationHandle(const AsyncOperationHandle&) = delete;
        AsyncOperationHandle& operator=(const AsyncOperationHandle&) = delete;
        
        // Movable
        AsyncOperationHandle(AsyncOperationHandle&& other) noexcept : id(other.id) {
            other.id = 0;
        }
        AsyncOperationHandle& operator=(AsyncOperationHandle&& other) noexcept {
            if (this != &other) {
                id = other.id;
                other.id = 0;
            }
            return *this;
        }
        
        bool operator==(const AsyncOperationHandle& other) const noexcept {
            return id == other.id;
        }
    };
    
    using AsyncReadResult = Result<ReadViewType>;
    
    static constexpr bool read_must_allocate = true;
    
    /// Configuration for IOCP setup
    struct Config {
        DWORD max_concurrent_threads = 0;  ///< Max threads for IOCP (0 = # of processors)
        bool use_unbuffered = false;       ///< Use FILE_FLAG_NO_BUFFERING (requires aligned I/O)
        bool use_write_through = false;    ///< Use FILE_FLAG_WRITE_THROUGH (bypass cache)
        bool use_sequential_scan = false;  ///< Hint sequential access pattern
        bool use_random_access = false;    ///< Hint random access pattern
    };
    
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
        , next_user_data_(other.next_user_data_.load())
        , pending_ops_(other.pending_ops_.load()) {
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
            next_user_data_.store(other.next_user_data_.load());
            pending_ops_.store(other.pending_ops_.load());
            other.file_handle_ = INVALID_HANDLE_VALUE;
            other.iocp_handle_ = INVALID_HANDLE_VALUE;
            other.size_ = 0;
            other.next_user_data_.store(1);
            other.pending_ops_.store(0);
        }
        return *this;
    }

    /// Open file with default configuration
    [[nodiscard]] Result<void> open(std::string_view path) noexcept {
        return open(path, Config{});
    }
    
    /// Open file and initialize IOCP
    [[nodiscard]] Result<void> open(std::string_view path, const Config& config) noexcept {
        close();
        
        path_ = path;
        
        // Build flags
        DWORD flags = FILE_FLAG_OVERLAPPED;  // Required for async I/O
        if (config.use_unbuffered) {
            flags |= FILE_FLAG_NO_BUFFERING;
        }
        if (config.use_write_through) {
            flags |= FILE_FLAG_WRITE_THROUGH;
        }
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
                      "Failed to open file: " + std::string(path) + 
                      " (Error " + std::to_string(error) + ")");
        }
        
        // Get file size
        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(file_handle_, &file_size)) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return Err(Error::Code::ReadError, 
                      "Failed to get file size (Error " + std::to_string(error) + ")");
        }
        size_ = static_cast<std::size_t>(file_size.QuadPart);
        
        // Create I/O Completion Port
        iocp_handle_ = CreateIoCompletionPort(
            file_handle_,
            nullptr,  // Create new IOCP
            0,        // Completion key (not used, we use OVERLAPPED user data)
            config.max_concurrent_threads
        );
        
        if (iocp_handle_ == nullptr) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return Err(Error::Code::ReadError, 
                      "Failed to create IOCP (Error " + std::to_string(error) + ")");
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
            return Err(Error::Code::ReadError, "Failed to create event for synchronous read");
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
                      "ReadFile failed (Error " + std::to_string(error) + ")");
        }
        
        // Wait for completion
        if (!GetOverlappedResult(file_handle_, &overlapped, &bytes_read, TRUE)) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(overlapped.hEvent);
            return Err(Error::Code::ReadError, 
                      "GetOverlappedResult failed (Error " + std::to_string(error) + ")");
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
            return Err(Error::Code::ReadError, "Failed to create event for synchronous read");
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
                      "ReadFile failed (Error " + std::to_string(error) + ")");
        }
        
        if (!GetOverlappedResult(file_handle_, &overlapped, &bytes_read, TRUE)) [[unlikely]] {
            DWORD error = GetLastError();
            CloseHandle(overlapped.hEvent);
            return Err(Error::Code::ReadError, 
                      "GetOverlappedResult failed (Error " + std::to_string(error) + ")");
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
    
    /// Submit async read into user-provided buffer
    /// 
    /// The buffer MUST remain valid until completion is retrieved.
    /// Buffer is NOT copied - Windows reads directly into it.
    /// 
    /// @param buffer User-provided buffer (must outlive operation)
    /// @param offset File offset to read from
    /// @param size Number of bytes to read
    /// @return Handle for tracking this operation
    [[nodiscard]] Result<AsyncOperationHandle> async_read_into(
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
        
        // Store operation context
        {
            std::lock_guard lock(context_mutex_);
            operation_contexts_[user_data] = OperationContext{
                std::move(overlapped),
                buffer.data(),
                bytes_to_read
            };
        }
        
        // Submit read operation
        OVERLAPPED* overlapped_ptr = nullptr;
        {
            std::lock_guard lock(context_mutex_);
            overlapped_ptr = operation_contexts_[user_data].overlapped.get();
        }
        
        DWORD bytes_read = 0;
        BOOL result = ReadFile(
            file_handle_,
            buffer.data(),
            static_cast<DWORD>(bytes_to_read),
            &bytes_read,
            overlapped_ptr
        );
        
        DWORD error = GetLastError();
        
        // Check for immediate completion or pending
        if (!result && error != ERROR_IO_PENDING) [[unlikely]] {
            // Operation failed
            std::lock_guard lock(context_mutex_);
            operation_contexts_.erase(user_data);
            return Err(Error::Code::ReadError, 
                      "ReadFile failed (Error " + std::to_string(error) + ")");
        }
        
        // Track pending operation
        pending_ops_.fetch_add(1, std::memory_order_release);
        
        return Ok(AsyncOperationHandle{user_data});
    }
    
    /// Poll for completed operations (non-blocking)
    [[nodiscard]] std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> 
    poll_completions(std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return {};
        }
        
        std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> results;
        
        // Poll with zero timeout
        std::size_t count = 0;
        while (max_completions == 0 || count < max_completions) {
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
            
            if (overlapped == nullptr) {
                // No completions available
                break;
            }
            
            results.push_back(process_completion(overlapped, result, bytes_transferred));
            count++;
        }
        
        return results;
    }
    
    /// Wait for at least one completion (blocking)
    [[nodiscard]] std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> 
    wait_completions(std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return {};
        }
        
        std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> results;
        
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
        
        if (overlapped != nullptr) {
            results.push_back(process_completion(overlapped, result, bytes_transferred));
            
            // Also poll for any other completions that arrived
            std::size_t count = 1;
            while (max_completions == 0 || count < max_completions) {
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
                
                results.push_back(process_completion(overlapped, result, bytes_transferred));
                count++;
            }
        }
        
        return results;
    }
    
    /// Wait for completions with timeout
    [[nodiscard]] std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> 
    wait_completions_for(std::chrono::milliseconds timeout, 
                        std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return {};
        }
        
        std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> results;
        
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
        
        if (overlapped != nullptr) {
            results.push_back(process_completion(overlapped, result, bytes_transferred));
            
            // Poll for any other completions
            std::size_t count = 1;
            while (max_completions == 0 || count < max_completions) {
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
                
                results.push_back(process_completion(overlapped, result, bytes_transferred));
                count++;
            }
        }
        
        return results;
    }
    
    /// Get number of pending operations
    [[nodiscard]] std::size_t pending_operations() const noexcept {
        return pending_ops_.load(std::memory_order_acquire);
    }
    
    /// Force submission of queued operations (no-op on Windows)
    /// 
    /// Windows IOCP submits operations immediately, so this is a no-op.
    /// Provided for API compatibility with io_uring-based readers.
    [[nodiscard]] Result<std::size_t> submit_pending() const noexcept {
        // No-op on Windows - operations are submitted immediately
        return Ok(static_cast<std::size_t>(0));
    }

private:
    /// Context for a pending operation
    struct OperationContext {
        std::unique_ptr<OVERLAPPED> overlapped;  ///< OVERLAPPED structure (must persist)
        void* buffer;                            ///< User-provided buffer
        std::size_t size;                        ///< Expected read size
    };
    
    /// Find user data from OVERLAPPED pointer
    [[nodiscard]] uint64_t find_user_data(OVERLAPPED* overlapped) const noexcept {
        std::lock_guard lock(context_mutex_);
        for (const auto& [user_data, ctx] : operation_contexts_) {
            if (ctx.overlapped.get() == overlapped) {
                return user_data;
            }
        }
        return 0;  // Not found (should never happen)
    }
    
    /// Process a completed operation
    [[nodiscard]] std::pair<AsyncOperationHandle, AsyncReadResult> 
    process_completion(OVERLAPPED* overlapped, BOOL success, DWORD bytes_transferred) const noexcept {
        
        // Find user data from OVERLAPPED
        uint64_t user_data = find_user_data(overlapped);
        
        // Decrement pending counter
        pending_ops_.fetch_sub(1, std::memory_order_release);
        
        // Retrieve operation context
        OperationContext ctx{};
        {
            std::lock_guard lock(context_mutex_);
            auto it = operation_contexts_.find(user_data);
            if (it != operation_contexts_.end()) {
                ctx = std::move(it->second);
                operation_contexts_.erase(it);
            }
        }
        
        // Check for errors
        if (!success) [[unlikely]] {
            DWORD error = GetLastError();
            return {AsyncOperationHandle{user_data}, Err(Error::Code::ReadError, 
                   "IOCP read failed (Error " + std::to_string(error) + ")")};
        }
        
        // Check for short read
        if (bytes_transferred < static_cast<DWORD>(ctx.size)) [[unlikely]] {
            return {AsyncOperationHandle{user_data}, Err(Error::Code::UnexpectedEndOfFile, 
                   "IOCP read returned fewer bytes than requested")};
        }
        
        // Success - wrap buffer in ReadView
        // Note: We don't own the buffer, so we can't use shared_ptr here
        // The caller must ensure buffer remains valid
        std::span<const std::byte> data_span(
            static_cast<const std::byte*>(ctx.buffer), 
            bytes_transferred
        );
        
        // Return view without ownership (buffer owned by caller)
        return {AsyncOperationHandle{user_data}, Ok(ReadViewType(data_span, nullptr))};
    }
    
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    HANDLE iocp_handle_{INVALID_HANDLE_VALUE};
    std::size_t size_{0};
    std::string path_;
    
    // Operation tracking
    mutable std::atomic<uint64_t> next_user_data_{1};
    mutable std::atomic<std::size_t> pending_ops_{0};
    
    // Operation context storage (mutable for const methods)
    mutable std::mutex context_mutex_;
    mutable std::unordered_map<uint64_t, OperationContext> operation_contexts_;
};

static_assert(RawReader<IOCPFileReader>, 
              "IOCPFileReader must satisfy RawReader concept");
static_assert(AsyncRawReader<IOCPFileReader>, 
              "IOCPFileReader must satisfy AsyncRawReader concept");

} // namespace tiffconcept

#endif // _WIN32 || _WIN64
