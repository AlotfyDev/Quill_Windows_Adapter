// ============================================================================
// Module: Sanitizing Sink Decorator (wraps Quill sinks)
// AA Spec: AA-M18-LogSanitization.md (Step 2 — Filter Chain)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#pragma once
#include <quill/sinks/StreamSink.h>
#include <quill/core/PatternFormatterOptions.h>
#include <memory>
#include <string>

#include "log_Adpt/config/SanitizationConfig.hpp"
#include "log_Adpt/filters/LogFilter.hpp"
#include "log_Adpt/filters/SanitizationLogFilter.hpp"

namespace Logger_Adapter::sinks {

/// @class SanitizingSink
/// @brief Quill sink decorator that sanitizes log messages before forwarding
/// @details AA-M18-4: Uses FileEventNotifier::before_write callback to sanitize on backend thread
///          AA-M18-5: No mutex in write path - rules immutable after construction
class SanitizingSink : public quill::StreamSink {
public:
    /// @brief Constructor
    /// @param config Sanitization configuration
    /// @param inner_config Quill FileSink config for inner stream
    /// @pre config must be valid
    /// @post SanitizationFilter built and ready for message processing
    explicit SanitizingSink(
        const config::SanitizationConfig& config,
        quill::FileSinkConfig const& inner_config = {});

    /// @brief Destructor
    ~SanitizingSink() override = default;

    /// @brief Override write_log to intercept and sanitize
    QUILL_ATTRIBUTE_HOT void write_log(
        quill::MacroMetadata const* log_metadata, uint64_t log_timestamp,
        std::string_view thread_id, std::string_view thread_name,
        std::string const& process_id, std::string_view logger_name,
        quill::LogLevel log_level, std::string_view log_level_description,
        std::string_view log_level_short_code,
        std::vector<std::pair<std::string, std::string>> const* named_args,
        std::string_view log_message, std::string_view log_statement) override;

private:
    /// @brief Sanitize a message using filter chain
    std::string SanitizeMessage(std::string_view message) const;

    filters::FilterChain filter_chain_;
    config::SanitizationConfig config_;
};

} // namespace Logger_Adapter::sinks