#include <benchmark/benchmark.h>
#include <filesystem>
#include <memory>

#include "benchmark_helpers.hpp"

#include "../tiffconcept/include/tiffconcept/compressors/compressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/compressors/compressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/image_reader.hpp"
#include "../tiffconcept/include/tiffconcept/image_writer.hpp"
#include "../tiffconcept/include/tiffconcept/ifd.hpp"
#include "../tiffconcept/include/tiffconcept/parsing.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_buffer.hpp"
#include "../tiffconcept/include/tiffconcept/tag_extraction.hpp"
#include "../tiffconcept/include/tiffconcept/types/tag_spec.hpp"
#include "../tiffconcept/include/tiffconcept/types/tag_spec_examples.hpp"
#include "../tiffconcept/include/tiffconcept/tiff_writer.hpp"
#include "../tiffconcept/include/tiffconcept/strategy/write_strategy.hpp"
#ifdef HAVE_LIBTIFF
#include <tiffio.h>
#endif

/// StreamFileReader from reader_stream is fine, but is not multithread friendly.
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    #include "../tiffconcept/include/tiffconcept/readers/reader_unix_pread.hpp"
    #include "../tiffconcept/include/tiffconcept/readers/reader_unix_mmap.hpp"
    using FileReader = tiffconcept::PreadFileReader;
    using MmapReader = tiffconcept::MmapFileReader;
#elif defined(_WIN32) || defined(_WIN64)
    #include "../tiffconcept/include/tiffconcept/readers/reader_windows.hpp"
    #include "../tiffconcept/include/tiffconcept/readers/reader_windows_mmap.hpp"
    // Use Windows memory-mapped file reader (best performance)
    using FileReader = tiffconcept::WindowsFileReader;
    using MmapReader = tiffconcept::WindowsMmapFileReader;
#else
    #include "../tiffconcept/include/tiffconcept/readers/reader_stream.hpp"
    // Fallback to portable stream reader
    using FileReader = tiffconcept::StreamFileReader;
    using MmapReader = tiffconcept::StreamFileReader;
#endif

namespace fs = std::filesystem;

using namespace tiffconcept;
using namespace tiff_bench;

// ============================================================================
// Reader Type Aliases for Benchmarking
// ============================================================================

template <typename T, typename DecompSpec>
using SimpleReaderType = SimpleReader<T, DecompSpec>;

template <typename T, typename DecompSpec>
using IOLimitedReaderType = IOLimitedReader<T, DecompSpec>;

template <typename T, typename DecompSpec>
using CPULimitedReaderType = CPULimitedReader<T, DecompSpec>;

// ============================================================================
// Metadata Parsing Benchmarks - Single Page
// ============================================================================

static void BM_Metadata_ParseIFD_SinglePage(benchmark::State& state) {
    // Parameters: width, format, endianness
    uint32_t width = state.range(0);
    TiffFormat format = static_cast<TiffFormat>(state.range(1));
    Endianness endian = static_cast<Endianness>(state.range(2));
    
    ImageConfig config{width, width, 1, 3, 64, 64};
    StorageConfig storage{false, true, CompressionType::None, PredictorType::None, endian, format};
    
    TempFileManager temp_mgr;
    TiffGenerator<uint8_t> gen(temp_mgr);
    auto filepath = gen.create_file("parse_ifd_single", config, storage, 1,
                                    ImagePattern::Gradient);
    
    // Benchmark IFD parsing
    for (auto _ : state) {
        FileReader file_reader(filepath.string());
        
        if (format == TiffFormat::Classic && endian == Endianness::Little) {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
            if (!ifd_offset.is_ok()) {
                state.SkipWithError("Failed to get IFD offset " + ifd_offset.error().message);
                return;
            }
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
                file_reader, ifd_offset.value());
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD " + ifd.error().message);
                return;
            }
            benchmark::DoNotOptimize(ifd);
        } else if (format == TiffFormat::BigTIFF && endian == Endianness::Little) {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::BigTIFF, std::endian::little>(file_reader);
            if (!ifd_offset.is_ok()) {
                state.SkipWithError("Failed to get big IFD offset " + ifd_offset.error().message);
                return;
            }
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::BigTIFF, std::endian::little>(
                file_reader, ifd_offset.value());
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD " + ifd.error().message);
                return;
            }
            benchmark::DoNotOptimize(ifd);
        } else if (format == TiffFormat::Classic && endian == Endianness::Big) {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::big>(file_reader);
            if (!ifd_offset.is_ok()) {
                state.SkipWithError("Failed to get IFD offset " + ifd_offset.error().message);
                return;
            }
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::big>(
                file_reader, ifd_offset.value());
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD " + ifd.error().message);
                return;
            }
            benchmark::DoNotOptimize(ifd);
        } else if (format == TiffFormat::BigTIFF && endian == Endianness::Big) {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::BigTIFF, std::endian::big>(file_reader);
            if (!ifd_offset.is_ok()) {
                state.SkipWithError("Failed to get big IFD offset " + ifd_offset.error().message);
                return;
            }
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::BigTIFF, std::endian::big>(
                file_reader, ifd_offset.value());
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD " + ifd.error().message);
                return;
            }
            benchmark::DoNotOptimize(ifd);
        }
    }
    
    state.SetItemsProcessed(state.iterations());
}

#ifdef HAVE_LIBTIFF
static void BM_LibTIFF_Metadata_ParseIFD_SinglePage(benchmark::State& state) {
    // Parameters: width, format, endianness
    uint32_t width = state.range(0);
    TiffFormat format = static_cast<TiffFormat>(state.range(1));
    Endianness endian = static_cast<Endianness>(state.range(2));
    
    ImageConfig config{width, width, 1, 3, 64, 64};
    StorageConfig storage{false, true, CompressionType::None, PredictorType::None, endian, format};
    
    TempFileManager temp_mgr;
    TiffGenerator<uint8_t> gen(temp_mgr);
    auto filepath = gen.create_file("parse_ifd_single", config, storage, 1,
                                    ImagePattern::Gradient);
    
    for (auto _ : state) {
        TIFF* tif = TIFFOpen(filepath.string().c_str(), "r");
        if (!tif) {
            state.SkipWithError("Failed to open TIFF file");
            return;
        }
        
        // Read basic IFD fields
        uint32_t w, h;
        uint16_t spp, bps, comp;
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps);
        TIFFGetField(tif, TIFFTAG_COMPRESSION, &comp);
        
        benchmark::DoNotOptimize(w);
        benchmark::DoNotOptimize(h);
        
        TIFFClose(tif);
    }
    
    state.SetItemsProcessed(state.iterations());
}
#endif // HAVE_LIBTIFF

