#pragma once
#include "ErrorContext.hpp"
#include <quill/LogMacros.h>
#include <quill/Logger.h>

#define MAKE_ERROR(code) ::Logger_Adapter::connector_errors::ErrorContext(code)

#define MAKE_ERROR_AT(code, msg) \
    ::Logger_Adapter::connector_errors::ErrorContext(code, __FILE__, __LINE__, __func__, msg)

#define RETURN_IF_ERROR(expr) \
    do { \
        auto _r_ = (expr); \
        if (_r_.IsError()) return _r_.Error(); \
    } while(0)

#define LOG_AND_RETURN_ERROR(logger, expr) \
    do { \
        auto _r_ = (expr); \
        if (_r_.IsError()) { \
            LOG_ERROR(logger, "{} failed: {}", #expr, \
                      ::Logger_Adapter::connector_errors::ErrorCodeToString(_r_.Error())); \
            return _r_.Error(); \
        } \
    } while(0)

#define RETURN_IF_NULL(ptr, error_code) \
    do { \
        if (!(ptr)) return (error_code); \
    } while(0)

#define RETURN_IF_FALSE(cond, error_code) \
    do { \
        if (!(cond)) return (error_code); \
    } while(0)
