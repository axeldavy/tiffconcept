#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstring>
#include <vector>

#include "../tiffconcept/include/tiffconcept/parsing.hpp"
#include "../tiffconcept/include/tiffconcept/tag_spec.hpp"
#include "../tiffconcept/include/tiffconcept/tag_writing.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_buffer.hpp"
#include "../tiffconcept/include/tiffconcept/types.hpp"

using namespace tiffconcept;

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
// Scalar Tag Parsing Tests
// ============================================================================

TEST(TagParsing, ParseScalarByte) {
    uint8_t value = 42;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::FillOrder), TiffDataType::Byte, 1, &value, sizeof(value));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::FillOrder, TiffDataType::Byte, uint8_t>;
    auto result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 42);
}

TEST(TagParsing, ParseScalarShort) {
    uint16_t value = 1234;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::Compression), TiffDataType::Short, 1, &value, sizeof(value));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::Compression, TiffDataType::Short, uint16_t>;
    auto result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 1234);
}

TEST(TagParsing, ParseScalarLong) {
    uint32_t value = 0x12345678;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Long, 1, &value, sizeof(value));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    auto result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 0x12345678);
}

TEST(TagParsing, ParseScalarFloat) {
    float value = 3.14159f;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Float, 1, &value, sizeof(value));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Float, float>;
    auto result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_FLOAT_EQ(result.value(), 3.14159f);
}

TEST(TagParsing, ParseScalarEnum) {
    uint16_t value = static_cast<uint16_t>(CompressionScheme::None);
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::Compression), TiffDataType::Short, 1, &value, sizeof(value));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = parsing::parse_tag<BufferViewReader, CompressionTag>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), CompressionScheme::None);
}

// ============================================================================
// Rational Tag Parsing Tests
// ============================================================================

TEST(TagParsing, ParseRationalInline) {
    std::array<uint32_t, 2> rational = {300, 1};
    std::size_t offset = 100;
    auto buffer = create_buffer_with_data(500, offset, rational.data(), sizeof(rational));
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Rational, 1, offset);
    
    auto result = parsing::parse_tag<BufferViewReader, XResolutionTag>(reader, tag);
    
    if (!result) {
        std::cerr << "Parse failed: code=" << static_cast<int>(result.error().code) << ", message=" << result.error().message << std::endl;
    }
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().numerator, 300u);
    EXPECT_EQ(result.value().denominator, 1u);
}

TEST(TagParsing, ParseRationalExternal) {
    std::array<uint32_t, 2> rational = {72, 1};
    std::size_t offset = 1000;
    auto buffer = create_buffer_with_data(2000, offset, rational.data(), sizeof(rational));
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::YResolution), TiffDataType::Rational, 1, offset);
    
    auto result = parsing::parse_tag<BufferViewReader, YResolutionTag>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().numerator, 72u);
    EXPECT_EQ(result.value().denominator, 1u);
}

TEST(TagParsing, ParseSignedRational) {
    std::array<int32_t, 2> rational = {-100, 3};
    std::size_t offset = 200;
    auto buffer = create_buffer_with_data(500, offset, rational.data(), sizeof(rational));
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XPosition), TiffDataType::SRational, 1, offset);
    
    using TagDesc = TagDescriptor<TagCode::XPosition, TiffDataType::SRational, SRational>;
    auto result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().numerator, -100);
    EXPECT_EQ(result.value().denominator, 3);
}

// ============================================================================
// String (ASCII) Tag Parsing Tests
// ============================================================================

TEST(TagParsing, ParseAsciiInline) {
    const char* str = "Hi";  // 3 bytes with null terminator
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::Software), TiffDataType::Ascii, 3, str, 3);
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = parsing::parse_tag<BufferViewReader, SoftwareTag>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), "Hi");
}

TEST(TagParsing, ParseAsciiExternal) {
    std::string str = "TiffConcept Library v1.0";
    std::size_t offset = 500;
    auto buffer = create_buffer_with_data(1000, offset, str.c_str(), str.size() + 1);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::Software), TiffDataType::Ascii, str.size() + 1, offset);
    
    auto result = parsing::parse_tag<BufferViewReader, SoftwareTag>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), str);
}

// ============================================================================
// Container Tag Parsing Tests
// ============================================================================

TEST(TagParsing, ParseShortArrayInline) {
    std::array<uint16_t, 2> values = {8, 8};
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::BitsPerSample), TiffDataType::Short, 2, values.data(), sizeof(values));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::BitsPerSample, TiffDataType::Short, std::vector<uint16_t>>;
    auto result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().size(), 2u);
    EXPECT_EQ(result.value()[0], 8);
    EXPECT_EQ(result.value()[1], 8);
}

TEST(TagParsing, ParseLongArrayExternal) {
    std::vector<uint32_t> offsets = {100, 200, 300, 400, 500};
    std::size_t offset = 2000;
    auto buffer = create_buffer_with_data(3000, offset, offsets.data(), offsets.size() * sizeof(uint32_t));
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), TiffDataType::Long, offsets.size(), offset);
    
    auto result = parsing::parse_tag<BufferViewReader, StripOffsetsTag>(reader, tag);
    
    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().size(), 5u);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        EXPECT_EQ(result.value()[i], offsets[i]);
    }
}

TEST(TagParsing, ParseByteArray) {
    std::vector<uint8_t> bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::size_t offset = 100;
    auto buffer = create_buffer_with_data(500, offset, bytes.data(), bytes.size());
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::ExtraSamples), TiffDataType::Byte, bytes.size(), offset);
    
    auto result = parsing::parse_tag<BufferViewReader, ExtraSamplesTag>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), bytes);
}

