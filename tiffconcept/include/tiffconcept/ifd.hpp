#pragma once

/**
 * @file ifd.hpp
 * @brief Image File Directory (IFD) structures and operations for TIFF files
 * 
 * This header provides types and functions for reading and writing TIFF IFDs (Image File Directories).
 * An IFD is a fundamental component of TIFF files that contains metadata tags and pointers to image data.
 * 
 * ## Key Concepts
 * 
 * - **IFD Structure**: A TIFF IFD consists of:
 *   - An entry count (2 bytes for Classic TIFF, 8 bytes for BigTIFF)
 *   - An array of tag entries
 *   - An offset to the next IFD (forming a linked list)
 * 
 * - **IFDOffset**: A strongly-typed wrapper for file offsets pointing to IFDs
 * 
 * - **IFDDescription**: Lightweight header information about an IFD (offset and entry count)
 * 
 * - **IFD**: Complete IFD structure including all tags and next IFD offset
 * 
 * ## Example Usage
 * 
 * @code{.cpp}
 * using namespace tiffconcept;
 * using namespace tiffconcept::ifd;
 * 
 * // Open a TIFF file
 * MmapFileReader reader("image.tif");
 * 
 * // Get the first IFD offset from the file header
 * auto first_offset_result = get_first_ifd_offset<MmapFileReader, TiffFormatType::Classic, std::endian::little>(reader);
 * if (!first_offset_result) {
 *     // Handle error
 *     return;
 * }
 * 
 * // Read the complete IFD
 * auto ifd_result = read_ifd<MmapFileReader, TiffFormatType::Classic, std::endian::little>(reader, first_offset_result.value());
 * if (!ifd_result) {
 *     // Handle error
 *     return;
 * }
 * 
 * const auto& ifd = ifd_result.value();
 * std::cout << "IFD has " << ifd.num_tags() << " tags\n";
 * 
 * // Iterate through the IFD chain
 * IFDOffset current_offset = ifd.next_ifd_offset;
 * while (current_offset) {
 *     auto next_ifd_result = read_ifd<MmapFileReader, TiffFormatType::Classic, std::endian::little>(reader, current_offset);
 *     if (!next_ifd_result) break;
 *     
 *     current_offset = next_ifd_result.value().next_ifd_offset;
 * }
 * @endcode
 * 
 * @note All functions are noexcept and use Result<T> for error handling
 * @note IFD operations support both Classic TIFF and BigTIFF formats via template parameters
 */

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

/**
 * @brief Strongly-typed IFD offset in the file
 * 
 * A type-safe wrapper around a file offset that points to an IFD.
 * This prevents mixing IFD offsets with other integer values.
 * 
 * An offset of 0 represents a null offset (end of IFD chain).
 */
struct IFDOffset {
    std::size_t value;  ///< The actual file offset in bytes
    
    /**
     * @brief Default constructor - creates a null offset (0)
     */
    constexpr IFDOffset() noexcept : value(0) {}
    
    /**
     * @brief Construct an IFD offset from a file position
     * @param offset File offset in bytes
     */
    constexpr explicit IFDOffset(std::size_t offset) noexcept : value(offset) {}
    
    /**
     * @brief Check if this is a null offset
     * @return true if offset is 0 (null), false otherwise
     */
    [[nodiscard]] constexpr bool is_null() const noexcept { return value == 0; }
    
    /**
     * @brief Check if this is a valid (non-null) offset
     * @return true if offset is non-zero, false otherwise
     */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
    
    /**
     * @brief Equality comparison
     */
    [[nodiscard]] constexpr bool operator==(const IFDOffset& other) const noexcept = default;
    
    /**
     * @brief Three-way comparison
     */
    [[nodiscard]] constexpr auto operator<=>(const IFDOffset& other) const noexcept = default;
};

/**
 * @brief Represents an IFD with its parsed header information
 * 
 * This structure contains lightweight metadata about an IFD without the actual tag data.
 * It's useful when you only need to know where an IFD is and how many tags it contains.
 * 
 * @tparam TiffFormat The TIFF format (Classic or BigTIFF)
 */
