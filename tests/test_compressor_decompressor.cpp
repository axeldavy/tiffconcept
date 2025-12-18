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
#include "../tiffconcept/include/tiffconcept/decompressor_base.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/result.hpp"
#include "../tiffconcept/include/tiffconcept/types.hpp"

using namespace tiffconcept;

// ============================================================================
// Helper Functions
// ============================================================================

/// Generate random byte data
std::vector<std::byte> generate_random_bytes(std::size_t count, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<std::byte> data(count);
    for (auto& byte : data) {
        byte = static_cast<std::byte>(dist(rng));
    }
    return data;
}

/// Generate data with runs (good for PackBits)
std::vector<std::byte> generate_data_with_runs(std::size_t count, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> value_dist(0, 255);
    std::uniform_int_distribution<std::size_t> run_dist(2, 20);
    
    std::vector<std::byte> data;
    data.reserve(count);
    
    while (data.size() < count) {
        std::size_t run_length = std::min(run_dist(rng), count - data.size());
        std::byte value = static_cast<std::byte>(value_dist(rng));
        for (std::size_t i = 0; i < run_length; ++i) {
            data.push_back(value);
        }
    }
    
    return data;
}

/// Generate compressible data (patterns)
std::vector<std::byte> generate_compressible_data(std::size_t count) {
    std::vector<std::byte> data;
    data.reserve(count);
    
    const std::array<std::byte, 16> pattern = {
        std::byte{0x00}, std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
        std::byte{0x44}, std::byte{0x55}, std::byte{0x66}, std::byte{0x77},
        std::byte{0x88}, std::byte{0x99}, std::byte{0xAA}, std::byte{0xBB},
        std::byte{0xCC}, std::byte{0xDD}, std::byte{0xEE}, std::byte{0xFF}
    };
    
    for (std::size_t i = 0; i < count; ++i) {
        data.push_back(pattern[i % pattern.size()]);
    }
    
    return data;
}

/// Compare byte vectors
bool vectors_equal(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
    if (a.size() != b.size()) return false;
    return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

/// Compare spans with vectors
bool span_equals_vector(std::span<const std::byte> span, const std::vector<std::byte>& vec, std::size_t size) {
    if (size != vec.size()) return false;
    return std::memcmp(span.data(), vec.data(), size) == 0;
}

// ============================================================================
// None Compressor/Decompressor Tests
// ============================================================================

TEST(NoneCompressorDecompressor, EmptyData) {
    NoneCompressor compressor;
    NoneDecompressor decompressor;
    
    std::vector<std::byte> input;
    std::vector<std::byte> compressed;
    
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    EXPECT_EQ(compress_result.value(), 0);
    
    std::vector<std::byte> decompressed(0);
    auto decompress_result = decompressor.decompress(decompressed, compressed);
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), 0);
}

TEST(NoneCompressorDecompressor, SmallData) {
    NoneCompressor compressor;
    NoneDecompressor decompressor;
    
    std::vector<std::byte> input = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}
    };
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    EXPECT_EQ(compress_result.value(), 4);
    EXPECT_TRUE(vectors_equal(compressed, input));
    
    std::vector<std::byte> decompressed(4);
    auto decompress_result = decompressor.decompress(decompressed, compressed);
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), 4);
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(NoneCompressorDecompressor, LargeRandomData) {
    NoneCompressor compressor;
    NoneDecompressor decompressor;
    
    auto input = generate_random_bytes(10000);
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    EXPECT_EQ(compress_result.value(), input.size());
    EXPECT_TRUE(vectors_equal(compressed, input));
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor.decompress(decompressed, compressed);
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), input.size());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(NoneCompressorDecompressor, WithOffset) {
    NoneCompressor compressor;
    NoneDecompressor decompressor;
    
    std::vector<std::byte> input = {
        std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}
    };
    
    std::vector<std::byte> compressed;
    compressed.resize(10, std::byte{0x00});
    
    auto compress_result = compressor.compress(compressed, 5, input);
    ASSERT_TRUE(compress_result.is_ok());
    EXPECT_EQ(compress_result.value(), 3);
    
    // Check that data is at offset 5
    EXPECT_EQ(compressed[5], std::byte{0xAA});
    EXPECT_EQ(compressed[6], std::byte{0xBB});
    EXPECT_EQ(compressed[7], std::byte{0xCC});
    
    std::vector<std::byte> decompressed(3);
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data() + 5, 3)
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(NoneDecompressor, OutputBufferTooSmall) {
    NoneDecompressor decompressor;
    
    std::vector<std::byte> input = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}
    };
    
    std::vector<std::byte> output(2);  // Too small
    auto result = decompressor.decompress(output, input);
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.error().code, Error::Code::InvalidFormat);
}

