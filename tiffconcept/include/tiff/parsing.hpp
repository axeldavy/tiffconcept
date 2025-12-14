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

namespace parsing {

/// Helper to read a structure from a reader
template <typename Reader, typename T>
[[nodiscard]] Result<T> read_struct_no_endianness_conversion(const Reader& reader, std::size_t offset) noexcept 
    requires RawReader<Reader>
{
    auto view_result = reader.read(offset, sizeof(T));
    
    if (!view_result) {
        return Err(view_result.error().code, "Failed to read structure at offset " + std::to_string(offset));
    }
    
    const auto& view = view_result.value();
    
    if (view.size() < sizeof(T)) {
        return Err(Error::Code::UnexpectedEndOfFile, 
                   "Incomplete read at offset " + std::to_string(offset));
    }
    
    T value;
    std::memcpy(&value, view.data().data(), sizeof(T));
    return Ok(std::move(value));
}

/// Helper to read multiple structures from a reader (into a pre-allocated span)
template <typename Reader, typename T, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<void> read_array_into(const Reader& reader, std::size_t offset, std::span<T> output) noexcept 
    requires RawReader<Reader>
{
    if (output.empty()) {
        return Ok();
    }

    std::size_t total_size = output.size() * sizeof(T);

    auto view_result = reader.read(offset, total_size);

    if (!view_result) {
        return Err(view_result.error().code, "Failed to read array at offset " + std::to_string(offset));
    }

    const auto& view = view_result.value();

    if (view.size() < total_size) {
        return Err(Error::Code::UnexpectedEndOfFile, 
                   "Incomplete array read at offset " + std::to_string(offset));
    }

    std::memcpy(output.data(), view.data().data(), total_size);
    
    // Convert endianness for each element
    if constexpr (SourceEndian != TargetEndian) {
        for (auto& val : output) {
            convert_endianness<T, SourceEndian, TargetEndian>(val);
        }
    }
    
    return Ok();
}

/// Helper to read multiple structures from a reader
template <typename Reader, typename T, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<std::vector<T>> read_array(const Reader& reader, std::size_t offset, std::size_t count) noexcept 
    requires RawReader<Reader>
{
    if (count == 0) {
        return Ok(std::vector<T>{});
    }
    
    std::vector<T> values(count);
    auto result = read_array_into<Reader, T, SourceEndian, TargetEndian>(reader, offset, std::span<T>(values));
    
    if(!result) {
        return Err(result.error().code, result.error().message);
    }
    
    return Ok(std::move(values));
}

template <TiffFormatType TiffFormat, std::endian StorageEndian>
using TagType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                   TiffTag<StorageEndian>, 
                                   TiffBigTag<StorageEndian>>;

/// Parse a single scalar value (arithmetic or enum)
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::element_type> parse_single_scalar(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader> 
{
    using ElementType = typename TagDesc::element_type;
    using RefType = typename TagDesc::reference_type;
    
    if (tag.is_inline()) {
        RefType value;
        std::memcpy(&value, &tag.value, sizeof(RefType));
        convert_endianness<RefType, SourceEndian, TargetEndian>(value);
        return Ok(static_cast<ElementType>(value));
    } else {
        if constexpr (SourceEndian != TargetEndian) {
            if constexpr (std::is_arithmetic_v<RefType>) {
                auto result = read_struct_no_endianness_conversion<Reader, RefType>(reader, tag.template get_offset<std::endian::native>());
                if (!result) return Err(result.error().code, result.error().message);
                RefType value = result.value();
                convert_endianness<RefType, SourceEndian, TargetEndian>(value);
                return Ok(static_cast<ElementType>(value));
            } else {
                // For custom types, we cannot handle endianness conversion
                []<bool flag = false>() {
                    static_assert(flag, "Non-inline custom type values require SourceEndian == TargetEndian");
                }();
            }
        }
        auto result = read_struct_no_endianness_conversion<Reader, RefType>(reader, tag.template get_offset<std::endian::native>());
        if (!result) return Err(result.error().code, result.error().message);
        return Ok(static_cast<ElementType>(result.value()));
    }
}

/// Parse a single rational (Rational or SRational type)
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::element_type> parse_single_rational(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>
{
    using RationalType = typename TagDesc::element_type;
    using ComponentType = typename TagDesc::reference_type;
    const std::size_t tag_count = static_cast<std::size_t>(tag.template get_count<std::endian::native>());
    
    // Rationals should have count=1 (representing one rational = 2 values)
    if (tag_count != 1) {
        return Err(Error::Code::InvalidTag, 
                  "Rational tag has invalid count: " + std::to_string(tag_count));
    }
    
    RationalType result;
    
    if (tag.is_inline()) {
        // For inline rationals (only possible if 2*sizeof(ComponentType) <= inline_limit)
        std::memcpy(&result.numerator, &tag.value, sizeof(ComponentType));
        std::memcpy(&result.denominator, reinterpret_cast<const std::byte*>(&tag.value) + sizeof(ComponentType), sizeof(ComponentType));
        convert_endianness<ComponentType, SourceEndian, TargetEndian>(result.numerator);
        convert_endianness<ComponentType, SourceEndian, TargetEndian>(result.denominator);
    } else {
        // External storage
        auto data_result = reader.read(tag.template get_offset<std::endian::native>(), 2 * sizeof(ComponentType));
        if (!data_result) {
            return Err(data_result.error().code, "Failed to read rational data");
        }
        
        const auto& view = data_result.value();
        if (view.size() < 2 * sizeof(ComponentType)) {
            return Err(Error::Code::UnexpectedEndOfFile, "Incomplete rational data");
        }
        
        std::memcpy(&result.numerator, view.data().data(), sizeof(ComponentType));
        std::memcpy(&result.denominator, view.data().data() + sizeof(ComponentType), sizeof(ComponentType));
        convert_endianness<ComponentType, SourceEndian, TargetEndian>(result.numerator);
        convert_endianness<ComponentType, SourceEndian, TargetEndian>(result.denominator);
    }
    
    return Ok(result);
}

/// Parse a container of scalars
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_scalar_container(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>
{
    using ContainerType = typename TagDesc::value_type;
    using ElemType = typename TagDesc::element_type;
    using RefType = typename TagDesc::reference_type;
    const std::size_t tag_count = static_cast<std::size_t>(tag.template get_count<std::endian::native>());

    std::cerr << "Debug: Parsing rational tag with count " << tag_count << std::endl;
    
    // Handle vector (dynamic size)
    if constexpr (requires { ContainerType{}.resize(tag_count); }) {
        if (tag_count == 0) {
            return Ok(ContainerType{});
        }
        ContainerType result;
        result.resize(tag_count);
        
        std::size_t data_size = tag_count * sizeof(RefType);
        
        if (data_size <= tag.inline_bytecount_limit()) {
            // Read as reference type for endianness conversion
            const std::byte* src = reinterpret_cast<const std::byte*>(&tag.value);
            for (std::size_t i = 0; i < tag_count; ++i) {
                RefType ref_val;
                std::memcpy(&ref_val, src + i * sizeof(RefType), sizeof(RefType));
                convert_endianness<RefType, SourceEndian, TargetEndian>(ref_val);
                result[i] = static_cast<ElemType>(ref_val);
            }
        } else {
            // Read as reference type, then convert to element type
            std::vector<RefType> ref_buffer(tag_count);
            auto read_result = read_array_into<Reader, RefType, SourceEndian, TargetEndian>(
                reader, tag.template get_offset<std::endian::native>(), std::span<RefType>(ref_buffer));
            if (!read_result) {
                return Err(read_result.error().code, read_result.error().message);
            }
            for (std::size_t i = 0; i < tag_count; ++i) {
                result[i] = static_cast<ElemType>(ref_buffer[i]);
            }
        }
        return Ok(std::move(result));
    }
    // Handle fixed-size array
    else {
        constexpr std::size_t N = std::tuple_size_v<ContainerType>;
        if (tag_count != N) {
            return Err(Error::Code::InvalidTag,
                      "Array count mismatch: expected " + std::to_string(N) + 
                      ", got " + std::to_string(tag_count));
        }
        
        ContainerType result;
        std::size_t data_size = N * sizeof(RefType);
        
        if (data_size <= tag.inline_bytecount_limit()) {
            // Read as reference type for endianness conversion
            const std::byte* src = reinterpret_cast<const std::byte*>(&tag.value);
            for (std::size_t i = 0; i < N; ++i) {
                RefType ref_val;
                std::memcpy(&ref_val, src + i * sizeof(RefType), sizeof(RefType));
                convert_endianness<RefType, SourceEndian, TargetEndian>(ref_val);
                result[i] = static_cast<ElemType>(ref_val);
            }
        } else {
            // Read as reference type, then convert to element type
            std::array<RefType, N> ref_buffer;
            auto read_result = read_array_into<Reader, RefType, SourceEndian, TargetEndian>(
                reader, tag.template get_offset<std::endian::native>(), std::span<RefType>(ref_buffer));
            if (!read_result) {
                return Err(read_result.error().code, read_result.error().message);
            }
            for (std::size_t i = 0; i < N; ++i) {
                result[i] = static_cast<ElemType>(ref_buffer[i]);
            }
        }
        return Ok(result);
    }
}

/// Parse a container of rationals
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_rational_container(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>
{
    using ContainerType = typename TagDesc::value_type;
    using ComponentType = typename TagDesc::reference_type;
    const std::size_t tag_count = static_cast<std::size_t>(tag.template get_count<std::endian::native>());
    
    // Handle vector (dynamic size)
    if constexpr (requires { ContainerType{}.resize(tag_count); }) {
        if (tag_count == 0) {
            return Ok(ContainerType{});
        }
        ContainerType result;
        result.resize(tag_count);
        
        const std::size_t total_bytes = tag_count * 2 * sizeof(ComponentType);
        
        if (total_bytes <= tag.inline_bytecount_limit()) {
            const std::byte* src = reinterpret_cast<const std::byte*>(&tag.value);
            for (std::size_t i = 0; i < tag_count; ++i) {
                std::memcpy(&result[i].numerator, src, sizeof(ComponentType));
                std::memcpy(&result[i].denominator, src + sizeof(ComponentType), sizeof(ComponentType));
                convert_endianness<ComponentType, SourceEndian, TargetEndian>(result[i].numerator);
                convert_endianness<ComponentType, SourceEndian, TargetEndian>(result[i].denominator);
                src += 2 * sizeof(ComponentType);
            }
        } else {
            auto data_result = reader.read(tag.template get_offset<std::endian::native>(), total_bytes);
            if (!data_result) {
                return Err(data_result.error().code, "Failed to read rational array");
            }
            
            const auto& view = data_result.value();
            if (view.size() < total_bytes) {
                return Err(Error::Code::UnexpectedEndOfFile, "Incomplete rational array");
            }
            
            const std::byte* src = view.data().data();
            for (std::size_t i = 0; i < tag_count; ++i) {
                std::memcpy(&result[i].numerator, src, sizeof(ComponentType));
                std::memcpy(&result[i].denominator, src + sizeof(ComponentType), sizeof(ComponentType));
                convert_endianness<ComponentType, SourceEndian, TargetEndian>(result[i].numerator);
                convert_endianness<ComponentType, SourceEndian, TargetEndian>(result[i].denominator);
                src += 2 * sizeof(ComponentType);
            }
        }
        return Ok(std::move(result));
    }
    // Handle fixed-size array
    else {
        constexpr std::size_t N = std::tuple_size_v<ContainerType>;
        if (tag_count != N) {
            return Err(Error::Code::InvalidTag,
                      "Rational array count mismatch: expected " + std::to_string(N) + 
                      ", got " + std::to_string(tag_count));
        }
        
        ContainerType result;
        const std::size_t total_bytes = N * 2 * sizeof(ComponentType);
        
        if (total_bytes <= tag.inline_bytecount_limit()) {
            const std::byte* src = reinterpret_cast<const std::byte*>(&tag.value);
            for (std::size_t i = 0; i < N; ++i) {
                std::memcpy(&result[i].numerator, src, sizeof(ComponentType));
                std::memcpy(&result[i].denominator, src + sizeof(ComponentType), sizeof(ComponentType));
                convert_endianness<ComponentType, SourceEndian, TargetEndian>(result[i].numerator);
                convert_endianness<ComponentType, SourceEndian, TargetEndian>(result[i].denominator);
                src += 2 * sizeof(ComponentType);
            }
        } else {
            auto data_result = reader.read(tag.template get_offset<std::endian::native>(), total_bytes);
            if (!data_result) {
                return Err(data_result.error().code, "Failed to read rational array");
            }
            
            const auto& view = data_result.value();
            if (view.size() < total_bytes) {
                return Err(Error::Code::UnexpectedEndOfFile, "Incomplete rational array");
            }
            
            const std::byte* src = view.data().data();
            for (std::size_t i = 0; i < N; ++i) {
                std::memcpy(&result[i].numerator, src, sizeof(ComponentType));
                std::memcpy(&result[i].denominator, src + sizeof(ComponentType), sizeof(ComponentType));
                convert_endianness<ComponentType, SourceEndian, TargetEndian>(result[i].numerator);
                convert_endianness<ComponentType, SourceEndian, TargetEndian>(result[i].denominator);
                src += 2 * sizeof(ComponentType);
            }
        }
        return Ok(result);
    }
}

/// Unified parse_tag_value using TagDescriptor properties
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_tag_value(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>
{
    using ValueType = typename TagDesc::value_type;
    
    // Handle strings specially
    if constexpr (std::is_same_v<ValueType, std::string>) {
        const std::size_t tag_count = static_cast<std::size_t>(tag.template get_count<std::endian::native>());
        if (tag_count == 0) {
            return Ok(std::string{});
        }
        
        if (tag_count <= tag.inline_bytecount_limit()) {
            const char* str_data = reinterpret_cast<const char*>(&tag.value);
            return Ok(std::string(str_data, std::min(tag_count, std::strlen(str_data))));
        } else {
            auto data_result = reader.read(tag.template get_offset<std::endian::native>(), tag_count);
            if (!data_result) {
                return Err(data_result.error().code, "Failed to read string data");
            }
            const auto& view = data_result.value();
            if (view.size() < tag_count) {
                return Err(Error::Code::UnexpectedEndOfFile, "Incomplete string data");
            }
            const char* str_data = reinterpret_cast<const char*>(view.data().data());
            return Ok(std::string(str_data, std::min(tag_count, std::strlen(str_data))));
        }
    }
    // Single value (scalar, enum, or rational)
    else if constexpr (!TagDesc::is_container) {
        if constexpr (TagDesc::is_rational) {
            return parse_single_rational<TagDesc, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
        } else {
            return parse_single_scalar<TagDesc, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
        }
    }
    // Container of values
    else {
        if constexpr (TagDesc::is_rational) {
            return parse_rational_container<TagDesc, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
        } else {
            return parse_scalar_container<TagDesc, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
        }
    }
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

// Forward declaration for type promotion helper
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian, std::endian TargetEndian>
[[nodiscard]] Result<typename TagDesc::value_type> do_type_promotion(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader,
    TiffDataType actual_datatype) noexcept 
    requires RawReader<Reader>;

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
    if (tag_code != static_cast<uint16_t>(TagDesc::code)) {
        return Err(Error::Code::InvalidTag, 
                   "Tag code mismatch: expected " + 
                   std::to_string(static_cast<uint16_t>(TagDesc::code)) +
                   ", got " + std::to_string(tag_code));
    }

    // Validate count for scalar tags
    if constexpr (std::is_arithmetic_v<typename TagDesc::value_type> ||
                  std::is_enum_v<typename TagDesc::value_type>) {
        if (tag.template get_count<std::endian::native>() != 1) {
            return Err(Error::Code::InvalidTag,
                    "Scalar tag " + std::to_string(tag_code) + 
                    " has count " + std::to_string(tag.template get_count<std::endian::native>()) + ", expected 1");
        }
    }

    // Check if datatype matches exactly
    if (tag_datatype == TagDesc::datatype) {
        return parse_tag_value<TagDesc, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
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

        // Perform type promotion - dispatch to helper template
        return do_type_promotion<TagDesc, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader, tag_datatype);
    }
}

// Helper template for type promotion - creates alternate TagDescriptor at namespace scope
template <TagCode Code, TiffDataType AltDataType, typename AltType>
struct AlternateTagDescriptor {
    static constexpr TagCode code = Code;
    static constexpr TiffDataType datatype = AltDataType;
    using value_type = AltType;
    using element_type = typename detail::unwrap_container<AltType>::type;
    using reference_type = typename detail::tiff_reference_type<AltDataType>::type;
    static constexpr bool is_container = std::ranges::range<AltType> && !std::is_same_v<AltType, std::string>;
    static constexpr bool is_rational = false;
    static constexpr std::array<TiffDataType, 0> alternate_types{};
};

// Helper to convert from parsed alternate type to target type
template <typename TargetType, typename SourceType>
inline TargetType convert_promoted_value(const SourceType& source) {
    if constexpr (std::is_arithmetic_v<TargetType> || std::is_enum_v<TargetType>) {
        // Scalar to scalar conversion
        return static_cast<TargetType>(source);
    } else if constexpr (std::ranges::range<TargetType> && !std::is_same_v<TargetType, std::string>) {
        // Container to container conversion
        using TargetElem = std::ranges::range_value_t<TargetType>;
        TargetType result;
        if constexpr (requires { result.reserve(std::ranges::size(source)); }) {
            result.reserve(std::ranges::size(source));
        }
        for (const auto& elem : source) {
            if constexpr (requires { result.push_back(static_cast<TargetElem>(elem)); }) {
                result.push_back(static_cast<TargetElem>(elem));
            }
        }
        return result;
    } else {
        static_assert(std::is_same_v<TargetType, SourceType>, "Unsupported type promotion");
        return source;
    }
}

// Dispatcher for type promotion
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian, std::endian TargetEndian>
[[nodiscard]] Result<typename TagDesc::value_type> do_type_promotion(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader,
    TiffDataType actual_datatype) noexcept 
    requires RawReader<Reader>
{
    using TargetType = typename TagDesc::value_type;
    constexpr bool is_container = TagDesc::is_container;
    
    #define PROMOTE_SCALAR(CppType, DataType) \
        do { \
            using AltDesc = AlternateTagDescriptor<TagDesc::code, DataType, CppType>; \
            auto result = parse_tag_value<AltDesc, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader); \
            if (!result) return Err(result.error().code, result.error().message); \
            return Ok(convert_promoted_value<TargetType>(result.value())); \
        } while(false)
    
    #define PROMOTE_CONTAINER(CppType, DataType) \
        do { \
            using AltDesc = AlternateTagDescriptor<TagDesc::code, DataType, std::vector<CppType>>; \
            auto result = parse_tag_value<AltDesc, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader); \
            if (!result) return Err(result.error().code, result.error().message); \
            return Ok(convert_promoted_value<TargetType>(result.value())); \
        } while(false)
    
    switch (actual_datatype) {
        case TiffDataType::Byte:
            if constexpr (is_container) { PROMOTE_CONTAINER(uint8_t, TiffDataType::Byte); }
            else { PROMOTE_SCALAR(uint8_t, TiffDataType::Byte); }
        case TiffDataType::Short:
            if constexpr (is_container) { PROMOTE_CONTAINER(uint16_t, TiffDataType::Short); }
            else { PROMOTE_SCALAR(uint16_t, TiffDataType::Short); }
        case TiffDataType::Long:
            if constexpr (is_container) { PROMOTE_CONTAINER(uint32_t, TiffDataType::Long); }
            else { PROMOTE_SCALAR(uint32_t, TiffDataType::Long); }
        case TiffDataType::Long8:
            if constexpr (is_container) { PROMOTE_CONTAINER(uint64_t, TiffDataType::Long8); }
            else { PROMOTE_SCALAR(uint64_t, TiffDataType::Long8); }
        case TiffDataType::SByte:
            if constexpr (is_container) { PROMOTE_CONTAINER(int8_t, TiffDataType::SByte); }
            else { PROMOTE_SCALAR(int8_t, TiffDataType::SByte); }
        case TiffDataType::SShort:
            if constexpr (is_container) { PROMOTE_CONTAINER(int16_t, TiffDataType::SShort); }
            else { PROMOTE_SCALAR(int16_t, TiffDataType::SShort); }
        case TiffDataType::SLong:
            if constexpr (is_container) { PROMOTE_CONTAINER(int32_t, TiffDataType::SLong); }
            else { PROMOTE_SCALAR(int32_t, TiffDataType::SLong); }
        case TiffDataType::SLong8:
            if constexpr (is_container) { PROMOTE_CONTAINER(int64_t, TiffDataType::SLong8); }
            else { PROMOTE_SCALAR(int64_t, TiffDataType::SLong8); }
        case TiffDataType::Float:
            if constexpr (is_container) { PROMOTE_CONTAINER(float, TiffDataType::Float); }
            else { PROMOTE_SCALAR(float, TiffDataType::Float); }
        case TiffDataType::Double:
            if constexpr (is_container) { PROMOTE_CONTAINER(double, TiffDataType::Double); }
            else { PROMOTE_SCALAR(double, TiffDataType::Double); }
        default:
            return Err(Error::Code::InvalidTagType, 
                       "Unsupported type promotion for datatype " + 
                       std::to_string(static_cast<uint16_t>(actual_datatype)));
    }
    
    #undef PROMOTE_SCALAR
    #undef PROMOTE_CONTAINER
}

template <RawReader Reader, typename TagDesc, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian TargetEndian = std::endian::native, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_tag(const Reader& reader, const TagType<TiffFormat, SourceEndian>& tag) noexcept {
    return parse_tag_value_with_promotion<TagDesc, Reader, TiffFormat, SourceEndian, TargetEndian>(tag, reader);
}

} // namespace parsing

} // namespace tiff
