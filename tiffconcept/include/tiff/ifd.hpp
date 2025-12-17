#pragma once

#include <array>
#include <bit>
#include <cstring>
#include <span>
#include <vector>
#include "parsing.hpp"
#include "reader_base.hpp"
#include "result.hpp"
#include "tag_spec.hpp"
#include "types.hpp"

namespace tiffconcept {

namespace ifd {

/// Strongly-typed IFD offset in the file
struct IFDOffset {
    std::size_t value;
    
    constexpr IFDOffset() noexcept : value(0) {}
    constexpr explicit IFDOffset(std::size_t offset) noexcept : value(offset) {}
    
    [[nodiscard]] constexpr bool is_null() const noexcept { return value == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const IFDOffset& other) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const IFDOffset& other) const noexcept = default;
};

/// Represents an IFD with its parsed header information
template <TiffFormatType TiffFormat = TiffFormatType::Classic>
struct IFDDescription {
    IFDOffset offset;                // File offset of this IFD
    std::size_t num_entries;         // Number of tag entries in this IFD
    
    /// Default constructor
    constexpr IFDDescription() noexcept 
        : offset(), num_entries(0) {}
    
    /// Constructor with offset and num_entries
    constexpr IFDDescription(IFDOffset offset_, std::size_t num_entries_) noexcept
        : offset(offset_), num_entries(num_entries_) {}
    
    /// Get offset type for this TIFF format
    using OffsetType = std::conditional_t<TiffFormat == TiffFormatType::Classic, uint32_t, uint64_t>;
    
    /// Get IFD header type for this TIFF format
    template <std::endian SourceEndian>
    using HeaderType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                          IFDHeader<SourceEndian>, 
                                          IFDBigHeader<SourceEndian>>;
    
    /// Get tag type for this TIFF format
    template <std::endian SourceEndian>
    using TagType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                       TiffTag<SourceEndian>, 
                                       TiffBigTag<SourceEndian>>;
};

/// Complete IFD structure with description, tags, and next IFD offset
template <TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
struct IFD {
    IFDDescription<TiffFormat> description;
    std::vector<parsing::TagType<TiffFormat, SourceEndian>> tags;
    IFDOffset next_ifd_offset;
    
    /// Default constructor
    IFD() noexcept : description(), tags(), next_ifd_offset() {}
    
    /// Constructor with description
    explicit IFD(const IFDDescription<TiffFormat>& desc) noexcept 
        : description(desc), tags(), next_ifd_offset() {}
    
    /// Constructor with all components
    IFD(const IFDDescription<TiffFormat>& desc, 
        std::vector<parsing::TagType<TiffFormat, SourceEndian>>&& tags_,
        IFDOffset next_offset) noexcept 
        : description(desc), tags(std::move(tags_)), next_ifd_offset(next_offset) {}

    // Get number of tags
    [[nodiscard]] std::size_t num_tags() const noexcept {
        return tags.size();
    }

    /// Get size in bytes of the IFD when written to file (header + tags + next offset)
    [[nodiscard]] constexpr static std::size_t size_in_bytes(std::size_t num_tags) noexcept {
        using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
        using TagType = typename IFDDescription<TiffFormat>::template TagType<SourceEndian>;
        using OffsetType = typename IFDDescription<TiffFormat>::OffsetType;
        
        return sizeof(IFDHeaderType) + 
               num_tags * sizeof(TagType) + 
               sizeof(OffsetType);
    }

    /// Get size in bytes of this IFD when written to file
    [[nodiscard]] std::size_t size_in_bytes() const noexcept {
        return size_in_bytes(num_tags());
    }