TEST(TagParsing, ParseFixedArray) {
    std::array<uint16_t, 2> page_info = {5, 10};  // Page 5 of 10
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::PageNumber), TiffDataType::Short, 2, page_info.data(), sizeof(page_info));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = parsing::parse_tag<BufferViewReader, PageNumberTag>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value()[0], 5);
    EXPECT_EQ(result.value()[1], 10);
}

TEST(TagParsing, ParseRationalArray) {
    std::array<Rational, 2> white_point = {
        Rational{31270u, 10000u},
        Rational{32900u, 10000u}
    };
    std::size_t offset = 800;
    auto buffer = create_buffer_with_data(1500, offset, white_point.data(), sizeof(white_point));
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::WhitePoint), TiffDataType::Rational, 2, offset);
    
    auto result = parsing::parse_tag<BufferViewReader, WhitePointTag>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value()[0].numerator, 31270u);
    EXPECT_EQ(result.value()[0].denominator, 10000u);
    EXPECT_EQ(result.value()[1].numerator, 32900u);
    EXPECT_EQ(result.value()[1].denominator, 10000u);
}

// ============================================================================
// Endianness Conversion Tests
// ============================================================================

TEST(TagParsing, ParseWithEndianSwap) {
    // Create a little-endian tag (native endianness assumed little for this test)
    uint32_t value = 0x12345678;
    
    auto tag = make_inline_tag<std::endian::native>(
        static_cast<uint16_t>(TagCode::ImageWidth), 
        TiffDataType::Long, 
        1, 
        &value, 
        sizeof(value)
    );
    
    BufferViewReader reader{std::span<const std::byte>{}};
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    auto result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 0x12345678);
}

// ============================================================================
// Type Promotion Tests
// ============================================================================

TEST(TagParsing, TypePromotionShortToLong) {
    uint16_t value = 1000;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Short, 1, &value, sizeof(value));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto result = parsing::parse_tag<BufferViewReader, ImageWidthTag>(reader, tag);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 1000u);
}

TEST(TagParsing, TypePromotionShortArrayToLongArray) {
    std::vector<uint16_t> values = {10, 20, 30};
    std::size_t offset = 100;
    auto buffer = create_buffer_with_data(500, offset, values.data(), values.size() * sizeof(uint16_t));
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), TiffDataType::Short, values.size(), offset);
    
    auto result = parsing::parse_tag<BufferViewReader, StripOffsetsTag>(reader, tag);
    
    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().size(), 3u);
    EXPECT_EQ(result.value()[0], 10u);
    EXPECT_EQ(result.value()[1], 20u);
    EXPECT_EQ(result.value()[2], 30u);
}

// ============================================================================
// Tag Writing Tests
// ============================================================================

TEST(TagWriting, WriteScalarByte) {
    uint8_t value = 123;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::FillOrder, TiffDataType::Byte, uint8_t>;
    auto result = tag_writing::write_tag_data<TagDesc, std::endian::native>(value, buffer, 1);
    
    ASSERT_TRUE(result);
    EXPECT_EQ(static_cast<uint8_t>(buffer[0]), 123);
}

TEST(TagWriting, WriteScalarShort) {
    uint16_t value = 0xABCD;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::Compression, TiffDataType::Short, uint16_t>;
    auto result = tag_writing::write_tag_data<TagDesc, std::endian::native>(value, buffer, 2);
    
    ASSERT_TRUE(result);
    
    uint16_t read_back;
    std::memcpy(&read_back, buffer.data(), sizeof(uint16_t));
    EXPECT_EQ(read_back, 0xABCD);
}

TEST(TagWriting, WriteScalarLong) {
    uint32_t value = 0x12345678;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    auto result = tag_writing::write_tag_data<TagDesc, std::endian::native>(value, buffer, 4);
    
    ASSERT_TRUE(result);
    
    uint32_t read_back;
    std::memcpy(&read_back, buffer.data(), sizeof(uint32_t));
    EXPECT_EQ(read_back, 0x12345678);
}

TEST(TagWriting, WriteScalarFloat) {
    float value = 2.71828f;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Float, float>;
    auto result = tag_writing::write_tag_data<TagDesc, std::endian::native>(value, buffer, 4);
    
    ASSERT_TRUE(result);
    
    float read_back;
    std::memcpy(&read_back, buffer.data(), sizeof(float));
    EXPECT_FLOAT_EQ(read_back, 2.71828f);
}

TEST(TagWriting, WriteRational) {
    Rational value = {300, 1};
    std::vector<std::byte> buffer(10, std::byte{0});
    
    auto result = tag_writing::write_tag_data<XResolutionTag, std::endian::native>(value, buffer, 8);
    
    ASSERT_TRUE(result);
    
    Rational read_back;
    std::memcpy(&read_back, buffer.data(), sizeof(read_back));
    EXPECT_EQ(read_back.numerator, 300u);
    EXPECT_EQ(read_back.denominator, 1u);
}

TEST(TagWriting, WriteAsciiString) {
    std::string value = "TestSoftware";
    std::vector<std::byte> buffer(20, std::byte{0});
    
    auto result = tag_writing::write_tag_data<SoftwareTag, std::endian::native>(value, buffer, value.size()+1);
    
    ASSERT_TRUE(result);
    
    std::string read_back(reinterpret_cast<const char*>(buffer.data()), value.size());
    EXPECT_EQ(read_back, "TestSoftware");
}

