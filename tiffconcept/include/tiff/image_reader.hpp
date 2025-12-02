#pragma once

#include <vector>
#include <span>
#include "decoder.hpp"
#include "tiling.hpp"
#include "strips.hpp"
#include "result.hpp"
#include "reader_base.hpp"

namespace tiff {


/// Output layout specification when extracting image data
enum class OutputSpec {
    DHWC,  // Depth, Height, Width, Channels
    DCHW,  // Depth, Channels, Height, Width
    CDHW,  // Channels, Depth, Height, Width
};

// OutputSpec defines the order of the dimensions when
// writing to the output buffer.
// ------------------------
// 2D single-channel images:
// By default the depth and channel dimensions are 1.
// Thus any OutputSpec results in the same layout: HW.
// ------------------------
// 2D multi-channel images:
// DHWC: HWC (channels last)
// DCHW/CDHW: CHW (channels first)
// ------------------------
// Best OutputSpec for performance ?
// The OutputSpec can impact extraction performance, and the
// best choice depends on your image layout.
// PlanarConfiguration::Chunky -> DHWC will be best
// PlanarConfiguration::Planar
//     -> DCHW/CDHW will be best. The best between DCHW and CDHW
//        depends on which channels/depths sections you extract.
// If you can control the encoding of the Tiff image, unless the
// channels are strongly correlated, PlanarConfiguration::Planar
// is generally preferred for better compression ratios.

/// Generic image reader that can handle both tiled and stripped images
/// Contains reusable resources (decoder, thread pools, I/O strategies)
/// Can read multiple images without reallocation by using TiledImageInfo or StrippedImageInfo
template <typename PixelType, typename DecompSpec>
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
    // Reusable decoder (contains all decompressor storage and scratch buffer)
    mutable ChunkDecoder<PixelType, DecompSpec> decoder_;

public:
    explicit ImageReader() : decoder_() {}
    
    /// Low-level method to read tiles and assemble into output buffer
    /// The buffer is assumed to represent the region [region_start_x, region_start_y] to
    /// [region_start_x + output_width, region_start_y + output_height]
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<void> read_tiles_to_buffer(
        const Reader& reader,
        const TiledImageInfo<PixelType>& image_info,
        std::span<PixelType> output_buffer,
        uint32_t output_width,
        uint32_t output_height,
        uint32_t region_start_x,
        uint32_t region_start_y) const noexcept {
        
        // Determine which tiles we need
        uint32_t tile_width = image_info.tile_width();
        uint32_t tile_height = image_info.tile_height();
        uint32_t start_tile_x = region_start_x / tile_width;
        uint32_t start_tile_y = region_start_y / tile_height;
        uint32_t end_tile_x = (region_start_x + output_width + tile_width - 1) / tile_width;
        uint32_t end_tile_y = (region_start_y + output_height + tile_height - 1) / tile_height;
        
        // Clamp to actual tile bounds
        end_tile_x = std::min(end_tile_x, image_info.tiles_across());
        end_tile_y = std::min(end_tile_y, image_info.tiles_down());
        
        uint16_t samples_per_pixel = image_info.samples_per_pixel();
        PlanarConfiguration planar_config = image_info.planar_configuration();
        
        // For planar configuration, we need to read each plane separately
        uint32_t num_planes = (planar_config == PlanarConfiguration::Planar) ? samples_per_pixel : 1;
        uint16_t samples_per_tile = (planar_config == PlanarConfiguration::Planar) ? 1 : samples_per_pixel;
        
        for (uint32_t plane = 0; plane < num_planes; ++plane) {
            for (uint32_t tile_y = start_tile_y; tile_y < end_tile_y; ++tile_y) {
                for (uint32_t tile_x = start_tile_x; tile_x < end_tile_x; ++tile_x) {
                    auto info_result = image_info.get_tile_info(tile_x, tile_y, plane);
                    if (!info_result) {
                        return Err(info_result.error().code, info_result.error().message);
                    }
                    
                    const auto& info = info_result.value();
                    
                    if (info.byte_count == 0) {
                        continue; // Empty tile
                    }
                    
                    // Read compressed data
                    auto view_result = reader.read(info.offset, info.byte_count);
                    if (!view_result) {
                        return Err(view_result.error().code, "Failed to read tile data");
                    }
                    auto view = std::move(view_result.value());
                    
                    // Decode tile using reusable decoder
                    auto decode_result = decoder_.decode(
                        view.data(),
                        tile_width,
                        tile_height,
                        image_info.compression(),
                        image_info.predictor(),
                        samples_per_tile
                    );
                    if (!decode_result) {
                        return Err(decode_result.error().code, decode_result.error().message);
                    }
                    
                    auto tile_data = decode_result.value();
                    
                    // Calculate intersection between tile and region
                    uint32_t src_start_x = (region_start_x > info.pixel_x) ? 
                        (region_start_x - info.pixel_x) : 0;
                    uint32_t src_start_y = (region_start_y > info.pixel_y) ? 
                        (region_start_y - info.pixel_y) : 0;
                    
                    uint32_t dst_start_x = (info.pixel_x > region_start_x) ? 
                        (info.pixel_x - region_start_x) : 0;
                    uint32_t dst_start_y = (info.pixel_y > region_start_y) ? 
                        (info.pixel_y - region_start_y) : 0;
                    
                    uint32_t copy_width = std::min(
                        info.width - src_start_x,
                        output_width - dst_start_x
                    );
                    
                    uint32_t copy_height = std::min(
                        info.height - src_start_y,
                        output_height - dst_start_y
                    );
                    
                    // Copy tile to output based on planar configuration
                    if (planar_config == PlanarConfiguration::Planar) {
                        copy_tile_to_buffer_planar(
                            tile_data, output_buffer,
                            tile_width, output_width,
                            src_start_x, src_start_y,
                            dst_start_x, dst_start_y,
                            copy_width, copy_height,
                            plane, samples_per_pixel
                        );
                    } else {
                        copy_tile_to_buffer_chunky(
                            tile_data, output_buffer,
                            tile_width, output_width,
                            src_start_x, src_start_y,
                            dst_start_x, dst_start_y,
                            copy_width, copy_height,
                            samples_per_pixel
                        );
                    }
                }
            }
        }
        
        return Ok();
    }
    
