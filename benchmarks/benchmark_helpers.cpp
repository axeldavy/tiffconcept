#include "benchmark_helpers.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#ifdef HAVE_LIBTIFF
#include <tiffio.h>
#endif

// TiffConcept includes for multi-page generation
#include "../tiffconcept/include/tiffconcept/compressors/compressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/compressors/compressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_stream.hpp"
#include "../tiffconcept/include/tiffconcept/tiff_writer.hpp"
#include "../tiffconcept/include/tiffconcept/strategy/write_strategy.hpp"

namespace tiff_bench {

using namespace tiffconcept;

// ============================================================================
// ImageConfig
// ============================================================================

std::string ImageConfig::name() const {
    std::ostringstream oss;
    oss << width << "x" << height;
    if (depth > 1) {
        oss << "x" << depth;
    }
    oss << "_c" << samples_per_pixel;
    
    oss << "_tiles" << tile_width << "x" << tile_height;
    if (tile_depth > 1) {
        oss << "x" << tile_depth;
    }
    
    return oss.str();
}

// ============================================================================
// BenchmarkParams
// ============================================================================

std::string BenchmarkParams::name() const {
    std::ostringstream oss;
    oss << image_config.name();
    
    // Add compression
    switch (storage_config.compression) {
        case CompressionType::None: oss << "_NoComp"; break;
        case CompressionType::ZSTD: oss << "_ZSTD"; break;
    }
    
    // Add predictor if not None
    if (storage_config.predictor != PredictorType::None) {
        switch (storage_config.predictor) {
            case PredictorType::Horizontal: oss << "_PredHoriz"; break;
            default: break;
        }
    }
    
    // Add format
    oss << (storage_config.format == TiffFormat::BigTIFF ? "_BigTIFF" : "_Classic");
    
    // Add endianness if Big
    if (storage_config.endianness == Endianness::Big) {
        oss << "_BigEndian";
    }
    
    // Add pages if multi-page
    if (num_pages > 1) {
        oss << "_" << num_pages << "pages";
    }
    
    return oss.str();
}

// ============================================================================
// ImageGenerator
// ============================================================================

template <typename T>
std::vector<T> ImageGenerator<T>::generate_random(const ImageConfig& config) {
    std::size_t size = config.num_pixels();
    std::vector<T> data(size);
    
    if constexpr (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<T> dist(0.0, 1.0);
        for (auto& val : data) {
            val = dist(rng_);
        }
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& val : data) {
            val = static_cast<T>(dist(rng_));
        }
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        std::uniform_int_distribution<int> dist(0, 65535);
        for (auto& val : data) {
            val = static_cast<T>(dist(rng_));
        }
    } else {
        std::uniform_int_distribution<T> dist(
            std::numeric_limits<T>::min(),
            std::numeric_limits<T>::max()
        );
        for (auto& val : data) {
            val = dist(rng_);
        }
    }
    
    return data;
}

template <typename T>
std::vector<T> ImageGenerator<T>::generate_gradient(const ImageConfig& config) {
    std::size_t size = config.num_pixels();
    std::vector<T> data(size);
    
    for (uint32_t z = 0; z < config.depth; ++z) {
        for (uint32_t y = 0; y < config.height; ++y) {
            for (uint32_t x = 0; x < config.width; ++x) {
                T value;
                if constexpr (std::is_floating_point_v<T>) {
                    value = static_cast<T>((x + y + z) % 256) / T(255);
                } else {
                    value = static_cast<T>((x + y + z) % 256);
                }
                
                for (uint16_t c = 0; c < config.samples_per_pixel; ++c) {
                    std::size_t idx = ((z * config.height + y) * config.width + x) 
                                    * config.samples_per_pixel + c;
                    data[idx] = value;
                }
            }
        }
    }
    
    return data;
}

template <typename T>
std::vector<T> ImageGenerator<T>::generate_constant(const ImageConfig& config, T value) {
    return std::vector<T>(config.num_pixels(), value);
}

// Explicit template instantiations
template class ImageGenerator<uint8_t>;
template class ImageGenerator<uint16_t>;
template class ImageGenerator<uint32_t>;
template class ImageGenerator<float>;
template class ImageGenerator<double>;

// ============================================================================
// TempFileManager
// ============================================================================

