cmake_minimum_required(VERSION 3.14)
project(tiffconcept_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include FetchContent module for downloading GoogleTest
include(FetchContent)

# Fetch GoogleTest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Include directories
include_directories(${CMAKE_SOURCE_DIR})

# Add the test executables
add_executable(test_reader_writer
    test_reader_writer.cpp
)

add_executable(test_tag_parsing_writing
    test_tag_parsing_writing.cpp
)

add_executable(test_extracted_tags
    test_extracted_tags.cpp
)

add_executable(test_predictor
    test_predictor.cpp
)

# Link against GoogleTest
target_link_libraries(test_reader_writer
    GTest::gtest_main
    GTest::gtest
)

target_link_libraries(test_tag_parsing_writing
    GTest::gtest_main
    GTest::gtest
)

target_link_libraries(test_extracted_tags
    GTest::gtest_main
    GTest::gtest
)

target_link_libraries(test_predictor
    GTest::gtest_main
    GTest::gtest
)

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    # Linux-specific libraries
    target_compile_definitions(test_reader_writer PRIVATE __unix__)
    target_compile_definitions(test_tag_parsing_writing PRIVATE __unix__)
    target_compile_definitions(test_extracted_tags PRIVATE __unix__)
    target_compile_definitions(test_predictor PRIVATE __unix__)
elseif(WIN32)
    # Windows-specific libraries
    target_compile_definitions(test_reader_writer PRIVATE _WIN32)
    target_compile_definitions(test_tag_parsing_writing PRIVATE _WIN32)
    target_compile_definitions(test_extracted_tags PRIVATE _WIN32)
    target_compile_definitions(test_predictor PRIVATE _WIN32)
endif()

# Add tests to CTest
include(GoogleTest)
gtest_discover_tests(test_reader_writer)
gtest_discover_tests(test_tag_parsing_writing)
gtest_discover_tests(test_extracted_tags)
gtest_discover_tests(test_predictor)

# Optional: Add compiler warnings
if(MSVC)
    target_compile_options(test_reader_writer PRIVATE /W4)
    target_compile_options(test_tag_parsing_writing PRIVATE /W4)
    target_compile_options(test_extracted_tags PRIVATE /W4)
    target_compile_options(test_predictor PRIVATE /W4)
else()
    target_compile_options(test_reader_writer PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_tag_parsing_writing PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_extracted_tags PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_predictor PRIVATE -Wall -Wextra -Wpedantic)
endif()
