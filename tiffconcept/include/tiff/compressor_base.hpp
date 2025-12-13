#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>
#include "result.hpp"
#include "types.hpp"

namespace tiff {

/// Concept for a compressor implementation
template<typename T>
concept CompressorImpl = requires(const T& compressor, 
                                  std::vector<std::byte>& output,
                                  std::size_t offset,
                                  std::span<const std::byte> input) {
    { compressor.compress(output, offset, input) } -> std::same_as<Result<std::size_t>>;
    { compressor.clone() } -> std::same_as<T>;
    { T::get_default_scheme() } -> std::same_as<CompressionScheme>;
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
};

/// Concept to check if a type is a CompressorDescriptor
template <typename T>
concept CompressorDescriptorType = requires {
    typename T::compressor_type;
    { T::schemes } -> std::convertible_to<std::span<const CompressionScheme>>;
    { T::handles(CompressionScheme::None) } -> std::same_as<bool>;
    requires CompressorImpl<typename T::compressor_type>;
};

/// Type trait to check if a compression scheme is supported by any compressor
template <CompressionScheme Scheme, typename... Compressors>
struct has_compressor_for : std::false_type {};

template <CompressionScheme Scheme, typename First, typename... Rest>
struct has_compressor_for<Scheme, First, Rest...> 
    : std::conditional_t<First::handles(Scheme), 
                         std::true_type, 
                         has_compressor_for<Scheme, Rest...>> {};

template <CompressionScheme Scheme, typename... Compressors>
inline constexpr bool has_compressor_for_v = has_compressor_for<Scheme, Compressors...>::value;

/// Get compressor descriptor that handles a specific scheme
template <CompressionScheme Scheme, typename... Compressors>
struct get_compressor_for;

template <CompressionScheme Scheme, typename First, typename... Rest>
struct get_compressor_for<Scheme, First, Rest...> {
    using type = std::conditional_t<First::handles(Scheme), 
                                    First, 
                                    typename get_compressor_for<Scheme, Rest...>::type>;
};

template <CompressionScheme Scheme>
struct get_compressor_for<Scheme> {
    using type = void;
};

template <CompressionScheme Scheme, typename... Compressors>
using get_compressor_for_t = typename get_compressor_for<Scheme, Compressors...>::type;

/// Helper to check if compression schemes don't overlap across compressors
template <typename... Compressors>
inline consteval bool compressor_schemes_are_unique() {
    constexpr std::size_t total_schemes = (Compressors::schemes.size() + ...);
    std::array<CompressionScheme, total_schemes> all_schemes{};
    
    std::size_t idx = 0;
    auto collect = [&]<typename Desc>() {
        for (auto scheme : Desc::schemes) {
            all_schemes[idx++] = scheme;
        }
    };
    (collect.template operator()<Compressors>(), ...);
    
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

/// Compile-time compressor specification
template <CompressorDescriptorType... Compressors>
struct CompressorSpec {
    static constexpr std::size_t num_compressors = sizeof...(Compressors);

    // Compile-time check that no compression scheme is handled by multiple compressors
    static_assert(compressor_schemes_are_unique<Compressors...>(), 
                  "Each compression scheme can only be handled by one compressor");

    /// Check if a compression scheme is supported
    template <CompressionScheme Scheme>
    static constexpr bool supports() noexcept {
        return has_compressor_for_v<Scheme, Compressors...>;
    }
    
    /// Check if a compression scheme is supported (runtime version)
    static constexpr bool supports(CompressionScheme scheme) noexcept {
        return (Compressors::handles(scheme) || ...);
    }
    
    /// Get compressor descriptor for a specific scheme
    template <CompressionScheme Scheme>
    using get_compressor = get_compressor_for_t<Scheme, Compressors...>;
    
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
    
    requires []<CompressorDescriptorType... Compressors>(CompressorSpec<Compressors...>*) {
        return std::is_same_v<std::remove_cvref_t<T>, CompressorSpec<Compressors...>>;
    }(static_cast<std::remove_cvref_t<T>*>(nullptr));

    requires T::num_compressors > 0;
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
        std::span<const std::byte> input) const noexcept {
        return compressor_.compress(output, offset, input);
    }
    
    /// Clone the compressor for multi-threading
    [[nodiscard]] CompressorHolder clone() const noexcept {
        CompressorHolder holder;
        holder.compressor_ = compressor_.clone();
        return holder;
    }
    
    /// Get the default compression scheme
    [[nodiscard]] static constexpr CompressionScheme get_default_scheme() noexcept {
        return typename CompressorDesc::compressor_type::get_default_scheme();
    }
    
    static constexpr bool handles(CompressionScheme scheme) noexcept {
        return CompressorDesc::handles(scheme);
    }
};

/// Compressor storage with dynamic dispatch based on scheme
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
        CompressionScheme scheme) const noexcept {
        
        if constexpr (I < std::tuple_size_v<decltype(holders_)>) {
            const auto& holder = std::get<I>(holders_);
            if (holder.handles(scheme)) {
                return holder.compress(output, offset, input);
            }
            return compress_impl<I + 1>(output, offset, input, scheme);
        } else {
            return Err(Error::Code::UnsupportedFeature, 
                      "Compression scheme not supported in this build");
        }
    }

public:
    constexpr CompressorStorage() noexcept = default;
    ~CompressorStorage() = default;
    
    CompressorStorage(const CompressorStorage&) = delete;
    CompressorStorage& operator=(const CompressorStorage&) = delete;
    
    constexpr CompressorStorage(CompressorStorage&&) noexcept = default;
    constexpr CompressorStorage& operator=(CompressorStorage&&) noexcept = default;
    
    /// Compress data based on compression scheme
    /// @param output The output vector - will be resized if needed
    /// @param offset Starting position in the output vector
    /// @param input Input data to compress
    /// @param scheme Compression scheme to use
    /// @return Number of bytes written (advance from offset)
    [[nodiscard]] Result<std::size_t> compress(
        std::vector<std::byte>& output,
        std::size_t offset,
        std::span<const std::byte> input,
        CompressionScheme scheme) const noexcept {
        
        return compress_impl(output, offset, input, scheme);
    }
    
    /// Check if a compression scheme is supported at compile time
    template <CompressionScheme Scheme>
    static constexpr bool supports() noexcept {
        return CompSpec::template supports<Scheme>();
    }
    
    /// Check if a compression scheme is supported at runtime
    static constexpr bool supports(CompressionScheme scheme) noexcept {
        return CompSpec::supports(scheme);
    }
    
    /// Clone this storage for multi-threading
    /// Each compressor will be cloned with fresh contexts
    [[nodiscard]] CompressorStorage clone() const noexcept {
        CompressorStorage storage;
        storage.clone_holders<0>(*this);
        return storage;
    }
    
private:
    /// Helper to clone all holders recursively
    template <std::size_t I>
    void clone_holders(const CompressorStorage& other) noexcept {
        if constexpr (I < std::tuple_size_v<decltype(holders_)>) {
            std::get<I>(holders_) = std::get<I>(other.holders_).clone();
            clone_holders<I + 1>(other);
        }
    }
};

} // namespace tiff
