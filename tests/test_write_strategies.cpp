#include <gtest/gtest.h>
#include <vector>
#include <algorithm>

#include "../tiffconcept/include/tiff/reader_buffer.hpp"
#include "../tiffconcept/include/tiff/result.hpp"
#include "../tiffconcept/include/tiff/write_strategy.hpp"

using namespace tiffconcept;

// ============================================================================
// Helper Functions
// ============================================================================

std::vector<ChunkWriteInfo> create_test_chunks(uint32_t count) {
    std::vector<ChunkWriteInfo> chunks;
    for (uint32_t i = 0; i < count; ++i) {
        ChunkWriteInfo chunk;
        chunk.chunk_index = i;
        chunk.pixel_x = (i % 4) * 64;
        chunk.pixel_y = (i / 4) * 64;
        chunk.pixel_z = 0;
        chunk.width = 64;
        chunk.height = 64;
        chunk.depth = 1;
        chunk.plane = 0;
        chunk.uncompressed_size = 64 * 64;
        chunk.compressed_size = 64 * 64;
        chunk.file_offset = 0;
        chunks.push_back(chunk);
    }
    return chunks;
}

std::vector<std::byte> create_test_data(std::size_t size) {
    std::vector<std::byte> data(size);
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::byte>(i % 256);
    }
    return data;
}

// ============================================================================
// IFD Placement Strategy Tests
// ============================================================================

TEST(IFDPlacementStrategy, IFDAtBeginning) {
    IFDAtBeginning strategy;
    
    std::size_t ifd_offset = strategy.calculate_ifd_offset(8, 100, 200);
    EXPECT_EQ(ifd_offset, 8);  // Right after current position
    
    std::size_t external_offset = strategy.calculate_external_data_offset(8, 8, 100, 200);
    EXPECT_EQ(external_offset, 8 + 100);  // After IFD
    
    EXPECT_FALSE(strategy.write_data_before_ifd);
}

TEST(IFDPlacementStrategy, IFDAtEnd) {
    IFDAtEnd strategy;
    
    std::size_t ifd_offset = strategy.calculate_ifd_offset(1000, 100, 200);
    EXPECT_EQ(ifd_offset, 1000);  // At current position
    
    std::size_t external_offset = strategy.calculate_external_data_offset(1000, 1000, 100, 200);
    EXPECT_EQ(external_offset, 1000 + 100);  // After IFD
    
    EXPECT_TRUE(strategy.write_data_before_ifd);
}

TEST(IFDPlacementStrategy, IFDInlineDefault) {
    IFDInline strategy;
    
    std::size_t ifd_offset = strategy.calculate_ifd_offset(500, 100, 200);
    EXPECT_EQ(ifd_offset, 500);  // No preferred offset, use current
    
    EXPECT_TRUE(strategy.write_data_before_ifd);
}

TEST(IFDPlacementStrategy, IFDInlinePreferred) {
    IFDInline strategy;
    strategy.preferred_offset = 300;
    
    std::size_t ifd_offset = strategy.calculate_ifd_offset(500, 100, 200);
    EXPECT_EQ(ifd_offset, 300);  // Use preferred offset
}

// ============================================================================
// Tile Ordering Strategy Tests
// ============================================================================

TEST(TileOrderingStrategy, ImageOrderTiles) {
    ImageOrderTiles strategy;
    
    auto chunks = create_test_chunks(16);  // 4x4 grid
    
    // Scramble order
    std::reverse(chunks.begin(), chunks.end());
    
    strategy.order_chunks(std::span<ChunkWriteInfo>(chunks));
    
    // Should be sorted by z, plane, y, x
    for (std::size_t i = 1; i < chunks.size(); ++i) {
        const auto& prev = chunks[i - 1];
        const auto& curr = chunks[i];
        
        bool correct_order = 
            (prev.pixel_z < curr.pixel_z) ||
            (prev.pixel_z == curr.pixel_z && prev.plane < curr.plane) ||
            (prev.pixel_z == curr.pixel_z && prev.plane == curr.plane && prev.pixel_y < curr.pixel_y) ||
            (prev.pixel_z == curr.pixel_z && prev.plane == curr.plane && prev.pixel_y == curr.pixel_y && prev.pixel_x <= curr.pixel_x);
        
        EXPECT_TRUE(correct_order);
    }
    
    EXPECT_TRUE(strategy.supports_parallel_encoding);
}

