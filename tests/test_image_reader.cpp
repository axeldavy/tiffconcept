#include <gtest/gtest.h>
#include <vector>
#include <random>
#include <cstring>
#include <cmath>

#include "../tiffconcept/include/tiff/image_reader.hpp"
#include "../tiffconcept/include/tiff/tiff_writer.hpp"
#include "../tiffconcept/include/tiff/compressor_standard.hpp"
#include "../tiffconcept/include/tiff/compressor_zstd.hpp"
#include "../tiffconcept/include/tiff/decompressor_standard.hpp"
#include "../tiffconcept/include/tiff/decompressor_zstd.hpp"
#include "../tiffconcept/include/tiff/write_strategy.hpp"
#include "../tiffconcept/include/tiff/read_strategy.hpp"
#include "../tiffconcept/include/tiff/reader_buffer.hpp"
#include "../tiffconcept/include/tiff/tiling.hpp"
#include "../tiffconcept/include/tiff/strips.hpp"
#include "../tiffconcept/include/tiff/ifd.hpp"
#include "../tiffconcept/include/tiff/tag_extraction.hpp"
#include "../tiffconcept/include/tiff/tag_spec.hpp"

using namespace tiffconcept;

// ============================================================================
// Helper Functions
// ============================================================================

template <typename T>
std::vector<T> generate_test_image(uint32_t width, uint32_t height, uint32_t depth, uint16_t channels, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::size_t size = static_cast<std::size_t>(width) * height * depth * channels;
    std::vector<T> data(size);
    
    if constexpr (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<T> dist(0.0, 1.0);
        for (auto& val : data) val = dist(rng);
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& val : data) val = static_cast<T>(dist(rng));
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        std::uniform_int_distribution<int> dist(0, 65535);
        for (auto& val : data) val = static_cast<T>(dist(rng));
    } else {
        std::uniform_int_distribution<T> dist(0, 1000);
        for (auto& val : data) val = dist(rng);
    }
    
    return data;
}

template <typename T>
bool compare_images(std::span<const T> expected, std::span<const T> actual, T tolerance = T{}) {
    if (expected.size() != actual.size()) {
        return false;
    }
    
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if constexpr (std::is_floating_point_v<T>) {
            if (std::abs(expected[i] - actual[i]) > tolerance) {
                return false;
            }
        } else {
            if (expected[i] != actual[i]) {
                return false;
            }
        }
    }
    
    return true;
}

// ============================================================================
// Tiled Image Tests
// ============================================================================

TEST(ImageReaderTest, ReadTiledImage_Uint8_NoCompression) {
    using PixelType = uint8_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<BufferWriter>, LazyOffsets>;
    using ReadStrat = SingleThreadedReader;
    
    const uint32_t width = 256, height = 256;
    const uint32_t tile_width = 64, tile_height = 64;
    const uint16_t samples_per_pixel = 1;
    
    // Generate test data
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel);
    
    // Write TIFF to buffer
    BufferWriter buffer_writer;
    TiffWriter<PixelType, CompSpec, WriteConfig> writer;
    
    auto write_result = writer.write_single_image<OutputSpec::DHWC>(
        buffer_writer, original_data,
        width, height, tile_width, tile_height, samples_per_pixel,
        PlanarConfiguration::Chunky, CompressionScheme::None, Predictor::None
    );
    ASSERT_TRUE(write_result.is_ok());
    
    // Extract buffer
    auto file_data = buffer_writer.buffer();
    BufferReader reader(file_data);
    
    // Parse IFD and extract tags
    auto ifd_offset_result = ifd::get_first_ifd_offset<BufferReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(ifd_offset_result.is_ok());
    
    auto ifd_header_result = ifd::read_ifd_header<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, ifd_offset_result.value());
    ASSERT_TRUE(ifd_header_result.is_ok());
    
    std::vector<parsing::TagType<TiffFormatType::Classic, std::endian::little>> tags;
    auto next_ifd_result = ifd::read_ifd_tags<BufferReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd_header_result.value(), tags);
    ASSERT_TRUE(next_ifd_result.is_ok());
    
    // Extract tags into structured form
    ExtractedTags<MinTiledSpec> metadata;
    auto extract_result = metadata.extract<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, tags);
    if (!extract_result.is_ok()) {
        std::cerr << "Tag extraction failed: " << extract_result.error().message << std::endl;
    }
    ASSERT_TRUE(extract_result.is_ok());
    
    // Build TiledImageInfo from metadata
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(metadata);
    if (!update_result.is_ok()) {
        std::cerr << "ImageInfo update failed: " << update_result.error().message << std::endl;
    }
    ASSERT_TRUE(update_result.is_ok());
    
    // Read the image
    ImageReader<PixelType, DecompSpec, ReadStrat> image_reader;
    std::vector<PixelType> output_data(original_data.size());
    auto region = image_info.shape().full_region();
    
    auto read_result = image_reader.read_region<OutputSpec::DHWC>(
        reader, image_info, output_data, region);
    ASSERT_TRUE(read_result.is_ok());
    
    // Compare results
    EXPECT_TRUE(compare_images<PixelType>(original_data, output_data));
}

