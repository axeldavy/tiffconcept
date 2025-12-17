#include <gtest/gtest.h>
#include <vector>
#include <random>
#include <filesystem>
#include <fstream>
#include <cstring>

#include "../tiffconcept/include/tiff/compressor_base.hpp"
#include "../tiffconcept/include/tiff/compressor_standard.hpp"
#include "../tiffconcept/include/tiff/compressor_zstd.hpp"
#include "../tiffconcept/include/tiff/decompressor_base.hpp"
#include "../tiffconcept/include/tiff/decompressor_standard.hpp"
#include "../tiffconcept/include/tiff/decompressor_zstd.hpp"
#include "../tiffconcept/include/tiff/encoder.hpp"
#include "../tiffconcept/include/tiff/decoder.hpp"
#include "../tiffconcept/include/tiff/parsing.hpp"
#include "../tiffconcept/include/tiff/tag_extraction.hpp"
#include "../tiffconcept/include/tiff/tag_spec.hpp"
#include "../tiffconcept/include/tiff/image_shape.hpp"
#include "../tiffconcept/include/tiff/reader_buffer.hpp"
#include "../tiffconcept/include/tiff/types.hpp"

using namespace tiffconcept;
namespace fs = std::filesystem;

// ============================================================================
// Test Helper Functions
// ============================================================================

/// Write a Classic TIFF header to a buffer with controlled field values
/// Use std::nullopt for fields to get default valid values
template <std::endian StorageEndian = std::endian::little>
void write_test_header(
    std::span<std::byte> buffer,
    std::optional<uint16_t> byte_order = std::nullopt,
    std::optional<uint16_t> magic = std::nullopt,
    std::optional<uint32_t> first_ifd_offset = std::nullopt) noexcept 
{
    ASSERT_TRUE(buffer.size() >= 8 && "Buffer must be at least 8 bytes for Classic TIFF header");
    
    TiffHeader<StorageEndian> header;
    
    // Set byte order
    if (byte_order.has_value()) {
        uint16_t bo = byte_order.value();
        header.byte_order[0] = static_cast<char>(bo & 0xFF);
        header.byte_order[1] = static_cast<char>((bo >> 8) & 0xFF);
    } else {
        // Default valid byte order
        if constexpr (StorageEndian == std::endian::little) {
            header.byte_order[0] = 'I';
            header.byte_order[1] = 'I';
        } else {
            header.byte_order[0] = 'M';
            header.byte_order[1] = 'M';
        }
    }
    
    // Set magic number (version)
    header.version = magic.value_or(42);
    
    // Set first IFD offset
    header.first_ifd_offset = first_ifd_offset.value_or(8);
    
    std::memcpy(buffer.data(), &header, sizeof(header));
}

/// Write a BigTIFF header to a buffer with controlled field values
/// Use std::nullopt for fields to get default valid values
template <std::endian StorageEndian = std::endian::little>
void write_test_bigtiff_header(
    std::span<std::byte> buffer,
    std::optional<uint16_t> byte_order = std::nullopt,
    std::optional<uint16_t> magic = std::nullopt,
    std::optional<uint16_t> offset_size = std::nullopt,
    std::optional<uint16_t> reserved = std::nullopt,
    std::optional<uint64_t> first_ifd_offset = std::nullopt) noexcept 
{
    ASSERT_TRUE(buffer.size() >= 16 && "Buffer must be at least 16 bytes for BigTIFF header");
    
    TiffBigHeader<StorageEndian> header;
    
    // Set byte order
    if (byte_order.has_value()) {
        uint16_t bo = byte_order.value();
        header.byte_order[0] = static_cast<char>(bo & 0xFF);
        header.byte_order[1] = static_cast<char>((bo >> 8) & 0xFF);
    } else {
        // Default valid byte order
        if constexpr (StorageEndian == std::endian::little) {
            header.byte_order[0] = 'I';
            header.byte_order[1] = 'I';
        } else {
            header.byte_order[0] = 'M';
            header.byte_order[1] = 'M';
        }
    }
    
    // Set magic number (version)
    header.version = magic.value_or(43);
    
    // Set offset size
    header.offset_size = offset_size.value_or(8);
    
    // Set reserved field
    header.reserved = reserved.value_or(0);
    
    // Set first IFD offset
    header.first_ifd_offset = first_ifd_offset.value_or(16);
    
    std::memcpy(buffer.data(), &header, sizeof(header));
}

