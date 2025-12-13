#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>
#include "compressor_base.hpp"
#include "result.hpp"

namespace tiff {

/// No compression - simple copy
class NoneCompressor {
public:
    constexpr NoneCompressor() noexcept = default;
    
    ~NoneCompressor() = default;
    
    // Non-copyable
    NoneCompressor(const NoneCompressor&) = delete;
    NoneCompressor& operator=(const NoneCompressor&) = delete;
    
    // Movable
    constexpr NoneCompressor(NoneCompressor&&) noexcept = default;
    constexpr NoneCompressor& operator=(NoneCompressor&&) noexcept = default;
    
    /// Clone this compressor for multi-threading
    [[nodiscard]] NoneCompressor clone() const noexcept {
        return NoneCompressor{};
    }
    
    /// Get the default compression scheme for this compressor
    [[nodiscard]] static constexpr CompressionScheme get_default_scheme() noexcept {
        return CompressionScheme::None;
    }
    
    /// Copy uncompressed data
    /// @param output Output vector - will be resized if needed
    /// @param offset Starting position in output vector
    /// @param input Input data to copy
    /// @return Number of bytes written
    [[nodiscard]] Result<std::size_t> compress(
        std::vector<std::byte>& output,
        std::size_t offset,
        std::span<const std::byte> input) const noexcept {
        
        std::size_t required_size = offset + input.size();
        
        // Resize if necessary
        if (output.size() < required_size) {
            try {
                output.resize(required_size);
            } catch (...) {
                return Err(Error::Code::MemoryError,
                           "Failed to resize output buffer");
            }
        }
        
        std::memcpy(output.data() + offset, input.data(), input.size());
        return Ok(input.size());
    }
};

/// None/uncompressed compressor descriptor
using NoneCompressorDesc = CompressorDescriptor<
    NoneCompressor,
    CompressionScheme::None
>;

/// PackBits compression (RLE compression scheme)
/// Simple byte-oriented run-length encoding as per TIFF spec Section 9
class PackBitsCompressor {
public:
    constexpr PackBitsCompressor() noexcept = default;
    
    ~PackBitsCompressor() = default;
    
    // Non-copyable
    PackBitsCompressor(const PackBitsCompressor&) = delete;
    PackBitsCompressor& operator=(const PackBitsCompressor&) = delete;
    
    // Movable
    constexpr PackBitsCompressor(PackBitsCompressor&&) noexcept = default;
    constexpr PackBitsCompressor& operator=(PackBitsCompressor&&) noexcept = default;
    
    /// Clone this compressor for multi-threading
    [[nodiscard]] PackBitsCompressor clone() const noexcept {
        return PackBitsCompressor{};
    }
    
    /// Get the default compression scheme for this compressor
    [[nodiscard]] static constexpr CompressionScheme get_default_scheme() noexcept {
        return CompressionScheme::PackBits;
    }
    
    /// Compress data using PackBits encoding
    /// Algorithm:
    /// - Find runs of identical bytes (replicated runs)
    /// - Find sequences of non-repeating bytes (literal runs)
    /// - Encode as: control byte followed by data
    ///   - If n >= 0: (n+1) literal bytes follow
    ///   - If n < 0 and n != -128: next byte is repeated (-n+1) times
    /// @param output Output vector - will be resized if needed
    /// @param offset Starting position in output vector
    /// @param input Input data to compress
    /// @return Number of bytes written
    [[nodiscard]] Result<std::size_t> compress(
        std::vector<std::byte>& output,
        std::size_t offset,
        std::span<const std::byte> input) const noexcept {
        
        if (input.empty()) {
            return Ok(std::size_t{0});
        }
        
        // Worst case: every byte is different + control bytes
        // Each literal run of 128 bytes needs 129 bytes (1 control + 128 data)
        std::size_t worst_case = offset + (input.size() + (input.size() + 127) / 128);
        
        if (output.size() < worst_case) {
            try {
                output.resize(worst_case);
            } catch (...) {
                return Err(Error::Code::MemoryError,
                           "Failed to resize output buffer");
            }
        }
        
        std::size_t in_pos = 0;
        std::size_t out_pos = offset;
        
        while (in_pos < input.size()) {
            // Look for a run of identical bytes
            std::size_t run_length = 1;
            while (in_pos + run_length < input.size() && 
                   run_length < 128 &&
                   input[in_pos + run_length] == input[in_pos]) {
                ++run_length;
            }
            
            // If we found a run of 2 or more, encode as replicated run
            if (run_length >= 2) {
                // Replicated run: encode as (1-n) followed by the byte
                output[out_pos++] = static_cast<std::byte>(1 - static_cast<int8_t>(run_length));
                output[out_pos++] = input[in_pos];
                in_pos += run_length;
            } else {
                // Look for a literal run (non-repeating bytes)
                std::size_t literal_start = in_pos;
                std::size_t literal_length = 1;
                
                while (literal_start + literal_length < input.size() && 
                       literal_length < 128) {
                    // Check if next bytes form a run
                    std::size_t next_run = 1;
                    while (literal_start + literal_length + next_run < input.size() && 
                           next_run < 3 &&  // Stop if we find 3+ identical bytes
                           input[literal_start + literal_length + next_run] == input[literal_start + literal_length]) {
                        ++next_run;
                    }
                    
                    // If we found a run of 3+, stop the literal run
                    if (next_run >= 3) {
                        break;
                    }
                    
                    ++literal_length;
                }
                
                // Encode literal run
                output[out_pos++] = static_cast<std::byte>(literal_length - 1);
                std::memcpy(&output[out_pos], &input[literal_start], literal_length);
                out_pos += literal_length;
                in_pos = literal_start + literal_length;
            }
        }
        
        return Ok(out_pos - offset);
    }
};

/// PackBits compressor descriptor
using PackBitsCompressorDesc = CompressorDescriptor<
    PackBitsCompressor,
    CompressionScheme::PackBits
>;

} // namespace tiff
