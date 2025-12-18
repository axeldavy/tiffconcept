#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <vector>
#include <random>
#include <algorithm>
#include <span>

#include "../tiffconcept/include/tiffconcept/compressor_base.hpp"
#include "../tiffconcept/include/tiffconcept/compressors/compressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/compressors/compressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/decoder.hpp"
#include "../tiffconcept/include/tiffconcept/decompressor_base.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/encoder.hpp"
#include "../tiffconcept/include/tiffconcept/result.hpp"
#include "../tiffconcept/include/tiffconcept/types.hpp"

using namespace tiffconcept;

// ============================================================================
// Helper Functions
// ============================================================================

/// Generate test image data
template <typename T>
std::vector<T> generate_test_image(uint32_t width, uint32_t height, uint16_t channels, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::size_t size = static_cast<std::size_t>(width) * height * channels;
    std::vector<T> data(size);
    
    if constexpr (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<T> dist(0.0, 1.0);
        for (auto& val : data) {
            val = dist(rng);
        }
    } else {
        std::uniform_int_distribution<T> dist(
            std::numeric_limits<T>::min(), 
            std::numeric_limits<T>::max()
        );
        for (auto& val : data) {
            val = dist(rng);
        }
    }
    
    return data;
}

/// Generate gradient image (compressible)
template <typename T>
std::vector<T> generate_gradient_image(uint32_t width, uint32_t height, uint16_t channels) {
    std::size_t size = static_cast<std::size_t>(width) * height * channels;
    std::vector<T> data(size);
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            T value;
            if constexpr (std::is_floating_point_v<T>) {
                value = static_cast<T>(x + y) / static_cast<T>(width + height);
            } else {
                value = static_cast<T>((x + y) % 256);
            }
            
            for (uint16_t c = 0; c < channels; ++c) {
                data[(y * width + x) * channels + c] = value;
            }
        }
    }
    
    return data;
}

// ============================================================================
// Basic Encoding Tests
// ============================================================================

TEST(ChunkEncoder, EncodeNoCompression) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    
    auto input = generate_test_image<uint8_t>(64, 64, 1);
    
    auto result = encoder.encode_2d(
        input,
        0,      // chunk_index
        0, 0,   // pixel position
        64, 64, // dimensions
        0,      // plane
        CompressionScheme::None,
        Predictor::None,
        1       // samples_per_pixel
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& chunk = result.value();
    EXPECT_EQ(chunk.info.chunk_index, 0);
    EXPECT_EQ(chunk.info.width, 64);
    EXPECT_EQ(chunk.info.height, 64);
    EXPECT_EQ(chunk.info.uncompressed_size, 64 * 64);
    EXPECT_EQ(chunk.info.compressed_size, 64 * 64);  // No compression
    EXPECT_EQ(chunk.data.size(), 64 * 64);
}

TEST(ChunkEncoder, EncodeWithZSTD) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc,
        ZstdCompressorDesc
    >;
    
    ChunkEncoder<uint16_t, CompSpec> encoder;
    
    // Generate compressible data
    auto input = generate_gradient_image<uint16_t>(128, 128, 1);
    
    auto result = encoder.encode_2d(
        input,
        0,
        0, 0,
        128, 128,
        0,
        CompressionScheme::ZSTD,
        Predictor::None,
        1
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& chunk = result.value();
    EXPECT_EQ(chunk.info.uncompressed_size, 128 * 128 * sizeof(uint16_t));
    EXPECT_LT(chunk.info.compressed_size, chunk.info.uncompressed_size);  // Should compress
    EXPECT_EQ(chunk.data.size(), chunk.info.compressed_size);
}

TEST(ChunkEncoder, EncodeWithPredictor) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    ChunkEncoder<uint16_t, CompSpec> encoder;
    
    auto input = generate_gradient_image<uint16_t>(64, 64, 3);
    
    auto result = encoder.encode_2d(
        input,
        0,
        0, 0,
        64, 64,
        0,
        CompressionScheme::None,
        Predictor::Horizontal,
        3  // RGB
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& chunk = result.value();
    EXPECT_EQ(chunk.info.uncompressed_size, 64 * 64 * 3 * sizeof(uint16_t));
    EXPECT_EQ(chunk.info.compressed_size, 64 * 64 * 3 * sizeof(uint16_t));
}

