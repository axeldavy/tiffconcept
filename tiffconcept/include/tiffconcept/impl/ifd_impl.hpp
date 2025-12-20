#pragma once

// This file contains the implementation of IFD operations.
// Do not include this file directly - it is included by ifd.hpp

#include <cassert>
#include <cstring>
#include <span>
#include <vector>
#include "../parsing.hpp"
#include "../reader_base.hpp"
#include "../result.hpp"
#include "../types.hpp"

#ifndef TIFFCONCEPT_IFD_HEADER
#include "../ifd.hpp" // for linters
#endif

namespace tiffconcept {

namespace ifd {

// ============================================================================
// IFD Member Function Implementations
// ============================================================================

template <TiffFormatType TiffFormat, std::endian SourceEndian>
Result<void> IFD<TiffFormat, SourceEndian>::write(std::span<std::byte> buffer) const noexcept {
    using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
    using TagType = typename IFDDescription<TiffFormat>::template TagType<SourceEndian>;
    using OffsetType = typename IFDDescription<TiffFormat>::OffsetType;
    
    const std::size_t required_size = size_in_bytes();
    if (buffer.size() < required_size) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, 
                  "Buffer too small for IFD: need " + std::to_string(required_size) + 
                  " bytes, got " + std::to_string(buffer.size()));
    }
    
    std::size_t pos = 0;
    
    // Write IFD header (num entries)
    IFDHeaderType header;
    if constexpr (TiffFormat == TiffFormatType::Classic) {
        header.num_entries = static_cast<uint16_t>(num_tags());
        if constexpr (SourceEndian != std::endian::native) {
            header.num_entries = byteswap(header.num_entries);
        }
    } else { // BigTIFF
        header.num_entries = static_cast<uint64_t>(num_tags());
        if constexpr (SourceEndian != std::endian::native) {
            header.num_entries = byteswap(header.num_entries);
        }
    }
    std::memcpy(buffer.data() + pos, &header, sizeof(IFDHeaderType));
    pos += sizeof(IFDHeaderType);
    
    // Write tags
    if (!tags.empty()) {
        std::memcpy(buffer.data() + pos, tags.data(), num_tags() * sizeof(TagType));
        pos += num_tags() * sizeof(TagType);
    }
    
    // Write next IFD offset
    OffsetType next_offset = static_cast<OffsetType>(next_ifd_offset.value);
    if constexpr (SourceEndian != std::endian::native) {
        next_offset = byteswap(next_offset);
    }
    std::memcpy(buffer.data() + pos, &next_offset, sizeof(OffsetType));
    
    return Ok();
}

template <TiffFormatType TiffFormat, std::endian SourceEndian>
std::vector<std::byte> IFD<TiffFormat, SourceEndian>::write() const noexcept {
    std::vector<std::byte> buffer(size_in_bytes());
    auto result = write(std::span<std::byte>(buffer));
    // This should never fail since we allocated the exact size
    assert(result.is_ok() && "write() to correctly sized buffer failed");
    return buffer;
}

// ============================================================================
// Free Function Implementations
// ============================================================================

template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
Result<IFDOffset> get_first_ifd_offset(const Reader& reader) noexcept {
    using HeaderType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                          TiffHeader<SourceEndian>, 
                                          TiffBigHeader<SourceEndian>>;
    
    auto header_result = parsing::read_struct_no_endianness_conversion<Reader, HeaderType>(reader, 0);

    if (header_result.is_error()) [[unlikely]] {
        return header_result.error();
    }

    const auto& header = header_result.value();
    if (!header.template is_valid<SourceEndian>()) [[unlikely]] {
        return Err(Error::Code::InvalidHeader, "TIFF header is not valid for the specified format");
    }

    std::size_t first_ifd_offset = header.template get_first_ifd_offset<std::endian::native>();
    return Ok(IFDOffset(first_ifd_offset));
}

template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
Result<IFDDescription<TiffFormat>> read_ifd_header(
    const Reader& reader, 
    IFDOffset offset) noexcept 
{
    using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
    
    auto ifd_header_result = parsing::read_struct_no_endianness_conversion<Reader, IFDHeaderType>(reader, offset.value);

    if (ifd_header_result.is_error()) [[unlikely]] {
        return ifd_header_result.error();
    }

    const auto& ifd_header = ifd_header_result.value();
    std::size_t num_entries = static_cast<std::size_t>(ifd_header.template get_num_entries<std::endian::native>());

    return Ok(IFDDescription<TiffFormat>(offset, num_entries));
}

