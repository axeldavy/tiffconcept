#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

namespace tiff {

/// Rational number representation (unsigned)
struct Rational {
    uint32_t numerator;
    uint32_t denominator;
};

/// Rational number representation (signed)
struct SRational {
    int32_t numerator;
    int32_t denominator;
};

/// Non-native float types represented as fixed-size byte arrays
struct Float16 {
    uint8_t bytes[2];

    constexpr Float16() noexcept : bytes{0, 0} {}

    explicit Float16(float value) noexcept {
        from_float(value);
    }
    
    // Helper to treat as uint16_t for predictor operations
    uint16_t as_uint16() const noexcept {
        return (static_cast<uint16_t>(bytes[0])) | 
               (static_cast<uint16_t>(bytes[1]) << 8);
    }
    
    void from_uint16(uint16_t val) noexcept {
        bytes[0] = static_cast<uint8_t>(val & 0xFF);
        bytes[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    }

    // Helpers to use Float16 in your program
    /// Convert from float32 to float16 (IEEE 754 half-precision)
    void from_float(float value) noexcept {
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
    
    /// Convert from float16 to float32
    [[nodiscard]] float to_float() const noexcept {
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
    
    /// Conversion operator to float
    explicit operator float() const noexcept {
        return to_float();
    }
};

struct Float24 {
    uint8_t bytes[3];

    constexpr Float24() noexcept : bytes{0, 0, 0} {}

    explicit Float24(float value) noexcept {
        from_float(value);
    }
    
    // Helper to treat as uint32_t for predictor operations (padded)
    uint32_t as_uint32() const noexcept {
        return (static_cast<uint32_t>(bytes[0])) | 
               (static_cast<uint32_t>(bytes[1]) << 8) |
               (static_cast<uint32_t>(bytes[2]) << 16);
    }
    
    void from_uint32(uint32_t val) noexcept {
        bytes[0] = static_cast<uint8_t>(val & 0xFF);
        bytes[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        bytes[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    }

    // Helpers to use Float24 in your program
    /// Convert from float32 to float24 (custom 24-bit format)
    /// Float24 format: 1 sign bit, 7 exponent bits, 16 mantissa bits
    void from_float(float value) noexcept {
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
    
    /// Convert from float24 to float32
    [[nodiscard]] float to_float() const noexcept {
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
    
    /// Conversion operator to float
    explicit operator float() const noexcept {
        return to_float();
    }
};

template <typename T>
[[nodiscard]] constexpr T byteswap(T value) noexcept requires std::is_integral_v<T> {
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

/// Helper to convert structure fields from source endianness to native
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

/// Basic TIFF header structure (Little-Endian)
template <std::endian StorageEndian>
struct [[gnu::packed]] TiffHeader {
    std::array<char, 2> byte_order;  // "II" (0x4949) for little-endian
    uint16_t version;                // 42 (0x002A) for classic TIFF
    uint32_t first_ifd_offset;       // Offset to first IFD

    [[nodiscard]] constexpr bool is_little_endian() const noexcept {
        return byte_order[0] == 'I' && byte_order[1] == 'I';
    }

    [[nodiscard]] constexpr bool is_big_endian() const noexcept {
        return byte_order[0] == 'M' && byte_order[1] == 'M';
    }

    template <std::endian TargetEndian>
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        if constexpr (TargetEndian != StorageEndian) {
            return false;
        }
        if constexpr (TargetEndian == std::endian::little) {
            return is_little_endian() && version == 42;
        } else if constexpr (TargetEndian == std::endian::big) {
            return is_big_endian() && version == 42;
        }
        static_assert(TargetEndian == std::endian::little || TargetEndian == std::endian::big,
                      "Invalid endian specified");
        return false;
    }

    template <std::endian TargetEndian>
    [[nodiscard]] uint32_t get_first_ifd_offset() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return first_ifd_offset;
        } else {
            return byteswap(first_ifd_offset);
        }
    }
};

static_assert(sizeof(TiffHeader<std::endian::little>) == 8, "TiffHeader must be 8 bytes");
static_assert(sizeof(TiffHeader<std::endian::big>) == 8, "TiffHeader must be 8 bytes");

/// Big TIFF header structure (Little-Endian)

template <std::endian StorageEndian>
struct [[gnu::packed]] TiffBigHeader {
    std::array<char, 2> byte_order;  // "II" (0x4949) for little-endian
    uint16_t version;                // 43 (0x002B) for BigTIFF
    uint16_t offset_size;            // Size of offsets (should be 8)
    uint16_t reserved;               // Must be 0
    uint64_t first_ifd_offset;       // Offset to first IFD

    [[nodiscard]] constexpr bool is_little_endian() const noexcept {
        return byte_order[0] == 'I' && byte_order[1] == 'I';
    }

    [[nodiscard]] constexpr bool is_big_endian() const noexcept {
        return byte_order[0] == 'M' && byte_order[1] == 'M';
    }

    template <std::endian TargetEndian>
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        if constexpr (TargetEndian != StorageEndian) {
            return false;
        }
        if constexpr (TargetEndian == std::endian::little) {
            return is_little_endian() && version == 43 && offset_size == 8 && reserved == 0;
        } else if constexpr (TargetEndian == std::endian::big) {
            return is_big_endian() && version == 43 && offset_size == 8 && reserved == 0;
        }
        static_assert(TargetEndian == std::endian::little || TargetEndian == std::endian::big,
                      "Invalid endian specified");
        return false;
    }

    template <std::endian TargetEndian>
    [[nodiscard]] uint64_t get_first_ifd_offset() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return first_ifd_offset;
        } else {
            return byteswap(first_ifd_offset);
        }
    }
};

static_assert(sizeof(TiffBigHeader<std::endian::little>) == 16, "TiffBigHeader must be 16 bytes");
static_assert(sizeof(TiffBigHeader<std::endian::big>) == 16, "TiffBigHeader must be 16 bytes");

enum class TiffFormatType : uint16_t {
    Classic,
    BigTIFF
};

/// IFD (Image File Directory) header
template <std::endian StorageEndian>
struct [[gnu::packed]] IFDHeader {
    uint16_t num_entries;  // Number of directory entries in this IFD

    template <std::endian TargetEndian>
    [[nodiscard]] uint16_t get_num_entries() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return num_entries;
        } else {
            return byteswap(num_entries);
        }
    }
};

template <std::endian StorageEndian>
struct [[gnu::packed]] IFDBigHeader {
    uint64_t num_entries;  // Number of directory entries in this IFD

