#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include <vector>
#include "result.hpp"
#include "types.hpp"

namespace tiffconcept {

// Forward declarations
struct ChunkWriteInfo;
template <typename T> class WriteContext;

/// Information about a chunk to be written
struct ChunkWriteInfo {
    uint32_t chunk_index;           // Linear index of chunk
    uint32_t pixel_x, pixel_y, pixel_z;  // Position in image
    uint32_t width, height, depth;       // Chunk dimensions
    uint16_t plane;                      // For planar configuration
    std::size_t uncompressed_size;       // Size before compression
    std::size_t compressed_size;         // Size after compression (0 if not yet known)
    std::size_t file_offset;             // Offset in file (0 if not yet known)

    bool operator==(const ChunkWriteInfo&) const = default;
};

/// Encoded chunk data ready to write
struct EncodedChunk {
    ChunkWriteInfo info;
    std::vector<std::byte> data;  // Compressed and encoded data
};

/// Context passed to strategies during writing operations
/// Provides access to computed metadata and state
template <typename Writer>
class WriteContext {
public:
    Writer& writer;
    std::vector<ChunkWriteInfo>& chunk_infos;  // Metadata for all chunks
    std::size_t& current_file_position;        // Current write position
    
    WriteContext(Writer& w, std::vector<ChunkWriteInfo>& infos, std::size_t& pos) noexcept
        : writer(w), chunk_infos(infos), current_file_position(pos) {}
};

/// ========================================================================
/// IFD Placement Strategy Concept
/// ========================================================================
/// Determines where to place IFD arrays (main array and external data arrays)
/// in the file relative to image data
template <typename T>
concept IFDPlacementStrategy = requires(const T& strategy) {
    // Calculate where the IFD should be placed given the current file state
    // Returns the offset where IFD should be written
    // Parameters: current_file_size, ifd_size, external_data_size
    { strategy.calculate_ifd_offset(std::size_t{}, std::size_t{}, std::size_t{}) } 
        -> std::same_as<std::size_t>;
    
    // Calculate where external data arrays (TileOffsets, TileByteCounts, etc) should be placed
    // Returns the offset where external data should be written
    // Parameters: current_file_size, ifd_offset, ifd_size, external_data_size
    { strategy.calculate_external_data_offset(std::size_t{}, std::size_t{}, std::size_t{}, std::size_t{}) }
        -> std::same_as<std::size_t>;
    
    // Should image data be written before or after IFD?
    { T::write_data_before_ifd } -> std::convertible_to<bool>;
};

/// ========================================================================
/// Tile Ordering Strategy Concept
/// ========================================================================
/// Determines the order in which tiles/strips should be written
template <typename T>
concept TileOrderingStrategy = requires(const T& strategy, std::span<ChunkWriteInfo> chunks) {
    // Reorder chunks according to strategy
    // Modifies the chunks span in-place to represent the desired write order
    { strategy.order_chunks(chunks) } -> std::same_as<void>;
    
    // Can this strategy benefit from parallel chunk encoding?
    { T::supports_parallel_encoding } -> std::convertible_to<bool>;
};

/// ========================================================================
/// Buffering Strategy Concept
/// ========================================================================
/// Determines how data is buffered before writing to the final destination
template <typename T, typename Writer>
concept BufferingStrategy = requires(T& strategy, Writer& writer, std::size_t offset, std::span<const std::byte> data) {
    // Write data according to buffering strategy
    { strategy.write(writer, offset, data) } -> std::same_as<Result<void>>;
    
    // Flush any buffered data to the writer
    { strategy.flush(writer) } -> std::same_as<Result<void>>;
    
    // Clear any cached state for reuse
    { strategy.clear() } -> std::same_as<void>;
    
    // Does this strategy maintain a temporary buffer?
    { T::uses_temporary_buffer } -> std::convertible_to<bool>;
};

/// ========================================================================
/// Offset Resolution Strategy Concept
/// ========================================================================
/// Determines when and how tile/strip offsets are resolved
template <typename T>
concept OffsetResolutionStrategy = requires(const T& strategy) {
    // Should offsets be calculated before any data is written (two-pass)?
    // If true, all chunk sizes must be known upfront
    { T::requires_size_precalculation } -> std::convertible_to<bool>;
    
    // Should offsets be written immediately as each chunk is written?
    // If false, offsets are collected and written later
    { T ::write_offsets_immediately } -> std::convertible_to<bool>;
    
    // Can support streaming mode where final positions aren't known until flush?
    { T::supports_streaming } -> std::convertible_to<bool>;
};

/// ========================================================================
/// Write Configuration Template
/// ========================================================================
/// Combines orthogonal strategies into a complete write configuration
template <
    typename IFDPlacement,
    typename TileOrdering, 
    typename Buffering,
    typename OffsetResolution
>
    requires IFDPlacementStrategy<IFDPlacement> &&
             TileOrderingStrategy<TileOrdering> &&
             OffsetResolutionStrategy<OffsetResolution>
struct WriteConfig {
    using ifd_placement_strategy = IFDPlacement;
    using tile_ordering_strategy = TileOrdering;
    using buffering_strategy = Buffering;
    using offset_resolution_strategy = OffsetResolution;
    
