#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "compressor_base.hpp"
#include "../types/result.hpp"

// libjxl encoder C++ API
#ifndef JXL_ENCODE_H_
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#endif

namespace tiffconcept {

/// Convert JxlEncoderError to human-readable string
static constexpr const char* jxl_encoder_error_string(int error) noexcept {
    switch (error) {
        case 0: return "OK";
        case 1: return "Generic error";
        case 2: return "Out of memory";
        case 3: return "JPEG bitstream reconstruction data error";
        case 4: return "Bad input (corrupt JPEG or ICC profile)";
        case 0x80: return "Not supported";
        case 0x81: return "API usage error";
        default: return "Unknown error";
    }
}

/// JPEG XL compressor using libjxl library
/// Supports lossless compression only
class JpegxlCompressor {
private:
    struct JxlEncoderDeleter {
        void operator()(JxlEncoder* enc) const noexcept {
            if (enc) {
                JxlEncoderDestroy(enc);
            }
        }
    };
    
    mutable std::unique_ptr<JxlEncoder, JxlEncoderDeleter> encoder_;
    int effort_level_; ///< Encoder effort level (1-9, default 1, fastest)
    
    /// Ensure encoder is initialized (lazy initialization)
    [[nodiscard]] Result<JxlEncoder*> ensure_encoder() const noexcept {
        if (!encoder_) {
            encoder_.reset(JxlEncoderCreate(nullptr));
            if (!encoder_) {
                return Err(Error::Code::MemoryError, 
                          "Failed to create JPEG XL encoder");
            }
        }
        return Ok(encoder_.get());
    }

public:
    /// Create a JPEG XL compressor with the specified effort level
    /// @param effort Encoder effort level (1-9, default 1)
    ///               Higher levels provide better compression but slower speed
    ///               Level 1 is the fastest but has a lower ratio
    explicit constexpr JpegxlCompressor(int effort = 1) noexcept 
        : effort_level_(effort) {}
    
    ~JpegxlCompressor() = default;
    
    // Non-copyable
    JpegxlCompressor(const JpegxlCompressor&) = delete;
    JpegxlCompressor& operator=(const JpegxlCompressor&) = delete;
    
    // Movable
    JpegxlCompressor(JpegxlCompressor&&) noexcept = default;
    JpegxlCompressor& operator=(JpegxlCompressor&&) noexcept = default;

    /// Get the default compression scheme for this compressor
    [[nodiscard]] static constexpr CompressionScheme get_default_scheme() noexcept {
        return CompressionScheme::JPEG_XL;
    }

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
    
