#include <pybind11/pybind11.h>
#include <pybind11/typing.h>

namespace py = pybind11;

// Forward declarations of binding functions
void bind_exceptions(py::module_& m);
void bind_enums(py::module_& m);
void bind_types(py::module_& m);
void bind_image_buffer(py::module_& m);
void bind_tiff_page(py::module_& m);
void bind_tiff_file(py::module_& m);

PYBIND11_MODULE(_tiffconcept_core, m,
                py::mod_gil_not_used()) {  // Enable free-threaded mode
    m.doc() = "Core C++ extension for tiffconcept - high-performance TIFF I/O library";

    // Version information with type annotation
    m.attr("__version__") = py::str("0.1.0");

    // Bind exceptions first (needed by other bindings)
    bind_exceptions(m);

    // Bind enums
    bind_enums(m);

    // Bind types
    bind_types(m);

    // Bind buffer class (before other classes that use it)
    bind_image_buffer(m);

    // Bind classes
    bind_tiff_page(m);
    bind_tiff_file(m);
}
