#pragma once

/**
 * @file memory.hpp
 * @brief Cache-aware memory allocation utilities for high-performance TIFF decoding
 * 
 * This header provides memory allocation primitives optimized for cache efficiency
 * in TIFF tile processing. The key insight is that memory alignment and layout
 * significantly impact performance through:
 * 
 * - **Cache Line Alignment**: Prevents false sharing and split loads/stores
 * - **L1 Cache Set Distribution**: Avoids conflicts when accessing multiple buffers
 * - **Hardware Prefetcher Efficiency**: Aligned, sequential access patterns
 * 
 * ## Performance Impact
 * 
 * In benchmarks with glibc malloc vs tcmalloc, we observed 30-35% performance
 * differences in single-threaded tile extraction, despite no actual allocations
 * occurring (buffers were pre-sized). The difference was purely due to memory
 * layout affecting:
 * 
 * - L1 cache miss rate: 55% (glibc) vs 46% (tcmalloc)
 * - Hardware prefetcher effectiveness
 * - memcpy() performance in tight loops
 * 
 * ## Key Components
 * 
 * ### AlignedBuffer<T>
 * 
 * A std::vector-like container with guaranteed alignment. Used for:
 * - TileDecoder scratch buffers (64-byte aligned for cache lines)
 * - Temporary decompression buffers
 * - Any buffer accessed sequentially in hot loops
 * 
 * ### AlignedMemoryResource
 * 
 * A std::pmr::memory_resource that provides aligned allocations. Can be used
 * with std::pmr::polymorphic_allocator for containers that need alignment
 * guarantees without custom allocator types.
 * 
 * ## Usage Guidelines
 * 
 * **When to use AlignedBuffer:**
 * - Buffers accessed in tight loops (e.g., tile extraction)
 * - Buffers that participate in memcpy operations
 * - Thread-local scratch buffers
 * 
 * **When to use AlignedMemoryResource:**
 * - With std::pmr::vector, std::pmr::string, etc.
 * - When you need dynamic buffer pools with alignment
 * - When integrating with existing PMR-aware code
 * 
 * **When NOT to use:**
 * - Small, short-lived allocations (<1KB)
 * - Buffers allocated once and never resized
 * - Non-sequential access patterns (e.g., hash tables)
 * 
 * @example
 * @code{.cpp}
 * // Example 1: AlignedBuffer for decoder scratch space
 * class TileDecoder {
 *     memory::AlignedBuffer<uint16_t> scratch_buffer_;
 * 
 * public:
 *     void decode(const std::byte* compressed, size_t size) {
 *         size_t required = width * height * channels;
 *         if (scratch_buffer_.size() < required) {
 *             scratch_buffer_.resize(required);  // Maintains 64-byte alignment
 *         }
 *         // Use scratch_buffer_.data() for decompression
 *     }
 * };
 * 
 * // Example 2: PMR with aligned allocations
 * memory::AlignedMemoryResource aligned_resource(64);
 * std::pmr::monotonic_buffer_resource mono_resource(&aligned_resource);
 * 
 * std::pmr::vector<std::byte> buffer(&mono_resource);
 * buffer.resize(1024 * 1024);  // Allocated with 64-byte alignment
 * @endcode
 * 
 * @note All allocations use std::aligned_alloc (C++17 standard)
 * @note Thread safety: AlignedBuffer is NOT thread-safe
 * @note Thread safety: AlignedMemoryResource IS thread-safe (via std::pmr)
 */

#include <cstddef>
#include <memory_resource>
#include <span>

