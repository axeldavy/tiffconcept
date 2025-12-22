// Standalone benchmark for reading TIFF files from a directory
// Supports different readers and partial/full image reading

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>
#include <cmath>

#include "../tiffconcept/include/tiffconcept/types/tag_spec_examples.hpp"
#include "../tiffconcept/include/tiffconcept/types/tiff_spec.hpp"
#include "../tiffconcept/include/tiffconcept/image_reader.hpp"
#include "../tiffconcept/include/tiffconcept/reader_base.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/types/result.hpp"
#include "../tiffconcept/include/tiffconcept/ifd.hpp"
#include "../tiffconcept/include/tiffconcept/tag_extraction.hpp"
#include "../tiffconcept/include/tiffconcept/parsing.hpp"

// Platform-specific file readers
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    #include "../tiffconcept/include/tiffconcept/readers/reader_unix_pread.hpp"
    using FileReader = tiffconcept::PreadFileReader;
#elif defined(_WIN32) || defined(_WIN64)
    #include "../tiffconcept/include/tiffconcept/readers/reader_windows.hpp"
    using FileReader = tiffconcept::WindowsFileReader;
#else
    #include "../tiffconcept/include/tiffconcept/readers/reader_stream.hpp"
    using FileReader = tiffconcept::StreamFileReader;
#endif

// Include libtiff for comparison
#include <tiffio.h>

namespace fs = std::filesystem;
using namespace tiffconcept;

// ============================================================================
// DecompSpec supporting None and Zstd
// ============================================================================

using BenchDecompSpec = DecompressorSpec<NoneDecompressorDesc, ZstdDecompressorDesc>;

// ============================================================================
// Generic Tag collection for both Stripped and Tiled images
// Minimal set for parsing simple strip-based images (BigTiff)
using ImageSpecTiffOrStrip = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    OptTag_t<StripOffsetsTag_BigTIFF>,
    OptTag_t<SamplesPerPixelTag>, // Default: 1
    OptTag_t<RowsPerStripTag>,
    OptTag_t<StripByteCountsTag_BigTIFF>,
    OptTag_t<PredictorTag>, // Default: 1 (no predictor)
    OptTag_t<TileWidthTag>,
    OptTag_t<TileLengthTag>,
    OptTag_t<TileOffsetsTag_BigTIFF>,
    OptTag_t<TileByteCountsTag_BigTIFF>,
    OptTag_t<SampleFormatTag> // Default: 1 (unsigned)
>;

// ============================================================================
// Statistics Helper
// ============================================================================

struct Statistics {
    std::vector<double> times_ms;
    std::vector<size_t> bytes_read;
    
    void add_measurement(double time_ms, size_t bytes) {
        times_ms.push_back(time_ms);
        bytes_read.push_back(bytes);
    }
    
    double mean_time() const {
        if (times_ms.empty()) return 0.0;
        return std::accumulate(times_ms.begin(), times_ms.end(), 0.0) / times_ms.size();
    }
    
    double stddev_time() const {
        if (times_ms.size() < 2) return 0.0;
        double mean = mean_time();
        double sum_sq = 0.0;
        for (double t : times_ms) {
            sum_sq += (t - mean) * (t - mean);
        }
        return std::sqrt(sum_sq / (times_ms.size() - 1));
    }
    
    double median_time() const {
        if (times_ms.empty()) return 0.0;
        auto sorted = times_ms;
        std::sort(sorted.begin(), sorted.end());
        size_t mid = sorted.size() / 2;
        if (sorted.size() % 2 == 0) {
            return (sorted[mid - 1] + sorted[mid]) / 2.0;
        }
        return sorted[mid];
    }
    
    double min_time() const {
        if (times_ms.empty()) return 0.0;
        return *std::min_element(times_ms.begin(), times_ms.end());
    }
    
    double max_time() const {
        if (times_ms.empty()) return 0.0;
        return *std::max_element(times_ms.begin(), times_ms.end());
    }
    
    size_t total_bytes() const {
        return std::accumulate(bytes_read.begin(), bytes_read.end(), size_t{0});
    }
    
    double total_time() const {
        return std::accumulate(times_ms.begin(), times_ms.end(), 0.0);
    }
    
    double throughput_mbps() const {
        double total_mb = total_bytes() / (1024.0 * 1024.0);
        double total_sec = total_time() / 1000.0;
        return total_sec > 0 ? total_mb / total_sec : 0.0;
    }
    
