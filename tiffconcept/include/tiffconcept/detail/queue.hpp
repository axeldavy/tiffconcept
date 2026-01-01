#pragma once

#include <atomic>
#include <cstddef>
#include <vector>


namespace tiffconcept::detail {
/// @brief Lock-free single-producer, multi-consumer queue for tile jobs
/// 
/// Optimized for:
/// - Single thread pushes tiles in batches (after I/O completion)
/// - Multiple workers try_pop frequently
/// - Cache-line optimized to reduce false sharing
template <typename Job>
struct alignas(64) LockFreeJobQueue {
    alignas(64) std::atomic<size_t> write_idx{0};  // Producer only
    alignas(64) std::atomic<size_t> read_idx{0};   // Consumers compete here
    alignas(64) std::vector<Job> jobs;             // Pre-allocated buffer
    size_t capacity_;                              // Max jobs, pending or not

    LockFreeJobQueue() = default;
    
    LockFreeJobQueue(size_t capacity) {
        capacity_ = capacity;
        jobs.resize(capacity_);
    }

    /// @brief Reset and resize the queue
    /// @note queue must be unused when calling reset
    void reset(size_t new_capacity) {
        write_idx.store(0, std::memory_order_release);
        read_idx.store(0, std::memory_order_release);
        capacity_ = new_capacity;
        jobs.resize(new_capacity);
    }
    
    /// @brief Try to pop a job (multi-consumer safe)
    /// @return true if job was popped, false if queue empty
    bool try_pop(Job& out_job) {
        size_t current_read = read_idx.load(std::memory_order_acquire);
        
        while (true) {
            // current_read is updated by compare_exchange_strong
            size_t current_write = write_idx.load(std::memory_order_acquire);
    
            // Queue empty?
            if (current_read >= current_write) {
                return false;
            }
            
            // Try to claim this slot (compete with other consumers)
            if (read_idx.compare_exchange_strong(
                    current_read, 
                    current_read + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                
                // We won! Read the job
                out_job = std::move(jobs[current_read]);
                return true;
            }
            
            // CAS failed, another thread claimed it. Retry with updated current_read
        }
    }
    
    /// @brief Push a single job (single-producer only)
    void push(Job job) {
        size_t current_write = write_idx.load(std::memory_order_acquire);

        assert(current_write < capacity_ && "Queue full on push");
        jobs[current_write] = std::move(job);

        // Publish job, release to ensure write finishes before updating write_idx
        write_idx.store(current_write + 1, std::memory_order_release);
    }
    
    /// @brief Get current queue size (approximate)
    size_t size() const {
        size_t write = write_idx.load(std::memory_order_acquire);
        size_t read = read_idx.load(std::memory_order_acquire);
        return write - read;
    }
    
    /// @brief Check if queue is empty (approximate)
    bool empty() const {
        return size() == 0;
    }
};

/// @brief Lock-free multi-producer, single-consumer queue for completion tokens
/// 
/// Uses sequence numbers to handle out-of-order writes from multiple producers.
/// Consumer can safely read tokens even if producers complete out-of-order.
/// 
/// Optimized for:
/// - Multiple worker threads push completion tokens after processing tiles
/// - Single consumer thread (which may also be a producer) batches completions
/// - No blocking: consumer never waits for slow producers
/// - Cache-line optimized to reduce false sharing
template <typename Token>
struct alignas(64) LockFreeMPSCQueue {
    alignas(64) std::atomic<size_t> claim_idx{0};     // Producers claim slots here
    alignas(64) std::atomic<size_t> read_idx{0};      // Consumer reads from here
    alignas(64) std::vector<Token> tokens;            // Pre-allocated buffer

    // Padded atomic to prevent false sharing between sequence slots
    struct alignas(64) PaddedAtomic {
        std::atomic<size_t> value;
        
        PaddedAtomic() : value(0) {}
        explicit PaddedAtomic(size_t v) : value(v) {}
        
        // Provide atomic-like interface
        size_t load(std::memory_order order) const noexcept {
            return value.load(order);
        }
        