// ============================================================================
// PackBits Compressor/Decompressor Tests
// ============================================================================

TEST(PackBitsCompressorDecompressor, EmptyData) {
    PackBitsCompressor compressor;
    
    std::vector<std::byte> input;
    std::vector<std::byte> compressed;
    
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    EXPECT_EQ(compress_result.value(), 0);
}

TEST(PackBitsCompressorDecompressor, SingleByte) {
    PackBitsCompressor compressor;
    PackBitsDecompressor decompressor;
    
    std::vector<std::byte> input = {std::byte{0x42}};
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    std::vector<std::byte> decompressed(1);
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(PackBitsCompressorDecompressor, ReplicatedRun) {
    PackBitsCompressor compressor;
    PackBitsDecompressor decompressor;
    
    // Five identical bytes
    std::vector<std::byte> input = {
        std::byte{0xAA}, std::byte{0xAA}, std::byte{0xAA}, 
        std::byte{0xAA}, std::byte{0xAA}
    };
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    // Should be encoded as: -4 (= 1-5), 0xAA
    EXPECT_EQ(compress_result.value(), 2);
    EXPECT_EQ(static_cast<int8_t>(compressed[0]), -4);
    EXPECT_EQ(compressed[1], std::byte{0xAA});
    
    std::vector<std::byte> decompressed(5);
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), 5);
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(PackBitsCompressorDecompressor, LiteralRun) {
    PackBitsCompressor compressor;
    PackBitsDecompressor decompressor;
    
    // All different bytes
    std::vector<std::byte> input = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}
    };
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    // Should be encoded as: 2 (= 3-1), 0x01, 0x02, 0x03
    EXPECT_EQ(compress_result.value(), 4);
    EXPECT_EQ(static_cast<int8_t>(compressed[0]), 2);
    
    std::vector<std::byte> decompressed(3);
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), 3);
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(PackBitsCompressorDecompressor, MixedRuns) {
    PackBitsCompressor compressor;
    PackBitsDecompressor decompressor;
    
    // Mix of replicated and literal runs
    std::vector<std::byte> input = {
        std::byte{0xAA}, std::byte{0xAA}, std::byte{0xAA},  // 3 identical
        std::byte{0x01}, std::byte{0x02},                    // 2 different
        std::byte{0xBB}, std::byte{0xBB}, std::byte{0xBB},  // 3 identical
        std::byte{0xBB}                                      // 1 more identical
    };
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), input.size());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(PackBitsCompressorDecompressor, LongReplicatedRun) {
    PackBitsCompressor compressor;
    PackBitsDecompressor decompressor;
    
    // 128 identical bytes (maximum for one run)
    std::vector<std::byte> input(128, std::byte{0xFF});
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    // Should be encoded as: -127 (= 1-128), 0xFF
    EXPECT_EQ(compress_result.value(), 2);
    EXPECT_EQ(static_cast<int8_t>(compressed[0]), -127);
    EXPECT_EQ(compressed[1], std::byte{0xFF});
    
    std::vector<std::byte> decompressed(128);
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), 128);
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(PackBitsCompressorDecompressor, DataWithRuns) {
    PackBitsCompressor compressor;
    PackBitsDecompressor decompressor;
    
    auto input = generate_data_with_runs(1000);
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    // Compression should reduce size for data with runs
    EXPECT_LT(compress_result.value(), input.size());
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), input.size());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(PackBitsCompressorDecompressor, RandomData) {
    PackBitsCompressor compressor;
    PackBitsDecompressor decompressor;
    
    auto input = generate_random_bytes(500);
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), input.size());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(PackBitsDecompressor, InvalidControlByte128) {
    PackBitsDecompressor decompressor;
    
    // Control byte -128 should be skipped (no operation)
    std::vector<std::byte> input = {
        std::byte{0x80},  // -128
        std::byte{0x00},  // 1 literal byte
        std::byte{0x42}
    };
    
    std::vector<std::byte> output(1);
    auto result = decompressor.decompress(output, input);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 1);
    EXPECT_EQ(output[0], std::byte{0x42});
}

