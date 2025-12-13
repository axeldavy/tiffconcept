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
#include "write_strategy.hpp"

namespace tiff {

/// Complete TIFF file writer
/// Orchestrates header, IFD, and image data writing
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
    
private:
    ImageWriterType image_writer_;
    IFDBuilderType ifd_builder_;
    IFDPlacement placement_strategy_;
    
    /// Write TIFF header
    template <typename Writer>
        requires RawWriter<Writer>
    [[nodiscard]] Result<void> write_header(
        Writer& writer,
        ifd::IFDOffset first_ifd_offset) noexcept {
        
        HeaderType header;
        
        // Set byte order
        if constexpr (TargetEndian == std::endian::little) {
            header.byte_order = 0x4949;  // "II"
        } else {
            header.byte_order = 0x4D4D;  // "MM"
        }
        
        // Set magic number and first IFD offset
        if constexpr (TiffFormat == TiffFormatType::Classic) {
            header.magic = 42;
            header.first_ifd_offset = static_cast<uint32_t>(first_ifd_offset.value);
        } else {  // BigTIFF
            header.magic = 43;
            header.offset_size = 8;
            header.always_zero = 0;
            header.first_ifd_offset = static_cast<uint64_t>(first_ifd_offset.value);
        }
        
        // Write header
        auto view_result = writer.write(0, sizeof(HeaderType));
        if (!view_result) {
            return Err(view_result.error().code, "Failed to write TIFF header");
        }
        
        auto view = std::move(view_result.value());
        std::memcpy(view.data().data(), &header, sizeof(HeaderType));
        
        return view.flush();
    }
    
public:
    TiffWriter() = default;
    explicit TiffWriter(IFDPlacement placement) 
        : placement_strategy_(placement), ifd_builder_(placement) {}
    
    /// Write a complete single-image TIFF file
    /// This is the main high-level API for writing a TIFF image
    template <typename Writer, TagDescriptorType... Tags>
        requires RawWriter<Writer> && ValidTagSpec<TagSpec<Tags...>>
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
        const ExtractedTags<Tags...>& additional_tags) noexcept {
        
        constexpr std::size_t header_size = sizeof(HeaderType);
        
        // Determine if we write data before or after IFD
        bool data_first = placement_strategy_.write_data_before_ifd();
        
        std::size_t current_file_pos = header_size;
        ifd::IFDOffset first_ifd_offset;
        WrittenImageInfo image_info;
        
        if (data_first) {
            // Strategy: write image data first, then IFD
            // Image data starts right after header
            auto write_result = image_writer_.write_image(
                writer,
                image_data,
                image_width,
                image_height,
                1,  // depth
                tile_width,
                tile_height,
                1,  // tile_depth
                samples_per_pixel,
                planar_config,
                compression,
                predictor,
                current_file_pos
            );
            
            if (!write_result) {
                return Err(write_result.error().code, write_result.error().message);
            }
            
            image_info = std::move(write_result.value());
            current_file_pos += image_info.total_data_size;
            
            // Now build IFD with tile offset/bytecount information
            ifd_builder_.clear();
            
            // Add image metadata tags
            auto add_result = ifd_builder_.add_tags(additional_tags);
            if (!add_result) {
                return Err(add_result.error().code, add_result.error().message);
            }
            
            // Add tile/strip offset and bytecount tags
            // Note: We'd need TagDescriptor for TileOffsets and TileByteCounts
            // For now, simplified - in real implementation, use proper tag descriptors
            
            // Write IFD
            auto ifd_offset_result = ifd_builder_.write_to_file(writer, current_file_pos);
            if (!ifd_offset_result) {
                return Err(ifd_offset_result.error().code, ifd_offset_result.error().message);
            }
            
            first_ifd_offset = ifd_offset_result.value();
            
        } else {
            // Strategy: reserve space for IFD, write image data, then write IFD
            // This is more complex - need to estimate IFD size first
            
            // Build IFD structure first to know its size
            ifd_builder_.clear();
            auto add_result = ifd_builder_.add_tags(additional_tags);
            if (!add_result) {
                return Err(add_result.error().code, add_result.error().message);
            }
            
            // Estimate space needed (will add tile offsets/counts later)
            std::size_t estimated_ifd_size = ifd_builder_.calculate_total_size();
            
            // Add space for TileOffsets and TileByteCounts arrays
            // Each is an array of uint64_t (for BigTIFF) or uint32_t (Classic)
            uint32_t num_tiles = ((image_width + tile_width - 1) / tile_width) *
                                ((image_height + tile_height - 1) / tile_height);
            
            if (planar_config == PlanarConfiguration::Planar) {
                num_tiles *= samples_per_pixel;
            }
            
            std::size_t offset_array_size = num_tiles * sizeof(uint64_t);  // Simplified
            estimated_ifd_size += 2 * offset_array_size;  // Both offsets and byte counts
            
            // Reserve space for IFD
            std::size_t ifd_start = current_file_pos;
            current_file_pos += estimated_ifd_size;
            
            // Write image data
            auto write_result = image_writer_.write_image(
                writer,
                image_data,
                image_width,
                image_height,
                1,  // depth
                tile_width,
                tile_height,
                1,  // tile_depth
                samples_per_pixel,
                planar_config,
                compression,
                predictor,
                current_file_pos
            );
            
            if (!write_result) {
                return Err(write_result.error().code, write_result.error().message);
            }
            
            image_info = std::move(write_result.value());
            
            // Now write IFD with actual tile information
            // (Simplified - real implementation needs to add tile offset/count tags)
            first_ifd_offset = ifd::IFDOffset(ifd_start);
        }
        
        // Write header with first IFD offset
        auto header_result = write_header(writer, first_ifd_offset);
        if (!header_result) {
            return Err(header_result.error().code, header_result.error().message);
        }
        
        return writer.flush();
    }
    
    /// Write a stripped image (convenience wrapper)
    template <typename Writer, TagDescriptorType... Tags>
        requires RawWriter<Writer> && ValidTagSpec<TagSpec<Tags...>>
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
        const ExtractedTags<Tags...>& additional_tags) noexcept {
        
        return write_single_image(
            writer,
            image_data,
            image_width,
            image_height,
            image_width,  // tile_width = full width
            rows_per_strip,
            samples_per_pixel,
            planar_config,
            compression,
            predictor,
            additional_tags
        );
    }
    
    /// Clear writer state for reuse
    void clear() noexcept {
        image_writer_.clear();
        ifd_builder_.clear();
    }
};

} // namespace tiff
