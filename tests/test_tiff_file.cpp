#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <random>
#include <cstring>
#include <cmath>

#include "../tiffconcept/include/tiffconcept/compressors/compressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/compressors/compressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/image_reader.hpp"
#include "../tiffconcept/include/tiffconcept/lowlevel/tag_writing.hpp"
#include "../tiffconcept/include/tiffconcept/lowlevel/tiling.hpp"
#include "../tiffconcept/include/tiffconcept/parsing.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_buffer.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_stream.hpp"
#include "../tiffconcept/include/tiffconcept/strategy/write_strategy.hpp"
#include "../tiffconcept/include/tiffconcept/tag_extraction.hpp"
#include "../tiffconcept/include/tiffconcept/tiff_writer.hpp"
#include "../tiffconcept/include/tiffconcept/types/tag_spec.hpp"
#include "../tiffconcept/include/tiffconcept/types/tag_spec_examples.hpp"

using namespace tiffconcept;
namespace fs = std::filesystem;

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

fs::path get_temp_filepath(const std::string& filename) {
    return fs::temp_directory_path() / filename;
}

void cleanup_file(const fs::path& path) {
    if (fs::exists(path)) {
        fs::remove(path);
    }
}

// ============================================================================
// Classic TIFF Round-trip Tests
// ============================================================================

TEST(TiffFileRoundtrip, ClassicTIFF_Uint8_NoCompression_LittleEndian) {
    using PixelType = uint8_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
    using WriterType = TiffWriter<PixelType, CompSpec, WriteConfig, TiffFormatType::Classic, std::endian::little>;
    
    const uint32_t width = 128;
    const uint32_t height = 128;
    const uint32_t tile_width = 32;
    const uint32_t tile_height = 32;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel, 12345);
    
    // Create minimal tag set
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag,
        SamplesPerPixelTag,
        PlanarConfigurationTag,
        TileWidthTag,
        TileLengthTag,
        OptTag_t<TileOffsetsTag>,
        OptTag_t<TileByteCountsTag>
    >;
    
    ExtractedTags<TagSpec> tags;
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    tags.template get<TagCode::BitsPerSample>() = std::vector<uint16_t>{8};
    tags.template get<TagCode::Compression>() = CompressionScheme::None;
    tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::MinIsBlack;
    tags.template get<TagCode::SamplesPerPixel>() = samples_per_pixel;
    tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(PlanarConfiguration::Chunky);
    tags.template get<TagCode::TileWidth>() = tile_width;
    tags.template get<TagCode::TileLength>() = tile_height;
    
    // Write TIFF file
    auto filepath = get_temp_filepath("test_classic_uint8_le.tif");
    cleanup_file(filepath);
    
    WriterType tiff_writer;
    StreamFileWriter file_writer(filepath.string());
    
    auto write_result = tiff_writer.template write_single_image<ImageLayoutSpec::DHWC>(
        file_writer,
        original_data,
        width, height,
        tile_width, tile_height,
        samples_per_pixel,
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::None,
        tags
    );
    
    ASSERT_TRUE(write_result.is_ok()) << "Failed to write TIFF: " << write_result.error().message;
    
    // Read TIFF file back
    StreamFileReader file_reader(filepath.string());
    
    // Get first IFD offset
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_offset = first_ifd_result.value();
    
    // Read IFD
    auto ifd_result = ifd::read_ifd<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, ifd_offset);
    ASSERT_TRUE(ifd_result.is_ok());
    
    // Extract tags
    ExtractedTags<TagSpec> read_tags;
    auto extract_result = read_tags.extract<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, std::span(ifd_result.value().tags));
    ASSERT_TRUE(extract_result.is_ok());
    
    // Create TiledImageInfo from tags
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(read_tags);
    ASSERT_TRUE(update_result.is_ok());
    
    // Read image data
    SimpleReader<PixelType, DecompSpec> image_reader;
    auto region = image_info.shape().full_region();
    std::vector<PixelType> read_data(region.num_samples());
    
    auto read_result = image_reader.read_region<ImageLayoutSpec::DHWC>(
        file_reader, read_tags, region, read_data
    );
    ASSERT_TRUE(read_result.is_ok());
    
    // Verify data matches
    EXPECT_TRUE(compare_images<PixelType>(original_data, read_data));
    
    cleanup_file(filepath);
}

