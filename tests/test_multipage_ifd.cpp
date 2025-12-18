#include <gtest/gtest.h>
#include <vector>
#include <random>
#include <cstring>
#include <algorithm>

#include "../tiffconcept/include/tiffconcept/parsing.hpp"
#include "../tiffconcept/include/tiffconcept/ifd.hpp"
#include "../tiffconcept/include/tiffconcept/ifd_builder.hpp"
#include "../tiffconcept/include/tiffconcept/tag_extraction.hpp"
#include "../tiffconcept/include/tiffconcept/tag_spec.hpp"
#include "../tiffconcept/include/tiffconcept/tag_writing.hpp"
#include "../tiffconcept/include/tiffconcept/readers/reader_buffer.hpp"
#include "../tiffconcept/include/tiffconcept/types.hpp"

using namespace tiffconcept;

// ============================================================================
// Helper Functions
// ============================================================================

template <typename T>
std::vector<T> generate_test_data(std::size_t count, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::vector<T> data(count);
    
    if constexpr (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<T> dist(0.0, 1.0);
        for (auto& val : data) val = dist(rng);
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& val : data) val = static_cast<T>(dist(rng));
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        std::uniform_int_distribution<int> dist(0, 65535);
        for (auto& val : data) val = static_cast<T>(dist(rng));
    } else {
        std::uniform_int_distribution<T> dist(0, 1000);
        for (auto& val : data) val = dist(rng);
    }
    
    return data;
}

// ============================================================================
// IFD Chain Reading Tests
// ============================================================================

TEST(MultiPageTIFF, ReadIFDChain_TwoPages) {
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag,
        BitsPerSampleTag
    >;
    
    // Build two IFDs manually
    IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd> builder1;
    ExtractedTags<TagSpec> tags1;
    tags1.get<TagCode::ImageWidth>() = 100;
    tags1.get<TagCode::ImageLength>() = 100;
    tags1.get<TagCode::BitsPerSample>() = std::vector<uint16_t>{8};
    
    auto add_result1 = builder1.add_tags(tags1);
    ASSERT_TRUE(add_result1.is_ok());
    
    IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd> builder2;
    ExtractedTags<TagSpec> tags2;
    tags2.get<TagCode::ImageWidth>() = 200;
    tags2.get<TagCode::ImageLength>() = 200;
    tags2.get<TagCode::BitsPerSample>() = std::vector<uint16_t>{16};
    
    auto add_result2 = builder2.add_tags(tags2);
    ASSERT_TRUE(add_result2.is_ok());
    
    // Write to buffer
    BufferWriter writer;
    ASSERT_TRUE(writer.resize(1024));  // Preallocate buffer
    
    // Write header
    TiffHeader<std::endian::little> header;
    header.byte_order = {0x49, 0x49};
    header.version = 42;
    header.first_ifd_offset = 8;  // After header
    
    auto header_view = writer.write(0, sizeof(header));
    ASSERT_TRUE(header_view.is_ok());
    std::memcpy(header_view.value().data().data(), &header, sizeof(header));
    ASSERT_TRUE(header_view.value().flush().is_ok());
    
    // Write first IFD at offset 8
    std::size_t ifd1_offset = 8;
    std::size_t ifd1_size = builder1.calculate_ifd_size();
    std::size_t ifd2_offset = ifd1_offset + ifd1_size;
    
    // Set next IFD offset in first IFD
    builder1.set_next_ifd_offset(ifd::IFDOffset(ifd2_offset));
    
    auto write_result1 = builder1.write_to_file(writer, ifd1_offset);
    ASSERT_TRUE(write_result1.is_ok());
    
    // Write second IFD (no next IFD)
    builder2.set_next_ifd_offset(ifd::IFDOffset(0));  // End of chain
    auto write_result2 = builder2.write_to_file(writer, ifd2_offset);
    ASSERT_TRUE(write_result2.is_ok());
    
    // Read back the IFD chain
    BufferViewReader reader(writer.buffer());
    
    auto first_ifd_result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto first_ifd = first_ifd_result.value();
    EXPECT_EQ(first_ifd.value, ifd1_offset);
    
    // Read first IFD
    auto ifd1_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, first_ifd);
    ASSERT_TRUE(ifd1_result.is_ok());
    
    auto& ifd1 = ifd1_result.value();
    auto next_ifd = ifd1.next_ifd_offset;
    EXPECT_EQ(next_ifd.value, ifd2_offset);
    
    // Extract and verify first IFD tags
    ExtractedTags<TagSpec> read_tags1;
    auto extract_result1 = read_tags1.extract<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, std::span(ifd1.tags));
    ASSERT_TRUE(extract_result1.is_ok());
    
    EXPECT_EQ(read_tags1.get<TagCode::ImageWidth>(), 100u);
    EXPECT_EQ(read_tags1.get<TagCode::ImageLength>(), 100u);
    EXPECT_EQ(read_tags1.get<TagCode::BitsPerSample>()[0], 8);
    
    // Read second IFD
    auto ifd2_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, next_ifd);
    ASSERT_TRUE(ifd2_result.is_ok());
    
    auto& ifd2 = ifd2_result.value();
    auto final_next = ifd2.next_ifd_offset;
    EXPECT_EQ(final_next.value, 0u);  // End of chain
    
    // Extract and verify second IFD tags
    ExtractedTags<TagSpec> read_tags2;
    auto extract_result2 = read_tags2.extract<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, std::span(ifd2.tags));
    ASSERT_TRUE(extract_result2.is_ok());
    
    EXPECT_EQ(read_tags2.get<TagCode::ImageWidth>(), 200u);
    EXPECT_EQ(read_tags2.get<TagCode::ImageLength>(), 200u);
    EXPECT_EQ(read_tags2.get<TagCode::BitsPerSample>()[0], 16);
}