static void BM_Metadata_ExtractTags_SinglePage(benchmark::State& state) {
    // Parameters: width, format, endianness
    uint32_t width = state.range(0);
    TiffFormat format = static_cast<TiffFormat>(state.range(1));
    Endianness endian = static_cast<Endianness>(state.range(2));
    
    ImageConfig config{width, width, 1, 3, 64, 64};
    StorageConfig storage{false, true, CompressionType::ZSTD, PredictorType::None, endian, format};
    
    TempFileManager temp_mgr;
    TiffGenerator<uint8_t> gen(temp_mgr);
    auto filepath = gen.create_file("extract_tags_single", config, storage, 1,
                                    ImagePattern::Gradient);
    
    // Benchmark tag extraction
    for (auto _ : state) {
        FileReader file_reader(filepath.string());

        if (format == TiffFormat::Classic && endian == Endianness::Little) {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
            if (!ifd_offset.is_ok()) {
                state.SkipWithError("Failed to get IFD offset " + ifd_offset.error().message);
                return;
            }
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
                file_reader, ifd_offset.value());
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD " + ifd.error().message);
                return;
            }
            
            ExtractedTags<MinTiledSpec> metadata;
            auto extract_result = metadata.extract<FileReader, TiffFormatType::Classic, std::endian::little>(
                file_reader, std::span(ifd.value().tags));
            if (!extract_result.is_ok()) {
                state.SkipWithError("Failed to extract tags " + extract_result.error().message);
                return;
            }
            benchmark::DoNotOptimize(metadata);
        } else if (format == TiffFormat::BigTIFF && endian == Endianness::Little) {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::BigTIFF, std::endian::little>(file_reader);
            if (!ifd_offset.is_ok()) {
                state.SkipWithError("Failed to get big IFD offset " + ifd_offset.error().message);
                return;
            }
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::BigTIFF, std::endian::little>(
                file_reader, ifd_offset.value());
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD " + ifd.error().message);
                return;
            }
            
            ExtractedTags<MinBigTiledSpec> metadata;
            auto extract_result = metadata.extract<FileReader, TiffFormatType::BigTIFF, std::endian::little>(
                file_reader, std::span(ifd.value().tags));
            if (!extract_result.is_ok()) {
                state.SkipWithError("Failed to extract tags " + extract_result.error().message);
                return;
            }
            benchmark::DoNotOptimize(metadata);
        } else if (format == TiffFormat::Classic && endian == Endianness::Big) {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::big>(file_reader);
            if (!ifd_offset.is_ok()) {
                state.SkipWithError("Failed to get IFD offset " + ifd_offset.error().message);
                return;
            }
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::big>(
                file_reader, ifd_offset.value());
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD " + ifd.error().message);
                return;
            }
            
            ExtractedTags<MinTiledSpec> metadata;
            auto extract_result = metadata.extract<FileReader, TiffFormatType::Classic, std::endian::big>(
                file_reader, std::span(ifd.value().tags));
            if (!extract_result.is_ok()) {
                state.SkipWithError("Failed to extract tags " + extract_result.error().message);
                return;
            }
            benchmark::DoNotOptimize(metadata);
        } else if (format == TiffFormat::BigTIFF && endian == Endianness::Big) {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::BigTIFF, std::endian::big>(file_reader);
            if (!ifd_offset.is_ok()) {
                state.SkipWithError("Failed to get big IFD offset " + ifd_offset.error().message);
                return;
            }
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::BigTIFF, std::endian::big>(
                file_reader, ifd_offset.value());
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD " + ifd.error().message);
                return;
            }
            
            ExtractedTags<MinBigTiledSpec> metadata;
            auto extract_result = metadata.extract<FileReader, TiffFormatType::BigTIFF, std::endian::big>(
                file_reader, std::span(ifd.value().tags));
            if (!extract_result.is_ok()) {
                state.SkipWithError("Failed to extract tags " + extract_result.error().message);
                return;
            }
            benchmark::DoNotOptimize(metadata);
        }
    }
    
    state.SetItemsProcessed(state.iterations());
}

#ifdef HAVE_LIBTIFF
static void BM_LibTIFF_Metadata_ExtractTags_SinglePage(benchmark::State& state) {
    // Parameters: width, format, endianness
    uint32_t width = state.range(0);
    TiffFormat format = static_cast<TiffFormat>(state.range(1));
    Endianness endian = static_cast<Endianness>(state.range(2));
    
    ImageConfig config{width, width, 1, 3, 64, 64};
    StorageConfig storage{false, true, CompressionType::ZSTD, PredictorType::None, endian, format};
    
    TempFileManager temp_mgr;
    TiffGenerator<uint8_t> gen(temp_mgr);
    auto filepath = gen.create_file("extract_tags_single", config, storage, 1,
                                    ImagePattern::Gradient);
    
    for (auto _ : state) {
        TIFF* tif = TIFFOpen(filepath.string().c_str(), "r");
        if (!tif) {
            state.SkipWithError("Failed to open TIFF file");
            return;
        }
        
        // Extract all tags that correspond to MinTiledSpec
        uint32_t image_width, image_length, tile_width, tile_length;
        uint16_t bits_per_sample, compression, samples_per_pixel, predictor, sample_format;
        uint32_t* tile_offsets;
        uint32_t* tile_byte_counts;
        
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &image_width);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &image_length);
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);
        TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression);
        TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel);
        TIFFGetFieldDefaulted(tif, TIFFTAG_PREDICTOR, &predictor);
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_length);
        TIFFGetField(tif, TIFFTAG_TILEOFFSETS, &tile_offsets);
        TIFFGetField(tif, TIFFTAG_TILEBYTECOUNTS, &tile_byte_counts);
        TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sample_format);
        
        benchmark::DoNotOptimize(image_width);
        benchmark::DoNotOptimize(tile_offsets);
        
        TIFFClose(tif);
    }
    
    state.SetItemsProcessed(state.iterations());
}
#endif // HAVE_LIBTIFF

// ============================================================================
// Metadata Parsing Benchmarks - Multi-Page
// ============================================================================

static void BM_Metadata_ParseIFD_MultiPage(benchmark::State& state) {
    // Parameters: num_pages, page_to_parse (0-based index)
    uint32_t num_pages = state.range(0);
    uint32_t target_page = state.range(1);
    
    ImageConfig config{512, 512, 1, 3, 128, 128};
    StorageConfig storage{false, true, CompressionType::None, PredictorType::None};
    
    TempFileManager temp_mgr;
    TiffGenerator<uint8_t> gen(temp_mgr);
    auto filepath = gen.create_file("parse_ifd_multi", config, storage, num_pages,
                                    ImagePattern::Gradient);
    
    // Benchmark parsing specific page
    for (auto _ : state) {
        FileReader file_reader(filepath.string());
        
        auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
        if (!ifd_offset.is_ok()) {
            state.SkipWithError("Failed to get first IFD offset " + ifd_offset.error().message);
            return;
        }
        
        // Navigate to target page
        for (uint32_t page = 0; page < target_page; ++page) {
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
                file_reader, ifd_offset.value());
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD during navigation " + ifd.error().message);
                return;
            }
            auto next_ifd_offset = ifd.value().next_ifd_offset;
            if (next_ifd_offset.value == 0) break;
            ifd_offset = next_ifd_offset;
        }
        
        // Parse target page IFD
        auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
            file_reader, ifd_offset.value());
        if (!ifd.is_ok()) {
            state.SkipWithError("Failed to read target IFD " + ifd.error().message);
            return;
        }
        benchmark::DoNotOptimize(ifd);
    }
    
    state.SetItemsProcessed(state.iterations());
}

#ifdef HAVE_LIBTIFF
static void BM_LibTIFF_Metadata_ParseIFD_MultiPage(benchmark::State& state) {
    // Parameters: num_pages, page_to_parse (0-based index)
    uint32_t num_pages = state.range(0);
    uint32_t target_page = state.range(1);
    
    ImageConfig config{512, 512, 1, 3, 128, 128};
    StorageConfig storage{false, true, CompressionType::None, PredictorType::None};
    
    TempFileManager temp_mgr;
    TiffGenerator<uint8_t> gen(temp_mgr);
    auto filepath = gen.create_file("parse_ifd_multi", config, storage, num_pages,
                                    ImagePattern::Gradient);
    
    for (auto _ : state) {
        TIFF* tif = TIFFOpen(filepath.string().c_str(), "r");
        if (!tif) {
            state.SkipWithError("Failed to open TIFF file");
            return;
        }
        
        // Navigate to target page
        TIFFSetDirectory(tif, target_page);
        
        uint32_t w, h;
        uint16_t spp, bps;
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps);
        
        benchmark::DoNotOptimize(w);
        
        TIFFClose(tif);
    }
    
    state.SetItemsProcessed(state.iterations());
}
#endif // HAVE_LIBTIFF

static void BM_Metadata_ExtractAllPages(benchmark::State& state) {
    // Parameter: num_pages
    uint32_t num_pages = state.range(0);
    
    ImageConfig config{256, 256, 1, 3, 128, 128};
    StorageConfig storage{false, true, CompressionType::ZSTD, PredictorType::None};
    
    TempFileManager temp_mgr;
    TiffGenerator<uint8_t> gen(temp_mgr);
    auto filepath = gen.create_file("extract_all_pages", config, storage, num_pages,
                                    ImagePattern::Gradient);
    
    // Benchmark extracting metadata from all pages
    for (auto _ : state) {
        FileReader file_reader(filepath.string());
        
        auto ifd_offset_result = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
        if (!ifd_offset_result.is_ok()) {
            state.SkipWithError("Failed to get first IFD offset " + ifd_offset_result.error().message);
            return;
        }
        auto ifd_offset = ifd_offset_result.value();
        
        uint32_t pages_extracted = 0;
        while (ifd_offset.value != 0) {
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
                file_reader, ifd_offset);
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD " + ifd.error().message);
                return;
            }
            
            ExtractedTags<MinTiledSpec> metadata;
            auto extract_result = metadata.extract<FileReader, TiffFormatType::Classic, std::endian::little>(
                file_reader, std::span(ifd.value().tags));
            if (!extract_result.is_ok()) {
                state.SkipWithError("Failed to extract tags " + extract_result.error().message);
                return;
            }
            
            benchmark::DoNotOptimize(metadata);
            ifd_offset = ifd.value().next_ifd_offset;
            pages_extracted++;
            
            if (pages_extracted >= num_pages) break;
        }
    }
    
    state.SetItemsProcessed(state.iterations() * num_pages);
}

