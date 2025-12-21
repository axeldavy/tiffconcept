#pragma once

#include <cstdint>

namespace tiffconcept {

/// @brief Logical coordinates of a tile/strip in the image
struct TileCoordinates {
    uint32_t x{0};          ///< X coordinate
    uint32_t y{0};          ///< Y coordinate
    uint32_t z{0};          ///< Z coordinate
    uint32_t s{0};          ///< Sample coordinate (== channel == plane)
};

/// @brief Dimensions of a tile/strip
struct TileSize {
    uint32_t width{0};      ///< Width in pixels
    uint32_t height{1};     ///< Height in pixels
    uint32_t depth{1};      ///< Depth in pixels
    uint32_t nsamples{1};   ///< Number of samples (channels) per pixel
};

/// @brief Identity and geometry of a tile
/// @note Describes "what" the tile is, not where it is stored
struct TileDescriptor {
    uint32_t index{0};      ///< Linear index (tile or strip index)
    TileCoordinates coords; ///< Logical coordinates
    TileSize size;          ///< Dimensions
};

/// @brief Physical location of data in the file
/// @note Describes "where" the tile is stored
struct FileSpan {
    uint64_t offset{0};     ///< File offset in bytes
    uint64_t length{0};     ///< Length in bytes (compressed size)

    [[nodiscard]] constexpr bool is_empty() const noexcept { return length == 0; } /// Empty spans mean missing tiles
    [[nodiscard]] constexpr uint64_t end_offset() const noexcept { return offset + length; }
};

/// @brief Complete tile information (Logical + Physical)
/// @note Used for both reading (input location) and writing (output location)
struct Tile {
    TileDescriptor id;      ///< Logical description
    FileSpan location;      ///< Physical location
};

} // namespace tiffconcept