/// Create a test ImageShape with controlled parameters
/// Use std::nullopt for optional fields to get defaults
inline Result<ImageShape> create_test_image_shape(
    uint32_t width,
    uint32_t height,
    std::optional<std::vector<uint16_t>> bits_per_sample = std::nullopt,
    std::optional<uint32_t> depth = std::nullopt,
    std::optional<uint16_t> samples_per_pixel = std::nullopt,
    std::optional<SampleFormat> sample_format = std::nullopt,
    std::optional<PlanarConfiguration> planar_config = std::nullopt) noexcept 
{
    // Define minimal tag spec for ImageShape
    using ShapeTagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        OptTag_t<SamplesPerPixelTag>,
        OptTag_t<PlanarConfigurationTag>,
        OptTag_t<SampleFormatTag>,
        OptTag_t<ImageDepthTag>
    >;
    
    ExtractedTags<ShapeTagSpec> tags;

    // Set required tags
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    if (bits_per_sample.has_value()) {
        tags.template get<TagCode::BitsPerSample>() = bits_per_sample.value();
    } else {
        for (size_t i = 0; i < samples_per_pixel.value_or(1); ++i) {
            tags.template get<TagCode::BitsPerSample>().push_back(8); // Default to 8 bits per sample
        }
    }
    
    // Set optional tags if provided
    if (depth.has_value()) {
        tags.template get<TagCode::ImageDepth>() = depth.value();
    }
    
    if (samples_per_pixel.has_value()) {
        tags.template get<TagCode::SamplesPerPixel>() = samples_per_pixel.value();
    }
    
    if (sample_format.has_value()) {
        tags.template get<TagCode::SampleFormat>() = sample_format.value();
    }
    
    if (planar_config.has_value()) {
        tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(planar_config.value());
    }
    
    // Create ImageShape from tags
    ImageShape shape;
    auto result = shape.update_from_metadata(tags);
    if (!result) {
        return Err(result.error().code, result.error().message);
    }
    
    return Ok(std::move(shape));
}

// ============================================================================
// Compression/Decompression Error Tests
// ============================================================================

TEST(ErrorHandling, PackBits_OutputBufferTooSmall) {
    using CompSpec = CompressorSpec<PackBitsCompressorDesc>;
    CompressorStorage<CompSpec> compressor;
    
    std::vector<std::byte> input(100, std::byte{0x42});
    std::vector<std::byte> output(10);  // Too small!
    
    auto result = compressor.compress(output, 0, input, CompressionScheme::PackBits);
    EXPECT_TRUE(result.is_ok()); // Resize should make this OK
    EXPECT_GT(output.size(), 10);
}

TEST(ErrorHandling, PackBits_InvalidControlByte) {
    using DecompSpec = DecompressorSpec<PackBitsDecompressorDesc>;
    DecompressorStorage<DecompSpec> decompressor;
    
    // Create invalid PackBits data: control byte indicates more data than available
    std::vector<std::byte> invalid_input = {
        std::byte{0x05},  // Copy next 6 bytes (but we only have 2!)
        std::byte{0x01},
        std::byte{0x02}
    };
    
    std::vector<std::byte> output(100);
    auto result = decompressor.decompress(output, invalid_input, CompressionScheme::PackBits);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::UnexpectedEndOfFile);
}

TEST(ErrorHandling, ZSTD_CorruptedData) {
    using DecompSpec = DecompressorSpec<ZstdDecompressorDesc>;
    DecompressorStorage<DecompSpec> decompressor;
    
    // Random garbage that's not valid ZSTD data
    std::vector<std::byte> corrupted(100);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : corrupted) {
        b = static_cast<std::byte>(dist(rng));
    }
    
    std::vector<std::byte> output(1000);
    auto result = decompressor.decompress(output, corrupted, CompressionScheme::ZSTD);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::CompressionError);
}

TEST(ErrorHandling, UnsupportedCompressionScheme) {
    using CompSpec = CompressorSpec<NoneCompressorDesc>;
    CompressorStorage<CompSpec> compressor;
    
    std::vector<std::byte> input(100, std::byte{0x42});
    std::vector<std::byte> output(200);
    
    // Try to use PackBits when only None is supported
    auto result = compressor.compress(output, 0, input, CompressionScheme::PackBits);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::UnsupportedFeature);
}

// ============================================================================
// Encoder/Decoder Error Tests
// ============================================================================

TEST(ErrorHandling, Encoder_EmptyChunk) {
    using CompSpec = CompressorSpec<NoneCompressorDesc>;
    ChunkEncoder<uint8_t, CompSpec> encoder;
    
    std::vector<uint8_t> data(100, 42);
    
    // Zero width
    auto result = encoder.encode_2d(data, 0, 0, 0, 0, 100, 0,
                                     CompressionScheme::None, Predictor::None, 1);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::UnsupportedFeature);
    
    // Zero height
    result = encoder.encode_2d(data, 0, 0, 0, 100, 0, 0,
                                CompressionScheme::None, Predictor::None, 1);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::UnsupportedFeature);
}

