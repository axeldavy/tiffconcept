#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <thread>
#include <array>
#include <cstring>

#include "../tiffconcept/include/tiff/reader_buffer.hpp"
#include "../tiffconcept/include/tiff/reader_stream.hpp"

#ifdef __unix__
#include "../tiffconcept/include/tiff/reader_unix_pread.hpp"
#include "../tiffconcept/include/tiff/reader_unix_mmap.hpp"
#endif

#ifdef _WIN32
#include "../tiffconcept/include/tiff/reader_windows.hpp"
#include "../tiffconcept/include/tiff/reader_windows_mmap.hpp"
#endif

namespace fs = std::filesystem;

// Helper function to create a test file with known content
fs::path create_test_file(const std::string& filename, const std::vector<std::byte>& content) {
    fs::path temp_dir = fs::temp_directory_path();
    fs::path test_file = temp_dir / filename;
    
    std::ofstream file(test_file, std::ios::binary);
    file.write(reinterpret_cast<const char*>(content.data()), content.size());
    file.close();
    
    return test_file;
}

// Helper function to read entire file content
std::vector<std::byte> read_file_content(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<std::byte> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

// ============================================================================
// Test Fixtures
// ============================================================================

template <typename ReaderT>
class RawReaderTest : public ::testing::Test {
protected:
    std::vector<std::byte> test_data;
    fs::path test_file_path;
    
    void SetUp() override {
        test_data.resize(1024);
        for (size_t i = 0; i < test_data.size(); ++i) {
            test_data[i] = static_cast<std::byte>(i % 256);
        }
        test_file_path = create_test_file("test_reader_file.bin", test_data);
    }
    
    void TearDown() override {
        if (fs::exists(test_file_path)) {
            fs::remove(test_file_path);
        }
    }
    
    ReaderT create_reader();
};

// Specialization for BufferViewReader (borrowed buffer)
template <>
tiff::BufferViewReader RawReaderTest<tiff::BufferViewReader>::create_reader() {
    return tiff::BufferViewReader(std::span<const std::byte>(test_data));
}

// Specialization for BufferReader (owned buffer)
template <>
tiff::BufferReader RawReaderTest<tiff::BufferReader>::create_reader() {
    return tiff::BufferReader(std::span<const std::byte>(test_data));
}

// Specialization for StreamFileReader
template <>
tiff::StreamFileReader RawReaderTest<tiff::StreamFileReader>::create_reader() {
    return tiff::StreamFileReader(test_file_path.string());
}

#ifdef __unix__
// Specialization for PreadFileReader
template <>
tiff::PreadFileReader RawReaderTest<tiff::PreadFileReader>::create_reader() {
    return tiff::PreadFileReader(test_file_path.string());
}

// Specialization for MmapFileReader
template <>
tiff::MmapFileReader RawReaderTest<tiff::MmapFileReader>::create_reader() {
    return tiff::MmapFileReader(test_file_path.string());
}
#endif

#ifdef _WIN32
// Specialization for WindowsFileReader
template <>
tiff::WindowsFileReader RawReaderTest<tiff::WindowsFileReader>::create_reader() {
    return tiff::WindowsFileReader(test_file_path.string());
}

// Specialization for WindowsMmapFileReader
template <>
tiff::WindowsMmapFileReader RawReaderTest<tiff::WindowsMmapFileReader>::create_reader() {
    return tiff::WindowsMmapFileReader(test_file_path.string());
}
#endif

// Define test types
using ReaderTypes = ::testing::Types<
    tiff::BufferViewReader,  // Borrowed buffer view
    tiff::BufferReader,      // Owned buffer
    tiff::StreamFileReader
#ifdef __unix__
    , tiff::PreadFileReader
    , tiff::MmapFileReader
#endif
#ifdef _WIN32
    , tiff::WindowsFileReader
    , tiff::WindowsMmapFileReader
#endif
>;

TYPED_TEST_SUITE(RawReaderTest, ReaderTypes);

// ============================================================================
// Basic RawReader Tests
// ============================================================================

TYPED_TEST(RawReaderTest, IsValidAfterConstruction) {
    auto reader = this->create_reader();
    EXPECT_TRUE(reader.is_valid());
}

TYPED_TEST(RawReaderTest, SizeReturnsCorrectValue) {
    auto reader = this->create_reader();
    auto size_result = reader.size();
    
    ASSERT_TRUE(size_result.is_ok());
    EXPECT_EQ(size_result.value(), this->test_data.size());
}

TYPED_TEST(RawReaderTest, ReadEntireFile) {
    auto reader = this->create_reader();
    auto read_result = reader.read(0, this->test_data.size());
    
    ASSERT_TRUE(read_result.is_ok());
    auto view = std::move(read_result.value());
    
    EXPECT_EQ(view.size(), this->test_data.size());
    EXPECT_FALSE(view.empty());
    
    auto data = view.data();
    EXPECT_EQ(std::memcmp(data.data(), this->test_data.data(), this->test_data.size()), 0);
}

TYPED_TEST(RawReaderTest, ReadPartialData) {
    auto reader = this->create_reader();
    auto read_result = reader.read(50, 100);
    
    ASSERT_TRUE(read_result.is_ok());
    auto view = std::move(read_result.value());
    EXPECT_EQ(view.size(), 100);
    EXPECT_EQ(std::memcmp(view.data().data(), this->test_data.data() + 50, 100), 0);
}

TYPED_TEST(RawReaderTest, ReadAtEndOfFile) {
    auto reader = this->create_reader();
    auto read_result = reader.read(this->test_data.size() - 10, 10);
    
    ASSERT_TRUE(read_result.is_ok());
    auto view = std::move(read_result.value());
    EXPECT_EQ(view.size(), 10);
    EXPECT_EQ(std::memcmp(view.data().data(), this->test_data.data() + this->test_data.size() - 10, 10), 0);
}

TYPED_TEST(RawReaderTest, ReadBeyondEOFReturnsError) {
    auto reader = this->create_reader();
    auto read_result = reader.read(this->test_data.size() + 100, 10);
    
    ASSERT_TRUE(read_result.is_error());
    EXPECT_EQ(read_result.error().code, tiff::Error::Code::OutOfBounds);
}

TYPED_TEST(RawReaderTest, ReadMoreThanAvailableTruncates) {
    auto reader = this->create_reader();
    auto read_result = reader.read(this->test_data.size() - 10, 50);
    
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_EQ(read_result.value().size(), 10);
}

TYPED_TEST(RawReaderTest, MultipleReadsReturnConsistentData) {
    auto reader = this->create_reader();
    auto read1 = reader.read(100, 50);
    auto read2 = reader.read(100, 50);
    
    ASSERT_TRUE(read1.is_ok());
    ASSERT_TRUE(read2.is_ok());
    EXPECT_EQ(std::memcmp(read1.value().data().data(), read2.value().data().data(), 50), 0);
}

TYPED_TEST(RawReaderTest, ThreadSafeReads) {
    auto reader = this->create_reader();
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};
    
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 10; ++i) {
                size_t offset = (t * 10 + i) % (this->test_data.size() - 10);
                auto result = reader.read(offset, 10);
                
                if (!result.is_ok() || result.value().size() != 10 ||
                    std::memcmp(result.value().data().data(), this->test_data.data() + offset, 10) != 0) {
                    errors++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(errors.load(), 0);
}

// ============================================================================
// Test Fixture for RawWriter implementations
// ============================================================================

template <typename WriterT>
class RawWriterTest : public ::testing::Test {
protected:
    fs::path test_file_path;
    
    void SetUp() override {
        fs::path temp_dir = fs::temp_directory_path();
        test_file_path = temp_dir / "test_writer_file.bin";
        
        // Remove if exists
        if (fs::exists(test_file_path)) {
            fs::remove(test_file_path);
        }
    }
    
    void TearDown() override {
        if (fs::exists(test_file_path)) {
            fs::remove(test_file_path);
        }
    }
    
    WriterT create_writer(size_t initial_size = 0);
};

// Specialization for BufferViewWriter (borrowed buffer)
template <>
tiff::BufferViewWriter RawWriterTest<tiff::BufferViewWriter>::create_writer(size_t initial_size) {
    static std::vector<std::byte> buffer;
    buffer.resize(initial_size);
    return tiff::BufferViewWriter(std::span<std::byte>(buffer));
}

// Specialization for BufferWriter (owned buffer)
template <>
tiff::BufferWriter RawWriterTest<tiff::BufferWriter>::create_writer(size_t initial_size) {
    return tiff::BufferWriter(initial_size);
}

// Specialization for StreamFileWriter
template <>
tiff::StreamFileWriter RawWriterTest<tiff::StreamFileWriter>::create_writer(size_t initial_size) {
    // Create empty file first
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return tiff::StreamFileWriter(test_file_path.string());
}

#ifdef __unix__
// Specialization for PwriteFileWriter
template <>
tiff::PwriteFileWriter RawWriterTest<tiff::PwriteFileWriter>::create_writer(size_t initial_size) {
    // Create empty file first
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    tiff::PwriteFileWriter writer(test_file_path.string());
    return writer;
}

#endif

#ifdef _WIN32
// Specialization for WindowsFileWriter
template <>
tiff::WindowsFileWriter RawWriterTest<tiff::WindowsFileWriter>::create_writer(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return tiff::WindowsFileWriter(test_file_path.string());
}

#endif

// Define test types for writers
using WriterTypes = ::testing::Types<
    tiff::BufferViewWriter,  // Borrowed buffer view
    tiff::BufferWriter,      // Owned buffer
    tiff::StreamFileWriter
#ifdef __unix__
    , tiff::PwriteFileWriter
#endif
#ifdef _WIN32
    , tiff::WindowsFileWriter
#endif
>;

TYPED_TEST_SUITE(RawWriterTest, WriterTypes);

// ============================================================================
// Basic RawWriter Tests
// ============================================================================

TYPED_TEST(RawWriterTest, IsValidAfterConstruction) {
    auto writer = this->create_writer(1024);
    EXPECT_TRUE(writer.is_valid());
}

TYPED_TEST(RawWriterTest, SizeReturnsCorrectValue) {
    auto writer = this->create_writer(1024);
    auto size_result = writer.size();
    
    ASSERT_TRUE(size_result.is_ok());
    EXPECT_EQ(size_result.value(), 1024);
}

TYPED_TEST(RawWriterTest, WriteAndFlush) {
    auto writer = this->create_writer(1024);
    auto write_result = writer.write(0, 100);
    ASSERT_TRUE(write_result.is_ok());
    
    auto view = std::move(write_result.value());
    EXPECT_EQ(view.size(), 100);
    
    auto data = view.data();
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i % 256);
    }
    
    EXPECT_TRUE(view.flush().is_ok());
    EXPECT_TRUE(writer.flush().is_ok());
}