#ifdef HAVE_LIBTIFF

static void BM_LibTIFF_Metadata_ExtractAllPages(benchmark::State& state) {
    // Parameter: num_pages
    uint32_t num_pages = state.range(0);
    
    ImageConfig config{256, 256, 1, 3, 128, 128};
    StorageConfig storage{false, true, CompressionType::ZSTD, PredictorType::None};
    
    TempFileManager temp_mgr;
    TiffGenerator<uint8_t> gen(temp_mgr);
    auto filepath = gen.create_file("extract_all_pages", config, storage, num_pages,
                                    ImagePattern::Gradient);
    
    for (auto _ : state) {
        TIFF* tif = TIFFOpen(filepath.string().c_str(), "r");
        if (!tif) {
            state.SkipWithError("Failed to open TIFF file");
            return;
        }
        
        do {
            // Extract all tags that correspond to MinTiledSpec
            uint32_t image_width, image_length, tile_width, tile_length;
            uint16_t bits_per_sample, compression, samples_per_pixel, predictor, sample_format;
            uint32_t* tile_offsets;
            uint32_t* tile_byte_counts;
            
            TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &image_width);
            TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &image_length);
            TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);
            TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression);
            TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel);
            // Predictor is optional - only present with certain compressions
            if (!TIFFGetField(tif, TIFFTAG_PREDICTOR, &predictor)) {
                predictor = 1; // Default: no predictor
            }
            TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width);
            TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_length);
            TIFFGetField(tif, TIFFTAG_TILEOFFSETS, &tile_offsets);
            TIFFGetField(tif, TIFFTAG_TILEBYTECOUNTS, &tile_byte_counts);
            TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sample_format);
            
            benchmark::DoNotOptimize(image_width);
            benchmark::DoNotOptimize(tile_offsets);
        } while (TIFFReadDirectory(tif));
        
        TIFFClose(tif);
    }
    
    state.SetItemsProcessed(state.iterations() * num_pages);
}
#endif // HAVE_LIBTIFF

// ============================================================================
// Read Benchmarks - Size Variations
// ============================================================================

template <typename T, typename ReaderType>
static void BM_Read_SizeVariation(benchmark::State& state) {
    // Parameters: width, channels, compression, endianness
    uint32_t width = state.range(0);
    uint16_t channels = state.range(1);
    CompressionType comp = static_cast<CompressionType>(state.range(2));
    Endianness endian = static_cast<Endianness>(state.range(3));
    
    ImageConfig config{width, width, 1, channels, 128, 128};
    StorageConfig storage{false, true, comp, PredictorType::None, endian};
    
    TempFileManager temp_mgr;
    TiffGenerator<T> gen(temp_mgr);
    auto filepath = gen.create_file("read_size_var", config, storage, 1,
                                    ImagePattern::Gradient);
    
    // Benchmark reading
    std::size_t bytes_processed = 0;

    // Putting reader instantiation here to avoid measuring construction time
    // One of the optimizations proposed by the library is to reuse as much
    // as possible already allocated resources.
    ReaderType reader;
    ExtractedTags<MinTiledSpec> metadata;
    TiledImageInfo<T> image_info;

    for (auto _ : state) {
        FileReader file_reader(filepath.string());
        if (endian == Endianness::Little) {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
                file_reader, ifd_offset.value());
            
            auto extract_result = metadata.extract<FileReader, TiffFormatType::Classic, std::endian::little>(
                file_reader, std::span(ifd.value().tags));
            if (!extract_result.is_ok()) {
                state.SkipWithError("Failed to extract tags " + extract_result.error().message);
                return;
            }
            
            extract_result = image_info.update_from_metadata(metadata);
            if (!extract_result.is_ok()) {
                state.SkipWithError("Failed to update image info from metadata " + extract_result.error().message);
                return;
            }

            auto region = image_info.shape().full_region();
            std::vector<T> output(region.num_samples());
            
            auto result = reader.template read_region<ImageLayoutSpec::DHWC>(
                file_reader, metadata, region, output);
            
            benchmark::DoNotOptimize(output);
            bytes_processed += output.size() * sizeof(T);
        } else {
            auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::big>(file_reader);
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::big>(
                file_reader, ifd_offset.value());
            
            auto extract_result = metadata.extract<FileReader, TiffFormatType::Classic, std::endian::big>(
                file_reader, std::span(ifd.value().tags));
            if (!extract_result.is_ok()) {
                state.SkipWithError("Failed to extract tags " + extract_result.error().message);
                return;
            }

            extract_result = image_info.update_from_metadata(metadata);
            if (!extract_result.is_ok()) {
                state.SkipWithError("Failed to update image info from metadata " + extract_result.error().message);
                return;
            }
            
            auto region = image_info.shape().full_region();
            std::vector<T> output(region.num_samples());
            
            auto result = reader.template read_region<ImageLayoutSpec::DHWC>(
                file_reader, metadata, region, output);
            
            benchmark::DoNotOptimize(output);
            bytes_processed += output.size() * sizeof(T);
        }
    }
    
    state.SetBytesProcessed(bytes_processed);
    state.SetItemsProcessed(state.iterations());
}

#ifdef HAVE_LIBTIFF

// Read Benchmarks
template <typename T>
static void BM_LibTIFF_Read_SizeVariation(benchmark::State& state) {
    // Parameters: width, channels, compression
    uint32_t width = state.range(0);
    uint16_t channels = state.range(1);
    CompressionType comp = static_cast<CompressionType>(state.range(2));
    
    ImageConfig config{width, width, 1, channels, 128, 128};
    StorageConfig storage{false, true, comp, PredictorType::None};
    
    TempFileManager temp_mgr;
    TiffGenerator<T> gen(temp_mgr);
    auto filepath = gen.create_file("read_size_var", config, storage, 1,
                                    ImagePattern::Gradient);
    
    // Benchmark reading
    std::size_t bytes_processed = 0;
    
    for (auto _ : state) {
        TIFF* tif = TIFFOpen(filepath.string().c_str(), "r");
        if (!tif) {
            state.SkipWithError("Failed to open TIFF file");
            return;
        }
        
        uint32_t w, h;
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
        
        std::vector<T> buffer(w * h * channels);
        
        uint32_t tile_width, tile_height;
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height);
        
        std::vector<T> tile_buffer(tile_width * tile_height * channels);
        
        for (uint32_t y = 0; y < h; y += tile_height) {
            for (uint32_t x = 0; x < w; x += tile_width) {
                TIFFReadTile(tif, tile_buffer.data(), x, y, 0, 0);
                
                uint32_t th = std::min(tile_height, h - y);
                uint32_t tw = std::min(tile_width, w - x);
                
                for (uint32_t ty = 0; ty < th; ++ty) {
                    for (uint32_t tx = 0; tx < tw; ++tx) {
                        for (uint16_t c = 0; c < channels; ++c) {
                            std::size_t src_idx = (ty * tile_width + tx) * channels + c;
                            std::size_t dst_idx = ((y + ty) * w + (x + tx)) * channels + c;
                            buffer[dst_idx] = tile_buffer[src_idx];
                        }
                    }
                }
            }
        }
        
        TIFFClose(tif);
        benchmark::DoNotOptimize(buffer);
        bytes_processed += buffer.size() * sizeof(T);
    }
    
    state.SetBytesProcessed(bytes_processed);
    state.SetItemsProcessed(state.iterations());
}
#endif // HAVE_LIBTIFF

// ============================================================================
// Read Benchmarks - Partial Regions
// ============================================================================