namespace tiffconcept {
namespace memory {

/// Cache line size for modern x86-64 and ARM CPUs
constexpr std::size_t CACHE_LINE_SIZE = 64;

/// Page size (typically 4KB on most systems)
constexpr std::size_t PAGE_SIZE = 4096;

/// Offset stride to avoid L1 cache conflicts (prime number near 4KB)
constexpr std::size_t CONFLICT_AVOIDANCE_STRIDE = 4093;  // Prime number

/**
 * @brief Simple aligned buffer with std::vector-like interface
 * 
 * AlignedBuffer provides a dynamically-sized, aligned memory buffer with
 * an API similar to std::vector. Unlike std::vector, it guarantees that
 * the underlying storage is aligned to a specified boundary.
 * 
 * ## Memory Layout
 * 
 * ```
 * ┌─────────────────────────────────────┐
 * │ Aligned allocation (via aligned_alloc) │
 * │ ┌─────────────────────────────────┐ │
 * │ │  Element 0                      │ │ ← data() points here
 * │ ├─────────────────────────────────┤ │
 * │ │  Element 1                      │ │
 * │ ├─────────────────────────────────┤ │
 * │ │  ...                            │ │
 * │ ├─────────────────────────────────┤ │
 * │ │  Element N-1                    │ │ ← size() elements
 * │ └─────────────────────────────────┘ │
 * │  Unused capacity                    │ ← capacity() - size()
 * └─────────────────────────────────────┘
 * ```
 * 
 * ## Key Properties
 * 
 * - **Alignment**: Data pointer is guaranteed aligned to constructor argument
 * - **Capacity growth**: Uses 1.5× growth strategy (same as libstdc++ vector)
 * - **No initialization**: Elements are NOT default-constructed on resize
 * - **Move-only**: Cannot be copied (avoids accidental expensive copies)
 *
 * ## Cache Conflict Avoidance
 * 
 * When multiple large buffers are allocated (e.g., output buffer + tile buffers),
 * they can map to the same L1 cache sets if allocated at predictable offsets.
 * This causes systematic cache conflicts that degrade performance.
 * 
 * AlignedBuffer adds a pseudo-random offset (based on allocation counter) to
 * spread allocations across different cache sets.
 * 
 * ## Performance Characteristics
 * 
 * | Operation | Complexity | Notes |
 * |-----------|-----------|--------|
 * | data() | O(1) | No overhead vs raw pointer |
 * | size() | O(1) | |
 * | resize() | O(n) if grows | May trigger reallocation + memcpy |
 * | reserve() | O(n) if grows | |
 * | clear() | O(1) | Does not deallocate |
 * 
 * @tparam T Element type (must be trivially copyable)
 * 
 * @note Elements are NOT initialized on allocation - call std::memset if needed
 * @note resize() does NOT value-initialize new elements
 * @note Thread safety: NOT thread-safe, use per-thread instances
 * 
 * @warning Do NOT use with types that have non-trivial destructors
 * @warning Do NOT store pointers to elements across resize() calls
 */
template <typename T>
class AlignedBuffer {
private:
    void* base_ptr_;
    T* data_;
    std::size_t capacity_;
    std::size_t size_;
    std::size_t alignment_;
    
    static std::pair<void*, T*> allocate_aligned(std::size_t count, std::size_t alignment);
    
public:
    /**
     * @brief Construct empty buffer with default alignment
     * 
     * Creates a buffer with no allocated memory. The first resize() or
     * reserve() will trigger allocation.
     * 
     * @note Default alignment is CACHE_LINE_SIZE (64 bytes)
     */
    constexpr AlignedBuffer() noexcept;
    
    /**
     * @brief Construct buffer with initial capacity and custom alignment
     * 
     * Allocates storage for at least `initial_capacity` elements, aligned
     * to `alignment` bytes. The size remains 0 until resize() is called.
     * 
     * @param initial_size Number of elements to allocate space for
     * @param alignment Alignment in bytes (must be power of 2)
     * 
     * @throws std::bad_alloc If allocation fails
     * @pre alignment must be a power of 2
     * @pre alignment >= alignof(T)
     * 
     * @note Size is initial_size after construction, capacity is >= initial_size
     * @note Elements are not value initialized
     */
    explicit AlignedBuffer(std::size_t initial_size, 
                           std::size_t alignment = CACHE_LINE_SIZE);
    
    /**
     * @brief Destructor - frees allocated memory
     * 
     * @note Does NOT call destructors on elements (assumes POD types)
     */
    ~AlignedBuffer();
    