TEST(TiffFileRoundtrip, ClassicTIFF_Uint16_ZSTD_BigEndian) {
    using PixelType = uint16_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
    using WriterType = TiffWriter<PixelType, CompSpec, WriteConfig, TiffFormatType::Classic, std::endian::big>;
    
    const uint32_t width = 100;
    const uint32_t height = 100;
    const uint32_t tile_width = 50;
    const uint32_t tile_height = 50;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel, 54321);
    
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag,
        SamplesPerPixelTag,
        PlanarConfigurationTag,
        PredictorTag,
        TileWidthTag,
        TileLengthTag,
        OptTag_t<TileOffsetsTag>,
        OptTag_t<TileByteCountsTag>
    >;
    
    ExtractedTags<TagSpec> tags;
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    tags.template get<TagCode::BitsPerSample>() = std::vector<uint16_t>{16};
    tags.template get<TagCode::Compression>() = CompressionScheme::ZSTD;
    tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::MinIsBlack;
    tags.template get<TagCode::SamplesPerPixel>() = samples_per_pixel;
    tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(PlanarConfiguration::Chunky);
    tags.template get<TagCode::TileWidth>() = tile_width;
    tags.template get<TagCode::TileLength>() = tile_height;
    tags.template get<TagCode::Predictor>() = Predictor::Horizontal;
    
    auto filepath = get_temp_filepath("test_classic_uint16_zstd_be.tif");
    cleanup_file(filepath);
    
    WriterType tiff_writer;
    StreamFileWriter file_writer(filepath.string());
    
    auto write_result = tiff_writer.template write_single_image<ImageLayoutSpec::DHWC>(
        file_writer,
        original_data,
        width, height,
        tile_width, tile_height,
        samples_per_pixel,
        PlanarConfiguration::Chunky,
        CompressionScheme::ZSTD,
        Predictor::Horizontal,
        tags
    );
    
    ASSERT_TRUE(write_result.is_ok());
    
    // Read back with big endian
    StreamFileReader file_reader(filepath.string());
    
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::Classic, std::endian::big>(file_reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_offset = first_ifd_result.value();
    
    auto ifd_result = ifd::read_ifd<StreamFileReader, TiffFormatType::Classic, std::endian::big>(
        file_reader, ifd_offset);
    ASSERT_TRUE(ifd_result.is_ok());
    
    ExtractedTags<TagSpec> read_tags;
    auto extract_result = read_tags.extract<StreamFileReader, TiffFormatType::Classic, std::endian::big>(
        file_reader, std::span(ifd_result.value().tags));
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(read_tags);
    ASSERT_TRUE(update_result.is_ok());
    
    SimpleReader<PixelType, DecompSpec> image_reader;
    auto region = image_info.shape().full_region();
    std::vector<PixelType> read_data(region.num_samples());
    
    auto read_result = image_reader.read_region<ImageLayoutSpec::DHWC>(
        file_reader, read_tags, region, read_data
    );
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, read_data));
    
    cleanup_file(filepath);
}

