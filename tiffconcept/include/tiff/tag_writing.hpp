#pragma once

#include <bit>
#include <cstring>
#include <span>
#include "result.hpp"
#include "tag_spec.hpp"
#include "types.hpp"

namespace tiff {

namespace tag_writing {

/// Helper to write a single value with endianness conversion
template <typename T, std::endian TargetEndian>
inline void write_value_with_endianness(std::byte* dest, T value) noexcept {
    if constexpr (TargetEndian != std::endian::native && std::is_arithmetic_v<T>) {
        value = byteswap(value);
    }
    std::memcpy(dest, &value, sizeof(T));
}

/// Helper to write a rational (Rational or SRational) as two consecutive values
/// Returns the number of bytes written
template <typename RationalType, std::endian TargetEndian>
inline std::size_t write_rational(std::byte* dest, const RationalType& rational) noexcept {
    using ComponentType = decltype(rational.numerator);
    write_value_with_endianness<ComponentType, TargetEndian>(dest, rational.numerator);
    write_value_with_endianness<ComponentType, TargetEndian>(dest + sizeof(ComponentType), rational.denominator);
    return 2 * sizeof(ComponentType);
}

/// Helper to write tag data to a buffer with proper type conversion and endianness
template <typename TagDesc, std::endian TargetEndian, typename ValueType>
[[nodiscard]] inline Result<void> write_tag_data(const ValueType& value, std::span<std::byte> buffer, std::size_t required_size) noexcept {
    if (buffer.size() < required_size) {
        return Err(Error::Code::OutOfBounds,
                  "Buffer too small: need " + std::to_string(required_size) + 
                  " bytes, got " + std::to_string(buffer.size()));
    }
    
    using ValueType_ = typename TagDesc::value_type;
    using ElementType = typename TagDesc::element_type;
    std::byte* dest = buffer.data();
    
    // For std::string (ASCII)
    if constexpr (std::is_same_v<ValueType_, std::string>) {
        std::memcpy(dest, value.data(), value.size());
        // Note: no null terminator (the size is stored in the tag count)
    }
    // Single value (scalar, enum, or rational)
    else if constexpr (!TagDesc::is_container) {
        if constexpr (TagDesc::is_rational) {
            write_rational<ElementType, TargetEndian>(dest, value);
        } else {
            using RefType = typename TagDesc::reference_type;
            write_value_with_endianness<RefType, TargetEndian>(dest, static_cast<RefType>(value));
        }
    }
    // Container of values
    else {
        if constexpr (TagDesc::is_rational) {
            // Container of rationals (Rational or SRational)
            std::size_t offset = 0;
            for (const auto& rational : value) {
                offset += write_rational<ElementType, TargetEndian>(dest + offset, rational);
            }
        } else {
            // Container of scalars
            using RefType = typename TagDesc::reference_type;
            if constexpr (TargetEndian == std::endian::native && std::is_same_v<ElementType, RefType>) {
                std::memcpy(dest, value.data(), value.size() * sizeof(ElementType));
            } else {
                std::size_t offset = 0;
                for (const auto& elem : value) {
                    write_value_with_endianness<RefType, TargetEndian>(dest + offset, static_cast<RefType>(elem));
                    offset += sizeof(RefType);
                }
            }
        }
    }
    
    return Ok();
}

/// Helper to calculate the byte size of tag data when written to file
template <typename TagDesc, typename ValueType>
[[nodiscard]] inline std::size_t calculate_tag_data_size(const ValueType& value) noexcept {
    constexpr TiffDataType datatype = TagDesc::datatype;
    constexpr std::size_t type_size = tiff_type_size(datatype);
    
    using RawType = typename TagDesc::value_type;
    
    // For scalar types (single value)
    if constexpr (std::is_arithmetic_v<RawType> || std::is_enum_v<RawType>) {
        return type_size;
    }
    // For Rational/SRational (single rational value)
    else if constexpr (std::is_same_v<RawType, Rational> || std::is_same_v<RawType, SRational>) {
        return type_size;
    }
    // For std::string (ASCII type - count includes null terminator)
    else if constexpr (std::is_same_v<RawType, std::string>) {
        return value.size() + 1; // +1 for null terminator
    }
    // For std::vector (count = dynamic size)
    else if constexpr (requires { value.size(); value.data(); typename RawType::value_type; } &&
                       !requires { std::tuple_size<RawType>::value; }) {
        return value.size() * type_size;
    }
    // For tuple-like types (std::array) - use tuple_size
    else if constexpr (requires { std::tuple_size<RawType>::value; }) {
        constexpr std::size_t N = std::tuple_size_v<RawType>;
        return N * type_size;
    }
    else {
        // Fallback: single value
        return type_size;
    }
}

} // namespace tag_writing

} // namespace tiff