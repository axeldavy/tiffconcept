#pragma once

/**
 * @file tiling.hpp
 * @brief Tile and Strip operations and layout management for TIFF images
 * 
 * This header provides utilities for working with tiled TIFF images, including:
 * - Copying tiles to/from image buffers with various memory layouts
 * - Managing tiled image metadata and geometry
 * - Handling planar vs chunky configurations
 * - Layout conversions between different memory orderings (DHWC, DCHW, CDHW)
 * 
 * ## Key Concepts
 * 
 * - **Tile**: A rectangular region of an image that can be independently compressed.
 *   TIFF tiles are typically 256×256 pixels but can be any size.
 * 
 * - **Strip**: A horizontal band of an image, typically spanning the full width
 *   and a fixed number of rows (RowsPerStrip). Essentially it is a tile that
 *   spans the whole width. However a subtle difference is that last strip does
 *   not do padding (unlike tiles). As padding compresses well, the difference is minor,
 *   and tiles are generally preferred for better random access and compression.
 * 
 * - **Planar vs Chunky**: 
 *   - Chunky (interleaved): All channels for a pixel are stored together (RGBRGBRGB...)
 *       This is typically used for RGB images as the channels are highly correlated, or
 *       to allow fast cropping.
 *   - Planar: Each channel is stored separately (RRR...GGG...BBB...)
 *       This is often better for compression when spatial correlation is higher than across channels,
 *       for instance in multispectral imaging.
 * 
 * - **Memory Layout**: The order in which multi-dimensional image data is stored:
 *   - DHWC: Depth, Height, Width, Channels (channels last, chunky arrangement)
 *   - DCHW: Depth, Channels, Height, Width (No native tiff equivalent, but common in some frameworks)
 *   - CDHW: Channels, Depth, Height, Width (planar arrangement)
 * 
 * - **Padding**: Tiles at image boundaries may extend beyond the actual image data.
 *   This library uses replicate padding (edge pixels are repeated).
 * 
 * ## Example Usage
 * 
 * @code{.cpp}
 * using namespace tiffconcept;
 * 
 * // Setup tiled image info
 * TiledImageInfo<uint16_t> tile_info;
 * auto result = tile_info.update_from_metadata(metadata);
 * 
 * // Get information about a specific tile
 * auto info_result = tile_info.get_tile_info(tile_x, tile_y);
 * if (info_result) {
 *     const auto& info = info_result.value();
 *     // info.offset, info.byte_count, etc.
 * }
 * 
 * // Copy decoded tile into output buffer (channels-last layout)
 * copy_tile_to_buffer<PlanarConfiguration::Chunky, ImageLayoutSpec::DHWC>(
 *     decoded_tile, output_buffer,
 *     tile_width, tile_height, tile_depth, samples_per_pixel,
 *     // ... parameters
 * );
 * @endcode
 * 
 * @note All copy operations are optimized with memcpy when possible
 * @note Layout conversions are handled at compile-time for zero overhead
 */

#include <cassert>
#include <cstring>
#include <span>
#include <vector>
#include "image_shape.hpp"
#include "result.hpp"
#include "types.hpp"

