#include <gtest/gtest.h>
#include <vector>
#include <random>
#include <cstring>
#include <cmath>

#include "../tiffconcept/include/tiffconcept/image_writer.hpp"
#include "../tiffconcept/include/tiffconcept/image_reader.hpp"
#include "../tiffconcept/include/tiffconcept/encoder.hpp"
#include "../tiffconcept/include/tiffconcept/decoder.hpp"
#include "../tiffconcept/include/tiffconcept/compressors/compressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/compressors/compressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/strategy/write_strategy.hpp"
#include "../tiffconcept/include/tiffconcept/strategy/read_strategy.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_buffer.hpp"
#include "../tiffconcept/include/tiffconcept/tiling.hpp"

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
// Basic Encoder/Decoder Round-trip Tests  
// ============================================================================

TEST(EncoderDecoderRoundtrip, Simple2D_Uint8_NoCompression) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using DecompSpec = DecompressorSpec<
        NoneDecompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    ChunkDecoder<uint8_t, DecompSpec> decoder;
    
    const uint32_t width = 128;
    const uint32_t height = 128;
    const uint16_t channels = 1;
    auto original = generate_test_image<uint8_t>(width, height, 1, channels);
    
    // Encode
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, width, height, 0,
        CompressionScheme::None, Predictor::None, channels
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    const auto& encoded = encode_result.value();
    
    // Decode
    auto decode_result = decoder.decode(
        encoded.data, width, height,
        CompressionScheme::None, Predictor::None, channels
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    auto decoded = decode_result.value();
    
    // Verify round-trip
    ASSERT_EQ(decoded.size(), original.size());
    EXPECT_TRUE(compare_images<uint8_t>(original, decoded));
}

TEST(EncoderDecoderRoundtrip, RGB_Uint8_ZSTD_HorizontalPredictor) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc,
        ZstdCompressorDesc
    >;
    using DecompSpec = DecompressorSpec<
        NoneDecompressorDesc,
        ZstdDecompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    ChunkDecoder<uint8_t, DecompSpec> decoder;
    
    const uint32_t width = 200;
    const uint32_t height = 200;
    const uint16_t channels = 3;
    auto original = generate_test_image<uint8_t>(width, height, 1, channels);
    
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, width, height, 0,
        CompressionScheme::ZSTD, Predictor::Horizontal, channels
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    const auto& encoded = encode_result.value();
    //EXPECT_LT(encoded.info.compressed_size, encoded.info.uncompressed_size); -> no because of random data
    
    auto decode_result = decoder.decode(
        encoded.data, width, height,
        CompressionScheme::ZSTD, Predictor::Horizontal, channels
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    auto decoded = decode_result.value();
    EXPECT_TRUE(compare_images<uint8_t>(original, decoded));
}

TEST(EncoderDecoderRoundtrip, Uint16_ZSTD) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc,
        ZstdCompressorDesc
    >;
    using DecompSpec = DecompressorSpec<
        NoneDecompressorDesc,
        ZstdDecompressorDesc
    >;
    
    ChunkEncoder<uint16_t, CompSpec> encoder;
    ChunkDecoder<uint16_t, DecompSpec> decoder;
    
    const uint32_t width = 256;
    const uint32_t height = 256;
    const uint16_t channels = 1;
    auto original = generate_test_image<uint16_t>(width, height, 1, channels);
    
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, width, height, 0,
        CompressionScheme::ZSTD, Predictor::None, channels
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    auto decode_result = decoder.decode(
        encode_result.value().data, width, height,
        CompressionScheme::ZSTD, Predictor::None, channels
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    EXPECT_TRUE(compare_images<uint16_t>(original, decode_result.value()));
}

