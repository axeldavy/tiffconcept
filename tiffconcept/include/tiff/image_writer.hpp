#pragma once

#include <algorithm>
#include <span>
#include <vector>
#include "chunk_info.hpp"
#include "encoder.hpp"
#include "ifd_builder.hpp"
#include "image_shape.hpp"
#include "result.hpp"
#include "tag_spec.hpp"
#include "tiling.hpp"
#include "types.hpp"
#include "write_strategy.hpp"

namespace tiff {

/// Information returned after writing image data
struct WrittenImageInfo {
    std::vector<uint64_t> tile_offsets;      // File offsets of each tile/strip
    std::vector<uint64_t> tile_byte_counts;  // Compressed sizes of each tile/strip
    std::size_t total_data_size;             // Total bytes written for image data
    std::size_t image_data_start_offset;     // File offset where image data starts
};

/// Helper to calculate chunk layout for tiled or stripped images
struct ChunkLayout {
    std::vector<ChunkWriteInfo> chunks;
    uint32_t chunks_across;    // Number of chunks in X direction
    uint32_t chunks_down;      // Number of chunks in Y direction
    uint32_t chunks_deep;      // Number of chunks in Z direction (usually 1)
    uint16_t num_planes;       // Number of planes (1 for chunky, samples_per_pixel for planar)
    
    /// Create layout for tiled image
    [[nodiscard]] static Result<ChunkLayout> create_tiled(
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
    [[nodiscard]] static Result<ChunkLayout> create_stripped(
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
};

/// Image writer - coordinates encoding and writing of image data
/// Uses configurable write strategies for flexible layout control
template <
    typename PixelType,
    typename CompSpec,
    typename WriteConfig_,
    TiffFormatType TiffFormat = TiffFormatType::Classic,
    std::endian TargetEndian = std::endian::native
>
    requires ValidCompressorSpec<CompSpec> &&
             predictor::DeltaDecodable<PixelType>
class ImageWriter {
public:
    using WriteConfig = WriteConfig_;
    using IFDPlacement = typename WriteConfig::ifd_placement_strategy;
    using TileOrdering = typename WriteConfig::tile_ordering_strategy;
    using BufferingStrat = typename WriteConfig::buffering_strategy;
    using OffsetResolution = typename WriteConfig::offset_resolution_strategy;
    
private:
    ChunkEncoder<PixelType, CompSpec> encoder_;
    TileOrdering ordering_strategy_;
    OffsetResolution offset_strategy_;
    
public:
    ImageWriter() = default;
    
    /// Write complete image to file
    /// Returns offset and size information for creating IFD tags
    /// @tparam InputSpec Layout of input_data buffer (DHWC, DCHW, or CDHW)
    template <OutputSpec InputSpec, typename Writer>
        requires RawWriter<Writer>
    [[nodiscard]] Result<WrittenImageInfo> write_image(
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
        
        if (input_data.size() < expected_samples) {
            return Err(Error::Code::OutOfBounds, "Input data size too small for image dimensions");
        }
        
        // Create chunk layout
        auto layout_result = ChunkLayout::create_tiled(
            image_width, image_height, image_depth,
            tile_width, tile_height, tile_depth,
            samples_per_pixel, planar_config
        );
        
        if (!layout_result) {
            return Err(layout_result.error().code, layout_result.error().message);
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
                    0,
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
            
            if (!encoded_result) {
                return Err(encoded_result.error().code, encoded_result.error().message);
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
            
            if (!write_result) {
                return Err(write_result.error().code, "Failed to write chunk data");
            }
            
            current_offset += encoded_chunk.info.compressed_size;
        }
        
        // Flush buffered data
        auto flush_result = buffering_strategy.flush(writer);
        if (!flush_result) {
            return Err(flush_result.error().code, "Failed to flush buffered data");
        }
        
        info.total_data_size = current_offset - data_start_offset;
        
        return Ok(std::move(info));
    }
    
    /// Write stripped image (convenience wrapper)
    template <OutputSpec InputSpec, typename Writer>
        requires RawWriter<Writer>
    [[nodiscard]] Result<WrittenImageInfo> write_stripped_image(
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

        if (image_height % rows_per_strip != 0) {
            return Err(Error::Code::UnsupportedFeature, "Current limitation: rows per strip must evenly divide image height");
        }
        
        return write_image<OutputSpec::DHWC>(
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
    void clear() noexcept {
        encoder_.clear();
    }
};

} // namespace tiff