namespace tiffconcept {

/**
 * @brief Tile dimension information for copy operations
 */
struct TileDimensions {
    std::size_t tile_depth;     ///< Depth of tile in pixels
    std::size_t tile_height;    ///< Height of tile in pixels
    std::size_t tile_width;     ///< Width of tile in pixels
    std::size_t tile_nsamples;  ///< Number of samples (channels) per pixel in tile
    std::size_t tile_z;         ///< Z start coordinate of region to copy into/from
    std::size_t tile_y;         ///< Y start coordinate of region to copy into/from
    std::size_t tile_x;         ///< X start coordinate of region to copy into/from
    std::size_t tile_s;         ///< Starting sample (channel) index of region to copy into/from
};

/**
 * @brief Copy data between tiles with layout conversion
 * 
 * A low-level utility for copying rectangular regions between memory buffers
 * with potentially different memory layouts. This function handles all combinations
 * of DHWC, DCHW, and CDHW layouts and optimizes with memcpy when data is contiguous.
 * 
 * This function is useful for:
 * - Converting image data between different memory layouts
 * - Extracting sub-regions with layout conversion
 * - Preparing data for different deep learning frameworks
 * - Custom tile manipulation operations
 * 
 * ## Layout Conversions Supported
 * 
 * The function handles all 9 combinations of input/output layouts:
 * - Same layout (optimized fast path): DHWC→DHWC, DCHW→DCHW, CDHW→CDHW
 * - Cross-layout conversions: DHWC↔DCHW, DHWC↔CDHW, DCHW↔CDHW
 * 
 * ## Performance
 * 
 * - Uses memcpy for contiguous regions (same layout or specific alignment)
 * - Falls back to element-by-element copy for complex layout conversions (in the future may be optimized further)
 * - All branching is resolved at compile-time via constexpr
 * 
 * @tparam OutSpec Output buffer memory layout (DHWC, DCHW, or CDHW)
 * @tparam InSpec Input buffer memory layout (DHWC, DCHW, or CDHW)
 * @tparam PixelType Pixel data type (uint8_t, uint16_t, float, etc.)
 * 
 * @param dst_tile_data Destination buffer
 * @param src_tile_data Source buffer (read-only)
 * @param dst_dims Destination buffer dimensions and starting coordinates
 * @param src_dims Source buffer dimensions and starting coordinates
 * @param copy_depth Number of depth slices to copy
 * @param copy_height Number of rows to copy
 * @param copy_width Number of columns to copy
 * @param copy_nchans Number of channels to copy
 * 
 * @pre dst_tile_data must be large enough to hold the destination region
 * @pre src_tile_data must contain valid data in the source region
 * @pre All copy coordinates must be within bounds (validated by assertions in debug builds)
 * @pre copy_depth + dst_dims.tile_z ≤ dst_dims.tile_depth
 * @pre copy_height + dst_dims.tile_y ≤ dst_dims.tile_height
 * @pre copy_width + dst_dims.tile_x ≤ dst_dims.tile_width
 * @pre copy_nchans + dst_dims.tile_s ≤ dst_dims.tile_nsamples
 * @pre (same constraints apply for src_dims)
 * 
 * @note All bounds checking is done via assertions in debug builds
 * @note Thread-safe (no shared state)
 * @note Uses optimized memcpy paths when possible
 * @note Source and destination buffers must not overlap
 * 
 * @par Example: Convert DHWC to DCHW format
 * @code
 * // Source: channels last
 * std::vector<float> src_data(depth * height * width * channels);
 * // ... fill src_data ...
 * 
 * // Destination: channels first after depth
 * std::vector<float> dst_data(depth * channels * height * width);
 * 
 * TileDimensions src_dims{depth, height, width, channels, 0, 0, 0, 0};
 * TileDimensions dst_dims{depth, height, width, channels, 0, 0, 0, 0};
 * 
 * copy_tile_to_tile<ImageLayoutSpec::DCHW, ImageLayoutSpec::DHWC, float>(
 *     dst_data, src_data,
 *     dst_dims, src_dims,
 *     depth, height, width, channels
 * );
 * @endcode
 * 
 * @par Example: Extract sub-region with layout change
 * @code
 * // Extract a 64×64×3 region from position (32, 32) in a 256×256×3 DHWC image
 * // and write it to a CDHW buffer starting at channel 1
 * 
 * TileDimensions src_dims{1, 256, 256, 3, 0, 32, 32, 0};  // Start at (32, 32)
 * TileDimensions dst_dims{1, 64, 64, 3, 0, 0, 0, 1};      // Write to channel 1
 * 
 * copy_tile_to_tile<ImageLayoutSpec::CDHW, ImageLayoutSpec::DHWC, uint8_t>(
 *     output_buffer, input_image,
 *     dst_dims, src_dims,
 *     1, 64, 64, 2  // Copy only 2 channels
 * );
 * @endcode
 * 
 * @see TileDimensions for dimension structure details
 * @see ImageLayoutSpec for supported memory layouts
 * @see copy_tile_to_buffer for higher-level tile copying with PlanarConfiguration
 */
template <ImageLayoutSpec OutSpec, ImageLayoutSpec InSpec, typename PixelType>
void copy_tile_to_tile(
    std::span<PixelType> dst_tile_data,
    const std::span<const PixelType> src_tile_data,
    const TileDimensions& dst_dims,
    const TileDimensions& src_dims,
    std::size_t copy_depth,
    std::size_t copy_height,
    std::size_t copy_width,
    std::size_t copy_nchans) noexcept;

/**
 * @brief Copy a tile into a larger image buffer
 * 
 * Copies tile data (or a portion thereof) into an output buffer with potentially
 * different memory layout. The function handles layout conversions and optimizes
 * with memcpy for contiguous regions.
 * 
 * This is typically used after decoding a compressed tile to place it into the
 * final output image buffer.
 * 
 * @tparam PlanarConfig Tile data configuration (Chunky or Planar)
 * @tparam OutSpec Output buffer layout (DHWC, DCHW, or CDHW)
 * @tparam PixelType Pixel data type (uint8_t, uint16_t, float, etc.)
 * 
 * @param tile_data Source tile data
 * @param output_buffer Destination image buffer
 * @param tile_depth Full tile depth in pixels
 * @param tile_height Full tile height in pixels
 * @param tile_width Full tile width in pixels
 * @param tile_nsamples Number of samples (channels) in tile
 * @param tile_start_depth Starting depth within tile to copy from
 * @param tile_start_height Starting height within tile to copy from
 * @param tile_start_width Starting width within tile to copy from
 * @param tile_start_sample Starting sample/channel within tile to copy from
 * @param output_depth Output buffer depth
 * @param output_height Output buffer height
 * @param output_width Output buffer width
 * @param output_nchans Total channels in output buffer
 * @param output_start_depth Starting depth in output buffer to copy to
 * @param output_start_height Starting height in output buffer to copy to
 * @param output_start_width Starting width in output buffer to copy to
 * @param output_start_chan Starting channel in output buffer to copy to
 * @param copy_depth Number of depth slices to copy
 * @param copy_height Number of rows to copy
 * @param copy_width Number of pixels per row to copy
 * @param copy_nchans Number of channels to copy
 * 
 * @note Planar tiles must have tile_nsamples == 1
 * @note All coordinates and sizes are validated with assertions in debug builds
 * @note Uses optimized memcpy paths when data is contiguous
 */
template <PlanarConfiguration PlanarConfig, ImageLayoutSpec OutSpec, typename PixelType>
void copy_tile_to_buffer(
    std::span<const PixelType> tile_data,
    std::span<PixelType> output_buffer,
    uint32_t tile_depth,
    uint32_t tile_height,
    uint32_t tile_width,
    uint16_t tile_nsamples,
    uint32_t tile_start_depth,
    uint32_t tile_start_height,
    uint32_t tile_start_width,
    uint16_t tile_start_sample,
    uint32_t output_depth,
    uint32_t output_height,
    uint32_t output_width,
    uint16_t output_nchans,
    uint32_t output_start_depth,
    uint32_t output_start_height,
    uint32_t output_start_width,
    uint16_t output_start_chan,
    uint32_t copy_depth,
    uint32_t copy_height,
    uint32_t copy_width,
    uint16_t copy_nchans) noexcept;

/**
 * @brief Fetch tile data from a larger image buffer
 * 
 * Copies data from an image buffer into a tile buffer, handling layout conversions
 * and applying replicate padding for tiles that extend beyond the image boundaries.
 * 
 * This is typically used before encoding/compressing a tile for writing to a TIFF file.
 * 
 * **Padding Behavior**: If the requested tile region extends beyond the input buffer,
 * the function applies replicate (edge) padding: pixels at the boundary are repeated
 * to fill the padded regions.
 * 
 * @tparam PlanarConfig Tile data configuration (Chunky or Planar)
 * @tparam InSpec Input buffer layout (DHWC, DCHW, or CDHW)
 * @tparam PixelType Pixel data type (uint8_t, uint16_t, float, etc.)
 * 
 * @param input_buffer Source image buffer
 * @param tile_data Destination tile buffer (must be pre-allocated)
 * @param input_depth Input buffer depth
 * @param input_height Input buffer height
 * @param input_width Input buffer width
 * @param input_nchans Total channels in input buffer
 * @param input_start_depth Starting depth in input buffer
 * @param input_start_height Starting height in input buffer
 * @param input_start_width Starting width in input buffer
 * @param input_start_chan Starting channel in input buffer
 * @param tile_depth Tile depth in pixels
 * @param tile_height Tile height in pixels
 * @param tile_width Tile width in pixels
 * @param tile_nsamples Number of samples (channels) per tile
 * 
 * @note Planar tiles must have tile_nsamples == 1
 * @note Applies replicate padding for boundary tiles
 * @note Uses optimized memcpy paths when data is contiguous
 * @note Tile buffer must be large enough: tile_depth × tile_height × tile_width × tile_nsamples
 */
template <PlanarConfiguration PlanarConfig, ImageLayoutSpec InSpec, typename PixelType>
void fetch_tile_from_buffer(
    std::span<const PixelType> input_buffer,
    std::span<PixelType> tile_data,
    uint32_t input_depth,
    uint32_t input_height,
    uint32_t input_width,
    uint16_t input_nchans,
    uint32_t input_start_depth,
    uint32_t input_start_height,
    uint32_t input_start_width,
    uint16_t input_start_chan,
    uint32_t tile_depth,
    uint32_t tile_height,
    uint32_t tile_width,
    uint16_t tile_nsamples) noexcept;

/**
 * @brief Information about a single tile
 * 
 * Contains geometry and file location information for a tile in a TIFF image.
 */
struct TileInfo {
    uint32_t tile_x;        ///< Tile column index
    uint32_t tile_y;        ///< Tile row index
    uint32_t tile_z;        ///< Tile depth index (for 3D images)
    uint32_t pixel_x;       ///< Starting pixel X coordinate in image
    uint32_t pixel_y;       ///< Starting pixel Y coordinate in image
    uint32_t pixel_z;       ///< Starting pixel Z coordinate in image
    uint32_t width;         ///< Tile width in pixels (may be less than tile_width at boundaries)
    uint32_t height;        ///< Tile height in pixels (may be less than tile_height at boundaries)
    uint32_t depth;         ///< Tile depth in pixels (may be less than tile_depth at boundaries)
    uint32_t offset;        ///< File offset to compressed tile data
    uint32_t byte_count;    ///< Size of compressed tile data in bytes
    uint32_t tile_index;    ///< Linear tile index in the file
};

/**
 * @brief Reusable tiled image information and metadata
 * 
 * TiledImageInfo manages metadata for a tiled TIFF image, including:
 * - Image dimensions and pixel format
 * - Tile dimensions and layout
 * - Compression and predictor settings
 * - Tile offset and byte count arrays
 * 
 * The class owns its metadata storage and can be reused for multiple images
 * by calling update_from_metadata() with different ExtractedTags.
 * 
 * @tparam PixelType The pixel data type (used for validation)
 * 
 * @note Thread-safe for read operations after initialization
 * @note update_from_metadata() reuses allocations when possible
 */
template <typename PixelType>
class TiledImageInfo {
private:
    ImageShape shape_;
    
