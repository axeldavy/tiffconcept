// Do not include this file directly. Include "tiffconcept/types.hpp" instead.

#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

#ifndef TIFFCONCEPT_TYPES_HEADER
#include "../types.hpp" // for linters
#endif

namespace tiffconcept {

// Float16 implementations

inline Float16::Float16(float value) noexcept {
    from_float(value);
}

inline uint16_t Float16::as_uint16() const noexcept {
    return (static_cast<uint16_t>(bytes[0])) | 
           (static_cast<uint16_t>(bytes[1]) << 8);
}

inline void Float16::from_uint16(uint16_t val) noexcept {
    bytes[0] = static_cast<uint8_t>(val & 0xFF);
    bytes[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

inline void Float16::from_float(float value) noexcept {
    uint32_t f32 = std::bit_cast<uint32_t>(value);
    
    uint32_t sign = (f32 >> 16) & 0x8000;
    int32_t exponent = ((f32 >> 23) & 0xFF) - 127;
    uint32_t mantissa = f32 & 0x7FFFFF;
    
    uint16_t f16;
    
    // Handle special cases
    if (exponent == 128) {
        // Infinity or NaN
        f16 = static_cast<uint16_t>(sign | 0x7C00 | (mantissa != 0 ? 0x0200 : 0));
    } else if (exponent > 15) {
        // Overflow to infinity
        f16 = static_cast<uint16_t>(sign | 0x7C00);
    } else if (exponent > -15) {
        // Normalized number
        exponent += 15;
        f16 = static_cast<uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
    } else if (exponent >= -24) {
        // Denormalized number
        mantissa |= 0x800000;
        uint32_t shift = static_cast<uint32_t>(-14 - exponent);
        f16 = static_cast<uint16_t>(sign | (mantissa >> shift));
    } else {
        // Underflow to zero
        f16 = static_cast<uint16_t>(sign);
    }
    
    from_uint16(f16);
}

inline float Float16::to_float() const noexcept {
    uint16_t f16 = as_uint16();
    
    uint32_t sign = (static_cast<uint32_t>(f16) & 0x8000) << 16;
    uint32_t exponent = (f16 >> 10) & 0x1F;
    uint32_t mantissa = f16 & 0x3FF;
    
    uint32_t f32;
    
    if (exponent == 0) {
        if (mantissa == 0) {
            // Zero
            f32 = sign;
        } else {
            // Denormalized number
            exponent = 1;
            while ((mantissa & 0x400) == 0) {
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0x3FF;
            f32 = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        // Infinity or NaN
        f32 = sign | 0x7F800000 | (mantissa << 13);
    } else {
        // Normalized number
        f32 = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
    }
    
    return std::bit_cast<float>(f32);
}

inline Float16::operator float() const noexcept {
    return to_float();
}

// Float24 implementations

inline Float24::Float24(float value) noexcept {
    from_float(value);
}

inline uint32_t Float24::as_uint32() const noexcept {
    return (static_cast<uint32_t>(bytes[0])) | 
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16);
}

inline void Float24::from_uint32(uint32_t val) noexcept {
    bytes[0] = static_cast<uint8_t>(val & 0xFF);
    bytes[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    bytes[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
}

inline void Float24::from_float(float value) noexcept {
    uint32_t f32 = std::bit_cast<uint32_t>(value);
    
    uint32_t sign = (f32 >> 31) & 0x1;
    int32_t exponent = ((f32 >> 23) & 0xFF) - 127;
    uint32_t mantissa = f32 & 0x7FFFFF;
    
    uint32_t f24;
    
    // Handle special cases
    if (exponent == 128) {
        // Infinity or NaN
        f24 = (sign << 23) | 0x7F0000 | ((mantissa != 0) ? 0x8000 : 0);
    } else if (exponent > 63) {
        // Overflow to infinity
        f24 = (sign << 23) | 0x7F0000;
    } else if (exponent > -63) {
        // Normalized number
        exponent += 63;
        f24 = (sign << 23) | (static_cast<uint32_t>(exponent) << 16) | (mantissa >> 7);
    } else if (exponent >= -78) {
        // Denormalized number
        mantissa |= 0x800000;
        uint32_t shift = static_cast<uint32_t>(-62 - exponent);
        f24 = (sign << 23) | (mantissa >> shift);
    } else {
        // Underflow to zero
        f24 = sign << 23;
    }
    
    from_uint32(f24 & 0xFFFFFF);
}

inline float Float24::to_float() const noexcept {
    uint32_t f24 = as_uint32();
    
    uint32_t sign = (f24 >> 23) & 0x1;
    uint32_t exponent = (f24 >> 16) & 0x7F;
    uint32_t mantissa = f24 & 0xFFFF;
    
    uint32_t f32;
    
    if (exponent == 0) {
        if (mantissa == 0) {
            // Zero
            f32 = sign << 31;
        } else {
            // Denormalized number
            exponent = 1;
            while ((mantissa & 0x10000) == 0) {
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0xFFFF;
            f32 = (sign << 31) | ((exponent + (127 - 63)) << 23) | (mantissa << 7);
        }
    } else if (exponent == 127) {
        // Infinity or NaN
        f32 = (sign << 31) | 0x7F800000 | (mantissa << 7);
    } else {
        // Normalized number
        f32 = (sign << 31) | ((exponent + (127 - 63)) << 23) | (mantissa << 7);
    }
    
    return std::bit_cast<float>(f32);
}

inline Float24::operator float() const noexcept {
    return to_float();
}

// byteswap template

template <typename T>
constexpr T byteswap(T value) noexcept requires std::is_integral_v<T> {
    if constexpr (sizeof(T) == 1) {
        return value;
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>((value >> 8) | (value << 8));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(
            ((value & 0xFF000000) >> 24) |
            ((value & 0x00FF0000) >> 8)  |
            ((value & 0x0000FF00) << 8)  |
            ((value & 0x000000FF) << 24)
        );
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(
            ((value & 0xFF00000000000000ULL) >> 56) |
            ((value & 0x00FF000000000000ULL) >> 40) |
            ((value & 0x0000FF0000000000ULL) >> 24) |
            ((value & 0x000000FF00000000ULL) >> 8)  |
            ((value & 0x00000000FF000000ULL) << 8)  |
            ((value & 0x0000000000FF0000ULL) << 24) |
            ((value & 0x000000000000FF00ULL) << 40) |
            ((value & 0x00000000000000FFULL) << 56)
        );
    }
}

// convert_endianness template

template <typename T, std::endian SourceEndian, std::endian TargetEndian>
constexpr void convert_endianness([[maybe_unused]] T& value) noexcept {
    if constexpr (SourceEndian != TargetEndian) {
        if constexpr (std::is_integral_v<T>) {
            value = byteswap(value);
        } else if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4) {
                uint32_t temp;
                std::memcpy(&temp, &value, sizeof(T));
                temp = byteswap(temp);
                std::memcpy(&value, &temp, sizeof(T));
            } else if constexpr (sizeof(T) == 8) {
                uint64_t temp;
                std::memcpy(&temp, &value, sizeof(T));
                temp = byteswap(temp);
                std::memcpy(&value, &temp, sizeof(T));
            }
        } else if constexpr (std::is_aggregate_v<T>) {
            // For structures, need to convert each field
            // This will be specialized for specific TIFF structures
            static_assert(sizeof(T) == 0, "convert_endianness not specialized for this structure type");
        }
    }
}

// TiffHeader template implementations

template <std::endian StorageEndian>
constexpr bool TiffHeader<StorageEndian>::is_little_endian() const noexcept {
    return byte_order[0] == 'I' && byte_order[1] == 'I';
}

template <std::endian StorageEndian>
constexpr bool TiffHeader<StorageEndian>::is_big_endian() const noexcept {
    return byte_order[0] == 'M' && byte_order[1] == 'M';
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
constexpr bool TiffHeader<StorageEndian>::is_valid() const noexcept {
    uint16_t native_version = (TargetEndian == std::endian::native) ? version : byteswap(version);
    if constexpr (TargetEndian != StorageEndian) {
        return false;
    }
    if constexpr (TargetEndian == std::endian::little) {
        return is_little_endian() && native_version == 42;
    } else if constexpr (TargetEndian == std::endian::big) {
        return is_big_endian() && native_version == 42;
    }
    static_assert(TargetEndian == std::endian::little || TargetEndian == std::endian::big,
                  "Invalid endian specified");
    return false;
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
uint32_t TiffHeader<StorageEndian>::get_first_ifd_offset() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return first_ifd_offset;
    } else {
        return byteswap(first_ifd_offset);
    }
}

// TiffBigHeader template implementations

template <std::endian StorageEndian>
constexpr bool TiffBigHeader<StorageEndian>::is_little_endian() const noexcept {
    return byte_order[0] == 'I' && byte_order[1] == 'I';
}

template <std::endian StorageEndian>
constexpr bool TiffBigHeader<StorageEndian>::is_big_endian() const noexcept {
    return byte_order[0] == 'M' && byte_order[1] == 'M';
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
constexpr bool TiffBigHeader<StorageEndian>::is_valid() const noexcept {
    uint16_t native_version = (TargetEndian == std::endian::native) ? version : byteswap(version);
    uint16_t native_offset_size = (TargetEndian == std::endian::native) ? offset_size : byteswap(offset_size);
    uint16_t native_reserved = (TargetEndian == std::endian::native) ? reserved : byteswap(reserved);

    if constexpr (TargetEndian != StorageEndian) {
        return false;
    }
    if constexpr (TargetEndian == std::endian::little) {
        return is_little_endian() && native_version == 43 && native_offset_size == 8 && native_reserved == 0;
    } else if constexpr (TargetEndian == std::endian::big) {
        return is_big_endian() && native_version == 43 && native_offset_size == 8 && native_reserved == 0;
    }
    static_assert(TargetEndian == std::endian::little || TargetEndian == std::endian::big,
                  "Invalid endian specified");
    return false;
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
uint64_t TiffBigHeader<StorageEndian>::get_first_ifd_offset() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return first_ifd_offset;
    } else {
        return byteswap(first_ifd_offset);
    }
}

// IFDHeader template implementations

template <std::endian StorageEndian>
template <std::endian TargetEndian>
uint16_t IFDHeader<StorageEndian>::get_num_entries() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return num_entries;
    } else {
        return byteswap(num_entries);
    }
}

// IFDBigHeader template implementations

template <std::endian StorageEndian>
template <std::endian TargetEndian>
uint64_t IFDBigHeader<StorageEndian>::get_num_entries() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return num_entries;
    } else {
        return byteswap(num_entries);
    }
}

// tiff_type_size function

constexpr std::size_t tiff_type_size(TiffDataType type) noexcept {
    switch (type) {
        case TiffDataType::Byte:
        case TiffDataType::Ascii:
        case TiffDataType::SByte:
        case TiffDataType::Undefined:
            return 1;
        case TiffDataType::Short:
        case TiffDataType::SShort:
            return 2;
        case TiffDataType::Long:
        case TiffDataType::SLong:
        case TiffDataType::Float:
        case TiffDataType::IFD:
            return 4;
        case TiffDataType::Rational:
        case TiffDataType::SRational:
        case TiffDataType::Double:
        case TiffDataType::Long8:
        case TiffDataType::SLong8:
        case TiffDataType::IFD8:
            return 8;
        default:
            return 0;
    }
}

// TiffTag template implementations

template <std::endian StorageEndian>
constexpr std::size_t TiffTag<StorageEndian>::inline_bytecount_limit() const noexcept {
    return 4;
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
uint16_t TiffTag<StorageEndian>::get_code() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return code;
    } else {
        return byteswap(code);
    }
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
TiffDataType TiffTag<StorageEndian>::get_datatype() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return datatype;
    } else {
        return static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(datatype)));
    }
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
uint32_t TiffTag<StorageEndian>::get_count() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return count;
    } else {
        return byteswap(count);
    }
}

