// Do not include this file directly. Include "tag_extraction.hpp" instead.

#pragma once

#include <algorithm>
#include <tuple>
#include <utility>
#include "../ifd.hpp"
#include "../lowlevel/tag_writing.hpp"
#include "../parsing.hpp"
#include "../types/result.hpp"
#include "../types/tag_spec.hpp"
#include "../types/optional.hpp"
#include "../types/tiff_spec.hpp"

#ifndef TIFFCONCEPT_TAG_EXTRACTION_HEADER
#include "../tag_extraction.hpp" // for linters
#endif

namespace tiffconcept {

namespace detail {

/// Check if all tags are optional (for empty IFD case)
template <typename... Tags, typename Storage>
[[nodiscard]] inline bool validate_all_optional(
    Storage& values,
    Error& last_error) noexcept {
    
    bool all_optional = true;
    
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        auto check_optional = [&]<std::size_t Idx, typename TagDesc>() {
            if constexpr (!TagDesc::is_optional) {
                all_optional = false;
                last_error = Err(
                    Error::Code::InvalidTag,
                    "Required tag " + 
                    std::to_string(static_cast<uint16_t>(TagDesc::code)) + 
                    " not found (empty IFD)"
                );
            } else {
                // Set optional tags to nullopt
                std::get<Idx>(values) = std::nullopt;
            }
        };
        
        (check_optional.template operator()<Is, Tags>(), ...);
    }(std::index_sequence_for<Tags...>{});
    
    return all_optional;
}

/// Extraction: O(n+m) two-pointer algorithm (requires sorted tags - tiff spec compliant)
template <typename Reader, TiffFormatType TiffFormat, std::endian SourceEndian, typename... Tags, typename Storage>
[[nodiscard]] inline bool extract_tags_sorted(
    Storage& values,
    const Reader& reader,
    std::span<const parsing::TagType<TiffFormat, SourceEndian>> file_tags,
    Error& last_error) noexcept {
    
    if (file_tags.empty()) { // If this occurs, there is no way to read anything anyway
        return validate_all_optional<Tags...>(values, last_error);
    }

#ifndef NDEBUG
    // Debug check: ensure file_tags are sorted
    for (std::size_t i = 1; i < file_tags.size(); ++i) {
        assert(file_tags[i-1].template get_code<std::endian::native>() <= 
               file_tags[i].template get_code<std::endian::native>() && 
               "File tags are not sorted!");
    }
#endif
    
    std::size_t file_idx = 0;
    bool success = true;

    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        auto process_spec_tag = [&]<std::size_t SpecIdx, typename TagDesc>() {
            if (!success) [[unlikely]] {
                return;
            }
            
            const uint16_t target_code = static_cast<uint16_t>(TagDesc::code);
            
            // Skip file tags before target
            while (file_idx < file_tags.size() && 
                    file_tags[file_idx].template get_code<std::endian::native>() < target_code) {
                ++file_idx;
            }
            
            const bool found = (file_idx < file_tags.size() && 
                                file_tags[file_idx].template get_code<std::endian::native>() == target_code);
            
            if constexpr (TagDesc::is_optional) {
                if (found) {
                    auto value_result = parsing::parse_tag<Reader, TagDesc, TiffFormat, std::endian::native, SourceEndian>(
                        reader, file_tags[file_idx]
                    );
                    
                    if (value_result) [[likely]] {
                        //std::cerr << "Debug: Parsed optional tag " 
                        //          << static_cast<uint16_t>(TagDesc::code) << std::endl;
                        std::get<SpecIdx>(values) = std::move(value_result.value());
                        ++file_idx;
                    } else { // TODO: should we fail on parse error for optional tags?
                        //std::cerr << "Warning: Failed to parse optional tag " 
                        //          << static_cast<uint16_t>(TagDesc::code) 
                        //          << ": " << value_result.error().message << std::endl;
                        std::get<SpecIdx>(values) = std::nullopt;
                    }
                } else {
                    std::get<SpecIdx>(values) = std::nullopt;
                }
            } else {
                if (!found) [[unlikely]] {
                    success = false;
                    last_error = Error{
                        Error::Code::InvalidTag,
                        "Required tag " + std::to_string(target_code) + " not found"
                    };
                    return;
                }
                
                auto value_result = parsing::parse_tag<Reader, TagDesc, TiffFormat, std::endian::native, SourceEndian>(
                    reader, file_tags[file_idx]
                );
                
                if (!value_result) [[unlikely]] {
                    success = false;
                    last_error = value_result.error();
                    return;
                }
                //std::cerr << "Debug: Parsed required tag " 
                //          << static_cast<uint16_t>(TagDesc::code) << std::endl;
                
                std::get<SpecIdx>(values) = std::move(value_result.value());
                ++file_idx;
            }
        };
        
        (process_spec_tag.template operator()<Is, Tags>(), ...);
    }(std::index_sequence_for<Tags...>{});
    
