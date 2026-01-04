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
from __future__ import annotations
import numpy as np
from tiffconcept._tiffconcept_core import CompressionScheme
from tiffconcept._tiffconcept_core import FileNotFoundError as TiffFileNotFoundError
from tiffconcept._tiffconcept_core import ImageBuffer
from tiffconcept._tiffconcept_core import ImageLayout
from tiffconcept._tiffconcept_core import ImageRegion
from tiffconcept._tiffconcept_core import ImageShape
from tiffconcept._tiffconcept_core import InvalidFormatError
from tiffconcept._tiffconcept_core import PhotometricInterpretation
from tiffconcept._tiffconcept_core import PlanarConfiguration
from tiffconcept._tiffconcept_core import Predictor
from tiffconcept._tiffconcept_core import ReadError
from tiffconcept._tiffconcept_core import SampleFormat
from tiffconcept._tiffconcept_core import TiffError
from tiffconcept._tiffconcept_core import TiffFile
from tiffconcept._tiffconcept_core import TiffPage
from tiffconcept._tiffconcept_core import UnsupportedFeatureError
from . import _tiffconcept_core
__all__: list = ['CompressionScheme', 'SampleFormat', 'PhotometricInterpretation', 'Predictor', 'PlanarConfiguration', 'ImageLayout', 'ImageShape', 'ImageRegion', 'ImageBuffer', 'TiffPage', 'TiffFile', 'TiffError', 'TiffFileNotFoundError', 'ReadError', 'InvalidFormatError', 'UnsupportedFeatureError', 'open']
def open(filepath: str) -> _tiffconcept_core.TiffFile:
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
__version__: str = '0.1.0'
