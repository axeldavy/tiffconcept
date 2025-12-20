
// This file contains the implementation of IFDBuilder.
// Do not include this file directly - it is included by ifd_builder.hpp


#pragma once

#include <algorithm>
#include <bit>
#include <cstring>
#include <optional>
#include <span>
#include <vector>
#include "../ifd.hpp"
#include "../result.hpp"
#include "../tag_extraction.hpp"
#include "../tag_spec.hpp"
#include "../tag_writing.hpp"
#include "../types.hpp"
#include "../strategy/write_strategy.hpp"
#pragma once

#ifndef TIFFCONCEPT_IFD_BUILDER_HEADER
#include "../ifd_builder.hpp" // for linters
#endif

namespace tiffconcept {

// ============================================================================
// IFDBuilder Private Member Function Implementations
// ============================================================================

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
template <typename TagDesc, typename ValueType>
Result<typename IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::TagType> 
IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::create_tag_entry(
    const ValueType& value,
    std::size_t& current_external_offset) noexcept {
    
    constexpr TiffDataType datatype = TagDesc::datatype;
    constexpr std::size_t type_size = tiff_type_size(datatype);
    constexpr std::size_t inline_limit = (TiffFormat == TiffFormatType::Classic) ? 4 : 8;
    
    TagType tag;

    tag.template set_code<std::endian::native>(static_cast<uint16_t>(TagDesc::code));
    tag.template set_datatype<std::endian::native>(datatype);
    
    // Calculate count and byte size
    std::size_t count = 0;
    std::size_t byte_size = 0;
    
    using RawType = typename TagDesc::value_type;
    
    if constexpr (std::is_same_v<RawType, std::string>) {
        if constexpr (datatype == TiffDataType::Ascii) {
            count = value.size() + 1;
            byte_size = count;
        } else {
            // Byte or Undefined
            count = value.size();
            byte_size = count;
        }
        byte_size = count;
    } else if constexpr (std::is_same_v<typename TagDesc::element_type, std::string>) {
        // Container of strings
        count = 0;
        for (const auto& str : value) {
            count += str.size() + 1; // +1 for null terminator
        }
        byte_size = count;
    } else if constexpr (TagDesc::is_container) {
        count = value.size();
        byte_size = count * type_size;
    } else {
        count = 1;
        byte_size = type_size;
    }
    
    if constexpr (TiffFormat == TiffFormatType::Classic) {
        tag.template set_count<std::endian::native>(static_cast<uint32_t>(count));
    } else {  // BigTIFF
        tag.template set_count<std::endian::native>(static_cast<uint64_t>(count));
    }
    
    // Inline or external data?
    if (byte_size <= inline_limit) {
        // Inline: write data directly to value field
        std::span<std::byte> inline_buffer(
            reinterpret_cast<std::byte*>(&tag.value),
            inline_limit
        );
        
        // Zero out the buffer first
        std::memset(inline_buffer.data(), 0, inline_limit);
        
        auto write_result = tag_writing::write_tag_data<TagDesc, TargetEndian>(
            value, inline_buffer, byte_size
        );
        
        if (write_result.is_error()) {
            return write_result.error();
        }
    } else {
        // External: store offset, append data to external buffer
        if constexpr (TargetEndian == std::endian::native) {
            tag.value.offset = static_cast<OffsetType>(current_external_offset);
        } else {
            tag.value.offset = byteswap(static_cast<OffsetType>(current_external_offset));
        }
        
        // Append data to external buffer
        std::size_t external_buffer_offset = external_data_.size();
        external_data_.resize(external_buffer_offset + byte_size);
        
        std::span<std::byte> external_buffer(
            external_data_.data() + external_buffer_offset,
            byte_size
        );
        
        auto write_result = tag_writing::write_tag_data<TagDesc, TargetEndian>(
            value, external_buffer, byte_size
        );
        
        if (write_result.is_error()) {
            return write_result.error();
        }
        
        current_external_offset += byte_size;
    }
    
    return Ok(tag);
}

// ============================================================================
// IFDBuilder Public Member Function Implementations
// ============================================================================

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::IFDBuilder(IFDPlacement placement) 
    : placement_strategy_(placement) {}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
template <typename... Args>
Result<void> IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::add_tags(
    const ExtractedTags<Args...>& extracted_tags) noexcept {

    using Spec = typename detail::GetSpec<Args...>::type;
    std::size_t current_external_offset = external_data_offset_;
    
    // Process each tag
    bool success = true;
    Error last_error{Error::Code::Success};
    
    [&]<typename... Tags>(TagSpec<Tags...>*) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            [[maybe_unused]] auto process_tag = [&]<std::size_t Idx, typename TagDesc>() {
                if (!success) return;
                
                const auto& value_storage = std::get<Idx>(extracted_tags.values);
                
                // Skip optional tags that are not set
                if constexpr (TagDesc::is_optional) {
                    if (!value_storage.has_value()) {
                        return;
                    }
                    const auto& value = value_storage.value();
                    
                    auto tag_result = create_tag_entry<TagDesc>(value, current_external_offset);
                    if (tag_result.is_error()) {
                        success = false;
                        last_error = tag_result.error();
                        return;
                    }
                    tags_.push_back(tag_result.value());
                } else {
                    auto tag_result = create_tag_entry<TagDesc>(value_storage, current_external_offset);
                    if (tag_result.is_error()) {
                        success = false;
                        last_error = tag_result.error();
                        return;
                    }
                    tags_.push_back(tag_result.value());
                }
            };
            
            (process_tag.template operator()<Is, Tags>(), ...);
        }(std::index_sequence_for<Tags...>{});
    }(static_cast<Spec*>(nullptr));
    
    if (!success) {
        return last_error;
    }

    // Update external_data_offset_ for any external data added
    external_data_offset_ = current_external_offset;
    
    // Sort tags by tag code (required by TIFF spec)
    // Note: We ensure Tags are sorted in TagSpec, thus if tags_ was empty,
    // this sort should be a no-op.
    std::sort(tags_.begin(), tags_.end(), [](const TagType& a, const TagType& b) {
        uint16_t code_a = a.template get_code<std::endian::native>();
        uint16_t code_b = b.template get_code<std::endian::native>();
        
        return code_a < code_b;
    });
    
    return Ok();
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
template <typename TagDesc, typename ValueType>
Result<void> IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::add_tag(
    const ValueType& value) noexcept {
    
    std::size_t current_external_offset = external_data_offset_;
    auto tag_result = create_tag_entry<TagDesc>(value, current_external_offset);
    
    if (tag_result.is_error()) {
        return tag_result.error();
    }
    
    tags_.push_back(tag_result.value());

    // Update external_data_offset_ with any new external data added
    external_data_offset_ = current_external_offset;
    
    // Re-sort tags. TODO: insert ordered instead.
    std::sort(tags_.begin(), tags_.end(), [](const TagType& a, const TagType& b) {
        uint16_t code_a = a.template get_code<std::endian::native>();
        uint16_t code_b = b.template get_code<std::endian::native>();
        
        return code_a < code_b;
    });
    
    return Ok();
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
void IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::set_external_data_offset(
    std::size_t offset) noexcept {
    external_data_offset_ = offset;
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
void IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::set_next_ifd_offset(
    ifd::IFDOffset offset) noexcept {
    next_ifd_offset_ = offset;
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
std::size_t IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::calculate_total_size() const noexcept {
    std::size_t ifd_size = ifd::IFD<TiffFormat, TargetEndian>::size_in_bytes(tags_.size());
    return ifd_size + external_data_.size();
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
std::size_t IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::calculate_ifd_size() const noexcept {
    return ifd::IFD<TiffFormat, TargetEndian>::size_in_bytes(tags_.size());
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
std::size_t IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::calculate_external_data_size() const noexcept {
    return external_data_.size();
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
Result<typename IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::IFDType> 
IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::build(ifd::IFDOffset ifd_offset) noexcept {
    IFDType ifd;
    ifd.description = ifd::IFDDescription<TiffFormat>(ifd_offset, tags_.size());
    ifd.tags = std::move(tags_);
    ifd.next_ifd_offset = next_ifd_offset_;
    
    return Ok(std::move(ifd));
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
std::span<const std::byte> IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::get_external_data() const noexcept {
    return external_data_;
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
template <typename Writer>
    requires RawWriter<Writer>
Result<ifd::IFDOffset> IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::write_to_file(
    Writer& writer,
    std::size_t current_file_size) noexcept {
    
    std::size_t ifd_size = calculate_ifd_size();
    std::size_t external_size = calculate_external_data_size();
    
    // Calculate offsets using placement strategy
    std::size_t ifd_offset = placement_strategy_.calculate_ifd_offset(
        current_file_size, ifd_size, external_size
    );
    
    std::size_t external_offset = placement_strategy_.calculate_external_data_offset(
        current_file_size, ifd_offset, ifd_size, external_size
    );
    
    // Calculate total size needed
    std::size_t max_offset = std::max(ifd_offset + ifd_size, 
                                    external_offset + external_size);
    
    // Resize writer if needed
    auto size_result = writer.size();
    if (size_result.is_error()) {
        return Err(size_result.error().code, "Failed to get writer size: " + size_result.error().message);
    }
    
    if (size_result.value() < max_offset) {
        auto resize_result = writer.resize(max_offset);
        if (resize_result.is_error()) {
            return Err(resize_result.error().code, "Failed to resize writer: " + resize_result.error().message);
        }
    }
    
    // Update tag offsets to point to correct external data location
    for (auto& tag : tags_) {
        TiffDataType dtype = tag.template get_datatype<std::endian::native>();
        std::size_t type_size = tiff_type_size(dtype);
        
        std::size_t count = tag.template get_count<std::endian::native>();
        
        std::size_t byte_size = count * type_size;
        constexpr std::size_t inline_limit = (TiffFormat == TiffFormatType::Classic) ? 4 : 8;
        
        // If data is external, update the offset
        if (byte_size > inline_limit) {
            // Get the relative offset within external_data_
            OffsetType relative_offset;
            if constexpr (TargetEndian == std::endian::native) {
                relative_offset = tag.value.offset;
            } else {
                relative_offset = byteswap(tag.value.offset);
            }
            
            // Update to absolute file offset
            OffsetType absolute_offset = static_cast<OffsetType>(external_offset + relative_offset);
            
            if constexpr (TargetEndian == std::endian::native) {
                tag.value.offset = absolute_offset;
            } else {
                tag.value.offset = byteswap(absolute_offset);
            }
        }
    }
    
    // Build IFD (this moves tags_)
    auto ifd_result = build(ifd::IFDOffset(ifd_offset));
    if (ifd_result.is_error()) {
        return ifd_result.error();
    }
    
    auto& ifd = ifd_result.value();
    
    // Write IFD
    auto ifd_bytes = ifd.write();
    auto ifd_view_result = writer.write(ifd_offset, ifd_bytes.size());
    if (ifd_view_result.is_error()) {
        return Err(ifd_view_result.error().code, "Failed to write IFD: " + ifd_view_result.error().message);
    }
    
    auto ifd_view = std::move(ifd_view_result.value());
    std::memcpy(ifd_view.data().data(), ifd_bytes.data(), ifd_bytes.size());
    auto ifd_flush = ifd_view.flush();
    if (ifd_flush.is_error()) {
        return Err(ifd_flush.error().code, "Failed to flush IFD: " + ifd_flush.error().message);
    }
    
    // Write external data if any
    if (!external_data_.empty()) {
        auto external_view_result = writer.write(external_offset, external_data_.size());
        if (external_view_result.is_error()) {
            return Err(external_view_result.error().code, "Failed to write external data: " + external_view_result.error().message);
        }
        
        auto external_view = std::move(external_view_result.value());
        std::memcpy(external_view.data().data(), external_data_.data(), external_data_.size());
        auto external_flush = external_view.flush();
        if (external_flush.is_error()) {
            return Err(external_flush.error().code, "Failed to flush external data: " + external_flush.error().message);
        }
    }
    
    return Ok(ifd::IFDOffset(ifd_offset));
}

template <TiffFormatType TiffFormat, std::endian TargetEndian, typename IFDPlacement>
    requires IFDPlacementStrategy<IFDPlacement>
void IFDBuilder<TiffFormat, TargetEndian, IFDPlacement>::clear() noexcept {
    tags_.clear();
    external_data_.clear();
    external_data_offset_ = 0;
    next_ifd_offset_ = ifd::IFDOffset();
}

} // namespace tiffconcept