TEST(ImageReaderTest, ReadTiledImage_Uint16_PackBits) {
    using PixelType = uint16_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<BufferWriter>, LazyOffsets>;
    using ReadStrat = SingleThreadedReader;
    
    const uint32_t width = 128, height = 128;
    const uint32_t tile_width = 32, tile_height = 32;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel);
    
    BufferWriter buffer_writer;
    TiffWriter<PixelType, CompSpec, WriteConfig> writer;
    
    auto write_result = writer.write_single_image<OutputSpec::DHWC>(
        buffer_writer, original_data,
        width, height, tile_width, tile_height, samples_per_pixel,
        PlanarConfiguration::Chunky, CompressionScheme::PackBits, Predictor::None
    );
    ASSERT_TRUE(write_result.is_ok());
    
    auto file_data = buffer_writer.buffer();
    BufferReader reader(file_data);
    
    auto ifd_offset_result = ifd::get_first_ifd_offset<BufferReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(ifd_offset_result.is_ok());
    
    auto ifd_header_result = ifd::read_ifd_header<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, ifd_offset_result.value());
    ASSERT_TRUE(ifd_header_result.is_ok());
    
    std::vector<parsing::TagType<TiffFormatType::Classic, std::endian::little>> tags;
    auto next_ifd_result = ifd::read_ifd_tags<BufferReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd_header_result.value(), tags);
    ASSERT_TRUE(next_ifd_result.is_ok());
    
    ExtractedTags<MinTiledSpec> metadata;
    auto extract_result = metadata.extract<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, tags);
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(metadata);
    ASSERT_TRUE(update_result.is_ok());
    
    ImageReader<PixelType, DecompSpec, ReadStrat> image_reader;
    std::vector<PixelType> output_data(original_data.size());
    auto region = image_info.shape().full_region();
    
    auto read_result = image_reader.read_region<OutputSpec::DHWC>(
        reader, image_info, output_data, region);
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, output_data));
}

