#pragma once
#include "ErrorCode.hpp"
#include <type_traits>
#include <utility>
#include <cassert>
#include <exception>

namespace Logger_Adapter::connector_errors {

/// @class ResultVoid
/// @brief Represents a result of an operation returning void, with optional error code
/// @details AA-M09: Composable error handling
class ResultVoid {
    ErrorCode code_;
public:
    ResultVoid() noexcept : code_(ErrorCode::Success) {}
    ResultVoid(ErrorCode code) noexcept : code_(code) {}

    bool IsOk() const noexcept { return code_ == ErrorCode::Success; }
    bool IsError() const noexcept { return code_ != ErrorCode::Success; }
    ErrorCode Error() const noexcept { return code_; }
    explicit operator bool() const noexcept { return IsOk(); }

    /// @brief Returns Success on ok, default_code on error.
    ErrorCode ValueOr(ErrorCode default_code) const noexcept {
        return IsOk() ? ErrorCode::Success : default_code;
    }
};

/// @class Result<T>
/// @brief Monadic-style outcome type holding either a value T or an ErrorCode
/// @details AA-M09: Monadic methods (ValueOr, AndThen, Map)
///          AA-M10: Guarded access policies on hot vs debug paths
template<typename T>
class Result {
    static_assert(!std::is_reference_v<T>, "Result does not support reference types");

    union {
        T value_;
        ErrorCode code_;
    };
    bool has_value_;

public:
    template<typename... Args>
    explicit Result(Args&&... args)
        : value_(std::forward<Args>(args)...), has_value_(true) {}

    Result(ErrorCode code) : code_(code), has_value_(false) {}

    ~Result() {
        if (has_value_) value_.~T();
    }

    Result(const Result& other) : has_value_(other.has_value_) {
        if (has_value_) new (&value_) T(other.value_);
        else code_ = other.code_;
    }
    Result& operator=(const Result& other) {
        if (this != &other) {
            if (has_value_) value_.~T();
            has_value_ = other.has_value_;
            if (has_value_) new (&value_) T(other.value_);
            else code_ = other.code_;
        }
        return *this;
    }

    Result(Result&& other) noexcept : has_value_(other.has_value_) {
        if (has_value_) new (&value_) T(std::move(other.value_));
        else code_ = other.code_;
    }
    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            if (has_value_) value_.~T();
            has_value_ = other.has_value_;
            if (has_value_) new (&value_) T(std::move(other.value_));
            else code_ = other.code_;
        }
        return *this;
    }

    bool IsOk() const noexcept { return has_value_; }
    bool IsError() const noexcept { return !has_value_; }
    explicit operator bool() const noexcept { return has_value_; }

    /// @brief Error code lookup. Safe to call anytime as union active member.
    ErrorCode Error() const noexcept { return code_; }

    /// @brief Guarded access to value. Triggers assert in Debug, terminates in Release.
    T& Value() & noexcept {
        if (!has_value_) {
#if !defined(NDEBUG)
            assert(has_value_ && "Result::Value() called on error — check IsOk() first");
#else
            std::terminate();
#if defined(_MSC_VER)
            __assume(false);
#endif
#endif
        }
        return value_;
    }

    /// @brief Guarded access to const value. Triggers assert in Debug, terminates in Release.
    const T& Value() const& noexcept {
        if (!has_value_) {
#if !defined(NDEBUG)
            assert(has_value_ && "Result::Value() called on error — check IsOk() first");
#else
            std::terminate();
#if defined(_MSC_VER)
            __assume(false);
#endif
#endif
        }
        return value_;
    }

    /// @brief Rvalue value retrieval.
    T Value() && noexcept {
        if (!has_value_) {
#if !defined(NDEBUG)
            assert(has_value_ && "Result::Value() called on error — check IsOk() first");
#else
            std::terminate();
#if defined(_MSC_VER)
            __assume(false);
#endif
#endif
        }
        return std::move(value_);
    }

    /// @brief Unsafe (unchecked) value retrieval for hot paths.
    T& ValueUnsafe() & noexcept {
        return value_;
    }

    const T& ValueUnsafe() const& noexcept {
        return value_;
    }

    T&& ValueUnsafe() && noexcept {
        return std::move(value_);
    }

    T& operator*() noexcept { return value_; }
    const T& operator*() const noexcept { return value_; }

    /// @brief Returns the value if successful, or static-casted default value on error.
    template<typename U, std::enable_if_t<std::is_convertible_v<U&&, T>, int> = 0>
    T ValueOr(U&& default_value) const& noexcept {
        return has_value_ ? value_ : static_cast<T>(std::forward<U>(default_value));
    }

    template<typename U, std::enable_if_t<std::is_convertible_v<U&&, T>, int> = 0>
    T ValueOr(U&& default_value) && noexcept {
        return has_value_ ? std::move(value_) : static_cast<T>(std::forward<U>(default_value));
    }

    /// @brief Chains: Ok(x) -> f(x) returns Result<U>, Error(e) -> Error(e)
    template<typename F>
    auto AndThen(F&& f) & -> decltype(f(value_)) {
        if (has_value_) {
            return f(value_);
        }
        return decltype(f(value_))(code_);
    }

    template<typename F>
    auto AndThen(F&& f) const& -> decltype(f(value_)) {
        if (has_value_) {
            return f(value_);
        }
        return decltype(f(value_))(code_);
    }

    template<typename F>
    auto AndThen(F&& f) && -> decltype(f(std::move(value_))) {
        if (has_value_) {
            return f(std::move(value_));
        }
        return decltype(f(std::move(value_)))(code_);
    }

    /// @brief Transforms: Ok(x) -> Ok(f(x)), Error(e) -> Error(e)
    template<typename F>
    auto Map(F&& f) & -> Result<decltype(f(value_))> {
        using U = decltype(f(value_));
        if (has_value_) {
            return Result<U>(f(value_));
        }
        return Result<U>(code_);
    }

    template<typename F>
    auto Map(F&& f) const& -> Result<decltype(f(value_))> {
        using U = decltype(f(value_));
        if (has_value_) {
            return Result<U>(f(value_));
        }
        return Result<U>(code_);
    }

    template<typename F>
    auto Map(F&& f) && -> Result<decltype(f(std::move(value_)))> {
        using U = decltype(f(std::move(value_)));
        if (has_value_) {
            return Result<U>(f(std::move(value_)));
        }
        return Result<U>(code_);
    }
};

} // namespace Logger_Adapter::connector_errors
