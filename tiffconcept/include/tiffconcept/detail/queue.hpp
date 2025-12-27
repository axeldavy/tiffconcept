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
struct LockFreeJobQueue {
    alignas(64) std::atomic<size_t> write_idx{0};  // Producer only
    alignas(64) std::atomic<size_t> read_idx{0};   // Consumers compete here
    alignas(64) std::vector<Job> jobs;             // Pre-allocated ring buffer
    size_t capacity_;                              // Max pending jobs

    LockFreeJobQueue() = default;
    
    LockFreeJobQueue(size_t capacity) {
        capacity_ = capacity;
        jobs.resize(capacity_);
    }

    /// @brief Reset and resize the queue
    void reset(size_t new_capacity) {
        write_idx.store(0, std::memory_order_relaxed);
        read_idx.store(0, std::memory_order_relaxed);
        capacity_ = new_capacity;
        jobs.resize(new_capacity);
    }
    
    /// @brief Try to pop a job (multi-consumer safe)
    /// @return true if job was popped, false if queue empty
    bool try_pop(Job& out_job) {
        // Load current read position
        size_t current_read = read_idx.load(std::memory_order_acquire);
        
        while (true) {
            size_t current_write = write_idx.load(std::memory_order_acquire);
            
            // Queue empty?
            if (current_read >= current_write) {
                return false;
            }
            
            // Try to claim this slot (compete with other consumers)
            if (read_idx.compare_exchange_weak(
                    current_read, 
                    current_read + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                
                // We won! Read the job
                out_job = std::move(jobs[current_read % capacity_]);
                return true;
            }
            
            // CAS failed, another thread claimed it. Retry with updated current_read
        }
    }
    
    /// @brief Push multiple jobs at once (single-producer only)
    /// @param new_jobs Jobs to push
    /// @return true if all jobs pushed, false if queue full
    bool push_batch(std::span<Job> new_jobs) {
        size_t current_write = write_idx.load(std::memory_order_relaxed);
        size_t current_read = read_idx.load(std::memory_order_acquire);
        
        // Check capacity
        size_t available = capacity_ - (current_write - current_read);
        if (new_jobs.size() > available) {
            return false; // Queue full
        }
        
        // Copy jobs into ring buffer
        for (size_t i = 0; i < new_jobs.size(); ++i) {
            jobs[(current_write + i) % capacity_] = std::move(new_jobs[i]);
        }
        
        // Publish all jobs at once
        write_idx.store(current_write + new_jobs.size(), std::memory_order_release);
        return true;
    }
    
    /// @brief Push a single job (single-producer only)
    void push(Job job) {
        Job jobs_array[1] = {std::move(job)};
        push_batch(jobs_array);
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

} // namespace tiffconcept::detail