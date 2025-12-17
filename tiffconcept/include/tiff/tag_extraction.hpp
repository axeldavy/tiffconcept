#pragma once

#include <algorithm>
#include <tuple>
#include <utility>
#include "ifd.hpp"
#include "parsing.hpp"
#include "result.hpp"
#include "tag_spec.hpp"
#include "tag_writing.hpp"
#include "types.hpp"

namespace tiffconcept {

// Forward declarations
template <typename... Tags>
struct ExtractedTags;

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
                if (found) [[likely]] {
                    auto value_result = parsing::parse_tag<Reader, TagDesc, TiffFormat, std::endian::native, SourceEndian>(
                        reader, file_tags[file_idx]
                    );
                    
                    if (value_result) [[likely]] {
                        //std::cerr << "Debug: Parsed optional tag " 
                        //          << static_cast<uint16_t>(TagDesc::code) << std::endl;
                        std::get<SpecIdx>(values) = std::move(value_result.value());
                        ++file_idx;
                    } else {
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

} // namespace detail

/// ExtractedTags structure generator - creates a struct from tag specifications
template <typename... Args>
struct ExtractedTags{
private:
    // Resolve the effective TagSpec type from arguments
    using Spec = typename detail::GetSpec<Args...>::type;

    static_assert(ValidTagSpec<Spec>, "ExtractedTags requires a valid TagSpec or list of TagDescriptors");

    // Helper to unpack TagSpec tags and define storage/logic
    template <typename T> struct Impl;

    template <typename... Tags>
    struct Impl<TagSpec<Tags...>> {
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
                } else {
                    // Otherwise, default-construct and assign
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
                    tag_buffer[i].template get_code<std::endian::native>()) {
                    is_sorted = false;
                    break;
                }
            }
            if (!is_sorted) {
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
                return Err(last_error.code, last_error.message);
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
                return Err(last_error.code, last_error.message);
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

    using Implementation = Impl<Spec>;

public:
    /// Storage for tag values
    typename Implementation::Storage values;

    /// TagSpec
    using tag_spec_type = Spec;

    /// @brief Clear all tag values
    /// Optional tags are set to std::nullopt, required tags are default-constructed
    constexpr void clear() noexcept {
        Implementation::clear(values);
    }

    /// @brief Get value by tag code at compile time
    template <TagCode Code>
    [[nodiscard]] constexpr decltype(auto) get() noexcept {
        // Non-const version - returns T&
        return Implementation::template get<Code>(values);
    }
    
    /// @brief Get value by tag code at compile time (const version)
    template <TagCode Code>
    [[nodiscard]] constexpr decltype(auto) get() const noexcept {
        // Const version - returns const T&
        return Implementation::template get<Code>(values);
    }

    /// @brief  Extract tags from an IFD into this structure (version that ensures tags are sorted)
    /// @tparam TiffFormat TIFF format type
    /// @tparam SourceEndian Endianness of the stored tags
    /// @param reader Reader to read tag data
    /// @param tag_buffer Pre-allocated span containing the IFD tag array
    /// @param sort If true, sorts the tag_buffer before extraction if it wasn't already (tiff spec mandates sorted tags)
    template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
    [[nodiscard]] Result<void> extract(
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
    template <RawReader Reader, TiffFormatType TiffFormat, std::endian SourceEndian>
    [[nodiscard]] Result<void> extract_strict(
        const Reader& reader,
        const std::span<parsing::TagType<TiffFormat, SourceEndian>> tag_buffer) noexcept {
        Error last_error{Error::Code::Success};
        return Implementation::template extract_strict_impl<Reader, TiffFormat, SourceEndian>(
            values, reader, tag_buffer);
    }

    // Writing helpers

    /// Get total extra byte size for all tags when writing to file
    /// This is the sum of all tag_extra_byte_size() for all tags
    template <TiffFormatType TiffFormat = TiffFormatType::Classic>
    [[nodiscard]] std::size_t extra_byte_size() const noexcept {
        return Implementation::template extra_byte_size_impl<TiffFormat>(values);
    }

    /// Get number of defined tags in this structure (num_tags minus unset optionals)
    [[nodiscard]] std::size_t num_defined_tags() const noexcept {
        return Implementation::num_defined_tags_impl(values);
    }

    /// @brief Check if a specific tag is defined in this TagSpec (this INCLUDES optionals)
    /// @param code Tag code to check
    /// @return True if the tag is defined in this TagSpec
    template <TagCode code>
    [[nodiscard]] consteval static bool has_tag() noexcept {
        return Spec::template has_tag<code>();
    }

    /// @brief Get the total number of tags in this structure
    /// @return The number of tags defined in the TagSpec
    [[nodiscard]] consteval static std::size_t num_tags() noexcept {
        return Spec::num_tags;
    }

    /// Get extra byte size for a specific tag when writing to file
    /// Returns 0 if the value fits inline, otherwise returns the byte size of the external data
    template <TagCode Code, TiffFormatType TiffFormat = TiffFormatType::Classic>
    [[nodiscard]] std::size_t tag_extra_byte_size() const noexcept {
        return Implementation::template tag_extra_byte_size_impl<Code, TiffFormat>(values);
    }

    /// Check if a specific tag needs to write an external data array when writing a file
    template <TagCode Code, TiffFormatType TiffFormat = TiffFormatType::Classic>
    [[nodiscard]] bool tag_needs_external_data() const noexcept {
        return Implementation::template tag_extra_byte_size_impl<Code, TiffFormat>(values) > 0;
    }

    /// Write external data for a specific tag to a span with correct type and endianness
    /// Returns error if the tag doesn't need external data or if the buffer is too small
    template <TagCode Code, TiffFormatType TiffFormat = TiffFormatType::Classic, std::endian TargetEndian = std::endian::native>
    [[nodiscard]] Result<void> tag_write_external_data(std::span<std::byte> buffer) const noexcept {
        return Implementation::template tag_write_external_data_impl<Code, TiffFormat, TargetEndian>(values, buffer);
    }
};


/// Helpers to extract optionals
namespace optional {
    template<typename T>
    struct is_optional : std::false_type {};

    template<typename T>
    struct is_optional<std::optional<T>> : std::true_type {};

    template<typename T>
    inline constexpr bool is_optional_v = is_optional<T>::value;

    template <typename T>
    concept IsContainer = requires(const T& t) {
        t.size();
        t[0];
    } && !std::is_same_v<std::decay_t<T>, std::string>;

    /// Helper to check if a value is present (for optional types)
    template <typename T>
    [[nodiscard]] constexpr bool is_value_present(const T& value) noexcept {
        if constexpr (is_optional_v<T>) {
            return value.has_value();
        } else {
            return true;
        }
    }

    /// Helper to unwrap a value (for optional types)
    template <typename T>
    [[nodiscard]] constexpr auto& unwrap_value(const T& value) noexcept {
        if constexpr (is_optional_v<T>) {
        return value.value();  // Return the whole value
    } else {
        return value;
    }
    }

    /// Helper to unwrap a value or return a default (for optional types)
    template <typename T, typename DefaultType>
    [[nodiscard]] constexpr auto& unwrap_value_or(const T& value, DefaultType&& default_value) noexcept {
        if constexpr (is_optional_v<T>) {
            if (value.has_value()) {
                return value.value();
            } else {
                return std::forward<DefaultType>(default_value);
            }
        } else {
            return value;
        }
    }

    /// Helper to extract a tag value or return a default
    template <TagCode Code, typename TagSpec, typename DefaultType>
    [[nodiscard]] constexpr auto extract_tag_or(
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
} // namespace optional

} // namespace tiffconcept