template <TiffFormatType TiffFormat = TiffFormatType::Classic>
struct IFDDescription {
    IFDOffset offset;                ///< File offset of this IFD
    std::size_t num_entries;         ///< Number of tag entries in this IFD
    
    /**
     * @brief Default constructor - creates an empty IFD description
     */
    constexpr IFDDescription() noexcept 
        : offset(), num_entries(0) {}
    
    /**
     * @brief Construct an IFD description with offset and entry count
     * @param offset_ File offset where the IFD is located
     * @param num_entries_ Number of tag entries in the IFD
     */
    constexpr IFDDescription(IFDOffset offset_, std::size_t num_entries_) noexcept
        : offset(offset_), num_entries(num_entries_) {}
    
    /**
     * @brief Get offset type for this TIFF format
     * 
     * Classic TIFF uses uint32_t offsets, BigTIFF uses uint64_t offsets.
     */
    using OffsetType = std::conditional_t<TiffFormat == TiffFormatType::Classic, uint32_t, uint64_t>;
    
    /**
     * @brief Get IFD header type for this TIFF format
     * @tparam SourceEndian The endianness of the TIFF file
     */
    template <std::endian SourceEndian>
    using HeaderType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                          IFDHeader<SourceEndian>, 
                                          IFDBigHeader<SourceEndian>>;
    
    /**
     * @brief Get tag type for this TIFF format
     * @tparam SourceEndian The endianness of the TIFF file
     */
    template <std::endian SourceEndian>
    using TagType = std::conditional_t<TiffFormat == TiffFormatType::Classic, 
                                       TiffTag<SourceEndian>, 
                                       TiffBigTag<SourceEndian>>;
};

/**
 * @brief Complete IFD structure with description, tags, and next IFD offset
 * 
 * This structure represents a fully parsed IFD including all tag entries and
 * the offset to the next IFD in the chain (if any).
 * 
 * IFDs form a linked list in TIFF files, where each IFD points to the next one
 * (or has a null offset if it's the last one).
 * 
 * @tparam TiffFormat The TIFF format (Classic or BigTIFF)
 * @tparam SourceEndian The endianness of the TIFF file (little or big)
 * 
 * @note Tags are stored in their on-disk format (with SourceEndian byte order)
 */
template <TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
struct IFD {
    IFDDescription<TiffFormat> description;                             ///< IFD metadata (offset, entry count)
    std::vector<parsing::TagType<TiffFormat, SourceEndian>> tags;       ///< Array of tag entries
    IFDOffset next_ifd_offset;                                          ///< Offset to next IFD (0 if last)
    
    /**
     * @brief Default constructor - creates an empty IFD
     */
    IFD() noexcept : description(), tags(), next_ifd_offset() {}
    
    /**
     * @brief Constructor with IFD description
     * @param desc IFD description (offset and entry count)
     */
    explicit IFD(const IFDDescription<TiffFormat>& desc) noexcept 
        : description(desc), tags(), next_ifd_offset() {}
    
    /**
     * @brief Constructor with all components
     * @param desc IFD description (offset and entry count)
     * @param tags_ Tag entries (moved)
     * @param next_offset Offset to next IFD in chain
     */
    IFD(const IFDDescription<TiffFormat>& desc, 
        std::vector<parsing::TagType<TiffFormat, SourceEndian>>&& tags_,
        IFDOffset next_offset) noexcept 
        : description(desc), tags(std::move(tags_)), next_ifd_offset(next_offset) {}

