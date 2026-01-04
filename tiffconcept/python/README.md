# Python Bindings for tiffconcept

High-performance Python bindings for the tiffconcept TIFF reading and writing library.

## Features

- **Fast I/O**: Platform-optimized file readers (io_uring on Linux 5.1+, pread on Unix, async I/O on Windows)
- **Multi-threading**: Optional multi-threaded decompression for better performance
- **Free-threaded Python**: Full support for Python 3.13+ nogil mode
- **Automatic type hints**: pybind11 3.0+ provides runtime type information for IDEs
- **Flexible layouts**: Support for DHWC (TensorFlow), DCHW (PyTorch), and CDHW (planar) memory layouts
- **Lazy loading**: Pages are loaded on-demand and cached
- **Multiple pixel types**: uint8, uint16, uint32, uint64, int8, int16, int32, int64, float32, float64
- **Compression support**: None, ZSTD, JPEG-LS, PackBits, and more
- **Multi-page files**: Full support for multi-page TIFF files
- **Native enums**: Python-friendly enum.IntEnum integration
- **Type hints**: Automatic runtime type hints via pybind11 3.0+

## Requirements

- Python >= 3.12
- NumPy >= 1.26
- pybind11 >= 3.0 (bundled)

**Free-threaded Python (3.13+)**: The library is built with `py::mod_gil_not_used()` and automatically
detects and supports free-threaded Python builds (3.13t+). No special configuration needed!

## Installation

### From source

```bash
cd /path/to/tiffconcept
pip install .
```

### Development install

```bash
cd /path/to/tiffconcept
pip install -e . -v
```

## Quick Start

```python
import tiffconcept as tc
import numpy as np

# Open a TIFF file
with tc.open('image.tif') as tif:
    print(f"File has {len(tif)} pages")
    print(f"Format: {tif.format}, Endian: {tif.endian}")
    
    # Access first page
    page = tif[0]
    print(f"Shape: {page.width}x{page.height}x{page.depth}")
    print(f"Channels: {page.channels}, dtype: {page.dtype}")
    print(f"Compression: {page.compression}")
    
    # Read image data - returns ImageBuffer
    buffer = page.read(layout='DHWC', use_threading=True)
    
    # Convert to numpy array (zero-copy view)
    data = np.asarray(buffer)
    print(f"Data shape: {data.shape}, dtype: {data.dtype}")
    
    # Read 2D grayscale image with simplified layout
    if page.depth == 1 and page.channels == 1:
        buffer = page.read(layout='HW')  # shape: (H, W)
        img = np.asarray(buffer)
    
    # Read 2D RGB image
    if page.depth == 1:
        buffer = page.read(layout='HWC')  # shape: (H, W, C)
        img = np.asarray(buffer)
    
    # Read into pre-allocated buffer (OpenCV style)
    dst = np.empty(page.shape('DHWC'), dtype=page.dtype)
    result = page.read(dst=dst, layout='DHWC')
    assert result is dst  # Same object returned
    
    # Read a specific region
    region = tc.ImageRegion(
        start_x=0, start_y=0,
        width=512, height=512
    )
    roi_buffer = page.read(region=region, layout='HWC')
    roi = np.asarray(roi_buffer)
    
    # Iterate over all pages
    for i, page in enumerate(tif):
        shape = page.shape('DHWC')
        print(f"Page {i}: {shape}")
```

## Memory Layouts

The library supports multiple memory layouts for flexibility with different frameworks:

### Full 4D Layouts (for volumetric/3D data)

- **DHWC** (Depth, Height, Width, Channels): TensorFlow/Keras style, channels last
- **DCHW** (Depth, Channels, Height, Width): PyTorch style
- **CDHW** (Channels, Depth, Height, Width): Planar format

### Simplified 2D/3D Layouts

- **HW** (Height, Width): For 2D grayscale images (requires depth=1, channels=1)
- **HWC** (Height, Width, Channels): For 2D color images (requires depth=1)
- **DHW** (Depth, Height, Width): For 3D grayscale volumes (requires channels=1)
- **CHW** (Channels, Height, Width): For 2D images, channels first (requires depth=1)

**Note**: Layout constraints are enforced at runtime. For example, requesting 'HW' layout for a multi-channel image will raise a `ValueError`.

## Reading Images

### Basic Reading

```python
# Allocate new buffer
buffer = page.read(layout='DHWC')
data = np.asarray(buffer)  # Zero-copy view
```

### Pre-allocated Buffer (OpenCV style)

```python
# Allocate destination array
dst = np.empty(page.shape('HWC'), dtype=page.dtype)

# Read directly into dst
result = page.read(dst=dst, layout='HWC')
assert result is dst  # Returns the same object

# dst now contains the image data
```

### Layout-specific Examples

```python
# 2D grayscale (single slice, single channel)
if page.depth == 1 and page.channels == 1:
    gray = np.asarray(page.read(layout='HW'))  # shape: (H, W)

# 2D RGB
if page.depth == 1:
    rgb = np.asarray(page.read(layout='HWC'))  # shape: (H, W, 3)
    # or PyTorch style
    rgb_torch = np.asarray(page.read(layout='CHW'))  # shape: (3, H, W)

# 3D grayscale volume
if page.channels == 1:
    volume = np.asarray(page.read(layout='DHW'))  # shape: (D, H, W)

# 3D multi-channel (e.g., time series or Z-stack with RGB)
volume_rgb = np.asarray(page.read(layout='DHWC'))  # shape: (D, H, W, C)
```

