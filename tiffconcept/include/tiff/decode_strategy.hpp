#pragma once

#include <concepts>
#include <functional>
#include <future>
#include <span>
#include <thread>
#include <vector>
#include "chunk_info.hpp"
#include "decoder.hpp"
#include "image_shape.hpp"
#include "read_strategy.hpp"
#include "result.hpp"
#include "types.hpp"

namespace tiffconcept {

/// Decoded chunk ready for copying to output buffer\n/// References data in a shared buffer (no per-chunk allocation)
template <typename PixelType>
struct DecodedChunk {
    ChunkInfo info;                         // Metadata about the chunk
    std::span<const PixelType> decoded_data; // View into shared decode buffer
};

/// Callback invoked when a chunk is decoded
/// Should copy the decoded data to the output buffer
/// Returns error if copy fails
template <typename PixelType>
using ChunkDecodedCallback = std::function<Result<void>(const DecodedChunk<PixelType>&)>;

/// Concept for a decode strategy
/// A decode strategy is responsible for decoding compressed chunk data
/// It processes chunks as they arrive and calls the callback for each decoded chunk
template <typename T, typename PixelType, typename DecompSpec>
concept DecodeStrategy = ValidDecompressorSpec<DecompSpec> && requires(
    T& strategy,
    const ChunkData& chunk,
    CompressionScheme compression,
    Predictor predictor,
    uint16_t samples_per_pixel,
    ChunkDecodedCallback<PixelType> callback
) {
    // Process a single chunk: decode and invoke callback
    // The callback writes directly to the output buffer
    { strategy.template process_chunk<PixelType>(
        chunk, compression, predictor, samples_per_pixel, callback
    ) } -> std::same_as<Result<void>>;
    
    // Process multiple chunks (may parallelize internally)
    { strategy.template process_chunks<PixelType>(
        std::declval<const std::vector<ChunkData>&>(), 
        compression, predictor, samples_per_pixel, callback
    ) } -> std::same_as<Result<void>>;
    
    // Clear any cached state (for reuse)
    { strategy.clear() } -> std::same_as<void>;
};

/// Single-threaded decoder that decodes chunks one by one
/// Uses one ChunkDecoder instance that is reused
template <typename DecompSpec>
    requires ValidDecompressorSpec<DecompSpec>
class SingleThreadedDecoder {
private:
    // Type-erased decoder storage (persistent across calls)
    mutable void* decoder_storage_ = nullptr;
    mutable void (*decoder_deleter_)(void*) = nullptr;
    mutable const std::type_info* decoder_type_ = nullptr;
    
    // Reusable decode buffer
    mutable std::vector<std::byte> decode_buffer_;
    
    template <typename PixelType>
    ChunkDecoder<PixelType, DecompSpec>& get_decoder() const noexcept {
        if (decoder_storage_ == nullptr || decoder_type_ != &typeid(PixelType)) {
            // Clean up old decoder if type changed
            if (decoder_storage_ != nullptr && decoder_deleter_ != nullptr) {
                decoder_deleter_(decoder_storage_);
            }
            
            // Allocate new decoder
            decoder_storage_ = new ChunkDecoder<PixelType, DecompSpec>();
            decoder_deleter_ = [](void* ptr) { delete static_cast<ChunkDecoder<PixelType, DecompSpec>*>(ptr); };
            decoder_type_ = &typeid(PixelType);
        }
        return *static_cast<ChunkDecoder<PixelType, DecompSpec>*>(decoder_storage_);
    }
    
public:
    SingleThreadedDecoder() noexcept = default;
    
    ~SingleThreadedDecoder() noexcept {
        if (decoder_storage_ != nullptr && decoder_deleter_ != nullptr) {
            decoder_deleter_(decoder_storage_);
        }
    }
    
    SingleThreadedDecoder(const SingleThreadedDecoder&) = delete;
    SingleThreadedDecoder& operator=(const SingleThreadedDecoder&) = delete;
    
