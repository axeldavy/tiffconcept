#pragma once

#include <cstring>
#include <span>
#include <vector>
#include "encoder.hpp"
#include "ifd.hpp"
#include "ifd_builder.hpp"
#include "image_writer.hpp"
#include "reader_base.hpp"
#include "result.hpp"
#include "tag_extraction.hpp"
#include "tag_spec.hpp"
#include "types.hpp"
#include "strategy/write_strategy.hpp"

namespace tiffconcept {

/// @brief Complete TIFF file writer
/// @tparam PixelType The pixel data type (e.g., uint8_t, uint16_t, float)
/// @tparam CompSpec Compressor specification defining compression algorithm
/// @tparam WriteConfig_ Write configuration (IFD placement, chunk strategy)
/// @tparam TiffFormat TIFF format type (Classic or BigTIFF)
/// @tparam TargetEndian Target endianness for the TIFF file
/// 
/// @note Orchestrates header, IFD, and image data writing
/// @note NOT thread-safe - use separate instances per thread
/// @note Supports both tiled and stripped image layouts
/// @note Supports 2D and 3D images
/// @note Validates user-provided tags against mandatory requirements
template <
    typename PixelType,
    typename CompSpec,
    typename WriteConfig_,
    TiffFormatType TiffFormat = TiffFormatType::Classic,
    std::endian TargetEndian = std::endian::native
>
    requires ValidCompressorSpec<CompSpec> &&
             predictor::DeltaDecodable<PixelType>
class TiffWriter {
public:
    using WriteConfig = WriteConfig_;
    using IFDPlacement = typename WriteConfig::ifd_placement_strategy;
    using ImageWriterType = ImageWriter<PixelType, CompSpec, WriteConfig, TiffFormat, TargetEndian>;
    using IFDBuilderType = IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>;
    using HeaderType = std::conditional_t<TiffFormat == TiffFormatType::Classic,
                                         TiffHeader<TargetEndian>,
                                         TiffBigHeader<TargetEndian>>;

    /// @brief Default constructor
    TiffWriter() = default;
    
    /// @brief Construct with specific IFD placement strategy
    /// @param placement IFD placement configuration
    explicit TiffWriter(IFDPlacement placement);
    
    /// @brief Write a complete single-image 2D tiled TIFF file
    /// @tparam InputSpec Image data layout specification (DHWC, DCHW, or CDHW)
    /// @tparam Writer Writer type implementing RawWriter concept
    /// @tparam TagArgs Variadic template parameters for additional tags
    /// @param writer The output writer
    /// @param image_data Image pixel data buffer
    /// @param image_width Image width in pixels
    /// @param image_height Image height in pixels
    /// @param tile_width Tile width in pixels
    /// @param tile_height Tile height in pixels
    /// @param samples_per_pixel Number of channels/samples per pixel
    /// @param planar_config Planar configuration (Chunky or Planar)
    /// @param compression Compression scheme to use
    /// @param predictor Predictor to apply before compression
    /// @param additional_tags Optional additional TIFF tags to include
    /// @return Result<void> indicating success or error
    /// @retval Success Image written successfully
    /// @retval FileNotFound Output file cannot be created
    /// @retval WriteError Error during file write operation
    /// @retval InvalidTag User-provided tag conflicts with mandatory tags
    /// @retval OutOfBounds Buffer size mismatch
    /// @retval CompressionError Compression failed
    /// @note This is the main high-level API for writing 2D tiled TIFF images
    /// @note Mandatory tags are automatically generated and validated
    /// @note User-provided tags in additional_tags must not conflict with mandatory tags
    template <ImageLayoutSpec InputSpec, typename Writer, typename... TagArgs>
        requires RawWriter<Writer>
    [[nodiscard]] Result<void> write_single_image(
        Writer& writer,
        std::span<const PixelType> image_data,
        uint32_t image_width,
        uint32_t image_height,
        uint32_t tile_width,
        uint32_t tile_height,
        uint16_t samples_per_pixel,
        PlanarConfiguration planar_config,
        CompressionScheme compression,
        Predictor predictor,
        const ExtractedTags<TagArgs...>& additional_tags = ExtractedTags<TagArgs...>{}) noexcept;

