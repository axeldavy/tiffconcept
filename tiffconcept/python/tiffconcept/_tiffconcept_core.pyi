"""
Core C++ extension for tiffconcept - high-performance TIFF I/O library
"""
from __future__ import annotations
import typing
__all__: list[str] = ['BigTIFF', 'CCITT1D', 'CDHW', 'CIELab', 'CMYK', 'Chunky', 'Classic', 'CompressionScheme', 'DCHW', 'DHWC', 'Deflate', 'Deflate_Adobe', 'FileNotFoundError', 'FloatingPoint', 'Group3Fax', 'Group4Fax', 'Horizontal', 'IEEEFloat', 'ImageBuffer', 'ImageLayout', 'ImageRegion', 'ImageShape', 'InvalidFormatError', 'JPEG', 'JPEG_LS', 'JPEG_Old', 'LOCO_I', 'LZW', 'Mask', 'MinIsBlack', 'MinIsWhite', 'NONE', 'OutOfBoundsError', 'PackBits', 'Palette', 'PhotometricInterpretation', 'Planar', 'PlanarConfiguration', 'Predictor', 'RGB', 'ReadError', 'SampleFormat', 'SignedInt', 'TiffError', 'TiffFile', 'TiffFileIterator', 'TiffFormatType', 'TiffPage', 'Undefined', 'UnsignedInt', 'UnsupportedFeatureError', 'WriteError', 'YCbCr', 'ZSTD', 'ZSTD_Alt']
class CompressionScheme:
    """
    TIFF compression schemes.\\n\\nSpecifies the compression algorithm used for image data.
    
    Members:
    
      NONE : No compression (raw data)
    
      CCITT1D : CCITT Group 3 1-Dimensional Modified Huffman run-length encoding
    
      Group3Fax : CCITT T.4 bi-level encoding (Group 3 Fax)
    
      Group4Fax : CCITT T.6 bi-level encoding (Group 4 Fax)
    
      LZW : Lempel-Ziv-Welch compression
    
      JPEG_Old : JPEG compression (old-style)
    
      JPEG : JPEG compression
    
      Deflate_Adobe : Deflate compression (Adobe variant)
    
      Deflate : Deflate compression (zlib)
    
      PackBits : PackBits compression (simple run-length encoding)
    
      JPEG_LS : JPEG-LS lossless/near-lossless compression
    
      ZSTD : Zstandard compression (primary code)
    
      ZSTD_Alt : Zstandard compression (alternate code)
    """
    CCITT1D: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.CCITT1D: 2>
    Deflate: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.Deflate: 32946>
    Deflate_Adobe: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.Deflate_Adobe: 8>
    Group3Fax: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.Group3Fax: 3>
    Group4Fax: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.Group4Fax: 4>
    JPEG: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.JPEG: 7>
    JPEG_LS: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.JPEG_LS: 34670>
    JPEG_Old: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.JPEG_Old: 6>
    LZW: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.LZW: 5>
    NONE: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.NONE: 1>
    PackBits: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.PackBits: 32773>
    ZSTD: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.ZSTD: 50000>
    ZSTD_Alt: typing.ClassVar[CompressionScheme]  # value = <CompressionScheme.ZSTD_Alt: 34926>
    __members__: typing.ClassVar[dict[str, CompressionScheme]]  # value = {'NONE': <CompressionScheme.NONE: 1>, 'CCITT1D': <CompressionScheme.CCITT1D: 2>, 'Group3Fax': <CompressionScheme.Group3Fax: 3>, 'Group4Fax': <CompressionScheme.Group4Fax: 4>, 'LZW': <CompressionScheme.LZW: 5>, 'JPEG_Old': <CompressionScheme.JPEG_Old: 6>, 'JPEG': <CompressionScheme.JPEG: 7>, 'Deflate_Adobe': <CompressionScheme.Deflate_Adobe: 8>, 'Deflate': <CompressionScheme.Deflate: 32946>, 'PackBits': <CompressionScheme.PackBits: 32773>, 'JPEG_LS': <CompressionScheme.JPEG_LS: 34670>, 'ZSTD': <CompressionScheme.ZSTD: 50000>, 'ZSTD_Alt': <CompressionScheme.ZSTD_Alt: 34926>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __ge__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __gt__(self, other: typing.Any) -> bool:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __le__(self, other: typing.Any) -> bool:
        ...
    def __lt__(self, other: typing.Any) -> bool:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class FileNotFoundError(FileNotFoundError):
    pass