template <typename T, typename ReaderType>
static void BM_Read_PartialRegion(benchmark::State& state) {
    // Parameters: image_width, region_width
    uint32_t image_width = state.range(0);
    uint32_t region_width = state.range(1);
    
    ImageConfig config{image_width, image_width, 1, 3, 128, 128};
    StorageConfig storage{false, true, CompressionType::None, PredictorType::None};
    
    TempFileManager temp_mgr;
    TiffGenerator<T> gen(temp_mgr);
    auto filepath = gen.create_file("read_partial", config, storage, 1,
                                    ImagePattern::Gradient);
    
    // Benchmark reading partial region (center of image)
    uint32_t x_offset = (image_width - region_width) / 2;
    uint32_t y_offset = (image_width - region_width) / 2;
    
    std::size_t bytes_processed = 0;
    ReaderType reader; // Putting reader instantiation here to avoid measuring construction time
    
    for (auto _ : state) {
        FileReader file_reader(filepath.string());
        
        auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
        if (!ifd_offset.is_ok()) {
            state.SkipWithError("Failed to get IFD offset " + ifd_offset.error().message);
            return;
        }
        auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
            file_reader, ifd_offset.value());
        if (!ifd.is_ok()) {
            state.SkipWithError("Failed to read IFD " + ifd.error().message);
            return;
        }
        
        ExtractedTags<MinTiledSpec> metadata;
        auto extract_ok = metadata.extract<FileReader, TiffFormatType::Classic, std::endian::little>(
            file_reader, std::span(ifd.value().tags));
        if (!extract_ok.is_ok()) {
            state.SkipWithError("Failed to extract tags " + extract_ok.error().message);
            return;
        }
        
        TiledImageInfo<T> image_info;
        auto update_ok = image_info.update_from_metadata(metadata);
        if (!update_ok.is_ok()) {
            state.SkipWithError("Failed to update image info " + update_ok.error().message);
            return;
        }
        
        ImageRegion region(0, 0, y_offset, x_offset, config.samples_per_pixel, 1, region_width, region_width);
        std::vector<T> output(region.num_samples());
        
        auto result = reader.template read_region<ImageLayoutSpec::DHWC>(
            file_reader, metadata, region, output);
        
        benchmark::DoNotOptimize(output);
        bytes_processed += output.size() * sizeof(T);
    }
    
    state.SetBytesProcessed(bytes_processed);
    state.SetItemsProcessed(state.iterations());
}

#ifdef HAVE_LIBTIFF

template <typename T>
static void BM_LibTIFF_Read_PartialRegion(benchmark::State& state) {
    uint32_t image_width = state.range(0);
    uint32_t region_width = state.range(1);
    
    ImageConfig config{image_width, image_width, 1, 3, 128, 128};
    StorageConfig storage{false, true, CompressionType::None, PredictorType::None};
    
    TempFileManager temp_mgr;
    LibTiffGenerator<T> gen(temp_mgr);
    auto filepath = gen.create_file("libtiff_read_partial", config, storage, 1,
                                    ImagePattern::Gradient);
    
    uint32_t x_offset = (image_width - region_width) / 2;
    uint32_t y_offset = (image_width - region_width) / 2;
    
    std::size_t bytes_processed = 0;
    
    for (auto _ : state) {
        TIFF* tif = TIFFOpen(filepath.string().c_str(), "r");
        if (!tif) {
            state.SkipWithError("Failed to open TIFF file");
            return;
        }
        
        uint32_t tile_width, tile_height;
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height);
        
        std::vector<T> output(region_width * region_width * 3);
        std::vector<T> tile_buffer(tile_width * tile_height * 3);
        
        uint32_t start_tile_y = (y_offset / tile_height) * tile_height;
        uint32_t start_tile_x = (x_offset / tile_width) * tile_width;
        uint32_t end_y = y_offset + region_width;
        uint32_t end_x = x_offset + region_width;
        
        for (uint32_t y = start_tile_y; y < end_y; y += tile_height) {
            for (uint32_t x = start_tile_x; x < end_x; x += tile_width) {
                TIFFReadTile(tif, tile_buffer.data(), x, y, 0, 0);
                
                // Extract relevant portion
                for (uint32_t ty = 0; ty < tile_height; ++ty) {
                    uint32_t img_y = y + ty;
                    if (img_y < y_offset || img_y >= y_offset + region_width) continue;
                    
                    for (uint32_t tx = 0; tx < tile_width; ++tx) {
                        uint32_t img_x = x + tx;
                        if (img_x < x_offset || img_x >= x_offset + region_width) continue;
                        
                        for (uint16_t c = 0; c < 3; ++c) {
                            std::size_t src_idx = (ty * tile_width + tx) * 3 + c;
                            std::size_t dst_idx = ((img_y - y_offset) * region_width + (img_x - x_offset)) * 3 + c;
                            output[dst_idx] = tile_buffer[src_idx];
                        }
                    }
                }
            }
        }
        
        TIFFClose(tif);
        benchmark::DoNotOptimize(output);
        bytes_processed += output.size() * sizeof(T);
    }
    
    state.SetBytesProcessed(bytes_processed);
    state.SetItemsProcessed(state.iterations());
}
#endif // HAVE_LIBTIFF

// ============================================================================
// Read Benchmarks - 3D Volumes
// ============================================================================

template <typename T, typename ReaderType>
static void BM_Read_3DVolume(benchmark::State& state) {
    // Parameters: xy_size, depth
    uint32_t xy_size = state.range(0);
    uint32_t depth = state.range(1);
    
    ImageConfig config{xy_size, xy_size, depth, 1, 128, 128, 16};
    StorageConfig storage{false, true, CompressionType::None, PredictorType::None};
    
    TempFileManager temp_mgr;
    TiffGenerator<T> gen(temp_mgr);
    auto filepath = gen.create_file("read_3d", config, storage, 1,
                                    ImagePattern::Gradient);
    
    // Benchmark reading full volume
    std::size_t bytes_processed = 0;

    ReaderType reader; // Putting reader instantiation here to avoid measuring construction time
    
    for (auto _ : state) {
        FileReader file_reader(filepath.string());
        
        auto ifd_offset = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
        auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
            file_reader, ifd_offset.value());
        
        ExtractedTags<MinTiledSpec> metadata;
        auto extraction_result = metadata.extract<FileReader, TiffFormatType::Classic, std::endian::little>(
            file_reader, std::span(ifd.value().tags));
        if (!extraction_result.is_ok()) {
            state.SkipWithError("Failed to extract tags " + extraction_result.error().message);
            return;
        }
        
        TiledImageInfo<T> image_info;
        extraction_result = image_info.update_from_metadata(metadata);
        if (!extraction_result.is_ok()) {
            state.SkipWithError("Failed to update image info from metadata " + extraction_result.error().message);
            return;
        }
        
        auto region = image_info.shape().full_region();
        std::vector<T> output(region.num_samples());
        
        auto result = reader.template read_region<ImageLayoutSpec::DHWC>(
            file_reader, metadata, region, output);
        
        benchmark::DoNotOptimize(output);
        bytes_processed += output.size() * sizeof(T);
    }
    
    state.SetBytesProcessed(bytes_processed);
    state.SetItemsProcessed(state.iterations());
}

// ============================================================================
// Read Benchmarks - Multi-Page
// ============================================================================

template <typename T, typename ReaderType>
static void BM_Read_MultiPage(benchmark::State& state) {
    // Parameters: num_pages, page_to_read
    uint32_t num_pages = state.range(0);
    uint32_t target_page = state.range(1);
    
    ImageConfig config{512, 512, 1, 3, 128, 128};
    StorageConfig storage{false, true, CompressionType::None, PredictorType::None};
    
    TempFileManager temp_mgr;
    TiffGenerator<T> gen(temp_mgr);
    auto filepath = gen.create_file("read_multipage", config, storage, num_pages,
                                    ImagePattern::Gradient);
    
    // Benchmark reading specific page
    std::size_t bytes_processed = 0;

    ReaderType reader; // Putting reader instantiation here to avoid measuring construction time
    
    for (auto _ : state) {
        FileReader file_reader(filepath.string());
        
        // Navigate to target page
        auto ifd_offset_result = ifd::get_first_ifd_offset<FileReader, TiffFormatType::Classic, std::endian::little>(file_reader);
        if (!ifd_offset_result.is_ok()) {
            state.SkipWithError("Failed to get first IFD offset " + ifd_offset_result.error().message);
            return;
        }
        auto ifd_offset = ifd_offset_result.value();
        for (uint32_t page = 0; page < target_page; ++page) {
            auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
                file_reader, ifd_offset);
            if (!ifd.is_ok()) {
                state.SkipWithError("Failed to read IFD during navigation " + ifd.error().message);
                return;
            }
            auto next_ifd_offset = ifd.value().next_ifd_offset;
            if (next_ifd_offset.value == 0) break;
            ifd_offset = next_ifd_offset;
        }
        
        // Read target page
        auto ifd = ifd::read_ifd<FileReader, TiffFormatType::Classic, std::endian::little>(
            file_reader, ifd_offset);
        if (!ifd.is_ok()) {
            state.SkipWithError("Failed to read target IFD " + ifd.error().message);
            return;
        }
        
        ExtractedTags<MinTiledSpec> metadata;
        auto extract_ok = metadata.extract<FileReader, TiffFormatType::Classic, std::endian::little>(
            file_reader, std::span(ifd.value().tags));
        if (!extract_ok.is_ok()) {
            state.SkipWithError("Failed to extract tags " + extract_ok.error().message);
            return;
        }
        
        TiledImageInfo<T> image_info;
        auto update_ok = image_info.update_from_metadata(metadata);
        if (!update_ok.is_ok()) {
            state.SkipWithError("Failed to update image info " + update_ok.error().message);
            return;
        }

        auto region = image_info.shape().full_region();
        std::vector<T> output(region.num_samples());
        
        auto result = reader.template read_region<ImageLayoutSpec::DHWC>(
            file_reader, metadata, region, output);
        
        benchmark::DoNotOptimize(output);
        benchmark::DoNotOptimize(result);
        bytes_processed += output.size() * sizeof(T);
    }
    
    state.SetBytesProcessed(bytes_processed);
    state.SetItemsProcessed(state.iterations());
}

