#pragma once

#include <cstdint>
#include <filesystem>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "../tiffconcept/include/tiffconcept/strategy/write_strategy.hpp"

namespace tiff_bench {

/// Endianness for TIFF files
enum class Endianness { Little, Big };

/// TIFF file format type
enum class TiffFormat { Classic, BigTIFF };

/// Compression type enumeration
enum class CompressionType { None, ZSTD };

/// Predictor type enumeration  
enum class PredictorType { None, Horizontal };

/// Read strategy type
enum class ReadStrategy { Fast, HighLatency, Parallel };

/// Write strategy type
enum class WriteStrategy { IFDAtEnd, IFDAtStart };

/// Tile ordering type
enum class TileOrdering { Sequential, Hilbert };

/// Test image configuration
struct ImageConfig {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint16_t samples_per_pixel;
    uint32_t tile_width;
    uint32_t tile_height; // use for rows_per_strip if use_strips is true
    uint32_t tile_depth = 1;
    
    std::string name() const;
    std::size_t num_pixels() const {
        return static_cast<std::size_t>(width) * height * depth * samples_per_pixel;
    }
    std::size_t num_bytes(std::size_t bytes_per_pixel) const {
        return num_pixels() * bytes_per_pixel;
    }
};

/// Test image storage type
struct StorageConfig {
    bool use_strips = false;
    bool read_optimized = true;
    CompressionType compression = CompressionType::None;
    PredictorType predictor = PredictorType::None;
    Endianness endianness = Endianness::Little;
    TiffFormat format = TiffFormat::Classic;
};

/// Benchmark parameter configuration
struct BenchmarkParams {
    ImageConfig image_config;
    StorageConfig storage_config;
    ReadStrategy read_strategy = ReadStrategy::Fast;
    WriteStrategy write_strategy = WriteStrategy::IFDAtEnd;
    TileOrdering tile_ordering = TileOrdering::Sequential;
    uint32_t num_pages = 1;  // For multi-page files
    
    std::string name() const;
};

/// Predefined image configurations for benchmarking
namespace configs {
    // Small images (quick tests)
    constexpr ImageConfig small_single_channel{64, 64, 1, 1, 32, 32};
    constexpr ImageConfig small_rgb{64, 64, 1, 3, 32, 32};
    
    // Medium images (typical use cases)
    constexpr ImageConfig medium_single_channel{512, 512, 1, 1, 128, 128};
    constexpr ImageConfig medium_rgb{512, 512, 1, 3, 128, 128};
    constexpr ImageConfig medium_rgba{512, 512, 1, 4, 128, 128};
    
    // Large images (performance stress tests)
    constexpr ImageConfig large_single_channel{2048, 2048, 1, 1, 256, 256};
    constexpr ImageConfig large_rgb{2048, 2048, 1, 3, 256, 256};
    constexpr ImageConfig large_rgba{2048, 2048, 1, 4, 256, 256};
    
    // Multi-channel (scientific imaging)
    constexpr ImageConfig multi_channel_8{512, 512, 1, 8, 128, 128};
    constexpr ImageConfig multi_channel_16{512, 512, 1, 16, 128, 128};
    constexpr ImageConfig multi_channel_32{512, 512, 1, 32, 128, 128};
    
    // 3D volumetric
    constexpr ImageConfig volume_small{256, 256, 32, 1, 128, 128, 16};
    constexpr ImageConfig volume_medium{512, 512, 64, 1, 128, 128, 16};

