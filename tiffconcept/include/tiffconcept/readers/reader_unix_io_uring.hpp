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
//#include <sys/syscall.h>
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
        return open(path, Config{});
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

        io_uring_params params{};
        params.cq_entries = std::max(config.sq_queue_depth, config.cq_queue_depth); // must be greater than config.sq_queue_depth
        params.flags = 
            IORING_SETUP_CQSIZE // use cq_entries to set CQ size
            | IORING_SETUP_CLAMP // clamp to max supported sizes
            | IORING_SETUP_SUBMIT_ALL // submit all queued ops on io_uring_submit, even on error
            | IORING_SETUP_COOP_TASKRUN // improves performance since we don't use multiple threads
            | IORING_SETUP_TASKRUN_FLAG // improves COOP_TASKRUN
            | IORING_SETUP_NO_SQARRAY; // don't mmap SQ array (saves memory)
        // may want to use IORING_SETUP_SINGLE_ISSUER and IORING_SETUP_DEFER_TASKRUN as well
        params.sq_thread_cpu = 0;
        params.sq_thread_idle = 0;
        // entries filled by io_uring_queue_init_params
        params.sq_entries = config.sq_queue_depth;
        params.features = 0;
        params.wq_fd = -1;
        
        int ret = io_uring_queue_init_params(config.sq_queue_depth, ring_.get(), &params);
        if (ret == -ENOMEM) {
            io_uring_queue_exit(ring_.get());
            // When allocating/releasing very fast, the kernel seems to have a
            // processing delay to free the previously used resources.
            // Retry after a short delay.
            std::this_thread::yield();
            ret = io_uring_queue_init_params(config.sq_queue_depth, ring_.get(), &params);
            if (ret == -ENOMEM) {
                io_uring_queue_exit(ring_.get());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                ret = io_uring_queue_init_params(config.sq_queue_depth, ring_.get(), &params);
            }
        }

        // prints features for debugging
        /*std::cerr << "IoUringFileReader: io_uring initialized with features:";
        if (params.features & IORING_FEAT_SINGLE_MMAP) {
            std::cerr << " SINGLE_MMAP";
        }
        if (params.features & IORING_FEAT_NODROP) {
            std::cerr << " NODROP";
        }
        if (params.features & IORING_FEAT_SUBMIT_STABLE) {
            std::cerr << " SUBMIT_STABLE";
        }
        if (params.features & IORING_FEAT_RW_CUR_POS) {
            std::cerr << " RW_CUR_POS";
        }
        if (params.features & IORING_FEAT_CUR_PERSONALITY) {
            std::cerr << " CUR_PERSONALITY";
        }
        if (params.features & IORING_FEAT_FAST_POLL) {
            std::cerr << " FAST_POLL";
        }
        if (params.features & IORING_FEAT_POLL_32BITS) {
            std::cerr << " POLL_32BITS";
        }
        if (params.features & IORING_FEAT_SQPOLL_NONFIXED) {
            std::cerr << " SQPOLL_NONFIXED";
        }
        if (params.features & IORING_FEAT_EXT_ARG) {
            std::cerr << " EXT_ARG";
        }
        if (params.features & IORING_FEAT_NATIVE_WORKERS) {
            std::cerr << " NATIVE_WORKERS";
        }
        if (params.features & IORING_FEAT_RSRC_TAGS) {
            std::cerr << " RSRC_TAGS";
        }
        if (params.features & IORING_FEAT_CQE_SKIP) {
            std::cerr << " CQE_SKIP";
        }
        if (params.features & IORING_FEAT_LINKED_FILE) {
            std::cerr << " LINKED_FILE";
        }
        if (params.features & IORING_FEAT_REG_REG_RING) {
            std::cerr << " REG_REG_RING";
        }
        if (params.features & IORING_FEAT_MIN_TIMEOUT) {
            std::cerr << " MIN_TIMEOUT";
        }
        if (params.features & IORING_FEAT_RECVSEND_BUNDLE) {
            std::cerr << " RECVSEND_BUNDLE";
        }
        std::cerr << "\n";*/

        if (ret < 0) [[unlikely]] {
            int err = -ret;
            ring_.reset();
            std::cerr << "IoUringFileReader: Failed to initialize io_uring, error " << err << "\n";
            std::cerr << "  liburing error: " << std::strerror(err) << "\n";
            std::cerr << params.features << "\n";
            return Err(Error::Code::ReadError, 
                      "Failed to initialize io_uring: " + std::string(std::strerror(err)));
        }
        real_cq_depth_ = params.cq_entries;
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
