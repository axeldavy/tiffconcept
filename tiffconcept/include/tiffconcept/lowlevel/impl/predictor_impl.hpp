#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <span>
#include "../../types/tiff_spec.hpp"
#include "../../detail/compilers.hpp"

#ifndef TIFFCONCEPT_PREDICTOR_HEADER
#include "../predictor.hpp" // for linters
#endif

#if defined(__AVX2__) || defined(__AVX512F__)
    #include <immintrin.h>
#elif defined(__SSE2__)
    #include <emmintrin.h>
#endif

namespace tiffconcept {

namespace predictor {

namespace detail {

// ============================================================================
// SIMD Prefix Sum Implementations
// ============================================================================

#ifdef __SSE2__

TIFFCONCEPT_FORCE_INLINE uint8_t carry_vec_to_carry_sse2_u8(__m128i carry_vec) noexcept {
    return static_cast<uint8_t>(_mm_extract_epi16(carry_vec, 7) >> 8);
}

TIFFCONCEPT_FORCE_INLINE uint16_t carry_vec_to_carry_sse2_u16(__m128i carry_vec) noexcept {
    return static_cast<uint16_t>(_mm_extract_epi16(carry_vec, 7));
}

TIFFCONCEPT_FORCE_INLINE __m128i carry_to_carry_vec_sse2_u8(uint8_t carry) noexcept {
    return _mm_set1_epi8(static_cast<char>(carry));
}

TIFFCONCEPT_FORCE_INLINE __m128i carry_to_carry_vec_sse2_u16(uint16_t carry) noexcept {
    return _mm_set1_epi16(static_cast<int16_t>(carry));
}

/// SSE2 prefix sum for uint8_t (16 elements at a time)
TIFFCONCEPT_FORCE_INLINE __m128i prefix_sum_sse2_u8(__m128i delta, __m128i& carry_vec) noexcept {
    __m128i sum = _mm_add_epi8(delta, _mm_slli_si128(delta, 1));
    sum = _mm_add_epi8(sum, _mm_slli_si128(sum, 2));
    sum = _mm_add_epi8(sum, _mm_slli_si128(sum, 4));
    sum = _mm_add_epi8(sum, _mm_slli_si128(sum, 8));

    // Add carry from previous vector
    sum = _mm_add_epi8(sum, carry_vec);
    
    // Extract and broadcast new carry (last element)
    carry_vec = _mm_set1_epi8(static_cast<char>(_mm_extract_epi16(sum, 7) >> 8));
    
    return sum;
}

/// SSE2 prefix sum for uint16_t (8 elements at a time)
TIFFCONCEPT_FORCE_INLINE __m128i prefix_sum_sse2_u16(__m128i delta, __m128i& carry_vec) noexcept {
    __m128i sum = _mm_add_epi16(delta, _mm_slli_si128(delta, 2));
    sum = _mm_add_epi16(sum, _mm_slli_si128(sum, 4));
    sum = _mm_add_epi16(sum, _mm_slli_si128(sum, 8));
    
    // Add carry from previous vector
    sum = _mm_add_epi16(sum, carry_vec);
    
    // Extract and broadcast new carry (last element)
    carry_vec = _mm_set1_epi16(static_cast<int16_t>(_mm_extract_epi16(sum, 7)));
    
    return sum;
}

#endif // SSE2

#ifdef __AVX2__

TIFFCONCEPT_FORCE_INLINE uint8_t carry_vec_to_carry_avx2_u8(__m256i carry_vec) noexcept {
    return static_cast<uint8_t>(_mm256_extract_epi8(carry_vec, 31));
}

TIFFCONCEPT_FORCE_INLINE uint16_t carry_vec_to_carry_avx2_u16(__m256i carry_vec) noexcept {
    return static_cast<uint16_t>(_mm256_extract_epi16(carry_vec, 15));
}

TIFFCONCEPT_FORCE_INLINE __m256i carry_to_carry_vec_avx2_u8(uint8_t carry) noexcept {
    return _mm256_set1_epi8(static_cast<char>(carry));
}

TIFFCONCEPT_FORCE_INLINE __m256i carry_to_carry_vec_avx2_u16(uint16_t carry) noexcept {
    return _mm256_set1_epi16(static_cast<int16_t>(carry));
}

/// AVX2 prefix sum for uint8_t (32 elements at a time)
TIFFCONCEPT_FORCE_INLINE __m256i prefix_sum_avx2_u8(__m256i delta, __m256i& carry_vec) noexcept {
    // Intra-lane prefix sum (each 128-bit lane independently)
    __m256i sum = _mm256_add_epi8(delta, _mm256_bslli_epi128(delta, 1));
    sum = _mm256_add_epi8(sum, _mm256_bslli_epi128(sum, 2));
    sum = _mm256_add_epi8(sum, _mm256_bslli_epi128(sum, 4));
    sum = _mm256_add_epi8(sum, _mm256_bslli_epi128(sum, 8));

#if defined(__AVX512VL__) && defined(__AVX512VBMI__)
    // Get a vector full of the last element of the lower lane
    __m256i lower_lane_last = _mm256_permutexvar_epi8(
        _mm256_set1_epi8(15), // Index 15 from each lane
        sum
    );
    // Blend: keep lower lane as-is, add to upper lane
    __m256i lane_carry = _mm256_mask_blend_epi8(0xFFFF0000, _mm256_setzero_si256(), lower_lane_last);
    sum = _mm256_add_epi8(sum, lane_carry);
#else
    // Retrieve last element of lower 128-bit lane
    uint8_t lower_lane_last = static_cast<uint8_t>(_mm256_extract_epi8(sum, 15));
    // Build carry vector
    __m256i lane_carry = _mm256_set_m128i(
        _mm_set1_epi8(static_cast<char>(lower_lane_last)),
        _mm_setzero_si128()
    );
    sum = _mm256_add_epi8(sum, lane_carry);
#endif

    // Add carry from previous vector
    sum = _mm256_add_epi8(sum, carry_vec);

    // Extract and broadcast new carry (last element)
#if defined(__AVX512VL__) && defined(__AVX512VBMI__)
    carry_vec = _mm256_permutexvar_epi8(_mm256_set1_epi8(31), sum);
#else
    carry_vec = _mm256_set1_epi8(static_cast<char>(_mm256_extract_epi8(sum, 31)));
#endif

    return sum;
}

/// AVX2 prefix sum for uint16_t (16 elements at a time)
TIFFCONCEPT_FORCE_INLINE __m256i prefix_sum_avx2_u16(__m256i delta, __m256i& carry_vec) noexcept {
    // Intra-lane prefix sum (each 128-bit lane independently)
    __m256i sum = _mm256_add_epi16(delta, _mm256_bslli_epi128(delta, 2));
    sum = _mm256_add_epi16(sum, _mm256_bslli_epi128(sum, 4));
    sum = _mm256_add_epi16(sum, _mm256_bslli_epi128(sum, 8));

    // Add first 128-bit lane's last element to second 128-bit lane
#if defined(__AVX512VL__)  && defined(__AVX512BW__)
    // Get a vector full of the last element of the lower lane
    __m256i lower_lane_last = _mm256_permutexvar_epi16(
        _mm256_set1_epi16(7), // Index 7 from each lane
        sum
    );
    // Blend: keep lower lane as-is, add to upper lane
    __m256i lane_carry = _mm256_mask_blend_epi16(0xFF00, _mm256_setzero_si256(), lower_lane_last);
    sum = _mm256_add_epi16(sum, lane_carry);

#else
    // Retrieve last element of lower 128-bit lane
    uint16_t lower_lane_last = static_cast<uint16_t>(_mm256_extract_epi16(sum, 7));
    // Build carry vector
    __m256i lane_carry = _mm256_set_m128i(
        _mm_set1_epi16(static_cast<int16_t>(lower_lane_last)),
        _mm_setzero_si128()
    );
    sum = _mm256_add_epi16(sum, lane_carry);
#endif

    // Add carry from previous vector
    sum = _mm256_add_epi16(sum, carry_vec);

    // Extract and broadcast new carry (last element)
#if defined(__AVX512VL__)  && defined(__AVX512BW__)
    carry_vec = _mm256_permutexvar_epi16(_mm256_set1_epi16(15), sum);
#else
    carry_vec = _mm256_set1_epi16(static_cast<int16_t>(_mm256_extract_epi16(sum, 15)));
#endif

    return sum;
}

#endif // AVX2

#ifdef __AVX512F__

TIFFCONCEPT_FORCE_INLINE uint8_t carry_vec_to_carry_avx512_u8(__m512i carry_vec) noexcept {
    // Extract last byte from the 512-bit vector
    __m128i last_lane = _mm512_extracti32x4_epi32(carry_vec, 3);
    return static_cast<uint8_t>(_mm_extract_epi16(last_lane, 7) >> 8);
}

TIFFCONCEPT_FORCE_INLINE uint16_t carry_vec_to_carry_avx512_u16(__m512i carry_vec) noexcept {
    // Extract last word from the 512-bit vector
    __m128i last_lane = _mm512_extracti32x4_epi32(carry_vec, 3);
    return static_cast<uint16_t>(_mm_extract_epi16(last_lane, 7));
}

TIFFCONCEPT_FORCE_INLINE __m512i carry_to_carry_vec_avx512_u8(uint8_t carry) noexcept {
    return _mm512_set1_epi8(static_cast<char>(carry));
}

TIFFCONCEPT_FORCE_INLINE __m512i carry_to_carry_vec_avx512_u16(uint16_t carry) noexcept {
    return _mm512_set1_epi16(static_cast<int16_t>(carry));
}

/// AVX512F prefix sum for uint8_t (64 elements at a time)
TIFFCONCEPT_FORCE_INLINE __m512i prefix_sum_avx512_u8(__m512i delta, __m512i& carry_vec) noexcept {
    // Intra-lane prefix sum using shifts (within each 128-bit lane)
    __m512i sum = _mm512_add_epi8(delta, _mm512_bslli_epi128(delta, 1));
    sum = _mm512_add_epi8(sum, _mm512_bslli_epi128(sum, 2));
    sum = _mm512_add_epi8(sum, _mm512_bslli_epi128(sum, 4));
    sum = _mm512_add_epi8(sum, _mm512_bslli_epi128(sum, 8));
    
#if defined(__AVX512VBMI__)
    // Use VBMI for efficient cross-lane propagation
    __m512i lane_carries = _mm512_permutexvar_epi8(
        _mm512_set_epi8(
            47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47,  // Lane 3 gets lane 2's last
            31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31,  // Lane 2 gets lane 1's last
            15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  // Lane 1 gets lane 0's last
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0   // Lane 0 gets zero
        ),
        sum
    );
    
#ifdef __AVX512BW__
    // Zero out lane 0's carry using AVX512BW mask operations
    lane_carries = _mm512_maskz_mov_epi8(0xFFFFFFFFFFFF0000ULL, lane_carries);
#else
    // Without AVX512BW, use blend with zero
    __m512i zero = _mm512_setzero_si512();
    __m512i mask_vec = _mm512_set_epi64(
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
        0, 0
    );
    lane_carries = _mm512_and_si512(lane_carries, mask_vec);
#endif
    
    sum = _mm512_add_epi8(sum, lane_carries);

    // Add carry from previous vector
    sum = _mm512_add_epi8(sum, carry_vec);
    
    // Extract final carry
    carry_vec = _mm512_permutexvar_epi8(_mm512_set1_epi8(63), sum);
    
#else
    // Without VBMI, use manual lane extraction and propagation (AVX512F only)
    __m128i lane0 = _mm512_extracti32x4_epi32(sum, 0);
    __m128i lane1 = _mm512_extracti32x4_epi32(sum, 1);
    __m128i lane2 = _mm512_extracti32x4_epi32(sum, 2);
    __m128i lane3 = _mm512_extracti32x4_epi32(sum, 3);
    
    uint8_t carry0 = static_cast<uint8_t>(_mm_extract_epi16(lane0, 7) >> 8);
    uint8_t carry1 = static_cast<uint8_t>(_mm_extract_epi16(lane1, 7) >> 8);
    uint8_t carry2 = static_cast<uint8_t>(_mm_extract_epi16(lane2, 7) >> 8);
    
    // Propagate carries across lanes
    lane1 = _mm_add_epi8(lane1, _mm_set1_epi8(static_cast<char>(carry0)));
    lane2 = _mm_add_epi8(lane2, _mm_set1_epi8(static_cast<char>(carry0 + carry1)));
    lane3 = _mm_add_epi8(lane3, _mm_set1_epi8(static_cast<char>(carry0 + carry1 + carry2)));
    
    sum = _mm512_inserti32x4(_mm512_inserti32x4(_mm512_inserti32x4(
        _mm512_castsi128_si512(lane0), lane1, 1), lane2, 2), lane3, 3);

    // Add carry from previous vector
    sum = _mm512_add_epi8(sum, carry_vec);
    
    // Extract final carry
    uint8_t carry3 = static_cast<uint8_t>(_mm_extract_epi16(lane3, 7) >> 8);
    carry_vec = _mm512_set1_epi8(static_cast<char>(carry0 + carry1 + carry2 + carry3));
#endif
    
    return sum;
}

/// AVX512F prefix sum for uint16_t (32 elements at a time)
TIFFCONCEPT_FORCE_INLINE __m512i prefix_sum_avx512_u16(__m512i delta, __m512i& carry_vec) noexcept {
    // Intra-lane prefix sum using shifts (within each 128-bit lane)
    __m512i sum = _mm512_add_epi16(delta, _mm512_bslli_epi128(delta, 2));
    sum = _mm512_add_epi16(sum, _mm512_bslli_epi128(sum, 4));
    sum = _mm512_add_epi16(sum, _mm512_bslli_epi128(sum, 8));
    
#if defined(__AVX512BW__)
    // Cross-lane carry propagation using permutexvar (requires AVX512BW for epi16)
    __m512i lane_carries = _mm512_permutexvar_epi16(
        _mm512_set_epi16(
            23, 23, 23, 23, 23, 23, 23, 23,  // Lane 3 gets lane 2's last
            15, 15, 15, 15, 15, 15, 15, 15,  // Lane 2 gets lane 1's last
             7,  7,  7,  7,  7,  7,  7,  7,  // Lane 1 gets lane 0's last
             0,  0,  0,  0,  0,  0,  0,  0   // Lane 0 gets zero
        ),
        sum
    );
    // Zero out lane 0's carry
    lane_carries = _mm512_maskz_mov_epi16(0xFFFFFF00, lane_carries);
    sum = _mm512_add_epi16(sum, lane_carries);

    // Add carry from previous vector
    sum = _mm512_add_epi16(sum, carry_vec);
    
    // Extract final carry
    carry_vec = _mm512_permutexvar_epi16(_mm512_set1_epi16(31), sum);
    
#else
    // Without AVX512BW, use manual lane extraction (AVX512F only)
    __m128i lane0 = _mm512_extracti32x4_epi32(sum, 0);
    __m128i lane1 = _mm512_extracti32x4_epi32(sum, 1);
    __m128i lane2 = _mm512_extracti32x4_epi32(sum, 2);
    __m128i lane3 = _mm512_extracti32x4_epi32(sum, 3);
    
    uint16_t carry0 = static_cast<uint16_t>(_mm_extract_epi16(lane0, 7));
    uint16_t carry1 = static_cast<uint16_t>(_mm_extract_epi16(lane1, 7));
    uint16_t carry2 = static_cast<uint16_t>(_mm_extract_epi16(lane2, 7));
    
    // Propagate carries across lanes
    lane1 = _mm_add_epi16(lane1, _mm_set1_epi16(static_cast<int16_t>(carry0)));
    lane2 = _mm_add_epi16(lane2, _mm_set1_epi16(static_cast<int16_t>(carry0 + carry1)));
    lane3 = _mm_add_epi16(lane3, _mm_set1_epi16(static_cast<int16_t>(carry0 + carry1 + carry2)));
    
    sum = _mm512_inserti32x4(_mm512_inserti32x4(_mm512_inserti32x4(
        _mm512_castsi128_si512(lane0), lane1, 1), lane2, 2), lane3, 3);

    // Add carry from previous vector
    sum = _mm512_add_epi16(sum, carry_vec);
    
    // Extract final carry
    uint16_t carry3 = static_cast<uint16_t>(_mm_extract_epi16(lane3, 7));
    carry_vec = _mm512_set1_epi16(static_cast<int16_t>(carry0 + carry1 + carry2 + carry3));
#endif
    
    return sum;
}

#endif // __AVX512F__


/// Apply horizontal differencing (TIFF predictor=2) decoding in place - specialized implementation
/// 
/// @tparam T Pixel type (uint8_t, uint16_t, etc.)
/// @tparam SamplesPerPixel Number of samples per pixel (compile-time constant for optimization)
template <DeltaDecodableInteger T, std::size_t SamplesPerPixel>
inline void delta_decode_horizontal_impl(
    std::span<T> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride) noexcept {
    
    for (std::size_t y = 0; y < height; ++y) {
        std::size_t row_offset = y * stride;
        
        if constexpr (SamplesPerPixel != 1) {
            // Note: could be optimized with SIMD as well, by deinterleaving channels
            // is a local array, process, then reinterleave iteratively
            for (std::size_t x = 1; x < width; ++x) {
                for (std::size_t s = 0; s < SamplesPerPixel; ++s) {
                    std::size_t curr_idx = row_offset + x * SamplesPerPixel + s;
                    std::size_t prev_idx = row_offset + (x - 1) * SamplesPerPixel + s;
                    buffer[curr_idx] += buffer[prev_idx];
                }
            }
        } else {
            T acc = 0;
            std::size_t x = 0;
#if defined(__AVX512F__)
            if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>) {
                __m512i acc_vec = carry_to_carry_vec_avx512_u8(static_cast<uint8_t>(acc));
                while (x + 64 <= width) {
                    __m512i delta_vec = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&buffer[row_offset + x]));
                    __m512i sum_vec = prefix_sum_avx512_u8(delta_vec, acc_vec);
                    _mm512_storeu_si512(reinterpret_cast<__m512i*>(&buffer[row_offset + x]), sum_vec);
                    x += 64;
                }
                acc = carry_vec_to_carry_avx512_u8(acc_vec);
            } else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>) {
                __m512i acc_vec = carry_to_carry_vec_avx512_u16(static_cast<uint16_t>(acc));
                while (x + 32 <= width) {
                    __m512i delta_vec = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&buffer[row_offset + x]));
                    __m512i sum_vec = prefix_sum_avx512_u16(delta_vec, acc_vec);
                    _mm512_storeu_si512(reinterpret_cast<__m512i*>(&buffer[row_offset + x]), sum_vec);
                    x += 32;
                }
                acc = carry_vec_to_carry_avx512_u16(acc_vec);
            }