    SingleThreadedDecoder(SingleThreadedDecoder&& other) noexcept
        : decoder_storage_(other.decoder_storage_)
        , decoder_deleter_(other.decoder_deleter_)
        , decoder_type_(other.decoder_type_)
        , decode_buffer_(std::move(other.decode_buffer_)) {
        other.decoder_storage_ = nullptr;
        other.decoder_deleter_ = nullptr;
        other.decoder_type_ = nullptr;
    }
    
    SingleThreadedDecoder& operator=(SingleThreadedDecoder&& other) noexcept {
        if (this != &other) {
            if (decoder_storage_ != nullptr && decoder_deleter_ != nullptr) {
                decoder_deleter_(decoder_storage_);
            }
            decoder_storage_ = other.decoder_storage_;
            decoder_deleter_ = other.decoder_deleter_;
            decoder_type_ = other.decoder_type_;
            decode_buffer_ = std::move(other.decode_buffer_);
            other.decoder_storage_ = nullptr;
            other.decoder_deleter_ = nullptr;
            other.decoder_type_ = nullptr;
        }
        return *this;
    }
    
    template <typename PixelType>
        requires (std::is_same_v<PixelType, uint8_t> || 
                  std::is_same_v<PixelType, uint16_t> ||
                  std::is_same_v<PixelType, uint32_t> ||
                  std::is_same_v<PixelType, uint64_t> ||
                  std::is_same_v<PixelType, int8_t> ||
                  std::is_same_v<PixelType, int16_t> ||
                  std::is_same_v<PixelType, int32_t> ||
                  std::is_same_v<PixelType, int64_t> ||
                  std::is_same_v<PixelType, float> ||
                  std::is_same_v<PixelType, double>)
    [[nodiscard]] Result<void> process_chunk(
        const ChunkData& chunk_data,
        CompressionScheme compression,
        Predictor predictor,
        uint16_t samples_per_pixel,
        const ChunkDecodedCallback<PixelType>& callback) noexcept {
        
        auto& decoder = get_decoder<PixelType>();
        const auto& info = chunk_data.info;
        
        // Decode the chunk
        auto decode_result = decoder.decode(
            chunk_data.data,
            info.width,
            info.height * info.depth,
            compression,
            predictor,
            samples_per_pixel
        );
        
        if (!decode_result) {
            return Err(decode_result.error().code, decode_result.error().message);
        }
        
        auto decoded_span = decode_result.value();
        
        // Create DecodedChunk referencing decoder's internal buffer
        DecodedChunk<PixelType> decoded;
        decoded.info = info;
        decoded.decoded_data = decoded_span;
        
        // Immediately invoke callback to copy to output
        // This happens before the next decode, so the buffer can be reused
        return callback(decoded);
    }
    
    template <typename PixelType>
        requires (std::is_same_v<PixelType, uint8_t> || 
                  std::is_same_v<PixelType, uint16_t> ||
                  std::is_same_v<PixelType, uint32_t> ||
                  std::is_same_v<PixelType, uint64_t> ||
                  std::is_same_v<PixelType, int8_t> ||
                  std::is_same_v<PixelType, int16_t> ||
                  std::is_same_v<PixelType, int32_t> ||
                  std::is_same_v<PixelType, int64_t> ||
                  std::is_same_v<PixelType, float> ||
                  std::is_same_v<PixelType, double>)
    [[nodiscard]] Result<void> process_chunks(
        const std::vector<ChunkData>& chunks,
        CompressionScheme compression,
        Predictor predictor,
        uint16_t samples_per_pixel,
        const ChunkDecodedCallback<PixelType>& callback) noexcept {
        
        // Single-threaded: process each chunk sequentially
        for (const auto& chunk_data : chunks) {
            auto result = process_chunk<PixelType>(
                chunk_data, compression, predictor, samples_per_pixel, callback
            );
            if (!result) {
                return result;
            }
        }
        
        return Ok();
    }
    
    void clear() noexcept {
        decode_buffer_.clear();
        decode_buffer_.shrink_to_fit();
        // Keep decoder allocated for reuse
    }
};

/// Multi-threaded decoder that decodes chunks in parallel using a thread pool
/// Each thread has its own ChunkDecoder instance (reused across calls)
template <typename DecompSpec>
    requires ValidDecompressorSpec<DecompSpec>
class MultiThreadedDecoder {
private:
    std::size_t num_threads_;
    
