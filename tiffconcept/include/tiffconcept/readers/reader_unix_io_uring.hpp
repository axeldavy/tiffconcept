#pragma once

#if defined(__linux__)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <liburing.h>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include "../reader_base.hpp"

namespace tiffconcept {
namespace io_uring_impl {

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

} // namespace io_uring_impl

/// High-performance async file reader using Linux io_uring
/// 
/// Design:
/// - Zero-copy async I/O with user-provided buffers
/// - Thread-safe: multiple threads can submit and poll concurrently
/// - Lock-free fast path: submission and completion reaping use minimal locking
/// - Fallback: Provides sync read() methods for compatibility
/// 
/// Performance Characteristics:
/// - Best for: High-latency I/O (NAS, cloud) or massively parallel local I/O
/// - Queue depth: Configurable (default 128), higher for network storage
/// - Latency: Near-optimal (single syscall for batch submission)
/// - Throughput: Saturates storage bandwidth with sufficient queue depth
/// 
/// Thread Safety:
/// - All methods are thread-safe and can be called concurrently
/// - Submissions and completions use internal locking (minimal contention)
/// - Each completion delivered to exactly one thread
/// 
/// Requirements:
/// - Linux kernel 5.1+ (io_uring support)
/// - liburing (development package)
/// - Compile with: -luring
/// 
/// Usage Example:
/// @code
///   IoUringFileReader reader("file.tif");
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
///   reader.submit_pending();  // Flush to kernel
///   
///   // Process completions
///   while (reader.pending_operations() > 0) {
///       for (auto& [handle, result] : reader.wait_completions()) {
///           if (result) process(result.value().data());
///       }
///   }
/// @endcode
class IoUringFileReader {
public:
    using ReadViewType = io_uring_impl::OwnedBufferReadView;
    
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
    
    /// Configuration for io_uring setup
    struct Config {
        uint32_t queue_depth = 128;      ///< Max concurrent operations (SQ/CQ size)
        bool use_sqpoll = false;         ///< Use kernel polling thread (requires CAP_SYS_NICE)
        bool use_iopoll = false;         ///< Use polling I/O (requires O_DIRECT, fast devices only)
        int32_t sq_thread_cpu = -1;      ///< CPU for SQPOLL thread (-1 = no affinity)
        uint32_t sq_thread_idle = 2000;  ///< SQPOLL idle timeout in ms
    };
    
    IoUringFileReader() noexcept = default;
    
    /// Open file with default configuration
    explicit IoUringFileReader(std::string_view path) noexcept {
        Config default_config;
        auto result = open(path, default_config);
        if (!result.is_ok()) {
            std::cerr << "IoUringFileReader: Failed to open file: " 
                      << result.error().message << "\n";
        }
    }
    
    /// Open file with custom configuration
    IoUringFileReader(std::string_view path, const Config& config) noexcept {
        (void)open(path, config);
    }
    
    ~IoUringFileReader() noexcept {
        close();
    }
    
    // Non-copyable
    IoUringFileReader(const IoUringFileReader&) = delete;
    IoUringFileReader& operator=(const IoUringFileReader&) = delete;
    
    // Movable
    IoUringFileReader(IoUringFileReader&& other) noexcept
        : fd_(other.fd_)
        , size_(other.size_)
        , path_(std::move(other.path_))
        , ring_(std::move(other.ring_))
        , next_user_data_(other.next_user_data_.load())
        , pending_ops_(other.pending_ops_.load()) {
        other.fd_ = -1;
        other.size_ = 0;
        other.next_user_data_.store(1);
        other.pending_ops_.store(0);
    }
    