    // Compile-time validation of strategy compatibility
    static_assert(
        !OffsetResolution::requires_size_precalculation || 
        !Buffering::uses_temporary_buffer ||
        OffsetResolution::supports_streaming,
        "Streaming buffering requires offset resolution strategy that supports streaming"
    );
    
    static_assert(
        !IFDPlacement::write_data_before_ifd || 
        !OffsetResolution::requires_size_precalculation ||
        OffsetResolution::supports_streaming,
        "Writing data before IFD requires size precalculation or streaming support"
    );
};

/// ========================================================================
/// IFD Placement Strategies - Concrete Implementations
/// ========================================================================

/// Place IFD at the beginning of the file (after header)
/// External data immediately follows IFD
/// Image data follows external data
/// Optimized for reading: metadata is cache-local
struct IFDAtBeginning {
    static constexpr bool write_data_before_ifd = false; // IFD first, then data

    [[nodiscard]] constexpr std::size_t calculate_ifd_offset(
        std::size_t current_file_size, 
        [[maybe_unused]] std::size_t ifd_size,
        [[maybe_unused]] std::size_t external_data_size) const noexcept {
        // IFD goes right after header (we'll write it at position 8 for classic, 16 for BigTIFF)
        // For simplicity, strategies work with file positions, caller handles header
        return current_file_size;
    }
    
    [[nodiscard]] constexpr std::size_t calculate_external_data_offset(
        [[maybe_unused]] std::size_t current_file_size,
        std::size_t ifd_offset,
        std::size_t ifd_size,
        [[maybe_unused]] std::size_t external_data_size) const noexcept {
        // External data immediately follows IFD
        return ifd_offset + ifd_size;
    }
};

/// Place IFD at the end of the file (after all image data)
/// External data immediately follows IFD
/// Optimized for writing: can write data as it's encoded
struct IFDAtEnd {
    static constexpr bool write_data_before_ifd = true; // Data first, then IFD

    [[nodiscard]] constexpr std::size_t calculate_ifd_offset(
        std::size_t current_file_size,
        [[maybe_unused]] std::size_t ifd_size,
        [[maybe_unused]] std::size_t external_data_size) const noexcept {
        // IFD goes after all existing data
        return current_file_size;
    }
    
    [[nodiscard]] constexpr std::size_t calculate_external_data_offset(
        [[maybe_unused]] std::size_t current_file_size,
        std::size_t ifd_offset,
        std::size_t ifd_size,
        [[maybe_unused]] std::size_t external_data_size) const noexcept {
        // External data immediately follows IFD
        return ifd_offset + ifd_size;
    }
};

/// Place IFD inline with data based on natural write order
/// For editing: try to reuse existing IFD location if possible
struct IFDInline {
    static constexpr bool write_data_before_ifd = true; // Flexible, default to data first
    std::size_t preferred_offset = 0;  // Preferred location (e.g., existing IFD offset)
    