TEST(TiffFileRoundtrip, ClassicTIFF_Float_Predictor) {
    using PixelType = float;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
    using WriterType = TiffWriter<PixelType, CompSpec, WriteConfig, TiffFormatType::Classic, std::endian::little>;
    
    const uint32_t width = 80;
    const uint32_t height = 80;
    const uint32_t tile_width = 40;
    const uint32_t tile_height = 40;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel, 99999);
    
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag,
        SamplesPerPixelTag,
        PlanarConfigurationTag,
        PredictorTag,
        TileWidthTag,
        TileLengthTag,
        OptTag_t<TileOffsetsTag>,
        OptTag_t<TileByteCountsTag>,
        SampleFormatTag
    >;
    
    ExtractedTags<TagSpec> tags;
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    tags.template get<TagCode::BitsPerSample>() = std::vector<uint16_t>{32};
    tags.template get<TagCode::Compression>() = CompressionScheme::ZSTD;
    tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::MinIsBlack;
    tags.template get<TagCode::SamplesPerPixel>() = samples_per_pixel;
    tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(PlanarConfiguration::Chunky);
    tags.template get<TagCode::TileWidth>() = tile_width;
    tags.template get<TagCode::TileLength>() = tile_height;
    tags.template get<TagCode::Predictor>() = Predictor::FloatingPoint;
    tags.template get<TagCode::SampleFormat>() = SampleFormat::IEEEFloat;
    
    auto filepath = get_temp_filepath("test_classic_float_predictor.tif");
    cleanup_file(filepath);
    
    WriterType tiff_writer;
    StreamFileWriter file_writer(filepath.string());
    
    auto write_result = tiff_writer.template write_single_image<ImageLayoutSpec::DHWC>(
        file_writer,
        original_data,
        width, height,
        tile_width, tile_height,
        samples_per_pixel,
        PlanarConfiguration::Chunky,
        CompressionScheme::ZSTD,
        Predictor::FloatingPoint,
        tags
    );
    
    ASSERT_TRUE(write_result.is_ok());
    
    StreamFileReader file_reader(filepath.string());
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_result = ifd::read_ifd<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, first_ifd_result.value());
    ASSERT_TRUE(ifd_result.is_ok());
    
    ExtractedTags<TagSpec> read_tags;
    auto extract_result = read_tags.extract<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, std::span(ifd_result.value().tags));
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(read_tags);
    ASSERT_TRUE(update_result.is_ok());
    
    SimpleReader<PixelType, DecompSpec> image_reader;
    auto region = image_info.shape().full_region();
    std::vector<PixelType> read_data(region.num_samples());
    
    auto read_result = image_reader.read_region<ImageLayoutSpec::DHWC>(
        file_reader, read_tags, region, read_data
    );
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, read_data, 1e-6f));
    
    cleanup_file(filepath);
}

TEST(TiffFileRoundtrip, ClassicTIFF_MultiChannel_Chunky) {
    using PixelType = uint8_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
    using WriterType = TiffWriter<PixelType, CompSpec, WriteConfig, TiffFormatType::Classic, std::endian::little>;
    
    const uint32_t width = 128;
    const uint32_t height = 128;
    const uint32_t tile_width = 64;
    const uint32_t tile_height = 64;
    const uint16_t samples_per_pixel = 3;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel, 11111);
    
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag,
        SamplesPerPixelTag,
        PlanarConfigurationTag,
        TileWidthTag,
        TileLengthTag,
        OptTag_t<TileOffsetsTag>,
        OptTag_t<TileByteCountsTag>
    >;
    
    ExtractedTags<TagSpec> tags;
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    tags.template get<TagCode::BitsPerSample>() = std::vector<uint16_t>{8, 8, 8};
    tags.template get<TagCode::Compression>() = CompressionScheme::PackBits;
    tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::RGB;
    tags.template get<TagCode::SamplesPerPixel>() = samples_per_pixel;
    tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(PlanarConfiguration::Chunky);
    tags.template get<TagCode::TileWidth>() = tile_width;
    tags.template get<TagCode::TileLength>() = tile_height;
    
    auto filepath = get_temp_filepath("test_classic_rgb_chunky.tif");
    cleanup_file(filepath);
    
    WriterType tiff_writer;
    StreamFileWriter file_writer(filepath.string());
    
    auto write_result = tiff_writer.template write_single_image<ImageLayoutSpec::DHWC>(
        file_writer,
        original_data,
        width, height,
        tile_width, tile_height,
        samples_per_pixel,
        PlanarConfiguration::Chunky,
        CompressionScheme::PackBits,
        Predictor::None,
        tags
    );
    
    ASSERT_TRUE(write_result.is_ok());
    
    StreamFileReader file_reader(filepath.string());
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_result = ifd::read_ifd<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, first_ifd_result.value());
    ASSERT_TRUE(ifd_result.is_ok());
    
    ExtractedTags<TagSpec> read_tags;
    auto extract_result = read_tags.extract<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, std::span(ifd_result.value().tags));
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(read_tags);
    ASSERT_TRUE(update_result.is_ok());
    
    SimpleReader<PixelType, DecompSpec> image_reader;
    auto region = image_info.shape().full_region();
    std::vector<PixelType> read_data(region.num_samples());
    
    auto read_result = image_reader.read_region<ImageLayoutSpec::DHWC>(
        file_reader, read_tags, region, read_data
    );
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, read_data));
    
    cleanup_file(filepath);
}

