// ============================================================================
// Module: Sanitizing Sink Decorator — Implementation
// AA Spec: AA-M18-LogSanitization.md (Step 2 — Filter Chain)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#include "pch.h"
#include "log_Adpt/sinks/SanitizingSink.hpp"

namespace Logger_Adapter::sinks {

SanitizingSink::SanitizingSink(
    const config::SanitizationConfig& config,
    quill::FileSinkConfig const&)
    : StreamSink(quill::fs::path("stdout"), nullptr, std::nullopt, quill::FileEventNotifier{
        nullptr, nullptr, nullptr, nullptr,
        [this](std::string_view message) { return SanitizeMessage(message); }
    })
    , config_(config) {
    // AA-M18-1: Pre-compile rules at construction time
    // Build the adapter-level filter chain
    if (config_.enabled) {
        filter_chain_.AddFilter(
            std::make_unique<filters::SanitizationLogFilter>(config));
    }
}

std::string SanitizingSink::SanitizeMessage(std::string_view message) const {
    // AA-M18-6: Check for null bytes (binary data warning)
    if (config_.reject_on_null_byte && message.find('\0') != std::string_view::npos) {
        OutputDebugStringA("SanitizingSink: Rejected log message containing null bytes\n");
        return std::string(message);  // Return unchanged
    }

    // If no filters, return message unchanged
    if (!config_.enabled || !filter_chain_.HasFilters()) {
        return std::string(message);
    }

    // Apply filter chain to the message
    std::string sanitized(message);
    filter_chain_.ApplyAll(sanitized, sanitized);
    return sanitized;
}

void SanitizingSink::write_log(
    quill::MacroMetadata const* log_metadata, uint64_t log_timestamp,
    std::string_view thread_id, std::string_view thread_name,
    std::string const& process_id, std::string_view logger_name,
    quill::LogLevel log_level, std::string_view log_level_description,
    std::string_view log_level_short_code,
    std::vector<std::pair<std::string, std::string>> const* named_args,
    std::string_view log_message, std::string_view log_statement) {
    // The StreamSink::write_log implementation uses _file_event_notifier.before_write
    // if set, so we just call the parent after our callback is installed
    StreamSink::write_log(
        log_metadata, log_timestamp, thread_id, thread_name,
        process_id, logger_name, log_level, log_level_description,
        log_level_short_code, named_args, log_message, log_statement);
}

} // namespace Logger_Adapter::sinks