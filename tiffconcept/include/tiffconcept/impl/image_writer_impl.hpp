// Do not include this file directly. Include "image_writer.hpp" instead.

#pragma once

#include <algorithm>
#include <span>
#include <vector>
#include "../strategy/chunk_strategy.hpp"
#include "../encoder.hpp"
#include "../ifd_builder.hpp"
#include "../image_shape.hpp"
#include "../result.hpp"
#include "../tag_spec.hpp"
#include "../tiling.hpp"
#include "../types.hpp"
#include "../strategy/write_strategy.hpp"


#ifndef TIFFCONCEPT_IMAGE_WRITER_HEADER
#include "../image_writer.hpp" // for linters
#endif

namespace tiffconcept {


/// Create layout for tiled image
[[nodiscard]] inline Result<ChunkLayout> ChunkLayout::create_tiled(
    uint32_t image_width,
    uint32_t image_height,
    uint32_t image_depth,
    uint32_t tile_width,
    uint32_t tile_height,
    uint32_t tile_depth,
    uint16_t samples_per_pixel,
    PlanarConfiguration planar_config) noexcept {
    
    ChunkLayout layout;
    
    layout.chunks_across = (image_width + tile_width - 1) / tile_width;
    layout.chunks_down = (image_height + tile_height - 1) / tile_height;
    layout.chunks_deep = (image_depth + tile_depth - 1) / tile_depth;
    layout.num_planes = (planar_config == PlanarConfiguration::Planar) ? samples_per_pixel : 1;
    
    uint32_t total_chunks = layout.chunks_across * layout.chunks_down * layout.chunks_deep * layout.num_planes;
    layout.chunks.reserve(total_chunks);
    
    uint32_t chunk_index = 0;
    for (uint16_t plane = 0; plane < layout.num_planes; ++plane) {
        for (uint32_t z = 0; z < layout.chunks_deep; ++z) {
            for (uint32_t y = 0; y < layout.chunks_down; ++y) {
                for (uint32_t x = 0; x < layout.chunks_across; ++x) {
                    ChunkWriteInfo chunk;
                    chunk.chunk_index = chunk_index++;
                    chunk.pixel_x = x * tile_width;
                    chunk.pixel_y = y * tile_height;
                    chunk.pixel_z = z * tile_depth;
                    chunk.width = std::min(tile_width, image_width - chunk.pixel_x);
                    chunk.height = std::min(tile_height, image_height - chunk.pixel_y);
                    chunk.depth = std::min(tile_depth, image_depth - chunk.pixel_z);
                    chunk.plane = plane;
                    chunk.uncompressed_size = 0;  // Will be set during encoding
                    chunk.compressed_size = 0;
                    chunk.file_offset = 0;
                    
                    layout.chunks.push_back(chunk);
                }
            }
        }
    }
    
    return Ok(std::move(layout));
}

/// Create layout for stripped image
[[nodiscard]] inline Result<ChunkLayout> ChunkLayout::create_stripped(
    uint32_t image_width,
    uint32_t image_height,
    uint32_t rows_per_strip,
    uint16_t samples_per_pixel,
    PlanarConfiguration planar_config) noexcept {
    
    return create_tiled(
        image_width,
        image_height,
        1,  // depth = 1 for 2D images
        image_width,  // strip width = full image width
        rows_per_strip,
        1,  // strip depth = 1
        samples_per_pixel,
        planar_config
    );
}

/// Image writer - coordinates encoding and writing of image data
/// Uses configurable write strategies for flexible layout control
template <
    typename PixelType,
    typename CompSpec,
    typename WriteConfig_,
    TiffFormatType TiffFormat,
    std::endian TargetEndian
>
    requires ValidCompressorSpec<CompSpec> &&
             predictor::DeltaDecodable<PixelType>
template <ImageLayoutSpec InputSpec, typename Writer>
    requires RawWriter<Writer>
inline Result<WrittenImageInfo> ImageWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::write_image(
    Writer& writer,
    std::span<const PixelType> input_data,
    uint32_t image_width,
    uint32_t image_height,
    uint32_t image_depth,
    uint32_t tile_width,
    uint32_t tile_height,
    uint32_t tile_depth,
    uint16_t samples_per_pixel,
    PlanarConfiguration planar_config,
    CompressionScheme compression,
    Predictor predictor,
    std::size_t data_start_offset) noexcept {
    
    // Validate input size
    std::size_t expected_samples = static_cast<std::size_t>(image_width) * image_height * image_depth * samples_per_pixel;
    
    if (input_data.size() < expected_samples) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Input data size too small for image dimensions");
    }
    
    // Create chunk layout
    auto layout_result = ChunkLayout::create_tiled(
        image_width, image_height, image_depth,
        tile_width, tile_height, tile_depth,
        samples_per_pixel, planar_config
    );
    
    if (layout_result.is_error()) [[unlikely]] {
        return layout_result.error();
    }
    
    auto layout = std::move(layout_result.value());
    
    // Apply tile ordering strategy
    ordering_strategy_.order_chunks(std::span<ChunkWriteInfo>(layout.chunks));
    
    // Encode all chunks
    std::vector<EncodedChunk> encoded_chunks;
    encoded_chunks.reserve(layout.chunks.size());
    
    std::vector<PixelType> tile_buffer;  // Reused for extracting tile data
    
    uint16_t effective_samples = (planar_config == PlanarConfiguration::Planar) ? 1 : samples_per_pixel;
    