TEST(MultiPageTIFF, ReadIFDChain_MultiplePages) {
    using TagSpec = TagSpec<
        ImageWidthTag,
        PageNumberTag
    >;
    
    constexpr int num_pages = 5;
    std::vector<IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd>> builders(num_pages);
    std::vector<ExtractedTags<TagSpec>> all_tags(num_pages);
    
    // Build IFDs for each page
    for (int i = 0; i < num_pages; ++i) {
        all_tags[i].get<TagCode::ImageWidth>() = 100 + i * 10;
        all_tags[i].get<TagCode::PageNumber>() = std::array<uint16_t, 2>{
            static_cast<uint16_t>(i), 
            static_cast<uint16_t>(num_pages)
        };
        
        auto add_result = builders[i].add_tags(all_tags[i]);
        ASSERT_TRUE(add_result.is_ok());
    }
    
    // Calculate offsets for all IFDs
    BufferWriter writer;
    ASSERT_TRUE(writer.resize(4096));  // Preallocate buffer
    std::size_t header_size = sizeof(TiffHeader<std::endian::little>);
    std::vector<std::size_t> ifd_offsets(num_pages);
    
    std::size_t current_offset = header_size;
    for (int i = 0; i < num_pages; ++i) {
        ifd_offsets[i] = current_offset;
        current_offset += builders[i].calculate_ifd_size();
    }
    
    // Write header
    TiffHeader<std::endian::little> header;
    header.byte_order = {0x49, 0x49};
    header.version = 42;
    header.first_ifd_offset = static_cast<uint32_t>(ifd_offsets[0]);
    
    auto header_view = writer.write(0, sizeof(header));
    ASSERT_TRUE(header_view.is_ok());
    std::memcpy(header_view.value().data().data(), &header, sizeof(header));
    ASSERT_TRUE(header_view.value().flush().is_ok());
    
    // Write all IFDs with proper next pointers
    for (int i = 0; i < num_pages; ++i) {
        if (i < num_pages - 1) {
            builders[i].set_next_ifd_offset(ifd::IFDOffset(ifd_offsets[i + 1]));
        } else {
            builders[i].set_next_ifd_offset(ifd::IFDOffset(0));  // Last IFD
        }
        
        auto write_result = builders[i].write_to_file(writer, ifd_offsets[i]);
        ASSERT_TRUE(write_result.is_ok());
    }
    
    // Read back and verify the chain
    BufferViewReader reader(writer.buffer());
    
    auto first_ifd_result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    ifd::IFDOffset current_ifd = first_ifd_result.value();
    
    for (int i = 0; i < num_pages; ++i) {
        EXPECT_EQ(current_ifd.value, ifd_offsets[i]);
        
        auto ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, current_ifd);
        ASSERT_TRUE(ifd_result.is_ok());
        
        auto& current_ifd_data = ifd_result.value();
        
        // Extract and verify tags
        ExtractedTags<TagSpec> read_tags;
        auto extract_result = read_tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
            reader, std::span(current_ifd_data.tags));
        ASSERT_TRUE(extract_result.is_ok());
        
        EXPECT_EQ(read_tags.get<TagCode::ImageWidth>(), 100 + i * 10);
        auto page_num = read_tags.get<TagCode::PageNumber>();
        EXPECT_EQ(page_num[0], i);
        EXPECT_EQ(page_num[1], num_pages);
        
        // Move to next IFD
        current_ifd = current_ifd_data.next_ifd_offset;
    }
    
    // Should have reached end of chain
    EXPECT_EQ(current_ifd.value, 0u);
}

