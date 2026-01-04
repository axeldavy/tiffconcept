#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/typing.h>

#include "../core/file_handle.hpp"
#include "tiff_page.hpp"
#include "image_buffer.hpp"
#include "exceptions.hpp"

namespace py = pybind11;
using namespace tiffconcept;
using namespace tiffconcept::python;

/**
 * @brief Python wrapper for a single TIFF page
 * 
 * Provides access to page metadata and lazy-loaded image data.
 */
PyTiffPage::PyTiffPage(std::shared_ptr<FileHandle> file_handle, size_t page_index)
    : file_handle_(file_handle), page_index_(page_index) {
    
    // Get page info
    auto page_info_result = file_handle_->get_page_info(page_index);
    if (page_info_result.is_error()) {
        translate_error(page_info_result.error());
    }
    page_info_ = page_info_result.value();
}

// Properties
uint32_t PyTiffPage::width() const { return page_info_.shape.image_width(); }
uint32_t PyTiffPage::height() const { return page_info_.shape.image_height(); }
uint32_t PyTiffPage::depth() const { return page_info_.shape.image_depth(); }
uint16_t PyTiffPage::channels() const { return page_info_.shape.samples_per_pixel(); }
uint16_t PyTiffPage::bits_per_sample() const { return page_info_.shape.bits_per_sample(); }
SampleFormat PyTiffPage::sample_format() const {
    return page_info_.tags.template get<TagCode::SampleFormat>().value_or(SampleFormat::UnsignedInt);
}

// Get numpy dtype string
std::string PyTiffPage::dtype() const {
    uint16_t bps = bits_per_sample();
    SampleFormat fmt = sample_format();
    
    if (fmt == SampleFormat::UnsignedInt) {
        if (bps == 8) return "uint8";
        if (bps == 16) return "uint16";
        if (bps == 32) return "uint32";
        if (bps == 64) return "uint64";
    } else if (fmt == SampleFormat::SignedInt) {
        if (bps == 8) return "int8";
        if (bps == 16) return "int16";
        if (bps == 32) return "int32";
        if (bps == 64) return "int64";
    } else if (fmt == SampleFormat::IEEEFloat) {
        if (bps == 32) return "float32";
        if (bps == 64) return "float64";
    }
    return "unknown";
}

// Get storage layout ("DHWC" for chunky, "CDHW" for planar)
std::string PyTiffPage::storage_layout() const {
    PlanarConfiguration config = page_info_.shape.planar_configuration();
    if (config == PlanarConfiguration::Planar) {
        return "CDHW";
    } else {
        return "DHWC";  // Chunky
    }
}

// Get compression scheme
CompressionScheme PyTiffPage::compression() const {
    return page_info_.tags.template get<TagCode::Compression>();
}

// Get photometric interpretation
PhotometricInterpretation PyTiffPage::photometric() const {
    return page_info_.tags.template get<TagCode::PhotometricInterpretation>().value_or(PhotometricInterpretation::MinIsBlack);
}

// Get shape as tuple
py::tuple PyTiffPage::shape(const std::string& layout) const {
    if (layout == "HW") {
        // Height, Width only - requires depth=1 and channels=1
        if (depth() != 1) {
            throw py::value_error("Layout 'HW' requires depth=1, but depth=" + 
                                std::to_string(depth()));
        }
        if (channels() != 1) {
            throw py::value_error("Layout 'HW' requires channels=1, but channels=" + 
                                std::to_string(channels()));
        }
        return py::make_tuple(height(), width());
        
    } else if (layout == "HWC") {
        // Height, Width, Channels - requires depth=1
        if (depth() != 1) {
            throw py::value_error("Layout 'HWC' requires depth=1, but depth=" + 
                                std::to_string(depth()));
        }
        return py::make_tuple(height(), width(), channels());
        
    } else if (layout == "DHW") {
        // Depth, Height, Width - requires channels=1
        if (channels() != 1) {
            throw py::value_error("Layout 'DHW' requires channels=1, but channels=" + 
                                std::to_string(channels()));
        }
        return py::make_tuple(depth(), height(), width());
        
    } else if (layout == "CHW") {
        // Channels, Height, Width - requires depth=1
        if (depth() != 1) {
            throw py::value_error("Layout 'CHW' requires depth=1, but depth=" + 
                                std::to_string(depth()));
        }
        return py::make_tuple(channels(), height(), width());
        
    } else if (layout == "DHWC") {
        return py::make_tuple(depth(), height(), width(), channels());
        
    } else if (layout == "DCHW") {
        return py::make_tuple(depth(), channels(), height(), width());
        
    } else if (layout == "CDHW") {
        return py::make_tuple(channels(), depth(), height(), width());
        
    } else {
        throw py::value_error("Invalid layout string: '" + layout + 
                            "'. Must be one of: 'HW', 'HWC', 'DHW', 'CHW', 'DHWC', 'DCHW', 'CDHW'");
    }
}

