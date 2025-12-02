#pragma once

#include <vector>
#include <span>
#include "metadata.hpp"
#include "result.hpp"
#include "types.hpp"
#include "image_shape.hpp"

namespace tiff {

/// Information about a single strip
struct StripInfo {
    uint32_t strip_index;   // Strip index
    uint32_t pixel_y;       // Starting pixel Y coordinate in image
    uint32_t width;         // Strip width in pixels (same as image width)
    uint32_t height;        // Strip height in pixels (typically rows_per_strip, less for last strip)
    uint32_t offset;        // File offset to compressed data
    uint32_t byte_count;    // Size of compressed data
};

/// Reusable stripped image information structure
template <typename PixelType>
class StrippedImageInfo {
private:
    ImageShape<PixelType> shape_;  // Common image properties
    
    uint32_t rows_per_strip_;
    
    std::vector<uint32_t> strip_offsets_;      // Owned data
    std::vector<uint32_t> strip_byte_counts_;  // Owned data
    
    CompressionScheme compression_;
    Predictor predictor_;
    
    uint32_t num_strips_;
    
public:
    StrippedImageInfo() noexcept
        : shape_()
        , rows_per_strip_(0)
        , strip_offsets_()
        , strip_byte_counts_()
        , compression_(CompressionScheme::None)
        , predictor_(Predictor::None)
        , num_strips_(0) {}
    
    /// Update this info to process a new image
    /// Copies metadata into owned storage (reuses allocations when possible)
    /// the original metadata object can be discarded after this call.
    template <typename TagSpec>
        requires StrippedImageTagSpec<TagSpec>
    [[nodiscard]] Result<void> update_from_metadata(
        const metadata_type_t<TagSpec>& metadata) noexcept {
        
        // Extract common image shape first
        auto shape_result = shape_.update_from_metadata(metadata);
        if (!shape_result) {
            return shape_result;
        }
        
        // Extract strip-specific fields
        auto rows_per_strip_val = extract_tag_value<TagCode::RowsPerStrip, TagSpec>(metadata);
        auto strip_offsets_val = extract_tag_value<TagCode::StripOffsets, TagSpec>(metadata);
        auto strip_byte_counts_val = extract_tag_value<TagCode::StripByteCounts, TagSpec>(metadata);
        
        // Validation
        if (!is_value_present(rows_per_strip_val)) {
            return Err(Error::Code::InvalidTag, "RowsPerStrip tag not found");
        }
        if (!is_value_present(strip_offsets_val)) {
            return Err(Error::Code::InvalidTag, "StripOffsets tag not found");
        }
        if (!is_value_present(strip_byte_counts_val)) {
            return Err(Error::Code::InvalidTag, "StripByteCounts tag not found");
        }
        
        // Extract strip dimensions
        rows_per_strip_ = unwrap_value(rows_per_strip_val);
        
        // Copy vector data (reuses allocation if size matches)
        const auto& offsets = unwrap_value(strip_offsets_val);
        const auto& byte_counts = unwrap_value(strip_byte_counts_val);
        
        strip_offsets_.assign(offsets.begin(), offsets.end());
        strip_byte_counts_.assign(byte_counts.begin(), byte_counts.end());
        
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
        num_strips_ = static_cast<uint32_t>(strip_offsets_.size());
        
        return Ok();
    }
    
    // Image shape access
    [[nodiscard]] const ImageShape<PixelType>& shape() const noexcept { return shape_; }
    
    // Strip-specific getters
    [[nodiscard]] uint32_t rows_per_strip() const noexcept { return rows_per_strip_; }
    [[nodiscard]] uint32_t num_strips() const noexcept { return num_strips_; }
    [[nodiscard]] CompressionScheme compression() const noexcept { return compression_; }
    [[nodiscard]] Predictor predictor() const noexcept { return predictor_; }
    
    [[nodiscard]] Result<StripInfo> get_strip_info(uint32_t strip_index, uint32_t plane = 0) const noexcept {
        // For planar configuration, calculate strips per plane
        uint32_t strips_per_plane = (shape_.image_height() + rows_per_strip_ - 1) / rows_per_strip_;
        uint32_t actual_strip_index;
        
        if (shape_.planar_configuration() == PlanarConfiguration::Planar) {
            if (plane >= shape_.samples_per_pixel()) {
                return Err(Error::Code::OutOfBounds, "Plane index out of bounds");
            }
            actual_strip_index = plane * strips_per_plane + strip_index;
        } else {
            actual_strip_index = strip_index;
        }
        
        if (actual_strip_index >= num_strips_) {
            return Err(Error::Code::OutOfBounds, "Strip index out of bounds");
        }
        
        StripInfo info;
        info.strip_index = actual_strip_index;
        info.pixel_y = strip_index * rows_per_strip_;  // Use logical strip_index for Y position
        info.width = shape_.image_width();
        info.height = std::min(rows_per_strip_, shape_.image_height() - info.pixel_y);
        info.offset = strip_offsets_[actual_strip_index];
        info.byte_count = strip_byte_counts_[actual_strip_index];
        
        return Ok(info);
    }
    
    [[nodiscard]] uint32_t strips_per_plane() const noexcept {
        return (shape_.image_height() + rows_per_strip_ - 1) / rows_per_strip_;
    }
};

} // namespace tiff
