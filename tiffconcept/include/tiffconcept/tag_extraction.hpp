#pragma once

#include <algorithm>
#include <tuple>
#include <utility>
#include "ifd.hpp"
#include "parsing.hpp"
#include "types/result.hpp"
#include "types/tag_spec.hpp"
#include "lowlevel/tag_writing.hpp"
#include "types/tiff_spec.hpp"

namespace tiffconcept {

/// @brief Container for extracted TIFF tag values
/// @tparam Args Either a TagSpec or a list of tag descriptors
/// 
/// @note Provides compile-time access to tag values by TagCode
/// @note Supports both required and optional tags
/// @note Handles tag extraction from IFD with O(n+m) sorted algorithm
/// @note Provides utilities for writing tags back to TIFF files
/// 
/// Example usage:
/// @code
/// ExtractedTags<ImageWidthTag, ImageLengthTag, OptTag_t<CompressionTag>> tags;
/// auto result = tags.extract<Reader, TiffFormatType::Classic, std::endian::little>(reader, tag_buffer);
/// if (result) {
///     uint32_t width = tags.get<TagCode::ImageWidth>();
///     auto compression = tags.get<TagCode::Compression>(); // std::optional<CompressionScheme>
/// }
/// @endcode
template <typename... Args>
struct ExtractedTags;

namespace optional {
    /// @brief Extract a tag value or return a default
    /// @tparam Code Tag code to extract
    /// @tparam TagSpec Tag specification type
    /// @tparam DefaultType Type of default value
    /// @param metadata ExtractedTags container
    /// @param default_value Default to return if tag is not present
    /// @return Extracted tag value or default
    template <TagCode Code, typename TagSpec, typename DefaultType>
    [[nodiscard]] constexpr auto extract_tag_or(
        const ExtractedTags<TagSpec>& metadata,
        DefaultType&& default_value) noexcept;

} // namespace optional

// Forward declaration for ExtractedTags specialization
namespace detail {
    // Helper to deduce TagSpec from arguments
    // Case 1: Arguments are a list of TagDescriptors -> Wrap in TagSpec
    template <typename... Args>
    struct GetSpec {
        using type = TagSpec<Args...>;
    };

    // Case 2: Argument is already a TagSpec -> Use as is
    template <typename... Tags>
    struct GetSpec<TagSpec<Tags...>> {
        using type = TagSpec<Tags...>;
    };

    //template <typename... Tags>
    //struct ExtractedTagsImpl<TagSpec<Tags...>>;
    template <typename... Tags>
    struct ExtractedTagsImpl;
}

/// @brief Container for extracted TIFF tag values
/// @tparam Args Either a TagSpec or a list of tag descriptors
/// 
/// Specialization that provides:
/// - Compile-time tag access by TagCode
/// - Tag extraction from IFD
/// - Tag writing utilities
/// - Memory reuse for optional tags
template <typename... Args>
struct ExtractedTags {
private:
    // Resolve the effective TagSpec type from arguments
    using Spec = typename detail::GetSpec<Args...>::type;

    static_assert(ValidTagSpec<Spec>, "ExtractedTags requires a valid TagSpec or list of TagDescriptors");

    using Implementation = detail::ExtractedTagsImpl<Spec>;

public:
    /// Storage for tag values
    typename Implementation::Storage values;

    /// TagSpec
    using tag_spec_type = Spec;

    /// @brief Clear all tag values
    /// @note Optional tags are set to std::nullopt
    /// @note Required tags are cleared (calling .clear() if available) for memory reuse
    constexpr void clear() noexcept;

    /// @brief Get value by tag code at compile time
    /// @tparam Code Tag code to access
    /// @return Reference to the tag value (mutable)
    /// @note For optional tags, returns std::optional<T>&
    /// @note For required tags, returns T&
    template <TagCode Code>
    [[nodiscard]] constexpr decltype(auto) get() noexcept;
    
    /// @brief Get value by tag code at compile time (const version)
    /// @tparam Code Tag code to access
    /// @return Const reference to the tag value
    template <TagCode Code>
    [[nodiscard]] constexpr decltype(auto) get() const noexcept;