TEST(MultiPageTIFF, BigTIFF_IFDChain) {
    using TagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag
    >;
    
    // Build two IFDs for BigTIFF
    IFDBuilder<TiffFormatType::BigTIFF, std::endian::little, IFDAtEnd> builder1;
    ExtractedTags<TagSpec> tags1;
    tags1.get<TagCode::ImageWidth>() = 300;
    tags1.get<TagCode::ImageLength>() = 300;
    
    auto add_result1 = builder1.add_tags(tags1);
    ASSERT_TRUE(add_result1.is_ok());
    
    IFDBuilder<TiffFormatType::BigTIFF, std::endian::little, IFDAtEnd> builder2;
    ExtractedTags<TagSpec> tags2;
    tags2.get<TagCode::ImageWidth>() = 400;
    tags2.get<TagCode::ImageLength>() = 400;
    
    auto add_result2 = builder2.add_tags(tags2);
    ASSERT_TRUE(add_result2.is_ok());
    
    // Write to buffer
    BufferWriter writer;
    ASSERT_TRUE(writer.resize(2048));  // Preallocate buffer
    
    // Write BigTIFF header
    TiffBigHeader<std::endian::little> header;
    header.byte_order = {0x49, 0x49};
    header.version = 43;  // BigTIFF
    header.offset_size = 8;
    header.reserved = 0;
    header.first_ifd_offset = 16;  // After BigTIFF header
    
    auto header_view = writer.write(0, sizeof(header));
    ASSERT_TRUE(header_view.is_ok());
    std::memcpy(header_view.value().data().data(), &header, sizeof(header));
    ASSERT_TRUE(header_view.value().flush().is_ok());
    
    // Write first IFD
    std::size_t ifd1_offset = 16;
    std::size_t ifd1_size = builder1.calculate_ifd_size();
    std::size_t ifd2_offset = ifd1_offset + ifd1_size;
    
    builder1.set_next_ifd_offset(ifd::IFDOffset(ifd2_offset));
    auto write_result1 = builder1.write_to_file(writer, ifd1_offset);
    ASSERT_TRUE(write_result1.is_ok());
    
    // Write second IFD
    builder2.set_next_ifd_offset(ifd::IFDOffset(0));
    auto write_result2 = builder2.write_to_file(writer, ifd2_offset);
    ASSERT_TRUE(write_result2.is_ok());
    
    // Read back
    BufferViewReader reader(writer.buffer());
    
    auto first_ifd_result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::BigTIFF, std::endian::little>(reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto first_ifd = first_ifd_result.value();
    
    // Read first IFD
    auto ifd1_result = ifd::read_ifd<BufferViewReader, TiffFormatType::BigTIFF, std::endian::little>(reader, first_ifd);
    ASSERT_TRUE(ifd1_result.is_ok());
    
    auto& ifd1 = ifd1_result.value();
    
    ExtractedTags<TagSpec> read_tags1;
    auto extract_result1 = read_tags1.extract<BufferViewReader, TiffFormatType::BigTIFF, std::endian::little>(
        reader, std::span(ifd1.tags));
    ASSERT_TRUE(extract_result1.is_ok());
    
    EXPECT_EQ(read_tags1.get<TagCode::ImageWidth>(), 300u);
    EXPECT_EQ(read_tags1.get<TagCode::ImageLength>(), 300u);
    
    // Read second IFD
    auto ifd2_result = ifd::read_ifd<BufferViewReader, TiffFormatType::BigTIFF, std::endian::little>(reader, ifd1.next_ifd_offset);
    ASSERT_TRUE(ifd2_result.is_ok());
    
    auto& ifd2 = ifd2_result.value();
    
    ExtractedTags<TagSpec> read_tags2;
    auto extract_result2 = read_tags2.extract<BufferViewReader, TiffFormatType::BigTIFF, std::endian::little>(
        reader, std::span(ifd2.tags));
    ASSERT_TRUE(extract_result2.is_ok());
    
    EXPECT_EQ(read_tags2.get<TagCode::ImageWidth>(), 400u);
    EXPECT_EQ(read_tags2.get<TagCode::ImageLength>(), 400u);
    EXPECT_EQ(ifd2.next_ifd_offset.value, 0u);
}

// ============================================================================
// Error Detection Tests
// ============================================================================

TEST(MultiPageTIFF, ErrorDetection_CircularReference) {
    using TagSpec = TagSpec<
        ImageWidthTag
    >;
    
    // Build two IFDs that form a circular reference
    IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd> builder1;
    ExtractedTags<TagSpec> tags1;
    tags1.get<TagCode::ImageWidth>() = 100;
    ASSERT_TRUE(builder1.add_tags(tags1).is_ok());
    
    IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd> builder2;
    ExtractedTags<TagSpec> tags2;
    tags2.get<TagCode::ImageWidth>() = 200;
    ASSERT_TRUE(builder2.add_tags(tags2).is_ok());
    
    BufferWriter writer;
    ASSERT_TRUE(writer.resize(2048));  // Preallocate buffer
    
    // Write header
    std::size_t header_size = sizeof(TiffHeader<std::endian::little>);
    std::size_t ifd1_offset = header_size;
    std::size_t ifd1_size = builder1.calculate_ifd_size();
    std::size_t ifd2_offset = ifd1_offset + ifd1_size;
    
    // Create circular reference: IFD1 -> IFD2 -> IFD1
    builder1.set_next_ifd_offset(ifd::IFDOffset(ifd2_offset));
    builder2.set_next_ifd_offset(ifd::IFDOffset(ifd1_offset));  // Points back!
    
    TiffHeader<std::endian::little> header;
    header.byte_order = {0x49, 0x49};
    header.version = 42;
    header.first_ifd_offset = static_cast<uint32_t>(ifd1_offset);
    
    auto header_view = writer.write(0, sizeof(header));
    ASSERT_TRUE(header_view.is_ok());
    std::memcpy(header_view.value().data().data(), &header, sizeof(header));
    auto header_flush_result = header_view.value().flush();
    ASSERT_TRUE(header_flush_result.is_ok());
    
    ASSERT_TRUE(builder1.write_to_file(writer, ifd1_offset).is_ok());
    ASSERT_TRUE(builder2.write_to_file(writer, ifd2_offset).is_ok());
    
    // Try to read - should detect circular reference
    BufferViewReader reader(writer.buffer());
    auto first_ifd_result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    std::vector<ifd::IFDOffset> visited;
    ifd::IFDOffset current = first_ifd_result.value();
    
    const int max_iterations = 10;
    bool found_circular = false;
    
    for (int i = 0; i < max_iterations && current.value != 0; ++i) {
        // Check if we've seen this offset before
        if (std::find(visited.begin(), visited.end(), current) != visited.end()) {
            found_circular = true;
            break;
        }
        
        visited.push_back(current);
        
        auto ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, current);
        ASSERT_TRUE(ifd_result.is_ok());
        
        current = ifd_result.value().next_ifd_offset;
    }
    
    EXPECT_TRUE(found_circular) << "Should have detected circular reference";
}

