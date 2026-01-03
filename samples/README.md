# Recode Sample

This sample demonstrates how to read a TIFF file and write it in 4 different variants:

1. **Uncompressed** - No compression
2. **ZSTD** - ZSTD compression only
3. **ZSTD + Horizontal predictor** - ZSTD with horizontal differencing predictor
4. **ZSTD + LOCO-I predictor** - ZSTD with LOCO-I predictor for better compression

Each variant is timed and the duration is printed.

## Building

```bash
cd samples
mkdir build
cd build
cmake ..
make
```

## Usage

```bash
./recode <input_tiff> <output_dir> <tile_width> <tile_height>
```

### Arguments

- `input_tiff`: Path to the source TIFF file to recode
- `output_dir`: Directory where output files will be written
- `tile_width`: Width of tiles in pixels (e.g., 512)
- `tile_height`: Height of tiles in pixels (e.g., 512)

### Example

```bash
./recode /path/to/image.tif output/ 512 512
```

This will create 4 files in the output directory:
- `image_uncompressed.tif`
- `image_zstd.tif`
- `image_zstd_horizontal.tif`
- `image_zstd_loco.tif`

Each file will have the timing printed during creation.

## Output

The tool will print:
- Image dimensions and metadata
- Progress for each variant being written
- Time taken for each write operation in milliseconds

Example output:
```
Reading input TIFF: test.tif
Image: 2048x2048x1, 3 channels, 8 bits/sample
Tile size: 512x512

Writing uncompressed to output/test_uncompressed.tif... done in 45 ms
Writing zstd to output/test_zstd.tif... done in 123 ms
Writing zstd_horizontal to output/test_zstd_horizontal.tif... done in 98 ms
Writing zstd_loco to output/test_zstd_loco.tif... done in 87 ms

All variants created successfully!
```