    for (const auto& chunk_info : layout.chunks) {
        // Calculate tile size (full tile dimensions, not actual chunk dimensions)
        std::size_t tile_size = static_cast<std::size_t>(tile_width) * tile_height * tile_depth * effective_samples;
        tile_buffer.resize(tile_size);


        // TODO: tiled and strips differ for the strategy of the last tile/strip
        // Tiles are always full size, but the height of the last strip may be less than rows_per_strip
        // For now we assume full tiles only and have asserts for stripped images in such cases

        // Use fetch_tile_from_buffer to extract and pad tile data
        if (planar_config == PlanarConfiguration::Planar && effective_samples == 1) {
            fetch_tile_from_buffer<PlanarConfiguration::Planar, InputSpec, PixelType>(
                input_data,
                std::span<PixelType>(tile_buffer),
                image_depth,
                image_height,
                image_width,
                samples_per_pixel,
                chunk_info.pixel_z,
                chunk_info.pixel_y,
                chunk_info.pixel_x,
                chunk_info.plane,
                tile_depth,
                tile_height,
                tile_width,
                effective_samples
            );
        } else {
            fetch_tile_from_buffer<PlanarConfiguration::Chunky, InputSpec, PixelType>(
                input_data,
                std::span<PixelType>(tile_buffer),
                image_depth,
                image_height,
                image_width,
                samples_per_pixel,
                chunk_info.pixel_z,
                chunk_info.pixel_y,
                chunk_info.pixel_x,
                chunk_info.plane,
                tile_depth,
                tile_height,
                tile_width,
                effective_samples
            );
        }
        
        // Encode chunk (use actual chunk dimensions for encoding)
        auto encoded_result = encoder_.encode(
            std::span<const PixelType>(tile_buffer),
            chunk_info.chunk_index,
            0,  // Tile is already extracted, so starts at 0
            0,
            0,
            tile_width,   // Use full tile dimensions
            tile_height,
            tile_depth,
            chunk_info.plane,
            compression,
            predictor,
            effective_samples
        );
        
        if (encoded_result.is_error()) [[unlikely]] {
            return encoded_result.error();
        }
        
        encoded_chunks.push_back(std::move(encoded_result.value()));
    }
    
    // Write chunks to file using buffering strategy
    BufferingStrat buffering_strategy;
    std::size_t current_offset = data_start_offset;
    
    WrittenImageInfo info;
    info.tile_offsets.resize(encoded_chunks.size());
    info.tile_byte_counts.resize(encoded_chunks.size());
    info.image_data_start_offset = data_start_offset;
    
    for (auto& encoded_chunk : encoded_chunks) {
        // Record offset
        info.tile_offsets[encoded_chunk.info.chunk_index] = current_offset;
        info.tile_byte_counts[encoded_chunk.info.chunk_index] = encoded_chunk.info.compressed_size;
        
        // Write chunk data
        auto write_result = buffering_strategy.write(
            writer,
            current_offset,
            std::span<const std::byte>(encoded_chunk.data)
        );
        
        if (write_result.is_error()) [[unlikely]] {
            return Err(write_result.error().code, "Failed to write chunk data: " + write_result.error().message);
        }
        
        current_offset += encoded_chunk.info.compressed_size;
    }
    
    // Flush buffered data
    auto flush_result = buffering_strategy.flush(writer);
    if (flush_result.is_error()) [[unlikely]] {
        return Err(flush_result.error().code, "Failed to flush buffered data: " + flush_result.error().message);
    }
    
    info.total_data_size = current_offset - data_start_offset;
    
    return Ok(std::move(info)); // TODO: is move not a counter pattern here ?
}

/// Write stripped image (convenience wrapper)
template <
    typename PixelType,
    typename CompSpec,
    typename WriteConfig_,
    TiffFormatType TiffFormat,
    std::endian TargetEndian
>
    requires ValidCompressorSpec<CompSpec> &&
             predictor::DeltaDecodable<PixelType>
template <ImageLayoutSpec InputSpec, typename Writer>
    requires RawWriter<Writer>
inline Result<WrittenImageInfo> ImageWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::write_stripped_image(
    Writer& writer,
    std::span<const PixelType> input_data,
    uint32_t image_width,
    uint32_t image_height,
    uint32_t rows_per_strip,
    uint16_t samples_per_pixel,
    PlanarConfiguration planar_config,
    CompressionScheme compression,
    Predictor predictor,
    std::size_t data_start_offset) noexcept {

    if (image_height % rows_per_strip != 0) [[unlikely]] {
        return Err(Error::Code::UnsupportedFeature, "Current limitation: rows per strip must evenly divide image height");
    }
    
    return write_image<ImageLayoutSpec::DHWC>(
        writer,
        input_data,
        image_width,
        image_height,
        1,  // depth
        image_width,  // tile_width = full width
        rows_per_strip,
        1,  // tile_depth
        samples_per_pixel,
        planar_config,
        compression,
        predictor,
        data_start_offset
    );
}

/// Clear encoder state (for memory management)
template <
    typename PixelType,
    typename CompSpec,
    typename WriteConfig_,
    TiffFormatType TiffFormat,
    std::endian TargetEndian
>
    requires ValidCompressorSpec<CompSpec> &&
             predictor::DeltaDecodable<PixelType>
inline void ImageWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::clear() noexcept {
    encoder_.clear();
}

} // namespace tiffconcept
