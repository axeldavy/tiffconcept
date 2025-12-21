#pragma once

#include <cstdint>
#include <type_traits>
#include "../types/result.hpp"
#include "../tag_extraction.hpp"
#include "../types/optional.hpp"
#include "../types/tag_spec.hpp"
#include "../types/tiff_spec.hpp"

#ifndef TIFFCONCEPT_IMAGE_SHAPE_HEADER
#include "../image_shape.hpp" // for linters
#endif

namespace tiffconcept {

constexpr ImageRegion::ImageRegion(uint16_t start_ch,
                        uint32_t z,
                        uint32_t y,
                        uint32_t x,
                        uint16_t num_ch,
                        uint32_t depth,
                        uint32_t height,
                        uint32_t width) noexcept
    : start_channel(start_ch), start_z(z), start_y(y), start_x(x),
      num_channels(num_ch), depth(depth), height(height), width(width) {}

/// Get the end coordinates (exclusive)
inline constexpr uint32_t ImageRegion::end_x() const noexcept { return start_x + width; }
inline constexpr uint32_t ImageRegion::end_y() const noexcept { return start_y + height; }
inline constexpr uint32_t ImageRegion::end_z() const noexcept { return start_z + depth; }
inline constexpr uint16_t ImageRegion::end_channel() const noexcept { return start_channel + num_channels; }

/// Check if the region is empty
inline constexpr bool ImageRegion::is_empty() const noexcept {
    return width == 0 || height == 0 || depth == 0 || num_channels == 0;
}

/// Get the total number of pixels in this region
inline constexpr std::size_t ImageRegion::num_pixels() const noexcept {
    return static_cast<std::size_t>(width) * height * depth;
}

/// Get the total number of samples (pixels * channels) in this region
inline constexpr std::size_t ImageRegion::num_samples() const noexcept {
    return num_pixels() * static_cast<std::size_t>(num_channels);
}

inline ImageShape::ImageShape() noexcept
    : image_width_(0)
    , image_height_(0)
    , image_depth_(1)
    , bits_per_sample_(0)
    , samples_per_pixel_(1)
    , sample_format_(SampleFormat::UnsignedInt)
    , planar_configuration_(PlanarConfiguration::Chunky) {}

/// Extract common image shape information from metadata
/// Does not require tile/strip-specific tags
template <typename TagSpec>
    requires ImageDimensionTagSpec<TagSpec>
inline Result<void> ImageShape::update_from_metadata(
    const ExtractedTags<TagSpec>& metadata) noexcept {
    
    // Extract needed fields
    const auto width_val = metadata.template get<TagCode::ImageWidth>();
    const auto height_val = metadata.template get<TagCode::ImageLength>();
    const auto bits_per_sample_val = metadata.template get<TagCode::BitsPerSample>();
    
    // Validation
    if (!optional::is_value_present(width_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "ImageWidth tag not found or invalid");
    }
    if (!optional::is_value_present(height_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "ImageLength tag not found or invalid");
    }
    if (!optional::is_value_present(bits_per_sample_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "BitsPerSample tag not found or invalid");
    }
    
    // Extract scalar values
    image_width_ = optional::unwrap_value(width_val);
    image_height_ = optional::unwrap_value(height_val);
    const auto& bits_per_sample_array = optional::unwrap_value(bits_per_sample_val);
    if (bits_per_sample_array.empty()) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "BitsPerSample tag is empty");
    }
    bits_per_sample_ = bits_per_sample_array[0];
    for (const auto& bps : bits_per_sample_array) {
        if (bps != bits_per_sample_) [[unlikely]] {
            return Err(Error::Code::UnsupportedFeature, "All samples must have the same BitsPerSample");
        }
    }
    
    // Extract 3D information (optional)
    image_depth_ = 1;
    if constexpr (TagSpec::template has_tag<TagCode::ImageDepth>()) {
        image_depth_ = optional::extract_tag_or<TagCode::ImageDepth, TagSpec>(
            metadata, uint32_t{1}
        );
    }
    
    // Extract multi-channel information
    samples_per_pixel_ = 1;
    if constexpr (TagSpec::template has_tag<TagCode::SamplesPerPixel>()) {
        samples_per_pixel_ = optional::extract_tag_or<TagCode::SamplesPerPixel, TagSpec>(
            metadata, uint16_t{1}
        );
    }

    if (bits_per_sample_array.size() != samples_per_pixel_) [[unlikely]] {
        return Err(Error::Code::UnsupportedFeature, 
                    "BitsPerSample array size does not match SamplesPerPixel");
    }
    
    planar_configuration_ = PlanarConfiguration::Chunky;
    if constexpr (TagSpec::template has_tag<TagCode::PlanarConfiguration>()) {
        auto planar_val = optional::extract_tag_or<TagCode::PlanarConfiguration, TagSpec>(
            metadata, uint16_t{1}
        );
        planar_configuration_ = static_cast<PlanarConfiguration>(planar_val);
    }
    
    // Extract sample format
    sample_format_ = SampleFormat::UnsignedInt;
    if constexpr (TagSpec::template has_tag<TagCode::SampleFormat>()) {
        sample_format_ = optional::extract_tag_or<TagCode::SampleFormat, TagSpec>(
            metadata, SampleFormat::UnsignedInt
        );
    }
    
    return Ok();
}

