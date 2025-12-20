
// This file contains the implementation of ChunkDecoder.
// Do not include this file directly - it is included by decoder.hpp

#pragma once

#include <cstring>
#include <span>
#include <vector>
#include "../decompressor_base.hpp"
#include "../predictor.hpp"
#include "../result.hpp"
#include "../types.hpp"

#ifndef TIFFCONCEPT_DECODER_HEADER
#include "../decoder.hpp" // for linters
#endif

namespace tiffconcept {

// ============================================================================
// ChunkDecoder Private Member Function Implementations
// ============================================================================

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
Result<void> ChunkDecoder<PixelType, DecompSpec>::apply_predictor(
    std::span<std::byte> data,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    Predictor predictor,
    uint16_t samples_per_pixel) const noexcept {
    
    // Cast byte span to typed span
    std::span<PixelType> typed_data(
        reinterpret_cast<PixelType*>(data.data()),
        data.size() / sizeof(PixelType)
    );
    
    if (predictor == Predictor::Horizontal) {
        if constexpr (!std::is_floating_point_v<PixelType>) {
            predictor::delta_decode_horizontal(typed_data, width, height, stride, samples_per_pixel);
        }
    } else if (predictor == Predictor::FloatingPoint) {
        if constexpr (std::is_floating_point_v<PixelType>) {
            predictor::delta_decode_floating_point(typed_data, width, height, stride, samples_per_pixel);
        }
    }
    return Ok();
}

// ============================================================================
// ChunkDecoder Public Member Function Implementations
// ============================================================================

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
ChunkDecoder<PixelType, DecompSpec>::ChunkDecoder() 
    : decompressors_(), scratch_buffer_() {}

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
Result<void> ChunkDecoder<PixelType, DecompSpec>::decode_into(
    std::span<const std::byte> compressed_input,
    std::span<std::byte> decompressed_output,
    uint32_t width,
    uint32_t height,
    CompressionScheme compression,
    Predictor predictor,
    uint16_t samples_per_pixel) const noexcept {
    
    if (decompressed_output.size() < width * height * samples_per_pixel * sizeof(PixelType)) {
        return Err(Error::Code::OutOfBounds, "Insufficient decompressed output size");
    }
    
    // Decompress
    auto decompress_result = decompressors_.decompress(
        decompressed_output, 
        compressed_input, 
        compression
    );
    
    if (decompress_result.is_error()) {
        return decompress_result.error();
    }
    
    // Apply predictor decoding if needed (in-place)
    // Stride in elements is width * samples_per_pixel
    return apply_predictor(decompressed_output, width, height, width * samples_per_pixel, predictor, samples_per_pixel);
}

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
Result<std::span<const PixelType>> ChunkDecoder<PixelType, DecompSpec>::decode(
    std::span<const std::byte> compressed_input,
    uint32_t width,
    uint32_t height,
    CompressionScheme compression,
    Predictor predictor,
    uint16_t samples_per_pixel) noexcept {
    
    // Ensure scratch buffer is large enough
    std::size_t required_size = width * height * samples_per_pixel;
    if (scratch_buffer_.size() < required_size) {
        scratch_buffer_.resize(required_size);
    }
    
    // Create span over scratch buffer
    std::span<std::byte> output(
        reinterpret_cast<std::byte*>(scratch_buffer_.data()),
        required_size * sizeof(PixelType)
    );
    
    // Decode into scratch buffer
    auto result = decode_into(compressed_input, output, width, height, compression, predictor, samples_per_pixel);
    if (!result) {
        return result.error();
    }
    
    // Return span over decoded data
    return Ok(std::span<const PixelType>(scratch_buffer_.data(), required_size));
}

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
Result<std::vector<PixelType>> ChunkDecoder<PixelType, DecompSpec>::decode_copy(
    std::span<const std::byte> compressed_input,
    uint32_t width,
    uint32_t height,
    CompressionScheme compression,
    Predictor predictor,
    uint16_t samples_per_pixel) const noexcept {
    
    // Allocate output vector
    std::size_t required_size = width * height * samples_per_pixel;
    std::vector<PixelType> output(required_size);
    
    // Create span over output vector
    std::span<std::byte> output_span(
        reinterpret_cast<std::byte*>(output.data()),
        required_size * sizeof(PixelType)
    );
    
    // Decode directly into the vector
    auto result = decode_into(compressed_input, output_span, width, height, compression, predictor, samples_per_pixel);
    if (result.is_error()) {
        return result.error();
    }
    
    return Ok(std::move(output));
}

} // namespace tiffconcept