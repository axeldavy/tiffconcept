#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <vector>
#include "types.hpp"

namespace tiff {

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

/// Validate that a TagDescriptor's value_type matches its TiffDataType
template <typename TagDesc>
consteval bool validate_tag_descriptor() {
    using ValueType = typename TagDesc::value_type;
    constexpr TiffDataType datatype = TagDesc::datatype;
    using RefType = tiff_reference_type_t<datatype>;
    
    // Special case: std::string for Ascii/Undefined
    if constexpr (std::is_same_v<ValueType, std::string>) {
        return datatype == TiffDataType::Ascii || datatype == TiffDataType::Undefined;
    }

    using BaseType = unwrap_container_t<ValueType>;
    
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
    using element_type = detail::unwrap_container_t<ValueType>; // value_type if not a container, else the type of the contained elements
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
    constexpr std::array codes = {Codes...};
    for (std::size_t i = 1; i < codes.size(); ++i) {
        if (codes[i] <= codes[i-1]) {
            return false;
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

    // Must have at least one tag
    requires T::num_tags > 0;
} && detail::is_tag_spec_v<T>; // Must be instantiation of TagSpec template

// Common tag descriptors

// Common tag descriptors (TIFF 6.0 specification) + libtiff documentation
// They are sorted by order of tag code. Keep TagCode order when building a TagSpec.

// Baseline tags
using NewSubfileTypeTag = TagDescriptor<TagCode::NewSubfileType, TiffDataType::Long, uint32_t, false>; // Default: 0
using SubfileTypeTag = TagDescriptor<TagCode::SubfileType, TiffDataType::Short, uint16_t, false>; // Deprecated
using ImageWidthTag = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t, false, TiffDataType::Short>;
using ImageLengthTag = TagDescriptor<TagCode::ImageLength, TiffDataType::Long, uint32_t, false, TiffDataType::Short>;
using BitsPerSampleTag = TagDescriptor<TagCode::BitsPerSample, TiffDataType::Short, std::vector<uint16_t>, false>; // Default: 1
using CompressionTag = TagDescriptor<TagCode::Compression, TiffDataType::Short, CompressionScheme, false>; // Default: 1 (None)
using PhotometricInterpretationTag = TagDescriptor<TagCode::PhotometricInterpretation, TiffDataType::Short, PhotometricInterpretation, false>; // 0: MinIsWhite, 1: MinIsBlack, 2: RGB, 3: Palette
using ThreshholdingTag = TagDescriptor<TagCode::Threshholding, TiffDataType::Short, uint16_t, false>; // Default: 1
using CellWidthTag = TagDescriptor<TagCode::CellWidth, TiffDataType::Short, uint16_t, false>;
using CellLengthTag = TagDescriptor<TagCode::CellLength, TiffDataType::Short, uint16_t, false>;
using FillOrderTag = TagDescriptor<TagCode::FillOrder, TiffDataType::Short, uint16_t, false>; // Default: 1, 1=MSB first, 2=LSB first
using DocumentNameTag = TagDescriptor<TagCode::DocumentName, TiffDataType::Ascii, std::string, false>;
using ImageDescriptionTag = TagDescriptor<TagCode::ImageDescription, TiffDataType::Ascii, std::string, false>;
using MakeTag = TagDescriptor<TagCode::Make, TiffDataType::Ascii, std::string, false>;
using ModelTag = TagDescriptor<TagCode::Model, TiffDataType::Ascii, std::string, false>;
using StripOffsetsTag = TagDescriptor<TagCode::StripOffsets, TiffDataType::Long, std::vector<uint32_t>, false, TiffDataType::Short>;
using OrientationTag = TagDescriptor<TagCode::Orientation, TiffDataType::Short, uint16_t, false>; // Default: 1 (top-left)
using SamplesPerPixelTag = TagDescriptor<TagCode::SamplesPerPixel, TiffDataType::Short, uint16_t, false>; // Default: 1
using RowsPerStripTag = TagDescriptor<TagCode::RowsPerStrip, TiffDataType::Long, uint32_t, false, TiffDataType::Short>; // Default: 2^32-1
using StripByteCountsTag = TagDescriptor<TagCode::StripByteCounts, TiffDataType::Long, std::vector<uint32_t>, false, TiffDataType::Short>;
using MinSampleValueTag = TagDescriptor<TagCode::MinSampleValue, TiffDataType::Short, std::vector<uint16_t>, false>; // Default: 0
using MaxSampleValueTag = TagDescriptor<TagCode::MaxSampleValue, TiffDataType::Short, std::vector<uint16_t>, false>; // Default: 2^BitsPerSample - 1
using XResolutionTag = TagDescriptor<TagCode::XResolution, TiffDataType::Rational, Rational, false>;
using YResolutionTag = TagDescriptor<TagCode::YResolution, TiffDataType::Rational, Rational, false>;
using PlanarConfigurationTag = TagDescriptor<TagCode::PlanarConfiguration, TiffDataType::Short, uint16_t, false>; // Default: 1 (chunky), 2=planar
using PageNameTag = TagDescriptor<TagCode::PageName, TiffDataType::Ascii, std::string, false>;
using XPositionTag = TagDescriptor<TagCode::XPosition, TiffDataType::Rational, Rational, false>;
using YPositionTag = TagDescriptor<TagCode::YPosition, TiffDataType::Rational, Rational, false>;
using FreeOffsetsTag = TagDescriptor<TagCode::FreeOffsets, TiffDataType::Long, std::vector<uint32_t>, false>;
using FreeByteCountsTag = TagDescriptor<TagCode::FreeByteCounts, TiffDataType::Long, std::vector<uint32_t>, false>;
using GrayResponseUnitTag = TagDescriptor<TagCode::GrayResponseUnit, TiffDataType::Short, uint16_t, false>; // Default: 2 (hundredths)
using GrayResponseCurveTag = TagDescriptor<TagCode::GrayResponseCurve, TiffDataType::Short, std::vector<uint16_t>, false>;
using T4OptionsTag = TagDescriptor<TagCode::T4Options, TiffDataType::Long, uint32_t, false>; // Default: 0
using T6OptionsTag = TagDescriptor<TagCode::T6Options, TiffDataType::Long, uint32_t, false>; // Default: 0
using ResolutionUnitTag = TagDescriptor<TagCode::ResolutionUnit, TiffDataType::Short, uint16_t, false>; // Default: 2 (inches), 1=none, 3=cm
using PageNumberTag = TagDescriptor<TagCode::PageNumber, TiffDataType::Short, std::array<uint16_t, 2>, false>;
using ColorResponseUnitTag = TagDescriptor<TagCode::ColorResponseUnit, TiffDataType::Short, uint16_t, false>;
using TransferFunctionTag = TagDescriptor<TagCode::TransferFunction, TiffDataType::Short, std::vector<uint16_t>, false>;
using SoftwareTag = TagDescriptor<TagCode::Software, TiffDataType::Ascii, std::string, false>;
using DateTimeTag = TagDescriptor<TagCode::DateTime, TiffDataType::Ascii, std::string, false>; // Format: "YYYY:MM:DD HH:MM:SS"
using ArtistTag = TagDescriptor<TagCode::Artist, TiffDataType::Ascii, std::string, false>;
using HostComputerTag = TagDescriptor<TagCode::HostComputer, TiffDataType::Ascii, std::string, false>;
using PredictorTag = TagDescriptor<TagCode::Predictor, TiffDataType::Short, Predictor, false>; // Default: 1 (none)
using WhitePointTag = TagDescriptor<TagCode::WhitePoint, TiffDataType::Rational, std::array<Rational, 2>, false>;
using PrimaryChromaticitiesTag = TagDescriptor<TagCode::PrimaryChromaticities, TiffDataType::Rational, std::array<Rational, 6>, false>;
using ColorMapTag = TagDescriptor<TagCode::ColorMap, TiffDataType::Short, std::vector<uint16_t>, false>;
using HalftoneHintsTag = TagDescriptor<TagCode::HalftoneHints, TiffDataType::Short, std::array<uint16_t, 2>, false>;
using TileWidthTag = TagDescriptor<TagCode::TileWidth, TiffDataType::Long, uint32_t, false, TiffDataType::Short>;
using TileLengthTag = TagDescriptor<TagCode::TileLength, TiffDataType::Long, uint32_t, false, TiffDataType::Short>;
using TileOffsetsTag = TagDescriptor<TagCode::TileOffsets, TiffDataType::Long, std::vector<uint32_t>, false, TiffDataType::Short>;
using TileByteCountsTag = TagDescriptor<TagCode::TileByteCounts, TiffDataType::Long, std::vector<uint32_t>, false, TiffDataType::Short>;
using BadFaxLinesTag = TagDescriptor<TagCode::BadFaxLines, TiffDataType::Long, uint32_t, false>;
using CleanFaxDataTag = TagDescriptor<TagCode::CleanFaxData, TiffDataType::Short, uint16_t, false>; // 0=clean, 1=regenerated, 2=unclean
using ConsecutiveBadFaxLinesTag = TagDescriptor<TagCode::ConsecutiveBadFaxLines, TiffDataType::Long, uint32_t, false>;
using SubIFDTag = TagDescriptor<TagCode::SubIFD, TiffDataType::IFD, std::vector<uint32_t>, false, TiffDataType::Short, TiffDataType::Long>;
using InkSetTag = TagDescriptor<TagCode::InkSet, TiffDataType::Short, uint16_t, false>; // Default: 1 (CMYK), 2=not CMYK
using InkNamesTag = TagDescriptor<TagCode::InkNames, TiffDataType::Ascii, std::string, false>;
using NumberOfInksTag = TagDescriptor<TagCode::NumberOfInks, TiffDataType::Short, uint16_t, false>; // Default: 4
using DotRangeTag = TagDescriptor<TagCode::DotRange, TiffDataType::Byte, std::vector<uint8_t>, false>;
using TargetPrinterTag = TagDescriptor<TagCode::TargetPrinter, TiffDataType::Ascii, std::string, false>;
using ExtraSamplesTag = TagDescriptor<TagCode::ExtraSamples, TiffDataType::Byte, std::vector<uint8_t>, false>;
using SampleFormatTag = TagDescriptor<TagCode::SampleFormat, TiffDataType::Short, SampleFormat, false>; // Default: 1 (unsigned). 2=signed, 3=float, 4=undefined
using SMinSampleValueTag = TagDescriptor<TagCode::SMinSampleValue, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using SMaxSampleValueTag = TagDescriptor<TagCode::SMaxSampleValue, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using TransferRangeTag = TagDescriptor<TagCode::TransferRange, TiffDataType::Short, std::array<uint16_t, 6>, false>;
using ClipPathTag = TagDescriptor<TagCode::ClipPath, TiffDataType::Byte, std::vector<uint8_t>, false>;
using XClipPathUnitsTag = TagDescriptor<TagCode::XClipPathUnits, TiffDataType::Long, uint32_t, false>;
using YClipPathUnitsTag = TagDescriptor<TagCode::YClipPathUnits, TiffDataType::Long, uint32_t, false>;
using IndexedTag = TagDescriptor<TagCode::Indexed, TiffDataType::Short, uint16_t, false>;
using JPEGTablesTag = TagDescriptor<TagCode::JPEGTables, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using GlobalParametersIFDTag = TagDescriptor<TagCode::GlobalParametersIFD, TiffDataType::Long, uint32_t, false>;
using ProfileTypeTag = TagDescriptor<TagCode::ProfileType, TiffDataType::Long, uint32_t, false>; // 0=unspecified, 1=G3Fax
using FaxProfileTag = TagDescriptor<TagCode::FaxProfile, TiffDataType::Short, uint16_t, false>; // 0=unknown, 1=minimal, 2=extended
using CodingMethodsTag = TagDescriptor<TagCode::CodingMethods, TiffDataType::Long, uint32_t, false>; // Bit flags
using VersionYearTag = TagDescriptor<TagCode::VersionYear, TiffDataType::Byte, std::array<uint8_t, 4>, false>;
using ModeNumberTag = TagDescriptor<TagCode::ModeNumber, TiffDataType::Byte, uint8_t, false>;
using DecodeTag = TagDescriptor<TagCode::Decode, TiffDataType::SRational, std::vector<SRational>, false>;
using ImageBaseColorTag = TagDescriptor<TagCode::ImageBaseColor, TiffDataType::Short, std::vector<uint16_t>, false>;
using T82OptionsTag = TagDescriptor<TagCode::T82Options, TiffDataType::Long, uint32_t, false>;

// JPEG tags
using JPEGProcTag = TagDescriptor<TagCode::JPEGProc, TiffDataType::Short, uint16_t, false>; // 1=baseline, 14=lossless
using JPEGInterchangeFormatTag = TagDescriptor<TagCode::JPEGInterchangeFormat, TiffDataType::Long, uint32_t, false>;
using JPEGInterchangeFormatLengthTag = TagDescriptor<TagCode::JPEGInterchangeFormatLngth, TiffDataType::Long, uint32_t, false>;
using JPEGRestartIntervalTag = TagDescriptor<TagCode::JPEGRestartInterval, TiffDataType::Short, uint16_t, false>;
using JPEGLosslessPredictorsTag = TagDescriptor<TagCode::JPEGLosslessPredictors, TiffDataType::Short, std::vector<uint16_t>, false>;
using JPEGPointTransformsTag = TagDescriptor<TagCode::JPEGPointTransforms, TiffDataType::Short, std::vector<uint16_t>, false>;
using JPEGQTablesTag = TagDescriptor<TagCode::JPEGQTables, TiffDataType::Long, std::vector<uint32_t>, false>;
using JPEGDCTablesTag = TagDescriptor<TagCode::JPEGDCTables, TiffDataType::Long, std::vector<uint32_t>, false>;
using JPEGACTablesTag = TagDescriptor<TagCode::JPEGACTables, TiffDataType::Long, std::vector<uint32_t>, false>;

// YCbCr tags
using YCbCrCoefficientsTag = TagDescriptor<TagCode::YCbCrCoefficients, TiffDataType::Rational, std::array<Rational, 3>, false>;
using YCbCrSubSamplingTag = TagDescriptor<TagCode::YCbCrSubSampling, TiffDataType::Short, std::array<uint16_t, 2>, false>; // Default: [2,2]
using YCbCrPositioningTag = TagDescriptor<TagCode::YCbCrPositioning, TiffDataType::Short, uint16_t, false>; // Default: 1 (centered)
using ReferenceBlackWhiteTag = TagDescriptor<TagCode::ReferenceBlackWhite, TiffDataType::Long, std::vector<uint32_t>, false>;

// Additional tags
using StripRowCountsTag = TagDescriptor<TagCode::StripRowCounts, TiffDataType::Long, std::vector<uint32_t>, false>;
using XMLPacketTag = TagDescriptor<TagCode::XMLPacket, TiffDataType::Byte, std::vector<uint8_t>, false>;

// Private/Extended tags
using MatteingTag = TagDescriptor<TagCode::Matteing, TiffDataType::Short, uint16_t, false>; // Obsolete, use ExtraSamples
using DataTypeTag = TagDescriptor<TagCode::DataType, TiffDataType::Short, uint16_t, false>; // Obsolete, use SampleFormat
using ImageDepthTag = TagDescriptor<TagCode::ImageDepth, TiffDataType::Long, uint32_t, false, TiffDataType::Short>; // Default: 1
using TileDepthTag = TagDescriptor<TagCode::TileDepth, TiffDataType::Long, uint32_t, false, TiffDataType::Short>; // Default: 1
using ImageFullWidthTag = TagDescriptor<TagCode::ImageFullWidth, TiffDataType::Long, uint32_t, false>;
using ImageFullLengthTag = TagDescriptor<TagCode::ImageFullLength, TiffDataType::Long, uint32_t, false>;
using TextureFormatTag = TagDescriptor<TagCode::TextureFormat, TiffDataType::Ascii, std::string, false>;
using TextureWrapModesTag = TagDescriptor<TagCode::TextureWrapModes, TiffDataType::Ascii, std::string, false>;
using FieldOfViewCotangentTag = TagDescriptor<TagCode::FieldOfViewCotangent, TiffDataType::Float, float, false>;
using MatrixWorldToScreenTag = TagDescriptor<TagCode::MatrixWorldToScreen, TiffDataType::Float, std::vector<float>, false>;
using MatrixWorldToCameraTag = TagDescriptor<TagCode::MatrixWorldToCamera, TiffDataType::Float, std::vector<float>, false>;
using CopyrightTag = TagDescriptor<TagCode::Copyright, TiffDataType::Ascii, std::string, false>;
using RichTIFFIPTCTag = TagDescriptor<TagCode::RichTIFFIPTC, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using PhotoshopTag = TagDescriptor<TagCode::Photoshop, TiffDataType::Byte, std::vector<uint8_t>, false>;
using EXIFIFDOffsetTag = TagDescriptor<TagCode::EXIFIFDOffset, TiffDataType::Long, uint32_t, false>;
using ICCProfileTag = TagDescriptor<TagCode::ICCProfile, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using ImageLayerTag = TagDescriptor<TagCode::ImageLayer, TiffDataType::Long, std::vector<uint32_t>, false>;
using GPSIFDOffsetTag = TagDescriptor<TagCode::GPSIFDOffset, TiffDataType::Long, uint32_t, false>;
using FaxRecvParamsTag = TagDescriptor<TagCode::FaxRecvParams, TiffDataType::Long, uint32_t, false>;
using FaxSubAddressTag = TagDescriptor<TagCode::FaxSubAddress, TiffDataType::Ascii, std::string, false>;
using FaxRecvTimeTag = TagDescriptor<TagCode::FaxRecvTime, TiffDataType::Long, uint32_t, false>;
using FaxDcsTag = TagDescriptor<TagCode::FaxDcs, TiffDataType::Ascii, std::string, false>;
using StoNitsTag = TagDescriptor<TagCode::StoNits, TiffDataType::Double, double, false>;
using PhotoshopDocDataBlockTag = TagDescriptor<TagCode::PhotoshopDocDataBlock, TiffDataType::Byte, std::vector<uint8_t>, false>;
using InteroperabilityIFDOffsetTag = TagDescriptor<TagCode::InteroperabilityIFDOffset, TiffDataType::Long, uint32_t, false>;

// DNG tags
using DNGVersionTag = TagDescriptor<TagCode::DNGVersion, TiffDataType::Byte, std::array<uint8_t, 4>, false>;
using DNGBackwardVersionTag = TagDescriptor<TagCode::DNGBackwardVersion, TiffDataType::Byte, std::array<uint8_t, 4>, false>;
using UniqueCameraModelTag = TagDescriptor<TagCode::UniqueCameraModel, TiffDataType::Ascii, std::string, false>;
using LocalizedCameraModelTag = TagDescriptor<TagCode::LocalizedCameraModel, TiffDataType::Ascii, std::string, false>;
using CFAPlaneColorTag = TagDescriptor<TagCode::CFAPlaneColor, TiffDataType::Byte, std::vector<uint8_t>, false>;
using CFALayoutTag = TagDescriptor<TagCode::CFALayout, TiffDataType::Short, uint16_t, false>;
using LinearizationTableTag = TagDescriptor<TagCode::LinearizationTable, TiffDataType::Short, std::vector<uint16_t>, false>;
using BlackLevelRepeatDimTag = TagDescriptor<TagCode::BlackLevelRepeatDim, TiffDataType::Short, std::array<uint16_t, 2>, false>;
using BlackLevelTag = TagDescriptor<TagCode::BlackLevel, TiffDataType::Rational, std::vector<Rational>, false>;
using BlackLevelDeltaHTag = TagDescriptor<TagCode::BlackLevelDeltaH, TiffDataType::SRational, std::vector<SRational>, false>;
using BlackLevelDeltaVTag = TagDescriptor<TagCode::BlackLevelDeltaV, TiffDataType::SRational, std::vector<SRational>, false>;
using WhiteLevelTag = TagDescriptor<TagCode::WhiteLevel, TiffDataType::Long, std::vector<uint32_t>, false>;
using DefaultScaleTag = TagDescriptor<TagCode::DefaultScale, TiffDataType::Rational, std::array<Rational, 2>, false>;
using DefaultCropOriginTag = TagDescriptor<TagCode::DefaultCropOrigin, TiffDataType::Rational, std::array<Rational, 2>, false>;
using DefaultCropSizeTag = TagDescriptor<TagCode::DefaultCropSize, TiffDataType::Rational, std::array<Rational, 2>, false>;
using ColorMatrix1Tag = TagDescriptor<TagCode::ColorMatrix1, TiffDataType::SRational, std::vector<SRational>, false>;
using ColorMatrix2Tag = TagDescriptor<TagCode::ColorMatrix2, TiffDataType::SRational, std::vector<SRational>, false>;
using CameraCalibration1Tag = TagDescriptor<TagCode::CameraCalibration1, TiffDataType::SRational, std::vector<SRational>, false>;
using CameraCalibration2Tag = TagDescriptor<TagCode::CameraCalibration2, TiffDataType::SRational, std::vector<SRational>, false>;
using ReductionMatrix1Tag = TagDescriptor<TagCode::ReductionMatrix1, TiffDataType::SRational, std::vector<SRational>, false>;
using ReductionMatrix2Tag = TagDescriptor<TagCode::ReductionMatrix2, TiffDataType::SRational, std::vector<SRational>, false>;
using AnalogBalanceTag = TagDescriptor<TagCode::AnalogBalance, TiffDataType::Rational, std::vector<Rational>, false>;
using AsShotNeutralTag = TagDescriptor<TagCode::AsShotNeutral, TiffDataType::Rational, std::vector<Rational>, false>;
using AsShotWhiteXYTag = TagDescriptor<TagCode::AsShotWhiteXY, TiffDataType::Rational, std::array<Rational, 2>, false>;
using BaselineExposureTag = TagDescriptor<TagCode::BaselineExposure, TiffDataType::SRational, SRational, false>;
using BaselineNoiseTag = TagDescriptor<TagCode::BaselineNoise, TiffDataType::Rational, Rational, false>;
using BaselineSharpnessTag = TagDescriptor<TagCode::BaselineSharpness, TiffDataType::Rational, Rational, false>;
using BayerGreenSplitTag = TagDescriptor<TagCode::BayerGreenSplit, TiffDataType::Long, uint32_t, false>;
using LinearResponseLimitTag = TagDescriptor<TagCode::LinearResponseLimit, TiffDataType::Rational, Rational, false>;
using CameraSerialNumberTag = TagDescriptor<TagCode::CameraSerialNumber, TiffDataType::Ascii, std::string, false>;
using LensInfoTag = TagDescriptor<TagCode::LensInfo, TiffDataType::Rational, std::array<Rational, 4>, false>;
using ChromaBlurRadiusTag = TagDescriptor<TagCode::ChromaBlurRadius, TiffDataType::Rational, Rational, false>;
using AntiAliasStrengthTag = TagDescriptor<TagCode::AntiAliasStrength, TiffDataType::Rational, Rational, false>;
using ShadowScaleTag = TagDescriptor<TagCode::ShadowScale, TiffDataType::Rational, Rational, false>;
using DNGPrivateDataTag = TagDescriptor<TagCode::DNGPrivateData, TiffDataType::Byte, std::vector<uint8_t>, false>;
using MakerNoteSafetyTag = TagDescriptor<TagCode::MakerNoteSafety, TiffDataType::Short, uint16_t, false>;
using CalibrationIlluminant1Tag = TagDescriptor<TagCode::CalibrationIlluminant1, TiffDataType::Short, uint16_t, false>;
using CalibrationIlluminant2Tag = TagDescriptor<TagCode::CalibrationIlluminant2, TiffDataType::Short, uint16_t, false>;
using BestQualityScaleTag = TagDescriptor<TagCode::BestQualityScale, TiffDataType::Rational, Rational, false>;
using RawDataUniqueIDTag = TagDescriptor<TagCode::RawDataUniqueID, TiffDataType::Byte, std::array<uint8_t, 16>, false>;
using OriginalRawFileNameTag = TagDescriptor<TagCode::OriginalRawFileName, TiffDataType::Ascii, std::string, false>;
using OriginalRawFileDataTag = TagDescriptor<TagCode::OriginalRawFileData, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using ActiveAreaTag = TagDescriptor<TagCode::ActiveArea, TiffDataType::Long, std::array<uint32_t, 4>, false>;
using MaskedAreasTag = TagDescriptor<TagCode::MaskedAreas, TiffDataType::Long, std::vector<uint32_t>, false>;
using AsShotICCProfileTag = TagDescriptor<TagCode::AsShotICCProfile, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using AsShotPreProfileMatrixTag = TagDescriptor<TagCode::AsShotPreProfileMatrix, TiffDataType::SRational, std::vector<SRational>, false>;
using CurrentICCProfileTag = TagDescriptor<TagCode::CurrentICCProfile, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using CurrentPreProfileMatrixTag = TagDescriptor<TagCode::CurrentPreProfileMatrix, TiffDataType::SRational, std::vector<SRational>, false>;


// Bigtiff variants
// Offsets and byte counts use 8-byte Long8 type. IFD offsets are also Long8/IFD8

using StripOffsetsTag_BigTIFF = TagDescriptor<TagCode::StripOffsets, TiffDataType::Long8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long>;
using StripByteCountsTag_BigTIFF = TagDescriptor<TagCode::StripByteCounts, TiffDataType::Long8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long>;
using TileOffsetsTag_BigTIFF = TagDescriptor<TagCode::TileOffsets, TiffDataType::Long8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long>;
using TileByteCountsTag_BigTIFF = TagDescriptor<TagCode::TileByteCounts, TiffDataType::Long8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long>;
using SubIFDTag_BigTIFF = TagDescriptor<TagCode::SubIFD, TiffDataType::IFD8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long, TiffDataType::IFD, TiffDataType::Long8>;


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



// Tag specification examples:

// Minimal set for parsing simple strip-based images
using MinStrippedSpec = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    RowsPerStripTag,
    StripOffsetsTag,
    StripByteCountsTag,
    OptTag_t<SamplesPerPixelTag> ,// Default: 1
    OptTag_t<PredictorTag>, // Default: 1 (no predictor)
    OptTag_t<SampleFormatTag> // Default: 1 (unsigned)
>;

// Minimal set for parsing simple tile-based images
using MinTiledSpec = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    TileWidthTag,
    TileLengthTag,
    TileOffsetsTag,
    TileByteCountsTag,
    OptTag_t<SamplesPerPixelTag>, // Default: 1
    OptTag_t<PredictorTag>, // Default: 1 (no predictor)
    OptTag_t<SampleFormatTag> // Default: 1 (unsigned)
>;

// extended minimal set to cover most image types
using MinImageSpec = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    OptTag_t<PhotometricInterpretationTag>, // Color space interpretation (RGB, Grayscale, Palette, etc.)
    OptTag_t<FillOrderTag>, // Bit order within bytes (MSB/LSB first)
    OptTag_t<StripOffsetsTag>,
    OptTag_t<OrientationTag>, // Image orientation (rotation/flip)
    OptTag_t<RowsPerStripTag>,
    OptTag_t<StripByteCountsTag>,
    OptTag_t<SamplesPerPixelTag>, // Number of components per pixel
    OptTag_t<MinSampleValueTag>, // Minimum sample value per component
    OptTag_t<MaxSampleValueTag>, // Maximum sample value per component
    OptTag_t<XResolutionTag>, // Horizontal resolution
    OptTag_t<YResolutionTag>, // Vertical resolution
    OptTag_t<PlanarConfigurationTag>, // Chunky (1) vs Planar (2) layout - for multi-channel images
    OptTag_t<ResolutionUnitTag>, // Unit for resolution (inches, cm, none)
    OptTag_t<PredictorTag>, // Compression predictor (none, horizontal, floating point)
    OptTag_t<ColorMapTag>, // Palette lookup table (PhotometricInterpretation=3)
    OptTag_t<TileWidthTag>, // Tile width (for tiled images)
    OptTag_t<TileLengthTag>, // Tile height (for tiled images)
    OptTag_t<TileOffsetsTag>, // Tile data offsets (for tiled images)
    OptTag_t<TileByteCountsTag>, // Tile data sizes (for tiled images)
    OptTag_t<ExtraSamplesTag>, // Alpha channel or other extra components
    OptTag_t<SampleFormatTag>, // Data type (unsigned, signed, float, undefined)
    OptTag_t<SMinSampleValueTag>, // Minimum sample value for signed/float data
    OptTag_t<SMaxSampleValueTag>, // Maximum sample value for signed/float data
    OptTag_t<JPEGProcTag>, // JPEG compression type (baseline, lossless)
    OptTag_t<JPEGInterchangeFormatTag>, // Offset to JPEG interchange format stream
    OptTag_t<JPEGInterchangeFormatLengthTag>, // Length of JPEG interchange format stream
    OptTag_t<JPEGRestartIntervalTag>, // JPEG restart interval
    OptTag_t<JPEGLosslessPredictorsTag>, // JPEG lossless predictor selection values
    OptTag_t<JPEGPointTransformsTag>, // JPEG point transform values
    OptTag_t<JPEGQTablesTag>, // Offsets to JPEG quantization tables
    OptTag_t<JPEGDCTablesTag>, // Offsets to JPEG DC Huffman tables
    OptTag_t<JPEGACTablesTag>, // Offsets to JPEG AC Huffman tables
    OptTag_t<YCbCrCoefficientsTag>, // YCbCr to RGB transformation coefficients
    OptTag_t<YCbCrSubSamplingTag>, // YCbCr chroma subsampling factors
    OptTag_t<YCbCrPositioningTag>, // YCbCr sample positioning (centered/cosited)
    OptTag_t<ReferenceBlackWhiteTag>, // YCbCr reference black/white values
    OptTag_t<WhitePointTag>, // CIE white point for calibrated RGB
    OptTag_t<PrimaryChromaticitiesTag>, // CIE primaries for calibrated RGB
    OptTag_t<TransferFunctionTag>, // Gamma correction curve
    OptTag_t<ICCProfileTag>, // Embedded ICC color profile
    OptTag_t<JPEGTablesTag>, // JPEG quantization/Huffman tables for abbreviated streams
    OptTag_t<ImageDepthTag>, // Number of images in a stack (3D images)
    OptTag_t<TileDepthTag>, // Tile depth for 3D tiled images
    OptTag_t<SubIFDTag> // Sub-image IFD offsets (thumbnails, reduced resolution copies)
>;


} // namespace tiff