TEST(TileOrderingStrategy, SequentialTiles) {
    SequentialTiles strategy;
    
    auto chunks = create_test_chunks(10);
    
    // Scramble by spatial position but keep chunk_index
    for (auto& chunk : chunks) {
        chunk.pixel_x = 1000 - chunk.pixel_x;
    }
    
    strategy.order_chunks(std::span<ChunkWriteInfo>(chunks));
    
    // Should be sorted by chunk_index
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        EXPECT_EQ(chunks[i].chunk_index, i);
    }
    
    EXPECT_FALSE(strategy.supports_parallel_encoding);
}

TEST(TileOrderingStrategy, OnDemandTiles) {
    OnDemandTiles strategy;
    
    auto chunks = create_test_chunks(10);
    auto original = chunks;
    
    // Scramble
    std::reverse(chunks.begin(), chunks.end());
    
    strategy.order_chunks(std::span<ChunkWriteInfo>(chunks));
    
    // Should not reorder
    EXPECT_NE(chunks, original);  // Still scrambled
    
    EXPECT_TRUE(strategy.supports_parallel_encoding);
}

// ============================================================================
// Buffering Strategy Tests
// ============================================================================

TEST(BufferingStrategy, DirectWrite) {
    BufferWriter writer;
    DirectWrite<BufferWriter> strategy;

    auto data = create_test_data(100);
    
    auto result = strategy.write(writer, 0, std::span<const std::byte>(data));
    ASSERT_TRUE(result.is_ok());
    
    auto flush_result = strategy.flush(writer);
    ASSERT_TRUE(flush_result.is_ok());
    
    EXPECT_FALSE(strategy.uses_temporary_buffer);
    
    // Verify data was written
    auto size_result = writer.size();
    ASSERT_TRUE(size_result.is_ok());
    EXPECT_EQ(size_result.value(), 100);
}

TEST(BufferingStrategy, DirectWriteMultipleWrites) {
    BufferWriter writer;
    DirectWrite<BufferWriter> strategy;
    
    auto data1 = create_test_data(50);
    auto data2 = create_test_data(50);
    
    ASSERT_TRUE(strategy.write(writer, 0, std::span<const std::byte>(data1)).is_ok());
    ASSERT_TRUE(strategy.write(writer, 50, std::span<const std::byte>(data2)).is_ok());
    ASSERT_TRUE(strategy.flush(writer).is_ok());
    
    auto size_result = writer.size();
    ASSERT_TRUE(size_result.is_ok());
    EXPECT_EQ(size_result.value(), 100);
}

TEST(BufferingStrategy, BufferedWrite) {
    BufferWriter writer;
    BufferedWrite<BufferWriter> strategy(64);  // 64-byte flush threshold
    
    auto data = create_test_data(32);
    
    // First write - should buffer
    ASSERT_TRUE(strategy.write(writer, 0, std::span<const std::byte>(data)).is_ok());
    
    // Not flushed yet
    auto size_result = writer.size();
    EXPECT_EQ(size_result.value_or(0), 0);
    
    // Second write - should trigger flush (total 64 bytes)
    ASSERT_TRUE(strategy.write(writer, 32, std::span<const std::byte>(data)).is_ok());
    
    // Should be flushed now
    size_result = writer.size();
    EXPECT_GE(size_result.value_or(0), 64);
    
    EXPECT_TRUE(strategy.uses_temporary_buffer);
}

TEST(BufferingStrategy, BufferedWriteExplicitFlush) {
    BufferWriter writer;
    BufferedWrite<BufferWriter> strategy(1024);  // Large threshold
    
    auto data = create_test_data(100);
    
    ASSERT_TRUE(strategy.write(writer, 0, std::span<const std::byte>(data)).is_ok());
    
    // Not auto-flushed
    auto size_result = writer.size();
    EXPECT_EQ(size_result.value_or(0), 0);
    
    // Explicit flush
    ASSERT_TRUE(strategy.flush(writer).is_ok());
    
    size_result = writer.size();
    EXPECT_EQ(size_result.value(), 100);
}

TEST(BufferingStrategy, BufferedWriteClear) {
    BufferWriter writer;
    BufferedWrite<BufferWriter> strategy;
    
    auto data = create_test_data(50);
    ASSERT_TRUE(strategy.write(writer, 0, std::span<const std::byte>(data)).is_ok());
    
    strategy.clear();
    
    // After clear, new writes should work
    ASSERT_TRUE(strategy.write(writer, 0, std::span<const std::byte>(data)).is_ok());
    ASSERT_TRUE(strategy.flush(writer).is_ok());
}

