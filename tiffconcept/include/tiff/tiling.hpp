#pragma once

#include <vector>
#include <span>
#include "metadata.hpp"
#include "result.hpp"
#include "types.hpp"
#include "image_shape.hpp"

namespace tiff {

/// Copy a tile (or a portion of a tile) into a larger image buffer
/// Template parameters control memory layout and enable compile-time optimization
/// @tparam PlanarConfig Whether tile data is chunky (interleaved) or planar (separate channels)
/// @tparam OutSpec Output buffer layout (DHWC, DCHW, or CDHW)
template <PlanarConfiguration PlanarConfig, OutputSpec OutSpec, typename PixelType>
inline void copy_tile_to_buffer(
    std::span<const PixelType> tile_data,
    std::span<PixelType> output_buffer,
    uint32_t tile_depth,
    uint32_t tile_height,
    uint32_t tile_width,
    uint16_t tile_nsamples,         // Number of samples (channels) in tile
    uint32_t tile_start_depth,      // Relative start depth in tile
    uint32_t tile_start_height,
    uint32_t tile_start_width,
    uint16_t tile_start_sample,     // Starting sample/channel in tile
    uint32_t output_depth,
    uint32_t output_height,
    uint32_t output_width,
    uint16_t output_nchans,         // Total channels in output buffer
    uint32_t output_start_depth,
    uint32_t output_start_height,
    uint32_t output_start_width,
    uint16_t output_start_chan,
    uint32_t copy_depth,
    uint32_t copy_height,
    uint32_t copy_width,
    uint16_t copy_nchans) noexcept
{
    assert (copy_depth + output_start_depth <= output_depth);
    assert (copy_height + output_start_height <= output_height);
    assert (copy_width + output_start_width <= output_width);
    assert (copy_nchans + output_start_chan <= output_nchans);
    assert (copy_depth + tile_start_depth <= tile_depth);
    assert (copy_height + tile_start_height <= tile_height);
    assert (copy_width + tile_start_width <= tile_width);
    assert (copy_nchans + tile_start_sample <= tile_nsamples);
    // Compile-time optimization: chuncked channels case
    if constexpr (PlanarConfig == PlanarConfiguration::Chunky) {
        // DHWC layout (depth, height, width, channels last)
        if constexpr (OutSpec == OutputSpec::DHWC) {
            // Chunky -> DHWC: chunky is already DHWC in memory. Copy the right region or the requested section.
            const std::size_t tile_slice_size = static_cast<std::size_t>(tile_width) * tile_height * tile_nsamples;
            const std::size_t output_slice_size = static_cast<std::size_t>(output_width) * output_height * output_nchans;

            if (copy_nchans == tile_nsamples && copy_nchans == output_nchans) {
                if (copy_width == tile_width && copy_width == output_width) {
                    if (copy_height == tile_height && copy_height == output_height) {
                        // HWC full slice copy
                        std::memcpy(
                            &output_buffer[static_cast<std::size_t>(output_start_depth) * output_slice_size],
                            &tile_data[static_cast<std::size_t>(tile_start_depth) * tile_slice_size],
                            copy_depth * tile_slice_size * sizeof(PixelType)
                        );
                        return;
                    } else {
                        // WC full slice copy
                        for (uint32_t d = 0; d < copy_depth; ++d) {
                            const std::size_t tile_slice_offset = static_cast<std::size_t>(tile_start_depth + d) * tile_slice_size;
                            const std::size_t output_slice_offset = static_cast<std::size_t>(output_start_depth + d) * output_slice_size;
                            
                            std::memcpy(
                                &output_buffer[output_slice_offset +
                                    static_cast<std::size_t>(output_start_height) * output_width * output_nchans],
                                &tile_data[tile_slice_offset +
                                    static_cast<std::size_t>(tile_start_height) * tile_width * tile_nsamples],
                                copy_height * tile_width * tile_nsamples * sizeof(PixelType)
                            );
                        }
                        return;
                    }
                } else {
                    // C full slice copy (most common case)
                    for (uint32_t d = 0; d < copy_depth; ++d) {
                        const std::size_t tile_slice_offset = static_cast<std::size_t>(tile_start_depth + d) * tile_slice_size;
                        const std::size_t output_slice_offset = static_cast<std::size_t>(output_start_depth + d) * output_slice_size;
                        
                        for (uint32_t h = 0; h < copy_height; ++h) {
                            const std::size_t tile_row_offset = tile_slice_offset +
                                static_cast<std::size_t>(tile_start_height + h) * tile_width * tile_nsamples +
                                static_cast<std::size_t>(tile_start_width) * tile_nsamples;
                            
                            const std::size_t output_row_offset = output_slice_offset +
                                static_cast<std::size_t>(output_start_height + h) * output_width * output_nchans +
                                static_cast<std::size_t>(output_start_width) * output_nchans;
                            
                            std::memcpy(
                                &output_buffer[output_row_offset],
                                &tile_data[tile_row_offset],
                                copy_width * tile_nsamples * sizeof(PixelType)
                            );
                        }
                    }
                    return;
                }
            }
            
            // Generic fallback
            for (uint32_t d = 0; d < copy_depth; ++d) {
                const std::size_t tile_slice_offset = static_cast<std::size_t>(tile_start_depth + d) * tile_slice_size;
                const std::size_t output_slice_offset = static_cast<std::size_t>(output_start_depth + d) * output_slice_size;
                
                for (uint32_t h = 0; h < copy_height; ++h) {
                    const std::size_t tile_row_offset = tile_slice_offset + 
                        static_cast<std::size_t>(tile_start_height + h) * tile_width * tile_nsamples +
                        static_cast<std::size_t>(tile_start_width) * tile_nsamples +
                        tile_start_sample;
                    
                    const std::size_t output_row_offset = output_slice_offset +
                        static_cast<std::size_t>(output_start_height + h) * output_width * output_nchans +
                        static_cast<std::size_t>(output_start_width) * output_nchans +
                        output_start_chan;
                    
                    // Copy pixel by pixel with channel selection
                    for (uint32_t w = 0; w < copy_width; ++w) {
                        std::memcpy(
                            &output_buffer[output_row_offset + w * output_nchans],
                            &tile_data[tile_row_offset + w * tile_nsamples],
                            copy_nchans * sizeof(PixelType)
                        );
                    }
                }
            }
        }
        // DCHW layout (depth, channels, height, width)
        else if constexpr (OutSpec == OutputSpec::DCHW) {
            const std::size_t tile_slice_size = static_cast<std::size_t>(tile_width) * tile_height * tile_nsamples;
            const std::size_t output_slice_size = static_cast<std::size_t>(output_width) * output_height * output_nchans;
            
            for (uint32_t d = 0; d < copy_depth; ++d) {
                const std::size_t tile_slice_offset = static_cast<std::size_t>(tile_start_depth + d) * tile_slice_size;
                const std::size_t output_slice_offset = static_cast<std::size_t>(output_start_depth + d) * output_slice_size;

                for (uint16_t c = 0; c < copy_nchans; ++c) {
                    const std::size_t output_chan_offset = output_slice_offset +
                        static_cast<std::size_t>(output_start_chan + c) * output_width * output_height;
                    
                    for (uint32_t h = 0; h < copy_height; ++h) {
                        const std::size_t tile_row_offset = tile_slice_offset +
                            static_cast<std::size_t>(tile_start_height + h) * tile_width * tile_nsamples +
                            static_cast<std::size_t>(tile_start_width) * tile_nsamples +
                            tile_start_sample + c;
                        
                        const std::size_t output_row_offset = output_chan_offset +
                            static_cast<std::size_t>(output_start_height + h) * output_width +
                            output_start_width;
                        
                        // Chunky source: need to stride through interleaved channels (unless single-channel)
                        if (tile_nsamples == 1) {
                            // Single-channel optimization
                            std::memcpy(
                                &output_buffer[output_row_offset],
                                &tile_data[tile_row_offset],
                                copy_width * sizeof(PixelType)
                            );
                        } else {
                            for (uint32_t w = 0; w < copy_width; ++w) {
                                output_buffer[output_row_offset + w] = tile_data[tile_row_offset + w * tile_nsamples];
                            }
                        }
                    }
                }
            }
        }
        // CDHW layout (channels, depth, height, width)
        else if constexpr (OutSpec == OutputSpec::CDHW) {
            const std::size_t tile_slice_size = static_cast<std::size_t>(tile_width) * tile_height * tile_nsamples;
            const std::size_t output_depth_size = static_cast<std::size_t>(output_width) * output_height * output_depth;
            
            for (uint16_t c = 0; c < copy_nchans; ++c) {
                const std::size_t output_chan_offset = static_cast<std::size_t>(output_start_chan + c) * output_depth_size;
                
                for (uint32_t d = 0; d < copy_depth; ++d) {
                    const std::size_t tile_slice_offset = static_cast<std::size_t>(tile_start_depth + d) * tile_slice_size;
                    const std::size_t output_slice_offset = output_chan_offset +
                        static_cast<std::size_t>(output_start_depth + d) * output_width * output_height;
                    
                    for (uint32_t h = 0; h < copy_height; ++h) {
                        const std::size_t tile_row_offset = tile_slice_offset +
                            static_cast<std::size_t>(tile_start_height + h) * tile_width * tile_nsamples +
                            static_cast<std::size_t>(tile_start_width) * tile_nsamples +
                            tile_start_sample + c;
                        
                        const std::size_t output_row_offset = output_slice_offset +
                            static_cast<std::size_t>(output_start_height + h) * output_width +
                            output_start_width;
                        
                        // Chunky source: stride through interleaved channels (unless single-channel)
                        if (tile_nsamples == 1) {
                            // Single-channel optimization
                            std::memcpy(
                                &output_buffer[output_row_offset],
                                &tile_data[tile_row_offset],
                                copy_width * sizeof(PixelType)
                            );
                        } else {
                            for (uint32_t w = 0; w < copy_width; ++w) {
                                output_buffer[output_row_offset + w] = tile_data[tile_row_offset + w * tile_nsamples];
                            }
                        }
                    }
                }
            }
        }
    }
    // Planar configuration
    else if constexpr (PlanarConfig == PlanarConfiguration::Planar) {
        assert (tile_nsamples == 1); // Tiles cannot have multiple samples in planar configuration
        assert (copy_nchans == 1);
        // DHWC layout (depth, height, width, channels last)
        if constexpr (OutSpec == OutputSpec::DHWC) {
            const std::size_t tile_slice_size = static_cast<std::size_t>(tile_width) * tile_height;
            const std::size_t output_slice_size = static_cast<std::size_t>(output_width) * output_height * output_nchans;
            
            for (uint32_t d = 0; d < copy_depth; ++d) {
                const std::size_t tile_slice_offset = static_cast<std::size_t>(tile_start_depth + d) * tile_slice_size;
                const std::size_t output_slice_offset = static_cast<std::size_t>(output_start_depth + d) * output_slice_size;
                
                for (uint32_t h = 0; h < copy_height; ++h) {
                    const std::size_t tile_row_offset = tile_slice_offset +
                        static_cast<std::size_t>(tile_start_height + h) * tile_width +
                        tile_start_width;

                    if (output_nchans == 1) {
                        // Single-channel optimization (for instance extracting only one plane)
                        std::memcpy(
                            &output_buffer[output_slice_offset +
                                static_cast<std::size_t>(output_start_height + h) * output_width +
                                output_start_width],
                            &tile_data[tile_row_offset],
                            copy_width * sizeof(PixelType)
                        );
                        continue;
                    }
                    
                    for (uint32_t w = 0; w < copy_width; ++w) {
                        const std::size_t output_pixel_offset = output_slice_offset +
                            static_cast<std::size_t>(output_start_height + h) * output_width * output_nchans +
                            static_cast<std::size_t>(output_start_width + w) * output_nchans +
                            output_start_chan;
                        
                        const std::size_t tile_plane_offset = static_cast<std::size_t>(tile_start_sample) * 
                            tile_width * tile_height * tile_depth;
                        output_buffer[output_pixel_offset] = tile_data[tile_plane_offset + tile_row_offset + w];
                        }
                    }
                }
            }
        }
        // DCHW layout (depth, channels, height, width)
        else if constexpr (OutSpec == OutputSpec::DCHW) {
            const std::size_t tile_plane_size = static_cast<std::size_t>(tile_width) * tile_height * tile_depth;
            const std::size_t output_slice_size = static_cast<std::size_t>(output_width) * output_height * output_nchans;
            
            for (uint32_t d = 0; d < copy_depth; ++d) {
                const std::size_t output_slice_offset = static_cast<std::size_t>(output_start_depth + d) * output_slice_size;
                
                const std::size_t tile_plane_offset = static_cast<std::size_t>(tile_start_sample) * tile_plane_size;
                const std::size_t tile_slice_offset = tile_plane_offset +
                    static_cast<std::size_t>(tile_start_depth + d) * tile_width * tile_height;
                
                const std::size_t output_chan_offset = output_slice_offset +
                    static_cast<std::size_t>(output_start_chan) * output_width * output_height;
                
                for (uint32_t h = 0; h < copy_height; ++h) {
                    const std::size_t tile_row_offset = tile_slice_offset +
                        static_cast<std::size_t>(tile_start_height + h) * tile_width +
                        tile_start_width;
                    
                    const std::size_t output_row_offset = output_chan_offset +
                        static_cast<std::size_t>(output_start_height + h) * output_width +
                        output_start_width;
                    
                    // Planar -> DCHW: can memcpy entire row
                    std::memcpy(
                        &output_buffer[output_row_offset],
                        &tile_data[tile_row_offset],
                        copy_width * sizeof(PixelType)
                    );
                }
            }
        }
        // CDHW layout (channels, depth, height, width)
        else if constexpr (OutSpec == OutputSpec::CDHW) {
            const std::size_t tile_plane_size = static_cast<std::size_t>(tile_width) * tile_height * tile_depth;
            const std::size_t output_depth_size = static_cast<std::size_t>(output_width) * output_height * output_depth;
            
            const std::size_t tile_plane_offset = static_cast<std::size_t>(tile_start_sample) * tile_plane_size;
            const std::size_t output_chan_offset = static_cast<std::size_t>(output_start_chan) * output_depth_size;
            
            for (uint32_t d = 0; d < copy_depth; ++d) {
                const std::size_t tile_slice_offset = tile_plane_offset +
                    static_cast<std::size_t>(tile_start_depth + d) * tile_width * tile_height;
                
                const std::size_t output_slice_offset = output_chan_offset +
                    static_cast<std::size_t>(output_start_depth + d) * output_width * output_height;
                
                for (uint32_t h = 0; h < copy_height; ++h) {
                    const std::size_t tile_row_offset = tile_slice_offset +
                        static_cast<std::size_t>(tile_start_height + h) * tile_width +
                        tile_start_width;
                    
                    const std::size_t output_row_offset = output_slice_offset +
                        static_cast<std::size_t>(output_start_height + h) * output_width +
                        output_start_width;
                    
                    // Planar -> CDHW: can memcpy entire row
                    std::memcpy(
                        &output_buffer[output_row_offset],
                        &tile_data[tile_row_offset],
                        copy_width * sizeof(PixelType)
                    );
                }
            }
        }
    }
}

