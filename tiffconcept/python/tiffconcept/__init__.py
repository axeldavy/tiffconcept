"""
tiffconcept - High-performance TIFF reading and writing library

This library provides fast reading and writing of TIFF files with support for:
- Multi-page TIFF files
- Various compression schemes (ZSTD, JPEG-LS, etc.)
- Multiple pixel types (uint8, uint16, uint32, int16, float32, float64, etc.)
- 2D and 3D images
- Flexible memory layouts (DHWC, DCHW, CDHW)
- Optional multi-threading for I/O operations

Basic usage:
    >>> import tiffconcept as tc
    >>> with tc.TiffFile('image.tif') as tif:
    ...     print(f"Pages: {len(tif)}")
    ...     page = tif[0]
    ...     print(f"Shape: {page.width}x{page.height}")
    ...     data = page.read(layout='DHWC')
"""

# Import the compiled extension
from ._tiffconcept_core import (
    # Enums
    CompressionScheme,
    SampleFormat,
    PhotometricInterpretation,
    Predictor,
    PlanarConfiguration,
    ImageLayout,
    
    # Classes
    ImageShape,
    ImageRegion,
    ImageBuffer,
    TiffPage,
    TiffFile,
    
    # Exceptions
    TiffError,
    FileNotFoundError as TiffFileNotFoundError,
    ReadError,
    InvalidFormatError,
    UnsupportedFeatureError,
    
    # Version info
    __version__,
)

__all__ = [
    # Enums
    'CompressionScheme',
    'SampleFormat',
    'PhotometricInterpretation',
    'Predictor',
    'PlanarConfiguration',
    'ImageLayout',
    
    # Classes
    'ImageShape',
    'ImageRegion',
    'ImageBuffer',
    'TiffPage',
    'TiffFile',
    
    # Exceptions
    'TiffError',
    'TiffFileNotFoundError',
    'ReadError',
    'InvalidFormatError',
    'UnsupportedFeatureError',
    
    # Convenience functions
    'open',
]


def open(filepath: str) -> TiffFile:
    """
    Open a TIFF file for reading.
    
    This is a convenience function that creates a TiffFile object. The file
    should be closed when done, either by calling close() or by using a
    context manager.
    
    Args:
        filepath: Path to the TIFF file to open
        
    Returns:
        TiffFile object representing the opened file
        
    Raises:
        TiffFileNotFoundError: If the file does not exist
        InvalidFormatError: If the file is not a valid TIFF file
        ReadError: If there is an error reading the file
        
    Examples:
        >>> # Using context manager (recommended)
        >>> with tc.open('image.tif') as tif:
        ...     data = tif[0].read()
        
        >>> # Manual close
        >>> tif = tc.open('image.tif')
        >>> try:
        ...     data = tif[0].read()
        ... finally:
        ...     tif.close()
    """
    return TiffFile(filepath)
