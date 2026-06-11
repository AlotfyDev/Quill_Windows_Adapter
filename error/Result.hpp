#pragma once
#include "ErrorCode.hpp"
#include <type_traits>
#include <utility>

namespace Logger_Adapter::connector_errors {

class ResultVoid {
    ErrorCode code_;
public:
    ResultVoid() noexcept : code_(ErrorCode::Success) {}
    ResultVoid(ErrorCode code) noexcept : code_(code) {}

    bool IsOk() const noexcept { return code_ == ErrorCode::Success; }
    bool IsError() const noexcept { return code_ != ErrorCode::Success; }
    ErrorCode Error() const noexcept { return code_; }
    explicit operator bool() const noexcept { return IsOk(); }
};

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

    ErrorCode Error() const noexcept { return code_; }

    T& Value() noexcept { return value_; }
    const T& Value() const noexcept { return value_; }

    T& operator*() noexcept { return value_; }
    const T& operator*() const noexcept { return value_; }
};

} // namespace
