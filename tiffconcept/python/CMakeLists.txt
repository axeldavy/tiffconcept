cmake_minimum_required(VERSION 3.15)
project(tiffconcept_python)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Define paths to parent tiffconcept directory
set(TIFFCONCEPT_ROOT ${CMAKE_SOURCE_DIR}/..)
set(TIFFCONCEPT_INCLUDE_DIR ${TIFFCONCEPT_ROOT}/include)
set(TIFFCONCEPT_EXTERNAL_DIR ${TIFFCONCEPT_ROOT}/external)

# Find Python and pybind11
find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG PATHS ${TIFFCONCEPT_EXTERNAL_DIR}/pybind11 REQUIRED)

# ==============================================================================
# ZSTD Library
# ==============================================================================
add_library(zstd_static STATIC
    ${TIFFCONCEPT_EXTERNAL_DIR}/zstd/src/zstd.c
)
target_include_directories(zstd_static PUBLIC
    ${TIFFCONCEPT_EXTERNAL_DIR}/zstd/include
)
set_target_properties(zstd_static PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

# ==============================================================================
# CharLS Library (JPEG-LS)
# ==============================================================================
find_package(charls QUIET)

if(charls_FOUND)
    message(STATUS "Using system CharLS library")
    set(CHARLS_LIBRARY charls::charls)
else()
    message(STATUS "Using bundled CharLS library")
    set(CHARLS_BUILD_TESTS OFF CACHE BOOL "Build CharLS tests")
    set(CHARLS_BUILD_SAMPLES OFF CACHE BOOL "Build CharLS samples")
    set(CHARLS_BUILD_FUZZ_TEST OFF CACHE BOOL "Build CharLS fuzz tests")
    set(CHARLS_INSTALL OFF CACHE BOOL "Install CharLS")
    
    add_subdirectory(${TIFFCONCEPT_EXTERNAL_DIR}/charls 
                     ${CMAKE_BINARY_DIR}/charls EXCLUDE_FROM_ALL)
    set(CHARLS_LIBRARY charls)
endif()

# ==============================================================================
# Python Module
# ==============================================================================

# Collect source files
set(PYTHON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/file_handle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bindings/enums.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bindings/exceptions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bindings/image_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bindings/module.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bindings/tiff_page.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bindings/tiff_file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bindings/types.cpp
)

# Create the Python module
pybind11_add_module(_tiffconcept_core MODULE ${PYTHON_SOURCES})

# Include directories
target_include_directories(_tiffconcept_core PRIVATE
    ${TIFFCONCEPT_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(_tiffconcept_core PRIVATE
    zstd_static
    ${CHARLS_LIBRARY}
)

# Platform-specific definitions
if(UNIX AND NOT APPLE)
    target_compile_definitions(_tiffconcept_core PRIVATE __unix__)
    # Check for io_uring support (Linux 5.1+)
    include(CheckIncludeFileCXX)
    check_include_file_cxx("liburing.h" HAVE_LIBURING)
    if(HAVE_LIBURING)
        target_compile_definitions(_tiffconcept_core PRIVATE HAVE_LIBURING)
        target_link_libraries(_tiffconcept_core PRIVATE uring)
        message(STATUS "io_uring support enabled")
    else()
        message(STATUS "io_uring not available, using pread")
    endif()
elseif(WIN32)
    target_compile_definitions(_tiffconcept_core PRIVATE _WIN32)
elseif(APPLE)
    target_compile_definitions(_tiffconcept_core PRIVATE __APPLE__)
endif()

# Compiler warnings and optimizations
if(MSVC)
    target_compile_options(_tiffconcept_core PRIVATE 
        /W4 /O2
        /wd4127  # Conditional expression is constant (common in templates)
    )
else()
    target_compile_options(_tiffconcept_core PRIVATE
        -Wall -Wextra -O3 -march=native
        -Wno-unused-parameter  # pybind11 generates some unused params
    )
endif()

# Support for free-threaded Python (3.13+)
if(Python_VERSION VERSION_GREATER_EQUAL "3.13")
    # Check if this is free-threaded Python
    execute_process(
        COMMAND ${Python_EXECUTABLE} -c "import sys; print('t' if hasattr(sys, '_is_gil_enabled') and not sys._is_gil_enabled() else '')"
        OUTPUT_VARIABLE PYTHON_GIL_DISABLED
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(PYTHON_GIL_DISABLED STREQUAL "t")
        message(STATUS "Building for free-threaded Python ${Python_VERSION}")
        target_compile_definitions(_tiffconcept_core PRIVATE Py_GIL_DISABLED)
    endif()
endif()

# Install the module
install(TARGETS _tiffconcept_core
    LIBRARY DESTINATION tiffconcept
    RUNTIME DESTINATION tiffconcept
)

# Install Python package files
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/tiffconcept/__init__.py
    DESTINATION tiffconcept
)

# Install type stubs if they exist
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tiffconcept/__init__.pyi)
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/tiffconcept/__init__.pyi
        DESTINATION tiffconcept
    )
endif()