class ImageBuffer:
    """
    Buffer object implementing Python's buffer protocol.
    
    This object can be passed to numpy.asarray() or any other function
    that accepts the buffer protocol to create a zero-copy view of the data.
    """
    def __buffer__(self, flags):
        """
        Return a buffer object that exposes the underlying memory of the object.
        """
    def __release_buffer__(self, buffer):
        """
        Release the buffer object that exposes the underlying memory of the object.
        """
    def __repr__(self) -> str:
        ...
    @property
    def dtype(self) -> str:
        """
        Data type string (e.g., 'uint8', 'float32')
        """
    @property
    def itemsize(self) -> int:
        """
        Size of each element in bytes
        """
    @property
    def nbytes(self) -> int:
        """
        Total number of bytes
        """
    @property
    def ndim(self) -> int:
        """
        Number of dimensions
        """
    @property
    def shape(self) -> list[int]:
        """
        Shape of the buffer as a tuple
        """
    @property
    def size(self) -> int:
        """
        Total number of elements
        """
    @property
    def strides(self) -> list[int]:
        """
        Strides of the buffer in bytes
        """
class ImageLayout:
    """
    Memory layout for image data.
    
    Specifies the order of dimensions in the output numpy array.
      - DHWC: Depth, Height, Width, Channels (TensorFlow/Keras style)
      - DCHW: Depth, Channels, Height, Width (PyTorch style)
      - CDHW: Channels, Depth, Height, Width (planar)
    
    Members:
    
      DHWC : Depth, Height, Width, Channels (channels last)
    
      DCHW : Depth, Channels, Height, Width
    
      CDHW : Channels, Depth, Height, Width (planar)
    """
    CDHW: typing.ClassVar[ImageLayout]  # value = <ImageLayout.CDHW: 2>
    DCHW: typing.ClassVar[ImageLayout]  # value = <ImageLayout.DCHW: 1>
    DHWC: typing.ClassVar[ImageLayout]  # value = <ImageLayout.DHWC: 0>
    __members__: typing.ClassVar[dict[str, ImageLayout]]  # value = {'DHWC': <ImageLayout.DHWC: 0>, 'DCHW': <ImageLayout.DCHW: 1>, 'CDHW': <ImageLayout.CDHW: 2>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __ge__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __gt__(self, other: typing.Any) -> bool:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __le__(self, other: typing.Any) -> bool:
        ...
    def __lt__(self, other: typing.Any) -> bool:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class ImageRegion:
    """
    Specifies a rectangular region within a TIFF image.
    
    All coordinates are zero-based. The region is specified as [start, start+size)
    for each dimension.
    """
    def __init__(self, start_channel: typing.SupportsInt = 0, start_z: typing.SupportsInt = 0, start_y: typing.SupportsInt = 0, start_x: typing.SupportsInt = 0, num_channels: typing.SupportsInt = 1, depth: typing.SupportsInt = 1, height: typing.SupportsInt = 0, width: typing.SupportsInt = 0) -> None:
        """
        Create an image region.
        
        Args:
            start_channel: Starting channel index
            start_z: Starting Z (depth) coordinate
            start_y: Starting Y (row) coordinate
            start_x: Starting X (column) coordinate
            num_channels: Number of channels to read
            depth: Number of Z slices to read
            height: Number of rows to read
            width: Number of columns to read
        """
    def __repr__(self) -> str:
        ...
    def end_channel(self) -> int:
        """
        Get the exclusive end channel index.
        
        Returns:
            End channel index
        """
    def end_x(self) -> int:
        """
        Get the exclusive end X coordinate.
        
        Returns:
            End X coordinate
        """
    def end_y(self) -> int:
        """
        Get the exclusive end Y coordinate.
        
        Returns:
            End Y coordinate
        """
    def end_z(self) -> int:
        """
        Get the exclusive end Z coordinate.
        
        Returns:
            End Z coordinate
        """
    def is_empty(self) -> bool:
        """
        Check if the region is empty.
        
        Returns:
            True if any dimension is zero
        """
    def num_pixels(self) -> int:
        """
        Get the total number of pixels in the region.
        
        Returns:
            width * height * depth
        """
    def num_samples(self) -> int:
        """
        Get the total number of samples in the region.
        
        Returns:
            width * height * depth * num_channels
        """
    @property
    def depth(self) -> int:
        """
        Depth (number of Z slices)
        """
    @depth.setter
    def depth(self, arg0: typing.SupportsInt) -> None:
        ...
    @property
    def height(self) -> int:
        """
        Height (number of rows)
        """
    @height.setter
    def height(self, arg0: typing.SupportsInt) -> None:
        ...
    @property
    def num_channels(self) -> int:
        """
        Number of channels
        """
    @num_channels.setter
    def num_channels(self, arg0: typing.SupportsInt) -> None:
        ...
    @property
    def start_channel(self) -> int:
        """
        Starting channel index
        """
    @start_channel.setter
    def start_channel(self, arg0: typing.SupportsInt) -> None:
        ...
    @property
    def start_x(self) -> int:
        """
        Starting X (column) coordinate
        """
    @start_x.setter
    def start_x(self, arg0: typing.SupportsInt) -> None:
        ...
    @property
    def start_y(self) -> int:
        """
        Starting Y (row) coordinate
        """
    @start_y.setter
    def start_y(self, arg0: typing.SupportsInt) -> None:
        ...
    @property
    def start_z(self) -> int:
        """
        Starting Z (depth) coordinate
        """
    @start_z.setter
    def start_z(self, arg0: typing.SupportsInt) -> None:
        ...
    @property
    def width(self) -> int:
        """
        Width (number of columns)
        """
    @width.setter
    def width(self, arg0: typing.SupportsInt) -> None:
        ...