#endif
#if defined(__AVX2__)
            if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>) {
                __m256i acc_vec = carry_to_carry_vec_avx2_u8(static_cast<uint8_t>(acc));
                while (x + 32 <= width) {
                    __m256i delta_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&buffer[row_offset + x]));
                    __m256i sum_vec = prefix_sum_avx2_u8(delta_vec, acc_vec);
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(&buffer[row_offset + x]), sum_vec);
                    x += 32;
                }
                acc = carry_vec_to_carry_avx2_u8(acc_vec);
            } else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>) {
                __m256i acc_vec = carry_to_carry_vec_avx2_u16(static_cast<uint16_t>(acc));
                while (x + 16 <= width) {
                    __m256i delta_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&buffer[row_offset + x]));
                    __m256i sum_vec = prefix_sum_avx2_u16(delta_vec, acc_vec);
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(&buffer[row_offset + x]), sum_vec);
                    x += 16;
                }
                acc = carry_vec_to_carry_avx2_u16(acc_vec);
            }
#endif
#if defined(__SSE2__) // not elif to passthough for the last remaining data if any
            if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>) {
                __m128i acc_vec = carry_to_carry_vec_sse2_u8(static_cast<uint8_t>(acc));
                while (x + 16 <= width) {
                    __m128i delta_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&buffer[row_offset + x]));
                    __m128i sum_vec = prefix_sum_sse2_u8(delta_vec, acc_vec);
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(&buffer[row_offset + x]), sum_vec);
                    x += 16;
                }
                acc = carry_vec_to_carry_sse2_u8(acc_vec);
            } else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>) {
                __m128i acc_vec = carry_to_carry_vec_sse2_u16(static_cast<uint16_t>(acc));
                while (x + 8 <= width) {
                    __m128i delta_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&buffer[row_offset + x]));
                    __m128i sum_vec = prefix_sum_sse2_u16(delta_vec, acc_vec);
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(&buffer[row_offset + x]), sum_vec);
                    x += 8;
                }
                acc = carry_vec_to_carry_sse2_u16(acc_vec);
            }