TEST(MultiPageTIFF, ErrorDetection_InvalidNextOffset) {
    using TagSpec = TagSpec<
        ImageWidthTag
    >;
    
    IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd> builder;
    ExtractedTags<TagSpec> tags;
    tags.get<TagCode::ImageWidth>() = 100;
    ASSERT_TRUE(builder.add_tags(tags).is_ok());
    
    // Set next IFD offset to invalid location (beyond buffer)
    builder.set_next_ifd_offset(ifd::IFDOffset(99999999));
    
    BufferWriter writer;
    ASSERT_TRUE(writer.resize(2048));  // Preallocate buffer
    std::size_t header_size = sizeof(TiffHeader<std::endian::little>);
    std::size_t ifd_offset = header_size;
    
    TiffHeader<std::endian::little> header;
    header.byte_order = {0x49, 0x49};
    header.version = 42;
    header.first_ifd_offset = static_cast<uint32_t>(ifd_offset);
    
    auto header_view = writer.write(0, sizeof(header));
    std::memcpy(header_view.value().data().data(), &header, sizeof(header));
    auto header_flush_result = header_view.value().flush();
    ASSERT_TRUE(header_flush_result.is_ok());
    
    ASSERT_TRUE(builder.write_to_file(writer, ifd_offset).is_ok());
    
    // Try to read
    BufferViewReader reader(writer.buffer());
    auto first_ifd_result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, first_ifd_result.value());
    ASSERT_TRUE(ifd_result.is_ok());
    
    auto next_ifd = ifd_result.value().next_ifd_offset;
    EXPECT_EQ(next_ifd.value, 99999999u);
    
    // Trying to read the next IFD should fail
    auto next_ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, next_ifd);
    EXPECT_FALSE(next_ifd_result.is_ok());
}

