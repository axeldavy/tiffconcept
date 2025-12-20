#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <span>
#include <vector>
#include "../reader_base.hpp"

namespace tiffconcept {
namespace buffer_impl {

/// Read-only view for borrowed buffer data (zero-copy)
class BorrowedBufferReadView {
private:
    std::span<const std::byte> data_;

public:
    BorrowedBufferReadView() noexcept = default;
    
    explicit BorrowedBufferReadView(std::span<const std::byte> data) noexcept
        : data_(data) {}
    
    [[nodiscard]] std::span<const std::byte> data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    BorrowedBufferReadView(BorrowedBufferReadView&&) noexcept = default;
    BorrowedBufferReadView& operator=(BorrowedBufferReadView&&) noexcept = default;
    BorrowedBufferReadView(const BorrowedBufferReadView&) = delete;
    BorrowedBufferReadView& operator=(const BorrowedBufferReadView&) = delete;
};

static_assert(DataReadOnlyView<BorrowedBufferReadView>, "BorrowedBufferReadView must satisfy DataReadOnlyView concept");

/// Write-only view for borrowed buffer data (zero-copy, writes directly)
class BorrowedBufferWriteView {
private:
    std::span<std::byte> data_;

public:
    BorrowedBufferWriteView() noexcept = default;
    
    explicit BorrowedBufferWriteView(std::span<std::byte> data) noexcept
        : data_(data) {}

    static constexpr bool supports_inplace_readback = true; // We will assume readback is possible. For specific cases where it is not, redefine this class.

    [[nodiscard]] std::span<std::byte> data() noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    // For write views, flush is a no-op (writes are immediate to buffer)
    [[nodiscard]] Result<void> flush() noexcept {
        return Ok();
    }
    
    BorrowedBufferWriteView(BorrowedBufferWriteView&&) noexcept = default;
    BorrowedBufferWriteView& operator=(BorrowedBufferWriteView&&) noexcept = default;
    BorrowedBufferWriteView(const BorrowedBufferWriteView&) = delete;
    BorrowedBufferWriteView& operator=(const BorrowedBufferWriteView&) = delete;
};

static_assert(DataWriteOnlyView<BorrowedBufferWriteView>, "BorrowedBufferWriteView must satisfy DataWriteOnlyView concept");
static_assert(DataWriteViewWithReadback<BorrowedBufferWriteView>, "BorrowedBufferWriteView must support readback");

namespace detail {
    // Access mode policies for in-memory buffers
    struct ReadOnlyAccess {
        static constexpr bool can_read = true;
        static constexpr bool can_write = false;
    };
    
    struct WriteOnlyAccess {
        static constexpr bool can_read = false;
        static constexpr bool can_write = true;
    };
    
    struct ReadWriteAccess {
        static constexpr bool can_read = true;
        static constexpr bool can_write = true;
    };
} // namespace detail

} // namespace buffer_impl

/// Base template for in-memory buffer view (borrowed, zero-copy)
/// AccessPolicy determines read/write capabilities
template <typename AccessPolicy>
class BufferViewBase {
protected:
    // For read-only: const buffer
    // For write-only or read-write: mutable buffer
    using StorageType = std::conditional_t<
        AccessPolicy::can_write,
        std::span<std::byte>,
        std::span<const std::byte>
    >;
    
    StorageType buffer_;

public:
    using ReadViewType = buffer_impl::BorrowedBufferReadView;
    using WriteViewType = buffer_impl::BorrowedBufferWriteView;
    
    BufferViewBase() noexcept = default;
    
    // Constructor for read-only access (const buffer)
    explicit BufferViewBase(std::span<const std::byte> buffer) noexcept 
        requires (!AccessPolicy::can_write)
        : buffer_(buffer) {}
    
    // Constructor for writable access (mutable buffer)
    explicit BufferViewBase(std::span<std::byte> buffer) noexcept 
        requires (AccessPolicy::can_write)
        : buffer_(buffer) {}
    
    // Template constructor for any span type (read-only)
    template <typename T>
    explicit BufferViewBase(std::span<const T> data) noexcept 
        requires (!AccessPolicy::can_write)
        : buffer_(std::as_bytes(data)) {}
    
    // Template constructor for any span type (writable)
    template <typename T>
    explicit BufferViewBase(std::span<T> data) noexcept 
        requires (AccessPolicy::can_write)
        : buffer_(std::as_writable_bytes(data)) {}
    
    /// Zero-copy read (only available if can_read is true)
    [[nodiscard]] Result<ReadViewType> read(std::size_t offset, std::size_t size) const noexcept 
        requires (AccessPolicy::can_read) {
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "Buffer not set");
        }
        
