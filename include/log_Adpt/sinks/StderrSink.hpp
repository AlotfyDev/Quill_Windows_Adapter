// ============================================================================
// Module: Stderr Sink Configuration
// AA Spec: AA-M13-StderrSink.md (stderr as independent sink)
// ============================================================================

#pragma once

namespace Logger_Adapter::sinks {

/// @struct StderrSinkConfig
/// @brief Configuration for the Stderr logging sink
/// @details AA-M13: Independent configuration block for stderr logs
struct StderrSinkConfig {
    bool enabled = false;  ///< Whether output to stderr is enabled
    bool colored = true;   ///< Whether stderr output should use colors
};

} // namespace Logger_Adapter::sinks
