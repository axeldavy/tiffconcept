#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "compressor_base.hpp"
#include "result.hpp"

// Forward declarations for ZSTD C API if the header is not included
#ifndef ZSTD_VERSION_MAJOR
extern "C" {
    typedef struct ZSTD_CCtx_s ZSTD_CCtx;
    
    ZSTD_CCtx* ZSTD_createCCtx(void);
    std::size_t ZSTD_freeCCtx(ZSTD_CCtx* cctx);
    std::size_t ZSTD_compressCCtx(ZSTD_CCtx* cctx,
                                   void* dst, std::size_t dstCapacity,
                                   const void* src, std::size_t srcSize,
                                   int compressionLevel);
    std::size_t ZSTD_compress(void* dst, std::size_t dstCapacity,
                               const void* src, std::size_t srcSize,
                               int compressionLevel);
    std::size_t ZSTD_compressBound(std::size_t srcSize);
    unsigned ZSTD_isError(std::size_t code);
    const char* ZSTD_getErrorName(std::size_t code);
}
#endif

namespace tiffconcept {

/// RAII wrapper for ZSTD compression context
/// Context is allocated lazily on first use
class ZstdCompressor {
private:
    struct ZstdContextDeleter {
        void operator()(ZSTD_CCtx* ctx) const noexcept {
            if (ctx) {
                ZSTD_freeCCtx(ctx);
            }
        }
    };
    
    mutable std::unique_ptr<ZSTD_CCtx, ZstdContextDeleter> context_;
    int compression_level_;
    
    /// Ensure context is initialized (lazy initialization)
    [[nodiscard]] Result<ZSTD_CCtx*> ensure_context() const noexcept {
        if (!context_) {
            context_.reset(ZSTD_createCCtx());
            if (!context_) {
                return Err(Error::Code::MemoryError, 
                          "Failed to create ZSTD compression context");
            }
        }
        return Ok(context_.get());
    }
    
public:
    /// Create a ZSTD compressor with the specified compression level
    /// @param level Compression level (1-22, default 3)
    ///              Higher levels provide better compression but slower speed
    ///              Levels >= 20 require more memory
    explicit constexpr ZstdCompressor(int level = 3) noexcept 
        : compression_level_(level) {}
    
    ~ZstdCompressor() = default;
    
    // Non-copyable
    ZstdCompressor(const ZstdCompressor&) = delete;
    ZstdCompressor& operator=(const ZstdCompressor&) = delete;
    
    // Movable
    ZstdCompressor(ZstdCompressor&&) noexcept = default;
    ZstdCompressor& operator=(ZstdCompressor&&) noexcept = default;
    
    /// Clone this compressor for multi-threading
    /// Creates a new compressor with the same compression level but a fresh context
    [[nodiscard]] ZstdCompressor clone() const noexcept {
        return ZstdCompressor{compression_level_};
    }
    
    /// Get the default compression scheme for this compressor
    [[nodiscard]] static constexpr CompressionScheme get_default_scheme() noexcept {
        return CompressionScheme::ZSTD;
    }
    
    /// Compress data using the context (thread-safe if each thread has its own context)
    /// Context is created lazily on first use
    /// @param output Output vector - will be resized if needed
    /// @param offset Starting position in output vector
    /// @param input Input data to compress
    /// @return Number of bytes written
    [[nodiscard]] Result<std::size_t> compress(
        std::vector<std::byte>& output,
        std::size_t offset,
        std::span<const std::byte> input) const noexcept {
        
        auto ctx_result = ensure_context();
        if (!ctx_result) {
            return Err(ctx_result.error().code, ctx_result.error().message);
        }
        
        // Calculate worst-case compressed size
        std::size_t bound = ZSTD_compressBound(input.size());
        std::size_t required_size = offset + bound;
        
        // Resize if necessary
        if (output.size() < required_size) {
            try {
                output.resize(required_size);
            } catch (...) {
                return Err(Error::Code::MemoryError,
                           "Failed to resize output buffer");
            }
        }
        
        std::size_t result = ZSTD_compressCCtx(
            ctx_result.value(),
            output.data() + offset,
            output.size() - offset,
            input.data(),
            input.size(),
            compression_level_
        );
        
        if (ZSTD_isError(result)) {
            return Err(Error::Code::CompressionError,
                       std::string("ZSTD compression failed: ") + 
                       ZSTD_getErrorName(result));
        }
        
        return Ok(result);
    }
    
    /// Get the compression level
    [[nodiscard]] constexpr int get_level() const noexcept {
        return compression_level_;
    }
    
    /// Set the compression level
    constexpr void set_level(int level) noexcept {
        compression_level_ = level;
    }
    
    /// Get the worst-case compressed size for given input size
    [[nodiscard]] static std::size_t get_compress_bound(std::size_t input_size) noexcept {
        return ZSTD_compressBound(input_size);
    }
};

/// ZSTD compressor descriptor
/// Handles both standard ZSTD (50000) and alternative ZSTD tag (50001)
using ZstdCompressorDesc = CompressorDescriptor<
    ZstdCompressor,
    CompressionScheme::ZSTD,
    CompressionScheme::ZSTD_Alt
>;

} // namespace tiffconcept