/// Information about a single tile
struct TileInfo {
    uint32_t tile_x;        // Tile column index
    uint32_t tile_y;        // Tile row index
    uint32_t tile_z;        // Tile depth index
    uint32_t pixel_x;       // Starting pixel X coordinate in image
    uint32_t pixel_y;       // Starting pixel Y coordinate in image
    uint32_t pixel_z;       // Starting pixel Z coordinate in image
    uint32_t width;         // Tile width in pixels
    uint32_t height;        // Tile height in pixels
    uint32_t depth;         // Tile depth in pixels
    uint32_t offset;        // File offset to compressed data
    uint32_t byte_count;    // Size of compressed data
    uint32_t tile_index;    // Linear tile index
};

/// Reusable tiled image information structure
template <typename PixelType>
class TiledImageInfo {
private:
    ImageShape<PixelType> shape_;  // Common image properties
    
    uint32_t tile_width_;
    uint32_t tile_height_;
    uint32_t tile_depth_;       // Tile depth (for 3D images)
    
    std::vector<uint32_t> tile_offsets_;      // Owned data
    std::vector<uint32_t> tile_byte_counts_;  // Owned data
    
    CompressionScheme compression_;
    Predictor predictor_;
    
    uint32_t tiles_across_;
    uint32_t tiles_down_;
    uint32_t tiles_deep_;       // Number of tiles in Z direction
    
public:
    TiledImageInfo() noexcept
        : shape_()
        , tile_width_(0)
        , tile_height_(0)
        , tile_depth_(1)
        , tile_offsets_()
        , tile_byte_counts_()
        , compression_(CompressionScheme::None)
        , predictor_(Predictor::None)
        , tiles_across_(0)
        , tiles_down_(0)
        , tiles_deep_(1) {}
    