#endif
            for (; x < width; ++x) {
                std::size_t curr_idx = row_offset + x;
                acc += buffer[curr_idx];
                buffer[curr_idx] = acc;
            }
        }
    }
}

/// Apply floating point horizontal differencing - specialized implementation
/// 
/// @tparam FloatType float or double
/// @tparam SamplesPerPixel Number of samples per pixel (compile-time constant for optimization)
template <DeltaDecodableNativeFloat FloatType, std::size_t SamplesPerPixel>
inline void delta_decode_floating_point_impl(
    std::span<FloatType> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride) noexcept {
    
    using UIntType = std::conditional_t<std::is_same_v<FloatType, float>, 
                                        uint32_t, 
                                        uint64_t>;
    
    static_assert(sizeof(FloatType) == sizeof(UIntType));
    
    for (std::size_t y = 0; y < height; ++y) {
        std::size_t row_offset = y * stride;
        
        // Unrolled loop for each channel - compiler can optimize better
        for (std::size_t x = 1; x < width; ++x) {
            for (std::size_t s = 0; s < SamplesPerPixel; ++s) {
                std::size_t curr_idx = row_offset + x * SamplesPerPixel + s;
                std::size_t prev_idx = row_offset + (x - 1) * SamplesPerPixel + s;
                
                auto prev_int = std::bit_cast<UIntType>(buffer[prev_idx]);
                auto curr_int = std::bit_cast<UIntType>(buffer[curr_idx]);
                
                buffer[curr_idx] = std::bit_cast<FloatType>(prev_int + curr_int);
            }
        }
    }
}