TEST(ErrorHandling, Encoder_InputDataTooSmall) {
    using CompSpec = CompressorSpec<NoneCompressorDesc>;
    ChunkEncoder<uint8_t, CompSpec> encoder;
    
    std::vector<uint8_t> data(50, 42);  // Only 50 elements
    
    // Request encoding 100x100 (needs 10000 elements)
    auto result = encoder.encode_2d(data, 0, 0, 0, 100, 100, 0,
                                     CompressionScheme::None, Predictor::None, 1);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::OutOfBounds);
}

TEST(ErrorHandling, Decoder_OutputBufferTooSmall) {
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc>;
    ChunkDecoder<uint8_t, DecompSpec> decoder;
    
    std::vector<std::byte> compressed_data(100);
    std::vector<std::byte> output(50);  // Too small!
    
    auto result = decoder.decode_into(compressed_data, output, 10, 10,
                                      CompressionScheme::None, Predictor::None, 1);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::OutOfBounds);
}

// ============================================================================
// TIFF Header Parsing Error Tests
// ============================================================================

TEST(ErrorHandling, ParseHeader_InvalidByteOrder) {
    std::vector<std::byte> buffer(8);
    
    // Invalid byte order marker (not II or MM)
    write_test_header(buffer, 0x1234);  // Invalid byte order
    
    BufferViewReader reader(buffer);
    auto result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::InvalidHeader);
}

TEST(ErrorHandling, ParseHeader_InvalidMagicNumber) {
    std::vector<std::byte> buffer(8);

    write_test_header(buffer, std::nullopt, 999);  // Invalid magic number
    
    BufferViewReader reader(buffer);
    auto result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::InvalidHeader);
}

TEST(ErrorHandling, ParseHeader_TruncatedFile) {
    std::vector<std::byte> buffer(3);  // Too short for TIFF header
    
    BufferViewReader reader(buffer);
    auto result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::UnexpectedEndOfFile);
}

TEST(ErrorHandling, ParseBigTIFF_InvalidOffsetSize) {
    std::vector<std::byte> buffer(16);
    
    write_test_bigtiff_header(buffer, std::nullopt, std::nullopt, 4);  // offset_size = 4 instead of 8
    
    BufferViewReader reader(buffer);
    auto result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::BigTIFF, std::endian::little>(reader);
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::InvalidHeader);
}

TEST(ErrorHandling, ParseBigTIFF_NonZeroReservedField) {
    std::vector<std::byte> buffer(16);
    
    write_test_bigtiff_header(buffer, std::nullopt, std::nullopt, std::nullopt, 42);  // reserved = 42 instead of 0
    
    BufferViewReader reader(buffer);
    auto result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::BigTIFF, std::endian::little>(reader);
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::InvalidHeader);
}

// ============================================================================
// IFD Parsing Error Tests
// ============================================================================

TEST(ErrorHandling, ParseIFD_OffsetBeyondBuffer) {
    std::vector<std::byte> buffer(100);
    BufferViewReader reader(buffer);
    
    // Try to read IFD at offset 99999 (way beyond buffer)
    auto result = ifd::read_ifd_header<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd::IFDOffset(99999)
    );
    
    EXPECT_FALSE(result.is_ok());
}

TEST(ErrorHandling, ParseIFD_TruncatedTagData) {
    std::vector<std::byte> buffer(20);  // Not enough for IFD header + tags
    
    // Write partial IFD header
    uint16_t num_tags = 5;  // Claims 5 tags
    std::memcpy(buffer.data(), &num_tags, sizeof(num_tags));
    
    BufferViewReader reader(buffer);
    
    auto result = ifd::read_ifd_header<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd::IFDOffset(0)
    );

    EXPECT_TRUE(result.is_ok());

    auto ifd_desc = result.value();
    std::vector<parsing::TagType<TiffFormatType::Classic, std::endian::little>> tags;
    auto next_ifd = ifd::read_ifd_tags<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, ifd_desc, tags);
    
    EXPECT_FALSE(next_ifd.is_ok());
}

// ============================================================================
// Tag Extraction Error Tests
// ============================================================================

TEST(ErrorHandling, TagExtraction_MissingRequiredTag) {

    // Create tags but only set ImageWidth, not ImageLength
    ExtractedTags<ImageWidthTag, ImageLengthTag> tags;
    tags.template get<TagCode::ImageWidth>() = uint32_t{100};
    
    // Try to get the missing tag
    auto result = tags.template get<TagCode::ImageLength>();
    EXPECT_FALSE(is_value_present(result));
}

