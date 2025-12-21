#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <vector>
#include "tag_codes.hpp"
#include "tiff_spec.hpp"

namespace tiffconcept {

/// Helpers for tag spec validation
namespace detail {

/// Get the reference C++ type for each TiffDataType
template <TiffDataType Type>
struct tiff_reference_type;

template <> struct tiff_reference_type<TiffDataType::Byte> { using type = uint8_t; };
template <> struct tiff_reference_type<TiffDataType::SByte> { using type = int8_t; };
template <> struct tiff_reference_type<TiffDataType::Ascii> { using type = char; };
template <> struct tiff_reference_type<TiffDataType::Undefined> { using type = uint8_t; };
template <> struct tiff_reference_type<TiffDataType::Short> { using type = uint16_t; };
template <> struct tiff_reference_type<TiffDataType::SShort> { using type = int16_t; };
template <> struct tiff_reference_type<TiffDataType::Long> { using type = uint32_t; };
template <> struct tiff_reference_type<TiffDataType::SLong> { using type = int32_t; };
template <> struct tiff_reference_type<TiffDataType::Float> { using type = float; };
template <> struct tiff_reference_type<TiffDataType::IFD> { using type = uint32_t; };
template <> struct tiff_reference_type<TiffDataType::Rational> { using type = uint32_t; }; // component type
template <> struct tiff_reference_type<TiffDataType::SRational> { using type = int32_t; }; // component type
template <> struct tiff_reference_type<TiffDataType::Double> { using type = double; };
template <> struct tiff_reference_type<TiffDataType::Long8> { using type = uint64_t; };
template <> struct tiff_reference_type<TiffDataType::SLong8> { using type = int64_t; };
template <> struct tiff_reference_type<TiffDataType::IFD8> { using type = uint64_t; };

template <TiffDataType Type>
using tiff_reference_type_t = typename tiff_reference_type<Type>::type;

/// Unwrap containers to get the base value type
template <typename T>
struct unwrap_container { using type = T; };

// Unwrap any range (vector, array, etc.). pairs and tuples are forbidden containers (no range).
template <typename T>
requires std::ranges::range<T> && (!std::is_same_v<T, std::string>)
struct unwrap_container<T> { 
    using type = typename unwrap_container<std::ranges::range_value_t<T>>::type; 
};

template <typename T>
using unwrap_container_t = typename unwrap_container<T>::type;

/// Check if two types are memory-compatible for memcpy
/// (same size, both trivially copyable, same alignment)
template <typename From, typename To>
concept MemoryCompatible = 
    std::is_trivially_copyable_v<From> &&
    std::is_trivially_copyable_v<To> &&
    sizeof(From) == sizeof(To) &&
    alignof(From) == alignof(To);

/// Helper to safely check if a type has tuple_size
template <typename T, typename = void>
struct has_tuple_size : std::false_type {};

template <typename T>
struct has_tuple_size<T, std::void_t<decltype(std::tuple_size<T>::value)>> : std::true_type {};

template <typename T>
inline constexpr bool has_tuple_size_v = has_tuple_size<T>::value;

/// Check if a type is a valid rational representation for the given component type
template <typename T, typename ComponentType>
consteval bool is_valid_rational_type() {
    // Check for Rational or SRational types first
    if constexpr (std::is_same_v<T, Rational> && std::is_same_v<ComponentType, uint32_t>) {
        return true;
    }
    if constexpr (std::is_same_v<T, SRational> && std::is_same_v<ComponentType, int32_t>) {
        return true;
    }
    
    // Must be trivially copyable
    if (!std::is_trivially_copyable_v<T>) {
        return false;
    }
    
    // Check size: should be exactly 2 * component size
    if (sizeof(T) != 2 * sizeof(ComponentType)) {
        return false;
    }
    
    // If it has tuple_size == 2, check tuple elements
    if constexpr (has_tuple_size_v<T>) {
        if constexpr (std::tuple_size_v<T> == 2) {
            using First = std::tuple_element_t<0, T>;
            using Second = std::tuple_element_t<1, T>;
            return MemoryCompatible<First, ComponentType> && 
                   MemoryCompatible<Second, ComponentType> &&
                   std::is_same_v<First, Second>; // Both components same type
        }
        return false;
    }
    
    // For other aggregates (e.g., struct { uint32_t a; uint32_t b; }),
    // we accept if size and trivial copyability match
    if constexpr (std::is_aggregate_v<T>) {
        // Size check already passed above, and it's trivially copyable
        return true;
    }
    
    return false;
}

/// Helper to check if a type is a container of strings
template <typename T>
struct is_string_container : std::false_type {};

template <typename T>
    requires std::ranges::range<T> && (!std::is_same_v<T, std::string>)
struct is_string_container<T> : std::is_same<std::ranges::range_value_t<T>, std::string> {};

template <typename T>
inline constexpr bool is_string_container_v = is_string_container<T>::value;

/// Validate that a TagDescriptor's value_type matches its TiffDataType
template <typename TagDesc>
consteval bool validate_tag_descriptor() {
    using ValueType = typename TagDesc::value_type;
    constexpr TiffDataType datatype = TagDesc::datatype;
    using RefType = tiff_reference_type_t<datatype>;
    
    // Special case: std::string for Ascii/Byte/Undefined
    if constexpr (std::is_same_v<ValueType, std::string>) {
        return datatype == TiffDataType::Ascii || datatype == TiffDataType::Byte || datatype == TiffDataType::Undefined;
    }

    using BaseType = unwrap_container_t<ValueType>;

    // Accept containers of strings for Ascii
    if constexpr (std::is_same_v<BaseType, std::string>) { // maybe use is_string_container_v ?
        return datatype == TiffDataType::Ascii;
    }
    
    // For rational types: ValueType should be a valid rational representation
    if constexpr (datatype == TiffDataType::Rational || datatype == TiffDataType::SRational) {
        return is_valid_rational_type<BaseType, RefType>();
    }

    // Check that type is memory-compatible with reference type
    return MemoryCompatible<BaseType, RefType>;
}

/// Validate that alternate types are not promoting rationals
template <typename TagDesc>
consteval bool validate_no_rational_promotion() {
    constexpr TiffDataType primary_type = TagDesc::datatype;
    constexpr auto alternate_types = TagDesc::alternate_types;
    
    constexpr bool primary_is_rational = 
        (primary_type == TiffDataType::Rational || primary_type == TiffDataType::SRational);

    if (primary_is_rational) {
        // Disallow any alternate types if primary is rational
        return alternate_types.empty();
    }

    return true;
}

/// Check if a type is a container (vector or array, but not string or rational)
template <typename T>
consteval bool is_container_type() {
    // String is not a container for our purposes
    if constexpr (std::is_same_v<T, std::string>) {
        return false;
    }
    // Rational types are not containers
    if constexpr (std::is_same_v<T, Rational> || std::is_same_v<T, SRational>) {
        return false;
    }
    // Check if it's a range (vector, array, etc.)
    if constexpr (std::ranges::range<T>) {
        return true;
    }
    return false;
}

} // namespace detail

/// Tag descriptor - defines a TIFF tag at compile time
template <TagCode Code, TiffDataType Type, typename ValueType, bool Optional = false, TiffDataType... AltTypes>
struct TagDescriptor {
    static constexpr TagCode code = Code;
    static constexpr TiffDataType datatype = Type;
    static constexpr bool is_optional = Optional;
    static constexpr std::array<TiffDataType, sizeof...(AltTypes)> alternate_types = {AltTypes...}; // Alternate accepted data types when reading, promoted to TiffDataType
    static constexpr bool is_container = detail::is_container_type<ValueType>();
    static constexpr bool is_rational = (Type == TiffDataType::Rational || Type == TiffDataType::SRational);
    using value_type = ValueType;
    using storage_type = std::conditional_t<Optional, std::optional<ValueType>, ValueType>;
    //using element_type = detail::unwrap_container_t<ValueType>; // value_type if not a container, else the type of the contained elements
    using element_type = std::conditional_t<
        std::is_same_v<ValueType, std::string>,
        char, // map string to char element type
        detail::unwrap_container_t<ValueType>
    >;
    using reference_type = detail::tiff_reference_type_t<Type>; // Reference integer C++ type for the primary TiffDataType. needed for endian conversion.

