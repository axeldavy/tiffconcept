#pragma once

#include <array>
#include <bit>
#include <cstring>
#include <span>
#include <vector>
#include "result.hpp"
#include "reader_base.hpp"
#include "tag_spec.hpp"
#include "types.hpp"

namespace tiff {


/// Helper to read a structure from a reader
template <typename Reader, typename T>
[[nodiscard]] Result<T> read_struct_no_endianness_convertion(const Reader& reader, std::size_t offset) noexcept 
    requires RawReader<Reader>
{
    auto view_result = reader.read(offset, sizeof(T));
    
    if [[unlikely]](!view_result) {
        return Err(view_result.error().code, "Failed to read structure at offset " + std::to_string(offset));
    }
    
    auto view = std::move(*view_result);
    
    if [[unlikely]](view.size() < sizeof(T)) {
        return Err(Error::Code::UnexpectedEndOfFile, 
                   "Incomplete read at offset " + std::to_string(offset));
    }
    
    T value;
    std::memcpy(&value, view.data().data(), sizeof(T));
    return Ok(std::move(value));
}

/// Helper to read multiple structures from a reader
template <typename Reader, typename T, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<std::vector<T>> read_array(const Reader& reader, std::size_t offset, std::size_t count) noexcept 
    requires RawReader<Reader>
{
    if (count == 0) {
        return Ok(std::vector<T>{});
    }
    
    std::size_t total_size = count * sizeof(T);
    
    auto view_result = reader.read(offset, total_size);
    
    if [[unlikely]](!view_result) {
        return Err(view_result.error().code, "Failed to read array at offset " + std::to_string(offset));
    }
    
    auto view = std::move(*view_result);
    
    if [[unlikely]](view.size() < total_size) {
        return Err(Error::Code::UnexpectedEndOfFile, 
                   "Incomplete array read at offset " + std::to_string(offset));
    }
    
    std::vector<T> values(count);
    std::memcpy(values.data(), view.data().data(), total_size);
    
    // Convert endianness for each element
    if constexpr (SourceEndian != TargetEndian) {
        for (auto& val : values) {
            convert_endianness<T, SourceEndian, TargetEndian>(val);
        }
    }
    
    return Ok(std::move(values));
}

template <TiffFormatType TiffFormat, std::endian StorageEndian>
using TagType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                   TiffTag<StorageEndian>, 
                                   TiffBigTag<StorageEndian>>;

/// Parse a single tag value based on its type (works for both TiffTag and TiffBigTag)
/// ValueType: value format to parse (arithmetic or enum)
/// Reader: type of the reader
/// TiffFormat: TIFF format (Classic or BigTIFF)
/// SourceEndian: endianness of the stored tag
/// TargetEndian: endianness of the output value
template <typename ValueType, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<ValueType> parse_tag_value(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires std::is_arithmetic_v<ValueType> && RawReader<Reader> 
{
    if (tag.is_inline()) {
        ValueType value;
        std::memcpy(&value, &tag.value, sizeof(ValueType));
        convert_endianness<ValueType, SourceEndian, TargetEndian>(value);
        return Ok(value);
    } else {
        // We can only hit this for custom user defined types. We cannot handle endianness conversion here.
        static_assert(std::is_same_v<SourceEndian, TargetEndian> || !std::is_same_v<ValueType, ValueType>, "Non-inline tag value read not supported for this type");
        return read_struct_no_endianness_convertion<Reader, ValueType>(reader, tag.template get_offset<std::endian::native>());
    }
}

/// Specialization for vector types (array tags)
template <typename T, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<std::vector<T>> parse_tag_value(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires std::is_arithmetic_v<T> && RawReader<Reader>
{
    const std::size_t tag_count = static_cast<std::size_t>(tag.template get_count<std::endian::native>());
    if (tag_count == 0) {
        return Ok(std::vector<T>{});
    }
    
    if (tag_count == 1) {
        auto single_result = parse_tag_value<T, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
        if [[unlikely]](!single_result) {
            return Err(single_result.error().code, single_result.error().message);
        }
        return Ok(std::vector<T>{single_result.value()});
    }
    
    std::size_t data_size = tag_count * tiff_type_size(tag.template get_datatype<std::endian::native>());
    
    if (data_size <= tag.inline_bytecount_limit()) {
        std::vector<T> values(tag_count);
        std::memcpy(values.data(), &tag.value, data_size);
        for (auto& val : values) {
            convert_endianness<T, SourceEndian, TargetEndian>(val);
        }
        return Ok(std::move(values));
    } else {
        return read_array<Reader, T, SourceEndian, TargetEndian>(reader, tag.template get_offset<std::endian::native>(), tag_count);
    }
}

/// Specialization for enum types
template <typename EnumType, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<EnumType> parse_tag_value(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires std::is_enum_v<EnumType> && RawReader<Reader>
{
    using UnderlyingType = std::underlying_type_t<EnumType>;
    auto result = parse_tag_value<UnderlyingType, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
    
    if [[unlikely]](!result) {
        return Err(result.error().code, result.error().message);
    }
    
    return Ok(static_cast<EnumType>(result.value()));
}

/// Helper to check if a tag descriptor accepts alternate types
template <typename TagDesc>
consteval bool has_alternate_types() {
    return TagDesc::alternate_types.size() > 0;
}

/// Helper to check if a datatype is in the alternate types list
template <typename TagDesc>
[[nodiscard]] constexpr bool is_alternate_type(TiffDataType dt) noexcept {
    for (const auto& alt : TagDesc::alternate_types) {
        if (alt == dt) return true;
    }
    return false;
}

/// Parse tag with datatype validation and allowed type promotion
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_tag_value_with_promotion(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>
{
    const auto tag_code = tag.template get_code<std::endian::native>();
    const auto tag_datatype = tag.template get_datatype<std::endian::native>();
    
    // Verify tag code matches
    if [[unlikely]](tag_code != static_cast<uint16_t>(TagDesc::code)) {
        return Err(Error::Code::InvalidTag, 
                   "Tag code mismatch: expected " + 
                   std::to_string(static_cast<uint16_t>(TagDesc::code)) +
                   ", got " + std::to_string(tag_code));
    }

    // Validate count for scalar tags
    if constexpr (std::is_arithmetic_v<typename TagDesc::value_type> ||
                  std::is_enum_v<typename TagDesc::value_type>) {
        if [[unlikely]](tag.template get_count<std::endian::native>() != 1) {
            return Err(Error::Code::InvalidTag,
                    "Scalar tag " + std::to_string(tag_code) + 
                    " has count " + std::to_string(tag.template get_count<std::endian::native>()) + ", expected 1");
        }
    }

    // Check if datatype matches exactly
    if (tag_datatype == TagDesc::datatype) {
        return parse_tag_value<typename TagDesc::value_type, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
    }

    // If no alternate types, it's an error
    if constexpr (!has_alternate_types<TagDesc>()) {
        return Err(Error::Code::InvalidTagType,
                   "Tag " + std::to_string(tag_code) + 
                   " datatype mismatch: expected " + 
                   std::to_string(static_cast<uint16_t>(TagDesc::datatype)) +
                   ", got " + std::to_string(static_cast<uint16_t>(tag_datatype)));
    } else {
        // Check if it's an accepted alternate type
        if (!is_alternate_type<TagDesc>(tag_datatype)) {
            return Err(Error::Code::InvalidTagType,
                       "Tag " + std::to_string(tag_code) + 
                       " has unsupported datatype " + 
                       std::to_string(static_cast<uint16_t>(tag_datatype)));
        }

        // Perform type promotion based on the actual datatype
        using TargetType = typename TagDesc::value_type;
        
        // Handle scalar vs vector target types
        if constexpr (std::is_arithmetic_v<TargetType>) {
            // Scalar promotion
            switch (tag_datatype) {
                case TiffDataType::Byte: {
                    auto result = parse_tag_value<uint8_t, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
                    if (!result) return Err(result.error().code, result.error().message);
                    return Ok(static_cast<TargetType>(result.value()));
                }
                case TiffDataType::Short: {
                    auto result = parse_tag_value<uint16_t, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
                    if (!result) return Err(result.error().code, result.error().message);
                    return Ok(static_cast<TargetType>(result.value()));
                }
                case TiffDataType::Long: {
                    auto result = parse_tag_value<uint32_t, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
                    if (!result) return Err(result.error().code, result.error().message);
                    return Ok(static_cast<TargetType>(result.value()));
                }
                case TiffDataType::Long8: {
                    auto result = parse_tag_value<uint64_t, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
                    if (!result) return Err(result.error().code, result.error().message);
                    return Ok(static_cast<TargetType>(result.value()));
                }
                default:
                    return Err(Error::Code::InvalidTagType, 
                               "Unsupported type promotion for tag " + std::to_string(tag_code));
            }
        } else if constexpr (requires { typename TargetType::value_type; }) {
            // Vector promotion
            using ElementType = typename TargetType::value_type;
            
            switch (tag_datatype) {
                case TiffDataType::Byte: {
                    auto result = parse_tag_value<std::vector<uint8_t>, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
                    if (!result) return Err(result.error().code, result.error().message);
                    TargetType promoted;
                    promoted.reserve(result.value().size());
                    for (const auto& val : result.value()) {
                        promoted.push_back(static_cast<ElementType>(val));
                    }
                    return Ok(std::move(promoted));
                }
                case TiffDataType::Short: {
                    auto result = parse_tag_value<std::vector<uint16_t>, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
                    if (!result) return Err(result.error().code, result.error().message);
                    TargetType promoted;
                    promoted.reserve(result.value().size());
                    for (const auto& val : result.value()) {
                        promoted.push_back(static_cast<ElementType>(val));
                    }
                    return Ok(std::move(promoted));
                }
                case TiffDataType::Long: {
                    auto result = parse_tag_value<std::vector<uint32_t>, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
                    if (!result) return Err(result.error().code, result.error().message);
                    TargetType promoted;
                    promoted.reserve(result.value().size());
                    for (const auto& val : result.value()) {
                        promoted.push_back(static_cast<ElementType>(val));
                    }
                    return Ok(std::move(promoted));
                }
                case TiffDataType::Long8: {
                    auto result = parse_tag_value<std::vector<uint64_t>, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
                    if (!result) return Err(result.error().code, result.error().message);
                    TargetType promoted;
                    promoted.reserve(result.value().size());
                    for (const auto& val : result.value()) {
                        promoted.push_back(static_cast<ElementType>(val));
                    }
                    return Ok(std::move(promoted));
                }
                default:
                    return Err(Error::Code::InvalidTagType, 
                               "Unsupported type promotion for tag " + std::to_string(tag_code));
            }
        } else {
            return Err(Error::Code::InvalidTagType, 
                       "Unsupported target type for promotion");
        }
    }
}


/// Tag reader - extracts tags from a TIFF IFD
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
class TagReader {
private:
    const Reader& reader_;
    
public:
    using TiffHeaderType = std::conditional_t<TiffFormat == TiffFormatType::Classic, TiffHeader<SourceEndian>, TiffBigHeader<SourceEndian>>;
    using IFDHeaderType = std::conditional_t<TiffFormat == TiffFormatType::Classic, IFDHeader<SourceEndian>, IFDBigHeader<SourceEndian>>;
    using OffsetType = std::conditional_t<TiffFormat == TiffFormatType::Classic, uint32_t, uint64_t>;

    explicit TagReader(const Reader& reader) noexcept
        : reader_(reader) {}
    
    /// Read TIFF header
    [[nodiscard]] Result<TiffHeaderType> read_header() const noexcept {
        return read_struct_no_endianness_convertion<Reader, TiffHeaderType>(reader_, 0);
    }
    
    /// Read IFD header at given offset
    [[nodiscard]] Result<IFDHeaderType> read_ifd_header(std::size_t offset) const noexcept {
        return read_struct_no_endianness_convertion<Reader, IFDHeaderType>(reader_, offset);
    }
    
    /// Read all tags from an IFD
    [[nodiscard]] Result<std::vector<TagType<TiffFormat, SourceEndian>>> read_ifd_tags(std::size_t ifd_offset) const noexcept {
        auto header_result = read_ifd_header(ifd_offset);
        if (!header_result) {
            return Err(header_result.error().code, header_result.error().message);
        }
        
        const auto& header = header_result.value();
        const auto num_entries = header.template get_num_entries<std::endian::native>();
        
        if (num_entries == 0) {
            return Ok(std::vector<TagType<TiffFormat, SourceEndian>>{});
        }
        
        // Read all tag entries
        std::size_t tags_offset = ifd_offset + sizeof(IFDHeaderType);
        return read_array<Reader, TagType<TiffFormat, SourceEndian>, SourceEndian, SourceEndian>(reader_, tags_offset, num_entries);
    }
    
    /// Get offset to next IFD
    [[nodiscard]] Result<std::size_t> read_next_ifd_offset(std::size_t ifd_offset) const noexcept {
        auto header_result = read_ifd_header(ifd_offset);
        if [[unlikely]](!header_result) {
            return Err(header_result.error().code, header_result.error().message);
        }
        
        const auto& header = header_result.value();
        const auto num_entries = header.template get_num_entries<std::endian::native>();
        
        // Next IFD offset is stored after all tag entries
        std::size_t next_offset_pos = ifd_offset + sizeof(IFDHeaderType) + 
                                       num_entries * sizeof(TagType<TiffFormat, SourceEndian>);
        
        auto result = read_struct_no_endianness_convertion<Reader, OffsetType>(reader_, next_offset_pos);
        if (!result) {
            return Err(result.error().code, result.error().message);
        }
        OffsetType next_offset = result.value();
        if constexpr (SourceEndian != std::endian::native) {
            convert_endianness<OffsetType, SourceEndian, std::endian::native>(next_offset);
        }
        static_assert(sizeof(OffsetType) <= sizeof(std::size_t), "OffsetType must fit into std::size_t");
        return Ok(static_cast<std::size_t>(next_offset));
    }

    /// Get the total number of pages (IFDs) in the TIFF file
    /// @param max_pages Maximum number of pages to count (0 = unlimited)
    [[nodiscard]] Result<std::size_t> get_page_count(std::size_t max_pages = 0) const noexcept {
        auto header_result = read_header();
        if [[unlikely]](!header_result) {
            return Err(header_result.error().code, header_result.error().message);
        }
        
        const auto& header = header_result.value();
        
        if [[unlikely]](!header.template is_valid<SourceEndian>()) {
            return Err(Error::Code::InvalidHeader, "Tiff header is not valid for the specified format");
        }
        
        std::size_t count = 0;
        std::size_t current_offset = header.template get_first_ifd_offset<std::endian::native>();
        
        // Follow the IFD chain until we reach the end (offset = 0) or max_pages
        while (current_offset != 0) {
            ++count;
            
            // Stop early if we've reached the maximum
            if [[unlikely]](max_pages > 0 && count >= max_pages) {
                return Ok(count);
            }
            
            auto next_result = read_next_ifd_offset(current_offset);
            if [[unlikely]](!next_result) {
                return Err(next_result.error().code, next_result.error().message);
            }
            
            current_offset = next_result.value();
        }
        
        return Ok(count);
    }

    /// Get all page offsets in the TIFF file
    /// @param max_pages Maximum number of pages to retrieve
    [[nodiscard]] Result<std::vector<std::size_t>> get_all_page_offsets(std::size_t max_pages = 1) const noexcept {
        if (max_pages == 0) {
            return Err(Error::Code::InvalidArgument, "max_pages must be greater than zero");
        }

        auto header_result = read_header();
        if [[unlikely]](!header_result) {
            return Err(header_result.error().code, header_result.error().message);
        }
        
        const auto& header = header_result.value();
        
        if [[unlikely]](!header.template is_valid<SourceEndian>()) {
            return Err(Error::Code::InvalidHeader, "Tiff header is not valid for the specified format");
        }
        
        std::vector<std::size_t> offsets;
        
        // Reserve space
        offsets.reserve(max_pages);
        
        std::size_t current_offset = header.template get_first_ifd_offset<std::endian::native>();
        
        while (current_offset != 0) {
            offsets.push_back(current_offset);
            
            // Stop early if we've reached the maximum
            if [[unlikely]](offsets.size() >= max_pages) {
                return Ok(std::move(offsets));
            }
            
            auto next_result = read_next_ifd_offset(current_offset);
            if [[unlikely]](!next_result) {
                return Err(next_result.error().code, next_result.error().message);
            }
            
            current_offset = next_result.value();
        }
        
        return Ok(std::move(offsets));
    }

    /// Fill a span with page offsets (zero-copy for pre-allocated buffers)
    /// @param offsets Span to fill with offsets
    /// @return Number of pages written
    [[nodiscard]] Result<std::size_t> get_page_offsets(std::span<std::size_t> offsets) const noexcept {
        auto header_result = read_header();
        if [[unlikely]](!header_result) {
            return Err(header_result.error().code, header_result.error().message);
        }
        
        const auto& header = header_result.value();
        
        if [[unlikely]](!header.template is_valid<SourceEndian>()) {
            return Err(Error::Code::InvalidHeader, "Tiff header is not valid for the specified format");
        }
        
        std::size_t count = 0;
        std::size_t current_offset = header.template get_first_ifd_offset<std::endian::native>();
        const std::size_t max_pages = offsets.size();
        
        while (current_offset != 0 && count < max_pages) {
            offsets[count] = current_offset;
            ++count;
            
            auto next_result = read_next_ifd_offset(current_offset);
            if [[unlikely]](!next_result) {
                return Err(next_result.error().code, next_result.error().message);
            }
            
            current_offset = next_result.value();
        }
        
        return Ok(count);
    }

    /// Find a specific tag in a list of tags, no sorting assumed
    [[nodiscard]] std::optional<TagType<TiffFormat, SourceEndian>> find_tag(
        const std::vector<TagType<TiffFormat, SourceEndian>>& tags,
        TagCode code) const noexcept {
        
        for (const auto& tag : tags) {
            if (tag.template get_code<std::endian::native>() == static_cast<uint16_t>(code)) {
                return tag;
            }
        }
        return std::nullopt;
    }
    
    /// Parse a tag value with type checking
    template <typename TagDesc, std::endian TargetEndian = std::endian::native>
    [[nodiscard]] Result<typename TagDesc::value_type> parse_tag(const TagType<TiffFormat, SourceEndian>& tag) const noexcept {
        return parse_tag_value_with_promotion<typename TagDesc::value_type, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader_);
    }
    
    /// Navigate to a specific page (IFD)
    [[nodiscard]] Result<std::size_t> get_page_offset(std::size_t page_index) const noexcept {
        auto header_result = read_header();
        if [[unlikely]](!header_result) {
            return Err(header_result.error().code, header_result.error().message);
        }
        
        const auto& header = header_result.value();
        
        if [[unlikely]](!header.template is_valid<SourceEndian>()) {
            return Err(Error::Code::InvalidHeader, "Not a valid TIFF file");
        }
        
        std::size_t current_offset = header.template get_first_ifd_offset<std::endian::native>();
        
        for (std::size_t i = 0; i < page_index; ++i) {
            if [[unlikely]](current_offset == 0) {
                return Err(Error::Code::InvalidPageIndex, 
                           "Page index " + std::to_string(page_index) + " not found");
            }
            
            auto next_result = read_next_ifd_offset(current_offset);
            if [[unlikely]](!next_result) {
                return Err(next_result.error().code, next_result.error().message);
            }
            
            current_offset = next_result.value();
        }
        
        if [[unlikely]](current_offset == 0) {
            return Err(Error::Code::InvalidPageIndex, 
                       "Page index " + std::to_string(page_index) + " not found");
        }
        
        return Ok(current_offset);
    }
    
    const Reader& reader() const noexcept { return reader_; }
};

} // namespace tiff
