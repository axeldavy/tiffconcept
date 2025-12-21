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
## System Requirements

- C++20 compiler
- CMake 3.14+
- Google Benchmark 1.8.3+ (automatically downloaded)
- libtiff (optional, for comparison benchmarks)
- ~500MB disk space for temporary test files