TEST(EncoderDecoderRoundtrip, Float_FloatingPointPredictor) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc,
        ZstdCompressorDesc
    >;
    using DecompSpec = DecompressorSpec<
        NoneDecompressorDesc,
        ZstdDecompressorDesc
    >;
    
    ChunkEncoder<float, CompSpec> encoder;
    ChunkDecoder<float, DecompSpec> decoder;
    
    const uint32_t width = 128;
    const uint32_t height = 128;
    const uint16_t channels = 1;
    auto original = generate_test_image<float>(width, height, 1, channels, 123);
    
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, width, height, 0,
        CompressionScheme::ZSTD, Predictor::FloatingPoint, channels
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    auto decode_result = decoder.decode(
        encode_result.value().data, width, height,
        CompressionScheme::ZSTD, Predictor::FloatingPoint, channels
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    EXPECT_TRUE(compare_images<float>(original, decode_result.value(), 1e-6f));
}

TEST(EncoderDecoderRoundtrip, Volume3D_Uint8) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using DecompSpec = DecompressorSpec<
        NoneDecompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    ChunkDecoder<uint8_t, DecompSpec> decoder;
    
    const uint32_t width = 64;
    const uint32_t height = 64;
    const uint32_t depth = 8;
    const uint16_t channels = 1;
    auto original = generate_test_image<uint8_t>(width, height, depth, channels);
    
    auto encode_result = encoder.encode(
        original, 0, 0, 0, 0, width, height, depth, 0,
        CompressionScheme::None, Predictor::None, channels
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    auto decode_result = decoder.decode(
        encode_result.value().data, width, height * depth,
        CompressionScheme::None, Predictor::None, channels
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    EXPECT_TRUE(compare_images<uint8_t>(original, decode_result.value()));
}

TEST(EncoderDecoderRoundtrip, Volume3D_Multichannel_ZSTD) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc,
        ZstdCompressorDesc
    >;
    using DecompSpec = DecompressorSpec<
        NoneDecompressorDesc,
        ZstdDecompressorDesc
    >;
    
    ChunkEncoder<uint16_t, CompSpec> encoder;
    ChunkDecoder<uint16_t, DecompSpec> decoder;
    
    const uint32_t width = 96;
    const uint32_t height = 96;
    const uint32_t depth = 12;
    const uint16_t channels = 3;
    auto original = generate_test_image<uint16_t>(width, height, depth, channels);
    
    auto encode_result = encoder.encode(
        original, 0, 0, 0, 0, width, height, depth, 0,
        CompressionScheme::ZSTD, Predictor::Horizontal, channels
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    auto decode_result = decoder.decode(
        encode_result.value().data, width, height * depth,
        CompressionScheme::ZSTD, Predictor::Horizontal, channels
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    EXPECT_TRUE(compare_images<uint16_t>(original, decode_result.value()));
}

// ============================================================================
// ImageWriter Tests (verifies writing infrastructure)
// ============================================================================

TEST(ImageWriter, WritesTilesCorrectly) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForReadingConfig<BufferReadWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    BufferReadWriter file_writer;
    
    const uint32_t width = 128;
    const uint32_t height = 128;
    const uint16_t channels = 1;
    auto image = generate_test_image<uint8_t>(width, height, 1, channels);
    
    auto result = writer.write_image<ImageLayoutSpec::DHWC>(
        file_writer, std::span<const uint8_t>(image),
        width, height, 1, 64, 64, 1, channels,
        PlanarConfiguration::Chunky,
        CompressionScheme::None, Predictor::None, 0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.tile_offsets.size(), 4);  // 2x2 grid
    EXPECT_EQ(info.tile_byte_counts.size(), 4);
    
    // Verify all tiles were written
    for (const auto& byte_count : info.tile_byte_counts) {
        EXPECT_GT(byte_count, 0);
    }
}

TEST(ImageWriter, WritesStripsCorrectly) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForReadingConfig<BufferReadWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    BufferReadWriter file_writer;
    
    const uint32_t width = 200;
    const uint32_t height = 150;
    const uint16_t channels = 3;
    auto image = generate_test_image<uint8_t>(width, height, 1, channels);
    
    auto result = writer.write_stripped_image<ImageLayoutSpec::DHWC>(
        file_writer, std::span<const uint8_t>(image),
        width, height, 50, channels,
        PlanarConfiguration::Chunky,
        CompressionScheme::None, Predictor::None, 0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.tile_offsets.size(), 3);  // 150 / 50 = 3 strips
    EXPECT_EQ(info.tile_byte_counts.size(), 3);
}

