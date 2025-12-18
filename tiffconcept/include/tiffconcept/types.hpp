#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

namespace tiffconcept {

// MSVC ignores the packed attribute
#pragma pack(push, 1)

/// @brief Rational number representation (unsigned)
/// @details Represents a fraction with unsigned 32-bit numerator and denominator.
/// Used in TIFF for values like resolution, scale factors, etc.
struct Rational {
    uint32_t numerator;   ///< Numerator of the fraction
    uint32_t denominator; ///< Denominator of the fraction
};

/// @brief Rational number representation (signed)
/// @details Represents a fraction with signed 32-bit numerator and denominator.
/// Used in TIFF for signed rational values.
struct SRational {
    int32_t numerator;   ///< Numerator of the fraction
    int32_t denominator; ///< Denominator of the fraction
};

/// @brief IEEE 754 half-precision (16-bit) floating point type
/// @details Stores float16 as a byte array for portability. Provides conversion
/// to/from float32 and helper methods for predictor operations.
/// @note Not all float32 values can be exactly represented in float16. Values may
/// overflow to infinity, underflow to zero, or lose precision.
struct Float16 {
    uint8_t bytes[2]; ///< Raw byte storage for the float16 value

    /// @brief Default constructor initializing to zero
    constexpr Float16() noexcept : bytes{0, 0} {}

    /// @brief Construct from float32 value
    /// @param value The float32 value to convert to float16
    explicit Float16(float value) noexcept;
    
    /// @brief Treat as uint16 for predictor operations
    /// @return The float16 bits interpreted as uint16 (little-endian order)
    [[nodiscard]] uint16_t as_uint16() const noexcept;
    
    /// @brief Set from uint16 value (for predictor operations)
    /// @param val The uint16 value to store as float16 bits
    void from_uint16(uint16_t val) noexcept;

    /// @brief Convert from float32 to float16 (IEEE 754 half-precision)
    /// @param value The float32 value to convert
    /// @note Handles infinity, NaN, overflow, underflow, and denormalized numbers
    void from_float(float value) noexcept;
    
    /// @brief Convert from float16 to float32
    /// @return The float32 representation of this float16 value
    [[nodiscard]] float to_float() const noexcept;
    
    /// @brief Conversion operator to float
    /// @return The float32 representation of this float16 value
    explicit operator float() const noexcept;
};

/// @brief Custom 24-bit floating point type
/// @details Stores float24 as a byte array. Uses custom format: 1 sign bit,
/// 7 exponent bits, 16 mantissa bits. Not an IEEE standard format.
/// @note This is a custom format, not widely supported outside TIFF.
struct Float24 {
    uint8_t bytes[3]; ///< Raw byte storage for the float24 value

    /// @brief Default constructor initializing to zero
    constexpr Float24() noexcept : bytes{0, 0, 0} {}

    /// @brief Construct from float32 value
    /// @param value The float32 value to convert to float24
    explicit Float24(float value) noexcept;
    
    /// @brief Treat as uint32 for predictor operations (padded to 24 bits)
    /// @return The float24 bits interpreted as uint32 (lower 24 bits)
    [[nodiscard]] uint32_t as_uint32() const noexcept;
    
    /// @brief Set from uint32 value (for predictor operations)
    /// @param val The uint32 value to store as float24 bits (uses lower 24 bits)
    void from_uint32(uint32_t val) noexcept;

    /// @brief Convert from float32 to float24
    /// @param value The float32 value to convert
    /// @note Float24 format: 1 sign bit, 7 exponent bits, 16 mantissa bits
    void from_float(float value) noexcept;
    
    /// @brief Convert from float24 to float32
    /// @return The float32 representation of this float24 value
    [[nodiscard]] float to_float() const noexcept;
    
    /// @brief Conversion operator to float
    /// @return The float32 representation of this float24 value
    explicit operator float() const noexcept;
};

/// @brief Byte-swap an integral value
/// @tparam T Integral type to swap
/// @param value The value to byte-swap
/// @return The byte-swapped value
/// @note For 1-byte types, returns the value unchanged
template <typename T>
[[nodiscard]] constexpr T byteswap(T value) noexcept requires std::is_integral_v<T>;

/// @brief Convert structure fields from source endianness to target endianness
/// @tparam T Type of the value to convert
/// @tparam SourceEndian Source endianness
/// @tparam TargetEndian Target endianness
/// @param value The value to convert (modified in place)
/// @note Handles integral types, floating point types, and structures (requires specialization)
template <typename T, std::endian SourceEndian, std::endian TargetEndian>
constexpr void convert_endianness([[maybe_unused]] T& value) noexcept;

/// @brief TIFF file header structure (Classic TIFF)
/// @tparam StorageEndian Endianness of the TIFF file
/// @details Contains the byte order mark, version (42 for Classic TIFF),
/// and offset to the first IFD. Always 8 bytes.
template <std::endian StorageEndian>
struct [[gnu::packed]] TiffHeader {
    std::array<char, 2> byte_order;  ///< "II" for little-endian, "MM" for big-endian
    uint16_t version;                ///< Must be 42 (0x002A) for classic TIFF
    uint32_t first_ifd_offset;       ///< Offset to first IFD from start of file

