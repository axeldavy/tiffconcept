#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <new>

#ifndef TIFFCONCEPT_MEMORY_HEADER
#include "../memory.hpp" // for linters
#endif

namespace tiffconcept {
namespace memory {

// ============================================================================
// AlignedBuffer Implementation
// ============================================================================

// MSVC doesn't defined std::aligned_alloc
#ifdef _MSC_VER
inline void* aligned_alloc(std::size_t alignment, std::size_t size) {
    return _aligned_malloc(size, alignment);
}
inline void aligned_free(void* ptr) {
    _aligned_free(ptr);
}
#else
inline void* aligned_alloc(std::size_t alignment, std::size_t size) {
    return std::aligned_alloc(alignment, size);
}
inline void aligned_free(void* ptr) {
    std::free(ptr);
}
#endif

#if 0
template <typename T>
T* AlignedBuffer<T>::allocate_aligned(std::size_t count, std::size_t alignment) {
    if (count == 0) return nullptr;
    
    // std::aligned_alloc requires size to be multiple of alignment
    std::size_t size_bytes = count * sizeof(T);
    std::size_t aligned_size = (size_bytes + alignment - 1) / alignment * alignment;
    
    void* ptr = memory::aligned_alloc(alignment, aligned_size);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return static_cast<T*>(ptr);
}
#endif

template <typename T>
inline std::pair<void*, T*> AlignedBuffer<T>::allocate_aligned(
    std::size_t count, 
    std::size_t alignment
) {
    if (count == 0) return {nullptr, nullptr};

    thread_local std::size_t allocation_counter_ = 0;
    
    // Calculate offset (multiple of alignment, up to 8× alignment)
    std::size_t offset_factor = (allocation_counter_++ * CONFLICT_AVOIDANCE_STRIDE) % 8;
    std::size_t offset = offset_factor * alignment;
    
    // Allocate extra space for offset
    std::size_t size_bytes = count * sizeof(T);
    std::size_t total_size = size_bytes + offset;
    
    // Round up to multiple of alignment
    std::size_t aligned_size = (total_size + alignment - 1) / alignment * alignment;
    
    void* base_ptr = memory::aligned_alloc(alignment, aligned_size);
    if (!base_ptr) {
        throw std::bad_alloc();
    }
    
    // Apply offset
    T* data_ptr = reinterpret_cast<T*>(static_cast<char*>(base_ptr) + offset);
    
    return {base_ptr, data_ptr};
}

template <typename T>
constexpr AlignedBuffer<T>::AlignedBuffer() noexcept
    : base_ptr_(nullptr)
    , data_(nullptr)
    , capacity_(0)
    , size_(0)
    , alignment_(CACHE_LINE_SIZE) {}

template <typename T>
AlignedBuffer<T>::AlignedBuffer(std::size_t initial_size, std::size_t alignment)
    : capacity_(initial_size)
    , size_(initial_size)
    , alignment_(alignment) {
    auto [base, data] = allocate_aligned(initial_size, alignment);
    base_ptr_ = base;
    data_ = data;
}

template <typename T>
AlignedBuffer<T>::~AlignedBuffer() {
    if (base_ptr_) {
        memory::aligned_free(base_ptr_);
    }
}

template <typename T>
AlignedBuffer<T>::AlignedBuffer(AlignedBuffer&& other) noexcept
    : base_ptr_(other.base_ptr_)
    , data_(other.data_)
    , capacity_(other.capacity_)
    , size_(other.size_)
    , alignment_(other.alignment_) {
    other.data_ = nullptr;
    other.capacity_ = 0;
    other.size_ = 0;
}

template <typename T>
AlignedBuffer<T>& AlignedBuffer<T>::operator=(AlignedBuffer&& other) noexcept {
    if (this != &other) {
        if (base_ptr_) {
            memory::aligned_free(base_ptr_);
        }
        
        base_ptr_ = other.base_ptr_;
        data_ = other.data_;
        capacity_ = other.capacity_;
        size_ = other.size_;
        alignment_ = other.alignment_;
        
        other.base_ptr_ = nullptr;
        other.data_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
    }
    return *this;
}

template <typename T>
[[nodiscard]] T* AlignedBuffer<T>::data() noexcept {
    return data_;
}

template <typename T>
[[nodiscard]] const T* AlignedBuffer<T>::data() const noexcept {
    return data_;
}

template <typename T>
[[nodiscard]] std::size_t AlignedBuffer<T>::size() const noexcept {
    return size_;
}

template <typename T>
[[nodiscard]] std::size_t AlignedBuffer<T>::capacity() const noexcept {
    return capacity_;
}

template <typename T>
[[nodiscard]] bool AlignedBuffer<T>::empty() const noexcept {
    return size_ == 0;
}

template <typename T>
void AlignedBuffer<T>::reserve(std::size_t new_capacity) {
    if (new_capacity <= capacity_) return;
    
    auto [new_base, new_data] = allocate_aligned(new_capacity, alignment_);
    
    // Copy existing elements
    if (data_ && size_ > 0) {
        std::memcpy(new_data, data_, size_ * sizeof(T));
    }
    
    if (base_ptr_) {
        memory::aligned_free(base_ptr_);
    }
    
    base_ptr_ = new_base;
    data_ = new_data;
    capacity_ = new_capacity;
}

template <typename T>
void AlignedBuffer<T>::resize(std::size_t new_size) {
    if (new_size > capacity_) {
        // Grow by 1.5× or to new_size, whichever is larger
        std::size_t new_capacity = std::max(new_size, capacity_ + capacity_ / 2);
        reserve(new_capacity);
    }
    size_ = new_size;
}

template <typename T>
void AlignedBuffer<T>::clear() noexcept {
    size_ = 0;
}

// ============================================================================
// AlignedBufferPool Implementation
// ============================================================================

inline AlignedBufferPool::AlignedUpstream::AlignedUpstream(std::size_t alignment)
    : alignment_(alignment) {}

inline void* AlignedBufferPool::AlignedUpstream::do_allocate(
    std::size_t bytes, 
    std::size_t align
) {
    // Use the larger of requested alignment and our minimum
    std::size_t actual_align = std::max(align, alignment_);
    
    // Round up size to multiple of alignment
    std::size_t aligned_bytes = (bytes + actual_align - 1) / actual_align * actual_align;
    
    void* ptr = memory::aligned_alloc(actual_align, aligned_bytes);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

inline void AlignedBufferPool::AlignedUpstream::do_deallocate(
    void* ptr, 
    std::size_t bytes, 
    std::size_t align
) {
    (void)bytes;
    (void)align;
    memory::aligned_free(ptr);
}

inline bool AlignedBufferPool::AlignedUpstream::do_is_equal(
    const std::pmr::memory_resource& other
) const noexcept {
    return this == &other;
}

inline AlignedBufferPool::AlignedBufferPool()
    : upstream_(CACHE_LINE_SIZE)
    , pool_(std::pmr::pool_options{
        .max_blocks_per_chunk = 0,  // Use default
        .largest_required_pool_block = 16 * 1024 * 1024  // 16MB
    }, &upstream_)
    , alignment_(CACHE_LINE_SIZE) {}

inline AlignedBufferPool::AlignedBufferPool(
    std::size_t alignment,
    std::size_t max_block_size
)
    : upstream_(alignment)
    , pool_(std::pmr::pool_options{
        .max_blocks_per_chunk = 0,
        .largest_required_pool_block = max_block_size
    }, &upstream_)
    , alignment_(alignment) {}

inline void AlignedBufferPool::release() {
    pool_.release();
}

inline void* AlignedBufferPool::do_allocate(std::size_t bytes, std::size_t align) {
    return pool_.allocate(bytes, std::max(align, alignment_));
}

inline void AlignedBufferPool::do_deallocate(void* ptr, std::size_t bytes, std::size_t align) {
    pool_.deallocate(ptr, bytes, std::max(align, alignment_));
}

inline bool AlignedBufferPool::do_is_equal(
    const std::pmr::memory_resource& other
) const noexcept {
    return this == &other;
}

} // namespace memory
} // namespace tiffconcept