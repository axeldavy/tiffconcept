#pragma once

#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <span>
#include <thread>
#include <vector>
#include "../decompressors/decompressor_base.hpp"
#include "../image_shape.hpp"
#include "../lowlevel/decoder.hpp"
#include "../lowlevel/tiling.hpp"
#include "../reader_base.hpp"
#include "../types/optional.hpp"
#include "../types/result.hpp"
#include "../types/tile_info.hpp"

#ifndef TIFFCONCEPT_IMAGE_READER_HEADER
#include "../image_reader.hpp"
#endif

namespace tiffconcept {

namespace detail {
    /// @brief Helper to check if a TagSpec has tile tags (optional or required)
    template <typename TSpec>
    concept HasTileTags = ValidTagSpec<TSpec> &&
        TSpec::template has_tag<TagCode::TileWidth>() &&
        TSpec::template has_tag<TagCode::TileLength>() &&
        TSpec::template has_tag<TagCode::TileOffsets>() &&
        TSpec::template has_tag<TagCode::TileByteCounts>(); // TODO: should be reuse TiledImageTagSpec ?
    
    /// @brief Helper to check if tile tags are present at runtime
    template <typename TSpec>
    requires HasTileTags<TSpec>
    [[nodiscard]] constexpr bool has_tile_tags_present(const ExtractedTags<TSpec>& tags) noexcept {
        using TileWidthTag = typename TSpec::template get_tag<TagCode::TileWidth>;
        using TileLengthTag = typename TSpec::template get_tag<TagCode::TileLength>;
        using TileOffsetsTag = typename TSpec::template get_tag<TagCode::TileOffsets>;
        using TileByteCountsTag = typename TSpec::template get_tag<TagCode::TileByteCounts>;
        
        if constexpr (TileWidthTag::is_optional || TileLengthTag::is_optional || 
                      TileOffsetsTag::is_optional || TileByteCountsTag::is_optional) {
            const auto& tw = tags.template get<TagCode::TileWidth>();
            const auto& tl = tags.template get<TagCode::TileLength>();
            const auto& to = tags.template get<TagCode::TileOffsets>();
            const auto& tbc = tags.template get<TagCode::TileByteCounts>();
            
            if constexpr (TileWidthTag::is_optional) {
                if (!tw.has_value()) return false;
            }
            if constexpr (TileLengthTag::is_optional) {
                if (!tl.has_value()) return false;
            }
            if constexpr (TileOffsetsTag::is_optional) {
                if (!to.has_value()) return false;
            }
            if constexpr (TileByteCountsTag::is_optional) {
                if (!tbc.has_value()) return false;
            }
            return true;
        } else {
            return true;
        }
    }
    
