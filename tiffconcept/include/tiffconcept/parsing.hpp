#pragma once

#include <array>
#include <bit>
#include <cstring>
#include <span>
#include <vector>
#include "types/result.hpp"
#include "reader_base.hpp"
#include "types/tag_spec.hpp"
#include "types/tiff_spec.hpp"

namespace tiffconcept {

namespace parsing {

template <TiffFormatType TiffFormat, std::endian StorageEndian>
using TagType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                   TiffTag<StorageEndian>, 
                                   TiffBigTag<StorageEndian>>;



/**
 * @brief Parse a TIFF tag entry into its native C++ representation
 * 
 * This function extracts from the target file and the pre-extracted structure (from IFD's tags field)
 * the TIFF tag into the structured C++ value defined by your Tag Descriptor. Endianess conversion,
 * type checking, promotion and data retrieval (inline or external) are all handled automatically.
 * 
 * You shouldn't be needing to call this function directly; instead, use higher-level functions
 * such as ExtractedTags.
 * 
 * ## Overview
 * 
 * The function performs several key operations:
 * - **Tag validation**: Verifies tag code matches the descriptor
 * - **Data type checking**: Validates or promotes the tag's data type
 * - **Inline/external data handling**: Reads data from tag inline storage or external file offset
 * - **Endianness conversion**: Converts from source to target byte order
 * - **Type conversion**: Maps TIFF types to C++ types (e.g., TIFF LONG → uint32_t)
 * 
 * ## Template Parameters
 * 
 * @tparam Reader The file reader type (must satisfy `RawReader` concept)
 * @tparam TagDesc Tag descriptor type defining the expected tag structure
 *                 Must include: `code`, `datatype`, `value_type`, and optionally `alternate_types`
 * @tparam TiffFormat TIFF format variant (Classic or BigTIFF)
 * @tparam TargetEndian Target byte order for the parsed value (usually std::endian::native)
 * @tparam SourceEndian Source byte order of the TIFF file (little or big endian)
 * 
 * ## Parameters
 * 
 * @param reader File reader providing access to TIFF data (for external tag values)
 * @param tag The TIFF tag entry to parse (from IFD tag array)
 * 
 * ## Return Value
 * 
 * @return Result<typename TagDesc::value_type> containing:
 *         - On success: The parsed value in native C++ type
 *         - On error: Error with detailed diagnostic information
 * 
 * ## Error Conditions
 * 
 * @retval Error::Code::InvalidTag Tag code doesn't match descriptor, or count is invalid for scalar types
 * @retval Error::Code::InvalidTagType Data type mismatch with no valid type promotion available
 * @retval Error::Code::ReadError Failed to read external tag data from file
 * @retval Error::Code::UnexpectedEndOfFile File ended while reading external tag data
 * 
 * ## Supported Data Types
 * 
 * The function handles all TIFF data types and C++ mappings:
 * 
 * | TIFF Type | C++ Type | Notes |
 * |-----------|----------|-------|
 * | BYTE | uint8_t | 8-bit unsigned |
 * | ASCII | std::string | NUL-terminated text |
 * | SHORT | uint16_t | 16-bit unsigned |
 * | LONG | uint32_t | 32-bit unsigned |
 * | RATIONAL | Rational | Two uint32_t (numerator/denominator) |
 * | SBYTE | int8_t | 8-bit signed |
 * | UNDEFINED | uint8_t | Uninterpreted bytes |
 * | SSHORT | int16_t | 16-bit signed |
 * | SLONG | int32_t | 32-bit signed |
 * | SRATIONAL | SRational | Two int32_t (numerator/denominator) |
 * | FLOAT | float | 32-bit IEEE float |
 * | DOUBLE | double | 64-bit IEEE double |
 * | LONG8 | uint64_t | 64-bit unsigned (BigTIFF) |
 * | SLONG8 | int64_t | 64-bit signed (BigTIFF) |
 * 
 * ## Type Promotion
 * 
 * When the tag's actual data type differs from the descriptor's expected type,
 * automatic type promotion may occur if:
 * - The tag descriptor specifies `alternate_types` array
 * - The actual type is listed in alternate_types
 * - The conversion is widening (e.g., SHORT → LONG)
 * 
 * Common promotions:
 * - Integer widening: BYTE → SHORT → LONG → LONG8
 * - Signed variants: SBYTE → SSHORT → SLONG → SLONG8
 * - Floating point: FLOAT → DOUBLE
 * 
 * ## Inline vs External Storage
 * 
 * TIFF tags store values either inline (within the tag entry) or externally (at a file offset):
 * - **Classic TIFF**: Values ≤ 4 bytes are inline
 * - **BigTIFF**: Values ≤ 8 bytes are inline
 * - Larger values stored externally, tag contains file offset
 * 
 * The function automatically handles both cases transparently.
 * 
 * ## Examples
 * 
 * ### Parse a scalar tag (ImageWidth)
 * @code{.cpp}
 * using namespace tiffconcept;
 * 
 * // Read tag from IFD
 * TiffTag<std::endian::little> width_tag = //... from IFD ... //;
 * 
 * // Parse as uint32_t
 * using WidthTag = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
 * auto result = parsing::parse_tag<MmapFileReader, WidthTag, 
 *                                   TiffFormatType::Classic, 
 *                                   std::endian::native, 
 *                                   std::endian::little>(reader, width_tag);
 * 
 * if (result) {
 *     uint32_t width = result.value();
 *     std::cout << "Image width: " << width << "\n";
 * }
 * @endcode
 * 
 * ### Parse a rational tag (XResolution)
 * @code{.cpp}
 * using XResTag = TagDescriptor<TagCode::XResolution, TiffDataType::Rational, Rational>;
 * auto result = parsing::parse_tag<MmapFileReader, XResTag>(reader, xres_tag);
 * 
 * if (result) {
 *     const Rational& xres = result.value();
 *     double dpi = static_cast<double>(xres.numerator) / xres.denominator;
 *     std::cout << "X Resolution: " << dpi << " dpi\n";
 * }
 * @endcode
 * 
 * ### Parse a string tag (Software)
 * @code{.cpp}
 * using SoftwareTag = TagDescriptor<TagCode::Software, TiffDataType::Ascii, std::string>;
 * auto result = parsing::parse_tag<MmapFileReader, SoftwareTag>(reader, software_tag);
 * 
 * if (result) {
 *     std::cout << "Created by: " << result.value() << "\n";
 * }
 * @endcode
 * 
 * ### Parse an array tag (StripOffsets)
 * @code{.cpp}
 * using StripOffsetsTag = TagDescriptor<TagCode::StripOffsets, TiffDataType::Long, 
 *                                       std::vector<uint32_t>>;
 * auto result = parsing::parse_tag<MmapFileReader, StripOffsetsTag>(reader, offsets_tag);
 * 
 * if (result) {
 *     for (uint32_t offset : result.value()) {
 *         std::cout << "Strip at offset: " << offset << "\n";
 *     }
 * }
 * @endcode
 * 
 * ### Handle type promotion
 * @code{.cpp}
 * // Descriptor expects LONG but allows SHORT as alternate
 * using WidthTag = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t,
 *                                 std::array{TiffDataType::Short}>;
 * 
 * // If tag contains SHORT data, it's automatically promoted to uint32_t
 * auto result = parsing::parse_tag<MmapFileReader, WidthTag>(reader, width_tag);
 * // Returns uint32_t regardless of whether source was SHORT or LONG
 * @endcode
 * 
 * ## Implementation Notes
 * 
 * - **Performance**: Minimizes allocations; inline data accessed via memcpy
 * - **Memory safety**: All buffer accesses are bounds-checked
 * - **Endianness**: Handles byte swapping for all multi-byte types
 * - **ASCII strings**: Trailing NUL bytes are trimmed automatically
 * - **Rationals**: Stored as two consecutive integers (numerator, denominator)
 * 
 * @note All operations are noexcept; errors returned via Result type
 * @note String arrays (multiple NUL-separated strings) are supported via std::vector<std::string>
 * 
 * @see TagDescriptor for creating tag descriptors
 * @see ExtractedTags for batch tag extraction from IFDs
 * @see read_ifd for reading complete IFD structures
 */
template <RawReader Reader, typename TagDesc, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian TargetEndian = std::endian::native, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<typename TagDesc::value_type> parse_tag(const Reader& reader, const TagType<TiffFormat, SourceEndian>& tag) noexcept;

} // namespace parsing

} // namespace tiffconcept

#define TIFFCONCEPT_PARSING_HEADER
#include "impl/parsing_impl.hpp"