    template <std::endian TargetEndian>
    [[nodiscard]] uint64_t get_num_entries() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return num_entries;
        } else {
            return byteswap(num_entries);
        }
    }
};

static_assert(sizeof(IFDHeader<std::endian::little>) == 2, "IFDHeader must be 2 bytes");
static_assert(sizeof(IFDHeader<std::endian::big>) == 2, "IFDHeader must be 2 bytes");
static_assert(sizeof(IFDBigHeader<std::endian::little>) == 8, "IFDBigHeader must be 8 bytes");
static_assert(sizeof(IFDBigHeader<std::endian::big>) == 8, "IFDBigHeader must be 8 bytes");

/// TIFF data types
enum class TiffDataType : uint16_t {
    Byte      = 1,  // 8-bit unsigned integer
    Ascii     = 2,  // 8-bit byte containing a 7-bit ASCII code
    Short     = 3,  // 16-bit unsigned integer
    Long      = 4,  // 32-bit unsigned integer
    Rational  = 5,  // Two LONGs: numerator, denominator
    SByte     = 6,  // 8-bit signed integer
    Undefined = 7,  // 8-bit byte
    SShort    = 8,  // 16-bit signed integer
    SLong     = 9,  // 32-bit signed integer
    SRational = 10, // Two SLONGs: numerator, denominator
    Float     = 11, // Single precision (4-byte) IEEE format
    Double    = 12, // Double precision (8-byte) IEEE format
    IFD       = 13, // 32-bit IFD offset, equivalent to Long
    
    // BigTIFF extensions
    Long8     = 16, // 64-bit unsigned integer
    SLong8    = 17, // 64-bit signed integer
    IFD8      = 18  // 64-bit IFD offset
};