    [[nodiscard]] std::size_t calculate_ifd_offset(
        std::size_t current_file_size,
        [[maybe_unused]] std::size_t ifd_size,
        [[maybe_unused]] std::size_t external_data_size) const noexcept {
        // Use preferred offset if set, otherwise end of file
        return (preferred_offset != 0) ? preferred_offset : current_file_size;
    }
    
    [[nodiscard]] std::size_t calculate_external_data_offset(
        [[maybe_unused]] std::size_t current_file_size,
        std::size_t ifd_offset,
        std::size_t ifd_size,
        [[maybe_unused]] std::size_t external_data_size) const noexcept {
        // External data immediately follows IFD
        return ifd_offset + ifd_size;
    }
};

/// ========================================================================
/// Tile Ordering Strategies - Concrete Implementations
/// ========================================================================

/// Write tiles in image order (row-major, then plane for planar config)
/// Optimizes for sequential reading
struct ImageOrderTiles {
    static constexpr bool supports_parallel_encoding = true; // Can encode in parallel

    void order_chunks(std::span<ChunkWriteInfo> chunks) const noexcept {
        // Sort by: z, plane, y, x
        std::sort(chunks.begin(), chunks.end(), 
            [](const ChunkWriteInfo& a, const ChunkWriteInfo& b) {
                if (a.pixel_z != b.pixel_z) return a.pixel_z < b.pixel_z;
                if (a.plane != b.plane) return a.plane < b.plane;
                if (a.pixel_y != b.pixel_y) return a.pixel_y < b.pixel_y;
                return a.pixel_x < b.pixel_x;
            });
    }
};

/// Write tiles in sequential order (as they were encoded)
/// Optimizes for writing speed
struct SequentialTiles {
    static constexpr bool supports_parallel_encoding = false; // Sequential encoding and writing

    void order_chunks(std::span<ChunkWriteInfo> chunks) const noexcept {
        // Keep existing order (chunk_index order)
        std::sort(chunks.begin(), chunks.end(),
            [](const ChunkWriteInfo& a, const ChunkWriteInfo& b) {
                return a.chunk_index < b.chunk_index;
            });
    }
};

/// Write tiles on-demand in arbitrary order
/// Used for editing where only specific tiles are updated
struct OnDemandTiles {
    static constexpr bool supports_parallel_encoding = true; // Can encode in parallel

    void order_chunks([[maybe_unused]] std::span<ChunkWriteInfo> chunks) const noexcept {
        // No reordering - use whatever order chunks are provided
    }
};

/// ========================================================================
/// Buffering Strategies - Concrete Implementations
/// ========================================================================

/// Write directly to the target writer without intermediate buffering
template <typename Writer>
class DirectWrite {
public:
    static constexpr bool uses_temporary_buffer = false; // No temporary buffer

    DirectWrite() noexcept = default;
    
    [[nodiscard]] Result<void> write(Writer& writer, std::size_t offset, std::span<const std::byte> data) noexcept {
        // Ensure writer is large enough
        auto size_result = writer.size();
        if (!size_result) {
            return Err(size_result.error().code, "DirectWrite: failed to get writer size");
        }
        
        std::size_t required_size = offset + data.size();
        if (size_result.value() < required_size) {
            auto resize_result = writer.resize(required_size);
            if (!resize_result) {
                return Err(resize_result.error().code, "DirectWrite: failed to resize writer");
            }
        }
        
        auto view_result = writer.write(offset, data.size());
        if (!view_result) {
            return Err(view_result.error().code, "DirectWrite: failed to get write view");
        }
        
        auto view = std::move(view_result.value());
        std::memcpy(view.data().data(), data.data(), data.size());
        
        return view.flush();
    }
    