    // Non-copyable (prevents accidental expensive copies)
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    
    /**
     * @brief Move constructor - transfers ownership
     * 
     * @param other Buffer to move from (left in valid but empty state)
     */
    AlignedBuffer(AlignedBuffer&& other) noexcept;
    
    /**
     * @brief Move assignment - transfers ownership
     * 
     * @param other Buffer to move from (left in valid but empty state)
     * @return Reference to this
     */
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;
    
    /**
     * @brief Get pointer to data
     * @return Aligned pointer to first element, or nullptr if empty
     */
    [[nodiscard]] T* data() noexcept;
    
    /**
     * @brief Get const pointer to data
     * @return Aligned pointer to first element, or nullptr if empty
     */
    [[nodiscard]] const T* data() const noexcept;
    
    /**
     * @brief Get current number of elements
     * @return Size in elements (NOT bytes)
     */
    [[nodiscard]] std::size_t size() const noexcept;
    
    /**
     * @brief Get current capacity
     * @return Capacity in elements (NOT bytes)
     */
    [[nodiscard]] std::size_t capacity() const noexcept;
    
    /**
     * @brief Check if buffer has no elements
     * @return true if size() == 0
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Get span over buffer elements
     * @return std::span<T> covering all elements [0, size())
     */
    [[nodiscard]] operator std::span<T>() noexcept {
        return std::span<T>(data_, size_);
    }
    
    /**
     * @brief Get const span over buffer elements
     * @return std::span<const T> covering all elements [0, size())
     */
    [[nodiscard]] operator std::span<const T>() const noexcept {
        return std::span<const T>(data_, size_);
    }
    
    /**
     * @brief Convert to span (explicit)
     * @return std::span<T> covering all elements [0, size())
     */
    [[nodiscard]] std::span<T> as_span() noexcept {
        return std::span<T>(data_, size_);
    }
    
    /**
     * @brief Convert to const span (explicit)
     * @return std::span<const T> covering all elements [0, size())
     */
    [[nodiscard]] std::span<const T> as_span() const noexcept {
        return std::span<const T>(data_, size_);
    }
    
    /**
     * @brief Get subspan starting at offset
     * @param offset Starting index
     * @return std::span<T> covering elements [offset, size())
     * @throws std::out_of_range if offset > size()
     */
    [[nodiscard]] std::span<T> subspan(std::size_t offset) {
        if (offset > size_) {
            throw std::out_of_range("AlignedBuffer::subspan: offset out of range");
        }
        return std::span<T>(data_ + offset, size_ - offset);
    }
    
    /**
     * @brief Get subspan with offset and count
     * @param offset Starting index
     * @param count Number of elements
     * @return std::span<T> covering elements [offset, offset + count)
     * @throws std::out_of_range if offset + count > size()
     */
    [[nodiscard]] std::span<T> subspan(std::size_t offset, std::size_t count) {
        if (offset + count > size_) {
            throw std::out_of_range("AlignedBuffer::subspan: range out of bounds");
        }
        return std::span<T>(data_ + offset, count);
    }
    
    /**
     * @brief Array subscript operator
     * @param index Element index
     * @return Reference to element at index
     * @note No bounds checking in release builds
     */
    [[nodiscard]] T& operator[](std::size_t index) noexcept {
        return data_[index];
    }
    
    /**
     * @brief Const array subscript operator
     * @param index Element index
     * @return Const reference to element at index
     * @note No bounds checking in release builds
     */
    [[nodiscard]] const T& operator[](std::size_t index) const noexcept {
        return data_[index];
    }
    
    /**
     * @brief Get iterator to beginning
     * @return Pointer to first element
     */
    [[nodiscard]] T* begin() noexcept {
        return data_;
    }
    
    /**
     * @brief Get const iterator to beginning
     * @return Const pointer to first element
     */
    [[nodiscard]] const T* begin() const noexcept {
        return data_;
    }
    
    /**
     * @brief Get iterator to end
     * @return Pointer to one past last element
     */
    [[nodiscard]] T* end() noexcept {
        return data_ + size_;
    }
    