class ImageShape:
    """
    Describes the shape and format of a TIFF image.
    
    Contains metadata extracted from TIFF tags including dimensions, pixel format,
    and data organization.
    """
    def __init__(self) -> None:
        """
        Create an empty ImageShape
        """
    def __repr__(self) -> str:
        ...
    def full_region(self) -> ImageRegion:
        """
        Get a region covering the entire image.
        
        Returns:
            ImageRegion covering the full image
        """
    def is_3d(self) -> bool:
        """
        Check if the image is 3D.
        
        Returns:
            True if depth > 1
        """
    def is_multi_channel(self) -> bool:
        """
        Check if the image has multiple channels.
        
        Returns:
            True if samples_per_pixel > 1
        """
    def is_planar(self) -> bool:
        """
        Check if the image uses planar configuration.
        
        Returns:
            True if planar_configuration is Planar
        """
    def num_elements(self) -> int:
        """
        Get the total number of elements.
        
        Returns:
            width * height * depth * samples_per_pixel
        """
    def num_pixels(self) -> int:
        """
        Get the total number of pixels.
        
        Returns:
            width * height * depth
        """
    @property
    def bits_per_sample(self) -> int:
        """
        Number of bits per sample (8, 16, 32, 64)
        """
    @property
    def depth(self) -> int:
        """
        Image depth (Z slices). 1 for 2D images
        """
    @property
    def height(self) -> int:
        """
        Image height in pixels
        """
    @property
    def planar_configuration(self) -> PlanarConfiguration:
        """
        Planar configuration (Chunky or Planar)
        """
    @property
    def sample_format(self) -> SampleFormat:
        """
        Sample data format (UnsignedInt, SignedInt, IEEEFloat)
        """
    @property
    def samples_per_pixel(self) -> int:
        """
        Number of samples (channels) per pixel
        """
    @property
    def width(self) -> int:
        """
        Image width in pixels
        """
class InvalidFormatError(ValueError):
    pass
class OutOfBoundsError(IndexError):
    pass