TEST(TagWriting, WriteShortArray) {
    std::vector<uint16_t> values = {1, 2, 3, 4, 5};
    std::vector<std::byte> buffer(20, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::BitsPerSample, TiffDataType::Short, std::vector<uint16_t>>;
    auto result = tag_writing::write_tag_data<TagDesc, std::endian::native>(values, buffer, values.size() * 2);
    
    ASSERT_TRUE(result);
    
    std::vector<uint16_t> read_back(5);
    std::memcpy(read_back.data(), buffer.data(), values.size() * sizeof(uint16_t));
    EXPECT_EQ(read_back, values);
}

TEST(TagWriting, WriteLongArray) {
    std::vector<uint32_t> values = {100, 200, 300};
    std::vector<std::byte> buffer(20, std::byte{0});
    
    auto result = tag_writing::write_tag_data<StripOffsetsTag, std::endian::native>(values, buffer, values.size() * 4);
    
    ASSERT_TRUE(result);
    
    std::vector<uint32_t> read_back(3);
    std::memcpy(read_back.data(), buffer.data(), values.size() * sizeof(uint32_t));
    EXPECT_EQ(read_back, values);
}

TEST(TagWriting, WriteFixedArray) {
    std::array<uint16_t, 2> value = {7, 20};
    std::vector<std::byte> buffer(10, std::byte{0});
    
    auto result = tag_writing::write_tag_data<PageNumberTag, std::endian::native>(value, buffer, 4);
    
    ASSERT_TRUE(result);
    
    std::array<uint16_t, 2> read_back;
    std::memcpy(read_back.data(), buffer.data(), sizeof(read_back));
    EXPECT_EQ(read_back[0], 7);
    EXPECT_EQ(read_back[1], 20);
}

TEST(TagWriting, WriteRationalArray) {
    std::array<Rational, 2> value = {
        Rational{100u, 3u},
        Rational{200u, 7u}
    };
    std::vector<std::byte> buffer(20, std::byte{0});
    
    auto result = tag_writing::write_tag_data<WhitePointTag, std::endian::native>(value, buffer, 16);
    
    ASSERT_TRUE(result);
    
    std::array<Rational, 2> read_back;
    std::memcpy(read_back.data(), buffer.data(), sizeof(read_back));
    EXPECT_EQ(read_back[0].numerator, 100u);
    EXPECT_EQ(read_back[0].denominator, 3u);
    EXPECT_EQ(read_back[1].numerator, 200u);
    EXPECT_EQ(read_back[1].denominator, 7u);
}

TEST(TagWriting, WriteWithEndianSwap) {
    uint32_t value = 0x12345678;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    auto result = tag_writing::write_tag_data<TagDesc, std::endian::big>(value, buffer, 4);
    
    ASSERT_TRUE(result);
    
    uint32_t read_back;
    std::memcpy(&read_back, buffer.data(), sizeof(uint32_t));
    
    if constexpr (std::endian::native == std::endian::little) {
        EXPECT_EQ(read_back, byteswap(value));
    } else {
        EXPECT_EQ(read_back, value);
    }
}

TEST(TagWriting, CalculateDataSize_Scalar) {
    uint32_t value = 123;
    auto size = tag_writing::calculate_tag_data_size<ImageWidthTag>(value);
    EXPECT_EQ(size, 4u);
}

TEST(TagWriting, CalculateDataSize_String) {
    std::string value = "Hello";
    auto size = tag_writing::calculate_tag_data_size<SoftwareTag>(value);
    EXPECT_EQ(size, 6u);  // +1 for null terminator
}

TEST(TagWriting, CalculateDataSize_Vector) {
    std::vector<uint32_t> value = {1, 2, 3, 4, 5};
    auto size = tag_writing::calculate_tag_data_size<StripOffsetsTag>(value);
    EXPECT_EQ(size, 20u);  // 5 * 4 bytes
}

TEST(TagWriting, CalculateDataSize_FixedArray) {
    std::array<uint16_t, 2> value = {1, 2};
    auto size = tag_writing::calculate_tag_data_size<PageNumberTag>(value);
    EXPECT_EQ(size, 4u);  // 2 * 2 bytes
}

TEST(TagWriting, CalculateDataSize_Rational) {
    std::pair<uint32_t, uint32_t> value = {300, 1};
    auto size = tag_writing::calculate_tag_data_size<XResolutionTag>(value);
    EXPECT_EQ(size, 8u);  // 2 * 4 bytes
}

// ============================================================================
// Round-trip Tests (Write then Read)
// ============================================================================

TEST(TagRoundTrip, ScalarValue) {
    uint32_t original = 0xDEADBEEF;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    // Write
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 4);
    ASSERT_TRUE(write_result);
    
    // Read back
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Long, 1, buffer.data(), 4);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, RationalValue) {
    Rational original = {72, 1};
    std::vector<std::byte> buffer(20, std::byte{0});
    
    // Write
    auto write_result = tag_writing::write_tag_data<XResolutionTag, std::endian::native>(original, buffer, 8);
    ASSERT_TRUE(write_result);
    
    // Read back
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Rational, 1, 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, XResolutionTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value().numerator, original.numerator);
    EXPECT_EQ(read_result.value().denominator, original.denominator);
}

