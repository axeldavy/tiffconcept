#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>
#include "../types/result.hpp"
#include "../types/tiff_spec.hpp"
#include "../types/tile_info.hpp"

namespace tiffconcept {

/// Concept for a compressor implementation
template<typename T>
concept CompressorImpl = requires(const T& compressor, 
                                  std::vector<std::byte>& output,
                                  std::size_t offset,
                                  std::span<const std::byte> input,
                                  const TileSize& tile_size,
                                  std::span<const SampleFormat> sample_formats,
                                  std::span<const uint8_t> bits_per_sample,
                                  std::endian endianness) {
    { compressor.compress(output, offset, input, tile_size, sample_formats, bits_per_sample, endianness) } 
        -> std::same_as<Result<std::size_t>>;
    { T::get_default_scheme() } -> std::same_as<CompressionScheme>;
    { T::supports_format(tile_size, sample_formats, bits_per_sample, endianness) } 
        -> std::same_as<bool>;
};

/// Compressor descriptor - defines a compressor at compile time
/// Can handle multiple compression scheme values (e.g., ZSTD and ZSTD_Alt)
template <typename CompressorType, CompressionScheme... Schemes>
    requires CompressorImpl<CompressorType> && (sizeof...(Schemes) > 0)
struct CompressorDescriptor {
    using compressor_type = CompressorType;
    static constexpr std::array<CompressionScheme, sizeof...(Schemes)> schemes = {Schemes...};
    
    /// Check if this compressor handles a specific scheme
    static constexpr bool handles(CompressionScheme scheme) noexcept {
        for (auto s : schemes) {
            if (s == scheme) return true;
        }
        return false;
    }
    
    /// Check if this compressor supports a specific format configuration
    static bool supports_format(const TileSize& tile_size,
                               std::span<const SampleFormat> sample_formats,
                               std::span<const uint8_t> bits_per_sample,
                               std::endian endianness) noexcept {
        return CompressorType::supports_format(tile_size, sample_formats, bits_per_sample, endianness);
    }
};

/// Concept to check if a type is a CompressorDescriptor
template <typename T>
concept CompressorDescriptorType = requires {
    typename T::compressor_type;
    { T::schemes } -> std::convertible_to<std::span<const CompressionScheme>>;
    { T::handles(CompressionScheme::None) } -> std::same_as<bool>;
    { T::supports_format(TileSize{}, std::span<const SampleFormat>{}, std::span<const uint8_t>{}, std::endian::native) } 
        -> std::same_as<bool>;
    requires CompressorImpl<typename T::compressor_type>;
};

/// Type trait to check if a compression scheme is supported by any compressor
/// Note: This only checks compression scheme, not format compatibility
template <CompressionScheme Scheme, typename... Compressors>
struct has_compressor_for : std::false_type {};

template <CompressionScheme Scheme, typename First, typename... Rest>
struct has_compressor_for<Scheme, First, Rest...> 
    : std::conditional_t<First::handles(Scheme), 
                         std::true_type, 
                         has_compressor_for<Scheme, Rest...>> {};

template <CompressionScheme Scheme, typename... Compressors>
inline constexpr bool has_compressor_for_v = has_compressor_for<Scheme, Compressors...>::value;

/// Compile-time compressor specification
template <CompressorDescriptorType... Compressors>
struct CompressorSpec {
    static constexpr std::size_t num_compressors = sizeof...(Compressors);

    /// Check if a compression scheme is supported (compile-time, scheme only)
    template <CompressionScheme Scheme>
    static constexpr bool supports() noexcept {
        return has_compressor_for_v<Scheme, Compressors...>;
    }
    
    /// Check if a compression scheme is supported (runtime version, scheme only)
    static constexpr bool supports(CompressionScheme scheme) noexcept {
        return (Compressors::handles(scheme) || ...);
    }
    
    /// Check if a compression scheme and format combination is supported (runtime version)
    static bool supports(CompressionScheme scheme,
                        const TileSize& tile_size,
                        std::span<const SampleFormat> sample_formats,
                        std::span<const uint8_t> bits_per_sample,
                        std::endian endianness) noexcept {
        // Check each compressor that handles this scheme
        bool found = false;
        auto check = [&]<typename Desc>() {
            if (Desc::handles(scheme) && 
                Desc::supports_format(tile_size, sample_formats, bits_per_sample, endianness)) {
                found = true;
            }
        };
        (check.template operator()<Compressors>(), ...);
        return found;
    }
    