    return success;
}

template <typename... Tags>
struct ExtractedTagsImpl<TagSpec<Tags...>> {
    using Storage = std::tuple<tag_storage_type_t<Tags>...>;

    // Non-const version
    template <TagCode Code, std::size_t Index, typename First, typename... Rest>
    [[nodiscard]] static constexpr decltype(auto) get_in_values(Storage& values) noexcept {
        if constexpr (First::code == Code) {
            return std::get<Index>(values);
        } else {
            return get_in_values<Code, Index + 1, Rest...>(values);
        }
    }

    // Const version
    template <TagCode Code, std::size_t Index, typename First, typename... Rest>
    [[nodiscard]] static constexpr decltype(auto) get_in_values(const Storage& values) noexcept {
        if constexpr (First::code == Code) {
            return std::get<Index>(values);
        } else {
            return get_in_values<Code, Index + 1, Rest...>(values);
        }
    }
    
    template <TagCode Code>
    [[nodiscard]] static constexpr decltype(auto) get(Storage& values) noexcept {
        return get_in_values<Code, 0, Tags...>(values);
    }

    template <TagCode Code>
    [[nodiscard]] static constexpr decltype(auto) get(const Storage& values) noexcept {
        return get_in_values<Code, 0, Tags...>(values);
    }

    template <std::size_t Index, typename First, typename... Rest>
    static constexpr void clear_impl(Storage& values) noexcept {
        if constexpr (First::is_optional) {
            std::get<Index>(values) = std::nullopt;
        } else {
            auto& value = std::get<Index>(values);
            using ValueType = typename First::value_type;
            
            // For types with a clear() method, call it to reuse allocated memory
            if constexpr (requires { value.clear(); }) {
                value.clear();
            }
            // For types with resize but not clear (e.g., some custom containers)
            else if constexpr (requires { value.resize(0); }) {
                value.resize(0);
            }
            // For fixed-size array types (std::array, C arrays)
            else if constexpr (requires { std::tuple_size<ValueType>::value; }) {
                using ElemType = std::remove_reference_t<decltype(value[0])>;
                for (auto& elem : value) {
                    if constexpr (requires { elem.clear(); }) {
                        elem.clear();
                    } else {
                        elem = ElemType{};
                    }
                }
            }
            // Otherwise, default-construct and assign
            else {
                value = ValueType{};
            }
        }
        
        if constexpr (sizeof...(Rest) > 0) {
            clear_impl<Index + 1, Rest...>(values);
        }
    }

    static constexpr void clear(Storage& values) noexcept {
        clear_impl<0, Tags...>(values);
    }

    template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
    [[nodiscard]] static Result<void> extract_impl(
        Storage& values,
        const Reader& reader,
        std::span<parsing::TagType<TiffFormat, SourceEndian>> tag_buffer) noexcept {
        // Check if already sorted (likely case)
        bool is_sorted = true;
        for (std::size_t i = 1; i < tag_buffer.size(); ++i) {
            if (tag_buffer[i-1].template get_code<std::endian::native>() > 
                tag_buffer[i].template get_code<std::endian::native>()) [[unlikely]] {
                is_sorted = false;
                break;
            }
        }
        if (!is_sorted) [[unlikely]] {
            std::sort(tag_buffer.begin(), tag_buffer.end(), 
                [](const auto& a, const auto& b) {
                    return a.template get_code<std::endian::native>() < 
                            b.template get_code<std::endian::native>();
                });
        }

        Error last_error{Error::Code::Success};
        // Assumes sorted tags (O(n+m) two-pointer)
        bool success = detail::extract_tags_sorted<Reader, TiffFormat, SourceEndian, Tags...>(
            values, reader, tag_buffer, last_error);

        if (!success) [[unlikely]] {
            return last_error;
        }
        
        return Ok();
    }

    template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
    [[nodiscard]] static Result<void> extract_strict_impl(
        Storage& values,
        const Reader& reader,
        const std::span<parsing::TagType<TiffFormat, SourceEndian>> tag_buffer) noexcept {
        Error last_error{Error::Code::Success};
        // Assumes sorted tags (O(n+m) two-pointer)
        bool success = detail::extract_tags_sorted<Reader, TiffFormat, SourceEndian, Tags...>(
            values, reader, tag_buffer, last_error);

        if (!success) [[unlikely]] {
            return last_error;
        }
        
        return Ok();
    }