class PhotometricInterpretation:
    """
    Photometric interpretation.
    
    Specifies the color space of the image data.
    
    Members:
    
      MinIsWhite : Minimum value is white (grayscale)
    
      MinIsBlack : Minimum value is black (grayscale)
    
      RGB : RGB color model
    
      Palette : Indexed color (palette/color map)
    
      Mask : Transparency mask
    
      CMYK : CMYK color model
    
      YCbCr : YCbCr color model
    
      CIELab : CIE L*a*b* color model
    """
    CIELab: typing.ClassVar[PhotometricInterpretation]  # value = <PhotometricInterpretation.CIELab: 8>
    CMYK: typing.ClassVar[PhotometricInterpretation]  # value = <PhotometricInterpretation.CMYK: 5>
    Mask: typing.ClassVar[PhotometricInterpretation]  # value = <PhotometricInterpretation.Mask: 4>
    MinIsBlack: typing.ClassVar[PhotometricInterpretation]  # value = <PhotometricInterpretation.MinIsBlack: 1>
    MinIsWhite: typing.ClassVar[PhotometricInterpretation]  # value = <PhotometricInterpretation.MinIsWhite: 0>
    Palette: typing.ClassVar[PhotometricInterpretation]  # value = <PhotometricInterpretation.Palette: 3>
    RGB: typing.ClassVar[PhotometricInterpretation]  # value = <PhotometricInterpretation.RGB: 2>
    YCbCr: typing.ClassVar[PhotometricInterpretation]  # value = <PhotometricInterpretation.YCbCr: 6>
    __members__: typing.ClassVar[dict[str, PhotometricInterpretation]]  # value = {'MinIsWhite': <PhotometricInterpretation.MinIsWhite: 0>, 'MinIsBlack': <PhotometricInterpretation.MinIsBlack: 1>, 'RGB': <PhotometricInterpretation.RGB: 2>, 'Palette': <PhotometricInterpretation.Palette: 3>, 'Mask': <PhotometricInterpretation.Mask: 4>, 'CMYK': <PhotometricInterpretation.CMYK: 5>, 'YCbCr': <PhotometricInterpretation.YCbCr: 6>, 'CIELab': <PhotometricInterpretation.CIELab: 8>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __ge__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __gt__(self, other: typing.Any) -> bool:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __le__(self, other: typing.Any) -> bool:
        ...
    def __lt__(self, other: typing.Any) -> bool:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class PlanarConfiguration:
    """
    Planar configuration for multi-channel images.
    
    Specifies how the components of each pixel are stored.
    
    Members:
    
      Chunky : Chunky format (RGBRGBRGB...)
    
      Planar : Planar format (RRR...GGG...BBB...)
    """
    Chunky: typing.ClassVar[PlanarConfiguration]  # value = <PlanarConfiguration.Chunky: 1>
    Planar: typing.ClassVar[PlanarConfiguration]  # value = <PlanarConfiguration.Planar: 2>
    __members__: typing.ClassVar[dict[str, PlanarConfiguration]]  # value = {'Chunky': <PlanarConfiguration.Chunky: 1>, 'Planar': <PlanarConfiguration.Planar: 2>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __ge__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __gt__(self, other: typing.Any) -> bool:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __le__(self, other: typing.Any) -> bool:
        ...
    def __lt__(self, other: typing.Any) -> bool:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class Predictor:
    """
    Predictor for compression.
    
    Specifies a mathematical operator applied before compression to improve compression ratios.
    
    Members:
    
      NONE : No predictor
    
      Horizontal : Horizontal differencing (predict from left pixel)
    
      FloatingPoint : Floating point horizontal differencing
    
      LOCO_I : LOCO-I predictor (used with JPEG-LS)
    """
    FloatingPoint: typing.ClassVar[Predictor]  # value = <Predictor.FloatingPoint: 3>
    Horizontal: typing.ClassVar[Predictor]  # value = <Predictor.Horizontal: 2>
    LOCO_I: typing.ClassVar[Predictor]  # value = <Predictor.LOCO_I: 8192>
    NONE: typing.ClassVar[Predictor]  # value = <Predictor.NONE: 1>
    __members__: typing.ClassVar[dict[str, Predictor]]  # value = {'NONE': <Predictor.NONE: 1>, 'Horizontal': <Predictor.Horizontal: 2>, 'FloatingPoint': <Predictor.FloatingPoint: 3>, 'LOCO_I': <Predictor.LOCO_I: 8192>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __ge__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __gt__(self, other: typing.Any) -> bool:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __le__(self, other: typing.Any) -> bool:
        ...
    def __lt__(self, other: typing.Any) -> bool:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class ReadError(OSError):
    pass
