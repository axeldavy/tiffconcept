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

// ============================================================================
// SIMD Path Coverage Tests
//
// All existing tests use width=10, which is too small to trigger any SIMD loop:
//   SSE2  u8  path requires width >= 16
//   SSE2  u16 path requires width >= 8
//   AVX2  u8  path requires width >= 32
//   AVX2  u16 path requires width >= 16
//   AVX512 u8  path requires width >= 64
//   AVX512 u16 path requires width >= 32
//
// The tests below cover every boundary, including transitions between multiple
// SIMD chunks and the scalar tail. Multi-row images verify that carry is reset
// at each row boundary (a global-carry bug would corrupt all rows after the first).
// Each test also compares against an independent scalar reference implementation
// so a bug in SIMD cannot be masked by a matching bug in encoding.
// ============================================================================

// Scalar reference: prefix-sum one row at a time (no SIMD)
template <typename T>
static std::vector<T> scalar_prefix_sum(std::vector<T> data,
                                        std::size_t width,
                                        std::size_t height,
                                        std::size_t stride) {
    for (std::size_t y = 0; y < height; ++y) {
        T acc = data[y * stride];
        for (std::size_t x = 1; x < width; ++x) {
            acc = static_cast<T>(acc + data[y * stride + x]);
            data[y * stride + x] = acc;
        }
    }
    return data;
}

// ---- uint8_t parameterised by width ----------------------------------------

class U8DeltaDecodeWidthTest : public ::testing::TestWithParam<std::size_t> {};

TEST_P(U8DeltaDecodeWidthTest, RoundTrip) {
    const std::size_t width  = GetParam();
    const std::size_t height = 4;
    const std::size_t stride = width;

    auto original = generate_random_data<uint8_t>(height * stride, width * 17 + 99);
    auto encoded  = original;
    delta_encode_horizontal(std::span(encoded), width, height, stride, 1);
    auto decoded  = encoded;
    delta_decode_horizontal(std::span(decoded),  width, height, stride, 1);
    EXPECT_EQ(original, decoded);
}

TEST_P(U8DeltaDecodeWidthTest, MatchesScalarReference) {
    const std::size_t width  = GetParam();
    const std::size_t height = 3;
    const std::size_t stride = width;

    // Start from arbitrary delta-encoded data (random bytes treated as deltas)
    auto encoded   = generate_random_data<uint8_t>(height * stride, width * 31 + 7);
    auto reference = scalar_prefix_sum(encoded, width, height, stride);

    delta_decode_horizontal(std::span(encoded), width, height, stride, 1);
    EXPECT_EQ(reference, encoded);
}

INSTANTIATE_TEST_SUITE_P(
    SIMDBoundaries, U8DeltaDecodeWidthTest,
    ::testing::Values(
        // Below SSE2 threshold
        1, 8, 15,
        // SSE2 exact fit and straddling boundaries
        16, 17, 23, 24,
        // AVX2 boundaries
        31, 32, 33,
        // Requires two SIMD regions (AVX2 + SSE2 tail, or AVX2×2)
        47, 48, 49, 63, 64, 65,
        // Multiple full AVX2 chunks plus a tail
        95, 96, 97, 100, 127, 128, 129,
        // Larger images typical of real TIFF tiles
        200, 512, 1000
    ),
    [](const ::testing::TestParamInfo<std::size_t>& info) {
        return "width_" + std::to_string(info.param);
    }
);

// ---- uint16_t parameterised by width ---------------------------------------

class U16DeltaDecodeWidthTest : public ::testing::TestWithParam<std::size_t> {};

TEST_P(U16DeltaDecodeWidthTest, RoundTrip) {
    const std::size_t width  = GetParam();
    const std::size_t height = 4;
    const std::size_t stride = width;

    auto original = generate_random_data<uint16_t>(height * stride, width * 13 + 77);
    auto encoded  = original;
    delta_encode_horizontal(std::span(encoded), width, height, stride, 1);
    auto decoded  = encoded;
    delta_decode_horizontal(std::span(decoded),  width, height, stride, 1);
    EXPECT_EQ(original, decoded);
}

TEST_P(U16DeltaDecodeWidthTest, MatchesScalarReference) {
    const std::size_t width  = GetParam();
    const std::size_t height = 3;
    const std::size_t stride = width;

    auto encoded   = generate_random_data<uint16_t>(height * stride, width * 29 + 3);
    auto reference = scalar_prefix_sum(encoded, width, height, stride);

    delta_decode_horizontal(std::span(encoded), width, height, stride, 1);
    EXPECT_EQ(reference, encoded);
}

INSTANTIATE_TEST_SUITE_P(
    SIMDBoundaries, U16DeltaDecodeWidthTest,
    ::testing::Values(
        // Below SSE2 threshold
        1, 4, 7,
        // SSE2 exact fit and straddling
        8, 9, 12, 15,
        // AVX2 boundaries
        16, 17, 23, 24,
        // Multiple SIMD regions
        31, 32, 33, 47, 48, 49,
        // AVX512 boundaries
        63, 64, 65, 95, 96, 97,
        100, 200, 512, 1000
    ),
    [](const ::testing::TestParamInfo<std::size_t>& info) {
        return "width_" + std::to_string(info.param);
    }
);

// ---- int8_t  (uses the same SIMD branches as uint8_t) ----------------------

class I8DeltaDecodeWidthTest : public ::testing::TestWithParam<std::size_t> {};