/// Apply floating point horizontal differencing for non-native float types - specialized implementation
/// 
/// @tparam FloatType Float16 or Float24
/// @tparam SamplesPerPixel Number of samples per pixel (compile-time constant for optimization)
template <DeltaDecodableNonNativeFloat FloatType, std::size_t SamplesPerPixel>
inline void delta_decode_nonnative_float_impl(
    std::span<FloatType> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride) noexcept {
    
    using UIntType = std::conditional_t<std::is_same_v<FloatType, Float16>, 
                                        uint16_t, 
                                        uint32_t>;
    
    for (std::size_t y = 0; y < height; ++y) {
        std::size_t row_offset = y * stride;
        
        for (std::size_t x = 1; x < width; ++x) {
            for (std::size_t s = 0; s < SamplesPerPixel; ++s) {
                std::size_t curr_idx = row_offset + x * SamplesPerPixel + s;
                std::size_t prev_idx = row_offset + (x - 1) * SamplesPerPixel + s;
                
                UIntType prev_int, curr_int;
                if constexpr (std::is_same_v<FloatType, Float16>) {
                    prev_int = buffer[prev_idx].as_uint16();
                    curr_int = buffer[curr_idx].as_uint16();
                    buffer[curr_idx].from_uint16(static_cast<uint16_t>(prev_int + curr_int));
                } else { // Float24
                    prev_int = buffer[prev_idx].as_uint32();
                    curr_int = buffer[curr_idx].as_uint32();
                    buffer[curr_idx].from_uint32((prev_int + curr_int) & 0xFFFFFF);
                }
            }
        }
    }
}

