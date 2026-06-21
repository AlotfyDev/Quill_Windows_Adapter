#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <quill/core/LogLevel.h>

namespace Logger_Adapter::config {

/// @struct LoggerEntry
/// @brief Configuration for a named logger instance
/// @details Layer 2 (POD) - Configuration DTO for Multi-Logger Shim
///          AA-C01: sink_names references named sinks in config
/// @see AA-C01-MultiLogger.md, TD-3-C01-MultiLogger.md
struct LoggerEntry {
    /// @brief Logger name - empty name causes Fail_EmptyName
    std::string name = "default";

    /// @brief Log level threshold (Quill LogLevel directly)
    quill::LogLevel log_level = quill::LogLevel::Debug;

    /// @brief Sink name references - maps to sink names in LoggingConfig
    /// AA: "console", "file", "json" - must match enabled sinks
    std::vector<std::string> sink_names = {"console"};

    /// @brief Enable backtrace ring buffer for this logger
    bool backtrace_enabled = false;

    /// @brief Backtrace ring buffer capacity
    uint32_t backtrace_capacity = 1000;

    /// @brief Auto-flush backtrace on this level or above
    quill::LogLevel backtrace_flush_level = quill::LogLevel::Error;
};

} // namespace Logger_Adapter::config