TEST(ChunkEncoder, EncodeFloatWithFloatingPointPredictor) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    ChunkEncoder<float, CompSpec> encoder;
    
    auto input = generate_gradient_image<float>(32, 32, 4);
    
    auto result = encoder.encode_2d(
        input,
        0,
        0, 0,
        32, 32,
        0,
        CompressionScheme::None,
        Predictor::FloatingPoint,
        4
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& chunk = result.value();
    EXPECT_EQ(chunk.info.uncompressed_size, 32 * 32 * 4 * sizeof(float));
}

// ============================================================================
// Round-trip Tests (Encode -> Decode)
// ============================================================================

TEST(EncoderDecoder, RoundTripNoCompressionNoPredictor) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using DecompSpec = DecompressorSpec<
        NoneDecompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    ChunkDecoder<uint8_t, DecompSpec> decoder;
    
    auto original = generate_test_image<uint8_t>(64, 64, 1, 123);
    
    // Encode
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, 64, 64, 0,
        CompressionScheme::None, Predictor::None, 1
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    const auto& encoded = encode_result.value();
    
    // Decode
    auto decode_result = decoder.decode(
        encoded.data, 64, 64,
        CompressionScheme::None, Predictor::None, 1
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    auto decoded = decode_result.value();
    
    // Compare
    ASSERT_EQ(decoded.size(), original.size());
    EXPECT_EQ(std::memcmp(decoded.data(), original.data(), original.size()), 0);
}

