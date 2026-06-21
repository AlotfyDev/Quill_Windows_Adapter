#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <quill/sinks/Sink.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/FileSink.h>
#include <quill/sinks/JsonSink.h>
#include <quill/Frontend.h>
#include <quill/LogLevel.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>

#include "logging/LoggingConfig.hpp"
#include "config/LoggerEntry.hpp"
#include "sinks/EventLogSink.hpp"
#include "sinks/SanitizingSink.hpp"
#include "sinks/DailyRotatingFileSink.hpp"

namespace Logger_Adapter::setup {

/// @enum ValidationResult
/// @brief Result codes for validating LoggerEntry configuration
/// @details Layer 1 (Toolbox) - Pure validation function
/// @see AA-C01-MultiLogger.md Step 1 - Validation decision tree
enum class ValidationResult {
    Pass,               ///< Entry is valid
    Fail_EmptyName,     ///< LoggerEntry::name is empty
    Fail_SinkNotFound,   ///< sink_names contains non-existent sink
    Fail_LevelOutOfRange,///< log_level out of Quill range
    Fail_NoSinks,       ///< sink_names vector is empty
    Fail_DuplicateName,  ///< Duplicate logger name (first wins)
    Fail_EmergencyInvalid///< Emergency logger config invalid (fatal)
};

/// @brief Helper to map logger name to EventLog Category ID
/// @param logger_name Name of the logger
/// @return Category ID (1 to 5)
inline WORD CategoryFromLoggerName(const std::string& logger_name) {
    if (logger_name == "Emergency") return 1;
    if (logger_name == "OrderExecution") return 2;
    if (logger_name == "Risk") return 3;
    if (logger_name == "MarketData") return 4;
    if (logger_name == "HealthProbe") return 5;
    return 1; // Default to System category
}

/// @brief Create a sink by name from LoggingConfig
/// @param name Sink name ("console", "file", "json", "stderr", "eventlog")
/// @param config Logging configuration containing sink settings
/// @param logger_name Name of the logger referencing this sink (for EventLog category mapping)
/// @return Shared pointer to sink, or nullptr if not found/disabled
/// @details Layer 1 (Toolbox) - Stateless sink creation
///          AA-C01: Uses config::sink_names references
///          AA-M13: Stderr independent sink
///          AA-P01: EventLog native sink
inline std::shared_ptr<quill::Sink> CreateSink(const std::string& name,
                                               const logging::LoggingConfig& config,
                                               const std::string& logger_name = "") {
    if (name == "console" && config.console.enabled) {
        quill::ConsoleSinkConfig console_cfg;
        console_cfg.set_stream(config.console.stream);
        console_cfg.set_colour_mode(
            config.console.colored
                ? quill::ConsoleSinkConfig::ColourMode::Automatic
                : quill::ConsoleSinkConfig::ColourMode::Never);
        return quill::Frontend::create_or_get_sink<quill::ConsoleSink>(name, console_cfg);
    }

    if (name == "stderr" && config.stderr_sink.enabled) {
        quill::ConsoleSinkConfig stderr_cfg;
        stderr_cfg.set_stream("stderr");
        stderr_cfg.set_colour_mode(
            config.stderr_sink.colored
                ? quill::ConsoleSinkConfig::ColourMode::Automatic
                : quill::ConsoleSinkConfig::ColourMode::Never);
        return quill::Frontend::create_or_get_sink<quill::ConsoleSink>(name, stderr_cfg);
    }

    if (name == "eventlog" && config.eventlog.enabled) {
        return quill::Frontend::create_or_get_sink<sinks::EventLogSink>(
            name,
            config.eventlog.source_name,
            CategoryFromLoggerName(logger_name)
        );
    }

if (name == "file" && config.file.enabled) {
        // AA-M02: Daily rotation with gzip/retention support
        if (config.file.daily.max_archive_days > 0 || config.file.daily.gzip_on_rotation) {
            return std::make_shared<sinks::DailyRotatingFileSink>(
                config.file.daily, quill::FileSinkConfig{});
        }
        // AA-M16: Quill's size-based rotation
        if (config.file.rotating) {
            quill::RotatingFileSinkConfig rotating_cfg;
            rotating_cfg.set_rotation_max_file_size(config.file.max_file_size);
            rotating_cfg.set_max_backup_files(config.file.max_files);
            return quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
                config.file.filename, rotating_cfg);
        }
        return quill::Frontend::create_or_get_sink<quill::FileSink>(config.file.filename);
     }