    template <typename TSpec>
    requires (!HasTileTags<TSpec>)
    [[nodiscard]] constexpr bool has_tile_tags_present(const ExtractedTags<TSpec>&) noexcept {
        return false;
    }
}

template <typename TagSpec>
requires (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
inline Result<void> collect_tiles_for_region(
    const ImageRegion& region,
    const ExtractedTags<TagSpec>& metadata,
    std::vector<Tile>& tiles) noexcept
{
    // Clear output vector
    tiles.clear();
    
    // Extract and validate image shape
    ImageShape shape;
    auto shape_result = shape.update_from_metadata(metadata);
    if (!shape_result) {
        return shape_result;
    }
    
    // Validate region
    auto validate_result = shape.validate_region(region);
    if (!validate_result) {
        return validate_result;
    }
    
    // Determine whether to use tiled or stripped mode
    constexpr bool has_tile_tags = detail::HasTileTags<TagSpec>;
    const bool use_tiled = has_tile_tags && detail::has_tile_tags_present(metadata);
    
    if constexpr (has_tile_tags) {
        if (use_tiled) {
            // Use tiled image mode - extract tile tags directly
            const auto& tile_width = optional::unwrap_value(metadata.template get<TagCode::TileWidth>());
            const auto& tile_height = optional::unwrap_value(metadata.template get<TagCode::TileLength>());
            const auto& tile_offsets = optional::unwrap_value(metadata.template get<TagCode::TileOffsets>());
            const auto& tile_byte_counts = optional::unwrap_value(metadata.template get<TagCode::TileByteCounts>());
            
            uint32_t tile_depth = 1;
            if constexpr (TagSpec::template has_tag<TagCode::TileDepth>()) {
                const auto& td = metadata.template get<TagCode::TileDepth>();
                if constexpr (std::remove_cvref_t<decltype(td)>::value_type::is_optional) {
                    if (td.has_value()) {
                        tile_depth = td.value();
                    }
                } else {
                    tile_depth = td;
                }
            }
            
            // Calculate tile grid dimensions
            uint32_t tiles_across = (shape.image_width() + tile_width - 1) / tile_width;
            uint32_t tiles_down = (shape.image_height() + tile_height - 1) / tile_height;
            uint32_t tiles_deep = (shape.image_depth() + tile_depth - 1) / tile_depth;
            
            // Calculate tile ranges that overlap the region
            uint32_t start_tile_x = region.start_x / tile_width;
            uint32_t start_tile_y = region.start_y / tile_height;
            uint32_t start_tile_z = region.start_z / tile_depth;
            
            uint32_t end_tile_x = (region.end_x() + tile_width - 1) / tile_width;
            uint32_t end_tile_y = (region.end_y() + tile_height - 1) / tile_height;
            uint32_t end_tile_z = (region.end_z() + tile_depth - 1) / tile_depth;
            
            end_tile_x = std::min(end_tile_x, tiles_across);
            end_tile_y = std::min(end_tile_y, tiles_down);
            end_tile_z = std::min(end_tile_z, tiles_deep);
            
            // Collect tiles
            PlanarConfiguration planar_config = shape.planar_configuration();
            uint32_t tiles_per_slice = tiles_across * tiles_down;
            uint32_t tiles_per_plane = tiles_per_slice * tiles_deep;
            
            if (planar_config == PlanarConfiguration::Planar) {
                // Each channel is stored separately
                for (uint16_t ch = 0; ch < region.num_channels; ++ch) {
                    uint16_t plane = region.start_channel + ch;
                    
                    for (uint32_t z = start_tile_z; z < end_tile_z; ++z) {
                        for (uint32_t y = start_tile_y; y < end_tile_y; ++y) {
                            for (uint32_t x = start_tile_x; x < end_tile_x; ++x) {
                                uint32_t tile_index = plane * tiles_per_plane + z * tiles_per_slice + y * tiles_across + x;
                                
                                if (tile_index >= tile_offsets.size()) [[unlikely]] {
                                    return Err(Error::Code::OutOfBounds, "Tile index out of bounds");
                                }
                                
                                uint64_t offset = tile_offsets[tile_index];
                                uint64_t length = tile_byte_counts[tile_index];
                                
                                if (length > 0) {
                                    uint32_t pixel_x = x * tile_width;
                                    uint32_t pixel_y = y * tile_height;
                                    uint32_t pixel_z = z * tile_depth;
                                    
                                    // No clamping - use full tile dimensions
                                    Tile tile;
                                    tile.id.index = tile_index;
                                    tile.id.coords = TileCoordinates{pixel_x, pixel_y, pixel_z, plane};
                                    tile.id.size = TileSize{tile_width, tile_height, tile_depth, 1u};
                                    tile.location.offset = offset;
                                    tile.location.length = length;
                                    
                                    tiles.push_back(tile);
                                }
                            }
                        }
                    }
                }
            } else {
                // Chunky: all channels are in each tile
                for (uint32_t z = start_tile_z; z < end_tile_z; ++z) {
                    for (uint32_t y = start_tile_y; y < end_tile_y; ++y) {
                        for (uint32_t x = start_tile_x; x < end_tile_x; ++x) {
                            uint32_t tile_index = z * tiles_per_slice + y * tiles_across + x;
                            
                            if (tile_index >= tile_offsets.size()) [[unlikely]] {
                                return Err(Error::Code::OutOfBounds, "Tile index out of bounds");
                            }
                            
                            uint64_t offset = tile_offsets[tile_index];
                            uint64_t length = tile_byte_counts[tile_index];
                            
                            if (length > 0) {
                                uint32_t pixel_x = x * tile_width;
                                uint32_t pixel_y = y * tile_height;
                                uint32_t pixel_z = z * tile_depth;
                                
                                // No clamping - use full tile dimensions
                                Tile tile;
                                tile.id.index = tile_index;
                                tile.id.coords = TileCoordinates{pixel_x, pixel_y, pixel_z, 0};
                                tile.id.size = TileSize{tile_width, tile_height, tile_depth, shape.samples_per_pixel()};
                                tile.location.offset = offset;
                                tile.location.length = length;
                                
                                tiles.push_back(tile);
                            }
                        }
                    }
                }
            }
            
            // Sort by file offset for efficient sequential reading
            std::sort(tiles.begin(), tiles.end(), [](const Tile& a, const Tile& b) {
                return a.location.offset < b.location.offset;
            });
            
            return Ok();
        }
    }
    
    // Fall back to stripped mode
    if constexpr (StrippedImageTagSpec<TagSpec>) {
        // Extract strip tags directly
        const auto& rows_per_strip = optional::unwrap_value(metadata.template get<TagCode::RowsPerStrip>());
        const auto& strip_offsets = optional::unwrap_value(metadata.template get<TagCode::StripOffsets>());
        const auto& strip_byte_counts = optional::unwrap_value(metadata.template get<TagCode::StripByteCounts>());
        
        // Calculate strip ranges that overlap the region
        uint32_t strips_per_plane = (shape.image_height() + rows_per_strip - 1) / rows_per_strip;
        uint32_t start_strip = region.start_y / rows_per_strip;
        uint32_t end_strip = (region.end_y() + rows_per_strip - 1) / rows_per_strip;
        end_strip = std::min(end_strip, strips_per_plane);
        
        // Collect strips
        PlanarConfiguration planar_config = shape.planar_configuration();
        
        if (planar_config == PlanarConfiguration::Planar) {
            // Each channel is stored separately
            for (uint16_t ch = 0; ch < region.num_channels; ++ch) {
                uint16_t plane = region.start_channel + ch;
                
                for (uint32_t strip_idx = start_strip; strip_idx < end_strip; ++strip_idx) {
                    uint32_t actual_strip_index = plane * strips_per_plane + strip_idx;
                    
                    if (actual_strip_index >= strip_offsets.size()) [[unlikely]] {
                        return Err(Error::Code::OutOfBounds, "Strip index out of bounds");
                    }
                    
                    uint64_t offset = strip_offsets[actual_strip_index];
                    uint64_t length = strip_byte_counts[actual_strip_index];
                    
                    if (length > 0) {
                        uint32_t pixel_y = strip_idx * rows_per_strip;
                        
                        // Clamp last strip height to image height per TIFF spec
                        uint32_t strip_height = rows_per_strip;
                        if (pixel_y + strip_height > shape.image_height()) {
                            strip_height = shape.image_height() - pixel_y;
                        }
                        
                        Tile tile;
                        tile.id.index = actual_strip_index;
                        tile.id.coords = TileCoordinates{0, pixel_y, 0, plane};
                        tile.id.size = TileSize{shape.image_width(), strip_height, 1u, 1u};
                        tile.location.offset = offset;
                        tile.location.length = length;
                        
                        tiles.push_back(tile);
                    }
                }
            }
        } else {
            // Chunky: all channels are in each strip
            for (uint32_t strip_idx = start_strip; strip_idx < end_strip; ++strip_idx) {
                if (strip_idx >= strip_offsets.size()) [[unlikely]] {
                    return Err(Error::Code::OutOfBounds, "Strip index out of bounds");
                }
                
                uint64_t offset = strip_offsets[strip_idx];
                uint64_t length = strip_byte_counts[strip_idx];
                
                if (length > 0) {
                    uint32_t pixel_y = strip_idx * rows_per_strip;
                    
                    // Clamp last strip height to image height per TIFF spec
                    uint32_t strip_height = rows_per_strip;
                    if (pixel_y + strip_height > shape.image_height()) {
                        strip_height = shape.image_height() - pixel_y;
                    }
                    
                    Tile tile;
                    tile.id.index = strip_idx;
                    tile.id.coords = TileCoordinates{0, pixel_y, 0, 0};
                    tile.id.size = TileSize{shape.image_width(), strip_height, 1u, shape.samples_per_pixel()};
                    tile.location.offset = offset;
                    tile.location.length = length;
                    
                    tiles.push_back(tile);
                }
            }
        }
        
        // Sort by file offset for efficient sequential reading
        std::sort(tiles.begin(), tiles.end(), [](const Tile& a, const Tile& b) {
            return a.location.offset < b.location.offset;
        });
        
        return Ok();
    } else {
        return Err(Error::Code::InvalidTag, "No valid tile or strip tags found");
    }
}

template <ImageLayoutSpec OutSpec, typename PixelType, typename TagSpec>
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
         (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
[[nodiscard]] inline Result<void> extract_tile_to_buffer(
    const Tile& tile,
    const ImageRegion& region,
    const ExtractedTags<TagSpec>& metadata,
    std::span<const PixelType> decoded_tile,
    std::span<PixelType> output_buffer) noexcept
{
    // Extract image shape
    ImageShape shape;
    auto shape_result = shape.update_from_metadata(metadata);
    if (!shape_result) {
        return shape_result;
    }
    
    // Validate pixel type
    auto format_validation = shape.validate_pixel_type<PixelType>();
    if (!format_validation) {
        return format_validation;
    }
    
    // Validate output buffer size
    const std::size_t expected_size = region.num_samples();
    if (output_buffer.size() != expected_size) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, 
                  "Output buffer size doesn't match region size");
    }
    
    // Validate decoded tile size
    const std::size_t expected_tile_size = static_cast<std::size_t>(tile.id.size.width) *
                                           tile.id.size.height *
                                           tile.id.size.depth *
                                           tile.id.size.nsamples;
    if (decoded_tile.size() != expected_tile_size) [[unlikely]] {
        return Err(Error::Code::OutOfBounds,
                  "Decoded tile size doesn't match tile dimensions");
    }
    
    // Calculate overlap between tile and region
    const uint32_t tile_x = tile.id.coords.x;
    const uint32_t tile_y = tile.id.coords.y;
    const uint32_t tile_z = tile.id.coords.z;
    const uint32_t tile_s = tile.id.coords.s;
    
    // Calculate intersection
    const uint32_t overlap_x_start = std::max(tile_x, region.start_x);
    const uint32_t overlap_y_start = std::max(tile_y, region.start_y);
    const uint32_t overlap_z_start = std::max(tile_z, region.start_z);
    
    const uint32_t tile_x_end = tile_x + tile.id.size.width;
    const uint32_t tile_y_end = tile_y + tile.id.size.height;
    const uint32_t tile_z_end = tile_z + tile.id.size.depth;
    
    const uint32_t overlap_x_end = std::min(tile_x_end, region.end_x());
    const uint32_t overlap_y_end = std::min(tile_y_end, region.end_y());
    const uint32_t overlap_z_end = std::min(tile_z_end, region.end_z());
    
    // Check if there's any overlap
    if (overlap_x_start >= overlap_x_end || 
        overlap_y_start >= overlap_y_end || 
        overlap_z_start >= overlap_z_end) [[unlikely]] {
        return Err(Error::Code::InvalidOperation, "Tile doesn't overlap with region");
    }
    
    // Calculate copy dimensions
    const uint32_t copy_width = overlap_x_end - overlap_x_start;
    const uint32_t copy_height = overlap_y_end - overlap_y_start;
    const uint32_t copy_depth = overlap_z_end - overlap_z_start;
    
    // Calculate source position in tile
    TileCoordinates src_pos{
        overlap_x_start - tile_x,
        overlap_y_start - tile_y,
        overlap_z_start - tile_z,
        0  // Source sample offset (handled by planar config)
    };
    
    // Calculate destination position in output buffer
    TileCoordinates dst_pos{
        overlap_x_start - region.start_x,
        overlap_y_start - region.start_y,
        overlap_z_start - region.start_z,
        0  // Destination sample offset
    };
    
    // Handle planar configuration
    PlanarConfiguration planar_config = shape.planar_configuration();
    uint32_t copy_nsamples;
    
    if (planar_config == PlanarConfiguration::Planar) {
        // For planar, tiles have 1 sample and correspond to a specific channel
        copy_nsamples = 1;
        
        // Check if this tile's channel is within the region
        if (tile_s < region.start_channel || tile_s >= region.end_channel()) [[unlikely]] {
            return Err(Error::Code::InvalidOperation, 
                      "Tile channel doesn't match region channels");
        }
        
        // Set destination sample offset
        dst_pos.s = tile_s - region.start_channel;
    } else {
        // For chunky, all channels are in the tile
        copy_nsamples = std::min(tile.id.size.nsamples, static_cast<uint32_t>(region.num_channels));
        src_pos.s = region.start_channel;
        dst_pos.s = 0;
    }
    
    // Set up dimensions for copy
    TileSize src_dims = tile.id.size;
    TileSize dst_dims{
        region.width,
        region.height,
        region.depth,
        region.num_channels
    };
    TileSize copy_dims{
        copy_width,
        copy_height,
        copy_depth,
        copy_nsamples
    };
    
    // Copy tile data to output buffer with layout conversion
    if (planar_config == PlanarConfiguration::Planar) {
        copy_tile_to_buffer<PlanarConfiguration::Planar, OutSpec>(
            decoded_tile,
            output_buffer,
            dst_dims,
            src_dims,
            copy_dims,
            dst_pos,
            src_pos
        );
    } else {
        copy_tile_to_buffer<PlanarConfiguration::Chunky, OutSpec>(
            decoded_tile,
            output_buffer,
            dst_dims,
            src_dims,
            copy_dims,
            dst_pos,
            src_pos
        );
    }
    
    return Ok();
}

// ============================================================================
// IOLimitedReader Implementation
// ============================================================================
//
// Design Overview:
// ----------------
// This reader is optimized for high-latency I/O scenarios (e.g., network file
// systems, cloud storage, or slow disks) where the bottleneck is primarily the
// number and latency of I/O operations rather than CPU processing.
//
// Key Strategies:
// 1. **Batching**: Adjacent tiles in the file are grouped into larger read
//    requests to minimize the number of I/O round-trips. Small gaps between
//    tiles are bridged to avoid issuing separate reads.
//
// 2. **Parallel I/O**: A persistent thread pool issues multiple read requests
//    concurrently, allowing overlapping I/O operations to maximize throughput
//    on high-latency storage.
//
// 3. **Serial Decoding**: The calling thread performs all decompression and
//    extraction. This design choice provides:
//    - Better cache locality when writing to the output buffer
//    - Allows the caller to control parallelization strategy
//    - Simplifies synchronization (no concurrent writes to output buffer)
//
// Thread Safety:
// --------------
// - read_region() is thread-safe and can be called concurrently from multiple
//   threads. Each call maintains its own job state and decoder.
// - The I/O thread pool is shared across all read_region() calls.
// - Decoder pool uses a mutex, but contention is low since decoding is
//   serialized per read_region() call.
//
// Performance Characteristics:
// ----------------------------
// - Best for: High-latency I/O with light to moderate compression
// - Memory: O(total_batch_size) per concurrent read_region() call
// - Concurrency: I/O threads × concurrent read_region() calls
// - Not recommended for: Local SSD/memory-mapped files or heavy compression
//   (use SimpleReader or CPULimitedReader instead)
//
// Invariants:
// -----------
// - Tiles are sorted by file offset (guaranteed by collect_tiles_for_region)
// - Batches cover all tiles exactly once, in order
// - tasks_in_flight == 0 implies all I/O threads have finished their work
// - Reader reference remains valid until all I/O tasks complete
// - Decoder is returned to pool before read_region() returns
//
// ============================================================================

template <typename PixelType, typename DecompSpec>
IOLimitedReader<PixelType, DecompSpec>::IOLimitedReader(Config config) 
    : config_(config) {
    if (config_.io_threads == 0) {
        config_.io_threads = std::thread::hardware_concurrency();
        if (config_.io_threads == 0) config_.io_threads = 1;
    }
    
    // Spawn persistent worker threads for I/O operations
    for (size_t i = 0; i < config_.io_threads; ++i) {
        threads_.emplace_back(&IOLimitedReader::worker_loop, this);
    }
}

template <typename PixelType, typename DecompSpec>
IOLimitedReader<PixelType, DecompSpec>::~IOLimitedReader() {
    // Signal all workers to stop and wait for them to finish
    {
        std::lock_guard lock(queue_mutex_);
        stop_threads_ = true;
    }
    queue_cv_.notify_all();
    
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

template <typename PixelType, typename DecompSpec>
std::unique_ptr<TileDecoder<PixelType, DecompSpec>> IOLimitedReader<PixelType, DecompSpec>::acquire_decoder() {
    std::lock_guard lock(decoder_pool_mutex_);
    
    auto current_thread = std::this_thread::get_id();
    
    // First pass: try to find a decoder that was last used by this thread
    // (the idea is to improve cache locality when reusing decoder storage)
    for (auto it = decoder_pool_.begin(); it != decoder_pool_.end(); ++it) {
        if (it->last_thread_id == current_thread) {
            auto decoder = std::move(it->decoder);
            decoder_pool_.erase(it);
            return decoder;
        }
    }
    
    // Second pass: take any available decoder
    if (!decoder_pool_.empty()) {
        auto decoder = std::move(decoder_pool_.back().decoder);
        decoder_pool_.pop_back();
        return decoder;
    }
    
    // No decoder available, allocate new one
    return std::make_unique<TileDecoder<PixelType, DecompSpec>>();
}

template <typename PixelType, typename DecompSpec>
void IOLimitedReader<PixelType, DecompSpec>::release_decoder(
    std::unique_ptr<TileDecoder<PixelType, DecompSpec>> decoder) {
    std::lock_guard lock(decoder_pool_mutex_);
    
    // Return decoder to pool tagged with current thread ID for future reuse
    // This improves cache locality when the same thread calls read_region again
    decoder_pool_.push_back({
        std::move(decoder), 
        std::this_thread::get_id()
    });
}

template <typename PixelType, typename DecompSpec>
void IOLimitedReader<PixelType, DecompSpec>::worker_loop() {
    // Persistent worker thread that executes I/O tasks from the shared queue
    while (true) {
        IOTask task;
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { 
                return stop_threads_ || !pending_tasks_.empty(); 
            });
            
            // Exit if shutdown requested and no work remains
            if (stop_threads_ && pending_tasks_.empty()) return;
            
            task = std::move(pending_tasks_.front());
            pending_tasks_.pop_front();
        }

        // Execute I/O task (reads from storage and reports completion)
        if (task) {
            task();
        }
    }
}