TEST(EncoderDecoder, RoundTripZSTDNoPredictor) {
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
    
    auto original = generate_gradient_image<uint16_t>(128, 128, 3);
    
    // Encode
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, 128, 128, 0,
        CompressionScheme::ZSTD, Predictor::None, 3
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    const auto& encoded = encode_result.value();
    
    // Decode
    auto decode_result = decoder.decode(
        encoded.data, 128, 128,
        CompressionScheme::ZSTD, Predictor::None, 3
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    auto decoded = decode_result.value();
    
    // Compare
    ASSERT_EQ(decoded.size(), original.size());
    EXPECT_EQ(std::memcmp(decoded.data(), original.data(), 
                         original.size() * sizeof(uint16_t)), 0);
}

TEST(EncoderDecoder, RoundTripNoCompressionHorizontalPredictor) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    using DecompSpec = DecompressorSpec<
        NoneDecompressorDesc
    >;
    
    ChunkEncoder<uint16_t, CompSpec> encoder;
    ChunkDecoder<uint16_t, DecompSpec> decoder;
    
    auto original = generate_test_image<uint16_t>(64, 64, 3, 456);
    
    // Encode
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, 64, 64, 0,
        CompressionScheme::None, Predictor::Horizontal, 3
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    const auto& encoded = encode_result.value();
    
    // Decode
    auto decode_result = decoder.decode(
        encoded.data, 64, 64,
        CompressionScheme::None, Predictor::Horizontal, 3
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    auto decoded = decode_result.value();
    
    // Compare
    ASSERT_EQ(decoded.size(), original.size());
    EXPECT_EQ(std::memcmp(decoded.data(), original.data(), 
                         original.size() * sizeof(uint16_t)), 0);
}

TEST(EncoderDecoder, RoundTripZSTDHorizontalPredictor) {
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
    
    auto original = generate_gradient_image<uint8_t>(256, 256, 1);
    
    // Encode
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, 256, 256, 0,
        CompressionScheme::ZSTD, Predictor::Horizontal, 1
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    const auto& encoded = encode_result.value();
    
    // Verify compression
    EXPECT_LT(encoded.info.compressed_size, encoded.info.uncompressed_size);
    
    // Decode
    auto decode_result = decoder.decode(
        encoded.data, 256, 256,
        CompressionScheme::ZSTD, Predictor::Horizontal, 1
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    auto decoded = decode_result.value();
    
    // Compare
    ASSERT_EQ(decoded.size(), original.size());
    EXPECT_EQ(std::memcmp(decoded.data(), original.data(), original.size()), 0);
}

TEST(EncoderDecoder, RoundTripFloatFloatingPointPredictor) {
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
    
    auto original = generate_gradient_image<float>(64, 64, 4);
    
    // Encode
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, 64, 64, 0,
        CompressionScheme::ZSTD, Predictor::FloatingPoint, 4
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    const auto& encoded = encode_result.value();
    
    // Decode
    auto decode_result = decoder.decode(
        encoded.data, 64, 64,
        CompressionScheme::ZSTD, Predictor::FloatingPoint, 4
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    auto decoded = decode_result.value();
    
    // Compare (floats should be exactly equal after round-trip)
    ASSERT_EQ(decoded.size(), original.size());
    EXPECT_EQ(std::memcmp(decoded.data(), original.data(), 
                         original.size() * sizeof(float)), 0);
}

TEST(EncoderDecoder, RoundTripPackBits) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc,
        PackBitsCompressorDesc
    >;
    using DecompSpec = DecompressorSpec<
        NoneDecompressorDesc,
        PackBitsDecompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    ChunkDecoder<uint8_t, DecompSpec> decoder;
    
    // Create data with runs (good for PackBits)
    std::vector<uint8_t> original(128 * 128);
    for (std::size_t i = 0; i < original.size(); ++i) {
        original[i] = static_cast<uint8_t>(i / 64);  // Creates runs
    }
    
    // Encode
    auto encode_result = encoder.encode_2d(
        original, 0, 0, 0, 128, 128, 0,
        CompressionScheme::PackBits, Predictor::None, 1
    );
    ASSERT_TRUE(encode_result.is_ok());
    
    const auto& encoded = encode_result.value();
    
    // Should compress
    EXPECT_LT(encoded.info.compressed_size, encoded.info.uncompressed_size);
    
    // Decode
    auto decode_result = decoder.decode(
        encoded.data, 128, 128,
        CompressionScheme::PackBits, Predictor::None, 1
    );
    ASSERT_TRUE(decode_result.is_ok());
    
    auto decoded = decode_result.value();
    
    // Compare
    ASSERT_EQ(decoded.size(), original.size());
    EXPECT_EQ(std::memcmp(decoded.data(), original.data(), original.size()), 0);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(ChunkEncoder, EmptyChunk) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    
    std::vector<uint8_t> empty;
    
    auto result = encoder.encode_2d(
        empty, 0, 0, 0, 0, 0, 0,
        CompressionScheme::None, Predictor::None, 1
    );
    
    // Should fail with OutOfBounds
    EXPECT_FALSE(result.is_ok());
}

TEST(ChunkEncoder, SinglePixel) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    ChunkEncoder<uint16_t, CompSpec> encoder;
    
    std::vector<uint16_t> single = {42};
    
    auto result = encoder.encode_2d(
        single, 0, 0, 0, 1, 1, 0,
        CompressionScheme::None, Predictor::None, 1
    );
    
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().info.compressed_size, sizeof(uint16_t));
}

TEST(ChunkEncoder, MultiChannelSinglePixel) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    
    std::vector<uint8_t> rgb = {255, 128, 64};
    
    auto result = encoder.encode_2d(
        rgb, 0, 0, 0, 1, 1, 0,
        CompressionScheme::None, Predictor::None, 3
    );
    
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().info.compressed_size, 3);
}

// ============================================================================
// Buffer Reuse Tests
// ============================================================================

TEST(ChunkEncoder, BufferReuse) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    
    // Encode multiple chunks
    for (int i = 0; i < 10; ++i) {
        auto input = generate_test_image<uint8_t>(64, 64, 1, i);
        
        auto result = encoder.encode_2d(
            input, i, 0, 0, 64, 64, 0,
            CompressionScheme::None, Predictor::Horizontal, 1
        );
        
        ASSERT_TRUE(result.is_ok());
    }
    
    // Check buffer sizes (should not grow indefinitely)
    auto [predictor_size, compressed_size] = encoder.buffer_sizes();
    
    // Buffers should be sized for 64x64 chunk
    EXPECT_GE(predictor_size, 64 * 64);
    EXPECT_LE(predictor_size, 64 * 64 * 2);  // Some growth is OK
}

