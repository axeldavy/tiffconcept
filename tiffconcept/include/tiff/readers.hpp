#pragma once

// TIFF Reader - Modular Header-Only Library
// 
// This library provides multiple reader implementations optimized for different platforms
// and use cases. Choose the implementation that best fits your needs:
//
// 1. MmapFileReader (reader_unix_mmap.hpp) - POSIX only
//    - Zero-copy memory-mapped file access (mmap)
//    - Best performance for large files
//    - Thread-safe without locks
//    - Platform: Linux, macOS, Unix
//
// 2. PreadFileReader (reader_unix_pread.hpp) - POSIX only
//    - Positioned read with allocated buffers (pread)
//    - Thread-safe without locks
//    - Good for random access patterns
//    - Platform: Linux, macOS, Unix
//
// 3. WindowsMmapFileReader (reader_windows_mmap.hpp) - Windows only
//    - Zero-copy memory-mapped file access (CreateFileMapping/MapViewOfFile)
//    - Best performance for large files on Windows
//    - Thread-safe without locks
//    - Platform: Windows
//
// 4. WindowsFileReader (reader_windows.hpp) - Windows only
//    - Uses ReadFile with OVERLAPPED
//    - Thread-safe without locks
//    - Native Windows file I/O with allocated buffers
//    - Platform: Windows
//
// 5. StreamFileReader (reader_stream.hpp) - Portable
//    - Uses std::ifstream
//    - Thread-safe with mutex
//    - Works on all platforms
//    - Fallback option
//
// 6. BufferReader (reader_buffer.hpp) - All platforms
//    - Zero-copy in-memory buffer reading
//    - Thread-safe without locks
//    - For pre-loaded data
//    - Platform: All
//
// USAGE:
// Include only the reader(s) you need:
//   #include "tiff/reader_unix_mmap.hpp"
//   using MyReader = tiff::MmapFileReader;
//
// Or use the automatic platform selection below by defining TIFF_AUTO_READER:
//   #define TIFF_AUTO_READER
//   #include "tiff/readers.hpp"
//   using MyReader = tiff::FileReader;  // Automatically selects best for platform

#include "reader_base.hpp"
#include "reader_buffer.hpp"

// Platform-specific readers - include based on platform
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    #include "reader_unix_mmap.hpp"
    #include "reader_unix_pread.hpp"
#endif

#if defined(_WIN32) || defined(_WIN64)
    #include "reader_windows_mmap.hpp"
    #include "reader_windows.hpp"
#endif

#include "reader_stream.hpp"

// Automatic platform selection (opt-in with TIFF_AUTO_READER)
#ifdef TIFF_AUTO_READER

namespace tiff {

// FileReader alias - automatically selects the best reader for the platform
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    // Use memory-mapped file reader on POSIX systems (best performance)
    using FileReader = MmapFileReader;
    using DefaultFileReader = MmapFileReader;
    using AlternativeFileReader = PreadFileReader;
#elif defined(_WIN32) || defined(_WIN64)
    // Use Windows memory-mapped file reader (best performance)
    using FileReader = WindowsMmapFileReader;
    using DefaultFileReader = WindowsMmapFileReader;
    using AlternativeFileReader = WindowsFileReader;
#else
    // Fallback to portable stream reader
    using FileReader = StreamFileReader;
    using DefaultFileReader = StreamFileReader;
#endif

} // namespace tiff

#endif // TIFF_AUTO_READER

// RECOMMENDATIONS:
//
// For best performance (zero-copy, lock-free):
//   - Linux/macOS/Unix: Use MmapFileReader (mmap-based)
//   - Windows: Use WindowsMmapFileReader (CreateFileMapping-based)
//   - Multi-threaded: All mmap/pread/OVERLAPPED readers are lock-free
//   - In-memory data: Use BufferReader for zero-copy views
//
// For Windows with allocated buffers:
//   - Use WindowsFileReader (ReadFile with OVERLAPPED, lock-free but allocates)
//
// For portability:
//   - Use StreamFileReader - works everywhere but requires mutex locking
//
// For flexibility:
//   - Manually select readers based on runtime conditions or user configuration
//   - All readers implement the same RawReader concept and are interchangeable
//
// Example - Manual selection with runtime check:
//   #include "tiff/reader_unix_mmap.hpp"
//   #include "tiff/reader_stream.hpp"
//   
//   template<typename Reader>
//   void process_tiff(Reader& reader) { /* ... */ }
//   
//   #if defined(__linux__)
//   MmapFileReader reader("file.tif");
//   #else
//   StreamFileReader reader("file.tif");
//   #endif
//   process_tiff(reader);