    // Strips
    constexpr StorageConfig simple_strips{true, true, CompressionType::None, PredictorType::None, Endianness::Little, TiffFormat::Classic};
    constexpr StorageConfig zstd_strips{true, true, CompressionType::ZSTD, PredictorType::Horizontal, Endianness::Little, TiffFormat::Classic};
    constexpr StorageConfig bigtiff_strips{true, true, CompressionType::None, PredictorType::None, Endianness::Little, TiffFormat::BigTIFF};
    constexpr StorageConfig bigendian_strips{true, true, CompressionType::None, PredictorType::None, Endianness::Big, TiffFormat::Classic};
    constexpr StorageConfig zstd_bigendian_strips{true, true, CompressionType::ZSTD, PredictorType::Horizontal, Endianness::Big, TiffFormat::Classic};
    // Tiles
    constexpr StorageConfig simple_tiles{false, true, CompressionType::None, PredictorType::None, Endianness::Little, TiffFormat::Classic};
    constexpr StorageConfig zstd_tiles{false, true, CompressionType::ZSTD, PredictorType::Horizontal, Endianness::Little, TiffFormat::Classic};
    constexpr StorageConfig bigtiff_tiles{false, true, CompressionType::None, PredictorType::None, Endianness::Little, TiffFormat::BigTIFF};
    constexpr StorageConfig bigendian_tiles{false, true, CompressionType::None, PredictorType::None, Endianness::Big, TiffFormat::Classic};
    constexpr StorageConfig zstd_bigendian_tiles{false, true, CompressionType::ZSTD, PredictorType::Horizontal, Endianness::Big, TiffFormat::Classic};
    // Number of pages for multi-page benchmarks
    constexpr std::array<int, 3> num_pages_options = {1, 10, 100};
}

enum ImagePattern {
    Gradient,
    Random,
    Constant
};

/// Image data generator
template <typename T>
class ImageGenerator {
public:
    explicit ImageGenerator(uint64_t seed = 42) : rng_(seed) {}
    
    /// Generate random image data
    std::vector<T> generate_random(const ImageConfig& config);
    
    /// Generate gradient pattern (compressible)
    std::vector<T> generate_gradient(const ImageConfig& config);
    
    /// Generate constant value (highly compressible)
    std::vector<T> generate_constant(const ImageConfig& config, T value);

private:
    std::mt19937_64 rng_;
};

/// Temporary file manager for benchmarks
class TempFileManager {
public:
    TempFileManager();
    ~TempFileManager();
    
    /// Get path for a temporary TIFF file
    std::filesystem::path get_temp_path(const std::string& name);
    
    /// Clean up all temporary files
    void cleanup_all();
    
private:
    std::filesystem::path temp_dir_;
    std::vector<std::filesystem::path> temp_files_;
};

/// Base fixture for benchmarks providing common setup/teardown
class BenchmarkFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State& state) override;
    void TearDown(const benchmark::State& state) override;
    
protected:
    std::unique_ptr<TempFileManager> temp_manager_;
};

/// Multi-page file generator
template <typename T>
class TiffGenerator {
public:
    explicit TiffGenerator(TempFileManager& temp_mgr) 
        : temp_manager_(temp_mgr), generator_() {}
    
    /// Create a multi-page TIFF file with the given parameters
    std::filesystem::path create_file(
        const std::string& name,
        const ImageConfig& image_config,
        const StorageConfig& storage_config,
        uint32_t num_pages,
        ImagePattern pattern = ImagePattern::Gradient);
    
private:
    TempFileManager& temp_manager_;
    ImageGenerator<T> generator_;
};

#ifdef HAVE_LIBTIFF
/// Multi-page file generator using LibTIFF
template <typename T>
class LibTiffGenerator {
public:
    explicit LibTiffGenerator(TempFileManager& temp_mgr) 
        : temp_manager_(temp_mgr), generator_() {}
    
    /// Create a multi-page TIFF file with the given parameters
    std::filesystem::path create_file(
        const std::string& name,
        const ImageConfig& image_config,
        const StorageConfig& storage_config,
        uint32_t num_pages,
        ImagePattern pattern = ImagePattern::Gradient);
    
private:
    TempFileManager& temp_manager_;
    ImageGenerator<T> generator_;
    
    /// Convert compression type to libtiff constant
    static int to_libtiff_compression(CompressionType comp);
    
    /// Convert predictor type to libtiff constant
    static int to_libtiff_predictor(PredictorType pred);
    
    /// Get sample format for pixel type
    static uint16_t get_sample_format();
};
#endif // HAVE_LIBTIFF

/// Benchmark statistics helper
struct BenchmarkStats {
    double mean_ns = 0.0;
    double stddev_ns = 0.0;
    double min_ns = 0.0;
    double max_ns = 0.0;
    std::size_t iterations = 0;
    
    double throughput_mbps() const;
    std::string to_string() const;
};

/// Helper to compute image throughput in MB/s
inline double compute_throughput(std::size_t bytes, double time_ns) {
    if (time_ns <= 0.0) return 0.0;
    double seconds = time_ns / 1e9;
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return mb / seconds;
}

} // namespace tiff_bench