    /// @brief Check if the file is little-endian
    /// @return true if byte_order is "II"
    [[nodiscard]] constexpr bool is_little_endian() const noexcept;

    /// @brief Check if the file is big-endian
    /// @return true if byte_order is "MM"
    [[nodiscard]] constexpr bool is_big_endian() const noexcept;

    /// @brief Validate the header for the target endianness
    /// @tparam TargetEndian Expected endianness
    /// @return true if byte order matches TargetEndian and version is 42
    template <std::endian TargetEndian>
    [[nodiscard]] constexpr bool is_valid() const noexcept;

    /// @brief Get the first IFD offset with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The first IFD offset in native byte order
    template <std::endian TargetEndian>
    [[nodiscard]] uint32_t get_first_ifd_offset() const noexcept;
};

static_assert(sizeof(TiffHeader<std::endian::little>) == 8, "TiffHeader must be 8 bytes");
static_assert(sizeof(TiffHeader<std::endian::big>) == 8, "TiffHeader must be 8 bytes");

/// @brief BigTIFF file header structure
/// @tparam StorageEndian Endianness of the TIFF file
/// @details Contains the byte order mark, version (43 for BigTIFF),
/// offset size (8), reserved field (0), and offset to the first IFD. Always 16 bytes.
template <std::endian StorageEndian>
struct [[gnu::packed]] TiffBigHeader {
    std::array<char, 2> byte_order;  ///< "II" for little-endian, "MM" for big-endian
    uint16_t version;                ///< Must be 43 (0x002B) for BigTIFF
    uint16_t offset_size;            ///< Size of offsets (must be 8 for BigTIFF)
    uint16_t reserved;               ///< Must be 0
    uint64_t first_ifd_offset;       ///< Offset to first IFD from start of file

    /// @brief Check if the file is little-endian
    /// @return true if byte_order is "II"
    [[nodiscard]] constexpr bool is_little_endian() const noexcept;

    /// @brief Check if the file is big-endian
    /// @return true if byte_order is "MM"
    [[nodiscard]] constexpr bool is_big_endian() const noexcept;

    /// @brief Validate the header for the target endianness
    /// @tparam TargetEndian Expected endianness
    /// @return true if byte order matches TargetEndian, version is 43, offset_size is 8, and reserved is 0
    template <std::endian TargetEndian>
    [[nodiscard]] constexpr bool is_valid() const noexcept;

    /// @brief Get the first IFD offset with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The first IFD offset in native byte order
    template <std::endian TargetEndian>
    [[nodiscard]] uint64_t get_first_ifd_offset() const noexcept;
};

static_assert(sizeof(TiffBigHeader<std::endian::little>) == 16, "TiffBigHeader must be 16 bytes");
static_assert(sizeof(TiffBigHeader<std::endian::big>) == 16, "TiffBigHeader must be 16 bytes");

/// @brief TIFF format type discriminator
enum class TiffFormatType : uint16_t {
    Classic, ///< Classic TIFF (32-bit offsets)
    BigTIFF  ///< BigTIFF (64-bit offsets)
};

/// @brief IFD (Image File Directory) header for Classic TIFF
/// @tparam StorageEndian Endianness of the TIFF file
/// @details Contains the number of directory entries in the IFD. Always 2 bytes.
template <std::endian StorageEndian>
struct [[gnu::packed]] IFDHeader {
    uint16_t num_entries;  ///< Number of directory entries in this IFD

    /// @brief Get the number of entries with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The number of entries in native byte order
    template <std::endian TargetEndian>
    [[nodiscard]] uint16_t get_num_entries() const noexcept;
};

/// @brief IFD (Image File Directory) header for BigTIFF
/// @tparam StorageEndian Endianness of the TIFF file
/// @details Contains the number of directory entries in the IFD. Always 8 bytes.
template <std::endian StorageEndian>
struct [[gnu::packed]] IFDBigHeader {
    uint64_t num_entries;  ///< Number of directory entries in this IFD

