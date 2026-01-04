#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/typing.h>

#include "../core/file_handle.hpp"
#include "tiff_file.hpp"
#include "tiff_page.hpp"
#include "exceptions.hpp"

namespace py = pybind11;
using namespace tiffconcept;
using namespace tiffconcept::python;

/**
 * @brief Python wrapper for a TIFF file with multiple pages
 * 
 * Provides sequence-like access to pages and context manager support.
 */
PyTiffFile::PyTiffFile(const std::string& filepath) 
    : file_handle_(std::make_shared<FileHandle>()) {
    auto result = file_handle_->open(filepath);
    if (result.is_error()) {
        translate_error(result.error());
    }
}

PyTiffFile::~PyTiffFile() {
    // We don't close the file as it
    // may be in use by the pages
}

// Context manager support
PyTiffFile& PyTiffFile::enter() {
    return *this;
}

void PyTiffFile::exit(py::object exc_type, py::object exc_value, py::object traceback) {
    close();
}

void PyTiffFile::close() {
    if (file_handle_ && file_handle_->is_open()) {
        file_handle_->close();
    }
}

// Sequence protocol
size_t PyTiffFile::len() const {
    return const_cast<FileHandle*>(file_handle_.get())->num_pages();
}

PyTiffPage PyTiffFile::getitem(ssize_t index) {
    size_t num_pages = len();
    
    // Handle negative indices
    if (index < 0) {
        index += num_pages;
    }
    
    // Check bounds
    if (index < 0 || static_cast<size_t>(index) >= num_pages) {
        throw py::index_error("Page index out of range");
    }
    
    return PyTiffPage(file_handle_, static_cast<size_t>(index));
}

// Iterator implementation
PyTiffFile::Iterator::Iterator(std::shared_ptr<FileHandle> file_handle, size_t index)
    : file_handle_(file_handle), index_(index) {}

PyTiffPage PyTiffFile::Iterator::next() {
    size_t num_pages = file_handle_->num_pages();
    if (index_ >= num_pages) {
        throw py::stop_iteration();
    }
    return PyTiffPage(file_handle_, index_++);
}

PyTiffFile::Iterator PyTiffFile::iter() {
    return Iterator(file_handle_, 0);
}

// Property accessors
bool PyTiffFile::is_open() const {
    return file_handle_ && file_handle_->is_open();
}

TiffFormatType PyTiffFile::format() const {
    if (!is_open()) {
        throw std::runtime_error("File is not open");
    }
    return file_handle_->get_format();
}

std::string PyTiffFile::endian() const {
    if (!is_open()) {
        throw std::runtime_error("File is not open");
    }
    return file_handle_->get_endian() == std::endian::little ? "little" : "big";
}

// Convenience method to get all pages
py::typing::List<PyTiffPage> PyTiffFile::pages() {
    size_t num_pages = len();
    py::list result;
    for (size_t i = 0; i < num_pages; ++i) {
        result.append(PyTiffPage(file_handle_, i));
    }
    return result;
}

void bind_tiff_file(py::module_& m) {
    // Iterator class
    py::class_<PyTiffFile::Iterator>(m, "TiffFileIterator", 
        "Iterator for TiffFile pages")
        .def("__iter__", [](PyTiffFile::Iterator& it) -> PyTiffFile::Iterator& { return it; })
        .def("__next__", &PyTiffFile::Iterator::next,
             py::return_value_policy::move,
             "Get the next page");

    // TiffFile class
    py::class_<PyTiffFile>(m, "TiffFile", 
        "Represents a multi-page TIFF file.\n\n"
        "Provides sequence-like access to pages and supports context manager protocol.\n"
        "Pages are lazily loaded - metadata is read on access, image data on read().\n\n"
        "Examples:\n"
        "    >>> with TiffFile('image.tif') as tif:\n"
        "    ...     print(f'Pages: {len(tif)}')\n"
        "    ...     data = tif[0].read()\n\n"
        "    >>> tif = TiffFile('image.tif')\n"
        "    >>> for page in tif:\n"
        "    ...     print(page.shape())\n"
        "    >>> tif.close()")
        .def(py::init<const std::string&>(),
             py::arg("filepath"),
             "Open a TIFF file.\n\n"
             "Args:\n"
             "    filepath: Path to the TIFF file to open\n\n"
             "Raises:\n"
             "    FileNotFoundError: If the file does not exist\n"
             "    InvalidFormatError: If the file is not a valid TIFF file\n"
             "    ReadError: If there is an error reading the file")
        .def("__enter__", &PyTiffFile::enter,
             "Enter context manager")
        .def("__exit__", &PyTiffFile::exit,
             "Exit context manager")
        .def("close", &PyTiffFile::close,
             "Close the file.\n\n"
             "It is safe to call this multiple times. After closing, the file\n"
             "cannot be accessed until reopened.")
        .def("__len__", &PyTiffFile::len,
             "Get the number of pages in the file.\n\n"
             "Returns:\n"
             "    Number of pages")
        .def("__getitem__", &PyTiffFile::getitem,
             py::arg("index"),
             py::return_value_policy::move,
              "Get a page by index.\n\n"
             "Supports negative indices (e.g., tif[-1] for last page).\n\n"
             "Args:\n"
             "    index: Page index (zero-based)\n\n"
             "Returns:\n"
             "    TiffPage object\n\n"
             "Raises:\n"
             "    IndexError: If index is out of range")
        .def("__iter__", &PyTiffFile::iter,
             "Iterate over all pages.\n\n"
             "Returns:\n"
             "    Iterator yielding TiffPage objects")
        .def_property_readonly("is_open", &PyTiffFile::is_open,
                              "Check if the file is open")
        .def_property_readonly("format", &PyTiffFile::format,
                              "TIFF format type (Classic or BigTIFF)")
        .def_property_readonly("endian", &PyTiffFile::endian,
                              "Byte order ('little' or 'big')")
        .def("pages", &PyTiffFile::pages,
            py::return_value_policy::move,
            "Get all pages as a list.\n\n"
             "Returns:\n"
             "    List of TiffPage objects\n\n"
             "Note:\n"
             "    This creates TiffPage objects for all pages but does not\n"
             "    load image data until read() is called on each page.")
        .def("__repr__", [](const PyTiffFile& file) {
            if (!file.is_open()) {
                return std::string("<TiffFile (closed)>");
            }
            size_t num = const_cast<PyTiffFile&>(file).len();
            return "<TiffFile with " + std::to_string(num) + " page" + 
                   (num == 1 ? "" : "s") + ">";
        });
}
