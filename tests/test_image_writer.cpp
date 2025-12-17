#include <gtest/gtest.h>
#include <vector>
#include <random>


#include "../tiffconcept/include/tiff/compressor_standard.hpp"
#include "../tiffconcept/include/tiff/compressor_zstd.hpp"
#include "../tiffconcept/include/tiff/image_writer.hpp"
#include "../tiffconcept/include/tiff/reader_buffer.hpp"
#include "../tiffconcept/include/tiff/write_strategy.hpp"


using namespace tiffconcept;

// ============================================================================
// Helper Functions
// ============================================================================

template <typename T>
std::vector<T> generate_test_image(uint32_t width, uint32_t height, uint16_t channels) {
    std::mt19937_64 rng(42);
    std::size_t size = static_cast<std::size_t>(width) * height * channels;
    std::vector<T> data(size);
    
    if constexpr (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<T> dist(0.0, 1.0);
        for (auto& val : data) val = dist(rng);
    } else {
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& val : data) val = static_cast<T>(dist(rng));
    }
    
    return data;
}

template <typename T>
std::vector<T> generate_gradient(uint32_t width, uint32_t height, uint16_t channels) {
    std::size_t size = static_cast<std::size_t>(width) * height * channels;
    std::vector<T> data(size);
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            T value = static_cast<T>((x + y) % 256);
            for (uint16_t c = 0; c < channels; ++c) {
                data[(y * width + x) * channels + c] = value;
            }
        }
    }
    
    return data;
}

// ============================================================================
// ChunkLayout Tests
// ============================================================================

TEST(ChunkLayout, CreateTiled) {
    auto result = ChunkLayout::create_tiled(
        512, 512, 1,    // image dimensions
        128, 128, 1,    // tile dimensions
        3,              // samples_per_pixel
        PlanarConfiguration::Chunky
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& layout = result.value();
    EXPECT_EQ(layout.chunks_across, 4);  // 512 / 128
    EXPECT_EQ(layout.chunks_down, 4);
    EXPECT_EQ(layout.chunks_deep, 1);
    EXPECT_EQ(layout.num_planes, 1);  // Chunky
    EXPECT_EQ(layout.chunks.size(), 16);  // 4x4
}

TEST(ChunkLayout, CreateTiledPlanar) {
    auto result = ChunkLayout::create_tiled(
        512, 512, 1,
        128, 128, 1,
        3,
        PlanarConfiguration::Planar
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& layout = result.value();
    EXPECT_EQ(layout.num_planes, 3);  // Planar
    EXPECT_EQ(layout.chunks.size(), 16 * 3);  // 4x4 tiles × 3 planes
}

TEST(ChunkLayout, CreateStripped) {
    auto result = ChunkLayout::create_stripped(
        512, 512,   // image dimensions
        16,         // rows_per_strip
        1,          // samples_per_pixel
        PlanarConfiguration::Chunky
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& layout = result.value();
    EXPECT_EQ(layout.chunks_across, 1);
    EXPECT_EQ(layout.chunks_down, 32);  // 512 / 16
    EXPECT_EQ(layout.chunks.size(), 32);
}

TEST(ChunkLayout, EdgeTiles) {
    auto result = ChunkLayout::create_tiled(
        500, 500, 1,    // Not evenly divisible
        128, 128, 1,
        1,
        PlanarConfiguration::Chunky
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& layout = result.value();
    EXPECT_EQ(layout.chunks_across, 4);  // Ceil(500/128)
    EXPECT_EQ(layout.chunks_down, 4);
    
    // Check edge tiles have correct dimensions
    const auto& last_tile_x = layout.chunks[3];  // Last tile in first row
    EXPECT_EQ(last_tile_x.width, 500 - 3 * 128);  // 116
    EXPECT_EQ(last_tile_x.height, 128);
}

// ============================================================================
// ImageWriter Basic Tests
// ============================================================================

TEST(ImageWriter, WriteSmallImageNoCompression) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForWritingConfig<BufferWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    
    auto image = generate_test_image<uint8_t>(64, 64, 1);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const uint8_t>(image),
        64, 64, 1,      // dimensions
        64, 64, 1,      // tile size (single tile)
        1,              // samples_per_pixel
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::None,
        0               // data_start_offset
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.tile_offsets.size(), 1);
    EXPECT_EQ(info.tile_byte_counts.size(), 1);
    EXPECT_EQ(info.tile_byte_counts[0], 64 * 64);
    EXPECT_EQ(info.total_data_size, 64 * 64);
}

TEST(ImageWriter, WriteTiledImage) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForReadingConfig<BufferWriter>;
    
    ImageWriter<uint16_t, CompSpec, Config> writer;
    
    auto image = generate_test_image<uint16_t>(256, 256, 3);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const uint16_t>(image),
        256, 256, 1,
        64, 64, 1,      // 64x64 tiles -> 16 tiles
        3,
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::None,
        0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.tile_offsets.size(), 16);  // 4x4 grid
    EXPECT_EQ(info.tile_byte_counts.size(), 16);
    
    // All tiles should be same size
    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(info.tile_byte_counts[i], 64 * 64 * 3 * sizeof(uint16_t));
    }
}