    /// @brief Get the number of entries with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The number of entries in native byte order
    template <std::endian TargetEndian>
    [[nodiscard]] uint64_t get_num_entries() const noexcept;
};

static_assert(sizeof(IFDHeader<std::endian::little>) == 2, "IFDHeader must be 2 bytes");
static_assert(sizeof(IFDHeader<std::endian::big>) == 2, "IFDHeader must be 2 bytes");
static_assert(sizeof(IFDBigHeader<std::endian::little>) == 8, "IFDBigHeader must be 8 bytes");
static_assert(sizeof(IFDBigHeader<std::endian::big>) == 8, "IFDBigHeader must be 8 bytes");

/// @brief TIFF data type enumeration
/// @details Defines the data types that can be stored in TIFF tags.
/// Includes baseline TIFF types and BigTIFF extensions.
enum class TiffDataType : uint16_t {
    Byte      = 1,  ///< 8-bit unsigned integer
    Ascii     = 2,  ///< 8-bit byte containing a 7-bit ASCII code
    Short     = 3,  ///< 16-bit unsigned integer
    Long      = 4,  ///< 32-bit unsigned integer
    Rational  = 5,  ///< Two LONGs: numerator, denominator
    SByte     = 6,  ///< 8-bit signed integer
    Undefined = 7,  ///< 8-bit byte (uninterpreted)
    SShort    = 8,  ///< 16-bit signed integer
    SLong     = 9,  ///< 32-bit signed integer
    SRational = 10, ///< Two SLONGs: numerator, denominator
    Float     = 11, ///< Single precision (4-byte) IEEE format
    Double    = 12, ///< Double precision (8-byte) IEEE format
    IFD       = 13, ///< 32-bit IFD offset, equivalent to Long
    
    // BigTIFF extensions
    Long8     = 16, ///< 64-bit unsigned integer
    SLong8    = 17, ///< 64-bit signed integer
    IFD8      = 18  ///< 64-bit IFD offset
};

/// @brief Get size in bytes of a TIFF data type
/// @param type The TIFF data type
/// @return Size in bytes, or 0 for unknown types
[[nodiscard]] constexpr std::size_t tiff_type_size(TiffDataType type) noexcept;

/// @brief Union for tag values that fit in 4 bytes (Classic TIFF)
/// @details For Classic TIFF, if count*size <= 4 bytes, the value is stored inline.
/// Otherwise, this union contains an offset to the actual data.
union [[gnu::packed]] TagValue {
    uint32_t offset;                ///< Offset to data if count*size > 4 bytes
    std::array<uint8_t, 4> bytes;   ///< For up to 4 BYTE values
    std::array<char, 4> ascii;      ///< For up to 4 ASCII chars
    std::array<uint16_t, 2> shorts; ///< For up to 2 SHORT values
    uint32_t long_val;              ///< For 1 LONG value
    std::array<int8_t, 4> sbytes;   ///< For up to 4 SBYTE values
    std::array<int16_t, 2> sshorts; ///< For up to 2 SSHORT values
    int32_t slong_val;              ///< For 1 SLONG value
    float float_val;                ///< For 1 FLOAT value
    uint32_t ifd_offset;            ///< For 1 IFD offset
};

/// @brief Union for tag values that fit in 8 bytes (BigTIFF)
/// @details For BigTIFF, if count*size <= 8 bytes, the value is stored inline.
/// Otherwise, this union contains an offset to the actual data.
union [[gnu::packed]] TagBigValue {
    uint64_t offset;                   ///< Offset to data if count*size > 8 bytes
    std::array<uint8_t, 8> bytes;      ///< For up to 8 BYTE values
    std::array<char, 8> ascii;         ///< For up to 8 ASCII chars
    std::array<uint16_t, 4> shorts;    ///< For up to 4 SHORT values
    std::array<uint32_t, 2> longs;     ///< For up to 2 LONG values
    std::array<float, 2> floats;       ///< For up to 2 FLOAT values
    std::array<uint32_t, 2> ifd_offsets; ///< For up to 2 IFD offsets
    uint64_t long8_val;                ///< For 1 LONG8 value
    std::array<int8_t, 8> sbytes;      ///< For up to 8 SBYTE values
    std::array<int16_t, 4> sshorts;    ///< For up to 4 SSHORT values
    std::array<int32_t, 2> slongs;     ///< For up to 2 SLONG values
    int64_t slong8_val;                ///< For 1 SLONG8 value
    uint64_t ifd8_offset;              ///< For 1 IFD8 offset
    double double_val;                 ///< For 1 DOUBLE value
};

static_assert(sizeof(TagValue) == 4, "TagValue must be 4 bytes");
static_assert(sizeof(TagBigValue) == 8, "TagBigValue must be 8 bytes");

/// @brief TIFF tag entry in IFD (Classic TIFF)
/// @tparam StorageEndian Endianness of the TIFF file
/// @details Each IFD entry is 12 bytes and contains tag code, data type, count, and value/offset.
template <std::endian StorageEndian>
struct [[gnu::packed]] TiffTag {
    uint16_t code;         ///< Tag identifier (see TagCode enum)
    TiffDataType datatype; ///< Data type of the tag value
    uint32_t count;        ///< Number of values of the specified type
    TagValue value;        ///< Value or offset to value