    IoUringFileReader& operator=(IoUringFileReader&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            size_ = other.size_;
            path_ = std::move(other.path_);
            ring_ = std::move(other.ring_);
            next_user_data_.store(other.next_user_data_.load());
            pending_ops_.store(other.pending_ops_.load());
            other.fd_ = -1;
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
    
    /// Open file and initialize io_uring
    [[nodiscard]] Result<void> open(std::string_view path, const Config& config) noexcept {
        close();
        
        path_ = path;
        
        // Open file (O_DIRECT required for IOPOLL)
        int flags = O_RDONLY;
        if (config.use_iopoll) {
            flags |= O_DIRECT;
        }
        
        fd_ = ::open(path_.c_str(), flags);
        if (fd_ < 0) [[unlikely]] {
            return Err(Error::Code::FileNotFound, 
                      "Failed to open file: " + std::string(path) + 
                      " (" + std::string(std::strerror(errno)) + ")");
        }
        
        // Get file size
        struct stat st;
        if (fstat(fd_, &st) != 0) [[unlikely]] {
            int err = errno;
            ::close(fd_);
            fd_ = -1;
            return Err(Error::Code::ReadError, 
                      "Failed to get file size: " + std::string(std::strerror(err)));
        }
        size_ = static_cast<std::size_t>(st.st_size);
        
        // Initialize io_uring
        ring_ = std::make_unique<io_uring>();
        
        io_uring_params params{};
        if (config.use_sqpoll) {
            params.flags |= IORING_SETUP_SQPOLL;
            if (config.sq_thread_cpu >= 0) {
                params.flags |= IORING_SETUP_SQ_AFF;
                params.sq_thread_cpu = config.sq_thread_cpu;
            }
            params.sq_thread_idle = config.sq_thread_idle;
        }
        if (config.use_iopoll) {
            params.flags |= IORING_SETUP_IOPOLL;
        }
        
        // Try with requested queue depth, fall back to smaller sizes if it fails
        uint32_t queue_depth = config.queue_depth;
        int ret = -ENOMEM;
        
        while (queue_depth >= 16 && ret < 0) {
            ret = io_uring_queue_init_params(queue_depth, ring_.get(), &params);
            
            if (ret == -ENOMEM && queue_depth > 16) {
                // Resource limit hit - try with half the queue depth
                queue_depth /= 2;
                std::cerr << "IoUringFileReader: Queue depth " << (queue_depth * 2) 
                         << " failed, retrying with " << queue_depth << "\n";
            } else {
                break;
            }
        }
        
        if (ret < 0) [[unlikely]] {
            int err = -ret;
            ::close(fd_);
            fd_ = -1;
            ring_.reset();
            return Err(Error::Code::ReadError, 
                      "Failed to initialize io_uring: " + std::string(std::strerror(err)) +
                      " (tried queue_depth down to " + std::to_string(queue_depth) + "). " +
                      "Try increasing RLIMIT_MEMLOCK with: ulimit -l unlimited");
        }
        
        return Ok();
    }
    
    /// Close file and cleanup io_uring
    void close() noexcept {
        if (ring_) {
            // Cancel all pending operations
            // Note: This is automatic when io_uring is destroyed, but we make it explicit
            io_uring_queue_exit(ring_.get());
            ring_.reset();
        }
        
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
            size_ = 0;
        }
        
        pending_ops_.store(0, std::memory_order_release);
        next_user_data_.store(1, std::memory_order_release);
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
        
        // Use pread for thread-safe positioned read
        ssize_t bytes_read = ::pread(fd_, buffer.get(), bytes_to_read, static_cast<off_t>(offset));
        
        if (bytes_read < 0) [[unlikely]] {
            return Err(Error::Code::ReadError, 
                      "pread failed: " + std::string(std::strerror(errno)));
        }
        
        if (bytes_read < static_cast<ssize_t>(bytes_to_read)) [[unlikely]] {
            return Err(Error::Code::UnexpectedEndOfFile, "pread returned fewer bytes than requested");
        }
        
        std::span<const std::byte> data_span(buffer.get(), static_cast<std::size_t>(bytes_read));
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
        
        ssize_t bytes_read = ::pread(fd_, dest_buffer, bytes_to_read, static_cast<off_t>(offset));
        
        if (bytes_read < 0) [[unlikely]] {
            return Err(Error::Code::ReadError, 
                      "pread failed: " + std::string(std::strerror(errno)));
        }
        
        if (bytes_read < static_cast<ssize_t>(bytes_to_read)) [[unlikely]] {
            return Err(Error::Code::UnexpectedEndOfFile, "pread returned fewer bytes than requested");
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
        return fd_ >= 0 && ring_ != nullptr;
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
    /// Buffer is NOT copied - io_uring reads directly into it.
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
            // Wrapped around (extremely rare) - skip zero as it's often special
            user_data = next_user_data_.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Get submission queue entry
        std::lock_guard lock(ring_mutex_);
        
        io_uring_sqe* sqe = io_uring_get_sqe(ring_.get());
        if (!sqe) [[unlikely]] {
            // Queue full - try submitting pending operations
            int submitted = io_uring_submit(ring_.get());
            if (submitted < 0) {
                return Err(Error::Code::ReadError, 
                          "io_uring submission failed: " + std::string(std::strerror(-submitted)));
            }
            
            // Try again after submission
            sqe = io_uring_get_sqe(ring_.get());
            if (!sqe) [[unlikely]] {
                return Err(Error::Code::ReadError, "io_uring queue exhausted");
            }
        }
        
        // Setup read operation
        io_uring_prep_read(sqe, fd_, buffer.data(), bytes_to_read, static_cast<off_t>(offset));
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(user_data));
        
        // Track pending operation
        pending_ops_.fetch_add(1, std::memory_order_release);
        
        // Store operation context for completion
        {
            std::lock_guard ctx_lock(context_mutex_);
            operation_contexts_[user_data] = OperationContext{
                buffer.data(),
                bytes_to_read
            };
        }
        
        return Ok(AsyncOperationHandle{user_data});
    }
    