TEST(ImageWriter, DifferentStrategiesProduceSameData) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    const uint32_t width = 64;
    const uint32_t height = 64;
    const uint16_t channels = 1;
    auto original = generate_test_image<uint8_t>(width, height, 1, channels);
    
    std::vector<BufferReadWriter> outputs;
    
    // OptimizedForReading
    {
        using Config = OptimizedForReadingConfig<BufferReadWriter>;
        ImageWriter<uint8_t, CompSpec, Config> writer;
        BufferReadWriter file_writer;
        
        auto result = writer.write_image<ImageLayoutSpec::DHWC>(
            file_writer, std::span<const uint8_t>(original),
            width, height, 1, 64, 64, 1, channels,
            PlanarConfiguration::Chunky,
            CompressionScheme::None, Predictor::None, 0
        );
        ASSERT_TRUE(result.is_ok());
        outputs.push_back(std::move(file_writer));
    }
    
    // OptimizedForWriting
    {
        using Config = OptimizedForWritingConfig<BufferReadWriter>;
        ImageWriter<uint8_t, CompSpec, Config> writer;
        BufferReadWriter file_writer;
        
        auto result = writer.write_image<ImageLayoutSpec::DHWC>(
            file_writer, std::span<const uint8_t>(original),
            width, height, 1, 64, 64, 1, channels,
            PlanarConfiguration::Chunky,
            CompressionScheme::None, Predictor::None, 0
        );
        ASSERT_TRUE(result.is_ok());
        outputs.push_back(std::move(file_writer));
    }
    
    // StreamingConfig
    {
        using Config = StreamingConfig<BufferReadWriter>;
        ImageWriter<uint8_t, CompSpec, Config> writer;
        BufferReadWriter file_writer;
        
        auto result = writer.write_image<ImageLayoutSpec::DHWC>(
            file_writer, std::span<const uint8_t>(original),
            width, height, 1, 64, 64, 1, channels,
            PlanarConfiguration::Chunky,
            CompressionScheme::None, Predictor::None, 0
        );
        ASSERT_TRUE(result.is_ok());
        outputs.push_back(std::move(file_writer));
    }
    
    // All strategies should produce buffers with the same content
    ASSERT_GT(outputs.size(), 1);
    const auto& first = outputs[0].buffer();
    for (std::size_t i = 1; i < outputs.size(); ++i) {
        const auto& current = outputs[i].buffer();
        EXPECT_EQ(first.size(), current.size());
        if (first.size() == current.size()) {
            EXPECT_EQ(std::memcmp(first.data(), current.data(), first.size()), 0);
        }
    }
}

TEST(ImageWriter, HandlesLargeImageWithManyTiles) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using Config = OptimizedForReadingConfig<BufferReadWriter>;
    
    ImageWriter<uint8_t, CompSpec, Config> writer;
    BufferReadWriter file_writer;
    
    // 512x512 with 16x16 tiles = 1024 tiles
    const uint32_t width = 512;
    const uint32_t height = 512;
    auto image = generate_test_image<uint8_t>(width, height, 1, 1);
    
    auto result = writer.write_image<ImageLayoutSpec::DHWC>(
        file_writer, std::span<const uint8_t>(image),
        width, height, 1, 16, 16, 1, 1,
        PlanarConfiguration::Chunky,
        CompressionScheme::None, Predictor::None, 0
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& info = result.value();
    EXPECT_EQ(info.tile_offsets.size(), 1024);
    EXPECT_EQ(info.tile_byte_counts.size(), 1024);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
