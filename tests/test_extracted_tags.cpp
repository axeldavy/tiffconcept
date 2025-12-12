#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstring>
#include <vector>

#include "../tiffconcept/include/tiff/tag_extraction.hpp"
#include "../tiffconcept/include/tiff/tag_spec.hpp"
#include "../tiffconcept/include/tiff/parsing.hpp"
#include "../tiffconcept/include/tiff/reader_buffer.hpp"
#include "../tiffconcept/include/tiff/types.hpp"

using namespace tiff;

// ============================================================================
// Helper Functions
// ============================================================================

/// Helper to create a TiffTag with data inline (fits in TagValue)
template <std::endian Endian = std::endian::native>
TiffTag<Endian> make_inline_tag(uint16_t code, TiffDataType type, uint32_t count, const void* data, std::size_t data_size) {
    TiffTag<Endian> tag{};
    
    // Store fields in the correct endianness
    if constexpr (Endian == std::endian::native) {
        tag.code = code;
        tag.datatype = type;
        tag.count = count;
    } else {
        tag.code = byteswap(code);
        tag.datatype = static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(type)));
        tag.count = byteswap(count);
    }
    
    // Copy data into value union (data itself should already be in correct endianness)
    std::memcpy(&tag.value, data, std::min(data_size, sizeof(TagValue)));
    
    return tag;
}

/// Helper to create a TiffTag with external data (offset pointer)
template <std::endian Endian = std::endian::native>
TiffTag<Endian> make_offset_tag(uint16_t code, TiffDataType type, uint32_t count, uint32_t offset) {
    TiffTag<Endian> tag{};
    
    // Store fields in the correct endianness
    if constexpr (Endian == std::endian::native) {
        tag.code = code;
        tag.datatype = type;
        tag.count = count;
        tag.value.offset = offset;
    } else {
        tag.code = byteswap(code);
        tag.datatype = static_cast<TiffDataType>(byteswap(static_cast<uint16_t>(type)));
        tag.count = byteswap(count);
        tag.value.offset = byteswap(offset);
    }
    
    return tag;
}

/// Helper to create a buffer with tag data at a specific offset
std::vector<std::byte> create_buffer_with_data(std::size_t total_size, std::size_t offset, const void* data, std::size_t data_size) {
    std::vector<std::byte> buffer(total_size, std::byte{0});
    std::memcpy(buffer.data() + offset, data, data_size);
    return buffer;
}

// ============================================================================
// Test Tag Descriptors
// ============================================================================

// Tag specification for basic TIFF
using BasicTiffSpec = TagSpec<
    BitsPerSampleTag,
    CompressionTag,
    ImageLengthTag,
    ImageWidthTag,
    PhotometricInterpretationTag,
    RowsPerStripTag,
    SamplesPerPixelTag,
    StripByteCountsTag,
    StripOffsetsTag,
    XResolutionTag,
    YResolutionTag
>;

// Tag specification with optional tags
using TiffWithOptionalSpec = TagSpec<
    ArtistTag,
    BitsPerSampleTag,
    CompressionTag,
    DateTimeTag,
    ImageLengthTag,
    ImageWidthTag,
    PhotometricInterpretationTag,
    RowsPerStripTag,
    SamplesPerPixelTag,
    SoftwareTag,
    StripByteCountsTag,
    StripOffsetsTag,
    XResolutionTag,
    YResolutionTag
>;

using OptSoftwareTag = TagDescriptor<TagCode::Software, TiffDataType::Ascii, std::string, true>;
using OptDateTimeTag = TagDescriptor<TagCode::DateTime, TiffDataType::Ascii, std::string, true>;
using OptArtistTag = TagDescriptor<TagCode::Artist, TiffDataType::Ascii, std::string, true>;

// ============================================================================
// Basic Construction and Access Tests
// ============================================================================

TEST(ExtractedTags, ConstructionAndNumTags) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    EXPECT_EQ(tags.num_tags(), 3);
}

TEST(ExtractedTags, GetByTagCode) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    // Set values directly
    std::get<0>(tags.values) = 1024;
    std::get<1>(tags.values) = 768;
    std::get<2>(tags.values) = tiff::CompressionScheme::ZSTD;
    
    // Access by tag code
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1024);
    EXPECT_EQ(tags.get<TagCode::ImageLength>(), 768);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::ZSTD);
}

TEST(ExtractedTags, GetByTagCodeConst) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    std::get<0>(tags.values) = 1024;
    std::get<1>(tags.values) = 768;
    std::get<2>(tags.values) = tiff::CompressionScheme::ZSTD;
    
    const auto& const_tags = tags;
    
    EXPECT_EQ(const_tags.get<TagCode::ImageWidth>(), 1024);
    EXPECT_EQ(const_tags.get<TagCode::ImageLength>(), 768);
    EXPECT_EQ(const_tags.get<TagCode::Compression>(), tiff::CompressionScheme::ZSTD);
}