    uint32_t tile_width_;
    uint32_t tile_height_;
    uint32_t tile_depth_;
    
    std::vector<std::size_t> tile_offsets_;
    std::vector<std::size_t> tile_byte_counts_;
    
    CompressionScheme compression_;
    Predictor predictor_;
    
    uint32_t tiles_across_;
    uint32_t tiles_down_;
    uint32_t tiles_deep_;
    
public:
    /**
     * @brief Default constructor - creates empty tile info
     */
    TiledImageInfo() noexcept;
    
    /**
     * @brief Update tile info from TIFF metadata
     * 
     * Extracts all necessary information from TIFF tags and validates the
     * pixel type matches the template parameter. The method reuses internal
     * allocations when possible for efficiency.
     * 
     * @tparam TagSpec Tag specification type (must include tile-related tags)
     * @param metadata Extracted TIFF tags
     * @return Ok() on success, or Error on validation failure
     * 
     * @throws None (noexcept)
     * @retval Error::Code::InvalidTag Required tile tags missing
     * @retval Error::Code::InvalidFormat Pixel type mismatch
     * 
     * @note Required tags: TileWidth, TileLength, TileOffsets, TileByteCounts
     * @note Optional tags: TileDepth (defaults to 1), Predictor (defaults to None)
     */
    template <typename TagSpec>
        requires TiledImageTagSpec<TagSpec>
    [[nodiscard]] Result<void> update_from_metadata(
        const ExtractedTags<TagSpec>& metadata) noexcept;
    