/// Apply horizontal differencing (TIFF predictor=2) encoding in place - specialized implementation
/// 
/// @tparam T Pixel type (uint8_t, uint16_t, etc.)
/// @tparam SamplesPerPixel Number of samples per pixel (compile-time constant for optimization)
template <DeltaDecodableInteger T, std::size_t SamplesPerPixel>
inline void delta_encode_horizontal_impl(
    std::span<T> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride) noexcept {
    
    for (std::size_t y = 0; y < height; ++y) {
        std::size_t row_offset = y * stride;
        
        // Process from right to left to avoid overwriting values we need
        for (std::size_t x = width - 1; x > 0; --x) {
            for (std::size_t s = 0; s < SamplesPerPixel; ++s) {
                std::size_t curr_idx = row_offset + x * SamplesPerPixel + s;
                std::size_t prev_idx = row_offset + (x - 1) * SamplesPerPixel + s;
                buffer[curr_idx] -= buffer[prev_idx];
            }
        }
    }
}

/// Apply floating point horizontal differencing encoding - specialized implementation
/// 
/// @tparam FloatType float or double
/// @tparam SamplesPerPixel Number of samples per pixel (compile-time constant for optimization)
template <DeltaDecodableNativeFloat FloatType, std::size_t SamplesPerPixel>
inline void delta_encode_floating_point_impl(
    std::span<FloatType> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride) noexcept {
    
    using UIntType = std::conditional_t<std::is_same_v<FloatType, float>, 
                                        uint32_t, 
                                        uint64_t>;
    
    static_assert(sizeof(FloatType) == sizeof(UIntType));
    
    for (std::size_t y = 0; y < height; ++y) {
        std::size_t row_offset = y * stride;
        
        // Process from right to left to avoid overwriting values we need
        for (std::size_t x = width - 1; x > 0; --x) {
            for (std::size_t s = 0; s < SamplesPerPixel; ++s) {
                std::size_t curr_idx = row_offset + x * SamplesPerPixel + s;
                std::size_t prev_idx = row_offset + (x - 1) * SamplesPerPixel + s;
                
                auto prev_int = std::bit_cast<UIntType>(buffer[prev_idx]);
                auto curr_int = std::bit_cast<UIntType>(buffer[curr_idx]);
                
                buffer[curr_idx] = std::bit_cast<FloatType>(curr_int - prev_int);
            }
        }
    }
}