TEST(ExtractedTags, OptionalTagsDefault) {
    ExtractedTags<ImageWidthTag, SoftwareTag, DateTimeTag> tags;
    
    // Optional tags should be default-constructed to nullopt
    // Required tags should be default-constructed to their type's default
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 0);
}

TEST(ExtractedTags, OptionalTagsSetValue) {
    ExtractedTags<ImageWidthTag, OptSoftwareTag, OptDateTimeTag> tags;
    
    tags.get<TagCode::Software>() = "TestSoftware";
    tags.get<TagCode::ImageWidth>() = 640;
    tags.get<TagCode::DateTime>() = "2025:12:12 10:00:00";
    
    ASSERT_TRUE(tags.get<TagCode::Software>().has_value());
    EXPECT_EQ(tags.get<TagCode::Software>().value(), "TestSoftware");
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 640);
    ASSERT_TRUE(tags.get<TagCode::DateTime>().has_value());
    EXPECT_EQ(tags.get<TagCode::DateTime>().value(), "2025:12:12 10:00:00");
}

// ============================================================================
// Clear Functionality Tests
// ============================================================================

TEST(ExtractedTags, ClearRequiredTags) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    tags.get<TagCode::ImageWidth>() = 1024;
    tags.get<TagCode::ImageLength>() = 768;
    tags.get<TagCode::Compression>() = tiff::CompressionScheme::LZW;
    
    tags.clear();
    
    // Required tags should be default-constructed
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 0);
    EXPECT_EQ(tags.get<TagCode::ImageLength>(), 0);
    EXPECT_EQ(static_cast<int>(tags.get<TagCode::Compression>()), 0);
}

TEST(ExtractedTags, ClearOptionalTags) {
    ExtractedTags<ImageWidthTag, OptSoftwareTag, OptDateTimeTag> tags;
    
    tags.get<TagCode::Software>() = "TestSoftware";
    tags.get<TagCode::ImageWidth>() = 640;
    tags.get<TagCode::DateTime>() = "2025:12:12 10:00:00";
    
    tags.clear();
    
    // Optional tags should be set to nullopt
    EXPECT_FALSE(tags.get<TagCode::Software>().has_value());
    EXPECT_FALSE(tags.get<TagCode::DateTime>().has_value());
    // Required tags should be default-constructed
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 0);
}

TEST(ExtractedTags, ClearVectorTags) {
    ExtractedTags<StripOffsetsTag, StripByteCountsTag> tags;
    
    tags.get<TagCode::StripOffsets>() = {100, 200, 300};
    tags.get<TagCode::StripByteCounts>() = {50, 60, 70};
    
    tags.clear();
    
    // Vectors should be cleared
    EXPECT_TRUE(tags.get<TagCode::StripOffsets>().empty());
    EXPECT_TRUE(tags.get<TagCode::StripByteCounts>().empty());
}

// ============================================================================
// Extract Tests with Sorted Tags
// ============================================================================

TEST(ExtractedTags, ExtractSortedTagsAllRequired) {
    // Create a simple tag set
    using SimpleSpec = TagSpec<ImageWidthTag, ImageLengthTag, CompressionTag>;
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    // Create tags in sorted order
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));

    uint32_t height = 768;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageLength), 
                                         TiffDataType::Long, 1, &height, sizeof(height)));

    uint16_t compression = 1;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    // Create a dummy reader
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::None);
    EXPECT_EQ(tags.get<TagCode::ImageLength>(), 768);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1024);
}

TEST(ExtractedTags, ExtractUnsortedTags) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    // Create tags in unsorted order
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    uint16_t compression = 1;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    uint32_t height = 768;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageLength), 
                                         TiffDataType::Long, 1, &height, sizeof(height)));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    // extract() should sort the tags before extraction
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::None);
    EXPECT_EQ(tags.get<TagCode::ImageLength>(), 768);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1024);
}

TEST(ExtractedTags, ExtractWithOptionalTagsPresent) {    
    ExtractedTags<ImageWidthTag, CompressionTag, OptSoftwareTag> tags;
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint16_t compression = 5;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    uint32_t width = 2048;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    // Software is an ASCII string stored externally
    const char* software_str = "TiffConcept v1.0";
    std::size_t software_len = std::strlen(software_str) + 1; // Include null terminator
    auto buffer = create_buffer_with_data(1024, 500, software_str, software_len);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::Software), 
                                         TiffDataType::Ascii, software_len, 500));
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::LZW);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 2048);
    ASSERT_TRUE(tags.get<TagCode::Software>().has_value());
    EXPECT_EQ(tags.get<TagCode::Software>().value(), "TiffConcept v1.0");
}

TEST(ExtractedTags, ExtractWithOptionalTagsMissing) {
    ExtractedTags<ImageWidthTag, CompressionTag, OptSoftwareTag> tags;
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint16_t compression = 5;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    uint32_t width = 2048;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    // Software tag is missing
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::LZW);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 2048);
    EXPECT_FALSE(tags.get<TagCode::Software>().has_value());
}