TEST(MultiPageTIFF, ErrorDetection_ZeroTagCount) {
    // Manually create an IFD with zero tags
    BufferWriter writer;
    ASSERT_TRUE(writer.resize(2048));  // Preallocate buffer
    std::size_t header_size = sizeof(TiffHeader<std::endian::little>);
    std::size_t ifd_offset = header_size;
    
    TiffHeader<std::endian::little> header;
    header.byte_order = {0x49, 0x49};
    header.version = 42;
    header.first_ifd_offset = static_cast<uint32_t>(ifd_offset);
    
    auto header_view = writer.write(0, sizeof(header));
    std::memcpy(header_view.value().data().data(), &header, sizeof(header));
    auto header_flush_result = header_view.value().flush();
    ASSERT_TRUE(header_flush_result.is_ok());
    
    // Write IFD with 0 tags
    uint16_t tag_count = 0;
    uint32_t next_ifd = 0;
    
    auto ifd_view = writer.write(ifd_offset, sizeof(tag_count) + sizeof(next_ifd));
    std::memcpy(ifd_view.value().data().data(), &tag_count, sizeof(tag_count));
    std::memcpy(ifd_view.value().data().data() + sizeof(tag_count), &next_ifd, sizeof(next_ifd));
    ASSERT_TRUE(ifd_view.value().flush().is_ok());
    
    // Try to read
    BufferViewReader reader(writer.buffer());
    auto first_ifd_result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, first_ifd_result.value());
    
    // Should succeed - zero tags is valid (though unusual)
    EXPECT_TRUE(ifd_result.is_ok());
    EXPECT_EQ(ifd_result.value().next_ifd_offset.value, 0u);
}

// ============================================================================
// Sub-IFD Tests
// ============================================================================