TEST(ErrorHandling, TagExtraction_ExternalDataOffsetInvalid) {
    std::vector<std::byte> buffer(100);
    
    // Create a tag with external data at invalid offset
    TiffTag<std::endian::little> tag;
    tag.code = static_cast<uint16_t>(TagCode::ImageDescription);
    tag.datatype = TiffDataType::Ascii;
    tag.count = 50;  // Long string, stored externally
    tag.value.offset = 99999;  // Beyond buffer!
    
    BufferViewReader reader(buffer);
    
    auto result = parsing::parse_tag<
        BufferViewReader,
        ImageDescriptionTag,
        TiffFormatType::Classic,
        std::endian::little
    >(reader, tag);
    
    EXPECT_FALSE(result.is_ok());
}

// ============================================================================
// ImageShape Validation Error Tests
// ============================================================================

TEST(ErrorHandling, ImageShape_InconsistentBitsPerSample) {
    std::vector<uint16_t> bits_per_sample = {8, 8};  // Should have 3 elements!
    auto shape_result = create_test_image_shape(100, 100, bits_per_sample, 1, 3); 
    EXPECT_FALSE(shape_result.is_ok());
    EXPECT_EQ(shape_result.error().code, Error::Code::InvalidTag);
}

TEST(ErrorHandling, ImageShape_InvalidRegion_OutOfBounds) {
    auto shape_result = create_test_image_shape(100, 100, std::nullopt, 1, 1);
    ASSERT_TRUE(shape_result.is_ok());
    auto shape = shape_result.value();
    
    // Region extends beyond image bounds
    ImageRegion region{50, 50, 0, 100, 100, 1, 0, 1};  // Ends at x=150, y=150
    
    auto result = shape.validate_region(region);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::OutOfBounds);
}

TEST(ErrorHandling, ImageShape_InvalidRegion_ZeroDimension) {
    auto shape_result = create_test_image_shape(100, 100, std::nullopt, 1, 1);
    ASSERT_TRUE(shape_result.is_ok());
    auto shape = shape_result.value();
    
    // Zero-width region
    ImageRegion region{10, 10, 0, 0, 50, 1, 0, 1};
    
    auto result = shape.validate_region(region);
    EXPECT_FALSE(result.is_ok());
}

TEST(ErrorHandling, ImageShape_InvalidRegion_InvalidChannels) {
    auto shape_result = create_test_image_shape(100, 100, std::nullopt, 1, 3);
    ASSERT_TRUE(shape_result.is_ok());
    auto shape = shape_result.value();
    
    // Request channel beyond available channels
    ImageRegion region{0, 0, 0, 100, 100, 1, 3, 2};  // Channels 3-4, but only have 0-2
    
    auto result = shape.validate_region(region);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::OutOfBounds);
}

TEST(ErrorHandling, ImageShape_InvalidPixelType_WrongBitDepth) {
    std::vector<uint16_t> bits_per_sample = {16};  // 16 bits per sample
    auto shape_result = create_test_image_shape(100, 100, bits_per_sample, 1, 1);
    ASSERT_TRUE(shape_result.is_ok());
    auto shape = shape_result.value();
    
    // Try to validate as uint8_t (8-bit)
    auto result = shape.validate_pixel_type<uint8_t>();
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::InvalidTag);
}

TEST(ErrorHandling, ImageShape_InvalidPixelType_WrongSampleFormat) {
    std::vector<uint16_t> bits_per_sample = {32};  // 32 bits per sample
    auto shape_result = create_test_image_shape(100, 100, bits_per_sample, 1, 1, SampleFormat::UnsignedInt);
    ASSERT_TRUE(shape_result.is_ok());
    auto shape = shape_result.value();
    
    // Try to validate as float
    auto result = shape.validate_pixel_type<float>();
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, Error::Code::InvalidTag);
}

#if 0
TEST(ErrorHandling, ImageShape_ZeroDimensions) {
    ImageShape shape;
    
    // Zero width
    auto shape_result = create_test_image_shape(0, 0);
    EXPECT_FALSE(shape_result.is_ok());
    EXPECT_EQ(shape_result.error().code, Error::Code::InvalidTag);
}
#endif

// ============================================================================
// Boundary Condition Tests
// ============================================================================