    void print(const std::string& reader_name) const {
        std::cout << "\n=== " << reader_name << " Statistics ===\n";
        std::cout << "Files processed: " << times_ms.size() << "\n";
        std::cout << "Total data read: " << (total_bytes() / (1024.0 * 1024.0)) << " MB\n";
        std::cout << "Total time: " << total_time() << " ms\n";
        std::cout << "Throughput: " << throughput_mbps() << " MB/s\n";
        std::cout << "\nPer-file timing:\n";
        std::cout << "  Mean:   " << mean_time() << " ms\n";
        std::cout << "  Median: " << median_time() << " ms\n";
        std::cout << "  Stddev: " << stddev_time() << " ms\n";
        std::cout << "  Min:    " << min_time() << " ms\n";
        std::cout << "  Max:    " << max_time() << " ms\n";
    }
};

// ============================================================================
// Libtiff Reader
// ============================================================================

size_t read_with_libtiff(const std::string& path, std::optional<uint32_t> tile_size) {
    TIFF* tif = TIFFOpen(path.c_str(), "r");
    if (!tif) {
        std::cerr << "Failed to open with libtiff: " << path << "\n";
        return 0;
    }
    
    uint32_t width, height;
    uint16_t samples_per_pixel = 1;
    uint16_t bits_per_sample = 8;
    
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel);
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);
    
    uint32_t read_width = width;
    uint32_t read_height = height;
    uint32_t x_offset = 0;
    uint32_t y_offset = 0;
    
    if (tile_size) {
        read_width = std::min(width, *tile_size);
        read_height = std::min(height, *tile_size);
    }
    
    size_t bytes_per_pixel = (bits_per_sample / 8) * samples_per_pixel;
    std::vector<uint8_t> output_buffer(read_height * read_width * bytes_per_pixel);
    
    // Check if image is tiled
    uint32_t tile_width = 0, tile_height = 0;
    bool is_tiled = TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width) && 
                    TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height);
    
    if (is_tiled) {
        // Tiled image - read tiles that overlap the region
        size_t tile_size_bytes = TIFFTileSize(tif);
        std::vector<uint8_t> tile_buffer(tile_size_bytes);
        
        for (uint32_t y = y_offset; y < y_offset + read_height; y += tile_height) {
            for (uint32_t x = x_offset; x < x_offset + read_width; x += tile_width) {
                // Read the tile
                if (TIFFReadTile(tif, tile_buffer.data(), x, y, 0, 0) < 0) {
                    std::cerr << "Failed to read tile at (" << x << ", " << y << ")\n";
                    TIFFClose(tif);
                    return false;
                }
                
                // Extract the relevant portion of the tile to output buffer
                uint32_t tile_x_start = (x >= x_offset) ? 0 : (x_offset - x);
                uint32_t tile_y_start = (y >= y_offset) ? 0 : (y_offset - y);
                uint32_t tile_x_end = std::min(tile_width, (x_offset + read_width) - x);
                uint32_t tile_y_end = std::min(tile_height, (y_offset + read_height) - y);
                
                for (uint32_t ty = tile_y_start; ty < tile_y_end; ++ty) {
                    uint32_t out_y = (y - y_offset) + ty;
                    if (out_y >= read_height) break;
                    
                    for (uint32_t tx = tile_x_start; tx < tile_x_end; ++tx) {
                        uint32_t out_x = (x - x_offset) + tx;
                        if (out_x >= read_width) break;
                        
                        size_t tile_idx = (ty * tile_width + tx) * bytes_per_pixel;
                        size_t out_idx = (out_y * read_width + out_x) * bytes_per_pixel;
                        
                        std::memcpy(output_buffer.data() + out_idx,
                                   tile_buffer.data() + tile_idx,
                                   bytes_per_pixel);
                    }
                }
            }
        }
    } else {
        // Stripped image - read scanlines
        size_t scanline_size = TIFFScanlineSize(tif);
        std::vector<uint8_t> scanline_buffer(scanline_size);
        
        for (uint32_t row = 0; row < read_height; ++row) {
            uint32_t src_row = y_offset + row;
            if (src_row >= height) break;
            
            if (TIFFReadScanline(tif, scanline_buffer.data(), src_row, 0) < 0) {
                std::cerr << "Failed to read scanline " << src_row << "\n";
                TIFFClose(tif);
                return false;
            }
            
            // Copy the relevant portion of the scanline
            if (x_offset == 0 && read_width == width) {
                // No cropping needed in X direction
                std::memcpy(output_buffer.data() + row * read_width * bytes_per_pixel,
                           scanline_buffer.data(),
                           read_width * bytes_per_pixel);
            } else {
                // Extract the crop region
                std::memcpy(output_buffer.data() + row * read_width * bytes_per_pixel,
                           scanline_buffer.data() + x_offset * bytes_per_pixel,
                           read_width * bytes_per_pixel);
            }
        }
    }
    
    TIFFClose(tif);
    
    size_t bytes_read = read_height * read_width * bytes_per_pixel;
    
    return bytes_read;
}

// ============================================================================
// Tiffconcept Reader Dispatcher
// ============================================================================

