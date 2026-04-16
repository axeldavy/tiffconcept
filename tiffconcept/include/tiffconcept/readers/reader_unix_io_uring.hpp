#pragma once

#if defined(__linux__)

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <chrono>
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
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>
#include "../reader_base.hpp"

namespace tiffconcept {
namespace io_uring_impl {

/// Read-only view that owns allocated buffer
/// Used for read() results
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
/// - Thread-safe: Calls are mutex protected. The instance should be
///   used only by one thread at a time (else wait_* would cause poll to block ).
//    This is a liburing limitation, and a more low level implementation
///   could allow wait and poll concurrently.
/// - Due to the above, use clone() if you need to read from several threads
/// - RawReader interface: Provides sync read() methods for compatibility
/// 
/// Performance Characteristics:
/// - Best for: High-latency I/O (NAS, cloud) or massively parallel local I/O
/// - Queue depth: Configurable (default 1024)
/// - Requests can complete out of order
/// - For network storage, it is preferrable the caller does batch consecutive
///   read calls.
/// 
/// Requirements:
/// - Linux kernel 5.1+ (io_uring support)
/// - liburing (development package)
/// - Compile with: -luring

class IoUringFileReader {
public:
    using ReadViewType = io_uring_impl::OwnedBufferReadView;
    
    static constexpr bool read_must_allocate = true;
    
    /// Configuration for io_uring setup
    struct Config {
        // We don't need a large submission queue since we submit batched read requests
        uint32_t sq_queue_depth = 16;     ///< Submission queue depth
        // Since requests can take time (network/storage latency), we want a larger completion queue
        uint32_t cq_queue_depth = 1024;     ///< Completion queue depth
    };


private:
    int fd_{-1};            /// opened file descriptor
    std::size_t size_{0};   /// size of the file (assumed to not change after open)
    std::size_t blksize_{4096}; /// hinted block size for optimal reads
    std::string path_;      /// path to the file
    Config stored_config_;  /// stored config for clone()

    struct IoUringDeleter {
        void operator()(io_uring* ring) const noexcept {
            if (ring) {
                io_uring_queue_exit(ring);
                delete ring;
            }
        }
    };

    // io_uring state (mutable for const methods)
    mutable std::unique_ptr<io_uring, IoUringDeleter> ring_;
    mutable uint64_t next_user_data_{1}; // unique ID for each operation
    mutable std::size_t pending_ops_{0}; // operations submitted to the instance (not necessarily the kernel) minus treated ones
    std::size_t real_cq_depth_{0}; // actual CQ depth allocated
    mutable std::mutex liburing_mutex_;  // Protects above variables


public:
    IoUringFileReader() noexcept = default;
    
    /// Open file with default configuration
    explicit IoUringFileReader(std::string_view path) noexcept {
        Config default_config;
        auto result = open(path, default_config);
        if (!result.is_ok()) {
            //std::cerr << "IoUringFileReader: Failed to open file: " 
            //          << result.error().message << "\n";
        }
    }
    
    /// Open file with custom configuration
    IoUringFileReader(std::string_view path, const Config& config) noexcept {
        stored_config_ = config;
        (void)open(path, config);
    }

    IoUringFileReader(const Config& config) noexcept {
        stored_config_ = config;
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
        , blksize_(other.blksize_)
        , path_(std::move(other.path_))
        , stored_config_(other.stored_config_)
        , ring_(std::move(other.ring_))
        , next_user_data_(other.next_user_data_)
        , pending_ops_(other.pending_ops_)
        , real_cq_depth_(other.real_cq_depth_) {
        other.fd_ = -1;
        other.size_ = 0;
        other.blksize_ = 4096;
        other.stored_config_ = Config{};
        other.next_user_data_ = 1;
        other.pending_ops_ = 0;
        other.real_cq_depth_ = 0;
    }
    
    IoUringFileReader& operator=(IoUringFileReader&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            size_ = other.size_;
            blksize_ = other.blksize_;
            path_ = std::move(other.path_);
            stored_config_ = other.stored_config_;
            ring_ = std::move(other.ring_);
            next_user_data_ = other.next_user_data_;
            pending_ops_ = other.pending_ops_;
            real_cq_depth_ = other.real_cq_depth_;
            other.fd_ = -1;
            other.size_ = 0;
            other.blksize_ = 4096;
            other.stored_config_ = Config{};
            other.next_user_data_ = 1;
            other.pending_ops_ = 0;
            other.real_cq_depth_ = 0;
        }
        return *this;
    }