    /// Apply a function to each compressor descriptor at compile time
    template <typename F>
    static constexpr void for_each(F&& func) {
        (func.template operator()<Compressors>(), ...);
    }
};

/// Concept to validate CompressorSpec structure at compile time
template <typename T>
concept ValidCompressorSpec = requires {
    { T::num_compressors } -> std::convertible_to<std::size_t>;
    requires T::num_compressors > 0;

    // Check that T has the expected interface of CompressorSpec
    { T::template supports<CompressionScheme::None>() } -> std::same_as<bool>; // compile-time version
    { T::supports(CompressionScheme::None) } -> std::same_as<bool>; // runtime version
    { T::supports(CompressionScheme::None, TileSize{}, std::span<const SampleFormat>{}, 
                  std::span<const uint8_t>{}, std::endian::native) } -> std::same_as<bool>;
};

/// Storage helper for a single compressor
template <typename CompressorDesc>
    requires CompressorDescriptorType<CompressorDesc>
class CompressorHolder {
private:
    [[no_unique_address]] mutable typename CompressorDesc::compressor_type compressor_;

public:
    constexpr CompressorHolder() noexcept = default;
    
    [[nodiscard]] Result<std::size_t> compress(
        std::vector<std::byte>& output,
        std::size_t offset,
        std::span<const std::byte> input,
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        std::endian endianness) const noexcept {
        return compressor_.compress(output, offset, input, tile_size, sample_formats, bits_per_sample, endianness);
    }

    /// Get the default compression scheme
    [[nodiscard]] static constexpr CompressionScheme get_default_scheme() noexcept {
        return CompressorDesc::compressor_type::get_default_scheme();
    }
    
    [[nodiscard]] static bool supports_format(
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        std::endian endianness) noexcept {
        return CompressorDesc::supports_format(tile_size, sample_formats, bits_per_sample, endianness);
    }
    
    static constexpr bool handles(CompressionScheme scheme) noexcept {
        return CompressorDesc::handles(scheme);
    }
};

/// Compressor storage with dynamic dispatch based on scheme and format
template <typename CompSpec>
    requires ValidCompressorSpec<CompSpec>
class CompressorStorage {
private:
    // Tuple of all compressor holders
    [[no_unique_address]] decltype([]<CompressorDescriptorType... Descs>(CompressorSpec<Descs...>*) {
        return std::tuple<CompressorHolder<Descs>...>{};
    }(static_cast<CompSpec*>(nullptr))) holders_;
    
    /// Helper to compress using the correct compressor at runtime
    template <std::size_t I = 0>
    [[nodiscard]] Result<std::size_t> compress_impl(
        std::vector<std::byte>& output,
        std::size_t offset,
        std::span<const std::byte> input,
        CompressionScheme scheme,
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        std::endian endianness) const noexcept {
        
        if constexpr (I < std::tuple_size_v<decltype(holders_)>) {
            using HolderType = std::tuple_element_t<I, decltype(holders_)>;
            if (HolderType::handles(scheme) && 
                HolderType::supports_format(tile_size, sample_formats, bits_per_sample, endianness)) {
                const auto& holder = std::get<I>(holders_);
                return holder.compress(output, offset, input, tile_size, sample_formats, bits_per_sample, endianness);
            }
            return compress_impl<I + 1>(output, offset, input, scheme, tile_size, sample_formats, bits_per_sample, endianness);
        } else {
            return Err(Error::Code::UnsupportedFeature, 
                      "Compression scheme or format not supported in this build");
        }
    }

public:
    constexpr CompressorStorage() noexcept = default;
    ~CompressorStorage() = default;
    
    CompressorStorage(const CompressorStorage&) = delete;
    CompressorStorage& operator=(const CompressorStorage&) = delete;
    
    constexpr CompressorStorage(CompressorStorage&&) noexcept = default;
    constexpr CompressorStorage& operator=(CompressorStorage&&) noexcept = default;
    
    /// Compress data based on compression scheme and format
    /// @param output The output vector - will be resized if needed
    /// @param offset Starting position in the output vector
    /// @param input Input data to compress
    /// @param scheme Compression scheme to use
    /// @param tile_size Tile dimensions
    /// @param sample_formats Sample format for each channel
    /// @param bits_per_sample Bits per sample for each channel
    /// @param endianness Endianness of the data
    /// @return Number of bytes written (advance from offset)
    [[nodiscard]] Result<std::size_t> compress(
        std::vector<std::byte>& output,
        std::size_t offset,
        std::span<const std::byte> input,
        CompressionScheme scheme,
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        std::endian endianness) const noexcept {
        
        return compress_impl(output, offset, input, scheme, tile_size, sample_formats, bits_per_sample, endianness);
    }
    
    /// Check if a compression scheme is supported at compile time (scheme only)
    template <CompressionScheme Scheme>
    static constexpr bool supports() noexcept {
        return CompSpec::template supports<Scheme>();
    }
    
    /// Check if a compression scheme is supported at runtime (scheme only)
    static constexpr bool supports(CompressionScheme scheme) noexcept {
        return CompSpec::supports(scheme);
    }
    
    /// Check if a compression scheme and format combination is supported at runtime
    static bool supports(CompressionScheme scheme,
                        const TileSize& tile_size,
                        std::span<const SampleFormat> sample_formats,
                        std::span<const uint8_t> bits_per_sample,
                        std::endian endianness) noexcept {
        return CompSpec::supports(scheme, tile_size, sample_formats, bits_per_sample, endianness);
    }
};

} // namespace tiffconcept