    /// Write IFD to a span (header + tags + next offset)
    /// The span must be at least size_in_bytes() bytes
    /// Returns Error if the span is too small
    [[nodiscard]] Result<void> write(std::span<std::byte> buffer) const noexcept {
        using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
        using TagType = typename IFDDescription<TiffFormat>::template TagType<SourceEndian>;
        using OffsetType = typename IFDDescription<TiffFormat>::OffsetType;
        
        const std::size_t required_size = size_in_bytes();
        if (buffer.size() < required_size) {
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

    /// Write IFD to a vector (allocates the necessary space)
    [[nodiscard]] std::vector<std::byte> write() const noexcept {
        std::vector<std::byte> buffer(size_in_bytes());
        auto result = write(std::span<std::byte>(buffer));
        // This should never fail since we allocated the exact size
        assert(result.is_ok() && "write() to correctly sized buffer failed");
        return buffer;
    }
};

/// Get the first IFD offset from a TIFF file header
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFDOffset> get_first_ifd_offset(const Reader& reader) noexcept {
    using HeaderType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                          TiffHeader<SourceEndian>, 
                                          TiffBigHeader<SourceEndian>>;
    
    auto header_result = parsing::read_struct_no_endianness_conversion<Reader, HeaderType>(reader, 0);

    if (!header_result) {
        return Err(header_result.error().code, header_result.error().message);
    }

    const auto& header = header_result.value();
    if (!header.template is_valid<SourceEndian>()) {
        return Err(Error::Code::InvalidHeader, "TIFF header is not valid for the specified format");
    }

    std::size_t first_ifd_offset = header.template get_first_ifd_offset<std::endian::native>();
    return Ok(IFDOffset(first_ifd_offset));
}

/// Read an IFD header and return its description
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFDDescription<TiffFormat>> read_ifd_header(
    const Reader& reader, 
    IFDOffset offset) noexcept 
{
    using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
    
    auto ifd_header_result = parsing::read_struct_no_endianness_conversion<Reader, IFDHeaderType>(reader, offset.value);

    if (!ifd_header_result) {
        return Err(ifd_header_result.error().code, ifd_header_result.error().message);
    }

    const auto& ifd_header = ifd_header_result.value();
    std::size_t num_entries = static_cast<std::size_t>(ifd_header.template get_num_entries<std::endian::native>());

    return Ok(IFDDescription<TiffFormat>(offset, num_entries));
}

/// Get offset to next IFD in the chain
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFDOffset> read_next_ifd_offset(
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
    if (!result) {
        return Err(result.error().code, result.error().message);
    }
    
    OffsetType next_offset = result.value();
    if constexpr (SourceEndian != std::endian::native) {
        convert_endianness<OffsetType, SourceEndian, std::endian::native>(next_offset);
    }
    
    static_assert(sizeof(OffsetType) <= sizeof(std::size_t), "OffsetType must fit into std::size_t");
    return Ok(IFDOffset(static_cast<std::size_t>(next_offset)));
}

/// Read tags array from an IFD. Return the next IFD offset as well.
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFDOffset> read_ifd_tags(
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
    
    if (!tags_result) {
        return Err(tags_result.error().code, tags_result.error().message);
    }
    
    out_tags = std::move(tags_result.value());
    
    // Read next IFD offset
    return read_next_ifd_offset<Reader, TiffFormat, SourceEndian>(reader, ifd_desc);
}

/// Read a complete IFD into a preallocated IFD struct (reuses tag vector allocation).
/// Reads IFD header, all tags, and next IFD offset.
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<void> read_ifd_into(
    const Reader& reader,
    IFDOffset offset,
    IFD<TiffFormat, SourceEndian>& ifd) noexcept 
{
    using IFDHeaderType = typename IFDDescription<TiffFormat>::template HeaderType<SourceEndian>;
    using TagType = typename IFDDescription<TiffFormat>::template TagType<SourceEndian>;
    using OffsetType = typename IFDDescription<TiffFormat>::OffsetType;
    
    // First, read just the header to know how many entries
    auto header_result = parsing::read_struct_no_endianness_conversion<Reader, IFDHeaderType>(reader, offset.value);
    if (!header_result) {
        return Err(header_result.error().code, header_result.error().message);
    }
    
    const auto& ifd_header = header_result.value();
    std::size_t num_entries = static_cast<std::size_t>(ifd_header.template get_num_entries<std::endian::native>());
    
    ifd.description = IFDDescription<TiffFormat>(offset, num_entries);
    
    if (num_entries == 0) {
        ifd.tags.clear();
        
        // Read next IFD offset
        auto next_offset_result = read_next_ifd_offset<Reader, TiffFormat, SourceEndian>(reader, ifd.description);
        if (!next_offset_result) {
            return Err(next_offset_result.error().code, next_offset_result.error().message);
        }
        
        ifd.next_ifd_offset = next_offset_result.value();
        return Ok();
    }
    
    // Calculate total size to read: tags + next offset
    std::size_t total_read_size = num_entries * sizeof(TagType) + sizeof(OffsetType);
    std::size_t tags_offset = offset.value + sizeof(IFDHeaderType);
    
    auto data_result = reader.read(tags_offset, total_read_size);
    if (!data_result) {
        return Err(data_result.error().code, "Failed to read IFD tags and next offset");
    }
    
    const auto& view = data_result.value();
    if (view.size() < total_read_size) {
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

/// Read a complete IFD and return a new IFD struct.
/// Reads IFD header, all tags, and next IFD offset.
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFD<TiffFormat, SourceEndian>> read_ifd(
    const Reader& reader,
    IFDOffset offset) noexcept 
{
    IFD<TiffFormat, SourceEndian> ifd;
    auto result = read_ifd_into<Reader, TiffFormat, SourceEndian>(reader, offset, ifd);
    
    if (!result) {
        return Err(result.error().code, result.error().message);
    }
    
    return Ok(std::move(ifd));
}

} // namespace ifd

} // namespace tiffconcept