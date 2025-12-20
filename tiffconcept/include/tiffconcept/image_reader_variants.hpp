#pragma once

#include "decompressor_base.hpp"
#include "decompressors/decompressor_standard.hpp"
#include "image_reader.hpp"
#include "strategy/read_strategy.hpp"

namespace tiffconcept {

/// Default decompressor spec with standard compression schemes
using StandardDecompSpec = DecompressorSpec<NoneDecompressorDesc, PackBitsDecompressorDesc>;

/// Common image reader configurations for different use cases
/// Decoding happens in the read callback (producer/consumer pattern)

/// Fast single-threaded reader with minimal overhead
/// Best for: Local fast storage, small images, low CPU overhead
/// Reads and decodes chunks one by one
template <typename PixelType, typename DecompSpec = StandardDecompSpec>
using FastReader = ImageReader<
    PixelType,
    DecompSpec,
    SingleThreadedReader
>;

/// Batched reader for high-latency I/O
/// Best for: Network storage, cloud storage, remote files
/// Groups nearby chunks to reduce I/O operations, decodes inline
template <typename PixelType, typename DecompSpec = StandardDecompSpec>
using HighLatencyReader = ImageReader<
    PixelType,
    DecompSpec,
    BatchedReader
>;

/// Multi-threaded reader for parallel I/O
/// Best for: High-latency I/O where parallel requests improve throughput
/// Multiple threads read chunks in parallel, decoding happens in callback
template <typename PixelType, typename DecompSpec = StandardDecompSpec>
using ParallelReader = ImageReader<
    PixelType,
    DecompSpec,
    MultiThreadedReader
>;

/// Factory functions for common configurations

/// Create a fast single-threaded reader (minimal overhead)
template <typename PixelType, typename DecompSpec = StandardDecompSpec>
[[nodiscard]] inline FastReader<PixelType, DecompSpec> make_fast_reader() noexcept {
    return FastReader<PixelType, DecompSpec>(SingleThreadedReader{});
}

/// Create a reader optimized for high-latency I/O
/// @param min_batch_size Minimum bytes to batch together
/// @param max_hole_size Maximum gap between chunks to still batch them
/// @param max_batch_span Maximum file span for a single batch (0 = unlimited)
template <typename PixelType, typename DecompSpec = StandardDecompSpec>
[[nodiscard]] inline HighLatencyReader<PixelType, DecompSpec> make_high_latency_reader(
    std::size_t min_batch_size = 1024 * 1024,
    std::size_t max_hole_size = 256 * 1024,
    std::size_t max_batch_span = 4 * 1024 * 1024) noexcept {
    
    return HighLatencyReader<PixelType, DecompSpec>(
        BatchedReader{BatchingParams{min_batch_size, max_hole_size, max_batch_span}}
    );
}

/// Create a reader with parallel I/O
/// @param num_io_threads Number of I/O threads (0 = hardware concurrency)
template <typename PixelType, typename DecompSpec = StandardDecompSpec>
[[nodiscard]] inline ParallelReader<PixelType, DecompSpec> make_parallel_reader(
    std::size_t num_io_threads = 0) noexcept {
    
    return ParallelReader<PixelType, DecompSpec>(
        MultiThreadedReader{num_io_threads}
    );
}

/// Create a custom reader with a specific read strategy
template <typename PixelType, typename DecompSpec, typename ReadStrat>
[[nodiscard]] inline ImageReader<PixelType, DecompSpec, ReadStrat>
make_custom_reader(ReadStrat read_strategy) noexcept {
    return ImageReader<PixelType, DecompSpec, ReadStrat>(std::move(read_strategy));
}

} // namespace tiffconcept