/// Apply floating point horizontal differencing encoding for non-native float types - specialized implementation
template <DeltaDecodableNonNativeFloat FloatType, std::size_t SamplesPerPixel>
inline void delta_encode_nonnative_float_impl(
    std::span<FloatType> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride) noexcept {
    
    using UIntType = std::conditional_t<std::is_same_v<FloatType, Float16>, 
                                        uint16_t, 
                                        uint32_t>;
    
    for (std::size_t y = 0; y < height; ++y) {
        std::size_t row_offset = y * stride;
        
        // Process from right to left to avoid overwriting values we need
        for (std::size_t x = width - 1; x > 0; --x) {
            for (std::size_t s = 0; s < SamplesPerPixel; ++s) {
                std::size_t curr_idx = row_offset + x * SamplesPerPixel + s;
                std::size_t prev_idx = row_offset + (x - 1) * SamplesPerPixel + s;
                
                UIntType prev_int, curr_int;
                if constexpr (std::is_same_v<FloatType, Float16>) {
                    prev_int = buffer[prev_idx].as_uint16();
                    curr_int = buffer[curr_idx].as_uint16();
                    buffer[curr_idx].from_uint16(static_cast<uint16_t>(curr_int - prev_int));
                } else { // Float24
                    prev_int = buffer[prev_idx].as_uint32();
                    curr_int = buffer[curr_idx].as_uint32();
                    buffer[curr_idx].from_uint32((curr_int - prev_int) & 0xFFFFFF);
                }
            }
        }
    }
}

template <class... T> constexpr bool always_false = false;

} // namespace detail

/// Apply horizontal differencing (TIFF predictor=2) decoding in place
/// 
/// This decodes delta-encoded data where each pixel stores the difference
/// from the previous pixel in the same row. For multi-channel images,
/// each channel is predicted separately from its own previous value.
/// 
/// @tparam T Pixel type (uint8_t, uint16_t, etc.)
/// @param buffer Buffer containing the encoded data (modified in place)
/// @param width Number of pixels per row
/// @param height Number of rows
/// @param stride Number of elements (samples) between row starts (>= width * samples_per_pixel)
/// @param samples_per_pixel Number of samples (channels) per pixel (default 1)
template <DeltaDecodableInteger T>
inline void delta_decode_horizontal(
    std::span<T> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel) noexcept {
    
    // Dispatch to optimized implementations for common cases
    switch (samples_per_pixel) {
        case 1:
            detail::delta_decode_horizontal_impl<T, 1>(buffer, width, height, stride);
            break;
        case 2:
            detail::delta_decode_horizontal_impl<T, 2>(buffer, width, height, stride);
            break;
        case 3:
            detail::delta_decode_horizontal_impl<T, 3>(buffer, width, height, stride);
            break;
        case 4:
            detail::delta_decode_horizontal_impl<T, 4>(buffer, width, height, stride);
            break;
        default:
            // Generic fallback for uncommon channel counts
            for (std::size_t y = 0; y < height; ++y) {
                std::size_t row_offset = y * stride;
                
                for (std::size_t x = 1; x < width; ++x) {
                    for (std::size_t s = 0; s < samples_per_pixel; ++s) {
                        std::size_t curr_idx = row_offset + x * samples_per_pixel + s;
                        std::size_t prev_idx = row_offset + (x - 1) * samples_per_pixel + s;
                        buffer[curr_idx] += buffer[prev_idx];
                    }
                }
            }
            break;
    }
}

