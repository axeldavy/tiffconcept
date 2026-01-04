#include <numeric>
#include <sstream>
#include <stdexcept>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "image_buffer.hpp"
#include "tiffconcept/types/tiff_spec.hpp"

namespace tiffconcept::python {

ImageBuffer::ImageBuffer(std::vector<ssize_t> shape, size_t itemsize,
                         std::string format, std::string format_str)
    : shape_(std::move(shape))
    , itemsize_(itemsize)
    , strides_(calculate_c_strides(shape_, itemsize_))
    , total_elements_(calculate_total_elements(shape_))
    , format_(std::move(format))
    , format_str_(std::move(format_str))
{
    // Allocate memory
    size_t total_bytes = total_elements_ * itemsize_;
    if (total_bytes > 0) {
        data_ = std::make_unique<uint8_t[]>(total_bytes);
        // Zero-initialize for safety
        std::memset(data_.get(), 0, total_bytes);
    }
}

std::vector<ssize_t> ImageBuffer::calculate_c_strides(
    const std::vector<ssize_t>& shape, size_t itemsize)
{
    if (shape.empty()) {
        return {};
    }
    
    std::vector<ssize_t> strides(shape.size());
    ssize_t stride = static_cast<ssize_t>(itemsize);
    
    // C-contiguous: iterate from last dimension to first
    for (ssize_t i = static_cast<ssize_t>(shape.size()) - 1; i >= 0; --i) {
        strides[i] = stride;
        stride *= shape[i];
    }
    
    return strides;
}

size_t ImageBuffer::calculate_total_elements(const std::vector<ssize_t>& shape) {
    if (shape.empty()) {
        return 0;
    }
    return std::accumulate(shape.begin(), shape.end(), size_t(1),
                          [](size_t a, ssize_t b) { return a * static_cast<size_t>(b); });
}

std::string ImageBuffer::repr() const {
    std::ostringstream oss;
    oss << "<ImageBuffer shape=(";
    for (size_t i = 0; i < shape_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << shape_[i];
    }
    oss << ") dtype=" << format_str_ << ">";
    return oss.str();
}

BufferFormat BufferFormat::from_sample_info(uint16_t bits_per_sample,
                                            tiffconcept::SampleFormat sample_format) {
    BufferFormat result;
    
    if (sample_format == tiffconcept::SampleFormat::UnsignedInt) {
        if (bits_per_sample == 8) {
            result.format = "B";
            result.format_str = "uint8";
            result.itemsize = 1;
        } else if (bits_per_sample == 16) {
            result.format = "H";
            result.format_str = "uint16";
            result.itemsize = 2;
        } else if (bits_per_sample == 32) {
            result.format = "I";
            result.format_str = "uint32";
            result.itemsize = 4;
        } else if (bits_per_sample == 64) {
            result.format = "Q";
            result.format_str = "uint64";
            result.itemsize = 8;
        } else {
            throw std::runtime_error("Unsupported bits_per_sample for UnsignedInt: " + 
                                   std::to_string(bits_per_sample));
        }
    } else if (sample_format == tiffconcept::SampleFormat::SignedInt) {
        if (bits_per_sample == 8) {
            result.format = "b";
            result.format_str = "int8";
            result.itemsize = 1;
        } else if (bits_per_sample == 16) {
            result.format = "h";
            result.format_str = "int16";
            result.itemsize = 2;
        } else if (bits_per_sample == 32) {
            result.format = "i";
            result.format_str = "int32";
            result.itemsize = 4;
        } else if (bits_per_sample == 64) {
            result.format = "q";
            result.format_str = "int64";
            result.itemsize = 8;
        } else {
            throw std::runtime_error("Unsupported bits_per_sample for SignedInt: " + 
                                   std::to_string(bits_per_sample));
        }
    } else if (sample_format == tiffconcept::SampleFormat::IEEEFloat) {
        if (bits_per_sample == 32) {
            result.format = "f";
            result.format_str = "float32";
            result.itemsize = 4;
        } else if (bits_per_sample == 64) {
            result.format = "d";
            result.format_str = "float64";
            result.itemsize = 8;
        } else {
            throw std::runtime_error("Unsupported bits_per_sample for IEEEFloat: " + 
                                   std::to_string(bits_per_sample));
        }
    } else {
        throw std::runtime_error("Unsupported sample_format");
    }
    
    return result;
}

} // namespace tiffconcept::python

using ImageBuffer = tiffconcept::python::ImageBuffer;

void bind_image_buffer(py::module_& m) {
    py::class_<ImageBuffer>(m, "ImageBuffer", py::buffer_protocol(),
        "Buffer object implementing Python's buffer protocol.\n\n"
        "This object can be passed to numpy.asarray() or any other function\n"
        "that accepts the buffer protocol to create a zero-copy view of the data.")
        .def_buffer([](ImageBuffer& buf) -> py::buffer_info {
            return py::buffer_info(
                buf.data(),                              // Pointer to buffer
                static_cast<ssize_t>(buf.itemsize()),    // Size of one element
                buf.format(),                             // Python struct-style format descriptor
                static_cast<ssize_t>(buf.ndim()),        // Number of dimensions
                buf.shape(),                              // Shape (dimensions)
                buf.strides()                             // Strides (in bytes)
            );
        })
        .def_property_readonly("shape", &ImageBuffer::shape,
                              "Shape of the buffer as a tuple")
        .def_property_readonly("strides", &ImageBuffer::strides,
                              "Strides of the buffer in bytes")
        .def_property_readonly("dtype", &ImageBuffer::format_str,
                              "Data type string (e.g., 'uint8', 'float32')")
        .def_property_readonly("itemsize", &ImageBuffer::itemsize,
                              "Size of each element in bytes")
        .def_property_readonly("ndim", &ImageBuffer::ndim,
                              "Number of dimensions")
        .def_property_readonly("size", &ImageBuffer::size,
                              "Total number of elements")
        .def_property_readonly("nbytes", &ImageBuffer::nbytes,
                              "Total number of bytes")
        .def("__repr__", &ImageBuffer::repr);
}

