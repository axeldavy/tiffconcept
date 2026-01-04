#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/typing.h>

#include "../core/file_handle.hpp"
#include "tiffconcept/types/tiff_spec.hpp"
#include "image_buffer.hpp"

namespace py = pybind11;
using namespace tiffconcept;
using namespace tiffconcept::python;

// Python wrapper for a TIFF page with lazy loading
class PyTiffPage {
public:
    PyTiffPage(std::shared_ptr<FileHandle> file_handle, size_t page_index);
    
    // Properties
    uint32_t width() const;
    uint32_t height() const;
    uint32_t depth() const;
    uint16_t channels() const;
    uint16_t bits_per_sample() const;
    SampleFormat sample_format() const;
    
    // Get numpy dtype string
    std::string dtype() const;
    
    // Get storage layout ("DHWC" for chunky, "CDHW" for planar)
    std::string storage_layout() const;
    
    // Get compression scheme
    tiffconcept::CompressionScheme compression() const;
    
    // Get photometric interpretation
    tiffconcept::PhotometricInterpretation photometric() const;
    
    // Get shape as tuple
    py::tuple shape(const std::string& layout = "DHWC") const;
    
    // Read the page (or a region) - returns ImageBuffer or dst if provided
    py::object read(std::optional<ImageRegion> region = std::nullopt,
                    py::object dst = py::none(),
                    const std::string& layout_str = "DHWC",
                    bool use_threading = true);

private:
    std::shared_ptr<FileHandle> file_handle_;
    size_t page_index_;
    tiffconcept::python::PageInfo page_info_;
    
    // Helper methods
    ImageBuffer allocate_buffer_by_dtype(const std::vector<ssize_t>& shape);
    Result<void> validate_dst_buffer(py::object dst, const std::vector<ssize_t>& expected_shape);
};

void bind_tiff_page(py::module_& m);