TEST(MultiPageTIFF, SubIFD_BasicReadWrite) {
    using MainTagSpec = TagSpec<
        ImageWidthTag,
        SubIFDTag
    >;
    
    using SubTagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag
    >;
    
    // Build main IFD
    IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd> main_builder;
    ExtractedTags<MainTagSpec> main_tags;
    main_tags.get<TagCode::ImageWidth>() = 1000;
    
    // Build sub-IFD
    IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd> sub_builder;
    ExtractedTags<SubTagSpec> sub_tags;
    sub_tags.get<TagCode::ImageWidth>() = 250;
    sub_tags.get<TagCode::ImageLength>() = 250;
    ASSERT_TRUE(sub_builder.add_tags(sub_tags).is_ok());
    
    BufferWriter writer;
    ASSERT_TRUE(writer.resize(4096));  // Preallocate buffer
    std::size_t header_size = sizeof(TiffHeader<std::endian::little>);
    std::size_t main_ifd_offset = header_size;
    std::size_t main_ifd_size = main_builder.calculate_ifd_size() + 20;  // Extra space for SubIFDs tag
    std::size_t sub_ifd_offset = main_ifd_offset + main_ifd_size;
    
    // Add SubIFDs tag to main IFD
    main_tags.get<TagCode::SubIFD>() = std::vector<uint32_t>{static_cast<uint32_t>(sub_ifd_offset)};
    ASSERT_TRUE(main_builder.add_tags(main_tags).is_ok());
    
    // Write header
    TiffHeader<std::endian::little> header;
    header.byte_order = {0x49, 0x49};
    header.version = 42;
    header.first_ifd_offset = static_cast<uint32_t>(main_ifd_offset);
    
    auto header_view = writer.write(0, sizeof(header));
    std::memcpy(header_view.value().data().data(), &header, sizeof(header));
    ASSERT_TRUE(header_view.value().flush().is_ok());
    
    // Write main IFD
    main_builder.set_next_ifd_offset(ifd::IFDOffset(0));
    ASSERT_TRUE(main_builder.write_to_file(writer, main_ifd_offset).is_ok());
    
    // Write sub-IFD
    sub_builder.set_next_ifd_offset(ifd::IFDOffset(0));
    ASSERT_TRUE(sub_builder.write_to_file(writer, sub_ifd_offset).is_ok());
    
    // Read back
    BufferViewReader reader(writer.buffer());
    auto first_ifd_result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    // Read main IFD
    auto main_ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, first_ifd_result.value());
    ASSERT_TRUE(main_ifd_result.is_ok());
    
    ExtractedTags<MainTagSpec> read_main_tags;
    auto extract_main_result = read_main_tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, std::span(main_ifd_result.value().tags));
    ASSERT_TRUE(extract_main_result.is_ok());
    
    EXPECT_EQ(read_main_tags.get<TagCode::ImageWidth>(), 1000u);
    
    auto sub_ifd_offsets = read_main_tags.get<TagCode::SubIFD>();
    ASSERT_EQ(sub_ifd_offsets.size(), 1u);
    EXPECT_EQ(sub_ifd_offsets[0], sub_ifd_offset);
    
    // Read sub-IFD
    auto sub_ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, ifd::IFDOffset(sub_ifd_offsets[0]));
    ASSERT_TRUE(sub_ifd_result.is_ok());
    
    ExtractedTags<SubTagSpec> read_sub_tags;
    auto extract_sub_result = read_sub_tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, std::span(sub_ifd_result.value().tags));
    ASSERT_TRUE(extract_sub_result.is_ok());
    
    EXPECT_EQ(read_sub_tags.get<TagCode::ImageWidth>(), 250u);
    EXPECT_EQ(read_sub_tags.get<TagCode::ImageLength>(), 250u);
}