TEST(TagRoundTrip, StringValue) {
    std::string original = "TiffConcept v1.0";
    std::vector<std::byte> buffer(50, std::byte{0});
    std::size_t data_offset = 10;
    
    // Write string data to buffer at offset
    std::memcpy(buffer.data() + data_offset, original.c_str(), original.size() + 1);
    
    // Create tag pointing to the data
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::Software), TiffDataType::Ascii, original.size() + 1, data_offset);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, SoftwareTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, VectorValue) {
    std::vector<uint32_t> original = {100, 200, 300, 400, 500};
    std::vector<std::byte> buffer(50, std::byte{0});
    
    // Write
    auto write_result = tag_writing::write_tag_data<StripOffsetsTag, std::endian::native>(original, buffer, original.size() * 4);
    ASSERT_TRUE(write_result);
    
    // Read back with external offset
    std::size_t offset = 0;
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), TiffDataType::Long, original.size(), offset);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, StripOffsetsTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, FixedArrayValue) {
    std::array<uint16_t, 2> original = {3, 15};
    std::vector<std::byte> buffer(10, std::byte{0});
    
    // Write
    auto write_result = tag_writing::write_tag_data<PageNumberTag, std::endian::native>(original, buffer, 4);
    ASSERT_TRUE(write_result);
    
    // Read back
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::PageNumber), TiffDataType::Short, 2, buffer.data(), 4);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, PageNumberTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value()[0], original[0]);
    EXPECT_EQ(read_result.value()[1], original[1]);
}

// ============================================================================
// Comprehensive Round-trip Tests (Write → Read → Write and Read → Write → Read)
// ============================================================================

// Scalar types - all data types
TEST(TagRoundTrip, ScalarByte_WriteRead) {
    uint8_t original = 42;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::FillOrder, TiffDataType::Byte, uint8_t>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 1);
    ASSERT_TRUE(write_result);
    
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::FillOrder), TiffDataType::Byte, 1, buffer.data(), 1);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarByte_ReadWrite) {
    uint8_t value = 123;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::FillOrder), TiffDataType::Byte, 1, &value, sizeof(value));
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::FillOrder, TiffDataType::Byte, uint8_t>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer(10, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer, 1);
    ASSERT_TRUE(write_result);
    
    uint8_t written_value;
    std::memcpy(&written_value, buffer.data(), sizeof(written_value));
    EXPECT_EQ(written_value, value);
}

TEST(TagRoundTrip, ScalarSByte_WriteRead) {
    int8_t original = -42;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::FillOrder, TiffDataType::SByte, int8_t>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 1);
    ASSERT_TRUE(write_result);
    
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::FillOrder), TiffDataType::SByte, 1, buffer.data(), 1);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarShort_WriteRead) {
    uint16_t original = 1234;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::Compression, TiffDataType::Short, uint16_t>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 2);
    ASSERT_TRUE(write_result);
    
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::Compression), TiffDataType::Short, 1, buffer.data(), 2);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarShort_ReadWrite) {
    uint16_t value = 5678;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::Compression), TiffDataType::Short, 1, &value, sizeof(value));
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::Compression, TiffDataType::Short, uint16_t>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer(10, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer, 2);
    ASSERT_TRUE(write_result);
    
    uint16_t written_value;
    std::memcpy(&written_value, buffer.data(), sizeof(written_value));
    EXPECT_EQ(written_value, value);
}

TEST(TagRoundTrip, ScalarSShort_WriteRead) {
    int16_t original = -1234;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::Compression, TiffDataType::SShort, int16_t>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 2);
    ASSERT_TRUE(write_result);
    
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::Compression), TiffDataType::SShort, 1, buffer.data(), 2);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarLong_WriteRead) {
    uint32_t original = 0xDEADBEEF;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 4);
    ASSERT_TRUE(write_result);
    
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Long, 1, buffer.data(), 4);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarLong_ReadWrite) {
    uint32_t value = 0xCAFEBABE;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Long, 1, &value, sizeof(value));
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer(10, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer, 4);
    ASSERT_TRUE(write_result);
    
    uint32_t written_value;
    std::memcpy(&written_value, buffer.data(), sizeof(written_value));
    EXPECT_EQ(written_value, value);
}

TEST(TagRoundTrip, ScalarSLong_WriteRead) {
    int32_t original = -123456;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::SLong, int32_t>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 4);
    ASSERT_TRUE(write_result);
    
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::SLong, 1, buffer.data(), 4);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarLong8_WriteRead) {
    uint64_t original = 0xDEADBEEFCAFEBABE;
    std::vector<std::byte> buffer(20, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long8, uint64_t>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 8);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Long8, 1, 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarLong8_ReadWrite) {
    uint64_t value = 0x123456789ABCDEF0;
    std::vector<std::byte> buffer_read(20, std::byte{0});
    std::memcpy(buffer_read.data(), &value, sizeof(value));
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Long8, 1, 0);
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long8, uint64_t>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(20, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer_write, 8);
    ASSERT_TRUE(write_result);
    
    uint64_t written_value;
    std::memcpy(&written_value, buffer_write.data(), sizeof(written_value));
    EXPECT_EQ(written_value, value);
}

TEST(TagRoundTrip, ScalarSLong8_WriteRead) {
    int64_t original = -9876543210;
    std::vector<std::byte> buffer(20, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::SLong8, int64_t>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 8);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::SLong8, 1, 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarFloat_WriteRead) {
    float original = 3.14159f;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Float, float>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 4);
    ASSERT_TRUE(write_result);
    
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Float, 1, buffer.data(), 4);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_FLOAT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarFloat_ReadWrite) {
    float value = 2.71828f;
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Float, 1, &value, sizeof(value));
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Float, float>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer(10, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer, 4);
    ASSERT_TRUE(write_result);
    
    float written_value;
    std::memcpy(&written_value, buffer.data(), sizeof(written_value));
    EXPECT_FLOAT_EQ(written_value, value);
}