    [[nodiscard]] Result<void> flush(Writer& writer) noexcept {
        return writer.flush();
    }
    
    void clear() noexcept {
        // No state to clear
    }
};

/// Buffer writes in memory, then flush to writer in larger chunks
/// Reduces number of I/O operations
template <typename Writer>
class BufferedWrite {
private:
    std::vector<std::byte> buffer_;
    std::size_t buffer_offset_ = 0;
    std::size_t min_flush_size_ = 64 * 1024;  // Flush when buffer reaches 64KB
    
public:
    static constexpr bool uses_temporary_buffer = true; // Maintains temporary buffer

    explicit BufferedWrite(std::size_t min_flush_size = 64 * 1024) noexcept 
        : min_flush_size_(min_flush_size) {}
    
    [[nodiscard]] Result<void> write(Writer& writer, std::size_t offset, std::span<const std::byte> data) noexcept {
        // For simplicity, flush buffer if write is non-contiguous
        if (!buffer_.empty() && offset != buffer_offset_ + buffer_.size()) {
            auto flush_result = flush_internal(writer);
            if (!flush_result) {
                return flush_result;
            }
        }
        
        // Start new buffer if empty
        if (buffer_.empty()) {
            buffer_offset_ = offset;
        }
        
        // Append to buffer
        buffer_.insert(buffer_.end(), data.begin(), data.end());
        
        // Flush if buffer is large enough
        if (buffer_.size() >= min_flush_size_) {
            return flush_internal(writer);
        }
        
        return Ok();
    }
    
    [[nodiscard]] Result<void> flush(Writer& writer) noexcept {
        auto result = flush_internal(writer);
        if (!result) {
            return result;
        }
        return writer.flush();
    }
    
    void clear() noexcept {
        buffer_.clear();
        buffer_offset_ = 0;
    }
    
private:
    [[nodiscard]] Result<void> flush_internal(Writer& writer) noexcept {
        if (buffer_.empty()) {
            return Ok();
        }
        
        // Ensure writer is large enough before writing
        auto size_result = writer.size();
        if (!size_result) {
            return Err(size_result.error().code, "BufferedWrite: failed to get writer size");
        }
        
        std::size_t required_size = buffer_offset_ + buffer_.size();
        if (size_result.value() < required_size) {
            auto resize_result = writer.resize(required_size);
            if (!resize_result) {
                return Err(resize_result.error().code, "BufferedWrite: failed to resize writer");
            }
        }
        
        auto view_result = writer.write(buffer_offset_, buffer_.size());
        if (!view_result) {
            return Err(view_result.error().code, "BufferedWrite: failed to get write view");
        }
        
        auto view = std::move(view_result.value());
        std::memcpy(view.data().data(), buffer_.data(), buffer_.size());
        
        auto flush_result = view.flush();
        if (!flush_result) {
            return flush_result;
        }
        
        buffer_.clear();
        buffer_offset_ = 0;
        
        return Ok();
    }
};

/// Write to a temporary in-memory buffer, then flush entire buffer at once
/// Enables calculating all offsets before any file I/O
template <typename Writer>
class StreamingWrite {
private:
    std::vector<std::byte> temp_buffer_;
    std::size_t base_offset_ = 0;
    
public:
    static constexpr bool uses_temporary_buffer = true; // Maintains temporary buffer

    StreamingWrite() noexcept = default;
    
    // Set base offset (where the temp buffer will be written in the file)
    void set_base_offset(std::size_t offset) noexcept {
        base_offset_ = offset;
    }
    
    [[nodiscard]] Result<void> write([[maybe_unused]] Writer& writer, std::size_t offset, std::span<const std::byte> data) noexcept {
        // Offset is relative to base_offset
        std::size_t relative_offset = offset - base_offset_;
        
        // Grow buffer if needed
        std::size_t required_size = relative_offset + data.size();
        if (temp_buffer_.size() < required_size) {
            temp_buffer_.resize(required_size);
        }
        
        // Copy data to temp buffer
        std::memcpy(temp_buffer_.data() + relative_offset, data.data(), data.size());
        
        return Ok();
    }
    
