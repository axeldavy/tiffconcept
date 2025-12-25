#pragma once

#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <thread>
#include <vector>
#include "detail/queue.hpp"
#include "decompressors/decompressor_base.hpp"
#include "lowlevel/decoder.hpp"
#include "lowlevel/tiling.hpp"
#include "image_shape.hpp"
#include "reader_base.hpp"
#include "types/result.hpp"

namespace tiffconcept {

/// @brief Collect tiles that overlap an ImageRegion from ExtractedTags
/// @tparam TagSpec Tag specification type (must contain minimum required tags for image extraction)
/// @param region The region to query tiles for
/// @param metadata Extracted TIFF tags containing image and tile/strip information
/// @param tiles Output vector to fill with tile information (in sorted file offset order)
/// @return Result<void> indicating success or error
/// @retval Success Tiles collected
/// @retval InvalidTag Required tags missing
/// @retval InvalidFormat Pixel type mismatch
/// @retval OutOfBounds Region exceeds image bounds
/// @note If tile tags are present (and not optional or populated), uses tiled mode
/// @note Otherwise falls back to stripped mode if strip tags are available
/// @note Tiles are sorted by FileSpan offset for efficient sequential reading
/// @note TileSize contains the full tile dimensions, not clamped to the region
/// @note For strips, the last strip height is clamped to image height per TIFF spec
/// @note All tiles that overlap the region are included, even partially
/// @note Thread-safe: no shared state, can be called concurrently
template <typename TagSpec>
requires (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
[[nodiscard]] Result<void> collect_tiles_for_region(
    const ImageRegion& region,
    const ExtractedTags<TagSpec>& metadata,
    std::vector<Tile>& tiles) noexcept;

/// @brief Extract a decoded tile to an output buffer with layout conversion
/// @tparam OutSpec Output buffer layout specification (DHWC, DCHW, or CDHW)
/// @tparam PixelType The pixel data type
/// @tparam TagSpec Tag specification type (must contain minimum required tags for image extraction)
/// @param tile The tile being extracted (from collect_tiles_for_region)
/// @param region The image region being read
/// @param metadata Extracted TIFF tags containing image metadata
/// @param decoded_tile Decoded tile data (already decompressed and predictor-decoded)
/// @param output_buffer Output buffer for the entire region
/// @return Result<void> indicating success or error
/// @retval Success Tile extracted and copied successfully
/// @retval InvalidTag Required tags missing
/// @retval InvalidFormat Pixel type mismatch
/// @retval OutOfBounds Output buffer size doesn't match region
/// @retval InvalidOperation Tile doesn't overlap with region
/// 
/// @note Validates output_buffer size matches region.num_samples()
/// @note Calculates overlap between tile and region
/// @note Performs layout conversion from tile format to output format
/// @note Thread-safe: no shared state, can be called concurrently
/// 
/// Example usage:
/// @code
/// // Decode tile separately
/// ChunkDecoder<uint8_t, DecompSpec> decoder; // one per thread
/// auto decoded_result = decoder.decode(compressed_data, tile.id.size.width, 
///                                       tile.id.size.height * tile.id.size.depth,
///                                       compression, predictor, tile.id.size.nsamples);
/// 
/// if (decoded_result) {
///     // Extract to output buffer
///     auto extract_result = extract_tile_to_buffer<ImageLayoutSpec::DHWC, uint8_t>(
///         tile, region, metadata, decoded_result.value(), output_buffer
///     );
/// }
/// @endcode
template <ImageLayoutSpec OutSpec, typename PixelType, typename TagSpec>
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
         (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
[[nodiscard]] inline Result<void> extract_tile_to_buffer(
    const Tile& tile,
    const ImageRegion& region,
    const ExtractedTags<TagSpec>& metadata,
    std::span<const PixelType> decoded_tile,
    std::span<PixelType> output_buffer) noexcept;

// ============================================================================
// Sample Readers
// ============================================================================

/// @brief Minimalistic reader implementation for demonstration purposes.
/// 
/// This reader processes tiles sequentially in a single thread:
/// 1. Read compressed data from file
/// 2. Decode (Decompress + Predictor)
/// 3. Extract to output buffer
///
/// @tparam PixelType The pixel data type
/// @tparam DecompSpec Decompressor specification type
/// @note Not Thread-safe: internal state (tiles_, decoder_) is reused
template <typename PixelType, typename DecompSpec>
class SimpleReader {
public:
    SimpleReader() = default;

    /// @brief Read a region of the image into the output buffer
    template <ImageLayoutSpec OutSpec, typename Reader, typename TagSpec>
    requires RawReader<Reader> && (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
    [[nodiscard]] Result<void> read_region(
        const Reader& reader,
        const ExtractedTags<TagSpec>& metadata,
        const ImageRegion& region,
        std::span<PixelType> output_buffer) noexcept {

        // 1. Identify which tiles are needed
        tiles_.clear();
        auto collect_res = collect_tiles_for_region(region, metadata, tiles_);
        if (!collect_res) return collect_res;

        // 2. Get compression and predictor from tags
        CompressionScheme compression = optional::extract_tag_or<TagCode::Compression, TagSpec>(
            metadata, CompressionScheme::None
        );
        
        Predictor predictor = Predictor::None;
        if constexpr (TagSpec::template has_tag<TagCode::Predictor>()) {
            predictor = optional::extract_tag_or<TagCode::Predictor, TagSpec>(
                metadata, Predictor::None
            );
        }

        // 3. Process each tile sequentially
        for (const auto& tile : tiles_) {
            // Read compressed data
            auto read_res = reader.read(tile.location.offset, tile.location.length);
            if (!read_res) return read_res.error();
            auto compressed_view = std::move(read_res.value());

            // Decode
            // Note: TileDecoder handles decompression and predictor steps
            auto decode_res = decoder_.decode(
                compressed_view.data(),
                tile.id.size.width,
                tile.id.size.height * tile.id.size.depth, // Treat depth as height extension for decoding
                compression,
                predictor,
                tile.id.size.nsamples
            );
            if (!decode_res) return decode_res.error();

            // Extract to output buffer
            auto extract_res = extract_tile_to_buffer<OutSpec, PixelType>(
                tile, region, metadata, decode_res.value(), output_buffer
            );
            if (!extract_res) return extract_res.error();
        }

        return Ok();
    }

private:
    std::vector<Tile> tiles_; // Reused vector for tile list
    TileDecoder<PixelType, DecompSpec> decoder_; // Reused decoder (holds scratch buffer)
};


/// @brief Reader optimized for CPU-bound scenarios (e.g., heavy compression like ZSTD/Deflate).
///
/// Design Philosophy:
/// - Parallel Processing: Uses a persistent thread pool to decode multiple tiles concurrently.
/// - Work Distribution: Each read_region() call partitions its tiles across available workers.
/// - Thread-Local Decoders: Each worker thread maintains its own decoder for cache locality.
///
/// Strategies:
/// - Dynamic Work Queue: Tiles are processed as workers become available.
/// - Independent Workers: Each thread handles Read, Decode, and Extract for assigned tiles.
/// - Per-Job Coordination: Each read_region() call has its own synchronization state.
///
/// Thread-safety:
/// - read_region is thread-safe and can be called concurrently from multiple threads.
/// - Internal worker threads are shared across all calls.
/// - Each worker has its own decoder to avoid contention.
///
/// Performance Characteristics:
/// - Best for: Heavy compression (ZSTD, Deflate) with fast I/O (SSD, memory-mapped)
/// - CPU Utilization: Maximizes parallelism for decompression
/// - Memory: O(decoder_scratch_size × worker_threads)
/// - Not recommended for: High-latency I/O (use IOLimitedReader instead)
///
/// @note Thread-safe: multiple threads can call read_region concurrently
/// @note Allocates thread_local storage for each worker and calling threads.
template <typename PixelType, typename DecompSpec>
class CPULimitedReader {
public:
    struct Config {
        size_t worker_threads = 0; // 0 = auto-detect
    };

    explicit CPULimitedReader(Config config = {});
    ~CPULimitedReader();

    template <ImageLayoutSpec OutSpec, typename Reader, typename TagSpec>
    requires RawReader<Reader> && (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
    [[nodiscard]] Result<void> read_region(
        const Reader& reader,
        const ExtractedTags<TagSpec>& metadata,
        const ImageRegion& region,
        std::span<PixelType> output_buffer) noexcept;

private:
    /// @brief Work item for a single tile
    struct TileTask {
        size_t tile_index;                  // Index into job's tile list
        std::function<Result<void>()> work; // Type-erased tile processing function
    };
    
    /// @brief Shared worker pool task
    using WorkerTask = std::function<void()>;

    Config config_;
    
    // Shared thread pool state
    std::vector<std::thread> threads_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<WorkerTask> pending_tasks_;
    bool stop_threads_ = false;

    void worker_loop();

    /// @brief Per-job state shared between calling thread and worker threads
    /// 
    /// Synchronization strategy:
    /// - mutex protects: tasks_remaining, first_error
    /// - atomics for: error_occurred (fast path check)
    /// - cv notifies calling thread when: all tasks complete or error occurs
    struct JobState {
        std::mutex mutex;                       // Protects tasks_remaining and first_error
        std::condition_variable cv;             // Signals calling thread
        
        // Tracking state
        size_t tasks_remaining{0};              // Number of tiles left to process
        std::atomic<bool> error_occurred{false}; // Fast path: check without lock
        Result<void> first_error = Ok();        // First error encountered (if any)
    };

    template <ImageLayoutSpec OutSpec, typename Reader, typename TagSpec>
    requires RawReader<Reader> && (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
    static void process_tile_task(
        const Reader& reader,
        const ExtractedTags<TagSpec>& metadata,
        const ImageRegion& region,
        std::span<PixelType> output_buffer,
        const std::vector<Tile>& tiles,
        size_t num_tiles_per_thread,
        size_t task_idx,
        std::shared_ptr<JobState> job_state) noexcept;
};

/// @brief Optimal reader combining async I/O with parallel processing
///
/// This is the "ultimate" TIFF reader designed for maximum performance across
/// all storage types: local SSD, NAS, cloud storage.
///
/// Design Philosophy:
/// - Async I/O: Submit all reads upfront using AsyncRawReader interface
/// - Batching: Group adjacent tiles to minimize network round-trips
/// - Parallel Processing: All threads (including caller) process completions concurrently
/// - Non-blocking Workers: Workers poll for completions without blocking on I/O
/// - Thread-Local Decoders: Each thread has its own decoder for cache locality
///
/// Architecture:
/// 1. Main thread collects tiles and creates batches (groups adjacent tiles)
/// 2. Main thread submits ALL async reads upfront (maximizes queue depth)
/// 3. Main thread + workers compete to poll completions and process them
/// 4. Each completion: decode (decompress + predictor) + extract to output
/// 5. Main thread waits until all tiles processed
///
/// Thread Safety:
/// - read_region is thread-safe and can be called concurrently from multiple threads
/// - Each read_region call has isolated state
/// - Workers process completions from any read_region call that's running
/// - Thread-local decoders eliminate contention
///
/// Performance Characteristics:
/// - Best for: ALL scenarios (optimal across storage types)
/// - Local SSD: Maximizes queue depth, parallel decode, near 100% CPU utilization
/// - NAS/Network: Batching reduces round-trips, parallel I/O + decode
/// - Cloud: Aggressive batching, massive parallelism
/// - Memory: O(batch_buffer_size + decoder_scratch × threads)
/// - CPU: Near 100% utilization when decompression is bottleneck
/// - I/O: Maximum bandwidth utilization with sufficient batching
///
/// Configuration:
/// - worker_threads: Processing threads (0 = auto-detect, typically # cores - 1)
/// - max_batch_size: Maximum batch read size (4MB default, increase for high-latency)
/// - max_gap_size: Maximum gap to bridge between tiles (64KB default)
///
/// @note Requires AsyncRawReader (io_uring on Linux, IOCP on Windows)
/// @note Main thread participates in processing (no idle time)
/// @note Workers never block on I/O (only poll completions)
/// @note Thread-safe: multiple threads can call read_region concurrently
///
/// Example:
/// @code
///   #ifdef __linux__
///   IoUringFileReader reader("file.tif");
///   #else
///   IOCPFileReader reader("file.tif");
///   #endif
///   
///   FastReader<uint8_t, DecompSpec> fast_reader;
///   auto result = fast_reader.read_region<ImageLayoutSpec::DHWC>(
///       reader, metadata, region, output_buffer
///   );
/// @endcode
template <typename PixelType, typename DecompSpec>
class FastReader {
public:
    struct Config {
        size_t worker_threads = 0;           ///< Processing threads (0 = auto, typically cores - 1)
        size_t max_batch_size = 0;           ///< Max bytes per batch (0 = auto)
        size_t max_gap_size = 64 * 1024;     ///< Max gap to bridge between tiles
    };

    explicit FastReader(Config config = {});
    ~FastReader();

    /// @brief Read a region using async I/O and parallel processing
    ///
    /// This method submits all reads upfront via async_read_into(), then
    /// all threads (including caller) compete to process completions.
    ///
    /// @tparam OutSpec Output layout (DHWC, DCHW, CDHW)
    /// @tparam Reader Async reader type (must satisfy AsyncRawReader)
    /// @tparam TagSpec Tag specification type
    ///
    /// @param reader Async reader (io_uring or IOCP)
    /// @param metadata Extracted TIFF tags
    /// @param region Region to read
    /// @param output_buffer Output buffer (must be region.num_samples() size)
    ///
    /// @return Ok on success, error otherwise
    ///
    /// Thread-safety: Safe to call concurrently from multiple threads
    template <ImageLayoutSpec OutSpec, typename Reader, typename TagSpec>
    requires AsyncRawReader<Reader> && (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
    [[nodiscard]] Result<void> read_region(
        const Reader& reader,
        const ExtractedTags<TagSpec>& metadata,
        const ImageRegion& region,
        std::span<PixelType> output_buffer) noexcept;

private:
    /// @brief Batch of adjacent tiles for efficient I/O
    struct Batch {
        size_t first_tile_index;  ///< Index of first tile in batch
        size_t tile_count;        ///< Number of tiles in batch
        size_t file_offset;       ///< Starting file offset
        size_t total_size;        ///< Total bytes to read (including gaps)
    };

    /// @brief Tile processing job (for job stealing)
    struct TileJob {
        std::function<void()> process;  ///< Processing function (decode + extract)
        
        TileJob() = default;
        TileJob(std::function<void()> fn) : process(std::move(fn)) {}
        
        operator bool() const { return process != nullptr; }
    };

    using JobQueue = detail::LockFreeJobQueue<TileJob>;

    /// @brief Active read_region job that workers can steal from
    struct ActiveJob {
        std::shared_ptr<JobQueue> job_queue;
        std::atomic<bool> active{true};
        std::condition_variable job_available_cv;  // Wake workers when jobs added
        std::mutex job_cv_mutex;                   // Protects CV wait
    };

    Config config_;
    
    // Worker thread pool (persistent)
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_workers_{false};
    std::condition_variable worker_wake_cv_;  // Wake workers when new job registered
    std::mutex worker_wake_mutex_;

    // Active jobs registry (for job stealing)
    std::shared_mutex active_jobs_mutex_;
    std::vector<std::shared_ptr<ActiveJob>> active_jobs_;


    /// @brief Register a job for worker threads to steal from
    void register_active_job(std::shared_ptr<ActiveJob> job);
    
    /// @brief Unregister a job when read_region completes
    void unregister_active_job(std::shared_ptr<ActiveJob> job);
    
    /// @brief Try to steal a tile from any active job
    bool try_steal_tile(TileJob& out_job);

    void worker_loop();

    /// @brief Per-job state shared between all processing threads
    ///
    /// Synchronization:
    /// - Atomic counters for lock-free fast path
    /// - Mutex only for error tracking
    /// - Main thread + workers all access this concurrently
    struct JobState {
        // Tracking
        std::atomic<size_t> tiles_completed{0};   ///< Tiles processed so far
        std::atomic<size_t> tiles_total{0};       ///< Total tiles to process
        std::atomic<bool> error_occurred{false};  ///< Fast-path error check
        
        // Error handling (protected by mutex)
        std::mutex error_mutex;
        Result<void> first_error = Ok();
        
        // Synchronization for main thread
        std::mutex completion_mutex;
        std::condition_variable completion_cv;
    };

    /// @brief Create batches from tiles (static helper)
    static void create_batches(
        const std::vector<Tile>& tiles, 
        const Config& config, 
        std::size_t max_batch_size_hint,
        std::vector<Batch>& out_batches);
        
    /// @brief Submit async read operations for batches
    ///
    /// Allocates memory for each batch separately and submits async reads.
    /// Each batch gets its own shared_ptr that will be shared among tile jobs.
    ///
    /// @param reader Async reader
    /// @param batches All batches
    /// @param next_batch_idx Next batch index to submit (updated)
    /// @param submission_handles Handle array to store results
    /// @param storage_buffer Vector of shared_ptrs for batch storage (indexed by batch_idx)
    ///
    /// @return Ok on success, error on failure
    template <typename Reader>
    requires AsyncRawReader<Reader>
    static Result<void> submit_batches(
        const Reader& reader,
        const std::vector<Batch>& batches,
        std::size_t& next_batch_idx,
        std::vector<uint64_t>& submission_handles,
        std::vector<std::shared_ptr<std::byte[]>>& storage_buffer) noexcept;

    
    /// @brief Get thread-local decoder instance
    static TileDecoder<PixelType, DecompSpec>& get_decoder() noexcept;

    /// @brief Process a single tile (decode + extract)
    ///
    /// This function is called by both main thread and worker threads.
    /// Each thread uses its own thread-local decoder.
    ///
    /// @param tile Tile to process
    /// @param compressed_data Compressed tile data
    /// @param metadata TIFF metadata
    /// @param region Image region
    /// @param output_buffer Output buffer
    ///
    /// @return Ok on success, error otherwise
    template <ImageLayoutSpec OutSpec, typename TagSpec>
    requires (TiledImageTagSpec<TagSpec> || StrippedImageTagSpec<TagSpec>)
    static Result<void> process_tile(
        const Tile& tile,
        const ExtractedTags<TagSpec>& metadata,
        const ImageRegion& region,
        std::span<PixelType> output_buffer,
        std::span<const std::byte> compressed_data) noexcept;
};

} // namespace tiffconcept

#define TIFFCONCEPT_IMAGE_READER_HEADER
#include "impl/image_reader_impl.hpp"