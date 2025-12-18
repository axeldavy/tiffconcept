#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include "result.hpp"

namespace tiffconcept {

/// Concept for a read-only view into data with RAII lifetime management
/// Only one thread at a time should access the view
template <typename T>
concept DataReadOnlyView = requires(T view) {
    // Access to the underlying data
    { view.data() } -> std::same_as<std::span<const std::byte>>;
    
    // Size of the data
    { view.size() } -> std::same_as<std::size_t>;
    
    // Check if view is empty
    { view.empty() } -> std::same_as<bool>;
    
    // Must be movable for Result<T> and transferring ownership
    requires std::move_constructible<T>;
    requires std::is_nothrow_move_constructible_v<T>;
};


/// Concept for a write-only view into data with RAII lifetime management
/// Data can be written to the storage when the view is released/destroyed,
/// when flush() is called or directly when accessing the data.
template <typename T>
concept DataWriteOnlyView = requires(T view) {
    // Access to the underlying writable data
    { view.data() } -> std::same_as<std::span<std::byte>>;
    
    // Size of the writable region
    { view.size() } -> std::same_as<std::size_t>;
    
    // Check if view is empty
    { view.empty() } -> std::same_as<bool>;
    
    // Must be movable for Result<T> and transferring ownership
    requires std::move_constructible<T>;
    requires std::is_nothrow_move_constructible_v<T>;
    
    // Must have a way to finalize the write (either explicit or in destructor)
    // flush() returns Result<void> to indicate success/failure
    { view.flush() } -> std::same_as<Result<void>>;
};


/// Check if a DataWriteOnlyView supports reading back written data before flush()
/// This allows in-place operations like decompression + predictor decoding
/// without requiring a separate temporary buffer.
/// 
/// To opt-in, the view type must define a static constexpr bool member:
///   static constexpr bool supports_inplace_readback = true;
template <typename T>
concept DataWriteViewWithReadback = DataWriteOnlyView<T> && requires(const T view) {
    // Allow reading what was written
    // This must work BEFORE flush() is called
    { T::supports_inplace_readback } -> std::convertible_to<bool>;
    requires T::supports_inplace_readback == true;
};


/// Concept for a raw reader that provides thread-safe positioned reads
template <typename T>
concept RawReader = requires(const T reader, std::size_t offset, std::size_t size) {
    // Read operation returning a view (implementation may use zero-copy or allocate)
    // The returned view type must satisfy DataReadOnlyView concept
    // Calls to read() must be threadsafe. Several threads may use the results
    // of read() simultaneously.
    { reader.read(offset, size) } -> std::same_as<Result<typename T::ReadViewType>>;
    requires DataReadOnlyView<typename T::ReadViewType>;
    
    // Get total size of the readable content
    { reader.size() } -> std::same_as<Result<std::size_t>>;
    
    // Check if reader is valid/open
    { reader.is_valid() } -> std::same_as<bool>;
};

/// Concept for a raw writer that provides thread-safe positioned writes
template <typename T>
concept RawWriter = requires(T writer, std::size_t offset, std::size_t size) {
    // Allocate/map a writable region at the given offset
    // The returned view type must satisfy DataWriteOnlyView concept
    // Calls to write() must be threadsafe. Several threads may use the results
    // of write() simultaneously.
    // If the item supports both RawReader and RawWriter, simultaneous reads and writes
    // must be safe as long as they do not overlap. Overlapping reads/writes result in
    // undefined behavior.
    { writer.write(offset, size) } -> std::same_as<Result<typename T::WriteViewType>>;
    requires DataWriteOnlyView<typename T::WriteViewType>;
    
    // Get total size of the writable content
    { writer.size() } -> std::same_as<Result<std::size_t>>;
    
    // Extend the file/buffer to a new size (for appending)
    { writer.resize(size) } -> std::same_as<Result<void>>;
    
    // Flush all pending writes to storage
    { writer.flush() } -> std::same_as<Result<void>>;
    
    // Check if writer is valid/open
    { writer.is_valid() } -> std::same_as<bool>;
};

} // namespace tiffconcept