TYPED_TEST(RawWriterTest, WriteAtOffset) {
    auto writer = this->create_writer(1024);
    auto write_result = writer.write(500, 100);
    ASSERT_TRUE(write_result.is_ok());
    
    auto view = std::move(write_result.value());
    auto data = view.data();
    std::fill(data.begin(), data.end(), std::byte{0xFF});
    
    EXPECT_TRUE(view.flush().is_ok());
}

TYPED_TEST(RawWriterTest, WriteBeyondSizeReturnsError) {
    auto writer = this->create_writer(100);
    auto write_result = writer.write(200, 10);
    
    ASSERT_TRUE(write_result.is_error());
    EXPECT_EQ(write_result.error().code, tiff::Error::Code::OutOfBounds);
}

TYPED_TEST(RawWriterTest, ResizeIncreases) {
    auto writer = this->create_writer(100);
    auto resize_result = writer.resize(500);
    
    if (resize_result.is_ok()) {
        auto size_result = writer.size();
        ASSERT_TRUE(size_result.is_ok());
        EXPECT_EQ(size_result.value(), 500);
    }
    // resize() is allowed to fail - not an error
}

TYPED_TEST(RawWriterTest, ResizeDecreases) {
    auto writer = this->create_writer(1000);
    auto resize_result = writer.resize(500);
    
    if (resize_result.is_ok()) {
        auto size_result = writer.size();
        ASSERT_TRUE(size_result.is_ok());
        EXPECT_EQ(size_result.value(), 500);
    }
    // resize() is allowed to fail - not an error
}