template <typename PixelType, typename DecompSpec>
void IOLimitedReader<PixelType, DecompSpec>::create_batches(
    const std::vector<Tile>& tiles, 
    const Config& config, 
    std::vector<Batch>& out_batches) {
    // Group adjacent tiles into batches to minimize I/O round-trips
    //
    // Algorithm:
    // - Start with the first tile
    // - For each subsequent tile, check if it's close enough to merge:
    //   * Gap between tiles ≤ max_gap_size (avoid reading too much unused data)
    //   * Total batch size ≤ max_batch_size (avoid excessive memory usage)
    // - If not mergeable, finalize current batch and start a new one
    //
    // Precondition: tiles are sorted by file offset (collect_tiles_for_region)
    // Postcondition: batches cover all tiles exactly once, in order
    
    if (tiles.empty()) return;

    size_t start_idx = 0;
    size_t current_offset = tiles[0].location.offset;
    size_t current_end = current_offset + tiles[0].location.length;

    for (size_t i = 1; i < tiles.size(); ++i) {
        const auto& tile = tiles[i];
        
        // Calculate gap between end of current batch and start of this tile
        size_t gap = tile.location.offset - current_end;
        size_t new_end = tile.location.offset + tile.location.length;
        size_t new_size = new_end - current_offset;

        // Break batch if gap is too large or total size exceeds limit
        bool break_batch = (gap > config.max_gap_size) || 
                          (new_size > config.max_batch_size);

        if (break_batch) {
            // Finalize current batch
            out_batches.push_back({
                start_idx, 
                i - start_idx, 
                current_offset, 
                current_end - current_offset
            });
            
            // Start new batch with current tile
            start_idx = i;
            current_offset = tile.location.offset;
            current_end = current_offset + tile.location.length;
        } else {
            // Extend current batch to include this tile
            // Use max() to handle potential overlaps (though shouldn't occur)
            current_end = std::max(current_end, new_end);
        }
    }
    
    // Finalize last batch
    out_batches.push_back({
        start_idx, 
        tiles.size() - start_idx, 
        current_offset, 
        current_end - current_offset
    });
}

