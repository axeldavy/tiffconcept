#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <thread>
#include <array>
#include <cstring>

#include "../tiffconcept/include/tiffconcept/readers/reader_buffer.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_stream.hpp"

#ifdef __unix__
#include "../tiffconcept/include/tiffconcept/readers/reader_unix_pread.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_unix_mmap.hpp"
#ifdef HAVE_LIBURING
#include "../tiffconcept/include/tiffconcept/readers/reader_unix_io_uring.hpp"
#endif
#endif

#ifdef _WIN32
#include "../tiffconcept/include/tiffconcept/readers/reader_windows.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_windows_mmap.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_windows_async.hpp"
#endif

namespace fs = std::filesystem;
using namespace tiffconcept;

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
BufferViewReader RawReaderTest<BufferViewReader>::create_reader() {
    return BufferViewReader(std::span<const std::byte>(test_data));
}

// Specialization for BufferReader (owned buffer)
template <>
BufferReader RawReaderTest<BufferReader>::create_reader() {
    return BufferReader(std::span<const std::byte>(test_data));
}

// Specialization for StreamFileReader
template <>
StreamFileReader RawReaderTest<StreamFileReader>::create_reader() {
    return StreamFileReader(test_file_path.string());
}

#ifdef __unix__
// Specialization for PreadFileReader
template <>
PreadFileReader RawReaderTest<PreadFileReader>::create_reader() {
    return PreadFileReader(test_file_path.string());
}

// Specialization for MmapFileReader
template <>
MmapFileReader RawReaderTest<MmapFileReader>::create_reader() {
    return MmapFileReader(test_file_path.string());
}

#ifdef HAVE_LIBURING
// Specialization for IoUringFileReader
template <>
IoUringFileReader RawReaderTest<IoUringFileReader>::create_reader() {
    IoUringFileReader::Config config;
    config.queue_depth = 32;  // Smaller queue for tests
    config.use_sqpoll = false;  // Don't require elevated privileges
    config.use_iopoll = false;  // Not all systems support this
    return IoUringFileReader(test_file_path.string(), config);
}
#endif
#endif

#ifdef _WIN32
// Specialization for WindowsFileReader
template <>
WindowsFileReader RawReaderTest<WindowsFileReader>::create_reader() {
    return WindowsFileReader(test_file_path.string());
}

// Specialization for WindowsMmapFileReader
template <>
WindowsMmapFileReader RawReaderTest<WindowsMmapFileReader>::create_reader() {
    return WindowsMmapFileReader(test_file_path.string());
}

// Specialization for IOCPFileReader
template <>
IOCPFileReader RawReaderTest<IOCPFileReader>::create_reader() {
    IOCPFileReader::Config config;
    config.max_concurrent_threads = 4;  // Limit for tests
    config.use_unbuffered = false;  // Use buffered I/O for tests
    return IOCPFileReader(test_file_path.string(), config);
}
#endif

// Define test types
using ReaderTypes = ::testing::Types<
    BufferViewReader,  // Borrowed buffer view
    BufferReader,      // Owned buffer
    StreamFileReader
#ifdef __unix__
    , PreadFileReader
    , MmapFileReader
#ifdef HAVE_LIBURING
    , IoUringFileReader  // Async io_uring reader
#endif
#endif
#ifdef _WIN32
    , WindowsFileReader
    , WindowsMmapFileReader
    , IOCPFileReader  // Async IOCP reader
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
    EXPECT_EQ(read_result.error().code, Error::Code::OutOfBounds);
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
BufferViewWriter RawWriterTest<BufferViewWriter>::create_writer(size_t initial_size) {
    static std::vector<std::byte> buffer;
    buffer.resize(initial_size);
    return BufferViewWriter(std::span<std::byte>(buffer));
}

// Specialization for BufferWriter (owned buffer)
template <>
BufferWriter RawWriterTest<BufferWriter>::create_writer(size_t initial_size) {
    return BufferWriter(initial_size);
}

