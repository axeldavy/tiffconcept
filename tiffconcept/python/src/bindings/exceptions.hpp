#pragma once

#include <pybind11/pybind11.h>

#include "tiffconcept/types/result.hpp"

namespace py = pybind11;
using namespace tiffconcept;

// Custom exception hierarchy for tiffconcept
class TiffError : public std::runtime_error {
public:
    explicit TiffError(const std::string& msg);
};

class FileNotFoundError : public TiffError {
public:
    explicit FileNotFoundError(const std::string& msg);
};

class ReadError : public TiffError {
public:
    explicit ReadError(const std::string& msg);
};

class WriteError : public TiffError {
public:
    explicit WriteError(const std::string& msg);
};

class InvalidFormatError : public TiffError {
public:
    explicit InvalidFormatError(const std::string& msg);
};

class UnsupportedFeatureError : public TiffError {
public:
    explicit UnsupportedFeatureError(const std::string& msg);
};

class OutOfBoundsError : public TiffError {
public:
    explicit OutOfBoundsError(const std::string& msg);
};

// Translate Result<T> errors to Python exceptions
void translate_error(const Error& error);

// Bind exception classes to Python module
void bind_exceptions(py::module_& m);