    template <TagCode Code, TiffFormatType TiffFormat>
    [[nodiscard]] static std::size_t tag_extra_byte_size_impl(const Storage& values) noexcept {
        constexpr std::size_t inline_limit = (TiffFormat == TiffFormatType::Classic) ? 4 : 8;
    
        using TagDesc = get_tag_t<Code, Tags...>;
        const auto& value_storage = get<Code>(values);
        
        std::size_t byte_size = 0;
        
        // Handle optional tags
        if constexpr (TagDesc::is_optional) {
            if (!value_storage.has_value()) {
                return 0;
            }
            const auto& value = value_storage.value();
            byte_size = tag_writing::calculate_tag_data_size<TagDesc>(value);
        } else {
            byte_size = tag_writing::calculate_tag_data_size<TagDesc>(value_storage);
        }
        
        // Only count if it doesn't fit inline
        return (byte_size <= inline_limit) ? 0 : byte_size;
    }

    [[nodiscard]] static std::size_t num_defined_tags_impl(const Storage& values) noexcept {
        std::size_t count = 0;
        
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            [[maybe_unused]] auto check_defined = [&]<std::size_t Idx, typename TagDesc>() {
                const auto& value_storage = std::get<Idx>(values);
                
                if constexpr (TagDesc::is_optional) {
                    if (value_storage.has_value()) {
                        ++count;
                    }
                } else {
                    ++count;
                }
            };
            
            (check_defined.template operator()<Is, Tags>(), ...);
        }(std::index_sequence_for<Tags...>{});
        
        return count;
    }

    template <TiffFormatType TiffFormat>
    [[nodiscard]] static std::size_t extra_byte_size_impl(const Storage& values) noexcept {
        constexpr std::size_t inline_limit = (TiffFormat == TiffFormatType::Classic) ? 4 : 8;
        std::size_t total_size = 0;
        
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            [[maybe_unused]] auto add_size = [&]<std::size_t Idx, typename TagDesc>() {
                const auto& value_storage = std::get<Idx>(values);
                
                std::size_t byte_size = 0;
                if constexpr (TagDesc::is_optional) {
                    if (value_storage.has_value()) {
                        const auto& value = value_storage.value();
                        byte_size = tag_writing::calculate_tag_data_size<TagDesc>(value);
                    }
                } else {
                    byte_size = tag_writing::calculate_tag_data_size<TagDesc>(value_storage);
                }
                
                // Only count if it doesn't fit inline
                if (byte_size > inline_limit) {
                    total_size += byte_size;
                }
            };
            
            (add_size.template operator()<Is, Tags>(), ...);
        }(std::index_sequence_for<Tags...>{});

        return total_size;
    }

    template <TagCode Code, TiffFormatType TiffFormat, std::endian TargetEndian>
    [[nodiscard]] static Result<void> tag_write_external_data_impl(const Storage& values, std::span<std::byte> buffer) noexcept {
        constexpr std::size_t inline_limit = (TiffFormat == TiffFormatType::Classic) ? 4 : 8;
        
        using TagDesc = get_tag_t<Code, Tags...>;
        const auto& value_storage = get<Code>(values);
        
        // Handle optional tags
        if constexpr (TagDesc::is_optional) {
            if (!value_storage.has_value()) {
                return Err(Error::Code::InvalidTag, 
                        "Cannot write external data for unset optional tag " + 
                        std::to_string(static_cast<uint16_t>(Code)));
            }
            const auto& value = value_storage.value();
            const std::size_t byte_size = tag_writing::calculate_tag_data_size<TagDesc>(value);
            
            if (byte_size <= inline_limit) {
                return Err(Error::Code::InvalidTag,
                        "Tag " + std::to_string(static_cast<uint16_t>(Code)) + 
                        " does not need external data (fits inline)");
            }
            
            return tag_writing::write_tag_data<TagDesc, TargetEndian>(value, buffer, byte_size);
        } else {
            const std::size_t byte_size = tag_writing::calculate_tag_data_size<TagDesc>(value_storage);
            
            if (byte_size <= inline_limit) {
                return Err(Error::Code::InvalidTag,
                        "Tag " + std::to_string(static_cast<uint16_t>(Code)) + 
                        " does not need external data (fits inline)");
            }
            
            return tag_writing::write_tag_data<TagDesc, TargetEndian>(value_storage, buffer, byte_size);
        }
    }
};

} // namespace detail

/// ExtractedTags structure generator - creates a struct from tag specifications
template <typename... Args>
constexpr void ExtractedTags<Args...>::clear() noexcept {
    Implementation::clear(values);
}

/// @brief Get value by tag code at compile time
template <typename... Args>
template <TagCode Code>
inline constexpr decltype(auto) ExtractedTags<Args...>::get() noexcept {
    // Non-const version - returns T&
    return Implementation::template get<Code>(values);
}

/// @brief Get value by tag code at compile time (const version)
template <typename... Args>
template <TagCode Code>
inline constexpr decltype(auto) ExtractedTags<Args...>::get() const noexcept {
    // Const version - returns const T&
    return Implementation::template get<Code>(values);
}