TEST(PackBitsDecompressor, UnexpectedEndOfInput) {
    PackBitsDecompressor decompressor;
    
    // Control byte indicates 3 literal bytes but only 2 are present
    std::vector<std::byte> input = {
        std::byte{0x02},  // 3 literal bytes expected
        std::byte{0x01}, std::byte{0x02}  // Only 2 bytes
    };
    
    std::vector<std::byte> output(10);
    auto result = decompressor.decompress(output, input);
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.error().code, Error::Code::InvalidFormat);
}

TEST(PackBitsDecompressor, OutputBufferTooSmall) {
    PackBitsDecompressor decompressor;
    
    std::vector<std::byte> input = {
        std::byte{0xFE},  // -2 = replicate 3 times
        std::byte{0xFF}
    };
    
    std::vector<std::byte> output(2);  // Too small for 3 bytes
    auto result = decompressor.decompress(output, input);
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.error().code, Error::Code::InvalidFormat);
}

// ============================================================================
// ZSTD Compressor/Decompressor Tests
// ============================================================================

TEST(ZstdCompressorDecompressor, EmptyData) {
    ZstdCompressor compressor;
    ZstdDecompressor decompressor;
    
    std::vector<std::byte> input;
    std::vector<std::byte> compressed;
    
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    // ZSTD will produce a valid frame even for empty data
    EXPECT_GT(compress_result.value(), 0);
    
    std::vector<std::byte> decompressed(0);
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), 0);
}