TEST(TagRoundTrip, ScalarDouble_WriteRead) {
    double original = 3.141592653589793;
    std::vector<std::byte> buffer(20, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Double, double>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 8);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Double, 1, 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_DOUBLE_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ScalarDouble_ReadWrite) {
    double value = 2.718281828459045;
    std::vector<std::byte> buffer_read(20, std::byte{0});
    std::memcpy(buffer_read.data(), &value, sizeof(value));
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Double, 1, 0);
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Double, double>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(20, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer_write, 8);
    ASSERT_TRUE(write_result);
    
    double written_value;
    std::memcpy(&written_value, buffer_write.data(), sizeof(written_value));
    EXPECT_DOUBLE_EQ(written_value, value);
}

// Rational types
TEST(TagRoundTrip, Rational_WriteRead) {
    Rational original = {72, 1};
    std::vector<std::byte> buffer(20, std::byte{0});
    
    auto write_result = tag_writing::write_tag_data<XResolutionTag, std::endian::native>(original, buffer, 8);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Rational, 1, 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, XResolutionTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value().numerator, original.numerator);
    EXPECT_EQ(read_result.value().denominator, original.denominator);
}

TEST(TagRoundTrip, Rational_ReadWrite) {
    std::array<uint32_t, 2> rational = {300, 1};
    std::size_t offset = 100;
    auto buffer_read = create_buffer_with_data(500, offset, rational.data(), sizeof(rational));
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Rational, 1, offset);
    auto read_result = parsing::parse_tag<BufferViewReader, XResolutionTag>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(20, std::byte{0});
    auto write_result = tag_writing::write_tag_data<XResolutionTag, std::endian::native>(read_result.value(), buffer_write, 8);
    ASSERT_TRUE(write_result);
    
    Rational written_value;
    std::memcpy(&written_value.numerator, buffer_write.data(), sizeof(uint32_t));
    std::memcpy(&written_value.denominator, buffer_write.data() + sizeof(uint32_t), sizeof(uint32_t));
    EXPECT_EQ(written_value.numerator, rational[0]);
    EXPECT_EQ(written_value.denominator, rational[1]);
}

TEST(TagRoundTrip, SRational_WriteRead) {
    SRational original = {-100, 3};
    std::vector<std::byte> buffer(20, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::XPosition, TiffDataType::SRational, SRational>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, 8);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XPosition), TiffDataType::SRational, 1, 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value().numerator, original.numerator);
    EXPECT_EQ(read_result.value().denominator, original.denominator);
}

TEST(TagRoundTrip, SRational_ReadWrite) {
    std::array<int32_t, 2> rational = {-200, 7};
    std::size_t offset = 200;
    auto buffer_read = create_buffer_with_data(500, offset, rational.data(), sizeof(rational));
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XPosition), TiffDataType::SRational, 1, offset);
    
    using TagDesc = TagDescriptor<TagCode::XPosition, TiffDataType::SRational, SRational>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(20, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer_write, 8);
    ASSERT_TRUE(write_result);
    
    SRational written_value;
    std::memcpy(&written_value.numerator, buffer_write.data(), sizeof(int32_t));
    std::memcpy(&written_value.denominator, buffer_write.data() + sizeof(int32_t), sizeof(int32_t));
    EXPECT_EQ(written_value.numerator, rational[0]);
    EXPECT_EQ(written_value.denominator, rational[1]);
}

// String/ASCII
TEST(TagRoundTrip, String_WriteRead) {
    std::string original = "TiffConcept v1.0";
    std::vector<std::byte> buffer(50, std::byte{0});
    
    // Write string data to buffer
    auto size = tag_writing::calculate_tag_data_size<SoftwareTag>(original);
    std::memcpy(buffer.data(), original.c_str(), size);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::Software), TiffDataType::Ascii, size, 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, SoftwareTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, String_ReadWrite) {
    std::string str = "Test String 123";
    std::size_t offset = 500;
    auto buffer_read = create_buffer_with_data(1000, offset, str.c_str(), str.size() + 1);
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::Software), TiffDataType::Ascii, str.size() + 1, offset);
    auto read_result = parsing::parse_tag<BufferViewReader, SoftwareTag>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(50, std::byte{0});
    auto size = tag_writing::calculate_tag_data_size<SoftwareTag>(read_result.value());
    std::memcpy(buffer_write.data(), read_result.value().c_str(), size);
    
    std::string written_value(reinterpret_cast<const char*>(buffer_write.data()));
    EXPECT_EQ(written_value, str);
}

// Vector/Array types
TEST(TagRoundTrip, ByteVector_WriteRead) {
    std::vector<uint8_t> original = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<std::byte> buffer(50, std::byte{0});
    
    auto write_result = tag_writing::write_tag_data<ExtraSamplesTag, std::endian::native>(original, buffer, original.size());
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::ExtraSamples), TiffDataType::Byte, original.size(), 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, ExtraSamplesTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ByteVector_ReadWrite) {
    std::vector<uint8_t> bytes = {11, 22, 33, 44, 55};
    std::size_t offset = 100;
    auto buffer_read = create_buffer_with_data(500, offset, bytes.data(), bytes.size());
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::ExtraSamples), TiffDataType::Byte, bytes.size(), offset);
    auto read_result = parsing::parse_tag<BufferViewReader, ExtraSamplesTag>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(50, std::byte{0});
    auto write_result = tag_writing::write_tag_data<ExtraSamplesTag, std::endian::native>(read_result.value(), buffer_write, read_result.value().size());
    ASSERT_TRUE(write_result);
    
    std::vector<uint8_t> written_value(read_result.value().size());
    std::memcpy(written_value.data(), buffer_write.data(), written_value.size());
    EXPECT_EQ(written_value, bytes);
}

