#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "tag_codes.hpp"
#include "tag_spec.hpp"

// Common tag descriptors (TIFF 6.0 specification) + libtiff documentation
// They are sorted by order of tag code. Keep TagCode order when building a TagSpec.

// This file contains base tag descriptors for common TIFF tags. You can redefine
// these in your own tag specifications if you need different containers or datatypes.

namespace tiffconcept {

// Baseline tags
using NewSubfileTypeTag = TagDescriptor<TagCode::NewSubfileType, TiffDataType::Long, uint32_t, false>; // Default: 0
using SubfileTypeTag = TagDescriptor<TagCode::SubfileType, TiffDataType::Short, uint16_t, false>; // Deprecated
using ImageWidthTag = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t, false, TiffDataType::Short>;
using ImageLengthTag = TagDescriptor<TagCode::ImageLength, TiffDataType::Long, uint32_t, false, TiffDataType::Short>;
using BitsPerSampleTag = TagDescriptor<TagCode::BitsPerSample, TiffDataType::Short, std::vector<uint16_t>, false>; // Default: 1
using CompressionTag = TagDescriptor<TagCode::Compression, TiffDataType::Short, CompressionScheme, false>; // Default: 1 (None)
using PhotometricInterpretationTag = TagDescriptor<TagCode::PhotometricInterpretation, TiffDataType::Short, PhotometricInterpretation, false>; // 0: MinIsWhite, 1: MinIsBlack, 2: RGB, 3: Palette
using ThreshholdingTag = TagDescriptor<TagCode::Threshholding, TiffDataType::Short, uint16_t, false>; // Default: 1
using CellWidthTag = TagDescriptor<TagCode::CellWidth, TiffDataType::Short, uint16_t, false>;
using CellLengthTag = TagDescriptor<TagCode::CellLength, TiffDataType::Short, uint16_t, false>;
using FillOrderTag = TagDescriptor<TagCode::FillOrder, TiffDataType::Short, uint16_t, false>; // Default: 1, 1=MSB first, 2=LSB first
using DocumentNameTag = TagDescriptor<TagCode::DocumentName, TiffDataType::Ascii, std::string, false>;
using ImageDescriptionTag = TagDescriptor<TagCode::ImageDescription, TiffDataType::Ascii, std::string, false>;
using MakeTag = TagDescriptor<TagCode::Make, TiffDataType::Ascii, std::string, false>;
using ModelTag = TagDescriptor<TagCode::Model, TiffDataType::Ascii, std::string, false>;
using StripOffsetsTag = TagDescriptor<TagCode::StripOffsets, TiffDataType::Long, std::vector<uint32_t>, false, TiffDataType::Short>;
using OrientationTag = TagDescriptor<TagCode::Orientation, TiffDataType::Short, uint16_t, false>; // Default: 1 (top-left)
using SamplesPerPixelTag = TagDescriptor<TagCode::SamplesPerPixel, TiffDataType::Short, uint16_t, false>; // Default: 1
using RowsPerStripTag = TagDescriptor<TagCode::RowsPerStrip, TiffDataType::Long, uint32_t, false, TiffDataType::Short>; // Default: 2^32-1
using StripByteCountsTag = TagDescriptor<TagCode::StripByteCounts, TiffDataType::Long, std::vector<uint32_t>, false, TiffDataType::Short>;
using MinSampleValueTag = TagDescriptor<TagCode::MinSampleValue, TiffDataType::Short, std::vector<uint16_t>, false>; // Default: 0
using MaxSampleValueTag = TagDescriptor<TagCode::MaxSampleValue, TiffDataType::Short, std::vector<uint16_t>, false>; // Default: 2^BitsPerSample - 1
using XResolutionTag = TagDescriptor<TagCode::XResolution, TiffDataType::Rational, Rational, false>;
using YResolutionTag = TagDescriptor<TagCode::YResolution, TiffDataType::Rational, Rational, false>;
using PlanarConfigurationTag = TagDescriptor<TagCode::PlanarConfiguration, TiffDataType::Short, uint16_t, false>; // Default: 1 (chunky), 2=planar
using PageNameTag = TagDescriptor<TagCode::PageName, TiffDataType::Ascii, std::string, false>;
using XPositionTag = TagDescriptor<TagCode::XPosition, TiffDataType::Rational, Rational, false>;
using YPositionTag = TagDescriptor<TagCode::YPosition, TiffDataType::Rational, Rational, false>;
using FreeOffsetsTag = TagDescriptor<TagCode::FreeOffsets, TiffDataType::Long, std::vector<uint32_t>, false>;
using FreeByteCountsTag = TagDescriptor<TagCode::FreeByteCounts, TiffDataType::Long, std::vector<uint32_t>, false>;
using GrayResponseUnitTag = TagDescriptor<TagCode::GrayResponseUnit, TiffDataType::Short, uint16_t, false>; // Default: 2 (hundredths)
using GrayResponseCurveTag = TagDescriptor<TagCode::GrayResponseCurve, TiffDataType::Short, std::vector<uint16_t>, false>;
using T4OptionsTag = TagDescriptor<TagCode::T4Options, TiffDataType::Long, uint32_t, false>; // Default: 0
using T6OptionsTag = TagDescriptor<TagCode::T6Options, TiffDataType::Long, uint32_t, false>; // Default: 0
using ResolutionUnitTag = TagDescriptor<TagCode::ResolutionUnit, TiffDataType::Short, uint16_t, false>; // Default: 2 (inches), 1=none, 3=cm
using PageNumberTag = TagDescriptor<TagCode::PageNumber, TiffDataType::Short, std::array<uint16_t, 2>, false>;
using ColorResponseUnitTag = TagDescriptor<TagCode::ColorResponseUnit, TiffDataType::Short, uint16_t, false>;
using TransferFunctionTag = TagDescriptor<TagCode::TransferFunction, TiffDataType::Short, std::vector<uint16_t>, false>;
using SoftwareTag = TagDescriptor<TagCode::Software, TiffDataType::Ascii, std::string, false>;
using DateTimeTag = TagDescriptor<TagCode::DateTime, TiffDataType::Ascii, std::string, false>; // Format: "YYYY:MM:DD HH:MM:SS"
using ArtistTag = TagDescriptor<TagCode::Artist, TiffDataType::Ascii, std::string, false>;
using HostComputerTag = TagDescriptor<TagCode::HostComputer, TiffDataType::Ascii, std::string, false>;
using PredictorTag = TagDescriptor<TagCode::Predictor, TiffDataType::Short, Predictor, false>; // Default: 1 (none)
using WhitePointTag = TagDescriptor<TagCode::WhitePoint, TiffDataType::Rational, std::array<Rational, 2>, false>;
using PrimaryChromaticitiesTag = TagDescriptor<TagCode::PrimaryChromaticities, TiffDataType::Rational, std::array<Rational, 6>, false>;
using ColorMapTag = TagDescriptor<TagCode::ColorMap, TiffDataType::Short, std::vector<uint16_t>, false>;
using HalftoneHintsTag = TagDescriptor<TagCode::HalftoneHints, TiffDataType::Short, std::array<uint16_t, 2>, false>;
using TileWidthTag = TagDescriptor<TagCode::TileWidth, TiffDataType::Long, uint32_t, false, TiffDataType::Short>;
using TileLengthTag = TagDescriptor<TagCode::TileLength, TiffDataType::Long, uint32_t, false, TiffDataType::Short>;
using TileOffsetsTag = TagDescriptor<TagCode::TileOffsets, TiffDataType::Long, std::vector<uint32_t>, false, TiffDataType::Short>;
using TileByteCountsTag = TagDescriptor<TagCode::TileByteCounts, TiffDataType::Long, std::vector<uint32_t>, false, TiffDataType::Short>;
using BadFaxLinesTag = TagDescriptor<TagCode::BadFaxLines, TiffDataType::Long, uint32_t, false>;
using CleanFaxDataTag = TagDescriptor<TagCode::CleanFaxData, TiffDataType::Short, uint16_t, false>; // 0=clean, 1=regenerated, 2=unclean
using ConsecutiveBadFaxLinesTag = TagDescriptor<TagCode::ConsecutiveBadFaxLines, TiffDataType::Long, uint32_t, false>;
using SubIFDTag = TagDescriptor<TagCode::SubIFD, TiffDataType::IFD, std::vector<uint32_t>, false, TiffDataType::Short, TiffDataType::Long>;
using InkSetTag = TagDescriptor<TagCode::InkSet, TiffDataType::Short, uint16_t, false>; // Default: 1 (CMYK), 2=not CMYK
using InkNamesTag = TagDescriptor<TagCode::InkNames, TiffDataType::Ascii, std::vector<std::string>, false>;
using NumberOfInksTag = TagDescriptor<TagCode::NumberOfInks, TiffDataType::Short, uint16_t, false>; // Default: 4
using DotRangeTag = TagDescriptor<TagCode::DotRange, TiffDataType::Byte, std::vector<uint8_t>, false>;
using TargetPrinterTag = TagDescriptor<TagCode::TargetPrinter, TiffDataType::Ascii, std::string, false>;
using ExtraSamplesTag = TagDescriptor<TagCode::ExtraSamples, TiffDataType::Byte, std::vector<uint8_t>, false>;
using SampleFormatTag = TagDescriptor<TagCode::SampleFormat, TiffDataType::Short, SampleFormat, false>; // Default: 1 (unsigned). 2=signed, 3=float, 4=undefined
using SMinSampleValueTag = TagDescriptor<TagCode::SMinSampleValue, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using SMaxSampleValueTag = TagDescriptor<TagCode::SMaxSampleValue, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using TransferRangeTag = TagDescriptor<TagCode::TransferRange, TiffDataType::Short, std::array<uint16_t, 6>, false>;
using ClipPathTag = TagDescriptor<TagCode::ClipPath, TiffDataType::Byte, std::vector<uint8_t>, false>;
using XClipPathUnitsTag = TagDescriptor<TagCode::XClipPathUnits, TiffDataType::Long, uint32_t, false>;
using YClipPathUnitsTag = TagDescriptor<TagCode::YClipPathUnits, TiffDataType::Long, uint32_t, false>;
using IndexedTag = TagDescriptor<TagCode::Indexed, TiffDataType::Short, uint16_t, false>;
using JPEGTablesTag = TagDescriptor<TagCode::JPEGTables, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using GlobalParametersIFDTag = TagDescriptor<TagCode::GlobalParametersIFD, TiffDataType::Long, uint32_t, false>;
using ProfileTypeTag = TagDescriptor<TagCode::ProfileType, TiffDataType::Long, uint32_t, false>; // 0=unspecified, 1=G3Fax
using FaxProfileTag = TagDescriptor<TagCode::FaxProfile, TiffDataType::Short, uint16_t, false>; // 0=unknown, 1=minimal, 2=extended
using CodingMethodsTag = TagDescriptor<TagCode::CodingMethods, TiffDataType::Long, uint32_t, false>; // Bit flags
using VersionYearTag = TagDescriptor<TagCode::VersionYear, TiffDataType::Byte, std::array<uint8_t, 4>, false>;
using ModeNumberTag = TagDescriptor<TagCode::ModeNumber, TiffDataType::Byte, uint8_t, false>;
using DecodeTag = TagDescriptor<TagCode::Decode, TiffDataType::SRational, std::vector<SRational>, false>;
using ImageBaseColorTag = TagDescriptor<TagCode::ImageBaseColor, TiffDataType::Short, std::vector<uint16_t>, false>;
using T82OptionsTag = TagDescriptor<TagCode::T82Options, TiffDataType::Long, uint32_t, false>;

// JPEG tags
using JPEGProcTag = TagDescriptor<TagCode::JPEGProc, TiffDataType::Short, uint16_t, false>; // 1=baseline, 14=lossless
using JPEGInterchangeFormatTag = TagDescriptor<TagCode::JPEGInterchangeFormat, TiffDataType::Long, uint32_t, false>;
using JPEGInterchangeFormatLengthTag = TagDescriptor<TagCode::JPEGInterchangeFormatLngth, TiffDataType::Long, uint32_t, false>;
using JPEGRestartIntervalTag = TagDescriptor<TagCode::JPEGRestartInterval, TiffDataType::Short, uint16_t, false>;
using JPEGLosslessPredictorsTag = TagDescriptor<TagCode::JPEGLosslessPredictors, TiffDataType::Short, std::vector<uint16_t>, false>;
using JPEGPointTransformsTag = TagDescriptor<TagCode::JPEGPointTransforms, TiffDataType::Short, std::vector<uint16_t>, false>;
using JPEGQTablesTag = TagDescriptor<TagCode::JPEGQTables, TiffDataType::Long, std::vector<uint32_t>, false>;
using JPEGDCTablesTag = TagDescriptor<TagCode::JPEGDCTables, TiffDataType::Long, std::vector<uint32_t>, false>;
using JPEGACTablesTag = TagDescriptor<TagCode::JPEGACTables, TiffDataType::Long, std::vector<uint32_t>, false>;

// YCbCr tags
using YCbCrCoefficientsTag = TagDescriptor<TagCode::YCbCrCoefficients, TiffDataType::Rational, std::array<Rational, 3>, false>;
using YCbCrSubSamplingTag = TagDescriptor<TagCode::YCbCrSubSampling, TiffDataType::Short, std::array<uint16_t, 2>, false>; // Default: [2,2]
using YCbCrPositioningTag = TagDescriptor<TagCode::YCbCrPositioning, TiffDataType::Short, uint16_t, false>; // Default: 1 (centered)
using ReferenceBlackWhiteTag = TagDescriptor<TagCode::ReferenceBlackWhite, TiffDataType::Long, std::vector<uint32_t>, false>;

// Additional tags
using StripRowCountsTag = TagDescriptor<TagCode::StripRowCounts, TiffDataType::Long, std::vector<uint32_t>, false>;
using XMLPacketTag = TagDescriptor<TagCode::XMLPacket, TiffDataType::Byte, std::vector<uint8_t>, false>;

// Private/Extended tags
using MatteingTag = TagDescriptor<TagCode::Matteing, TiffDataType::Short, uint16_t, false>; // Obsolete, use ExtraSamples
using DataTypeTag = TagDescriptor<TagCode::DataType, TiffDataType::Short, uint16_t, false>; // Obsolete, use SampleFormat
using ImageDepthTag = TagDescriptor<TagCode::ImageDepth, TiffDataType::Long, uint32_t, false, TiffDataType::Short>; // Default: 1
using TileDepthTag = TagDescriptor<TagCode::TileDepth, TiffDataType::Long, uint32_t, false, TiffDataType::Short>; // Default: 1
using ImageFullWidthTag = TagDescriptor<TagCode::ImageFullWidth, TiffDataType::Long, uint32_t, false>;
using ImageFullLengthTag = TagDescriptor<TagCode::ImageFullLength, TiffDataType::Long, uint32_t, false>;
using TextureFormatTag = TagDescriptor<TagCode::TextureFormat, TiffDataType::Ascii, std::string, false>;
using TextureWrapModesTag = TagDescriptor<TagCode::TextureWrapModes, TiffDataType::Ascii, std::string, false>;
using FieldOfViewCotangentTag = TagDescriptor<TagCode::FieldOfViewCotangent, TiffDataType::Float, float, false>;
using MatrixWorldToScreenTag = TagDescriptor<TagCode::MatrixWorldToScreen, TiffDataType::Float, std::vector<float>, false>;
using MatrixWorldToCameraTag = TagDescriptor<TagCode::MatrixWorldToCamera, TiffDataType::Float, std::vector<float>, false>;
using CopyrightTag = TagDescriptor<TagCode::Copyright, TiffDataType::Ascii, std::string, false>;
using RichTIFFIPTCTag = TagDescriptor<TagCode::RichTIFFIPTC, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using PhotoshopTag = TagDescriptor<TagCode::Photoshop, TiffDataType::Byte, std::vector<uint8_t>, false>;
using EXIFIFDOffsetTag = TagDescriptor<TagCode::EXIFIFDOffset, TiffDataType::Long, uint32_t, false>;
using ICCProfileTag = TagDescriptor<TagCode::ICCProfile, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using ImageLayerTag = TagDescriptor<TagCode::ImageLayer, TiffDataType::Long, std::vector<uint32_t>, false>;
using GPSIFDOffsetTag = TagDescriptor<TagCode::GPSIFDOffset, TiffDataType::Long, uint32_t, false>;
using FaxRecvParamsTag = TagDescriptor<TagCode::FaxRecvParams, TiffDataType::Long, uint32_t, false>;
using FaxSubAddressTag = TagDescriptor<TagCode::FaxSubAddress, TiffDataType::Ascii, std::string, false>;
using FaxRecvTimeTag = TagDescriptor<TagCode::FaxRecvTime, TiffDataType::Long, uint32_t, false>;
using FaxDcsTag = TagDescriptor<TagCode::FaxDcs, TiffDataType::Ascii, std::string, false>;
using StoNitsTag = TagDescriptor<TagCode::StoNits, TiffDataType::Double, double, false>;
using PhotoshopDocDataBlockTag = TagDescriptor<TagCode::PhotoshopDocDataBlock, TiffDataType::Byte, std::vector<uint8_t>, false>;
using InteroperabilityIFDOffsetTag = TagDescriptor<TagCode::InteroperabilityIFDOffset, TiffDataType::Long, uint32_t, false>;

// DNG tags
using DNGVersionTag = TagDescriptor<TagCode::DNGVersion, TiffDataType::Byte, std::array<uint8_t, 4>, false>;
using DNGBackwardVersionTag = TagDescriptor<TagCode::DNGBackwardVersion, TiffDataType::Byte, std::array<uint8_t, 4>, false>;
using UniqueCameraModelTag = TagDescriptor<TagCode::UniqueCameraModel, TiffDataType::Ascii, std::string, false>;
using LocalizedCameraModelTag = TagDescriptor<TagCode::LocalizedCameraModel, TiffDataType::Ascii, std::string, false>;
using CFAPlaneColorTag = TagDescriptor<TagCode::CFAPlaneColor, TiffDataType::Byte, std::vector<uint8_t>, false>;
using CFALayoutTag = TagDescriptor<TagCode::CFALayout, TiffDataType::Short, uint16_t, false>;
using LinearizationTableTag = TagDescriptor<TagCode::LinearizationTable, TiffDataType::Short, std::vector<uint16_t>, false>;
using BlackLevelRepeatDimTag = TagDescriptor<TagCode::BlackLevelRepeatDim, TiffDataType::Short, std::array<uint16_t, 2>, false>;
using BlackLevelTag = TagDescriptor<TagCode::BlackLevel, TiffDataType::Rational, std::vector<Rational>, false>;
using BlackLevelDeltaHTag = TagDescriptor<TagCode::BlackLevelDeltaH, TiffDataType::SRational, std::vector<SRational>, false>;
using BlackLevelDeltaVTag = TagDescriptor<TagCode::BlackLevelDeltaV, TiffDataType::SRational, std::vector<SRational>, false>;
using WhiteLevelTag = TagDescriptor<TagCode::WhiteLevel, TiffDataType::Long, std::vector<uint32_t>, false>;
using DefaultScaleTag = TagDescriptor<TagCode::DefaultScale, TiffDataType::Rational, std::array<Rational, 2>, false>;
using DefaultCropOriginTag = TagDescriptor<TagCode::DefaultCropOrigin, TiffDataType::Rational, std::array<Rational, 2>, false>;
using DefaultCropSizeTag = TagDescriptor<TagCode::DefaultCropSize, TiffDataType::Rational, std::array<Rational, 2>, false>;
using ColorMatrix1Tag = TagDescriptor<TagCode::ColorMatrix1, TiffDataType::SRational, std::vector<SRational>, false>;
using ColorMatrix2Tag = TagDescriptor<TagCode::ColorMatrix2, TiffDataType::SRational, std::vector<SRational>, false>;
using CameraCalibration1Tag = TagDescriptor<TagCode::CameraCalibration1, TiffDataType::SRational, std::vector<SRational>, false>;
using CameraCalibration2Tag = TagDescriptor<TagCode::CameraCalibration2, TiffDataType::SRational, std::vector<SRational>, false>;
using ReductionMatrix1Tag = TagDescriptor<TagCode::ReductionMatrix1, TiffDataType::SRational, std::vector<SRational>, false>;
using ReductionMatrix2Tag = TagDescriptor<TagCode::ReductionMatrix2, TiffDataType::SRational, std::vector<SRational>, false>;
using AnalogBalanceTag = TagDescriptor<TagCode::AnalogBalance, TiffDataType::Rational, std::vector<Rational>, false>;
using AsShotNeutralTag = TagDescriptor<TagCode::AsShotNeutral, TiffDataType::Rational, std::vector<Rational>, false>;
using AsShotWhiteXYTag = TagDescriptor<TagCode::AsShotWhiteXY, TiffDataType::Rational, std::array<Rational, 2>, false>;
using BaselineExposureTag = TagDescriptor<TagCode::BaselineExposure, TiffDataType::SRational, SRational, false>;
using BaselineNoiseTag = TagDescriptor<TagCode::BaselineNoise, TiffDataType::Rational, Rational, false>;
using BaselineSharpnessTag = TagDescriptor<TagCode::BaselineSharpness, TiffDataType::Rational, Rational, false>;
using BayerGreenSplitTag = TagDescriptor<TagCode::BayerGreenSplit, TiffDataType::Long, uint32_t, false>;
using LinearResponseLimitTag = TagDescriptor<TagCode::LinearResponseLimit, TiffDataType::Rational, Rational, false>;
using CameraSerialNumberTag = TagDescriptor<TagCode::CameraSerialNumber, TiffDataType::Ascii, std::string, false>;
using LensInfoTag = TagDescriptor<TagCode::LensInfo, TiffDataType::Rational, std::array<Rational, 4>, false>;
using ChromaBlurRadiusTag = TagDescriptor<TagCode::ChromaBlurRadius, TiffDataType::Rational, Rational, false>;
using AntiAliasStrengthTag = TagDescriptor<TagCode::AntiAliasStrength, TiffDataType::Rational, Rational, false>;
using ShadowScaleTag = TagDescriptor<TagCode::ShadowScale, TiffDataType::Rational, Rational, false>;
using DNGPrivateDataTag = TagDescriptor<TagCode::DNGPrivateData, TiffDataType::Byte, std::vector<uint8_t>, false>;
using MakerNoteSafetyTag = TagDescriptor<TagCode::MakerNoteSafety, TiffDataType::Short, uint16_t, false>;
using CalibrationIlluminant1Tag = TagDescriptor<TagCode::CalibrationIlluminant1, TiffDataType::Short, uint16_t, false>;
using CalibrationIlluminant2Tag = TagDescriptor<TagCode::CalibrationIlluminant2, TiffDataType::Short, uint16_t, false>;
using BestQualityScaleTag = TagDescriptor<TagCode::BestQualityScale, TiffDataType::Rational, Rational, false>;
using RawDataUniqueIDTag = TagDescriptor<TagCode::RawDataUniqueID, TiffDataType::Byte, std::array<uint8_t, 16>, false>;
using OriginalRawFileNameTag = TagDescriptor<TagCode::OriginalRawFileName, TiffDataType::Ascii, std::string, false>;
using OriginalRawFileDataTag = TagDescriptor<TagCode::OriginalRawFileData, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using ActiveAreaTag = TagDescriptor<TagCode::ActiveArea, TiffDataType::Long, std::array<uint32_t, 4>, false>;
using MaskedAreasTag = TagDescriptor<TagCode::MaskedAreas, TiffDataType::Long, std::vector<uint32_t>, false>;
using AsShotICCProfileTag = TagDescriptor<TagCode::AsShotICCProfile, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using AsShotPreProfileMatrixTag = TagDescriptor<TagCode::AsShotPreProfileMatrix, TiffDataType::SRational, std::vector<SRational>, false>;
using CurrentICCProfileTag = TagDescriptor<TagCode::CurrentICCProfile, TiffDataType::Undefined, std::vector<uint8_t>, false>;
using CurrentPreProfileMatrixTag = TagDescriptor<TagCode::CurrentPreProfileMatrix, TiffDataType::SRational, std::vector<SRational>, false>;


// Bigtiff variants
// Offsets and byte counts use 8-byte Long8 type. IFD offsets are also Long8/IFD8

using StripOffsetsTag_BigTIFF = TagDescriptor<TagCode::StripOffsets, TiffDataType::Long8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long>;
using StripByteCountsTag_BigTIFF = TagDescriptor<TagCode::StripByteCounts, TiffDataType::Long8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long>;
using TileOffsetsTag_BigTIFF = TagDescriptor<TagCode::TileOffsets, TiffDataType::Long8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long>;
using TileByteCountsTag_BigTIFF = TagDescriptor<TagCode::TileByteCounts, TiffDataType::Long8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long>;
using SubIFDTag_BigTIFF = TagDescriptor<TagCode::SubIFD, TiffDataType::IFD8, std::vector<uint64_t>, false, TiffDataType::Short, TiffDataType::Long, TiffDataType::IFD, TiffDataType::Long8>;

// Tag specification examples:

// Minimal set for parsing simple strip-based images
using MinStrippedSpec = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    StripOffsetsTag,
    OptTag_t<SamplesPerPixelTag> ,// Default: 1
    RowsPerStripTag,
    StripByteCountsTag,
    OptTag_t<PredictorTag>, // Default: 1 (no predictor)
    OptTag_t<SampleFormatTag> // Default: 1 (unsigned)
>;

// Minimal set for parsing simple tile-based images
using MinTiledSpec = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    OptTag_t<SamplesPerPixelTag>, // Default: 1
    OptTag_t<PredictorTag>, // Default: 1 (no predictor)
    TileWidthTag,
    TileLengthTag,
    TileOffsetsTag,
    TileByteCountsTag,
    OptTag_t<SampleFormatTag> // Default: 1 (unsigned)
>;

// Minimal set for parsing simple strip-based images (BigTiff)
using MinBigStrippedSpec = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    StripOffsetsTag_BigTIFF,
    OptTag_t<SamplesPerPixelTag>, // Default: 1
    RowsPerStripTag,
    StripByteCountsTag_BigTIFF,
    OptTag_t<PredictorTag>, // Default: 1 (no predictor)
    OptTag_t<SampleFormatTag> // Default: 1 (unsigned)
>;

// Minimal set for parsing simple tile-based images
using MinBigTiledSpec = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    OptTag_t<SamplesPerPixelTag>, // Default: 1
    OptTag_t<PredictorTag>, // Default: 1 (no predictor)
    TileWidthTag,
    TileLengthTag,
    TileOffsetsTag_BigTIFF,
    TileByteCountsTag_BigTIFF,
    OptTag_t<SampleFormatTag> // Default: 1 (unsigned)
>;

// Extended minimal set to cover most image types (classic tiff)
using MinImageSpec = TagSpec<
    ImageWidthTag,
    ImageLengthTag,
    BitsPerSampleTag,
    CompressionTag,
    OptTag_t<PhotometricInterpretationTag>, // Color space interpretation (RGB, Grayscale, Palette, etc.)
    OptTag_t<FillOrderTag>, // Bit order within bytes (MSB/LSB first)
    OptTag_t<StripOffsetsTag>,
    OptTag_t<OrientationTag>, // Image orientation (rotation/flip)
    OptTag_t<SamplesPerPixelTag>, // Number of components per pixel
    OptTag_t<RowsPerStripTag>,
    OptTag_t<StripByteCountsTag>,
    OptTag_t<MinSampleValueTag>, // Minimum sample value per component
    OptTag_t<MaxSampleValueTag>, // Maximum sample value per component
    OptTag_t<XResolutionTag>, // Horizontal resolution
    OptTag_t<YResolutionTag>, // Vertical resolution
    OptTag_t<PlanarConfigurationTag>, // Chunky (1) vs Planar (2) layout - for multi-channel images
    OptTag_t<ResolutionUnitTag>, // Unit for resolution (inches, cm, none)
    OptTag_t<TransferFunctionTag>, // Gamma correction curve
    OptTag_t<PredictorTag>, // Compression predictor (none, horizontal, floating point)
    OptTag_t<WhitePointTag>, // CIE white point for calibrated RGB
    OptTag_t<PrimaryChromaticitiesTag>, // CIE primaries for calibrated RGB
    OptTag_t<ColorMapTag>, // Palette lookup table (PhotometricInterpretation=3)
    OptTag_t<TileWidthTag>, // Tile width (for tiled images)
    OptTag_t<TileLengthTag>, // Tile height (for tiled images)
    OptTag_t<TileOffsetsTag>, // Tile data offsets (for tiled images)
    OptTag_t<TileByteCountsTag>, // Tile data sizes (for tiled images)
    OptTag_t<SubIFDTag>, // Sub-image IFD offsets (thumbnails, reduced resolution copies)
    OptTag_t<ExtraSamplesTag>, // Alpha channel or other extra components
    OptTag_t<SampleFormatTag>, // Data type (unsigned, signed, float, undefined)
    OptTag_t<SMinSampleValueTag>, // Minimum sample value for signed/float data
    OptTag_t<SMaxSampleValueTag>, // Maximum sample value for signed/float data
    OptTag_t<JPEGTablesTag>, // JPEG quantization/Huffman tables for abbreviated streams
    OptTag_t<JPEGProcTag>, // JPEG compression type (baseline, lossless)
    OptTag_t<JPEGInterchangeFormatTag>, // Offset to JPEG interchange format stream
    OptTag_t<JPEGInterchangeFormatLengthTag>, // Length of JPEG interchange format stream
    OptTag_t<JPEGRestartIntervalTag>, // JPEG restart interval
    OptTag_t<JPEGLosslessPredictorsTag>, // JPEG lossless predictor selection values
    OptTag_t<JPEGPointTransformsTag>, // JPEG point transform values
    OptTag_t<JPEGQTablesTag>, // Offsets to JPEG quantization tables
    OptTag_t<JPEGDCTablesTag>, // Offsets to JPEG DC Huffman tables
    OptTag_t<JPEGACTablesTag>, // Offsets to JPEG AC Huffman tables
    OptTag_t<YCbCrCoefficientsTag>, // YCbCr to RGB transformation coefficients
    OptTag_t<YCbCrSubSamplingTag>, // YCbCr chroma subsampling factors
    OptTag_t<YCbCrPositioningTag>, // YCbCr sample positioning (centered/cosited)
    OptTag_t<ReferenceBlackWhiteTag>, // YCbCr reference black/white values
    OptTag_t<ImageDepthTag>, // Number of images in a stack (3D images)
    OptTag_t<TileDepthTag>, // Tile depth for 3D tiled images
    OptTag_t<ICCProfileTag> // Embedded ICC color profile
>;

} // namespace tiffconcept