TEST_P(I8DeltaDecodeWidthTest, RoundTrip) {
    const std::size_t width  = GetParam();
    const std::size_t height = 3;
    const std::size_t stride = width;

    auto original = generate_random_data<int8_t>(height * stride, width * 11 + 5);
    auto encoded  = original;
    delta_encode_horizontal(std::span(encoded), width, height, stride, 1);
    auto decoded  = encoded;
    delta_decode_horizontal(std::span(decoded),  width, height, stride, 1);
    EXPECT_EQ(original, decoded);
}

INSTANTIATE_TEST_SUITE_P(
    SIMDBoundaries, I8DeltaDecodeWidthTest,
    ::testing::Values(15, 16, 17, 32, 33, 48, 64, 100, 200),
    [](const ::testing::TestParamInfo<std::size_t>& info) {
        return "width_" + std::to_string(info.param);
    }
);

// ---- int16_t  (uses the same SIMD branches as uint16_t) --------------------

class I16DeltaDecodeWidthTest : public ::testing::TestWithParam<std::size_t> {};

TEST_P(I16DeltaDecodeWidthTest, RoundTrip) {
    const std::size_t width  = GetParam();
    const std::size_t height = 3;
    const std::size_t stride = width;

    auto original = generate_random_data<int16_t>(height * stride, width * 7 + 3);
    auto encoded  = original;
    delta_encode_horizontal(std::span(encoded), width, height, stride, 1);
    auto decoded  = encoded;
    delta_decode_horizontal(std::span(decoded),  width, height, stride, 1);
    EXPECT_EQ(original, decoded);
}

INSTANTIATE_TEST_SUITE_P(
    SIMDBoundaries, I16DeltaDecodeWidthTest,
    ::testing::Values(7, 8, 9, 16, 17, 32, 33, 64, 65, 100, 200),
    [](const ::testing::TestParamInfo<std::size_t>& info) {
        return "width_" + std::to_string(info.param);
    }
);

// ---- Carry isolation: each row must be decoded independently ----------------
//
// If carry from one row bleeds into the next, rows after the first will decode
// incorrectly. We detect this by encoding rows with identical content and verifying
// all rows produced identical encoded data, then checking round-trip.

TEST(PredictorSIMDTest, U8CarryResetPerRow) {
    const std::size_t width  = 64; // two full AVX2 chunks
    const std::size_t height = 5;
    const std::size_t stride = width;

    auto row = generate_random_data<uint8_t>(width, 0xDEADBEEF);
    std::vector<uint8_t> original(height * stride);
    for (std::size_t y = 0; y < height; ++y)
        std::copy(row.begin(), row.end(), original.begin() + y * stride);

    auto encoded = original;
    delta_encode_horizontal(std::span(encoded), width, height, stride, 1);

    // All encoded rows must be identical (because all original rows were identical)
    for (std::size_t y = 1; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            EXPECT_EQ(encoded[x], encoded[y * stride + x])
                << "Encoded row " << y << " element " << x << " differs from row 0";
        }
    }

    auto decoded = encoded;
    delta_decode_horizontal(std::span(decoded), width, height, stride, 1);
    EXPECT_EQ(original, decoded);
}

TEST(PredictorSIMDTest, U16CarryResetPerRow) {
    const std::size_t width  = 32; // two full AVX2 chunks for u16
    const std::size_t height = 5;
    const std::size_t stride = width;

    auto row = generate_random_data<uint16_t>(width, 0xCAFEBABE);
    std::vector<uint16_t> original(height * stride);
    for (std::size_t y = 0; y < height; ++y)
        std::copy(row.begin(), row.end(), original.begin() + y * stride);

    auto encoded = original;
    delta_encode_horizontal(std::span(encoded), width, height, stride, 1);

    for (std::size_t y = 1; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            EXPECT_EQ(encoded[x], encoded[y * stride + x])
                << "Encoded row " << y << " element " << x << " differs from row 0";
        }
    }

    auto decoded = encoded;
    delta_decode_horizontal(std::span(decoded), width, height, stride, 1);
    EXPECT_EQ(original, decoded);
}

// ---- Known-value decode test -----------------------------------------------
//
// Feeds delta data with a known correct output so a bug in SIMD cannot be
// hidden by a matching bug in the encoder.

TEST(PredictorSIMDTest, U8KnownDecodeWide) {
    // Construct a 64-element (two AVX2 chunks) encoded row:
    // enc[0] = 5 (starting value), enc[1..63] = 3 (constant delta)
    // Expected decoded output: 5, 8, 11, ..., 5 + 3*63 = 194
    const std::size_t width = 64;
    std::vector<uint8_t> encoded(width);
    encoded[0] = 5;
    std::fill(encoded.begin() + 1, encoded.end(), 3u);

    std::vector<uint8_t> expected(width);
    for (std::size_t i = 0; i < width; ++i)
        expected[i] = static_cast<uint8_t>(5 + 3 * i);

    delta_decode_horizontal(std::span(encoded), width, 1, width, 1);
    EXPECT_EQ(expected, encoded);
}

TEST(PredictorSIMDTest, U16KnownDecodeWide) {
    // 32-element row (two AVX2 u16 chunks)
    // enc[0] = 100, enc[1..31] = 200 (delta)
    // decoded[i] = 100 + 200*i  (mod 65536)
    const std::size_t width = 32;
    std::vector<uint16_t> encoded(width);
    encoded[0] = 100;
    std::fill(encoded.begin() + 1, encoded.end(), uint16_t{200});

    std::vector<uint16_t> expected(width);
    for (std::size_t i = 0; i < width; ++i)
        expected[i] = static_cast<uint16_t>(100 + 200 * i);

    delta_decode_horizontal(std::span(encoded), width, 1, width, 1);
    EXPECT_EQ(expected, encoded);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}