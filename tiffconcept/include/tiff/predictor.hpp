#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <span>

namespace tiff {

/// Concept for types that can be delta decoded
template <typename T>
concept DeltaDecodable = std::is_arithmetic_v<T> && 
                         (std::is_same_v<T, uint8_t> || 
                          std::is_same_v<T, uint16_t> ||
                          std::is_same_v<T, uint32_t> ||
                          std::is_same_v<T, uint64_t> ||
                          std::is_same_v<T, int8_t> ||
                          std::is_same_v<T, int16_t> ||
                          std::is_same_v<T, int32_t> ||
                          std::is_same_v<T, int64_t> ||
                          std::is_same_v<T, float> ||
                          std::is_same_v<T, double>); // there exists also float16 and float24 but we don't support them natively

/// Apply horizontal differencing (TIFF predictor=2) decoding in place - specialized implementation
/// 
/// @tparam T Pixel type (uint8_t, uint16_t, etc.)
/// @tparam SamplesPerPixel Number of samples per pixel (compile-time constant for optimization)
template <DeltaDecodable T, std::size_t SamplesPerPixel>
inline void delta_decode_horizontal_impl(
    std::span<T> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride) noexcept {
    
    for (std::size_t y = 0; y < height; ++y) {
        std::size_t row_offset = y * stride;
        
        // Unrolled loop for each channel - compiler can optimize better
        for (std::size_t x = 1; x < width; ++x) {
            for (std::size_t s = 0; s < SamplesPerPixel; ++s) {
                std::size_t curr_idx = row_offset + x * SamplesPerPixel + s;
                std::size_t prev_idx = row_offset + (x - 1) * SamplesPerPixel + s;
                buffer[curr_idx] += buffer[prev_idx];
            }
        }
    }
}

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
template <DeltaDecodable T>
inline void delta_decode_horizontal(
    std::span<T> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel = 1) noexcept {
    
    // Dispatch to optimized implementations for common cases
    switch (samples_per_pixel) {
        case 1:
            delta_decode_horizontal_impl<T, 1>(buffer, width, height, stride);
            break;
        case 2:
            delta_decode_horizontal_impl<T, 2>(buffer, width, height, stride);
            break;
        case 3:
            delta_decode_horizontal_impl<T, 3>(buffer, width, height, stride);
            break;
        case 4:
            delta_decode_horizontal_impl<T, 4>(buffer, width, height, stride);
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

/// Apply floating point horizontal differencing - specialized implementation
/// 
/// @tparam FloatType float or double
/// @tparam SamplesPerPixel Number of samples per pixel (compile-time constant for optimization)
template <typename FloatType, std::size_t SamplesPerPixel>
    requires std::is_same_v<FloatType, float> || std::is_same_v<FloatType, double>
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

template <typename FloatType>
    requires std::is_same_v<FloatType, float> || std::is_same_v<FloatType, double>
inline void delta_decode_floating_point(
    std::span<FloatType> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel = 1) noexcept {
    
    using UIntType = std::conditional_t<std::is_same_v<FloatType, float>, 
                                        uint32_t, 
                                        uint64_t>;
    
    static_assert(sizeof(FloatType) == sizeof(UIntType));
    
    // Dispatch to optimized implementations for common cases
    switch (samples_per_pixel) {
        case 1:
            delta_decode_floating_point_impl<FloatType, 1>(buffer, width, height, stride);
            break;
        case 2:
            delta_decode_floating_point_impl<FloatType, 2>(buffer, width, height, stride);
            break;
        case 3:
            delta_decode_floating_point_impl<FloatType, 3>(buffer, width, height, stride);
            break;
        case 4:
            delta_decode_floating_point_impl<FloatType, 4>(buffer, width, height, stride);
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
}

} // namespace tiff