TEST(MultiPageTIFF, SubIFD_MultipleThumbnails) {
    using MainTagSpec = TagSpec<
        ImageWidthTag,
        SubIFDTag
    >;
    
    using SubTagSpec = TagSpec<
        ImageWidthTag,
        ImageLengthTag
    >;
    
    constexpr int num_thumbnails = 3;
    std::vector<uint32_t> thumbnail_sizes = {64, 128, 256};
    std::vector<IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd>> sub_builders(num_thumbnails);
    std::vector<std::size_t> sub_ifd_offsets(num_thumbnails);
    
    // Build sub-IFDs for thumbnails
    for (int i = 0; i < num_thumbnails; ++i) {
        ExtractedTags<SubTagSpec> tags;
        tags.get<TagCode::ImageWidth>() = thumbnail_sizes[i];
        tags.get<TagCode::ImageLength>() = thumbnail_sizes[i];
        ASSERT_TRUE(sub_builders[i].add_tags(tags).is_ok());
    }
    
    // Build main IFD
    IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd> main_builder;
    
    BufferWriter writer;
    ASSERT_TRUE(writer.resize(8192));  // Preallocate buffer
    std::size_t header_size = sizeof(TiffHeader<std::endian::little>);
    std::size_t main_ifd_offset = header_size;
    
    // Calculate positions for all sub-IFDs
    std::size_t current_offset = main_ifd_offset + 200;  // Space for main IFD
    for (int i = 0; i < num_thumbnails; ++i) {
        sub_ifd_offsets[i] = current_offset;
        current_offset += sub_builders[i].calculate_ifd_size();
    }
    
    // Create main IFD with SubIFDs tag
    ExtractedTags<MainTagSpec> main_tags;
    main_tags.get<TagCode::ImageWidth>() = 2000;
    std::vector<uint32_t> sub_offsets_u32;
    for (auto offset : sub_ifd_offsets) {
        sub_offsets_u32.push_back(static_cast<uint32_t>(offset));
    }
    main_tags.get<TagCode::SubIFD>() = sub_offsets_u32;
    ASSERT_TRUE(main_builder.add_tags(main_tags).is_ok());
    
    // Write header
    TiffHeader<std::endian::little> header;
    header.byte_order = {0x49, 0x49};
    header.version = 42;
    header.first_ifd_offset = static_cast<uint32_t>(main_ifd_offset);
    
    auto header_view = writer.write(0, sizeof(header));
    std::memcpy(header_view.value().data().data(), &header, sizeof(header));
    ASSERT_TRUE(header_view.value().flush().is_ok());
    
    // Write main IFD
    main_builder.set_next_ifd_offset(ifd::IFDOffset(0));
    ASSERT_TRUE(main_builder.write_to_file(writer, main_ifd_offset).is_ok());
    
    // Write all sub-IFDs
    for (int i = 0; i < num_thumbnails; ++i) {
        sub_builders[i].set_next_ifd_offset(ifd::IFDOffset(0));
        ASSERT_TRUE(sub_builders[i].write_to_file(writer, sub_ifd_offsets[i]).is_ok());
    }
    
    // Read back and verify
    BufferViewReader reader(writer.buffer());
    auto first_ifd_result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    auto main_ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, first_ifd_result.value());
    ASSERT_TRUE(main_ifd_result.is_ok());
    auto tag_array = main_ifd_result.value().tags;
    
    ExtractedTags<MainTagSpec> read_main_tags;
    auto extract_main_result = read_main_tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
        reader, std::span(main_ifd_result.value().tags));
    ASSERT_TRUE(extract_main_result.is_ok());

    auto sub_offsets = read_main_tags.get<TagCode::SubIFD>();
    ASSERT_EQ(sub_offsets.size(), num_thumbnails);
    
    // Read and verify each thumbnail
    for (int i = 0; i < num_thumbnails; ++i) {
        auto sub_ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
            reader, ifd::IFDOffset(sub_offsets[i]));
        ASSERT_TRUE(sub_ifd_result.is_ok());
        
        ExtractedTags<SubTagSpec> read_sub_tags;
        auto extract_sub_result = read_sub_tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
            reader, std::span(sub_ifd_result.value().tags));
        ASSERT_TRUE(extract_sub_result.is_ok());
        
        EXPECT_EQ(read_sub_tags.get<TagCode::ImageWidth>(), thumbnail_sizes[i]);
        EXPECT_EQ(read_sub_tags.get<TagCode::ImageLength>(), thumbnail_sizes[i]);
    }
}