    /// @brief Write a complete single-image 3D tiled TIFF file
    /// @tparam InputSpec Image data layout specification (DHWC, DCHW, or CDHW)
    /// @tparam Writer Writer type implementing RawWriter concept
    /// @tparam TagArgs Variadic template parameters for additional tags
    /// @param writer The output writer
    /// @param image_data Image pixel data buffer
    /// @param image_width Image width in pixels
    /// @param image_height Image height in pixels
    /// @param image_depth Image depth in slices
    /// @param tile_width Tile width in pixels
    /// @param tile_height Tile height in pixels
    /// @param tile_depth Tile depth in slices
    /// @param samples_per_pixel Number of channels/samples per pixel
    /// @param planar_config Planar configuration (Chunky or Planar)
    /// @param compression Compression scheme to use
    /// @param predictor Predictor to apply before compression
    /// @param additional_tags Optional additional TIFF tags to include
    /// @return Result<void> indicating success or error
    /// @retval Success Image written successfully
    /// @retval FileNotFound Output file cannot be created
    /// @retval WriteError Error during file write operation
    /// @retval InvalidTag User-provided tag conflicts with mandatory tags
    /// @retval OutOfBounds Buffer size mismatch
    /// @retval CompressionError Compression failed
    /// @note Supports 3D volumetric TIFF images with depth dimension
    /// @note ImageDepth and TileDepth tags are automatically set when depth > 1
    template <ImageLayoutSpec InputSpec, typename Writer, typename... TagArgs>
        requires RawWriter<Writer>
    [[nodiscard]] Result<void> write_single_image(
        Writer& writer,
        std::span<const PixelType> image_data,
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
        const ExtractedTags<TagArgs...>& additional_tags = ExtractedTags<TagArgs...>{}) noexcept;
    
    /// @brief Write a stripped image (convenience wrapper for strip-based layout)
    /// @tparam InputSpec Image data layout specification (DHWC, DCHW, or CDHW)
    /// @tparam Writer Writer type implementing RawWriter concept
    /// @tparam TagArgs Variadic template parameters for additional tags
    /// @param writer The output writer
    /// @param image_data Image pixel data buffer
    /// @param image_width Image width in pixels
    /// @param image_height Image height in pixels
    /// @param rows_per_strip Number of rows per strip
    /// @param samples_per_pixel Number of channels/samples per pixel
    /// @param planar_config Planar configuration (Chunky or Planar)
    /// @param compression Compression scheme to use
    /// @param predictor Predictor to apply before compression
    /// @param additional_tags Optional additional TIFF tags to include
    /// @return Result<void> indicating success or error
    /// @retval Success Image written successfully
    /// @retval FileNotFound Output file cannot be created
    /// @retval WriteError Error during file write operation
    /// @retval InvalidTag User-provided tag conflicts with mandatory tags
    /// @retval UnsupportedFeature rows_per_strip does not evenly divide image_height
    /// @retval OutOfBounds Buffer size mismatch
    /// @retval CompressionError Compression failed
    /// @note Current limitation: rows_per_strip must evenly divide image_height
    /// @note Sets RowsPerStrip tag automatically
    template <ImageLayoutSpec InputSpec, typename Writer, typename... TagArgs>
        requires RawWriter<Writer>
    [[nodiscard]] Result<void> write_stripped_image(
        Writer& writer,
        std::span<const PixelType> image_data,
        uint32_t image_width,
        uint32_t image_height,
        uint32_t rows_per_strip,
        uint16_t samples_per_pixel,
        PlanarConfiguration planar_config,
        CompressionScheme compression,
        Predictor predictor,
        const ExtractedTags<TagArgs...>& additional_tags = ExtractedTags<TagArgs...>{}) noexcept;
    
    /// @brief Clear writer state for reuse
    /// @note Resets internal buffers and allows writing a new image
    void clear() noexcept;

private:
    ImageWriterType image_writer_;
    IFDBuilderType ifd_builder_;
    IFDPlacement placement_strategy_;
    
    /// Write TIFF header
    template <typename Writer>
        requires RawWriter<Writer>
    [[nodiscard]] static Result<void> write_header(
        Writer& writer,
        ifd::IFDOffset first_ifd_offset) noexcept;