    /// Update this info to process a new image
    /// Copies metadata into owned storage (reuses allocations when possible)
    /// the original metadata object can be discarded after this call.
    template <typename TagSpec>
        requires TiledImageTagSpec<TagSpec>
    [[nodiscard]] Result<void> update_from_metadata(
        const metadata_type_t<TagSpec>& metadata) noexcept {
        
        // Extract common image shape first
        auto shape_result = shape_.update_from_metadata(metadata);
        if (!shape_result) {
            return shape_result;
        }
        
        // Extract tile-specific fields
        auto tile_width_val = extract_tag_value<TagCode::TileWidth, TagSpec>(metadata);
        auto tile_length_val = extract_tag_value<TagCode::TileLength, TagSpec>(metadata);
        auto tile_offsets_val = extract_tag_value<TagCode::TileOffsets, TagSpec>(metadata);
        auto tile_byte_counts_val = extract_tag_value<TagCode::TileByteCounts, TagSpec>(metadata);
        
        // Validation
        if (!is_value_present(tile_width_val)) {
            return Err(Error::Code::InvalidTag, "TileWidth tag not found");
        }
        if (!is_value_present(tile_length_val)) {
            return Err(Error::Code::InvalidTag, "TileLength tag not found");
        }
        if (!is_value_present(tile_offsets_val)) {
            return Err(Error::Code::InvalidTag, "TileOffsets tag not found");
        }
        if (!is_value_present(tile_byte_counts_val)) {
            return Err(Error::Code::InvalidTag, "TileByteCounts tag not found");
        }
        
        // Extract tile dimensions
        tile_width_ = unwrap_value(tile_width_val);
        tile_height_ = unwrap_value(tile_length_val);
        
        tile_depth_ = 1;
        if constexpr (TagSpec::template has_tag<TagCode::TileDepth>()) {
            tile_depth_ = extract_tag_or<TagCode::TileDepth, TagSpec>(
                metadata, uint32_t{1}
            );
        }
        
        // Copy vector data (reuses allocation if size matches)
        const auto& offsets = unwrap_value(tile_offsets_val);
        const auto& byte_counts = unwrap_value(tile_byte_counts_val);
        
        tile_offsets_.assign(offsets.begin(), offsets.end());
        tile_byte_counts_.assign(byte_counts.begin(), byte_counts.end());
        
        compression_ = extract_tag_or<TagCode::Compression, TagSpec>(
            metadata, CompressionScheme::None
        );
        
        predictor_ = Predictor::None;
        if constexpr (TagSpec::template has_tag<TagCode::Predictor>()) {
            predictor_ = extract_tag_or<TagCode::Predictor, TagSpec>(
                metadata, Predictor::None
            );
        }
        
        // Calculate derived values
        tiles_across_ = (shape_.image_width() + tile_width_ - 1) / tile_width_;
        tiles_down_ = (shape_.image_height() + tile_height_ - 1) / tile_height_;
        tiles_deep_ = (shape_.image_depth() + tile_depth_ - 1) / tile_depth_;
        
        return Ok();
    }
    