    /// Open file with default configuration
    [[nodiscard]] Result<void> open(std::string_view path) noexcept {
        return open(path, stored_config_);
    }

    /// Allocate the async io_uring resources
    [[nodiscard]] Result<void> initialize_io_uring(const Config& config) noexcept {
        std::lock_guard lock(liburing_mutex_);

        if (ring_) {
            // Already initialized
            return Ok();
        }
        
        // Initialize io_uring
        ring_ = std::unique_ptr<io_uring, IoUringDeleter>(new io_uring{});

        // Flags built hierarchically so a single binary supports kernels from 5.1+.
        // Candidates are ordered most → least capable; EINVAL (flag not known by
        // this kernel) advances to the next entry.  ENOMEM halves the queue depths
        // before retrying (see below).
        //
        // IORING_SETUP_SUBMIT_ALL is intentionally absent: for file reads there are
        // no prep-time errors, so it adds per-submit overhead for nothing.
        constexpr uint32_t F_5_5  = 0u
#ifdef IORING_SETUP_CQSIZE
            | IORING_SETUP_CQSIZE          // 5.5+: honour params.cq_entries for CQ size
#endif
            ;
        constexpr uint32_t F_5_6  = F_5_5
#ifdef IORING_SETUP_CLAMP
            | IORING_SETUP_CLAMP           // 5.6+: silently clamp to kernel maximums
#endif
            ;
        constexpr uint32_t F_5_19 = F_5_6
#ifdef IORING_SETUP_COOP_TASKRUN
            | IORING_SETUP_COOP_TASKRUN    // 5.19+: avoid signal interruption on completion
#endif
#ifdef IORING_SETUP_TASKRUN_FLAG
            | IORING_SETUP_TASKRUN_FLAG    // 5.19+: expose task-run flag in SQ ring head
#endif
            ;
        // 6.0+: NO_SQARRAY skips the SQ index-array mmap (saves one mmap region);
        // SINGLE_ISSUER tells the kernel only one CPU submits SQEs, enabling
        // internal locking and memory-layout optimisations that reduce per-ring
        // footprint — both directly mitigate ENOMEM in tight alloc/dealloc loops.
        constexpr uint32_t F_6_0  = F_5_19
#ifdef IORING_SETUP_NO_SQARRAY
            | IORING_SETUP_NO_SQARRAY      // 6.0+: skip SQ index-array mmap
#endif
#ifdef IORING_SETUP_SINGLE_ISSUER
            | IORING_SETUP_SINGLE_ISSUER   // 6.0+: single-CPU submission path
#endif
            ;

        // Unique candidates most → least capable.  Duplicates (present when the
        // build liburing headers predate certain flags) are skipped at runtime.
        const uint32_t flag_candidates[] = { F_6_0, F_5_19, F_5_6, F_5_5, 0u };

        constexpr uint32_t min_depth = 8u;
        const uint32_t target_sq = std::max(config.sq_queue_depth, min_depth);
        const uint32_t target_cq = std::max({config.cq_queue_depth, target_sq, min_depth});

        // The CQ holds only *completed* (kernel-finished, not-yet-drained) entries.
        // available_async_ops() enforces pending_ops_ <= real_cq_depth_, so at most
        // real_cq_depth_ completions can exist simultaneously — the CQ never overflows.

        int ret = -EINVAL;
        bool done = false;
        uint32_t prev_flags = ~0u;
        for (uint32_t flags : flag_candidates) {
            if (done || flags == prev_flags) continue;
            prev_flags = flags;

            uint32_t sq = target_sq;
            uint32_t cq = target_cq;

            while (!done) {
                // Always zero-initialise the struct before each attempt.
                // io_uring_queue_init_params cleans up internally on failure
                // (unmaps rings, closes the ring fd).  Reusing the struct without
                // zeroing it first is undefined behaviour (double-unmap/double-close).
                *ring_ = io_uring{};
                io_uring_params params{};
                params.flags      = flags;
                params.cq_entries = cq;   // respected when IORING_SETUP_CQSIZE is set
                params.sq_entries = sq;   // informational; actual value filled by kernel
                params.wq_fd      = -1;

                ret = io_uring_queue_init_params(sq, ring_.get(), &params);
                if (ret == 0) {
                    real_cq_depth_ = params.cq_entries;
                    done = true;
                } else if (ret == -EINVAL) {
                    break;        // unsupported flags → try next flag level
                } else if (ret != -ENOMEM) {
                    done = true;  // EPERM or similar — stop entirely
                } else {
                    // ENOMEM: the kernel defers ring cleanup via RCU, so destroyed
                    // rings temporarily hold memory.  Halving the queue sizes makes
                    // forward progress without sleeping or changing system tunables.
                    if (sq == min_depth) break;  // already at minimum → try next level
                    cq = std::max(cq / 2u, min_depth);
                    sq = std::max(sq / 2u, min_depth);
                }
            }
        }

        if (ret < 0) [[unlikely]] {
            ring_.reset();
            return Err(Error::Code::ReadError,
                      "Failed to initialize io_uring: " + std::string(std::strerror(-ret)));
        }
        return Ok();
    }
    