TEST(ExtractedTags, ExtractWithExternalData) {
    ExtractedTags<ImageWidthTag, StripOffsetsTag> tags;
    
    // Create a buffer with external strip offsets data
    std::vector<uint32_t> offsets = {1000, 2000, 3000, 4000};
    auto buffer = create_buffer_with_data(1024, 100, offsets.data(), offsets.size() * sizeof(uint32_t));
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), 
                                         TiffDataType::Long, 4, 100));
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1024);
    ASSERT_EQ(tags.get<TagCode::StripOffsets>().size(), 4);
    EXPECT_EQ(tags.get<TagCode::StripOffsets>()[0], 1000);
    EXPECT_EQ(tags.get<TagCode::StripOffsets>()[1], 2000);
    EXPECT_EQ(tags.get<TagCode::StripOffsets>()[2], 3000);
    EXPECT_EQ(tags.get<TagCode::StripOffsets>()[3], 4000);
}

TEST(ExtractedTags, ExtractWithRationalData) {
    ExtractedTags<ImageWidthTag, XResolutionTag, YResolutionTag> tags;
    
    // Create buffer with rational data
    struct RationalData {
        uint32_t numerator;
        uint32_t denominator;
    };
    
    RationalData x_res{300, 1};
    RationalData y_res{300, 1};
    
    std::vector<std::byte> buffer(1024, std::byte{0});
    std::memcpy(buffer.data() + 100, &x_res, sizeof(RationalData));
    std::memcpy(buffer.data() + 200, &y_res, sizeof(RationalData));
    
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 2400;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), 
                                         TiffDataType::Rational, 1, 100));
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::YResolution), 
                                         TiffDataType::Rational, 1, 200));
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 2400);
    EXPECT_EQ(tags.get<TagCode::XResolution>().numerator, 300);
    EXPECT_EQ(tags.get<TagCode::XResolution>().denominator, 1);
    EXPECT_EQ(tags.get<TagCode::YResolution>().numerator, 300);
    EXPECT_EQ(tags.get<TagCode::YResolution>().denominator, 1);
}

// ============================================================================
// Extract Strict Tests
// ============================================================================

TEST(ExtractedTags, ExtractStrictWithSortedTags) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    // Create tags in sorted order
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));

    uint32_t height = 768;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageLength), 
                                         TiffDataType::Long, 1, &height, sizeof(height)));

    uint16_t compression = 1;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));

    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract_strict<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::None);
    EXPECT_EQ(tags.get<TagCode::ImageLength>(), 768);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1024);
}

#if 0 // raises assertion failure in debug mode due to unsorted tags check
TEST(ExtractedTags, ExtractStrictWithUnsortedTagsMayFail) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    // Create tags in unsorted order
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    uint16_t compression = 1;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    uint32_t height = 768;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageLength), 
                                         TiffDataType::Long, 1, &height, sizeof(height)));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    // extract_strict assumes tags are sorted, so it may fail or not find all tags
    auto result = tags.extract_strict<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    // With unsorted tags, the two-pointer algorithm may miss tags
    // This test documents the behavior - it should fail
    EXPECT_FALSE(result);
}
#endif

// ============================================================================
// Error Cases Tests
// ============================================================================

TEST(ExtractedTags, ExtractMissingRequiredTag) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    // Only provide 2 out of 3 required tags
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint16_t compression = 1;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    // ImageLength is missing
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, Error::Code::InvalidTag);
}

TEST(ExtractedTags, ExtractEmptyIFDWithRequiredTags) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    // Empty tag buffer
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, Error::Code::InvalidTag);
}

TEST(ExtractedTags, ExtractEmptyIFDWithOnlyOptionalTags) {
    ExtractedTags<OptSoftwareTag, OptDateTimeTag, OptArtistTag> tags;
    
    // Empty tag buffer - all tags are optional
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    // Should succeed since all tags are optional
    ASSERT_TRUE(result);
    EXPECT_FALSE(tags.get<TagCode::Software>().has_value());
    EXPECT_FALSE(tags.get<TagCode::DateTime>().has_value());
    EXPECT_FALSE(tags.get<TagCode::Artist>().has_value());
}

TEST(ExtractedTags, ExtractOptionalTagWithParseFailure) {
    ExtractedTags<ImageWidthTag, OptSoftwareTag> tags;
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    // Create an invalid Software tag (wrong datatype - Short instead of Ascii)
    uint16_t invalid_software = 12345;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Software), 
                                         TiffDataType::Short, 1, &invalid_software, sizeof(invalid_software)));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    // Should succeed - optional tag parse failure is handled gracefully
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1024);
    // Optional tag should be nullopt due to parse failure
    EXPECT_FALSE(tags.get<TagCode::Software>().has_value());
}

