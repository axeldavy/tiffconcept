#pragma once

/**
 * @file ifd_builder.hpp
 * @brief IFD (Image File Directory) builder for constructing TIFF metadata structures
 * 
 * This header provides the IFDBuilder class for creating TIFF IFDs from extracted tags
 * and custom data arrays. It handles the complex details of tag encoding, inline vs
 * external data placement, and offset management.
 * 
 * ## Key Concepts
 * 
 * - **Inline vs External Data**: Small tag values (≤4 bytes for Classic TIFF, ≤8 bytes
 *   for BigTIFF) are stored directly in the tag entry. Larger values are stored externally
 *   with an offset in the tag entry.
 * 
 * - **Tag Sorting**: Tags are automatically sorted by tag code as required by the TIFF
 *   specification.
 * 
 * - **Offset Management**: The builder tracks file offsets for IFD placement and external
 *   data, updating tag offsets appropriately.
 * 
 * ## Example Usage
 * 
 * @code{.cpp}
 * using namespace tiffconcept;
 * 
 * // Create builder with IFD placement strategy
 * IFDBuilder<TiffFormatType::Classic, std::endian::little> builder;
 * 
 * // Add tags from extracted metadata
 * auto result = builder.add_tags(extracted_tags);
 * if (!result) {
 *     // Handle error
 * }
 * 
 * // Add additional computed arrays
 * std::vector<uint64_t> tile_offsets = {1024, 2048, 4096};
 * builder.add_tag<TileOffsetsTag>(tile_offsets);
 * 
 * // Build and write to file
 * auto ifd_offset_result = builder.write_to_file(writer, current_file_size);
 * @endcode
 * 
 * @note All operations are noexcept and use Result<T> for error handling
 * @note The builder maintains sorted tag order automatically
 */

#include <algorithm>
#include <bit>
#include <cstring>
#include <optional>
#include <span>
#include <vector>
#include "ifd.hpp"
#include "result.hpp"
#include "tag_extraction.hpp"
#include "tag_spec.hpp"
#include "tag_writing.hpp"
#include "types.hpp"
#include "strategy/write_strategy.hpp"

namespace tiffconcept {

/**
 * @brief Builds an IFD from ExtractedTags and computed arrays
 * 
 * IFDBuilder constructs TIFF Image File Directories by encoding tag metadata
 * and managing the layout of tag data in the file. It handles:
 * - Inline vs external data placement based on value size
 * - Automatic tag sorting by tag code
 * - Offset calculation and management
 * - Integration with IFD placement strategies
 * 
 * The builder operates in two phases:
 * 1. **Construction**: Add tags and build internal representation
 * 2. **Writing**: Calculate offsets and write IFD + external data to file
 * 
 * @tparam TiffFormat TIFF format type (Classic or BigTIFF)
 * @tparam TargetEndian Target byte order for written data (little or big endian)
 * @tparam IFDPlacement Strategy for IFD placement in file (default: IFDAtEnd)
 * 
 * @note The builder can be reused via clear() for building multiple IFDs
 * @note Thread-safe: No - use separate builders per thread
 */
template <
    TiffFormatType TiffFormat = TiffFormatType::Classic,
    std::endian TargetEndian = std::endian::native,
    typename IFDPlacement = IFDAtEnd
>
    requires IFDPlacementStrategy<IFDPlacement>
class IFDBuilder {
public:
    using OffsetType = typename ifd::IFDDescription<TiffFormat>::OffsetType;  ///< Offset type (uint32_t or uint64_t)
    using TagType = typename ifd::IFDDescription<TiffFormat>::template TagType<TargetEndian>;  ///< Tag entry type
    using IFDType = ifd::IFD<TiffFormat, TargetEndian>;  ///< Complete IFD type
    
private:
    IFDPlacement placement_strategy_;
    std::vector<TagType> tags_;
    std::vector<std::byte> external_data_;
    std::size_t external_data_offset_ = 0;
    ifd::IFDOffset next_ifd_offset_;
    
    /// Helper to create a tag entry (implementation in ifd_builder_impl.hpp)
    template <typename TagDesc, typename ValueType>
    [[nodiscard]] Result<TagType> create_tag_entry(
        const ValueType& value,
        std::size_t& current_external_offset) noexcept;
    
public:
    /**
     * @brief Default constructor
     */
    IFDBuilder() = default;
    
    /**
     * @brief Construct with explicit placement strategy
     * @param placement IFD placement strategy instance
     */
    explicit IFDBuilder(IFDPlacement placement);
    
    /**
     * @brief Build IFD from extracted tags
     * 
     * Processes all tags from an ExtractedTags structure, encoding each according
     * to its type and size. Small values are stored inline in tag entries, while
     * large values are appended to the external data buffer.
     * 
     * Tags are automatically sorted by tag code after addition.
     * 
     * @tparam Args Tag descriptor types in the TagSpec
     * @param extracted_tags Tags extracted from a TIFF file or constructed manually
     * @return Ok() on success, or Error on failure
     * 
     * @throws None (noexcept)
     * @retval Error::Code::InvalidTag Tag encoding failed
     * @retval Error::Code::OutOfBounds Data buffer overflow
     * 
     * @note Optional tags that are not set (nullopt) are skipped
     * @note Call this before add_tag() for additional custom tags
     */
    template <typename... Args>
    [[nodiscard]] Result<void> add_tags(const ExtractedTags<Args...>& extracted_tags) noexcept;
    
