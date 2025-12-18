#pragma once

#include <cstring>
#include <span>
#include <vector>
#include "../compressor_base.hpp"
#include "../predictor.hpp"
#include "../result.hpp"
#include "../types.hpp"
#include "../strategy/write_strategy.hpp"

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
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    Predictor predictor,
    uint16_t samples_per_pixel) noexcept {
    
    if (predictor == Predictor::None) {
        // No encoding needed, return input as bytes
        return Ok(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(input_data.data()),
            input_data.size() * sizeof(PixelType)
        ));
    }
    
    // Need to copy data for encoding (predictor encoding is destructive)
    std::size_t required_size = width * height * samples_per_pixel;
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
                width, height, stride, samples_per_pixel
            );
        }
    } else if (predictor == Predictor::FloatingPoint) {
        if constexpr (std::is_floating_point_v<PixelType>) {
            predictor::delta_encode_floating_point(
                std::span<PixelType>(predictor_buffer_.data(), required_size),
                width, height, stride, samples_per_pixel
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
    uint32_t chunk_index,
    uint32_t pixel_x,
    uint32_t pixel_y,
    uint32_t pixel_z,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint16_t plane,
    CompressionScheme compression,
    Predictor predictor,
    uint16_t samples_per_pixel) noexcept {

    // Validate dimensions are non-zero
    if (width == 0 || height == 0 || depth == 0) {
        return Err(Error::Code::UnsupportedFeature,
                    "Empty chunk");
    }
    // Validate input size
    std::size_t expected_size = width * height * depth * samples_per_pixel;
    if (input_data.size() < expected_size) {
        return Err(Error::Code::OutOfBounds, 
                    "Input data too small for chunk dimensions");
    }
    
    // For 3D chunks, flatten depth into height for 2D processing
    uint32_t effective_height = height * depth;
    
    // Apply predictor encoding
    // Stride in elements is width * samples_per_pixel
    auto predictor_result = apply_predictor(
        input_data.subspan(0, expected_size),
        width, effective_height, width * samples_per_pixel,
        predictor, samples_per_pixel
    );
    
    if (!predictor_result) {
        return Err(predictor_result.error().code, predictor_result.error().message);
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
        compression
    );
    
    if (!compress_result) {
        return Err(compress_result.error().code, compress_result.error().message);
    }
    
    std::size_t compressed_size = compress_result.value();
    
    // Build EncodedChunk
    EncodedChunk chunk;
    chunk.info.chunk_index = chunk_index;
    chunk.info.pixel_x = pixel_x;
    chunk.info.pixel_y = pixel_y;
    chunk.info.pixel_z = pixel_z;
    chunk.info.width = width;
    chunk.info.height = height;
    chunk.info.depth = depth;
    chunk.info.plane = plane;
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

/// Encode a 2D chunk (convenience wrapper for common case)
template <typename PixelType, typename CompSpec>
    requires predictor::DeltaDecodable<PixelType> && ValidCompressorSpec<CompSpec>
[[nodiscard]] Result<EncodedChunk> ChunkEncoder<PixelType, CompSpec>::encode_2d(
    std::span<const PixelType> input_data,
    uint32_t chunk_index,
    uint32_t pixel_x,
    uint32_t pixel_y,
    uint32_t width,
    uint32_t height,
    uint16_t plane,
    CompressionScheme compression,
    Predictor predictor,
    uint16_t samples_per_pixel) noexcept {
    
    return encode(
        input_data,
        chunk_index,
        pixel_x, pixel_y, 0,  // z = 0
        width, height, 1,     // depth = 1
        plane,
        compression,
        predictor,
        samples_per_pixel
    );
}

/// Encode chunk using image offset calculation
/// For tiled images: calculates tile position from tile indices
/// For stripped images: calculates strip position
template <typename PixelType, typename CompSpec>
    requires predictor::DeltaDecodable<PixelType> && ValidCompressorSpec<CompSpec>
[[nodiscard]] Result<EncodedChunk> ChunkEncoder<PixelType, CompSpec>::encode_with_tile_indices(
    std::span<const PixelType> input_data,
    uint32_t chunk_index,
    uint32_t tile_x_index,
    uint32_t tile_y_index,
    uint32_t tile_z_index,
    uint32_t tile_width,
    uint32_t tile_height,
    uint32_t tile_depth,
    uint16_t plane,
    CompressionScheme compression,
    Predictor predictor,
    uint16_t samples_per_pixel) noexcept {
    
    return encode(
        input_data,
        chunk_index,
        tile_x_index * tile_width,
        tile_y_index * tile_height,
        tile_z_index * tile_depth,
        tile_width,
        tile_height,
        tile_depth,
        plane,
        compression,
        predictor,
        samples_per_pixel
    );
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