    /**
     * @brief Get image shape information
     * @return Reference to ImageShape containing dimensions and format
     */
    [[nodiscard]] const ImageShape& shape() const noexcept;
    
    /**
     * @brief Get tile width in pixels
     */
    [[nodiscard]] uint32_t tile_width() const noexcept;
    
    /**
     * @brief Get tile height in pixels
     */
    [[nodiscard]] uint32_t tile_height() const noexcept;
    
    /**
     * @brief Get tile depth in pixels (for 3D images)
     */
    [[nodiscard]] uint32_t tile_depth() const noexcept;
    
    /**
     * @brief Get number of tiles across (X direction)
     */
    [[nodiscard]] uint32_t tiles_across() const noexcept;
    
    /**
     * @brief Get number of tiles down (Y direction)
     */
    [[nodiscard]] uint32_t tiles_down() const noexcept;
    
    /**
     * @brief Get number of tiles deep (Z direction)
     */
    [[nodiscard]] uint32_t tiles_deep() const noexcept;
    
    /**
     * @brief Get total number of tiles
     * 
     * For planar images: tiles_across × tiles_down × tiles_deep × samples_per_pixel
     * For chunky images: tiles_across × tiles_down × tiles_deep
     */
    [[nodiscard]] std::size_t num_tiles() const noexcept;
    