// Read the page data with optional region and destination buffer
py::object PyTiffPage::read(std::optional<ImageRegion> region_opt,
                            py::object dst,
                            const std::string& layout_str,
                            bool use_threading) {
    // Use full region if none specified
    ImageRegion region = region_opt.value_or(page_info_.shape.full_region());
    
    // Validate region
    auto validate_result = page_info_.shape.validate_region(region);
    if (validate_result.is_error()) {
        translate_error(validate_result.error());
    }
    
    // Validate layout string and determine internal layout and output shape
    ImageLayoutSpec internal_layout;
    std::vector<ssize_t> shape;
    
    if (layout_str == "HW") {
        // Height, Width only - requires depth=1 and channels=1
        if (region.depth != 1) {
            throw py::value_error("Layout 'HW' requires depth=1, but depth=" + 
                                std::to_string(region.depth));
        }
        if (region.num_channels != 1) {
            throw py::value_error("Layout 'HW' requires channels=1, but channels=" + 
                                std::to_string(region.num_channels));
        }
        internal_layout = ImageLayoutSpec::DHWC;
        shape = {static_cast<ssize_t>(region.height), 
                static_cast<ssize_t>(region.width)};
                
    } else if (layout_str == "HWC") {
        // Height, Width, Channels - requires depth=1
        if (region.depth != 1) {
            throw py::value_error("Layout 'HWC' requires depth=1, but depth=" + 
                                std::to_string(region.depth));
        }
        internal_layout = ImageLayoutSpec::DHWC;
        shape = {static_cast<ssize_t>(region.height), 
                static_cast<ssize_t>(region.width),
                static_cast<ssize_t>(region.num_channels)};
                
    } else if (layout_str == "DHW") {
        // Depth, Height, Width - requires channels=1
        if (region.num_channels != 1) {
            throw py::value_error("Layout 'DHW' requires channels=1, but channels=" + 
                                std::to_string(region.num_channels));
        }
        internal_layout = ImageLayoutSpec::DHWC;
        shape = {static_cast<ssize_t>(region.depth),
                static_cast<ssize_t>(region.height), 
                static_cast<ssize_t>(region.width)};
                
    } else if (layout_str == "CHW") {
        // Channels, Height, Width - requires depth=1
        if (region.depth != 1) {
            throw py::value_error("Layout 'CHW' requires depth=1, but depth=" + 
                                std::to_string(region.depth));
        }
        internal_layout = ImageLayoutSpec::CDHW;
        shape = {static_cast<ssize_t>(region.num_channels),
                static_cast<ssize_t>(region.height), 
                static_cast<ssize_t>(region.width)};
                
    } else if (layout_str == "DHWC") {
        internal_layout = ImageLayoutSpec::DHWC;
        shape = {static_cast<ssize_t>(region.depth), 
                static_cast<ssize_t>(region.height), 
                static_cast<ssize_t>(region.width), 
                static_cast<ssize_t>(region.num_channels)};
                
    } else if (layout_str == "DCHW") {
        internal_layout = ImageLayoutSpec::DCHW;
        shape = {static_cast<ssize_t>(region.depth), 
                static_cast<ssize_t>(region.num_channels), 
                static_cast<ssize_t>(region.height), 
                static_cast<ssize_t>(region.width)};
                
    } else if (layout_str == "CDHW") {
        internal_layout = ImageLayoutSpec::CDHW;
        shape = {static_cast<ssize_t>(region.num_channels), 
                static_cast<ssize_t>(region.depth), 
                static_cast<ssize_t>(region.height), 
                static_cast<ssize_t>(region.width)};
                
    } else {
        throw py::value_error("Invalid layout string: '" + layout_str + 
                            "'. Must be one of: 'HW', 'HWC', 'DHW', 'CHW', 'DHWC', 'DCHW', 'CDHW'");
    }
    
    // Determine where to write: either a user-provided buffer or a new ImageBuffer
    std::span<uint8_t> target_span;
    
    if (dst.is_none()) {
        // Allocate new buffer and write to it
        ImageBuffer output_buffer = allocate_buffer_by_dtype(shape);
        target_span = output_buffer.as_span();
        
        // Read the data
        {
            py::gil_scoped_release release;  // Release GIL for I/O
            auto read_result = file_handle_->read_region(
                page_index_, region, target_span, internal_layout, use_threading);
            if (read_result.is_error()) {
                translate_error(read_result.error());
            }
        }
        
        // Return the new ImageBuffer
        return py::cast(std::move(output_buffer));
    } else {
        // Validate provided buffer
        auto val_result = validate_dst_buffer(dst, shape);
        if (val_result.is_error()) {
            translate_error(val_result.error());
        }
        
        // Get buffer from provided object and write directly to it
        py::buffer dst_buffer = dst.cast<py::buffer>();
        py::buffer_info dst_buf_info = dst_buffer.request(true);
        target_span = std::span<uint8_t>(
            static_cast<uint8_t*>(dst_buf_info.ptr),
            dst_buf_info.size * dst_buf_info.itemsize
        );
        
        // Read the data directly into dst
        {
            py::gil_scoped_release release;  // Release GIL for I/O
            auto read_result = file_handle_->read_region(
                page_index_, region, target_span, internal_layout, use_threading);
            if (read_result.is_error()) {
                translate_error(read_result.error());
            }
        }
        
        // Return dst (OpenCV style)
        return dst;
    }
}