    // Compile-time validation
    static_assert(detail::validate_tag_descriptor<TagDescriptor>(),
                  "TagDescriptor validation failed: "
                  "value_type must be memory-compatible (same size, trivially copyable) with TiffDataType. "
                  "Transparent wrappers (single-member structs) are allowed. "
                  "For Rational/SRational, use pair-like types with matching component types.");
    
    static_assert(detail::validate_no_rational_promotion<TagDescriptor>(),
                  "TagDescriptor has invalid alternate types: "
                  "rational types (Rational/SRational) do not handle promotion.");
};

/// Helper to create optional variant of a tag descriptor
template <TagCode Code, TiffDataType Type, typename ValueType, bool Optional, TiffDataType... AltTypes>
constexpr auto OptTag(TagDescriptor<Code, Type, ValueType, Optional, AltTypes...>) {
    return TagDescriptor<Code, Type, ValueType, true, AltTypes...>{};
}

/// Template alias for optional tag descriptor types
template <typename TagDesc>
using OptTag_t = decltype(OptTag(TagDesc{}));

/// Helper to extract value type from a tag descriptor
template <typename T>
struct tag_value_type;

template <TagCode Code, TiffDataType Type, typename ValueType, bool Optional, TiffDataType... AltTypes>
struct tag_value_type<TagDescriptor<Code, Type, ValueType, Optional, AltTypes...>> {
    using type = ValueType;
};

template <typename T>
using tag_value_type_t = typename tag_value_type<T>::type;

/// Helper to extract storage type from a tag descriptor
template <typename T>
struct tag_storage_type;

template <TagCode Code, TiffDataType Type, typename ValueType, bool Optional, TiffDataType... AltTypes>
struct tag_storage_type<TagDescriptor<Code, Type, ValueType, Optional, AltTypes...>> {
    using type = typename TagDescriptor<Code, Type, ValueType, Optional, AltTypes...>::storage_type;
};

template <typename T>
using tag_storage_type_t = typename tag_storage_type<T>::type;

/// Concept to check if a type is a TagDescriptor
template <typename T>
concept TagDescriptorType = requires {
    { T::code } -> std::convertible_to<TagCode>;
    { T::datatype } -> std::convertible_to<TiffDataType>;
    { T::is_optional } -> std::convertible_to<bool>;
    T::alternate_types;
    typename T::value_type;
    typename T::storage_type;
};

/// Type trait to check if a tag is in a list
template <TagCode Code, typename... Tags>
struct has_tag : std::false_type {};

template <TagCode Code, typename First, typename... Rest>
struct has_tag<Code, First, Rest...> 
    : std::conditional_t<First::code == Code, std::true_type, has_tag<Code, Rest...>> {};

/// Get tag descriptor by code from a list
template <TagCode Code, typename... Tags>
struct get_tag;

template <TagCode Code, typename First, typename... Rest>
struct get_tag<Code, First, Rest...> {
    using type = std::conditional_t<First::code == Code, First, typename get_tag<Code, Rest...>::type>;
};

template <TagCode Code>
struct get_tag<Code> {
    using type = void;
};

template <TagCode Code, typename... Tags>
using get_tag_t = typename get_tag<Code, Tags...>::type;


/// Helper to check if tag codes are sorted at compile time
template <TagCode... Codes>
consteval bool are_codes_sorted() {
    if constexpr (sizeof...(Codes) > 0) {
        constexpr std::array codes = {Codes...};
        for (std::size_t i = 1; i < codes.size(); ++i) {
            if (codes[i] <= codes[i-1]) {
                return false;
            }
        }
    }
    return true;
}

/// Compile-time tag specification
template <TagDescriptorType... Tags>
struct TagSpec {
    static constexpr std::size_t num_tags = sizeof...(Tags);

