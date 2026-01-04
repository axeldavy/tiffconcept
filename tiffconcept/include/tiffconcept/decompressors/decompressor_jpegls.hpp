#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include "decompressor_base.hpp"
#include "../types/result.hpp"

// CharLS JPEG-LS decoder C++ API
#ifndef CHARLS_JPEGLS_DECODER
#include <charls/jpegls_error.hpp>
#include <charls/jpegls_decoder.hpp>
#endif

namespace tiffconcept {

/// JPEG-LS decompressor using CharLS library
/// Supports lossless and near-lossless compression
class JpeglsDecompressor {
public:
    constexpr JpeglsDecompressor() noexcept = default;
    
    ~JpeglsDecompressor() = default;
    
    // Non-copyable
    JpeglsDecompressor(const JpeglsDecompressor&) = delete;
    JpeglsDecompressor& operator=(const JpeglsDecompressor&) = delete;
    
    // Movable
    JpeglsDecompressor(JpeglsDecompressor&&) noexcept = default;
    JpeglsDecompressor& operator=(JpeglsDecompressor&&) noexcept = default;

    /// Check if format is supported
    /// JPEG-LS requires depth=1 (no 3D support) and supports 1-255 samples per pixel
    /// Supports 2-16 bits per sample for unsigned integers only
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
        
        // JPEG-LS supports up to 255 components
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
    
    /// Decompress JPEG-LS encoded data
    /// Input data is expected to be a complete JPEG-LS stream
    /// Output will be in DHWC chunked format (depth always 1 for JPEG-LS)
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
                       "JPEG-LS: Unsupported tile format (requires depth=1, unsigned int, 2-16 bits)");
        }
        
        try {
            // Create decoder instance
            charls::jpegls_decoder decoder;
            
            // Set source buffer
            decoder.source(input.data(), input.size());
            
            // Read header
            decoder.read_header();
            
            // Get frame info to validate against tile parameters
            const auto frame_info = decoder.frame_info();
            
            // Validate dimensions match
            if (frame_info.width != tile_size.width || 
                frame_info.height != tile_size.height ||
                frame_info.component_count != static_cast<int32_t>(tile_size.nsamples)) {
                return Err(Error::Code::InvalidFormat,
                           "JPEG-LS: Frame dimensions don't match tile size");
            }
            
            // Validate bits per sample
            if (frame_info.bits_per_sample != bits_per_sample[0]) {
                return Err(Error::Code::InvalidFormat,
                           "JPEG-LS: Bits per sample mismatch");
            }
            
            // Calculate expected output size
            const std::size_t bytes_per_sample = (bits_per_sample[0] + 7) / 8;
            const std::size_t expected_size = 
                static_cast<std::size_t>(tile_size.width) * 
                tile_size.height * 
                tile_size.nsamples * 
                bytes_per_sample;
            
            if (output.size() < expected_size) {
                return Err(Error::Code::InvalidFormat,
                           "JPEG-LS: Output buffer too small");
            }
            
            // Decode to output buffer
            // CharLS outputs interleaved data (sample by sample) which matches DHWC format
            decoder.decode(output.data(), output.size());
            
            return Ok(expected_size);
            
        } catch (const charls::jpegls_error& e) {
            return Err(Error::Code::InvalidFormat,
                       std::string("JPEG-LS decompression failed: ") + e.what());
        } catch (const std::exception& e) {
            return Err(Error::Code::Unknown,
                       std::string("JPEG-LS decompression exception: ") + e.what());
        } catch (...) {
            return Err(Error::Code::Unknown,
                       "JPEG-LS decompression failed with unknown exception");
        }
    }
};

/// JPEG-LS decompressor descriptor
/// Handles standard JPEG-LS compression
using JpeglsDecompressorDesc = DecompressorDescriptor<
    JpeglsDecompressor,
    CompressionScheme::JPEG_LS
>;

} // namespace tiffconcept