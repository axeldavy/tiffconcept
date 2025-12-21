#pragma once

// This file contains the implementation of IFD operations.
// Do not include this file directly - it is included by ifd.hpp

#include <cassert>
#include <cstring>
#include <span>
#include <vector>
#include "../parsing.hpp"
#include "../reader_base.hpp"
#include "../types/result.hpp"
#include "../types/tiff_spec.hpp"

#ifndef TIFFCONCEPT_IFD_HEADER
#include "../ifd.hpp" // for linters
#endif

namespace tiffconcept {

namespace ifd {

#if 0
// ============================================================================
// IFDDescription Member Function Implementations
// ============================================================================

/**
 * @brief Default constructor - creates an empty IFD description
 */
template <TiffFormatType TiffFormat>
constexpr IFDDescription<TiffFormat>::IFDDescription() noexcept 
        : offset(), num_entries(0) {}
    
/**
 * @brief Construct an IFD description with offset and entry count
 * @param offset_ File offset where the IFD is located
 * @param num_entries_ Number of tag entries in the IFD
 */
template <TiffFormatType TiffFormat>
constexpr IFDDescription<TiffFormat>::IFDDescription(IFDOffset offset_, std::size_t num_entries_) noexcept
    : offset(offset_), num_entries(num_entries_) {}

#endif
// ============================================================================
// IFD Member Function Implementations
// ============================================================================

/**
 * @brief Default constructor - creates an empty IFD
 */
template <TiffFormatType TiffFormat, std::endian SourceEndian>
IFD<TiffFormat, SourceEndian>::IFD() noexcept : description(), tags(), next_ifd_offset() {}

/**
 * @brief Constructor with IFD description
 * @param desc IFD description (offset and entry count)
 */
template <TiffFormatType TiffFormat, std::endian SourceEndian>
IFD<TiffFormat, SourceEndian>::IFD(const IFDDescription<TiffFormat>& desc) noexcept 
    : description(desc), tags(), next_ifd_offset() {}

/**
 * @brief Constructor with all components
 * @param desc IFD description (offset and entry count)
 * @param tags_ Tag entries (moved)
 * @param next_offset Offset to next IFD in chain
 */
template <TiffFormatType TiffFormat, std::endian SourceEndian>
IFD<TiffFormat, SourceEndian>::IFD(
    const IFDDescription<TiffFormat>& desc, 
    std::vector<parsing::TagType<TiffFormat, SourceEndian>>&& tags_,
    IFDOffset next_offset) noexcept 
    : description(desc), tags(std::move(tags_)), next_ifd_offset(next_offset)
{}

/**
 * @brief Get number of tags in this IFD
 * @return Number of tag entries
 */
template <TiffFormatType TiffFormat, std::endian SourceEndian>
inline std::size_t IFD<TiffFormat, SourceEndian>::num_tags() const noexcept {
    return tags.size();
}

/**
 * @brief Calculate size in bytes of an IFD when written to file
 * 
 * This is the total size including:
 * - IFD header (entry count)
 * - All tag entries
 * - Next IFD offset
 * 
 * @param num_tags Number of tags in the IFD
 * @return Total size in bytes
 */
template <TiffFormatType TiffFormat, std::endian SourceEndian>
inline constexpr std::size_t IFD<TiffFormat, SourceEndian>::size_in_bytes(std::size_t num_tags) noexcept {
    using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
    using TagType = typename IFDDescription<TiffFormat>::template TagType<SourceEndian>;
    using OffsetType = typename IFDDescription<TiffFormat>::OffsetType;
    
    return sizeof(IFDHeaderType) + 
            num_tags * sizeof(TagType) + 
            sizeof(OffsetType);
}

/**
 * @brief Get size in bytes of this IFD when written to file
 * @return Total size in bytes
 */
template <TiffFormatType TiffFormat, std::endian SourceEndian>
inline std::size_t IFD<TiffFormat, SourceEndian>::size_in_bytes() const noexcept {
    return size_in_bytes(num_tags());
}

/**
 * @brief Write IFD to a byte buffer
 * 
 * Writes the complete IFD structure to a buffer:
 * 1. IFD header (entry count)
 * 2. All tag entries
 * 3. Next IFD offset
 * 
 * The data is written in the SourceEndian byte order.
 * 
 * @param buffer Output buffer (must be at least size_in_bytes() bytes)
 * @return Ok() on success, or Error on failure
 * 
 * @throws None (noexcept)
 * @retval Error::Code::OutOfBounds Buffer is too small for the IFD data
 * 
 * @note Tags must already be in the correct byte order (SourceEndian)
 * @note The buffer must be pre-allocated with sufficient size
 */
template <TiffFormatType TiffFormat, std::endian SourceEndian>
inline Result<void> IFD<TiffFormat, SourceEndian>::write(std::span<std::byte> buffer) const noexcept {
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

/**
 * @brief Write IFD to a new vector
 * 
 * Convenience method that allocates a vector of the exact size needed
 * and writes the IFD to it.
 * 
 * @return Vector containing the serialized IFD
 * 
 * @note This always succeeds since the buffer is correctly sized
 */
template <TiffFormatType TiffFormat, std::endian SourceEndian>
inline std::vector<std::byte> IFD<TiffFormat, SourceEndian>::write() const noexcept {
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
    
    auto header_result = parsing::detail::read_struct_no_endianness_conversion<Reader, HeaderType>(reader, 0);

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
    
    auto ifd_header_result = parsing::detail::read_struct_no_endianness_conversion<Reader, IFDHeaderType>(reader, offset.value);

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
    
    auto result = parsing::detail::read_struct_no_endianness_conversion<Reader, OffsetType>(reader, next_offset_pos);
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
    auto tags_result = parsing::detail::read_array<Reader, TagType, SourceEndian, SourceEndian>(
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
    auto header_result = parsing::detail::read_struct_no_endianness_conversion<Reader, IFDHeaderType>(reader, offset.value);
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