/// Get size in bytes of a TIFF data type
[[nodiscard]] constexpr std::size_t tiff_type_size(TiffDataType type) noexcept {
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

/// Union for tag values that fit in 4 bytes
union [[gnu::packed]] TagValue {
    uint32_t offset;              // Offset to data if count*size > 4 bytes
    std::array<uint8_t, 4> bytes; // For up to 4 BYTE values
    std::array<char, 4> ascii;    // For up to 4 ASCII chars
    std::array<uint16_t, 2> shorts; // For up to 2 SHORT values
    uint32_t long_val;            // For 1 LONG value
    std::array<int8_t, 4> sbytes; // For up to 4 SBYTE values
    std::array<int16_t, 2> sshorts; // For up to 2 SSHORT values
    int32_t slong_val;            // For 1 SLONG value
    float float_val;              // For 1 FLOAT value
    uint32_t ifd_offset;          // For 1 IFD offset
};

/// Union for tag values that fit in 8 bytes (BigTIFF)
union [[gnu::packed]] TagBigValue {
    uint64_t offset;               // Offset to data if count*size > 8 bytes
    std::array<uint8_t, 8> bytes;  // For up to 8 BYTE values
    std::array<char, 8> ascii;     // For up to 8 ASCII chars
    std::array<uint16_t, 4> shorts; // For up to 4 SHORT values
    std::array<uint32_t, 2> longs;   // For up to 2 LONG values
    std::array<float, 2> floats;    // For up to 2 FLOAT values
    std::array<uint32_t, 2> ifd_offsets; // For up to 2 IFD offsets
    uint64_t long8_val;            // For 1 LONG8 value
    std::array<int8_t, 8> sbytes;   // For up to 8 SBYTE values
    std::array<int16_t, 4> sshorts; // For up to 4 SSHORT values
    std::array<int32_t, 2> slongs;   // For up to 2 SLONG values
    int64_t slong8_val;            // For 1 SLONG8 value
    uint64_t ifd8_offset;          // For 1 IFD8 offset
    double double_val;             // For 1 DOUBLE value
};

static_assert(sizeof(TagValue) == 4, "TagValue must be 4 bytes");
static_assert(sizeof(TagBigValue) == 8, "TagBigValue must be 8 bytes");

/// TIFF tag entry in IFD

template <std::endian StorageEndian>
struct [[gnu::packed]] TiffTag {
    uint16_t code;         // Tag identifier
    TiffDataType datatype; // Data type
    uint32_t count;        // Number of values
    TagValue value;        // Value or offset to value

    /// Get the maximum byte count needed for inline storage
    [[nodiscard]] constexpr std::size_t inline_bytecount_limit() const noexcept {
        return 4;
    }

    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint16_t get_code() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return code;
        } else {
            return byteswap(code);
        }
    }

    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] TiffDataType get_datatype() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return datatype;
        } else {
            return static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(datatype)));
        }
    }

    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint32_t get_count() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return count;
        } else {
            return byteswap(count);
        }
    }

    /// Check if value is stored inline or as an offset
    [[nodiscard]] bool is_inline() const noexcept {
        return get_count<std::endian::native>() * tiff_type_size(get_datatype<std::endian::native>()) <= inline_bytecount_limit();
    }

    /// Get the offset to the data (only valid if !is_inline())
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint32_t get_offset() const noexcept {
        assert(!is_inline() && "get_offset() called on inline value");
        if constexpr (TargetEndian == StorageEndian) {
            return value.offset;
        } else {
            return byteswap(value.offset);
        }
    }

    /// Get the total size of the data in bytes
    [[nodiscard]] std::size_t data_size() const noexcept {
        return get_count<std::endian::native>() * tiff_type_size(get_datatype<std::endian::native>());
    }

    /// Writing helpers
    template <std::endian SourceEndian = std::endian::native>
    void set_code(uint16_t tag_code) noexcept {
        if constexpr (SourceEndian == StorageEndian) {
            code = tag_code;
        } else {
            code = byteswap(tag_code);
        }
    }

    template <std::endian SourceEndian = std::endian::native>
    void set_datatype(TiffDataType type) noexcept {
        if constexpr (SourceEndian == StorageEndian) {
            datatype = type;
        } else {
            datatype = static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(type)));
        }
    }

    template <std::endian SourceEndian = std::endian::native>
    void set_count(uint32_t cnt) noexcept {
        if constexpr (SourceEndian == StorageEndian) {
            count = cnt;
        } else {
            count = byteswap(cnt);
        }
    }
};

