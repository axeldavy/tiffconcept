#pragma once

/**
 * @file decoder.hpp
 * @brief Chunk decoder for TIFF image data decompression and predictor decoding
 * 
 * This header provides the ChunkDecoder class for decoding compressed TIFF chunks
 * (tiles or strips). It handles both decompression and predictor decoding in a
 * single operation.
 * 
 * ## Key Concepts
 * 
 * - **Chunk**: A tile or strip of image data that can be independently compressed
 * 
 * - **Decompression**: Converting compressed data back to raw pixel values using
 *   schemes like LZW, Deflate, ZSTD, etc.
 * 
 * - **Predictor Decoding**: Reversing differential encoding applied before compression
 *   to improve compression ratios (e.g., horizontal differencing)
 * 
 * - **Scratch Buffer**: Internal reusable buffer to avoid allocations on repeated
 *   decode operations
 * 
 * ## Decoding Pipeline
 * 
 * For each chunk:
 * 1. Decompress using the specified compression scheme
 * 2. Apply predictor decoding if needed (in-place)
 * 3. Return decoded pixel data
 * 
 * ## Thread Safety
 * 
 * ChunkDecoder is NOT thread-safe. Use one decoder instance per thread, or
 * synchronize access externally.
 * 
 * ## Example Usage
 * 
 * @code{.cpp}
 * using namespace tiffconcept;
 * 
 * // Create decoder for uint16_t pixels with standard decompression
 * using DecompSpec = StandardDecompressors;
 * ChunkDecoder<uint16_t, DecompSpec> decoder;
 * 
 * // Decode a compressed tile
 * auto result = decoder.decode(
 *     compressed_data,
 *     tile_width,
 *     tile_height,
 *     CompressionScheme::LZW,
 *     Predictor::Horizontal,
 *     samples_per_pixel
 * );
 * 
 * if (result) {
 *     auto decoded_pixels = result.value();
 *     // Use decoded_pixels...
 * }
 * @endcode
 * 
 * @note All operations are noexcept and use Result<T> for error handling
 * @note The decoder maintains internal state (scratch buffer) for efficiency
 */

#include <cstring>
#include <span>
#include <vector>
#include "decompressor_base.hpp"
#include "predictor.hpp"
#include "result.hpp"
#include "types.hpp"

namespace tiffconcept {

/**
 * @brief Chunk decoder for tiles and strips
 * 
 * ChunkDecoder handles the complete decoding pipeline for TIFF image chunks:
 * decompression followed by predictor decoding. It maintains a reusable scratch
 * buffer to avoid allocations on repeated decode operations.
 * 
 * The decoder is templated on:
 * - **PixelType**: The output pixel type (uint8_t, uint16_t, float, etc.)
 * - **DecompSpec**: Decompressor specification defining supported compression schemes
 * 
 * ## Memory Management
 * 
 * The decoder provides three decoding modes:
 * 1. **decode()**: Uses internal scratch buffer (zero-copy for caller)
 * 2. **decode_into()**: Decodes into caller-provided buffer
 * 3. **decode_copy()**: Returns a new vector with decoded data
 * 
 * @tparam PixelType The pixel data type (must support delta decoding)
 * @tparam DecompSpec Decompressor specification (e.g., StandardDecompressors, ZstdDecompressors)
 * 
 * @note NOT thread-safe - use one decoder instance per thread
 * @note The scratch buffer grows as needed but never shrinks
 */
template <typename PixelType, typename DecompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidDecompressorSpec<DecompSpec>
class ChunkDecoder {
private:
    DecompressorStorage<DecompSpec> decompressors_;
    mutable std::vector<PixelType> scratch_buffer_;  // Reusable buffer for decode()
    
    /// Apply predictor decoding in-place (implementation in decoder_impl.hpp)
    [[nodiscard]] Result<void> apply_predictor(
        std::span<std::byte> data,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        Predictor predictor,
        uint16_t samples_per_pixel) const noexcept;
    
public:
    /**
     * @brief Default constructor
     * 
     * Creates a decoder with empty scratch buffer. The buffer will grow
     * as needed during decode operations.
     */
    ChunkDecoder();
    
