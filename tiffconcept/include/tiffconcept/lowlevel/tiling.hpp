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
#include "../image_shape.hpp"
#include "../types/result.hpp"
#include "../types/tiff_spec.hpp"
#include "../types/tile_info.hpp"

namespace tiffconcept {

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
 * @param dst_dims Destination tile dimensions (width, height, depth, nsamples)
 * @param src_dims Source tile dimensions (width, height, depth, nsamples)
 * @param copy_dims Dimensions to copy (width, height, depth, nsamples)
 * @param dst_pos Starting position in destination (x, y, z, s coordinates)
 * @param src_pos Starting position in source (x, y, z, s coordinates)
 * 
 * @pre dst_tile_data must be large enough to hold the destination region
 * @pre src_tile_data must contain valid data in the source region
 * @pre All copy coordinates must be within bounds (validated by assertions in debug builds)
 * @pre copy_dims.depth + dst_pos.z ≤ dst_dims.depth
 * @pre copy_dims.height + dst_pos.y ≤ dst_dims.height
 * @pre copy_dims.width + dst_pos.x ≤ dst_dims.width
 * @pre copy_dims.nsamples + dst_pos.s ≤ dst_dims.nsamples
 * @pre (same constraints apply for src_dims and src_pos)
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
 * TileSize src_dims{width, height, depth, channels};
 * TileSize dst_dims{width, height, depth, channels};
 * TileSize copy_dims{width, height, depth, channels};
 * TileCoordinates src_pos{0, 0, 0, 0};
 * TileCoordinates dst_pos{0, 0, 0, 0};
 * 
 * copy_tile_to_tile<ImageLayoutSpec::DCHW, ImageLayoutSpec::DHWC, float>(
 *     dst_data, src_data,
 *     dst_dims, src_dims,
 *     copy_dims, dst_pos, src_pos
 * );
 * @endcode
 * 
 * @par Example: Extract sub-region with layout change
 * @code
 * // Extract a 64×64×3 region from position (32, 32) in a 256×256×3 DHWC image
 * // and write it to a CDHW buffer starting at channel 1
 * 
 * TileSize src_dims{256, 256, 1, 3};
 * TileSize dst_dims{64, 64, 1, 3};
 * TileSize copy_dims{64, 64, 1, 2};  // Copy only 2 channels
 * TileCoordinates src_pos{32, 32, 0, 0};  // Start at (32, 32)
 * TileCoordinates dst_pos{0, 0, 0, 1};    // Write to channel 1
 * 
 * copy_tile_to_tile<ImageLayoutSpec::CDHW, ImageLayoutSpec::DHWC, uint8_t>(
 *     output_buffer, input_image,
 *     dst_dims, src_dims,
 *     copy_dims, dst_pos, src_pos
 * );
 * @endcode
 * 
 * @see TileSize for dimension structure details
 * @see TileCoordinates for position structure details
 * @see ImageLayoutSpec for supported memory layouts
 * @see copy_tile_to_buffer for higher-level tile copying with PlanarConfiguration
 */
template <ImageLayoutSpec OutSpec, ImageLayoutSpec InSpec, typename PixelType>
void copy_tile_to_tile(
    std::span<PixelType> dst_tile_data,
    const std::span<const PixelType> src_tile_data,
    const TileSize dst_dims,
    const TileSize src_dims,
    const TileSize copy_dims,
    const TileCoordinates dst_pos,
    const TileCoordinates src_pos) noexcept;

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
 * @param dst_dims Output buffer dimensions (width, height, depth, nsamples)
 * @param src_dims Tile dimensions (width, height, depth, nsamples)
 * @param copy_dims Dimensions to copy (width, height, depth, nsamples)
 * @param dst_pos Starting position in output buffer (x, y, z, s coordinates)
 * @param src_pos Starting position in tile (x, y, z, s coordinates)
 * 
 * @note Planar tiles must have src_dims.nsamples == 1
 * @note All coordinates and sizes are validated with assertions in debug builds
 * @note Uses optimized memcpy paths when data is contiguous
 * 
 * @see TileSize for dimension structure
 * @see TileCoordinates for position structure
 */
template <PlanarConfiguration PlanarConfig, ImageLayoutSpec OutSpec, typename PixelType>
void copy_tile_to_buffer(
    std::span<const PixelType> tile_data,
    std::span<PixelType> output_buffer,
    const TileSize dst_dims,
    const TileSize src_dims,
    const TileSize copy_dims,
    const TileCoordinates dst_pos,
    const TileCoordinates src_pos) noexcept;

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
 * @param dst_dims Tile dimensions (width, height, depth, nsamples)
 * @param src_dims Input buffer dimensions (width, height, depth, nsamples)
 * @param src_pos Starting position in input buffer (x, y, z, s coordinates)
 * 
 * @note Planar tiles must have dst_dims.nsamples == 1
 * @note Applies replicate padding for boundary tiles
 * @note Uses optimized memcpy paths when data is contiguous
 * @note Tile buffer must be large enough: dst_dims.depth × dst_dims.height × dst_dims.width × dst_dims.nsamples
 * 
 * @see TileSize for dimension structure
 * @see TileCoordinates for position structure
 */
template <PlanarConfiguration PlanarConfig, ImageLayoutSpec InSpec, typename PixelType>
void fetch_tile_from_buffer(
    std::span<const PixelType> input_buffer,
    std::span<PixelType> tile_data,
    const TileSize dst_dims,
    const TileSize src_dims,
    const TileCoordinates src_pos) noexcept;

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
     * @brief Get information for a tile
     * 
     * Returns complete tile information including logical coordinates, dimensions,
     * and physical file location (offset and byte count).
     * 
     * @param tile_x Tile column index
     * @param tile_y Tile row index
     * @param tile_z Tile depth index
     * @param plane Plane index (for planar images, 0 for chunky)
     * @return Result containing Tile struct, or an error
     * 
     * @throws None (noexcept)
     * @retval Error::Code::OutOfBounds Tile coordinates out of range
     * 
     * @note For planar images, tiles are organized by plane first
     * @note The returned Tile contains both TileDescriptor (logical info) and FileSpan (physical location)
     * 
     * @see Tile for the structure of returned data
     * @see TileDescriptor for logical tile information
     * @see FileSpan for physical file location
     */
    [[nodiscard]] Result<Tile> get_tile_info(
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
     * Returns complete strip information including logical coordinates, dimensions,
     * and physical file location (offset and byte count).
     * 
     * For planar images, strips are organized by plane:
     * [plane0_strip0, plane0_strip1, ..., plane1_strip0, plane1_strip1, ...]
     * 
     * @param strip_index Strip index within the plane (0 to strips_per_plane - 1)
     * @param plane Plane index (for planar images, 0 for chunky)
     * @return Result containing Tile struct (strips are represented as tiles), or an error
     * 
     * @throws None (noexcept)
     * @retval Error::Code::OutOfBounds Strip index or plane out of range
     * 
     * @note For chunky images, plane parameter is ignored
     * @note strip_index is relative to the plane, not absolute file index
     * @note The returned Tile contains both TileDescriptor (logical info) and FileSpan (physical location)
     * 
     * @par Example:
     * @code
     * // Get first strip of second channel in planar image
     * auto result = strip_info.get_strip_info(0, 1);
     * if (result) {
     *     const Tile& strip = result.value();
     *     // strip.id.coords has the logical coordinates
     *     // strip.location has file offset and length
     * }
     * @endcode
     * 
     * @see Tile for the structure of returned data
     * @see TileDescriptor for logical strip information
     * @see FileSpan for physical file location
     */
    [[nodiscard]] Result<Tile> get_strip_info(uint32_t strip_index, uint32_t plane = 0) const noexcept;
    
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