template <std::endian StorageEndian>
inline bool TiffTag<StorageEndian>::is_inline() const noexcept {
    return get_count<std::endian::native>() * tiff_type_size(get_datatype<std::endian::native>()) <= inline_bytecount_limit();
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
inline uint32_t TiffTag<StorageEndian>::get_offset() const noexcept {
    assert(!is_inline() && "get_offset() called on inline value");
    if constexpr (TargetEndian == StorageEndian) {
        return value.offset;
    } else {
        return byteswap(value.offset);
    }
}

/// Get the total size of the data in bytes
template <std::endian StorageEndian>
inline std::size_t TiffTag<StorageEndian>::data_size() const noexcept {
    return get_count<std::endian::native>() * tiff_type_size(get_datatype<std::endian::native>());
}

template <std::endian StorageEndian>
template <std::endian SourceEndian>
void TiffTag<StorageEndian>::set_code(uint16_t tag_code) noexcept {
    if constexpr (SourceEndian == StorageEndian) {
        code = tag_code;
    } else {
        code = byteswap(tag_code);
    }
}

template <std::endian StorageEndian>
template <std::endian SourceEndian>
void TiffTag<StorageEndian>::set_datatype(TiffDataType type) noexcept {
    if constexpr (SourceEndian == StorageEndian) {
        datatype = type;
    } else {
        datatype = static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(type)));
    }
}