template <DeltaDecodableFloat FloatType>
inline void delta_decode_floating_point(
    std::span<FloatType> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel) noexcept {
    
    if constexpr (DeltaDecodableNativeFloat<FloatType>) {
        using UIntType = std::conditional_t<std::is_same_v<FloatType, float>, 
                                            uint32_t, 
                                            uint64_t>;
        
        static_assert(sizeof(FloatType) == sizeof(UIntType));
        
        // Dispatch to optimized implementations for common cases
        switch (samples_per_pixel) {
            case 1:
                detail::delta_decode_floating_point_impl<FloatType, 1>(buffer, width, height, stride);
                break;
            case 2:
                detail::delta_decode_floating_point_impl<FloatType, 2>(buffer, width, height, stride);
                break;
            case 3:
                detail::delta_decode_floating_point_impl<FloatType, 3>(buffer, width, height, stride);
                break;
            case 4:
                detail::delta_decode_floating_point_impl<FloatType, 4>(buffer, width, height, stride);
                break;
            default:
                // Generic fallback for uncommon channel counts
                for (std::size_t y = 0; y < height; ++y) {
                    std::size_t row_offset = y * stride;
                    
                    for (std::size_t x = 1; x < width; ++x) {
                        for (std::size_t s = 0; s < samples_per_pixel; ++s) {
                            std::size_t curr_idx = row_offset + x * samples_per_pixel + s;
                            std::size_t prev_idx = row_offset + (x - 1) * samples_per_pixel + s;
                            
                            auto prev_int = std::bit_cast<UIntType>(buffer[prev_idx]);
                            auto curr_int = std::bit_cast<UIntType>(buffer[curr_idx]);
                            
                            buffer[curr_idx] = std::bit_cast<FloatType>(prev_int + curr_int);
                        }
                    }
                }
                break;
        }
    } else if constexpr (DeltaDecodableNonNativeFloat<FloatType>) {
        switch (samples_per_pixel) {
            case 1:
                detail::delta_decode_nonnative_float_impl<FloatType, 1>(buffer, width, height, stride);
                break;
            case 2:
                detail::delta_decode_nonnative_float_impl<FloatType, 2>(buffer, width, height, stride);
                break;
            case 3:
                detail::delta_decode_nonnative_float_impl<FloatType, 3>(buffer, width, height, stride);
                break;
            case 4:
                detail::delta_decode_nonnative_float_impl<FloatType, 4>(buffer, width, height, stride);
                break;
            default:
                // Generic fallback
                using UIntType = std::conditional_t<std::is_same_v<FloatType, Float16>, 
                                                    uint16_t, 
                                                    uint32_t>;
                for (std::size_t y = 0; y < height; ++y) {
                    std::size_t row_offset = y * stride;
                    
                    for (std::size_t x = 1; x < width; ++x) {
                        for (std::size_t s = 0; s < samples_per_pixel; ++s) {
                            std::size_t curr_idx = row_offset + x * samples_per_pixel + s;
                            std::size_t prev_idx = row_offset + (x - 1) * samples_per_pixel + s;
                            
                            UIntType prev_int, curr_int;
                            if constexpr (std::is_same_v<FloatType, Float16>) {
                                prev_int = buffer[prev_idx].as_uint16();
                                curr_int = buffer[curr_idx].as_uint16();
                                buffer[curr_idx].from_uint16(static_cast<uint16_t>(prev_int + curr_int));
                            } else {
                                prev_int = buffer[prev_idx].as_uint32();
                                curr_int = buffer[curr_idx].as_uint32();
                                buffer[curr_idx].from_uint32((prev_int + curr_int) & 0xFFFFFF);
                            }
                        }
                    }
                }
                break;
        }
    } else {
        static_assert(detail::always_false<FloatType>, "Unsupported FloatType for delta_decode_floating_point");
    }
}

/// Apply horizontal differencing (TIFF predictor=2) encoding in place
/// 
/// This encodes data by storing the difference between each pixel and the
/// previous pixel in the same row. For multi-channel images, each channel
/// is predicted separately from its own previous value. The first pixel in
/// each row remains unchanged.
/// 
/// @tparam T Pixel type (uint8_t, uint16_t, etc.)
/// @param buffer Buffer containing the raw data (modified in place)
/// @param width Number of pixels per row
/// @param height Number of rows
/// @param stride Number of elements (samples) between row starts (>= width * samples_per_pixel)
/// @param samples_per_pixel Number of samples (channels) per pixel (default 1)
template <DeltaDecodableInteger T>
inline void delta_encode_horizontal(
    std::span<T> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel) noexcept {
    
    // Dispatch to optimized implementations for common cases
    switch (samples_per_pixel) {
        case 1:
            detail::delta_encode_horizontal_impl<T, 1>(buffer, width, height, stride);
            break;
        case 2:
            detail::delta_encode_horizontal_impl<T, 2>(buffer, width, height, stride);
            break;
        case 3:
            detail::delta_encode_horizontal_impl<T, 3>(buffer, width, height, stride);
            break;
        case 4:
            detail::delta_encode_horizontal_impl<T, 4>(buffer, width, height, stride);
            break;
        default:
            // Generic fallback for uncommon channel counts
            for (std::size_t y = 0; y < height; ++y) {
                std::size_t row_offset = y * stride;
                
                // Process from right to left to avoid overwriting values we need
                for (std::size_t x = width - 1; x > 0; --x) {
                    for (std::size_t s = 0; s < samples_per_pixel; ++s) {
                        std::size_t curr_idx = row_offset + x * samples_per_pixel + s;
                        std::size_t prev_idx = row_offset + (x - 1) * samples_per_pixel + s;
                        buffer[curr_idx] -= buffer[prev_idx];
                    }
                }
            }
            break;
    }
}