TEST(BufferingStrategy, StreamingWrite) {
    BufferWriter writer;
    StreamingWrite<BufferWriter> strategy;
    
    strategy.set_base_offset(1000);
    
    auto data1 = create_test_data(100);
    auto data2 = create_test_data(100);
    
    // Write to temp buffer
    ASSERT_TRUE(strategy.write(writer, 1000, std::span<const std::byte>(data1)).is_ok());
    ASSERT_TRUE(strategy.write(writer, 1100, std::span<const std::byte>(data2)).is_ok());
    
    // Nothing written to file yet
    auto size_result = writer.size();
    EXPECT_EQ(size_result.value_or(0), 0);
    
    // Flush everything at once
    ASSERT_TRUE(strategy.flush(writer).is_ok());
    
    // Now data should be written
    size_result = writer.size();
    EXPECT_EQ(size_result.value(), 1200);  // base_offset + buffer_size
    
    EXPECT_TRUE(strategy.uses_temporary_buffer);
}

TEST(BufferingStrategy, StreamingWriteBufferSize) {
    BufferWriter writer;
    StreamingWrite<BufferWriter> strategy;
    
    strategy.set_base_offset(0);
    
    auto data = create_test_data(500);
    ASSERT_TRUE(strategy.write(writer, 0, std::span<const std::byte>(data)).is_ok());
    
    EXPECT_EQ(strategy.buffer_size(), 500);
}

// ============================================================================
// Offset Resolution Strategy Tests
// ============================================================================

TEST(OffsetResolutionStrategy, TwoPassOffsets) {
    TwoPassOffsets strategy;
    
    EXPECT_TRUE(strategy.requires_size_precalculation);
    EXPECT_FALSE(strategy.write_offsets_immediately);
    EXPECT_TRUE(strategy.supports_streaming);
}

TEST(OffsetResolutionStrategy, LazyOffsets) {
    LazyOffsets strategy;
    
    EXPECT_FALSE(strategy.requires_size_precalculation);
    EXPECT_FALSE(strategy.write_offsets_immediately);
    EXPECT_FALSE(strategy.supports_streaming);
}

TEST(OffsetResolutionStrategy, ImmediateOffsets) {
    ImmediateOffsets strategy;
    
    EXPECT_FALSE(strategy.requires_size_precalculation);
    EXPECT_TRUE(strategy.write_offsets_immediately);
    EXPECT_FALSE(strategy.supports_streaming);
}

// ============================================================================
// Write Config Tests
// ============================================================================

TEST(WriteConfig, OptimizedForReading) {
    using Config = OptimizedForReadingConfig<BufferWriter>;
    
    // Should compile successfully
    using IFDPlacement = Config::ifd_placement_strategy;
    using TileOrdering = Config::tile_ordering_strategy;
    using Buffering = Config::buffering_strategy;
    using OffsetRes = Config::offset_resolution_strategy;
    
    // Verify expected types
    static_assert(std::is_same_v<IFDPlacement, IFDAtBeginning>);
    static_assert(std::is_same_v<TileOrdering, ImageOrderTiles>);
    static_assert(std::is_same_v<Buffering, StreamingWrite<BufferWriter>>);
    static_assert(std::is_same_v<OffsetRes, TwoPassOffsets>);
}

TEST(WriteConfig, OptimizedForWriting) {
    using Config = OptimizedForWritingConfig<BufferWriter>;
    
    static_assert(std::is_same_v<typename Config::ifd_placement_strategy, IFDAtEnd>);
    static_assert(std::is_same_v<typename Config::tile_ordering_strategy, SequentialTiles>);
    static_assert(std::is_same_v<typename Config::offset_resolution_strategy, LazyOffsets>);
}

TEST(WriteConfig, StreamingConfig) {
    using Config = StreamingConfig<BufferWriter>;
    
    static_assert(std::is_same_v<typename Config::ifd_placement_strategy, IFDAtEnd>);
    static_assert(std::is_same_v<typename Config::tile_ordering_strategy, SequentialTiles>);
    static_assert(std::is_same_v<typename Config::offset_resolution_strategy, TwoPassOffsets>);
}

