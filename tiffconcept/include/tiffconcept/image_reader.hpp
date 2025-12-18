#pragma once

#include <algorithm>
#include <mutex>
#include <span>
#include <vector>
#include "strategy/chunk_strategy.hpp"
#include "decoder.hpp"
#include "decompressor_base.hpp"
#include "image_shape.hpp"
#include "strategy/read_strategy.hpp"
#include "reader_base.hpp"
#include "result.hpp"
#include "tiling.hpp"

namespace tiffconcept {

/// @brief Chunk processor that decodes and copies chunks to output buffer
/// @tparam OutSpec Output buffer layout specification (DHWC, DCHW, or CDHW)
/// @tparam PixelType The pixel data type (e.g., uint8_t, uint16_t, float)
/// @tparam DecompSpec Decompressor specification
/// @tparam ImageInfo Image information type (TiledImageInfo or StrippedImageInfo)
/// 
/// @note Thread-safe processor with internal decoder and mutex
/// @note Used in producer/consumer pattern with read strategies
/// @note Decodes compressed chunks and copies to output buffer with layout conversion
template <ImageLayoutSpec OutSpec, typename PixelType, typename DecompSpec, typename ImageInfo>
    requires ValidDecompressorSpec<DecompSpec>
class ChunkDecoderProcessor {
public:
    /// @brief Construct a chunk decoder processor
    /// @param decoder Reference to chunk decoder (shared across batches)
    /// @param image_info Reference to image metadata
    /// @param output_buffer Output buffer for decoded data
    /// @param region Region being read
    /// @param samples_per_pixel Number of samples per pixel for this chunk
    ChunkDecoderProcessor(
        ChunkDecoder<PixelType, DecompSpec>& decoder,
        const ImageInfo& image_info,
        std::span<PixelType> output_buffer,
        const ImageRegion& region,
        uint16_t samples_per_pixel) noexcept;
    
    /// @brief Thread-safe processing of chunk batches
    /// @param chunk_data_batch Batch of chunks to process
    /// @return Result<void> indicating success or error
    /// @retval Success All chunks decoded and copied successfully
    /// @retval CompressionError Decompression failed
    /// @retval OutOfBounds Buffer access error
    /// @note Acquires mutex for thread-safe access to decoder and output buffer
    /// @note Decodes each chunk and copies to appropriate position in output buffer
    [[nodiscard]] Result<void> process(std::span<const ChunkData> chunk_data_batch) noexcept;

private:
    ChunkDecoder<PixelType, DecompSpec>& decoder_;
    const ImageInfo& image_info_;
    std::span<PixelType> output_buffer_;
    const ImageRegion& region_;
    uint16_t samples_per_pixel_;
    mutable std::mutex process_mutex_;  // Thread-safe processing
    
    /// Copy a decoded chunk to the output buffer
    [[nodiscard]] Result<void> copy_decoded_chunk_to_buffer(
        const ChunkInfo& info,
        std::span<const PixelType> decoded_data) const noexcept;
};

/// @brief Strategy-based image reader for both tiled and stripped images
/// @tparam PixelType The pixel data type (uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, float, double)
/// @tparam DecompSpec Decompressor specification
/// @tparam ReadStrat Read strategy (e.g., SingleThreadedReader, BatchedReader)
/// 
/// @note Uses producer/consumer model: read strategy reads chunks and passes to decoder
/// @note Handles both tiled and stripped image layouts
/// @note Supports partial region reading with automatic chunk selection
/// @note Thread-safety depends on ReadStrat implementation
/// 
/// Example usage:
/// @code
/// ImageReader<uint8_t, DecompressorSpec<...>, SingleThreadedReader> reader;
/// ImageRegion region = image_info.shape().full_region();
/// std::vector<uint8_t> buffer(region.num_samples());
/// auto result = reader.read_region<ImageLayoutSpec::DHWC>(file_reader, image_info, buffer, region);
/// @endcode
template <typename PixelType, typename DecompSpec, typename ReadStrat>
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
class ImageReader {
public:
    /// @brief Construct image reader with specified read strategy
    /// @param read_strategy Read strategy instance (controls chunk batching, threading, etc.)
    explicit ImageReader(ReadStrat read_strategy = ReadStrat{}) noexcept;
    
    /// @brief Get mutable access to read strategy
    /// @return Reference to read strategy for configuration
    /// @note Useful for changing batching parameters, thread counts, etc.
    [[nodiscard]] ReadStrat& read_strategy() noexcept;
    
    /// @brief Get const access to read strategy
    /// @return Const reference to read strategy
    [[nodiscard]] const ReadStrat& read_strategy() const noexcept;
    