TYPED_TEST(RawWriterTest, MultipleWrites) {
    auto writer = this->create_writer(1024);
    
    {
        auto view = std::move(writer.write(0, 256).value());
        std::fill(view.data().begin(), view.data().end(), std::byte{0xAA});
        EXPECT_TRUE(view.flush().is_ok());
    }
    
    {
        auto view = std::move(writer.write(256, 256).value());
        std::fill(view.data().begin(), view.data().end(), std::byte{0xBB});
        EXPECT_TRUE(view.flush().is_ok());
    }
    
    EXPECT_TRUE(writer.flush().is_ok());
}

TYPED_TEST(RawWriterTest, ThreadSafeWrites) {
    auto writer = this->create_writer(4096);
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};
    
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t]() {
            auto result = writer.write(t * 1024, 1024);
            if (!result.is_ok()) {
                errors++;
                return;
            }
            
            auto view = std::move(result.value());
            std::fill(view.data().begin(), view.data().end(), static_cast<std::byte>(t));
            
            if (!view.flush().is_ok()) {
                errors++;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(errors.load(), 0);
    EXPECT_TRUE(writer.flush().is_ok());
}

// ============================================================================
// Test Fixture for ReadWrite implementations
// ============================================================================

template <typename ReadWriterT>
class RawReadWriterTest : public ::testing::Test {
protected:
    fs::path test_file_path;
    
    void SetUp() override {
        fs::path temp_dir = fs::temp_directory_path();
        test_file_path = temp_dir / "test_readwriter_file.bin";
        
        // Remove if exists
        if (fs::exists(test_file_path)) {
            fs::remove(test_file_path);
        }
    }
    
    void TearDown() override {
        if (fs::exists(test_file_path)) {
            fs::remove(test_file_path);
        }
    }
    
    ReadWriterT create_readwriter(size_t initial_size = 0);
};

// Specialization for BufferViewReadWriter (borrowed buffer)
template <>
tiff::BufferViewReadWriter RawReadWriterTest<tiff::BufferViewReadWriter>::create_readwriter(size_t initial_size) {
    static std::vector<std::byte> buffer;
    buffer.resize(initial_size);
    return tiff::BufferViewReadWriter(std::span<std::byte>(buffer));
}

// Specialization for BufferReadWriter (owned buffer)
template <>
tiff::BufferReadWriter RawReadWriterTest<tiff::BufferReadWriter>::create_readwriter(size_t initial_size) {
    return tiff::BufferReadWriter(initial_size);
}

// Specialization for StreamFileReadWriter
template <>
tiff::StreamFileReadWriter RawReadWriterTest<tiff::StreamFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return tiff::StreamFileReadWriter(test_file_path.string());
}

