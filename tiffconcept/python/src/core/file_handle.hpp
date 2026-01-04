#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <mutex>

#include "tiffconcept/types/result.hpp"
#include "tiffconcept/types/tiff_spec.hpp"
#include "tiffconcept/types/tag_spec.hpp"
#include "tiffconcept/types/tag_spec_examples.hpp"
#include "tiffconcept/ifd.hpp"
#include "tiffconcept/tag_extraction.hpp"
#include "tiffconcept/image_shape.hpp"
#include "tiffconcept/image_reader.hpp"
#include "tiffconcept/reader_base.hpp"
#include "tiffconcept/decompressors/decompressor_standard.hpp"
#include "tiffconcept/decompressors/decompressor_zstd.hpp"
#include "tiffconcept/decompressors/decompressor_jpegls.hpp"
#include "tiffconcept/decompressors/decompressor_jpegxl.hpp"

// Platform-specific file readers
#if defined(HAVE_LIBURING)
    #include "tiffconcept/readers/reader_unix_io_uring.hpp"
#endif
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    #include "tiffconcept/readers/reader_unix_pread.hpp"
#endif
#if defined(_WIN32) || defined(_WIN64)
    #include "tiffconcept/readers/reader_windows.hpp"
    #include "tiffconcept/readers/reader_windows_async.hpp"
#endif
#include "tiffconcept/readers/reader_stream.hpp"

namespace tiffconcept::python {

// Decompressor spec supporting common formats
using PyDecompressorSpec = DecompressorSpec<
    NoneDecompressorDesc,
    ZstdDecompressorDesc,
    JpeglsDecompressorDesc,
    JpegxlDecompressorDesc,
    PackBitsDecompressorDesc
>;

// Tag spec for reading pages
using PyTagSpec = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    OptTag_t<PhotometricInterpretationTag>,
    OptTag_t<ImageDescriptionTag>,
    OptTag_t<StripOffsetsTag_BigTIFF>,
    OptTag_t<SamplesPerPixelTag>,
    OptTag_t<RowsPerStripTag>,
    OptTag_t<StripByteCountsTag_BigTIFF>,
    OptTag_t<PlanarConfigurationTag>,
    OptTag_t<PredictorTag>,
    OptTag_t<TileWidthTag>,
    OptTag_t<TileLengthTag>,
    OptTag_t<TileOffsetsTag_BigTIFF>,
    OptTag_t<TileByteCountsTag_BigTIFF>,
    OptTag_t<ExtraSamplesTag>,
    OptTag_t<SampleFormatTag>,
    OptTag_t<ImageDepthTag>,
    OptTag_t<TileDepthTag>
>;

// Verify PyTagSpec supports both tiled and stripped images
static_assert(TiledImageTagSpec<PyTagSpec>, 
              "PyTagSpec must satisfy TiledImageTagSpec concept for tiled image reading");
static_assert(StrippedImageTagSpec<PyTagSpec>, 
              "PyTagSpec must satisfy StrippedImageTagSpec concept for stripped image reading");



enum class PixelTypeEnum {
    Unknown,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Int8,
    Int16,
    Int32,
    Int64,
    Float32,
    Float64
};


/**
 * @brief Information about a single page in a TIFF file
 */
struct PageInfo {
    ifd::IFDOffset offset;
    ExtractedTags<PyTagSpec> tags;
    ImageShape shape;
    bool is_valid = false;
    PixelTypeEnum get_pixel_type() const;
};


/**
 * @brief Manages file handle, IFD chain, and page caching for TIFF files
 * 
 * This class provides:
 * - Platform-specific file reader selection (io_uring > pread > Windows async > Windows > stream)
 * - Lazy loading and caching of page metadata
 * - Thread-safe access to cached pages
 * - IFD chain traversal
 */
class FileHandle {
private:
    // Platform-specific reader type
    #if defined(HAVE_LIBURING)
        using FileReaderType = IoUringFileReader;
        static constexpr bool is_async_reader = true;
    #elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        using FileReaderType = PreadFileReader;
        static constexpr bool is_async_reader = false;
    #elif defined(_WIN32) || defined(_WIN64)
        // Try Windows async (IOCP) first
        using FileReaderType = IOCPFileReader;
        static constexpr bool is_async_reader = true;
    #else
        using FileReaderType = StreamFileReader;
        static constexpr bool is_async_reader = false;
    #endif

    std::unique_ptr<FileReaderType> reader_;
    std::string filepath_;
    bool is_open_ = false;
    
    // File format info
    TiffFormatType format_;
    std::endian endian_;
    
    // Page caching
    std::vector<PageInfo> pages_;
    bool pages_loaded_ = false;
    std::mutex pages_mutex_;

public:
    FileHandle();
    ~FileHandle();

    // No copy, move only
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle(FileHandle&&) = delete; // blocked by std::mutex
    FileHandle& operator=(FileHandle&&) = delete;

    /**
     * @brief Open a TIFF file
     * @param filepath Path to the TIFF file
     * @return Result indicating success or error
     */
    Result<void> open(const std::string& filepath);

    /**
     * @brief Close the file
     */
    void close();

    /**
     * @brief Check if file is open
     * @return True if file is open
     */
    bool is_open() const { return is_open_; }

    /**
     * @brief Get the number of pages in the file
     * @return Number of pages
     */
    size_t num_pages();

    /**
     * @brief Get page information by index
     * @param page_index Zero-based page index
     * @return Result containing PageInfo or error
     */
    Result<PageInfo> get_page_info(size_t page_index);

    /**
     * @brief Read a region from a page
     * 
     * This function dispatches to the appropriate reader based on:
     * - Platform (io_uring, pread, Windows async, Windows, stream)
     * - Threading preference (SimpleReader, CPULimitedReader, FastReader)
     * - Layout specification
     * 
     * @param page_index Zero-based page index to read from
     * @param region Region to read
     * @param output Output buffer span
     * @param layout Memory layout for output
     * @param use_threading Whether to use multi-threaded reader
     * @return Result indicating the found pixel type on success or error
     */
    Result<PixelTypeEnum> read_region(
        size_t page_index,
        const ImageRegion& region,
        std::span<uint8_t> output,
        ImageLayoutSpec layout,
        bool use_threading);

    /**
     * @brief Get the TIFF format type
     * @return TiffFormatType (Classic or BigTIFF)
     */
    TiffFormatType get_format() const { return format_; }

    /**
     * @brief Get the byte order
     * @return std::endian (little or big)
     */
    std::endian get_endian() const { return endian_; }

private:
    // Load all page IFD offsets and metadata
    Result<void> load_all_pages();
    
    // Load pages up to specified index (inclusive)
    Result<void> load_pages_up_to(size_t target_index);
    
    // Extract tags for a specific format/endian combination
    template<TiffFormatType Format, std::endian Order>
    Result<PageInfo> try_extract_page_tags(ifd::IFDOffset offset);
    
    // Detect format and endianness
    Result<void> detect_format();

    /// Read dispatch, no validation
    template<typename PixelType>
    Result<void> read_region_dispatch(
        const PageInfo& page_info,
        const ImageRegion& region,
        std::span<PixelType> output,
        ImageLayoutSpec layout_spec,
        bool use_threading);
};

} // namespace tiffconcept::python