TEST(ExtractedTags, ExtractMixedOptionalTagsPartialPresence) {
    ExtractedTags<ImageWidthTag, OptSoftwareTag, OptDateTimeTag, OptArtistTag> tags;
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 800;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    // Only DateTime is present
    const char* datetime_str = "2025:12:13 14:30:00";
    std::size_t datetime_len = std::strlen(datetime_str) + 1;
    auto buffer = create_buffer_with_data(1024, 300, datetime_str, datetime_len);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::DateTime), 
                                         TiffDataType::Ascii, datetime_len, 300));
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 800);
    EXPECT_FALSE(tags.get<TagCode::Software>().has_value());
    ASSERT_TRUE(tags.get<TagCode::DateTime>().has_value());
    EXPECT_EQ(tags.get<TagCode::DateTime>().value(), "2025:12:13 14:30:00");
    EXPECT_FALSE(tags.get<TagCode::Artist>().has_value());
}

TEST(ExtractedTags, ExtractRequiredTagParseFailure) {
    ExtractedTags<ImageWidthTag, CompressionTag> tags;
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    // Create an invalid ImageWidth tag with invalid count
    uint32_t width = 1024;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                               TiffDataType::Long, 0, &width, sizeof(width)); // count=0 is invalid
    tag_buffer.push_back(tag);
    
    uint16_t compression = 1;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    // Should fail because required tag parse failed
    EXPECT_FALSE(result);
}

// ============================================================================
// Tag Writing Helper Tests
// ============================================================================

TEST(ExtractedTags, TagExtraByteSizeInline) {
    ExtractedTags<ImageWidthTag, CompressionTag> tags;
    
    tags.get<TagCode::ImageWidth>() = 1024;
    tags.get<TagCode::Compression>() = tiff::CompressionScheme::LZW;
    
    // Both are scalar values that fit inline (4 bytes for Long, 2 bytes for Short)
    EXPECT_EQ(tags.tag_extra_byte_size<TagCode::ImageWidth>(), 0);
    EXPECT_EQ(tags.tag_extra_byte_size<TagCode::Compression>(), 0);
}

TEST(ExtractedTags, TagExtraByteSizeExternal) {
    ExtractedTags<StripOffsetsTag> tags;
    
    tags.get<TagCode::StripOffsets>() = {100, 200, 300, 400};
    
    // Vector of 4 uint32_t values = 16 bytes, doesn't fit in 4-byte inline space
    EXPECT_EQ(tags.tag_extra_byte_size<TagCode::StripOffsets>(), 16);
}

TEST(ExtractedTags, TagExtraByteSizeString) {
    ExtractedTags<SoftwareTag> tags;
    
    tags.get<TagCode::Software>() = "TiffConcept";
    
    // String "TiffConcept" + null terminator = 12 bytes, doesn't fit in 4-byte inline space
    EXPECT_EQ(tags.tag_extra_byte_size<TagCode::Software>(), 12);
}

TEST(ExtractedTags, TagExtraByteSizeOptionalEmpty) {
    ExtractedTags<SoftwareTag> tags;
    
    // Optional tag with no value
    EXPECT_EQ(tags.tag_extra_byte_size<TagCode::Software>(), 0);
}

TEST(ExtractedTags, TagNeedsExternalData) {
    ExtractedTags<ImageWidthTag, StripOffsetsTag, SoftwareTag> tags;
    
    tags.get<TagCode::ImageWidth>() = 1024;
    tags.get<TagCode::StripOffsets>() = {100, 200, 300};
    tags.get<TagCode::Software>() = "Test";
    
    EXPECT_FALSE(tags.tag_needs_external_data<TagCode::ImageWidth>());
    EXPECT_TRUE(tags.tag_needs_external_data<TagCode::StripOffsets>());
    EXPECT_TRUE((tags.tag_needs_external_data<TagCode::Software, TiffFormatType::Classic>())); // "Test" + null = 5 bytes, doesn't fit in Classic TIFF
    EXPECT_FALSE((tags.tag_needs_external_data<TagCode::Software, TiffFormatType::BigTIFF>())); // "Test" + null = 5 bytes, fits in BigTIFF
}

TEST(ExtractedTags, ExtraByteSizeTotal) {
    ExtractedTags<ImageWidthTag, StripOffsetsTag, StripByteCountsTag, SoftwareTag> tags;
    
    tags.get<TagCode::ImageWidth>() = 1024;
    tags.get<TagCode::StripOffsets>() = {100, 200, 300};
    tags.get<TagCode::StripByteCounts>() = {50, 60, 70};
    tags.get<TagCode::Software>() = "TiffConcept v1.0";
    
    // ImageWidth: 0 (inline)
    // StripOffsets: 12 bytes (3 * 4)
    // StripByteCounts: 12 bytes (3 * 4)
    // Software: 17 bytes ("TiffConcept v1.0" + null)
    EXPECT_EQ(tags.extra_byte_size(), 12 + 12 + 17);
}

TEST(ExtractedTags, ExtraByteSizeBigTIFF) {
    ExtractedTags<ImageWidthTag, SoftwareTag> tags;
    
    tags.get<TagCode::ImageWidth>() = 1024;
    tags.get<TagCode::Software>() = "Test";
    
    // In Classic TIFF, inline limit is 4 bytes
    EXPECT_EQ(tags.extra_byte_size<TiffFormatType::Classic>(), 5); // "Test" + null
    
    // In BigTIFF, inline limit is 8 bytes
    EXPECT_EQ(tags.extra_byte_size<TiffFormatType::BigTIFF>(), 0); // Fits inline
}