#ifdef HAVE_LIBTIFF
template <typename T>
static void BM_LibTIFF_Read_MultiPage(benchmark::State& state) {
    uint32_t num_pages = state.range(0);
    uint32_t target_page = state.range(1);
    
    ImageConfig config{512, 512, 1, 3, 128, 128};
    StorageConfig storage{false, true, CompressionType::None, PredictorType::None};
    
    TempFileManager temp_mgr;
    LibTiffGenerator<T> gen(temp_mgr);
    auto filepath = gen.create_file("libtiff_read_multipage", config, storage, num_pages,
                                    ImagePattern::Gradient);
    
    std::size_t bytes_processed = 0;
    
    for (auto _ : state) {
        TIFF* tif = TIFFOpen(filepath.string().c_str(), "r");
        if (!tif) {
            state.SkipWithError("Failed to open TIFF file");
            return;
        }
        
        TIFFSetDirectory(tif, target_page);
        
        uint32_t w, h, tile_width, tile_height;
        uint16_t spp;
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height);
        
        std::vector<T> buffer(w * h * spp);
        std::vector<T> tile_buffer(tile_width * tile_height * spp);
        
        for (uint32_t y = 0; y < h; y += tile_height) {
            for (uint32_t x = 0; x < w; x += tile_width) {
                TIFFReadTile(tif, tile_buffer.data(), x, y, 0, 0);
                
                uint32_t th = std::min(tile_height, h - y);
                uint32_t tw = std::min(tile_width, w - x);
                
                for (uint32_t ty = 0; ty < th; ++ty) {
                    for (uint32_t tx = 0; tx < tw; ++tx) {
                        for (uint16_t c = 0; c < spp; ++c) {
                            std::size_t src_idx = (ty * tile_width + tx) * spp + c;
                            std::size_t dst_idx = ((y + ty) * w + (x + tx)) * spp + c;
                            buffer[dst_idx] = tile_buffer[src_idx];
                        }
                    }
                }
            }
        }
        
        TIFFClose(tif);
        benchmark::DoNotOptimize(buffer);
        bytes_processed += buffer.size() * sizeof(T);
    }
    
    state.SetBytesProcessed(bytes_processed);
    state.SetItemsProcessed(state.iterations());
}
#endif // HAVE_LIBTIFF

// ============================================================================
// Write Benchmarks - Size and Channel Variations
// ============================================================================

#ifdef HAVE_LIBTIFF

template <typename T>
static void BM_LibTIFF_Write_SizeChannelVariation(benchmark::State& state) {
    uint32_t width = state.range(0);
    uint16_t channels = state.range(1);
    CompressionType comp = static_cast<CompressionType>(state.range(2));
    PredictorType pred = static_cast<PredictorType>(state.range(3));
    
    ImageConfig config{width, width, 1, channels, 128, 128};
    ImageGenerator<T> gen;
    auto image_data = gen.generate_gradient(config);
    
    TempFileManager temp_mgr;
    
    std::size_t bytes_processed = 0;
    
    // Determine compression and predictor
    CompressionScheme comp_scheme = (comp == CompressionType::ZSTD) ? CompressionScheme::ZSTD : CompressionScheme::None;
    std::size_t tile_size = config.tile_width * config.tile_height * channels;
    std::vector<T> tile_buffer(tile_size);
    
    for (auto _ : state) {
        auto filepath = temp_mgr.get_temp_path("libtiff_write_size_channel");
        TIFF* tif = TIFFOpen(filepath.string().c_str(), "w");
        
        TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, config.width);
        TIFFSetField(tif, TIFFTAG_IMAGELENGTH, config.height);
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, channels);
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, sizeof(T) * 8);
        TIFFSetField(tif, TIFFTAG_COMPRESSION, comp_scheme);
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
        TIFFSetField(tif, TIFFTAG_TILEWIDTH, config.tile_width);
        TIFFSetField(tif, TIFFTAG_TILELENGTH, config.tile_height);
        TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        
        for (uint32_t y = 0; y < config.height; y += config.tile_height) {
            for (uint32_t x = 0; x < config.width; x += config.tile_width) {
                uint32_t th = std::min(config.tile_height, config.height - y);
                uint32_t tw = std::min(config.tile_width, config.width - x);
                
                for (uint32_t ty = 0; ty < th; ++ty) {
                    for (uint32_t tx = 0; tx < tw; ++tx) {
                        for (uint16_t c = 0; c < channels; ++c) {
                            std::size_t src_idx = ((y + ty) * config.width + (x + tx)) * channels + c;
                            std::size_t dst_idx = (ty * config.tile_width + tx) * channels + c;
                            tile_buffer[dst_idx] = image_data[src_idx];
                        }
                    }
                }
                
                TIFFWriteTile(tif, tile_buffer.data(), x, y, 0, 0);
            }
        }
        
        TIFFClose(tif);
        bytes_processed += image_data.size() * sizeof(T);
    }
    
    state.SetBytesProcessed(bytes_processed);
    state.SetItemsProcessed(state.iterations());
}

#endif // HAVE_LIBTIFF


#ifdef HAVE_LIBTIFF


template <typename T>
static void BM_LibTIFF_Write_MultiPage(benchmark::State& state) {
    uint32_t num_pages = state.range(0);
    CompressionType comp = static_cast<CompressionType>(state.range(1));
    
    ImageConfig config{512, 512, 1, 3, 128, 128};
    ImageGenerator<T> gen;
    auto image_data = gen.generate_gradient(config);
    
    TempFileManager temp_mgr;
    int libtiff_comp = (comp == CompressionType::ZSTD) ? COMPRESSION_ZSTD : COMPRESSION_NONE;
    
    std::size_t bytes_processed = 0;
    std::size_t tile_size = config.tile_width * config.tile_height * config.samples_per_pixel;
    std::vector<T> tile_buffer(tile_size);
    
    for (auto _ : state) {
        auto filepath = temp_mgr.get_temp_path("libtiff_write_multipage");
        TIFF* tif = TIFFOpen(filepath.string().c_str(), "w");
        
        for (uint32_t page = 0; page < num_pages; ++page) {
            TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, config.width);
            TIFFSetField(tif, TIFFTAG_IMAGELENGTH, config.height);
            TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, config.samples_per_pixel);
            TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, sizeof(T) * 8);
            TIFFSetField(tif, TIFFTAG_COMPRESSION, libtiff_comp);
            TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
            TIFFSetField(tif, TIFFTAG_TILEWIDTH, config.tile_width);
            TIFFSetField(tif, TIFFTAG_TILELENGTH, config.tile_height);
            TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
            
            for (uint32_t y = 0; y < config.height; y += config.tile_height) {
                for (uint32_t x = 0; x < config.width; x += config.tile_width) {
                    uint32_t th = std::min(config.tile_height, config.height - y);
                    uint32_t tw = std::min(config.tile_width, config.width - x);
                    
                    for (uint32_t ty = 0; ty < th; ++ty) {
                        for (uint32_t tx = 0; tx < tw; ++tx) {
                            for (uint16_t c = 0; c < config.samples_per_pixel; ++c) {
                                std::size_t src_idx = ((y + ty) * config.width + (x + tx)) * config.samples_per_pixel + c;
                                std::size_t dst_idx = (ty * config.tile_width + tx) * config.samples_per_pixel + c;
                                tile_buffer[dst_idx] = image_data[src_idx];
                            }
                        }
                    }
                    
                    TIFFWriteTile(tif, tile_buffer.data(), x, y, 0, 0);
                }
            }
            
            if (page < num_pages - 1) {
                TIFFWriteDirectory(tif);
            }
        }
        
        TIFFClose(tif);
        bytes_processed += image_data.size() * sizeof(T) * num_pages;
    }
    
    state.SetBytesProcessed(bytes_processed);
    state.SetItemsProcessed(state.iterations());
}