    if (name == "json" && config.json.enabled) {
        return quill::Frontend::create_or_get_sink<quill::JsonFileSink>(
            config.json.filename, quill::FileSinkConfig{});
    }

    return nullptr;
}

/// @brief Create all sinks for a LoggerEntry
/// @param entry Logger configuration entry
/// @param config Global logging configuration
/// @return Vector of sink shared pointers
/// @details Layer 1 (Toolbox) - Stateless sink collection
/// @see AA-C01-MultiLogger.md Step 2
inline std::vector<std::shared_ptr<quill::Sink>> CreateSinksForLogger(
    const config::LoggerEntry& entry, const logging::LoggingConfig& config) {
    std::vector<std::shared_ptr<quill::Sink>> sinks;

    for (const auto& sink_name : entry.sink_names) {
        auto sink = CreateSink(sink_name, config, entry.name);
        if (sink) {
            auto sanitized = WrapWithSanitizer(sink, config);
            if (sanitized) {
                sinks.push_back(sanitized);
            } else {
                sinks.push_back(sink);
            }
        }
    }

    return sinks;
}

/// @brief Wrap a sink with SanitizingSink if sanitization is enabled
/// @param sink The inner Quill sink to wrap (unused - we create our own SanitizingSink)
/// @param config Global logging configuration (sanitization settings)
/// @return Either a SanitizingSink, or nullptr if sanitization disabled
/// @details AA-M18-3: Zero overhead when sanitization disabled (returns nullptr)
/// @note SanitizingSink integrates via FileEventNotifier::before_write callback
inline std::shared_ptr<quill::Sink> WrapWithSanitizer(
    std::shared_ptr<quill::Sink> /*sink*/, const logging::LoggingConfig& config) {
    if (!config.sanitization.enabled) {
        return nullptr; // AA-M18-3: passthrough when disabled
    }
    return std::make_shared<sinks::SanitizingSink>(config.sanitization);
}

/// @brief Validate a LoggerEntry configuration and clamp out-of-range log levels
/// @param entry Logger configuration to validate (modified if level is clamped)
/// @param config Global logging configuration
/// @param error_out Output string for error message
/// @return ValidationResult indicating pass/failure reason
/// @details Layer 1 (Toolbox) - Performs validation and sanitization (clamping)
///          AA-C01: Implements validation decision tree
/// @see TD-3-C01-MultiLogger.md - Decision tree
inline ValidationResult ValidateLoggerEntry(config::LoggerEntry& entry,
                                             const logging::LoggingConfig& config,
                                             std::string& error_out) {
    if (entry.name.empty()) {
        error_out = "Logger name is empty";
        return ValidationResult::Fail_EmptyName;
    }

    if (entry.sink_names.empty()) {
        error_out = "sink_names is empty";
        return ValidationResult::Fail_NoSinks;
    }

    // Check if referenced sinks exist and are enabled
    for (const auto& sink_name : entry.sink_names) {
        bool found = false;
        if (sink_name == "console" && config.console.enabled) found = true;
        if (sink_name == "file" && config.file.enabled) found = true;
        if (sink_name == "json" && config.json.enabled) found = true;
        if (sink_name == "stderr" && config.stderr_sink.enabled) found = true;
        if (sink_name == "eventlog" && config.eventlog.enabled) found = true;

        if (!found) {
            error_out = "Sink '" + sink_name + "' not found or not enabled";
            return ValidationResult::Fail_SinkNotFound;
        }
    }

    if (entry.name == "Emergency" && entry.sink_names.empty()) {
        error_out = "Emergency logger must have at least one sink";
        return ValidationResult::Fail_EmergencyInvalid;
    }

    // Check and clamp log level
    int level_num = static_cast<int>(entry.log_level);
    if (level_num < 0 || level_num > 7) {
        error_out = "Log level " + std::to_string(level_num) + " out of range, clamping";
        if (level_num < 0) {
            entry.log_level = quill::LogLevel::TraceL3;
        } else {
            entry.log_level = quill::LogLevel::Critical;
        }
        return ValidationResult::Fail_LevelOutOfRange;
    }

    return ValidationResult::Pass;
}

} // namespace Logger_Adapter::setup