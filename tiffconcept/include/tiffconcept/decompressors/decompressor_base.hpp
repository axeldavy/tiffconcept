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

/// Concept for a decompressor implementation
template<typename T>
concept DecompressorImpl = requires(const T& decompressor, 
                                    std::span<std::byte> output,
                                    std::span<const std::byte> input,
                                    const TileSize& tile_size,
                                    std::span<const SampleFormat> sample_formats,
                                    std::span<const uint8_t> bits_per_sample,
                                    std::endian endianness) {
    { decompressor.decompress(output, input, tile_size, sample_formats, bits_per_sample, endianness) } 
        -> std::same_as<Result<std::size_t>>;
    { T::supports_format(tile_size, sample_formats, bits_per_sample, endianness) } 
        -> std::same_as<bool>;
};

/// Decompressor descriptor - defines a decompressor at compile time
/// Can handle multiple compression scheme values (e.g., ZSTD and ZSTD_Alt)
template <typename DecompressorType, CompressionScheme... Schemes>
    requires DecompressorImpl<DecompressorType> && (sizeof...(Schemes) > 0)
struct DecompressorDescriptor {
    using decompressor_type = DecompressorType;
    static constexpr std::array<CompressionScheme, sizeof...(Schemes)> schemes = {Schemes...};
    
    /// Check if this decompressor handles a specific scheme
    static constexpr bool handles(CompressionScheme scheme) noexcept {
        for (auto s : schemes) {
            if (s == scheme) return true;
        }
        return false;
    }
    
    /// Check if this decompressor supports a specific format configuration
    static bool supports_format(const TileSize& tile_size,
                               std::span<const SampleFormat> sample_formats,
                               std::span<const uint8_t> bits_per_sample,
                               std::endian endianness) noexcept {
        return DecompressorType::supports_format(tile_size, sample_formats, bits_per_sample, endianness);
    }
};

/// Concept to check if a type is a DecompressorDescriptor
template <typename T>
concept DecompressorDescriptorType = requires {
    typename T::decompressor_type;
    { T::schemes } -> std::convertible_to<std::span<const CompressionScheme>>;
    { T::handles(CompressionScheme::None) } -> std::same_as<bool>;
    { T::supports_format(TileSize{}, std::span<const SampleFormat>{}, std::span<const uint8_t>{}, std::endian::native) } 
        -> std::same_as<bool>;
    requires DecompressorImpl<typename T::decompressor_type>;
};

/// Type trait to check if a compression scheme is supported by any decompressor
/// Note: This only checks compression scheme, not format compatibility
template <CompressionScheme Scheme, typename... Decompressors>
struct has_decompressor_for : std::false_type {};

template <CompressionScheme Scheme, typename First, typename... Rest>
struct has_decompressor_for<Scheme, First, Rest...> 
    : std::conditional_t<First::handles(Scheme), 
                         std::true_type, 
                         has_decompressor_for<Scheme, Rest...>> {};

template <CompressionScheme Scheme, typename... Decompressors>
inline constexpr bool has_decompressor_for_v = has_decompressor_for<Scheme, Decompressors...>::value;

/// Compile-time decompressor specification
template <DecompressorDescriptorType... Decompressors>
struct DecompressorSpec {
    static constexpr std::size_t num_decompressors = sizeof...(Decompressors);

    /// Check if a compression scheme is supported (compile-time, scheme only)
    template <CompressionScheme Scheme>
    static constexpr bool supports() noexcept {
        return has_decompressor_for_v<Scheme, Decompressors...>;
    }
    
    /// Check if a compression scheme is supported (runtime version, scheme only)
    static constexpr bool supports(CompressionScheme scheme) noexcept {
        return (Decompressors::handles(scheme) || ...);
    }
    
    /// Check if a compression scheme and format combination is supported (runtime version)
    static bool supports(CompressionScheme scheme,
                        const TileSize& tile_size,
                        std::span<const SampleFormat> sample_formats,
                        std::span<const uint8_t> bits_per_sample,
                        std::endian endianness) noexcept {
        // Check each decompressor that handles this scheme
        bool found = false;
        auto check = [&]<typename Desc>() {
            if (Desc::handles(scheme) && 
                Desc::supports_format(tile_size, sample_formats, bits_per_sample, endianness)) {
                found = true;
            }
        };
        (check.template operator()<Decompressors>(), ...);
        return found;
    }
    
    /// Apply a function to each decompressor descriptor at compile time
    template <typename F>
    static constexpr void for_each(F&& func) {
        (func.template operator()<Decompressors>(), ...);
    }
};