// Allocate a new ImageBuffer based on dtype
ImageBuffer PyTiffPage::allocate_buffer_by_dtype(const std::vector<ssize_t>& shape) {
    uint16_t bps = bits_per_sample();
    SampleFormat fmt = sample_format();
    
    auto buffer_format = BufferFormat::from_sample_info(bps, fmt);
    
    return ImageBuffer(shape, buffer_format.itemsize, 
                      buffer_format.format, buffer_format.format_str);
}

// Validate that destination buffer has correct shape and dtype
Result<void> PyTiffPage::validate_dst_buffer(py::object dst, const std::vector<ssize_t>& expected_shape) {
    // Try to get buffer info
    py::buffer dst_buffer;
    try {
        dst_buffer = dst.cast<py::buffer>();
    } catch (const py::cast_error&) {
        return Err(Error::Code::InvalidFormat, "Destination must implement buffer protocol");
    }
    
    py::buffer_info buf;
    try {
        buf = dst_buffer.request(true);  // Request writable buffer
    } catch (const std::exception& e) {
        return Err(Error::Code::InvalidFormat, 
            std::string("Failed to access destination buffer: ") + e.what());
    }
    
    // Check if C-contiguous
    // Calculate expected strides for C-contiguous layout
    bool is_c_contiguous = true;
    if (!expected_shape.empty()) {
        ssize_t expected_stride = buf.itemsize;
        for (ssize_t i = static_cast<ssize_t>(expected_shape.size()) - 1; i >= 0; --i) {
            if (buf.strides[i] != expected_stride) {
                is_c_contiguous = false;
                break;
            }
            expected_stride *= expected_shape[i];
        }
    }
    
    if (!is_c_contiguous) {
        return Err(Error::Code::InvalidFormat, "Destination buffer must be C-contiguous");
    }
    
    // Check shape
    if (buf.ndim != static_cast<ssize_t>(expected_shape.size())) {
        return Err(Error::Code::InvalidFormat, 
            "Destination buffer has wrong number of dimensions");
    }
    
    for (ssize_t i = 0; i < buf.ndim; ++i) {
        if (buf.shape[i] != expected_shape[i]) {
            return Err(Error::Code::InvalidFormat, 
                "Destination buffer has wrong shape");
        }
    }
    
    // Check dtype by comparing format string and itemsize
    uint16_t bps = bits_per_sample();
    SampleFormat fmt = sample_format();
    auto expected_format = BufferFormat::from_sample_info(bps, fmt);
    
    if (buf.itemsize != static_cast<ssize_t>(expected_format.itemsize)) {
        return Err(Error::Code::InvalidFormat, 
            "Destination buffer has wrong itemsize");
    }
    
    // Check format string (simplified check)
    if (buf.format != expected_format.format) {
        return Err(Error::Code::InvalidFormat, 
            "Destination buffer has wrong dtype");
    }
    
    return Ok();
}

