#pragma once
#include "ErrorCode.hpp"
#include <cstdint>

namespace Logger_Adapter::connector_errors {

struct ErrorContext {
    ErrorCode code;
    const char* file;
    uint32_t line;
    const char* function;
    const char* message;
    uint64_t timestamp_ms;

    explicit ErrorContext(ErrorCode c = ErrorCode::Unknown) noexcept
        : code(c), file(nullptr), line(0), function(nullptr), message(nullptr), timestamp_ms(0) {}

    ErrorContext(ErrorCode c, const char* f, uint32_t l, const char* fn, const char* msg) noexcept
        : code(c), file(f), line(l), function(fn), message(msg), timestamp_ms(0) {}

    void SetTimestamp(uint64_t ts) noexcept { timestamp_ms = ts; }

    bool IsSuccess() const noexcept { return code == ErrorCode::Success; }
    bool IsError() const noexcept { return code != ErrorCode::Success; }
};

} // namespace
