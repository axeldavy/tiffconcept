#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <span>
#include "../types/tiff_spec.hpp"

namespace tiffconcept {

namespace predictor {

/// Concept for types that can be delta decoded
template <typename T>
concept DeltaDecodableInteger = std::is_same_v<T, uint8_t> || 
                                std::is_same_v<T, uint16_t> ||
                                std::is_same_v<T, uint32_t> ||
                                std::is_same_v<T, uint64_t> ||
                                std::is_same_v<T, int8_t> ||
                                std::is_same_v<T, int16_t> ||
                                std::is_same_v<T, int32_t> ||
                                std::is_same_v<T, int64_t>;

template <typename T>
concept DeltaDecodableNativeFloat = std::is_same_v<T, float> ||
                                    std::is_same_v<T, double>;

template <typename T>
concept DeltaDecodableNonNativeFloat = std::is_same_v<T, Float16> ||
                                       std::is_same_v<T, Float24>;

template <typename T>
concept DeltaDecodableFloat = DeltaDecodableNativeFloat<T> ||
                              DeltaDecodableNonNativeFloat<T>;

template <typename T>
concept DeltaDecodable = DeltaDecodableInteger<T> ||
                         DeltaDecodableFloat<T>;

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
void delta_decode_horizontal(
    std::span<T> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel = 1) noexcept;

template <DeltaDecodableFloat FloatType>
void delta_decode_floating_point(
    std::span<FloatType> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel = 1) noexcept;

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
void delta_encode_horizontal(
    std::span<T> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel = 1) noexcept;

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
void delta_encode_floating_point(
    std::span<FloatType> buffer,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    std::size_t samples_per_pixel = 1) noexcept;

} // namespace predictor

} // namespace tiffconcept

#define TIFFCONCEPT_PREDICTOR_HEADER
#include "impl/predictor_impl.hpp"