void bind_tiff_page(py::module_& m) {
    py::class_<PyTiffPage>(m, "TiffPage", 
        "Represents a single page in a TIFF file.\n\n"
        "Provides access to page metadata and image data. Image data is loaded\n"
        "lazily on first access and cached for subsequent reads.")
        .def_property_readonly("width", &PyTiffPage::width,
                              "Image width in pixels")
        .def_property_readonly("height", &PyTiffPage::height,
                              "Image height in pixels")
        .def_property_readonly("depth", &PyTiffPage::depth,
                              "Image depth (Z slices). 1 for 2D images")
        .def_property_readonly("channels", &PyTiffPage::channels,
                              "Number of channels (samples per pixel)")
        .def_property_readonly("bits_per_sample", &PyTiffPage::bits_per_sample,
                              "Number of bits per sample (8, 16, 32, 64)")
        .def_property_readonly("dtype", &PyTiffPage::dtype,
                              "NumPy dtype string (e.g., 'uint8', 'float32')")
        .def_property_readonly("storage_layout", &PyTiffPage::storage_layout,
                              "Storage layout in file: 'DHWC' (chunky/interleaved) or 'CDHW' (planar/separate channels)")
        .def_property_readonly("compression", &PyTiffPage::compression,
                              "Compression scheme used")
        .def_property_readonly("photometric", &PyTiffPage::photometric,
                              "Photometric interpretation")
        .def("shape", &PyTiffPage::shape,
             py::arg("layout") = "DHWC",
             py::return_value_policy::copy,
             "Get the shape as a tuple.\\n\\n"
             "Args:\\n"
             "    layout: Memory layout\\n"
             "            - 'HW': Height, Width (requires depth=1, channels=1)\\n"
             "            - 'HWC': Height, Width, Channels (requires depth=1)\\n"
             "            - 'DHW': Depth, Height, Width (requires channels=1)\\n"
             "            - 'CHW': Channels, Height, Width (requires depth=1)\\n"
             "            - 'DHWC': Depth, Height, Width, Channels (default)\\n"
             "            - 'DCHW': Depth, Channels, Height, Width\\n"
             "            - 'CDHW': Channels, Depth, Height, Width\\n\\n"
             "Returns:\\n"
             "    Tuple of dimensions in the specified layout\\n\\n"
             "Raises:\\n"
             "    ValueError: If the image dimensions don't conform to the requested layout")
        .def("read", &PyTiffPage::read,
             py::kw_only(),
             py::arg("region") = py::none(),
             py::arg("dst") = py::none(),
             py::arg("layout") = "DHWC",
             py::arg("use_threading") = true,
             py::return_value_policy::move,
             "Read the page (or a region) as an ImageBuffer.\n\n"
             "Args:\n"
             "    region: Optional ImageRegion to read a subset of the page.\n"
             "            If None, reads the entire page.\n"
             "    dst: Optional buffer object (for instance a numpy array) to store the result.\n"
             "         Must be C-contiguous with correct shape and dtype.\n"
             "         If provided, data is read directly into dst and dst is returned.\n"
             "         If None, a new ImageBuffer is allocated and returned.\n"
             "         The ImageBuffer can be converted to a numpy array np.asarray().\n"
             "    layout: Memory layout for the output array\n"
             "            - 'HW': Height, Width (requires depth=1, channels=1)\n"
             "            - 'HWC': Height, Width, Channels (requires depth=1)\n"
             "            - 'DHW': Depth, Height, Width (requires channels=1)\n"
             "            - 'CHW': Channels, Height, Width (requires depth=1)\n"
             "            - 'DHWC': Depth, Height, Width, Channels (default)\n"
             "            - 'DCHW': Depth, Channels, Height, Width\n"
             "            - 'CDHW': Channels, Depth, Height, Width\n"
             "    use_threading: If True, use multi-threaded reader for better performance.\n"
             "                   Ignored for very small images.\n\n"
             "Returns:\n"
             "    If dst is None: ImageBuffer containing the image data.\n"
             "    If dst is provided: dst (the same object passed in, now filled with data).\n\n"
             "Examples:\n"
             "    >>> # Allocate new buffer\n"
             "    >>> buffer = page.read()\n"
             "    >>> import numpy as np\n"
             "    >>> array = np.asarray(buffer)  # Zero-copy view\n\n"
             "    >>> # Read 2D grayscale image\n"
             "    >>> buffer = page.read(layout='HW')  # shape will be (H, W)\n\n"
             "    >>> # Read 2D RGB image\n"
             "    >>> buffer = page.read(layout='HWC')  # shape will be (H, W, C)\n\n"
             "    >>> # Read into existing array (OpenCV style)\n"
             "    >>> dst = np.empty(page.shape(), dtype=page.dtype)\n"
             "    >>> result = page.read(dst=dst)  # result is dst\n"
             "    >>> assert result is dst\n\n"
             "Note:\n"
             "    - Data is read fresh each time (no caching)\n"
             "    - The GIL is released during I/O operations for better parallelism.\n"
             "    - Using pre-allocated dst avoids allocations and returns dst directly.\n"
             "    - Layout constraints are enforced: ValueError is raised if the data\n"
             "      cannot conform to the requested layout (e.g., 'HW' with channels > 1).")
        .def("__repr__", [](const PyTiffPage& page) {
            return "<TiffPage " + std::to_string(page.width()) + "x" + 
                   std::to_string(page.height()) + "x" + std::to_string(page.depth()) +
                   " channels=" + std::to_string(page.channels()) +
                   " dtype=" + page.dtype() + ">";
        });
}