    /**
     * @brief Get compression scheme
     */
    [[nodiscard]] CompressionScheme compression() const noexcept;
    
    /**
     * @brief Get predictor type
     */
    [[nodiscard]] Predictor predictor() const noexcept;
    
    /**
     * @brief Get information for a 2D tile
     * 
     * @param tile_x Tile column index
     * @param tile_y Tile row index
     * @param plane Plane index (for planar images, 0 for chunky)
     * @return Result containing TileInfo, or an error
     * 
     * @throws None (noexcept)
     * @retval Error::Code::OutOfBounds Tile coordinates out of range
     */
    [[nodiscard]] Result<TileInfo> get_tile_info(
        uint32_t tile_x, 
        uint32_t tile_y, 
        uint32_t plane = 0) const noexcept;
    
    /**
     * @brief Get information for a 3D tile
     * 
     * @param tile_x Tile column index
     * @param tile_y Tile row index
     * @param tile_z Tile depth index
     * @param plane Plane index (for planar images, 0 for chunky)
     * @return Result containing TileInfo, or an error
     * 
     * @throws None (noexcept)
     * @retval Error::Code::OutOfBounds Tile coordinates out of range
     * 
     * @note For planar images, tiles are organized by plane first
     */
    [[nodiscard]] Result<TileInfo> get_tile_info_3d(
        uint32_t tile_x, 
        uint32_t tile_y, 
        uint32_t tile_z, 
        uint32_t plane = 0) const noexcept;
    
    /**
     * @brief Get number of tiles per plane
     * 
     * @return tiles_across × tiles_down × tiles_deep
     */
    [[nodiscard]] uint32_t tiles_per_plane() const noexcept;
};

/**
 * @brief Information about a single strip
 * 
 * Contains geometry and file location information for a strip in a TIFF image.
 */
struct StripInfo {
    uint32_t strip_index;   ///< Strip index in the file
    uint32_t pixel_y;       ///< Starting pixel Y coordinate in image
    uint32_t width;         ///< Strip width in pixels (same as image width)
    uint32_t height;        ///< Strip height in pixels (may be less than rows_per_strip for last strip)
    uint32_t offset;        ///< File offset to compressed strip data
    uint32_t byte_count;    ///< Size of compressed strip data in bytes
};

/**
 * @brief Reusable stripped image information and metadata
 * 
 * StrippedImageInfo manages metadata for a stripped TIFF image, including:
 * - Image dimensions and pixel format
 * - Strip layout (rows per strip)
 * - Compression and predictor settings
 * - Strip offset and byte count arrays
 * 
 * The class owns its metadata storage and can be reused for multiple images
 * by calling update_from_metadata() with different ExtractedTags.
 * 
 * @tparam PixelType The pixel data type (used for validation)
 * 
 * @note Thread-safe for read operations after initialization
 * @note update_from_metadata() reuses allocations when possible
 */
template <typename PixelType>
class StrippedImageInfo {
private:
    ImageShape shape_;
    