#endif // HAVE_LIBTIFF


// ============================================================================
// Benchmark Registration
// ============================================================================

// Metadata Parsing - Single Page. does not depend much on image size
// Params: width, format, endianness
BENCHMARK(BM_Metadata_ParseIFD_SinglePage)
    ->Args({512, 0, 0})      // 512x512, Classic, Little
    ->Args({512, 1, 0})   // 512x512, BigTIFF, Little
    ->Args({512, 1, 1})   // 512x512, BigTIFF, Big
    ->Name("TiffConcept/Metadata/ParseIFD/SinglePage")
    ->Unit(benchmark::kMicrosecond);

#ifdef HAVE_LIBTIFF

// Metadata Parsing - Single Page
BENCHMARK(BM_LibTIFF_Metadata_ParseIFD_SinglePage)
    ->Args({512, 0, 0})      // 512x512, Classic, Little
    ->Args({512, 1, 0})   // 512x512, BigTIFF, Little
    ->Args({512, 1, 1})   // 512x512, BigTIFF, Big
    ->Name("LibTIFF/Metadata/ParseIFD/SinglePage")
    ->Unit(benchmark::kMicrosecond);

#endif // HAVE_LIBTIFF

// Params: width, channels
BENCHMARK(BM_Metadata_ExtractTags_SinglePage)
    ->Args({512, 0, 0})      // 512x512, Classic, Little
    ->Args({512, 1, 0})   // 512x512, BigTIFF, Little
    ->Args({512, 1, 1})   // 512x512, BigTIFF, Big
    ->Name("TiffConcept/Metadata/ExtractTags/SinglePage")
    ->Unit(benchmark::kMicrosecond);

#ifdef HAVE_LIBTIFF
BENCHMARK(BM_LibTIFF_Metadata_ExtractTags_SinglePage)
    ->Args({512, 0, 0})      // 512x512, Classic, Little
    ->Args({512, 1, 0})   // 512x512, BigTIFF, Little
    ->Args({512, 1, 1})   // 512x512, BigTIFF, Big
    ->Name("LibTIFF/Metadata/ExtractTags/SinglePage")
    ->Unit(benchmark::kMicrosecond);
#endif // HAVE_LIBTIFF

// Metadata Parsing - Multi-Page
// Params: num_pages, page_to_parse
BENCHMARK(BM_Metadata_ParseIFD_MultiPage)
    ->Args({10, 0})    // 10 pages, read page 0
    ->Args({10, 5})    // 10 pages, read page 5 (middle)
    ->Args({10, 9})    // 10 pages, read page 9 (last)
    ->Args({50, 25})   // 50 pages, read page 25 (middle)
    ->Args({100, 50})  // 100 pages, read page 50 (middle)
    ->Name("TiffConcept/Metadata/ParseIFD/MultiPage")
    ->Unit(benchmark::kMicrosecond);

#ifdef HAVE_LIBTIFF
// Metadata Parsing - Multi-Page
BENCHMARK(BM_LibTIFF_Metadata_ParseIFD_MultiPage)
    ->Args({10, 0})
    ->Args({10, 5})
    ->Args({10, 9})
    ->Args({50, 25})
    ->Args({100, 50})
    ->Name("LibTIFF/Metadata/ParseIFD/MultiPage")
    ->Unit(benchmark::kMicrosecond);
#endif // HAVE_LIBTIFF

// Params: num_pages
BENCHMARK(BM_Metadata_ExtractAllPages)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Name("TiffConcept/Metadata/ExtractAllPages")
    ->Unit(benchmark::kMillisecond);

#ifdef HAVE_LIBTIFF
BENCHMARK(BM_LibTIFF_Metadata_ExtractAllPages)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Name("LibTIFF/Metadata/ExtractAllPages")
    ->Unit(benchmark::kMillisecond);

#endif // HAVE_LIBTIFF

// Read - Size Variations
// Params: width, channels, compression, endianness

// SimpleReader benchmarks
BENCHMARK(BM_Read_SizeVariation<uint8_t, SimpleReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc, ZstdDecompressorDesc>>>)
    ->Args({64, 1, 0, 0})      // 64x64, 1ch, None, Little
    ->Args({256, 1, 0, 0})     // 256x256, 1ch, None, Little
    ->Args({512, 3, 0, 0})     // 512x512, 3ch, None, Little
    ->Args({1024, 3, 0, 0})    // 1024x1024, 3ch, None, Little
    ->Args({2048, 3, 0, 0})    // 2048x2048, 3ch, None, Little
    ->Args({4096, 3, 0, 0})    // 4096x4096, 3ch, None, Little
    ->Args({8192, 1, 0, 0})    // 8192x8192, 1ch, None, Little
    ->Args({8192, 1, 1, 0})    // 8192x8192, 1ch, ZSTD, Little
    ->Args({512, 8, 0, 0})     // 512x512, 8ch, None, Little
    ->Args({512, 16, 0, 0})    // 512x512, 16ch, None, Little
    ->Args({512, 32, 0, 0})    // 512x512, 32ch, None, Little
    ->Args({512, 64, 0, 0})    // 512x512, 64ch, None, Little
    ->Args({512, 3, 1, 0})     // 512x512, 3ch, ZSTD, Little
    ->Args({2048, 3, 1, 0})    // 2048x2048, 3ch, ZSTD, Little
    ->Name("TiffConcept/Read/SimpleReader/SizeVariation/uint8")
    ->Unit(benchmark::kMillisecond);

// IOLimitedReader benchmarks
BENCHMARK(BM_Read_SizeVariation<uint8_t, IOLimitedReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc, ZstdDecompressorDesc>>>)
    ->Args({64, 1, 0, 0})      // 64x64, 1ch, None, Little
    ->Args({256, 1, 0, 0})     // 256x256, 1ch, None, Little
    ->Args({512, 3, 0, 0})     // 512x512, 3ch, None, Little
    ->Args({1024, 3, 0, 0})    // 1024x1024, 3ch, None, Little
    ->Args({2048, 3, 0, 0})    // 2048x2048, 3ch, None, Little
    ->Args({4096, 3, 0, 0})    // 4096x4096, 3ch, None, Little
    ->Args({8192, 1, 0, 0})    // 8192x8192, 1ch, None, Little
    ->Args({8192, 1, 1, 0})    // 8192x8192, 1ch, ZSTD, Little
    ->Args({512, 8, 0, 0})     // 512x512, 8ch, None, Little
    ->Args({512, 16, 0, 0})    // 512x512, 16ch, None, Little
    ->Args({512, 32, 0, 0})    // 512x512, 32ch, None, Little
    ->Args({512, 64, 0, 0})    // 512x512, 64ch, None, Little
    ->Args({512, 3, 1, 0})     // 512x512, 3ch, ZSTD, Little
    ->Args({2048, 3, 1, 0})    // 2048x2048, 3ch, ZSTD, Little
    ->Name("TiffConcept/Read/IOLimitedReader/SizeVariation/uint8")
    ->Unit(benchmark::kMillisecond);