    /// @brief Read a region from a tiled image
    /// @tparam OutSpec Output buffer layout specification (DHWC, DCHW, or CDHW)
    /// @tparam Reader Reader type implementing RawReader concept
    /// @param reader File reader for accessing chunk data
    /// @param image_info Tiled image metadata
    /// @param output_buffer Output buffer for decoded pixels
    /// @param region Region to read from the image
    /// @return Result<void> indicating success or error
    /// @retval Success Region read successfully
    /// @retval OutOfBounds Region exceeds image bounds or buffer too small
    /// @retval UnsupportedFeature Unsupported configuration
    /// @retval CompressionError Decompression failed
    /// @note Automatically determines which tiles overlap the region
    /// @note Supports all 9 layout conversions (DHWC↔DCHW↔CDHW)
    template <ImageLayoutSpec OutSpec, typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<void> read_region(
        const Reader& reader,
        const TiledImageInfo<PixelType>& image_info,
        std::span<PixelType> output_buffer,
        const ImageRegion& region) noexcept;
    
    /// @brief Read a region from a stripped image
    /// @tparam OutSpec Output buffer layout specification (DHWC, DCHW, or CDHW)
    /// @tparam Reader Reader type implementing RawReader concept
    /// @param reader File reader for accessing strip data
    /// @param image_info Stripped image metadata
    /// @param output_buffer Output buffer for decoded pixels
    /// @param region Region to read from the image
    /// @return Result<void> indicating success or error
    /// @retval Success Region read successfully
    /// @retval OutOfBounds Region exceeds image bounds or buffer too small
    /// @retval UnsupportedFeature 3D regions not supported for stripped images
    /// @retval CompressionError Decompression failed
    /// @note Strips do not support 3D (depth must be 1)
    /// @note Automatically determines which strips overlap the region
    template <ImageLayoutSpec OutSpec, typename Reader>
        requires RawReader<Reader>
    [[nodiscard]] Result<void> read_region(
        const Reader& reader,
        const StrippedImageInfo<PixelType>& image_info,
        std::span<PixelType> output_buffer,
        const ImageRegion& region) noexcept;
    
    /// @brief Clear internal caches and reset strategies for reuse
    /// @note Releases memory used for chunk lists and decoder buffers
    /// @note Resets read strategy state
    void clear() noexcept;

private:
    mutable ReadStrat read_strategy_;
    
    // Reusable storage for chunk collection and decoding
    mutable std::vector<ChunkInfo> chunk_list_;
    mutable ChunkDecoder<PixelType, DecompSpec> decoder_;

    /// Common implementation for reading regions (works for both tiled and stripped)
    template <ImageLayoutSpec OutSpec, typename Reader, typename ImageInfo>
        requires RawReader<Reader>
    [[nodiscard]] Result<void> read_region_impl(
        const Reader& reader,
        const ImageInfo& image_info,
        std::span<PixelType> output_buffer,
        const ImageRegion& region) noexcept;
    
    /// Collect chunks for tiled images
    void collect_chunks_for_region(
        const TiledImageInfo<PixelType>& image_info,
        const ImageRegion& region,
        std::vector<ChunkInfo>& chunks) const noexcept;
    
    /// Collect chunks for stripped images
    void collect_chunks_for_region(
        const StrippedImageInfo<PixelType>& image_info,
        const ImageRegion& region,
        std::vector<ChunkInfo>& chunks) const noexcept;
    
    /// Generic chunk collection - works for both tiles and strips
    template <typename ImageInfo>
    void collect_chunks_in_range(
        const ImageInfo& image_info,
        const ImageRegion& region,
        std::vector<ChunkInfo>& chunks,
        uint32_t x_start, uint32_t x_end,
        uint32_t y_start, uint32_t y_end,
        uint32_t z_start, uint32_t z_end) const noexcept;
    
    /// Add chunk for tiled images
    void add_chunk_if_valid(
        const TiledImageInfo<PixelType>& image_info,
        uint32_t tile_x, uint32_t tile_y, uint32_t tile_z, uint16_t plane,
        std::vector<ChunkInfo>& chunks) const noexcept;
    
    /// Add chunk for stripped images
    void add_chunk_if_valid(
        const StrippedImageInfo<PixelType>& image_info,
        uint32_t x, uint32_t strip_idx, uint32_t z, uint16_t plane,
        std::vector<ChunkInfo>& chunks) const noexcept;
};

} // namespace tiffconcept

#define TIFFCONCEPT_IMAGE_READER_HEADER
#include "impl/image_reader_impl.hpp"