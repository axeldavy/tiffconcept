
// This file contains the implementation of TileDecoder.
// Do not include this file directly - it is included by decoder.hpp

#pragma once

#include <cstring>
#include <span>
#include <vector>
#include "../../decompressors/decompressor_base.hpp"
#include "../predictor.hpp"
#include "../../types/result.hpp"
#include "../../types/tiff_spec.hpp"
#include "../../types/tile_info.hpp"

#ifndef TIFFCONCEPT_DECODER_HEADER
#include "../decoder.hpp" // for linters
#endif

namespace tiffconcept {

// ============================================================================
// TileDecoder Private Member Function Implementations
// ============================================================================

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
Result<void> TileDecoder<PixelType, DecompSpec>::apply_predictor(
    std::span<std::byte> data,
    const TileSize& tile_size,
    Predictor predictor) const noexcept {
    
    // Cast byte span to typed span
    std::span<PixelType> typed_data(
        reinterpret_cast<PixelType*>(data.data()),
        data.size() / sizeof(PixelType)
    );
    
    if (predictor == Predictor::Horizontal) {
        if constexpr (!std::is_floating_point_v<PixelType>) {
            predictor::delta_decode_horizontal(
                typed_data,
                tile_size.width,
                tile_size.height * tile_size.depth,
                tile_size.width * tile_size.nsamples,
                tile_size.nsamples);
        }
    } else if (predictor == Predictor::FloatingPoint) {
        if constexpr (std::is_floating_point_v<PixelType>) {
            predictor::delta_decode_floating_point(
                typed_data,
                tile_size.width,
                tile_size.height * tile_size.depth,
                tile_size.width * tile_size.nsamples,
                tile_size.nsamples);
        }
    } else if (predictor == Predictor::LOCO_I) {
        if constexpr (!std::is_floating_point_v<PixelType> && sizeof(PixelType) < 8) {
            predictor::loco_i_decode(
                typed_data,
                tile_size.width,
                tile_size.height * tile_size.depth,
                tile_size.width * tile_size.nsamples,
                tile_size.nsamples);
        }
    }
    return Ok();
}

// ============================================================================
// TileDecoder Public Member Function Implementations
// ============================================================================

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
TileDecoder<PixelType, DecompSpec>::TileDecoder() 
    : decompressors_(), scratch_buffer_() {}

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
inline Result<void> TileDecoder<PixelType, DecompSpec>::decode_into_impl(
    std::span<const std::byte> compressed_input,
    std::span<std::byte> decompressed_output,
    const TileSize& tile_size,
    CompressionScheme compression,
    Predictor predictor) const noexcept {

    if (decompressed_output.size() < tile_size.width * tile_size.height * tile_size.nsamples * sizeof(PixelType)) {
        return Err(Error::Code::OutOfBounds, "Insufficient decompressed output size");
    }

    // construct sample formats and bits per sample array from PixelType
    // (assuming all samples have the same format and bit depth)
    std::vector<SampleFormat> sample_formats(tile_size.nsamples);
    std::vector<uint8_t> bits_per_sample(tile_size.nsamples);
    if constexpr (std::is_floating_point_v<PixelType>) {
        for (auto& fmt : sample_formats) {
            fmt = SampleFormat::IEEEFloat;
        }
    } else if constexpr (std::is_signed_v<PixelType>) {
        for (auto& fmt : sample_formats) {
            fmt = SampleFormat::SignedInt;
        }
    } else {
        for (auto& fmt : sample_formats) {
            fmt = SampleFormat::UnsignedInt;
        }
    }
    uint8_t bit_depth = static_cast<uint8_t>(sizeof(PixelType) * 8);
    for (auto& bps : bits_per_sample) {
        bps = bit_depth;
    }
    
    // Decompress
    auto decompress_result = decompressors_.decompress(
        decompressed_output, 
        compressed_input, 
        compression,
        tile_size,
        std::span<const SampleFormat>(sample_formats),
        std::span<const uint8_t>(bits_per_sample),
        std::endian::native
    );
    
    if (decompress_result.is_error()) [[unlikely]] {
        return decompress_result.error();
    }
    
    // Apply predictor decoding if needed (in-place)
    // Stride in elements is width * nsamples
    return apply_predictor(decompressed_output, tile_size, predictor);
}

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
inline Result<void> TileDecoder<PixelType, DecompSpec>::decode_into(
    std::span<const std::byte> compressed_input,
    std::span<std::byte> decompressed_output,
    const TileSize& tile_size,
    CompressionScheme compression,
    Predictor predictor) const noexcept {

    std::lock_guard<std::mutex> lock(safety_mutex_);
    
    if (decompressed_output.size() < tile_size.width * tile_size.height * tile_size.nsamples * sizeof(PixelType)) {
        return Err(Error::Code::OutOfBounds, "Insufficient decompressed output size");
    }
    
    return decode_into_impl(
        compressed_input,
        decompressed_output,
        tile_size,
        compression,
        predictor
    );
}

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
inline Result<std::span<const PixelType>> TileDecoder<PixelType, DecompSpec>::decode(
    std::span<const std::byte> compressed_input,
    const TileSize& tile_size,
    CompressionScheme compression,
    Predictor predictor) noexcept {
    std::lock_guard<std::mutex> lock(safety_mutex_);
    
    // Ensure scratch buffer is large enough
    std::size_t required_size = tile_size.width * tile_size.height * tile_size.depth * tile_size.nsamples;
    if (scratch_buffer_.size() < required_size) {
        scratch_buffer_.resize(required_size);
    }
    
    // Create span over scratch buffer
    std::span<std::byte> output(
        reinterpret_cast<std::byte*>(scratch_buffer_.data()),
        required_size * sizeof(PixelType)
    );
    
    // Decode into scratch buffer
    auto result = decode_into_impl(compressed_input, output, tile_size, compression, predictor);
    if (result.is_error()) [[unlikely]] {
        return result.error();
    }
    
    // Return span over decoded data
    return Ok(std::span<const PixelType>(scratch_buffer_.data(), required_size));
}

template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
inline Result<std::vector<PixelType>> TileDecoder<PixelType, DecompSpec>::decode_copy(
    std::span<const std::byte> compressed_input,
    const TileSize& tile_size,
    CompressionScheme compression,
    Predictor predictor) const noexcept {
    std::lock_guard<std::mutex> lock(safety_mutex_);
    
    // Allocate output vector
    std::size_t required_size = tile_size.width * tile_size.height * tile_size.depth * tile_size.nsamples;
    std::vector<PixelType> output(required_size);
    
    // Create span over output vector
    std::span<std::byte> output_span(
        reinterpret_cast<std::byte*>(output.data()),
        required_size * sizeof(PixelType)
    );
    
    // Decode directly into the vector
    auto result = decode_into_impl(compressed_input, output_span, tile_size, compression, predictor);
    if (result.is_error()) [[unlikely]] {
        return result.error();
    }
    
    return Ok(std::move(output));
}

} // namespace tiffconcept