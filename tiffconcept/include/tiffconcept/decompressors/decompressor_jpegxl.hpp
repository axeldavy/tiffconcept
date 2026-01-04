#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include "decompressor_base.hpp"
#include "../types/result.hpp"

// libjxl decoder C++ API
#ifndef JXL_DECODE_H_
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#endif

namespace tiffconcept {

/// Convert JxlDecoderStatus to human-readable string
static constexpr const char* jxl_decoder_status_string(int status) noexcept {
    switch (status) {
        case 0: return "Success";
        case 1: return "Error";
        case 2: return "Need more input";
        case 3: return "Need preview out buffer";
        case 5: return "Need image out buffer";
        case 6: return "JPEG need more output";
        case 7: return "Box need more output";
        case 0x40: return "Basic info";
        case 0x100: return "Color encoding";
        case 0x200: return "Preview image";
        case 0x400: return "Frame";
        case 0x1000: return "Full image";
        case 0x2000: return "JPEG reconstruction";
        case 0x4000: return "Box";
        case 0x8000: return "Frame progression";
        case 0x10000: return "Box complete";
        default: return "Unknown status";
    }
}

/// JPEG XL decompressor using libjxl library
/// Supports lossless and near-lossless decompression
class JpegxlDecompressor {
private:
    struct JxlDecoderDeleter {
        void operator()(JxlDecoder* dec) const noexcept {
            if (dec) {
                JxlDecoderDestroy(dec);
            }
        }
    };
    
    mutable std::unique_ptr<JxlDecoder, JxlDecoderDeleter> decoder_;
    
    /// Ensure decoder is initialized (lazy initialization)
    [[nodiscard]] Result<JxlDecoder*> ensure_decoder() const noexcept {
        if (!decoder_) {
            decoder_.reset(JxlDecoderCreate(nullptr));
            if (!decoder_) {
                return Err(Error::Code::MemoryError, 
                          "Failed to create JPEG XL decoder");
            }
        }
        return Ok(decoder_.get());
    }

public:
    constexpr JpegxlDecompressor() noexcept = default;
    
    ~JpegxlDecompressor() = default;
    
    // Non-copyable
    JpegxlDecompressor(const JpegxlDecompressor&) = delete;
    JpegxlDecompressor& operator=(const JpegxlDecompressor&) = delete;
    
    // Movable
    JpegxlDecompressor(JpegxlDecompressor&&) noexcept = default;
    JpegxlDecompressor& operator=(JpegxlDecompressor&&) noexcept = default;

    /// Check if format is supported
    /// JPEG XL requires depth=1 (no 3D support)
    /// Supports 1-255 samples per pixel
    /// Supports 8-16 bits per sample for integers, 16/32 bits for floats
    [[nodiscard]] static constexpr bool supports_format(
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        [[maybe_unused]] std::endian endianness) noexcept {
        
        // JPEG XL doesn't support depth > 1
        if (tile_size.depth != 1) {
            return false;
        }
        
        // Must have at least one sample
        if (sample_formats.empty() || bits_per_sample.empty()) {
            return false;
        }
        
        // JPEG XL supports up to 4096 components, but we limit to 255 for sanity
        if (tile_size.nsamples > 255 || tile_size.nsamples == 0) {
            return false;
        }
        
        // Check that all samples have the same format and bits per sample
        const SampleFormat format = sample_formats[0];
        const uint8_t bits = bits_per_sample[0];
        
        // Validate format and bit depth combinations
        if (format == SampleFormat::UnsignedInt || format == SampleFormat::SignedInt) {
            // Integer formats: 8-16 bits
            if (bits < 8 || bits > 16) {
                return false;
            }
        } else if (format == SampleFormat::IEEEFloat) {
            // Float formats: 16 or 32 bits
            if (bits != 16 && bits != 32) {
                return false;
            }
        } else {
            // Undefined format not supported
            return false;
        }
        
        // All samples must have the same format and bits per sample
        for (std::size_t i = 1; i < sample_formats.size(); ++i) {
            if (sample_formats[i] != format || bits_per_sample[i] != bits) {
                return false;
            }
        }
        
        return true;
    }
    