TEST(ExtractedTags, TagWriteExternalData) {
    ExtractedTags<StripOffsetsTag> tags;
    
    tags.get<TagCode::StripOffsets>() = {100, 200, 300};
    
    std::vector<std::byte> buffer(12);
    
    auto result = tags.tag_write_external_data<TagCode::StripOffsets>(std::span(buffer));
    
    ASSERT_TRUE(result);
    
    // Verify the data was written correctly
    uint32_t* data = reinterpret_cast<uint32_t*>(buffer.data());
    EXPECT_EQ(data[0], 100);
    EXPECT_EQ(data[1], 200);
    EXPECT_EQ(data[2], 300);
}

TEST(ExtractedTags, TagWriteExternalDataString) {
    ExtractedTags<SoftwareTag> tags;
    
    tags.get<TagCode::Software>() = "TiffConcept";
    
    std::vector<std::byte> buffer(12);
    
    auto result = tags.tag_write_external_data<TagCode::Software>(std::span(buffer));
    
    ASSERT_TRUE(result);
    
    // Verify the string was written correctly
    const char* str = reinterpret_cast<const char*>(buffer.data());
    EXPECT_STREQ(str, "TiffConcept");
}

TEST(ExtractedTags, TagWriteExternalDataBufferTooSmall) {
    ExtractedTags<StripOffsetsTag> tags;
    
    tags.get<TagCode::StripOffsets>() = {100, 200, 300};
    
    std::vector<std::byte> buffer(8); // Too small, needs 12 bytes
    
    auto result = tags.tag_write_external_data<TagCode::StripOffsets>(std::span(buffer));
    
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, Error::Code::OutOfBounds);
}

TEST(ExtractedTags, TagWriteExternalDataInlineValue) {
    ExtractedTags<ImageWidthTag> tags;
    
    tags.get<TagCode::ImageWidth>() = 1024;
    
    std::vector<std::byte> buffer(4);
    
    // Should fail because this tag doesn't need external data
    auto result = tags.tag_write_external_data<TagCode::ImageWidth>(std::span(buffer));
    
    EXPECT_FALSE(result);
}

TEST(ExtractedTags, TagWriteExternalDataOptionalEmpty) {
    ExtractedTags<SoftwareTag> tags;
    
    // Optional tag with no value
    std::vector<std::byte> buffer(10);
    
    auto result = tags.tag_write_external_data<TagCode::Software>(std::span(buffer));
    
    EXPECT_FALSE(result);
}

TEST(ExtractedTags, TagWriteExternalDataWithEndianness) {
    ExtractedTags<StripOffsetsTag> tags;
    
    tags.get<TagCode::StripOffsets>() = {0x12345678, 0x9ABCDEF0};
    
    std::vector<std::byte> buffer(8);
    
    // Write with little endian
    auto result = tags.tag_write_external_data<TagCode::StripOffsets, TiffFormatType::Classic, std::endian::little>(
        std::span(buffer));
    
    ASSERT_TRUE(result);
    
    // Verify endianness conversion if needed
    if constexpr (std::endian::native == std::endian::big) {
        // On big-endian systems, data should be byte-swapped
        uint32_t* data = reinterpret_cast<uint32_t*>(buffer.data());
        EXPECT_EQ(byteswap(data[0]), 0x12345678);
        EXPECT_EQ(byteswap(data[1]), 0x9ABCDEF0);
    } else {
        // On little-endian systems, no swap needed
        uint32_t* data = reinterpret_cast<uint32_t*>(buffer.data());
        EXPECT_EQ(data[0], 0x12345678);
        EXPECT_EQ(data[1], 0x9ABCDEF0);
    }
}

// ============================================================================
// Complex Integration Tests
// ============================================================================

TEST(ExtractedTags, RoundTripExtractAndWrite) {
    ExtractedTags<ImageWidthTag, CompressionTag, StripOffsetsTag> tags;
    
    // Create tags with external data
    std::vector<uint32_t> offsets = {1000, 2000, 3000};
    auto buffer = create_buffer_with_data(1024, 100, offsets.data(), offsets.size() * sizeof(uint32_t));
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint16_t compression = 5;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), 
                                         TiffDataType::Long, 3, 100));
    
    // Extract
    auto extract_result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(extract_result);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::LZW);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1024);
    EXPECT_EQ(tags.get<TagCode::StripOffsets>().size(), 3);
    
    // Write external data back
    std::size_t write_size = tags.tag_extra_byte_size<TagCode::StripOffsets>();
    EXPECT_EQ(write_size, 12);
    
    std::vector<std::byte> write_buffer(write_size);
    auto write_result = tags.tag_write_external_data<TagCode::StripOffsets>(std::span(write_buffer));
    
    ASSERT_TRUE(write_result);
    
    // Verify written data matches original
    uint32_t* written_data = reinterpret_cast<uint32_t*>(write_buffer.data());
    EXPECT_EQ(written_data[0], 1000);
    EXPECT_EQ(written_data[1], 2000);
    EXPECT_EQ(written_data[2], 3000);
}

