#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstring>
#include <vector>

#include "../tiffconcept/include/tiffconcept/ifd.hpp"
#include "../tiffconcept/include/tiffconcept/lowlevel/ifd_builder.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_buffer.hpp"
#include "../tiffconcept/include/tiffconcept/tag_extraction.hpp"
#include "../tiffconcept/include/tiffconcept/types/tag_spec.hpp"
#include "../tiffconcept/include/tiffconcept/types/tag_spec_examples.hpp"
#include "../tiffconcept/include/tiffconcept/types/tiff_spec.hpp"
#include "../tiffconcept/include/tiffconcept/strategy/write_strategy.hpp"

using namespace tiffconcept;

// ============================================================================
// Helper Functions
// ============================================================================

/// Create minimal ExtractedTags for testing
auto create_minimal_tags() {
    ExtractedTags<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag,
        CompressionTag,
        PhotometricInterpretationTag
    > tags;
    
    tags.get<TagCode::ImageWidth>() = 512;
    tags.get<TagCode::ImageLength>() = 512;
    tags.get<TagCode::BitsPerSample>() = std::vector<uint16_t>{8, 8, 8};
    tags.get<TagCode::Compression>() = CompressionScheme::None;
    tags.get<TagCode::PhotometricInterpretation>() = PhotometricInterpretation::RGB;
    
    return tags;
}

// ============================================================================
// Basic IFDBuilder Tests
// ============================================================================

TEST(IFDBuilder, CreateEmpty) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    // Should be able to calculate sizes even with no tags
    EXPECT_EQ(builder.calculate_external_data_size(), 0);
}

TEST(IFDBuilder, AddMinimalTags) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    auto tags = create_minimal_tags();
    auto result = builder.add_tags(tags);
    
    ASSERT_TRUE(result.is_ok());
    
    // BitsPerSample (3 x uint16_t = 6 bytes) exceeds 4-byte inline limit
    EXPECT_EQ(builder.calculate_external_data_size(), 6);
    
    // IFD should contain 5 tags
    std::size_t expected_size = sizeof(uint16_t) +  // num_entries
                               5 * sizeof(TiffTag<std::endian::native>) +
                               sizeof(uint32_t);  // next_ifd_offset
    EXPECT_EQ(builder.calculate_ifd_size(), expected_size);
}

TEST(IFDBuilder, AddTagsWithExternalData) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    ExtractedTags<ImageWidthTag, ImageLengthTag, BitsPerSampleTag> tags;
    tags.get<TagCode::ImageWidth>() = 512;
    tags.get<TagCode::ImageLength>() = 512;
    tags.get<TagCode::BitsPerSample>() = std::vector<uint16_t>{8, 8, 8};
    
    auto result = builder.add_tags(tags);
    ASSERT_TRUE(result.is_ok());
    
    // Vector of 3 uint16_t should require external storage
    EXPECT_GT(builder.calculate_external_data_size(), 0);
    EXPECT_EQ(builder.calculate_external_data_size(), 3 * sizeof(uint16_t));
}

TEST(IFDBuilder, AddSingleTag) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    auto result = builder.add_tag<ImageWidthTag>(uint32_t{1024});
    ASSERT_TRUE(result.is_ok());
    
    // Should have 1 tag
    std::size_t expected_size = sizeof(uint16_t) +
                               1 * sizeof(TiffTag<std::endian::native>) +
                               sizeof(uint32_t);
    EXPECT_EQ(builder.calculate_ifd_size(), expected_size);
}

TEST(IFDBuilder, AddMultipleSingleTags) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    ASSERT_TRUE(builder.add_tag<ImageWidthTag>(uint32_t{512}).is_ok());
    ASSERT_TRUE(builder.add_tag<ImageLengthTag>(uint32_t{512}).is_ok());
    ASSERT_TRUE(builder.add_tag<CompressionTag>(CompressionScheme::ZSTD).is_ok());
    
    // Tags should be sorted by code
    auto ifd_result = builder.build(ifd::IFDOffset(1000));
    ASSERT_TRUE(ifd_result.is_ok());
    
    const auto& ifd = ifd_result.value();
    EXPECT_EQ(ifd.tags.size(), 3);
    
    // Verify sorting (Compression=259, ImageLength=257, ImageWidth=256)
    uint16_t prev_code = 0;
    for (const auto& tag : ifd.tags) {
        uint16_t code = tag.get_code<std::endian::native>();
        EXPECT_GT(code, prev_code);
        prev_code = code;
    }
}

// ============================================================================
// Offset Calculation Tests
// ============================================================================

TEST(IFDBuilder, SetExternalDataOffset) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    builder.set_external_data_offset(5000);
    
    // Add tag with external data
    auto result = builder.add_tag<BitsPerSampleTag>(std::vector<uint16_t>{16, 16, 16});
    ASSERT_TRUE(result.is_ok());
    
    // External data should exist
    EXPECT_GT(builder.calculate_external_data_size(), 0);
}

