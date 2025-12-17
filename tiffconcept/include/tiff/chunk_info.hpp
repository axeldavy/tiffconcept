#pragma once


#include <algorithm>
#include <concepts>
#include <cstdint>
#include <span>
#include <vector>
#include "result.hpp"
#include "types.hpp"

namespace tiffconcept {

/// Information about a chunk (tile or strip) to be read
struct ChunkInfo {
    uint32_t offset;        // File offset to compressed data
    uint32_t byte_count;    // Size of compressed data in bytes
    uint32_t chunk_index;   // Linear chunk index (tile_index or strip_index)
    uint32_t pixel_x;       // Starting pixel X coordinate in image
    uint32_t pixel_y;       // Starting pixel Y coordinate in image
    uint32_t pixel_z;       // Starting pixel Z coordinate in image
    uint32_t width;         // Chunk width in pixels
    uint32_t height;        // Chunk height in pixels
    uint32_t depth;         // Chunk depth in pixels (1 for strips)
    uint16_t plane;         // Plane/channel index (for planar configuration)
};

/// A batch of chunks to be read together (non-owning view)
/// Chunks in a batch may not be contiguous in the file, but batching
/// reduces the number of I/O operations
struct ChunkBatch {
    std::span<const ChunkInfo> chunks;  // Non-owning view of chunks
    uint32_t min_offset;                // Minimum offset in this batch
    uint32_t max_end_offset;            // Maximum end offset in this batch
    
    /// Get the total span of file data covered by this batch
    /// This may include "holes" between non-contiguous chunks
    [[nodiscard]] uint32_t file_span() const noexcept {
        return max_end_offset - min_offset;
    }
    
    /// Get the total actual data size (sum of chunk byte_counts)
    [[nodiscard]] std::size_t total_data_size() const noexcept {
        std::size_t total = 0;
        for (const auto& chunk : chunks) {
            total += chunk.byte_count;
        }
        return total;
    }
    
    /// Get the overhead ratio (holes / total_span)
    [[nodiscard]] float overhead_ratio() const noexcept {
        uint32_t span = file_span();
        if (span == 0) return 0.0f;
        std::size_t data = total_data_size();
        return 1.0f - (static_cast<float>(data) / static_cast<float>(span));
    }
};

/// Parameters for batching read operations
struct BatchingParams {
    std::size_t min_batch_size;      // Minimum total bytes to batch (0 = no batching)
    std::size_t max_hole_size;       // Maximum gap between chunks to still batch them together
    std::size_t max_batch_span;      // Maximum file span for a single batch (0 = unlimited)
    
    /// Default: no batching
    static constexpr BatchingParams none() noexcept {
        return BatchingParams{0, 0, 0};
    }
    
    /// Aggressive batching for high-latency I/O (e.g., network, cloud storage)
    /// Batches up to 4MB with holes up to 256KB
    static constexpr BatchingParams high_latency() noexcept {
        return BatchingParams{
            .min_batch_size = 1024 * 1024,      // 1MB minimum batch
            .max_hole_size = 256 * 1024,        // 256KB holes allowed
            .max_batch_span = 4 * 1024 * 1024   // 4MB max span
        };
    }
    
    /// Moderate batching for local storage
    /// Smaller batches with tighter hole constraints
    static constexpr BatchingParams local_storage() noexcept {
        return BatchingParams{
            .min_batch_size = 128 * 1024,       // 128KB minimum batch
            .max_hole_size = 32 * 1024,         // 32KB holes allowed
            .max_batch_span = 1024 * 1024       // 1MB max span
        };
    }
    
    /// Read everything in one batch
    static constexpr BatchingParams all_at_once() noexcept {
        return BatchingParams{
            .min_batch_size = 0,
            .max_hole_size = SIZE_MAX,
            .max_batch_span = 0
        };
    }
};

/// Callback invoked for each batch
template <typename Callback>
concept BatchCallback = requires(Callback& cb, const ChunkBatch& batch) {
    { cb(batch) } -> std::same_as<Result<void>>;
};

/// Group chunks into batches based on batching parameters and invoke callback for each
/// Chunks should be sorted by offset before calling this function
/// The callback is invoked with each batch as it's created (no allocation of batch vector)
template <typename Callback>
    requires BatchCallback<Callback>
inline Result<void> create_batches(
    std::span<const ChunkInfo> chunks,
    const BatchingParams& params,
    Callback&& callback) noexcept {
    
    if (chunks.empty()) {
        return Ok();
    }
    
    // If no batching, each chunk is its own batch
    if (params.min_batch_size == 0) {
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            const auto& chunk = chunks[i];
            ChunkBatch batch;
            batch.chunks = chunks.subspan(i, 1);
            batch.min_offset = chunk.offset;
            batch.max_end_offset = chunk.offset + chunk.byte_count;
            auto result = callback(batch);
            if (!result) {
                return result;
            }
        }
        return Ok();
    }
    
    // Greedy batching algorithm
    std::size_t batch_start = 0;
    std::size_t batch_count = 0;
    uint32_t min_offset = chunks[0].offset;
    uint32_t max_end_offset = chunks[0].offset + chunks[0].byte_count;
    std::size_t accumulated_size = chunks[0].byte_count;
    
    for (std::size_t i = 1; i <= chunks.size(); ++i) {
        bool flush_batch = (i == chunks.size());
        
        if (i < chunks.size()) {
            const auto& chunk = chunks[i];
            uint32_t chunk_end = chunk.offset + chunk.byte_count;
            uint32_t gap = chunk.offset - max_end_offset;
            uint32_t new_span = chunk_end - min_offset;
            
            // Check if we should start a new batch
            bool should_split = false;
            
            if (params.max_hole_size > 0 && gap > params.max_hole_size) {
                should_split = true;
            }
            if (params.max_batch_span > 0 && new_span > params.max_batch_span) {
                should_split = true;
            }
            
            if (should_split && accumulated_size >= params.min_batch_size) {
                flush_batch = true;
            } else {
                // Add to current batch
                batch_count++;
                max_end_offset = std::max(max_end_offset, chunk_end);
                accumulated_size += chunk.byte_count;
            }
        }
        
        if (flush_batch && batch_count > 0) {
            // Emit current batch
            ChunkBatch batch;
            batch.chunks = chunks.subspan(batch_start, batch_count + 1);
            batch.min_offset = min_offset;
            batch.max_end_offset = max_end_offset;
            
            auto result = callback(batch);
            if (!result) {
                return result;
            }
            
            // Start new batch if not at end
            if (i < chunks.size()) {
                batch_start = i;
                batch_count = 0;
                min_offset = chunks[i].offset;
                max_end_offset = chunks[i].offset + chunks[i].byte_count;
                accumulated_size = chunks[i].byte_count;
            }
        }
    }
    
    return Ok();
}

} // namespace tiffconcept