template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
Result<IFDOffset> read_next_ifd_offset(
    const Reader& reader, 
    const IFDDescription<TiffFormat>& ifd_desc) noexcept 
{
    using OffsetType = typename IFDDescription<TiffFormat>::OffsetType;
    using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
    using TagType = typename IFDDescription<TiffFormat>::template TagType<SourceEndian>;
    
    // Next IFD offset is stored after all tag entries
    std::size_t next_offset_pos = ifd_desc.offset.value + sizeof(IFDHeaderType) + 
                                  ifd_desc.num_entries * sizeof(TagType);
    
    auto result = parsing::read_struct_no_endianness_conversion<Reader, OffsetType>(reader, next_offset_pos);
    if (result.is_error()) [[unlikely]] {
        return result.error();
    }
    
    OffsetType next_offset = result.value();
    if constexpr (SourceEndian != std::endian::native) {
        convert_endianness<OffsetType, SourceEndian, std::endian::native>(next_offset);
    }
    
    static_assert(sizeof(OffsetType) <= sizeof(std::size_t), "OffsetType must fit into std::size_t");
    return Ok(IFDOffset(static_cast<std::size_t>(next_offset)));
}

template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
Result<IFDOffset> read_ifd_tags(
    const Reader& reader,
    const IFDDescription<TiffFormat>& ifd_desc,
    std::vector<parsing::TagType<TiffFormat, SourceEndian>>& out_tags) noexcept 
{
    using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
    using TagType = typename IFDDescription<TiffFormat>::template TagType<SourceEndian>;
    
    if (ifd_desc.num_entries == 0) {
        out_tags.clear();
        return read_next_ifd_offset<Reader, TiffFormat, SourceEndian>(reader, ifd_desc);
    }
    
    // Read all tag entries
    std::size_t tags_offset = ifd_desc.offset.value + sizeof(IFDHeaderType);
    auto tags_result = parsing::read_array<Reader, TagType, SourceEndian, SourceEndian>(
        reader, tags_offset, ifd_desc.num_entries);
    
    if (tags_result.is_error()) [[unlikely]] {
        return tags_result.error();
    }
    
    out_tags = std::move(tags_result.value());
    
    // Read next IFD offset
    return read_next_ifd_offset<Reader, TiffFormat, SourceEndian>(reader, ifd_desc);
}

template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
Result<void> read_ifd_into(
    const Reader& reader,
    IFDOffset offset,
    IFD<TiffFormat, SourceEndian>& ifd) noexcept 
{
    using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
    using TagType = typename IFDDescription<TiffFormat>::template TagType<SourceEndian>;
    using OffsetType = typename IFDDescription<TiffFormat>::OffsetType;
    
    // First, read just the header to know how many entries
    auto header_result = parsing::read_struct_no_endianness_conversion<Reader, IFDHeaderType>(reader, offset.value);
    if (header_result.is_error()) [[unlikely]] {
        return header_result.error();
    }
    
    const auto& ifd_header = header_result.value();
    std::size_t num_entries = static_cast<std::size_t>(ifd_header.template get_num_entries<std::endian::native>());
    
    ifd.description = IFDDescription<TiffFormat>(offset, num_entries);
    
    if (num_entries == 0) {
        ifd.tags.clear();
        
        // Read next IFD offset
        auto next_offset_result = read_next_ifd_offset<Reader, TiffFormat, SourceEndian>(reader, ifd.description);
        if (next_offset_result.is_error()) [[unlikely]] {
            return next_offset_result.error();
        }
        
        ifd.next_ifd_offset = next_offset_result.value();
        return Ok();
    }
    
    // Calculate total size to read: tags + next offset
    std::size_t total_read_size = num_entries * sizeof(TagType) + sizeof(OffsetType);
    std::size_t tags_offset = offset.value + sizeof(IFDHeaderType);
    
    auto data_result = reader.read(tags_offset, total_read_size);
    if (data_result.is_error()) [[unlikely]] {
        return Err(data_result.error().code, "Failed to read IFD tags and next offset: " + data_result.error().message);
    }
    
    const auto& view = data_result.value();
    if (view.size() < total_read_size) [[unlikely]] {
        return Err(Error::Code::UnexpectedEndOfFile, "Incomplete IFD read");
    }
    
    // Parse tags
    ifd.tags.resize(num_entries);
    std::memcpy(ifd.tags.data(), view.data().data(), num_entries * sizeof(TagType));
    
    // Parse next offset
    OffsetType next_offset;
    std::memcpy(&next_offset, view.data().data() + num_entries * sizeof(TagType), sizeof(OffsetType));
    
    if constexpr (SourceEndian != std::endian::native) {
        convert_endianness<OffsetType, SourceEndian, std::endian::native>(next_offset);
    }
    
    ifd.next_ifd_offset = IFDOffset(static_cast<std::size_t>(next_offset));
    return Ok();
}

template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
Result<IFD<TiffFormat, SourceEndian>> read_ifd(
    const Reader& reader,
    IFDOffset offset) noexcept 
{
    IFD<TiffFormat, SourceEndian> ifd;
    auto result = read_ifd_into<Reader, TiffFormat, SourceEndian>(reader, offset, ifd);
    
    if (result.is_error()) [[unlikely]] {
        return result.error();
    }
    
    return Ok(std::move(ifd));
}

} // namespace ifd

} // namespace tiffconcept