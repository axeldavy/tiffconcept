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

# Add ZSTD library
add_library(zstd_static STATIC
    ${CMAKE_SOURCE_DIR}/../tiffconcept/src/zstd.c
)
target_include_directories(zstd_static PRIVATE
    ${CMAKE_SOURCE_DIR}/../tiffconcept/include/zstd
)
target_include_directories(zstd_static PUBLIC
    ${CMAKE_SOURCE_DIR}/../tiffconcept/include/zstd
)
set_target_properties(zstd_static PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

# Try to find libtiff
find_package(TIFF)

# Add the benchmark executable
add_executable(tiff_benchmarks
    benchmark.cpp
    benchmark_helpers.cpp
)


# Link against Google Benchmark and ZSTD
target_link_libraries(tiff_benchmarks
    benchmark::benchmark
    zstd_static
)

# Conditionally link libtiff if found
if(TIFF_FOUND)
    target_link_libraries(tiff_benchmarks ${TIFF_LIBRARIES})
    target_include_directories(tiff_benchmarks PRIVATE ${TIFF_INCLUDE_DIRS})
    target_compile_definitions(tiff_benchmarks PRIVATE HAVE_LIBTIFF)
    
    message(STATUS "libtiff found - comparison benchmarks enabled")
else()
    message(STATUS "libtiff not found - comparison benchmarks will be skipped")
endif()

# Add compiler warnings
if(MSVC)
    target_compile_options(tiff_benchmarks PRIVATE /W4 /O2)
else()
    target_compile_options(tiff_benchmarks PRIVATE -Wall -Wextra -O3 -march=native -DNDEBUG)
    #target_compile_options(tiff_benchmarks PRIVATE -Wall -Wextra -Wno-unused-result -Wno-unused-value -O2 -DNDEBUG)
endif()

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    target_compile_definitions(tiff_benchmarks PRIVATE __unix__)
elseif(WIN32)
    target_compile_definitions(tiff_benchmarks PRIVATE _WIN32)
endif()

# Add standalone benchmark for reading directories
add_executable(benchmark_read_directory
    benchmark_read_directory.cpp
)

# Link against ZSTD and libtiff
target_link_libraries(benchmark_read_directory
    zstd_static
)

if(TIFF_FOUND)
    target_link_libraries(benchmark_read_directory ${TIFF_LIBRARIES})
    target_include_directories(benchmark_read_directory PRIVATE ${TIFF_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "libtiff is required for benchmark_read_directory")
endif()

# Add compiler warnings and optimizations
if(MSVC)
    target_compile_options(benchmark_read_directory PRIVATE /W4 /O2)
else()
    target_compile_options(benchmark_read_directory PRIVATE -Wall -Wextra -O3 -march=native -DNDEBUG)
endif()

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    target_compile_definitions(benchmark_read_directory PRIVATE __unix__)
elseif(WIN32)
    target_compile_definitions(benchmark_read_directory PRIVATE _WIN32)
endif()
