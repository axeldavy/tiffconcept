#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/typing.h>

#include "../core/file_handle.hpp"
#include "tiff_page.hpp"

namespace py = pybind11;
using namespace tiffconcept;
using namespace tiffconcept::python;

/**
 * @brief Python wrapper for a TIFF file with multiple pages
 * 
 * Provides sequence-like access to pages and context manager support.
 */
class PyTiffFile {
public:
    PyTiffFile(const std::string& filepath);
    ~PyTiffFile();

    // Context manager support
    PyTiffFile& enter();
    void exit(py::object exc_type, py::object exc_value, py::object traceback);
    void close();

    // Sequence protocol
    size_t len() const;
    PyTiffPage getitem(ssize_t index);

    // Iterator support
    class Iterator {
    public:
        Iterator(std::shared_ptr<FileHandle> file_handle, size_t index);
        PyTiffPage next();

    private:
        std::shared_ptr<FileHandle> file_handle_;
        size_t index_;
    };

    Iterator iter();

    // Property accessors
    bool is_open() const;
    tiffconcept::TiffFormatType format() const;
    std::string endian() const;

    // Convenience method to get all pages with proper type hints
    py::typing::List<PyTiffPage> pages();

private:
    std::shared_ptr<FileHandle> file_handle_;
};

void bind_tiff_file(py::module_& m);
