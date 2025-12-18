#pragma once

#include <bit>
#include <cstring>
#include <span>
#include "result.hpp"
#include "tag_spec.hpp"
#include "types.hpp"

namespace tiffconcept {

namespace tag_writing {

/// @brief Write a single value to a byte buffer with endianness conversion
/// @tparam T The type of value to write (must be arithmetic)
/// @tparam TargetEndian The target endianness for the written data
/// @param dest Pointer to the destination buffer
/// @param value The value to write
/// @note This function performs byte-swapping if TargetEndian differs from native endianness
template <typename T, std::endian TargetEndian>
void write_value_with_endianness(std::byte* dest, T value) noexcept;

/// @brief Write a rational value (Rational or SRational) to a byte buffer
/// @tparam RationalType Either Rational or SRational
/// @tparam TargetEndian The target endianness for the written data
/// @param dest Pointer to the destination buffer
/// @param rational The rational value to write (numerator followed by denominator)
/// @return The number of bytes written (always 8 for Rational/SRational)
/// @note Writes numerator and denominator as consecutive values with proper endianness
template <typename RationalType, std::endian TargetEndian>
std::size_t write_rational(std::byte* dest, const RationalType& rational) noexcept;

/// @brief Write tag data to a buffer with proper type conversion and endianness
/// @tparam TagDesc The tag descriptor type defining the tag's characteristics
/// @tparam TargetEndian The target endianness for the written data
/// @tparam ValueType The type of the value to write
/// @param value The value to write (scalar, container, or string depending on TagDesc)
/// @param buffer The destination buffer as a span of bytes
/// @param required_size The required buffer size in bytes
/// @return Result<void> indicating success or error
/// @retval Success Data written successfully
/// @retval OutOfBounds Buffer too small for the data
/// @note Handles scalars, arrays, vectors, strings, rationals, and enums with automatic conversion
/// @note For strings (ASCII tags), does not add null terminator (size in tag count)
/// @note For rationals, writes numerator and denominator consecutively
/// @note For containers, iterates and writes each element with proper endianness
template <typename TagDesc, std::endian TargetEndian, typename ValueType>
[[nodiscard]] Result<void> write_tag_data(const ValueType& value, std::span<std::byte> buffer, std::size_t required_size) noexcept;

/// @brief Calculate the byte size of tag data when written to file
/// @tparam TagDesc The tag descriptor type defining the tag's characteristics
/// @tparam ValueType The type of the value
/// @param value The value whose size to calculate
/// @return The size in bytes required to store the value
/// @note For scalars: returns size of the TIFF data type
/// @note For strings: returns string length + 1 (for null terminator)
/// @note For containers: returns element count × element size
/// @note For fixed-size arrays: uses std::tuple_size to determine count
template <typename TagDesc, typename ValueType>
[[nodiscard]] std::size_t calculate_tag_data_size(const ValueType& value) noexcept;

} // namespace tag_writing

} // namespace tiffconcept

#include "impl/tag_writing_impl.hpp"