    /// Poll for completed operations (non-blocking)
    [[nodiscard]] std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> 
    poll_completions(std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return {};
        }
        
        std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> results;
        
        std::lock_guard lock(ring_mutex_);
        
        io_uring_cqe* cqe;
        unsigned head;
        unsigned count = 0;
        
        // Peek at all available completions without blocking
        io_uring_for_each_cqe(ring_.get(), head, cqe) {
            if (max_completions > 0 && count >= max_completions) {
                break;
            }
            
            results.push_back(process_completion(cqe));
            count++;
        }
        
        // Mark processed completions as seen
        if (count > 0) {
            io_uring_cq_advance(ring_.get(), count);
        }
        
        return results;
    }
    
    /// Wait for at least one completion (blocking)
    [[nodiscard]] std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> 
    wait_completions(std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return {};
        }
        
        // Quick check: are completions already available?
        {
            std::lock_guard lock(ring_mutex_);
            io_uring_cqe* cqe = nullptr;
            int ret = io_uring_peek_cqe(ring_.get(), &cqe);
            if (ret == 0 && cqe != nullptr) {
                // Completions available, use poll_completions
                return poll_completions(max_completions);
            }
        }
        
        // No completions available - wait for at least one
        std::vector<std::pair<AsyncOperationHandle, AsyncReadResult>> results;
        
        std::lock_guard lock(ring_mutex_);
        
        io_uring_cqe* cqe = nullptr;
        int ret = io_uring_wait_cqe(ring_.get(), &cqe);
        
        if (ret < 0) [[unlikely]] {
            // Wait failed - return empty (could be interrupted by signal)
            return results;
        }
        
