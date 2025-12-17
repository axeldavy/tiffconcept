# A Modern C++ TIFF Reader Library

A header-only TIFF reading library using C++20 features

## What is a TIFF file ?

A TIFF (Tagged Image File Format) file is a flexible and adaptable file format for storing images and associated metadata.

Essentially it is composed of:
- A small header indicating endianness and offset to the first Image File Directory (IFD)
- One or more IFDs, each containing a set of tags (metadata) and pointers to the actual image data
- Image data, which can be stored in various formats and compressions

To describe it differently, a tiff can contain one or multiple images (pages). Each image is always associated with an IFD.
An IFD is a small array of tags, complemented with extra arrays for large tag values.

The concept of reading a TIFF file can thus be broken down into:
- Reading the header to determine endianness and locate the first IFD
- Iterating through IFDs, in order to select the desired image/page
- Fetching the tag array from the IFD
- Extracting from the tag array and the extra arrays only the tags you care about (for instance the minimum set to read the image data)
- From the extracted information (size, etc), select the image region you want to read
- Read the target region from the image data, decompressing and decoding it as needed

The TIFF format is very flexible, and for instance allows:
- multichannel images
- images stored in strips or tiles
- images stored as chains (for instances pages of a physical document scanner), or as pyramids (multiple resolutions of the same image)
- Storing all kinds of metadata (EXIF, XMP, custom tags, etc)

As a result, to support most use-cases, the library decomposes the concept of reading a TIFF file into these separate steps.

Higher level helpers are provided for simple use-cases (read the main image, read a page, etc).

Similarly, for writing a TIFF file, the separate steps (sub-concepts) are:
- Write the header
- Edit an existing tag array
- Reserve space for one or more tag arrays (unlike some libraries, for cache locality reasons, this library encourages to write tag arrays at the beginning of the file)
- Write image data, compressing and encoding it as needed
- Write IFDs (tag array), its extra arrays and update its pointers to image data
- Edit IFD chains and pyramids

## Why yet another TIFF reading library ?

This project started by the realization that reading a TIFF file when you know exactly what you expect and want is fast and can be done
with straightforward code. However reading any TIFF file is complex:
- The file can be big or little endian
- Many element sizes change between classic TIFF and BigTIFF
- Some tags have default values or can be skipped under some conditions
- Some tags can be single values or arrays
- Many compression schemes
- Images can be organized in strips or tiles

If you need to support all possible use-cases, you end up with a lot of code and complexity. And potentially overhead.
If this is what you need, use a general purpose library like LibTIFF.

However, if you can define at compile time the subset you want to support, or the subset of tags you care about, this
template based library can generate optimized code for your use-case.

For instance this library wishes to allow:
- Quickly read a subset of tags
- Direct use of your custom interface for reading/writing the file (memory mapped file, custom IO, etc). Default file/buffer readers are provided.
- Reading entire images, portions of images, subpages, 3D images, etc.
- Specify at compile time the pixel type you want to read (uint8_t, uint16_t, etc)
- Add your tag specifications easily
- Minimal reallocations and copies
- Tune reading patterns
- Thread-safe by design (unless explicitly documented otherwise)
- Since no one wants to handle an unexpected exception at runtime, no exceptions are used. Instead a `Result<T>` monadic type is used for error handling.

## Why C++ 20

While this library could be made compatible with older C++ standards, this library
makes extensive use of C++20 concepts to ensure strong typing and correctness at compile time.

## Why monads

This library uses a `Result<T>` type for error handling instead of exceptions.
The main issue with exceptions, besides performance, is the lack of explicitness over
which exceptions can be thrown and when.

Since this library relies on templated code and compile-time generation of structures,
the lower cost of monads enables quickly changing paths if an error occurs without
the overhead of stack unwinding.

## The library doesn't support XXXXX

Supporting all possible TIFF features is a lot of work and testing.

Currently the library only supports a subset of compression schemes for instance.

If your use-case is generic and not supported, you can propose a contribution to add it, or use a more general purpose library like LibTIFF.

## Contributions

Contributions are welcome!

The library's code design goals are:
- Modern C++ idioms
- Generic and extensible design
- Strong typing with concepts
- Avoiding reallocations
- Specialized code generation must avoid unnecessary overhead
- Readability and maintainability (sadly handling all TIFF cases can lead to complex code)

## Use of AI

The library code has been written with the help of AI tools like GitHub Copilot.
While I have taken an interest in recent C++20 features, and I have writen C and C++ code
in the past, I am mainly a Python/Cython developer. I have used AI tools to help me
write better C++20 code, and to speed up the development process. The code has seen many iterations, and
has been read and modified and read with scrutiny multiple times to ensure correctness and quality.

## My background with TIFF

I have worked with TIFF files for many years, mainly with Python. TIFF support (coverage, speed) has always been a hot topic
in my labs.
My disatisfaction with the existing libraries led me to write a custom TIFF reader. Initially I attempted to modify tifffile (`https://github.com/cgohlke/tifffile`), but it was not possible to avoid the many allocations and parsing without changing the whole design. I then wrote a custom Cython-based TIFF reader, and discovered it was much simpler and quite straightforward to read TIFF files when you know what you want.

The library has been for me both a way to experiment with modern C++ features, and to extend the speed of my custom TIFF reader to more use-cases.

## Testing and Benchmarks

The library includes comprehensive test coverage using Google Test and performance benchmarks using Google Benchmark.

### Running Tests
```bash
cd tests
mkdir build && cd build
cmake ..
make -j
ctest
```

### Running Benchmarks
```bash
cd benchmarks
mkdir build && cd build
cmake ..
make -j
./tiff_benchmarks
```

See `benchmarks/README.md` for detailed information about available benchmarks and performance comparisons with libtiff.

## Roadmap

- [Mostly done] BigTIFF, Endianness, parsing metadata
- [Mostly done] Reading a tile/strip, raw or with ZSTD decompression, predictor decoding, 2D/3D, single channel or multi-channel (any number of channels)
- [Done] Writing TIFF files, with various schemes for ordering IFDs (packing page information at the beginning may be more cache efficient when walking through TIFF pages)
- [Done] Reading an image region efficiently, single-thread vs multi-thread
- [Done] Comprehensive test suite and benchmarks

- [Not planned] support for non-standard integer and floating points (Palette images, 16-bit floats, etc)
- [Not planned] support for compression schemes other than ZSTD

Why not planned ? Because I intended this library primarily to fit my use-cases, but put a lot of work to make it
reusable and extensible for others. Once I am satisfied with my use-cases, I will accept and review contributions, but that's it.

Why ZSTD ? With horizontal differencing predictor and planar channels, ZSTD gives very good compression ratios and speed for most cases in image processing I have met.
If you use other compression schemes, you can:
- Implement the library interfaces for the compression schemes you need. Potentially contribute them back.
- Use another library like LibTIFF. If you still need to walk through TIFF files and metadata very fast, you can use this library for metadata extraction and LibTIFF for image data extraction.

## License

This library is provided under the MIT license. See the LICENSE file for details.