// ============================================================================
// Page Index/Navigation Tests
// ============================================================================

TEST(MultiPageTIFF, PageNavigation_RandomAccess) {
    using TagSpec = TagSpec<
        ImageWidthTag,
        PageNumberTag
    >;
    
    constexpr int num_pages = 10;
    std::vector<IFDBuilder<TiffFormatType::Classic, std::endian::little, IFDAtEnd>> builders(num_pages);
    std::vector<std::size_t> ifd_offsets(num_pages);
    
    // Build pages
    for (int i = 0; i < num_pages; ++i) {
        ExtractedTags<TagSpec> tags;
        tags.get<TagCode::ImageWidth>() = 100 * (i + 1);
        tags.get<TagCode::PageNumber>() = std::array<uint16_t, 2>{
            static_cast<uint16_t>(i), 
            static_cast<uint16_t>(num_pages)
        };
        ASSERT_TRUE(builders[i].add_tags(tags).is_ok());
    }
    
    // Calculate offsets
    BufferWriter writer;
    ASSERT_TRUE(writer.resize(8192));  // Preallocate buffer
    std::size_t current_offset = sizeof(TiffHeader<std::endian::little>);
    for (int i = 0; i < num_pages; ++i) {
        ifd_offsets[i] = current_offset;
        current_offset += builders[i].calculate_ifd_size();
    }
    
    // Write header
    TiffHeader<std::endian::little> header;
    header.byte_order = {0x49, 0x49};
    header.version = 42;
    header.first_ifd_offset = static_cast<uint32_t>(ifd_offsets[0]);
    
    auto header_view = writer.write(0, sizeof(header));
    std::memcpy(header_view.value().data().data(), &header, sizeof(header));
    ASSERT_TRUE(header_view.value().flush().is_ok());
    
    // Write all IFDs
    for (int i = 0; i < num_pages; ++i) {
        if (i < num_pages - 1) {
            builders[i].set_next_ifd_offset(ifd::IFDOffset(ifd_offsets[i + 1]));
        } else {
            builders[i].set_next_ifd_offset(ifd::IFDOffset(0));
        }
        ASSERT_TRUE(builders[i].write_to_file(writer, ifd_offsets[i]).is_ok());
    }
    
    // Build an index of all page offsets by traversing the chain
    BufferViewReader reader(writer.buffer());
    auto first_ifd_result = ifd::get_first_ifd_offset<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader);
    ASSERT_TRUE(first_ifd_result.is_ok());
    
    std::vector<ifd::IFDOffset> page_index;
    ifd::IFDOffset current = first_ifd_result.value();
    
    while (current.value != 0) {
        page_index.push_back(current);
        
        auto ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, current);
        ASSERT_TRUE(ifd_result.is_ok());
        
        current = ifd_result.value().next_ifd_offset;
    }
    
    ASSERT_EQ(page_index.size(), num_pages);
    
    // Now access pages in random order using the index
    std::vector<int> access_order = {5, 2, 8, 0, 9, 3, 1, 7, 4, 6};
    
    for (int page_num : access_order) {
        auto ifd_result = ifd::read_ifd<BufferViewReader, TiffFormatType::Classic, std::endian::little>(reader, page_index[page_num]);
        ASSERT_TRUE(ifd_result.is_ok());
        
        ExtractedTags<TagSpec> tags;
        auto extract_result = tags.extract<BufferViewReader, TiffFormatType::Classic, std::endian::little>(
            reader, std::span(ifd_result.value().tags));
        ASSERT_TRUE(extract_result.is_ok());
        
        EXPECT_EQ(tags.get<TagCode::ImageWidth>(), 100 * (page_num + 1));
        auto page_info = tags.get<TagCode::PageNumber>();
        EXPECT_EQ(page_info[0], page_num);
    }
}