    /// Decompress JPEG XL encoded data
    /// Input data is expected to be a complete JPEG XL stream
    /// Output will be in DHWC chunked format (depth always 1 for JPEG XL)
    [[nodiscard]] Result<std::size_t> decompress(
        std::span<std::byte> output,
        std::span<const std::byte> input,
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        [[maybe_unused]] std::endian endianness) const noexcept {
        
        // Validate format is supported
        if (!supports_format(tile_size, sample_formats, bits_per_sample, endianness)) {
            return Err(Error::Code::UnsupportedFeature,
                       "JPEG XL: Unsupported tile format (requires depth=1, 8-16 bit int or 16/32 bit float)");
        }
        
        auto dec_result = ensure_decoder();
        if (!dec_result) [[unlikely]] {
            return Err(dec_result.error().code, dec_result.error().message);
        }
        JxlDecoder* dec = dec_result.value();
        
        // Reset decoder for reuse
        JxlDecoderReset(dec);
        
        try {
            // Subscribe to events
            const int events = JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE;
            auto sub_status = JxlDecoderSubscribeEvents(dec, events);
            if (sub_status != JXL_DEC_SUCCESS) {
                return Err(Error::Code::InvalidFormat,
                           std::string("JPEG XL: Failed to subscribe to decoder events - ") + 
                           jxl_decoder_status_string(sub_status) + " (" + std::to_string(sub_status) + ")");
            }
            
            // Set input
            auto input_status = JxlDecoderSetInput(dec, reinterpret_cast<const uint8_t*>(input.data()), input.size());
            if (input_status != JXL_DEC_SUCCESS) {
                return Err(Error::Code::InvalidFormat,
                           std::string("JPEG XL: Failed to set input - ") + 
                           jxl_decoder_status_string(input_status) + " (" + std::to_string(input_status) + ")");
            }
            
            JxlDecoderCloseInput(dec);
            
            // Set pixel format
            JxlPixelFormat pixel_format = {};
            pixel_format.num_channels = static_cast<uint32_t>(tile_size.nsamples);
            // endianness indicates the byte order within the file, but libjxl already handles that
            // pixel_format.endianess is the target endianess which is always native for us 
            //pixel_format.endianness = (endianness == std::endian::little) ? JXL_LITTLE_ENDIAN : JXL_BIG_ENDIAN;
            pixel_format.endianness = JXL_NATIVE_ENDIAN;
            pixel_format.align = 0;
            
            const SampleFormat format = sample_formats[0];
            
            // Map data type
            if (format == SampleFormat::UnsignedInt) {
                if (bits_per_sample[0] == 8) {
                    pixel_format.data_type = JXL_TYPE_UINT8;
                } else if (bits_per_sample[0] == 16) {
                    pixel_format.data_type = JXL_TYPE_UINT16;
                } else {
                    return Err(Error::Code::UnsupportedFeature,
                               "JPEG XL: Unsupported unsigned int bit depth");
                }
            } else if (format == SampleFormat::SignedInt) {
                // JPEG XL doesn't have native signed int types, so we treat as unsigned
                // The bit pattern is preserved
                if (bits_per_sample[0] == 8) {
                    pixel_format.data_type = JXL_TYPE_UINT8;
                } else if (bits_per_sample[0] == 16) {
                    pixel_format.data_type = JXL_TYPE_UINT16;
                } else {
                    return Err(Error::Code::UnsupportedFeature,
                               "JPEG XL: Unsupported signed int bit depth");
                }
            } else if (format == SampleFormat::IEEEFloat) {
                if (bits_per_sample[0] == 16) {
                    pixel_format.data_type = JXL_TYPE_FLOAT16;
                } else if (bits_per_sample[0] == 32) {
                    pixel_format.data_type = JXL_TYPE_FLOAT;
                } else {
                    return Err(Error::Code::UnsupportedFeature,
                               "JPEG XL: Unsupported float bit depth");
                }
            } else {
                return Err(Error::Code::UnsupportedFeature,
                           "JPEG XL: Unsupported sample format");
            }
            
            // Process events
            bool image_set = false;
            JxlBasicInfo info = {};
            
            for (;;) {
                JxlDecoderStatus status = JxlDecoderProcessInput(dec);
                
                if (status == JXL_DEC_BASIC_INFO) {
                    auto info_status = JxlDecoderGetBasicInfo(dec, &info);
                    if (info_status != JXL_DEC_SUCCESS) {
                        return Err(Error::Code::InvalidFormat,
                                   std::string("JPEG XL: Failed to get basic info - ") + 
                                   jxl_decoder_status_string(info_status) + " (" + std::to_string(info_status) + ")");
                    }
                    
                    // Validate dimensions match
                    if (info.xsize != tile_size.width || info.ysize != tile_size.height) {
                        return Err(Error::Code::InvalidFormat,
                                   "JPEG XL: Frame dimensions don't match tile size");
                    }
                    
                    // Validate channel count
                    const uint32_t total_channels = info.num_color_channels + info.num_extra_channels;
                    if (total_channels != tile_size.nsamples) {
                        return Err(Error::Code::InvalidFormat,
                                   "JPEG XL: Channel count mismatch");
                    }
                    
                    // Validate bits per sample
                    if (info.bits_per_sample != bits_per_sample[0]) {
                        return Err(Error::Code::InvalidFormat,
                                   "JPEG XL: Bits per sample mismatch");
                    }
                    
                    // Validate exponent bits for float formats
                    if (format == SampleFormat::IEEEFloat) {
                        if (bits_per_sample[0] == 16 && info.exponent_bits_per_sample != 5) {
                            return Err(Error::Code::InvalidFormat,
                                       "JPEG XL: Expected float16 format");
                        } else if (bits_per_sample[0] == 32 && info.exponent_bits_per_sample != 8) {
                            return Err(Error::Code::InvalidFormat,
                                       "JPEG XL: Expected float32 format");
                        }
                    } else {
                        if (info.exponent_bits_per_sample != 0) {
                            return Err(Error::Code::InvalidFormat,
                                       "JPEG XL: Expected integer format");
                        }
                    }
                }
                else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
                    // Calculate expected output size
                    const std::size_t bytes_per_sample = (bits_per_sample[0] + 7) / 8;
                    const std::size_t expected_size = 
                        static_cast<std::size_t>(tile_size.width) * 
                        tile_size.height * 
                        tile_size.nsamples * 
                        bytes_per_sample;
                    
                    if (output.size() < expected_size) {
                        return Err(Error::Code::InvalidFormat,
                                   "JPEG XL: Output buffer too small");
                    }
                    
                    // Query buffer size from decoder
                    std::size_t buffer_size;
                    auto buf_status = JxlDecoderImageOutBufferSize(dec, &pixel_format, &buffer_size);
                    if (buf_status != JXL_DEC_SUCCESS) {
                        return Err(Error::Code::InvalidFormat,
                                   std::string("JPEG XL: Failed to get output buffer size - ") + 
                                   jxl_decoder_status_string(buf_status) + " (" + std::to_string(buf_status) + ")");
                    }
                    
                    if (buffer_size > output.size()) {
                        return Err(Error::Code::InvalidFormat,
                                   "JPEG XL: Calculated buffer size exceeds output buffer");
                    }
                    
                    // Set output buffer
                    auto set_buf_status = JxlDecoderSetImageOutBuffer(dec, &pixel_format, output.data(), output.size());
                    if (set_buf_status != JXL_DEC_SUCCESS) {
                        return Err(Error::Code::InvalidFormat,
                                   std::string("JPEG XL: Failed to set output buffer - ") + 
                                   jxl_decoder_status_string(set_buf_status) + " (" + std::to_string(set_buf_status) + ")");
                    }
                    
                    image_set = true;
                }
                else if (status == JXL_DEC_FULL_IMAGE) {
                    // Image decoded successfully
                    if (!image_set) {
                        return Err(Error::Code::InvalidFormat,
                                   "JPEG XL: Image decoded but buffer was not set");
                    }
                    
                    // Calculate actual output size
                    const std::size_t bytes_per_sample = (bits_per_sample[0] + 7) / 8;
                    const std::size_t decoded_size = 
                        static_cast<std::size_t>(tile_size.width) * 
                        tile_size.height * 
                        tile_size.nsamples * 
                        bytes_per_sample;
                    
                    return Ok(decoded_size);
                }
                else if (status == JXL_DEC_SUCCESS) {
                    // All done
                    if (!image_set) {
                        return Err(Error::Code::InvalidFormat,
                                   "JPEG XL: Decoder finished but no image was decoded");
                    }
                    break;
                }
                else if (status == JXL_DEC_ERROR) {
                    return Err(Error::Code::InvalidFormat,
                               "JPEG XL: Decoder error");
                }
                else {
                    return Err(Error::Code::InvalidFormat,
                               "JPEG XL: Unexpected decoder status");
                }
            }
            
            // Calculate final size
            const std::size_t bytes_per_sample = (bits_per_sample[0] + 7) / 8;
            const std::size_t decoded_size = 
                static_cast<std::size_t>(tile_size.width) * 
                tile_size.height * 
                tile_size.nsamples * 
                bytes_per_sample;
            
            return Ok(decoded_size);
            
        } catch (const std::exception& e) {
            return Err(Error::Code::Unknown,
                       std::string("JPEG XL decompression exception: ") + e.what());
        } catch (...) {
            return Err(Error::Code::Unknown,
                       "JPEG XL decompression failed with unknown exception");
        }
    }
};

/// JPEG XL decompressor descriptor
/// Handles both standard JPEG XL (50002) and alternative tag (52546)
using JpegxlDecompressorDesc = DecompressorDescriptor<
    JpegxlDecompressor,
    CompressionScheme::JPEG_XL,
    CompressionScheme::JPEG_XL_Alt
>;

} // namespace tiffconcept