template <std::endian StorageEndian>
struct [[gnu::packed]] TiffBigTag {
    uint16_t code;         // Tag identifier
    TiffDataType datatype; // Data type
    uint64_t count;        // Number of values
    TagBigValue value;     // Value or offset to value

    /// Get the maximum byte count needed for inline storage
    [[nodiscard]] constexpr std::size_t inline_bytecount_limit() const noexcept {
        return 8;
    }

    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint16_t get_code() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return code;
        } else {
            return byteswap(code);
        }
    }

    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] TiffDataType get_datatype() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return datatype;
        } else {
            return static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(datatype)));
        }
    }

    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint64_t get_count() const noexcept {
        if constexpr (TargetEndian == StorageEndian) {
            return count;
        } else {
            return byteswap(count);
        }
    }

    /// Check if value is stored inline or as an offset
    [[nodiscard]] bool is_inline() const noexcept {
        return get_count<std::endian::native>() * tiff_type_size(get_datatype<std::endian::native>()) <= inline_bytecount_limit();
    }

    /// Get the offset to the data (only valid if !is_inline())
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint64_t get_offset() const noexcept {
        assert(!is_inline() && "get_offset() called on inline value");
        if constexpr (TargetEndian == StorageEndian) {
            return value.offset;
        } else {
            return byteswap(value.offset);
        }
    }

    /// Get the total size of the data in bytes
    [[nodiscard]] std::size_t data_size() const noexcept {
        return get_count<std::endian::native>() * tiff_type_size(get_datatype<std::endian::native>());
    }

        /// Writing helpers
    template <std::endian SourceEndian = std::endian::native>
    void set_code(uint16_t tag_code) noexcept {
        if constexpr (SourceEndian == StorageEndian) {
            code = tag_code;
        } else {
            code = byteswap(tag_code);
        }
    }

    template <std::endian SourceEndian = std::endian::native>
    void set_datatype(TiffDataType type) noexcept {
        if constexpr (SourceEndian == StorageEndian) {
            datatype = type;
        } else {
            datatype = static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(type)));
        }
    }

    template <std::endian SourceEndian = std::endian::native>
    void set_count(uint64_t cnt) noexcept {
        if constexpr (SourceEndian == StorageEndian) {
            count = cnt;
        } else {
            count = byteswap(cnt);
        }
    }
};

static_assert(sizeof(TiffTag<std::endian::little>) == 12, "TiffTag must be 12 bytes");
static_assert(sizeof(TiffTag<std::endian::big>) == 12, "TiffTag must be 12 bytes");
static_assert(sizeof(TiffBigTag<std::endian::little>) == 20, "TiffBigTag must be 20 bytes");
static_assert(sizeof(TiffBigTag<std::endian::big>) == 20, "TiffBigTag must be 20 bytes");