TEST(WriteConfig, EditInPlaceConfig) {
    using Config = EditInPlaceConfig<BufferWriter>;
    
    static_assert(std::is_same_v<typename Config::ifd_placement_strategy, IFDInline>);
    static_assert(std::is_same_v<typename Config::tile_ordering_strategy, OnDemandTiles>);
    static_assert(std::is_same_v<typename Config::offset_resolution_strategy, ImmediateOffsets>);
}

TEST(WriteConfig, CustomConfiguration) {
    // Custom mix of strategies
    using CustomConfig = WriteConfig<
        IFDAtBeginning,
        SequentialTiles,
        BufferedWrite<BufferWriter>,
        TwoPassOffsets
    >;
    
    // Should compile and have correct types
    static_assert(std::is_same_v<typename CustomConfig::ifd_placement_strategy, IFDAtBeginning>);
    static_assert(std::is_same_v<typename CustomConfig::tile_ordering_strategy, SequentialTiles>);
    static_assert(std::is_same_v<typename CustomConfig::offset_resolution_strategy, TwoPassOffsets>);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(WriteStrategies, CompleteWorkflow) {
    using Config = OptimizedForReadingConfig<BufferWriter>;
    
    BufferWriter writer;
    
    // Use strategies
    typename Config::tile_ordering_strategy ordering;
    StreamingWrite<BufferWriter> buffering;  // From config
    
    // Create and order chunks
    auto chunks = create_test_chunks(16);
    ordering.order_chunks(std::span<ChunkWriteInfo>(chunks));
    
    // Write data using buffering
    buffering.set_base_offset(1000);
    for (const auto& chunk : chunks) {
        auto data = create_test_data(chunk.compressed_size);
        ASSERT_TRUE(buffering.write(writer, 1000 + chunk.chunk_index * 100,
                                   std::span<const std::byte>(data)).is_ok());
    }
    
    // Flush
    ASSERT_TRUE(buffering.flush(writer).is_ok());
    
    // Verify something was written
    auto size_result = writer.size();
    ASSERT_TRUE(size_result.is_ok());
    EXPECT_GT(size_result.value(), 0);
}

TEST(WriteStrategies, StrategyCompatibility) {
    // These should all compile (compatible strategies)
    {
        using Config = WriteConfig<IFDAtBeginning, ImageOrderTiles, 
                                  StreamingWrite<BufferWriter>, TwoPassOffsets>;
        static_assert(std::is_same_v<typename Config::ifd_placement_strategy, IFDAtBeginning>);
    }
    {
        using Config = WriteConfig<IFDAtEnd, SequentialTiles,
                                  DirectWrite<BufferWriter>, LazyOffsets>;
        static_assert(std::is_same_v<typename Config::ifd_placement_strategy, IFDAtEnd>);
    }
    {
        using Config = WriteConfig<IFDInline, OnDemandTiles,
                                  DirectWrite<BufferWriter>, ImmediateOffsets>;
        static_assert(std::is_same_v<typename Config::ifd_placement_strategy, IFDInline>);
    }
}

// ============================================================================
// Concept Validation Tests
// ============================================================================

TEST(Concepts, IFDPlacementStrategyConcept) {
    static_assert(IFDPlacementStrategy<IFDAtBeginning>);
    static_assert(IFDPlacementStrategy<IFDAtEnd>);
    static_assert(IFDPlacementStrategy<IFDInline>);
}

TEST(Concepts, TileOrderingStrategyConcept) {
    static_assert(TileOrderingStrategy<ImageOrderTiles>);
    static_assert(TileOrderingStrategy<SequentialTiles>);
    static_assert(TileOrderingStrategy<OnDemandTiles>);
}

TEST(Concepts, BufferingStrategyConcept) {
    static_assert(BufferingStrategy<DirectWrite<BufferWriter>, BufferWriter>);
    static_assert(BufferingStrategy<BufferedWrite<BufferWriter>, BufferWriter>);
    static_assert(BufferingStrategy<StreamingWrite<BufferWriter>, BufferWriter>);
}

TEST(Concepts, OffsetResolutionStrategyConcept) {
    static_assert(OffsetResolutionStrategy<TwoPassOffsets>);
    static_assert(OffsetResolutionStrategy<LazyOffsets>);
    static_assert(OffsetResolutionStrategy<ImmediateOffsets>);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