enum class ReaderType {
    Simple,
    IOLimited,
    CPULimited,
    Libtiff
};

// Helper to extract tags trying all 4 format/order combinations
struct TagExtractionResult {
    ExtractedTags<ImageSpecTiffOrStrip> tags;
    bool success;
};

template <TiffFormatType TiffFormat, std::endian Order>
TagExtractionResult try_extract_tags(FileReader& file_reader) {
    TagExtractionResult result{};
    result.success = false;
    
    // Get first IFD offset
    auto ifd_offset_result = ifd::get_first_ifd_offset<FileReader, TiffFormat, Order>(file_reader);
    if (!ifd_offset_result.is_ok()) return result;
    
    // Read IFD header
    auto ifd_header_result = ifd::read_ifd_header<FileReader, TiffFormat, Order>(file_reader, ifd_offset_result.value());
    if (!ifd_header_result.is_ok()) return result;
    
    // Read IFD tags
    std::vector<parsing::TagType<TiffFormat, Order>> tags_raw;
    auto next_ifd_result = ifd::read_ifd_tags<FileReader, TiffFormat, Order>(
        file_reader, ifd_header_result.value(), tags_raw);
    if (!next_ifd_result.is_ok()) return result;
    
    // Extract tags into structured form
    auto extract_result = result.tags.extract<FileReader, TiffFormat, Order>(file_reader, tags_raw);
    if (!extract_result.is_ok()) return result;
    
    result.success = true;
    return result;
}

TagExtractionResult extract_tags_from_file(FileReader& file_reader) {
    // Try all 4 combinations of format and endianness
    
    // Classic little endian (most common)
    auto result = try_extract_tags<TiffFormatType::Classic, std::endian::little>(file_reader);
    if (result.success) return result;
    
    // Classic big endian
    result = try_extract_tags<TiffFormatType::Classic, std::endian::big>(file_reader);
    if (result.success) return result;
    
    // BigTIFF little endian
    result = try_extract_tags<TiffFormatType::BigTIFF, std::endian::little>(file_reader);
    if (result.success) return result;
    
    // BigTIFF big endian
    result = try_extract_tags<TiffFormatType::BigTIFF, std::endian::big>(file_reader);
    if (result.success) return result;
    
    return result; // All failed
}

template <typename PixelType>
size_t dispatch_and_read(
    FileReader& file_reader,
    const ExtractedTags<ImageSpecTiffOrStrip>& tags,
    std::optional<uint32_t> tile_size,
    ReaderType reader_type) {
    
    // Extract image shape
    ImageShape shape;
    auto shape_result = shape.update_from_metadata(tags);
    if (!shape_result.is_ok()) return 0;
    
    // Validate pixel type matches
    if (!shape.validate_pixel_type<PixelType>()) {
        return 0;
    }
    
    // Calculate read region
    uint32_t read_width = shape.image_width();
    uint32_t read_height = shape.image_height();
    uint32_t read_depth = shape.image_depth();
    
    if (tile_size) {
        read_width = std::min(shape.image_width(), *tile_size);
        read_height = std::min(shape.image_height(), *tile_size);
        read_depth = std::min(shape.image_depth(), *tile_size);
    }
    
    ImageRegion region = ImageRegion{0, 0, 0, 0, shape.samples_per_pixel(), read_depth, read_height, read_width};
    std::vector<PixelType> buffer(region.num_samples());
    
    Result<void> read_result = Ok();
    switch (reader_type) {
        case ReaderType::Simple: {
            SimpleReader<PixelType, BenchDecompSpec> reader{};
            read_result = reader.template read_region<ImageLayoutSpec::DHWC>(file_reader, tags, region, buffer);
            break;
        }
        case ReaderType::IOLimited: {
            IOLimitedReader<PixelType, BenchDecompSpec> reader{};
            read_result = reader.template read_region<ImageLayoutSpec::DHWC>(file_reader, tags, region, buffer);
            break;
        }
        case ReaderType::CPULimited: {
            CPULimitedReader<PixelType, BenchDecompSpec> reader{};
            read_result = reader.template read_region<ImageLayoutSpec::DHWC>(file_reader, tags, region, buffer);
            break;
        }
        default:
            return false;
    }
    
    return read_result.is_ok() ? buffer.size() * sizeof(PixelType) : 0;
}