    [[nodiscard]] Result<void> flush(Writer& writer) noexcept {
        if (temp_buffer_.empty()) {
            return Ok();
        }
        
        // Ensure writer is large enough for base_offset + buffer
        auto size_result = writer.size();
        if (!size_result) {
            return Err(size_result.error().code, "StreamingWrite: failed to get writer size");
        }
        
        std::size_t required_size = base_offset_ + temp_buffer_.size();
        if (size_result.value() < required_size) {
            auto resize_result = writer.resize(required_size);
            if (!resize_result) {
                return Err(resize_result.error().code, "StreamingWrite: failed to resize writer");
            }
        }
        
        // Write entire buffer to file
        auto view_result = writer.write(base_offset_, temp_buffer_.size());
        if (!view_result) {
            return Err(view_result.error().code, "StreamingWrite: failed to get write view");
        }
        
        auto view = std::move(view_result.value());
        std::memcpy(view.data().data(), temp_buffer_.data(), temp_buffer_.size());
        
        auto flush_result = view.flush();
        if (!flush_result) {
            return flush_result;
        }
        
        return writer.flush();
    }
    
    void clear() noexcept {
        temp_buffer_.clear();
        base_offset_ = 0;
    }
    
    // Get current size of temporary buffer
    [[nodiscard]] std::size_t buffer_size() const noexcept {
        return temp_buffer_.size();
    }
};

/// ========================================================================
/// Offset Resolution Strategies - Concrete Implementations
/// ========================================================================

/// Calculate all offsets in first pass, then write data in second pass
/// Enables optimal layout but requires knowing all sizes upfront
struct TwoPassOffsets {
    static constexpr bool requires_size_precalculation = true;
    static constexpr bool supports_streaming = true;  // Can work with streaming buffer
    static constexpr bool write_offsets_immediately = false;  // Offsets written after data
};

/// Calculate offsets lazily as chunks are written
/// Uses placeholders that are patched later
struct LazyOffsets {
    static constexpr bool requires_size_precalculation = false;
    static constexpr bool supports_streaming = false;  // Needs to patch file
    static constexpr bool write_offsets_immediately = false;  // Offsets patched later
};

/// Write offsets immediately as each chunk is written
/// For editing mode where offsets are known
struct ImmediateOffsets {
    static constexpr bool requires_size_precalculation = false;
    static constexpr bool supports_streaming = false;  // Needs to patch file
    static constexpr bool write_offsets_immediately = true;
};

/// ========================================================================
/// Predefined Write Configurations
/// ========================================================================

/// Optimized for reading: IFD at beginning, tiles in image order, two-pass
template <typename Writer>
using OptimizedForReadingConfig = WriteConfig<
    IFDAtBeginning,
    ImageOrderTiles,
    StreamingWrite<Writer>,
    TwoPassOffsets
>;

/// Optimized for writing: IFD at end, sequential tiles, direct write
template <typename Writer>
using OptimizedForWritingConfig = WriteConfig<
    IFDAtEnd,
    SequentialTiles,
    DirectWrite<Writer>,
    LazyOffsets
>;

/// Streaming mode: buffered writes with two-pass offsets
template <typename Writer>
using StreamingConfig = WriteConfig<
    IFDAtEnd,
    SequentialTiles,
    StreamingWrite<Writer>,
    TwoPassOffsets
>;

/// Editing mode: inline IFD, on-demand tiles, direct write
template <typename Writer>
using EditInPlaceConfig = WriteConfig<
    IFDInline,
    OnDemandTiles,
    DirectWrite<Writer>,
    ImmediateOffsets
>;

} // namespace tiffconcept
