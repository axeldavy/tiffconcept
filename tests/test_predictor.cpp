#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstring>
#include <vector>
#include <random>
#include <limits>
#include <algorithm>

#include "../tiffconcept/include/tiffconcept/lowlevel/predictor.hpp"
#include "../tiffconcept/include/tiffconcept/types/tiff_spec.hpp"

using namespace tiffconcept;
using namespace tiffconcept::predictor;

// ============================================================================
// Helper Functions
// ============================================================================

/// Helper to generate random data
template <typename T>
std::vector<T> generate_random_data(std::size_t count, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::vector<T> data(count);
    
    if constexpr (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<T> dist(
            static_cast<T>(-1000.0), 
            static_cast<T>(1000.0)
        );
        for (auto& val : data) {
            val = dist(rng);
        }
    } else if constexpr (std::is_same_v<T, Float16> || std::is_same_v<T, Float24>) {
        std::uniform_real_distribution<float> dist(-1000.0f, 1000.0f);
        for (auto& val : data) {
            val = T(dist(rng));
        }
    } else {
        std::uniform_int_distribution<std::conditional_t<
            sizeof(T) == 1, int, T
        >> dist(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
        for (auto& val : data) {
            val = static_cast<T>(dist(rng));
        }
    }
    
    return data;
}

// ============================================================================
// Integer Horizontal Predictor Tests
// ============================================================================

TEST(PredictorTest, HorizontalDeltaUInt8SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original = generate_random_data<uint8_t>(height * stride);
    std::vector<uint8_t> encoded = original;
    
    // Encode
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    
    // Decode
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    // Verify round-trip
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaUInt16SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint16_t> original = generate_random_data<uint16_t>(height * stride);
    std::vector<uint16_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint16_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaUInt32SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint32_t> original = generate_random_data<uint32_t>(height * stride);
    std::vector<uint32_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint32_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaUInt64SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint64_t> original = generate_random_data<uint64_t>(height * stride);
    std::vector<uint64_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint64_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaInt8SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<int8_t> original = generate_random_data<int8_t>(height * stride);
    std::vector<int8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<int8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaInt16SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<int16_t> original = generate_random_data<int16_t>(height * stride);
    std::vector<int16_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<int16_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaInt32SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<int32_t> original = generate_random_data<int32_t>(height * stride);
    std::vector<int32_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<int32_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaInt64SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<int64_t> original = generate_random_data<int64_t>(height * stride);
    std::vector<int64_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<int64_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

// ============================================================================
// Multi-Channel Integer Tests
// ============================================================================

TEST(PredictorTest, HorizontalDeltaUInt8TwoChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 2;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original = generate_random_data<uint8_t>(height * stride);
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaUInt8ThreeChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original = generate_random_data<uint8_t>(height * stride);
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaUInt8FourChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 4;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original = generate_random_data<uint8_t>(height * stride);
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaUInt16RGB) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint16_t> original = generate_random_data<uint16_t>(height * stride);
    std::vector<uint16_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint16_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaUInt16RGBA) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 4;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint16_t> original = generate_random_data<uint16_t>(height * stride);
    std::vector<uint16_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint16_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

// ============================================================================
// Uncommon Channel Counts (Generic Fallback Path)
// ============================================================================

TEST(PredictorTest, HorizontalDeltaUInt8FiveChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 5;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original = generate_random_data<uint8_t>(height * stride);
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, HorizontalDeltaUInt16SevenChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 7;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint16_t> original = generate_random_data<uint16_t>(height * stride);
    std::vector<uint16_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint16_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

// ============================================================================
// Native Float Tests
// ============================================================================

TEST(PredictorTest, FloatingPointFloat32SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<float> original = generate_random_data<float>(height * stride);
    std::vector<float> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<float> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, FloatingPointFloat64SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<double> original = generate_random_data<double>(height * stride);
    std::vector<double> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<double> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, FloatingPointFloat32TwoChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 2;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<float> original = generate_random_data<float>(height * stride);
    std::vector<float> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<float> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, FloatingPointFloat32ThreeChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<float> original = generate_random_data<float>(height * stride);
    std::vector<float> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<float> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, FloatingPointFloat32FourChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 4;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<float> original = generate_random_data<float>(height * stride);
    std::vector<float> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<float> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, FloatingPointFloat64RGB) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<double> original = generate_random_data<double>(height * stride);
    std::vector<double> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<double> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, FloatingPointFloat32FiveChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 5;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<float> original = generate_random_data<float>(height * stride);
    std::vector<float> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<float> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

// ============================================================================
// Non-Native Float Tests (Float16, Float24)
// ============================================================================

TEST(PredictorTest, FloatingPointFloat16SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<Float16> original = generate_random_data<Float16>(height * stride);
    std::vector<Float16> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<Float16> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    // Compare byte-by-byte for non-native floats
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(original[i].as_uint16(), decoded[i].as_uint16()) 
            << "Mismatch at index " << i;
    }
}

TEST(PredictorTest, FloatingPointFloat24SingleChannel) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<Float24> original = generate_random_data<Float24>(height * stride);
    std::vector<Float24> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<Float24> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    // Compare byte-by-byte for non-native floats
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(original[i].as_uint32(), decoded[i].as_uint32()) 
            << "Mismatch at index " << i;
    }
}

TEST(PredictorTest, FloatingPointFloat16ThreeChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<Float16> original = generate_random_data<Float16>(height * stride);
    std::vector<Float16> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<Float16> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(original[i].as_uint16(), decoded[i].as_uint16()) 
            << "Mismatch at index " << i;
    }
}