    /**
     * @brief Get const iterator to end
     * @return Const pointer to one past last element
     */
    [[nodiscard]] const T* end() const noexcept {
        return data_ + size_;
    }
    
    /**
     * @brief Reserve capacity without changing size
     * 
     * Ensures capacity is at least `new_capacity`. If reallocation occurs,
     * existing elements are copied to new storage.
     * 
     * @param new_capacity Desired capacity in elements
     * 
     * @throws std::bad_alloc If allocation fails
     * @note If new_capacity <= capacity(), this is a no-op
     * @note Invalidates all pointers/references if reallocation occurs
     */
    void reserve(std::size_t new_capacity);
    
    /**
     * @brief Resize buffer to new size
     * 
     * If new_size > capacity(), triggers reallocation with 1.5× growth.
     * New elements are NOT initialized.
     * 
     * @param new_size Desired size in elements
     * 
     * @throws std::bad_alloc If allocation fails
     * @note New elements (if any) are uninitialized
     * @note Invalidates all pointers/references if reallocation occurs
     */
    void resize(std::size_t new_size);
    
    /**
     * @brief Clear size without deallocating
     * 
     * Sets size to 0 but retains allocated capacity.
     * 
     * @note Does NOT call destructors or free memory
     * @note capacity() remains unchanged
     */
    void clear() noexcept;
};

/**
 * @brief PMR memory resource with aligned allocations
 * 
 * AlignedMemoryResource is a std::pmr::memory_resource that guarantees
 * all allocations are aligned to a specified boundary. It can be used
 * as an upstream resource for other PMR allocators.
 * 
 * ## Use Cases
 * 
 * 1. **With pmr::monotonic_buffer_resource**: Fast batch allocations
 * 2. **With pmr::synchronized_pool_resource**: Thread-safe pooling
 * 3. **With pmr::unsynchronized_pool_resource**: Single-threaded pooling
 * 4. **Standalone**: Direct aligned allocations
 * 
 * ## Example Integration
 * 
 * ```cpp
 * // Create aligned upstream resource
 * AlignedMemoryResource aligned(64);  // 64-byte alignment
 * 
 * // Use with monotonic buffer for fast batch allocations
 * std::pmr::monotonic_buffer_resource mono(&aligned);
 * std::pmr::vector<std::byte> batch_buffer(&mono);
 * 
 * // All allocations from batch_buffer are 64-byte aligned
 * batch_buffer.resize(1024 * 1024);
 * 
 * // Reset mono resource to reuse memory
 * mono.release();
 * ```
 * 
 * ## Thread Safety
 * 
 * This class is thread-safe when used through std::pmr mechanisms:
 * - do_allocate() is thread-safe (uses std::aligned_alloc)
 * - do_deallocate() is thread-safe (uses std::free)
 * - Multiple threads can allocate/deallocate concurrently
 * 
 * @note This is NOT a pool - it forwards directly to std::aligned_alloc
 * @note Use pmr::synchronized_pool_resource on top for pooling behavior
 */
class AlignedMemoryResource : public std::pmr::memory_resource {
private:
    std::size_t alignment_;
    
protected:
    /**
     * @brief Allocate aligned memory
     * 
     * @param bytes Number of bytes to allocate
     * @param alignment Requested alignment (overridden by constructor alignment)
     * @return Pointer to allocated memory
     * 
     * @throws std::bad_alloc If allocation fails
     * @note Uses std::aligned_alloc internally
     */
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    
    /**
     * @brief Deallocate memory
     * 
     * @param ptr Pointer to memory to free
     * @param bytes Size of allocation (ignored)
     * @param alignment Alignment of allocation (ignored)
     * 
     * @note Thread-safe (std::free is thread-safe)
     */
    void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override;
    