// Specialization for StreamFileWriter
template <>
StreamFileWriter RawWriterTest<StreamFileWriter>::create_writer(size_t initial_size) {
    // Create empty file first
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return StreamFileWriter(test_file_path.string());
}

#ifdef __unix__
// Specialization for PwriteFileWriter
template <>
PwriteFileWriter RawWriterTest<PwriteFileWriter>::create_writer(size_t initial_size) {
    // Create empty file first
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    PwriteFileWriter writer(test_file_path.string());
    return writer;
}

#endif

#ifdef _WIN32
// Specialization for WindowsFileWriter
template <>
WindowsFileWriter RawWriterTest<WindowsFileWriter>::create_writer(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return WindowsFileWriter(test_file_path.string());
}

#endif

// Define test types for writers
using WriterTypes = ::testing::Types<
    BufferViewWriter,  // Borrowed buffer view
    BufferWriter,      // Owned buffer
    StreamFileWriter
#ifdef __unix__
    , PwriteFileWriter
#endif
#ifdef _WIN32
    , WindowsFileWriter
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
    EXPECT_EQ(write_result.error().code, Error::Code::OutOfBounds);
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
BufferViewReadWriter RawReadWriterTest<BufferViewReadWriter>::create_readwriter(size_t initial_size) {
    static std::vector<std::byte> buffer;
    buffer.resize(initial_size);
    return BufferViewReadWriter(std::span<std::byte>(buffer));
}

// Specialization for BufferReadWriter (owned buffer)
template <>
BufferReadWriter RawReadWriterTest<BufferReadWriter>::create_readwriter(size_t initial_size) {
    return BufferReadWriter(initial_size);
}

// Specialization for StreamFileReadWriter
template <>
StreamFileReadWriter RawReadWriterTest<StreamFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return StreamFileReadWriter(test_file_path.string());
}

#ifdef __unix__
template <>
PreadFileReadWriter RawReadWriterTest<PreadFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return PreadFileReadWriter(test_file_path.string());
}

template <>
MmapFileReadWriter RawReadWriterTest<MmapFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return MmapFileReadWriter(test_file_path.string());
}
#endif

#ifdef _WIN32
template <>
WindowsFileReadWriter RawReadWriterTest<WindowsFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return WindowsFileReadWriter(test_file_path.string());
}

template <>
WindowsMmapFileReadWriter RawReadWriterTest<WindowsMmapFileReadWriter>::create_readwriter(size_t initial_size) {
    std::ofstream file(test_file_path, std::ios::binary);
    if (initial_size > 0) {
        std::vector<std::byte> zeros(initial_size, std::byte{0});
        file.write(reinterpret_cast<const char*>(zeros.data()), initial_size);
    }
    file.close();
    
    return WindowsMmapFileReadWriter(test_file_path.string());
}
#endif

using ReadWriterTypes = ::testing::Types<
    BufferViewReadWriter,  // Borrowed buffer view
    BufferReadWriter,      // Owned buffer
    StreamFileReadWriter
#ifdef __unix__
    , PreadFileReadWriter
    , MmapFileReadWriter
#endif
#ifdef _WIN32
    , WindowsFileReadWriter
    , WindowsMmapFileReadWriter
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
    BufferViewReader reader{std::span<const std::byte>(empty_buffer)};
    
    EXPECT_TRUE(reader.is_valid());
    EXPECT_EQ(reader.size().value(), 0);
    EXPECT_TRUE(reader.read(0, 10).is_error());
}

TEST(CornerCases, ReadZeroBytes) {
    std::vector<std::byte> buffer(100, std::byte{0});
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto view = std::move(reader.read(0, 0).value());
    EXPECT_EQ(view.size(), 0);
    EXPECT_TRUE(view.empty());
}

TEST(CornerCases, NonExistentFile) {
    StreamFileReader reader("/nonexistent/path/to/file.bin");
    EXPECT_FALSE(reader.is_valid());
}