    // Per-thread decoder storage (persistent, type-erased)
    mutable std::vector<void*> thread_decoders_;
    mutable std::vector<void (*)(void*)> thread_deleters_;
    mutable const std::type_info* decoder_type_ = nullptr;
    
    template <typename PixelType>
    void ensure_thread_decoders() const noexcept {
        if (decoder_type_ != &typeid(PixelType)) {
            // Type changed, clean up old decoders
            for (std::size_t i = 0; i < thread_decoders_.size(); ++i) {
                if (thread_decoders_[i] != nullptr && thread_deleters_[i] != nullptr) {
                    thread_deleters_[i](thread_decoders_[i]);
                }
            }
            thread_decoders_.clear();
            thread_deleters_.clear();
            decoder_type_ = &typeid(PixelType);
        }
        
        // Ensure we have enough decoders
        while (thread_decoders_.size() < num_threads_) {
            thread_decoders_.push_back(new ChunkDecoder<PixelType, DecompSpec>());
            thread_deleters_.push_back([](void* ptr) { 
                delete static_cast<ChunkDecoder<PixelType, DecompSpec>*>(ptr); 
            });
        }
    }
    
public:
    explicit MultiThreadedDecoder(std::size_t num_threads = 0) noexcept
        : num_threads_(num_threads == 0 ? std::thread::hardware_concurrency() : num_threads) {
        
        if (num_threads_ == 0) {
            num_threads_ = 1;  // Fallback if hardware_concurrency() returns 0
        }
    }
    
    ~MultiThreadedDecoder() noexcept {
        for (std::size_t i = 0; i < thread_decoders_.size(); ++i) {
            if (thread_decoders_[i] != nullptr && thread_deleters_[i] != nullptr) {
                thread_deleters_[i](thread_decoders_[i]);
            }
        }
    }
    
    MultiThreadedDecoder(const MultiThreadedDecoder&) = delete;
    MultiThreadedDecoder& operator=(const MultiThreadedDecoder&) = delete;
    
    MultiThreadedDecoder(MultiThreadedDecoder&& other) noexcept
        : num_threads_(other.num_threads_)
        , thread_decoders_(std::move(other.thread_decoders_))
        , thread_deleters_(std::move(other.thread_deleters_))
        , decoder_type_(other.decoder_type_) {
        other.decoder_type_ = nullptr;
    }
    
    MultiThreadedDecoder& operator=(MultiThreadedDecoder&& other) noexcept {
        if (this != &other) {
            for (std::size_t i = 0; i < thread_decoders_.size(); ++i) {
                if (thread_decoders_[i] != nullptr && thread_deleters_[i] != nullptr) {
                    thread_deleters_[i](thread_decoders_[i]);
                }
            }
            num_threads_ = other.num_threads_;
            thread_decoders_ = std::move(other.thread_decoders_);
            thread_deleters_ = std::move(other.thread_deleters_);
            decoder_type_ = other.decoder_type_;
            other.decoder_type_ = nullptr;
        }
        return *this;
    }
    
    template <typename PixelType>
        requires (std::is_same_v<PixelType, uint8_t> || 
                  std::is_same_v<PixelType, uint16_t> ||
                  std::is_same_v<PixelType, uint32_t> ||
                  std::is_same_v<PixelType, uint64_t> ||
                  std::is_same_v<PixelType, int8_t> ||
                  std::is_same_v<PixelType, int16_t> ||
                  std::is_same_v<PixelType, int32_t> ||
                  std::is_same_v<PixelType, int64_t> ||
                  std::is_same_v<PixelType, float> ||
                  std::is_same_v<PixelType, double>)
    [[nodiscard]] Result<void> process_chunk(
        const ChunkData& chunk_data,
        CompressionScheme compression,
        Predictor predictor,
        uint16_t samples_per_pixel,
        const ChunkDecodedCallback<PixelType>& callback) noexcept {
        
        // For single chunk, use first decoder
        ensure_thread_decoders<PixelType>();
        auto& decoder = *static_cast<ChunkDecoder<PixelType, DecompSpec>*>(thread_decoders_[0]);
        
        const auto& info = chunk_data.info;
        
        // Decode the chunk
        auto decode_result = decoder.decode(
            chunk_data.data,
            info.width,
            info.height * info.depth,
            compression,
            predictor,
            samples_per_pixel
        );
        
        if (!decode_result) {
            return Err(decode_result.error().code, decode_result.error().message);
        }
        
        auto decoded_span = decode_result.value();
        
        // Create DecodedChunk and invoke callback
        DecodedChunk<PixelType> decoded;
        decoded.info = info;
        decoded.decoded_data = decoded_span;
        
        return callback(decoded);
    }
    