    /**
     * @brief Get number of tags in this IFD
     * @return Number of tag entries
     */
    [[nodiscard]] std::size_t num_tags() const noexcept {
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
    [[nodiscard]] constexpr static std::size_t size_in_bytes(std::size_t num_tags) noexcept {
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
    [[nodiscard]] std::size_t size_in_bytes() const noexcept {
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
    [[nodiscard]] Result<void> write(std::span<std::byte> buffer) const noexcept;

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
    [[nodiscard]] std::vector<std::byte> write() const noexcept;
};

/**
 * @brief Get the first IFD offset from a TIFF file header
 * 
 * Reads the TIFF file header and extracts the offset to the first IFD.
 * This is the entry point for reading IFD chains in a TIFF file.
 * 
 * @tparam Reader The reader type (must satisfy RawReader concept)
 * @tparam TiffFormat TIFF format type (Classic or BigTIFF)
 * @tparam SourceEndian Byte order of the TIFF file (little or big endian)
 * 
 * @param reader The file reader to use
 * @return Result containing the first IFD offset, or an error
 * 
 * @throws None (noexcept)
 * @retval Error::Code::ReadError Failed to read the TIFF header from file
 * @retval Error::Code::UnexpectedEndOfFile File is too small to contain a valid header
 * @retval Error::Code::InvalidHeader TIFF header magic number or format is invalid
 * 
 * @note The reader must be positioned at the start of a valid TIFF file
 * @note For multi-page TIFF files, this returns only the first IFD offset
 */
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFDOffset> get_first_ifd_offset(const Reader& reader) noexcept;

/**
 * @brief Read an IFD header and return its description
 * 
 * Reads only the IFD header (entry count) without reading the tags themselves.
 * This is useful when you need to know how many tags an IFD contains before
 * allocating memory or when you want to skip reading the full IFD.
 * 
 * @tparam Reader The reader type (must satisfy RawReader concept)
 * @tparam TiffFormat TIFF format type (Classic or BigTIFF)
 * @tparam SourceEndian Byte order of the TIFF file (little or big endian)
 * 
 * @param reader The file reader to use
 * @param offset File offset where the IFD is located
 * @return Result containing the IFD description, or an error
 * 
 * @throws None (noexcept)
 * @retval Error::Code::ReadError Failed to read IFD header from file
 * @retval Error::Code::UnexpectedEndOfFile File ended unexpectedly while reading header
 * @retval Error::Code::OutOfBounds Offset is beyond the file size
 */
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFDDescription<TiffFormat>> read_ifd_header(const Reader& reader, IFDOffset offset) noexcept;

/**
 * @brief Get offset to next IFD in the chain
 * 
 * Reads the "next IFD" offset field that follows the tag entries in an IFD.
 * This offset forms the linked list structure of TIFF IFDs.
 * 
 * @tparam Reader The reader type (must satisfy RawReader concept)
 * @tparam TiffFormat TIFF format type (Classic or BigTIFF)
 * @tparam SourceEndian Byte order of the TIFF file (little or big endian)
 * 
 * @param reader The file reader to use
 * @param ifd_desc Description of the IFD (must contain correct offset and entry count)
 * @return Result containing the next IFD offset (may be null/0 if last IFD), or an error
 * 
 * @throws None (noexcept)
 * @retval Error::Code::ReadError Failed to read next IFD offset from file
 * @retval Error::Code::UnexpectedEndOfFile File ended unexpectedly while reading offset
 * @retval Error::Code::OutOfBounds Calculated offset position is beyond the file size
 * 
 * @note A return value of IFDOffset(0) indicates this is the last IFD in the chain
 * @note The position of the next offset field is calculated as:
 *       IFD_offset + header_size + (num_entries * tag_size)
 */
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFDOffset> read_next_ifd_offset(
    const Reader& reader, 
    const IFDDescription<TiffFormat>& ifd_desc) noexcept;

/**
 * @brief Read tags array from an IFD and return next IFD offset
 * 
 * Reads all tag entries from an IFD into a provided vector and also returns
 * the offset to the next IFD in the chain.
 * 
 * @tparam Reader The reader type (must satisfy RawReader concept)
 * @tparam TiffFormat TIFF format type (Classic or BigTIFF)
 * @tparam SourceEndian Byte order of the TIFF file (little or big endian)
 * 
 * @param reader The file reader to use
 * @param ifd_desc Description of the IFD to read
 * @param[out] out_tags Vector that will be filled with tag entries (resized as needed)
 * @return Result containing the next IFD offset, or an error
 * 
 * @throws None (noexcept)
 * @retval Error::Code::ReadError Failed to read tag data from file
 * @retval Error::Code::UnexpectedEndOfFile File ended unexpectedly while reading tags
 * @retval Error::Code::OutOfBounds Tag data position is beyond the file size
 * 
 * @note The out_tags vector is cleared if the IFD has no entries
 * @note Tags are stored in their on-disk format (SourceEndian byte order)
 * @note This function reads both the tags and the next IFD offset in one operation
 */
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFDOffset> read_ifd_tags(
    const Reader& reader,
    const IFDDescription<TiffFormat>& ifd_desc,
    std::vector<parsing::TagType<TiffFormat, SourceEndian>>& out_tags) noexcept;

/**
 * @brief Read a complete IFD into a preallocated IFD struct
 * 
 * Reads a complete IFD (header, tags, and next offset) into an existing IFD structure.
 * This function reuses the tag vector allocation if it has sufficient capacity,
 * which can improve performance when reading multiple IFDs.
 * 
 * @tparam Reader The reader type (must satisfy RawReader concept)
 * @tparam TiffFormat TIFF format type (Classic or BigTIFF)
 * @tparam SourceEndian Byte order of the TIFF file (little or big endian)
 * 
 * @param reader The file reader to use
 * @param offset File offset where the IFD is located
 * @param[out] ifd IFD structure to populate (will be modified)
 * @return Ok() on success, or Error on failure
 * 
 * @throws None (noexcept)
 * @retval Error::Code::ReadError Failed to read IFD data from file
 * @retval Error::Code::UnexpectedEndOfFile File ended unexpectedly while reading IFD
 * @retval Error::Code::OutOfBounds IFD offset is beyond the file size
 * 
 * @note This function performs optimized bulk reading of tags and next offset
 * @note The ifd parameter's tag vector will be resized as needed
 * @note All previous contents of ifd will be replaced
 */
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<void> read_ifd_into(const Reader& reader, IFDOffset offset, IFD<TiffFormat, SourceEndian>& ifd) noexcept;

/**
 * @brief Read a complete IFD and return a new IFD struct
 * 
 * Reads a complete IFD (header, tags, and next offset) from the file and
 * returns it as a new IFD structure.
 * 
 * This is a convenience wrapper around read_ifd_into() that creates a new
 * IFD structure for you.
 * 
 * @tparam Reader The reader type (must satisfy RawReader concept)
 * @tparam TiffFormat TIFF format type (Classic or BigTIFF)
 * @tparam SourceEndian Byte order of the TIFF file (little or big endian)
 * 
 * @param reader The file reader to use
 * @param offset File offset where the IFD is located
 * @return Result containing the complete IFD, or an error
 * 
 * @throws None (noexcept)
 * @retval Error::Code::ReadError Failed to read IFD data from file
 * @retval Error::Code::UnexpectedEndOfFile File ended unexpectedly while reading IFD
 * @retval Error::Code::OutOfBounds IFD offset is beyond the file size
 * 
 * @note For reading multiple IFDs, consider using read_ifd_into() to reuse allocations
 * @note The returned IFD contains all tag entries in their on-disk format
 * 
 * @par Example:
 * @code
 * auto ifd_result = read_ifd<MyReader, TiffFormatType::Classic, std::endian::little>(reader, offset);
 * if (ifd_result) {
 *     const auto& ifd = ifd_result.value();
 *     // Use the IFD...
 * }
 * @endcode
 */
template <RawReader Reader, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian SourceEndian = std::endian::native>
[[nodiscard]] Result<IFD<TiffFormat, SourceEndian>> read_ifd(const Reader& reader, IFDOffset offset) noexcept; 

} // namespace ifd

} // namespace tiffconcept

// Include implementation
#define TIFFCONCEPT_IFD_HEADER
#include "impl/ifd_impl.hpp"