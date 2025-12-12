#pragma once

#include <algorithm>
#include <mutex>
#include <span>
#include <vector>
#include "chunk_info.hpp"
#include "decoder.hpp"
#include "decompressor_base.hpp"
#include "image_shape.hpp"
#include "read_strategy.hpp"
#include "reader_base.hpp"
#include "result.hpp"
#include "strips.hpp"
#include "tiling.hpp"

namespace tiff {

/// Chunk processor that decodes and copies chunks to output buffer
/// Thread-safe processor with internal decoder and mutex
template <OutputSpec OutSpec, typename PixelType, typename DecompSpec, typename ImageInfo>
    requires ValidDecompressorSpec<DecompSpec>
class ChunkDecoderProcessor {
private:
    ChunkDecoder<PixelType, DecompSpec>& decoder_;
    const ImageInfo& image_info_;
    std::span<PixelType> output_buffer_;
    const ImageRegion& region_;
    uint16_t samples_per_pixel_;
    mutable std::mutex process_mutex_;  // Thread-safe processing
    
public:
    ChunkDecoderProcessor(
        ChunkDecoder<PixelType, DecompSpec>& decoder,
        const ImageInfo& image_info,
        std::span<PixelType> output_buffer,
        const ImageRegion& region,
        uint16_t samples_per_pixel) noexcept
        : decoder_(decoder)
        , image_info_(image_info)
        , output_buffer_(output_buffer)
        , region_(region)
        , samples_per_pixel_(samples_per_pixel) {}
    
