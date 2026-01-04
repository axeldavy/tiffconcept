#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/typing.h>

#include "tiffconcept/image_shape.hpp"
#include "tiffconcept/types/tiff_spec.hpp"

namespace py = pybind11;
using namespace tiffconcept;

void bind_types(py::module_& m) {
    // ImageRegion struct
    py::class_<ImageRegion>(m, "ImageRegion", 
        "Specifies a rectangular region within a TIFF image.\n\n"
        "All coordinates are zero-based. The region is specified as [start, start+size)\n"
        "for each dimension.")
        .def(py::init<uint16_t, uint32_t, uint32_t, uint32_t, uint16_t, uint32_t, uint32_t, uint32_t>(),
             py::arg("start_channel") = 0,
             py::arg("start_z") = 0,
             py::arg("start_y") = 0,
             py::arg("start_x") = 0,
             py::arg("num_channels") = 1,
             py::arg("depth") = 1,
             py::arg("height") = 0,
             py::arg("width") = 0,
             "Create an image region.\n\n"
             "Args:\n"
             "    start_channel: Starting channel index\n"
             "    start_z: Starting Z (depth) coordinate\n"
             "    start_y: Starting Y (row) coordinate\n"
             "    start_x: Starting X (column) coordinate\n"
             "    num_channels: Number of channels to read\n"
             "    depth: Number of Z slices to read\n"
             "    height: Number of rows to read\n"
             "    width: Number of columns to read")
        .def_readwrite("start_channel", &ImageRegion::start_channel,
                      "Starting channel index")
        .def_readwrite("start_z", &ImageRegion::start_z,
                      "Starting Z (depth) coordinate")
        .def_readwrite("start_y", &ImageRegion::start_y,
                      "Starting Y (row) coordinate")
        .def_readwrite("start_x", &ImageRegion::start_x,
                      "Starting X (column) coordinate")
        .def_readwrite("num_channels", &ImageRegion::num_channels,
                      "Number of channels")
        .def_readwrite("depth", &ImageRegion::depth,
                      "Depth (number of Z slices)")
        .def_readwrite("height", &ImageRegion::height,
                      "Height (number of rows)")
        .def_readwrite("width", &ImageRegion::width,
                      "Width (number of columns)")
        .def("end_x", &ImageRegion::end_x,
             "Get the exclusive end X coordinate.\n\nReturns:\n    End X coordinate")
        .def("end_y", &ImageRegion::end_y,
             "Get the exclusive end Y coordinate.\n\nReturns:\n    End Y coordinate")
        .def("end_z", &ImageRegion::end_z,
             "Get the exclusive end Z coordinate.\n\nReturns:\n    End Z coordinate")
        .def("end_channel", &ImageRegion::end_channel,
             "Get the exclusive end channel index.\n\nReturns:\n    End channel index")
        .def("is_empty", &ImageRegion::is_empty,
             "Check if the region is empty.\n\nReturns:\n    True if any dimension is zero")
        .def("num_pixels", &ImageRegion::num_pixels,
             "Get the total number of pixels in the region.\n\nReturns:\n    width * height * depth")
        .def("num_samples", &ImageRegion::num_samples,
             "Get the total number of samples in the region.\n\n"
             "Returns:\n    width * height * depth * num_channels")
        .def("__repr__", [](const ImageRegion& r) {
            return "<ImageRegion x=" + std::to_string(r.start_x) + ":" + std::to_string(r.end_x()) +
                   " y=" + std::to_string(r.start_y) + ":" + std::to_string(r.end_y()) +
                   " z=" + std::to_string(r.start_z) + ":" + std::to_string(r.end_z()) +
                   " c=" + std::to_string(r.start_channel) + ":" + std::to_string(r.end_channel()) + ">";
        });

    // ImageShape class
    py::class_<ImageShape>(m, "ImageShape", 
        "Describes the shape and format of a TIFF image.\n\n"
        "Contains metadata extracted from TIFF tags including dimensions, pixel format,\n"
        "and data organization.")
        .def(py::init<>(),
             "Create an empty ImageShape")
        .def_property_readonly("width", &ImageShape::image_width,
                              "Image width in pixels")
        .def_property_readonly("height", &ImageShape::image_height,
                              "Image height in pixels")
        .def_property_readonly("depth", &ImageShape::image_depth,
                              "Image depth (Z slices). 1 for 2D images")
        .def_property_readonly("bits_per_sample", &ImageShape::bits_per_sample,
                              "Number of bits per sample (8, 16, 32, 64)")
        .def_property_readonly("samples_per_pixel", &ImageShape::samples_per_pixel,
                              "Number of samples (channels) per pixel")
        .def_property_readonly("sample_format", &ImageShape::sample_format,
                              "Sample data format (UnsignedInt, SignedInt, IEEEFloat)")
        .def_property_readonly("planar_configuration", &ImageShape::planar_configuration,
                              "Planar configuration (Chunky or Planar)")
        .def("is_3d", &ImageShape::is_3d,
             "Check if the image is 3D.\n\nReturns:\n    True if depth > 1")
        .def("is_multi_channel", &ImageShape::is_multi_channel,
             "Check if the image has multiple channels.\n\n"
             "Returns:\n    True if samples_per_pixel > 1")
        .def("is_planar", &ImageShape::is_planar,
             "Check if the image uses planar configuration.\n\n"
             "Returns:\n    True if planar_configuration is Planar")
        .def("num_pixels", &ImageShape::num_pixels,
             "Get the total number of pixels.\n\nReturns:\n    width * height * depth")
        .def("num_elements", &ImageShape::num_elements,
             "Get the total number of elements.\n\n"
             "Returns:\n    width * height * depth * samples_per_pixel")
        .def("full_region", &ImageShape::full_region,
             "Get a region covering the entire image.\n\n"
             "Returns:\n    ImageRegion covering the full image")
        .def("__repr__", [](const ImageShape& s) {
            return "<ImageShape " + std::to_string(s.image_width()) + "x" + 
                   std::to_string(s.image_height()) + "x" + std::to_string(s.image_depth()) +
                   " channels=" + std::to_string(s.samples_per_pixel()) +
                   " bps=" + std::to_string(s.bits_per_sample()) + ">";
        });
}