        if (offset >= buffer_.size()) [[unlikely]] {
            return Err(Error::Code::OutOfBounds, "Read offset beyond buffer size");
        }
        
        std::size_t bytes_to_read = std::min(size, buffer_.size() - offset);
        
        // Convert to const span for read view
        std::span<const std::byte> const_span;
        if constexpr (AccessPolicy::can_write) {
            const_span = std::span<const std::byte>(buffer_.data() + offset, bytes_to_read);
        } else {
            const_span = std::span<const std::byte>(buffer_.data() + offset, bytes_to_read);
        }
        
        return Ok(buffer_impl::BorrowedBufferReadView(const_span));
    }
    
    /// Zero-copy write (only available if can_write is true)
    [[nodiscard]] Result<WriteViewType> write(std::size_t offset, std::size_t size) noexcept 
        requires (AccessPolicy::can_write) {
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::WriteError, "Buffer not set");
        }
        
        if (offset >= buffer_.size()) [[unlikely]] {
            return Err(Error::Code::OutOfBounds, "Write offset beyond buffer size");
        }
        
        std::size_t bytes_to_write = std::min(size, buffer_.size() - offset);
        std::span<std::byte> data_span(buffer_.data() + offset, bytes_to_write);
        
        return Ok(buffer_impl::BorrowedBufferWriteView(data_span));
    }
    
    [[nodiscard]] Result<std::size_t> size() const noexcept {
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "Buffer not set");
        }
        return Ok(buffer_.size());
    }
    
    /// Resize buffer (only available if can_write is true)
    /// For borrowed buffers, this can only "succeed" if the size matches
    [[nodiscard]] Result<void> resize(std::size_t new_size) noexcept 
        requires (AccessPolicy::can_write) {
        if (new_size != buffer_.size()) [[unlikely]] {
            return Err(Error::Code::WriteError, "Cannot resize borrowed buffer");
        }
        return Ok();
    }
    
    /// Flush writes (only available if can_write is true)
    /// No-op for in-memory buffer (writes are immediate)
    [[nodiscard]] Result<void> flush() noexcept 
        requires (AccessPolicy::can_write) {
        return Ok();
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        // An empty buffer is still valid - it just has zero size
        return true;
    }
    
    // Set buffer (read-only version)
    void set_buffer(std::span<const std::byte> buffer) noexcept 
        requires (!AccessPolicy::can_write) {
        buffer_ = buffer;
    }
    
    // Set buffer (writable version)
    void set_buffer(std::span<std::byte> buffer) noexcept 
        requires (AccessPolicy::can_write) {
        buffer_ = buffer;
    }
    
    // Get buffer (const version for read-only)
    [[nodiscard]] std::span<const std::byte> buffer() const noexcept 
        requires (!AccessPolicy::can_write) {
        return buffer_;
    }
    
    // Get buffer (mutable version for writable)
    [[nodiscard]] std::span<std::byte> buffer() noexcept 
        requires (AccessPolicy::can_write) {
        return buffer_;
    }
    
    // Get buffer (const version for writable - can always read from writable)
    [[nodiscard]] std::span<const std::byte> buffer() const noexcept 
        requires (AccessPolicy::can_write) {
        return buffer_;
    }
};

/// In-memory buffer view reader (borrowed, zero-copy, thread-safe for immutable buffers)
using BufferViewReader = BufferViewBase<buffer_impl::detail::ReadOnlyAccess>;

static_assert(RawReader<BufferViewReader>, "BufferViewReader must satisfy RawReader concept");

/// In-memory buffer view writer (borrowed, zero-copy, writes directly to buffer)
using BufferViewWriter = BufferViewBase<buffer_impl::detail::WriteOnlyAccess>;

static_assert(RawWriter<BufferViewWriter>, "BufferViewWriter must satisfy RawWriter concept");

/// In-memory buffer view reader+writer (borrowed, zero-copy, reads and writes directly)
using BufferViewReadWriter = BufferViewBase<buffer_impl::detail::ReadWriteAccess>;

static_assert(RawReader<BufferViewReadWriter>, "BufferViewReadWriter must satisfy RawReader concept");
static_assert(RawWriter<BufferViewReadWriter>, "BufferViewReadWriter must satisfy RawWriter concept");

// ============================================================================
// Owned buffer implementations backed by std::vector
// ============================================================================

/// Base template for owned in-memory buffer (backed by std::vector)
/// AccessPolicy determines read/write capabilities
template <typename AccessPolicy>
class BufferBase {
protected:
    std::vector<std::byte> buffer_;

public:
    using ReadViewType = buffer_impl::BorrowedBufferReadView;
    using WriteViewType = buffer_impl::BorrowedBufferWriteView;
    