    /// Open file and initialize io_uring
    [[nodiscard]] Result<void> open(std::string_view path, const Config& config) noexcept {
        close();
        auto io_uring_init = initialize_io_uring(config);
        if (io_uring_init.is_error()) {
            return io_uring_init.error();
        }
        
        path_ = path;
        stored_config_ = config;

        fd_ = ::open(path_.c_str(), O_RDONLY);
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
        blksize_ = static_cast<std::size_t>(st.st_blksize);

        
        //std::cerr << "IoUringFileReader: Initialized io_uring with queue depth " 
        //          << queue_depth << " for file " << path_ << "\n";
        
        return Ok();
    }
    
    /// Close file and cleanup io_uring
    void close() noexcept {
        //std::cerr << "IoUringFileReader released resources for file " << path_ << "\n";
        
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
            size_ = 0;
        }

        pending_ops_ = 0;
        next_user_data_ = 1;
    }
    
    // ========================================================================
    // RawReader Interface (Synchronous Operations)
    // ========================================================================

    [[nodiscard]] Result<std::size_t> hint_batch_size() const noexcept {
        return blksize_;
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

    /// Create independent clone for parallel access
    [[nodiscard]] IoUringFileReader clone() const noexcept {
        if (!is_valid()) [[unlikely]] {
            return IoUringFileReader();
        }
        // Re-open same file with same config
        // Note: Requires storing Config for later use
        IoUringFileReader cloned(path_, stored_config_);
        return cloned;
    }

    /// Get available async operation slots
    [[nodiscard]] std::size_t available_async_ops() const noexcept {
        std::lock_guard lock(liburing_mutex_);
        //return io_uring_sq_space_left(ring_.get());
        return pending_ops_ < real_cq_depth_ ? (real_cq_depth_ - pending_ops_) : 0;
    }
    
    /// Submit async read into user-provided buffer
    /// 
    /// The buffer MUST remain valid until completion is retrieved.
    /// Buffer is NOT copied - io_uring reads directly into it.
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
        
        std::size_t bytes_to_read = size;
        

        std::lock_guard lock(liburing_mutex_);

        // Allocate unique handle for this operation
        uint64_t user_data = next_user_data_++;
        
        // Get submission queue entry
        
        io_uring_sqe* sqe = io_uring_get_sqe(ring_.get());
        if (!sqe) [[unlikely]] {
            // Queue full - try submitting pending operations and wait for a completion
            int submitted = io_uring_submit_and_wait(ring_.get(), 1);
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
        ++pending_ops_;
        
        return Ok(uint64_t{user_data});
    }
    
    /// Poll for completed operations (non-blocking)
    Result<std::size_t> poll_completions(
        std::vector<std::pair<uint64_t, Result<std::size_t>>>& completions,
        std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        std::lock_guard lock(liburing_mutex_);
        
        io_uring_cqe* cqe;
        unsigned head;
        unsigned count = 0;
        
        // Peek at all available completions without blocking
        io_uring_for_each_cqe(ring_.get(), head, cqe) {
            completions.push_back(process_completion(cqe));
            ++count;
            if (max_completions > 0 && count >= max_completions) {
                break;
            }
        }
        
        // Mark processed completions as seen
        if (count > 0) {
            io_uring_cq_advance(ring_.get(), count);
        }
        pending_ops_ -= count;
        
        return count;
    }
    
    /// Wait for at least one completion (blocking)
    /// @param completions Output vector for completions (appended, not cleared)
    /// @param max_completions Maximum new completions to retrieve (0 = all available)
    /// @return Number of completions added to the vector
    Result<size_t> wait_completions(
        std::vector<std::pair<uint64_t, Result<std::size_t>>>& completions,
        std::size_t max_completions = 0) const noexcept {
        
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        std::lock_guard lock(liburing_mutex_);
        
        io_uring_cqe* cqe = nullptr;
        int ret = io_uring_wait_cqe(ring_.get(), &cqe);
        
        if (ret < 0) [[unlikely]] {
            return Err(Error::Code::ReadError, 
                      "io_uring_wait_cqe failed: " + std::string(std::strerror(-ret)));
        }
        
        size_t count = 0;
        if (cqe) {
            count = 1;
            completions.push_back(process_completion(cqe));
            io_uring_cqe_seen(ring_.get(), cqe);
            
            // Also collect any other completions that arrived
            unsigned head;
            if (max_completions != 1) {
                io_uring_for_each_cqe(ring_.get(), head, cqe) {
                    completions.push_back(process_completion(cqe));
                    ++count;
                    if (max_completions > 0 && count >= max_completions) {
                        break;
                    }
                }
            }

            if (count > 1)
                io_uring_cq_advance(ring_.get(), count-1);
            pending_ops_ -= count;
        }
        return count;
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
        
        std::lock_guard lock(liburing_mutex_);
        
        // Setup timeout
        __kernel_timespec ts{};
        ts.tv_sec = timeout.count() / 1000;
        ts.tv_nsec = (timeout.count() % 1000) * 1000000;
        
        io_uring_cqe* cqe = nullptr;
        int ret = io_uring_wait_cqe_timeout(ring_.get(), &cqe, &ts);
        
        if (ret == -ETIME) {
            // Timeout expired, no completions
            return 0;
        }
        
        if (ret < 0) [[unlikely]] {
            // Other error
            return Err(Error::Code::ReadError, 
                      "io_uring_wait_cqe_timeout failed: " + std::string(std::strerror(-ret)));
        }
        
        size_t count = 0;
        if (cqe) {
            count = 1;
            completions.push_back(process_completion(cqe));
            io_uring_cqe_seen(ring_.get(), cqe);
            
            // Also collect any other completions that arrived
            unsigned head;
            if (max_completions != 1) {
                io_uring_for_each_cqe(ring_.get(), head, cqe) {
                    completions.push_back(process_completion(cqe));
                    ++count;
                    if (max_completions > 0 && count >= max_completions) {
                        break;
                    }
                }
            }

            if (count > 1)
                io_uring_cq_advance(ring_.get(), count-1);
            pending_ops_ -= count;
        }
        
        return count;
    }
    
    /// Get number of pending operations
    [[nodiscard]] std::size_t pending_operations() const noexcept {
        std::lock_guard lock(liburing_mutex_);
        return pending_ops_;
    }
    
    /// Force submission of queued operations to kernel
    /// 
    /// io_uring batches submissions internally. This forces a flush.
    /// Call after submitting a batch of operations for optimal performance.
    [[nodiscard]] Result<void> flush_async_operations() const noexcept {
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "File not open");
        }
        
