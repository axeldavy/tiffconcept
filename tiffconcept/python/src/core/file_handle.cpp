#include "file_handle.hpp"
#include <algorithm>

namespace tiffconcept::python {

PixelTypeEnum PageInfo::get_pixel_type() const {
    if (!is_valid) {
        return PixelTypeEnum::Unknown;
    }
    uint16_t bps = shape.bits_per_sample();
    SampleFormat fmt = shape.sample_format();
    
    // Map BitsPerSample and SampleFormat to PixelTypeEnum
    if (fmt == SampleFormat::UnsignedInt) {
        if (bps == 8) {
            return PixelTypeEnum::UInt8;
        } else if (bps == 16) {
            return PixelTypeEnum::UInt16;
        } else if (bps == 32) {
            return PixelTypeEnum::UInt32;
        } else if (bps == 64) {
            return PixelTypeEnum::UInt64;
        }
    } else if (fmt == SampleFormat::SignedInt) {
        if (bps == 8) {
            return PixelTypeEnum::Int8;
        } else if (bps == 16) {
            return PixelTypeEnum::Int16;
        } else if (bps == 32) {
            return PixelTypeEnum::Int32;
        } else if (bps == 64) {
            return PixelTypeEnum::Int64;
        }
    } else if (fmt == SampleFormat::IEEEFloat) {
        if (bps == 32) {
            return PixelTypeEnum::Float32;
        } else if (bps == 64) {
            return PixelTypeEnum::Float64;
        }
    }
    
    return PixelTypeEnum::Unknown; // Unsupported combination
}

FileHandle::FileHandle() : reader_(std::make_unique<FileReaderType>()) {}

FileHandle::~FileHandle() {
    close();
}

Result<void> FileHandle::open(const std::string& filepath) {
    if (is_open_) {
        close();
    }
    
    filepath_ = filepath;
    auto result = reader_->open(filepath);
    if (result.is_error()) {
        return result.error();
    }
    
    is_open_ = true;
    
    // Detect format and endianness
    auto detect_result = detect_format();
    if (detect_result.is_error()) {
        close();
        return detect_result.error();
    }
    
    return Ok();
}

void FileHandle::close() {
    if (is_open_) {
        reader_->close();
        is_open_ = false;
        pages_.clear();
        pages_loaded_ = false;
    }
}

Result<void> FileHandle::detect_format() {
    // Try all 4 combinations to detect format
    // Classic little endian (most common)
    auto result = ifd::get_first_ifd_offset<FileReaderType, TiffFormatType::Classic, std::endian::little>(*reader_);
    if (result.is_ok()) {
        format_ = TiffFormatType::Classic;
        endian_ = std::endian::little;
        return Ok();
    }
    
    // BigTIFF little endian
    result = ifd::get_first_ifd_offset<FileReaderType, TiffFormatType::BigTIFF, std::endian::little>(*reader_);
    if (result.is_ok()) {
        format_ = TiffFormatType::BigTIFF;
        endian_ = std::endian::little;
        return Ok();
    }
    
    // Classic big endian
    result = ifd::get_first_ifd_offset<FileReaderType, TiffFormatType::Classic, std::endian::big>(*reader_);
    if (result.is_ok()) {
        format_ = TiffFormatType::Classic;
        endian_ = std::endian::big;
        return Ok();
    }
    
    // BigTIFF big endian
    result = ifd::get_first_ifd_offset<FileReaderType, TiffFormatType::BigTIFF, std::endian::big>(*reader_);
    if (result.is_ok()) {
        format_ = TiffFormatType::BigTIFF;
        endian_ = std::endian::big;
        return Ok();
    }
    
    return Err(Error::Code::InvalidFormat, "Not a valid TIFF file or unsupported format");
}

template<TiffFormatType Format, std::endian Order>
Result<PageInfo> FileHandle::try_extract_page_tags(ifd::IFDOffset offset) {
    PageInfo page_info;
    page_info.offset = offset;
    
    // Get first IFD offset if not provided
    if (offset.value == 0) {
        auto ifd_offset_result = ifd::get_first_ifd_offset<FileReaderType, Format, Order>(*reader_);
        if (ifd_offset_result.is_error()) {
            return ifd_offset_result.error();
        }
        page_info.offset = ifd_offset_result.value();
    }
    
    // Read IFD header
    auto ifd_header_result = ifd::read_ifd_header<FileReaderType, Format, Order>(*reader_, page_info.offset);
    if (ifd_header_result.is_error()) {
        return ifd_header_result.error();
    }
    
    // Read IFD tags
    std::vector<parsing::TagType<Format, Order>> tags_raw;
    auto next_ifd_result = ifd::read_ifd_tags<FileReaderType, Format, Order>(
        *reader_, ifd_header_result.value(), tags_raw);
    if (next_ifd_result.is_error()) {
        return next_ifd_result.error();
    }
    
    // Extract tags into structured form
    auto extract_result = page_info.tags.template extract<FileReaderType, Format, Order>(*reader_, tags_raw);
    if (extract_result.is_error()) {
        // Page exists but doesn't contain valid image metadata
        page_info.is_valid = false;
        return page_info;
    }
    
    // Update image shape from metadata
    auto shape_result = page_info.shape.update_from_metadata(page_info.tags);
    if (shape_result.is_error()) {
        // Page exists but doesn't contain valid image metadata
        page_info.is_valid = false;
        return page_info;
    }
    
    page_info.is_valid = true;
    return page_info;
}

Result<void> FileHandle::load_pages_up_to(size_t target_index) {
    std::lock_guard<std::mutex> lock(pages_mutex_);
    
    // If all pages already loaded, or we already have enough
    if (pages_loaded_ || pages_.size() > target_index) {
        return Ok();
    }
    
    // Lambda to load pages incrementally
    auto load_pages_typed = [this, target_index]<TiffFormatType Format, std::endian Order>() -> Result<void> {
        ifd::IFDOffset current_offset;
        
        // If no pages loaded yet, start from first IFD
        if (pages_.empty()) {
            auto first_ifd_result = ifd::get_first_ifd_offset<FileReaderType, Format, Order>(*reader_);
            if (first_ifd_result.is_error()) {
                return first_ifd_result.error();
            }
            current_offset = first_ifd_result.value();
        } else {
            // Resume from last known page's next offset
            // Need to get the next offset from the last loaded page
            auto ifd_header_result = ifd::read_ifd_header<FileReaderType, Format, Order>(
                *reader_, pages_.back().offset);
            if (ifd_header_result.is_error()) {
                return ifd_header_result.error();
            }
            
            std::vector<parsing::TagType<Format, Order>> tags_raw;
            auto next_ifd_result = ifd::read_ifd_tags<FileReaderType, Format, Order>(
                *reader_, ifd_header_result.value(), tags_raw);
            if (next_ifd_result.is_error()) {
                return next_ifd_result.error();
            }
            
            current_offset = next_ifd_result.value();
        }
        
        // Load pages until we reach target_index or run out of pages
        while (!current_offset.is_null() && pages_.size() <= target_index) {
            auto page_result = try_extract_page_tags<Format, Order>(current_offset);
            if (page_result.is_error()) {
                return page_result.error();
            }
            
            pages_.push_back(std::move(page_result.value()));
            
            // Get next IFD offset
            auto ifd_header_result = ifd::read_ifd_header<FileReaderType, Format, Order>(*reader_, current_offset);
            if (ifd_header_result.is_error()) {
                break;
            }
            
            std::vector<parsing::TagType<Format, Order>> tags_raw;
            auto next_ifd_result = ifd::read_ifd_tags<FileReaderType, Format, Order>(
                *reader_, ifd_header_result.value(), tags_raw);
            if (next_ifd_result.is_error()) {
                break;
            }
            
            current_offset = next_ifd_result.value();
        }
        
        // If we've hit a null offset, all pages are loaded
        if (current_offset.is_null()) {
            pages_loaded_ = true;
        }
        
        return Ok();
    };
    
    // Dispatch based on format and endianness
    Result<void> result = Err(Error::Code::InvalidFormat, "Unknown format");
    
    if (format_ == TiffFormatType::Classic && endian_ == std::endian::little) {
        result = load_pages_typed.template operator()<TiffFormatType::Classic, std::endian::little>();
    } else if (format_ == TiffFormatType::Classic && endian_ == std::endian::big) {
        result = load_pages_typed.template operator()<TiffFormatType::Classic, std::endian::big>();
    } else if (format_ == TiffFormatType::BigTIFF && endian_ == std::endian::little) {
        result = load_pages_typed.template operator()<TiffFormatType::BigTIFF, std::endian::little>();
    } else if (format_ == TiffFormatType::BigTIFF && endian_ == std::endian::big) {
        result = load_pages_typed.template operator()<TiffFormatType::BigTIFF, std::endian::big>();
    }
    
    return result;
}

Result<void> FileHandle::load_all_pages() {
    std::lock_guard<std::mutex> lock(pages_mutex_);
    
    if (pages_loaded_) {
        return Ok();
    }
    
    // Dispatch based on detected format and endianness
    auto load_pages_typed = [this]<TiffFormatType Format, std::endian Order>() -> Result<void> {
        ifd::IFDOffset current_offset;
        
        // If no pages loaded yet, start from first IFD
        if (pages_.empty()) {
            auto first_ifd_result = ifd::get_first_ifd_offset<FileReaderType, Format, Order>(*reader_);
            if (first_ifd_result.is_error()) {
                return first_ifd_result.error();
            }
            current_offset = first_ifd_result.value();
        } else {
            // Resume from last known page's next offset
            auto ifd_header_result = ifd::read_ifd_header<FileReaderType, Format, Order>(
                *reader_, pages_.back().offset);
            if (ifd_header_result.is_error()) {
                return ifd_header_result.error();
            }
            
            std::vector<parsing::TagType<Format, Order>> tags_raw;
            auto next_ifd_result = ifd::read_ifd_tags<FileReaderType, Format, Order>(
                *reader_, ifd_header_result.value(), tags_raw);
            if (next_ifd_result.is_error()) {
                return next_ifd_result.error();
            }
            
            current_offset = next_ifd_result.value();
        }
        
        // Load all remaining pages
        while (!current_offset.is_null()) {
            auto page_result = try_extract_page_tags<Format, Order>(current_offset);
            if (page_result.is_error()) {
                return page_result.error();
            }
            
            pages_.push_back(std::move(page_result.value()));
            
            // Get next IFD offset
            auto ifd_header_result = ifd::read_ifd_header<FileReaderType, Format, Order>(*reader_, current_offset);
            if (ifd_header_result.is_error()) {
                break;
            }
            
            std::vector<parsing::TagType<Format, Order>> tags_raw;
            auto next_ifd_result = ifd::read_ifd_tags<FileReaderType, Format, Order>(
                *reader_, ifd_header_result.value(), tags_raw);
            if (next_ifd_result.is_error()) {
                break;
            }
            
            current_offset = next_ifd_result.value();
        }
        
        return Ok();
    };
    
    // Dispatch based on format and endianness
    Result<void> result = Err(Error::Code::InvalidFormat, "Unknown format");
    
    if (format_ == TiffFormatType::Classic && endian_ == std::endian::little) {
        result = load_pages_typed.template operator()<TiffFormatType::Classic, std::endian::little>();
    } else if (format_ == TiffFormatType::Classic && endian_ == std::endian::big) {
        result = load_pages_typed.template operator()<TiffFormatType::Classic, std::endian::big>();
    } else if (format_ == TiffFormatType::BigTIFF && endian_ == std::endian::little) {
        result = load_pages_typed.template operator()<TiffFormatType::BigTIFF, std::endian::little>();
    } else if (format_ == TiffFormatType::BigTIFF && endian_ == std::endian::big) {
        result = load_pages_typed.template operator()<TiffFormatType::BigTIFF, std::endian::big>();
    }
    
    if (result.is_ok()) {
        pages_loaded_ = true;
    }
    
    return result;
}

size_t FileHandle::num_pages() {
    if (!pages_loaded_) {
        auto result = load_all_pages();
        if (result.is_error()) {
            return 0;
        }
    }
    return pages_.size();
}

Result<PageInfo> FileHandle::get_page_info(size_t page_index) {
    // Lazy load pages up to the requested index
    auto result = load_pages_up_to(page_index);
    if (result.is_error()) {
        return result.error();
    }
    
    if (page_index >= pages_.size()) {
        return Err(Error::Code::OutOfBounds, 
                   "Page index " + std::to_string(page_index) + 
                   " out of range (0-" + std::to_string(pages_.size() - 1) + ")");
    }
    
    return pages_[page_index];
}

Result<PixelTypeEnum> FileHandle::read_region(
    size_t page_index,
    const ImageRegion& region,
    std::span<uint8_t> output,
    ImageLayoutSpec layout,
    bool use_threading) {
    
    if (!is_open_) {
        return Err(Error::Code::InvalidOperation, "File is not open");
    }
    
    // Get page info (will lazily load up to this page)
    auto page_info_result = get_page_info(page_index);
    if (page_info_result.is_error()) {
        return page_info_result.error();
    }
    
    const PageInfo& page_info = page_info_result.value();
    if (!page_info.is_valid) {
        return Err(Error::Code::InvalidOperation, 
                   "Page " + std::to_string(page_index) + " is not a valid image page");
    }

    const auto shape_validation = page_info.shape.validate_region(region);
    if (shape_validation.is_error()) {
        return shape_validation.error();
    }
    
    // Dispatch based on sample format and bits per sample
    uint16_t bps = page_info.shape.bits_per_sample();
    SampleFormat fmt = page_info.shape.sample_format();
    
    if (fmt == SampleFormat::UnsignedInt) {
        if (bps == 8) {
            std::span<uint8_t> output_span(
                reinterpret_cast<uint8_t*>(output.data()), output.size() / sizeof(uint8_t));
            const auto read_result = read_region_dispatch<uint8_t>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::UInt8;
        } else if (bps == 16) {
            std::span<uint16_t> output_span(
                reinterpret_cast<uint16_t*>(output.data()), output.size() / sizeof(uint16_t));
            const auto read_result = read_region_dispatch<uint16_t>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::UInt16;
        } else if (bps == 32) {
            std::span<uint32_t> output_span(
                reinterpret_cast<uint32_t*>(output.data()), output.size() / sizeof(uint32_t));
            const auto read_result = read_region_dispatch<uint32_t>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::UInt32;
        } else if (bps == 64) {
            std::span<uint64_t> output_span(
                reinterpret_cast<uint64_t*>(output.data()), output.size() / sizeof(uint64_t));
            const auto read_result = read_region_dispatch<uint64_t>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::UInt64;
        }
    } else if (fmt == SampleFormat::SignedInt) {
        if (bps == 8) {
            std::span<int8_t> output_span(
                reinterpret_cast<int8_t*>(output.data()), output.size() / sizeof(int8_t));
            const auto read_result = read_region_dispatch<int8_t>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::Int8;
        } else if (bps == 16) {
            std::span<int16_t> output_span(
                reinterpret_cast<int16_t*>(output.data()), output.size() / sizeof(int16_t));
            const auto read_result = read_region_dispatch<int16_t>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::Int16;
        } else if (bps == 32) {
            std::span<int32_t> output_span(
                reinterpret_cast<int32_t*>(output.data()), output.size() / sizeof(int32_t));
            const auto read_result = read_region_dispatch<int32_t>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::Int32;
        } else if (bps == 64) {
            std::span<int64_t> output_span(
                reinterpret_cast<int64_t*>(output.data()), output.size() / sizeof(int64_t));
            const auto read_result = read_region_dispatch<int64_t>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::Int64;
        }
    } else if (fmt == SampleFormat::IEEEFloat) {
        if (bps == 32) {
            std::span<float> output_span(
                reinterpret_cast<float*>(output.data()), output.size() / sizeof(float));
            const auto read_result = read_region_dispatch<float>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::Float32;
        } else if (bps == 64) {
            std::span<double> output_span(
                reinterpret_cast<double*>(output.data()), output.size() / sizeof(double));
            const auto read_result = read_region_dispatch<double>(page_info, region, output_span, layout, use_threading);
            if (read_result.is_error()) {
                return read_result.error();
            }
            return PixelTypeEnum::Float64;
        }
    }
    
    return Err(Error::Code::UnsupportedFeature, 
               "Unsupported pixel type: " + std::to_string(static_cast<int>(fmt)) + 
               "/" + std::to_string(bps));
}

template<typename PixelType>
Result<void> FileHandle::read_region_dispatch(
    const PageInfo& page_info,
    const ImageRegion& region,
    std::span<PixelType> output,
    ImageLayoutSpec layout_spec,
    bool use_threading) {

    // Select reader based on threading preference and reader type
    // Thread-local storage for readers to avoid re-initialization
    thread_local static SimpleReader<PixelType, PyDecompressorSpec> simple_reader;
    thread_local static CPULimitedReader<PixelType, PyDecompressorSpec> cpu_reader;
    
    Result<void> read_result = Ok();
    
    if (!use_threading) {
        // Use SimpleReader (no threading)
        if (layout_spec == ImageLayoutSpec::DHWC) {
            read_result = simple_reader.template read_region<ImageLayoutSpec::DHWC>(
                *reader_, page_info.tags, region, output);
        } else if (layout_spec == ImageLayoutSpec::DCHW) {
            read_result = simple_reader.template read_region<ImageLayoutSpec::DCHW>(
                *reader_, page_info.tags, region, output);
        } else {
            read_result = simple_reader.template read_region<ImageLayoutSpec::CDHW>(
                *reader_, page_info.tags, region, output);
        }
    } else {
        // Use CPULimitedReader for sync readers, FastReader for async
        if constexpr (is_async_reader) {
            // For async readers, use FastReader with cloned reader
            thread_local static FastReader<PixelType, PyDecompressorSpec> fast_reader;
            auto cloned_reader = reader_->clone();
            
            if (layout_spec == ImageLayoutSpec::DHWC) {
                read_result = fast_reader.template read_region<ImageLayoutSpec::DHWC>(
                    cloned_reader, page_info.tags, region, output);
            } else if (layout_spec == ImageLayoutSpec::DCHW) {
                read_result = fast_reader.template read_region<ImageLayoutSpec::DCHW>(
                    cloned_reader, page_info.tags, region, output);
            } else {
                read_result = fast_reader.template read_region<ImageLayoutSpec::CDHW>(
                    cloned_reader, page_info.tags, region, output);
            }
        } else {
            // For sync readers (pread, etc), use CPULimitedReader
            if (layout_spec == ImageLayoutSpec::DHWC) {
                read_result = cpu_reader.template read_region<ImageLayoutSpec::DHWC>(
                    *reader_, page_info.tags, region, output);
            } else if (layout_spec == ImageLayoutSpec::DCHW) {
                read_result = cpu_reader.template read_region<ImageLayoutSpec::DCHW>(
                    *reader_, page_info.tags, region, output);
            } else {
                read_result = cpu_reader.template read_region<ImageLayoutSpec::CDHW>(
                    *reader_, page_info.tags, region, output);
            }
        }
    }
    
    return read_result;
}

// Explicit template instantiations
template Result<void> FileHandle::read_region_dispatch<uint8_t>(const PageInfo&, const ImageRegion&, std::span<uint8_t>, ImageLayoutSpec, bool);
template Result<void> FileHandle::read_region_dispatch<uint16_t>(const PageInfo&, const ImageRegion&, std::span<uint16_t>, ImageLayoutSpec, bool);
template Result<void> FileHandle::read_region_dispatch<uint32_t>(const PageInfo&, const ImageRegion&, std::span<uint32_t>, ImageLayoutSpec, bool);
template Result<void> FileHandle::read_region_dispatch<uint64_t>(const PageInfo&, const ImageRegion&, std::span<uint64_t>, ImageLayoutSpec, bool);
template Result<void> FileHandle::read_region_dispatch<int8_t>(const PageInfo&, const ImageRegion&, std::span<int8_t>, ImageLayoutSpec, bool);
template Result<void> FileHandle::read_region_dispatch<int16_t>(const PageInfo&, const ImageRegion&, std::span<int16_t>, ImageLayoutSpec, bool);
template Result<void> FileHandle::read_region_dispatch<int32_t>(const PageInfo&, const ImageRegion&, std::span<int32_t>, ImageLayoutSpec, bool);
template Result<void> FileHandle::read_region_dispatch<int64_t>(const PageInfo&, const ImageRegion&, std::span<int64_t>, ImageLayoutSpec, bool);
template Result<void> FileHandle::read_region_dispatch<float>(const PageInfo&, const ImageRegion&, std::span<float>, ImageLayoutSpec, bool);
template Result<void> FileHandle::read_region_dispatch<double>(const PageInfo&, const ImageRegion&, std::span<double>, ImageLayoutSpec, bool);
template Result<PageInfo> FileHandle::try_extract_page_tags<TiffFormatType::Classic, std::endian::little>(ifd::IFDOffset);
template Result<PageInfo> FileHandle::try_extract_page_tags<TiffFormatType::Classic, std::endian::big>(ifd::IFDOffset);
template Result<PageInfo> FileHandle::try_extract_page_tags<TiffFormatType::BigTIFF, std::endian::little>(ifd::IFDOffset);
template Result<PageInfo> FileHandle::try_extract_page_tags<TiffFormatType::BigTIFF, std::endian::big>(ifd::IFDOffset);

} // namespace tiffconcept::python
