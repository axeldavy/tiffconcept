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
# Executable: recode
# ==============================================================================
add_executable(recode
    recode.cpp
)

target_link_libraries(recode
    zstd_static
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