// CPULimitedReader benchmarks
BENCHMARK(BM_Read_SizeVariation<uint8_t, CPULimitedReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc, ZstdDecompressorDesc>>>)
    ->Args({64, 1, 0, 0})      // 64x64, 1ch, None, Little
    ->Args({256, 1, 0, 0})     // 256x256, 1ch, None, Little
    ->Args({512, 3, 0, 0})     // 512x512, 3ch, None, Little
    ->Args({1024, 3, 0, 0})    // 1024x1024, 3ch, None, Little
    ->Args({2048, 3, 0, 0})    // 2048x2048, 3ch, None, Little
    ->Args({4096, 3, 0, 0})    // 4096x4096, 3ch, None, Little
    ->Args({8192, 1, 0, 0})    // 8192x8192, 1ch, None, Little
    ->Args({8192, 1, 1, 0})    // 8192x8192, 1ch, ZSTD, Little
    ->Args({512, 8, 0, 0})     // 512x512, 8ch, None, Little
    ->Args({512, 16, 0, 0})    // 512x512, 16ch, None, Little
    ->Args({512, 32, 0, 0})    // 512x512, 32ch, None, Little
    ->Args({512, 64, 0, 0})    // 512x512, 64ch, None, Little
    ->Args({512, 3, 1, 0})     // 512x512, 3ch, ZSTD, Little
    ->Args({2048, 3, 1, 0})    // 2048x2048, 3ch, ZSTD, Little
    ->Name("TiffConcept/Read/CPULimitedReader/SizeVariation/uint8")
    ->Unit(benchmark::kMillisecond);

// SimpleReader uint16_t
BENCHMARK(BM_Read_SizeVariation<uint16_t, SimpleReaderType<uint16_t, DecompressorSpec<NoneDecompressorDesc, ZstdDecompressorDesc>>>)
    ->Args({512, 3, 0, 0})     // 512x512, 3ch, None, Little
    ->Args({1024, 3, 0, 0})    // 1024x1024, 3ch, None, Little
    ->Args({2048, 3, 0, 0})    // 2048x2048, 3ch, None, Little
    ->Args({512, 16, 0, 0})    // 512x512, 16ch, None, Little
    ->Args({512, 3, 1, 0})     // 512x512, 3ch, ZSTD, Little
    ->Args({512, 3, 1, 1})    // 2048x2048, 3ch, ZSTD, Big
    ->Name("TiffConcept/Read/SimpleReader/SizeVariation/uint16")
    ->Unit(benchmark::kMillisecond);

// IOLimitedReader uint16_t
BENCHMARK(BM_Read_SizeVariation<uint16_t, IOLimitedReaderType<uint16_t, DecompressorSpec<NoneDecompressorDesc, ZstdDecompressorDesc>>>)
    ->Args({512, 3, 0, 0})     // 512x512, 3ch, None, Little
    ->Args({1024, 3, 0, 0})    // 1024x1024, 3ch, None, Little
    ->Args({2048, 3, 0, 0})    // 2048x2048, 3ch, None, Little
    ->Args({512, 16, 0, 0})    // 512x512, 16ch, None, Little
    ->Args({512, 3, 1, 0})     // 512x512, 3ch, ZSTD, Little
    ->Args({512, 3, 1, 1})    // 2048x2048, 3ch, ZSTD, Big
    ->Name("TiffConcept/Read/IOLimitedReader/SizeVariation/uint16")
    ->Unit(benchmark::kMillisecond);

// CPULimitedReader uint16_t
BENCHMARK(BM_Read_SizeVariation<uint16_t, CPULimitedReaderType<uint16_t, DecompressorSpec<NoneDecompressorDesc, ZstdDecompressorDesc>>>)
    ->Args({512, 3, 0, 0})     // 512x512, 3ch, None, Little
    ->Args({1024, 3, 0, 0})    // 1024x1024, 3ch, None, Little
    ->Args({2048, 3, 0, 0})    // 2048x2048, 3ch, None, Little
    ->Args({512, 16, 0, 0})    // 512x512, 16ch, None, Little
    ->Args({512, 3, 1, 0})     // 512x512, 3ch, ZSTD, Little
    ->Args({512, 3, 1, 1})    // 2048x2048, 3ch, ZSTD, Big
    ->Name("TiffConcept/Read/CPULimitedReader/SizeVariation/uint16")
    ->Unit(benchmark::kMillisecond);


#ifdef HAVE_LIBTIFF
// Read - Size Variations
BENCHMARK(BM_LibTIFF_Read_SizeVariation<uint8_t>)
    ->Args({64, 1, 0})
    ->Args({256, 1, 0})
    ->Args({512, 3, 0})
    ->Args({1024, 3, 0})
    ->Args({2048, 3, 0})
    ->Args({4096, 3, 0})
    ->Args({8192, 1, 0})
    ->Args({8192, 1, 1})
    ->Args({512, 8, 0})
    ->Args({512, 16, 0})
    ->Args({512, 32, 0})
    ->Args({512, 64, 0})
    ->Args({512, 3, 1})
    ->Args({2048, 3, 1})
    ->Name("LibTIFF/Read/SizeVariation/uint8")
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_LibTIFF_Read_SizeVariation<uint16_t>)
    ->Args({512, 3, 0})
    ->Args({1024, 3, 0})
    ->Args({2048, 3, 0})
    ->Args({512, 16, 0})
    ->Args({512, 3, 1})
    ->Name("LibTIFF/Read/SizeVariation/uint16")
    ->Unit(benchmark::kMillisecond);
#endif // HAVE_LIBTIFF


// Read - Partial Regions
// Params: image_width, region_width

// SimpleReader
BENCHMARK(BM_Read_PartialRegion<uint8_t, SimpleReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc>>>)
    ->Args({1024, 128})   // Read 128x128 from 1024x1024
    ->Args({1024, 256})   // Read 256x256 from 1024x1024
    ->Args({2048, 256})   // Read 256x256 from 2048x2048
    ->Args({2048, 512})   // Read 512x512 from 2048x2048
    ->Args({4096, 512})   // Read 512x512 from 4096x4096
    ->Args({4096, 1024})  // Read 1024x1024 from 4096x4096
    ->Name("TiffConcept/Read/SimpleReader/PartialRegion/uint8")
    ->Unit(benchmark::kMillisecond);

// IOLimitedReader
BENCHMARK(BM_Read_PartialRegion<uint8_t, IOLimitedReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc>>>)
    ->Args({1024, 128})   // Read 128x128 from 1024x1024
    ->Args({1024, 256})   // Read 256x256 from 1024x1024
    ->Args({2048, 256})   // Read 256x256 from 2048x2048
    ->Args({2048, 512})   // Read 512x512 from 2048x2048
    ->Args({4096, 512})   // Read 512x512 from 4096x4096
    ->Args({4096, 1024})  // Read 1024x1024 from 4096x4096
    ->Name("TiffConcept/Read/IOLimitedReader/PartialRegion/uint8")
    ->Unit(benchmark::kMillisecond);

// CPULimitedReader
BENCHMARK(BM_Read_PartialRegion<uint8_t, CPULimitedReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc>>>)
    ->Args({1024, 128})   // Read 128x128 from 1024x1024
    ->Args({1024, 256})   // Read 256x256 from 1024x1024
    ->Args({2048, 256})   // Read 256x256 from 2048x2048
    ->Args({2048, 512})   // Read 512x512 from 2048x2048
    ->Args({4096, 512})   // Read 512x512 from 4096x4096
    ->Args({4096, 1024})  // Read 1024x1024 from 4096x4096
    ->Name("TiffConcept/Read/CPULimitedReader/PartialRegion/uint8")
    ->Unit(benchmark::kMillisecond);


#ifdef HAVE_LIBTIFF
// Read - Partial Regions
BENCHMARK(BM_LibTIFF_Read_PartialRegion<uint8_t>)
    ->Args({1024, 128})
    ->Args({1024, 256})
    ->Args({2048, 256})
    ->Args({2048, 512})
    ->Args({4096, 512})
    ->Args({4096, 1024})
    ->Name("LibTIFF/Read/PartialRegion/uint8")
    ->Unit(benchmark::kMillisecond);
#endif // HAVE_LIBTIFF


// Read - 3D Volumes
// Params: xy_size, depth

// SimpleReader uint8_t
 BENCHMARK(BM_Read_3DVolume<uint8_t, SimpleReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc>>>)
     ->Args({128, 16})   // 128x128x16
     ->Args({128, 32})   // 128x128x32
     ->Args({256, 16})   // 256x256x16
     ->Args({256, 32})   // 256x256x32
     ->Args({256, 64})   // 256x256x64
     ->Args({512, 32})   // 512x512x32
     ->Name("TiffConcept/Read/SimpleReader/3DVolume/uint8")
     ->Unit(benchmark::kMillisecond);

