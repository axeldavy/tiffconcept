#pragma once

#include <cstdint>
#include <type_traits>
#include "result.hpp"
#include "tag_extraction.hpp"
#include "tag_spec.hpp"
#include "types.hpp"

namespace tiffconcept {

/// Represents a rectangular region in an image
struct ImageRegion {
    uint32_t start_x;
    uint32_t start_y;
    uint32_t start_z;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint16_t start_channel;  // Starting channel index (0 for first channel)
    uint16_t num_channels;   // Number of channels to read.
    
    constexpr ImageRegion(uint16_t start_ch,
                          uint32_t z,
                          uint32_t y,
                          uint32_t x,
                          uint16_t num_ch,
                          uint32_t depth,
                          uint32_t height,
                          uint32_t width) noexcept
        : start_x(x), start_y(y), start_z(z), width(width), height(height), depth(depth)
        , start_channel(start_ch), num_channels(num_ch) {}
    
    /// Get the end coordinates (exclusive)
    [[nodiscard]] constexpr uint32_t end_x() const noexcept { return start_x + width; }
    [[nodiscard]] constexpr uint32_t end_y() const noexcept { return start_y + height; }
    [[nodiscard]] constexpr uint32_t end_z() const noexcept { return start_z + depth; }
    [[nodiscard]] constexpr uint16_t end_channel() const noexcept { return start_channel + num_channels; }
    
    /// Check if the region is empty
    [[nodiscard]] constexpr bool is_empty() const noexcept {
        return width == 0 || height == 0 || depth == 0 || num_channels == 0;
    }
    
    /// Get the total number of pixels in this region
    [[nodiscard]] constexpr std::size_t num_pixels() const noexcept {
        return static_cast<std::size_t>(width) * height * depth;
    }