    /**
     * @brief Decode chunk into caller-provided buffer
     * 
     * Decodes compressed chunk data directly into the provided output buffer.
     * This is the most flexible method when you manage your own buffers.
     * 
     * The decoding process:
     * 1. Decompress data using the specified compression scheme
     * 2. Apply predictor decoding if predictor != None
     * 
     * @param compressed_input Compressed chunk data
     * @param decompressed_output Output buffer (must be large enough)
     * @param width Chunk width in pixels
     * @param height Chunk height in pixels
     * @param compression Compression scheme used
     * @param predictor Predictor used (None, Horizontal, or FloatingPoint)
     * @param samples_per_pixel Number of samples per pixel (channels)
     * @return Ok() on success, or Error on failure
     * 
     * @throws None (noexcept)
     * @retval Error::Code::OutOfBounds Output buffer is too small
     * @retval Error::Code::CompressionError Decompression failed
     * @retval Error::Code::InvalidFormat Unsupported compression or predictor
     * 
     * @note Output buffer must be at least width × height × samples_per_pixel × sizeof(PixelType) bytes
     * @note Predictor decoding is applied in-place in the output buffer
     * @note For floating-point types, only FloatingPoint predictor is applied
     * @note For integer types, only Horizontal predictor is applied
     */
    [[nodiscard]] Result<void> decode_into(
        std::span<const std::byte> compressed_input,
        std::span<std::byte> decompressed_output,
        uint32_t width,
        uint32_t height,
        CompressionScheme compression = CompressionScheme::None,
        Predictor predictor = Predictor::None,
        uint16_t samples_per_pixel = 1) const noexcept;
    
    /**
     * @brief Decode chunk using internal scratch buffer
     * 
     * Decodes compressed chunk data into the decoder's internal scratch buffer
     * and returns a span over the decoded data. This is the most efficient method
     * for repeated decode operations as it reuses the scratch buffer.
     * 
     * **Important**: The returned span is valid only until:
     * - The next call to decode() on this decoder instance
     * - The ChunkDecoder object is destroyed
     * 
     * @param compressed_input Compressed chunk data
     * @param width Chunk width in pixels
     * @param height Chunk height in pixels
     * @param compression Compression scheme used
     * @param predictor Predictor used (None, Horizontal, or FloatingPoint)
     * @param samples_per_pixel Number of samples per pixel (channels)
     * @return Result containing span over decoded pixels, or an error
     * 
     * @throws None (noexcept)
     * @retval Error::Code::CompressionError Decompression failed
     * @retval Error::Code::InvalidFormat Unsupported compression or predictor
     * @retval Error::Code::MemoryError Failed to allocate scratch buffer
     * 
     * @note The scratch buffer grows as needed but never shrinks
     * @note NOT thread-safe - don't share decoder between threads
     * @note Returned span is invalidated by next decode() call
     * 
     * @par Example:
     * @code
     * auto result = decoder.decode(compressed, 256, 256, CompressionScheme::LZW);
     * if (result) {
     *     auto pixels = result.value();
     *     // Use pixels immediately - don't store the span
     *     process_pixels(pixels);
     * }
     * @endcode
     */
    [[nodiscard]] Result<std::span<const PixelType>> decode(
        std::span<const std::byte> compressed_input,
        uint32_t width,
        uint32_t height,
        CompressionScheme compression = CompressionScheme::None,
        Predictor predictor = Predictor::None,
        uint16_t samples_per_pixel = 1) noexcept;
    
    /**
     * @brief Decode chunk and return owned copy
     * 
     * Decodes compressed chunk data and returns a new vector containing the
     * decoded pixels. Use this when you need to own the decoded data and
     * store it beyond the lifetime of the decode operation.
     * 
     * @param compressed_input Compressed chunk data
     * @param width Chunk width in pixels
     * @param height Chunk height in pixels
     * @param compression Compression scheme used
     * @param predictor Predictor used (None, Horizontal, or FloatingPoint)
     * @param samples_per_pixel Number of samples per pixel (channels)
     * @return Result containing vector of decoded pixels, or an error
     * 
     * @throws None (noexcept)
     * @retval Error::Code::CompressionError Decompression failed
     * @retval Error::Code::InvalidFormat Unsupported compression or predictor
     * @retval Error::Code::MemoryError Failed to allocate output vector
     * 
     * @note This allocates a new vector for each call
     * @note Use decode() if you don't need to own the data
     * @note The returned vector contains width × height × samples_per_pixel elements
     * 
     * @par Example:
     * @code
     * auto result = decoder.decode_copy(compressed, 256, 256, CompressionScheme::Deflate);
     * if (result) {
     *     auto pixels = std::move(result.value());
     *     // pixels can be stored and used later
     *     my_cache[key] = std::move(pixels);
     * }
     * @endcode
     */
    [[nodiscard]] Result<std::vector<PixelType>> decode_copy(
        std::span<const std::byte> compressed_input,
        uint32_t width,
        uint32_t height,
        CompressionScheme compression = CompressionScheme::None,
        Predictor predictor = Predictor::None,
        uint16_t samples_per_pixel = 1) const noexcept;
};

} // namespace tiffconcept

// Include implementation
#define TIFFCONCEPT_DECODER_HEADER
#include "impl/decoder_impl.hpp"