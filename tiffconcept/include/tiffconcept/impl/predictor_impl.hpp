#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <span>
#include "../types.hpp"

#ifndef TIFFCONCEPT_PREDICTOR_HEADER
#include "../predictor.hpp" // for linters
#endif

namespace tiffconcept {

namespace predictor {

namespace detail {

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