TEST(ZstdCompressorDecompressor, SmallData) {
    ZstdCompressor compressor;
    ZstdDecompressor decompressor;
    
    std::vector<std::byte> input = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}
    };
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    EXPECT_GT(compress_result.value(), 0);
    
    std::vector<std::byte> decompressed(4);
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), 4);
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(ZstdCompressorDecompressor, CompressibleData) {
    ZstdCompressor compressor;
    ZstdDecompressor decompressor;
    
    auto input = generate_compressible_data(10000);
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    // Should achieve good compression
    EXPECT_LT(compress_result.value(), input.size() / 2);
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), input.size());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(ZstdCompressorDecompressor, RandomData) {
    ZstdCompressor compressor;
    ZstdDecompressor decompressor;
    
    auto input = generate_random_bytes(5000);
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), input.size());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(ZstdCompressorDecompressor, LargeData) {
    ZstdCompressor compressor;
    ZstdDecompressor decompressor;
    
    auto input = generate_compressible_data(100000);
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor.decompress(
        decompressed, 
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_EQ(decompress_result.value(), input.size());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(ZstdCompressor, DifferentCompressionLevels) {
    auto input = generate_compressible_data(10000);
    
    std::vector<std::byte> compressed_level1;
    std::vector<std::byte> compressed_level9;
    std::vector<std::byte> compressed_level15;
    
    {
        ZstdCompressor compressor(1);
        auto result1 = compressor.compress(compressed_level1, 0, input);
        ASSERT_TRUE(result1.is_ok());
    }
    
    {
        ZstdCompressor compressor(9);
        auto result9 = compressor.compress(compressed_level9, 0, input);
        ASSERT_TRUE(result9.is_ok());
    }
    
    ZstdCompressor compressor(15);
    auto result15 = compressor.compress(compressed_level15, 0, input);
    ASSERT_TRUE(result15.is_ok());
    
    // Higher compression levels should produce smaller output
    EXPECT_GE(compressed_level1.size(), compressed_level9.size());
    EXPECT_GE(compressed_level9.size(), compressed_level15.size());
    
    // Should decompress correctly
    ZstdDecompressor decompressor;
    std::vector<std::byte> decompressed(input.size());
    
    auto result = decompressor.decompress(decompressed, std::span<const std::byte>(compressed_level15.data(), result15.value()));
    if (!result.is_ok()) {
        std::cout << "Decompression error: " << result.error().message << std::endl;
        std::cout << "Compressed size: " << compressed_level15.size() << std::endl;
        std::cout << "Input size: " << input.size() << std::endl;
    }
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(ZstdCompressor, WithOffset) {
    ZstdCompressor compressor;
    
    std::vector<std::byte> input = generate_random_bytes(100);
    std::vector<std::byte> compressed;
    compressed.resize(50, std::byte{0xFF});  // Pre-fill with data
    
    auto compress_result = compressor.compress(compressed, 50, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    // First 50 bytes should be untouched
    for (std::size_t i = 0; i < 50; ++i) {
        EXPECT_EQ(compressed[i], std::byte{0xFF});
    }
    
    // Decompress from offset
    ZstdDecompressor decompressor;
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor.decompress(
        decompressed,
        std::span<const std::byte>(compressed.data() + 50, compress_result.value())
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(ZstdCompressor, GetCompressBound) {
    std::size_t input_size = 10000;
    std::size_t bound = ZstdCompressor::get_compress_bound(input_size);
    
    EXPECT_GT(bound, input_size);  // Bound should be larger than input
    
    // Verify that the bound is sufficient
    auto input = generate_random_bytes(input_size);
    ZstdCompressor compressor;
    
    std::vector<std::byte> compressed;
    auto result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(result.is_ok());
    EXPECT_LE(result.value(), bound);
}

TEST(ZstdDecompressor, GetDecompressedSize) {
    ZstdCompressor compressor;
    auto input = generate_random_bytes(1234);
    
    std::vector<std::byte> compressed;
    auto compress_result = compressor.compress(compressed, 0, input);
    ASSERT_TRUE(compress_result.is_ok());
    
    auto size_result = ZstdDecompressor::get_decompressed_size(
        std::span<const std::byte>(compressed.data(), compress_result.value())
    );
    ASSERT_TRUE(size_result.is_ok());
    EXPECT_EQ(size_result.value(), 1234);
}

TEST(ZstdDecompressor, InvalidFrame) {
    ZstdDecompressor decompressor;
    
    std::vector<std::byte> invalid_data = {
        std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}
    };
    
    std::vector<std::byte> output(100);
    auto result = decompressor.decompress(output, invalid_data);
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.error().code, Error::Code::InvalidFormat);
}

// ============================================================================
// Compressor/Decompressor Storage Tests
// ============================================================================

using StandardCompressorSpec = CompressorSpec<
    NoneCompressorDesc,
    PackBitsCompressorDesc
>;

using StandardDecompressorSpec = DecompressorSpec<
    NoneDecompressorDesc,
    PackBitsDecompressorDesc
>;

using FullCompressorSpec = CompressorSpec<
    NoneCompressorDesc,
    PackBitsCompressorDesc,
    ZstdCompressorDesc
>;

using FullDecompressorSpec = DecompressorSpec<
    NoneDecompressorDesc,
    PackBitsDecompressorDesc,
    ZstdDecompressorDesc
>;

TEST(CompressorStorage, StandardSpec) {
    CompressorStorage<StandardCompressorSpec> storage;
    
    EXPECT_TRUE(storage.supports(CompressionScheme::None));
    EXPECT_TRUE(storage.supports(CompressionScheme::PackBits));
    EXPECT_FALSE(storage.supports(CompressionScheme::ZSTD));
    
    EXPECT_TRUE(storage.supports<CompressionScheme::None>());
    EXPECT_TRUE(storage.supports<CompressionScheme::PackBits>());
    EXPECT_FALSE(storage.supports<CompressionScheme::ZSTD>());
}

TEST(DecompressorStorage, StandardSpec) {
    DecompressorStorage<StandardDecompressorSpec> storage;
    
    EXPECT_TRUE(storage.supports(CompressionScheme::None));
    EXPECT_TRUE(storage.supports(CompressionScheme::PackBits));
    EXPECT_FALSE(storage.supports(CompressionScheme::ZSTD));
}

TEST(CompressorDecompressorStorage, NoneScheme) {
    CompressorStorage<FullCompressorSpec> compressor_storage;
    DecompressorStorage<FullDecompressorSpec> decompressor_storage;
    
    auto input = generate_random_bytes(1000);
    std::vector<std::byte> compressed;
    
    auto compress_result = compressor_storage.compress(
        compressed, 0, input, CompressionScheme::None
    );
    ASSERT_TRUE(compress_result.is_ok());
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor_storage.decompress(
        decompressed,
        std::span<const std::byte>(compressed.data(), compress_result.value()),
        CompressionScheme::None
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(CompressorDecompressorStorage, PackBitsScheme) {
    CompressorStorage<FullCompressorSpec> compressor_storage;
    DecompressorStorage<FullDecompressorSpec> decompressor_storage;
    
    auto input = generate_data_with_runs(1000);
    std::vector<std::byte> compressed;
    
    auto compress_result = compressor_storage.compress(
        compressed, 0, input, CompressionScheme::PackBits
    );
    ASSERT_TRUE(compress_result.is_ok());
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor_storage.decompress(
        decompressed,
        std::span<const std::byte>(compressed.data(), compress_result.value()),
        CompressionScheme::PackBits
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(CompressorDecompressorStorage, ZstdScheme) {
    CompressorStorage<FullCompressorSpec> compressor_storage;
    DecompressorStorage<FullDecompressorSpec> decompressor_storage;
    
    auto input = generate_compressible_data(5000);
    std::vector<std::byte> compressed;
    
    auto compress_result = compressor_storage.compress(
        compressed, 0, input, CompressionScheme::ZSTD
    );
    ASSERT_TRUE(compress_result.is_ok());
    
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor_storage.decompress(
        decompressed,
        std::span<const std::byte>(compressed.data(), compress_result.value()),
        CompressionScheme::ZSTD
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(CompressorDecompressorStorage, ZstdAltScheme) {
    CompressorStorage<FullCompressorSpec> compressor_storage;
    DecompressorStorage<FullDecompressorSpec> decompressor_storage;
    
    auto input = generate_random_bytes(2000);
    std::vector<std::byte> compressed;
    
    // Compress with ZSTD_Alt scheme
    auto compress_result = compressor_storage.compress(
        compressed, 0, input, CompressionScheme::ZSTD_Alt
    );
    ASSERT_TRUE(compress_result.is_ok());
    
    // Decompress with ZSTD_Alt scheme
    std::vector<std::byte> decompressed(input.size());
    auto decompress_result = decompressor_storage.decompress(
        decompressed,
        std::span<const std::byte>(compressed.data(), compress_result.value()),
        CompressionScheme::ZSTD_Alt
    );
    ASSERT_TRUE(decompress_result.is_ok());
    EXPECT_TRUE(vectors_equal(decompressed, input));
}

TEST(CompressorStorage, UnsupportedScheme) {
    CompressorStorage<StandardCompressorSpec> storage;
    
    auto input = generate_random_bytes(100);
    std::vector<std::byte> compressed;
    
    auto result = storage.compress(
        compressed, 0, input, CompressionScheme::ZSTD
    );
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.error().code, Error::Code::UnsupportedFeature);
}

TEST(DecompressorStorage, UnsupportedScheme) {
    DecompressorStorage<StandardDecompressorSpec> storage;
    
    std::vector<std::byte> input = {std::byte{0x01}, std::byte{0x02}};
    std::vector<std::byte> output(10);
    
    auto result = storage.decompress(
        output, input, CompressionScheme::ZSTD
    );
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.error().code, Error::Code::UnsupportedFeature);
}

// ============================================================================
// Round-trip Tests (Comprehensive)
// ============================================================================

TEST(RoundTrip, AllSchemesWithRandomData) {
    CompressorStorage<FullCompressorSpec> compressor_storage;
    DecompressorStorage<FullDecompressorSpec> decompressor_storage;
    
    const std::array schemes = {
        CompressionScheme::None,
        CompressionScheme::PackBits,
        CompressionScheme::ZSTD,
        CompressionScheme::ZSTD_Alt
    };
    
    for (auto scheme : schemes) {
        auto input = generate_random_bytes(1000, static_cast<uint64_t>(scheme));
        std::vector<std::byte> compressed;
        
        auto compress_result = compressor_storage.compress(
            compressed, 0, input, scheme
        );
        ASSERT_TRUE(compress_result.is_ok()) << "Scheme: " << static_cast<int>(scheme);
        
        std::vector<std::byte> decompressed(input.size());
        auto decompress_result = decompressor_storage.decompress(
            decompressed,
            std::span<const std::byte>(compressed.data(), compress_result.value()),
            scheme
        );
        ASSERT_TRUE(decompress_result.is_ok()) << "Scheme: " << static_cast<int>(scheme);
        EXPECT_TRUE(vectors_equal(decompressed, input)) << "Scheme: " << static_cast<int>(scheme);
    }
}

TEST(RoundTrip, AllSchemesWithCompressibleData) {
    CompressorStorage<FullCompressorSpec> compressor_storage;
    DecompressorStorage<FullDecompressorSpec> decompressor_storage;
    
    const std::array schemes = {
        CompressionScheme::None,
        CompressionScheme::PackBits,
        CompressionScheme::ZSTD
    };
    
    for (auto scheme : schemes) {
        auto input = generate_compressible_data(5000);
        std::vector<std::byte> compressed;
        
        auto compress_result = compressor_storage.compress(
            compressed, 0, input, scheme
        );
        ASSERT_TRUE(compress_result.is_ok()) << "Scheme: " << static_cast<int>(scheme);
        
        std::vector<std::byte> decompressed(input.size());
        auto decompress_result = decompressor_storage.decompress(
            decompressed,
            std::span<const std::byte>(compressed.data(), compress_result.value()),
            scheme
        );
        ASSERT_TRUE(decompress_result.is_ok()) << "Scheme: " << static_cast<int>(scheme);
        EXPECT_TRUE(vectors_equal(decompressed, input)) << "Scheme: " << static_cast<int>(scheme);
    }
}

TEST(RoundTrip, MultipleSizesPackBits) {
    PackBitsCompressor compressor;
    PackBitsDecompressor decompressor;
    
    const std::array sizes = {0, 1, 10, 100, 127, 128, 255, 256, 1000, 10000};
    
    for (auto size : sizes) {
        auto input = generate_data_with_runs(size, size);
        std::vector<std::byte> compressed;
        
        auto compress_result = compressor.compress(compressed, 0, input);
        ASSERT_TRUE(compress_result.is_ok()) << "Size: " << size;
        
        std::vector<std::byte> decompressed(size);
        auto decompress_result = decompressor.decompress(
            decompressed,
            std::span<const std::byte>(compressed.data(), compress_result.value())
        );
        ASSERT_TRUE(decompress_result.is_ok()) << "Size: " << size;
        EXPECT_TRUE(vectors_equal(decompressed, input)) << "Size: " << size;
    }
}

TEST(RoundTrip, MultipleSizesZstd) {
    ZstdCompressor compressor;
    ZstdDecompressor decompressor;
    
    const std::array sizes = {0, 1, 10, 100, 1000, 10000, 50000};
    
    for (auto size : sizes) {
        auto input = generate_compressible_data(size);
        std::vector<std::byte> compressed;
        
        auto compress_result = compressor.compress(compressed, 0, input);
        ASSERT_TRUE(compress_result.is_ok()) << "Size: " << size;
        
        std::vector<std::byte> decompressed(size);
        auto decompress_result = decompressor.decompress(
            decompressed,
            std::span<const std::byte>(compressed.data(), compress_result.value())
        );
        ASSERT_TRUE(decompress_result.is_ok()) << "Size: " << size;
        EXPECT_TRUE(vectors_equal(decompressed, input)) << "Size: " << size;
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
