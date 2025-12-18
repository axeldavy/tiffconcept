#pragma once

#include <cstring>
#include <span>
#include <vector>
#include "compressor_base.hpp"
#include "predictor.hpp"
#include "result.hpp"
#include "types.hpp"
#include "strategy/write_strategy.hpp"

namespace tiffconcept {

/// @brief Chunk encoder - handles predictor encoding and compression for tiles and strips
/// @tparam PixelType The pixel data type (e.g., uint8_t, uint16_t, float)
/// @tparam CompSpec Compressor specification defining compression algorithm
/// 
/// @note NOT thread-safe - only one thread should use an instance at a time
/// @note Contains scratch buffers to avoid reallocations across multiple encode operations
/// @note Reusable: call encode() multiple times, then clear() to release memory
/// 
/// @warning Do not share instances between threads
/// @warning Predictor encoding is destructive - input data is copied to internal buffer
template <typename PixelType, typename CompSpec>
    requires predictor::DeltaDecodable<PixelType> &&
             ValidCompressorSpec<CompSpec>
class ChunkEncoder {
public:
    /// @brief Default constructor
    /// @note Initializes empty scratch buffers
    ChunkEncoder();
    
    /// @brief Encode chunk (tile or strip) and return as EncodedChunk
    /// @param input_data Input pixel data for the chunk
    /// @param chunk_index Index of this chunk in the image
    /// @param pixel_x X-coordinate of chunk's top-left pixel in image
    /// @param pixel_y Y-coordinate of chunk's top-left pixel in image
    /// @param pixel_z Z-coordinate of chunk's top-left pixel in image (for 3D)
    /// @param width Chunk width in pixels
    /// @param height Chunk height in pixels
    /// @param depth Chunk depth in slices (1 for 2D)
    /// @param plane Plane index (for planar configuration)
    /// @param compression Compression scheme to apply
    /// @param predictor Predictor to apply before compression
    /// @param samples_per_pixel Number of samples per pixel
    /// @return Result<EncodedChunk> containing compressed data and metadata
    /// @retval Success Chunk encoded successfully
    /// @retval UnsupportedFeature Empty chunk (width/height/depth is zero)
    /// @retval OutOfBounds Input data too small for chunk dimensions
    /// @retval CompressionError Compression failed
    /// @note The returned EncodedChunk owns the compressed data
    /// @note For 3D chunks, depth is flattened into height for 2D processing
    /// @note Scratch buffers are reused across calls for efficiency
    [[nodiscard]] Result<EncodedChunk> encode(
        std::span<const PixelType> input_data,
        uint32_t chunk_index,
        uint32_t pixel_x,
        uint32_t pixel_y,
        uint32_t pixel_z,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint16_t plane,
        CompressionScheme compression = CompressionScheme::None,
        Predictor predictor = Predictor::None,
        uint16_t samples_per_pixel = 1) noexcept;
    
    /// @brief Encode a 2D chunk (convenience wrapper for common case)
    /// @param input_data Input pixel data for the chunk
    /// @param chunk_index Index of this chunk in the image
    /// @param pixel_x X-coordinate of chunk's top-left pixel in image
    /// @param pixel_y Y-coordinate of chunk's top-left pixel in image
    /// @param width Chunk width in pixels
    /// @param height Chunk height in pixels
    /// @param plane Plane index (for planar configuration)
    /// @param compression Compression scheme to apply
    /// @param predictor Predictor to apply before compression
    /// @param samples_per_pixel Number of samples per pixel
    /// @return Result<EncodedChunk> containing compressed data and metadata
    /// @retval Success Chunk encoded successfully
    /// @retval UnsupportedFeature Empty chunk
    /// @retval OutOfBounds Input data too small
    /// @retval CompressionError Compression failed
    /// @note Sets pixel_z = 0 and depth = 1 automatically
    [[nodiscard]] Result<EncodedChunk> encode_2d(
        std::span<const PixelType> input_data,
        uint32_t chunk_index,
        uint32_t pixel_x,
        uint32_t pixel_y,
        uint32_t width,
        uint32_t height,
        uint16_t plane,
        CompressionScheme compression = CompressionScheme::None,
        Predictor predictor = Predictor::None,
        uint16_t samples_per_pixel = 1) noexcept;
    
    /// @brief Encode chunk using tile indices instead of pixel coordinates
    /// @param input_data Input pixel data for the chunk
    /// @param chunk_index Index of this chunk in the image
    /// @param tile_x_index Tile index in X dimension
    /// @param tile_y_index Tile index in Y dimension
    /// @param tile_z_index Tile index in Z dimension
    /// @param tile_width Width of each tile in pixels
    /// @param tile_height Height of each tile in pixels
    /// @param tile_depth Depth of each tile in slices
    /// @param plane Plane index (for planar configuration)
    /// @param compression Compression scheme to apply
    /// @param predictor Predictor to apply before compression
    /// @param samples_per_pixel Number of samples per pixel
    /// @return Result<EncodedChunk> containing compressed data and metadata
    /// @retval Success Chunk encoded successfully
    /// @retval UnsupportedFeature Empty chunk
    /// @retval OutOfBounds Input data too small
    /// @retval CompressionError Compression failed
    /// @note Calculates pixel coordinates as: pixel_x = tile_x_index * tile_width, etc.
    /// @note For tiled images: use tile grid indices
    /// @note For stripped images: use strip index with tile_x_index = 0
    [[nodiscard]] Result<EncodedChunk> encode_with_tile_indices(
        std::span<const PixelType> input_data,
        uint32_t chunk_index,
        uint32_t tile_x_index,
        uint32_t tile_y_index,
        uint32_t tile_z_index,
        uint32_t tile_width,
        uint32_t tile_height,
        uint32_t tile_depth,
        uint16_t plane,
        CompressionScheme compression = CompressionScheme::None,
        Predictor predictor = Predictor::None,
        uint16_t samples_per_pixel = 1) noexcept;
    
    /// @brief Clear scratch buffers and release memory
    /// @note Useful for memory management after batch encoding
    /// @note Shrinks buffers to zero capacity
    void clear() noexcept;
    
    /// @brief Get current scratch buffer sizes for diagnostics
    /// @return Pair of (predictor_buffer_bytes, compressed_buffer_bytes)
    /// @note Returns capacity in bytes, not current size
    [[nodiscard]] std::pair<std::size_t, std::size_t> buffer_sizes() const noexcept;

private:
    CompressorStorage<CompSpec> compressors_;
    std::vector<PixelType> predictor_buffer_;  // Reusable buffer for predictor encoding
    std::vector<std::byte> compressed_buffer_; // Reusable buffer for compression output
    
    /// Apply predictor encoding (modifies data in-place)
    /// Returns the span to use for compression (either original or predictor_buffer_)
    [[nodiscard]] Result<std::span<const std::byte>> apply_predictor(
        std::span<const PixelType> input_data,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        Predictor predictor,
        uint16_t samples_per_pixel) noexcept;
};

} // namespace tiffconcept

#define TIFFCONCEPT_ENCODER_HEADER
#include "impl/encoder_impl.hpp"