    BufferBase() noexcept = default;
    
    // Constructor with initial size
    explicit BufferBase(std::size_t size) 
        : buffer_(size) {}
    
    // Constructor from existing data (copy)
    explicit BufferBase(std::span<const std::byte> data) 
        : buffer_(data.begin(), data.end()) {}
    
    // Template constructor from any span type (copy)
    template <typename T>
    explicit BufferBase(std::span<const T> data) 
        : buffer_(std::as_bytes(data).begin(), std::as_bytes(data).end()) {}
    
    /// Zero-copy read (only available if can_read is true)
    [[nodiscard]] Result<ReadViewType> read(std::size_t offset, std::size_t size) const noexcept 
        requires (AccessPolicy::can_read) {
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::ReadError, "Buffer not initialized");
        }
        
        if (offset >= buffer_.size()) [[unlikely]] {
            return Err(Error::Code::OutOfBounds, "Read offset beyond buffer size");
        }
        
        std::size_t bytes_to_read = std::min(size, buffer_.size() - offset);
        std::span<const std::byte> const_span(buffer_.data() + offset, bytes_to_read);
        
        return Ok(buffer_impl::BorrowedBufferReadView(const_span));
    }
    
    /// Zero-copy write (only available if can_write is true)
    [[nodiscard]] Result<WriteViewType> write(std::size_t offset, std::size_t size) noexcept 
        requires (AccessPolicy::can_write) {
        if (!is_valid()) [[unlikely]] {
            return Err(Error::Code::WriteError, "Buffer not initialized");
        }
        
        if (offset >= buffer_.size()) [[unlikely]] {
            return Err(Error::Code::OutOfBounds, "Write offset beyond buffer size");
        }
        
        std::size_t bytes_to_write = std::min(size, buffer_.size() - offset);
        std::span<std::byte> data_span(buffer_.data() + offset, bytes_to_write);
        
        return Ok(buffer_impl::BorrowedBufferWriteView(data_span));
    }
    
    [[nodiscard]] Result<std::size_t> size() const noexcept {
        return Ok(buffer_.size());
    }
    
    /// Resize buffer (only available if can_write is true)
    /// For owned buffers, this can actually resize the vector
    [[nodiscard]] Result<void> resize(std::size_t new_size) noexcept 
        requires (AccessPolicy::can_write) {
        try {
            buffer_.resize(new_size);
            return Ok();
        } catch (...) {
            return Err(Error::Code::WriteError, "Failed to resize buffer");
        }
    }
    
    /// Flush writes (only available if can_write is true)
    /// No-op for in-memory buffer (writes are immediate)
    [[nodiscard]] Result<void> flush() noexcept 
        requires (AccessPolicy::can_write) {
        return Ok();
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        return true;  // Owned buffers are always valid (even if empty)
    }
    
    // Get buffer as const span
    [[nodiscard]] std::span<const std::byte> buffer() const noexcept {
        return buffer_;
    }
    
    // Get buffer as mutable span (only for writable access)
    [[nodiscard]] std::span<std::byte> buffer() noexcept 
        requires (AccessPolicy::can_write) {
        return buffer_;
    }
    
    // Get underlying vector (const)
    [[nodiscard]] const std::vector<std::byte>& data() const noexcept {
        return buffer_;
    }
    
    // Get underlying vector (mutable, only for writable access)
    [[nodiscard]] std::vector<std::byte>& data() noexcept 
        requires (AccessPolicy::can_write) {
        return buffer_;
    }
};

/// In-memory owned buffer reader (backed by vector, thread-safe for immutable buffers)
using BufferReader = BufferBase<buffer_impl::detail::ReadOnlyAccess>;

static_assert(RawReader<BufferReader>, "BufferReader must satisfy RawReader concept");

/// In-memory owned buffer writer (backed by vector, writes directly to buffer)
using BufferWriter = BufferBase<buffer_impl::detail::WriteOnlyAccess>;

static_assert(RawWriter<BufferWriter>, "BufferWriter must satisfy RawWriter concept");

/// In-memory owned buffer reader+writer (backed by vector, reads and writes directly)
using BufferReadWriter = BufferBase<buffer_impl::detail::ReadWriteAccess>;

static_assert(RawReader<BufferReadWriter>, "BufferReadWriter must satisfy RawReader concept");
static_assert(RawWriter<BufferReadWriter>, "BufferReadWriter must satisfy RawWriter concept");

} // namespace tiffconcept