        std::lock_guard lock(liburing_mutex_);
        
        int submitted = io_uring_submit(ring_.get());
        if (submitted < 0) [[unlikely]] {
            return Err(Error::Code::ReadError, 
                      "io_uring_submit failed: " + std::string(std::strerror(-submitted)));
        }
        
        return Ok();
    }

private:
    
    /// Process a single completion queue entry
    /// Assumes liburing_mutex_ is held
    [[nodiscard]] std::pair<uint64_t, Result<std::size_t>>
    process_completion(io_uring_cqe* cqe) const noexcept {
        
        uint64_t user_data = reinterpret_cast<uint64_t>(io_uring_cqe_get_data(cqe));
        int result = cqe->res;
        
        // Check for errors
        if (result < 0) [[unlikely]] {
            return {uint64_t{user_data}, Err(Error::Code::ReadError, 
                   "io_uring read failed: " + std::string(std::strerror(-result)))};
        }
        
        // Return view without ownership (buffer owned by caller)
        return {uint64_t{user_data}, Ok(static_cast<std::size_t>(result)) };
    }
    
};

static_assert(RawReader<IoUringFileReader>, 
              "IoUringFileReader must satisfy RawReader concept");
static_assert(AsyncRawReader<IoUringFileReader>, 
              "IoUringFileReader must satisfy AsyncRawReader concept");

} // namespace tiffconcept

#endif // __linux__