TEST(CornerCases, WriteZeroBytes) {
    std::vector<std::byte> buffer(100, std::byte{0});
    BufferViewWriter writer{std::span<std::byte>(buffer)};
    
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
    
    StreamFileWriter writer(temp_file.string());
    auto resize_result = writer.resize(0);
    
    if (resize_result.is_ok()) {
        EXPECT_EQ(writer.size().value(), 0);
    }
    
    fs::remove(temp_file);
}

TEST(CornerCases, BorrowedBufferResizeBehavior) {
    std::vector<std::byte> buffer(100, std::byte{0});
    BufferViewWriter writer{std::span<std::byte>(buffer)};
    
    // Resizing borrowed buffer to different size may fail
    auto resize_result = writer.resize(200);
    // It's OK if this fails - borrowed buffers typically can't resize
    
    // Resizing to same size should work
    EXPECT_TRUE(writer.resize(100).is_ok());
}

TEST(CornerCases, OwnedBufferResize) {
    BufferWriter writer(100);
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
    buffer_impl::BorrowedBufferReadView view{span};
    
    EXPECT_EQ(view.size(), 100);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(view.data().data(), data.data());
}

TEST(ViewTests, BorrowedBufferWriteView) {
    std::array<std::byte, 100> data;
    std::span<std::byte> span{data};
    buffer_impl::BorrowedBufferWriteView view{span};
    
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
    stream_impl::OwnedBufferReadView view{span, buffer};
    
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
        
        pread_impl::OwnedBufferWriteView view(std::span<std::byte>(buffer.get(), 50), buffer, fd, 0);
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
    buffer_impl::BorrowedBufferReadView view;
    EXPECT_EQ(view.size(), 0);
    EXPECT_TRUE(view.empty());
}

// ============================================================================
// AsyncRawReader Tests
// ============================================================================

#if defined(__linux__) && defined(HAVE_LIBURING)
TEST(AsyncReaderTests, IoUringBasicAsyncRead) {
    // Create test data
    std::vector<std::byte> test_data(8192);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iouring.bin", test_data);
    
    IoUringFileReader::Config config;
    config.queue_depth = 32;
    config.use_sqpoll = false;
    config.use_iopoll = false;
    
    IoUringFileReader reader(test_file.string(), config);
    ASSERT_TRUE(reader.is_valid());
    
    // Test single async read
    std::vector<std::byte> buffer(1024);
    auto handle_res = reader.async_read_into(buffer, 0, 1024);
    ASSERT_TRUE(handle_res.is_ok());
    
    // Submit to kernel
    ASSERT_TRUE(reader.submit_pending().is_ok());
    
    // Wait for completion
    auto completions = reader.wait_completions(1);
    ASSERT_EQ(completions.size(), 1);
    
    auto& [handle, result] = completions[0];
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().size(), 1024);
    EXPECT_EQ(std::memcmp(result.value().data().data(), test_data.data(), 1024), 0);
    
    fs::remove(test_file);
}

TEST(AsyncReaderTests, IoUringMultipleAsyncReads) {
    // Create test data
    std::vector<std::byte> test_data(16384);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iouring_multi.bin", test_data);
    
    IoUringFileReader::Config config;
    config.queue_depth = 64;
    config.use_sqpoll = false;
    config.use_iopoll = false;
    
    IoUringFileReader reader(test_file.string(), config);
    ASSERT_TRUE(reader.is_valid());
    
    // Submit multiple async reads
    std::vector<std::vector<std::byte>> buffers(8);
    std::vector<typename IoUringFileReader::AsyncOperationHandle> handles;
    
    for (size_t i = 0; i < 8; ++i) {
        buffers[i].resize(1024);
        auto handle_res = reader.async_read_into(buffers[i], i * 2048, 1024);
        ASSERT_TRUE(handle_res.is_ok());
        handles.push_back(std::move(handle_res.value()));
    }
    
    // Submit all to kernel
    ASSERT_TRUE(reader.submit_pending().is_ok());
    
    // Wait for all completions
    auto completions = reader.wait_completions(8);
    EXPECT_EQ(completions.size(), 8);
    
    // Verify all reads succeeded
    for (auto& [handle, result] : completions) {
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().size(), 1024);
    }
    
    fs::remove(test_file);
}

