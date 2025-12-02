#pragma once

#include <tuple>
#include <utility>
#include "tag_spec.hpp"
#include "tag_reader.hpp"
#include "result.hpp"

namespace tiff {

/// Metadata structure generator - creates a struct from tag specifications
template <typename... Tags>
    requires (TagDescriptorType<Tags> && ...)
struct Metadata {
    std::tuple<tag_storage_type_t<Tags>...> values;
    
    /// Get value by tag code at compile time
    template <TagCode Code>
    [[nodiscard]] constexpr decltype(auto)& get() noexcept {
        return get_by_index<Code, 0, Tags...>();
    }
    
    template <TagCode Code>
    [[nodiscard]] constexpr const decltype(auto)& get() const noexcept {
        return get_by_index<Code, 0, Tags...>();
    }
    
private:
    template <TagCode Code, std::size_t Index, typename First, typename... Rest>
    [[nodiscard]] constexpr decltype(auto)& get_by_index() noexcept {
        if constexpr (First::code == Code) {
            return std::get<Index>(values);
        } else {
            return get_by_index<Code, Index + 1, Rest...>();
        }
    }
    
    template <TagCode Code, std::size_t Index, typename First, typename... Rest>
    [[nodiscard]] constexpr const decltype(auto)& get_by_index() const noexcept {
        if constexpr (First::code == Code) {
            return std::get<Index>(values);
        } else {
            return get_by_index<Code, Index + 1, Rest...>();
        }
    }
};

/// Extract metadata from TIFF file using compile-time tag specification
/// 
/// @tparam TSpec Tag specification (TagSpec<...>)
/// @tparam Reader File reader type
/// @tparam Lenient If true, accepts unsorted TIFF tags (slower). If false, assumes sorted tags (faster, corresponds to tiff specification)
template <typename TSpec, typename Reader, bool Lenient = false>
    requires ValidTagSpec<TSpec> && RawReader<Reader>
class MetadataExtractor {
private:
    TagReader<Reader> tag_reader_;
    
public:
    explicit MetadataExtractor(const Reader& reader) noexcept
        : tag_reader_(reader) {}
    
    /// Extract metadata from a specific page using TSpec
    [[nodiscard]] auto extract(std::size_t page_index = 0) const noexcept 
        -> Result<metadata_type_t<TSpec>> {
        return extract_impl(page_index, TSpec{});
    }
    
    /// Extract metadata into pre-allocated structure with dynamic tag buffer
    /// @param metadata Pre-allocated metadata structure to fill
    /// @param page_index Page index to extract from
    /// @param tag_buffer Pre-allocated span for reading tags
    /// @return Success or error
    template <typename... Tags>
    [[nodiscard]] Result<void> extract_into(
        Metadata<Tags...>& metadata,
        std::size_t page_index,
        std::span<TiffTag> tag_buffer) const noexcept 
        requires std::same_as<TSpec, TagSpec<Tags...>> {
        
        return extract_into_impl(metadata, page_index, tag_buffer, TSpec{});
    }
    
    /// Extract metadata into pre-allocated structure with fixed-size tag buffer
    /// @param metadata Pre-allocated metadata structure to fill
    /// @param page_index Page index to extract from
    /// @param tag_buffer Pre-allocated fixed-size array for reading tags
    /// @return Success or error
    template <std::size_t MaxTags, typename... Tags>
    [[nodiscard]] Result<void> extract_into(
        Metadata<Tags...>& metadata,
        std::size_t page_index,
        std::array<TiffTag, MaxTags>& tag_buffer) const noexcept 
        requires std::same_as<TSpec, TagSpec<Tags...>> {
        
        return extract_into_impl(metadata, page_index, std::span{tag_buffer}, TSpec{});
    }
    
    const TagReader<Reader>& tag_reader() const noexcept {
        return tag_reader_;
    }
    
private:
    /// Implementation that unpacks TSpec into Tags...
    template <typename... Tags>
    [[nodiscard]] Result<Metadata<Tags...>> extract_impl(
        std::size_t page_index,
        TagSpec<Tags...>) const noexcept {
        
        // Get page offset
        auto page_offset_result = tag_reader_.get_page_offset(page_index);
        if [[unlikely]](!page_offset_result) {
            return Err(page_offset_result.error().code, page_offset_result.error().message);
        }
        
        // Read all tags from IFD
        auto tags_result = tag_reader_.read_ifd_tags(page_offset_result.value());
        if [[unlikely]](!tags_result) {
            return Err(tags_result.error().code, tags_result.error().message);
        }
        
        auto& file_tags = tags_result.value();
        
        Metadata<Tags...> metadata;
        
        // Use extract_from_ifd_impl
        auto result = extract_from_ifd_impl(metadata, std::span{file_tags});
        if [[unlikely]](!result) {
            return Err(result.error().code, result.error().message);
        }
        
        return Ok(std::move(metadata));
    }
    