    /// Compress data using JPEG XL encoding (lossless)
    /// Input data is in DHWC chunked format (depth always 1 for JPEG XL)
    /// @param output Output vector - will be resized if needed
    /// @param offset Starting position in output vector
    /// @param input Input data to compress
    /// @return Number of bytes written
    [[nodiscard]] Result<std::size_t> compress(
        std::vector<std::byte>& output,
        std::size_t offset,
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
        
        auto enc_result = ensure_encoder();
        if (!enc_result) [[unlikely]] {
            return Err(enc_result.error().code, enc_result.error().message);
        }
        JxlEncoder* enc = enc_result.value();
        
        // Reset encoder for reuse
        JxlEncoderReset(enc);
        
        try {
            // Set up basic info
            JxlBasicInfo basic_info;
            JxlEncoderInitBasicInfo(&basic_info);
            basic_info.xsize = tile_size.width;
            basic_info.ysize = tile_size.height;
            basic_info.bits_per_sample = bits_per_sample[0];
            basic_info.num_color_channels = (tile_size.nsamples >= 3) ? 3 : tile_size.nsamples;
            basic_info.num_extra_channels = (tile_size.nsamples > 3) ? (tile_size.nsamples - 3) : 0;
            
            const SampleFormat format = sample_formats[0];
            if (format == SampleFormat::IEEEFloat) {
                if (bits_per_sample[0] == 16) {
                    basic_info.exponent_bits_per_sample = 5; // float16
                } else if (bits_per_sample[0] == 32) {
                    basic_info.exponent_bits_per_sample = 8; // float32
                }
            } else {
                basic_info.exponent_bits_per_sample = 0; // integer
            }
            
            basic_info.uses_original_profile = JXL_TRUE;
            basic_info.have_container = JXL_FALSE;
            
            if (JxlEncoderSetBasicInfo(enc, &basic_info) != JXL_ENC_SUCCESS) {
                return Err(Error::Code::CompressionError,
                           "JPEG XL: Failed to set basic info (" + std::string(jxl_encoder_error_string(JxlEncoderGetError(enc))) + ")");
            }
            
            // Set color encoding
            JxlColorEncoding color_encoding = {};
            if (tile_size.nsamples == 1) {
                JxlColorEncodingSetToSRGB(&color_encoding, JXL_TRUE); // Grayscale
            } else {
                JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE); // RGB
            }
            
            if (JxlEncoderSetColorEncoding(enc, &color_encoding) != JXL_ENC_SUCCESS) {
                return Err(Error::Code::CompressionError,
                           "JPEG XL: Failed to set color encoding (" + std::string(jxl_encoder_error_string(JxlEncoderGetError(enc))) + ")");
            }
            
            // Create frame settings
            JxlEncoderFrameSettings* frame_settings = JxlEncoderFrameSettingsCreate(enc, nullptr);
            if (!frame_settings) {
                return Err(Error::Code::CompressionError,
                           "JPEG XL: Failed to create frame settings (" + std::string(jxl_encoder_error_string(JxlEncoderGetError(enc))) + ")");
            }
            
            // Set lossless mode
            if (JxlEncoderSetFrameLossless(frame_settings, true) != JXL_ENC_SUCCESS) {
                return Err(Error::Code::CompressionError,
                           "JPEG XL: Failed to set lossless mode (" + std::string(jxl_encoder_error_string(JxlEncoderGetError(enc))) + ")");
            }
            
            // Set effort level
            if (JxlEncoderFrameSettingsSetOption(frame_settings, JXL_ENC_FRAME_SETTING_EFFORT, effort_level_) != JXL_ENC_SUCCESS) {
                return Err(Error::Code::CompressionError,
                           "JPEG XL: Failed to set effort level (" + std::string(jxl_encoder_error_string(JxlEncoderGetError(enc))) + ")");
            }

            // Set decoding speed level - 4 (faster decoding)
            if (JxlEncoderFrameSettingsSetOption(frame_settings, JXL_ENC_FRAME_SETTING_DECODING_SPEED, 4) != JXL_ENC_SUCCESS) {
                return Err(Error::Code::CompressionError,
                           "JPEG XL: Failed to set decoding speed level (" + std::string(jxl_encoder_error_string(JxlEncoderGetError(enc))) + ")");
            }
            
            // Set pixel format
            JxlPixelFormat pixel_format = {};
            pixel_format.num_channels = static_cast<uint32_t>(tile_size.nsamples);
            pixel_format.endianness = (endianness == std::endian::little) ? JXL_LITTLE_ENDIAN : JXL_BIG_ENDIAN;
            pixel_format.align = 0;
            
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
            
            // Calculate expected input size
            const std::size_t bytes_per_sample = (bits_per_sample[0] + 7) / 8;
            const std::size_t expected_input_size = 
                static_cast<std::size_t>(tile_size.width) * 
                tile_size.height * 
                tile_size.nsamples * 
                bytes_per_sample;
            
            if (input.size() < expected_input_size) {
                return Err(Error::Code::InvalidFormat,
                           "JPEG XL: Input buffer too small");
            }
            
            // Add image frame
            if (JxlEncoderAddImageFrame(frame_settings, &pixel_format, 
                                       const_cast<void*>(static_cast<const void*>(input.data())), 
                                       expected_input_size) != JXL_ENC_SUCCESS) {
                return Err(Error::Code::CompressionError,
                           "JPEG XL: Failed to add image frame (" + std::string(jxl_encoder_error_string(JxlEncoderGetError(enc))) + ")");
            }
            
            // Close input
            JxlEncoderCloseInput(enc);
            
            // Process output
            // Start with a reasonable estimate for compressed size
            const std::size_t initial_estimate = expected_input_size / 2 + 4096;
            const std::size_t required_size = offset + initial_estimate;
            
            if (output.size() < required_size) {
                try {
                    output.resize(required_size);
                } catch (...) {
                    return Err(Error::Code::MemoryError,
                               "JPEG XL: Failed to resize output buffer");
                }
            }
            
            uint8_t* next_out = reinterpret_cast<uint8_t*>(output.data() + offset);
            std::size_t avail_out = output.size() - offset;
            std::size_t total_written = 0;
            
            JxlEncoderStatus status;
            while ((status = JxlEncoderProcessOutput(enc, &next_out, &avail_out)) == JXL_ENC_NEED_MORE_OUTPUT) {
                // Track how much was written
                total_written = (next_out - reinterpret_cast<uint8_t*>(output.data() + offset));
                
                // Need more space - grow buffer
                const std::size_t new_size = output.size() + expected_input_size / 4 + 4096;
                try {
                    output.resize(new_size);
                } catch (...) {
                    return Err(Error::Code::MemoryError,
                               "JPEG XL: Failed to grow output buffer");
                }
                
                // Update pointers
                next_out = reinterpret_cast<uint8_t*>(output.data() + offset + total_written);
                avail_out = output.size() - offset - total_written;
            }
            
            if (status != JXL_ENC_SUCCESS) {
                return Err(Error::Code::CompressionError,
                           "JPEG XL: Encoding failed");
            }
            
            // Calculate final size
            total_written = (next_out - reinterpret_cast<uint8_t*>(output.data() + offset));
            
            return Ok(total_written);
            
        } catch (const std::exception& e) {
            return Err(Error::Code::Unknown,
                       std::string("JPEG XL compression exception: ") + e.what());
        } catch (...) {
            return Err(Error::Code::Unknown,
                       "JPEG XL compression failed with unknown exception");
        }
    }
    
    /// Get the encoder effort level
    [[nodiscard]] constexpr int get_effort() const noexcept {
        return effort_level_;
    }
    
    /// Set the encoder effort level (1-9)
    constexpr void set_effort(int effort) noexcept {
        effort_level_ = effort;
    }
};

/// JPEG XL compressor descriptor
/// Handles both standard JPEG XL (50002) and alternative tag (52546)
using JpegxlCompressorDesc = CompressorDescriptor<
    JpegxlCompressor,
    CompressionScheme::JPEG_XL,
    CompressionScheme::JPEG_XL_Alt
>;

} // namespace tiffconcept