TEST(TagRoundTrip, ShortVector_WriteRead) {
    std::vector<uint16_t> original = {8, 8, 8, 16};
    std::vector<std::byte> buffer(50, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::BitsPerSample, TiffDataType::Short, std::vector<uint16_t>>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, original.size() * 2);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::BitsPerSample), TiffDataType::Short, original.size(), 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, ShortVector_ReadWrite) {
    std::vector<uint16_t> values = {100, 200, 300};
    std::size_t offset = 100;
    auto buffer_read = create_buffer_with_data(500, offset, values.data(), values.size() * sizeof(uint16_t));
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::BitsPerSample), TiffDataType::Short, values.size(), offset);
    
    using TagDesc = TagDescriptor<TagCode::BitsPerSample, TiffDataType::Short, std::vector<uint16_t>>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(50, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer_write, read_result.value().size() * 2);
    ASSERT_TRUE(write_result);
    
    std::vector<uint16_t> written_value(read_result.value().size());
    std::memcpy(written_value.data(), buffer_write.data(), written_value.size() * sizeof(uint16_t));
    EXPECT_EQ(written_value, values);
}

TEST(TagRoundTrip, LongVector_WriteRead) {
    std::vector<uint32_t> original = {100, 200, 300, 400, 500};
    std::vector<std::byte> buffer(50, std::byte{0});
    
    auto write_result = tag_writing::write_tag_data<StripOffsetsTag, std::endian::native>(original, buffer, original.size() * 4);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), TiffDataType::Long, original.size(), 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, StripOffsetsTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, LongVector_ReadWrite) {
    std::vector<uint32_t> offsets = {1000, 2000, 3000};
    std::size_t offset = 2000;
    auto buffer_read = create_buffer_with_data(3000, offset, offsets.data(), offsets.size() * sizeof(uint32_t));
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), TiffDataType::Long, offsets.size(), offset);
    auto read_result = parsing::parse_tag<BufferViewReader, StripOffsetsTag>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(50, std::byte{0});
    auto write_result = tag_writing::write_tag_data<StripOffsetsTag, std::endian::native>(read_result.value(), buffer_write, read_result.value().size() * 4);
    ASSERT_TRUE(write_result);
    
    std::vector<uint32_t> written_value(read_result.value().size());
    std::memcpy(written_value.data(), buffer_write.data(), written_value.size() * sizeof(uint32_t));
    EXPECT_EQ(written_value, offsets);
}

TEST(TagRoundTrip, Long8Vector_WriteRead) {
    std::vector<uint64_t> original = {0x100000000, 0x200000000, 0x300000000};
    std::vector<std::byte> buffer(100, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::StripOffsets, TiffDataType::Long8, std::vector<uint64_t>>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, original.size() * 8);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), TiffDataType::Long8, original.size(), 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, Long8Vector_ReadWrite) {
    std::vector<uint64_t> offsets = {0xABCDEF0123456789, 0xFEDCBA9876543210};
    std::size_t offset = 100;
    auto buffer_read = create_buffer_with_data(200, offset, offsets.data(), offsets.size() * sizeof(uint64_t));
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::StripOffsets), TiffDataType::Long8, offsets.size(), offset);
    
    using TagDesc = TagDescriptor<TagCode::StripOffsets, TiffDataType::Long8, std::vector<uint64_t>>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(100, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer_write, read_result.value().size() * 8);
    ASSERT_TRUE(write_result);
    
    std::vector<uint64_t> written_value(read_result.value().size());
    std::memcpy(written_value.data(), buffer_write.data(), written_value.size() * sizeof(uint64_t));
    EXPECT_EQ(written_value, offsets);
}

TEST(TagRoundTrip, FloatVector_WriteRead) {
    std::vector<float> original = {1.1f, 2.2f, 3.3f, 4.4f};
    std::vector<std::byte> buffer(50, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Float, std::vector<float>>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, original.size() * 4);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Float, original.size(), 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    ASSERT_EQ(read_result.value().size(), original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_FLOAT_EQ(read_result.value()[i], original[i]);
    }
}

TEST(TagRoundTrip, FloatVector_ReadWrite) {
    std::vector<float> values = {5.5f, 6.6f, 7.7f};
    std::size_t offset = 100;
    auto buffer_read = create_buffer_with_data(200, offset, values.data(), values.size() * sizeof(float));
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Float, values.size(), offset);
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Float, std::vector<float>>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(50, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer_write, read_result.value().size() * 4);
    ASSERT_TRUE(write_result);
    
    std::vector<float> written_value(read_result.value().size());
    std::memcpy(written_value.data(), buffer_write.data(), written_value.size() * sizeof(float));
    ASSERT_EQ(written_value.size(), values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        EXPECT_FLOAT_EQ(written_value[i], values[i]);
    }
}

TEST(TagRoundTrip, DoubleVector_WriteRead) {
    std::vector<double> original = {1.111, 2.222, 3.333};
    std::vector<std::byte> buffer(100, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Double, std::vector<double>>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, original.size() * 8);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Double, original.size(), 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    ASSERT_EQ(read_result.value().size(), original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_DOUBLE_EQ(read_result.value()[i], original[i]);
    }
}

