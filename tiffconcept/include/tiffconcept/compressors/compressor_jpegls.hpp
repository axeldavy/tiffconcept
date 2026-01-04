// compressor_jpegls.hpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "compressor_base.hpp"
#include "../types/result.hpp"

// CharLS JPEG-LS encoder C++ API
#ifndef CHARLS_JPEGLS_ENCODER
#include <charls/jpegls_error.hpp>
#include <charls/jpegls_encoder.hpp>
#endif

namespace tiffconcept {

/// JPEG-LS compressor using CharLS library
/// Supports lossless and near-lossless compression
class JpeglsCompressor {
private:
    int32_t near_lossless_{0}; ///< NEAR parameter: 0 = lossless, >0 = lossy

public:
    /// Create a JPEG-LS compressor
    /// @param near_lossless NEAR parameter (0 = lossless, >0 = lossy with max error = NEAR)
    explicit constexpr JpeglsCompressor(int32_t near_lossless = 0) noexcept 
        : near_lossless_(near_lossless) {}
    
    ~JpeglsCompressor() = default;
    
    // Non-copyable
    JpeglsCompressor(const JpeglsCompressor&) = delete;
    JpeglsCompressor& operator=(const JpeglsCompressor&) = delete;
    
    // Movable
    JpeglsCompressor(JpeglsCompressor&&) noexcept = default;
    JpeglsCompressor& operator=(JpeglsCompressor&&) noexcept = default;

    /// Get the default compression scheme for this compressor
    [[nodiscard]] static constexpr CompressionScheme get_default_scheme() noexcept {
        return CompressionScheme::JPEG_LS;
    }

    /// Check if format is supported
    /// JPEG-LS requires depth=1 (no 3D support) and supports 1-4 samples per pixel
    /// Supports 2-16 bits per sample for integers, no direct float support
    [[nodiscard]] static constexpr bool supports_format(
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        [[maybe_unused]] std::endian endianness) noexcept {
        
        // JPEG-LS doesn't support depth > 1
        if (tile_size.depth != 1) {
            return false;
        }
        
        // Must have at least one sample
        if (sample_formats.empty() || bits_per_sample.empty()) {
            return false;
        }
        
        // JPEG-LS supports up to 255 components, but typically 1-4
        if (tile_size.nsamples > 255 || tile_size.nsamples == 0) {
            return false;
        }
        
        // Check that all samples have the same format and bits per sample
        const SampleFormat format = sample_formats[0];
        const uint8_t bits = bits_per_sample[0];
        
        // JPEG-LS only supports unsigned integer data
        if (format != SampleFormat::UnsignedInt) {
            return false;
        }
        
        // JPEG-LS supports 2-16 bits per sample
        if (bits < 2 || bits > 16) {
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
    
    /// Compress data using JPEG-LS encoding
    /// Input data is in DHWC chunked format (depth always 1 for JPEG-LS)
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
                       "JPEG-LS: Unsupported tile format (requires depth=1, unsigned int, 2-16 bits)");
        }
        
        try {
            // Create encoder instance
            charls::jpegls_encoder encoder;
            
            // Set frame info
            charls::frame_info frame_info{};
            frame_info.width = tile_size.width;
            frame_info.height = tile_size.height;
            frame_info.bits_per_sample = bits_per_sample[0];
            frame_info.component_count = static_cast<int32_t>(tile_size.nsamples);
            
            encoder.frame_info(frame_info);
            
            // Set near-lossless parameter
            if (near_lossless_ > 0) {
                encoder.near_lossless(near_lossless_);
            }
            
            // Set interleave mode based on number of samples
            // For DHWC format with nsamples > 1, data is interleaved by sample
            if (tile_size.nsamples > 1) {
                encoder.interleave_mode(charls::interleave_mode::sample);
            } else {
                encoder.interleave_mode(charls::interleave_mode::none);
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
                           "JPEG-LS: Input buffer too small");
            }
            
            // Estimate destination size (CharLS uses worst-case calculation)
            const std::size_t estimated_size = encoder.estimated_destination_size();
            const std::size_t required_size = offset + estimated_size;
            
            // Resize output buffer if necessary
            if (output.size() < required_size) {
                try {
                    output.resize(required_size);
                } catch (...) {
                    return Err(Error::Code::MemoryError,
                               "JPEG-LS: Failed to resize output buffer");
                }
            }
            
            // Set destination
            encoder.destination(output.data() + offset, output.size() - offset);
            
            // Encode
            const std::size_t bytes_written = encoder.encode(input.data(), expected_input_size);
            
            return Ok(bytes_written);
            
        } catch (const charls::jpegls_error& e) {
            return Err(Error::Code::CompressionError,
                       std::string("JPEG-LS compression failed: ") + e.what());
        } catch (const std::exception& e) {
            return Err(Error::Code::Unknown,
                       std::string("JPEG-LS compression exception: ") + e.what());
        } catch (...) {
            return Err(Error::Code::Unknown,
                       "JPEG-LS compression failed with unknown exception");
        }
    }
    
    /// Get the NEAR parameter
    [[nodiscard]] constexpr int32_t get_near_lossless() const noexcept {
        return near_lossless_;
    }
    
    /// Set the NEAR parameter (0 = lossless)
    constexpr void set_near_lossless(int32_t near_lossless) noexcept {
        near_lossless_ = near_lossless;
    }
};

/// JPEG-LS compressor descriptor
/// Handles standard JPEG-LS compression
using JpeglsCompressorDesc = CompressorDescriptor<
    JpeglsCompressor,
    CompressionScheme::JPEG_LS
>;

} // namespace tiffconcept