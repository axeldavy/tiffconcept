#pragma once

#include <algorithm>
#include <concepts>
#include <functional>
#include <future>
#include <mutex>
#include <span>
#include <thread>
#include <vector>
#include "chunk_strategy.hpp"
#include "../reader_base.hpp"
#include "../result.hpp"

namespace tiffconcept {

/// Data for a single chunk read from file
/// References data in a shared buffer (no per-chunk allocation)
struct ChunkData {
    ChunkInfo info;                         // Metadata about the chunk
    std::span<const std::byte> data;        // View into shared buffer
};

/// Concept for a chunk processor that handles chunk data
/// The processor must be thread-safe and can have internal storage
/// The process method will be called (potentially from multiple threads)
template <typename T>
concept ChunkProcessor = requires(T& processor, std::span<const ChunkData> chunk_data) {
    // Process a batch of chunks in a thread-safe manner
    // The processor can have internal state and must handle concurrent calls
    { processor.process(chunk_data) } -> std::same_as<Result<void>>;
};

/// Concept for a read strategy
/// A read strategy is responsible for reading chunk data from file
/// It must be reusable across multiple read_region calls
/// The processor is invoked with chunk data as it becomes available
template <typename T, typename Reader, typename Processor>
concept ReadStrategy = RawReader<Reader> && ChunkProcessor<Processor> && requires(
    T& strategy,
    const Reader& reader,
    std::span<const ChunkInfo> chunks,
    Processor& processor
) {
    // Read chunks according to the strategy, invoking processor for each batch
    // The processor receives a span of ChunkData and should process it immediately
    { strategy.read_chunks(reader, chunks, processor) } -> std::same_as<Result<void>>;
    
    // Clear any cached state (for reuse)
    { strategy.clear() } -> std::same_as<void>;
};

/// Single-threaded reader that reads chunks one by one
/// No batching, minimal memory usage
/// Invokes processor immediately after each chunk is read
class SingleThreadedReader {
private:
    mutable std::vector<std::byte> chunk_buffer_;  // Reusable buffer for chunk reads
    mutable ChunkData chunk_data_;                 // Reusable ChunkData for processor
    
public:
    SingleThreadedReader() noexcept = default;
    
    template <typename Reader, typename Processor>
        requires RawReader<Reader> && ChunkProcessor<Processor>
    [[nodiscard]] Result<void> read_chunks(
        const Reader& reader,
        std::span<const ChunkInfo> chunks,
        Processor& processor) noexcept {
        
        for (const auto& chunk : chunks) {
            if (chunk.byte_count == 0) {
                continue;  // Skip empty chunks
            }
            
            auto view_result = reader.read(chunk.offset, chunk.byte_count);
            if (!view_result) {
                return Err(view_result.error().code, view_result.error().message);
            }
            
            auto view = std::move(view_result.value());
            auto view_span = view.data();
            
            // Copy into our reusable buffer
            chunk_buffer_.clear();
            chunk_buffer_.resize(view_span.size());
            std::memcpy(chunk_buffer_.data(), view_span.data(), view_span.size());
            
            // Prepare ChunkData and invoke processor
            chunk_data_.info = chunk;
            chunk_data_.data = std::span<const std::byte>(chunk_buffer_.data(), chunk_buffer_.size());
            
            // Invoke processor with single chunk
            auto result = processor.process(std::span<const ChunkData>(&chunk_data_, 1));
            if (!result) {
                return result;
            }
        }
        
        return Ok();
    }
    
    void clear() noexcept {
        chunk_buffer_.clear();
        chunk_buffer_.shrink_to_fit();
    }
};

/// Batched reader that groups nearby chunks to reduce I/O operations
/// Uses BatchingParams to control batching behavior
/// Invokes processor for each batch as it's read
class BatchedReader {
private:
    BatchingParams params_;
    mutable std::vector<std::byte> batch_buffer_;  // Reusable buffer for batch reads
    mutable std::vector<ChunkData> batch_chunk_data_; // Reusable ChunkData array for processor
    
public:
    explicit BatchedReader(BatchingParams params = BatchingParams::none()) noexcept
        : params_(params), batch_buffer_() {}
    