/// Concept to validate DecompressorSpec structure at compile time
template <typename T>
concept ValidDecompressorSpec = requires {
    { T::num_decompressors } -> std::convertible_to<std::size_t>;
    requires T::num_decompressors > 0;

    // Check that T has the expected interface of DecompressorSpec
    { T::template supports<CompressionScheme::None>() } -> std::same_as<bool>; // compile-time version
    { T::supports(CompressionScheme::None) } -> std::same_as<bool>; // runtime version
    { T::supports(CompressionScheme::None, TileSize{}, std::span<const SampleFormat>{}, 
                  std::span<const uint8_t>{}, std::endian::native) } -> std::same_as<bool>;
};

/// Storage helper for a single decompressor
template <typename DecompressorDesc>
    requires DecompressorDescriptorType<DecompressorDesc>
class DecompressorHolder {
private:
    [[no_unique_address]] mutable typename DecompressorDesc::decompressor_type decompressor_;

public:
    constexpr DecompressorHolder() noexcept = default;
    
    [[nodiscard]] Result<std::size_t> decompress(
        std::span<std::byte> output,
        std::span<const std::byte> input,
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        std::endian endianness) const noexcept {
        return decompressor_.decompress(output, input, tile_size, sample_formats, bits_per_sample, endianness);
    }
    
    [[nodiscard]] static bool supports_format(
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        std::endian endianness) noexcept {
        return DecompressorDesc::supports_format(tile_size, sample_formats, bits_per_sample, endianness);
    }
    
    static constexpr bool handles(CompressionScheme scheme) noexcept {
        return DecompressorDesc::handles(scheme);
    }
};

/// Decompressor storage with dynamic dispatch based on scheme and format
template <typename DecompSpec>
    requires ValidDecompressorSpec<DecompSpec>
class DecompressorStorage {
private:
    // Tuple of all decompressor holders
    [[no_unique_address]] decltype([]<DecompressorDescriptorType... Descs>(DecompressorSpec<Descs...>*) {
        return std::tuple<DecompressorHolder<Descs>...>{};
    }(static_cast<DecompSpec*>(nullptr))) holders_;
    
    /// Helper to decompress using the correct decompressor at runtime
    template <std::size_t I = 0>
    [[nodiscard]] Result<std::size_t> decompress_impl(
        std::span<std::byte> output,
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
                return holder.decompress(output, input, tile_size, sample_formats, bits_per_sample, endianness);
            }
            return decompress_impl<I + 1>(output, input, scheme, tile_size, sample_formats, bits_per_sample, endianness);
        } else {
            return Err(Error::Code::UnsupportedFeature, 
                      "Compression scheme or format not supported in this build");
        }
    }

public:
    constexpr DecompressorStorage() noexcept = default;
    ~DecompressorStorage() = default;
    
    DecompressorStorage(const DecompressorStorage&) = delete;
    DecompressorStorage& operator=(const DecompressorStorage&) = delete;
    
    constexpr DecompressorStorage(DecompressorStorage&&) noexcept = default;
    constexpr DecompressorStorage& operator=(DecompressorStorage&&) noexcept = default;
    
    /// Decompress data based on compression scheme and format
    [[nodiscard]] Result<std::size_t> decompress(
        std::span<std::byte> output,
        std::span<const std::byte> input,
        CompressionScheme scheme,
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        std::endian endianness) const noexcept {
        
        return decompress_impl(output, input, scheme, tile_size, sample_formats, bits_per_sample, endianness);
    }
    
    /// Overload for vector input
    [[nodiscard]] Result<std::size_t> decompress(
        std::span<std::byte> output,
        const std::vector<std::byte>& input,
        CompressionScheme scheme,
        const TileSize& tile_size,
        std::span<const SampleFormat> sample_formats,
        std::span<const uint8_t> bits_per_sample,
        std::endian endianness) const noexcept {
        
        return decompress(output, std::span{input}, scheme, tile_size, sample_formats, bits_per_sample, endianness);
    }
    
    /// Check if a compression scheme is supported at compile time (scheme only)
    template <CompressionScheme Scheme>
    static constexpr bool supports() noexcept {
        return DecompSpec::template supports<Scheme>();
    }
    
    /// Check if a compression scheme is supported at runtime (scheme only)
    static constexpr bool supports(CompressionScheme scheme) noexcept {
        return DecompSpec::supports(scheme);
    }
    
    /// Check if a compression scheme and format combination is supported at runtime
    static bool supports(CompressionScheme scheme,
                        const TileSize& tile_size,
                        std::span<const SampleFormat> sample_formats,
                        std::span<const uint8_t> bits_per_sample,
                        std::endian endianness) noexcept {
        return DecompSpec::supports(scheme, tile_size, sample_formats, bits_per_sample, endianness);
    }
};

} // namespace tiffconcept