TEST(ExtractedTags, MultipleExtractCallsReuseMemory) {
    ExtractedTags<StripOffsetsTag, StripByteCountsTag> tags;
    
    // First extraction
    {
        std::vector<uint32_t> offsets1 = {100, 200};
        std::vector<uint32_t> counts1 = {50, 60};
        
        auto buffer = create_buffer_with_data(1024, 100, offsets1.data(), offsets1.size() * sizeof(uint32_t));
        std::memcpy(buffer.data() + 200, counts1.data(), counts1.size() * sizeof(uint32_t));
        BufferViewReader reader{std::span<const std::byte>(buffer)};
        
        std::vector<TiffTag<std::endian::native>> tag_buffer;
        tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::StripByteCounts), 
                                             TiffDataType::Long, 2, 200));
        tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), 
                                             TiffDataType::Long, 2, 100));
        
        auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
            reader, std::span(tag_buffer));
        
        ASSERT_TRUE(result);
        EXPECT_EQ(tags.get<TagCode::StripOffsets>().size(), 2);
        EXPECT_EQ(tags.get<TagCode::StripByteCounts>().size(), 2);
    }
    
    // Second extraction with different sizes - should reuse memory where possible
    {
        std::vector<uint32_t> offsets2 = {300, 400, 500};
        std::vector<uint32_t> counts2 = {70, 80, 90};
        
        auto buffer = create_buffer_with_data(1024, 100, offsets2.data(), offsets2.size() * sizeof(uint32_t));
        std::memcpy(buffer.data() + 200, counts2.data(), counts2.size() * sizeof(uint32_t));
        BufferViewReader reader{std::span<const std::byte>(buffer)};
        
        std::vector<TiffTag<std::endian::native>> tag_buffer;
        tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::StripByteCounts), 
                                             TiffDataType::Long, 3, 200));
        tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), 
                                             TiffDataType::Long, 3, 100));
        
        auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
            reader, std::span(tag_buffer));
        
        ASSERT_TRUE(result);
        EXPECT_EQ(tags.get<TagCode::StripOffsets>().size(), 3);
        EXPECT_EQ(tags.get<TagCode::StripOffsets>()[0], 300);
        EXPECT_EQ(tags.get<TagCode::StripOffsets>()[2], 500);
        EXPECT_EQ(tags.get<TagCode::StripByteCounts>().size(), 3);
    }
}

// ============================================================================
// BigTIFF Format Tests
// ============================================================================

TEST(ExtractedTags, ExtractBigTIFFFormat) {
    ExtractedTags<ImageWidthTag, CompressionTag> tags;
    
    std::vector<TiffBigTag<std::endian::native>> tag_buffer;
    
    // Create BigTIFF tags
    TiffBigTag<std::endian::native> width_tag{};
    width_tag.code = static_cast<uint16_t>(TagCode::ImageWidth);
    width_tag.datatype = TiffDataType::Long;
    width_tag.count = 1;
    uint32_t width = 4096;
    std::memcpy(&width_tag.value, &width, sizeof(width));
    tag_buffer.push_back(width_tag);
    
    TiffBigTag<std::endian::native> compression_tag{};
    compression_tag.code = static_cast<uint16_t>(TagCode::Compression);
    compression_tag.datatype = TiffDataType::Short;
    compression_tag.count = 1;
    uint16_t compression = 5;
    std::memcpy(&compression_tag.value, &compression, sizeof(compression));
    tag_buffer.push_back(compression_tag);
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::BigTIFF, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 4096);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::LZW);
}

TEST(ExtractedTags, TagExtraByteSizeBigTIFFInlineLimit) {
    ExtractedTags<StripOffsetsTag, SoftwareTag> tags;
    
    // String with 7 characters + null = 8 bytes (fits in BigTIFF inline, not Classic)
    tags.get<TagCode::Software>() = "Testing";
    tags.get<TagCode::StripOffsets>() = {100, 200};
    
    // Classic TIFF: inline limit is 4 bytes
    EXPECT_EQ((tags.tag_extra_byte_size<TagCode::Software, TiffFormatType::Classic>()), 8);
    EXPECT_EQ((tags.tag_extra_byte_size<TagCode::StripOffsets, TiffFormatType::Classic>()), 8);
    
    // BigTIFF: inline limit is 8 bytes
    EXPECT_EQ((tags.tag_extra_byte_size<TagCode::Software, TiffFormatType::BigTIFF>()), 0); // Fits inline
    EXPECT_EQ((tags.tag_extra_byte_size<TagCode::StripOffsets, TiffFormatType::BigTIFF>()), 0); // Fits inline
}