class SampleFormat:
    """
    Sample data format.
    
    Specifies how pixel sample values should be interpreted.
    
    Members:
    
      UnsignedInt : Unsigned integer data
    
      SignedInt : Signed (two's complement) integer data
    
      IEEEFloat : IEEE floating point data
    
      Undefined : Undefined data format
    """
    IEEEFloat: typing.ClassVar[SampleFormat]  # value = <SampleFormat.IEEEFloat: 3>
    SignedInt: typing.ClassVar[SampleFormat]  # value = <SampleFormat.SignedInt: 2>
    Undefined: typing.ClassVar[SampleFormat]  # value = <SampleFormat.Undefined: 4>
    UnsignedInt: typing.ClassVar[SampleFormat]  # value = <SampleFormat.UnsignedInt: 1>
    __members__: typing.ClassVar[dict[str, SampleFormat]]  # value = {'UnsignedInt': <SampleFormat.UnsignedInt: 1>, 'SignedInt': <SampleFormat.SignedInt: 2>, 'IEEEFloat': <SampleFormat.IEEEFloat: 3>, 'Undefined': <SampleFormat.Undefined: 4>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __ge__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __gt__(self, other: typing.Any) -> bool:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __le__(self, other: typing.Any) -> bool:
        ...
    def __lt__(self, other: typing.Any) -> bool:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class TiffError(RuntimeError):
    pass
class TiffFile:
    """
    Represents a multi-page TIFF file.
    
    Provides sequence-like access to pages and supports context manager protocol.
    Pages are lazily loaded - metadata is read on access, image data on read().
    
    Examples:
        >>> with TiffFile('image.tif') as tif:
        ...     print(f'Pages: {len(tif)}')
        ...     data = tif[0].read()
    
        >>> tif = TiffFile('image.tif')
        >>> for page in tif:
        ...     print(page.shape())
        >>> tif.close()
    """
    def __enter__(self) -> TiffFile:
        """
        Enter context manager
        """
    def __exit__(self, arg0: typing.Any, arg1: typing.Any, arg2: typing.Any) -> None:
        """
        Exit context manager
        """
    def __getitem__(self, index: typing.SupportsInt) -> TiffPage:
        """
        Get a page by index.
        
        Supports negative indices (e.g., tif[-1] for last page).
        
        Args:
            index: Page index (zero-based)
        
        Returns:
            TiffPage object
        
        Raises:
            IndexError: If index is out of range
        """
    def __init__(self, filepath: str) -> None:
        """
        Open a TIFF file.
        
        Args:
            filepath: Path to the TIFF file to open
        
        Raises:
            FileNotFoundError: If the file does not exist
            InvalidFormatError: If the file is not a valid TIFF file
            ReadError: If there is an error reading the file
        """
    def __iter__(self) -> TiffFileIterator:
        """
        Iterate over all pages.
        
        Returns:
            Iterator yielding TiffPage objects
        """
    def __len__(self) -> int:
        """
        Get the number of pages in the file.
        
        Returns:
            Number of pages
        """
    def __repr__(self) -> str:
        ...
    def close(self) -> None:
        """
        Close the file.
        
        It is safe to call this multiple times. After closing, the file
        cannot be accessed until reopened.
        """
    def pages(self) -> list[TiffPage]:
        """
        Get all pages as a list.
        
        Returns:
            List of TiffPage objects
        
        Note:
            This creates TiffPage objects for all pages but does not
            load image data until read() is called on each page.
        """
    @property
    def endian(self) -> str:
        """
        Byte order ('little' or 'big')
        """
    @property
    def format(self) -> TiffFormatType:
        """
        TIFF format type (Classic or BigTIFF)
        """
    @property
    def is_open(self) -> bool:
        """
        Check if the file is open
        """
class TiffFileIterator:
    """
    Iterator for TiffFile pages
    """
    def __iter__(self) -> TiffFileIterator:
        ...
    def __next__(self) -> TiffPage:
        """
        Get the next page
        """