    // Image shape access
    [[nodiscard]] const ImageShape<PixelType>& shape() const noexcept { return shape_; }
    
    // Tile-specific getters
    [[nodiscard]] uint32_t tile_width() const noexcept { return tile_width_; }
    [[nodiscard]] uint32_t tile_height() const noexcept { return tile_height_; }
    [[nodiscard]] uint32_t tile_depth() const noexcept { return tile_depth_; }
    [[nodiscard]] uint32_t tiles_across() const noexcept { return tiles_across_; }
    [[nodiscard]] uint32_t tiles_down() const noexcept { return tiles_down_; }
    [[nodiscard]] uint32_t tiles_deep() const noexcept { return tiles_deep_; }
    [[nodiscard]] std::size_t num_tiles() const noexcept { return tile_offsets_.size(); }
    [[nodiscard]] CompressionScheme compression() const noexcept { return compression_; }
    [[nodiscard]] Predictor predictor() const noexcept { return predictor_; }
    
    [[nodiscard]] Result<TileInfo> get_tile_info(uint32_t tile_x, uint32_t tile_y, uint32_t plane = 0) const noexcept {
        return get_tile_info_3d(tile_x, tile_y, 0, plane);
    }
    
    [[nodiscard]] Result<TileInfo> get_tile_info_3d(uint32_t tile_x, uint32_t tile_y, uint32_t tile_z, uint32_t plane = 0) const noexcept {
        if (tile_x >= tiles_across_ || tile_y >= tiles_down_ || tile_z >= tiles_deep_) {
            return Err(Error::Code::OutOfBounds, "Tile coordinates out of bounds");
        }
        
        // For planar configuration, tiles are organized by plane:
        // [plane0_tile0, plane0_tile1, ..., plane1_tile0, plane1_tile1, ...]
        // For 3D, tiles are organized as [z=0 tiles, z=1 tiles, ...]
        uint32_t tiles_per_slice = tiles_across_ * tiles_down_;
        uint32_t tiles_per_plane = tiles_per_slice * tiles_deep_;
        uint32_t tile_index;
        
        if (shape_.planar_configuration() == PlanarConfiguration::Planar) {
            if (plane >= shape_.samples_per_pixel()) {
                return Err(Error::Code::OutOfBounds, "Plane index out of bounds");
            }
            tile_index = plane * tiles_per_plane + tile_z * tiles_per_slice + tile_y * tiles_across_ + tile_x;
        } else {
            // Chunky: ignore plane parameter, all data in one tile
            tile_index = tile_z * tiles_per_slice + tile_y * tiles_across_ + tile_x;
        }
        
        if (tile_index >= tile_offsets_.size()) {
            return Err(Error::Code::OutOfBounds, "Tile index out of bounds");
        }
        
        TileInfo info;
        info.tile_x = tile_x;
        info.tile_y = tile_y;
        info.tile_z = tile_z;
        info.pixel_x = tile_x * tile_width_;
        info.pixel_y = tile_y * tile_height_;
        info.pixel_z = tile_z * tile_depth_;
        info.width = std::min(tile_width_, shape_.image_width() - info.pixel_x);
        info.height = std::min(tile_height_, shape_.image_height() - info.pixel_y);
        info.depth = std::min(tile_depth_, shape_.image_depth() - info.pixel_z);
        info.offset = tile_offsets_[tile_index];
        info.byte_count = tile_byte_counts_[tile_index];
        info.tile_index = tile_index;
        
        return Ok(info);
    }
    
    [[nodiscard]] uint32_t tiles_per_plane() const noexcept {
        return tiles_across_ * tiles_down_;
    }
};

} // namespace tiff