TempFileManager::TempFileManager() {
    temp_dir_ = std::filesystem::temp_directory_path() / "tiffconcept_benchmarks";
    std::filesystem::create_directories(temp_dir_);
}

TempFileManager::~TempFileManager() {
    cleanup_all();
}

std::filesystem::path TempFileManager::get_temp_path(const std::string& name) {
    auto path = temp_dir_ / (name + ".tif");
    temp_files_.push_back(path);
    return path;
}

void TempFileManager::cleanup_all() {
    for (const auto& path : temp_files_) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        // Ignore errors during cleanup
    }
    temp_files_.clear();
    
    std::error_code ec;
    std::filesystem::remove(temp_dir_, ec);
}

// ============================================================================
// BenchmarkFixture
// ============================================================================

void BenchmarkFixture::SetUp([[maybe_unused]] const benchmark::State& state) {
    temp_manager_ = std::make_unique<TempFileManager>();
}

void BenchmarkFixture::TearDown([[maybe_unused]] const benchmark::State& state) {
    temp_manager_.reset();
}

// ============================================================================
// MultiPageFileGenerator
// ============================================================================

template <typename T>
std::filesystem::path TiffGenerator<T>::create_file(
    const std::string& name,
    const ImageConfig& image_config,
    const StorageConfig& storage_config,
    uint32_t num_pages,
    ImagePattern pattern) {
    
    auto filepath = temp_manager_.get_temp_path(name);
    
    // Generate image data based on pattern
    std::vector<T> image_data;
    if (pattern == ImagePattern::Random) {
        image_data = generator_.generate_random(image_config);
    } else if (pattern == ImagePattern::Constant) {
        image_data = generator_.generate_constant(image_config, static_cast<T>(128));
    } else {
        image_data = generator_.generate_gradient(image_config);
    }

    // Determine compression and predictor settings
    // Use storage_config settings, but allow override via parameters
    CompressionScheme comp_scheme = CompressionScheme::None;
    Predictor pred = Predictor::None;
    
    CompressionType compression = storage_config.compression;
    
    switch (compression) {
        case CompressionType::None: comp_scheme = CompressionScheme::None; break;
        case CompressionType::ZSTD: comp_scheme = CompressionScheme::ZSTD; break;
    }
    
    switch (storage_config.predictor) {
        case PredictorType::None: pred = Predictor::None; break;
        case PredictorType::Horizontal: pred = Predictor::Horizontal; break;
    }

    bool read_optimized = storage_config.read_optimized;

    // Determine endianness
    constexpr auto little_endian = std::endian::little;
    constexpr auto big_endian = std::endian::big;
    
    // Determine TiffFormat
    constexpr auto classic_format = TiffFormatType::Classic;
    constexpr auto bigtiff_format = TiffFormatType::BigTIFF;
    
    // Create additional tags to override defaults
    using AdditionalTagSpec = TagSpec<
        PhotometricInterpretationTag,
        ExtraSamplesTag
    >;
    ExtractedTags<AdditionalTagSpec> additional_tags;
    
    // Always use MinIsBlack photometric interpretation
    additional_tags.template get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::MinIsBlack;
    
    // Set extra samples for multi-channel images
    if (image_config.samples_per_pixel > 1) {
        std::vector<uint8_t> extra_samples(image_config.samples_per_pixel - 1, 0); // 0 = unspecified
        additional_tags.template get<TagCode::ExtraSamples>() = std::move(extra_samples);
    }
    
    // Create a lambda to write the file with the appropriate template parameters
    auto write_file = [&]<std::endian TargetEndian, TiffFormatType Format, typename IFDStrategy, typename TileStrategy>() {
        if (compression == CompressionType::ZSTD) {
            using CompSpec = CompressorSpec<ZstdCompressorDesc>;
            using WConfig = WriteConfig<IFDStrategy, TileStrategy, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
            using WriterType = TiffWriter<T, CompSpec, WConfig, Format, TargetEndian>;
            
            WriterType writer;
            StreamFileWriter file_writer(filepath.string());
            
            for (uint32_t page = 0; page < num_pages; ++page) {
                Result<void> result;
                if (storage_config.use_strips) {
                    result = writer.template write_stripped_image<ImageLayoutSpec::DHWC>(
                        file_writer,
                        std::span<const T>(image_data),
                        image_config.width, 
                        image_config.height,
                        image_config.tile_height, // rows_per_strip
                        image_config.samples_per_pixel,
                        PlanarConfiguration::Chunky,
                        comp_scheme,
                        pred,
                        additional_tags
                    );
                } else {
                    if (image_config.depth > 1) {
                        result = writer.template write_single_image<ImageLayoutSpec::DHWC>(
                            file_writer,
                            std::span<const T>(image_data),
                            image_config.width, 
                            image_config.height,
                            image_config.depth,
                            image_config.tile_width, 
                            image_config.tile_height,
                            image_config.tile_depth,
                            image_config.samples_per_pixel,
                            PlanarConfiguration::Chunky,
                            comp_scheme,
                            pred,
                            additional_tags
                        );
                    } else {
                        result = writer.template write_single_image<ImageLayoutSpec::DHWC>(
                            file_writer,
                            std::span<const T>(image_data),
                            image_config.width, 
                            image_config.height,
                            image_config.tile_width, 
                            image_config.tile_height,
                            image_config.samples_per_pixel,
                            PlanarConfiguration::Chunky,
                            comp_scheme,
                            pred,
                            additional_tags
                        );
                    }
                }
                
                if (!result) {
                    throw std::runtime_error("Failed to write TIFF image: " + std::string(result.error().message));
                }
            }
        } else {
            using CompSpec = CompressorSpec<NoneCompressorDesc>;
            using WConfig = WriteConfig<IFDStrategy, TileStrategy, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
            using WriterType = TiffWriter<T, CompSpec, WConfig, Format, TargetEndian>;
            
            WriterType writer;
            StreamFileWriter file_writer(filepath.string());
            
            for (uint32_t page = 0; page < num_pages; ++page) {
                Result<void> result;
                if (storage_config.use_strips) {
                    result = writer.template write_stripped_image<ImageLayoutSpec::DHWC>(
                        file_writer,
                        std::span<const T>(image_data),
                        image_config.width, 
                        image_config.height,
                        image_config.tile_height, // rows_per_strip
                        image_config.samples_per_pixel,
                        PlanarConfiguration::Chunky,
                        comp_scheme,
                        pred,
                        additional_tags
                    );
                } else {
                    if (image_config.depth > 1) {
                        result = writer.template write_single_image<ImageLayoutSpec::DHWC>(
                            file_writer,
                            std::span<const T>(image_data),
                            image_config.width, 
                            image_config.height,
                            image_config.depth,
                            image_config.tile_width, 
                            image_config.tile_height,
                            image_config.tile_depth,
                            image_config.samples_per_pixel,
                            PlanarConfiguration::Chunky,
                            comp_scheme,
                            pred,
                            additional_tags
                        );
                    } else {
                        result = writer.template write_single_image<ImageLayoutSpec::DHWC>(
                            file_writer,
                            std::span<const T>(image_data),
                            image_config.width, 
                            image_config.height,
                            image_config.tile_width, 
                            image_config.tile_height,
                            image_config.samples_per_pixel,
                            PlanarConfiguration::Chunky,
                            comp_scheme,
                            pred,
                            additional_tags
                        );
                    }
                }
                
                if (!result) {
                    throw std::runtime_error("Failed to write TIFF image: " + std::string(result.error().message));
                }
            }
        }
    };
    
    // Dispatch based on all configuration options
    if (storage_config.endianness == Endianness::Little) {
        if (storage_config.format == TiffFormat::Classic) {
            if (read_optimized) {
                write_file.template operator()<little_endian, classic_format, IFDAtBeginning, ImageOrderTiles>();
            } else {
                write_file.template operator()<little_endian, classic_format, IFDAtEnd, SequentialTiles>();
            }
        } else {
            if (read_optimized) {
                write_file.template operator()<little_endian, bigtiff_format, IFDAtBeginning, ImageOrderTiles>();
            } else {
                write_file.template operator()<little_endian, bigtiff_format, IFDAtEnd, SequentialTiles>();
            }
        }
    } else {
        if (storage_config.format == TiffFormat::Classic) {
            if (read_optimized) {
                write_file.template operator()<big_endian, classic_format, IFDAtBeginning, ImageOrderTiles>();
            } else {
                write_file.template operator()<big_endian, classic_format, IFDAtEnd, SequentialTiles>();
            }
        } else {
            if (read_optimized) {
                write_file.template operator()<big_endian, bigtiff_format, IFDAtBeginning, ImageOrderTiles>();
            } else {
                write_file.template operator()<big_endian, bigtiff_format, IFDAtEnd, SequentialTiles>();
            }
        }
    }
    
    return filepath;
}