    /// Low-level method to read strips and assemble into output buffer
    /// The buffer is assumed to represent the region [region_start_x, region_start_y] to
    /// [region_start_x + output_width, region_start_y + output_height]
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<void> read_strips_to_buffer(
        const Reader& reader,
        const StrippedImageInfo<PixelType>& image_info,
        std::span<PixelType> output_buffer,
        uint32_t output_width,
        uint32_t output_height,
        uint32_t region_start_x,
        uint32_t region_start_y) const noexcept {
        
        // Determine which strips we need
        uint32_t rows_per_strip = image_info.rows_per_strip();
        uint32_t strips_per_plane = image_info.strips_per_plane();
        uint32_t start_strip = region_start_y / rows_per_strip;
        uint32_t end_strip = (region_start_y + output_height + rows_per_strip - 1) / rows_per_strip;
        
        // Clamp to actual strip bounds per plane
        end_strip = std::min(end_strip, strips_per_plane);
        
        uint16_t samples_per_pixel = image_info.samples_per_pixel();
        PlanarConfiguration planar_config = image_info.planar_configuration();
        
        // For planar configuration, we need to read each plane separately
        uint32_t num_planes = (planar_config == PlanarConfiguration::Planar) ? samples_per_pixel : 1;
        uint16_t samples_per_strip = (planar_config == PlanarConfiguration::Planar) ? 1 : samples_per_pixel;
        
        for (uint32_t plane = 0; plane < num_planes; ++plane) {
            for (uint32_t strip_idx = start_strip; strip_idx < end_strip; ++strip_idx) {
                auto info_result = image_info.get_strip_info(strip_idx, plane);
                if (!info_result) {
                    return Err(info_result.error().code, info_result.error().message);
                }
                
                const auto& info = info_result.value();
                
                if (info.byte_count == 0) {
                    continue; // Empty strip
                }
                
                // Read compressed data
                auto view_result = reader.read(info.offset, info.byte_count);
                if (!view_result) {
                    return Err(view_result.error().code, "Failed to read strip data");
                }
                auto view = std::move(view_result.value());
                
                // Decode strip using reusable decoder
                auto decode_result = decoder_.decode(
                    view.data(),
                    info.width,
                    info.height,
                    image_info.compression(),
                    image_info.predictor(),
                    samples_per_strip
                );
                if (!decode_result) {
                    return Err(decode_result.error().code, decode_result.error().message);
                }
                
                auto strip_data = decode_result.value();
                
                // Calculate intersection between strip and region
                uint32_t src_start_x = (region_start_x > 0) ? region_start_x : 0;
                uint32_t src_start_y = (region_start_y > info.pixel_y) ? 
                    (region_start_y - info.pixel_y) : 0;
                
                uint32_t dst_start_x = 0;
                uint32_t dst_start_y = (info.pixel_y > region_start_y) ? 
                    (info.pixel_y - region_start_y) : 0;
                
                uint32_t copy_width = std::min(
                    info.width - src_start_x,
                    output_width - dst_start_x
                );
                
                uint32_t copy_height = std::min(
                    info.height - src_start_y,
                    output_height - dst_start_y
                );
                
                // Copy strip to output based on planar configuration
                if (planar_config == PlanarConfiguration::Planar) {
                    copy_tile_to_buffer_planar(
                        strip_data, output_buffer,
                        info.width, output_width,
                        src_start_x, src_start_y,
                        dst_start_x, dst_start_y,
                        copy_width, copy_height,
                        plane, samples_per_pixel
                    );
                } else {
                    copy_tile_to_buffer_chunky(
                        strip_data, output_buffer,
                        info.width, output_width,
                        src_start_x, src_start_y,
                        dst_start_x, dst_start_y,
                        copy_width, copy_height,
                        samples_per_pixel
                    );
                }
            }
        }
        
        return Ok();
    }

