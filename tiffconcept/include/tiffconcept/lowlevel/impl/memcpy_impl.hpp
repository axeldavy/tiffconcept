#pragma once

#include <cstddef>

#include "../../detail/compilers.hpp"

#ifndef TIFFCONCEPT_MEMCPY_HEADER
#include "../memcpy.hpp" // for linters
#endif

#if defined(__AVX2__)
    #define TIFFCONCEPT_HAS_AVX2 1
    #include <immintrin.h>
#elif defined(__SSE2__)
    #define TIFFCONCEPT_HAS_SSE2 1
    #include <emmintrin.h>
#endif


namespace tiffconcept {

namespace memcpy {

namespace detail {

/// Check if a pointer is aligned to a given boundary (must be power of 2)
template<std::size_t Alignment>
constexpr bool is_aligned(const void* ptr) noexcept {
    static_assert((Alignment & (Alignment - 1)) == 0, "Alignment must be power of 2");
    return (reinterpret_cast<std::uintptr_t>(ptr) & (Alignment - 1)) == 0;
}

// ============================================================================
// SSE2 Implementation
// ============================================================================

#if defined(TIFFCONCEPT_HAS_SSE2) || defined(TIFFCONCEPT_HAS_AVX2)

/// Copy 16 bytes - streaming when both aligned
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_16_sse2(void* __restrict dst, 
                           const void* __restrict src) noexcept {
    if constexpr (SrcAligned && DstAligned) {
        __m128i xmm = _mm_load_si128(static_cast<const __m128i*>(src));
        _mm_stream_si128(static_cast<__m128i*>(dst), xmm);
    } else if constexpr (SrcAligned && !DstAligned) {
        __m128i xmm = _mm_load_si128(static_cast<const __m128i*>(src));
        _mm_storeu_si128(static_cast<__m128i*>(dst), xmm);
    } else if constexpr (!SrcAligned && DstAligned) {
        __m128i xmm = _mm_loadu_si128(static_cast<const __m128i*>(src));
        _mm_stream_si128(static_cast<__m128i*>(dst), xmm);
    } else {
        __m128i xmm = _mm_loadu_si128(static_cast<const __m128i*>(src));
        _mm_storeu_si128(static_cast<__m128i*>(dst), xmm);
    }
}

/// Copy 32 bytes using SSE2 (2 × 16-byte operations)
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_32_sse2(void* __restrict dst, 
                           const void* __restrict src) noexcept {
    const auto* src_ptr = static_cast<const __m128i*>(src);
    auto* dst_ptr = static_cast<__m128i*>(dst);
    
    if constexpr (SrcAligned && DstAligned) {
        __m128i xmm0 = _mm_load_si128(src_ptr);
        __m128i xmm1 = _mm_load_si128(src_ptr + 1);
        _mm_stream_si128(dst_ptr, xmm0);
        _mm_stream_si128(dst_ptr + 1, xmm1);
    } else if constexpr (SrcAligned && !DstAligned) {
        __m128i xmm0 = _mm_load_si128(src_ptr);
        __m128i xmm1 = _mm_load_si128(src_ptr + 1);
        _mm_storeu_si128(dst_ptr, xmm0);
        _mm_storeu_si128(dst_ptr + 1, xmm1);
    } else if constexpr (!SrcAligned && DstAligned) {
        __m128i xmm0 = _mm_loadu_si128(src_ptr);
        __m128i xmm1 = _mm_loadu_si128(src_ptr + 1);
        _mm_stream_si128(dst_ptr, xmm0);
        _mm_stream_si128(dst_ptr + 1, xmm1);
    } else {
        __m128i xmm0 = _mm_loadu_si128(src_ptr);
        __m128i xmm1 = _mm_loadu_si128(src_ptr + 1);
        _mm_storeu_si128(dst_ptr, xmm0);
        _mm_storeu_si128(dst_ptr + 1, xmm1);
    }
}

/// Copy 64 bytes using SSE2 (4 × 16-byte operations)
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_64_sse2(void* __restrict dst, 
                           const void* __restrict src) noexcept {
    const auto* src_ptr = static_cast<const __m128i*>(src);
    auto* dst_ptr = static_cast<__m128i*>(dst);
    
    if constexpr (SrcAligned && DstAligned) {
        __m128i xmm0 = _mm_load_si128(src_ptr);
        __m128i xmm1 = _mm_load_si128(src_ptr + 1);
        __m128i xmm2 = _mm_load_si128(src_ptr + 2);
        __m128i xmm3 = _mm_load_si128(src_ptr + 3);
        _mm_stream_si128(dst_ptr, xmm0);
        _mm_stream_si128(dst_ptr + 1, xmm1);
        _mm_stream_si128(dst_ptr + 2, xmm2);
        _mm_stream_si128(dst_ptr + 3, xmm3);
    } else if constexpr (SrcAligned && !DstAligned) {
        __m128i xmm0 = _mm_load_si128(src_ptr);
        __m128i xmm1 = _mm_load_si128(src_ptr + 1);
        __m128i xmm2 = _mm_load_si128(src_ptr + 2);
        __m128i xmm3 = _mm_load_si128(src_ptr + 3);
        _mm_storeu_si128(dst_ptr, xmm0);
        _mm_storeu_si128(dst_ptr + 1, xmm1);
        _mm_storeu_si128(dst_ptr + 2, xmm2);
        _mm_storeu_si128(dst_ptr + 3, xmm3);
    } else if constexpr (!SrcAligned && DstAligned) {
        __m128i xmm0 = _mm_loadu_si128(src_ptr);
        __m128i xmm1 = _mm_loadu_si128(src_ptr + 1);
        __m128i xmm2 = _mm_loadu_si128(src_ptr + 2);
        __m128i xmm3 = _mm_loadu_si128(src_ptr + 3);
        _mm_stream_si128(dst_ptr, xmm0);
        _mm_stream_si128(dst_ptr + 1, xmm1);
        _mm_stream_si128(dst_ptr + 2, xmm2);
        _mm_stream_si128(dst_ptr + 3, xmm3);
    } else {
        __m128i xmm0 = _mm_loadu_si128(src_ptr);
        __m128i xmm1 = _mm_loadu_si128(src_ptr + 1);
        __m128i xmm2 = _mm_loadu_si128(src_ptr + 2);
        __m128i xmm3 = _mm_loadu_si128(src_ptr + 3);
        _mm_storeu_si128(dst_ptr, xmm0);
        _mm_storeu_si128(dst_ptr + 1, xmm1);
        _mm_storeu_si128(dst_ptr + 2, xmm2);
        _mm_storeu_si128(dst_ptr + 3, xmm3);
    }
}

/// Copy 128 bytes using SSE2 (8 × 16-byte operations)
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_128_sse2(void* __restrict dst, 
                            const void* __restrict src) noexcept {
    const auto* src_ptr = static_cast<const __m128i*>(src);
    auto* dst_ptr = static_cast<__m128i*>(dst);
    
    if constexpr (SrcAligned && DstAligned) {
        __m128i xmm0 = _mm_load_si128(src_ptr);
        __m128i xmm1 = _mm_load_si128(src_ptr + 1);
        __m128i xmm2 = _mm_load_si128(src_ptr + 2);
        __m128i xmm3 = _mm_load_si128(src_ptr + 3);
        __m128i xmm4 = _mm_load_si128(src_ptr + 4);
        __m128i xmm5 = _mm_load_si128(src_ptr + 5);
        __m128i xmm6 = _mm_load_si128(src_ptr + 6);
        __m128i xmm7 = _mm_load_si128(src_ptr + 7);
        _mm_stream_si128(dst_ptr, xmm0);
        _mm_stream_si128(dst_ptr + 1, xmm1);
        _mm_stream_si128(dst_ptr + 2, xmm2);
        _mm_stream_si128(dst_ptr + 3, xmm3);
        _mm_stream_si128(dst_ptr + 4, xmm4);
        _mm_stream_si128(dst_ptr + 5, xmm5);
        _mm_stream_si128(dst_ptr + 6, xmm6);
        _mm_stream_si128(dst_ptr + 7, xmm7);
    } else if constexpr (SrcAligned && !DstAligned) {
        __m128i xmm0 = _mm_load_si128(src_ptr);
        __m128i xmm1 = _mm_load_si128(src_ptr + 1);
        __m128i xmm2 = _mm_load_si128(src_ptr + 2);
        __m128i xmm3 = _mm_load_si128(src_ptr + 3);
        __m128i xmm4 = _mm_load_si128(src_ptr + 4);
        __m128i xmm5 = _mm_load_si128(src_ptr + 5);
        __m128i xmm6 = _mm_load_si128(src_ptr + 6);
        __m128i xmm7 = _mm_load_si128(src_ptr + 7);
        _mm_storeu_si128(dst_ptr, xmm0);
        _mm_storeu_si128(dst_ptr + 1, xmm1);
        _mm_storeu_si128(dst_ptr + 2, xmm2);
        _mm_storeu_si128(dst_ptr + 3, xmm3);
        _mm_storeu_si128(dst_ptr + 4, xmm4);
        _mm_storeu_si128(dst_ptr + 5, xmm5);
        _mm_storeu_si128(dst_ptr + 6, xmm6);
        _mm_storeu_si128(dst_ptr + 7, xmm7);
    } else if constexpr (!SrcAligned && DstAligned) {
        __m128i xmm0 = _mm_loadu_si128(src_ptr);
        __m128i xmm1 = _mm_loadu_si128(src_ptr + 1);
        __m128i xmm2 = _mm_loadu_si128(src_ptr + 2);
        __m128i xmm3 = _mm_loadu_si128(src_ptr + 3);
        __m128i xmm4 = _mm_loadu_si128(src_ptr + 4);
        __m128i xmm5 = _mm_loadu_si128(src_ptr + 5);
        __m128i xmm6 = _mm_loadu_si128(src_ptr + 6);
        __m128i xmm7 = _mm_loadu_si128(src_ptr + 7);
        _mm_stream_si128(dst_ptr, xmm0);
        _mm_stream_si128(dst_ptr + 1, xmm1);
        _mm_stream_si128(dst_ptr + 2, xmm2);
        _mm_stream_si128(dst_ptr + 3, xmm3);
        _mm_stream_si128(dst_ptr + 4, xmm4);
        _mm_stream_si128(dst_ptr + 5, xmm5);
        _mm_stream_si128(dst_ptr + 6, xmm6);
        _mm_stream_si128(dst_ptr + 7, xmm7);
    } else {
        __m128i xmm0 = _mm_loadu_si128(src_ptr);
        __m128i xmm1 = _mm_loadu_si128(src_ptr + 1);
        __m128i xmm2 = _mm_loadu_si128(src_ptr + 2);
        __m128i xmm3 = _mm_loadu_si128(src_ptr + 3);
        __m128i xmm4 = _mm_loadu_si128(src_ptr + 4);
        __m128i xmm5 = _mm_loadu_si128(src_ptr + 5);
        __m128i xmm6 = _mm_loadu_si128(src_ptr + 6);
        __m128i xmm7 = _mm_loadu_si128(src_ptr + 7);
        _mm_storeu_si128(dst_ptr, xmm0);
        _mm_storeu_si128(dst_ptr + 1, xmm1);
        _mm_storeu_si128(dst_ptr + 2, xmm2);
        _mm_storeu_si128(dst_ptr + 3, xmm3);
        _mm_storeu_si128(dst_ptr + 4, xmm4);
        _mm_storeu_si128(dst_ptr + 5, xmm5);
        _mm_storeu_si128(dst_ptr + 6, xmm6);
        _mm_storeu_si128(dst_ptr + 7, xmm7);
    }
}

/// Copy 256 bytes using SSE2 (16 × 16-byte operations)
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_256_sse2(void* __restrict dst, 
                            const void* __restrict src) noexcept {
    // Split into two 128-byte copies
    memcpy_128_sse2<SrcAligned, DstAligned>(dst, src);
    memcpy_128_sse2<SrcAligned, DstAligned>(static_cast<char*>(dst) + 128, 
                                            static_cast<const char*>(src) + 128);
}

#endif // SSE2 or AVX2

// ============================================================================
// AVX2 Implementation
// ============================================================================

#ifdef TIFFCONCEPT_HAS_AVX2

/// Copy 32 bytes using AVX2 - streaming when destination aligned
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_32_avx2(void* __restrict dst, 
                           const void* __restrict src) noexcept {
    if constexpr (SrcAligned && DstAligned) {
        __m256i ymm = _mm256_load_si256(static_cast<const __m256i*>(src));
        _mm256_stream_si256(static_cast<__m256i*>(dst), ymm);
    } else if constexpr (SrcAligned && !DstAligned) {
        __m256i ymm = _mm256_load_si256(static_cast<const __m256i*>(src));
        _mm256_storeu_si256(static_cast<__m256i*>(dst), ymm);
    } else if constexpr (!SrcAligned && DstAligned) {
        __m256i ymm = _mm256_loadu_si256(static_cast<const __m256i*>(src));
        _mm256_stream_si256(static_cast<__m256i*>(dst), ymm);
    } else {
        __m256i ymm = _mm256_loadu_si256(static_cast<const __m256i*>(src));
        _mm256_storeu_si256(static_cast<__m256i*>(dst), ymm);
    }
}

/// Copy 64 bytes using AVX2 (2 × 32-byte operations)
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_64_avx2(void* __restrict dst, 
                           const void* __restrict src) noexcept {
    const auto* src_ptr = static_cast<const __m256i*>(src);
    auto* dst_ptr = static_cast<__m256i*>(dst);
    
    if constexpr (SrcAligned && DstAligned) {
        __m256i ymm0 = _mm256_load_si256(src_ptr);
        __m256i ymm1 = _mm256_load_si256(src_ptr + 1);
        _mm256_stream_si256(dst_ptr, ymm0);
        _mm256_stream_si256(dst_ptr + 1, ymm1);
    } else if constexpr (SrcAligned && !DstAligned) {
        __m256i ymm0 = _mm256_load_si256(src_ptr);
        __m256i ymm1 = _mm256_load_si256(src_ptr + 1);
        _mm256_storeu_si256(dst_ptr, ymm0);
        _mm256_storeu_si256(dst_ptr + 1, ymm1);
    } else if constexpr (!SrcAligned && DstAligned) {
        __m256i ymm0 = _mm256_loadu_si256(src_ptr);
        __m256i ymm1 = _mm256_loadu_si256(src_ptr + 1);
        _mm256_stream_si256(dst_ptr, ymm0);
        _mm256_stream_si256(dst_ptr + 1, ymm1);
    } else {
        __m256i ymm0 = _mm256_loadu_si256(src_ptr);
        __m256i ymm1 = _mm256_loadu_si256(src_ptr + 1);
        _mm256_storeu_si256(dst_ptr, ymm0);
        _mm256_storeu_si256(dst_ptr + 1, ymm1);
    }
}

/// Copy 128 bytes using AVX2 (4 × 32-byte operations)
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_128_avx2(void* __restrict dst, 
                            const void* __restrict src) noexcept {
    const auto* src_ptr = static_cast<const __m256i*>(src);
    auto* dst_ptr = static_cast<__m256i*>(dst);
    
    if constexpr (SrcAligned && DstAligned) {
        __m256i ymm0 = _mm256_load_si256(src_ptr);
        __m256i ymm1 = _mm256_load_si256(src_ptr + 1);
        __m256i ymm2 = _mm256_load_si256(src_ptr + 2);
        __m256i ymm3 = _mm256_load_si256(src_ptr + 3);
        _mm256_stream_si256(dst_ptr, ymm0);
        _mm256_stream_si256(dst_ptr + 1, ymm1);
        _mm256_stream_si256(dst_ptr + 2, ymm2);
        _mm256_stream_si256(dst_ptr + 3, ymm3);
    } else if constexpr (SrcAligned && !DstAligned) {
        __m256i ymm0 = _mm256_load_si256(src_ptr);
        __m256i ymm1 = _mm256_load_si256(src_ptr + 1);
        __m256i ymm2 = _mm256_load_si256(src_ptr + 2);
        __m256i ymm3 = _mm256_load_si256(src_ptr + 3);
        _mm256_storeu_si256(dst_ptr, ymm0);
        _mm256_storeu_si256(dst_ptr + 1, ymm1);
        _mm256_storeu_si256(dst_ptr + 2, ymm2);
        _mm256_storeu_si256(dst_ptr + 3, ymm3);
    } else if constexpr (!SrcAligned && DstAligned) {
        __m256i ymm0 = _mm256_loadu_si256(src_ptr);
        __m256i ymm1 = _mm256_loadu_si256(src_ptr + 1);
        __m256i ymm2 = _mm256_loadu_si256(src_ptr + 2);
        __m256i ymm3 = _mm256_loadu_si256(src_ptr + 3);
        _mm256_stream_si256(dst_ptr, ymm0);
        _mm256_stream_si256(dst_ptr + 1, ymm1);
        _mm256_stream_si256(dst_ptr + 2, ymm2);
        _mm256_stream_si256(dst_ptr + 3, ymm3);
    } else {
        __m256i ymm0 = _mm256_loadu_si256(src_ptr);
        __m256i ymm1 = _mm256_loadu_si256(src_ptr + 1);
        __m256i ymm2 = _mm256_loadu_si256(src_ptr + 2);
        __m256i ymm3 = _mm256_loadu_si256(src_ptr + 3);
        _mm256_storeu_si256(dst_ptr, ymm0);
        _mm256_storeu_si256(dst_ptr + 1, ymm1);
        _mm256_storeu_si256(dst_ptr + 2, ymm2);
        _mm256_storeu_si256(dst_ptr + 3, ymm3);
    }
}

/// Copy 256 bytes using AVX2 (8 × 32-byte operations)
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_256_avx2(void* __restrict dst, 
                            const void* __restrict src) noexcept {
    const auto* src_ptr = static_cast<const __m256i*>(src);
    auto* dst_ptr = static_cast<__m256i*>(dst);
    
    if constexpr (SrcAligned && DstAligned) {
        __m256i ymm0 = _mm256_load_si256(src_ptr);
        __m256i ymm1 = _mm256_load_si256(src_ptr + 1);
        __m256i ymm2 = _mm256_load_si256(src_ptr + 2);
        __m256i ymm3 = _mm256_load_si256(src_ptr + 3);
        __m256i ymm4 = _mm256_load_si256(src_ptr + 4);
        __m256i ymm5 = _mm256_load_si256(src_ptr + 5);
        __m256i ymm6 = _mm256_load_si256(src_ptr + 6);
        __m256i ymm7 = _mm256_load_si256(src_ptr + 7);
        _mm256_stream_si256(dst_ptr, ymm0);
        _mm256_stream_si256(dst_ptr + 1, ymm1);
        _mm256_stream_si256(dst_ptr + 2, ymm2);
        _mm256_stream_si256(dst_ptr + 3, ymm3);
        _mm256_stream_si256(dst_ptr + 4, ymm4);
        _mm256_stream_si256(dst_ptr + 5, ymm5);
        _mm256_stream_si256(dst_ptr + 6, ymm6);
        _mm256_stream_si256(dst_ptr + 7, ymm7);
    } else if constexpr (SrcAligned && !DstAligned) {
        __m256i ymm0 = _mm256_load_si256(src_ptr);
        __m256i ymm1 = _mm256_load_si256(src_ptr + 1);
        __m256i ymm2 = _mm256_load_si256(src_ptr + 2);
        __m256i ymm3 = _mm256_load_si256(src_ptr + 3);
        __m256i ymm4 = _mm256_load_si256(src_ptr + 4);
        __m256i ymm5 = _mm256_load_si256(src_ptr + 5);
        __m256i ymm6 = _mm256_load_si256(src_ptr + 6);
        __m256i ymm7 = _mm256_load_si256(src_ptr + 7);
        _mm256_storeu_si256(dst_ptr, ymm0);
        _mm256_storeu_si256(dst_ptr + 1, ymm1);
        _mm256_storeu_si256(dst_ptr + 2, ymm2);
        _mm256_storeu_si256(dst_ptr + 3, ymm3);
        _mm256_storeu_si256(dst_ptr + 4, ymm4);
        _mm256_storeu_si256(dst_ptr + 5, ymm5);
        _mm256_storeu_si256(dst_ptr + 6, ymm6);
        _mm256_storeu_si256(dst_ptr + 7, ymm7);
    } else if constexpr (!SrcAligned && DstAligned) {
        __m256i ymm0 = _mm256_loadu_si256(src_ptr);
        __m256i ymm1 = _mm256_loadu_si256(src_ptr + 1);
        __m256i ymm2 = _mm256_loadu_si256(src_ptr + 2);
        __m256i ymm3 = _mm256_loadu_si256(src_ptr + 3);
        __m256i ymm4 = _mm256_loadu_si256(src_ptr + 4);
        __m256i ymm5 = _mm256_loadu_si256(src_ptr + 5);
        __m256i ymm6 = _mm256_loadu_si256(src_ptr + 6);
        __m256i ymm7 = _mm256_loadu_si256(src_ptr + 7);
        _mm256_stream_si256(dst_ptr, ymm0);
        _mm256_stream_si256(dst_ptr + 1, ymm1);
        _mm256_stream_si256(dst_ptr + 2, ymm2);
        _mm256_stream_si256(dst_ptr + 3, ymm3);
        _mm256_stream_si256(dst_ptr + 4, ymm4);
        _mm256_stream_si256(dst_ptr + 5, ymm5);
        _mm256_stream_si256(dst_ptr + 6, ymm6);
        _mm256_stream_si256(dst_ptr + 7, ymm7);
    } else {
        __m256i ymm0 = _mm256_loadu_si256(src_ptr);
        __m256i ymm1 = _mm256_loadu_si256(src_ptr + 1);
        __m256i ymm2 = _mm256_loadu_si256(src_ptr + 2);
        __m256i ymm3 = _mm256_loadu_si256(src_ptr + 3);
        __m256i ymm4 = _mm256_loadu_si256(src_ptr + 4);
        __m256i ymm5 = _mm256_loadu_si256(src_ptr + 5);
        __m256i ymm6 = _mm256_loadu_si256(src_ptr + 6);
        __m256i ymm7 = _mm256_loadu_si256(src_ptr + 7);
        _mm256_storeu_si256(dst_ptr, ymm0);
        _mm256_storeu_si256(dst_ptr + 1, ymm1);
        _mm256_storeu_si256(dst_ptr + 2, ymm2);
        _mm256_storeu_si256(dst_ptr + 3, ymm3);
        _mm256_storeu_si256(dst_ptr + 4, ymm4);
        _mm256_storeu_si256(dst_ptr + 5, ymm5);
        _mm256_storeu_si256(dst_ptr + 6, ymm6);
        _mm256_storeu_si256(dst_ptr + 7, ymm7);
    }
}

#endif // AVX2

// ============================================================================
// Public Interface - auto-selects best implementation
// ============================================================================

#ifdef TIFFCONCEPT_HAS_AVX2

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_32(void* __restrict dst, const void* __restrict src) noexcept {
    memcpy_32_avx2<SrcAligned, DstAligned>(dst, src);
}

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_64(void* __restrict dst, const void* __restrict src) noexcept {
    memcpy_64_avx2<SrcAligned, DstAligned>(dst, src);
}

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_128(void* __restrict dst, const void* __restrict src) noexcept {
    memcpy_128_avx2<SrcAligned, DstAligned>(dst, src);
}

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_256(void* __restrict dst, const void* __restrict src) noexcept {
    memcpy_256_avx2<SrcAligned, DstAligned>(dst, src);
}

#elif defined(TIFFCONCEPT_HAS_SSE2)

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_32(void* __restrict dst, const void* __restrict src) noexcept {
    memcpy_32_sse2<SrcAligned, DstAligned>(dst, src);
}

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_64(void* __restrict dst, const void* __restrict src) noexcept {
    memcpy_64_sse2<SrcAligned, DstAligned>(dst, src);
}

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_128(void* __restrict dst, const void* __restrict src) noexcept {
    memcpy_128_sse2<SrcAligned, DstAligned>(dst, src);
}

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_256(void* __restrict dst, const void* __restrict src) noexcept {
    memcpy_256_sse2<SrcAligned, DstAligned>(dst, src);
}

#else

// Fallback to memcpy for platforms without SIMD
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_32(void* __restrict dst, const void* __restrict src) noexcept {
    std::memcpy(dst, src, 32);
}

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_64(void* __restrict dst, const void* __restrict src) noexcept {
    std::memcpy(dst, src, 64);
}

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_128(void* __restrict dst, const void* __restrict src) noexcept {
    std::memcpy(dst, src, 128);
}

template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_256(void* __restrict dst, const void* __restrict src) noexcept {
    std::memcpy(dst, src, 256);
}

#endif


/// Generic memcpy for arbitrary sizes with SIMD when available
/// _mm_sfence should be called by the caller if DstAligned is true
template<bool SrcAligned, bool DstAligned>
TIFFCONCEPT_FORCE_INLINE void memcpy_generic(void* __restrict dst, 
                          const void* __restrict src, 
                          std::size_t size) noexcept {
    auto* dst_ptr = static_cast<char*>(dst);
    const auto* src_ptr = static_cast<const char*>(src);
    
#ifdef TIFFCONCEPT_HAS_AVX2
    // Use AVX2 for large blocks
    while (size >= 256) {
        memcpy_256<SrcAligned, DstAligned>(dst_ptr, src_ptr);
        dst_ptr += 256;
        src_ptr += 256;
        size -= 256;
    }
    
    while (size >= 128) {
        memcpy_128<SrcAligned, DstAligned>(dst_ptr, src_ptr);
        dst_ptr += 128;
        src_ptr += 128;
        size -= 128;
    }
    
    while (size >= 64) {
        memcpy_64<SrcAligned, DstAligned>(dst_ptr, src_ptr);
        dst_ptr += 64;
        src_ptr += 64;
        size -= 64;
    }
    
    while (size >= 32) {
        memcpy_32<SrcAligned, DstAligned>(dst_ptr, src_ptr);
        dst_ptr += 32;
        src_ptr += 32;
        size -= 32;
    }
#elif defined(TIFFCONCEPT_HAS_SSE2)
    // Use SSE2 for large blocks
    while (size >= 256) {
        memcpy_256<SrcAligned, DstAligned>(dst_ptr, src_ptr);
        dst_ptr += 256;
        src_ptr += 256;
        size -= 256;
    }
    
    while (size >= 128) {
        memcpy_128<SrcAligned, DstAligned>(dst_ptr, src_ptr);
        dst_ptr += 128;
        src_ptr += 128;
        size -= 128;
    }
    
    while (size >= 64) {
        memcpy_64<SrcAligned, DstAligned>(dst_ptr, src_ptr);
        dst_ptr += 64;
        src_ptr += 64;
        size -= 64;
    }
    
    while (size >= 32) {
        memcpy_32<SrcAligned, DstAligned>(dst_ptr, src_ptr);
        dst_ptr += 32;
        src_ptr += 32;
        size -= 32;
    }
    
    while (size >= 16) {
        memcpy_16_sse2<SrcAligned, DstAligned>(dst_ptr, src_ptr);
        dst_ptr += 16;
        src_ptr += 16;
        size -= 16;
    }
#endif
    
    // Copy remaining bytes
    if (size >= 8) {
        *reinterpret_cast<uint64_t*>(dst_ptr) = *reinterpret_cast<const uint64_t*>(src_ptr);
        dst_ptr += 8;
        src_ptr += 8;
        size -= 8;
    }
    
    if (size >= 4) {
        *reinterpret_cast<uint32_t*>(dst_ptr) = *reinterpret_cast<const uint32_t*>(src_ptr);
        dst_ptr += 4;
        src_ptr += 4;
        size -= 4;
    }
    
    if (size >= 2) {
        *reinterpret_cast<uint16_t*>(dst_ptr) = *reinterpret_cast<const uint16_t*>(src_ptr);
        dst_ptr += 2;
        src_ptr += 2;
        size -= 2;
    }
    
    if (size > 0) {
        *dst_ptr = *src_ptr;
    }
}

/// Repeat copy with compile-time known size (power of 2)
template<bool SrcAligned, bool DstAligned, std::size_t CopySize>
TIFFCONCEPT_FORCE_INLINE void repeat_memcpy_fixed(void* __restrict dst_base,
                                const void* __restrict src_base,
                                std::size_t dst_stride,
                                std::size_t src_stride,
                                std::size_t repeat_count) noexcept {
    auto* dst = static_cast<char*>(dst_base);
    const auto* src = static_cast<const char*>(src_base);
    
    if constexpr (CopySize <= 16) {
        for (std::size_t i = 0; i < repeat_count; ++i) {
            for (std::size_t offset = 0; offset < CopySize; ++offset) {
                dst[offset] = src[offset];
            }
            dst += dst_stride;
            src += src_stride;
        }
    }
#if defined(TIFFCONCEPT_HAS_AVX2) || defined(TIFFCONCEPT_HAS_SSE2)
    else if constexpr (CopySize == 32) {
        for (std::size_t i = 0; i < repeat_count; ++i) {
            memcpy_32<SrcAligned, DstAligned>(dst, src);
            dst += dst_stride;
            src += src_stride;
        }
        if constexpr (DstAligned) _mm_sfence();
    } else if constexpr (CopySize == 64) {
        for (std::size_t i = 0; i < repeat_count; ++i) {
            memcpy_64<SrcAligned, DstAligned>(dst, src);
            dst += dst_stride;
            src += src_stride;
        }
        if constexpr (DstAligned) _mm_sfence();
    } else if constexpr (CopySize == 128) {
        for (std::size_t i = 0; i < repeat_count; ++i) {
            memcpy_128<SrcAligned, DstAligned>(dst, src);
            dst += dst_stride;
            src += src_stride;
        }
        if constexpr (DstAligned) _mm_sfence();
    } else if constexpr (CopySize == 256) {
        for (std::size_t i = 0; i < repeat_count; ++i) {
            memcpy_256<SrcAligned, DstAligned>(dst, src);
            dst += dst_stride;
            src += src_stride;
        }
        if constexpr (DstAligned) _mm_sfence();
    } else if constexpr (CopySize == 512) {
        for (std::size_t i = 0; i < repeat_count; ++i) {
            memcpy_256<SrcAligned, DstAligned>(dst, src);
            memcpy_256<SrcAligned, DstAligned>(dst + 256, src + 256);
            dst += dst_stride;
            src += src_stride;
        }
        if constexpr (DstAligned) _mm_sfence();
    } else if constexpr (CopySize == 1024) {
        for (std::size_t i = 0; i < repeat_count; ++i) {
            memcpy_256<SrcAligned, DstAligned>(dst, src);
            memcpy_256<SrcAligned, DstAligned>(dst + 256, src + 256);
            memcpy_256<SrcAligned, DstAligned>(dst + 512, src + 512);
            memcpy_256<SrcAligned, DstAligned>(dst + 768, src + 768);
            dst += dst_stride;
            src += src_stride;
        }
        if constexpr (DstAligned) _mm_sfence();
    }
#else
    else {
        // Generic fallback for non-power-of-2 sizes
        for (std::size_t i = 0; i < repeat_count; ++i) {
            memcpy_generic<SrcAligned, DstAligned>(dst, src, CopySize);
            dst += dst_stride;
            src += src_stride;
        }
#if defined(TIFFCONCEPT_HAS_SSE2) || defined(TIFFCONCEPT_HAS_AVX2)
        if constexpr (DstAligned && CopySize >= 32) _mm_sfence();
#endif
    }
#endif
}

/// Runtime dispatch for alignment
template<std::size_t CopySize>
TIFFCONCEPT_FORCE_INLINE void repeat_memcpy_dispatch_alignment(void* __restrict dst_base,
                                            const void* __restrict src_base,
                                            std::size_t dst_stride,
                                            std::size_t src_stride,
                                            std::size_t repeat_count) noexcept {
#ifdef TIFFCONCEPT_HAS_AVX2
    const bool src_aligned = is_aligned<32>(src_base) && src_stride % 32 == 0;
    const bool dst_aligned = is_aligned<32>(dst_base) && dst_stride % 32 == 0;
#elif defined(TIFFCONCEPT_HAS_SSE2)
    const bool src_aligned = is_aligned<16>(src_base) && src_stride % 16 == 0;
    const bool dst_aligned = is_aligned<16>(dst_base) && dst_stride % 16 == 0;
#else
    const bool src_aligned = false;
    const bool dst_aligned = false;
#endif
    
    if (src_aligned && dst_aligned) {
        repeat_memcpy_fixed<true, true, CopySize>(dst_base, src_base, dst_stride, src_stride, repeat_count);
    } else if (dst_aligned) {
        repeat_memcpy_fixed<false, true, CopySize>(dst_base, src_base, dst_stride, src_stride, repeat_count);
    } else if (src_aligned) {
        repeat_memcpy_fixed<true, false, CopySize>(dst_base, src_base, dst_stride, src_stride, repeat_count);
    } else {
        repeat_memcpy_fixed<false, false, CopySize>(dst_base, src_base, dst_stride, src_stride, repeat_count);
    }
}

} // namespace detail

// ============================================================================
// Public repeat_memcpy function
// ============================================================================

/// Repeated memory copy with stride support
/// Optimized for tile copies with common power-of-2 sizes
inline void repeat_memcpy(void* __restrict dst_base,
                         const void* __restrict src_base,
                         std::size_t copy_size,
                         std::size_t dst_stride,
                         std::size_t src_stride,
                         std::size_t repeat_count) noexcept {
    // Early exit for zero repeats
    if (repeat_count == 0) return;
    
    // Dispatch based on common tile sizes (powers of 2)
    switch (copy_size) {
        case 1:   detail::repeat_memcpy_dispatch_alignment<1>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 2:   detail::repeat_memcpy_dispatch_alignment<2>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 4:   detail::repeat_memcpy_dispatch_alignment<4>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 8:   detail::repeat_memcpy_dispatch_alignment<8>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 16:  detail::repeat_memcpy_dispatch_alignment<16>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 32:  detail::repeat_memcpy_dispatch_alignment<32>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 64:  detail::repeat_memcpy_dispatch_alignment<64>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 128: detail::repeat_memcpy_dispatch_alignment<128>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 256: detail::repeat_memcpy_dispatch_alignment<256>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 512: detail::repeat_memcpy_dispatch_alignment<512>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        case 1024: detail::repeat_memcpy_dispatch_alignment<1024>(dst_base, src_base, dst_stride, src_stride, repeat_count); break;
        
        default: {
            // Runtime size - check alignment dynamically
            auto* dst = static_cast<char*>(dst_base);
            const auto* src = static_cast<const char*>(src_base);
            
#ifdef TIFFCONCEPT_HAS_AVX2
            const bool src_aligned = detail::is_aligned<32>(src_base) && src_stride % 32 == 0;
            const bool dst_aligned = detail::is_aligned<32>(dst_base) && dst_stride % 32 == 0;
#elif defined(TIFFCONCEPT_HAS_SSE2)
            const bool src_aligned = detail::is_aligned<16>(src_base) && src_stride % 16 == 0;
            const bool dst_aligned = detail::is_aligned<16>(dst_base) && dst_stride % 16 == 0;
#else
            const bool src_aligned = false;
            const bool dst_aligned = false;
#endif
            
            if (src_aligned && dst_aligned) {
                for (std::size_t i = 0; i < repeat_count; ++i) {
                    detail::memcpy_generic<true, true>(dst, src, copy_size);
                    dst += dst_stride;
                    src += src_stride;
                }
            } else if (dst_aligned) {
                for (std::size_t i = 0; i < repeat_count; ++i) {
                    detail::memcpy_generic<false, true>(dst, src, copy_size);
                    dst += dst_stride;
                    src += src_stride;
                }
            } else if (src_aligned) {
                for (std::size_t i = 0; i < repeat_count; ++i) {
                    detail::memcpy_generic<true, false>(dst, src, copy_size);
                    dst += dst_stride;
                    src += src_stride;
                }
            } else {
                for (std::size_t i = 0; i < repeat_count; ++i) {
                    detail::memcpy_generic<false, false>(dst, src, copy_size);
                    dst += dst_stride;
                    src += src_stride;
                }
            }
            
#if defined(TIFFCONCEPT_HAS_SSE2) || defined(TIFFCONCEPT_HAS_AVX2)
            _mm_sfence();
#endif
            break;
        }
    }
}


} // namespace memcpy

} // namespace tiffconcept