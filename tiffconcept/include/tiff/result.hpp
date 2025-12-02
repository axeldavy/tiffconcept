#pragma once

#include <concepts>
#include <string>
#include <utility>
#include <variant>

namespace tiff {

/// Error type for TIFF operations
struct Error {
    enum class Code {
        Success,
        FileNotFound,
        ReadError,
        WriteError,
        InvalidHeader,
        InvalidFormat,
        InvalidTag,
        UnsupportedFeature,
        OutOfBounds,
        MemoryError,
        UnexpectedEndOfFile,
        InvalidTagType,
        InvalidPageIndex,
        CompressionError,
        IOError  // Alias for ReadError compatibility
    };

    Code code;
    std::string message;

    constexpr Error(Code c, std::string msg = "") noexcept
        : code(c), message(std::move(msg)) {}

    [[nodiscard]] constexpr bool is_success() const noexcept {
        return code == Code::Success;
    }

    [[nodiscard]] constexpr bool is_error() const noexcept {
        return code != Code::Success;
    }
};

/// Result type for operations that may fail without exceptions
template <typename T>
class [[nodiscard]] Result {
private:
    std::variant<T, Error> data_;

public:
    // Constructors
    constexpr Result(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : data_(std::forward<T>(value)) {}

    constexpr Result(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
        : data_(value) {}

    constexpr Result(Error&& error) noexcept
        : data_(std::forward<Error>(error)) {}

    constexpr Result(const Error& error) noexcept
        : data_(error) {}

    // Status checks
    [[nodiscard]] constexpr bool is_ok() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    [[nodiscard]] constexpr bool is_error() const noexcept {
        return std::holds_alternative<Error>(data_);
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_ok();
    }

    // Value access
    [[nodiscard]] constexpr T& value() & noexcept {
        return std::get<T>(data_);
    }

    [[nodiscard]] constexpr const T& value() const& noexcept {
        return std::get<T>(data_);
    }

    [[nodiscard]] constexpr T&& value() && noexcept {
        return std::get<T>(std::move(data_));
    }

    [[nodiscard]] constexpr const T&& value() const&& noexcept {
        return std::get<T>(std::move(data_));
    }

    // Error access
    [[nodiscard]] constexpr const Error& error() const noexcept {
        return std::get<Error>(data_);
    }

    // Value extraction with default
    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& default_value) const& {
        if (is_ok()) {
            return value();
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& default_value) && {
        if (is_ok()) {
            return std::move(value());
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    // Monadic operations
    template <typename F>
    [[nodiscard]] constexpr auto and_then(F&& func) & -> decltype(func(std::declval<T&>())) {
        if (is_ok()) {
            return func(value());
        }
        using RetType = decltype(func(std::declval<T&>()));
        return RetType{error()};
    }

    template <typename F>
    [[nodiscard]] constexpr auto and_then(F&& func) const& -> decltype(func(std::declval<const T&>())) {
        if (is_ok()) {
            return func(value());
        }
        using RetType = decltype(func(std::declval<const T&>()));
        return RetType{error()};
    }

    template <typename F>
    [[nodiscard]] constexpr auto and_then(F&& func) && -> decltype(func(std::declval<T&&>())) {
        if (is_ok()) {
            return func(std::move(value()));
        }
        using RetType = decltype(func(std::declval<T&&>()));
        return RetType{error()};
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform(F&& func) & {
        using U = decltype(func(std::declval<T&>()));
        if (is_ok()) {
            return Result<U>{func(value())};
        }
        return Result<U>{error()};
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform(F&& func) const& {
        using U = decltype(func(std::declval<const T&>()));
        if (is_ok()) {
            return Result<U>{func(value())};
        }
        return Result<U>{error()};
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform(F&& func) && {
        using U = decltype(func(std::declval<T&&>()));
        if (is_ok()) {
            return Result<U>{func(std::move(value()))};
        }
        return Result<U>{error()};
    }

    template <typename F>
    [[nodiscard]] constexpr Result or_else(F&& func) const& {
        if (is_error()) {
            return func(error());
        }
        return *this;
    }

    template <typename F>
    [[nodiscard]] constexpr Result or_else(F&& func) && {
        if (is_error()) {
            return func(error());
        }
        return std::move(*this);
    }
};

// Specialization for void
template <>
class [[nodiscard]] Result<void> {
private:
    std::variant<std::monostate, Error> data_;

public:
    constexpr Result() noexcept : data_(std::monostate{}) {}

    constexpr Result(Error&& error) noexcept
        : data_(std::forward<Error>(error)) {}

    constexpr Result(const Error& error) noexcept
        : data_(error) {}

    [[nodiscard]] constexpr bool is_ok() const noexcept {
        return std::holds_alternative<std::monostate>(data_);
    }

    [[nodiscard]] constexpr bool is_error() const noexcept {
        return std::holds_alternative<Error>(data_);
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_ok();
    }

    [[nodiscard]] constexpr const Error& error() const noexcept {
        return std::get<Error>(data_);
    }
};

// Helper functions for creating results
template <typename T>
[[nodiscard]] constexpr Result<std::decay_t<T>> Ok(T&& value) {
    return Result<std::decay_t<T>>{std::forward<T>(value)};
}

[[nodiscard]] constexpr Result<void> Ok() {
    return Result<void>{};
}

[[nodiscard]] constexpr Error Err(Error::Code code, std::string message = "") {
    return Error{code, std::move(message)};
}

/// Check if a value is present (works for both optional and direct values)
/// For Result<T>, checks has_value()
/// For direct values, always returns true
template <typename T>
constexpr bool is_value_present(const T& value) noexcept {
    if constexpr (requires { value.is_ok(); }) {
        // It's an optional-like type
        return value.is_ok();
    } else {
        // It's a direct value (always present)
        return true;
    }
}

/// Extract the underlying value from either optional or direct storage
/// For Result<T>, returns T&&
/// For direct values, returns T&&
template <typename T>
constexpr decltype(auto) unwrap_value(T&& value) noexcept {
    if constexpr (requires { std::forward<T>(value).value(); }) {
        // It's an optional-like type
        return std::forward<T>(value).value();
    } else {
        // It's a direct value
        return std::forward<T>(value);
    }
}

} // namespace tiff