TEST(AsyncReaderTests, IoUringPollCompletions) {
    std::vector<std::byte> test_data(4096);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iouring_poll.bin", test_data);
    
    IoUringFileReader reader(test_file.string());
    ASSERT_TRUE(reader.is_valid());
    
    // Submit async read
    std::vector<std::byte> buffer(2048);
    auto handle_res = reader.async_read_into(buffer, 0, 2048);
    ASSERT_TRUE(handle_res.is_ok());
    ASSERT_TRUE(reader.submit_pending().is_ok());
    
    // Poll for completion (may return empty initially)
    size_t max_polls = 1000;
    size_t poll_count = 0;
    bool completed = false;
    
    while (poll_count++ < max_polls) {
        auto completions = reader.poll_completions(0);
        if (!completions.empty()) {
            ASSERT_EQ(completions.size(), 1);
            ASSERT_TRUE(completions[0].second.is_ok());
            EXPECT_EQ(completions[0].second.value().size(), 2048);
            completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    EXPECT_TRUE(completed) << "Read did not complete within polling limit";
    fs::remove(test_file);
}

TEST(AsyncReaderTests, IoUringWaitWithTimeout) {
    std::vector<std::byte> test_data(4096);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iouring_timeout.bin", test_data);
    
    IoUringFileReader reader(test_file.string());
    ASSERT_TRUE(reader.is_valid());
    
    // Submit async read
    std::vector<std::byte> buffer(2048);
    auto handle_res = reader.async_read_into(buffer, 0, 2048);
    ASSERT_TRUE(handle_res.is_ok());
    ASSERT_TRUE(reader.submit_pending().is_ok());
    
    // Wait with timeout (should complete quickly)
    auto completions = reader.wait_completions_for(
        std::chrono::milliseconds(1000),
        1
    );
    
    ASSERT_EQ(completions.size(), 1);
    ASSERT_TRUE(completions[0].second.is_ok());
    EXPECT_EQ(completions[0].second.value().size(), 2048);
    
    fs::remove(test_file);
}

TEST(AsyncReaderTests, IoUringSyncFallback) {
    // Test that sync read still works
    std::vector<std::byte> test_data(2048);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iouring_sync.bin", test_data);
    
    IoUringFileReader reader(test_file.string());
    ASSERT_TRUE(reader.is_valid());
    
    // Use sync read method
    auto read_res = reader.read(512, 1024);
    ASSERT_TRUE(read_res.is_ok());
    EXPECT_EQ(read_res.value().size(), 1024);
    EXPECT_EQ(std::memcmp(
        read_res.value().data().data(),
        test_data.data() + 512,
        1024
    ), 0);
    
    fs::remove(test_file);
}
#endif  // __linux__ && HAVE_LIBURING

#ifdef _WIN32
TEST(AsyncReaderTests, IOCPBasicAsyncRead) {
    // Create test data
    std::vector<std::byte> test_data(8192);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iocp.bin", test_data);
    
    IOCPFileReader::Config config;
    config.max_concurrent_threads = 4;
    config.use_unbuffered = false;
    
    IOCPFileReader reader(test_file.string(), config);
    ASSERT_TRUE(reader.is_valid());
    
    // Test single async read
    std::vector<std::byte> buffer(1024);
    auto handle_res = reader.async_read_into(buffer, 0, 1024);
    ASSERT_TRUE(handle_res.is_ok());
    
    // Submit (no-op for IOCP, but API compatible)
    ASSERT_TRUE(reader.submit_pending().is_ok());
    
    // Wait for completion
    auto completions = reader.wait_completions(1);
    ASSERT_EQ(completions.size(), 1);
    
    auto& [handle, result] = completions[0];
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().size(), 1024);
    EXPECT_EQ(std::memcmp(result.value().data().data(), test_data.data(), 1024), 0);
    
    fs::remove(test_file);
}

TEST(AsyncReaderTests, IOCPMultipleAsyncReads) {
    // Create test data
    std::vector<std::byte> test_data(16384);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iocp_multi.bin", test_data);
    
    IOCPFileReader::Config config;
    config.max_concurrent_threads = 8;
    config.use_unbuffered = false;
    
    IOCPFileReader reader(test_file.string(), config);
    ASSERT_TRUE(reader.is_valid());
    
    // Submit multiple async reads
    std::vector<std::vector<std::byte>> buffers(8);
    std::vector<typename IOCPFileReader::AsyncOperationHandle> handles;
    
    for (size_t i = 0; i < 8; ++i) {
        buffers[i].resize(1024);
        auto handle_res = reader.async_read_into(buffers[i], i * 2048, 1024);
        ASSERT_TRUE(handle_res.is_ok());
        handles.push_back(std::move(handle_res.value()));
    }
    
    // Wait for all completions
    auto completions = reader.wait_completions(8);
    EXPECT_EQ(completions.size(), 8);
    
    // Verify all reads succeeded
    for (auto& [handle, result] : completions) {
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().size(), 1024);
    }
    
    fs::remove(test_file);
}