    uint32_t rows_per_strip_;
    
    std::vector<std::size_t> strip_offsets_;
    std::vector<std::size_t> strip_byte_counts_;
    
    CompressionScheme compression_;
    Predictor predictor_;
    
    uint32_t num_strips_;
    
public:
    /**
     * @brief Default constructor - creates empty strip info
     */
    StrippedImageInfo() noexcept;
    
    /**
     * @brief Update strip info from TIFF metadata
     * 
     * Extracts all necessary information from TIFF tags and validates the
     * pixel type matches the template parameter. The method reuses internal
     * allocations when possible for efficiency.
     * 
     * @tparam TagSpec Tag specification type (must include strip-related tags)
     * @param metadata Extracted TIFF tags
     * @return Ok() on success, or Error on validation failure
     * 
     * @throws None (noexcept)
     * @retval Error::Code::InvalidTag Required strip tags missing
     * @retval Error::Code::InvalidFormat Pixel type mismatch
     * 
     * @note Required tags: RowsPerStrip, StripOffsets, StripByteCounts
     * @note Optional tags: Predictor (defaults to None)
     */
    template <typename TagSpec>
        requires StrippedImageTagSpec<TagSpec>
    [[nodiscard]] Result<void> update_from_metadata(
        const ExtractedTags<TagSpec>& metadata) noexcept;
    
    /**
     * @brief Get image shape information
     * @return Reference to ImageShape containing dimensions and format
     */
    [[nodiscard]] const ImageShape& shape() const noexcept;
    
    /**
     * @brief Get number of rows per strip
     * 
     * @return Rows per strip (except possibly the last strip)
     */
    [[nodiscard]] uint32_t rows_per_strip() const noexcept;
    
    /**
     * @brief Get total number of strips
     * 
     * For planar images: strips_per_plane × samples_per_pixel
     * For chunky images: strips_per_plane
     */
    [[nodiscard]] uint32_t num_strips() const noexcept;
    
    /**
     * @brief Get compression scheme
     */
    [[nodiscard]] CompressionScheme compression() const noexcept;
    
    /**
     * @brief Get predictor type
     */
    [[nodiscard]] Predictor predictor() const noexcept;
    
    /**
     * @brief Get information for a specific strip
     * 
     * For planar images, strips are organized by plane:
     * [plane0_strip0, plane0_strip1, ..., plane1_strip0, plane1_strip1, ...]
     * 
     * @param strip_index Strip index within the plane (0 to strips_per_plane - 1)
     * @param plane Plane index (for planar images, 0 for chunky)
     * @return Result containing StripInfo, or an error
     * 
     * @throws None (noexcept)
     * @retval Error::Code::OutOfBounds Strip index or plane out of range
     * 
     * @note For chunky images, plane parameter is ignored
     * @note strip_index is relative to the plane, not absolute file index
     * 
     * @par Example:
     * @code
     * // Get first strip of second channel in planar image
     * auto strip = strip_info.get_strip_info(0, 1);
     * @endcode
     */
    [[nodiscard]] Result<StripInfo> get_strip_info(uint32_t strip_index, uint32_t plane = 0) const noexcept;
    
    /**
     * @brief Get number of strips per plane
     * 
     * Calculates the number of strips needed to cover the image height.
     * 
     * @return ceil(image_height / rows_per_strip)
     */
    [[nodiscard]] uint32_t strips_per_plane() const noexcept;
};

} // namespace tiffconcept

// Include implementation
#define TIFFCONCEPT_TILING_HEADER
#include "impl/tiling_impl.hpp"