TEST(ExtractedTags, ExtraByteSizeTotalBigTIFF) {
    ExtractedTags<ImageWidthTag, StripOffsetsTag, SoftwareTag> tags;
    
    tags.get<TagCode::ImageWidth>() = 2048;
    tags.get<TagCode::StripOffsets>() = {100, 200}; // 8 bytes
    tags.get<TagCode::Software>() = "Test123"; // 8 bytes with null
    
    // Classic TIFF: both StripOffsets and Software need external data
    EXPECT_EQ(tags.extra_byte_size<TiffFormatType::Classic>(), 16);
    
    // BigTIFF: both fit inline
    EXPECT_EQ(tags.extra_byte_size<TiffFormatType::BigTIFF>(), 0);
}

// ============================================================================
// Endianness Tests
// ============================================================================

TEST(ExtractedTags, ExtractWithBigEndianSource) {
    ExtractedTags<ImageWidthTag, CompressionTag> tags;
    
    std::vector<TiffTag<std::endian::big>> tag_buffer;
    
    // Create tags with big-endian byte order
    uint32_t width = 1024;
    width = byteswap(width);
    tag_buffer.push_back(make_inline_tag<std::endian::big>(
        static_cast<uint16_t>(TagCode::ImageWidth), 
        TiffDataType::Long, 1, &width, sizeof(width)));
    
    uint16_t compression = 5;
    compression = byteswap(compression);
    tag_buffer.push_back(make_inline_tag<std::endian::big>(
        static_cast<uint16_t>(TagCode::Compression), 
        TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::big>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1024);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::LZW);
}

TEST(ExtractedTags, ExtractWithLittleEndianSource) {
    ExtractedTags<ImageWidthTag, CompressionTag> tags;
    
    std::vector<TiffTag<std::endian::little>> tag_buffer;
    
    uint32_t width = 2048;
    tag_buffer.push_back(make_inline_tag<std::endian::little>(
        static_cast<uint16_t>(TagCode::ImageWidth), 
        TiffDataType::Long, 1, &width, sizeof(width)));
    
    uint16_t compression = 1;
    tag_buffer.push_back(make_inline_tag<std::endian::little>(
        static_cast<uint16_t>(TagCode::Compression), 
        TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 2048);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::None);
}

TEST(ExtractedTags, TagWriteExternalDataBigEndian) {
    ExtractedTags<StripOffsetsTag> tags;
    
    tags.get<TagCode::StripOffsets>() = {0x12345678, 0x9ABCDEF0, 0xCAFEBABE};
    
    std::vector<std::byte> buffer(12);
    
    auto result = tags.tag_write_external_data<TagCode::StripOffsets, TiffFormatType::Classic, std::endian::big>(
        std::span(buffer));
    
    ASSERT_TRUE(result);
    
    // Verify big-endian byte order
    if constexpr (std::endian::native == std::endian::little) {
        // On little-endian systems, data should be byte-swapped
        uint32_t* data = reinterpret_cast<uint32_t*>(buffer.data());
        EXPECT_EQ(byteswap(data[0]), 0x12345678);
        EXPECT_EQ(byteswap(data[1]), 0x9ABCDEF0);
        EXPECT_EQ(byteswap(data[2]), 0xCAFEBABE);
    } else {
        // On big-endian systems, no swap needed
        uint32_t* data = reinterpret_cast<uint32_t*>(buffer.data());
        EXPECT_EQ(data[0], 0x12345678);
        EXPECT_EQ(data[1], 0x9ABCDEF0);
        EXPECT_EQ(data[2], 0xCAFEBABE);
    }
}

TEST(ExtractedTags, TagWriteExternalDataBigTIFF) {
    ExtractedTags<StripOffsetsTag> tags;
    
    tags.get<TagCode::StripOffsets>() = {1000, 2000, 3000};
    
    std::vector<std::byte> buffer(12);
    
    // BigTIFF format (inline limit is 8, but we have 12 bytes of data)
    auto result = tags.tag_write_external_data<TagCode::StripOffsets, TiffFormatType::BigTIFF>(
        std::span(buffer));
    
    ASSERT_TRUE(result);
    
    uint32_t* data = reinterpret_cast<uint32_t*>(buffer.data());
    EXPECT_EQ(data[0], 1000);
    EXPECT_EQ(data[1], 2000);
    EXPECT_EQ(data[2], 3000);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(ExtractedTags, NumTagsVariousCounts) {
    ExtractedTags<ImageWidthTag> single_tag;
    EXPECT_EQ(single_tag.num_tags(), 1);
    
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag, 
                  PhotometricInterpretationTag, SamplesPerPixelTag> five_tags;
    EXPECT_EQ(five_tags.num_tags(), 5);
}

TEST(ExtractedTags, ClearSingleTag) {
    ExtractedTags<ImageWidthTag> tags;
    
    tags.get<TagCode::ImageWidth>() = 1920;
    tags.clear();
    
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 0);
}

TEST(ExtractedTags, ClearSingleOptionalTag) {
    ExtractedTags<OptSoftwareTag> tags;
    
    tags.get<TagCode::Software>() = "SingleTag";
    tags.clear();
    
    EXPECT_FALSE(tags.get<TagCode::Software>().has_value());
}