/// Apply floating point horizontal differencing encoding in place
/// 
/// This encodes floating point data by storing the difference between each
/// pixel's bit representation and the previous pixel in the same row. The
/// differencing is done on the integer bit representation to ensure
/// lossless encoding.
/// 
/// @tparam FloatType float or double
/// @param buffer Buffer containing the raw floating point data (modified in place)
/// @param width Number of pixels per row
/// @param height Number of rows
/// @param stride Number of elements (samples) between row starts (>= width * samples_per_pixel)
/// @param samples_per_pixel Number of samples (channels) per pixel (default 1)
template <DeltaDecodableFloat FloatType>
inline void delta_encode_floating_point(
    std::span<FloatType> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel) noexcept {
    if constexpr (DeltaDecodableNativeFloat<FloatType>) {
        using UIntType = std::conditional_t<std::is_same_v<FloatType, float>, 
                                            uint32_t, 
                                            uint64_t>;
        
        static_assert(sizeof(FloatType) == sizeof(UIntType));
        
        // Dispatch to optimized implementations for common cases
        switch (samples_per_pixel) {
            case 1:
                detail::delta_encode_floating_point_impl<FloatType, 1>(buffer, width, height, stride);
                break;
            case 2:
                detail::delta_encode_floating_point_impl<FloatType, 2>(buffer, width, height, stride);
                break;
            case 3:
                detail::delta_encode_floating_point_impl<FloatType, 3>(buffer, width, height, stride);
                break;
            case 4:
                detail::delta_encode_floating_point_impl<FloatType, 4>(buffer, width, height, stride);
                break;
            default:
                // Generic fallback for uncommon channel counts
                for (std::size_t y = 0; y < height; ++y) {
                    std::size_t row_offset = y * stride;
                    
                    // Process from right to left to avoid overwriting values we need
                    for (std::size_t x = width - 1; x > 0; --x) {
                        for (std::size_t s = 0; s < samples_per_pixel; ++s) {
                            std::size_t curr_idx = row_offset + x * samples_per_pixel + s;
                            std::size_t prev_idx = row_offset + (x - 1) * samples_per_pixel + s;
                            
                            auto prev_int = std::bit_cast<UIntType>(buffer[prev_idx]);
                            auto curr_int = std::bit_cast<UIntType>(buffer[curr_idx]);
                            
                            buffer[curr_idx] = std::bit_cast<FloatType>(curr_int - prev_int);
                        }
                    }
                }
                break;
        }
    } else if constexpr (DeltaDecodableNonNativeFloat<FloatType>) {
        switch (samples_per_pixel) {
            case 1:
                detail::delta_encode_nonnative_float_impl<FloatType, 1>(buffer, width, height, stride);
                break;
            case 2:
                detail::delta_encode_nonnative_float_impl<FloatType, 2>(buffer, width, height, stride);
                break;
            case 3:
                detail::delta_encode_nonnative_float_impl<FloatType, 3>(buffer, width, height, stride);
                break;
            case 4:
                detail::delta_encode_nonnative_float_impl<FloatType, 4>(buffer, width, height, stride);
                break;
            default:
                // Generic fallback
                using UIntType = std::conditional_t<std::is_same_v<FloatType, Float16>, 
                                                    uint16_t, 
                                                    uint32_t>;
                for (std::size_t y = 0; y < height; ++y) {
                    std::size_t row_offset = y * stride;
                    
                    for (std::size_t x = width - 1; x > 0; --x) {
                        for (std::size_t s = 0; s < samples_per_pixel; ++s) {
                            std::size_t curr_idx = row_offset + x * samples_per_pixel + s;
                            std::size_t prev_idx = row_offset + (x - 1) * samples_per_pixel + s;
                            
                            UIntType prev_int, curr_int;
                            if constexpr (std::is_same_v<FloatType, Float16>) {
                                prev_int = buffer[prev_idx].as_uint16();
                                curr_int = buffer[curr_idx].as_uint16();
                                buffer[curr_idx].from_uint16(static_cast<uint16_t>(curr_int - prev_int));
                            } else {
                                prev_int = buffer[prev_idx].as_uint32();
                                curr_int = buffer[curr_idx].as_uint32();
                                buffer[curr_idx].from_uint32((curr_int - prev_int) & 0xFFFFFF);
                            }
                        }
                    }
                }
                break;
        }
    } else {
        static_assert(detail::always_false<FloatType>, "Unsupported FloatType for delta_encode_floating_point");
    }
}

} // namespace predictor

} // namespace tiffconcept
