// ============================================================================
// Module: Null Sink - Silent discard sink for Windows
// AA Spec: Addresses Quill StreamSink /dev/null gap on Windows (FINDING 2)
//
// RATIONALE:
//   Quill v10.0.1 checks for "/dev/null" in StreamSink (line 76) but this path
//   is Unix-only. On Windows, the file gets created silently instead of discarding.
//   This NullSink provides explicit null-sink capability.
// ============================================================================

#pragma once
#include <quill/sinks/Sink.h>

namespace Logger_Adapter::sinks {

/// @class NullSink
/// @brief Sink that discards all log messages silently
/// @details AA-F01: Provides null-sink behavior for Windows where "/dev/null" doesn't exist
class NullSink : public quill::Sink {
public:
    NullSink() = default;
    ~NullSink() override = default;

    /// @brief Accept but discard all log messages
    void write_log(quill::MacroMetadata const* /*log_metadata*/,
                   uint64_t /*log_timestamp*/,
                   std::string_view /*thread_id*/,
                   std::string_view /*thread_name*/,
                   std::string const& /*process_id*/,
                   std::string_view /*logger_name*/,
                   quill::LogLevel /*log_level*/,
                   std::string_view /*log_level_description*/,
                   std::string_view /*log_level_short_code*/,
                   std::vector<std::pair<std::string, std::string>> const* /*named_args*/,
                   std::string_view /*log_message*/,
                   std::string_view /*log_statement*/) override {
        // Intentionally empty - discards all messages
    }

    /// @brief No-op flush
    void flush_sink() override {
        // No-op - nothing to flush
    }
};

} // namespace Logger_Adapter::sinks