    // Compile-time check that tags are sorted by code
    static_assert(are_codes_sorted<Tags::code...>(),
                  "Tags must be sorted by TagCode in ascending order");

    /// Check if a tag code is in this spec
    template <TagCode Code>
    static constexpr bool has_tag() noexcept {
        return ((Tags::code == Code) || ...);
    }

    /// Get tag descriptor by code
    template <TagCode Code>
    using get_tag = get_tag_t<Code, Tags...>;
    
    /// Apply a function to each tag descriptor at compile time
    template <typename F>
    static constexpr void for_each(F&& func) {
        (func.template operator()<Tags>(), ...);
    }
};

namespace detail {
    template <typename T>
    struct is_tag_spec : std::false_type {};
    
    template <TagDescriptorType... Tags>
    struct is_tag_spec<TagSpec<Tags...>> : std::true_type {};
    
    template <typename T>
    inline constexpr bool is_tag_spec_v = is_tag_spec<std::remove_cvref_t<T>>::value;
}

/// Concept to validate TagSpec structure at compile time
template <typename T>
concept ValidTagSpec = requires {
    // Must have num_tags
    { T::num_tags } -> std::convertible_to<std::size_t>;

    // zero tags is allowed as it avoid branching in some code paths
    requires T::num_tags >= 0;
} && detail::is_tag_spec_v<T>; // Must be instantiation of TagSpec template

// Common tag descriptors




/// Concept to check if a TagSpec contains all required tags for querying image dimensions
template <typename TSpec>
concept ImageDimensionTagSpec = ValidTagSpec<TSpec> &&
    TSpec::template has_tag<TagCode::ImageWidth>() &&
    TSpec::template has_tag<TagCode::ImageLength>() &&
    TSpec::template has_tag<TagCode::BitsPerSample>();

/// Concept to check if a TagSpec contains all required tags for stripped images
template <typename TSpec>
concept StrippedImageTagSpec = ImageDimensionTagSpec<TSpec> &&
    TSpec::template has_tag<TagCode::Compression>() &&
    TSpec::template has_tag<TagCode::RowsPerStrip>() &&
    TSpec::template has_tag<TagCode::StripOffsets>() &&
    TSpec::template has_tag<TagCode::StripByteCounts>();

/// Concept to check if a TagSpec contains all required tags for tiled images
template <typename TSpec>
concept TiledImageTagSpec = ImageDimensionTagSpec<TSpec> &&
    TSpec::template has_tag<TagCode::Compression>() &&
    TSpec::template has_tag<TagCode::TileWidth>() &&
    TSpec::template has_tag<TagCode::TileLength>() &&
    TSpec::template has_tag<TagCode::TileOffsets>() &&
    TSpec::template has_tag<TagCode::TileByteCounts>();


} // namespace tiffconcept