    // High-level methods
    
    /// Read entire image using provided reader and image info
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<std::vector<PixelType>> read_image(
        const Reader& reader,
        const TiledImageInfo<PixelType>& image_info) const noexcept {
        
        std::vector<PixelType> image(
            image_info.image_width() * image_info.image_height() * image_info.samples_per_pixel(), 0
        );
        
        auto result = read_tiles_to_buffer(
            reader,
            image_info,
            std::span(image),
            image_info.image_width(),
            image_info.image_height(),
            0, 0
        );
        
        if (!result) {
            return Err(result.error().code, result.error().message);
        }
        
        return Ok(std::move(image));
    }
    
    /// Read region using provided reader and image info
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<std::vector<PixelType>> read_region(
        const Reader& reader,
        const TiledImageInfo<PixelType>& image_info,
        uint32_t start_x, uint32_t start_y,
        uint32_t width, uint32_t height) const noexcept {
        
        if (start_x + width > image_info.image_width() || 
            start_y + height > image_info.image_height()) {
            return Err(Error::Code::OutOfBounds, "Region exceeds image bounds");
        }
        
        std::vector<PixelType> region(width * height * image_info.samples_per_pixel(), 0);
        
        auto result = read_tiles_to_buffer(
            reader,
            image_info,
            std::span(region),
            width, height,
            start_x, start_y
        );
        
        if (!result) {
            return Err(result.error().code, result.error().message);
        }
        
        return Ok(std::move(region));
    }
    
    /// Read entire stripped image using provided reader and image info
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<std::vector<PixelType>> read_image(
        const Reader& reader,
        const StrippedImageInfo<PixelType>& image_info) const noexcept {
        
        std::vector<PixelType> image(
            image_info.image_width() * image_info.image_height() * image_info.samples_per_pixel(), 0
        );
        
        auto result = read_strips_to_buffer(
            reader,
            image_info,
            std::span(image),
            image_info.image_width(),
            image_info.image_height(),
            0, 0
        );
        
        if (!result) {
            return Err(result.error().code, result.error().message);
        }
        
        return Ok(std::move(image));
    }
    
    /// Read region from stripped image using provided reader and image info
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<std::vector<PixelType>> read_region(
        const Reader& reader,
        const StrippedImageInfo<PixelType>& image_info,
        uint32_t start_x, uint32_t start_y,
        uint32_t width, uint32_t height) const noexcept {
        
        if (start_x + width > image_info.image_width() || 
            start_y + height > image_info.image_height()) {
            return Err(Error::Code::OutOfBounds, "Region exceeds image bounds");
        }
        
        std::vector<PixelType> region(width * height * image_info.samples_per_pixel(), 0);
        
        auto result = read_strips_to_buffer(
            reader,
            image_info,
            std::span(region),
            width, height,
            start_x, start_y
        );
        
        if (!result) {
            return Err(result.error().code, result.error().message);
        }
        
        return Ok(std::move(region));
    }
    
    // ===== 3D Image Reading Methods =====
    