TEST(ImageReaderTest, ReadTiledImage_Float_NoCompression) {
    using PixelType = float;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<BufferWriter>, LazyOffsets>;
    using ReadStrat = SingleThreadedReader;
    
    const uint32_t width = 64, height = 64;
    const uint32_t tile_width = 16, tile_height = 16;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel);
    
    BufferWriter buffer_writer;
    TiffWriter<PixelType, CompSpec, WriteConfig> writer;
    
    auto write_result = writer.write_single_image<OutputSpec::DHWC>(
        buffer_writer, original_data,
        width, height, tile_width, tile_height, samples_per_pixel,
        PlanarConfiguration::Chunky, CompressionScheme::None, Predictor::None
    );
    ASSERT_TRUE(write_result.is_ok());
    
    auto file_data = buffer_writer.buffer();
    BufferReader reader(file_data);
    
    auto ifd_offset_result = ifd::get_first_ifd_offset<BufferReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(ifd_offset_result.is_ok());
    
    auto ifd_header_result = ifd::read_ifd_header<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, ifd_offset_result.value());
    ASSERT_TRUE(ifd_header_result.is_ok());
    
    std::vector<parsing::TagType<TiffFormatType::Classic, std::endian::little>> tags;
    auto next_ifd_result = ifd::read_ifd_tags<BufferReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd_header_result.value(), tags);
    ASSERT_TRUE(next_ifd_result.is_ok());
    
    ExtractedTags<MinTiledSpec> metadata;
    auto extract_result = metadata.extract<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, tags);
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(metadata);
    ASSERT_TRUE(update_result.is_ok());
    
    ImageReader<PixelType, DecompSpec, ReadStrat> image_reader;
    std::vector<PixelType> output_data(original_data.size());
    auto region = image_info.shape().full_region();
    
    auto read_result = image_reader.read_region<OutputSpec::DHWC>(
        reader, image_info, output_data, region);
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, output_data, PixelType{1e-6}));
}

// ============================================================================
// Stripped Image Tests
// ============================================================================

TEST(ImageReaderTest, ReadStrippedImage_Uint8_NoCompression) {
    using PixelType = uint8_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<BufferWriter>, LazyOffsets>;
    using ReadStrat = SingleThreadedReader;
    
    const uint32_t width = 128, height = 128;
    const uint32_t rows_per_strip = 16;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel);
    
    // Write as strips (tile_width = image width)
    BufferWriter buffer_writer;
    TiffWriter<PixelType, CompSpec, WriteConfig> writer;
    
    auto write_result = writer.write_stripped_image<OutputSpec::DHWC>(
        buffer_writer, original_data,
        width, height, rows_per_strip, samples_per_pixel,
        PlanarConfiguration::Chunky, CompressionScheme::None, Predictor::None
    );
    ASSERT_TRUE(write_result.is_ok());
    
    auto file_data = buffer_writer.buffer();
    BufferReader reader(file_data);
    
    // Parse using stripped tag spec
    auto ifd_offset_result = ifd::get_first_ifd_offset<BufferReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(ifd_offset_result.is_ok());
    
    auto ifd_header_result = ifd::read_ifd_header<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, ifd_offset_result.value());
    ASSERT_TRUE(ifd_header_result.is_ok());
    
    std::vector<parsing::TagType<TiffFormatType::Classic, std::endian::little>> tags;
    auto next_ifd_result = ifd::read_ifd_tags<BufferReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd_header_result.value(), tags);
    ASSERT_TRUE(next_ifd_result.is_ok());
    
    ExtractedTags<MinStrippedSpec> metadata;
    auto extract_result = metadata.extract<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, tags);
    if (!extract_result.is_ok()) {
        std::cerr << "Tag extraction failed: " << extract_result.error().message << std::endl;
    }
    ASSERT_TRUE(extract_result.is_ok());
    
    StrippedImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(metadata);
    ASSERT_TRUE(update_result.is_ok());
    
    ImageReader<PixelType, DecompSpec, ReadStrat> image_reader;
    std::vector<PixelType> output_data(original_data.size());
    auto region = image_info.shape().full_region();
    
    auto read_result = image_reader.read_region<OutputSpec::DHWC>(
        reader, image_info, output_data, region);
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, output_data));
}

// ============================================================================
// Multi-channel Tests
// ============================================================================