    /// Thread-safe processing of chunk batches
    [[nodiscard]] Result<void> process(std::span<const ChunkData> chunk_data_batch) noexcept {
        // Lock for thread-safe access to decoder and output buffer
        std::lock_guard<std::mutex> lock(process_mutex_);
        
        // Process each chunk in the batch: decode + copy to output
        for (const auto& chunk_data : chunk_data_batch) {
            const auto& info = chunk_data.info;
            
            // Decode the compressed chunk data
            auto decode_result = decoder_.decode(
                chunk_data.data,
                info.width,
                info.height * info.depth,
                image_info_.compression(),
                image_info_.predictor(),
                samples_per_pixel_
            );
            
            if (!decode_result) {
                return Err(decode_result.error().code, decode_result.error().message);
            }
            
            auto decoded_span = decode_result.value();
            
            // Copy decoded data to output buffer
            auto copy_result = copy_decoded_chunk_to_buffer(
                info, decoded_span
            );
            
            if (!copy_result) {
                return copy_result;
            }
        }
        
        return Ok();
    }
    
private:
    /// Copy a decoded chunk to the output buffer
    [[nodiscard]] Result<void> copy_decoded_chunk_to_buffer(
        const ChunkInfo& info,
        std::span<const PixelType> decoded_data) const noexcept {
        
        PlanarConfiguration planar_config = image_info_.shape().planar_configuration();
        
        // Calculate intersection between chunk and region
        uint32_t chunk_src_start_x = (region_.start_x > info.pixel_x) ? 
            (region_.start_x - info.pixel_x) : 0;
        uint32_t chunk_src_start_y = (region_.start_y > info.pixel_y) ? 
            (region_.start_y - info.pixel_y) : 0;
        uint32_t chunk_src_start_z = (region_.start_z > info.pixel_z) ? 
            (region_.start_z - info.pixel_z) : 0;
        
        uint32_t output_start_x = (info.pixel_x > region_.start_x) ? 
            (info.pixel_x - region_.start_x) : 0;
        uint32_t output_start_y = (info.pixel_y > region_.start_y) ? 
            (info.pixel_y - region_.start_y) : 0;
        uint32_t output_start_z = (info.pixel_z > region_.start_z) ? 
            (info.pixel_z - region_.start_z) : 0;
        
        uint32_t copy_width = std::min(
            info.width - chunk_src_start_x,
            region_.width - output_start_x
        );
        
        uint32_t copy_height = std::min(
            info.height - chunk_src_start_y,
            region_.height - output_start_y
        );
        
        uint32_t copy_depth = std::min(
            info.depth - chunk_src_start_z,
            region_.depth - output_start_z
        );
        
        // Determine output channel offset for planar configuration
        uint16_t output_channel_offset = 0;
        if (planar_config == PlanarConfiguration::Planar) {
            output_channel_offset = info.plane - region_.start_channel;
        }
        
        // Copy chunk data to output buffer
        if (planar_config == PlanarConfiguration::Planar) {
            copy_tile_to_buffer<PlanarConfiguration::Planar, OutSpec>(
                decoded_data, output_buffer_,
                info.depth, info.height, info.width, 1,
                chunk_src_start_z, chunk_src_start_y, chunk_src_start_x, 0,
                region_.depth, region_.height, region_.width, region_.num_channels,
                output_start_z, output_start_y, output_start_x, output_channel_offset,
                copy_depth, copy_height, copy_width, 1
            );
        } else {
            uint16_t samples_per_chunk = image_info_.shape().samples_per_pixel();
            uint16_t chunk_src_start_sample = region_.start_channel;
            uint16_t copy_nchans = region_.num_channels;
            
            copy_tile_to_buffer<PlanarConfiguration::Chunky, OutSpec>(
                decoded_data, output_buffer_,
                info.depth, info.height, info.width, samples_per_chunk,
                chunk_src_start_z, chunk_src_start_y, chunk_src_start_x, chunk_src_start_sample,
                region_.depth, region_.height, region_.width, region_.num_channels,
                output_start_z, output_start_y, output_start_x, 0,
                copy_depth, copy_height, copy_width, copy_nchans
            );
        }
        
        return Ok();
    }
};

/// Strategy-based image reader that can handle both tiled and stripped images
/// Uses a producer/consumer model: read strategy reads chunks and passes them to decoder
/// 
/// @tparam PixelType The pixel data type (uint8_t, uint16_t, float, etc.)
/// @tparam DecompSpec Decompressor specification
/// @tparam ReadStrat Read strategy (e.g., SingleThreadedReader, BatchedReader)
template <typename PixelType, typename DecompSpec, typename ReadStrat>
    requires (std::is_same_v<PixelType, uint8_t> || 
              std::is_same_v<PixelType, uint16_t> ||
              std::is_same_v<PixelType, uint32_t> ||
              std::is_same_v<PixelType, uint64_t> ||
              std::is_same_v<PixelType, int8_t> ||
              std::is_same_v<PixelType, int16_t> ||
              std::is_same_v<PixelType, int32_t> ||
              std::is_same_v<PixelType, int64_t> ||
              std::is_same_v<PixelType, float> ||
              std::is_same_v<PixelType, double>) &&
             ValidDecompressorSpec<DecompSpec>
class ImageReader {
private:
    mutable ReadStrat read_strategy_;
    
    // Reusable storage for chunk collection and decoding
    mutable std::vector<ChunkInfo> chunk_list_;
    mutable ChunkDecoder<PixelType, DecompSpec> decoder_;

public:
    explicit ImageReader(
        ReadStrat read_strategy = ReadStrat{}) noexcept
        : read_strategy_(std::move(read_strategy))
        , chunk_list_()
        , decoder_() {}
    
    /// Get mutable access to read strategy (e.g., to change batching params)
    [[nodiscard]] ReadStrat& read_strategy() noexcept {
        return read_strategy_;
    }
    
    /// Get const access to read strategy
    [[nodiscard]] const ReadStrat& read_strategy() const noexcept {
        return read_strategy_;
    }
    
    /// Read a region from a tiled image
    template <OutputSpec OutSpec, typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<void> read_region(
        const Reader& reader,
        const TiledImageInfo<PixelType>& image_info,
        std::span<PixelType> output_buffer,
        const ImageRegion& region) noexcept {
        
        return read_region_impl<OutSpec>(
            reader, image_info, output_buffer, region
        );
    }
    
    /// Read a region from a stripped image
    template <OutputSpec OutSpec, typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<void> read_region(
        const Reader& reader,
        const StrippedImageInfo<PixelType>& image_info,
        std::span<PixelType> output_buffer,
        const ImageRegion& region) noexcept {
        
        // Strips don't support 3D
        if (region.start_z != 0 || region.depth != 1) {
            return Err(Error::Code::InvalidArgument, "Stripped images do not support 3D regions");
        }
        
        return read_region_impl<OutSpec>(
            reader, image_info, output_buffer, region
        );
    }
    