// Explicit template instantiations
template class TiffGenerator<uint8_t>;
template class TiffGenerator<uint16_t>;
template class TiffGenerator<uint32_t>;
template class TiffGenerator<float>;
template class TiffGenerator<double>;

// ============================================================================
// LibTiffGenerator
// ============================================================================

#ifdef HAVE_LIBTIFF

template <typename T>
int LibTiffGenerator<T>::to_libtiff_compression(CompressionType comp) {
    switch (comp) {
        case CompressionType::None: return COMPRESSION_NONE;
        case CompressionType::ZSTD: return COMPRESSION_ZSTD;
        default: return COMPRESSION_NONE;
    }
}

template <typename T>
int LibTiffGenerator<T>::to_libtiff_predictor(PredictorType pred) {
    switch (pred) {
        case PredictorType::None: return 1; // No predictor
        case PredictorType::Horizontal: return 2; // Horizontal differencing
        default: return 1;
    }
}

template <typename T>
uint16_t LibTiffGenerator<T>::get_sample_format() {
    if constexpr (std::is_floating_point_v<T>) {
        return SAMPLEFORMAT_IEEEFP;
    } else if constexpr (std::is_signed_v<T>) {
        return SAMPLEFORMAT_INT;
    } else {
        return SAMPLEFORMAT_UINT;
    }
}