    /// Get the total number of samples (pixels * channels) in this region
    [[nodiscard]] constexpr std::size_t num_samples() const noexcept {
        return num_pixels() * static_cast<std::size_t>(num_channels);
    }
};

/// Common image shape metadata that applies to both tiled and stripped images
/// Contains width, height, depth, samples, bit depth, format, and planar configuration
/// Can be extracted before deciding whether to read the full image
class ImageShape {
private:
    uint32_t image_width_;
    uint32_t image_height_;
    uint32_t image_depth_;
    uint16_t bits_per_sample_;
    uint16_t samples_per_pixel_;
    SampleFormat sample_format_;
    PlanarConfiguration planar_configuration_;

public:
    ImageShape() noexcept
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
    [[nodiscard]] Result<void> update_from_metadata(
        const ExtractedTags<TagSpec>& metadata) noexcept {
        
        // Extract needed fields
        const auto width_val = metadata.template get<TagCode::ImageWidth>();
        const auto height_val = metadata.template get<TagCode::ImageLength>();
        const auto bits_per_sample_val = metadata.template get<TagCode::BitsPerSample>();
        
        // Validation
        if (!optional::is_value_present(width_val)) {
            return Err(Error::Code::InvalidTag, "ImageWidth tag not found or invalid");
        }
        if (!optional::is_value_present(height_val)) {
            return Err(Error::Code::InvalidTag, "ImageLength tag not found or invalid");
        }
        if (!optional::is_value_present(bits_per_sample_val)) {
            return Err(Error::Code::InvalidTag, "BitsPerSample tag not found or invalid");
        }
        
        // Extract scalar values
        image_width_ = optional::unwrap_value(width_val);
        image_height_ = optional::unwrap_value(height_val);
        const auto& bits_per_sample_array = optional::unwrap_value(bits_per_sample_val);
        if (bits_per_sample_array.empty()) {
            return Err(Error::Code::InvalidTag, "BitsPerSample tag is empty");
        }
        bits_per_sample_ = bits_per_sample_array[0];
        for (const auto& bps : bits_per_sample_array) {
            if (bps != bits_per_sample_) {
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

        if (bits_per_sample_array.size() != samples_per_pixel_) {
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
    [[nodiscard]] Result<void> validate_pixel_type() const noexcept {
        if constexpr (std::is_same_v<PixelType, uint8_t>) {
            if (bits_per_sample_ != 8 || sample_format_ != SampleFormat::UnsignedInt) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type uint8_t requires 8-bit unsigned integer format");
            }
        } else if constexpr (std::is_same_v<PixelType, uint16_t>) {
            if (bits_per_sample_ != 16 || sample_format_ != SampleFormat::UnsignedInt) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type uint16_t requires 16-bit unsigned integer format");
            }
        } else if constexpr (std::is_same_v<PixelType, uint32_t>) {
            if (bits_per_sample_ != 32 || sample_format_ != SampleFormat::UnsignedInt) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type uint32_t requires 32-bit unsigned integer format");
            }
        } else if constexpr (std::is_same_v<PixelType, uint64_t>) {
            if (bits_per_sample_ != 64 || sample_format_ != SampleFormat::UnsignedInt) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type uint64_t requires 64-bit unsigned integer format");
            }
        } else if constexpr (std::is_same_v<PixelType, int8_t>) {
            if (bits_per_sample_ != 8 || sample_format_ != SampleFormat::SignedInt) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type int8_t requires 8-bit signed integer format");
            }
        } else if constexpr (std::is_same_v<PixelType, int16_t>) {
            if (bits_per_sample_ != 16 || sample_format_ != SampleFormat::SignedInt) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type int16_t requires 16-bit signed integer format");
            }
        } else if constexpr (std::is_same_v<PixelType, int32_t>) {
            if (bits_per_sample_ != 32 || sample_format_ != SampleFormat::SignedInt) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type int32_t requires 32-bit signed integer format");
            }
        } else if constexpr (std::is_same_v<PixelType, int64_t>) {
            if (bits_per_sample_ != 64 || sample_format_ != SampleFormat::SignedInt) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type int64_t requires 64-bit signed integer format");
            }
        } else if constexpr (std::is_same_v<PixelType, float>) {
            if (bits_per_sample_ != 32 || sample_format_ != SampleFormat::IEEEFloat) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type float requires 32-bit IEEE float format");
            }
        } else if constexpr (std::is_same_v<PixelType, double>) {
            if (bits_per_sample_ != 64 || sample_format_ != SampleFormat::IEEEFloat) {
                return Err(Error::Code::InvalidFormat, 
                           "Pixel type double requires 64-bit IEEE float format");
            }
        }
        return Ok();
    }
    
    // Getters
    [[nodiscard]] uint32_t image_width() const noexcept { return image_width_; }
    [[nodiscard]] uint32_t image_height() const noexcept { return image_height_; }
    [[nodiscard]] uint32_t image_depth() const noexcept { return image_depth_; }
    [[nodiscard]] uint16_t bits_per_sample() const noexcept { return bits_per_sample_; }
    [[nodiscard]] uint16_t samples_per_pixel() const noexcept { return samples_per_pixel_; }
    [[nodiscard]] SampleFormat sample_format() const noexcept { return sample_format_; }
    [[nodiscard]] PlanarConfiguration planar_configuration() const noexcept { return planar_configuration_; }
    
    [[nodiscard]] bool is_3d() const noexcept { return image_depth_ > 1; }
    [[nodiscard]] bool is_multi_channel() const noexcept { return samples_per_pixel_ > 1; }
    [[nodiscard]] bool is_planar() const noexcept { return planar_configuration_ == PlanarConfiguration::Planar; }
    
    /// Get the total number of pixels in the image
    [[nodiscard]] std::size_t num_pixels() const noexcept {
        return static_cast<std::size_t>(image_width_) * image_height_ * image_depth_;
    }
    
    /// Get the total number of elements (pixels * samples)
    [[nodiscard]] std::size_t num_elements() const noexcept {
        return num_pixels() * samples_per_pixel_;
    }
    
    /// Create a region representing the full image
    [[nodiscard]] ImageRegion full_region() const noexcept {
        return ImageRegion(0, 0, 0, 0, samples_per_pixel_, image_depth_, image_height_, image_width_);
    }
    
    /// Validate that a region fits within the image bounds
    [[nodiscard]] Result<void> validate_region(const ImageRegion& region) const noexcept {
        if (region.is_empty()) {
            return Err(Error::Code::OutOfBounds, "Region is empty");
        }
        
        if (region.end_x() > image_width_) {
            return Err(Error::Code::OutOfBounds, "Region exceeds image width");
        }
        
        if (region.end_y() > image_height_) {
            return Err(Error::Code::OutOfBounds, "Region exceeds image height");
        }
        
        if (region.end_z() > image_depth_) {
            return Err(Error::Code::OutOfBounds, "Region exceeds image depth");
        }

        if (region.start_channel >= samples_per_pixel_) {
            return Err(Error::Code::OutOfBounds, "Start channel exceeds available channels");
        }

        if (region.end_channel() > samples_per_pixel_) {
            return Err(Error::Code::OutOfBounds, "Channel range exceeds available channels");
        }
        
        return Ok();
    }
};

} // namespace tiffconcept
