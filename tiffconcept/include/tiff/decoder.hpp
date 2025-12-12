#pragma once

#include <cstring>
#include <span>
#include <vector>
#include "decompressor_base.hpp"
#include "predictor.hpp"
#include "result.hpp"
#include "types.hpp"

namespace tiff {

/// Chunk decoder - handles decompression and predictor decoding for tiles and strips
/// NOT thread-safe - only one thread should use it at a time.
/// Contains a scratch buffer to avoid reallocations
template <typename PixelType, typename DecompSpec>
    requires (std::is_same_v<PixelType, uint8_t> || 
              std::is_same_v<PixelType, uint16_t> ||
              std::is_same_v<PixelType, uint32_t> ||
              std::is_same_v<PixelType, uint64_t> ||
              std::is_same_v<PixelType, int8_t> ||
              std::is_same_v<PixelType, int16_t> ||
              std::is_same_v<PixelType, int32_t> ||
              std::is_same_v<PixelType, int64_t> ||
              std::is_same_v<PixelType, float> ||
              std::is_same_v<PixelType, double>) &&
             ValidDecompressorSpec<DecompSpec>
class ChunkDecoder {
private:
    DecompressorStorage<DecompSpec> decompressors_;
    std::vector<PixelType> scratch_buffer_;  // Reusable buffer for decode()
    
    /// Apply predictor decoding in-place
    [[nodiscard]] Result<void> apply_predictor(
        std::span<std::byte> data,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        Predictor predictor,
        uint16_t samples_per_pixel) const noexcept {
        
        // Cast byte span to typed span
        std::span<PixelType> typed_data(
            reinterpret_cast<PixelType*>(data.data()),
            data.size() / sizeof(PixelType)
        );
        
        if (predictor == Predictor::Horizontal) {
            if constexpr (!std::is_floating_point_v<PixelType>) {
                delta_decode_horizontal(typed_data, width, height, stride, samples_per_pixel);
            }
        } else if (predictor == Predictor::FloatingPoint) {
            if constexpr (std::is_floating_point_v<PixelType>) {
                delta_decode_floating_point(typed_data, width, height, stride, samples_per_pixel);
            }
        }
        return Ok();
    }
    
public:
    ChunkDecoder() : decompressors_(), scratch_buffer_() {}
    
    /// Decode chunk (tile or strip) into provided output buffer
    /// Output buffer must be large enough for the full chunk (width * height * samples_per_pixel * sizeof(PixelType))
    [[nodiscard]] Result<void> decode_into(
        std::span<const std::byte> compressed_input,
        std::span<std::byte> decompressed_output,
        uint32_t width,
        uint32_t height,
        CompressionScheme compression = CompressionScheme::None,
        Predictor predictor = Predictor::None,
        uint16_t samples_per_pixel = 1) const noexcept {
        
        if (decompressed_output.size() < width * height * samples_per_pixel * sizeof(PixelType)) {
            return Err(Error::Code::OutOfBounds, "Insufficient decompressed output size");
        }
        
        // Decompress
        auto decompress_result = decompressors_.decompress(
            decompressed_output, 
            compressed_input, 
            compression
        );
        
        if (!decompress_result) {
            return Err(decompress_result.error().code, decompress_result.error().message);
        }
        
        // Apply predictor decoding if needed (in-place)
        // Stride in elements is width * samples_per_pixel
        return apply_predictor(decompressed_output, width, height, width * samples_per_pixel, predictor, samples_per_pixel);
    }
    
    /// Decode chunk (tile or strip) using internal scratch buffer (returned by reference)
    /// The returned span is valid until the next call to decode(), or until
    /// the ChunkDecoder object is destroyed.
    /// This avoids allocation on each call
    [[nodiscard]] Result<std::span<const PixelType>> decode(
        std::span<const std::byte> compressed_input,
        uint32_t width,
        uint32_t height,
        CompressionScheme compression = CompressionScheme::None,
        Predictor predictor = Predictor::None,
        uint16_t samples_per_pixel = 1) noexcept {
        
        // Ensure scratch buffer is large enough
        std::size_t required_size = width * height * samples_per_pixel;
        if (scratch_buffer_.size() < required_size) {
            scratch_buffer_.resize(required_size);
        }
        
        // Create span over scratch buffer
        std::span<std::byte> output(
            reinterpret_cast<std::byte*>(scratch_buffer_.data()),
            required_size * sizeof(PixelType)
        );
        
        // Decode into scratch buffer
        auto result = decode_into(compressed_input, output, width, height, compression, predictor, samples_per_pixel);
        if (!result) {
            return Err(result.error().code, result.error().message);
        }
        
        // Return span over decoded data
        return Ok(std::span<const PixelType>(scratch_buffer_.data(), required_size));
    }
    
    /// Decode chunk (tile or strip) and return a copy as vector
    /// Useful when you need to own the data
    [[nodiscard]] Result<std::vector<PixelType>> decode_copy(
        std::span<const std::byte> compressed_input,
        uint32_t width,
        uint32_t height,
        CompressionScheme compression = CompressionScheme::None,
        Predictor predictor = Predictor::None,
        uint16_t samples_per_pixel = 1) const noexcept {
        
        // Allocate output vector
        std::size_t required_size = width * height * samples_per_pixel;
        std::vector<PixelType> output(required_size);
        
        // Create span over output vector
        std::span<std::byte> output_span(
            reinterpret_cast<std::byte*>(output.data()),
            required_size * sizeof(PixelType)
        );
        
        // Decode directly into the vector
        auto result = decode_into(compressed_input, output_span, width, height, compression, predictor, samples_per_pixel);
        if (!result) {
            return Err(result.error().code, result.error().message);
        }
        
        return Ok(std::move(output));
    }
};

} // namespace tiff