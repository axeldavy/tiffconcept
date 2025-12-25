#pragma once

#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <shared_mutex>
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
        // Extract strip tags - check if they exist first
        const auto& rows_per_strip_opt = metadata.template get<TagCode::RowsPerStrip>();
        const auto& strip_offsets_opt = metadata.template get<TagCode::StripOffsets>();
        const auto& strip_byte_counts_opt = metadata.template get<TagCode::StripByteCounts>();
        
        // Check if all required strip tags are present
        using RowsPerStripTag = typename TagSpec::template get_tag<TagCode::RowsPerStrip>;
        using StripOffsetsTag = typename TagSpec::template get_tag<TagCode::StripOffsets>;
        using StripByteCountsTag = typename TagSpec::template get_tag<TagCode::StripByteCounts>;
        
        bool strips_available = true;
        if constexpr (RowsPerStripTag::is_optional) {
            if (!rows_per_strip_opt.has_value()) strips_available = false;
        }
        if constexpr (StripOffsetsTag::is_optional) {
            if (!strip_offsets_opt.has_value()) strips_available = false;
        }
        if constexpr (StripByteCountsTag::is_optional) {
            if (!strip_byte_counts_opt.has_value()) strips_available = false;
        }
        
        if (!strips_available) {
            return Err(Error::Code::InvalidTag, "No valid tile or strip tags found");
        }
        
        const auto& rows_per_strip = optional::unwrap_value(rows_per_strip_opt);
        const auto& strip_offsets = optional::unwrap_value(strip_offsets_opt);
        const auto& strip_byte_counts = optional::unwrap_value(strip_byte_counts_opt);
        
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
            thread_local std::vector<std::byte> encoded_tile_buffer;

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
            Result<std::span<const PixelType>> decode_res{std::span<const PixelType>{}};

            for (size_t local_tile_idx = 0; local_tile_idx < num_tiles_per_thread; ++local_tile_idx) {
                size_t tile_idx = task_idx * num_tiles_per_thread + local_tile_idx;
                if (tile_idx >= tiles.size()) {
                    break;
                }
                const auto& tile = tiles[tile_idx];
            
                // Read compressed tile data
                if constexpr (!Reader::read_must_allocate) {
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
                    decode_res = thread_decoder.decode(
                        read_res.value().data(),
                        tile.id.size.width,
                        tile.id.size.height * tile.id.size.depth,
                        compression,
                        predictor,
                        tile.id.size.nsamples
                    );
                } else {
                    // for cache locality, reuse buffer for each tile
                    // Ensure buffer is large enough
                    if (encoded_tile_buffer.size() < tile.location.length) {
                        encoded_tile_buffer.resize(tile.location.length);
                    }

                    auto read_res = reader.read_into(
                        encoded_tile_buffer.data(),
                        tile.location.offset, 
                        tile.location.length
                    );
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
                    decode_res = thread_decoder.decode(
                        std::span<const std::byte>(
                            encoded_tile_buffer.data(), 
                            tile.location.length
                        ),
                        tile.id.size.width,
                        tile.id.size.height * tile.id.size.depth,
                        compression,
                        predictor,
                        tile.id.size.nsamples
                    );
                }
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

// ============================================================================
// FastReader Implementation
// ============================================================================
//
// This is the "ultimate" TIFF reader combining the best strategies from both
// IOLimitedReader and CPULimitedReader, leveraging AsyncRawReader for optimal
// performance across all storage types.
//
// Design Overview:
// ----------------
// The FastReader achieves optimal performance by:
//
// 1. **Upfront Submission**: All read operations are submitted immediately via
//    async_read_into(), maximizing I/O queue depth before any processing begins.
//    This is crucial for network storage where latency dominates.
//
// 2. **Batching Strategy**: Adjacent tiles are grouped into larger read requests
//    to minimize round-trips on high-latency storage (NAS, cloud). Small gaps
//    are bridged to avoid issuing separate requests.
//
// 3. **Parallel Processing**: All threads (main + workers) compete to poll
//    completions and process them. This maximizes both I/O and CPU utilization.
//
// 4. **Non-blocking Workers**: Workers poll for completions without blocking,
//    allowing them to process work from any concurrent read_region() call.
//
// 5. **Thread-Local Decoders**: Each thread maintains its own decoder for
//    optimal cache locality and zero contention.
//
// Thread Model:
// -------------
// - Main thread: Submits all reads, then participates in processing
// - Worker threads: Continuously poll for completions from any job
// - All threads: Independent, no synchronization during processing
// - Completion: Lock-free atomic counters, mutex only for errors
//
// Performance Characteristics by Storage Type:
// ---------------------------------------------
//
// Local NVMe SSD:
// - Queue depth: 128+ concurrent operations
// - CPU utilization: ~100% (parallel decode)
// - I/O bandwidth: Fully saturated
// - Expected: 5-10 GB/s throughput
//
// SATA SSD:
// - Queue depth: 32-64 operations
// - CPU utilization: ~100%
// - I/O bandwidth: Fully saturated
// - Expected: 500-1500 MB/s throughput
//
// NAS (1GbE):
// - Batching: Critical (reduces 100+ RPCs to 10-20)
// - Queue depth: 4-8 batches in flight
// - Network: Fully utilized (~125 MB/s)
// - Expected: 100-120 MB/s throughput
//
// NAS (10GbE):
// - Batching: Essential
// - Queue depth: 16-32 batches
// - Network: Fully utilized (~1.25 GB/s)
// - Expected: 800-1200 MB/s throughput
//
// Cloud Storage (S3/GCS):
// - Batching: Critical (aggressive batching)
// - Queue depth: 50-100+ requests
// - Network: Depends on bandwidth
// - Expected: Varies (100 MB/s - 1 GB/s)
//
// Memory Model:
// -------------
// - Per batch: buffer_size (1-4 MB typically)
// - Per thread: decoder scratch buffer (~1-10 MB)
// - Total: O(num_batches × batch_size + num_threads × decoder_size)
//
// ============================================================================

template <typename PixelType, typename DecompSpec>
FastReader<PixelType, DecompSpec>::FastReader(Config config)
    : config_(config) {
    
    if (config_.worker_threads == 0) {
        config_.worker_threads = std::max(1u, std::thread::hardware_concurrency());
    }

    // minus one to account for main thread participation
    config_.worker_threads -= 1;
    
    // Spawn worker threads
    for (size_t i = 0; i < config_.worker_threads; ++i) {
        workers_.emplace_back(&FastReader::worker_loop, this);
    }
}

template <typename PixelType, typename DecompSpec>
FastReader<PixelType, DecompSpec>::~FastReader() {
    // Signal workers to stop
    stop_workers_.store(true, std::memory_order_release);
    
    // Wait for all workers to finish
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

template <typename PixelType, typename DecompSpec>
template <ImageLayoutSpec OutSpec, typename Reader, typename TagSpec>
requires AsyncRawReader<Reader> && (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
Result<void> FastReader<PixelType, DecompSpec>::read_region(
    const Reader& reader,
    const ExtractedTags<TagSpec>& metadata,
    const ImageRegion& region,
    std::span<PixelType> output_buffer) noexcept {

    // ========================================================================
    // Phase 0: A few RAII structures
    // ========================================================================

    /// @brief RAII wrapper for job registration/unregistration
    struct ActiveJobGuard {
        FastReader* reader;
        std::shared_ptr<ActiveJob> job;
        
        ActiveJobGuard(FastReader* r, std::shared_ptr<JobQueue> queue) 
            : reader(r), job(std::make_shared<ActiveJob>()) {
            job->job_queue = std::move(queue);
            job->active.store(true, std::memory_order_release);
            reader->register_active_job(job);
        }
        
        ~ActiveJobGuard() {
            reader->unregister_active_job(job);
        }
        
        std::shared_ptr<JobQueue> queue() const { return job->job_queue; }
    };

    /// @brief RAII wrapper for batch submission and cleanup
    struct BatchSubmissionGuard {
        const Reader& reader;
        const std::vector<Batch>& batches;
        std::size_t next_batch_idx = 0;
        std::vector<uint64_t> submission_handles;
        std::vector<std::shared_ptr<std::byte[]>> storage_buffers;
        
        BatchSubmissionGuard(const Reader& r, const std::vector<Batch>& b)
            : reader(r), batches(b) {
            submission_handles.resize(batches.size());
            storage_buffers.resize(batches.size());
        }
        
        ~BatchSubmissionGuard() {
            if (reader.flush_async_operations()) {
                // Wait for any pending operations to complete
                while (reader.pending_operations() > 0) {
                    (void)reader.wait_completions(1);
                }
            }
        }
        
        Result<void> submit_next_batches() {
            return FastReader<PixelType, DecompSpec>::submit_batches(
                reader, batches, next_batch_idx, 
                submission_handles, storage_buffers
            );
        }
        
        std::shared_ptr<std::byte[]> get_batch_buffer(uint64_t handle) {
            for (std::size_t i = 0; i < batches.size(); ++i) {
                if (submission_handles[i] == handle) {
                    return std::move(storage_buffers[i]);
                }
            }
            return nullptr;
        }
        
        std::size_t find_batch_index(uint64_t handle) const {
            for (std::size_t i = 0; i < batches.size(); ++i) {
                if (submission_handles[i] == handle) {
                    return i;
                }
            }
            return batches.size(); // Invalid index
        }
    };
    
    // ========================================================================
    // Phase 1: Collect tiles and create batches
    // ========================================================================
    
    std::vector<Tile> tiles;
    auto collect_res = collect_tiles_for_region(region, metadata, tiles);
    if (!collect_res) {
        return collect_res;
    }
    
    if (tiles.empty()) {
        return Ok();
    }
    
    std::vector<Batch> batches;
    auto hint_batch_size_result = reader.hint_batch_size();
    std::size_t batch_size_hint = hint_batch_size_result ? hint_batch_size_result.value() : 4 * 1024 * 1024;
    create_batches(tiles, config_, batch_size_hint, batches);
    
    if (batches.empty()) {
        return Ok();
    }
    
    // ========================================================================
    // Phase 2: Setup job state and register with worker pool (RAII)
    // ========================================================================
    
    auto job_state = std::make_shared<JobState>();
    job_state->tiles_total.store(tiles.size(), std::memory_order_release);
    
    auto job_queue = std::make_shared<JobQueue>(tiles.size());
    ActiveJobGuard job_guard{this, job_queue};
    
    // ========================================================================
    // Phase 3: Setup batch submission state (RAII)
    // ========================================================================
    
    BatchSubmissionGuard batch_guard{reader, batches};
    
    // ========================================================================
    // Phase 4: Initial submission
    // ========================================================================
    
    auto submit_res = batch_guard.submit_next_batches();
    if (submit_res.is_error()) {
        return submit_res.error();
    }
    
    // ========================================================================
    // Phase 5: Main processing loop
    // ========================================================================
    
    auto record_error = [&job_state](Error error) {
        bool expected = false;
        if (job_state->error_occurred.compare_exchange_strong(
                expected, true,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            std::lock_guard lock(job_state->error_mutex);
            job_state->first_error = error;
        }
    };
    
    auto process_completion = [&](uint64_t handle, Result<size_t> io_result) -> bool {
        std::size_t batch_idx = batch_guard.find_batch_index(handle);
        if (batch_idx >= batches.size()) {
            return false; // Invalid handle
        }

        if (io_result.is_error()) {
            record_error(io_result.error());
            return false;
        }

        if (io_result.value() != batches[batch_idx].total_size) {
            record_error(Error(Error::Code::ReadError, 
                             "Incomplete read for batch"));
            return false;
        }
        
        const auto& batch = batches[batch_idx];
        auto batch_buffer = batch_guard.get_batch_buffer(handle);
        
        // Create and queue tile jobs for this batch
        for (size_t tile_idx = batch.first_tile_index;
             tile_idx < batch.first_tile_index + batch.tile_count;
             ++tile_idx) {
            
            const auto& tile = tiles[tile_idx];
            
            // Create tile job with processing lambda
            TileJob tile_job([&tile, &metadata, &region, output_buffer, 
                             job_state, batch_buffer, batch]() {
                
                std::size_t tile_offset_in_batch = 
                    tile.location.offset - batch.file_offset;
                
                std::span<const std::byte> compressed_data(
                    batch_buffer.get() + tile_offset_in_batch,
                    tile.location.length
                );
                
                auto result = FastReader<PixelType, DecompSpec>::process_tile<OutSpec, TagSpec>(
                    tile, metadata, region, output_buffer, 
                    compressed_data
                );
                
                if (result.is_error()) {
                    bool expected = false;
                    if (job_state->error_occurred.compare_exchange_strong(
                            expected, true,
                            std::memory_order_release,
                            std::memory_order_relaxed)) {
                        std::lock_guard lock(job_state->error_mutex);
                        job_state->first_error = result.error();
                    }
                }
                
                job_state->tiles_completed.fetch_add(1, std::memory_order_release);
            });
            
            job_guard.queue()->push(std::move(tile_job));
        }

        // Wake workers after adding jobs, unless there is just work for one
        if (job_guard.queue()->size() > 1) {
            worker_wake_cv_.notify_all();
        }
        
        return true;
    };
    
    auto process_one_available_tile = [&]() {
        TileJob tile_job;
        if (job_guard.queue()->try_pop(tile_job)) {
            if (tile_job.process) {
                tile_job.process();
            }
            
            if (job_state->error_occurred.load(std::memory_order_acquire)) {
                return false;
            }
        }
        return true;
    };
    
    // Main loop: submit batches and process completions
    while (job_state->tiles_completed.load(std::memory_order_acquire) < tiles.size()) {
        // Check for errors
        if (job_state->error_occurred.load(std::memory_order_acquire)) {
            break;
        }
        
        // Submit more batches if available
        auto submit_res = batch_guard.submit_next_batches();
        if (submit_res.is_error()) {
            record_error(submit_res.error());
            break;
        }
        
        // Poll for completions
        auto completions = reader.poll_completions(0);
        
        if (completions.empty() && job_queue.get()->empty()) {
            // Wait briefly for completions
            completions = reader.wait_completions_for(
                std::chrono::milliseconds(1), 0
            );
            
            if (completions.empty()) {
                continue;
            }
        }
        
        // Process all completions
        for (auto& [handle, result] : completions) {
            process_completion(handle, result);
        }
        
        // Process one tile (not all of them to avoid worker starvation)
        if (!process_one_available_tile()) {
            break;
        }
    }
    
    // Note: BatchSubmissionGuard destructor will wait for pending I/O
    // and ActiveJobGuard destructor will unregister the job
    
    // ========================================================================
    // Phase 6: Return final result
    // ========================================================================
    
    if (job_state->error_occurred.load(std::memory_order_acquire)) {
        std::lock_guard lock(job_state->error_mutex);
        return job_state->first_error;
    }
    
    return Ok();
}


template <typename PixelType, typename DecompSpec>
void FastReader<PixelType, DecompSpec>::register_active_job(std::shared_ptr<ActiveJob> job) {
    std::unique_lock lock(active_jobs_mutex_);
    active_jobs_.push_back(job);
}

template <typename PixelType, typename DecompSpec>
void FastReader<PixelType, DecompSpec>::unregister_active_job(std::shared_ptr<ActiveJob> job) {
    std::unique_lock lock(active_jobs_mutex_);
    job->active.store(false, std::memory_order_release);
    active_jobs_.erase(
        std::remove(active_jobs_.begin(), active_jobs_.end(), job),
        active_jobs_.end()
    );
}

template <typename PixelType, typename DecompSpec>
bool FastReader<PixelType, DecompSpec>::try_steal_tile(TileJob& out_job) {
    std::shared_lock lock(active_jobs_mutex_);
    
    // Try to steal from any active job
    for (auto& job : active_jobs_) {
        if (job->active.load(std::memory_order_acquire) && 
            job->job_queue->try_pop(out_job)) {
            return true;
        }
    }
    
    return false;
}

template <typename PixelType, typename DecompSpec>
void FastReader<PixelType, DecompSpec>::worker_loop() {
    while (!stop_workers_.load(std::memory_order_acquire)) {
        TileJob job;
        
        // Try to steal a tile from any active job
        if (!try_steal_tile(job)) {
            // No work available, wait for notification or timeout
            std::unique_lock<std::mutex> lock(worker_wake_mutex_);
            // The try_steal_tile() check above handles spurious wakeups
            worker_wake_cv_.wait_for(lock, std::chrono::milliseconds(50));
            continue;
        }
        
        // Execute the tile processing function
        if (job.process) {
            job.process();
        }
    }
}

template <typename PixelType, typename DecompSpec>
void FastReader<PixelType, DecompSpec>::create_batches(
    const std::vector<Tile>& tiles,
    const Config& config,
    std::size_t max_batch_size_hint,
    std::vector<Batch>& out_batches) {
    
    // Same batching algorithm as IOLimitedReader
    // Group adjacent tiles to minimize I/O round-trips
    
    if (tiles.empty()) {
        return;
    }
    
    size_t start_idx = 0;
    size_t current_offset = tiles[0].location.offset;
    size_t current_end = current_offset + tiles[0].location.length;
    //size_t tot_size_before = 0; // sum of size of batches before this batch
    std::size_t max_batch_size = config.max_batch_size == 0 ? 
                                 max_batch_size_hint : 
                                 config.max_batch_size;
    std::size_t max_gap_size = std::min(config.max_gap_size, max_batch_size / 2);
    
    for (size_t i = 1; i < tiles.size(); ++i) {
        const auto& tile = tiles[i];
        
        // Calculate gap and potential new batch size
        size_t gap = tile.location.offset - current_end;
        size_t new_end = tile.location.offset + tile.location.length;
        size_t new_size = new_end - current_offset;
        
        // Break batch if gap is too large or size exceeds limit
        bool break_batch = (gap > max_gap_size) ||
                          (new_size > max_batch_size);
        
        if (break_batch) {
            // Finalize current batch (do not include i).
            out_batches.push_back({
                start_idx,
                i - start_idx,
                current_offset,
                current_end - current_offset//,
                //tot_size_before
            });
            //tot_size_before += current_end - current_offset;

            // Start new batch. Each batch has at least one item.
            start_idx = i;
            current_offset = tile.location.offset;
            current_end = tile.location.offset + tile.location.length;
        } else {
            // Extend current batch
            current_end = new_end;
        }
    }
    
    // Finalize last batch
    out_batches.push_back({
        start_idx,
        tiles.size() - start_idx,
        current_offset,
        current_end - current_offset//,
        //tot_size_before
    });
}

template <typename PixelType, typename DecompSpec>
template <typename Reader>
requires AsyncRawReader<Reader>
Result<void> FastReader<PixelType, DecompSpec>::submit_batches(
    const Reader& reader,
    const std::vector<Batch>& batches,
    std::size_t& next_batch_idx,
    std::vector<uint64_t>& submission_handles,
    std::vector<std::shared_ptr<std::byte[]>>& storage_buffer) noexcept {
    
    if (next_batch_idx >= batches.size()) {
        return Ok();
    }
    
    size_t submission_limit = reader.available_async_ops();
    if (submission_limit == 0) {
        return Ok(); // No capacity to submit more yet
    }
    
    // Submit as many batches as possible within queue depth limit
    while (next_batch_idx < batches.size() && submission_limit > 0) {
        const auto& batch = batches[next_batch_idx];
        
        // Allocate memory for this batch
        auto batch_buffer = std::shared_ptr<std::byte[]>(new std::byte[batch.total_size]);
        std::span<std::byte> buffer_span(batch_buffer.get(), batch.total_size);
        
        // Submit async read
        auto handle_res = reader.async_read_into(
            buffer_span,
            batch.file_offset,
            batch.total_size
        );
        
        if (!handle_res) {
            return handle_res.error();
        }
        
        // Store handle and buffer
        submission_handles[next_batch_idx] = handle_res.value();
        storage_buffer[next_batch_idx] = std::move(batch_buffer);
        ++next_batch_idx;
        --submission_limit;
    }
    
    // Flush submissions
    auto flush_res = reader.flush_async_operations();
    if (!flush_res) {
        return flush_res.error();
    }
    
    return Ok();
}

template <typename PixelType, typename DecompSpec>
TileDecoder<PixelType, DecompSpec>& FastReader<PixelType, DecompSpec>::get_decoder() noexcept {
    // Get thread-local decoder (initialized once per worker thread)
    thread_local TileDecoder<PixelType, DecompSpec> thread_decoder;
    return thread_decoder;
}

template <typename PixelType, typename DecompSpec>
template <ImageLayoutSpec OutSpec, typename TagSpec>
requires (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
Result<void> FastReader<PixelType, DecompSpec>::process_tile(
    const Tile& tile,
    const ExtractedTags<TagSpec>& metadata,
    const ImageRegion& region,
    std::span<PixelType> output_buffer,
    std::span<const std::byte> compressed_data) noexcept {
    
    // Get thread-local decoder (initialized once per worker thread)
    auto& thread_decoder = get_decoder();
    
    // Extract compression and predictor from metadata (cheap operation)
    CompressionScheme compression = optional::extract_tag_or<TagCode::Compression, TagSpec>(
        metadata, CompressionScheme::None
    );
    
    Predictor predictor = Predictor::None;
    if constexpr (TagSpec::template has_tag<TagCode::Predictor>()) {
        predictor = optional::extract_tag_or<TagCode::Predictor, TagSpec>(
            metadata, Predictor::None
        );
    }
    
    // Decode tile
    auto decode_res = thread_decoder.decode(
        compressed_data,
        tile.id.size.width,
        tile.id.size.height * tile.id.size.depth,
        compression,
        predictor,
        tile.id.size.nsamples
    );
    
    if (decode_res.is_error()) {
        return decode_res.error();
    }
    
    // Extract to output buffer
    auto extract_res = extract_tile_to_buffer<OutSpec, PixelType>(
        tile,
        region,
        metadata,
        decode_res.value(),
        output_buffer
    );
    
    if (extract_res.is_error()) {
        return extract_res.error();
    }
    
    return Ok();
}


} // namespace tiffconcept