/// Standard TIFF tag codes
enum class TagCode : uint16_t {
    // Baseline TIFF tags
    NewSubfileType          = 254,
    SubfileType             = 255,
    ImageWidth              = 256,
    ImageLength             = 257,
    BitsPerSample           = 258,
    Compression             = 259,
    PhotometricInterpretation = 262,
    Threshholding           = 263,
    CellWidth               = 264,
    CellLength              = 265,
    FillOrder               = 266,
    DocumentName            = 269,
    ImageDescription        = 270,
    Make                    = 271,
    Model                   = 272,
    StripOffsets            = 273,
    Orientation             = 274,
    SamplesPerPixel         = 277,
    RowsPerStrip            = 278,
    StripByteCounts         = 279,
    MinSampleValue          = 280,
    MaxSampleValue          = 281,
    XResolution             = 282,
    YResolution             = 283,
    PlanarConfiguration     = 284,
    PageName                = 285,
    XPosition               = 286,
    YPosition               = 287,
    FreeOffsets             = 288,
    FreeByteCounts          = 289,
    GrayResponseUnit        = 290,
    GrayResponseCurve       = 291,
    T4Options               = 292,
    T6Options               = 293,
    ResolutionUnit          = 296,
    PageNumber              = 297,
    ColorResponseUnit       = 300,
    TransferFunction        = 301,
    Software                = 305,
    DateTime                = 306,
    Artist                  = 315,
    HostComputer            = 316,
    Predictor               = 317,
    WhitePoint              = 318,
    PrimaryChromaticities   = 319,
    ColorMap                = 320,
    HalftoneHints           = 321,
    TileWidth               = 322,
    TileLength              = 323,
    TileOffsets             = 324,
    TileByteCounts          = 325,
    BadFaxLines             = 326,
    CleanFaxData            = 327,
    ConsecutiveBadFaxLines  = 328,
    SubIFD                  = 330,
    InkSet                  = 332,
    InkNames                = 333,
    NumberOfInks            = 334,
    DotRange                = 336,
    TargetPrinter           = 337,
    ExtraSamples            = 338,
    SampleFormat            = 339,
    SMinSampleValue         = 340,
    SMaxSampleValue         = 341,
    TransferRange           = 342,
    ClipPath                = 343,
    XClipPathUnits          = 344,
    YClipPathUnits          = 345,
    Indexed                 = 346,
    JPEGTables              = 347,
    GlobalParametersIFD     = 400,
    ProfileType             = 401,
    FaxProfile              = 402,
    CodingMethods           = 403,
    VersionYear             = 404,
    ModeNumber              = 405,
    Decode                  = 433,
    ImageBaseColor          = 434,
    T82Options              = 435,
    JPEGProc                = 512,
    JPEGInterchangeFormat   = 513,
    JPEGInterchangeFormatLngth = 514,
    JPEGRestartInterval     = 515,
    JPEGLosslessPredictors  = 517,
    JPEGPointTransforms     = 518,
    JPEGQTables             = 519,
    JPEGDCTables            = 520,
    JPEGACTables            = 521,
    YCbCrCoefficients       = 529,
    YCbCrSubSampling        = 530,
    YCbCrPositioning        = 531,
    ReferenceBlackWhite     = 532,
    StripRowCounts          = 559,
    XMLPacket               = 700,
    
    // Private tags
    Matteing                = 32995,  // Obsolete, use ExtraSamples
    DataType                = 32996,  // Obsolete, use SampleFormat
    ImageDepth              = 32997,
    TileDepth               = 32998,
    ImageFullWidth          = 33300,
    ImageFullLength         = 33301,
    TextureFormat           = 33302,
    TextureWrapModes        = 33303,
    FieldOfViewCotangent    = 33304,
    MatrixWorldToScreen     = 33305,
    MatrixWorldToCamera     = 33306,
    Copyright               = 33432,
    RichTIFFIPTC            = 33723,
    Photoshop               = 34377,
    EXIFIFDOffset           = 34665,
    ICCProfile              = 34675,
    ImageLayer              = 34732,
    GPSIFDOffset            = 34853,
    FaxRecvParams           = 34908,
    FaxSubAddress           = 34909,
    FaxRecvTime             = 34910,
    FaxDcs                  = 34911,
    StoNits                 = 37439,
    PhotoshopDocDataBlock   = 37724,
    InteroperabilityIFDOffset = 40965,
    