### Region Reading

```python
# Read a specific region
region = tc.ImageRegion(
    start_x=100, start_y=100,
    width=256, height=256,
    start_z=0, depth=1,
    channel_start=0, num_channels=3
)

roi = np.asarray(page.read(region=region, layout='HWC'))
```

## Threading

The `use_threading` parameter controls multi-threaded decompression:

- `use_threading=True` (default): Uses multi-threaded reader for better performance
  - `CPULimitedReader` for sync file readers (pread, etc.)
  - `FastReader` with cloned reader for async readers (io_uring, Windows async)
- `use_threading=False`: Uses single-threaded `SimpleReader`

The GIL is released during I/O operations for better parallelism.

## API Reference

### Classes

- **TiffFile**: Multi-page TIFF file
  - `open(filepath: str) -> TiffFile`: Open a TIFF file (also available as module function `tc.open()`)
  - `__len__() -> int`: Get number of pages
  - `__getitem__(index: int) -> TiffPage`: Get page by index (supports negative indices)
  - `__iter__() -> Iterator[TiffPage]`: Iterate over pages
  - `pages() -> List[TiffPage]`: Get all pages as a list
  - `close()`: Close the file
  - `is_open: bool`: Check if file is open
  - `format: TiffFormatType`: TIFF format (Classic or BigTIFF)
  - `endian: str`: Byte order ('little' or 'big')

- **TiffPage**: Single page in a TIFF file
  - `width: int`: Image width in pixels
  - `height: int`: Image height in pixels
  - `depth: int`: Image depth (Z slices), 1 for 2D images
  - `channels: int`: Number of channels
  - `bits_per_sample: int`: Bits per sample (8, 16, 32, 64)
  - `dtype: str`: NumPy dtype string (e.g., 'uint8', 'float32')
  - `storage_layout: str`: Storage layout in file ('DHWC' or 'CDHW')
  - `compression: CompressionScheme`: Compression method
  - `photometric: PhotometricInterpretation`: Color space
  - `shape(layout: str = 'DHWC') -> tuple`: Get shape in specified layout
  - `read(*, region: Optional[ImageRegion] = None, dst: Optional[Buffer] = None, layout: str = 'DHWC', use_threading: bool = True) -> ImageBuffer | Buffer`: Read image data

- **ImageBuffer**: Buffer object implementing Python's buffer protocol
  - `shape: tuple`: Shape as tuple
  - `strides: tuple`: Strides in bytes
  - `dtype: str`: Data type string
  - `itemsize: int`: Size of each element in bytes
  - `ndim: int`: Number of dimensions
  - `size: int`: Total number of elements
  - `nbytes: int`: Total number of bytes

- **ImageRegion**: Region specification
  - `start_x: int`: Starting X coordinate
  - `start_y: int`: Starting Y coordinate
  - `width: int`: Width in pixels
  - `height: int`: Height in pixels
  - `start_z: int`: Starting Z coordinate (default: 0)
  - `depth: int`: Depth in slices (default: 1)
  - `channel_start: int`: Starting channel (default: 0)
  - `num_channels: int`: Number of channels (default: all)

### Enums

- **CompressionScheme**: Compression methods (None, ZSTD, JPEG_LS, etc.)
- **SampleFormat**: Data format (UnsignedInt, SignedInt, IEEEFloat)
- **ImageLayout**: Memory layout (DHWC, DCHW, CDHW)
- **PhotometricInterpretation**: Color space
- **Predictor**: Compression predictor
- **PlanarConfiguration**: Chunky or Planar

### Exceptions

- **TiffError**: Base exception
- **FileNotFoundError**: File not found
- **ReadError**: Read error
- **InvalidFormatError**: Invalid TIFF format
- **UnsupportedFeatureError**: Unsupported feature
- **OutOfBoundsError**: Index out of bounds

## Testing

```bash
# Install test dependencies
pip install pytest pytest-cov

# Run tests
pytest tiffconcept/python/tests/

# With coverage
pytest tiffconcept/python/tests/ --cov=tiffconcept --cov-report=html
```

## Building for Multiple Python Versions

The package supports Python 3.8+ including free-threaded Python 3.14+.

```bash
# Build for specific Python version
python3.11 -m pip install .

# Build wheel
pip install build
python -m build
```

## Platform Support

- **Linux**: io_uring (5.1+), pread
- **macOS**: pread
- **Windows**: Async I/O, synchronous fallback

## Performance Tips

1. Use `use_threading=True` for large images
2. Choose layout matching your downstream library:
   - DCHW for PyTorch
   - DHWC for TensorFlow
   - HWC for most 2D image processing
   - CHW for PyTorch 2D images
3. Use context managers to ensure files are closed properly
4. For batch processing, reuse TiffFile object across multiple reads
5. Use pre-allocated buffers with `dst` parameter to avoid allocations in loops
6. For 2D images, use simplified layouts (HW, HWC, CHW) for cleaner code

## Example: Batch Processing

```python
import tiffconcept as tc
import numpy as np

with tc.open('stack.tif') as tif:
    # Pre-allocate output array for all pages
    first_page = tif[0]
    batch = np.empty(
        (len(tif), *first_page.shape('HWC')),
        dtype=first_page.dtype
    )
    
    # Read all pages into pre-allocated array
    for i, page in enumerate(tif):
        page.read(dst=batch[i], layout='HWC')
    
    # Process batch...
    print(f"Loaded batch: {batch.shape}")
```

## License

MIT License (same as parent tiffconcept project)
