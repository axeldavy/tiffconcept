#pragma once

#include <cstring>
#include <span>
#include <vector>
#include "../../compressors/compressor_base.hpp"
#include "../predictor.hpp"
#include "../../types/result.hpp"
#include "../../types/tiff_spec.hpp"
#include "../../types/tile_info.hpp"
//#include "../strategy/write_strategy.hpp"

#ifndef TIFFCONCEPT_ENCODER_HEADER
#include "../encoder.hpp" // for linters
#endif

namespace tiffconcept {

/// Chunk encoder - handles predictor encoding and compression for tiles and strips
/// NOT thread-safe - only one thread should use it at a time.
/// Contains scratch buffers to avoid reallocations
template <typename PixelType, typename CompSpec>
    requires predictor::DeltaDecodable<PixelType> && ValidCompressorSpec<CompSpec>
ChunkEncoder<PixelType, CompSpec>::ChunkEncoder() 
    : compressors_(), predictor_buffer_(), compressed_buffer_() {}
    
/// Apply predictor encoding (modifies data in-place)
/// Returns the span to use for compression (either original or predictor_buffer_)
template <typename PixelType, typename CompSpec>
    requires predictor::DeltaDecodable<PixelType> && ValidCompressorSpec<CompSpec>
[[nodiscard]] Result<std::span<const std::byte>> ChunkEncoder<PixelType, CompSpec>::apply_predictor(
    std::span<const PixelType> input_data,
    const TileSize& tile_size,
    Predictor predictor) noexcept {
    
    if (predictor == Predictor::None) {
        // No encoding needed, return input as bytes
        return Ok(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(input_data.data()),
            input_data.size() * sizeof(PixelType)
        ));
    }
    
    // Need to copy data for encoding (predictor encoding is destructive)
    std::size_t required_size = tile_size.width * tile_size.height * tile_size.depth * tile_size.nsamples;
    if (predictor_buffer_.size() < required_size) {
        predictor_buffer_.resize(required_size);
    }
    
    // Copy input to predictor buffer
    std::memcpy(predictor_buffer_.data(), input_data.data(), required_size * sizeof(PixelType));
    
    // Apply predictor encoding in-place
    if (predictor == Predictor::Horizontal) {
        if constexpr (!std::is_floating_point_v<PixelType>) {
            predictor::delta_encode_horizontal(
                std::span<PixelType>(predictor_buffer_.data(), required_size),
                tile_size.width,
                tile_size.height * tile_size.depth,
                tile_size.width * tile_size.nsamples,
                tile_size.nsamples
            );
        }
    } else if (predictor == Predictor::FloatingPoint) {
        if constexpr (std::is_floating_point_v<PixelType>) {
            predictor::delta_encode_floating_point(
                std::span<PixelType>(predictor_buffer_.data(), required_size),
                tile_size.width,
                tile_size.height * tile_size.depth,
                tile_size.width * tile_size.nsamples,
                tile_size.nsamples
            );
        }
    } else if (predictor == Predictor::LOCO_I) {
        if constexpr (!std::is_floating_point_v<PixelType> && sizeof(PixelType) < 8) {
            predictor::loco_i_encode(
                std::span<PixelType>(predictor_buffer_.data(), required_size),
                tile_size.width,
                tile_size.height * tile_size.depth,
                tile_size.width * tile_size.nsamples,
                tile_size.nsamples
            );
        }
    }
    
    // Return encoded data as byte span
    return Ok(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(predictor_buffer_.data()),
        required_size * sizeof(PixelType)
    ));
}

    
/// Encode chunk (tile or strip) and return as EncodedChunk
/// The chunk info will be populated with uncompressed and compressed sizes
/// The returned EncodedChunk owns the compressed data
template <typename PixelType, typename CompSpec>
    requires predictor::DeltaDecodable<PixelType> && ValidCompressorSpec<CompSpec>
[[nodiscard]] Result<EncodedChunk> ChunkEncoder<PixelType, CompSpec>::encode(
    std::span<const PixelType> input_data,
    const TileDescriptor& tile_desc,
    CompressionScheme compression,
    Predictor predictor) noexcept {

    const TileSize& tile_size = tile_desc.size;

    // Validate dimensions are non-zero
    if (tile_size.width == 0 || tile_size.height == 0 || tile_size.depth == 0) [[unlikely]] {
        return Err(Error::Code::UnsupportedFeature,
                    "Empty chunk");
    }
    // Validate input size
    std::size_t expected_size = tile_size.width * tile_size.height * tile_size.depth * tile_size.nsamples;
    if (input_data.size() < expected_size) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, 
                    "Input data too small for chunk dimensions");
    }
    
    // Apply predictor encoding
    // Stride in elements is width * samples_per_pixel
    auto predictor_result = apply_predictor(
        input_data.subspan(0, expected_size),
        tile_size,
        predictor
    );
    
    if (predictor_result.is_error()) [[unlikely]] {
        return predictor_result.error();
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
    
    std::span<const std::byte> encoded_data = predictor_result.value();
    std::size_t uncompressed_size = encoded_data.size();
    
    // Compress
    // Clear the compressed buffer and compress into it
    compressed_buffer_.clear();
    
    auto compress_result = compressors_.compress(
        compressed_buffer_,
        0,  // offset = 0 since we cleared the buffer
        encoded_data,
        compression,
        tile_size,
        std::span<const SampleFormat>(sample_formats),
        std::span<const uint8_t>(bits_per_sample),
        std::endian::native
    );
    
    if (compress_result.is_error()) [[unlikely]] {
        return compress_result.error();
    }
    
    std::size_t compressed_size = compress_result.value();
    
    // Build EncodedChunk
    EncodedChunk chunk;
    chunk.info.chunk_descriptor = tile_desc;
    chunk.info.uncompressed_size = uncompressed_size;
    chunk.info.compressed_size = compressed_size;
    chunk.info.file_offset = 0;  // Not yet known
    
    // Move compressed data (only the used portion)
    chunk.data = std::vector<std::byte>(
        compressed_buffer_.begin(),
        compressed_buffer_.begin() + compressed_size
    );
    
    return Ok(std::move(chunk));
}

/// Clear scratch buffers (for memory management)
template <typename PixelType, typename CompSpec>
    requires predictor::DeltaDecodable<PixelType> && ValidCompressorSpec<CompSpec>
void ChunkEncoder<PixelType, CompSpec>::clear() noexcept {
    predictor_buffer_.clear();
    predictor_buffer_.shrink_to_fit();
    compressed_buffer_.clear();
    compressed_buffer_.shrink_to_fit();
}

/// Get current scratch buffer sizes (for diagnostics)
template <typename PixelType, typename CompSpec>
    requires predictor::DeltaDecodable<PixelType> && ValidCompressorSpec<CompSpec>
[[nodiscard]] std::pair<std::size_t, std::size_t> ChunkEncoder<PixelType, CompSpec>::buffer_sizes() const noexcept {
    return {
        predictor_buffer_.capacity() * sizeof(PixelType),
        compressed_buffer_.capacity()
    };
}

} // namespace tiffconcept
