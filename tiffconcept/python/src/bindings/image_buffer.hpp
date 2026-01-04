#pragma once

#include <pybind11/pybind11.h>
#include <memory>
#include <vector>
#include <cstdint>
#include <cstring>
#include <span>

#include "tiffconcept/types/tiff_spec.hpp"

namespace py = pybind11;

namespace tiffconcept::python {

/**
 * @brief Custom buffer object implementing Python's buffer protocol
 * 
 * This class provides a NumPy-compatible buffer without linking to NumPy.
 * Users can wrap it with numpy.asarray() to get a NumPy array.
 * 
 * Future optimization: Replace allocation with pooled aligned buffers.
 */
class ImageBuffer {
public:
    /**
     * @brief Construct a new ImageBuffer with the given shape and data type
     * 
     * @param shape Array dimensions
     * @param itemsize Size of each element in bytes
     * @param format Format string (NumPy-compatible, e.g., "B" for uint8, "H" for uint16)
     * @param format_str Human-readable format (e.g., "uint8", "float32")
     */
    ImageBuffer(std::vector<ssize_t> shape, size_t itemsize, 
                std::string format, std::string format_str);
    
    // Disable copy, allow move
    ImageBuffer(const ImageBuffer&) = delete;
    ImageBuffer& operator=(const ImageBuffer&) = delete;
    ImageBuffer(ImageBuffer&&) = default;
    ImageBuffer& operator=(ImageBuffer&&) = default;
    
    // Access to buffer data
    void* data() { return data_.get(); }
    const void* data() const { return data_.get(); }
    
    // Buffer dimensions
    const std::vector<ssize_t>& shape() const { return shape_; }
    const std::vector<ssize_t>& strides() const { return strides_; }
    size_t itemsize() const { return itemsize_; }
    size_t ndim() const { return shape_.size(); }
    size_t size() const { return total_elements_; }
    size_t nbytes() const { return total_elements_ * itemsize_; }
    
    // Format information
    const std::string& format() const { return format_; }
    const std::string& format_str() const { return format_str_; }
    
    // Get a span over the raw buffer
    std::span<uint8_t> as_span() {
        return std::span<uint8_t>(static_cast<uint8_t*>(data_.get()), nbytes());
    }
    
    // String representation
    std::string repr() const;

private:
    std::unique_ptr<uint8_t[]> data_;
    std::vector<ssize_t> shape_;
    size_t itemsize_;
    std::vector<ssize_t> strides_;
    size_t total_elements_;
    std::string format_;       // Buffer protocol format (e.g., "B", "H", "f")
    std::string format_str_;   // Human-readable format (e.g., "uint8", "float32")
    
    // Calculate strides for C-contiguous layout
    static std::vector<ssize_t> calculate_c_strides(
        const std::vector<ssize_t>& shape, size_t itemsize);
    
    // Calculate total number of elements
    static size_t calculate_total_elements(const std::vector<ssize_t>& shape);
};

/**
 * @brief Helper to determine buffer protocol format string from dtype parameters
 */
struct BufferFormat {
    std::string format;      // Buffer protocol format (e.g., "B", "H", "f")
    std::string format_str;  // Human-readable (e.g., "uint8", "float32")
    size_t itemsize;
    
    // Create from bits_per_sample and sample_format
    static BufferFormat from_sample_info(uint16_t bits_per_sample, 
                                         tiffconcept::SampleFormat sample_format);
};

} // namespace tiffconcept::python

// Bind the ImageBuffer class to Python
void bind_image_buffer(py::module_& m);