TEST(ImageWriter, WriteStrippedImage) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForWritingConfig<BufferWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    
    auto image = generate_test_image<uint8_t>(512, 512, 1);
    BufferWriter file_writer;
    
    auto result = writer.write_stripped_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const uint8_t>(image),
        512, 512,
        16,             // rows_per_strip
        1,
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::None,
        0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.tile_offsets.size(), 32);  // 512 / 16
}

// ============================================================================
// Compression Tests
// ============================================================================

TEST(ImageWriter, WriteWithZSTD) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc,
        ZstdCompressorDesc
    >;
    using Config = OptimizedForReadingConfig<BufferWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    
    auto image = generate_gradient<uint8_t>(256, 256, 3);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const uint8_t>(image),
        256, 256, 1,
        64, 64, 1,
        3,
        PlanarConfiguration::Chunky,
        CompressionScheme::ZSTD,
        Predictor::None,
        0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    
    // Should compress gradient data
    std::size_t uncompressed_total = 16 * 64 * 64 * 3;
    EXPECT_LT(info.total_data_size, uncompressed_total);
}

TEST(ImageWriter, WriteWithPredictor) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForWritingConfig<BufferWriter>;
    
    ImageWriter<uint16_t, CompSpec, Config> writer;
    
    auto image = generate_gradient<uint16_t>(128, 128, 1);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const uint16_t>(image),
        128, 128, 1,
        64, 64, 1,
        1,
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::Horizontal,
        0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    // Data should be written (predictor doesn't change size without compression)
    const auto& info = result.value();
    EXPECT_EQ(info.total_data_size, 128 * 128 * sizeof(uint16_t));
}

TEST(ImageWriter, WriteWithZSTDAndPredictor) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc,
        ZstdCompressorDesc
    >;
    using Config = StreamingConfig<BufferWriter>;
    
    ImageWriter<uint16_t, CompSpec, Config> writer;
    
    auto image = generate_gradient<uint16_t>(256, 256, 3);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const uint16_t>(image),
        256, 256, 1,
        128, 128, 1,
        3,
        PlanarConfiguration::Chunky,
        CompressionScheme::ZSTD,
        Predictor::Horizontal,
        0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    // Predictor + compression should give good ratio on gradient
    const auto& info = result.value();
    std::size_t uncompressed = 256 * 256 * 3 * sizeof(uint16_t);
    EXPECT_LT(info.total_data_size, uncompressed / 2);  // At least 2x compression
}

// ============================================================================
// Planar Configuration Tests
// ============================================================================