    template <typename Reader, typename Processor>
        requires RawReader<Reader> && ChunkProcessor<Processor>
    [[nodiscard]] Result<void> read_chunks(
        const Reader& reader,
        std::span<const ChunkInfo> chunks,
        Processor& processor) noexcept {
        
        if (chunks.empty()) {
            return Ok();
        }
        
        // Process chunks in batches using the batching algorithm
        auto batch_callback = [this, &reader, &processor](const ChunkBatch& batch) -> Result<void> {
            return read_and_process_batch(reader, batch, processor);
        };
        
        return create_batches(chunks, params_, batch_callback);
    }
    
    void clear() noexcept {
        batch_buffer_.clear();
        batch_buffer_.shrink_to_fit();
        batch_chunk_data_.clear();
        batch_chunk_data_.shrink_to_fit();
    }
    
    /// Update batching parameters
    void set_params(BatchingParams params) noexcept {
        params_ = params;
    }
    
    [[nodiscard]] const BatchingParams& params() const noexcept {
        return params_;
    }
    
private:
    template <typename Reader, typename Processor>
        requires RawReader<Reader> && ChunkProcessor<Processor>
    [[nodiscard]] Result<void> read_and_process_batch(
        const Reader& reader,
        const ChunkBatch& batch,
        Processor& processor) noexcept {
        
        batch_buffer_.clear();
        batch_chunk_data_.clear();
        
        // If batch is just one chunk, read it directly
        if (batch.chunks.size() == 1) {
            const auto& chunk = batch.chunks[0];
            if (chunk.byte_count == 0) {
                return Ok();
            }
            
            auto view_result = reader.read(chunk.offset, chunk.byte_count);
            if (!view_result) {
                return Err(view_result.error().code, view_result.error().message);
            }
            
            auto view = std::move(view_result.value());
            auto view_span = view.data();
            
            // Copy to buffer
            batch_buffer_.resize(view_span.size());
            std::memcpy(batch_buffer_.data(), view_span.data(), view_span.size());
            
            ChunkData chunk_data;
            chunk_data.info = chunk;
            chunk_data.data = std::span<const std::byte>(batch_buffer_.data(), batch_buffer_.size());
            
            // Invoke processor
            return processor.process(std::span<const ChunkData>(&chunk_data, 1));
        }
        
        // Read the entire batch span
        uint32_t batch_size = batch.file_span();
        auto view_result = reader.read(batch.min_offset, batch_size);
        if (!view_result) {
            return Err(view_result.error().code, view_result.error().message);
        }
        
        auto view = std::move(view_result.value());
        auto view_span = view.data();
        
        // Copy to buffer
        batch_buffer_.resize(view_span.size());
        std::memcpy(batch_buffer_.data(), view_span.data(), view_span.size());
        
        // Prepare ChunkData for all chunks in batch
        batch_chunk_data_.reserve(batch.chunks.size());
        for (const auto& chunk : batch.chunks) {
            if (chunk.byte_count == 0) {
                continue;
            }
            
            std::size_t relative_offset = chunk.offset - batch.min_offset;
            if (relative_offset + chunk.byte_count > view_span.size()) {
                return Err(Error::Code::OutOfBounds, "Chunk extends beyond batch data");
            }
            
            ChunkData chunk_data;
            chunk_data.info = chunk;
            chunk_data.data = std::span<const std::byte>(
                batch_buffer_.data() + relative_offset,
                chunk.byte_count
            );
            
            batch_chunk_data_.push_back(chunk_data);
        }
        
        // Invoke processor with all chunks in this batch
        return processor.process(std::span<const ChunkData>(batch_chunk_data_.data(), batch_chunk_data_.size()));
    }
};

/// Multi-threaded reader that reads chunks in parallel
/// Each thread reads chunks independently, useful for high-latency I/O
/// where parallel requests can significantly improve throughput
/// Processor must be thread-safe as it's invoked from multiple threads
class MultiThreadedReader {
private:
    std::size_t num_threads_;
    
public:
    explicit MultiThreadedReader(std::size_t num_threads = 0) noexcept
        : num_threads_(num_threads == 0 ? std::thread::hardware_concurrency() : num_threads) {
        
        if (num_threads_ == 0) {
            num_threads_ = 1;  // Fallback if hardware_concurrency() returns 0
        }
    }
    
