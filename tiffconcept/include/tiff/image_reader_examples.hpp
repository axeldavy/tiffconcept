#pragma once

// Example usage of the strategy-based image reader architecture
// This file demonstrates various reader configurations

#include "tiff/image_reader_variants.hpp"
#include "tiff/metadata.hpp"
#include "tiff/tag_spec.hpp"

namespace tiffconcept::examples {

/// Example 1: Fast local storage reading
/// Best for: SSD, fast local disks, small files
template <typename Reader>
void example_fast_reading(const Reader& file_reader) {
    // Create a fast reader with minimal overhead
    auto reader = make_fast_reader<uint16_t>();
    
    // Extract metadata and create image info
    // ... (metadata extraction code)
    
    TiledImageInfo<uint16_t> image_info;
    // image_info.update_from_metadata(metadata);
    
    // Read the full image
    auto region = image_info.shape().full_region();
    std::vector<uint16_t> pixels(region.num_samples());
    
    auto result = reader.read_region<OutputSpec::DHWC>(
        file_reader, image_info, pixels, region
    );
    
    // Process pixels...
}

/// Example 2: High-latency I/O (network, cloud storage)
/// Best for: S3, Azure Blob, network shares, high-latency storage
template <typename Reader>
void example_high_latency_reading(const Reader& file_reader) {
    // Create a reader optimized for high-latency I/O
    // Batches reads to reduce round-trips
    auto reader = make_high_latency_reader<uint16_t>(
        1024 * 1024,    // 1MB minimum batch
        256 * 1024,     // 256KB holes allowed
        4 * 1024 * 1024 // 4MB max span
    );
    
    TiledImageInfo<uint16_t> image_info;
    // ... setup image_info
    
    auto region = image_info.shape().full_region();
    std::vector<uint16_t> pixels(region.num_samples());
    
    auto result = reader.read_region<OutputSpec::DHWC>(
        file_reader, image_info, pixels, region
    );
    
    // Process pixels...
}

/// Example 3: Parallel I/O for high-latency storage
/// Best for: Cloud storage where parallel requests improve throughput
template <typename Reader>
void example_parallel_io(const Reader& file_reader) {
    // Create a reader with parallel I/O
    // Multiple threads read chunks simultaneously
    auto reader = make_parallel_reader<uint16_t>(8);
    
    TiledImageInfo<uint16_t> image_info;
    // ... setup image_info
    
    auto region = image_info.shape().full_region();
    std::vector<uint16_t> pixels(region.num_samples());
    
    auto result = reader.read_region<OutputSpec::DHWC>(
        file_reader, image_info, pixels, region
    );
    
    // Process pixels...
}

/// Example 4: Custom configuration
/// Build your own combination of read strategy
template <typename Reader>
void example_custom_reader(const Reader& file_reader) {
    // Custom batching parameters for moderate latency
    auto custom_batching = BatchingParams{
        .min_batch_size = 256 * 1024,   // 256KB minimum
        .max_hole_size = 32 * 1024,     // 32KB holes
        .max_batch_span = 1024 * 1024   // 1MB max span
    };
    
    // Create custom reader with specific read strategy
    // Decoding happens inline in the read callback
    auto reader = make_custom_reader<uint16_t, StandardDecompSpec>(
        BatchedReader{custom_batching}
    );
    
    TiledImageInfo<uint16_t> image_info;
    // ... setup image_info
    
    auto region = image_info.shape().full_region();
    std::vector<uint16_t> pixels(region.num_samples());
    
    auto result = reader.read_region<OutputSpec::DHWC>(
        file_reader, image_info, pixels, region
    );
    
    // Process pixels...
}

/// Example 5: Processing many images efficiently
/// Reuses internal buffers across multiple images
template <typename Reader>
void example_batch_processing(const std::vector<Reader>& file_readers) {
    // Create reader once
    auto reader = make_high_latency_reader<uint16_t>();
    
    // Process many images
    for (const auto& file_reader : file_readers) {
        TiledImageInfo<uint16_t> image_info;
        // ... setup image_info for this file
        
        auto region = image_info.shape().full_region();
        std::vector<uint16_t> pixels(region.num_samples());
        
        // Internal buffers are reused across calls
        auto result = reader.read_region<OutputSpec::DHWC>(
            file_reader, image_info, pixels, region
        );
        
        if (!result) {
            // Handle error
            continue;
        }
        
        // Process pixels...
    }
    
    // Optionally free memory when done
    reader.clear();
}

/// Example 7: Runtime reconfiguration
/// Change reader behavior at runtime
template <typename Reader>
void example_runtime_config(const Reader& file_reader) {
    auto reader = make_high_latency_reader<uint16_t>();
    
    // Start with high-latency preset
    // ... read some images
    
    // Switch to more aggressive batching
    reader.read_strategy().set_params(BatchingParams::all_at_once());
    
    // Continue reading with new settings
    TiledImageInfo<uint16_t> image_info;
    auto region = image_info.shape().full_region();
    std::vector<uint16_t> pixels(region.num_samples());
    
    auto result = reader.read_region<OutputSpec::DHWC>(
        file_reader, image_info, pixels, region
    );
}

/// Example 8: Different output layouts
/// Read into different memory layouts
template <typename Reader>
void example_output_layouts(const Reader& file_reader) {
    auto reader = make_fast_reader<uint16_t>();
    
    TiledImageInfo<uint16_t> image_info;
    auto region = image_info.shape().full_region();
    
    // DHWC layout (depth, height, width, channels)
    std::vector<uint16_t> dhwc_pixels(region.num_samples());
    reader.read_region<OutputSpec::DHWC>(
        file_reader, image_info, dhwc_pixels, region
    );
    
    // DCHW layout (depth, channels, height, width)
    std::vector<uint16_t> dchw_pixels(region.num_samples());
    reader.read_region<OutputSpec::DCHW>(
        file_reader, image_info, dchw_pixels, region
    );
    
    // CDHW layout (channels, depth, height, width)
    std::vector<uint16_t> cdhw_pixels(region.num_samples());
    reader.read_region<OutputSpec::CDHW>(
        file_reader, image_info, cdhw_pixels, region
    );
}

/// Example 9: Reading partial regions
/// Read only a subset of the image
template <typename Reader>
void example_partial_region(const Reader& file_reader) {
    auto reader = make_fast_reader<uint16_t>();
    
    TiledImageInfo<uint16_t> image_info;
    
    // Read a 512x512 region starting at (100, 100)
    // Read channels 0-2 only
    ImageRegion region(
        0,      // start_channel
        0,      // start_z
        100,    // start_y
        100,    // start_x
        3,      // num_channels
        1,      // depth
        512,    // height
        512     // width
    );
    
    std::vector<uint16_t> pixels(region.num_samples());
    
    auto result = reader.read_region<OutputSpec::DHWC>(
        file_reader, image_info, pixels, region
    );
}

/// Example 10: Stripped images
/// Works the same way as tiled images
template <typename Reader>
void example_stripped_image(const Reader& file_reader) {
    auto reader = make_fast_reader<uint16_t>();
    
    StrippedImageInfo<uint16_t> image_info;
    // ... setup image_info from metadata
    
    auto region = image_info.shape().full_region();
    std::vector<uint16_t> pixels(region.num_samples());
    
    // Same API for stripped images
    auto result = reader.read_region<OutputSpec::DHWC>(
        file_reader, image_info, pixels, region
    );
}

} // namespace tiffconcept::examples