    /// @brief Extract tags from an IFD into this structure
    /// @tparam Reader Reader type implementing RawReader concept
    /// @tparam TiffFormat TIFF format type (Classic or BigTIFF)
    /// @tparam SourceEndian Endianness of the stored tags
    /// @param reader Reader to read tag data
    /// @param tag_buffer Span containing the IFD tag array
    /// @return Result<void> indicating success or error
    /// @retval Success Tags extracted successfully
    /// @retval InvalidTag Required tag not found or parsing failed
    /// @note Automatically sorts tag_buffer if not already sorted (TIFF spec requires sorted tags)
    /// @note Uses O(n+m) two-pointer algorithm for efficient extraction
    /// @note Sets optional tags to std::nullopt if not found
    template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
    [[nodiscard]] Result<void> extract(
        const Reader& reader,
        std::span<parsing::TagType<TiffFormat, SourceEndian>> tag_buffer) noexcept;

    /// @brief Extract tags from an IFD (strict version that assumes tags are sorted)
    /// @tparam Reader Reader type implementing RawReader concept
    /// @tparam TiffFormat TIFF format type (Classic or BigTIFF)
    /// @tparam SourceEndian Endianness of the stored tags
    /// @param reader Reader to read tag data
    /// @param tag_buffer Const span containing the IFD tag array
    /// @return Result<void> indicating success or error
    /// @retval Success Tags extracted successfully
    /// @retval InvalidTag Required tag not found or parsing failed
    /// @note ASSUMES tags are already sorted (TIFF spec compliant)
    /// @note If tags are not sorted, extraction may miss tags or raise errors
    /// @note Faster than extract() as it doesn't check/sort
    template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
    [[nodiscard]] Result<void> extract_strict(
        const Reader& reader,
        const std::span<parsing::TagType<TiffFormat, SourceEndian>> tag_buffer) noexcept;

    /// @brief Get total extra byte size for all tags when writing to file
    /// @tparam TiffFormat TIFF format type (determines inline limit: 4 for Classic, 8 for BigTIFF)
    /// @return Sum of external data sizes for all tags
    /// @note Returns 0 for tags that fit inline
    /// @note Only counts data that must be stored outside the IFD entry
    template <TiffFormatType TiffFormat = TiffFormatType::Classic>
    [[nodiscard]] std::size_t extra_byte_size() const noexcept;

    /// @brief Get number of defined tags in this structure
    /// @return Count of required tags plus present optional tags
    /// @note Excludes optional tags that are std::nullopt
    [[nodiscard]] std::size_t num_defined_tags() const noexcept;

    /// @brief Check if a specific tag is defined in this TagSpec
    /// @tparam code Tag code to check
    /// @return True if the tag is defined (includes both required and optional)
    /// @note Compile-time check - does NOT check if optional tag has a value
    template <TagCode code>
    [[nodiscard]] consteval static bool has_tag() noexcept;

    /// @brief Get the total number of tags in this structure
    /// @return The number of tags defined in the TagSpec
    /// @note Compile-time constant
    [[nodiscard]] consteval static std::size_t num_tags() noexcept;

    /// @brief Get extra byte size for a specific tag when writing to file
    /// @tparam Code Tag code
    /// @tparam TiffFormat TIFF format type (determines inline limit)
    /// @return Byte size if external data needed, 0 if fits inline
    /// @note For optional tags, returns 0 if not set
    template <TagCode Code, TiffFormatType TiffFormat = TiffFormatType::Classic>
    [[nodiscard]] std::size_t tag_extra_byte_size() const noexcept;

    /// @brief Check if a specific tag needs external data when writing
    /// @tparam Code Tag code
    /// @tparam TiffFormat TIFF format type
    /// @return True if tag requires external data storage
    template <TagCode Code, TiffFormatType TiffFormat = TiffFormatType::Classic>
    [[nodiscard]] bool tag_needs_external_data() const noexcept;

    /// @brief Write external data for a specific tag to a buffer
    /// @tparam Code Tag code
    /// @tparam TiffFormat TIFF format type
    /// @tparam TargetEndian Target endianness for written data
    /// @param buffer Destination buffer
    /// @return Result<void> indicating success or error
    /// @retval Success Data written successfully
    /// @retval InvalidTag Tag doesn't need external data (fits inline)
    /// @retval InvalidTag Optional tag is not set
    /// @retval OutOfBounds Buffer too small
    template <TagCode Code, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian TargetEndian = std::endian::native>
    [[nodiscard]] Result<void> tag_write_external_data(std::span<std::byte> buffer) const noexcept;
};

} // namespace tiffconcept

#define TIFFCONCEPT_TAG_EXTRACTION_HEADER
#include "impl/tag_extraction_impl.hpp"