    /// Clear internal caches and reset strategies for reuse
    void clear() noexcept {
        chunk_list_.clear();
        chunk_list_.shrink_to_fit();
        read_strategy_.clear();
    }

private:
    /// Common implementation for reading regions (works for both tiled and stripped)
    template <OutputSpec OutSpec, typename Reader, typename ImageInfo>
        requires RawReader<Reader>
    [[nodiscard]] Result<void> read_region_impl(
        const Reader& reader,
        const ImageInfo& image_info,
        std::span<PixelType> output_buffer,
        const ImageRegion& region) noexcept {
        
        // Validate region bounds
        auto validation = image_info.shape().validate_region(region);
        if (!validation) {
            return validation;
        }
        
        // Validate output buffer size
        std::size_t expected_size = region.num_samples();
        if (output_buffer.size() < expected_size) {
            return Err(Error::Code::InvalidArgument, "Output buffer too small");
        }
        
        // Collect all chunks needed for this region
        chunk_list_.clear();
        collect_chunks_for_region(image_info, region, chunk_list_);
        
        if (chunk_list_.empty()) {
            return Ok();  // Nothing to read
        }

        // Ensure chunks are sorted by file offset for efficient reading and batching
        std::sort(chunk_list_.begin(), chunk_list_.end(), 
            [](const ChunkInfo& a, const ChunkInfo& b) {
                return a.offset < b.offset;
        });

        // Producer/consumer pattern: read strategy reads chunks and invokes processor
        // The processor decodes and copies data to output buffer (thread-safe)
        uint16_t samples_per_pixel = image_info.shape().planar_configuration() == PlanarConfiguration::Planar ? 
            1 : image_info.shape().samples_per_pixel();
        
        ChunkDecoderProcessor<OutSpec, PixelType, DecompSpec, ImageInfo> processor(
            decoder_,
            image_info,
            output_buffer,
            region,
            samples_per_pixel
        );
        
        // Invoke read strategy with processor
        return read_strategy_.read_chunks(reader, std::span<const ChunkInfo>(chunk_list_), processor);
    }
    
    /// Collect chunks for tiled images
    void collect_chunks_for_region(
        const TiledImageInfo<PixelType>& image_info,
        const ImageRegion& region,
        std::vector<ChunkInfo>& chunks) const noexcept {
        
        uint32_t tile_width = image_info.tile_width();
        uint32_t tile_height = image_info.tile_height();
        uint32_t tile_depth = image_info.tile_depth();
        
        uint32_t start_tile_x = region.start_x / tile_width;
        uint32_t start_tile_y = region.start_y / tile_height;
        uint32_t start_tile_z = region.start_z / tile_depth;
        
        uint32_t end_tile_x = (region.end_x() + tile_width - 1) / tile_width;
        uint32_t end_tile_y = (region.end_y() + tile_height - 1) / tile_height;
        uint32_t end_tile_z = (region.end_z() + tile_depth - 1) / tile_depth;
        
        end_tile_x = std::min(end_tile_x, image_info.tiles_across());
        end_tile_y = std::min(end_tile_y, image_info.tiles_down());
        end_tile_z = std::min(end_tile_z, image_info.tiles_deep());
        
        collect_chunks_in_range(
            image_info, region, chunks,
            start_tile_x, end_tile_x,
            start_tile_y, end_tile_y,
            start_tile_z, end_tile_z
        );
    }
    
    /// Collect chunks for stripped images (strips are just tiles with x=0, depth=1, width=image_width)
    void collect_chunks_for_region(
        const StrippedImageInfo<PixelType>& image_info,
        const ImageRegion& region,
        std::vector<ChunkInfo>& chunks) const noexcept {
        
        uint32_t rows_per_strip = image_info.rows_per_strip();
        uint32_t start_strip = region.start_y / rows_per_strip;
        uint32_t end_strip = (region.end_y() + rows_per_strip - 1) / rows_per_strip;
        
        uint32_t strips_per_plane = image_info.strips_per_plane();
        end_strip = std::min(end_strip, strips_per_plane);
        
        // Strips are like tiles with x dimension always at 0, z=0, depth=1
        collect_chunks_in_range(
            image_info, region, chunks,
            0, 1,              // Only one "tile" column (x=0)
            start_strip, end_strip,
            0, 1               // Only one "tile" depth (z=0)
        );
    }
    