    template <typename Reader, typename Processor>
        requires RawReader<Reader> && ChunkProcessor<Processor>
    [[nodiscard]] Result<void> read_chunks(
        const Reader& reader,
        std::span<const ChunkInfo> chunks,
        Processor& processor) noexcept {
        
        if (chunks.empty()) {
            return Ok();
        }
        
        // If only one chunk or one thread, do single-threaded
        if (chunks.size() == 1 || num_threads_ == 1) {
            std::vector<std::byte> buffer;
            ChunkData chunk_data;
            
            for (const auto& chunk : chunks) {
                if (chunk.byte_count == 0) {
                    continue;
                }
                
                auto view_result = reader.read(chunk.offset, chunk.byte_count);
                if (!view_result) {
                    return Err(view_result.error().code, view_result.error().message);
                }
                
                auto view = std::move(view_result.value());
                auto view_span = view.data();
                
                buffer.clear();
                buffer.resize(view_span.size());
                std::memcpy(buffer.data(), view_span.data(), view_span.size());
                
                chunk_data.info = chunk;
                chunk_data.data = std::span<const std::byte>(buffer.data(), buffer.size());
                
                auto result = processor.process(std::span<const ChunkData>(&chunk_data, 1));
                if (!result) {
                    return result;
                }
            }
            
            return Ok();
        }
        
        // Multi-threaded reading
        std::vector<std::future<Result<void>>> futures;
        futures.reserve(num_threads_);
        
        // Divide chunks among threads
        std::size_t chunks_per_thread = (chunks.size() + num_threads_ - 1) / num_threads_;
        
        for (std::size_t thread_id = 0; thread_id < num_threads_; ++thread_id) {
            std::size_t start_idx = thread_id * chunks_per_thread;
            if (start_idx >= chunks.size()) {
                break;
            }
            
            std::size_t end_idx = std::min(start_idx + chunks_per_thread, chunks.size());
            
            // Launch async read task
            futures.push_back(std::async(std::launch::async,
                [&reader, chunks, &processor, start_idx, end_idx]() -> Result<void> {
                    
                    std::vector<std::byte> thread_buffer;
                    ChunkData chunk_data;
                    
                    for (std::size_t i = start_idx; i < end_idx; ++i) {
                        const auto& chunk = chunks[i];
                        if (chunk.byte_count == 0) {
                            continue;
                        }
                        
                        auto view_result = reader.read(chunk.offset, chunk.byte_count);
                        if (!view_result) {
                            return Err(view_result.error().code, view_result.error().message);
                        }
                        
                        auto view = std::move(view_result.value());
                        auto view_span = view.data();
                        
                        thread_buffer.clear();
                        thread_buffer.resize(view_span.size());
                        std::memcpy(thread_buffer.data(), view_span.data(), view_span.size());
                        
                        chunk_data.info = chunk;
                        chunk_data.data = std::span<const std::byte>(thread_buffer.data(), thread_buffer.size());
                        
                        // Invoke processor (must be thread-safe)
                        auto result = processor.process(std::span<const ChunkData>(&chunk_data, 1));
                        
                        if (!result) {
                            return result;
                        }
                    }
                    
                    return Ok();
                }
            ));
        }
        
        // Wait for all threads
        for (auto& future : futures) {
            auto result = future.get();
            if (!result) {
                return Err(result.error().code, result.error().message);
            }
        }
        
        return Ok();
    }
    
    void clear() noexcept {
        // Nothing to clear in the new design
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