        void store(size_t v, std::memory_order order) noexcept {
            value.store(v, order);
        }
    };
    alignas(64) std::unique_ptr<PaddedAtomic[]> sequences;  // Per-slot sequence numbers
    size_t capacity_;                                 // Max tokens

    LockFreeMPSCQueue() = default;
    
    LockFreeMPSCQueue(size_t capacity) : capacity_(capacity) {
        tokens.resize(capacity_);
        sequences = std::make_unique<PaddedAtomic[]>(capacity_);
        // Initialize sequences: slot i is ready when sequences[i] == i
        for (size_t i = 0; i < capacity_; ++i) {
            sequences[i].store(SIZE_MAX, std::memory_order_release);
        }
    }

    /// @brief Reset and resize the queue
    /// @note queue must be unused when calling reset
    void reset(size_t new_capacity) {
        claim_idx.store(0, std::memory_order_release);
        read_idx.store(0, std::memory_order_release);
        tokens.resize(new_capacity);
        if (new_capacity > capacity_)
            sequences = std::make_unique<PaddedAtomic[]>(new_capacity);
        for (size_t i = 0; i < new_capacity; ++i) {
            sequences[i].store(SIZE_MAX, std::memory_order_release);
        }
        capacity_ = new_capacity;
    }
    
    /// @brief Push a token (multi-producer safe, lock-free)
    void push(Token token) {
        // Atomically claim a slot
        size_t slot = claim_idx.fetch_add(1, std::memory_order_acquire);
        
        // Queue full?
        assert(slot < capacity_ && "Queue full on push");
        
        // Write token to claimed slot (no one else can write here)
        tokens[slot] = std::move(token);
        
        // Publish: mark this slot as ready by storing its sequence number
        // This creates a happens-before relationship with consumer's acquire load
        sequences[slot].store(slot, std::memory_order_release);
    }
    
    /// @brief Try to pop a single token (single-consumer only, lock-free)
    /// @return true if token was popped, false if queue empty or next slot not ready
    bool try_pop(Token& out_token) {
        size_t current_read = read_idx.load(std::memory_order_acquire);

        if (current_read >= capacity_) {
            // Reached capacity - no more items can come
            return false;
        }
        
        // Check if next slot is ready
        // If sequence[current_read] == current_read, the token is published
        size_t seq = sequences[current_read].load(std::memory_order_acquire);
        
        if (seq != current_read) {
            // Slot not ready yet - either empty or producer still writing
            return false;
        }
        
        // Slot is ready! Read the token
        out_token = std::move(tokens[current_read]);
        
        // Mark slot as consumed (for debugging/reuse)
        sequences[current_read].store(SIZE_MAX, std::memory_order_release);
        
        // Advance read position
        read_idx.store(current_read + 1, std::memory_order_release);
        
        return true;
    }

    /// @brief Pop a single token (single-consumer only, lock-free) (fails only if empty)
    bool pop(Token& out_token) {
        while (true) {
            if (try_pop(out_token)) {
                return true;
            }
#if 0       // alternate version that fails when capacity reached
            // Check if we're truly at capacity (no more items can come)
            size_t current_read = read_idx.load(std::memory_order_acquire);
            size_t claimed = claim_idx.load(std::memory_order_acquire);
            if (current_read >= claimed && current_read >= capacity_) {
                return false;
            }
#endif
            // check if all producers are done with their pending writes
            size_t current_read = read_idx.load(std::memory_order_acquire);
            if (current_read >= claim_idx.load(std::memory_order_acquire)) {
                return false; // queue empty
            }            

            // Brief yield to avoid burning CPU
            std::this_thread::yield();
        }
    }
    
    /// @brief Get current queue size (approximate, may include in-flight writes)
    size_t size() const {
        size_t claimed = claim_idx.load(std::memory_order_acquire);
        size_t read = read_idx.load(std::memory_order_acquire);
        return claimed > read ? claimed - read : 0;
    }
    
    /// @brief Check if queue is empty (approximate)
    bool empty() const {
        return size() == 0;
    }
};

} // namespace tiffconcept::detail