    using StripOffsetsT = std::conditional_t<TiffFormat == TiffFormatType::Classic, StripOffsetsTag, StripOffsetsTag_BigTIFF>;
    using StripByteCountsT = std::conditional_t<TiffFormat == TiffFormatType::Classic, StripByteCountsTag, StripByteCountsTag_BigTIFF>;
    using TileOffsetsT = std::conditional_t<TiffFormat == TiffFormatType::Classic, TileOffsetsTag, TileOffsetsTag_BigTIFF>;
    using TileByteCountsT = std::conditional_t<TiffFormat == TiffFormatType::Classic, TileByteCountsTag, TileByteCountsTag_BigTIFF>;

    using WritingTagsType = ExtractedTags<
        OptTag_t<ImageWidthTag>,
        OptTag_t<ImageLengthTag>,
        OptTag_t<BitsPerSampleTag>,
        OptTag_t<CompressionTag>,
        OptTag_t<PhotometricInterpretationTag>,
        OptTag_t<StripOffsetsT>,
        OptTag_t<SamplesPerPixelTag>,
        OptTag_t<RowsPerStripTag>,
        OptTag_t<StripByteCountsT>,
        OptTag_t<PlanarConfigurationTag>,
        OptTag_t<PredictorTag>,
        OptTag_t<TileWidthTag>,
        OptTag_t<TileLengthTag>,
        OptTag_t<TileOffsetsT>,
        OptTag_t<TileByteCountsT>,
        OptTag_t<SampleFormatTag>,
        OptTag_t<ImageDepthTag>,
        OptTag_t<TileDepthTag>
    >;

    template <TagCode Code, typename... DstTagArgs, typename... SrcTagArgs, typename DefaultType>
    static inline void fill_tag_if_missing_in_src(
        ExtractedTags<DstTagArgs...>& metadata_dst,
        const ExtractedTags<SrcTagArgs...>& metadata_src,
        DefaultType&& default_value) noexcept;

    template <TagCode Code, typename... SrcTagArgs, typename DefaultType>
    [[nodiscard]] static inline Result<void> validate_invalid_override(
        const ExtractedTags<SrcTagArgs...>& src_tags,
        DefaultType&& valid_value) noexcept;

    /// Validate that the user provided tags do not define mandatory tags,
    /// unless they are already the correct value or nullopt.
    template <typename... UserTags>
    static Result<void> validate_user_tags(
        const ExtractedTags<UserTags...>& src_tags,
        uint32_t image_width,
        uint32_t image_height,
        uint32_t image_depth,
        uint32_t tile_width,
        uint32_t tile_height,
        uint32_t tile_depth,
        bool     is_tiled,
        uint16_t samples_per_pixel,
        PlanarConfiguration planar_config,
        CompressionScheme compression,
        Predictor predictor) noexcept;

    template <typename... UserTags>
    static void fill_mandatory_tags(
        WritingTagsType& dst_tags,
        const ExtractedTags<UserTags...>& src_tags,
        uint32_t image_width,
        uint32_t image_height,
        uint32_t image_depth,
        uint32_t tile_width,
        uint32_t tile_height,
        uint32_t tile_depth,
        bool     is_tiled,
        uint16_t samples_per_pixel,
        PlanarConfiguration planar_config,
        CompressionScheme compression,
        Predictor predictor,
        uint32_t  num_strips_or_tiles) noexcept;

    static void fill_tile_arrays(
        WritingTagsType& tags,
        const WrittenImageInfo& image_info) noexcept;

    static void fill_strip_arrays(
        WritingTagsType& tags,
        const WrittenImageInfo& image_info) noexcept;

    template <ImageLayoutSpec InputSpec, typename Writer, typename... TagArgs>
        requires RawWriter<Writer>
    [[nodiscard]] Result<void> write_image_impl(
        Writer& writer,
        std::span<const PixelType> image_data,
        uint32_t image_width,
        uint32_t image_height,
        uint32_t image_depth,
        uint32_t tile_width,
        uint32_t tile_height,
        uint32_t tile_depth,
        bool     is_tiled,
        uint16_t samples_per_pixel,
        PlanarConfiguration planar_config,
        CompressionScheme compression,
        Predictor predictor,
        const ExtractedTags<TagArgs...>& additional_tags = ExtractedTags<TagArgs...>{}) noexcept;
};

} // namespace tiffconcept

#define TIFFCONCEPT_TIFF_WRITER_HEADER
#include "impl/tiff_writer_impl.hpp"