    /// Core implementation for extract_into that reads tags and extracts
    template <typename... Tags>
    [[nodiscard]] Result<void> extract_into_impl(
        Metadata<Tags...>& metadata,
        std::size_t page_index,
        std::span<TiffTag> tag_buffer,
        TagSpec<Tags...>) const noexcept {
        
        // Get page offset
        auto page_offset_result = tag_reader_.get_page_offset(page_index);
        if [[unlikely]](!page_offset_result) {
            return Err(page_offset_result.error().code, page_offset_result.error().message);
        }
        
        // Read IFD header to know how many tags we need
        auto header_result = tag_reader_.read_ifd_header(page_offset_result.value());
        if [[unlikely]](!header_result) {
            return Err(header_result.error().code, header_result.error().message);
        }
        
        const auto& header = header_result.value();
        
        // Validate buffer size
        if [[unlikely]](tag_buffer.size() < header.num_entries) {
            return Err(Error::Code::InvalidArgument, 
                       "Tag buffer too small: need " + std::to_string(header.num_entries) + 
                       " entries, got " + std::to_string(tag_buffer.size()));
        }
        
        // Read tags directly into buffer
        if (header.num_entries > 0) {
            std::size_t tags_offset = page_offset_result.value() + sizeof(IFDHeader);
            std::size_t byte_size = header.num_entries * sizeof(TiffTag);
            
            auto view_result = tag_reader_.reader_.read(tags_offset, byte_size);
            
            if [[unlikely]](!view_result) {
                return Err(view_result.error().code, "Failed to read tags");
            }
            
            auto view = std::move(*view_result);
            
            if [[unlikely]](view.size() < byte_size) {
                return Err(Error::Code::UnexpectedEndOfFile, 
                          "Failed to read tags");
            }
            
            // Copy into buffer
            std::memcpy(reinterpret_cast<std::byte*>(tag_buffer.data()), 
                       view.data().data(), byte_size);
        }
        
        // Create span for actual tags read
        std::span<TiffTag> file_tags = tag_buffer.subspan(0, header.num_entries);
        
        // Use existing extract_from_ifd_impl to handle sorting and extraction
        return extract_from_ifd_impl(metadata, file_tags);
    }

    /// Extract tags from IFD span into preallocated metadata structure
    template <typename... Tags>
    [[nodiscard]] Result<void> extract_from_ifd_impl(Metadata<Tags...>& metadata,
                                                     std::span<TiffTag> file_tags) const noexcept {
        // Compile-time dispatch based on Lenient flag
        if constexpr (Lenient) {
            // Lenient mode: works with unsorted tags as well (misformed files)
            // Check if it is sorted
            bool is_sorted = true;
            for (std::size_t i = 1; i < file_tags.size(); ++i) {
                if (file_tags[i-1].code > file_tags[i].code) {
                    is_sorted = false;
                    break;
                }
            }
            if (!is_sorted) {
                std::sort(file_tags.begin(), file_tags.end(), 
                     [](const TiffTag& a, const TiffTag& b) {
                         return a.code < b.code;
                     });
            }
        }

        Error last_error{Error::Code::Success};
        bool success;
        // Assumes sorted tags (O(n+m) two-pointer)
        success = extract_tags_sorted<Tags...>(metadata, file_tags, last_error);

        if [[unlikely]](!success) {
            return Err(last_error.code, last_error.message);
        }
        
        return Ok();
    }

