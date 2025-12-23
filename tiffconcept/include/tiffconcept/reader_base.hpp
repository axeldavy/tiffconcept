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
    
    // Must have a way to finalize the write (either explicit or in destructor)
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
    { reader.read(offset, size) } -> std::same_as<Result<typename T::ReadViewType>>;
    requires DataReadOnlyView<typename T::ReadViewType>;

    // Alternative read_into() method that reads directly into provided buffer
    { reader.read_into(buffer, offset, size) } -> std::same_as<Result<void>>;
    
    // Get total size of the readable content
    { reader.size() } -> std::same_as<Result<std::size_t>>;
    
    // Check if reader is valid/open
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
    { writer.write(offset, size) } -> std::same_as<Result<typename T::WriteViewType>>;
    requires DataWriteOnlyView<typename T::WriteViewType>;
    
    // Get total size of the writable content
    { writer.size() } -> std::same_as<Result<std::size_t>>;
    
    // Extend the file/buffer to a new size (for appending)
    { writer.resize(size) } -> std::same_as<Result<void>>;
    
    // Flush all pending writes to storage
    { writer.flush() } -> std::same_as<Result<void>>;
    
    // Check if writer is valid/open
    { writer.is_valid() } -> std::same_as<bool>;
};

/// Concept for asynchronous raw reader with completion-based I/O
/// Designed for efficient implementation on Linux (io_uring) and Windows (IOCP)
/// 
/// Design Philosophy:
/// - Completion-based: operations complete asynchronously, results retrieved via polling
/// - Zero-copy: caller provides buffers that must remain valid until completion
/// - Batch-friendly: submit multiple operations before waiting for results
/// - Thread-safe: multiple threads can submit and wait concurrently
/// 
/// Lifetime Requirements:
/// - Reader must outlive all pending operations
/// - Buffers passed to async_read_into() must remain valid until operation completes
/// - Operation handles must remain valid until completion retrieved or cancelled
/// 
/// Typical Usage Pattern:
/// @code
///   // Phase 1: Submit all reads upfront
///   std::vector<Handle> handles;
///   std::vector<std::unique_ptr<std::byte[]>> buffers;
///   
///   for (auto& tile : tiles) {
///       auto buffer = std::make_unique<std::byte[]>(tile.size);
///       auto handle = reader.async_read_into(
///           std::span(buffer.get(), tile.size), 
///           tile.offset, 
///           tile.size
///       ).value();
///       handles.push_back(std::move(handle));
///       buffers.push_back(std::move(buffer));
///   }
///   
///   reader.submit_pending();  // Force submission to OS
///   
///   // Phase 2: Process completions as they arrive
///   while (reader.pending_operations() > 0) {
///       auto completions = reader.wait_completions();
///       for (auto& [handle, result] : completions) {
///           if (result) {
///               // Process result.value().data()
///           }
///       }
///   }
/// @endcode
template <typename T>
concept AsyncRawReader = RawReader<T> && requires(
    const T reader,
    std::span<std::byte> buffer,
    std::size_t offset,
    std::size_t size,
    std::chrono::milliseconds timeout
) {
    // ========================================================================
    // Core Types
    // ========================================================================
    
    /// Opaque handle representing a pending async operation
    /// Must be movable but not copyable (unique ownership)
    /// Cancelled automatically on destruction if operation not yet completed
    typename T::AsyncOperationHandle;
    requires std::move_constructible<typename T::AsyncOperationHandle>;
    requires !std::copy_constructible<typename T::AsyncOperationHandle>;
    
    /// Result of a completed async read operation
    /// Contains either the read data view or an error
    typename T::AsyncReadResult;
    requires std::same_as<typename T::AsyncReadResult, 
                         Result<typename T::ReadViewType>>;
    
    // ========================================================================
    // Async Operation Submission
    // ========================================================================
    
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
    /// 
    /// Postconditions:
    /// - Operation is queued (may not be submitted to OS immediately)
    /// - Handle is valid until completion retrieved or cancelled
    /// - Returned handle owns the operation (RAII cancellation on destruction)
    /// 
    /// Thread-safety: Safe to call concurrently from multiple threads
    /// 
    /// Performance Notes:
    /// - Submissions may be batched internally until submit_pending() is called
    /// - For minimum latency, call submit_pending() after submitting a batch
    /// - For maximum throughput, submit all operations before calling submit_pending()
    /// 
    /// @param buffer User-provided buffer (must outlive operation)
    /// @param offset File offset to read from (in bytes)
    /// @param size Number of bytes to read
    /// @return Handle to track operation, or error if submission failed
    { reader.async_read_into(buffer, offset, size) } 
        -> std::same_as<Result<typename T::AsyncOperationHandle>>;
    
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
    /// Each completion is delivered to exactly one thread.
    /// 
    /// @param max_completions Maximum number of completions to retrieve
    ///                        (0 = retrieve all available completions)
    /// @return Vector of (handle, result) pairs for completed operations.
    ///         Vector may be empty if no operations have completed.
    { reader.poll_completions(std::size_t{}) } 
        -> std::same_as<std::vector<std::pair<
            typename T::AsyncOperationHandle, 
            typename T::AsyncReadResult>>>;
    
    /// Wait for at least one operation to complete (blocking)
    /// 
    /// Blocks until at least one pending operation completes, then returns
    /// all available completions (may be more than one). Returns immediately
    /// if completions are already available from previous operations.
    /// 
    /// If no operations are pending, returns empty vector immediately.
    /// 
    /// Thread-safety: Safe to call concurrently from multiple threads.
    /// Multiple waiting threads may all wake when completions arrive.
    /// Each completion is delivered to exactly one thread.
    /// 
    /// @param max_completions Maximum number of completions to retrieve
    ///                        (0 = retrieve all available completions)
    /// @return Vector of (handle, result) pairs for completed operations.
    ///         Vector may be empty if no operations were pending.
    { reader.wait_completions(std::size_t{}) } 
        -> std::same_as<std::vector<std::pair<
            typename T::AsyncOperationHandle, 
            typename T::AsyncReadResult>>>;
    
    /// Wait for completions with timeout (blocking)
    /// 
    /// Similar to wait_completions() but returns after timeout expires
    /// even if no operations completed. Useful for implementing cancellation
    /// or periodic progress checks.
    /// 
    /// @param timeout Maximum time to wait for completions
    /// @param max_completions Maximum number of completions to retrieve
    /// @return Vector of completions (may be empty if timeout expired)
    { reader.wait_completions_for(timeout, std::size_t{}) } 
        -> std::same_as<std::vector<std::pair<
            typename T::AsyncOperationHandle, 
            typename T::AsyncReadResult>>>;
    
    // ========================================================================
    // Operation Management
    // ========================================================================
    
    /// Get number of operations pending completion
    /// 
    /// Returns the number of operations that have been submitted via
    /// async_read_into() but whose completions have not yet been retrieved.
    /// 
    /// Note: This is an approximate count - the value may become stale
    /// immediately after reading. Primarily useful for determining when
    /// all operations have finished (pending_operations() == 0).
    /// 
    /// Thread-safety: Safe to call concurrently (atomic or lock-protected)
    /// 
    /// @return Approximate number of pending operations
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
    /// @return Number of operations submitted to OS, or error if submission failed
    { reader.submit_pending() } -> std::same_as<Result<std::size_t>>;
};

} // namespace tiffconcept
