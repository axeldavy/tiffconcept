# TiffConcept Benchmarks

This directory contains comprehensive performance benchmarks for the TiffConcept library, comparing against libtiff where available.

## Building

```bash
cd benchmarks
mkdir build && cd build
cmake ..
make -j
```

## Running

Run all benchmarks:
```bash
./tiff_benchmarks
```

Run specific benchmarks with filters:
```bash
./tiff_benchmarks --benchmark_filter=Write
./tiff_benchmarks --benchmark_filter=Read
./tiff_benchmarks --benchmark_filter=TiffConcept
./tiff_benchmarks --benchmark_filter=LibTIFF
```

Output formats:
```bash
# JSON output
./tiff_benchmarks --benchmark_format=json --benchmark_out=results.json

# CSV output
./tiff_benchmarks --benchmark_format=csv --benchmark_out=results.csv

# Console output with color
./tiff_benchmarks --benchmark_color=true
```

Control iterations:
```bash
# Run each benchmark for 5 seconds minimum
./tiff_benchmarks --benchmark_min_time=5.0

# Run exactly 100 iterations
./tiff_benchmarks --benchmark_repetitions=100
```

## Standalone Directory Benchmark

A specialized tool for benchmarking reading multiple TIFF files from a directory without cache effects:

```bash
./benchmark_read_directory <directory> <reader> [tile_size]
```

**Arguments:**
- `directory`: Path to directory containing TIFF files
- `reader`: One of: `simple`, `io`, `cpu`, `libtiff`
  - `simple`: SimpleReader (single-threaded, sequential)
  - `io`: IOLimitedReader (optimized for high-latency I/O)
  - `cpu`: CPULimitedReader (optimized for CPU-bound decompression)
  - `libtiff`: Reference implementation for comparison
- `tile_size`: Optional. Size of square tile to read from origin. If omitted, reads full image.

**Examples:**
```bash
# Read full images with CPULimitedReader
./benchmark_read_directory /data/images cpu

# Read 512x512 tiles with SimpleReader  
./benchmark_read_directory /data/images simple 512

# Compare with libtiff (full images)
./benchmark_read_directory /data/images libtiff

# Benchmark partial reads with IOLimitedReader
./benchmark_read_directory /path/to/large/images io 1024
```

**Features:**
- Processes all TIFF files in the directory once to avoid cache effects
- Automatically handles different pixel types (uint8, uint16, int16, float, etc.)
- Automatically detects endianness (little/big endian)
- Supports both tiled and stripped images
- Displays comprehensive statistics: mean, median, stddev, min, max, throughput
- Only reads first page of multi-page TIFFs

**Output:**
The tool provides detailed statistics including:
- Per-file timing (mean, median, standard deviation, min, max)
- Total throughput in MB/s
- Success/failure counts

## System Requirements

- C++20 compiler
- CMake 3.14+
- Google Benchmark 1.8.3+ (automatically downloaded)
- libtiff (optional, for comparison benchmarks)
- ~500MB disk space for temporary test files