class TiffFormatType:
    """
    TIFF format variant.
    
    Specifies whether the file uses Classic TIFF or BigTIFF format.
    
    Members:
    
      Classic : Classic TIFF (32-bit offsets, max 4GB)
    
      BigTIFF : BigTIFF (64-bit offsets, no size limit)
    """
    BigTIFF: typing.ClassVar[TiffFormatType]  # value = <TiffFormatType.BigTIFF: 1>
    Classic: typing.ClassVar[TiffFormatType]  # value = <TiffFormatType.Classic: 0>
    __members__: typing.ClassVar[dict[str, TiffFormatType]]  # value = {'Classic': <TiffFormatType.Classic: 0>, 'BigTIFF': <TiffFormatType.BigTIFF: 1>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __ge__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __gt__(self, other: typing.Any) -> bool:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __le__(self, other: typing.Any) -> bool:
        ...
    def __lt__(self, other: typing.Any) -> bool:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class TiffPage:
    """
    Represents a single page in a TIFF file.
    
    Provides access to page metadata and image data. Image data is loaded
    lazily on first access and cached for subsequent reads.
    """
    def __repr__(self) -> str:
        ...
    def read(self, *, region: tiffconcept._tiffconcept_core.ImageRegion | None = None, dst: typing.Any = None, layout: str = 'DHWC', use_threading: bool = True) -> typing.Any:
        """
        Read the page (or a region) as an ImageBuffer.
        
        Args:
            region: Optional ImageRegion to read a subset of the page.
                    If None, reads the entire page.
            dst: Optional buffer object (for instance a numpy array) to store the result.
                 Must be C-contiguous with correct shape and dtype.
                 If provided, data is read directly into dst and dst is returned.
                 If None, a new ImageBuffer is allocated and returned.
                 The ImageBuffer can be converted to a numpy array np.asarray().
            layout: Memory layout for the output array
                    - 'HW': Height, Width (requires depth=1, channels=1)
                    - 'HWC': Height, Width, Channels (requires depth=1)
                    - 'DHW': Depth, Height, Width (requires channels=1)
                    - 'CHW': Channels, Height, Width (requires depth=1)
                    - 'DHWC': Depth, Height, Width, Channels (default)
                    - 'DCHW': Depth, Channels, Height, Width
                    - 'CDHW': Channels, Depth, Height, Width
            use_threading: If True, use multi-threaded reader for better performance.
                           Ignored for very small images.
        
        Returns:
            If dst is None: ImageBuffer containing the image data.
            If dst is provided: dst (the same object passed in, now filled with data).
        
        Examples:
            >>> # Allocate new buffer
            >>> buffer = page.read()
            >>> import numpy as np
            >>> array = np.asarray(buffer)  # Zero-copy view
        
            >>> # Read 2D grayscale image
            >>> buffer = page.read(layout='HW')  # shape will be (H, W)
        
            >>> # Read 2D RGB image
            >>> buffer = page.read(layout='HWC')  # shape will be (H, W, C)
        
            >>> # Read into existing array (OpenCV style)
            >>> dst = np.empty(page.shape(), dtype=page.dtype)
            >>> result = page.read(dst=dst)  # result is dst
            >>> assert result is dst
        
        Note:
            - Data is read fresh each time (no caching)
            - The GIL is released during I/O operations for better parallelism.
            - Using pre-allocated dst avoids allocations and returns dst directly.
            - Layout constraints are enforced: ValueError is raised if the data
              cannot conform to the requested layout (e.g., 'HW' with channels > 1).
        """
    def shape(self, layout: str = 'DHWC') -> tuple:
        """
        Get the shape as a tuple.\\n\\nArgs:\\n    layout: Memory layout\\n            - 'HW': Height, Width (requires depth=1, channels=1)\\n            - 'HWC': Height, Width, Channels (requires depth=1)\\n            - 'DHW': Depth, Height, Width (requires channels=1)\\n            - 'CHW': Channels, Height, Width (requires depth=1)\\n            - 'DHWC': Depth, Height, Width, Channels (default)\\n            - 'DCHW': Depth, Channels, Height, Width\\n            - 'CDHW': Channels, Depth, Height, Width\\n\\nReturns:\\n    Tuple of dimensions in the specified layout\\n\\nRaises:\\n    ValueError: If the image dimensions don't conform to the requested layout
        """
    @property
    def bits_per_sample(self) -> int:
        """
        Number of bits per sample (8, 16, 32, 64)
        """
    @property
    def channels(self) -> int:
        """
        Number of channels (samples per pixel)
        """
    @property
    def compression(self) -> CompressionScheme:
        """
        Compression scheme used
        """
    @property
    def depth(self) -> int:
        """
        Image depth (Z slices). 1 for 2D images
        """
    @property
    def dtype(self) -> str:
        """
        NumPy dtype string (e.g., 'uint8', 'float32')
        """
    @property
    def height(self) -> int:
        """
        Image height in pixels
        """
    @property
    def photometric(self) -> PhotometricInterpretation:
        """
        Photometric interpretation
        """
    @property
    def storage_layout(self) -> str:
        """
        Storage layout in file: 'DHWC' (chunky/interleaved) or 'CDHW' (planar/separate channels)
        """
    @property
    def width(self) -> int:
        """
        Image width in pixels
        """