#ifdef __unix__
template <>
tiff::PreadFileReadWriter RawReadWriterTest<tiff::PreadFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return tiff::PreadFileReadWriter(test_file_path.string());
}

template <>
tiff::MmapFileReadWriter RawReadWriterTest<tiff::MmapFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return tiff::MmapFileReadWriter(test_file_path.string());
}
#endif

#ifdef _WIN32
template <>
tiff::WindowsFileReadWriter RawReadWriterTest<tiff::WindowsFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return tiff::WindowsFileReadWriter(test_file_path.string());
}

template <>
tiff::WindowsMmapFileReadWriter RawReadWriterTest<tiff::WindowsMmapFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return tiff::WindowsMmapFileReadWriter(test_file_path.string());
}
#endif

using ReadWriterTypes = ::testing::Types<
    tiff::BufferViewReadWriter,  // Borrowed buffer view
    tiff::BufferReadWriter,      // Owned buffer
    tiff::StreamFileReadWriter
#ifdef __unix__
    , tiff::PreadFileReadWriter
    , tiff::MmapFileReadWriter
#endif
#ifdef _WIN32
    , tiff::WindowsFileReadWriter
    , tiff::WindowsMmapFileReadWriter
#endif
>;

TYPED_TEST_SUITE(RawReadWriterTest, ReadWriterTypes);