TEST(IFDBuilder, SetNextIFDOffset) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    auto tags = create_minimal_tags();
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    builder.set_next_ifd_offset(ifd::IFDOffset(2000));
    
    auto ifd_result = builder.build(ifd::IFDOffset(1000));
    ASSERT_TRUE(ifd_result.is_ok());
    
    const auto& ifd = ifd_result.value();
    EXPECT_EQ(ifd.next_ifd_offset.value, 2000);
}

// ============================================================================
// Build Tests
// ============================================================================

TEST(IFDBuilder, BuildIFD) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    auto tags = create_minimal_tags();
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    auto ifd_result = builder.build(ifd::IFDOffset(1000));
    ASSERT_TRUE(ifd_result.is_ok());
    
    const auto& ifd = ifd_result.value();
    EXPECT_EQ(ifd.description.offset.value, 1000);
    EXPECT_EQ(ifd.description.num_entries, 5);
    EXPECT_EQ(ifd.tags.size(), 5);
    EXPECT_EQ(ifd.next_ifd_offset.value, 0);  // Default null
}

TEST(IFDBuilder, GetExternalData) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    std::vector<uint16_t> bits_per_sample = {8, 8, 8, 8};
    ASSERT_TRUE(builder.add_tag<BitsPerSampleTag>(bits_per_sample).is_ok());
    
    auto external_data = builder.get_external_data();
    EXPECT_EQ(external_data.size(), 4 * sizeof(uint16_t));
    
    // Verify data content
    std::vector<uint16_t> read_back(4);
    std::memcpy(read_back.data(), external_data.data(), external_data.size());
    EXPECT_EQ(read_back, bits_per_sample);
}

// ============================================================================
// Write to File Tests
// ============================================================================

TEST(IFDBuilder, WriteToBuffer) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    auto tags = create_minimal_tags();
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    BufferWriter writer;
    
    auto offset_result = builder.write_to_file(writer, 0);
    ASSERT_TRUE(offset_result.is_ok());
    
    // IFD should have been written
    auto size_result = writer.size();
    ASSERT_TRUE(size_result.is_ok());
    EXPECT_GT(size_result.value(), 0);
}

TEST(IFDBuilder, WriteWithExternalData) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    ASSERT_TRUE(builder.add_tag<ImageWidthTag>(uint32_t{512}).is_ok());
    ASSERT_TRUE(builder.add_tag<BitsPerSampleTag>(
        std::vector<uint16_t>{16, 16, 16}).is_ok());
    
    BufferWriter writer;
    
    auto offset_result = builder.write_to_file(writer, 0);
    ASSERT_TRUE(offset_result.is_ok());
    
    // Should have written IFD + external data
    auto size_result = writer.size();
    ASSERT_TRUE(size_result.is_ok());
    
    std::size_t expected_min = builder.calculate_total_size();
    EXPECT_GE(size_result.value(), expected_min);
}

// ============================================================================
// Clear and Reuse Tests
// ============================================================================

TEST(IFDBuilder, Clear) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    auto tags = create_minimal_tags();
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    EXPECT_GT(builder.calculate_ifd_size(), 0);
    
    builder.clear();
    
    // Should be empty after clear
    std::size_t empty_size = sizeof(uint16_t) + sizeof(uint32_t);  // Just header and next offset
    EXPECT_EQ(builder.calculate_ifd_size(), empty_size);
    EXPECT_EQ(builder.calculate_external_data_size(), 0);
}

TEST(IFDBuilder, ReuseAfterClear) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    // First use
    auto tags1 = create_minimal_tags();
    ASSERT_TRUE(builder.add_tags(tags1).is_ok());
    
    BufferWriter writer1;
    ASSERT_TRUE(builder.write_to_file(writer1, 0).is_ok());
    
    // Clear and reuse
    builder.clear();
    
    // Second use
    ASSERT_TRUE(builder.add_tag<ImageWidthTag>(uint32_t{1024}).is_ok());
    
    BufferWriter writer2;
    ASSERT_TRUE(builder.write_to_file(writer2, 0).is_ok());
}

// ============================================================================
// Different Strategies Tests
// ============================================================================

TEST(IFDBuilder, IFDAtBeginningStrategy) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtBeginning> builder;
    
    auto tags = create_minimal_tags();
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    BufferWriter writer;
    
    // With IFDAtBeginning, IFD should be at current file position
    auto offset_result = builder.write_to_file(writer, 8);  // After header
    ASSERT_TRUE(offset_result.is_ok());
    
    // IFD offset should be at beginning
    EXPECT_EQ(offset_result.value().value, 8);
}

TEST(IFDBuilder, IFDAtEndStrategy) {
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    
    auto tags = create_minimal_tags();
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    BufferWriter writer;
    
    // With IFDAtEnd, IFD goes at current position
    auto offset_result = builder.write_to_file(writer, 1000);
    ASSERT_TRUE(offset_result.is_ok());
    
    EXPECT_EQ(offset_result.value().value, 1000);
}