/// Validate that the PixelType matches the bits_per_sample and sample_format
/// Support for other types can be added as needed
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
inline Result<void> ImageShape::validate_pixel_type() const noexcept {
    if constexpr (std::is_same_v<PixelType, uint8_t>) {
        if (bits_per_sample_ != 8 || sample_format_ != SampleFormat::UnsignedInt) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type uint8_t requires 8-bit unsigned integer format");
        }
    } else if constexpr (std::is_same_v<PixelType, uint16_t>) {
        if (bits_per_sample_ != 16 || sample_format_ != SampleFormat::UnsignedInt) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type uint16_t requires 16-bit unsigned integer format");
        }
    } else if constexpr (std::is_same_v<PixelType, uint32_t>) {
        if (bits_per_sample_ != 32 || sample_format_ != SampleFormat::UnsignedInt) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type uint32_t requires 32-bit unsigned integer format");
        }
    } else if constexpr (std::is_same_v<PixelType, uint64_t>) {
        if (bits_per_sample_ != 64 || sample_format_ != SampleFormat::UnsignedInt) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type uint64_t requires 64-bit unsigned integer format");
        }
    } else if constexpr (std::is_same_v<PixelType, int8_t>) {
        if (bits_per_sample_ != 8 || sample_format_ != SampleFormat::SignedInt) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type int8_t requires 8-bit signed integer format");
        }
    } else if constexpr (std::is_same_v<PixelType, int16_t>) {
        if (bits_per_sample_ != 16 || sample_format_ != SampleFormat::SignedInt) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type int16_t requires 16-bit signed integer format");
        }
    } else if constexpr (std::is_same_v<PixelType, int32_t>) {
        if (bits_per_sample_ != 32 || sample_format_ != SampleFormat::SignedInt) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type int32_t requires 32-bit signed integer format");
        }
    } else if constexpr (std::is_same_v<PixelType, int64_t>) {
        if (bits_per_sample_ != 64 || sample_format_ != SampleFormat::SignedInt) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type int64_t requires 64-bit signed integer format");
        }
    } else if constexpr (std::is_same_v<PixelType, float>) {
        if (bits_per_sample_ != 32 || sample_format_ != SampleFormat::IEEEFloat) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type float requires 32-bit IEEE float format");
        }
    } else if constexpr (std::is_same_v<PixelType, double>) {
        if (bits_per_sample_ != 64 || sample_format_ != SampleFormat::IEEEFloat) [[unlikely]] {
            return Err(Error::Code::InvalidFormat, 
                        "Pixel type double requires 64-bit IEEE float format");
        }
    }
    return Ok();
}

// Getters
inline uint32_t ImageShape::image_width() const noexcept { return image_width_; }
inline uint32_t ImageShape::image_height() const noexcept { return image_height_; }
inline uint32_t ImageShape::image_depth() const noexcept { return image_depth_; }
inline uint16_t ImageShape::bits_per_sample() const noexcept { return bits_per_sample_; }
inline uint16_t ImageShape::samples_per_pixel() const noexcept { return samples_per_pixel_; }
inline SampleFormat ImageShape::sample_format() const noexcept { return sample_format_; }
inline PlanarConfiguration ImageShape::planar_configuration() const noexcept { return planar_configuration_; }

inline bool ImageShape::is_3d() const noexcept { return image_depth_ > 1; }
inline bool ImageShape::is_multi_channel() const noexcept { return samples_per_pixel_ > 1; }
inline bool ImageShape::is_planar() const noexcept { return planar_configuration_ == PlanarConfiguration::Planar; }

/// Get the total number of pixels in the image
inline std::size_t ImageShape::num_pixels() const noexcept {
    return static_cast<std::size_t>(image_width_) * image_height_ * image_depth_;
}

/// Get the total number of elements (pixels * samples)
inline std::size_t ImageShape::num_elements() const noexcept {
    return num_pixels() * samples_per_pixel_;
}

/// Create a region representing the full image
inline ImageRegion ImageShape::full_region() const noexcept {
    return ImageRegion(0, 0, 0, 0, samples_per_pixel_, image_depth_, image_height_, image_width_);
}

/// Validate that a region fits within the image bounds
inline Result<void> ImageShape::validate_region(const ImageRegion& region) const noexcept {
    if (region.is_empty()) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Region is empty");
    }
    
    if (region.end_x() > image_width_) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Region exceeds image width");
    }
    
    if (region.end_y() > image_height_) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Region exceeds image height");
    }
    
    if (region.end_z() > image_depth_) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Region exceeds image depth");
    }

    if (region.start_channel >= samples_per_pixel_) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Start channel exceeds available channels");
    }

    if (region.end_channel() > samples_per_pixel_) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Channel range exceeds available channels");
    }
    
    return Ok();
}

} // namespace tiffconcept