TEST(AsyncReaderTests, IOCPPollCompletions) {
    std::vector<std::byte> test_data(4096);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iocp_poll.bin", test_data);
    
    IOCPFileReader reader(test_file.string());
    ASSERT_TRUE(reader.is_valid());
    
    // Submit async read
    std::vector<std::byte> buffer(2048);
    auto handle_res = reader.async_read_into(buffer, 0, 2048);
    ASSERT_TRUE(handle_res.is_ok());
    
    // Poll for completion (may return empty initially)
    size_t max_polls = 1000;
    size_t poll_count = 0;
    bool completed = false;
    
    while (poll_count++ < max_polls) {
        auto completions = reader.poll_completions(0);
        if (!completions.empty()) {
            ASSERT_EQ(completions.size(), 1);
            ASSERT_TRUE(completions[0].second.is_ok());
            EXPECT_EQ(completions[0].second.value().size(), 2048);
            completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    EXPECT_TRUE(completed) << "Read did not complete within polling limit";
    fs::remove(test_file);
}

TEST(AsyncReaderTests, IOCPWaitWithTimeout) {
    std::vector<std::byte> test_data(4096);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iocp_timeout.bin", test_data);
    
    IOCPFileReader reader(test_file.string());
    ASSERT_TRUE(reader.is_valid());
    
    // Submit async read
    std::vector<std::byte> buffer(2048);
    auto handle_res = reader.async_read_into(buffer, 0, 2048);
    ASSERT_TRUE(handle_res.is_ok());
    
    // Wait with timeout (should complete quickly)
    auto completions = reader.wait_completions_for(
        std::chrono::milliseconds(1000),
        1
    );
    
    ASSERT_EQ(completions.size(), 1);
    ASSERT_TRUE(completions[0].second.is_ok());
    EXPECT_EQ(completions[0].second.value().size(), 2048);
    
    fs::remove(test_file);
}

TEST(AsyncReaderTests, IOCPSyncFallback) {
    // Test that sync read still works
    std::vector<std::byte> test_data(2048);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<std::byte>(i % 256);
    }
    
    fs::path test_file = create_test_file("async_test_iocp_sync.bin", test_data);
    
    IOCPFileReader reader(test_file.string());
    ASSERT_TRUE(reader.is_valid());
    
    // Use sync read method
    auto read_res = reader.read(512, 1024);
    ASSERT_TRUE(read_res.is_ok());
    EXPECT_EQ(read_res.value().size(), 1024);
    EXPECT_EQ(std::memcmp(
        read_res.value().data().data(),
        test_data.data() + 512,
        1024
    ), 0);
    
    fs::remove(test_file);
}
#endif  // _WIN32

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
    StreamFileReader reader(large_file.string());
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
    
    StreamFileWriter writer(temp_file.string());
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