class UnsupportedFeatureError(NotImplementedError):
    pass
class WriteError(OSError):
    pass
BigTIFF: TiffFormatType  # value = <TiffFormatType.BigTIFF: 1>
CCITT1D: CompressionScheme  # value = <CompressionScheme.CCITT1D: 2>
CDHW: ImageLayout  # value = <ImageLayout.CDHW: 2>
CIELab: PhotometricInterpretation  # value = <PhotometricInterpretation.CIELab: 8>
CMYK: PhotometricInterpretation  # value = <PhotometricInterpretation.CMYK: 5>
Chunky: PlanarConfiguration  # value = <PlanarConfiguration.Chunky: 1>
Classic: TiffFormatType  # value = <TiffFormatType.Classic: 0>
DCHW: ImageLayout  # value = <ImageLayout.DCHW: 1>
DHWC: ImageLayout  # value = <ImageLayout.DHWC: 0>
Deflate: CompressionScheme  # value = <CompressionScheme.Deflate: 32946>
Deflate_Adobe: CompressionScheme  # value = <CompressionScheme.Deflate_Adobe: 8>
FloatingPoint: Predictor  # value = <Predictor.FloatingPoint: 3>
Group3Fax: CompressionScheme  # value = <CompressionScheme.Group3Fax: 3>
Group4Fax: CompressionScheme  # value = <CompressionScheme.Group4Fax: 4>
Horizontal: Predictor  # value = <Predictor.Horizontal: 2>
IEEEFloat: SampleFormat  # value = <SampleFormat.IEEEFloat: 3>
JPEG: CompressionScheme  # value = <CompressionScheme.JPEG: 7>
JPEG_LS: CompressionScheme  # value = <CompressionScheme.JPEG_LS: 34670>
JPEG_Old: CompressionScheme  # value = <CompressionScheme.JPEG_Old: 6>
LOCO_I: Predictor  # value = <Predictor.LOCO_I: 8192>
LZW: CompressionScheme  # value = <CompressionScheme.LZW: 5>
Mask: PhotometricInterpretation  # value = <PhotometricInterpretation.Mask: 4>
MinIsBlack: PhotometricInterpretation  # value = <PhotometricInterpretation.MinIsBlack: 1>
MinIsWhite: PhotometricInterpretation  # value = <PhotometricInterpretation.MinIsWhite: 0>
NONE: Predictor  # value = <Predictor.NONE: 1>
PackBits: CompressionScheme  # value = <CompressionScheme.PackBits: 32773>
Palette: PhotometricInterpretation  # value = <PhotometricInterpretation.Palette: 3>
Planar: PlanarConfiguration  # value = <PlanarConfiguration.Planar: 2>
RGB: PhotometricInterpretation  # value = <PhotometricInterpretation.RGB: 2>
SignedInt: SampleFormat  # value = <SampleFormat.SignedInt: 2>
Undefined: SampleFormat  # value = <SampleFormat.Undefined: 4>
UnsignedInt: SampleFormat  # value = <SampleFormat.UnsignedInt: 1>
YCbCr: PhotometricInterpretation  # value = <PhotometricInterpretation.YCbCr: 6>
ZSTD: CompressionScheme  # value = <CompressionScheme.ZSTD: 50000>
ZSTD_Alt: CompressionScheme  # value = <CompressionScheme.ZSTD_Alt: 34926>
__version__: str = '0.1.0'