    /// Low-level method to read 3D tiles and assemble into output buffer
    /// The buffer represents a 3D volume [region_start_x, region_start_y, region_start_z] to
    /// [region_start_x + output_width, region_start_y + output_height, region_start_z + output_depth]
    /// Buffer layout: [z=0 slice, z=1 slice, ...] where each slice is width*height*samples
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<void> read_tiles_to_buffer_3d(
        const Reader& reader,
        const TiledImageInfo<PixelType>& image_info,
        std::span<PixelType> output_buffer,
        uint32_t output_width,
        uint32_t output_height,
        uint32_t output_depth,
        uint32_t region_start_x,
        uint32_t region_start_y,
        uint32_t region_start_z) const noexcept {
        
        // Determine which tiles we need
        uint32_t tile_width = image_info.tile_width();
        uint32_t tile_height = image_info.tile_height();
        uint32_t tile_depth = image_info.tile_depth();
        
        uint32_t start_tile_x = region_start_x / tile_width;
        uint32_t start_tile_y = region_start_y / tile_height;
        uint32_t start_tile_z = region_start_z / tile_depth;
        
        uint32_t end_tile_x = (region_start_x + output_width + tile_width - 1) / tile_width;
        uint32_t end_tile_y = (region_start_y + output_height + tile_height - 1) / tile_height;
        uint32_t end_tile_z = (region_start_z + output_depth + tile_depth - 1) / tile_depth;
        
        // Clamp to actual tile bounds
        end_tile_x = std::min(end_tile_x, image_info.tiles_across());
        end_tile_y = std::min(end_tile_y, image_info.tiles_down());
        end_tile_z = std::min(end_tile_z, image_info.tiles_deep());
        
        uint16_t samples_per_pixel = image_info.samples_per_pixel();
        PlanarConfiguration planar_config = image_info.planar_configuration();
        
        // For planar configuration, we need to read each plane separately
        uint32_t num_planes = (planar_config == PlanarConfiguration::Planar) ? samples_per_pixel : 1;
        uint16_t samples_per_tile = (planar_config == PlanarConfiguration::Planar) ? 1 : samples_per_pixel;
        
        for (uint32_t plane = 0; plane < num_planes; ++plane) {
            for (uint32_t tile_z = start_tile_z; tile_z < end_tile_z; ++tile_z) {
                for (uint32_t tile_y = start_tile_y; tile_y < end_tile_y; ++tile_y) {
                    for (uint32_t tile_x = start_tile_x; tile_x < end_tile_x; ++tile_x) {
                        auto info_result = image_info.get_tile_info_3d(tile_x, tile_y, tile_z, plane);
                        if (!info_result) {
                            return Err(info_result.error().code, info_result.error().message);
                        }
                        
                        const auto& info = info_result.value();
                        
                        if (info.byte_count == 0) {
                            continue; // Empty tile
                        }
                        
                        // Read compressed data
                        auto view_result = reader.read(info.offset, info.byte_count);
                        if (!view_result) {
                            return Err(view_result.error().code, "Failed to read tile data");
                        }
                        auto view = std::move(view_result.value());
                        
                        // Decode tile using reusable decoder
                        auto decode_result = decoder_.decode(
                            view.data(),
                            tile_width,
                            tile_height * tile_depth,  // Treat 3D tile as tall 2D tile
                            image_info.compression(),
                            image_info.predictor(),
                            samples_per_tile
                        );
                        if (!decode_result) {
                            return Err(decode_result.error().code, decode_result.error().message);
                        }
                        
                        auto tile_data = decode_result.value();
                        
                        // Calculate intersection between tile and region
                        uint32_t src_start_x = (region_start_x > info.pixel_x) ? 
                            (region_start_x - info.pixel_x) : 0;
                        uint32_t src_start_y = (region_start_y > info.pixel_y) ? 
                            (region_start_y - info.pixel_y) : 0;
                        uint32_t src_start_z = (region_start_z > info.pixel_z) ? 
                            (region_start_z - info.pixel_z) : 0;
                        
                        uint32_t dst_start_x = (info.pixel_x > region_start_x) ? 
                            (info.pixel_x - region_start_x) : 0;
                        uint32_t dst_start_y = (info.pixel_y > region_start_y) ? 
                            (info.pixel_y - region_start_y) : 0;
                        uint32_t dst_start_z = (info.pixel_z > region_start_z) ? 
                            (info.pixel_z - region_start_z) : 0;
                        
                        uint32_t copy_width = std::min(
                            info.width - src_start_x,
                            output_width - dst_start_x
                        );
                        
                        uint32_t copy_height = std::min(
                            info.height - src_start_y,
                            output_height - dst_start_y
                        );
                        
                        uint32_t copy_depth = std::min(
                            info.depth - src_start_z,
                            output_depth - dst_start_z
                        );
                        
                        // Copy tile to output based on planar configuration
                        if (planar_config == PlanarConfiguration::Planar) {
                            copy_tile_to_buffer_3d_planar(
                                tile_data, output_buffer,
                                tile_width, tile_height,
                                output_width, output_height,
                                src_start_x, src_start_y, src_start_z,
                                dst_start_x, dst_start_y, dst_start_z,
                                copy_width, copy_height, copy_depth,
                                plane, samples_per_pixel
                            );
                        } else {
                            copy_tile_to_buffer_3d_chunky(
                                tile_data, output_buffer,
                                tile_width, tile_height,
                                output_width, output_height,
                                src_start_x, src_start_y, src_start_z,
                                dst_start_x, dst_start_y, dst_start_z,
                                copy_width, copy_height, copy_depth,
                                samples_per_pixel
                            );
                        }
                    }
                }
            }
        }
        
        return Ok();
    }
    