TEST(ImageReaderTest, ReadTiledImage_RGB_Uint8) {
    using PixelType = uint8_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<BufferWriter>, LazyOffsets>;
    using ReadStrat = SingleThreadedReader;
    
    const uint32_t width = 128, height = 128;
    const uint32_t tile_width = 32, tile_height = 32;
    const uint16_t samples_per_pixel = 3;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel);
    
    BufferWriter buffer_writer;
    TiffWriter<PixelType, CompSpec, WriteConfig> writer;
    
    auto write_result = writer.write_single_image<OutputSpec::DHWC>(
        buffer_writer, original_data,
        width, height, tile_width, tile_height, samples_per_pixel,
        PlanarConfiguration::Chunky, CompressionScheme::None, Predictor::None
    );
    ASSERT_TRUE(write_result.is_ok());
    
    auto file_data = buffer_writer.buffer();
    BufferReader reader(file_data);
    
    auto ifd_offset_result = ifd::get_first_ifd_offset<BufferReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(ifd_offset_result.is_ok());
    
    auto ifd_header_result = ifd::read_ifd_header<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, ifd_offset_result.value());
    ASSERT_TRUE(ifd_header_result.is_ok());
    
    std::vector<parsing::TagType<TiffFormatType::Classic, std::endian::little>> tags;
    auto next_ifd_result = ifd::read_ifd_tags<BufferReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd_header_result.value(), tags);
    ASSERT_TRUE(next_ifd_result.is_ok());
    
    ExtractedTags<MinTiledSpec> metadata;
    auto extract_result = metadata.extract<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, tags);
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(metadata);
    ASSERT_TRUE(update_result.is_ok());
    
    ImageReader<PixelType, DecompSpec, ReadStrat> image_reader;
    std::vector<PixelType> output_data(original_data.size());
    auto region = image_info.shape().full_region();
    
    auto read_result = image_reader.read_region<OutputSpec::DHWC>(
        reader, image_info, output_data, region);
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, output_data));
}

// ============================================================================
// Partial Region Reading Tests
// ============================================================================

TEST(ImageReaderTest, ReadPartialRegion_Tiled) {
    using PixelType = uint16_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<BufferWriter>, LazyOffsets>;
    using ReadStrat = SingleThreadedReader;
    
    const uint32_t width = 256, height = 256;
    const uint32_t tile_width = 64, tile_height = 64;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel);
    
    BufferWriter buffer_writer;
    TiffWriter<PixelType, CompSpec, WriteConfig> writer;
    
    auto write_result = writer.write_single_image<OutputSpec::DHWC>(
        buffer_writer, original_data,
        width, height, tile_width, tile_height, samples_per_pixel,
        PlanarConfiguration::Chunky, CompressionScheme::None, Predictor::None
    );
    ASSERT_TRUE(write_result.is_ok());
    
    auto file_data = buffer_writer.buffer();
    BufferReader reader(file_data);
    
    auto ifd_offset_result = ifd::get_first_ifd_offset<BufferReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(ifd_offset_result.is_ok());
    
    auto ifd_header_result = ifd::read_ifd_header<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, ifd_offset_result.value());
    ASSERT_TRUE(ifd_header_result.is_ok());
    
    std::vector<parsing::TagType<TiffFormatType::Classic, std::endian::little>> tags;
    auto next_ifd_result = ifd::read_ifd_tags<BufferReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd_header_result.value(), tags);
    ASSERT_TRUE(next_ifd_result.is_ok());
    
    ExtractedTags<MinTiledSpec> metadata;
    auto extract_result = metadata.extract<BufferReader, TiffFormatType::Classic, std::endian::little>(reader, tags);
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(metadata);
    ASSERT_TRUE(update_result.is_ok());
    
    // Read a 64x64 region starting at (100, 100)
    ImageRegion region(100, 100, 0, 0, samples_per_pixel, 1, 64, 64);
    std::vector<PixelType> output_data(64 * 64 * samples_per_pixel);
    
    ImageReader<PixelType, DecompSpec, ReadStrat> image_reader;
    auto read_result = image_reader.read_region<OutputSpec::DHWC>(
        reader, image_info, output_data, region);
    ASSERT_TRUE(read_result.is_ok());
    
    // Verify by comparing with expected region from original data
    for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            std::size_t orig_idx = ((100 + y) * width + (100 + x)) * samples_per_pixel;
            std::size_t out_idx = (y * 64 + x) * samples_per_pixel;
            for (uint16_t c = 0; c < samples_per_pixel; ++c) {
                EXPECT_EQ(original_data[orig_idx + c], output_data[out_idx + c]);
            }
        }
    }
}