TEST(ExtractedTags, ErrorMessageContent) {
    ExtractedTags<ImageWidthTag, ImageLengthTag> tags;
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    // ImageLength is missing
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, Error::Code::InvalidTag);
    
    // Verify error message contains tag code
    std::string error_msg = result.error().message;
    std::string tag_code_str = std::to_string(static_cast<uint16_t>(TagCode::ImageLength));
    EXPECT_NE(error_msg.find(tag_code_str), std::string::npos);
    EXPECT_NE(error_msg.find("not found"), std::string::npos);
}

TEST(ExtractedTags, ErrorMessageEmptyIFD) {
    ExtractedTags<ImageWidthTag> tags;
    
    std::vector<TiffTag<std::endian::native>> tag_buffer; // Empty
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, Error::Code::InvalidTag);
    EXPECT_NE(result.error().message.find("empty IFD"), std::string::npos);
}

TEST(ExtractedTags, ExtractAlreadySortedOptimization) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, CompressionTag> tags;
    
    // Create tags already in sorted order
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint16_t compression = 1;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    uint32_t height = 768;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageLength), 
                                         TiffDataType::Long, 1, &height, sizeof(height)));
    
    uint32_t width = 1024;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    // This should take the "already sorted" fast path
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::None);
    EXPECT_EQ(tags.get<TagCode::ImageLength>(), 768);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1024);
}

TEST(ExtractedTags, ExtractStrictAlreadySorted) {
    ExtractedTags<ImageWidthTag, CompressionTag> tags;
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 1280;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    uint16_t compression = 5;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::Compression), 
                                         TiffDataType::Short, 1, &compression, sizeof(compression)));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    // extract_strict should work with pre-sorted tags
    auto result = tags.extract_strict<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::Compression>(), tiff::CompressionScheme::LZW);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1280);
}

// ============================================================================
// Complex Scenarios
// ============================================================================

TEST(ExtractedTags, MixedInlineAndExternalData) {
    ExtractedTags<ImageWidthTag, ImageLengthTag, StripOffsetsTag, StripByteCountsTag, 
                  XResolutionTag, SoftwareTag> tags;
    
    // Prepare external data
    std::vector<uint32_t> offsets = {1000, 2000, 3000, 4000};
    std::vector<uint32_t> counts = {500, 600, 700, 800};
    const char* software = "ComplexTest";
    struct RationalData { uint32_t num; uint32_t den; } x_res{300, 1};
    
    std::vector<std::byte> buffer(2048, std::byte{0});
    std::memcpy(buffer.data() + 100, offsets.data(), offsets.size() * sizeof(uint32_t));
    std::memcpy(buffer.data() + 200, counts.data(), counts.size() * sizeof(uint32_t));
    std::memcpy(buffer.data() + 300, software, std::strlen(software) + 1);
    std::memcpy(buffer.data() + 400, &x_res, sizeof(RationalData));
    
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    
    uint32_t width = 1920;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), 
                                         TiffDataType::Long, 1, &width, sizeof(width)));
    
    uint32_t height = 1080;
    tag_buffer.push_back(make_inline_tag(static_cast<uint16_t>(TagCode::ImageLength), 
                                         TiffDataType::Long, 1, &height, sizeof(height)));
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), 
                                         TiffDataType::Long, 4, 100));
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::StripByteCounts), 
                                         TiffDataType::Long, 4, 200));
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::Software), 
                                         TiffDataType::Ascii, std::strlen(software) + 1, 300));
    
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), 
                                         TiffDataType::Rational, 1, 400));
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 1920);
    EXPECT_EQ(tags.get<TagCode::ImageLength>(), 1080);
    EXPECT_EQ(tags.get<TagCode::StripOffsets>().size(), 4);
    EXPECT_EQ(tags.get<TagCode::StripOffsets>()[3], 4000);
    EXPECT_EQ(tags.get<TagCode::StripByteCounts>()[3], 800);
    EXPECT_EQ(tags.get<TagCode::Software>(), "ComplexTest");
    EXPECT_EQ(tags.get<TagCode::XResolution>().numerator, 300);
}

TEST(ExtractedTags, AllOptionalTagsWithMixedPresence) {
    ExtractedTags<OptSoftwareTag, OptDateTimeTag, OptArtistTag> tags;
    
    // Only Software is present
    const char* software = "PartialPresence";
    auto buffer = create_buffer_with_data(1024, 100, software, std::strlen(software) + 1);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    std::vector<TiffTag<std::endian::native>> tag_buffer;
    tag_buffer.push_back(make_offset_tag(static_cast<uint16_t>(TagCode::Software), 
                                         TiffDataType::Ascii, std::strlen(software) + 1, 100));
    
    auto result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::native>(
        reader, std::span(tag_buffer));
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(tags.get<TagCode::Software>().has_value());
    EXPECT_EQ(tags.get<TagCode::Software>().value(), "PartialPresence");
    EXPECT_FALSE(tags.get<TagCode::DateTime>().has_value());
    EXPECT_FALSE(tags.get<TagCode::Artist>().has_value());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