    /**
     * @brief Add a single tag manually
     * 
     * Adds a single tag with a custom value. Useful for adding computed arrays
     * like TileOffsets, TileByteCounts, etc. that are not part of extracted metadata.
     * 
     * @tparam TagDesc Tag descriptor type (e.g., TileOffsetsTag)
     * @tparam ValueType Type of the value (deduced from parameter)
     * @param value Tag value (scalar, container, or string)
     * @return Ok() on success, or Error on failure
     * 
     * @throws None (noexcept)
     * @retval Error::Code::InvalidTag Tag encoding failed
     * @retval Error::Code::OutOfBounds Data buffer overflow
     * 
     * @note Tags are automatically re-sorted after addition
     * @note Can be called multiple times to add multiple tags
     * 
     * @par Example:
     * @code
     * std::vector<uint64_t> offsets = {1024, 2048, 4096};
     * builder.add_tag<TileOffsetsTag>(offsets);
     * @endcode
     */
    template <typename TagDesc, typename ValueType>
    [[nodiscard]] Result<void> add_tag(const ValueType& value) noexcept;
    
    /**
     * @brief Set the file offset where external data will be written
     * 
     * Must be called before adding tags if you want to control where external
     * tag data is placed in the file.
     * 
     * @param offset File offset for external data
     * 
     * @note This sets the base offset; actual offsets in tags will be relative to this
     * @note Call this before add_tags() or add_tag()
     */
    void set_external_data_offset(std::size_t offset) noexcept;
    
    /**
     * @brief Set the next IFD offset for IFD chains
     * 
     * Sets the "next IFD" pointer that forms linked lists of IFDs in multi-page
     * TIFF files or pyramid structures.
     * 
     * @param offset Offset to next IFD (use IFDOffset(0) for last IFD)
     * 
     * @note A null offset (0) indicates this is the last IFD in the chain
     */
    void set_next_ifd_offset(ifd::IFDOffset offset) noexcept;
    
    /**
     * @brief Calculate total size needed for IFD and external data
     * 
     * Returns the combined size of the IFD structure (header + tags + next offset)
     * and all external tag data.
     * 
     * @return Total size in bytes
     * 
     * @note Use this to allocate file space before writing
     */
    [[nodiscard]] std::size_t calculate_total_size() const noexcept;
    
    /**
     * @brief Get size of just the IFD array
     * 
     * Returns the size of the IFD structure without external data.
     * This includes the entry count, all tag entries, and the next IFD offset.
     * 
     * @return IFD size in bytes
     */
    [[nodiscard]] std::size_t calculate_ifd_size() const noexcept;
    
    /**
     * @brief Get size of external data buffer
     * 
     * Returns the total size of all tag data that doesn't fit inline.
     * 
     * @return External data size in bytes
     */
    [[nodiscard]] std::size_t calculate_external_data_size() const noexcept;
    
    /**
     * @brief Build the IFD structure
     * 
     * Creates an IFD object with all tags. This moves the internal tag vector,
     * so the builder should not be used for writing after this call unless cleared.
     * 
     * @param ifd_offset File offset where IFD will be located
     * @return Result containing the built IFD, or an error
     * 
     * @throws None (noexcept)
     * 
     * @note This moves the internal state; call clear() to reuse the builder
     * @note Tag offsets are relative and need updating before file writing
     */
    [[nodiscard]] Result<IFDType> build(ifd::IFDOffset ifd_offset) noexcept;
    
    /**
     * @brief Get external data buffer for manual writing
     * 
     * Returns a read-only view of the external data buffer. Use this if you
     * want to write the IFD and external data separately.
     * 
     * @return Span over external data bytes
     * 
     * @note The data is in target endian byte order
     */
    [[nodiscard]] std::span<const std::byte> get_external_data() const noexcept;
    
    /**
     * @brief Write IFD and external data to file
     * 
     * This is the primary method for writing a complete IFD to a file.
     * It handles:
     * 1. Calculating IFD and external data offsets using the placement strategy
     * 2. Resizing the file if needed
     * 3. Updating tag offsets to absolute file positions
     * 4. Writing the IFD structure
     * 5. Writing all external tag data
     * 
     * @tparam Writer The writer type (must satisfy RawWriter concept)
     * @param writer File writer to use
     * @param current_file_size Current size of the file before writing
     * @return Result containing the IFD's file offset, or an error
     * 
     * @throws None (noexcept)
     * @retval Error::Code::WriteError Failed to write IFD or external data
     * @retval Error::Code::IOError File resize failed
     * 
     * @note After this call, the builder's state is consumed (tags are moved)
     * @note The returned offset should be used to update IFD chain pointers
     * 
     * @par Example:
     * @code
     * auto ifd_offset_result = builder.write_to_file(writer, writer.size().value());
     * if (ifd_offset_result) {
     *     // Update previous IFD's next_ifd pointer to ifd_offset_result.value()
     * }
     * @endcode
     */
    template <typename Writer>
        requires RawWriter<Writer>
    [[nodiscard]] Result<ifd::IFDOffset> write_to_file(
        Writer& writer,
        std::size_t current_file_size) noexcept;
    
    /**
     * @brief Clear builder state for reuse
     * 
     * Resets all internal state, allowing the builder to be reused for
     * constructing another IFD.
     * 
     * @note Call this after write_to_file() if you want to build another IFD
     */
    void clear() noexcept;
};

} // namespace tiffconcept

// Include implementation
#define TIFFCONCEPT_IFD_BUILDER_HEADER
#include "impl/ifd_builder_impl.hpp"