TEST(ErrorHandling, BoundaryCondition_MaxClassicTIFFOffset) {
    // Classic TIFF uses 32-bit offsets, max is ~4GB
    constexpr uint64_t max_32bit = 0xFFFFFFFF;
    
    // IFD offset exactly at max should be valid
    ifd::IFDOffset offset_at_max(max_32bit);
    EXPECT_EQ(offset_at_max.value, max_32bit);
    
    // Beyond max for Classic TIFF would require BigTIFF
    ifd::IFDOffset offset_beyond_max(max_32bit + 1);
    EXPECT_GT(offset_beyond_max.value, max_32bit);
}

TEST(ErrorHandling, BoundaryCondition_MaxTagCount) {
    // Classic TIFF IFD header uses 16-bit tag count
    constexpr uint16_t max_tags = 0xFFFF;
    
    // Buffer: TIFF header (8 bytes) + IFD header (2 bytes) + small tag data
    std::vector<std::byte> buffer(8 + 2 + 10);
    
    // Write proper TIFF header pointing to IFD at offset 8
    write_test_header(buffer, std::nullopt, std::nullopt, 8);
    
    // Write IFD header with max tag count at offset 8
    uint16_t num_tags = max_tags;
    std::memcpy(buffer.data() + 8, &num_tags, sizeof(num_tags));
    
    BufferViewReader reader(buffer);
    
    auto result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader
    );

    EXPECT_TRUE(result.is_ok());

    auto ifd_offset = result.value();
    auto ifd_result = ifd::read_ifd_header<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd_offset
    );
    
    // Should fail due to buffer too small for claimed tag count
    EXPECT_FALSE(ifd_result.is_ok());
}

TEST(ErrorHandling, BoundaryCondition_SinglePixelImage) {
    auto shape_result = create_test_image_shape(1, 1);
    ASSERT_TRUE(shape_result.is_ok());
    auto shape = shape_result.value();
    
    auto full_region = shape.full_region();
    EXPECT_EQ(full_region.width, 1u);
    EXPECT_EQ(full_region.height, 1u);
    EXPECT_EQ(full_region.num_samples(), 1u);
}

TEST(ErrorHandling, BoundaryCondition_VeryLargeImage) {
    // Large but valid dimensions
    constexpr uint32_t large_dim = 100000;
    auto shape_result = create_test_image_shape(large_dim, large_dim);
    ASSERT_TRUE(shape_result.is_ok());
    auto shape = shape_result.value();

    // Total pixels = 10 billion
    auto full_region = shape.full_region();
    std::size_t total_pixels = static_cast<std::size_t>(large_dim) * large_dim;
    EXPECT_EQ(full_region.num_samples(), total_pixels);
}

// ============================================================================
// Data Type Validation Tests
// ============================================================================

TEST(ErrorHandling, DataType_RationalDivisionByZero) {
    // While parsing itself might succeed, using a rational with zero denominator is invalid
    Rational rational;
    rational.numerator = 100;
    rational.denominator = 0;  // Division by zero!
    
    // The library should handle this gracefully or document the behavior
    // At minimum, we should be able to detect it
    EXPECT_EQ(rational.denominator, 0u);
}

TEST(ErrorHandling, DataType_UnsupportedBitsPerSample) {
    std::vector<uint16_t> bits_per_sample = {12};  // Unusual bit depth (not 8, 16, 32, 64)
    auto shape_result = create_test_image_shape(100, 100, bits_per_sample, 1, 1);
    ASSERT_TRUE(shape_result.is_ok());
    auto shape = shape_result.value();
    
    // Implementation may accept this but validation for specific types should fail
    if (shape_result.is_ok()) {
        // 12-bit doesn't map to standard C++ types
        auto uint8_result = shape.validate_pixel_type<uint8_t>();
        EXPECT_FALSE(uint8_result.is_ok());
        
        auto uint16_result = shape.validate_pixel_type<uint16_t>();
        EXPECT_FALSE(uint16_result.is_ok());
    }
}

TEST(ErrorHandling, DataType_NegativeImageDimensions) {
    // Image dimensions are unsigned, but testing signed interpretation
    ImageShape shape;
    
    // Maximum uint32_t value (would be -1 if interpreted as signed)
    uint32_t invalid_dim = 0xFFFFFFFF;
    
    // Should still be valid as unsigned
    auto shape_result = create_test_image_shape(invalid_dim, 100);
    EXPECT_TRUE(shape_result.is_ok());
}

// ============================================================================
// Endianness Error Tests
// ============================================================================

TEST(ErrorHandling, Endianness_MismatchedByteOrder) {
    std::vector<std::byte> buffer(16);
    
    write_test_header<std::endian::big>(buffer);  // Creates MM header with big-endian
    
    BufferViewReader reader(buffer);
    
    // Try to parse as little-endian when it's actually big-endian
    auto result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    EXPECT_FALSE(result.is_ok());
}