TEST(TiffFileRoundtrip, ClassicTIFF_MultiChannel_Planar) {
    using PixelType = uint16_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
    using WriterType = TiffWriter<PixelType, CompSpec, WriteConfig, TiffFormatType::Classic, std::endian::little>;
    
    const uint32_t width = 64;
    const uint32_t height = 64;
    const uint32_t tile_width = 32;
    const uint32_t tile_height = 32;
    const uint16_t samples_per_pixel = 4;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel, 22222);
    
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag,
        SamplesPerPixelTag,
        PlanarConfigurationTag,
        TileWidthTag,
        TileLengthTag,
        OptTag_t<TileOffsetsTag>,
        OptTag_t<TileByteCountsTag>
    >;
    
    ExtractedTags<TagSpec> tags;
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    tags.template get<TagCode::BitsPerSample>() = std::vector<uint16_t>{16, 16, 16, 16};
    tags.template get<TagCode::Compression>() = CompressionScheme::ZSTD;
    tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::RGB;
    tags.template get<TagCode::SamplesPerPixel>() = samples_per_pixel;
    tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(PlanarConfiguration::Planar);
    tags.template get<TagCode::TileWidth>() = tile_width;
    tags.template get<TagCode::TileLength>() = tile_height;
    
    auto filepath = get_temp_filepath("test_classic_rgba_planar.tif");
    cleanup_file(filepath);
    
    WriterType tiff_writer;
    StreamFileWriter file_writer(filepath.string());
    
    auto write_result = tiff_writer.template write_single_image<ImageLayoutSpec::DHWC>(
        file_writer,
        original_data,
        width, height,
        tile_width, tile_height,
        samples_per_pixel,
        PlanarConfiguration::Planar,
        CompressionScheme::ZSTD,
        Predictor::None,
        tags
    );
    
    ASSERT_TRUE(write_result.is_ok());
    
    StreamFileReader file_reader(filepath.string());
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_result = ifd::read_ifd<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, first_ifd_result.value());
    ASSERT_TRUE(ifd_result.is_ok());
    
    ExtractedTags<TagSpec> read_tags;
    auto extract_result = read_tags.extract<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, std::span(ifd_result.value().tags));
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(read_tags);
    ASSERT_TRUE(update_result.is_ok());
    
    SimpleReader<PixelType, DecompSpec> image_reader;
    auto region = image_info.shape().full_region();
    std::vector<PixelType> read_data(region.num_samples());
    
    auto read_result = image_reader.read_region<ImageLayoutSpec::DHWC>(
        file_reader, read_tags, region, read_data
    );
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, read_data));
    
    cleanup_file(filepath);
}

// ============================================================================
// Stripped Image Round-trip Tests
// ============================================================================

