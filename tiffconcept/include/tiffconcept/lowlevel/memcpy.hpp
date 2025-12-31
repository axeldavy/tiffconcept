#pragma once

#include <cstddef>

namespace tiffconcept {

namespace memcpy {

/// Repeated memcpy - copies the same block of memory multiple times with strides
void repeat_memcpy(
    void* __restrict dst,
    const void* __restrict src,
    std::size_t copy_size,
    std::size_t dst_stride,
    std::size_t src_stride,
    std::size_t repeat_count
) noexcept;

} // namespace memcpy

} // namespace tiffconcept

// Include implementation
#define TIFFCONCEPT_MEMCPY_HEADER
#include "impl/memcpy_impl.hpp"