    /// @brief Get the maximum byte count for inline storage
    /// @return 4 bytes for Classic TIFF
    [[nodiscard]] constexpr std::size_t inline_bytecount_limit() const noexcept;

    /// @brief Get the tag code with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The tag code in native byte order
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint16_t get_code() const noexcept;

    /// @brief Get the data type with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The data type in native byte order
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] TiffDataType get_datatype() const noexcept;

    /// @brief Get the count with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The count in native byte order
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint32_t get_count() const noexcept;

    /// @brief Check if value is stored inline or as an offset
    /// @return true if the value fits in the 4-byte inline storage
    [[nodiscard]] bool is_inline() const noexcept;

    /// @brief Get the offset to the data (only valid if !is_inline())
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The offset to external data in native byte order
    /// @note Asserts if called on an inline value
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint32_t get_offset() const noexcept;

    /// @brief Get the total size of the data in bytes
    /// @return count * size_of_type
    [[nodiscard]] std::size_t data_size() const noexcept;

    /// @brief Set the tag code with endianness conversion
    /// @tparam SourceEndian Endianness of the provided value
    /// @param tag_code The tag code to set
    template <std::endian SourceEndian = std::endian::native>
    void set_code(uint16_t tag_code) noexcept;

    /// @brief Set the data type with endianness conversion
    /// @tparam SourceEndian Endianness of the provided value
    /// @param type The data type to set
    template <std::endian SourceEndian = std::endian::native>
    void set_datatype(TiffDataType type) noexcept;

    /// @brief Set the count with endianness conversion
    /// @tparam SourceEndian Endianness of the provided value
    /// @param cnt The count to set
    template <std::endian SourceEndian = std::endian::native>
    void set_count(uint32_t cnt) noexcept;
};

/// @brief TIFF tag entry in IFD (BigTIFF)
/// @tparam StorageEndian Endianness of the TIFF file
/// @details Each BigTIFF IFD entry is 20 bytes and contains tag code, data type, count, and value/offset.
template <std::endian StorageEndian>
struct [[gnu::packed]] TiffBigTag {
    uint16_t code;         ///< Tag identifier (see TagCode enum)
    TiffDataType datatype; ///< Data type of the tag value
    uint64_t count;        ///< Number of values of the specified type
    TagBigValue value;     ///< Value or offset to value

    /// @brief Get the maximum byte count for inline storage
    /// @return 8 bytes for BigTIFF
    [[nodiscard]] constexpr std::size_t inline_bytecount_limit() const noexcept;

    /// @brief Get the tag code with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The tag code in native byte order
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint16_t get_code() const noexcept;

    /// @brief Get the data type with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The data type in native byte order
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] TiffDataType get_datatype() const noexcept;

    /// @brief Get the count with endianness conversion
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The count in native byte order
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint64_t get_count() const noexcept;

    /// @brief Check if value is stored inline or as an offset
    /// @return true if the value fits in the 8-byte inline storage
    [[nodiscard]] bool is_inline() const noexcept;

    /// @brief Get the offset to the data (only valid if !is_inline())
    /// @tparam TargetEndian Target endianness for the returned value
    /// @return The offset to external data in native byte order
    /// @note Asserts if called on an inline value
    template <std::endian TargetEndian = std::endian::native>
    [[nodiscard]] uint64_t get_offset() const noexcept;

    /// @brief Get the total size of the data in bytes
    /// @return count * size_of_type
    [[nodiscard]] std::size_t data_size() const noexcept;

    /// @brief Set the tag code with endianness conversion
    /// @tparam SourceEndian Endianness of the provided value
    /// @param tag_code The tag code to set
    template <std::endian SourceEndian = std::endian::native>
    void set_code(uint16_t tag_code) noexcept;

    /// @brief Set the data type with endianness conversion
    /// @tparam SourceEndian Endianness of the provided value
    /// @param type The data type to set
    template <std::endian SourceEndian = std::endian::native>
    void set_datatype(TiffDataType type) noexcept;

    /// @brief Set the count with endianness conversion
    /// @tparam SourceEndian Endianness of the provided value
    /// @param cnt The count to set
    template <std::endian SourceEndian = std::endian::native>
    void set_count(uint64_t cnt) noexcept;
};

