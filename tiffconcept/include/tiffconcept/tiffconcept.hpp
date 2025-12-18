#pragma once

/// Main header for the TIFF reader library
/// 
/// This library provides a modern C++ interface for reading TIFF files
/// with compile-time tag specification and strong typing.
/// 
/// Key features:
/// - No exceptions: uses Result<T> for error handling
/// - Compile-time tag specification using templates
/// - Thread-safe reading via RawReader concept
/// - Strong typing with concepts and constexpr
/// - Support for custom tag sets
/// 
/// Example usage:
/// ```cpp
/// #include <tiff/tiff.hpp>
/// 
/// using namespace tiffconcept;
/// 
/// // Define your tag specification
/// using MySpec = TagSpec<
///     ImageWidthTag,
///     ImageLengthTag,
///     BitsPerSampleTag,
///     CompressionTag
/// >;
/// 
/// // Open a file
/// FileReader reader("image.tif");
/// if (!reader.is_valid()) {
///     // Handle error
/// }
/// 
/// // Extract metadata
/// auto result = extract_metadata<MySpec>(reader);
/// if (result) {
///     auto& metadata = result.value();
///     auto width = metadata.get<TagCode::ImageWidth>();
///     auto height = metadata.get<TagCode::ImageLength>();
/// }
/// ```

#include "result.hpp"
#include "types.hpp"
#include "reader_base.hpp"
#include "tag_spec.hpp"
#include "image_shape.hpp"