    /**
     * @brief Check if two resources are equal
     * 
     * @param other Resource to compare with
     * @return true if this == &other
     */
    bool do_is_equal(const memory_resource& other) const noexcept override;
    
public:
    /**
     * @brief Construct with specified alignment
     * 
     * @param alignment Alignment for all allocations (must be power of 2)
     * 
     * @pre alignment must be a power of 2
     * @note Default alignment is CACHE_LINE_SIZE (64 bytes)
     */
    explicit AlignedMemoryResource(std::size_t alignment = CACHE_LINE_SIZE) noexcept;
    
    /**
     * @brief Get configured alignment
     * @return Alignment in bytes
     */
    [[nodiscard]] std::size_t alignment() const noexcept;
};

/**
 * @brief Thread-safe aligned memory pool using PMR
 * 
 * AlignedPoolResource combines aligned allocation with pooling for efficient
 * reuse of memory. It uses std::pmr::synchronized_pool_resource internally
 * with an AlignedMemoryResource as the upstream allocator.
 * 
 * ## Benefits
 * 
 * - **Thread-safe**: Multiple threads can allocate/deallocate concurrently
 * - **Aligned**: All allocations are cache-line aligned
 * - **Pooled**: Deallocated memory is reused instead of freed
 * - **PMR-compatible**: Works with std::pmr::polymorphic_allocator
 * 
 * ## Performance Characteristics
 * 
 * | Operation | Typical Cost | Notes |
 * |-----------|-------------|--------|
 * | First allocation | ~100-200ns | Allocates from upstream |
 * | Cached allocation | ~20-50ns | Reuses pooled block |
 * | Deallocation | ~10-30ns | Returns to pool |
 * | release() | ~O(n) | Frees all pooled blocks |
 * 
 * ## Example Usage
 * 
 * ```cpp
 * // Create aligned pool (64-byte alignment, default pool options)
 * AlignedPoolResource pool(64);
 * 
 * // Use with pmr::vector
 * std::pmr::vector<std::byte> buffer1(&pool);
 * buffer1.resize(1024);  // Allocates from pool
 * 
 * std::pmr::vector<std::byte> buffer2(&pool);
 * buffer2.resize(1024);  // Reuses freed memory from pool
 * 
 * // Release all pooled memory
 * pool.release();
 * ```
 * 
 * @note Suitable for high-frequency allocations of similar sizes
 * @note Not suitable for very large allocations (>1MB) - use monotonic_buffer_resource
 */
class AlignedPoolResource {
private:
    AlignedMemoryResource aligned_resource_;
    std::pmr::synchronized_pool_resource pool_;
    
public:
    /**
     * @brief Construct with custom alignment and pool options
     * 
     * @param alignment Alignment for all allocations (must be power of 2)
     * @param options Pool configuration (max blocks per chunk, etc.)
     * 
     * @note Default alignment is CACHE_LINE_SIZE (64 bytes)
     * @note See std::pmr::pool_options for configuration details
     */
    explicit AlignedPoolResource(
        std::size_t alignment = CACHE_LINE_SIZE,
        const std::pmr::pool_options& options = std::pmr::pool_options{}
    );
    
    /**
     * @brief Get polymorphic allocator for this pool
     * 
     * @return Allocator that can be passed to PMR containers
     * 
     * @example
     * ```cpp
     * AlignedPoolResource pool(64);
     * auto alloc = pool.get_allocator();
     * std::pmr::vector<int> vec(alloc);
     * ```
     */
    [[nodiscard]] std::pmr::polymorphic_allocator<std::byte> get_allocator() noexcept;
    
    /**
     * @brief Release all pooled memory back to upstream
     * 
     * Frees all memory held by the pool. Subsequent allocations will
     * request fresh memory from the upstream allocator.
     * 
     * @note This is NOT thread-safe with concurrent allocations
     * @note Only call when no allocations are in use
     */
    void release();
    
    /**
     * @brief Get configured alignment
     * @return Alignment in bytes
     */
    [[nodiscard]] std::size_t alignment() const noexcept;
};

} // namespace memory
} // namespace tiffconcept

// Include implementation
#define TIFFCONCEPT_MEMORY_HEADER
#include "impl/memory_impl.hpp"