// ============================================================================
// ReadWrite Tests
// ============================================================================

TYPED_TEST(RawReadWriterTest, WriteAndReadBack) {
    auto rw = this->create_readwriter(1024);
    
    {
        auto view = std::move(rw.write(0, 100).value());
        auto data = view.data();
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<std::byte>(i % 256);
        }
        EXPECT_TRUE(view.flush().is_ok());
    }
    
    EXPECT_TRUE(rw.flush().is_ok());
    
    {
        auto view = std::move(rw.read(0, 100).value());
        auto data = view.data();
        for (size_t i = 0; i < data.size(); ++i) {
            EXPECT_EQ(data[i], static_cast<std::byte>(i % 256));
        }
    }
}

TYPED_TEST(RawReadWriterTest, NonOverlappingReadWriteSimultaneous) {
    auto rw = this->create_readwriter(2048);
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};
    
    threads.emplace_back([&]() {
        auto result = rw.write(0, 1024);
        if (!result.is_ok()) {
            errors++;
            return;
        }
        auto view = std::move(result.value());
        std::fill(view.data().begin(), view.data().end(), std::byte{0xAA});
        if (!view.flush().is_ok()) {
            errors++;
        }
    });
    
    threads.emplace_back([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto result = rw.read(1024, 1024);
        if (!result.is_ok()) {
            errors++;
        }
    });
    
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(errors.load(), 0);
}

// ============================================================================
// Corner Cases
// ============================================================================

TEST(CornerCases, EmptyBufferReader) {
    std::vector<std::byte> empty_buffer;
    tiff::BufferViewReader reader{std::span<const std::byte>(empty_buffer)};
    
    EXPECT_TRUE(reader.is_valid());
    EXPECT_EQ(reader.size().value(), 0);
    EXPECT_TRUE(reader.read(0, 10).is_error());
}

TEST(CornerCases, ReadZeroBytes) {
    std::vector<std::byte> buffer(100, std::byte{0});
    tiff::BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto view = std::move(reader.read(0, 0).value());
    EXPECT_EQ(view.size(), 0);
    EXPECT_TRUE(view.empty());
}

TEST(CornerCases, NonExistentFile) {
    tiff::StreamFileReader reader("/nonexistent/path/to/file.bin");
    EXPECT_FALSE(reader.is_valid());
}

TEST(CornerCases, WriteZeroBytes) {
    std::vector<std::byte> buffer(100, std::byte{0});
    tiff::BufferViewWriter writer{std::span<std::byte>(buffer)};
    
    auto view = std::move(writer.write(0, 0).value());
    EXPECT_EQ(view.size(), 0);
    EXPECT_TRUE(view.empty());
    EXPECT_TRUE(view.flush().is_ok());
}

TEST(CornerCases, ResizeToZero) {
    fs::path temp_file = fs::temp_directory_path() / "resize_test.bin";
    
    {
        std::ofstream file(temp_file, std::ios::binary);
        std::vector<std::byte> data(1000, std::byte{0xFF});
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    
    tiff::StreamFileWriter writer(temp_file.string());
    auto resize_result = writer.resize(0);
    
    if (resize_result.is_ok()) {
        EXPECT_EQ(writer.size().value(), 0);
    }
    
    fs::remove(temp_file);
}

TEST(CornerCases, BorrowedBufferResizeBehavior) {
    std::vector<std::byte> buffer(100, std::byte{0});
    tiff::BufferViewWriter writer{std::span<std::byte>(buffer)};
    
    // Resizing borrowed buffer to different size may fail
    auto resize_result = writer.resize(200);
    // It's OK if this fails - borrowed buffers typically can't resize
    
    // Resizing to same size should work
    EXPECT_TRUE(writer.resize(100).is_ok());
}

TEST(CornerCases, OwnedBufferResize) {
    tiff::BufferWriter writer(100);
    EXPECT_EQ(writer.size().value(), 100);
    
    // Owned buffers should support resize
    if (writer.resize(200).is_ok()) {
        EXPECT_EQ(writer.size().value(), 200);
    }
    
    if (writer.resize(50).is_ok()) {
        EXPECT_EQ(writer.size().value(), 50);
    }
}

// ============================================================================
// View Tests
// ============================================================================

TEST(ViewTests, BorrowedBufferReadView) {
    std::array<std::byte, 100> data;
    std::span<const std::byte> span{data};
    tiff::buffer_impl::BorrowedBufferReadView view{span};
    
    EXPECT_EQ(view.size(), 100);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(view.data().data(), data.data());
}

TEST(ViewTests, BorrowedBufferWriteView) {
    std::array<std::byte, 100> data;
    std::span<std::byte> span{data};
    tiff::buffer_impl::BorrowedBufferWriteView view{span};
    
    EXPECT_EQ(view.size(), 100);
    EXPECT_FALSE(view.empty());
    EXPECT_TRUE(view.supports_inplace_readback);
    
    auto writable = view.data();
    for (size_t i = 0; i < writable.size(); ++i) {
        writable[i] = static_cast<std::byte>(i);
    }
    EXPECT_TRUE(view.flush().is_ok());
    
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(data[i], static_cast<std::byte>(i));
    }
}

