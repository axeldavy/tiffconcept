#pragma once

#include <cstddef>
#include <span>
#include <cstring>
#include "decompressor_base.hpp"
#include "result.hpp"

namespace tiff {

/// No compression - simple copy
class NoneDecompressor {
public:
    constexpr NoneDecompressor() noexcept = default;
    
    ~NoneDecompressor() = default;
    
    // Non-copyable
    NoneDecompressor(const NoneDecompressor&) = delete;
    NoneDecompressor& operator=(const NoneDecompressor&) = delete;
    
    // Movable
    constexpr NoneDecompressor(NoneDecompressor&&) noexcept = default;
    constexpr NoneDecompressor& operator=(NoneDecompressor&&) noexcept = default;
    
    /// Copy uncompressed data
    [[nodiscard]] Result<std::size_t> decompress(
        std::span<std::byte> output,
        std::span<const std::byte> input) const noexcept {
        
        if (input.size() > output.size()) {
            return Err(Error::Code::InvalidFormat,
                       "Output buffer too small for uncompressed data");
        }
        
        std::memcpy(output.data(), input.data(), input.size());
        return Ok(input.size());
    }
};

/// None/uncompressed decompressor descriptor
using NoneDecompressorDesc = DecompressorDescriptor<
    NoneDecompressor,
    CompressionScheme::None
>;

/// PackBits decompression (RLE compression scheme)
/// Simple byte-oriented run-length encoding as per TIFF spec Section 9
class PackBitsDecompressor {
public:
    constexpr PackBitsDecompressor() noexcept = default;
    
    ~PackBitsDecompressor() = default;
    
    // Non-copyable
    PackBitsDecompressor(const PackBitsDecompressor&) = delete;
    PackBitsDecompressor& operator=(const PackBitsDecompressor&) = delete;
    
    // Movable
    constexpr PackBitsDecompressor(PackBitsDecompressor&&) noexcept = default;
    constexpr PackBitsDecompressor& operator=(PackBitsDecompressor&&) noexcept = default;
    
    /// Decompress PackBits encoded data
    /// Algorithm:
    /// - Read a signed byte n
    /// - If n >= 0: copy next (n+1) bytes literally
    /// - If n < 0 and n != -128: copy next byte (-n+1) times
    /// - If n == -128: no operation (skip)
    [[nodiscard]] Result<std::size_t> decompress(
        std::span<std::byte> output,
        std::span<const std::byte> input) const noexcept {
        
        std::size_t in_pos = 0;
        std::size_t out_pos = 0;
        
        while (in_pos < input.size()) {
            if (out_pos >= output.size()) {
                return Err(Error::Code::InvalidFormat,
                           "Output buffer too small for PackBits decompression");
            }
            
            // Read control byte
            int8_t n = static_cast<int8_t>(input[in_pos++]);
            
            if (n == -128) {
                // No operation
                continue;
            }
            
            if (n >= 0) {
                // Literal run: copy next (n+1) bytes
                std::size_t count = static_cast<std::size_t>(n) + 1;
                
                if (in_pos + count > input.size()) {
                    return Err(Error::Code::InvalidFormat,
                               "PackBits: unexpected end of input in literal run");
                }
                
                if (out_pos + count > output.size()) {
                    return Err(Error::Code::InvalidFormat,
                               "PackBits: output buffer too small");
                }
                
                std::memcpy(&output[out_pos], &input[in_pos], count);
                in_pos += count;
                out_pos += count;
            } else {
                // Replicated run: copy next byte (-n+1) times
                std::size_t count = static_cast<std::size_t>(-n) + 1;
                
                if (in_pos >= input.size()) {
                    return Err(Error::Code::InvalidFormat,
                               "PackBits: unexpected end of input in replicated run");
                }
                
                if (out_pos + count > output.size()) {
                    return Err(Error::Code::InvalidFormat,
                               "PackBits: output buffer too small");
                }
                
                std::byte value = input[in_pos++];
                std::fill_n(&output[out_pos], count, value);
                out_pos += count;
            }
        }
        
        return Ok(out_pos);
    }
};

/// PackBits decompressor descriptor
using PackBitsDecompressorDesc = DecompressorDescriptor<
    PackBitsDecompressor,
    CompressionScheme::PackBits
>;

// The spec also makes mandatory support for CCITT compression schemes (1D, Group 3, Group 4)
// We will leave it as an exercise for the reader...

} // namespace tiff