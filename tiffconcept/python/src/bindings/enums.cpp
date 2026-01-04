#include <pybind11/pybind11.h>
#include <pybind11/typing.h>

#include "tiffconcept/types/tiff_spec.hpp"

namespace py = pybind11;
using namespace tiffconcept;

void bind_enums(py::module_& m) {
    // CompressionScheme enum - use native enum for better Python integration
    py::enum_<CompressionScheme>(m, "CompressionScheme", py::arithmetic(),
        "TIFF compression schemes.\\n\\n"
        "Specifies the compression algorithm used for image data.")
        .value("NONE", CompressionScheme::None,
               "No compression (raw data)")
        .value("CCITT1D", CompressionScheme::CCITT_RLE, 
               "CCITT Group 3 1-Dimensional Modified Huffman run-length encoding")
        .value("Group3Fax", CompressionScheme::CCITT_Fax3, 
               "CCITT T.4 bi-level encoding (Group 3 Fax)")
        .value("Group4Fax", CompressionScheme::CCITT_Fax4, 
               "CCITT T.6 bi-level encoding (Group 4 Fax)")
        .value("LZW", CompressionScheme::LZW, 
               "Lempel-Ziv-Welch compression")
        .value("JPEG_Old", CompressionScheme::JPEG_Old, 
               "JPEG compression (old-style)")
        .value("JPEG", CompressionScheme::JPEG, 
               "JPEG compression")
        .value("Deflate_Adobe", CompressionScheme::Deflate_Adobe, 
               "Deflate compression (Adobe variant)")
        .value("Deflate", CompressionScheme::Deflate, 
               "Deflate compression (zlib)")
        .value("PackBits", CompressionScheme::PackBits, 
               "PackBits compression (simple run-length encoding)")
        .value("JPEG_LS", CompressionScheme::JPEG_LS, 
               "JPEG-LS lossless/near-lossless compression")
        .value("ZSTD", CompressionScheme::ZSTD, 
               "Zstandard compression (primary code)")
        .value("ZSTD_Alt", CompressionScheme::ZSTD_Alt, 
               "Zstandard compression (alternate code)")
        .export_values();

    // SampleFormat enum - use native enum
    py::enum_<SampleFormat>(m, "SampleFormat", py::arithmetic(), 
        "Sample data format.\n\n"
        "Specifies how pixel sample values should be interpreted.")
        .value("UnsignedInt", SampleFormat::UnsignedInt, 
               "Unsigned integer data")
        .value("SignedInt", SampleFormat::SignedInt, 
               "Signed (two's complement) integer data")
        .value("IEEEFloat", SampleFormat::IEEEFloat, 
               "IEEE floating point data")
        .value("Undefined", SampleFormat::Undefined, 
               "Undefined data format")
        .export_values();

    // PhotometricInterpretation enum - use native enum
    py::enum_<PhotometricInterpretation>(m, "PhotometricInterpretation", py::arithmetic(), 
        "Photometric interpretation.\n\n"
        "Specifies the color space of the image data.")
        .value("MinIsWhite", PhotometricInterpretation::MinIsWhite, 
               "Minimum value is white (grayscale)")
        .value("MinIsBlack", PhotometricInterpretation::MinIsBlack, 
               "Minimum value is black (grayscale)")
        .value("RGB", PhotometricInterpretation::RGB, 
               "RGB color model")
        .value("Palette", PhotometricInterpretation::Palette, 
               "Indexed color (palette/color map)")
        .value("Mask", PhotometricInterpretation::Mask, 
               "Transparency mask")
        .value("CMYK", PhotometricInterpretation::CMYK, 
               "CMYK color model")
        .value("YCbCr", PhotometricInterpretation::YCbCr, 
               "YCbCr color model")
        .value("CIELab", PhotometricInterpretation::CIELab, 
               "CIE L*a*b* color model")
        .export_values();

    // Predictor enum - use native enum
    py::enum_<Predictor>(m, "Predictor", py::arithmetic(), 
        "Predictor for compression.\n\n"
        "Specifies a mathematical operator applied before compression to improve "
        "compression ratios.")
        .value("NONE", Predictor::None, 
               "No predictor")
        .value("Horizontal", Predictor::Horizontal, 
               "Horizontal differencing (predict from left pixel)")
        .value("FloatingPoint", Predictor::FloatingPoint, 
               "Floating point horizontal differencing")
        .value("LOCO_I", Predictor::LOCO_I, 
               "LOCO-I predictor (used with JPEG-LS)")
        .export_values();

    // PlanarConfiguration enum - use native enum
    py::enum_<PlanarConfiguration>(m, "PlanarConfiguration", py::arithmetic(), 
        "Planar configuration for multi-channel images.\n\n"
        "Specifies how the components of each pixel are stored.")
        .value("Chunky", PlanarConfiguration::Chunky, 
               "Chunky format (RGBRGBRGB...)")
        .value("Planar", PlanarConfiguration::Planar, 
               "Planar format (RRR...GGG...BBB...)")
        .export_values();

    // ImageLayout enum (Python-specific) - use native enum
    py::enum_<ImageLayoutSpec>(m, "ImageLayout", py::arithmetic(), 
        "Memory layout for image data.\n\n"
        "Specifies the order of dimensions in the output numpy array.\n"
        "  - DHWC: Depth, Height, Width, Channels (TensorFlow/Keras style)\n"
        "  - DCHW: Depth, Channels, Height, Width (PyTorch style)\n"
        "  - CDHW: Channels, Depth, Height, Width (planar)")
        .value("DHWC", ImageLayoutSpec::DHWC, 
               "Depth, Height, Width, Channels (channels last)")
        .value("DCHW", ImageLayoutSpec::DCHW, 
               "Depth, Channels, Height, Width")
        .value("CDHW", ImageLayoutSpec::CDHW, 
               "Channels, Depth, Height, Width (planar)")
        .export_values();

    // TiffFormatType enum - use native enum
    py::enum_<TiffFormatType>(m, "TiffFormatType", py::arithmetic(), 
        "TIFF format variant.\n\n"
        "Specifies whether the file uses Classic TIFF or BigTIFF format.")
        .value("Classic", TiffFormatType::Classic, 
               "Classic TIFF (32-bit offsets, max 4GB)")
        .value("BigTIFF", TiffFormatType::BigTIFF, 
               "BigTIFF (64-bit offsets, no size limit)")
        .export_values();
}