TEST(TiffFileRoundtrip, ClassicTIFF_Stripped_Uint8) {
    using PixelType = uint8_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
    using WriterType = TiffWriter<PixelType, CompSpec, WriteConfig, TiffFormatType::Classic, std::endian::little>;
    
    const uint32_t width = 120;
    const uint32_t height = 120;
    const uint32_t rows_per_strip = 30;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel, 33333);
    
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag,
        OptTag_t<StripOffsetsTag>,
        SamplesPerPixelTag,
        RowsPerStripTag,
        OptTag_t<StripByteCountsTag>,
        PlanarConfigurationTag
    >;
    
    ExtractedTags<TagSpec> tags;
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    tags.template get<TagCode::BitsPerSample>() = std::vector<uint16_t>{8};
    tags.template get<TagCode::Compression>() = CompressionScheme::None;
    tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::MinIsBlack;
    tags.template get<TagCode::SamplesPerPixel>() = samples_per_pixel;
    tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(PlanarConfiguration::Chunky);
    tags.template get<TagCode::RowsPerStrip>() = rows_per_strip;
    
    auto filepath = get_temp_filepath("test_classic_stripped.tif");
    cleanup_file(filepath);
    
    WriterType tiff_writer;
    StreamFileWriter file_writer(filepath.string());
    
    auto write_result = tiff_writer.template write_stripped_image<ImageLayoutSpec::DHWC>(
        file_writer,
        original_data,
        width, height,
        rows_per_strip,
        samples_per_pixel,
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::None,
        tags
    );
    
    ASSERT_TRUE(write_result.is_ok());
    
    StreamFileReader file_reader(filepath.string());
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_result = ifd::read_ifd<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, first_ifd_result.value());
    ASSERT_TRUE(ifd_result.is_ok());
    
    ExtractedTags<TagSpec> read_tags;
    auto extract_result = read_tags.extract<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, std::span(ifd_result.value().tags));
    ASSERT_TRUE(extract_result.is_ok());
    
    StrippedImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(read_tags);
    ASSERT_TRUE(update_result.is_ok());
    
    SimpleReader<PixelType, DecompSpec> image_reader;
    auto region = image_info.shape().full_region();
    std::vector<PixelType> read_data(region.num_samples());
    
    auto read_result = image_reader.read_region<ImageLayoutSpec::DHWC>(
        file_reader, read_tags, region, read_data
    );
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, read_data));
    
    cleanup_file(filepath);
}

// ============================================================================
// BigTIFF Round-trip Tests
// ============================================================================

TEST(TiffFileRoundtrip, BigTIFF_Uint16_LittleEndian) {
    using PixelType = uint16_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
    using WriterType = TiffWriter<PixelType, CompSpec, WriteConfig, TiffFormatType::BigTIFF, std::endian::little>;
    
    const uint32_t width = 150;
    const uint32_t height = 150;
    const uint32_t tile_width = 50;
    const uint32_t tile_height = 50;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel, 44444);
    
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag,
        SamplesPerPixelTag,
        PlanarConfigurationTag,
        TileWidthTag,
        TileLengthTag,
        OptTag_t<TileOffsetsTag_BigTIFF>,
        OptTag_t<TileByteCountsTag_BigTIFF>
    >;
    
    ExtractedTags<TagSpec> tags;
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    tags.template get<TagCode::BitsPerSample>() = std::vector<uint16_t>{16};
    tags.template get<TagCode::Compression>() = CompressionScheme::None;
    tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::MinIsBlack;
    tags.template get<TagCode::SamplesPerPixel>() = samples_per_pixel;
    tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(PlanarConfiguration::Chunky);
    tags.template get<TagCode::TileWidth>() = tile_width;
    tags.template get<TagCode::TileLength>() = tile_height;
    
    auto filepath = get_temp_filepath("test_bigtiff_uint16_le.tif");
    cleanup_file(filepath);
    
    WriterType tiff_writer;
    StreamFileWriter file_writer(filepath.string());
    
    auto write_result = tiff_writer.template write_single_image<ImageLayoutSpec::DHWC>(
        file_writer,
        original_data,
        width, height,
        tile_width, tile_height,
        samples_per_pixel,
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::None,
        tags
    );
    
    ASSERT_TRUE(write_result.is_ok());
    
    StreamFileReader file_reader(filepath.string());
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::BigTIFF, std::endian::little>(file_reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_result = ifd::read_ifd<StreamFileReader, TiffFormatType::BigTIFF, std::endian::little>(
        file_reader, first_ifd_result.value());
    ASSERT_TRUE(ifd_result.is_ok());
    
    ExtractedTags<TagSpec> read_tags;
    auto extract_result = read_tags.extract<StreamFileReader, TiffFormatType::BigTIFF, std::endian::little>(
        file_reader, std::span(ifd_result.value().tags));
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(read_tags);
    ASSERT_TRUE(update_result.is_ok());
    
    SimpleReader<PixelType, DecompSpec> image_reader;
    auto region = image_info.shape().full_region();
    std::vector<PixelType> read_data(region.num_samples());
    
    auto read_result = image_reader.read_region<ImageLayoutSpec::DHWC>(
        file_reader, read_tags, region, read_data
    );
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, read_data));
    
    cleanup_file(filepath);
}

