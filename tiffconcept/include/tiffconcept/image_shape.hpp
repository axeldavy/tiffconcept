#pragma once

#include <cstdint>
#include <type_traits>
#include "types/result.hpp"
#include "tag_extraction.hpp"
#include "types/tag_spec.hpp"
#include "types/tiff_spec.hpp"

namespace tiffconcept {

/// @brief Represents a rectangular region in an image
/// @note Supports 3D volumetric regions and multi-channel images
/// @note Coordinates are inclusive at start, exclusive at end
struct ImageRegion {
    uint16_t start_channel;  ///< Starting channel index (0 for first channel)
    uint32_t start_z;        ///< Starting Z coordinate (inclusive, for 3D images)
    uint32_t start_y;        ///< Starting Y coordinate (inclusive)
    uint32_t start_x;        ///< Starting X coordinate (inclusive)
    uint16_t num_channels;   ///< Number of channels to read
    uint32_t depth;          ///< Depth in slices (1 for 2D images)
    uint32_t height;         ///< Height in pixels
    uint32_t width;          ///< Width in pixels
    
    /// @brief Construct an image region
    /// @param start_ch Starting channel index
    /// @param z Starting Z coordinate
    /// @param y Starting Y coordinate
    /// @param x Starting X coordinate
    /// @param num_ch Number of channels
    /// @param depth Depth in slices
    /// @param height Height in pixels
    /// @param width Width in pixels
    constexpr ImageRegion(uint16_t start_ch,
                          uint32_t z,
                          uint32_t y,
                          uint32_t x,
                          uint16_t num_ch,
                          uint32_t depth,
                          uint32_t height,
                          uint32_t width) noexcept;
    
    /// @brief Get the exclusive end X coordinate
    [[nodiscard]] constexpr uint32_t end_x() const noexcept;
    
    /// @brief Get the exclusive end Y coordinate
    [[nodiscard]] constexpr uint32_t end_y() const noexcept;
    
    /// @brief Get the exclusive end Z coordinate
    [[nodiscard]] constexpr uint32_t end_z() const noexcept;
    
    /// @brief Get the exclusive end channel index
    [[nodiscard]] constexpr uint16_t end_channel() const noexcept;
    
    /// @brief Check if the region is empty (zero width/height/depth/channels)
    [[nodiscard]] constexpr bool is_empty() const noexcept;
    
    /// @brief Get the total number of pixels in this region
    /// @return width × height × depth
    [[nodiscard]] constexpr std::size_t num_pixels() const noexcept;

    /// @brief Get the total number of samples (pixels × channels) in this region
    /// @return width × height × depth × num_channels
    [[nodiscard]] constexpr std::size_t num_samples() const noexcept;
};

/// @brief Common image shape metadata for both tiled and stripped images
/// @note Contains width, height, depth, samples, bit depth, format, and planar configuration
/// @note Can be extracted before deciding whether to read the full image
/// @note Provides validation for pixel types and image regions
class ImageShape {
public:
    /// @brief Default constructor
    /// @note Initializes to invalid state (width=0, height=0)
    ImageShape() noexcept;
    
    /// @brief Extract common image shape information from metadata
    /// @tparam TagSpec Tag specification type (must satisfy ImageDimensionTagSpec)
    /// @param metadata Extracted tags containing image dimension information
    /// @return Result<void> indicating success or error
    /// @retval Success Metadata extracted successfully
    /// @retval InvalidTag Required tag missing (ImageWidth, ImageLength, or BitsPerSample)
    /// @retval UnsupportedFeature All samples must have the same BitsPerSample
    /// @retval UnsupportedFeature BitsPerSample array size doesn't match SamplesPerPixel
    /// @note Does not require tile/strip-specific tags
    /// @note Extracts: ImageWidth, ImageLength, BitsPerSample (required)
    /// @note Extracts: ImageDepth, SamplesPerPixel, PlanarConfiguration, SampleFormat (optional)
    template <typename TagSpec>
        requires ImageDimensionTagSpec<TagSpec>
    [[nodiscard]] Result<void> update_from_metadata(
        const ExtractedTags<TagSpec>& metadata) noexcept;
    
