#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <memory>
#include <vector>
#include <concepts>
#include <cstring>
#include <array>
#include "result.hpp"
#include "types.hpp"

namespace tiff {

/// Concept for a decompressor implementation
template<typename T>
concept DecompressorImpl = requires(const T& decompressor, 
                                    std::span<std::byte> output,
                                    std::span<const std::byte> input) {
    { decompressor.decompress(output, input) } -> std::same_as<Result<std::size_t>>;
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
};

/// Concept to check if a type is a DecompressorDescriptor
template <typename T>
concept DecompressorDescriptorType = requires {
    typename T::decompressor_type;
    { T::schemes } -> std::convertible_to<std::span<const CompressionScheme>>;
    { T::handles(CompressionScheme::None) } -> std::same_as<bool>;
    requires DecompressorImpl<typename T::decompressor_type>;
};

/// Type trait to check if a compression scheme is supported by any decompressor
template <CompressionScheme Scheme, typename... Decompressors>
struct has_decompressor_for : std::false_type {};

template <CompressionScheme Scheme, typename First, typename... Rest>
struct has_decompressor_for<Scheme, First, Rest...> 
    : std::conditional_t<First::handles(Scheme), 
                         std::true_type, 
                         has_decompressor_for<Scheme, Rest...>> {};

template <CompressionScheme Scheme, typename... Decompressors>
inline constexpr bool has_decompressor_for_v = has_decompressor_for<Scheme, Decompressors...>::value;

/// Get decompressor descriptor that handles a specific scheme
template <CompressionScheme Scheme, typename... Decompressors>
struct get_decompressor_for;

template <CompressionScheme Scheme, typename First, typename... Rest>
struct get_decompressor_for<Scheme, First, Rest...> {
    using type = std::conditional_t<First::handles(Scheme), 
                                    First, 
                                    typename get_decompressor_for<Scheme, Rest...>::type>;
};

template <CompressionScheme Scheme>
struct get_decompressor_for<Scheme> {
    using type = void;
};

template <CompressionScheme Scheme, typename... Decompressors>
using get_decompressor_for_t = typename get_decompressor_for<Scheme, Decompressors...>::type;

/// Helper to check if compression schemes don't overlap across decompressors
template <typename... Decompressors>
consteval bool schemes_are_unique() {
    constexpr std::size_t total_schemes = (Decompressors::schemes.size() + ...);
    std::array<CompressionScheme, total_schemes> all_schemes{};
    
    std::size_t idx = 0;
    auto collect = [&]<typename Desc>() {
        for (auto scheme : Desc::schemes) {
            all_schemes[idx++] = scheme;
        }
    };
    (collect.template operator()<Decompressors>(), ...);
    
    // Check for duplicates
    for (std::size_t i = 0; i < all_schemes.size(); ++i) {
        for (std::size_t j = i + 1; j < all_schemes.size(); ++j) {
            if (all_schemes[i] == all_schemes[j]) {
                return false;
            }
        }
    }
    return true;
}

/// Compile-time decompressor specification
template <DecompressorDescriptorType... Decompressors>
struct DecompressorSpec {
    static constexpr std::size_t num_decompressors = sizeof...(Decompressors);

    // Compile-time check that no compression scheme is handled by multiple decompressors
    static_assert(schemes_are_unique<Decompressors...>(), 
                  "Each compression scheme can only be handled by one decompressor");

    /// Check if a compression scheme is supported
    template <CompressionScheme Scheme>
    static constexpr bool supports() noexcept {
        return has_decompressor_for_v<Scheme, Decompressors...>;
    }
    
    /// Check if a compression scheme is supported (runtime version)
    static constexpr bool supports(CompressionScheme scheme) noexcept {
        return (Decompressors::handles(scheme) || ...);
    }
    
    /// Get decompressor descriptor for a specific scheme
    template <CompressionScheme Scheme>
    using get_decompressor = get_decompressor_for_t<Scheme, Decompressors...>;
    
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
    
    requires []<DecompressorDescriptorType... Decompressors>(DecompressorSpec<Decompressors...>*) {
        return std::is_same_v<std::remove_cvref_t<T>, DecompressorSpec<Decompressors...>>;
    }(static_cast<std::remove_cvref_t<T>*>(nullptr));

    requires T::num_decompressors > 0;
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
        std::span<const std::byte> input) const noexcept {
        return decompressor_.decompress(output, input);
    }
    
    static constexpr bool handles(CompressionScheme scheme) noexcept {
        return DecompressorDesc::handles(scheme);
    }
};

/// Decompressor storage with dynamic dispatch based on scheme
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
        CompressionScheme scheme) const noexcept {
        
        if constexpr (I < std::tuple_size_v<decltype(holders_)>) {
            const auto& holder = std::get<I>(holders_);
            if (holder.handles(scheme)) {
                return holder.decompress(output, input);
            }
            return decompress_impl<I + 1>(output, input, scheme);
        } else {
            return Err(Error::Code::UnsupportedFeature, 
                      "Compression scheme not supported in this build");
        }
    }

public:
    constexpr DecompressorStorage() noexcept = default;
    ~DecompressorStorage() = default;
    
    DecompressorStorage(const DecompressorStorage&) = delete;
    DecompressorStorage& operator=(const DecompressorStorage&) = delete;
    
    constexpr DecompressorStorage(DecompressorStorage&&) noexcept = default;
    constexpr DecompressorStorage& operator=(DecompressorStorage&&) noexcept = default;
    
    /// Decompress data based on compression scheme
    [[nodiscard]] Result<std::size_t> decompress(
        std::span<std::byte> output,
        std::span<const std::byte> input,
        CompressionScheme scheme) const noexcept {
        
        return decompress_impl(output, input, scheme);
    }
    
    /// Overload for vector input
    [[nodiscard]] Result<std::size_t> decompress(
        std::span<std::byte> output,
        const std::vector<std::byte>& input,
        CompressionScheme scheme) const noexcept {
        
        return decompress(output, std::span{input}, scheme);
    }
    
    /// Check if a compression scheme is supported at compile time
    template <CompressionScheme Scheme>
    static constexpr bool supports() noexcept {
        return DecompSpec::template supports<Scheme>();
    }
    
    /// Check if a compression scheme is supported at runtime
    static constexpr bool supports(CompressionScheme scheme) noexcept {
        return DecompSpec::supports(scheme);
    }
};

} // namespace tiff