    /// Read entire 3D image using provided reader and image info
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<std::vector<PixelType>> read_image_3d(
        const Reader& reader,
        const TiledImageInfo<PixelType>& image_info) const noexcept {
        
        std::size_t volume_size = static_cast<std::size_t>(image_info.image_width()) * 
                                  image_info.image_height() * 
                                  image_info.image_depth() * 
                                  image_info.samples_per_pixel();
        
        std::vector<PixelType> volume(volume_size, 0);
        
        auto result = read_tiles_to_buffer_3d(
            reader,
            image_info,
            std::span(volume),
            image_info.image_width(),
            image_info.image_height(),
            image_info.image_depth(),
            0, 0, 0
        );
        
        if (!result) {
            return Err(result.error().code, result.error().message);
        }
        
        return Ok(std::move(volume));
    }
    
    /// Read 3D region using provided reader and image info
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<std::vector<PixelType>> read_region_3d(
        const Reader& reader,
        const TiledImageInfo<PixelType>& image_info,
        uint32_t start_x, uint32_t start_y, uint32_t start_z,
        uint32_t width, uint32_t height, uint32_t depth) const noexcept {
        
        if (start_x + width > image_info.image_width() || 
            start_y + height > image_info.image_height() ||
            start_z + depth > image_info.image_depth()) {
            return Err(Error::Code::OutOfBounds, "Region exceeds image bounds");
        }
        
        std::size_t region_size = static_cast<std::size_t>(width) * height * depth * image_info.samples_per_pixel();
        std::vector<PixelType> region(region_size, 0);
        
        auto result = read_tiles_to_buffer_3d(
            reader,
            image_info,
            std::span(region),
            width, height, depth,
            start_x, start_y, start_z
        );
        
        if (!result) {
            return Err(result.error().code, result.error().message);
        }
        
        return Ok(std::move(region));
    }
    
    /// Read entire 3D stripped image using provided reader and image info
    /// For stripped images, depth slices are stored sequentially
    template <typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<std::vector<PixelType>> read_image_3d(
        const Reader& reader,
        const StrippedImageInfo<PixelType>& image_info) const noexcept {
        
        std::size_t volume_size = static_cast<std::size_t>(image_info.image_width()) * 
                                  image_info.image_height() * 
                                  image_info.image_depth() * 
                                  image_info.samples_per_pixel();
        
        std::vector<PixelType> volume(volume_size, 0);
        
        // Read each depth slice separately
        std::size_t slice_size = static_cast<std::size_t>(image_info.image_width()) * 
                                 image_info.image_height() * 
                                 image_info.samples_per_pixel();
        
        for (uint32_t z = 0; z < image_info.image_depth(); ++z) {
            std::size_t slice_offset = z * slice_size;
            std::span<PixelType> slice_buffer(volume.data() + slice_offset, slice_size);
            
            auto result = read_strips_to_buffer(
                reader,
                image_info,
                slice_buffer,
                image_info.image_width(),
                image_info.image_height(),
                0, 0
            );
            
            if (!result) {
                return Err(result.error().code, result.error().message);
            }
        }
        
        return Ok(std::move(volume));
    }
};


} // namespace tiff