TEST(PredictorTest, FloatingPointFloat24FourChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 4;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<Float24> original = generate_random_data<Float24>(height * stride);
    std::vector<Float24> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<Float24> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(original[i].as_uint32(), decoded[i].as_uint32()) 
            << "Mismatch at index " << i;
    }
}

TEST(PredictorTest, FloatingPointFloat16FiveChannels) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 5;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<Float16> original = generate_random_data<Float16>(height * stride);
    std::vector<Float16> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<Float16> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(original[i].as_uint16(), decoded[i].as_uint16()) 
            << "Mismatch at index " << i;
    }
}

// ============================================================================
// Edge Cases and Special Values
// ============================================================================

TEST(PredictorTest, SinglePixelImage) {
    const std::size_t width = 1;
    const std::size_t height = 1;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original = {100, 150, 200};
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, SingleRowImage) {
    const std::size_t width = 10;
    const std::size_t height = 1;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint16_t> original = generate_random_data<uint16_t>(height * stride);
    std::vector<uint16_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint16_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, SingleColumnImage) {
    const std::size_t width = 1;
    const std::size_t height = 10;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint16_t> original = generate_random_data<uint16_t>(height * stride);
    std::vector<uint16_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint16_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, AllZeros) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original(height * stride, 0);
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, AllMaxValues) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original(height * stride, 255);
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, FloatSpecialValues) {
    const std::size_t width = 10;
    const std::size_t height = 1;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<float> original = {
        0.0f, 
        -0.0f, 
        1.0f, 
        -1.0f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::min(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest()
    };
    std::vector<float> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<float> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    // Compare bit patterns for special values (NaN != NaN, so we compare bits)
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(std::bit_cast<uint32_t>(original[i]), std::bit_cast<uint32_t>(decoded[i]))
            << "Mismatch at index " << i;
    }
}

// ============================================================================
// Stride Tests (Verify padding/stride handling)
// ============================================================================

TEST(PredictorTest, WithStridePadding) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel + 5; // Add padding
    
    std::vector<uint8_t> original(height * stride);
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    
    // Fill only the valid pixel data, leave padding undefined
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width * samples_per_pixel; ++x) {
            original[y * stride + x] = static_cast<uint8_t>(dist(rng));
        }
    }
    
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    // Compare only the valid pixel data
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width * samples_per_pixel; ++x) {
            EXPECT_EQ(original[y * stride + x], decoded[y * stride + x])
                << "Mismatch at row " << y << ", col " << x;
        }
    }
}

// ============================================================================
// Large Image Tests
// ============================================================================

TEST(PredictorTest, LargeImageUInt8) {
    const std::size_t width = 1024;
    const std::size_t height = 1024;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original = generate_random_data<uint8_t>(height * stride, 12345);
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, LargeImageFloat32) {
    const std::size_t width = 512;
    const std::size_t height = 512;
    const std::size_t samples_per_pixel = 4;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<float> original = generate_random_data<float>(height * stride, 67890);
    std::vector<float> encoded = original;
    
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    std::vector<float> decoded = encoded;
    delta_decode_floating_point(std::span(decoded.begin(), decoded.size()), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

// ============================================================================
// Verify Encoding Actually Changes Data (not identity operation)
// ============================================================================

TEST(PredictorTest, EncodingChangesData) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original = generate_random_data<uint8_t>(height * stride);
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    
    // Encoded data should be different from original (unless highly correlated)
    bool has_difference = false;
    for (std::size_t i = 0; i < original.size(); ++i) {
        if (original[i] != encoded[i]) {
            has_difference = true;
            break;
        }
    }
    
    EXPECT_TRUE(has_difference) << "Encoding should change the data";
}

TEST(PredictorTest, EncodedFirstPixelUnchanged) {
    const std::size_t width = 10;
    const std::size_t height = 5;
    const std::size_t samples_per_pixel = 3;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<uint8_t> original = generate_random_data<uint8_t>(height * stride);
    std::vector<uint8_t> encoded = original;
    
    delta_encode_horizontal(std::span(encoded), width, height, stride, samples_per_pixel);
    
    // First pixel of each row should remain unchanged
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t s = 0; s < samples_per_pixel; ++s) {
            EXPECT_EQ(original[y * stride + s], encoded[y * stride + s])
                << "First pixel of row " << y << ", channel " << s << " should be unchanged";
        }
    }
}

// ============================================================================
// Decode-Only Tests (verify decoding works on pre-encoded data)
// ============================================================================

TEST(PredictorTest, DecodePreEncodedData) {
    const std::size_t width = 4;
    const std::size_t height = 2;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    // Create simple test data where we know the encoding
    std::vector<uint8_t> original = {10, 20, 30, 40,   // Row 0
                                     50, 60, 70, 80};  // Row 1
    
    // Manually encoded (each pixel stores difference from previous)
    std::vector<uint8_t> encoded = {10, 10, 10, 10,   // Row 0: 10, 20-10, 30-20, 40-30
                                    50, 10, 10, 10};  // Row 1: 50, 60-50, 70-60, 80-70
    
    std::vector<uint8_t> decoded = encoded;
    delta_decode_horizontal(std::span(decoded), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

TEST(PredictorTest, DecodePreEncodedFloat) {
    const std::size_t width = 4;
    const std::size_t height = 1;
    const std::size_t samples_per_pixel = 1;
    const std::size_t stride = width * samples_per_pixel;
    
    std::vector<float> original = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> encoded = original;
    
    // Encode
    delta_encode_floating_point(std::span(encoded), width, height, stride, samples_per_pixel);
    
    // Decode
    std::vector<float> decoded = encoded;
    delta_decode_floating_point(std::span(decoded), width, height, stride, samples_per_pixel);
    
    EXPECT_EQ(original, decoded);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}