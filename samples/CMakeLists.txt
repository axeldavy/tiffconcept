cmake_minimum_required(VERSION 3.14)
project(tiffconcept_samples)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable optimization
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

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
# Executable: recode
# ==============================================================================
add_executable(recode
    recode.cpp
)

target_link_libraries(recode
    zstd_static
    ${CHARLS_LIBRARY}
)

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_compile_definitions(recode PRIVATE __unix__)
elseif(WIN32)
    target_compile_definitions(recode PRIVATE _WIN32)
endif()

# Compiler warnings and optimizations
if(MSVC)
    target_compile_options(recode PRIVATE /W4 /O2)
else()
    target_compile_options(recode PRIVATE -Wall -Wextra -O3 -march=native -DNDEBUG)
endif()
