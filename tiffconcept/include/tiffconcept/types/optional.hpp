#pragma once

#include <optional>
#include <string>
#include <type_traits>

namespace tiffconcept {

namespace optional {
    /// @brief Type trait to check if a type is std::optional
    template<typename T>
    struct is_optional : std::false_type {};

    template<typename T>
    struct is_optional<std::optional<T>> : std::true_type {};

    template<typename T>
    inline constexpr bool is_optional_v = is_optional<T>::value;

    /// @brief Concept for container types (excluding strings)
    template <typename T>
    concept IsContainer = requires(const T& t) {
        t.size();
        t[0];
    } && !std::is_same_v<std::decay_t<T>, std::string>;

    /// @brief Check if a value is present (for optional types)
    /// @tparam T Value type (may be std::optional<U>)
    /// @param value The value to check
    /// @return True if value is present (always true for non-optional types)
    template <typename T>
    [[nodiscard]] inline constexpr bool is_value_present(const T& value) noexcept {
        if constexpr (is_optional_v<T>) {
            return value.has_value();
        } else {
            return true;
        }
    }

    /// @brief Unwrap a value (for optional types)
    /// @tparam T Value type (may be std::optional<U>)
    /// @param value The value to unwrap
    /// @return Reference to the unwrapped value
    /// @note For optional types, returns value.value(); for others, returns value directly
    template <typename T>
    [[nodiscard]] inline constexpr auto& unwrap_value(const T& value) noexcept {
        if constexpr (is_optional_v<T>) {
            return value.value();  // Return the whole value
        } else {
            return value;
        }
    }

    /// @brief Unwrap a value or return a default (for optional types)
    /// @tparam T Value type (may be std::optional<U>)
    /// @tparam DefaultType Type of default value
    /// @param value The value to unwrap
    /// @param default_value Default to return if value is not present
    /// @return Reference to unwrapped value or default
    template <typename T, typename DefaultType>
    [[nodiscard]] inline constexpr auto& unwrap_value_or(const T& value, DefaultType&& default_value) noexcept {
        if constexpr (is_optional_v<T>) {
            if (value.has_value()) {
                return value.value();
            } else {
                return std::forward<DefaultType>(default_value);
            }
        } else {
            return value;
        }
    }

} // namespace optional

} // namespace tiffconcept