    template <typename PixelType>
        requires (std::is_same_v<PixelType, uint8_t> || 
                  std::is_same_v<PixelType, uint16_t> ||
                  std::is_same_v<PixelType, uint32_t> ||
                  std::is_same_v<PixelType, uint64_t> ||
                  std::is_same_v<PixelType, int8_t> ||
                  std::is_same_v<PixelType, int16_t> ||
                  std::is_same_v<PixelType, int32_t> ||
                  std::is_same_v<PixelType, int64_t> ||
                  std::is_same_v<PixelType, float> ||
                  std::is_same_v<PixelType, double>)
    [[nodiscard]] Result<void> process_chunks(
        const std::vector<ChunkData>& chunks,
        CompressionScheme compression,
        Predictor predictor,
        uint16_t samples_per_pixel,
        const ChunkDecodedCallback<PixelType>& callback) noexcept {
        
        if (chunks.empty()) {
            return Ok();
        }
        
        // Ensure thread decoders are ready
        ensure_thread_decoders<PixelType>();
        
        // If only one chunk or one thread, fall back to single-threaded
        if (chunks.size() == 1 || num_threads_ == 1) {
            return process_chunk<PixelType>(chunks[0], compression, predictor, samples_per_pixel, callback);
        }
        
        // Multi-threaded: decode and copy in parallel
        // Each thread decodes a chunk and immediately invokes the callback
        std::vector<std::future<Result<void>>> futures;
        futures.reserve(chunks.size());
        
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            futures.push_back(std::async(std::launch::async, 
                [this, i, &chunks, compression, predictor, samples_per_pixel, &callback]() -> Result<void> {
                
                std::size_t thread_id = i % num_threads_;
                auto& decoder = *static_cast<ChunkDecoder<PixelType, DecompSpec>*>(thread_decoders_[thread_id]);
                
                const auto& chunk_data = chunks[i];
                const auto& info = chunk_data.info;
                
                // Decode the chunk
                auto decode_result = decoder.decode(
                    chunk_data.data,
                    info.width,
                    info.height * info.depth,
                    compression,
                    predictor,
                    samples_per_pixel
                );
                
                if (!decode_result) {
                    return Err(decode_result.error().code, decode_result.error().message);
                }
                
                auto decoded_span = decode_result.value();
                
                // Create DecodedChunk and invoke callback to copy
                // This happens in the decode thread - parallel copy!
                DecodedChunk<PixelType> decoded;
                decoded.info = info;
                decoded.decoded_data = decoded_span;
                
                return callback(decoded);
            }));
        }
        
        // Wait for all tasks and check for errors
        for (auto& future : futures) {
            auto result = future.get();
            if (!result) {
                return result;
            }
        }
        
        return Ok();
    }
    
    void clear() noexcept {
        // Keep thread decoders allocated for reuse
        // They contain their own internal buffers that get reused
    }
    
    [[nodiscard]] std::size_t num_threads() const noexcept {
        return num_threads_;
    }
    
    void set_num_threads(std::size_t num_threads) noexcept {
        num_threads_ = num_threads == 0 ? std::thread::hardware_concurrency() : num_threads;
        if (num_threads_ == 0) {
            num_threads_ = 1;
        }
    }
};

} // namespace tiffconcept