/// @brief  Extract tags from an IFD into this structure (version that ensures tags are sorted)
/// @tparam TiffFormat TIFF format type
/// @tparam SourceEndian Endianness of the stored tags
/// @param reader Reader to read tag data
/// @param tag_buffer Pre-allocated span containing the IFD tag array
/// @param sort If true, sorts the tag_buffer before extraction if it wasn't already (tiff spec mandates sorted tags)
template <typename... Args>
template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
inline Result<void> ExtractedTags<Args...>::extract(
    const Reader& reader,
    std::span<parsing::TagType<TiffFormat, SourceEndian>> tag_buffer) noexcept {
    return Implementation::template extract_impl<Reader, TiffFormat, SourceEndian>(
        values, reader, tag_buffer);
}

/// @brief  Extract tags from an IFD into this structure (version that assumes tags are sorted - tiff spec compliant)
/// @tparam TiffFormat TIFF format type
/// @tparam SourceEndian Endianness of the stored tags
/// @param reader Reader to read tag data
/// @param tag_buffer Pre-allocated span containing the IFD tag array
/// @param sort If true, sorts the tag_buffer before extraction if it wasn't already (tiff spec mandates sorted tags)
/// If the tags are not sorted, extraction may raise an error, or not detect all tags.
template <typename... Args>
template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
inline Result<void> ExtractedTags<Args...>::extract_strict(
    const Reader& reader,
    const std::span<parsing::TagType<TiffFormat, SourceEndian>> tag_buffer) noexcept {
    Error last_error{Error::Code::Success};
    return Implementation::template extract_strict_impl<Reader, TiffFormat, SourceEndian>(
        values, reader, tag_buffer);
}

// Writing helpers

/// Get total extra byte size for all tags when writing to file
/// This is the sum of all tag_extra_byte_size() for all tags
template <typename... Args>
template <TiffFormatType TiffFormat>
inline std::size_t ExtractedTags<Args...>::extra_byte_size() const noexcept {
    return Implementation::template extra_byte_size_impl<TiffFormat>(values);
}

/// Get number of defined tags in this structure (num_tags minus unset optionals)
template <typename... Args>
inline std::size_t ExtractedTags<Args...>::num_defined_tags() const noexcept {
    return Implementation::num_defined_tags_impl(values);
}

/// @brief Check if a specific tag is defined in this TagSpec (this INCLUDES optionals)
/// @param code Tag code to check
/// @return True if the tag is defined in this TagSpec
template <typename... Args>
template <TagCode code>
inline consteval bool ExtractedTags<Args...>::has_tag() noexcept {
    return Spec::template has_tag<code>();
}

/// @brief Get the total number of tags in this structure
/// @return The number of tags defined in the TagSpec
template <typename... Args>
inline consteval std::size_t ExtractedTags<Args...>::num_tags() noexcept {
    return Spec::num_tags;
}

/// Get extra byte size for a specific tag when writing to file
/// Returns 0 if the value fits inline, otherwise returns the byte size of the external data
template <typename... Args>
template <TagCode Code, TiffFormatType TiffFormat>
inline std::size_t ExtractedTags<Args...>::tag_extra_byte_size() const noexcept {
    return Implementation::template tag_extra_byte_size_impl<Code, TiffFormat>(values);
}

/// Check if a specific tag needs to write an external data array when writing a file
template <typename... Args>
template <TagCode Code, TiffFormatType TiffFormat>
inline bool ExtractedTags<Args...>::tag_needs_external_data() const noexcept {
    return Implementation::template tag_extra_byte_size_impl<Code, TiffFormat>(values) > 0;
}

/// Write external data for a specific tag to a span with correct type and endianness
/// Returns error if the tag doesn't need external data or if the buffer is too small
template <typename... Args>
template <TagCode Code, TiffFormatType TiffFormat, std::endian TargetEndian>
inline Result<void> ExtractedTags<Args...>::tag_write_external_data(std::span<std::byte> buffer) const noexcept {
    return Implementation::template tag_write_external_data_impl<Code, TiffFormat, TargetEndian>(values, buffer);
}

namespace optional {
    /// Helper to extract a tag value or return a default
    template <TagCode Code, typename TagSpec, typename DefaultType>
    inline constexpr auto extract_tag_or(
        const ExtractedTags<TagSpec>& metadata,
        DefaultType&& default_value) noexcept {
        
        const auto& val = metadata.template get<Code>();
        using ValType = std::decay_t<decltype(val)>;
        
        if constexpr (is_optional_v<ValType>) {
            if (val.has_value()) {
                return *val;
            } else {
                return std::forward<DefaultType>(default_value);
            }
        } else {
            return val;
        }
    }
}

} // namespace tiffconcept