    /// Generic chunk collection - works for both tiles and strips
    /// For strips: x_start=0, x_end=1, z_start=0, z_end=1
    template <typename ImageInfo>
    void collect_chunks_in_range(
        const ImageInfo& image_info,
        const ImageRegion& region,
        std::vector<ChunkInfo>& chunks,
        uint32_t x_start, uint32_t x_end,
        uint32_t y_start, uint32_t y_end,
        uint32_t z_start, uint32_t z_end) const noexcept {
        
        PlanarConfiguration planar_config = image_info.shape().planar_configuration();
        
        if (planar_config == PlanarConfiguration::Planar) {
            // Each channel is stored separately
            for (uint16_t ch = 0; ch < region.num_channels; ++ch) {
                uint16_t source_channel = region.start_channel + ch;
                
                for (uint32_t z = z_start; z < z_end; ++z) {
                    for (uint32_t y = y_start; y < y_end; ++y) {
                        for (uint32_t x = x_start; x < x_end; ++x) {
                            add_chunk_if_valid(image_info, x, y, z, source_channel, chunks);
                        }
                    }
                }
            }
        } else {
            // Chunky: all channels are in each chunk
            for (uint32_t z = z_start; z < z_end; ++z) {
                for (uint32_t y = y_start; y < y_end; ++y) {
                    for (uint32_t x = x_start; x < x_end; ++x) {
                        add_chunk_if_valid(image_info, x, y, z, 0, chunks);
                    }
                }
            }
        }
    }
    
    /// Add chunk for tiled images
    void add_chunk_if_valid(
        const TiledImageInfo<PixelType>& image_info,
        uint32_t tile_x, uint32_t tile_y, uint32_t tile_z, uint16_t plane,
        std::vector<ChunkInfo>& chunks) const noexcept {
        
        auto info_result = image_info.get_tile_info_3d(tile_x, tile_y, tile_z, plane);
        if (info_result && info_result.value().byte_count > 0) {
            const auto& tile_info = info_result.value();
            ChunkInfo chunk;
            chunk.offset = tile_info.offset;
            chunk.byte_count = tile_info.byte_count;
            chunk.chunk_index = tile_info.tile_index;
            chunk.pixel_x = tile_info.pixel_x;
            chunk.pixel_y = tile_info.pixel_y;
            chunk.pixel_z = tile_info.pixel_z;
            chunk.width = tile_info.width;
            chunk.height = tile_info.height;
            chunk.depth = tile_info.depth;
            chunk.plane = plane;
            chunks.push_back(chunk);
        }
    }
    
    /// Add chunk for stripped images (strip_idx is treated as y index)
    void add_chunk_if_valid(
        const StrippedImageInfo<PixelType>& image_info,
        uint32_t /*x*/, uint32_t strip_idx, uint32_t /*z*/, uint16_t plane,
        std::vector<ChunkInfo>& chunks) const noexcept {
        
        auto info_result = image_info.get_strip_info(strip_idx, plane);
        if (info_result && info_result.value().byte_count > 0) {
            const auto& strip_info = info_result.value();
            ChunkInfo chunk;
            chunk.offset = strip_info.offset;
            chunk.byte_count = strip_info.byte_count;
            chunk.chunk_index = strip_info.strip_index;
            chunk.pixel_x = 0;  // Strips always start at x=0
            chunk.pixel_y = strip_info.pixel_y;
            chunk.pixel_z = 0;  // Strips are 2D
            chunk.width = strip_info.width;
            chunk.height = strip_info.height;
            chunk.depth = 1;    // Strips are 2D
            chunk.plane = plane;
            chunks.push_back(chunk);
        }
    }
};

} // namespace tiff