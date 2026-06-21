#pragma once
#include <quill/LogMacros.h>
#include <quill/Logger.h>

/// @file Backtrace.hpp
/// @brief Backtrace logging macros wrapping Quill's ring-buffer backtrace
/// @details Layer 1 (Toolbox) - Macros for critical error diagnostics
/// @see AA-C03-BacktraceLogging.md

/// @brief Log a message to the backtrace ring buffer (not visible until flush)
/// @param logger Logger with backtrace initialized
/// @param fmt Format string
/// @param ... Format arguments
/// @details AA-C03: SAFETY: init_backtrace() must have been called on the logger before use
///          Without init_backtrace(), behavior is undefined (Quill may no-op or assert)
///          Always pair with LoggerEntry::backtrace_enabled = true
#define LOG_BACKTRACE(logger, fmt, ...) \
    QUILL_LOG_BACKTRACE(logger, fmt, ##__VA_ARGS__)

/// @brief Explicitly flush backtrace to output
/// @param logger Logger with backtrace to flush
/// @details AA-C03: Safe to call even if backtrace was not initialized (no-op)
///          After flushing, the ring buffer is cleared by Quill; subsequent calls produce no output
///          until new LOG_BACKTRACE calls are made
#define FLUSH_BACKTRACE(logger) \
    logger->flush_backtrace()