TEST(ImageWriter, WritePlanarImage) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForWritingConfig<BufferWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    
    auto image = generate_test_image<uint8_t>(128, 128, 3);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const uint8_t>(image),
        128, 128, 1,
        64, 64, 1,
        3,
        PlanarConfiguration::Planar,
        CompressionScheme::None,
        Predictor::None,
        0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.tile_offsets.size(), 12);  // 4 tiles × 3 planes
}

// ============================================================================
// Different Pixel Types
// ============================================================================

TEST(ImageWriter, WriteFloat32) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForWritingConfig<BufferWriter>;
    
    ImageWriter<float, CompSpec, Config> writer;
    
    auto image = generate_test_image<float>(64, 64, 4);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const float>(image),
        64, 64, 1,
        64, 64, 1,
        4,
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::None,
        0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.total_data_size, 64 * 64 * 4 * sizeof(float));
}

// ============================================================================
// Different Write Strategies
// ============================================================================

TEST(ImageWriter, OptimizedForReading) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForReadingConfig<BufferWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    
    auto image = generate_test_image<uint8_t>(256, 256, 1);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const uint8_t>(image),
        256, 256, 1,
        64, 64, 1,
        1,
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::None,
        0
    );
    
    ASSERT_TRUE(result.is_ok());
}

TEST(ImageWriter, StreamingMode) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = StreamingConfig<BufferWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    
    auto image = generate_test_image<uint8_t>(256, 256, 1);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer,
        std::span<const uint8_t>(image),
        256, 256, 1,
        64, 64, 1,
        1,
        PlanarConfiguration::Chunky,
        CompressionScheme::None,
        Predictor::None,
        0
    );
    
    ASSERT_TRUE(result.is_ok());
}

// ============================================================================
// Clear and Reuse Tests
// ============================================================================

TEST(ImageWriter, ClearBuffers) {
    using CompSpec = CompressorSpec<
        ZstdCompressorDesc
    >;
    using Config = OptimizedForWritingConfig<BufferWriter>;
    
    ImageWriter<uint16_t, CompSpec, Config> writer;
    
    // Write first image
    auto image1 = generate_test_image<uint16_t>(128, 128, 3);
    BufferWriter writer1;
    
    auto result1 = writer.write_image<OutputSpec::DHWC>(
        writer1, std::span<const uint16_t>(image1),
        128, 128, 1, 64, 64, 1, 3,
        PlanarConfiguration::Chunky,
        CompressionScheme::ZSTD, Predictor::None, 0
    );
    ASSERT_TRUE(result1.is_ok());
    
    // Clear
    writer.clear();
    
    // Write second image (should work fine)
    auto image2 = generate_test_image<uint16_t>(256, 256, 1);
    BufferWriter writer2;
    
    auto result2 = writer.write_image<OutputSpec::DHWC>(
        writer2, std::span<const uint16_t>(image2),
        256, 256, 1, 128, 128, 1, 1,
        PlanarConfiguration::Chunky,
        CompressionScheme::ZSTD, Predictor::None, 0
    );
    ASSERT_TRUE(result2.is_ok());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(ImageWriter, SinglePixelImage) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForWritingConfig<BufferWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    
    std::vector<uint8_t> single = {42};
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer, std::span<const uint8_t>(single),
        1, 1, 1, 1, 1, 1, 1,
        PlanarConfiguration::Chunky,
        CompressionScheme::None, Predictor::None, 0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.tile_offsets.size(), 1);
    EXPECT_EQ(info.tile_byte_counts[0], 1);
}

TEST(ImageWriter, NonZeroDataOffset) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForWritingConfig<BufferWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    
    auto image = generate_test_image<uint8_t>(64, 64, 1);
    BufferWriter file_writer;
    
    auto result = writer.write_image<OutputSpec::DHWC>(
        file_writer, std::span<const uint8_t>(image),
        64, 64, 1, 64, 64, 1, 1,
        PlanarConfiguration::Chunky,
        CompressionScheme::None, Predictor::None,
        1000  // Start at offset 1000
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.image_data_start_offset, 1000);
    EXPECT_EQ(info.tile_offsets[0], 1000);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