size_t read_with_tiffconcept(
    const std::string& path,
    std::optional<uint32_t> tile_size,
    ReaderType reader_type) {

    FileReader file_reader;
    auto open_result = file_reader.open(path);
    if (!open_result.is_ok()) {
        std::cerr << "Failed to open file: " << path << "\n";
        return 0;
    }
    
    // Step 1: Extract tags (try all 4 IFD format/endianness combinations)
    auto tag_result = extract_tags_from_file(file_reader);
    if (!tag_result.success) {
        std::cerr << "Failed to extract tags from: " << path << "\n";
        return 0;
    }
    
    // Step 2: Try all pixel types
    size_t bytes_read = 0;
    bytes_read = dispatch_and_read<uint8_t>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    bytes_read = dispatch_and_read<uint16_t>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    bytes_read = dispatch_and_read<uint32_t>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    bytes_read = dispatch_and_read<uint64_t>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    bytes_read = dispatch_and_read<int8_t>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    bytes_read = dispatch_and_read<int16_t>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    bytes_read = dispatch_and_read<int32_t>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    bytes_read = dispatch_and_read<int64_t>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    bytes_read = dispatch_and_read<float>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    bytes_read = dispatch_and_read<double>(file_reader, tag_result.tags, tile_size, reader_type); if (bytes_read) return bytes_read;
    
    std::cerr << "Failed to read with any pixel type: " << path << "\n";
    return 0;
}

// ============================================================================
// Main
// ============================================================================

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <directory> <reader> [tile_size]\n";
    std::cout << "\nArguments:\n";
    std::cout << "  directory  : Path to directory containing TIFF files\n";
    std::cout << "  reader     : One of: simple, io, cpu, libtiff\n";
    std::cout << "  tile_size  : Optional. Size of square tile to read from origin.\n";
    std::cout << "               If omitted, reads full image.\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << prog_name << " /data/images cpu         # Read full images with CPULimitedReader\n";
    std::cout << "  " << prog_name << " /data/images simple 512  # Read 512x512 tiles with SimpleReader\n";
    std::cout << "  " << prog_name << " /data/images libtiff     # Read full images with libtiff\n";
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string directory = argv[1];
    std::string reader_str = argv[2];
    std::optional<uint32_t> tile_size;
    
    if (argc == 4) {
        try {
            tile_size = std::stoul(argv[3]);
        } catch (...) {
            std::cerr << "Invalid tile size: " << argv[3] << "\n";
            return 1;
        }
    }
    
    // Parse reader type
    ReaderType reader_type;
    std::string reader_name;
    
    if (reader_str == "simple") {
        reader_type = ReaderType::Simple;
        reader_name = "SimpleReader";
    } else if (reader_str == "io") {
        reader_type = ReaderType::IOLimited;
        reader_name = "IOLimitedReader";
    } else if (reader_str == "cpu") {
        reader_type = ReaderType::CPULimited;
        reader_name = "CPULimitedReader";
    } else if (reader_str == "libtiff") {
        reader_type = ReaderType::Libtiff;
        reader_name = "Libtiff";
    } else {
        std::cerr << "Unknown reader: " << reader_str << "\n";
        std::cerr << "Valid options: simple, io, cpu, libtiff\n";
        return 1;
    }
    
    // Verify directory exists
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        std::cerr << "Directory does not exist: " << directory << "\n";
        return 1;
    }
    
    // Collect TIFF files
    std::vector<std::string> tiff_files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".tif" || ext == ".tiff") {
                tiff_files.push_back(entry.path().string());
            }
        }
    }
    
    if (tiff_files.empty()) {
        std::cerr << "No TIFF files found in: " << directory << "\n";
        return 1;
    }
    
    // Sort for consistent ordering
    std::sort(tiff_files.begin(), tiff_files.end());
    
    std::cout << "Found " << tiff_files.size() << " TIFF files\n";
    std::cout << "Reader: " << reader_name << "\n";
    if (tile_size) {
        std::cout << "Read mode: Partial (" << *tile_size << "x" << *tile_size << " tile)\n";
    } else {
        std::cout << "Read mode: Full image\n";
    }
    std::cout << "\nProcessing...\n";
    
    Statistics stats;
    size_t success_count = 0;
    size_t fail_count = 0;
    
    for (const auto& path : tiff_files) {
        std::cout << "Reading: " << fs::path(path).filename().string() << " ... " << std::flush;
        
        size_t bytes_read;
        auto start = std::chrono::high_resolution_clock::now();
        if (reader_type == ReaderType::Libtiff) {
            bytes_read = read_with_libtiff(path, tile_size);
        } else {
            bytes_read = read_with_tiffconcept(path, tile_size, reader_type);
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        stats.add_measurement(elapsed_ms, bytes_read);
        
        if (bytes_read > 0) {
            std::cout << "OK (" << stats.times_ms.back() << " ms)\n";
            success_count++;
        } else {
            std::cout << "FAILED\n";
            fail_count++;
        }
    }
    
    std::cout << "\n=== Summary ===\n";
    std::cout << "Successful: " << success_count << "/" << tiff_files.size() << "\n";
    std::cout << "Failed: " << fail_count << "/" << tiff_files.size() << "\n";
    
    if (success_count > 0) {
        stats.print(reader_name);
    }
    
    return fail_count > 0 ? 1 : 0;
}
