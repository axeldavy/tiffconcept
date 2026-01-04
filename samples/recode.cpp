
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
#include "../tiffconcept/include/tiffconcept/compressors/compressor_jpegls.hpp"
#include "../tiffconcept/include/tiffconcept/compressors/compressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/compressors/compressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_jpegls.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_standard.hpp"
#include "../tiffconcept/include/tiffconcept/decompressors/decompressor_zstd.hpp"
#include "../tiffconcept/include/tiffconcept/types/result.hpp"
#include "../tiffconcept/include/tiffconcept/ifd.hpp"
#include "../tiffconcept/include/tiffconcept/tag_extraction.hpp"
#include "../tiffconcept/include/tiffconcept/parsing.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_stream.hpp"
#include "../tiffconcept/include/tiffconcept/tiff_writer.hpp"
#include "../tiffconcept/include/tiffconcept/strategy/write_strategy.hpp"

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

namespace fs = std::filesystem;
using namespace tiffconcept;

// ============================================================================
// DecompSpec supporting None and Zstd
// ============================================================================

using BenchDecompSpec = DecompressorSpec<
    NoneDecompressorDesc,
    ZstdDecompressorDesc,
    JpeglsDecompressorDesc
>;

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
// Tiffconcept Reader Dispatcher
// ============================================================================

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
Result<std::pair<std::unique_ptr<uint8_t[]>, ImageShape>> dispatch_and_read(
    FileReader& file_reader,
    const ExtractedTags<ImageSpecTiffOrStrip>& tags,
    std::optional<uint32_t> tile_size) {

    // Extract image shape
    static ImageShape shape;
    auto shape_result = shape.update_from_metadata(tags);
    if (!shape_result.is_ok()) return shape_result.error();
    
    // Validate pixel type matches
    auto validate_result = shape.validate_pixel_type<PixelType>();
    if (!validate_result.is_ok()) {
        return validate_result.error();
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
    auto output = std::make_unique<uint8_t[]>(region.num_samples() * sizeof(PixelType));
    std::span<PixelType> output_span(reinterpret_cast<PixelType*>(output.get()), region.num_samples());

    
    Result<void> read_result = Ok();
    static CPULimitedReader<PixelType, BenchDecompSpec> reader{};
    read_result = reader.template read_region<ImageLayoutSpec::DHWC>(file_reader, tags, region, output_span);
    if (read_result.is_error()) return read_result.error();
    return std::make_pair(std::move(output), std::move(shape));
}

Result<std::pair<std::unique_ptr<uint8_t[]>, ImageShape>> read_with_tiffconcept(
    const std::string& path,
    std::optional<uint32_t> tile_size) {

    FileReader file_reader;
    auto open_result = file_reader.open(path);
    if (!open_result.is_ok()) {
        return open_result.error();
    }
    
    // Step 1: Extract tags (try all 4 IFD format/endianness combinations)
    auto tag_result = extract_tags_from_file(file_reader);
    if (!tag_result.success) {
        return Err(Error::Code::InvalidFormat, "Failed to extract tags from file: " + path);
    }
    
    // Step 2: Try all pixel types
    auto result = dispatch_and_read<uint8_t>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    result = dispatch_and_read<uint16_t>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    result = dispatch_and_read<uint32_t>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    result = dispatch_and_read<uint64_t>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    result = dispatch_and_read<int8_t>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    result = dispatch_and_read<int16_t>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    result = dispatch_and_read<int32_t>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    result = dispatch_and_read<int64_t>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    result = dispatch_and_read<float>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    result = dispatch_and_read<double>(file_reader, tag_result.tags, tile_size); if (result.is_ok()) return result;
    
    return Err(Error::Code::InvalidFormat, "Unsupported pixel type in file: " + path);
}

/// Tiffconcept write

/// image configuration
struct ImageConfig {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint16_t samples_per_pixel;
    uint32_t tile_width;
    uint32_t tile_height; // use for rows_per_strip if use_strips is true
    uint32_t tile_depth = 1;
    
    std::string name() const {
        return std::to_string(width) + "x" + std::to_string(height) + "x" + std::to_string(depth) + "_ch" + std::to_string(samples_per_pixel);
    }
    std::size_t num_pixels() const {
        return static_cast<std::size_t>(width) * height * depth * samples_per_pixel;
    }
    std::size_t num_bytes(std::size_t bytes_per_pixel) const {
        return num_pixels() * bytes_per_pixel;
    }
};

/// image storage type
struct StorageConfig {
    bool use_strips = false;
    bool read_optimized = true;
    CompressionScheme compression = CompressionScheme::None;
    Predictor predictor = Predictor::None;
    std::endian endianness = std::endian::little;
    TiffFormatType format = TiffFormatType::Classic;
};

void write_file(
    const std::string& file_path,
    const ImageConfig& image_config,
    const StorageConfig& storage_config,
    std::unique_ptr<const uint8_t[]> image_data,
    ImageShape& input_shape) {

    // Determine compression and predictor settings
    // Use storage_config settings, but allow override via parameters
    CompressionScheme comp_scheme = storage_config.compression;
    Predictor pred = storage_config.predictor;


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
    auto write_file = [&]<std::endian TargetEndian, TiffFormatType Format, typename IFDStrategy, typename TileStrategy, typename T>() {
        if (comp_scheme == CompressionScheme::ZSTD) {
            using CompSpec = CompressorSpec<ZstdCompressorDesc>;
            using WConfig = WriteConfig<IFDStrategy, TileStrategy, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
            using WriterType = TiffWriter<T, CompSpec, WConfig, Format, TargetEndian>;
            
            WriterType writer;
            StreamFileWriter file_writer(file_path);

            // Cast image_data to proper type
            std::span<const T> data_span(
                reinterpret_cast<const T*>(image_data.get()), 
                input_shape.num_elements()
            );

            Result<void> result;
            if (storage_config.use_strips) {
                result = writer.template write_stripped_image<ImageLayoutSpec::DHWC>(
                    file_writer,
                    data_span,
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
                        data_span,
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
                        data_span,
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
                
                if (!result) {
                    throw std::runtime_error("Failed to write TIFF image: " + std::string(result.error().message));
                }
            }
        } else if (comp_scheme == CompressionScheme::JPEG_LS) {
            using CompSpec = CompressorSpec<JpeglsCompressorDesc>;
            using WConfig = WriteConfig<IFDStrategy, TileStrategy, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
            using WriterType = TiffWriter<T, CompSpec, WConfig, Format, TargetEndian>;
            
            WriterType writer;
            StreamFileWriter file_writer(file_path);

            std::span<const T> data_span(
                reinterpret_cast<const T*>(image_data.get()), 
                input_shape.num_elements()
            );
            
            auto write_result = writer.template write_single_image<ImageLayoutSpec::DHWC>(
                file_writer, 
                data_span, 
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
            
            if (!write_result.is_ok()) {
                std::cerr << "Failed to write TIFF with JPEG-LS: " << write_result.error().message << "\n";
                std::exit(1);
            }
        } else {
            using CompSpec = CompressorSpec<NoneCompressorDesc>;
            using WConfig = WriteConfig<IFDStrategy, TileStrategy, DirectWrite<StreamFileWriter>, TwoPassOffsets>;
            using WriterType = TiffWriter<T, CompSpec, WConfig, Format, TargetEndian>;
            
            WriterType writer;
            StreamFileWriter file_writer(file_path);

            // Cast image_data to proper type
            std::span<const T> data_span(
                reinterpret_cast<const T*>(image_data.get()), 
                input_shape.num_elements()
            );

            Result<void> result;
            if (storage_config.use_strips) {
                result = writer.template write_stripped_image<ImageLayoutSpec::DHWC>(
                    file_writer,
                    data_span,
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
                        data_span,
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
                        data_span,
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
                
                if (!result) {
                    throw std::runtime_error("Failed to write TIFF image: " + std::string(result.error().message));
                }
            }
        }
    };
    
    // Dispatch based on all configuration options and pixel type
    auto dispatch_by_type = [&]<std::endian TargetEndian, TiffFormatType Format>() {
        // Determine pixel type from input_shape
        uint16_t bps = input_shape.bits_per_sample();
        SampleFormat fmt = input_shape.sample_format();
        
        if (fmt == SampleFormat::UnsignedInt) {
            if (bps == 8) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, uint8_t>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, uint8_t>();
                }
            } else if (bps == 16) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, uint16_t>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, uint16_t>();
                }
            } else if (bps == 32) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, uint32_t>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, uint32_t>();
                }
            } else if (bps == 64) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, uint64_t>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, uint64_t>();
                }
            }
        } else if (fmt == SampleFormat::SignedInt) {
            if (bps == 8) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, int8_t>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, int8_t>();
                }
            } else if (bps == 16) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, int16_t>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, int16_t>();
                }
            } else if (bps == 32) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, int32_t>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, int32_t>();
                }
            } else if (bps == 64) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, int64_t>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, int64_t>();
                }
            }
        } else if (fmt == SampleFormat::IEEEFloat) {
            if (bps == 32) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, float>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, float>();
                }
            } else if (bps == 64) {
                if (read_optimized) {
                    write_file.template operator()<TargetEndian, Format, IFDAtBeginning, ImageOrderTiles, double>();
                } else {
                    write_file.template operator()<TargetEndian, Format, IFDAtEnd, SequentialTiles, double>();
                }
            }
        }
    };
    
    if (storage_config.endianness == std::endian::little) {
        if (storage_config.format == TiffFormatType::Classic) {
            dispatch_by_type.template operator()<little_endian, classic_format>();
        } else {
            dispatch_by_type.template operator()<little_endian, bigtiff_format>();
        }
    } else {
        if (storage_config.format == TiffFormatType::Classic) {
            dispatch_by_type.template operator()<big_endian, classic_format>();
        } else {
            dispatch_by_type.template operator()<big_endian, bigtiff_format>();
        }
    }
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <input_tiff> <output_dir> <tile_width> <tile_height>\n";
    std::cout << "\nCreates 5 variants of the input TIFF:\n";
    std::cout << "  1. Uncompressed\n";
    std::cout << "  2. ZSTD compressed\n";
    std::cout << "  3. ZSTD + Horizontal predictor\n";
    std::cout << "  4. ZSTD + LOCO predictor\n";
    std::cout << "  5. JPEG-LS lossless\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << prog_name << " input.tif output/ 512 512\n";
}