    /// @brief Validate that the PixelType matches bits_per_sample and sample_format
    /// @tparam PixelType The pixel data type to validate
    /// @return Result<void> indicating success or error
    /// @retval Success PixelType matches the image format
    /// @retval InvalidFormat PixelType doesn't match bits_per_sample or sample_format
    /// @note Supported types: uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, float, double
    /// @note Validates both bit depth and signedness/float format
    template <typename PixelType>
    requires (std::is_same_v<PixelType, uint8_t> || 
              std::is_same_v<PixelType, uint16_t> ||
              std::is_same_v<PixelType, uint32_t> ||
              std::is_same_v<PixelType, uint64_t> ||
              std::is_same_v<PixelType, int8_t> ||
              std::is_same_v<PixelType, int16_t> ||
              std::is_same_v<PixelType, int32_t> ||
              std::is_same_v<PixelType, int64_t> ||
              std::is_same_v<PixelType, float> ||
              std::is_same_v<PixelType, double>)
    [[nodiscard]] Result<void> validate_pixel_type() const noexcept;
    
    /// @brief Get image width in pixels
    [[nodiscard]] uint32_t image_width() const noexcept;
    
    /// @brief Get image height in pixels
    [[nodiscard]] uint32_t image_height() const noexcept;
    
    /// @brief Get image depth in slices (1 for 2D images)
    [[nodiscard]] uint32_t image_depth() const noexcept;
    
    /// @brief Get bits per sample (e.g., 8, 16, 32, 64)
    [[nodiscard]] uint16_t bits_per_sample() const noexcept;
    
    /// @brief Get samples per pixel (channels, e.g., 1 for grayscale, 3 for RGB)
    [[nodiscard]] uint16_t samples_per_pixel() const noexcept;
    
    /// @brief Get sample format (UnsignedInt, SignedInt, IEEEFloat, Undefined)
    [[nodiscard]] SampleFormat sample_format() const noexcept;
    
    /// @brief Get planar configuration (Chunky or Planar)
    [[nodiscard]] PlanarConfiguration planar_configuration() const noexcept;
    
    /// @brief Check if this is a 3D volumetric image (depth > 1)
    [[nodiscard]] bool is_3d() const noexcept;
    
    /// @brief Check if this is a multi-channel image (samples_per_pixel > 1)
    [[nodiscard]] bool is_multi_channel() const noexcept;
    
    /// @brief Check if planar configuration is Planar (separate planes per channel)
    [[nodiscard]] bool is_planar() const noexcept;
    
    /// @brief Get the total number of pixels in the image
    /// @return width × height × depth
    [[nodiscard]] std::size_t num_pixels() const noexcept;
    
    /// @brief Get the total number of elements (pixels × samples)
    /// @return width × height × depth × samples_per_pixel
    [[nodiscard]] std::size_t num_elements() const noexcept;
    
    /// @brief Create a region representing the full image
    /// @return ImageRegion covering all pixels and channels
    [[nodiscard]] ImageRegion full_region() const noexcept;
    
    /// @brief Validate that a region fits within the image bounds
    /// @param region The region to validate
    /// @return Result<void> indicating success or error
    /// @retval Success Region is valid and within bounds
    /// @retval OutOfBounds Region is empty
    /// @retval OutOfBounds Region exceeds image width/height/depth
    /// @retval OutOfBounds Channel range exceeds available channels
    [[nodiscard]] Result<void> validate_region(const ImageRegion& region) const noexcept;

private:
    uint32_t image_width_;
    uint32_t image_height_;
    uint32_t image_depth_;
    uint16_t bits_per_sample_;
    uint16_t samples_per_pixel_;
    SampleFormat sample_format_;
    PlanarConfiguration planar_configuration_;
};

} // namespace tiffconcept

#define TIFFCONCEPT_IMAGE_SHAPE_HEADER
#include "impl/image_shape_impl.hpp"