TEST(IFDBuilder, IFDInlineStrategy) {
    IFDInline strategy;
    strategy.preferred_offset = 500;
    
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDInline> builder(strategy);
    
    auto tags = create_minimal_tags();
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    BufferWriter writer;
    
    auto offset_result = builder.write_to_file(writer, 1000);
    ASSERT_TRUE(offset_result.is_ok());
    
    // Should use preferred offset
    EXPECT_EQ(offset_result.value().value, 500);
}

// ============================================================================
// BigTIFF Tests
// ============================================================================

TEST(IFDBuilder, BigTIFFFormat) {
    IFDBuilder<TiffFormatType::BigTIFF, std::endian::native, IFDAtEnd> builder;
    
    auto tags = create_minimal_tags();
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    // BigTIFF has larger structures
    std::size_t expected_size = sizeof(uint64_t) +  // num_entries (8 bytes)
                               5 * sizeof(TiffBigTag<std::endian::native>) +
                               sizeof(uint64_t);  // next_ifd_offset (8 bytes)
    EXPECT_EQ(builder.calculate_ifd_size(), expected_size);
}

TEST(IFDBuilder, BigTIFFWithExternalData) {
    IFDBuilder<TiffFormatType::BigTIFF, std::endian::native, IFDAtEnd> builder;
    
    std::vector<uint16_t> bits = {16, 16, 16, 16};
    ASSERT_TRUE(builder.add_tag<BitsPerSampleTag>(bits).is_ok());
    
    BufferWriter writer;
    auto offset_result = builder.write_to_file(writer, 16);  // After BigTIFF header
    ASSERT_TRUE(offset_result.is_ok());
}

// ============================================================================
// Endianness Tests
// ============================================================================

TEST(IFDBuilder, LittleEndian) {
    IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd> builder;
    
    ASSERT_TRUE(builder.add_tag<ImageWidthTag>(uint32_t{0x12345678}).is_ok());
    
    auto ifd_result = builder.build(ifd::IFDOffset(1000));
    ASSERT_TRUE(ifd_result.is_ok());
    
    // Tags should be in little-endian format
    const auto& ifd = ifd_result.value();
    EXPECT_EQ(ifd.tags.size(), 1);
}

TEST(IFDBuilder, BigEndian) {
    IFDBuilder<TiffFormatType::Classic, std::endian::big, IFDAtEnd> builder;
    
    ASSERT_TRUE(builder.add_tag<ImageWidthTag>(uint32_t{0x12345678}).is_ok());
    
    auto ifd_result = builder.build(ifd::IFDOffset(1000));
    ASSERT_TRUE(ifd_result.is_ok());
    
    const auto& ifd = ifd_result.value();
    EXPECT_EQ(ifd.tags.size(), 1);
}

// ============================================================================
// Optional Tags Tests
// ============================================================================

TEST(IFDBuilder, OptionalTagPresent) {  
    ExtractedTags<
        ImageWidthTag,
        TagDescriptor<TagCode::XResolution, TiffDataType::Rational, Rational, true>
    > tags;
    
    tags.get<TagCode::ImageWidth>() = 512;
    tags.get<TagCode::XResolution>() = Rational{300, 1};
    
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    // Should have 2 tags
    auto ifd_result = builder.build(ifd::IFDOffset(1000));
    ASSERT_TRUE(ifd_result.is_ok());
    EXPECT_EQ(ifd_result.value().tags.size(), 2);
}

TEST(IFDBuilder, OptionalTagAbsent) {
    ExtractedTags<
        ImageWidthTag,
        TagDescriptor<TagCode::XResolution, TiffDataType::Rational, Rational, true>
    > tags;
    
    tags.get<TagCode::ImageWidth>() = 512;
    // XResolution not set (optional)
    
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    // Should have only 1 tag (XResolution skipped)
    auto ifd_result = builder.build(ifd::IFDOffset(1000));
    ASSERT_TRUE(ifd_result.is_ok());
    EXPECT_EQ(ifd_result.value().tags.size(), 1);
}

// ============================================================================
// String Tag Tests
// ============================================================================

TEST(IFDBuilder, StringTagShort) {
    ExtractedTags<TagDescriptor<TagCode::Software, TiffDataType::Ascii, std::string>> tags;
    tags.get<TagCode::Software>() = "Hi!";  // 4 bytes - fits inline
    
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    // Short string should fit inline
    EXPECT_EQ(builder.calculate_external_data_size(), 0);
}

TEST(IFDBuilder, StringTagLong) {
    ExtractedTags<TagDescriptor<TagCode::Software, TiffDataType::Ascii, std::string>> tags;
    tags.get<TagCode::Software>() = "This is a long software string";
    
    IFDBuilder<TiffFormatType::Classic, std::endian::native, IFDAtEnd> builder;
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    // Long string needs external storage
    EXPECT_GT(builder.calculate_external_data_size(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