    // DNG tags
    DNGVersion              = 50706,
    DNGBackwardVersion      = 50707,
    UniqueCameraModel       = 50708,
    LocalizedCameraModel    = 50709,
    CFAPlaneColor           = 50710,
    CFALayout               = 50711,
    LinearizationTable      = 50712,
    BlackLevelRepeatDim     = 50713,
    BlackLevel              = 50714,
    BlackLevelDeltaH        = 50715,
    BlackLevelDeltaV        = 50716,
    WhiteLevel              = 50717,
    DefaultScale            = 50718,
    DefaultCropOrigin       = 50719,
    DefaultCropSize         = 50720,
    ColorMatrix1            = 50721,
    ColorMatrix2            = 50722,
    CameraCalibration1      = 50723,
    CameraCalibration2      = 50724,
    ReductionMatrix1        = 50725,
    ReductionMatrix2        = 50726,
    AnalogBalance           = 50727,
    AsShotNeutral           = 50728,
    AsShotWhiteXY           = 50729,
    BaselineExposure        = 50730,
    BaselineNoise           = 50731,
    BaselineSharpness       = 50732,
    BayerGreenSplit         = 50733,
    LinearResponseLimit     = 50734,
    CameraSerialNumber      = 50735,
    LensInfo                = 50736,
    ChromaBlurRadius        = 50737,
    AntiAliasStrength       = 50738,
    ShadowScale             = 50739,
    DNGPrivateData          = 50740,
    MakerNoteSafety         = 50741,
    CalibrationIlluminant1  = 50778,
    CalibrationIlluminant2  = 50779,
    BestQualityScale        = 50780,
    RawDataUniqueID         = 50781,
    OriginalRawFileName     = 50827,
    OriginalRawFileData     = 50828,
    ActiveArea              = 50829,
    MaskedAreas             = 50830,
    AsShotICCProfile        = 50831,
    AsShotPreProfileMatrix  = 50832,
    CurrentICCProfile       = 50833,
    CurrentPreProfileMatrix = 50834,
};

/// Compression schemes
enum class CompressionScheme : uint16_t {
    None           = 1,
    CCITT_RLE      = 2,
    CCITT_Fax3     = 3,
    CCITT_Fax4     = 4,
    LZW            = 5,
    JPEG_Old       = 6,
    JPEG           = 7,
    Deflate_Adobe  = 8,
    Deflate        = 32946,
    PackBits       = 32773,
    ZSTD           = 50000,  // Non-standard but commonly used
    ZSTD_Alt       = 34926,  // Alternative ZSTD code
};

/// Sample format
enum class SampleFormat : uint16_t {
    UnsignedInt   = 1,
    SignedInt     = 2,
    IEEEFloat     = 3,
    Undefined     = 4,
};

/// Photometric interpretation
enum class PhotometricInterpretation : uint16_t {
    MinIsWhite    = 0,
    MinIsBlack    = 1,
    RGB           = 2,
    Palette       = 3,
    Mask          = 4,
    CMYK          = 5,
    YCbCr         = 6,
    CIELab        = 8,
};

/// Predictor for compression
enum class Predictor : uint16_t {
    None          = 1,
    Horizontal    = 2,
    FloatingPoint = 3,
};

/// Planar configuration for multi-channel images
enum class PlanarConfiguration : uint16_t {
    Chunky = 1,  // RGBRGBRGB... (interleaved)
    Planar = 2,  // RRR...GGG...BBB... (separate planes)
};


/// Output layout specification when extracting image data
enum class OutputSpec {
    DHWC,  // Depth, Height, Width, Channels
    DCHW,  // Depth, Channels, Height, Width
    CDHW,  // Channels, Depth, Height, Width
};

// OutputSpec defines the order of the dimensions when
// writing to the output buffer.
// ------------------------
// 2D single-channel images:
// By default the depth and channel dimensions are 1.
// Thus any OutputSpec results in the same layout: HW.
// ------------------------
// 2D multi-channel images:
// DHWC: HWC (channels last)
// DCHW/CDHW: CHW (channels first)
// ------------------------
// Best OutputSpec for performance ?
// The OutputSpec can impact extraction performance, and the
// best choice depends on your image layout.
// PlanarConfiguration::Chunky -> DHWC will be best
// PlanarConfiguration::Planar
//     -> DCHW/CDHW will be best. The best between DCHW and CDHW
//        depends on which channels/depths sections you extract.
// If you can control the encoding of the Tiff image, unless the
// channels are strongly correlated, PlanarConfiguration::Planar
// is generally preferred for better compression ratios.

} // namespace tiff