TEST(TagRoundTrip, DoubleVector_ReadWrite) {
    std::vector<double> values = {4.444, 5.555};
    std::size_t offset = 100;
    auto buffer_read = create_buffer_with_data(200, offset, values.data(), values.size() * sizeof(double));
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Double, values.size(), offset);
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Double, std::vector<double>>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(100, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer_write, read_result.value().size() * 8);
    ASSERT_TRUE(write_result);
    
    std::vector<double> written_value(read_result.value().size());
    std::memcpy(written_value.data(), buffer_write.data(), written_value.size() * sizeof(double));
    ASSERT_EQ(written_value.size(), values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        EXPECT_DOUBLE_EQ(written_value[i], values[i]);
    }
}

// Fixed-size arrays
TEST(TagRoundTrip, FixedArray_WriteRead) {
    std::array<uint16_t, 2> original = {3, 15};
    std::vector<std::byte> buffer(10, std::byte{0});
    
    auto write_result = tag_writing::write_tag_data<PageNumberTag, std::endian::native>(original, buffer, 4);
    ASSERT_TRUE(write_result);
    
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::PageNumber), TiffDataType::Short, 2, buffer.data(), 4);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, PageNumberTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value()[0], original[0]);
    EXPECT_EQ(read_result.value()[1], original[1]);
}

TEST(TagRoundTrip, FixedArray_ReadWrite) {
    std::array<uint16_t, 2> values = {7, 21};
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::PageNumber), TiffDataType::Short, 2, values.data(), sizeof(values));
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto read_result = parsing::parse_tag<BufferViewReader, PageNumberTag>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer(10, std::byte{0});
    auto write_result = tag_writing::write_tag_data<PageNumberTag, std::endian::native>(read_result.value(), buffer, 4);
    ASSERT_TRUE(write_result);
    
    std::array<uint16_t, 2> written_value;
    std::memcpy(written_value.data(), buffer.data(), sizeof(written_value));
    EXPECT_EQ(written_value[0], values[0]);
    EXPECT_EQ(written_value[1], values[1]);
}

// Rational vectors
TEST(TagRoundTrip, RationalVector_WriteRead) {
    std::vector<Rational> original = {{300, 1}, {72, 1}, {150, 2}};
    std::vector<std::byte> buffer(100, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Rational, std::vector<Rational>>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, original.size() * 8);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Rational, original.size(), 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    ASSERT_EQ(read_result.value().size(), original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(read_result.value()[i].numerator, original[i].numerator);
        EXPECT_EQ(read_result.value()[i].denominator, original[i].denominator);
    }
}

TEST(TagRoundTrip, RationalVector_ReadWrite) {
    std::vector<Rational> rationals = {{100, 3}, {200, 7}};
    std::vector<uint32_t> data;
    for (const auto& r : rationals) {
        data.push_back(r.numerator);
        data.push_back(r.denominator);
    }
    
    std::size_t offset = 100;
    auto buffer_read = create_buffer_with_data(500, offset, data.data(), data.size() * sizeof(uint32_t));
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XResolution), TiffDataType::Rational, rationals.size(), offset);
    
    using TagDesc = TagDescriptor<TagCode::XResolution, TiffDataType::Rational, std::vector<Rational>>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(100, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer_write, read_result.value().size() * 8);
    ASSERT_TRUE(write_result);
    
    std::vector<Rational> written_value(read_result.value().size());
    for (size_t i = 0; i < written_value.size(); ++i) {
        std::memcpy(&written_value[i].numerator, buffer_write.data() + i * 8, sizeof(uint32_t));
        std::memcpy(&written_value[i].denominator, buffer_write.data() + i * 8 + sizeof(uint32_t), sizeof(uint32_t));
    }
    
    ASSERT_EQ(written_value.size(), rationals.size());
    for (size_t i = 0; i < rationals.size(); ++i) {
        EXPECT_EQ(written_value[i].numerator, rationals[i].numerator);
        EXPECT_EQ(written_value[i].denominator, rationals[i].denominator);
    }
}

TEST(TagRoundTrip, SRationalVector_WriteRead) {
    std::vector<SRational> original = {{-100, 3}, {200, -7}, {-50, -2}};
    std::vector<std::byte> buffer(100, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::XPosition, TiffDataType::SRational, std::vector<SRational>>;
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(original, buffer, original.size() * 8);
    ASSERT_TRUE(write_result);
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XPosition), TiffDataType::SRational, original.size(), 0);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_TRUE(read_result);
    ASSERT_EQ(read_result.value().size(), original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(read_result.value()[i].numerator, original[i].numerator);
        EXPECT_EQ(read_result.value()[i].denominator, original[i].denominator);
    }
}