template <std::endian StorageEndian>
template <std::endian SourceEndian>
void TiffTag<StorageEndian>::set_count(uint32_t cnt) noexcept {
    if constexpr (SourceEndian == StorageEndian) {
        count = cnt;
    } else {
        count = byteswap(cnt);
    }
}

// TiffBigTag template implementations

template <std::endian StorageEndian>
constexpr std::size_t TiffBigTag<StorageEndian>::inline_bytecount_limit() const noexcept {
    return 8;
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
uint16_t TiffBigTag<StorageEndian>::get_code() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return code;
    } else {
        return byteswap(code);
    }
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
TiffDataType TiffBigTag<StorageEndian>::get_datatype() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return datatype;
    } else {
        return static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(datatype)));
    }
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
uint64_t TiffBigTag<StorageEndian>::get_count() const noexcept {
    if constexpr (TargetEndian == StorageEndian) {
        return count;
    } else {
        return byteswap(count);
    }
}

template <std::endian StorageEndian>
inline bool TiffBigTag<StorageEndian>::is_inline() const noexcept {
    return get_count<std::endian::native>() * tiff_type_size(get_datatype<std::endian::native>()) <= inline_bytecount_limit();
}

template <std::endian StorageEndian>
template <std::endian TargetEndian>
inline uint64_t TiffBigTag<StorageEndian>::get_offset() const noexcept {
    assert(!is_inline() && "get_offset() called on inline value");
    if constexpr (TargetEndian == StorageEndian) {
        return value.offset;
    } else {
        return byteswap(value.offset);
    }
}

/// Get the total size of the data in bytes
template <std::endian StorageEndian>
inline std::size_t TiffBigTag<StorageEndian>::data_size() const noexcept {
    return get_count<std::endian::native>() * tiff_type_size(get_datatype<std::endian::native>());
}

template <std::endian StorageEndian>
template <std::endian SourceEndian>
void TiffBigTag<StorageEndian>::set_code(uint16_t tag_code) noexcept {
    if constexpr (SourceEndian == StorageEndian) {
        code = tag_code;
    } else {
        code = byteswap(tag_code);
    }
}

template <std::endian StorageEndian>
template <std::endian SourceEndian>
void TiffBigTag<StorageEndian>::set_datatype(TiffDataType type) noexcept {
    if constexpr (SourceEndian == StorageEndian) {
        datatype = type;
    } else {
        datatype = static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(type)));
    }
}

template <std::endian StorageEndian>
template <std::endian SourceEndian>
void TiffBigTag<StorageEndian>::set_count(uint64_t cnt) noexcept {
    if constexpr (SourceEndian == StorageEndian) {
        count = cnt;
    } else {
        count = byteswap(cnt);
    }
}

} // namespace tiffconcept