TEST(TiffFileRoundtrip, BigTIFF_Uint32_ZSTD_BigEndian) {
    using PixelType = uint32_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc, PackBitsCompressorDesc, ZstdCompressorDesc>;
    using DecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc, ZstdDecompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
    using WriterType = TiffWriter<PixelType, CompSpec, WriteConfig, TiffFormatType::BigTIFF, std::endian::big>;
    
    const uint32_t width = 100;
    const uint32_t height = 100;
    const uint32_t tile_width = 25;
    const uint32_t tile_height = 25;
    const uint16_t samples_per_pixel = 1;
    
    auto original_data = generate_test_image<PixelType>(width, height, 1, samples_per_pixel, 55555);
    
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag,
        SamplesPerPixelTag,
        PlanarConfigurationTag,
        TileWidthTag,
        TileLengthTag,
        OptTag_t<TileOffsetsTag_BigTIFF>,
        OptTag_t<TileByteCountsTag_BigTIFF>,
        SampleFormatTag
    >;
    
    ExtractedTags<TagSpec> tags;
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    tags.template get<TagCode::BitsPerSample>() = std::vector<uint16_t>{32};
    tags.template get<TagCode::Compression>() = CompressionScheme::ZSTD;
    tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::MinIsBlack;
    tags.template get<TagCode::SamplesPerPixel>() = samples_per_pixel;
    tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(PlanarConfiguration::Chunky);
    tags.template get<TagCode::TileWidth>() = tile_width;
    tags.template get<TagCode::TileLength>() = tile_height;
    tags.template get<TagCode::SampleFormat>() = SampleFormat::UnsignedInt;
    
    auto filepath = get_temp_filepath("test_bigtiff_uint32_zstd_be.tif");
    cleanup_file(filepath);
    
    WriterType tiff_writer;
    StreamFileWriter file_writer(filepath.string());
    
    auto write_result = tiff_writer.template write_single_image<ImageLayoutSpec::DHWC>(
        file_writer,
        original_data,
        width, height,
        tile_width, tile_height,
        samples_per_pixel,
        PlanarConfiguration::Chunky,
        CompressionScheme::ZSTD,
        Predictor::None,
        tags
    );
    
    ASSERT_TRUE(write_result.is_ok());
    
    StreamFileReader file_reader(filepath.string());
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::BigTIFF, std::endian::big>(file_reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_result = ifd::read_ifd<StreamFileReader, TiffFormatType::BigTIFF, std::endian::big>(
        file_reader, first_ifd_result.value());
    ASSERT_TRUE(ifd_result.is_ok());
    
    ExtractedTags<TagSpec> read_tags;
    auto extract_result = read_tags.extract<StreamFileReader, TiffFormatType::BigTIFF, std::endian::big>(
        file_reader, std::span(ifd_result.value().tags));
    ASSERT_TRUE(extract_result.is_ok());
    
    TiledImageInfo<PixelType> image_info;
    auto update_result = image_info.update_from_metadata(read_tags);
    ASSERT_TRUE(update_result.is_ok());
    
    SimpleReader<PixelType, DecompSpec> image_reader;
    auto region = image_info.shape().full_region();
    std::vector<PixelType> read_data(region.num_samples());
    
    auto read_result = image_reader.read_region<ImageLayoutSpec::DHWC>(
        file_reader, read_tags, region, read_data
    );
    ASSERT_TRUE(read_result.is_ok());
    
    EXPECT_TRUE(compare_images<PixelType>(original_data, read_data));
    
    cleanup_file(filepath);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(TiffFileRoundtrip, ErrorInvalidHeaderMagic) {
    // Create a file with invalid magic number
    auto filepath = get_temp_filepath("test_invalid_magic.tif");
    cleanup_file(filepath);
    
    std::ofstream file(filepath, std::ios::binary);
    uint16_t byte_order = 0x4949;  // Little endian
    uint16_t invalid_magic = 999;  // Invalid (should be 42 or 43)
    uint32_t ifd_offset = 8;
    file.write(reinterpret_cast<const char*>(&byte_order), sizeof(byte_order));
    file.write(reinterpret_cast<const char*>(&invalid_magic), sizeof(invalid_magic));
    file.write(reinterpret_cast<const char*>(&ifd_offset), sizeof(ifd_offset));  // IFD offset
    file.close();
    
    StreamFileReader file_reader(filepath.string());
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
    
    EXPECT_FALSE(first_ifd_result.is_ok());
    EXPECT_EQ(first_ifd_result.error().code, Error::Code::InvalidHeader);
    
    cleanup_file(filepath);
}

TEST(TiffFileRoundtrip, ErrorTruncatedFile) {
    // Write a valid TIFF but truncate it
    using PixelType = uint8_t;
    using CompSpec = CompressorSpec<NoneCompressorDesc>;
    using WriteConfig = WriteConfig<IFDAtEnd, SequentialTiles, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
    using WriterType = TiffWriter<PixelType, CompSpec, WriteConfig, TiffFormatType::Classic, std::endian::little>;
    
    const uint32_t width = 64;
    const uint32_t height = 64;
    auto original_data = generate_test_image<PixelType>(width, height, 1, 1, 66666);
    
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag,
        SamplesPerPixelTag,
        PlanarConfigurationTag,
        TileWidthTag,
        TileLengthTag,
        OptTag_t<TileOffsetsTag>,
        OptTag_t<TileByteCountsTag>
    >;
    
    ExtractedTags<TagSpec> tags;
    tags.template get<TagCode::ImageWidth>() = width;
    tags.template get<TagCode::ImageLength>() = height;
    tags.template get<TagCode::BitsPerSample>() = std::vector<uint16_t>{8};
    tags.template get<TagCode::Compression>() = CompressionScheme::None;
    tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::MinIsBlack;
    tags.template get<TagCode::SamplesPerPixel>() = uint16_t{1};
    tags.template get<TagCode::PlanarConfiguration>() = static_cast<uint16_t>(PlanarConfiguration::Chunky);
    tags.template get<TagCode::TileWidth>() = uint32_t{32};
    tags.template get<TagCode::TileLength>() = uint32_t{32};
    
    auto filepath = get_temp_filepath("test_truncated.tif");
    cleanup_file(filepath);
    
    WriterType tiff_writer;
    StreamFileWriter file_writer(filepath.string());
    
    auto write_result = tiff_writer.template write_single_image<ImageLayoutSpec::DHWC>(
        file_writer, original_data, width, height, 32, 32, 1,
        PlanarConfiguration::Chunky, CompressionScheme::None, Predictor::None, tags
    );
    ASSERT_TRUE(write_result.is_ok());
    
    // Truncate the file to header only
    {
        std::ofstream truncate(filepath, std::ios::binary | std::ios::trunc);
        uint16_t byte_order = 0x4949;
        uint16_t magic = 42;
        uint32_t ifd_offset = 8;
        truncate.write(reinterpret_cast<const char*>(&byte_order), sizeof(byte_order));
        truncate.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        truncate.write(reinterpret_cast<const char*>(&ifd_offset), sizeof(ifd_offset));
    }
    
    StreamFileReader file_reader(filepath.string());
    auto first_ifd_result = ifd::get_first_ifd_offset<StreamFileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_result = ifd::read_ifd<StreamFileReader, TiffFormatType::Classic, std::endian::little>(
        file_reader, first_ifd_result.value());
    
    // Should fail due to truncation
    EXPECT_FALSE(ifd_result.is_ok());
    
    cleanup_file(filepath);
}
