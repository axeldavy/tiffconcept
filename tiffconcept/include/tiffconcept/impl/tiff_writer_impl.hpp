// Do not include this file directly. Include "tiff_writer.hpp" instead.

#pragma once

#include <cstring>
#include <span>
#include <vector>
#include "../lowlevel/encoder.hpp"
#include "../ifd.hpp"
#include "../image_writer.hpp"
#include "../lowlevel/ifd_builder.hpp"
#include "../reader_base.hpp"
#include "../tag_extraction.hpp"
#include "../types/result.hpp"
#include "../types/tag_spec.hpp"
#include "../types/tiff_spec.hpp"
#include "../strategy/write_strategy.hpp"

#ifndef TIFFCONCEPT_TIFF_WRITER_HEADER
#include "../tiff_writer.hpp" // for linters
#endif

namespace tiffconcept {

template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::TiffWriter(IFDPlacement placement)
    : placement_strategy_(placement), ifd_builder_(placement) {}

template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
template <typename Writer>
    requires RawWriter<Writer>
inline Result<void> TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::write_header(
    Writer& writer,
    ifd::IFDOffset first_ifd_offset) noexcept {

    if constexpr (TiffFormat == TiffFormatType::Classic) {
        std::array<uint8_t, 8> raw_header = {};
        if constexpr (TargetEndian == std::endian::little) {
            raw_header[0] = 'I';
            raw_header[1] = 'I';
            raw_header[2] = 42;
            raw_header[3] = 0;
        } else {
            raw_header[0] = 'M';
            raw_header[1] = 'M';
            raw_header[2] = 0;
            raw_header[3] = 42;
        }
        uint32_t ifd_offset = static_cast<uint32_t>(first_ifd_offset.value);
        if (TargetEndian == std::endian::little) {
            raw_header[4] = static_cast<uint8_t>(ifd_offset & 0xFF);
            raw_header[5] = static_cast<uint8_t>((ifd_offset >> 8) & 0xFF);
            raw_header[6] = static_cast<uint8_t>((ifd_offset >> 16) & 0xFF);
            raw_header[7] = static_cast<uint8_t>((ifd_offset >> 24) & 0xFF);
        } else {
            raw_header[4] = static_cast<uint8_t>((ifd_offset >> 24) & 0xFF);
            raw_header[5] = static_cast<uint8_t>((ifd_offset >> 16) & 0xFF);
            raw_header[6] = static_cast<uint8_t>((ifd_offset >> 8) & 0xFF);
            raw_header[7] = static_cast<uint8_t>(ifd_offset & 0xFF);
        }

        // Write header
        auto view_result = writer.write(0, sizeof(HeaderType));
        if (view_result.is_error()) [[unlikely]] {
            return Err(view_result.error().code, "Failed to write TIFF header: " + view_result.error().message);
        }

        auto view = std::move(view_result.value());
        std::memcpy(view.data().data(), raw_header.data(), sizeof(raw_header));
        return view.flush();
    } else if constexpr (TiffFormat == TiffFormatType::BigTIFF) {
        std::array<uint8_t, 16> raw_header = {};
        if constexpr (TargetEndian == std::endian::little) {
            raw_header[0] = 'I';
            raw_header[1] = 'I';
            raw_header[2] = 43;
            raw_header[3] = 0;
        } else {
            raw_header[0] = 'M';
            raw_header[1] = 'M';
            raw_header[2] = 0;
            raw_header[3] = 43;
        }
        // Offset size (8) and reserved (0)
        if (TargetEndian == std::endian::little) {
            raw_header[4] = 8;
            raw_header[5] = 0;
        } else {
            raw_header[4] = 0;
            raw_header[5] = 8;
        }
        raw_header[6] = 0;
        raw_header[7] = 0;
        uint64_t ifd_offset = static_cast<uint64_t>(first_ifd_offset.value);
        if (TargetEndian == std::endian::little) {
            for (int i = 0; i < 8; ++i) {
                raw_header[8 + i] = static_cast<uint8_t>((ifd_offset >> (i * 8)) & 0xFF);
            }
        } else {
            for (int i = 0; i < 8; ++i) {
                raw_header[8 + i] = static_cast<uint8_t>((ifd_offset >> ((7 - i) * 8)) & 0xFF);
            }
        }

        // Write header
        auto view_result = writer.write(0, sizeof(HeaderType));
        if (view_result.is_error()) [[unlikely]] {
            return Err(view_result.error().code, "Failed to write BigTIFF header: " + view_result.error().message);
        }

        auto view = std::move(view_result.value());
        std::memcpy(view.data().data(), raw_header.data(), sizeof(raw_header));
        return view.flush();
    } else {
        return Err(Error::Code::InvalidFormat, "Unsupported TIFF format for header writing");
    }
}

template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
template <TagCode Code, typename... DstTagArgs, typename... SrcTagArgs, typename DefaultType>
inline void TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::fill_tag_if_missing_in_src(
    ExtractedTags<DstTagArgs...>& metadata_dst,
    [[maybe_unused]] const ExtractedTags<SrcTagArgs...>& metadata_src,
    DefaultType&& default_value) noexcept {
    if constexpr (!std::remove_cvref_t<decltype(metadata_src)>::template has_tag<Code>()) {
        metadata_dst.template get<Code>() = std::forward<DefaultType>(default_value);
    } else if (!optional::is_value_present(metadata_src.template get<Code>())) {
        metadata_dst.template get<Code>() = std::forward<DefaultType>(default_value);
    } else {
        // Reset tag to enable memory reuse
        metadata_dst.template get<Code>() = std::nullopt;
    }
}

template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
template <TagCode Code, typename... SrcTagArgs, typename DefaultType>
inline Result<void> TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::validate_invalid_override(
    const ExtractedTags<SrcTagArgs...>& src_tags,
    DefaultType&& valid_value) noexcept {
    if constexpr (std::remove_cvref_t<decltype(src_tags)>::template has_tag<Code>()) {
        const auto& tag_opt = src_tags.template get<Code>();
        if (optional::unwrap_value_or(tag_opt, valid_value) != valid_value) [[unlikely]] {
            return Err(Error::Code::InvalidTag,
                        "User-provided tag " + std::to_string(static_cast<uint16_t>(Code)) +
                        " provides an incompatible override. Undefine the tag or provide the correct value.");
        }
    }
    return Ok();
}

/// Validate that the user provided tags do not define mandatory tags,
/// unless they are already the correct value or nullopt.
template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
template <typename... UserTags>
inline Result<void> TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::validate_user_tags(
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
    [[maybe_unused]] CompressionScheme compression,
    Predictor predictor) noexcept {

    // 1. ImageWidth
    auto res = validate_invalid_override<TagCode::ImageWidth>(src_tags, image_width);
    if (res.is_error()) [[unlikely]] {
        return res;
    }

    // 2. ImageLength
    res = validate_invalid_override<TagCode::ImageLength>(src_tags, image_height);
    if (res.is_error()) [[unlikely]] {
        return res;
    }

    // 3. BitsPerSample
    {
        if constexpr (std::remove_cvref_t<decltype(src_tags)>::template has_tag<TagCode::BitsPerSample>()) {
            const auto& bps_opt = src_tags.template get<TagCode::BitsPerSample>();
            if (optional::is_value_present(bps_opt)) {
                const auto& bps_values = optional::unwrap_value(bps_opt);
                if (bps_values.size() != samples_per_pixel) [[unlikely]] {
                    return Err(Error::Code::InvalidTag,
                                "User-provided BitsPerSample tag provides an incompatible override. "
                                "Number of samples does not match SamplesPerPixel.");
                }
                for (const auto& bps : bps_values) {
                    if (bps != sizeof(PixelType) * 8) [[unlikely]] {
                        return Err(Error::Code::InvalidTag,
                                    "User-provided BitsPerSample tag provides an incompatible override. "
                                    "BitsPerSample does not match PixelType size.");
                    }
                }
            }
        }
    }
    // 4. Compression
    // We do not validate compression as several compressions map to the same pixel data layout
    // 5. PhotometricInterpretation
    // We do not validate photometric interpretation as it may be something the user wants to override
    // 6. SamplesPerPixel
    res = validate_invalid_override<TagCode::SamplesPerPixel>(src_tags, samples_per_pixel);
    if (res.is_error()) [[unlikely]] {
        return res;
    }

    // 7. PlanarConfiguration
    res = validate_invalid_override<TagCode::PlanarConfiguration>(src_tags, static_cast<uint16_t>(planar_config));
    if (res.is_error()) [[unlikely]] {
        return res;
    }

    // 8. Predictor
    res = validate_invalid_override<TagCode::Predictor>(src_tags, predictor);
    if (res.is_error()) [[unlikely]] {
        return res;
    }

    // 9. SampleFormat
    {
        SampleFormat sf = SampleFormat::UnsignedInt;
        if constexpr (std::is_floating_point_v<PixelType>) sf = SampleFormat::IEEEFloat;
        else if constexpr (std::is_signed_v<PixelType>) sf = SampleFormat::SignedInt;
        res = validate_invalid_override<TagCode::SampleFormat>(src_tags, sf);
        if (res.is_error()) [[unlikely]] {
            return res;
        }
    }
    // 10. Strips/Tiles
    // Strips and tiles offsets and byte counts are generated during writing, thus must not be set
    if constexpr (std::remove_cvref_t<decltype(src_tags)>::template has_tag<TagCode::StripOffsets>()) {
        const auto& tag_opt = src_tags.template get<TagCode::StripOffsets>();
        if (optional::is_value_present(tag_opt)) [[unlikely]] {
            return Err(Error::Code::InvalidTag,
                        "User-provided StripOffsets tag is not allowed. It will be generated during writing.");
        }
        static_assert(optional::is_optional_v<std::remove_cvref_t<decltype(tag_opt)>>, "Only empty StripOffsets tag is allowed. User-provided StripOffsets tag is not allowed. It will be generated during writing.");
    }
    if constexpr (std::remove_cvref_t<decltype(src_tags)>::template has_tag<TagCode::StripByteCounts>()) {
        const auto& tag_opt = src_tags.template get<TagCode::StripByteCounts>();
        if (optional::is_value_present(tag_opt)) [[unlikely]] {
            return Err(Error::Code::InvalidTag,
                        "User-provided StripByteCounts tag is not allowed. It will be generated during writing.");
        }
        static_assert(optional::is_optional_v<std::remove_cvref_t<decltype(tag_opt)>>, "Only empty StripByteCounts tag is allowed. User-provided StripByteCounts tag is not allowed. It will be generated during writing.");
    }
    if constexpr (std::remove_cvref_t<decltype(src_tags)>::template has_tag<TagCode::TileOffsets>()) {
        const auto& tag_opt = src_tags.template get<TagCode::TileOffsets>();
        if (optional::is_value_present(tag_opt)) [[unlikely]] {
            return Err(Error::Code::InvalidTag,
                        "User-provided TileOffsets tag is not allowed. It will be generated during writing.");
        }
        static_assert(optional::is_optional_v<std::remove_cvref_t<decltype(tag_opt)>>, "Only empty TileOffsets tag is allowed. User-provided TileOffsets tag is not allowed. It will be generated during writing.");
    }
    if constexpr (std::remove_cvref_t<decltype(src_tags)>::template has_tag<TagCode::TileByteCounts>()) {
        const auto& tag_opt = src_tags.template get<TagCode::TileByteCounts>();
        if (optional::is_value_present(tag_opt)) [[unlikely]] {
            return Err(Error::Code::InvalidTag,
                        "User-provided TileByteCounts tag is not allowed. It will be generated during writing.");
        }
        static_assert(optional::is_optional_v<std::remove_cvref_t<decltype(tag_opt)>>, "Only empty TileByteCounts tag is allowed. User-provided TileByteCounts tag is not allowed. It will be generated during writing.");
    }
    if (is_tiled) {
        // RowsPerStrip
        if constexpr (std::remove_cvref_t<decltype(src_tags)>::template has_tag<TagCode::RowsPerStrip>()) {
            const auto& tag_opt = src_tags.template get<TagCode::RowsPerStrip>();
            if (optional::is_value_present(tag_opt)) [[unlikely]] {
                return Err(Error::Code::InvalidTag,
                            "User-provided RowsPerStrip tag is not allowed for tiled images.");
            }
        }
        // TileWidth
        res = validate_invalid_override<TagCode::TileWidth>(src_tags, tile_width);
        if (res.is_error()) [[unlikely]] {
            return res;
        }
        // TileLength
        res = validate_invalid_override<TagCode::TileLength>(src_tags, tile_height);
        if (res.is_error()) [[unlikely]] {
            return res;
        }
    } else {
        // RowsPerStrip
        res = validate_invalid_override<TagCode::RowsPerStrip>(src_tags, tile_height);
        if (res.is_error()) [[unlikely]] {
            return res;
        }
        // TileWidth
        if constexpr (std::remove_cvref_t<decltype(src_tags)>::template has_tag<TagCode::TileWidth>()) {
            const auto& tag_opt = src_tags.template get<TagCode::TileWidth>();
            if (optional::is_value_present(tag_opt)) [[unlikely]] {
                return Err(Error::Code::InvalidTag,
                            "User-provided TileWidth tag is not allowed for stripped images.");
            }
        }
        // TileLength
        if constexpr (std::remove_cvref_t<decltype(src_tags)>::template has_tag<TagCode::TileLength>()) {
            const auto& tag_opt = src_tags.template get<TagCode::TileLength>();
            if (optional::is_value_present(tag_opt)) [[unlikely]] {
                return Err(Error::Code::InvalidTag,
                            "User-provided TileLength tag is not allowed for stripped images.");
            }
        }
    }
    // 11. ImageDepth
    res = validate_invalid_override<TagCode::ImageDepth>(src_tags, image_depth);
    if (res.is_error()) [[unlikely]] {
        return res;
    }
    // 12. TileDepth
    res = validate_invalid_override<TagCode::TileDepth>(src_tags, tile_depth);
    if (res.is_error()) [[unlikely]] {
        return res;
    }
    return Ok();
}

template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
template <typename... UserTags>
inline void TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::fill_mandatory_tags(
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
    uint32_t  num_strips_or_tiles
) noexcept {
    // 1. ImageWidth
    fill_tag_if_missing_in_src<TagCode::ImageWidth>(dst_tags, src_tags, image_width);
    // 2. ImageLength
    fill_tag_if_missing_in_src<TagCode::ImageLength>(dst_tags, src_tags, image_height);
    // 3. BitsPerSample
    {
        bool bps_in_src;
        if constexpr (!std::remove_cvref_t<decltype(src_tags)>::template has_tag<TagCode::BitsPerSample>()) {
            bps_in_src = false;
        } else {
            bps_in_src = optional::is_value_present(src_tags.template get<TagCode::BitsPerSample>());
        }
        if (!bps_in_src) {
            // Initialize tag
            if (!dst_tags.template get<TagCode::BitsPerSample>().has_value()) {
                dst_tags.template get<TagCode::BitsPerSample>().emplace();
            }
            auto &bps_vec = dst_tags.template get<TagCode::BitsPerSample>().value();
            //std::cerr << "Debug: Filling BitsPerSample tag with " << samples_per_pixel << " samples." << std::endl;
            bps_vec.resize(samples_per_pixel);
            // Fill
            for (std::size_t i = 0; i < samples_per_pixel; ++i) {
                bps_vec[i] = sizeof(PixelType) * 8;
                //std::cerr << "Debug: BitsPerSample[" << i << "] = " << bps_vec[i] << std::endl;
            }
        } else {
            // Reset tag to enable memory reuse
            dst_tags.template get<TagCode::BitsPerSample>() = std::nullopt;
        }
    }
    // 4. Compression
    fill_tag_if_missing_in_src<TagCode::Compression>(dst_tags, src_tags, compression);
    // 5. PhotometricInterpretation
    {
        PhotometricInterpretation pi = PhotometricInterpretation::MinIsBlack;
        if (samples_per_pixel == 3 || samples_per_pixel == 4) pi = PhotometricInterpretation::RGB;
        fill_tag_if_missing_in_src<TagCode::PhotometricInterpretation>(dst_tags, src_tags, pi);
    }
    // 6. SamplesPerPixel
    fill_tag_if_missing_in_src<TagCode::SamplesPerPixel>(dst_tags, src_tags, samples_per_pixel);
    // 7. PlanarConfiguration
    fill_tag_if_missing_in_src<TagCode::PlanarConfiguration>(dst_tags, src_tags, static_cast<uint16_t>(planar_config));
    // 8. Predictor
    if (predictor != Predictor::None) {
        fill_tag_if_missing_in_src<TagCode::Predictor>(dst_tags, src_tags, predictor);
    } else {
        // Reset tag to enable memory reuse
        dst_tags.template get<TagCode::Predictor>() = std::nullopt;
    }
    // 9. SampleFormat
    {
        SampleFormat sf = SampleFormat::UnsignedInt;
        if constexpr (std::is_floating_point_v<PixelType>) sf = SampleFormat::IEEEFloat;
        else if constexpr (std::is_signed_v<PixelType>) sf = SampleFormat::SignedInt;
        fill_tag_if_missing_in_src<TagCode::SampleFormat>(dst_tags, src_tags, sf);
    }
    // 10. Strips/Tiles
    if (!is_tiled) {
        fill_tag_if_missing_in_src<TagCode::RowsPerStrip>(dst_tags, src_tags, tile_height);
        dst_tags.template get<TagCode::TileWidth>() = std::nullopt;
        dst_tags.template get<TagCode::TileLength>() = std::nullopt;
        dst_tags.template get<TagCode::TileOffsets>() = std::nullopt;
        dst_tags.template get<TagCode::TileByteCounts>() = std::nullopt;
        // StripOffsets & StripByteCounts
        // initialize arrays if they are nullopt
        if (!dst_tags.template get<TagCode::StripOffsets>().has_value()) {
            dst_tags.template get<TagCode::StripOffsets>().emplace();
        }
        if (!dst_tags.template get<TagCode::StripByteCounts>().has_value()) {
            dst_tags.template get<TagCode::StripByteCounts>().emplace();
        }
        // Fill
        auto &dst_strip_offsets = dst_tags.template get<TagCode::StripOffsets>().value();
        auto &dst_strip_byte_counts = dst_tags.template get<TagCode::StripByteCounts>().value();
        dst_strip_offsets.resize(num_strips_or_tiles);
        dst_strip_byte_counts.resize(num_strips_or_tiles);
        // Initialize with zeros (code for missing strip/tile)
        for (std::size_t i = 0; i < num_strips_or_tiles; ++i) {
            dst_strip_offsets[i] = 0;
            dst_strip_byte_counts[i] = 0;
        }
    } else {
        dst_tags.template get<TagCode::RowsPerStrip>() = std::nullopt;
        fill_tag_if_missing_in_src<TagCode::TileWidth>(dst_tags, src_tags, tile_width);
        fill_tag_if_missing_in_src<TagCode::TileLength>(dst_tags, src_tags, tile_height);
        dst_tags.template get<TagCode::StripOffsets>() = std::nullopt;
        dst_tags.template get<TagCode::StripByteCounts>() = std::nullopt;
        // TileOffsets & TileByteCounts
        // initialize arrays if they are nullopt
        if (!dst_tags.template get<TagCode::TileOffsets>().has_value()) {
            dst_tags.template get<TagCode::TileOffsets>().emplace();
        }
        if (!dst_tags.template get<TagCode::TileByteCounts>().has_value()) {
            dst_tags.template get<TagCode::TileByteCounts>().emplace();
        }
        // Fill
        auto &dst_tile_offsets = dst_tags.template get<TagCode::TileOffsets>().value();
        auto &dst_tile_byte_counts = dst_tags.template get<TagCode::TileByteCounts>().value();
        dst_tile_offsets.resize(num_strips_or_tiles);
        dst_tile_byte_counts.resize(num_strips_or_tiles);
        // Initialize with zeros (code for missing strip/tile)
        for (std::size_t i = 0; i < num_strips_or_tiles; ++i) {
            dst_tile_offsets[i] = 0;
            dst_tile_byte_counts[i] = 0;
        }
    }
    // 11. ImageDepth
    if (tile_depth > 1 || image_depth > 1) {
        assert (!is_tiled && "3D images must be tiled");
        fill_tag_if_missing_in_src<TagCode::ImageDepth>(dst_tags, src_tags, image_depth);
        fill_tag_if_missing_in_src<TagCode::TileDepth>(dst_tags, src_tags, tile_depth);
    } else {
        // Reset tag to enable memory reuse
        dst_tags.template get<TagCode::ImageDepth>() = std::nullopt;
        dst_tags.template get<TagCode::TileDepth>() = std::nullopt;
    }
}

template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
inline void TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::fill_tile_arrays(
    WritingTagsType& tags,
    const WrittenImageInfo& image_info) noexcept {
    auto& tile_offsets = tags.template get<TagCode::TileOffsets>().value();
    auto& tile_byte_counts = tags.template get<TagCode::TileByteCounts>().value();
    std::size_t num_chunks = std::min(
        tile_offsets.size(),
        std::min(image_info.tile_offsets.size(), image_info.tile_byte_counts.size()));
    // we assume the arrays are already resized appropriately
    if constexpr (TiffFormat == TiffFormatType::Classic) {
        for (std::size_t i = 0; i < num_chunks; ++i) {
            tile_offsets[i] = static_cast<uint32_t>(image_info.tile_offsets[i]);
            tile_byte_counts[i] = static_cast<uint32_t>(image_info.tile_byte_counts[i]);
        }
    } else {
        static_assert(sizeof(std::size_t) == sizeof(uint64_t), "Unexpected size_t size");
        std::memcpy(
            tile_offsets.data(),
            image_info.tile_offsets.data(),
            num_chunks * sizeof(uint64_t));
        std::memcpy(
            tile_byte_counts.data(),
            image_info.tile_byte_counts.data(),
            num_chunks * sizeof(uint64_t));
    }
}

template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
inline void TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::fill_strip_arrays(
    WritingTagsType& tags,
    const WrittenImageInfo& image_info) noexcept {
    auto& strip_offsets = tags.template get<TagCode::StripOffsets>().value();
    auto& strip_byte_counts = tags.template get<TagCode::StripByteCounts>().value();
    std::size_t num_chunks = std::min(
        strip_offsets.size(),
        std::min(image_info.tile_offsets.size(), image_info.tile_byte_counts.size()));
    // we assume the arrays are already resized appropriately
    if constexpr (TiffFormat == TiffFormatType::Classic) {
        for (std::size_t i = 0; i < num_chunks; ++i) {
            strip_offsets[i] = static_cast<uint32_t>(image_info.tile_offsets[i]);
            strip_byte_counts[i] = static_cast<uint32_t>(image_info.tile_byte_counts[i]);
        }
    } else {
        static_assert(sizeof(std::size_t) == sizeof(uint64_t), "Unexpected size_t size");
        std::memcpy(
            strip_offsets.data(),
            image_info.tile_offsets.data(),
            num_chunks * sizeof(uint64_t));
        std::memcpy(
            strip_byte_counts.data(),
            image_info.tile_byte_counts.data(),
            num_chunks * sizeof(uint64_t));
    }
}

template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
template <ImageLayoutSpec InputSpec, typename Writer, typename... TagArgs>
    requires RawWriter<Writer>
inline Result<void> TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::write_image_impl(
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
    const ExtractedTags<TagArgs...>& additional_tags) noexcept {
    
    constexpr std::size_t header_size = sizeof(HeaderType);
    
    std::size_t current_file_pos = header_size;
    ifd::IFDOffset first_ifd_offset;

    // Calculate number of chunks
    uint32_t tiles_across = (image_width + tile_width - 1) / tile_width;
    uint32_t tiles_down = (image_height + tile_height - 1) / tile_height;
    uint32_t tiles_deep = (image_depth + tile_depth - 1) / tile_depth;
    uint32_t num_chunks = tiles_across * tiles_down * tiles_deep;
    if (planar_config == PlanarConfiguration::Planar) {
        num_chunks *= samples_per_pixel;
    }

    // Validate user tags
    auto validation_result = validate_user_tags(
        additional_tags, image_width, image_height, image_depth,
        tile_width, tile_height, tile_depth,
        is_tiled, samples_per_pixel, planar_config, compression, predictor);
    if (validation_result.is_error()) [[unlikely]] {
        return validation_result;
    }

    // Create mandatory tags structure with zero-filled offsets/byte counts
    WritingTagsType mandatory_tags;
    fill_mandatory_tags(
        mandatory_tags, additional_tags,
        image_width, image_height, image_depth,
        tile_width, tile_height, tile_depth,
        is_tiled, samples_per_pixel, planar_config, compression, predictor,
        num_chunks);

    if (placement_strategy_.write_data_before_ifd) {
        // Strategy: write image data first, then IFD
        // Image data starts right after header
        auto write_result = image_writer_.template write_image<InputSpec>(
            writer,
            image_data,
            image_width,
            image_height,
            image_depth,
            tile_width,
            tile_height,
            tile_depth,
            samples_per_pixel,
            planar_config,
            compression,
            predictor,
            current_file_pos
        );
        
        if (write_result.is_error()) [[unlikely]] {
            return write_result.error();
        }
        
        WrittenImageInfo &image_info = write_result.value();
        current_file_pos += image_info.total_data_size;
        
        // Update offsets and byte counts in mandatory tags
        if (is_tiled) {
            fill_tile_arrays(mandatory_tags, image_info);
        } else {
            fill_strip_arrays(mandatory_tags, image_info);
        }
        
        // Build IFD with actual tile offset/bytecount information
        ifd_builder_.clear();
        auto add_result = ifd_builder_.add_tags(mandatory_tags);
        if (add_result.is_error()) [[unlikely]] {
            return add_result;
        }

        add_result = ifd_builder_.add_tags(additional_tags);
        if (add_result.is_error()) [[unlikely]] {
            return add_result;
        }
        
        // Write IFD
        auto ifd_offset_result = ifd_builder_.write_to_file(writer, current_file_pos);
        if (ifd_offset_result.is_error()) [[unlikely]] {
            return ifd_offset_result.error();
        }
        
        first_ifd_offset = ifd_offset_result.value();
        
    } else {
        // Strategy: reserve space for IFD, write image data, then write IFD

        std::size_t num_real_tags = mandatory_tags.num_defined_tags() + additional_tags.num_defined_tags();
        std::size_t estimated_ifd_size = ifd::IFD<TiffFormat, TargetEndian>::size_in_bytes(num_real_tags)
            + mandatory_tags.extra_byte_size() + additional_tags.extra_byte_size();
        
        // Reserve space for IFD
        std::size_t ifd_start = current_file_pos;
        current_file_pos += estimated_ifd_size;
        
        // Write image data
        auto write_result = image_writer_.template write_image<InputSpec>(
            writer,
            image_data,
            image_width,
            image_height,
            image_depth,
            tile_width,
            tile_height,
            tile_depth,
            samples_per_pixel,
            planar_config,
            compression,
            predictor,
            current_file_pos
        );
        
        if (write_result.is_error()) [[unlikely]] {
            return write_result.error();
        }
        
        WrittenImageInfo &image_info = write_result.value();
        
        // Update offsets and byte counts in mandatory tags
        if (is_tiled) {
            fill_tile_arrays(mandatory_tags, image_info);
        } else {
            fill_strip_arrays(mandatory_tags, image_info);
        }
        
        // Now write IFD with actual tile information
        ifd_builder_.clear();
        auto add_result = ifd_builder_.add_tags(mandatory_tags);
        if (add_result.is_error()) [[unlikely]] {
            return add_result;
        }

        add_result = ifd_builder_.add_tags(additional_tags);
        if (add_result.is_error()) [[unlikely]] {
            return add_result;
        }

        assert(ifd_builder_.calculate_total_size() == estimated_ifd_size &&
                "IFD size greater than estimated size reserved");
        // Write IFD at the reserved position
        auto ifd_offset_result = ifd_builder_.write_to_file(writer, ifd_start);
        if (ifd_offset_result.is_error()) [[unlikely]] {
            return ifd_offset_result.error();
        }

        first_ifd_offset = ifd_offset_result.value();
    }
    
    // Write header with first IFD offset
    auto header_result = write_header(writer, first_ifd_offset);
    if (header_result.is_error()) [[unlikely]] {
        return header_result;
    }
    
    return writer.flush();
}
    

/// Write a complete single-image TIFF file
/// This is the main high-level API for writing a TIFF image
template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
template <ImageLayoutSpec InputSpec, typename Writer, typename... TagArgs>
    requires RawWriter<Writer>
inline Result<void> TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::write_single_image(
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
    const ExtractedTags<TagArgs...>& additional_tags) noexcept {
    return write_image_impl<InputSpec>(
        writer,
        image_data,
        image_width,
        image_height,
        1, // image_depth
        tile_width,
        tile_height,
        1, // tile_depth
        true, // is_tiled
        samples_per_pixel,
        planar_config,
        compression,
        predictor,
        additional_tags
    );
}

/// Write a complete single-image TIFF file
/// This is the main high-level API for writing a TIFF image
template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
template <ImageLayoutSpec InputSpec, typename Writer, typename... TagArgs>
    requires RawWriter<Writer>
inline Result<void> TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::write_single_image(
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
    const ExtractedTags<TagArgs...>& additional_tags) noexcept {
    return write_image_impl<InputSpec>(
        writer,
        image_data,
        image_width,
        image_height,
        image_depth,
        tile_width,
        tile_height,
        tile_depth,
        true, // is_tiled
        samples_per_pixel,
        planar_config,
        compression,
        predictor,
        additional_tags
    );
}

/// Write a stripped image (convenience wrapper)
template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
template <ImageLayoutSpec InputSpec, typename Writer, typename... TagArgs>
    requires RawWriter<Writer>
inline Result<void> TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::write_stripped_image(
    Writer& writer,
    std::span<const PixelType> image_data,
    uint32_t image_width,
    uint32_t image_height,
    uint32_t rows_per_strip,
    uint16_t samples_per_pixel,
    PlanarConfiguration planar_config,
    CompressionScheme compression,
    Predictor predictor,
    const ExtractedTags<TagArgs...>& additional_tags) noexcept {

    if (image_height % rows_per_strip != 0) [[unlikely]] {
        return Err(Error::Code::UnsupportedFeature, "Current limitation: rows per strip must evenly divide image height");
    }

    return write_image_impl<InputSpec>(
        writer,
        image_data,
        image_width,
        image_height,
        1, // image_depth
        image_width,  // tile_width = full width
        rows_per_strip, // TODO: handle last strip properly
        1, // tile_depth
        false, // is_tiled
        samples_per_pixel,
        planar_config,
        compression,
        predictor,
        additional_tags
    );
}
    
/// Clear writer state for reuse
template <typename PixelType, typename CompSpec, typename WriteConfig_, TiffFormatType TiffFormat, std::endian TargetEndian>
    requires ValidCompressorSpec<CompSpec> && predictor::DeltaDecodable<PixelType>
inline void TiffWriter<PixelType, CompSpec, WriteConfig_, TiffFormat, TargetEndian>::clear() noexcept {
    image_writer_.clear();
    ifd_builder_.clear();
}

} // namespace tiffconcept