TEST(TagRoundTrip, SRationalVector_ReadWrite) {
    std::vector<SRational> rationals = {{-300, 11}, {400, -13}};
    std::vector<int32_t> data;
    for (const auto& r : rationals) {
        data.push_back(r.numerator);
        data.push_back(r.denominator);
    }
    
    std::size_t offset = 100;
    auto buffer_read = create_buffer_with_data(500, offset, data.data(), data.size() * sizeof(int32_t));
    BufferViewReader reader{std::span<const std::byte>(buffer_read)};
    
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::XPosition), TiffDataType::SRational, rationals.size(), offset);
    
    using TagDesc = TagDescriptor<TagCode::XPosition, TiffDataType::SRational, std::vector<SRational>>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer_write(100, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, std::endian::native>(read_result.value(), buffer_write, read_result.value().size() * 8);
    ASSERT_TRUE(write_result);
    
    std::vector<SRational> written_value(read_result.value().size());
    for (size_t i = 0; i < written_value.size(); ++i) {
        std::memcpy(&written_value[i].numerator, buffer_write.data() + i * 8, sizeof(int32_t));
        std::memcpy(&written_value[i].denominator, buffer_write.data() + i * 8 + sizeof(int32_t), sizeof(int32_t));
    }
    
    ASSERT_EQ(written_value.size(), rationals.size());
    for (size_t i = 0; i < rationals.size(); ++i) {
        EXPECT_EQ(written_value[i].numerator, rationals[i].numerator);
        EXPECT_EQ(written_value[i].denominator, rationals[i].denominator);
    }
}

// Enum type
TEST(TagRoundTrip, Enum_WriteRead) {
    CompressionScheme original = CompressionScheme::LZW;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    auto write_result = tag_writing::write_tag_data<CompressionTag, std::endian::native>(original, buffer, 2);
    ASSERT_TRUE(write_result);
    
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::Compression), TiffDataType::Short, 1, buffer.data(), 2);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, CompressionTag>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, Enum_ReadWrite) {
    uint16_t value = static_cast<uint16_t>(CompressionScheme::Deflate);
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::Compression), TiffDataType::Short, 1, &value, sizeof(value));
    BufferViewReader reader(std::span<const std::byte>{});
    
    auto read_result = parsing::parse_tag<BufferViewReader, CompressionTag>(reader, tag);
    ASSERT_TRUE(read_result);
    
    std::vector<std::byte> buffer(10, std::byte{0});
    auto write_result = tag_writing::write_tag_data<CompressionTag, std::endian::native>(read_result.value(), buffer, 2);
    ASSERT_TRUE(write_result);
    
    uint16_t written_value;
    std::memcpy(&written_value, buffer.data(), sizeof(written_value));
    EXPECT_EQ(written_value, value);
}

// Endianness tests
TEST(TagRoundTrip, EndianSwap_WriteRead) {
    uint32_t original = 0x12345678;
    std::vector<std::byte> buffer(10, std::byte{0});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    
    // Write with opposite endianness
    constexpr auto opposite_endian = (std::endian::native == std::endian::little) ? std::endian::big : std::endian::little;
    auto write_result = tag_writing::write_tag_data<TagDesc, opposite_endian>(original, buffer, 4);
    ASSERT_TRUE(write_result);
    
    // Read back with matching endianness
    auto tag = make_inline_tag<opposite_endian>(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Long, 1, buffer.data(), 4);
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc, TiffFormatType::Classic, std::endian::native, opposite_endian>(reader, tag);
    
    ASSERT_TRUE(read_result);
    EXPECT_EQ(read_result.value(), original);
}

TEST(TagRoundTrip, EndianSwap_ReadWrite) {
    constexpr auto opposite_endian = (std::endian::native == std::endian::little) ? std::endian::big : std::endian::little;
    uint32_t value = 0xABCDEF01;
    
    // Create tag with opposite endianness
    auto tag = make_inline_tag<opposite_endian>(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Long, 1, &value, sizeof(value));
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    auto read_result = parsing::parse_tag<BufferViewReader, TagDesc, TiffFormatType::Classic, std::endian::native, opposite_endian>(reader, tag);
    ASSERT_TRUE(read_result);
    
    // Write back with opposite endianness
    std::vector<std::byte> buffer(10, std::byte{0});
    auto write_result = tag_writing::write_tag_data<TagDesc, opposite_endian>(read_result.value(), buffer, 4);
    ASSERT_TRUE(write_result);
    
    // Verify by reading raw bytes (they should be byte-swapped)
    uint32_t written_value;
    std::memcpy(&written_value, buffer.data(), sizeof(written_value));
    EXPECT_EQ(written_value, value);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(TagParsing, ErrorInvalidDataType) {
    uint32_t value = 123;
    // Create tag with wrong datatype (expect Short, provide Long)
    auto tag = make_inline_tag(static_cast<uint16_t>(TagCode::Compression), TiffDataType::Long, 1, &value, sizeof(value));
    
    BufferViewReader reader(std::span<const std::byte>{});
    
    using TagDesc = TagDescriptor<TagCode::Compression, TiffDataType::Short, uint16_t>;
    auto result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, Error::Code::InvalidTagType);
}

TEST(TagParsing, ErrorBufferTooSmall) {
    std::vector<std::byte> buffer(5, std::byte{0});  // Too small
    BufferViewReader reader{std::span<const std::byte>(buffer)};
    
    // Use Long8 (8 bytes) to force external storage, or use count=2
    auto tag = make_offset_tag(static_cast<uint16_t>(TagCode::ImageWidth), TiffDataType::Long, 2, 100);
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, std::vector<uint32_t>>;
    auto result = parsing::parse_tag<BufferViewReader, TagDesc>(reader, tag);
    
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Error::Code::OutOfBounds);
}

TEST(TagWriting, ErrorBufferTooSmall) {
    uint32_t value = 123;
    std::vector<std::byte> buffer(2, std::byte{0});  // Too small for uint32_t
    
    using TagDesc = TagDescriptor<TagCode::ImageWidth, TiffDataType::Long, uint32_t>;
    auto result = tag_writing::write_tag_data<TagDesc, std::endian::native>(value, buffer, 4);
    
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, Error::Code::OutOfBounds);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