        if (cqe) {
            results.push_back(process_completion(cqe));
            io_uring_cqe_seen(ring_.get(), cqe);
            
            // Also collect any other completions that arrived
            unsigned head;
            unsigned count = 0;
            io_uring_for_each_cqe(ring_.get(), head, cqe) {
                if (max_completions > 0 && count + 1 >= max_completions) {
                    break;
                }
                results.push_back(process_completion(cqe));
                count++;
            }
            
            if (count > 0) {
                io_uring_cq_advance(ring_.get(), count);
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
        
        std::lock_guard lock(ring_mutex_);
        
        // Setup timeout
        __kernel_timespec ts{};
        ts.tv_sec = timeout.count() / 1000;
        ts.tv_nsec = (timeout.count() % 1000) * 1000000;
        
        io_uring_cqe* cqe = nullptr;
        int ret = io_uring_wait_cqe_timeout(ring_.get(), &cqe, &ts);
        
        if (ret == -ETIME) {
            // Timeout expired, no completions
            return results;
        }
        
        if (ret < 0) [[unlikely]] {
            // Other error
            return results;
        }
        
        if (cqe) {
            results.push_back(process_completion(cqe));
            io_uring_cqe_seen(ring_.get(), cqe);
            
            // Collect any other completions
            unsigned head;
            unsigned count = 0;
            io_uring_for_each_cqe(ring_.get(), head, cqe) {
                if (max_completions > 0 && count + 1 >= max_completions) {
                    break;
                }
                results.push_back(process_completion(cqe));
                count++;
            }
            
            if (count > 0) {
                io_uring_cq_advance(ring_.get(), count);
            }
        }
        
        return results;
    }
    
    /// Get number of pending operations
    [[nodiscard]] std::size_t pending_operations() const noexcept {
        return pending_ops_.load(std::memory_order_acquire);
    }
    
    /// Force submission of queued operations to kernel
    /// 
    /// io_uring batches submissions internally. This forces a flush.
    /// Call after submitting a batch of operations for optimal performance.
    [[nodiscard]] Result<std::size_t> submit_pending() const noexcept {
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        std::lock_guard lock(ring_mutex_);
        
        int submitted = io_uring_submit(ring_.get());
        if (submitted < 0) [[unlikely]] {
            return Err(Error::Code::ReadError, 
                      "io_uring_submit failed: " + std::string(std::strerror(-submitted)));
        }
        
        return Ok(static_cast<std::size_t>(submitted));
    }

private:
    /// Context for a pending operation
    struct OperationContext {
        void* buffer;       ///< User-provided buffer
        std::size_t size;   ///< Expected read size
    };
    
    /// Process a single completion queue entry
    [[nodiscard]] std::pair<AsyncOperationHandle, AsyncReadResult> 
    process_completion(io_uring_cqe* cqe) const noexcept {
        
        uint64_t user_data = reinterpret_cast<uint64_t>(io_uring_cqe_get_data(cqe));
        int result = cqe->res;
        
        // Decrement pending counter
        pending_ops_.fetch_sub(1, std::memory_order_release);
        
        // Retrieve operation context
        OperationContext ctx{};
        {
            std::lock_guard lock(context_mutex_);
            auto it = operation_contexts_.find(user_data);
            if (it != operation_contexts_.end()) {
                ctx = it->second;
                operation_contexts_.erase(it);
            }
        }
        
        // Check for errors
        if (result < 0) [[unlikely]] {
            return {AsyncOperationHandle{user_data}, Err(Error::Code::ReadError, 
                   "io_uring read failed: " + std::string(std::strerror(-result)))};
        }
        
        // Check for short read
        if (static_cast<std::size_t>(result) < ctx.size) [[unlikely]] {
            return {AsyncOperationHandle{user_data}, Err(Error::Code::UnexpectedEndOfFile, 
                   "io_uring read returned fewer bytes than requested")};
        }
        
        // Success - wrap buffer in ReadView
        // Note: We don't own the buffer, so we can't use shared_ptr here
        // The caller must ensure buffer remains valid
        std::span<const std::byte> data_span(
            static_cast<const std::byte*>(ctx.buffer), 
            static_cast<std::size_t>(result)
        );
        
        // Return view without ownership (buffer owned by caller)
        return {AsyncOperationHandle{user_data}, Ok(ReadViewType(data_span, nullptr))};
    }
    
    int fd_{-1};
    std::size_t size_{0};
    std::string path_;
    
    // io_uring state (mutable for const methods)
    mutable std::unique_ptr<io_uring> ring_;
    mutable std::mutex ring_mutex_;  // Protects ring access
    
    // Operation tracking
    mutable std::atomic<uint64_t> next_user_data_{1};
    mutable std::atomic<std::size_t> pending_ops_{0};
    
    // Operation context storage
    mutable std::mutex context_mutex_;
    mutable std::unordered_map<uint64_t, OperationContext> operation_contexts_;
};

static_assert(RawReader<IoUringFileReader>, 
              "IoUringFileReader must satisfy RawReader concept");
static_assert(AsyncRawReader<IoUringFileReader>, 
              "IoUringFileReader must satisfy AsyncRawReader concept");

} // namespace tiffconcept

#endif // __linux__
