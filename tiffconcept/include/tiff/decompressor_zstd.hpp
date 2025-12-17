#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include "decompressor_base.hpp"
#include "result.hpp"

// Forward declarations for ZSTD C API if the header is not included
// We do not include <zstd.h> directly to avoid a hard dependency
// on the compression part of the library (this file can compile against
// just the decompression functions)
#ifndef ZSTD_VERSION_MAJOR
extern "C" {
    typedef struct ZSTD_DCtx_s ZSTD_DCtx;
    
    ZSTD_DCtx* ZSTD_createDCtx(void);
    std::size_t ZSTD_freeDCtx(ZSTD_DCtx* dctx);
    std::size_t ZSTD_decompressDCtx(ZSTD_DCtx* dctx, 
                                     void* dst, std::size_t dstCapacity,
                                     const void* src, std::size_t srcSize);
    std::size_t ZSTD_decompress(void* dst, std::size_t dstCapacity,
                                 const void* src, std::size_t srcSize);
    unsigned ZSTD_isError(std::size_t code);
    const char* ZSTD_getErrorName(std::size_t code);
    unsigned long long ZSTD_getFrameContentSize(const void* src, std::size_t srcSize);
}
#endif

namespace tiffconcept {

/// RAII wrapper for ZSTD decompression context
/// Context is allocated lazily on first use
class ZstdDecompressor {
private:
    struct ZstdContextDeleter {
        void operator()(ZSTD_DCtx* ctx) const noexcept {
            if (ctx) {
                ZSTD_freeDCtx(ctx);
            }
        }
    };
    
    mutable std::unique_ptr<ZSTD_DCtx, ZstdContextDeleter> context_;
    
    /// Ensure context is initialized (lazy initialization)
    [[nodiscard]] Result<ZSTD_DCtx*> ensure_context() const noexcept {
        if (!context_) {
            context_.reset(ZSTD_createDCtx());
            if (!context_) {
                return Err(Error::Code::MemoryError, 
                          "Failed to create ZSTD decompression context");
            }
        }
        return Ok(context_.get());
    }
    
public:
    constexpr ZstdDecompressor() noexcept = default;
    
    ~ZstdDecompressor() = default;
    
    // Non-copyable
    ZstdDecompressor(const ZstdDecompressor&) = delete;
    ZstdDecompressor& operator=(const ZstdDecompressor&) = delete;
    
    // Movable
    ZstdDecompressor(ZstdDecompressor&&) noexcept = default;
    ZstdDecompressor& operator=(ZstdDecompressor&&) noexcept = default;
    
    /// Decompress data using the context (thread-safe if each thread has its own context)
    /// Context is created lazily on first use
    [[nodiscard]] Result<std::size_t> decompress(
        std::span<std::byte> output,
        std::span<const std::byte> input) const noexcept {
        
        auto ctx_result = ensure_context();
        if (!ctx_result) {
            return Err(ctx_result.error().code, ctx_result.error().message);
        }
        
        std::size_t result = ZSTD_decompressDCtx(
            ctx_result.value(),
            output.data(),
            output.size(),
            input.data(),
            input.size()
        );
        
        if (ZSTD_isError(result)) {
            return Err(Error::Code::InvalidFormat,
                       std::string("ZSTD decompression failed: ") + 
                       ZSTD_getErrorName(result));
        }
        
        return Ok(result);
    }
    
    /// Get the decompressed size from the frame header
    [[nodiscard]] static Result<std::size_t> get_decompressed_size(
        std::span<const std::byte> input) noexcept {
        
        unsigned long long size = ZSTD_getFrameContentSize(input.data(), input.size());
        
        // Check for error values
        constexpr unsigned long long CONTENTSIZE_UNKNOWN = 0xFFFFFFFFFFFFFFFFULL;
        constexpr unsigned long long CONTENTSIZE_ERROR = 0xFFFFFFFFFFFFFFFEULL;
        
        if (size == CONTENTSIZE_ERROR) {
            return Err(Error::Code::InvalidFormat, "Invalid ZSTD frame header");
        }
        
        if (size == CONTENTSIZE_UNKNOWN) {
            return Err(Error::Code::UnsupportedFeature, 
                       "Decompressed size not available in frame");
        }
        
        return Ok(static_cast<std::size_t>(size));
    }
};

/// ZSTD decompressor descriptor
/// Handles both standard ZSTD (50000) and alternative ZSTD tag (50001)
using ZstdDecompressorDesc = DecompressorDescriptor<
    ZstdDecompressor,
    CompressionScheme::ZSTD,
    CompressionScheme::ZSTD_Alt
>;

} // namespace tiffconcept