template <typename PixelType, typename DecompSpec>
template <ImageLayoutSpec OutSpec, typename Reader, typename TagSpec>
requires RawReader<Reader> && (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
Result<void> IOLimitedReader<PixelType, DecompSpec>::read_region(
    const Reader& reader,
    const ExtractedTags<TagSpec>& metadata,
    const ImageRegion& region,
    std::span<PixelType> output_buffer) noexcept {

    // ========================================================================
    // Phase 1: Preparation (thread-local, no synchronization needed)
    // ========================================================================
    
    // Identify which tiles overlap the requested region
    std::vector<Tile> tiles;
    auto collect_res = collect_tiles_for_region(region, metadata, tiles);
    if (!collect_res) return collect_res;

    if (tiles.empty()) return Ok();

    // Group tiles into batches for efficient I/O
    std::vector<Batch> batches;
    create_batches(tiles, config_, batches);

    if (batches.empty()) return Ok();

    // ========================================================================
    // Phase 2: Setup Job State (per-call state for coordinating I/O and processing)
    // ========================================================================
    
    /// @brief Completed batch with loaded data
    struct CompletedBatch {
        size_t batch_index;                             // Which batch this is
        std::shared_ptr<std::vector<std::byte>> data;   // Batch data from I/O
    };

    /// @brief Per-job state shared between I/O threads and processing thread
    /// 
    /// Synchronization strategy:
    /// - mutex protects: completed_queue, first_error
    /// - atomics for: tasks_in_flight, error_occurred
    /// - cv notifies processing thread when: data arrives, error occurs, or I/O completes
    struct JobState {
        std::mutex mutex;                           // Protects completed_queue and first_error
        std::condition_variable cv;                 // Signals processing thread
        std::deque<CompletedBatch> completed_queue; // FIFO queue of loaded batches
        
        // Tracking state
        std::atomic<size_t> tasks_in_flight{0};     // Number of I/O operations in progress
        std::atomic<bool> error_occurred{false};    // Fast path: check without lock
        Result<void> first_error = Ok();            // First error encountered (if any)
    };

    auto job_state = std::make_shared<JobState>();
    job_state->tasks_in_flight.store(batches.size(), std::memory_order_release);

    // ========================================================================
    // Phase 3: Submit I/O Tasks to Worker Thread Pool
    // ========================================================================
    {
        std::lock_guard lock(queue_mutex_);
        for (size_t i = 0; i < batches.size(); ++i) {
            const auto& batch = batches[i];
            
            // Each task captures:
            // - job_state by shared_ptr (keeps it alive)
            // - reader by reference (MUST wait for tasks before returning!)
            // - batch parameters by value (safe, copied at capture)
            pending_tasks_.push_back(
                [&reader, offset=batch.file_offset, size=batch.total_size, 
                 batch_idx=i, job_state]() {
                    try {
                        // Perform I/O operation
                        auto res = reader.read(offset, size);
                        
                        // Report result to job state
                        {
                            std::lock_guard lock(job_state->mutex);
                            if (res.is_ok()) {
                                // Success: Store data for processing
                                // Use shared_ptr to allow processing thread to hold reference
                                auto buffer_view = std::move(res.value());
                                auto data_span = buffer_view.data();
                                auto data_ptr = std::make_shared<std::vector<std::byte>>(
                                    data_span.begin(), 
                                    data_span.end()
                                );
                                job_state->completed_queue.push_back({
                                    batch_idx, 
                                    std::move(data_ptr)
                                });
                            } else {
                                // Failure: Record first error only
                                // Use acquire-release for atomic check without lock
                                bool expected = false;
                                if (job_state->error_occurred.compare_exchange_strong(
                                        expected, true, 
                                        std::memory_order_release, 
                                        std::memory_order_relaxed)) {
                                    job_state->first_error = res.error();
                                }
                            }
                        }
                    } catch (...) {
                        // Exception in reader (should not happen, but defend against it)
                        std::lock_guard lock(job_state->mutex);
                        bool expected = false;
                        if (job_state->error_occurred.compare_exchange_strong(
                                expected, true, 
                                std::memory_order_release, 
                                std::memory_order_relaxed)) {
                            job_state->first_error = Err(Error::Code::Unknown, 
                                                        "Exception in I/O operation");
                        }
                    }
                    
                    // Decrement in-flight counter and wake processing thread
                    job_state->tasks_in_flight.fetch_sub(1, std::memory_order_release);
                    job_state->cv.notify_one();
                }
            );
        }
    }
    queue_cv_.notify_all(); // Wake I/O worker threads

    // ========================================================================
    // Phase 4: Process Results on Calling Thread (decode + extract)
    // ========================================================================
    
    /// @brief RAII wrapper for decoder acquisition/release
    struct ScopedDecoder {
        IOLimitedReader* reader;
        std::unique_ptr<TileDecoder<PixelType, DecompSpec>> decoder;
        
        ScopedDecoder(IOLimitedReader* r) 
            : reader(r), decoder(r->acquire_decoder()) {}
        
        ~ScopedDecoder() { 
            if (decoder) reader->release_decoder(std::move(decoder)); 
        }
        
        TileDecoder<PixelType, DecompSpec>& get() { return *decoder; }
    };
    
    ScopedDecoder scoped_decoder(this);
    auto& decoder = scoped_decoder.get();
    
    size_t batches_processed = 0;
    Result<void> final_result = Ok();

    // Get compression and predictor from tags
    CompressionScheme compression = optional::extract_tag_or<TagCode::Compression, TagSpec>(
        metadata, CompressionScheme::None
    );
    
    Predictor predictor = Predictor::None;
    if constexpr (TagSpec::template has_tag<TagCode::Predictor>()) {
        predictor = optional::extract_tag_or<TagCode::Predictor, TagSpec>(
            metadata, Predictor::None
        );
    }

    // Process batches as they arrive from I/O threads
    while (batches_processed < batches.size()) {
        std::unique_lock lock(job_state->mutex);
        
        // Wait for: data available, error occurred, or all I/O complete
        job_state->cv.wait(lock, [&] { 
            return !job_state->completed_queue.empty() || 
                   job_state->error_occurred.load(std::memory_order_acquire) || 
                   job_state->tasks_in_flight.load(std::memory_order_acquire) == 0; 
        });

        // Check for I/O errors (early exit on failure)
        if (job_state->error_occurred.load(std::memory_order_acquire)) {
            final_result = job_state->first_error;
            break; // Skip to wait_for_io_completion
        }

        // Process all available batches
        while (!job_state->completed_queue.empty()) {
            auto item = job_state->completed_queue.front();
            job_state->completed_queue.pop_front();
            
            // Release lock during CPU-intensive decoding/extraction
            // This allows I/O threads to continue queueing results
            lock.unlock();

            const auto& batch = batches[item.batch_index];
            const auto& buffer = *item.data;

            // Process each tile in this batch
            for (size_t i = 0; i < batch.tile_count; ++i) {
                try {
                    const auto& tile = tiles[batch.first_tile_index + i];
                    
                    // Calculate tile's position within the batch buffer
                    size_t offset_in_batch = tile.location.offset - batch.file_offset;
                    
                    // Validate buffer bounds (should never fail if batching logic is correct)
                    if (offset_in_batch + tile.location.length > buffer.size()) [[unlikely]] {
                        std::lock_guard err_lock(job_state->mutex);
                        job_state->error_occurred.store(true, std::memory_order_release);
                        job_state->first_error = Err(Error::Code::OutOfBounds, 
                                                    "Tile outside batch buffer");
                        final_result = job_state->first_error;
                        goto wait_for_io_completion;
                    }

                    // Create span for this tile's compressed data
                    std::span<const std::byte> compressed_span(
                        buffer.data() + offset_in_batch, 
                        tile.location.length
                    );

                    // Decode tile (decompress + predictor)
                    auto decode_res = decoder.decode(
                        compressed_span,
                        tile.id.size.width,
                        tile.id.size.height * tile.id.size.depth,
                        compression,
                        predictor,
                        tile.id.size.nsamples
                    );

                    if (!decode_res) {
                        std::lock_guard err_lock(job_state->mutex);
                        job_state->error_occurred.store(true, std::memory_order_release);
                        job_state->first_error = decode_res.error();
                        final_result = decode_res.error();
                        goto wait_for_io_completion;
                    }

                    // Extract tile data to output buffer (with layout conversion)
                    auto extract_res = extract_tile_to_buffer<OutSpec, PixelType>(
                        tile, region, metadata, decode_res.value(), output_buffer
                    );

                    if (!extract_res) {
                        std::lock_guard err_lock(job_state->mutex);
                        job_state->error_occurred.store(true, std::memory_order_release);
                        job_state->first_error = extract_res.error();
                        final_result = extract_res.error();
                        goto wait_for_io_completion;
                    }
                } catch (...) {
                    // Exception during decode/extract (should not happen, but defend against it)
                    std::lock_guard err_lock(job_state->mutex);
                    job_state->error_occurred.store(true, std::memory_order_release);
                    job_state->first_error = Err(Error::Code::Unknown, 
                                                "Exception during tile processing");
                    final_result = job_state->first_error;
                    goto wait_for_io_completion;
                }
            }

            batches_processed++;
            
            // Re-acquire lock for next wait iteration
            lock.lock();
        }
        
        // Success case: all batches processed
        if (batches_processed == batches.size()) {
            break;
        }
    }

wait_for_io_completion:
    // ========================================================================
    // Phase 5: CRITICAL - Wait for All I/O Tasks to Complete
    // ========================================================================
    //
    // We MUST wait for all I/O tasks to finish before returning because:
    // 1. Tasks hold a reference to 'reader' (passed by reference)
    // 2. Tasks hold a shared_ptr to job_state
    // 3. Returning early would invalidate these references, causing UB
    //
    // This wait ensures all tasks have completed, even if we're exiting early
    // due to an error. The I/O threads may still be reading, and we must let
    // them finish before the reader reference becomes invalid.
    {
        std::unique_lock lock(job_state->mutex);
        job_state->cv.wait(lock, [&] {
            return job_state->tasks_in_flight.load(std::memory_order_acquire) == 0; 
        });
    }

    return final_result;
}

// ============================================================================
// CPULimitedReader Implementation
// ============================================================================
//
// Design Overview:
// ----------------
// This reader is optimized for CPU-bound scenarios where decompression is the
// primary bottleneck (e.g., ZSTD, Deflate, LZW) and I/O is fast (e.g., local
// SSD, memory-mapped files, or pre-cached data).
//
// Key Strategies:
// 1. **Parallel Decoding**: Multiple worker threads decode tiles concurrently,
//    maximizing CPU utilization across all cores.
//
// 2. **Thread-Local Decoders**: Each worker thread maintains its own decoder
//    to avoid contention and improve cache locality.
//
// 3. **Fixed Work Distribution**: Tiles are assigned to workers in a fixed manner
//    based on their thread index, ensuring predictable load balancing.
//
// Thread Safety:
// --------------
// - read_region() is thread-safe and can be called concurrently from multiple
//   threads. Each call maintains its own job state.
// - The worker thread pool is shared across all read_region() calls.
// - Workers use thread-local decoders (no sharing between threads).
// - Output buffer writes are coordinated per-job to prevent data races.
//
// Performance Characteristics:
// ----------------------------
// - Best for: CPU-bound decompression with fast I/O
// - CPU Utilization: Near 100% when tiles are large and heavily compressed
// - Memory: O(decoder_scratch_size × worker_threads)
// - Concurrency: worker_threads tiles processed in parallel
// - Not recommended for: High-latency I/O (use IOLimitedReader instead)
//
// Invariants:
// -----------
// - Worker threads are persistent and reused across multiple read_region() calls
// - Each worker has exactly one thread-local decoder
// - tasks_remaining == 0 implies all tiles for a job have been processed
// - Output buffer is safe to read after read_region() returns
// - Job state is destroyed only after all workers finish processing its tiles
//
// ============================================================================

template <typename PixelType, typename DecompSpec>
CPULimitedReader<PixelType, DecompSpec>::CPULimitedReader(Config config) 
    : config_(config) {
    if (config_.worker_threads == 0) {
        config_.worker_threads = std::thread::hardware_concurrency();
        if (config_.worker_threads == 0) config_.worker_threads = 1;
    }

    // Spawn persistent worker threads
    for (size_t i = 0; i < config_.worker_threads; ++i) {
        threads_.emplace_back(&CPULimitedReader::worker_loop, this);
    }
}

template <typename PixelType, typename DecompSpec>
CPULimitedReader<PixelType, DecompSpec>::~CPULimitedReader() {
    // Signal all workers to stop and wait for them to finish
    {
        std::lock_guard lock(queue_mutex_);
        stop_threads_ = true;
    }
    queue_cv_.notify_all();
    
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

template <typename PixelType, typename DecompSpec>
void CPULimitedReader<PixelType, DecompSpec>::worker_loop() {
    // Thread-local decoder for this worker (one per thread, never shared)
    TileDecoder<PixelType, DecompSpec> local_decoder;
    
    while (true) {
        WorkerTask task;
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { 
                return stop_threads_ || !pending_tasks_.empty(); 
            });
            
            // Exit if shutdown requested and no work remains
            if (stop_threads_ && pending_tasks_.empty()) return;
            
            task = std::move(pending_tasks_.front());
            pending_tasks_.pop_front();
        }

        // Execute tile processing task
        // The task has captured all necessary context including the decoder reference
        if (task) {
            task();
        }
    }
}

