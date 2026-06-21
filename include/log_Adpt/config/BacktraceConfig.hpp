#pragma once
#include <cstdint>

namespace Logger_Adapter::config {

/// @struct BacktraceConfig
/// @brief Configuration for Quill ring-buffer backtrace mechanism
/// @details Layer 2 (POD) - Configuration for critical error diagnostics
///          AA-C03: Ring buffer capacity and flush-on-level settings
/// @see AA-C03-BacktraceLogging.md
struct BacktraceConfig {
    bool enabled = false;  ///< Enable backtrace for this logger

    /// Ring buffer capacity.
    /// Emergency: 5000 (full trade lifecycle)
    /// HealthProbe: 100 (heartbeats only)
    /// Trading subsystems: 1000
    /// Default: 1000
    uint32_t capacity = 1000;

    /// Messages at this level and above auto-flush the backtrace.
    /// Default: Error (6)
    uint32_t flush_on_level = 6;
};

} // namespace Logger_Adapter::config