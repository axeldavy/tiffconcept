#pragma once

#include <algorithm>
#include <span>
#include <vector>
#include "image_shape.hpp"
#include "lowlevel/encoder.hpp"
#include "lowlevel/ifd_builder.hpp"
#include "types/result.hpp"
#include "strategy/write_strategy.hpp"
#include "types/tag_spec.hpp"
#include "lowlevel/tiling.hpp"
#include "types/tiff_spec.hpp"

namespace tiffconcept {

/// @brief Information returned after writing image data
/// @note Contains file offsets and sizes for all chunks (tiles/strips)
/// @note Used to populate IFD tags like TileOffsets, TileByteCounts, etc.
struct WrittenImageInfo {
    std::vector<uint64_t> tile_offsets;      ///< File offsets of each tile/strip
    std::vector<uint64_t> tile_byte_counts;  ///< Compressed sizes of each tile/strip
    std::size_t total_data_size;             ///< Total bytes written for image data
    std::size_t image_data_start_offset;     ///< File offset where image data starts
};

/// @brief Helper to calculate chunk layout for tiled or stripped images
/// @note Generates chunk metadata for all tiles/strips in the image
/// @note Handles both tiled and stripped layouts
/// @note Supports planar and chunky configurations
struct ChunkLayout {
    std::vector<ChunkWriteInfo> chunks;  ///< Metadata for all chunks
    uint32_t chunks_across;              ///< Number of chunks in X direction
    uint32_t chunks_down;                ///< Number of chunks in Y direction
    uint32_t chunks_deep;                ///< Number of chunks in Z direction (usually 1)
    uint16_t num_planes;                 ///< Number of planes (1 for chunky, samples_per_pixel for planar)
    
    /// @brief Create layout for tiled image
    /// @param image_width Image width in pixels
    /// @param image_height Image height in pixels
    /// @param image_depth Image depth in slices
    /// @param tile_width Tile width in pixels
    /// @param tile_height Tile height in pixels
    /// @param tile_depth Tile depth in slices
    /// @param samples_per_pixel Number of samples per pixel
    /// @param planar_config Planar configuration (Chunky or Planar)
    /// @return Result<ChunkLayout> containing chunk metadata
    /// @retval Success Layout created successfully
    /// @note Edge tiles may be smaller than tile dimensions
    /// @note For planar config, creates separate planes for each channel
    [[nodiscard]] static Result<ChunkLayout> create_tiled(
        uint32_t image_width,
        uint32_t image_height,
        uint32_t image_depth,
        uint32_t tile_width,
        uint32_t tile_height,
        uint32_t tile_depth,
        uint16_t samples_per_pixel,
        PlanarConfiguration planar_config) noexcept;
    
    /// @brief Create layout for stripped image
    /// @param image_width Image width in pixels
    /// @param image_height Image height in pixels
    /// @param rows_per_strip Number of rows per strip
    /// @param samples_per_pixel Number of samples per pixel
    /// @param planar_config Planar configuration (Chunky or Planar)
    /// @return Result<ChunkLayout> containing chunk metadata
    /// @retval Success Layout created successfully
    /// @note Strips are full-width horizontal bands
    /// @note Equivalent to tiles with width=image_width, height=rows_per_strip
    [[nodiscard]] static Result<ChunkLayout> create_stripped(
        uint32_t image_width,
        uint32_t image_height,
        uint32_t rows_per_strip,
        uint16_t samples_per_pixel,
        PlanarConfiguration planar_config) noexcept;
};

/// @brief Image writer - coordinates encoding and writing of image data
/// @tparam PixelType The pixel data type (e.g., uint8_t, uint16_t, float)
/// @tparam CompSpec Compressor specification defining compression algorithm
/// @tparam WriteConfig_ Write configuration (IFD placement, tile ordering, buffering)
/// @tparam TiffFormat TIFF format type (Classic or BigTIFF)
/// @tparam TargetEndian Target endianness for the TIFF file
/// 
/// @note Uses configurable write strategies for flexible layout control
/// @note Handles tile extraction, encoding, and writing
/// @note Supports both tiled and stripped image layouts
/// @note NOT thread-safe - use separate instances per thread
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

    /// @brief Default constructor
    ImageWriter() = default;
    
    /// @brief Write complete image to file
    /// @tparam InputSpec Layout of input_data buffer (DHWC, DCHW, or CDHW)
    /// @tparam Writer Writer type implementing RawWriter concept
    /// @param writer The output writer
    /// @param input_data Image pixel data buffer
    /// @param image_width Image width in pixels
    /// @param image_height Image height in pixels
    /// @param image_depth Image depth in slices
    /// @param tile_width Tile width in pixels
    /// @param tile_height Tile height in pixels
    /// @param tile_depth Tile depth in slices
    /// @param samples_per_pixel Number of samples per pixel
    /// @param planar_config Planar configuration (Chunky or Planar)
    /// @param compression Compression scheme to use
    /// @param predictor Predictor to apply before compression
    /// @param data_start_offset File offset where image data should start
    /// @return Result<WrittenImageInfo> containing offset and size information
    /// @retval Success Image data written successfully
    /// @retval OutOfBounds Input data size too small for image dimensions
    /// @retval WriteError Failed to write chunk data
    /// @retval CompressionError Encoding failed
    /// @note Returns offset and size information for creating IFD tags
    /// @note Applies tile ordering strategy before writing
    /// @note Edge tiles are padded using replicate strategy
    /// @note Current limitation: tiles must be full size (no partial last tiles)
    template <ImageLayoutSpec InputSpec, typename Writer>
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
        std::size_t data_start_offset) noexcept;
    
    /// @brief Write stripped image (convenience wrapper)
    /// @tparam InputSpec Layout of input_data buffer (DHWC, DCHW, or CDHW)
    /// @tparam Writer Writer type implementing RawWriter concept
    /// @param writer The output writer
    /// @param input_data Image pixel data buffer
    /// @param image_width Image width in pixels
    /// @param image_height Image height in pixels
    /// @param rows_per_strip Number of rows per strip
    /// @param samples_per_pixel Number of samples per pixel
    /// @param planar_config Planar configuration (Chunky or Planar)
    /// @param compression Compression scheme to use
    /// @param predictor Predictor to apply before compression
    /// @param data_start_offset File offset where image data should start
    /// @return Result<WrittenImageInfo> containing offset and size information
    /// @retval Success Image data written successfully
    /// @retval UnsupportedFeature rows_per_strip doesn't evenly divide image_height
    /// @retval OutOfBounds Input data size too small
    /// @retval WriteError Failed to write strip data
    /// @retval CompressionError Encoding failed
    /// @note Current limitation: rows_per_strip must evenly divide image_height
    template <ImageLayoutSpec InputSpec, typename Writer>
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
        std::size_t data_start_offset) noexcept;
    
    /// @brief Clear encoder state for memory management
    /// @note Releases scratch buffers used by the encoder
    void clear() noexcept;

private:
    ChunkEncoder<PixelType, CompSpec> encoder_;
    TileOrdering ordering_strategy_;
    OffsetResolution offset_strategy_;
};

} // namespace tiffconcept

#define TIFFCONCEPT_IMAGE_WRITER_HEADER
#include "impl/image_writer_impl.hpp"