TEST(ChunkEncoder, ClearBuffers) {
    using CompSpec = CompressorSpec<
        ZstdCompressorDesc
    >;
    
    ChunkEncoder<uint16_t, CompSpec> encoder;
    
    // Encode something
    auto input = generate_test_image<uint16_t>(128, 128, 3);
    auto result = encoder.encode_2d(
        input, 0, 0, 0, 128, 128, 0,
        CompressionScheme::ZSTD, Predictor::Horizontal, 3
    );
    ASSERT_TRUE(result.is_ok());
    
    // Check buffers are allocated
    auto [before_pred, before_comp] = encoder.buffer_sizes();
    EXPECT_GT(before_pred + before_comp, 0);
    
    // Clear
    encoder.clear();
    
    // Buffers should be freed
    auto [after_pred, after_comp] = encoder.buffer_sizes();
    EXPECT_EQ(after_pred, 0);
    EXPECT_EQ(after_comp, 0);
}

// ============================================================================
// Multiple Pixel Types
// ============================================================================

TEST(ChunkEncoder, DifferentPixelTypes) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    // uint8_t
    {
        ChunkEncoder<uint8_t, CompSpec> encoder;
        auto input = generate_test_image<uint8_t>(32, 32, 1);
        auto result = encoder.encode_2d(input, 0, 0, 0, 32, 32, 0,
                                       CompressionScheme::None, Predictor::None, 1);
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().info.uncompressed_size, 32 * 32);
    }
    
    // uint16_t
    {
        ChunkEncoder<uint16_t, CompSpec> encoder;
        auto input = generate_test_image<uint16_t>(32, 32, 1);
        auto result = encoder.encode_2d(input, 0, 0, 0, 32, 32, 0,
                                       CompressionScheme::None, Predictor::None, 1);
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().info.uncompressed_size, 32 * 32 * 2);
    }
    
    // uint32_t
    {
        ChunkEncoder<uint32_t, CompSpec> encoder;
        auto input = generate_test_image<uint32_t>(32, 32, 1);
        auto result = encoder.encode_2d(input, 0, 0, 0, 32, 32, 0,
                                       CompressionScheme::None, Predictor::None, 1);
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().info.uncompressed_size, 32 * 32 * 4);
    }
    
    // float
    {
        ChunkEncoder<float, CompSpec> encoder;
        auto input = generate_test_image<float>(32, 32, 1);
        auto result = encoder.encode_2d(input, 0, 0, 0, 32, 32, 0,
                                       CompressionScheme::None, Predictor::None, 1);
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().info.uncompressed_size, 32 * 32 * 4);
    }
}

// ============================================================================
// Tile Index Tests
// ============================================================================

TEST(ChunkEncoder, EncodeTileIndices) {
    using CompSpec = CompressorSpec<
        NoneCompressorDesc
    >;
    
    ChunkEncoder<uint8_t, CompSpec> encoder;
    
    auto input = generate_test_image<uint8_t>(64, 64, 1);
    
    auto result = encoder.encode_with_tile_indices(
        input,
        5,          // chunk_index
        2, 3, 0,    // tile indices (x, y, z)
        64, 64, 1,  // tile dimensions
        0,          // plane
        CompressionScheme::None,
        Predictor::None,
        1
    );
    
    ASSERT_TRUE(result.is_ok());
    
    const auto& chunk = result.value();
    EXPECT_EQ(chunk.info.chunk_index, 5);
    EXPECT_EQ(chunk.info.pixel_x, 2 * 64);  // tile_x_index * tile_width
    EXPECT_EQ(chunk.info.pixel_y, 3 * 64);
    EXPECT_EQ(chunk.info.pixel_z, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