TEST(ViewTests, OwnedBufferReadView) {
    auto buffer = std::shared_ptr<std::byte[]>(new std::byte[100]);
    std::span<const std::byte> span{buffer.get(), 100};
    tiff::stream_impl::OwnedBufferReadView view{span, buffer};
    
    EXPECT_EQ(view.size(), 100);
    EXPECT_FALSE(view.empty());
}

TEST(ViewTests, OwnedBufferWriteViewFlushOnDestruction) {
    fs::path temp_file = fs::temp_directory_path() / "view_flush_test.bin";
    
#ifdef __unix__
    int fd = open(temp_file.c_str(), O_RDWR | O_CREAT, 0644);
    ASSERT_GE(fd, 0);
    ftruncate(fd, 100);
    
    {
        auto buffer = std::shared_ptr<std::byte[]>(new std::byte[50]);
        for (size_t i = 0; i < 50; ++i) {
            buffer[i] = static_cast<std::byte>(i);
        }
        
        tiff::pread_impl::OwnedBufferWriteView view(std::span<std::byte>(buffer.get(), 50), buffer, fd, 0);
    }
    
    std::array<std::byte, 50> verify;
    pread(fd, verify.data(), 50, 0);
    
    for (size_t i = 0; i < 50; ++i) {
        EXPECT_EQ(verify[i], static_cast<std::byte>(i));
    }
    
    close(fd);
#endif
    
    fs::remove(temp_file);
}

TEST(ViewTests, EmptyView) {
    tiff::buffer_impl::BorrowedBufferReadView view;
    EXPECT_EQ(view.size(), 0);
    EXPECT_TRUE(view.empty());
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST(StressTests, LargeFileRead) {
    const size_t large_size = 10 * 1024 * 1024;
    std::vector<std::byte> large_data(large_size);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path large_file = create_test_file("large_test.bin", large_data);
    tiff::StreamFileReader reader(large_file.string());
    ASSERT_TRUE(reader.is_valid());
    
    const size_t chunk_size = 64 * 1024;
    for (size_t offset = 0; offset < large_size; offset += chunk_size) {
        size_t read_size = std::min(chunk_size, large_size - offset);
        auto result = reader.read(offset, read_size);
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().size(), read_size);
    }
    
    fs::remove(large_file);
}

TEST(StressTests, ManySmallWrites) {
    fs::path temp_file = fs::temp_directory_path() / "many_writes.bin";
    
    {
        std::ofstream file(temp_file, std::ios::binary);
        std::vector<std::byte> zeros(10000, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), zeros.size());
    }
    
    tiff::StreamFileWriter writer(temp_file.string());
    ASSERT_TRUE(writer.is_valid());
    
    for (size_t i = 0; i < 1000; ++i) {
        size_t offset = (i * 10) % 9000;
        auto view = std::move(writer.write(offset, 10).value());
        std::fill(view.data().begin(), view.data().end(), static_cast<std::byte>(i % 256));
        EXPECT_TRUE(view.flush().is_ok());
    }
    
    EXPECT_TRUE(writer.flush().is_ok());
    fs::remove(temp_file);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