template <typename T>
std::filesystem::path LibTiffGenerator<T>::create_file(
    const std::string& name,
    const ImageConfig& image_config,
    const StorageConfig& storage_config,
    uint32_t num_pages,
    ImagePattern pattern) {
    
    auto filepath = temp_manager_.get_temp_path(name);
    
    // Generate image data based on pattern
    std::vector<T> image_data;
    if (pattern == ImagePattern::Random) {
        image_data = generator_.generate_random(image_config);
    } else if (pattern == ImagePattern::Constant) {
        image_data = generator_.generate_constant(image_config, static_cast<T>(128));
    } else {
        image_data = generator_.generate_gradient(image_config);
    }

    // Determine compression and predictor settings
    int libtiff_comp = to_libtiff_compression(storage_config.compression);
    int libtiff_pred = to_libtiff_predictor(storage_config.predictor);
    
    // Open file with appropriate mode based on endianness and format
    std::string mode = "w";
    if (storage_config.format == TiffFormat::BigTIFF) {
        mode += "8"; // BigTIFF mode
    }
    if (storage_config.endianness == Endianness::Big) {
        mode += "b"; // Big-endian mode
    } else {
        mode += "l"; // Little-endian mode
    }
    
    TIFF* tif = TIFFOpen(filepath.string().c_str(), mode.c_str());
    if (!tif) {
        throw std::runtime_error("Failed to open TIFF file for writing: " + filepath.string());
    }
    
    std::size_t tile_or_strip_size = storage_config.use_strips 
        ? image_config.width * image_config.tile_height * image_config.samples_per_pixel
        : image_config.tile_width * image_config.tile_height * image_config.tile_depth * image_config.samples_per_pixel;
    std::vector<T> buffer(tile_or_strip_size);
    
    for (uint32_t page = 0; page < num_pages; ++page) {
        // Set basic tags
        TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, image_config.width);
        TIFFSetField(tif, TIFFTAG_IMAGELENGTH, image_config.height);
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, image_config.samples_per_pixel);
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, sizeof(T) * 8);
        TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, get_sample_format());
        TIFFSetField(tif, TIFFTAG_COMPRESSION, libtiff_comp);
        TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        
        // Set predictor if using compression
        if (storage_config.compression != CompressionType::None && storage_config.predictor != PredictorType::None) {
            TIFFSetField(tif, TIFFTAG_PREDICTOR, libtiff_pred);
        }
        
        // Always use PHOTOMETRIC_MINISBLACK with extra samples for multi-channel
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
        
        // Set extra samples for channels beyond the first
        if (image_config.samples_per_pixel > 1) {
            uint16_t num_extra = image_config.samples_per_pixel - 1;
            std::vector<uint16_t> extra_samples(num_extra, EXTRASAMPLE_UNSPECIFIED);
            TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, num_extra, extra_samples.data());
        }
        
        if (storage_config.use_strips) {
            // Write as strips
            TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, image_config.tile_height);
            
            for (uint32_t y = 0; y < image_config.height; y += image_config.tile_height) {
                uint32_t rows = std::min(image_config.tile_height, image_config.height - y);
                
                // Copy strip data
                for (uint32_t row = 0; row < rows; ++row) {
                    for (uint32_t x = 0; x < image_config.width; ++x) {
                        for (uint16_t c = 0; c < image_config.samples_per_pixel; ++c) {
                            std::size_t src_idx = ((y + row) * image_config.width + x) * image_config.samples_per_pixel + c;
                            std::size_t dst_idx = (row * image_config.width + x) * image_config.samples_per_pixel + c;
                            buffer[dst_idx] = image_data[src_idx];
                        }
                    }
                }
                
                TIFFWriteEncodedStrip(tif, y / image_config.tile_height, buffer.data(), 
                                     rows * image_config.width * image_config.samples_per_pixel * sizeof(T));
            }
        } else {
            // Write as tiles
            TIFFSetField(tif, TIFFTAG_TILEWIDTH, image_config.tile_width);
            TIFFSetField(tif, TIFFTAG_TILELENGTH, image_config.tile_height);
            
            if (image_config.depth > 1) {
                TIFFSetField(tif, TIFFTAG_IMAGEDEPTH, image_config.depth);
                TIFFSetField(tif, TIFFTAG_TILEDEPTH, image_config.tile_depth);
            }
            
            for (uint32_t z = 0; z < image_config.depth; z += image_config.tile_depth) {
                for (uint32_t y = 0; y < image_config.height; y += image_config.tile_height) {
                    for (uint32_t x = 0; x < image_config.width; x += image_config.tile_width) {
                        uint32_t td = std::min(image_config.tile_depth, image_config.depth - z);
                        uint32_t th = std::min(image_config.tile_height, image_config.height - y);
                        uint32_t tw = std::min(image_config.tile_width, image_config.width - x);
                        
                        // Copy tile data
                        for (uint32_t tz = 0; tz < td; ++tz) {
                            for (uint32_t ty = 0; ty < th; ++ty) {
                                for (uint32_t tx = 0; tx < tw; ++tx) {
                                    for (uint16_t c = 0; c < image_config.samples_per_pixel; ++c) {
                                        std::size_t src_idx = (((z + tz) * image_config.height + (y + ty)) * image_config.width + (x + tx)) 
                                                             * image_config.samples_per_pixel + c;
                                        std::size_t dst_idx = ((tz * image_config.tile_height + ty) * image_config.tile_width + tx) 
                                                             * image_config.samples_per_pixel + c;
                                        buffer[dst_idx] = image_data[src_idx];
                                    }
                                }
                            }
                        }
                        
                        if (image_config.depth > 1) {
                            TIFFWriteTile(tif, buffer.data(), x, y, z, 0);
                        } else {
                            TIFFWriteTile(tif, buffer.data(), x, y, 0, 0);
                        }
                    }
                }
            }
        }
        
        // Write directory for next page
        if (page < num_pages - 1) {
            TIFFWriteDirectory(tif);
        }
    }
    
    TIFFClose(tif);
    return filepath;
}

// Explicit template instantiations
template class LibTiffGenerator<uint8_t>;
template class LibTiffGenerator<uint16_t>;
template class LibTiffGenerator<uint32_t>;
template class LibTiffGenerator<float>;
template class LibTiffGenerator<double>;

#endif // HAVE_LIBTIFF

// ============================================================================
// BenchmarkStats
// ============================================================================

double BenchmarkStats::throughput_mbps() const {
    if (mean_ns <= 0.0) return 0.0;
    // This should be set externally based on data size
    return 0.0;
}

std::string BenchmarkStats::to_string() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "Mean: " << (mean_ns / 1e6) << " ms, ";
    oss << "StdDev: " << (stddev_ns / 1e6) << " ms, ";
    oss << "Min: " << (min_ns / 1e6) << " ms, ";
    oss << "Max: " << (max_ns / 1e6) << " ms, ";
    oss << "Iterations: " << iterations;
    return oss.str();
}

} // namespace tiff_bench