template <typename PixelType, typename DecompSpec>
template <ImageLayoutSpec OutSpec, typename Reader, typename TagSpec>
requires RawReader<Reader> && (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
Result<void> CPULimitedReader<PixelType, DecompSpec>::read_region(
    const Reader& reader,
    const ExtractedTags<TagSpec>& metadata,
    const ImageRegion& region,
    std::span<PixelType> output_buffer) noexcept {

    // ========================================================================
    // Phase 1: Preparation (thread-local, no synchronization needed)
    // ========================================================================
    
    // Identify which tiles overlap the requested region
    std::vector<Tile> tiles;
    auto collect_res = collect_tiles_for_region(region, metadata, tiles);
    if (!collect_res) return collect_res;

    if (tiles.empty()) return Ok();

    // ========================================================================
    // Phase 2: Setup Job State (per-call state for coordinating worker threads)
    // ========================================================================

    std::size_t num_real_workers = config_.worker_threads + 1; // Including calling thread

    std::size_t num_tiles_per_thread = 
        (tiles.size() + num_real_workers - 1) / num_real_workers;
    std::size_t total_tasks = 
        (tiles.size() + num_tiles_per_thread - 1) / num_tiles_per_thread;

    auto job_state = std::make_shared<JobState>();
    job_state->tasks_remaining = total_tasks;

    // ========================================================================
    // Phase 3: Submit Tile Processing Tasks to Worker Thread Pool
    // ========================================================================
    if (total_tasks > 1) {
        {
            std::lock_guard lock(queue_mutex_);
            
            for (size_t task_idx = 1; task_idx < total_tasks; ++task_idx) {
                // Each task captures:
                // - job_state by shared_ptr (keeps it alive)
                // - reader by reference (MUST wait for tasks before returning!)
                // - tiles by reference
                // - metadata, region, output_buffer by reference
                // - task_idx by value (each thread processes a range of tiles)
                // - num_tiles_per_thread by value
                pending_tasks_.push_back(
                    [&reader, &metadata, &region, output_buffer, &tiles, num_tiles_per_thread, task_idx, job_state]() {
                        CPULimitedReader::process_tile_task<OutSpec>(
                            reader,
                            metadata,
                            region,
                            output_buffer,
                            tiles,
                            num_tiles_per_thread,
                            task_idx,
                            job_state
                        );
                    }
                );
            }
        }
        queue_cv_.notify_all(); // Wake all worker threads
    }

    // Process first task on calling thread
    CPULimitedReader::process_tile_task<OutSpec>(
        reader,
        metadata,
        region,
        output_buffer,
        tiles,
        num_tiles_per_thread,
        0, // task_idx
        job_state
    );

    // ========================================================================
    // Phase 4: Wait for All Tiles to Complete
    // ========================================================================
    //
    // We MUST wait for all tile processing tasks to finish before returning:
    // 1. Tasks hold references to reader, metadata, region, and output_buffer
    // 2. Tasks hold a shared_ptr to job_state
    // 3. Returning early would invalidate these references, causing UB
    //
    // This wait ensures all workers have finished processing tiles for this job,
    // even if we're exiting early due to an error.
    {
        std::unique_lock lock(job_state->mutex);
        job_state->cv.wait(lock, [&] {
            return job_state->tasks_remaining == 0;
        });
        
        // Check if any errors occurred during processing
        if (job_state->error_occurred.load(std::memory_order_acquire)) {
            return job_state->first_error;
        }
    }

    return Ok();
}


template <typename PixelType, typename DecompSpec>
template <ImageLayoutSpec OutSpec, typename Reader, typename TagSpec>
requires RawReader<Reader> && (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
inline void CPULimitedReader<PixelType, DecompSpec>::process_tile_task(
    const Reader& reader,
    const ExtractedTags<TagSpec>& metadata,
    const ImageRegion& region,
    std::span<PixelType> output_buffer,
    const std::vector<Tile>& tiles,
    size_t num_tiles_per_thread,
    size_t task_idx,
    std::shared_ptr<CPULimitedReader<PixelType, DecompSpec>::JobState> job_state) noexcept {
{
    // RAII helper to ensure counter is always decremented
        struct TaskGuard {
            std::shared_ptr<CPULimitedReader<PixelType, DecompSpec>::JobState> state;
            ~TaskGuard() {
                std::lock_guard lock(state->mutex);
                state->tasks_remaining--;
                state->cv.notify_one();
            }
        };
        TaskGuard guard{job_state};
        
        // Early exit if another task already failed
        if (job_state->error_occurred.load(std::memory_order_acquire)) [[unlikely]] {
            return;
        }
        
        try {
            // Get thread-local decoder (initialized once per worker thread)
            thread_local TileDecoder<PixelType, DecompSpec> thread_decoder;

            // Get compression and predictor from tags
            CompressionScheme compression = optional::extract_tag_or<TagCode::Compression, TagSpec>(
                metadata, CompressionScheme::None
            );
            
            Predictor predictor = Predictor::None;
            if constexpr (TagSpec::template has_tag<TagCode::Predictor>()) {
                predictor = optional::extract_tag_or<TagCode::Predictor, TagSpec>(
                    metadata, Predictor::None
                );
            }

            for (size_t local_tile_idx = 0; local_tile_idx < num_tiles_per_thread; ++local_tile_idx) {
                size_t tile_idx = task_idx * num_tiles_per_thread + local_tile_idx;
                if (tile_idx >= tiles.size()) {
                    break;
                }
                const auto& tile = tiles[tile_idx];
            
                // Read compressed tile data
                auto read_res = reader.read(tile.location.offset, tile.location.length);
                if (!read_res) [[unlikely]] {
                    std::lock_guard lock(job_state->mutex);
                    bool expected = false;
                    if (job_state->error_occurred.compare_exchange_strong(
                            expected, true, 
                            std::memory_order_release, 
                            std::memory_order_relaxed)) {
                        job_state->first_error = read_res.error();
                    }
                    return;
                }

                // Decode tile (decompress + predictor)
                auto decode_res = thread_decoder.decode(
                    read_res.value().data(),
                    tile.id.size.width,
                    tile.id.size.height * tile.id.size.depth,
                    compression,
                    predictor,
                    tile.id.size.nsamples
                );
                if (!decode_res) [[unlikely]] {
                    std::lock_guard lock(job_state->mutex);
                    bool expected = false;
                    if (job_state->error_occurred.compare_exchange_strong(
                            expected, true, 
                            std::memory_order_release, 
                            std::memory_order_relaxed)) {
                        job_state->first_error = decode_res.error();
                    }
                    return;
                }

                // Extract tile data to output buffer (with layout conversion)
                auto extract_res = extract_tile_to_buffer<OutSpec, PixelType>(
                    tile, region, metadata, decode_res.value(), output_buffer
                );
                if (!extract_res) [[unlikely]] {
                    std::lock_guard lock(job_state->mutex);
                    bool expected = false;
                    if (job_state->error_occurred.compare_exchange_strong(
                            expected, true, 
                            std::memory_order_release, 
                            std::memory_order_relaxed)) {
                        job_state->first_error = extract_res.error();
                    }
                    return;
                }
            }
        } catch (...) {
            // Exception during processing (should not happen, but defend against it)
            std::lock_guard lock(job_state->mutex);
            bool expected = false;
            if (job_state->error_occurred.compare_exchange_strong(
                    expected, true, 
                    std::memory_order_release, 
                    std::memory_order_relaxed)) {
                job_state->first_error = Err(Error::Code::Unknown, 
                                            "Exception during tile processing");
            }
            // Note: TaskGuard will still decrement counter in destructor
        }
    }
}

} // namespace tiffconcept