// IOLimitedReader uint8_t
 BENCHMARK(BM_Read_3DVolume<uint8_t, IOLimitedReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc>>>)
     ->Args({128, 16})   // 128x128x16
     ->Args({128, 32})   // 128x128x32
     ->Args({256, 16})   // 256x256x16
     ->Args({256, 32})   // 256x256x32
     ->Args({256, 64})   // 256x256x64
     ->Args({512, 32})   // 512x512x32
     ->Name("TiffConcept/Read/IOLimitedReader/3DVolume/uint8")
     ->Unit(benchmark::kMillisecond);

// CPULimitedReader uint8_t
 BENCHMARK(BM_Read_3DVolume<uint8_t, CPULimitedReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc>>>)
     ->Args({128, 16})   // 128x128x16
     ->Args({128, 32})   // 128x128x32
     ->Args({256, 16})   // 256x256x16
     ->Args({256, 32})   // 256x256x32
     ->Args({256, 64})   // 256x256x64
     ->Args({512, 32})   // 512x512x32
     ->Name("TiffConcept/Read/CPULimitedReader/3DVolume/uint8")
     ->Unit(benchmark::kMillisecond);

// SimpleReader uint16_t
 BENCHMARK(BM_Read_3DVolume<uint16_t, SimpleReaderType<uint16_t, DecompressorSpec<NoneDecompressorDesc>>>)
     ->Args({256, 32})   // 256x256x32
     ->Args({512, 32})   // 512x512x32
     ->Name("TiffConcept/Read/SimpleReader/3DVolume/uint16")
     ->Unit(benchmark::kMillisecond);

// IOLimitedReader uint16_t
 BENCHMARK(BM_Read_3DVolume<uint16_t, IOLimitedReaderType<uint16_t, DecompressorSpec<NoneDecompressorDesc>>>)
     ->Args({256, 32})   // 256x256x32
     ->Args({512, 32})   // 512x512x32
     ->Name("TiffConcept/Read/IOLimitedReader/3DVolume/uint16")
     ->Unit(benchmark::kMillisecond);

// CPULimitedReader uint16_t
 BENCHMARK(BM_Read_3DVolume<uint16_t, CPULimitedReaderType<uint16_t, DecompressorSpec<NoneDecompressorDesc>>>)
     ->Args({256, 32})   // 256x256x32
     ->Args({512, 32})   // 512x512x32
     ->Name("TiffConcept/Read/CPULimitedReader/3DVolume/uint16")
     ->Unit(benchmark::kMillisecond);



// Read - Multi-Page
// Params: num_pages, page_to_read

// SimpleReader
BENCHMARK(BM_Read_MultiPage<uint8_t, SimpleReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc>>>)
    ->Args({10, 0})    // 10 pages, read page 0
    ->Args({10, 5})    // 10 pages, read page 5
    ->Args({10, 9})    // 10 pages, read page 9
    ->Args({50, 0})    // 50 pages, read page 0
    ->Args({50, 25})   // 50 pages, read page 25
    ->Args({50, 49})   // 50 pages, read page 49
    ->Name("TiffConcept/Read/SimpleReader/MultiPage/uint8")
    ->Unit(benchmark::kMillisecond);

// IOLimitedReader
BENCHMARK(BM_Read_MultiPage<uint8_t, IOLimitedReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc>>>)
    ->Args({10, 0})    // 10 pages, read page 0
    ->Args({10, 5})    // 10 pages, read page 5
    ->Args({10, 9})    // 10 pages, read page 9
    ->Args({50, 0})    // 50 pages, read page 0
    ->Args({50, 25})   // 50 pages, read page 25
    ->Args({50, 49})   // 50 pages, read page 49
    ->Name("TiffConcept/Read/IOLimitedReader/MultiPage/uint8")
    ->Unit(benchmark::kMillisecond);

// CPULimitedReader
BENCHMARK(BM_Read_MultiPage<uint8_t, CPULimitedReaderType<uint8_t, DecompressorSpec<NoneDecompressorDesc>>>)
    ->Args({10, 0})    // 10 pages, read page 0
    ->Args({10, 5})    // 10 pages, read page 5
    ->Args({10, 9})    // 10 pages, read page 9
    ->Args({50, 0})    // 50 pages, read page 0
    ->Args({50, 25})   // 50 pages, read page 25
    ->Args({50, 49})   // 50 pages, read page 49
    ->Name("TiffConcept/Read/CPULimitedReader/MultiPage/uint8")
    ->Unit(benchmark::kMillisecond);


#ifdef HAVE_LIBTIFF
// Read - Multi-Page
BENCHMARK(BM_LibTIFF_Read_MultiPage<uint8_t>)
    ->Args({10, 0})
    ->Args({10, 5})
    ->Args({10, 9})
    ->Args({50, 0})
    ->Args({50, 25})
    ->Args({50, 49})
    ->Name("LibTIFF/Read/MultiPage/uint8")
    ->Unit(benchmark::kMillisecond);
#endif // HAVE_LIBTIFF
#if 0
// Write - Size and Channel Variations
// Params: width, channels, compression, predictor
BENCHMARK(BM_Write_SizeChannelVariation<uint8_t>)
    ->Args({256, 1, 0, 0})    // 256x256, 1ch, None, None
    ->Args({512, 3, 0, 0})    // 512x512, 3ch, None, None
    ->Args({1024, 3, 0, 0})   // 1024x1024, 3ch, None, None
    ->Args({2048, 3, 0, 0})   // 2048x2048, 3ch, None, None
    ->Args({512, 8, 0, 0})    // 512x512, 8ch, None, None
    ->Args({512, 16, 0, 0})   // 512x512, 16ch, None, None
    ->Args({512, 32, 0, 0})   // 512x512, 32ch, None, None
    ->Args({512, 3, 1, 0})    // 512x512, 3ch, ZSTD, None
    ->Args({512, 3, 1, 1})    // 512x512, 3ch, ZSTD, Horizontal
    ->Args({1024, 3, 1, 1})   // 1024x1024, 3ch, ZSTD, Horizontal
    ->Name("TiffConcept/Write/SizeChannelVariation/uint8")
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Write_SizeChannelVariation<uint16_t>)
    ->Args({512, 3, 0, 0})    // 512x512, 3ch, None, None
    ->Args({1024, 3, 0, 0})   // 1024x1024, 3ch, None, None
    ->Args({512, 16, 0, 0})   // 512x512, 16ch, None, None
    ->Args({512, 3, 1, 0})    // 512x512, 3ch, ZSTD, None
    ->Args({512, 3, 1, 1})    // 512x512, 3ch, ZSTD, Horizontal
    ->Name("TiffConcept/Write/SizeChannelVariation/uint16")
    ->Unit(benchmark::kMillisecond);

#ifdef HAVE_LIBTIFF
// Write - Size and Channel Variations
BENCHMARK(BM_LibTIFF_Write_SizeChannelVariation<uint8_t>)
    ->Args({256, 1, 0})
    ->Args({512, 3, 0})
    ->Args({1024, 3, 0})
    ->Args({2048, 3, 0})
    ->Args({512, 8, 0})
    ->Args({512, 16, 0})
    ->Args({512, 32, 0})
    ->Args({512, 3, 1})
    ->Name("LibTIFF/Write/SizeChannelVariation/uint8")
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_LibTIFF_Write_SizeChannelVariation<uint16_t>)
    ->Args({512, 3, 0})
    ->Args({1024, 3, 0})
    ->Args({512, 16, 0})
    ->Args({512, 3, 1})
    ->Name("LibTIFF/Write/SizeChannelVariation/uint16")
    ->Unit(benchmark::kMillisecond);
#endif // HAVE_LIBTIFF

// Write - Multi-Page
// Params: num_pages, compression
BENCHMARK(BM_Write_MultiPage<uint8_t>)
    ->Args({10, 0})    // 10 pages, None
    ->Args({50, 0})    // 50 pages, None
    ->Args({10, 1})    // 10 pages, ZSTD
    ->Args({50, 1})    // 50 pages, ZSTD
    ->Name("TiffConcept/Write/MultiPage/uint8")
    ->Unit(benchmark::kMillisecond);

#ifdef HAVE_LIBTIFF
// Write - Multi-Page
BENCHMARK(BM_LibTIFF_Write_MultiPage<uint8_t>)
    ->Args({10, 0})
    ->Args({50, 0})
    ->Args({10, 1})
    ->Args({50, 1})
    ->Name("LibTIFF/Write/MultiPage/uint8")
    ->Unit(benchmark::kMillisecond);

#endif // HAVE_LIBTIFF

#endif

BENCHMARK_MAIN();