static_assert(sizeof(TiffTag<std::endian::little>) == 12, "TiffTag must be 12 bytes");
static_assert(sizeof(TiffTag<std::endian::big>) == 12, "TiffTag must be 12 bytes");
static_assert(sizeof(TiffBigTag<std::endian::little>) == 20, "TiffBigTag must be 20 bytes");
static_assert(sizeof(TiffBigTag<std::endian::big>) == 20, "TiffBigTag must be 20 bytes");

#pragma pack(pop) // End of packed structures

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

/// @brief Compression schemes supported by TIFF
enum class CompressionScheme : uint16_t {
    None           = 1,     ///< No compression
    CCITT_RLE      = 2,     ///< CCITT modified Huffman RLE
    CCITT_Fax3     = 3,     ///< CCITT Group 3 fax
    CCITT_Fax4     = 4,     ///< CCITT Group 4 fax
    LZW            = 5,     ///< Lempel-Ziv-Welch
    JPEG_Old       = 6,     ///< Old-style JPEG (deprecated)
    JPEG           = 7,     ///< JPEG compression
    Deflate_Adobe  = 8,     ///< Adobe-style Deflate
    Deflate        = 32946, ///< PKZIP-style Deflate
    PackBits       = 32773, ///< PackBits compression
    ZSTD           = 50000, ///< Zstandard (non-standard but commonly used)
    ZSTD_Alt       = 34926, ///< Alternative Zstandard code
};

/// @brief Sample format specification
enum class SampleFormat : uint16_t {
    UnsignedInt   = 1, ///< Unsigned integer
    SignedInt     = 2, ///< Signed integer
    IEEEFloat     = 3, ///< IEEE floating point
    Undefined     = 4, ///< Undefined/uninterpreted
};

/// @brief Photometric interpretation (color space)
enum class PhotometricInterpretation : uint16_t {
    MinIsWhite    = 0, ///< Minimum value is white
    MinIsBlack    = 1, ///< Minimum value is black
    RGB           = 2, ///< RGB color space
    Palette       = 3, ///< Palette/indexed color
    Mask          = 4, ///< Transparency mask
    CMYK          = 5, ///< CMYK color space
    YCbCr         = 6, ///< YCbCr color space
    CIELab        = 8, ///< CIE L*a*b* color space
};

/// @brief Predictor for compression
enum class Predictor : uint16_t {
    None          = 1, ///< No predictor
    Horizontal    = 2, ///< Horizontal differencing
    FloatingPoint = 3, ///< Floating point horizontal differencing
};

/// @brief Planar configuration for multi-channel images
enum class PlanarConfiguration : uint16_t {
    Chunky = 1,  ///< RGBRGBRGB... (interleaved channels)
    Planar = 2,  ///< RRR...GGG...BBB... (separate planes per channel)
};

/// @brief Image buffer layout specification when extracting image data
/// @details Defines the order of dimensions in the output buffer.
/// 
/// For 2D single-channel images (depth=1, channels=1):
/// All layouts produce the same HW order.
/// 
/// For 2D multi-channel images (depth=1):
/// - DHWC: HWC (channels last)
/// - DCHW/CDHW: CHW (channels first)
/// 
/// Performance considerations:
/// - PlanarConfiguration::Chunky → DHWC typically performs best
/// - PlanarConfiguration::Planar → DCHW/CDHW typically performs best
///   (choice between DCHW and CDHW depends on which channels/depths are extracted)
/// 
/// @note For compression: Unless channels are strongly correlated,
/// PlanarConfiguration::Planar generally provides better compression ratios.
enum class ImageLayoutSpec {
    DHWC,  ///< Depth, Height, Width, Channels
    DCHW,  ///< Depth, Channels, Height, Width
    CDHW,  ///< Channels, Depth, Height, Width
};

} // namespace tiffconcept

#define TIFFCONCEPT_TYPES_HEADER
#include "impl/types_impl.hpp"