int main(int argc, char** argv) {
    if (argc != 5) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_dir = argv[2];
    uint32_t tile_width = std::stoul(argv[3]);
    uint32_t tile_height = std::stoul(argv[4]);

    // Verify input file exists
    if (!fs::exists(input_path)) {
        std::cerr << "Error: Input file does not exist: " << input_path << "\n";
        return 1;
    }

    // Create output directory if needed
    if (!fs::exists(output_dir)) {
        fs::create_directories(output_dir);
    }

    std::cout << "Reading input TIFF: " << input_path << "\n";
    
    // Read the input TIFF
    auto read_result = read_with_tiffconcept(input_path, std::nullopt);
    if (read_result.is_error()) {
        std::cerr << "Error reading input file: " << read_result.error().message << "\n";
        return 1;
    }

    auto [image_data, input_shape] = std::move(read_result.value());
    
    // Setup image config from input shape
    ImageConfig image_config{
        input_shape.image_width(),
        input_shape.image_height(),
        input_shape.image_depth(),
        input_shape.samples_per_pixel(),
        tile_width,
        tile_height,
        std::min(input_shape.image_depth(), static_cast<uint32_t>(1))
    };

    std::cout << "Image: " << image_config.width << "x" << image_config.height 
              << "x" << image_config.depth << ", " << image_config.samples_per_pixel 
              << " channels, " << input_shape.bits_per_sample() << " bits/sample\n";
    std::cout << "Tile size: " << tile_width << "x" << tile_height << "\n\n";

    // Get base filename without extension
    fs::path input_file(input_path);
    std::string base_name = input_file.stem().string();

    // Define the 4 variants to create
    struct Variant {
        std::string name;
        CompressionScheme compression;
        Predictor predictor;
    };

    std::vector<Variant> variants = {
        {"uncompressed", CompressionScheme::None, Predictor::None},
        {"zstd", CompressionScheme::ZSTD, Predictor::None},
        {"zstd_horizontal", CompressionScheme::ZSTD, Predictor::Horizontal},
        {"zstd_loco", CompressionScheme::ZSTD, Predictor::LOCO_I},
        {"jpeg_ls", CompressionScheme::JPEG_LS, Predictor::None}
    };

    // Create each variant
    for (const auto& variant : variants) {
        std::string output_path = output_dir + "/" + base_name + "_" + variant.name + ".tif";
        
        StorageConfig storage_config{
            false, // use_strips
            true,  // read_optimized
            variant.compression,
            variant.predictor,
            std::endian::little,
            TiffFormatType::BigTIFF
        };

        std::cout << "Writing " << variant.name << " to " << output_path << "... " << std::flush;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Clone the image data for writing (write_file expects unique_ptr)
        auto data_copy = std::make_unique<uint8_t[]>(input_shape.num_elements() * (input_shape.bits_per_sample() / 8));
        std::memcpy(data_copy.get(), image_data.get(), input_shape.num_elements() * (input_shape.bits_per_sample() / 8));
        
        write_file(output_path, image_config, storage_config, std::move(data_copy), input_shape);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "done in " << duration.count() << " ms\n";
    }

    std::cout << "\nAll variants created successfully!\n";
    return 0;
}
