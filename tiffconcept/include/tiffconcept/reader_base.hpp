#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include "types/result.hpp"

namespace tiffconcept {

/// Concept for a read-only view into data with RAII lifetime management
/// Only one thread at a time should access the view
template <typename T>
concept DataReadOnlyView = requires(T view) {
    // Access to the underlying data
    { view.data() } -> std::same_as<std::span<const std::byte>>;
    
    // Size of the data
    { view.size() } -> std::same_as<std::size_t>;
    
    // Check if view is empty
    { view.empty() } -> std::same_as<bool>;
    
    // Must be movable for Result<T> and transferring ownership
    requires std::move_constructible<T>;
    requires std::is_nothrow_move_constructible_v<T>;
};


/// Concept for a write-only view into data with RAII lifetime management
/// Data can be written to the storage when the view is released/destroyed,
/// when flush() is called or directly when accessing the data.
/// Only one thread at a time should access the view
template <typename T>
concept DataWriteOnlyView = requires(T view) {
    // Access to the underlying writable data
    { view.data() } -> std::same_as<std::span<std::byte>>;
    
    // Size of the writable region
    { view.size() } -> std::same_as<std::size_t>;
    
    // Check if view is empty
    { view.empty() } -> std::same_as<bool>;
    
    // Must be movable for Result<T> and transferring ownership
    requires std::move_constructible<T>;
    requires std::is_nothrow_move_constructible_v<T>;
    
    // Before flush, there is no guarantee the write actually has
    // an effect. Destructing the view does an implicit flush. 
    // flush() returns Result<void> to indicate success/failure
    { view.flush() } -> std::same_as<Result<void>>;
};


/// Check if a DataWriteOnlyView supports reading back written data before flush()
/// This allows in-place operations like decompression + predictor decoding
/// without requiring a separate temporary buffer.
/// 
/// To opt-in, the view type must define a static constexpr bool member:
///   static constexpr bool supports_inplace_readback = true;
template <typename T>
concept DataWriteViewWithReadback = DataWriteOnlyView<T> && requires(const T view) {
    // Allow reading what was written
    // This must work BEFORE flush() is called
    { T::supports_inplace_readback } -> std::convertible_to<bool>;
    requires T::supports_inplace_readback == true;
};


/// Concept for a raw reader that provides thread-safe positioned reads
template <typename T>
concept RawReader = requires(const T reader, void* buffer, std::size_t offset, std::size_t size) {
    // Read operation returning a view (implementation may use zero-copy or allocate)
    // The returned view type must satisfy DataReadOnlyView concept
    // Calls to read() must be threadsafe. Several threads may use the results
    // of read() simultaneously.
    // All the DataReadOnlyView returned must be destroyed before the RawReader is destroyed.
    { reader.read(offset, size) } -> std::same_as<Result<typename T::ReadViewType>>;
    requires DataReadOnlyView<typename T::ReadViewType>;

    // Alternative read_into() method that reads directly into provided buffer
    // Must be thread-safe
    { reader.read_into(buffer, offset, size) } -> std::same_as<Result<void>>;
    
    // Get total size of the readable content
    // Must be thread-safe
    { reader.size() } -> std::same_as<Result<std::size_t>>;

    // Get a rough hint for the optimal size in byte for read operations
    // used to batch consecutive reads before calling read().
    { reader.hint_batch_size() } -> std::same_as<Result<std::size_t>>;
    
    // Check if reader is valid/open
    // Must be thread-safe, but with the assumption that
    // the status of is_valid will not change during processing
    // In other words, it is disallowed to call reader methods to
    // close the file or open a new one while tiffconcept uses it.
    { reader.is_valid() } -> std::same_as<bool>;

    // Hint whether read() must allocate new buffer or can return zero-copy views
    // If true, read_into() should be preferred for performance
    { T::read_must_allocate } -> std::convertible_to<bool>;
};

/// Concept for a raw writer that provides thread-safe positioned writes
template <typename T>
concept RawWriter = requires(T writer, std::size_t offset, std::size_t size) {
    // Allocate/map a writable region at the given offset
    // The returned view type must satisfy DataWriteOnlyView concept
    // Calls to write() must be threadsafe. Several threads may use the results
    // of write() simultaneously.
    // If the item supports both RawReader and RawWriter, simultaneous reads and writes
    // must be safe as long as they do not overlap. Overlapping reads/writes result in
    // undefined behavior.
    // All DataWriteOnlyView instances must be destroyed before the reader is destroyed.
    { writer.write(offset, size) } -> std::same_as<Result<typename T::WriteViewType>>;
    requires DataWriteOnlyView<typename T::WriteViewType>;
    
    // Get total size of the writable content
    // Must be thread-safe
    { writer.size() } -> std::same_as<Result<std::size_t>>;
    
    // Extend the file/buffer to a new size (for appending)
    // Must be thread-safe
    { writer.resize(size) } -> std::same_as<Result<void>>;
    
    // Flush all pending writes to storage
    // Must be thread-safe
    { writer.flush() } -> std::same_as<Result<void>>; // TODO: seems redundant. Remove ?
    
    // Check if writer is valid/open
    { writer.is_valid() } -> std::same_as<bool>;
};

/// Concept for asynchronous raw reader with completion-based I/O
///
/// Design Philosophy:
/// - Completion-based: operations complete asynchronously, results retrieved via polling
/// - Zero-copy: caller provides buffers that must remain valid until completion
/// - Batch-friendly: submit multiple operations before waiting for results
/// - Single-Thread: While safe to call from multiple threads, it is expected that
///   async operations are handled by a single thread (submission, waits, etc). Not
///   doing so can result in deadlock (it is ok to move all operations to another thread
///   as long as a single thread manage them).
/// - Clonable: To counter-balance the previous condition, a clone() method enables to
///   have a separate instance that can be used in parallel on another thread.
/// - Bounded: A maximum number of pending operations is allowed on a thread.
///
/// Lifetime Requirements:
/// - Reader must outlive all pending operations
/// - Buffers associated to an operation handles MUST remain valid until completion retrieved
/// - When the reader is passed to tiffconcept, all previous operations must have been
///   completed beforehand. The reader should not be used outside tiffconcept when the
///   call has not completed.
///
template <typename T>
concept AsyncRawReader = RawReader<T> && requires(
    const T reader,
    std::span<std::byte> buffer,
    std::size_t offset,
    std::size_t size,
    std::chrono::milliseconds timeout
) {
    /// Get a new reader instance of the same content
    { reader.clone() } -> std::same_as<T>;

    // ========================================================================
    // Async Operation Submission
    // ========================================================================
    
    /// Return an approximate number of async operations before async_read
    /// may fail (submission queue full)
    { reader.available_async_ops() } -> std::same_as<std::size_t>;
    
    /// Submit async read into user-provided buffer (zero-copy)
    /// 
    /// The buffer MUST remain valid until the completion is retrieved via
    /// poll_completions() or wait_completions(). Modifying or destroying
    /// the buffer before completion results in undefined behavior.
    /// 
    /// Preconditions:
    /// - buffer.size() >= size
    /// - buffer remains valid until completion retrieved
    /// - offset + size <= reader.size()
    /// - reader.is_valid() == true
    /// - reader.available_async_ops() > 0
    /// 
    /// Postconditions:
    /// - Operation is queued (may not be submitted to OS immediately)
    /// - Handle is valid until completion retrieved
    /// - The buffer must be valid until completion for the handle is obtained.
    /// 
    /// Thread-safety: Safe to call concurrently from multiple threads
    ///   but not desirable (see Design section)
    /// 
    /// Note:
    /// - Submissions may be batched internally until submit_pending() is called
    /// 
    /// @param buffer User-provided buffer (must outlive operation)
    /// @param offset File offset to read from (in bytes)
    /// @param size Number of bytes to read
    /// @return Handle to track operation, or error if submission failed
    /// @return WriteError is used for failures due to full submission queue.
    { reader.async_read_into(buffer, offset, size) } 
        -> std::same_as<Result<uint64_t>>;
    
    // ========================================================================
    // Completion Retrieval
    // ========================================================================
    
    /// Poll for completed operations (non-blocking)
    /// 
    /// Returns immediately with any operations that have completed since
    /// the last poll. If no operations have completed, returns empty vector.
    /// Does not block waiting for I/O.
    /// 
    /// Each completion is returned exactly once. After retrieving a completion,
    /// the associated buffer is safe to reuse or destroy.
    /// 
    /// Thread-safety: Safe to call concurrently from multiple threads.
    /// Warning: May block if another thread uses wait_for_completions.
    /// Each completion is delivered to exactly one thread.
    /// 
    /// @param max_completions Maximum number of completions to retrieve
    ///                        (0 = retrieve all available completions)
    /// @return Vector of (handle, result) pairs for completed operations.
    ///         result contains the number of bytes read or an error.
    ///         Vector may be empty if no operations have completed.
    { reader.poll_completions(
        std::declval<std::vector<std::pair<uint64_t, Result<std::size_t>>>&>(),
        std::size_t{}) }
        -> std::same_as<Result<std::size_t>>;
    
    /// Wait for at least one operation to complete (blocking)
    /// 
    /// Blocks until at least one pending operation completes, then returns
    /// all available completions (may be more than one). Returns immediately
    /// if completions are already available from previous operations.
    /// 
    /// If no operations are pending, returns empty vector immediately.
    /// 
    /// Thread-safety: Safe, but may block other threads from polling.
    /// Each completion is delivered to exactly one thread.
    /// 
    /// @param max_completions Maximum number of completions to retrieve
    ///                        (0 = retrieve all available completions)
    /// @return Vector of (handle, result) pairs for completed operations.
    ///         result contains the number of bytes read or an error.
    ///         Vector may be empty if no operations were pending.
    { reader.wait_completions(
        std::declval<std::vector<std::pair<uint64_t, Result<std::size_t>>>&>(),
        std::size_t{}) } 
        -> std::same_as<Result<std::size_t>>;
    
    /// Wait for completions with timeout (blocking)
    /// 
    /// Similar to wait_completions() but returns after timeout expires
    /// even if no operations completed.
    /// 
    /// @param timeout Maximum time to wait for completions
    /// @param max_completions Maximum number of completions to retrieve
    /// @return Vector of completions (may be empty if timeout expired)
    ///         result contains the number of bytes read or an error.
    ///         Vector may be empty if no operations were pending.
    { reader.wait_completions_for(
        std::declval<std::vector<std::pair<uint64_t, Result<std::size_t>>>&>(),
        timeout, std::size_t{}) } 
        -> std::same_as<Result<std::size_t>>;
    
    // ========================================================================
    // Operation Management
    // ========================================================================
    
    /// Get number of operations pending completion
    /// 
    /// Returns the number of operations that have been submitted via
    /// async_read_into() but whose completions have not yet been retrieved.
    /// 
    /// Note: This is an exact count. Primarily useful for determining when
    /// all operations have finished (pending_operations() == 0).
    /// 
    /// Thread-safety: Safe to call concurrently (atomic or lock-protected)
    /// 
    /// @return Number of pending operations
    { reader.pending_operations() } -> std::same_as<std::size_t>;
    
    /// Force submission of queued operations to OS
    /// 
    /// Some implementations (notably io_uring) may queue submissions
    /// internally until explicitly flushed to the kernel. This method
    /// ensures all previously submitted operations are passed to the OS.
    /// 
    /// For optimal performance:
    /// - Submit a batch of operations (async_read_into × N)
    /// - Call submit_pending() once to flush the batch
    /// - Wait for completions
    /// 
    /// For implementations that submit immediately (e.g., IOCP), this
    /// may be a no-op but should still be called for portability.
    /// 
    /// Thread-safety: Safe to call concurrently
    /// 
    /// @return Success or error if submission failed
    { reader.flush_async_operations() } -> std::same_as<Result<void>>;
};

} // namespace tiffconcept
