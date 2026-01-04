cmake_minimum_required(VERSION 3.14)
project(tiffconcept_benchmarks)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable optimization
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Include FetchContent module for downloading Google Benchmark
include(FetchContent)

# Fetch Google Benchmark
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# Include directories
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/../tiffconcept/include)

# ==============================================================================
# ZSTD Library
# ==============================================================================
add_library(zstd_static STATIC
    ${CMAKE_SOURCE_DIR}/../tiffconcept/external/zstd/src/zstd.c
)
target_include_directories(zstd_static PRIVATE
    ${CMAKE_SOURCE_DIR}/../tiffconcept/external/zstd/include
)
target_include_directories(zstd_static PUBLIC
    ${CMAKE_SOURCE_DIR}/../tiffconcept/external/zstd/include
)
set_target_properties(zstd_static PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)


# ==============================================================================
# CharLS Library (JPEG-LS)
# ==============================================================================
# Try to find system CharLS first
find_package(charls QUIET)

if(charls_FOUND)
    message(STATUS "Using system CharLS library")
    set(CHARLS_LIBRARY charls::charls)
else()
    message(STATUS "Using bundled CharLS library")
    # Build CharLS from source
    set(CHARLS_BUILD_TESTS OFF CACHE BOOL "Build CharLS tests")
    set(CHARLS_BUILD_SAMPLES OFF CACHE BOOL "Build CharLS samples")
    set(CHARLS_BUILD_FUZZ_TEST OFF CACHE BOOL "Build CharLS fuzz tests")
    set(CHARLS_INSTALL OFF CACHE BOOL "Install CharLS")
    
    add_subdirectory(${CMAKE_SOURCE_DIR}/../tiffconcept/external/charls ${CMAKE_BINARY_DIR}/charls EXCLUDE_FROM_ALL)
    set(CHARLS_LIBRARY charls)
endif()

# ==============================================================================
# Optional Dependencies
# ==============================================================================

# Try to find libtiff (optional for comparison benchmarks)
find_package(TIFF)
if(TIFF_FOUND)
    message(STATUS "Found libtiff - comparison benchmarks enabled")
else()
    message(STATUS "libtiff not found - comparison benchmarks will be skipped")
endif()

# Find liburing for async I/O support (Linux only)
set(HAVE_LIBURING OFF)
if(UNIX AND NOT APPLE)
    find_library(LIBURING_LIBRARY uring)
    if(LIBURING_LIBRARY)
        message(STATUS "Found liburing: ${LIBURING_LIBRARY}")
        set(HAVE_LIBURING ON)
    else()
        message(WARNING "liburing not found. FastReader benchmarks will be disabled on Linux.")
    endif()
endif()

# ==============================================================================
# Executable: tiff_benchmarks
# ==============================================================================
add_executable(tiff_benchmarks
    benchmark.cpp
    benchmark_helpers.cpp
)

target_link_libraries(tiff_benchmarks
    benchmark::benchmark
    zstd_static
)

# Conditionally link libtiff
if(TIFF_FOUND)
    target_link_libraries(tiff_benchmarks ${TIFF_LIBRARIES})
    target_include_directories(tiff_benchmarks PRIVATE ${TIFF_INCLUDE_DIRS})
    target_compile_definitions(tiff_benchmarks PRIVATE HAVE_LIBTIFF)
endif()

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_compile_definitions(tiff_benchmarks PRIVATE __unix__)
    
    # Link liburing if available
    if(HAVE_LIBURING)
        target_link_libraries(tiff_benchmarks ${LIBURING_LIBRARY})
        target_compile_definitions(tiff_benchmarks PRIVATE HAVE_LIBURING)
    endif()
elseif(WIN32)
    target_compile_definitions(tiff_benchmarks PRIVATE _WIN32)
endif()

# Compiler warnings and optimizations
if(MSVC)
    target_compile_options(tiff_benchmarks PRIVATE /W4 /O2)
else()
    target_compile_options(tiff_benchmarks PRIVATE -Wall -Wextra -O3 -march=native -DNDEBUG)
endif()

# ==============================================================================
# Executable: benchmark_read_directory
# ==============================================================================
add_executable(benchmark_read_directory
    benchmark_read_directory.cpp
)

target_link_libraries(benchmark_read_directory
    zstd_static
    ${CHARLS_LIBRARY}
)

# libtiff is required for benchmark_read_directory
if(TIFF_FOUND)
    target_link_libraries(benchmark_read_directory ${TIFF_LIBRARIES})
    target_include_directories(benchmark_read_directory PRIVATE ${TIFF_INCLUDE_DIRS})
    target_compile_definitions(benchmark_read_directory PRIVATE HAVE_LIBTIFF)
else()
    message(FATAL_ERROR "libtiff is required for benchmark_read_directory")
endif()

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_compile_definitions(benchmark_read_directory PRIVATE __unix__)
    
    # Link liburing if available
    if(HAVE_LIBURING)
        target_link_libraries(benchmark_read_directory ${LIBURING_LIBRARY})
        target_compile_definitions(benchmark_read_directory PRIVATE HAVE_LIBURING)
    endif()
elseif(WIN32)
    target_compile_definitions(benchmark_read_directory PRIVATE _WIN32)
endif()

# Compiler warnings and optimizations
if(MSVC)
    target_compile_options(benchmark_read_directory PRIVATE /W4 /O2)
else()
    target_compile_options(benchmark_read_directory PRIVATE -Wall -Wextra -O3 -march=native -DNDEBUG)
endif()