    /// Extraction: O(n+m) two-pointer algorithm (requires sorted tags - tiff spec compliant)
    template <typename... Tags>
    [[nodiscard]] bool extract_tags_sorted(
        Metadata<Tags...>& metadata,
        std::span<const TiffTag> file_tags,
        Error& last_error) const noexcept {
        
        if (file_tags.empty()) { // If this occurs, there is no way to read anything anyway
            return validate_all_optional<Tags...>(metadata, last_error);
        }

#ifndef NDEBUG
        // Debug check: ensure file_tags are sorted
        for (std::size_t i = 1; i < file_tags.size(); ++i) {
            assert(file_tags[i-1].code <= file_tags[i].code && "File tags are not sorted! Use Lenient=true for unsorted tags support.");
        }
#endif
        
        std::size_t file_idx = 0;
        std::size_t spec_idx = 0;
        bool success = true;
        
        auto process_spec_tag = [&]<typename TagDesc>() {
            if [[unlikely]](!success) {
                ++spec_idx;
                return;
            }
            
            const uint16_t target_code = static_cast<uint16_t>(TagDesc::code);
            
            // Skip file tags before target (key optimization!)
            while (file_idx < file_tags.size() && 
                   file_tags[file_idx].code < target_code) {
                ++file_idx;
            }
            
            const bool found = (file_idx < file_tags.size() && 
                               file_tags[file_idx].code == target_code);
            
            if constexpr (TagDesc::is_optional) {
                if [[likely]](found) {
                    auto value_result = tag_reader_.template parse_tag<TagDesc>(
                        file_tags[file_idx]
                    );
                    
                    if [[likely]](value_result) {
                        std::get<spec_idx>(metadata.values) = std::move(value_result.value());
                        ++file_idx;
                    } else {
                        std::get<spec_idx>(metadata.values) = std::nullopt;
                    }
                } else {
                    std::get<spec_idx>(metadata.values) = std::nullopt;
                }
            } else {
                if [[unlikely]](!found) {
                    success = false;
                    last_error = Err(
                        Error::Code::InvalidTag,
                        "Required tag " + std::to_string(target_code) + " not found"
                    );
                    ++spec_idx;
                    return;
                }
                
                auto value_result = tag_reader_.template parse_tag<TagDesc>(
                    file_tags[file_idx]
                );
                
                if [[unlikely]](!value_result) {
                    success = false;
                    last_error = value_result.error();
                    ++spec_idx;
                    return;
                }
                
                std::get<spec_idx>(metadata.values) = std::move(value_result.value());
                ++file_idx;
            }
            
            ++spec_idx;
        };
        
        (process_spec_tag.template operator()<Tags>(), ...);
        
        return success;
    }

    /// Check if all tags are optional (for empty IFD case)
    template <typename... Tags>
    [[nodiscard]] bool validate_all_optional(
        Metadata<Tags...>& metadata,
        Error& last_error) const noexcept {
        
        bool all_optional = true;
        std::size_t idx = 0;
        
        auto check_optional = [&]<typename TagDesc>() {
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
                std::get<idx>(metadata.values) = std::nullopt;
            }
            ++idx;
        };
        
        (check_optional.template operator()<Tags>(), ...);
        
        return all_optional;
    }
};

/// Convenience function to extract metadata
/// @tparam Lenient If true, accepts unsorted TIFF files. If false, assumes sorted tags (faster)
template <typename TSpec, typename Reader, bool Lenient = true>
    requires ValidTagSpec<TSpec> && RawReader<Reader>
[[nodiscard]] auto extract_metadata(const Reader& reader, std::size_t page_index = 0) noexcept {
    MetadataExtractor<TSpec, Reader, Lenient> extractor(reader);
    return extractor.extract(page_index);
}


/// Helper trait to deduce metadata type from TagSpec
template <typename TagSpec>
struct metadata_type;

template <typename... Tags>
struct metadata_type<TagSpec<Tags...>> {
    using type = Metadata<Tags...>;
};

template <typename TagSpec>
using metadata_type_t = typename metadata_type<TagSpec>::type;


/// Extract a tag value from metadata, handling both optional and required tags
/// Returns the appropriate type based on whether the tag is optional in the spec
/// - For optional tags: returns std::optional<ValueType>
/// - For required tags: returns ValueType directly
template <TagCode Code, typename TagSpec, typename MetadataType>
    requires ValidTagSpec<TagSpec> && 
             TagSpec::template has_tag_v<Code> &&
             std::same_as<MetadataType, metadata_type_t<TagSpec>>
[[nodiscard]] constexpr decltype(auto) extract_tag_value(const MetadataType& metadata) noexcept {
    return metadata.template get<Code>();
}

/// Get a tag value with a default fallback
/// If the tag is present and has a value, returns that value
/// Otherwise, returns the provided default value
/// Works for both optional and required tags
template <TagCode Code, typename TagSpec, typename MetadataType, typename DefaultType>
    requires ValidTagSpec<TagSpec> && 
             TagSpec::template has_tag_v<Code> &&
             std::same_as<MetadataType, metadata_type_t<TagSpec>>
[[nodiscard]] constexpr auto extract_tag_or(
    const MetadataType& metadata, 
    DefaultType&& default_value) noexcept {
    
    auto value = metadata.template get<Code>();
    
    if constexpr (requires { value.value_or(std::forward<DefaultType>(default_value)); }) {
        // It's an optional - use value_or
        return value.value_or(std::forward<DefaultType>(default_value));
    } else {
        // It's a direct value - return it (ignore default)
        return value;
    }
}

/// Check if a tag is present in the metadata AND has a value
/// For required tags that are always present, always returns true
/// For optional tags, checks if they have a value
template <TagCode Code, typename TagSpec, typename MetadataType>
    requires ValidTagSpec<TagSpec> && 
             TagSpec::template has_tag_v<Code> &&
             std::same_as<MetadataType, metadata_type_t<TagSpec>>
[[nodiscard]] constexpr bool has_tag_value(const MetadataType& metadata) noexcept {
    return is_value_present(metadata.template get<Code>());
}

} // namespace tiff
