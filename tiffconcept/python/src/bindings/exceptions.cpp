
#include <pybind11/pybind11.h>

#include "exceptions.hpp"

// Exception implementations
TiffError::TiffError(const std::string& msg) : std::runtime_error(msg) {}
FileNotFoundError::FileNotFoundError(const std::string& msg) : TiffError(msg) {}
ReadError::ReadError(const std::string& msg) : TiffError(msg) {}
WriteError::WriteError(const std::string& msg) : TiffError(msg) {}
InvalidFormatError::InvalidFormatError(const std::string& msg) : TiffError(msg) {}
UnsupportedFeatureError::UnsupportedFeatureError(const std::string& msg) : TiffError(msg) {}
OutOfBoundsError::OutOfBoundsError(const std::string& msg) : TiffError(msg) {}

// Translate Result<T> errors to Python exceptions
void translate_error(const Error& error) {
    switch (error.code) {
        case Error::Code::Success:
            // No error
            break;
        case Error::Code::FileNotFound:
            throw FileNotFoundError(error.message);
        case Error::Code::ReadError:
        case Error::Code::UnexpectedEndOfFile:
            throw ReadError(error.message);
        case Error::Code::WriteError:
            throw WriteError(error.message);
        case Error::Code::InvalidHeader:
        case Error::Code::InvalidFormat:
        case Error::Code::InvalidTag:
        case Error::Code::InvalidTagType:
            throw InvalidFormatError(error.message);
        case Error::Code::UnsupportedFeature:
            throw UnsupportedFeatureError(error.message);
        case Error::Code::OutOfBounds:
        case Error::Code::InvalidPageIndex:
            throw OutOfBoundsError(error.message);
        case Error::Code::InvalidOperation:
        case Error::Code::MemoryError:
        case Error::Code::CompressionError:
        case Error::Code::Unknown:
        default:
            throw TiffError(error.message);
    }
}

void bind_exceptions(py::module_& m) {
    // Register exception classes
    py::register_exception<TiffError>(m, "TiffError", PyExc_RuntimeError);
    py::register_exception<FileNotFoundError>(m, "FileNotFoundError", PyExc_FileNotFoundError);
    py::register_exception<ReadError>(m, "ReadError", PyExc_IOError);
    py::register_exception<WriteError>(m, "WriteError", PyExc_IOError);
    py::register_exception<InvalidFormatError>(m, "InvalidFormatError", PyExc_ValueError);
    py::register_exception<UnsupportedFeatureError>(m, "UnsupportedFeatureError", PyExc_NotImplementedError);
    py::register_exception<OutOfBoundsError>(m, "OutOfBoundsError", PyExc_IndexError);
}
