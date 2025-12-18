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

namespace tiffconcept {

namespace parsing {

/// Helper to read a structure from a reader
template <typename Reader, typename T>
[[nodiscard]] Result<T> read_struct_no_endianness_conversion(const Reader& reader, std::size_t offset) noexcept 
    requires RawReader<Reader>;

/// Helper to read multiple structures from a reader (into a pre-allocated span)
template <typename Reader, typename T, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<void> read_array_into(const Reader& reader, std::size_t offset, std::span<T> output) noexcept 
    requires RawReader<Reader>;

/// Helper to read multiple structures from a reader
template <typename Reader, typename T, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<std::vector<T>> read_array(const Reader& reader, std::size_t offset, std::size_t count) noexcept 
    requires RawReader<Reader>;

template <TiffFormatType TiffFormat, std::endian StorageEndian>
using TagType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                   TiffTag<StorageEndian>, 
                                   TiffBigTag<StorageEndian>>;

/// Parse a single scalar value (arithmetic or enum)
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::element_type> parse_single_scalar(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>;

/// Parse a single rational (Rational or SRational type)
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::element_type> parse_single_rational(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>;

/// Parse a container of scalars
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_scalar_container(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>;

/// Parse a container of rationals
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_rational_container(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>;

/// Unified parse_tag_value using TagDescriptor properties
template <typename TagDesc, typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian = std::endian::native, std::endian TargetEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_tag_value(
    const TagType<TiffFormat, SourceEndian>& tag,
    const Reader& reader) noexcept 
    requires RawReader<Reader>;

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
    requires RawReader<Reader>;

// Helper to convert from parsed alternate type to target type
template <typename TargetType, typename SourceType>
inline TargetType convert_promoted_value(const SourceType& source);

template <RawReader Reader, typename TagDesc, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian TargetEndian = std::endian::native, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_tag(const Reader& reader, const TagType<TiffFormat, SourceEndian>& tag) noexcept;

} // namespace parsing

} // namespace tiffconcept

#define TIFFCONCEPT_PARSING_HEADER
#include "impl/parsing_impl.hpp"