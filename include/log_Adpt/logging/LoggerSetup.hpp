#pragma once

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "LoggingConfig.hpp"
#include "setup/LoggerRegistry.hpp"
#include "setup/SinkFactory.hpp"
#include "config/PatternValidator.hpp"
#include "windows/ThreadAffinity.hpp"
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/FileSink.h>
#include <quill/sinks/RotatingFileSink.h>
#include <quill/sinks/JsonSink.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <stdexcept>

namespace Logger_Adapter::logging {

namespace detail {
/// @brief Access the root logger singleton
inline quill::Logger*& root_logger_ptr() {
    static quill::Logger* root = nullptr;
    return root;
}

/// @brief Access the initialization flag
inline std::atomic<bool>& initialized() {
    static std::atomic<bool> flag{false};
    return flag;
}

/// @brief Access the shutdown flag for thread safety
inline std::atomic<bool>& shutting_down() {
    static std::atomic<bool> flag{false};
    return flag;
}

/// @brief Access the was_shutdown flag to block re-init (AA-C05)
inline std::atomic<bool>& was_shutdown() {
    static std::atomic<bool> flag{false};
    return flag;
}
} // namespace detail

/// @brief Convert integer level to Quill LogLevel enum
inline quill::LogLevel ToQuillLogLevel(int level) {
    if (level < 0) return quill::LogLevel::TraceL3;
    if (level > 7) return quill::LogLevel::Critical;
    switch (level) {
        case 0: return quill::LogLevel::TraceL3;
        case 1: return quill::LogLevel::TraceL2;
        case 2: return quill::LogLevel::TraceL1;
        case 3: return quill::LogLevel::Debug;
        case 4: return quill::LogLevel::Info;
        case 5: return quill::LogLevel::Warning;
        case 6: return quill::LogLevel::Error;
        case 7: return quill::LogLevel::Critical;
        default: return quill::LogLevel::Info;
    }
}

/// @brief Helper to create PatternFormatterOptions from PatternConfig with validation
/// @details AA-M06 Step 4: Validate format pattern before constructing options
inline quill::PatternFormatterOptions MakePatternFormatter(const config::PatternConfig& cfg) {
    quill::PatternFormatterOptions opts;

    if (!cfg.format.empty()) {
        auto validation = config::ValidatePattern(cfg.format);
        if (!validation.valid) {
            throw std::invalid_argument(
                "Logger_Adapter::MakePatternFormatter: " + validation.error_message);
        }
        opts.format_pattern = cfg.format;
    }

    if (!cfg.timestamp_format.empty()) {
        opts.timestamp_pattern = cfg.timestamp_format;
    }

    opts.timestamp_timezone = cfg.utc ? quill::Timezone::GmtTime : quill::Timezone::LocalTime;
    return opts;
}

/// @brief Set log level on a named logger
/// @param name Logger name
/// @param level Target log level
/// @return true if level was set (logger exists and level valid), false otherwise
/// @details AA-C04: Thread-safe; valid levels are Trace(0), Debug(3), Info(4), Warning(5), Error(6), Critical(7), None(disabled)
inline bool SetLogLevel(const char* name, quill::LogLevel level) {
    // Validate level - Quill defines valid levels as 0-7 inclusive
    // LogLevel::None has value 8 in Quill, which disables logging
    int level_val = static_cast<int>(level);
    if (level_val < 0 || level_val > 8) {
        return false; // Invalid level range
    }

    auto* logger = setup::LoggerRegistry::Get(name);
    if (!logger) {
        return false; // Logger doesn't exist
    }
    logger->set_log_level(level);
    return true;
}

/// @brief Get log level from a named logger
/// @param name Logger name
/// @return Current log level, or None if logger doesn't exist
inline quill::LogLevel GetLogLevel(const char* name) {
    auto* logger = setup::LoggerRegistry::Get(name);
    if (!logger) {
        return quill::LogLevel::None;
    }
    return logger->log_level();
}

/// @brief Shutdown the logging system
inline void ShutdownLogging() {
    bool was_init = detail::initialized().load(std::memory_order_acquire);
    detail::shutting_down().store(true, std::memory_order_release);
    if (was_init) {
        detail::was_shutdown().store(true, std::memory_order_release);
    }

    for (auto* logger : setup::LoggerRegistry::GetAllLoggers()) {
        if (logger) {
            logger->flush_backtrace();
        }
    }

    quill::Backend::stop();

    // Reset initialization flags to allow re-initialization (only if ResetLoggingForTesting is called, but we clear initialized to prevent double shutdown issues)
    detail::initialized().store(false, std::memory_order_release);
    detail::root_logger_ptr() = nullptr;
}



/// @brief Initialize the logging system with configuration
/// @details AA-C01: Creates root logger + named loggers
///          AA-P02: Restores and sets thread affinity masks for the backend
///          AA-M06: Per-logger/sink pattern validation
///          AA-M13: Registers stderr sink
///          AA-P01: Registers EventLog sink
inline bool InitializeLogging(LoggingConfig const& config) {
    static std::mutex init_mutex;
    std::lock_guard<std::mutex> lock(init_mutex);
    if (detail::was_shutdown().load(std::memory_order_acquire)) {
        return false;
    }
    if (detail::initialized().load(std::memory_order_acquire)) {
        return true;
    }

    bool ok = true;
    try {
        detail::shutting_down().store(false, std::memory_order_release);

        // Configure backend options
        quill::BackendOptions backend_opts;
        backend_opts.thread_name = config.backend_thread_name;
        backend_opts.sleep_duration = config.sleep_duration;
        backend_opts.enable_yield_when_idle = config.enable_yield_when_idle;

        // Apply thread affinity BEFORE starting backend so backend thread inherits it (AA-P02)
        GROUP_AFFINITY original_ga;
        bool need_restore_group = false;
        DWORD_PTR original_mask = 0;
        bool need_restore_mask = false;

        if (config.thread_affinity.processor_mask != 0) {
            if (config.thread_affinity.use_group_affinity) {
                GROUP_AFFINITY ga;
                ga.Mask = config.thread_affinity.processor_mask;
                ga.Group = config.thread_affinity.group;
                ga.Reserved[0] = 0;
                ga.Reserved[1] = 0;
                ga.Reserved[2] = 0;
                if (SetThreadGroupAffinity(GetCurrentThread(), &ga, &original_ga)) {
                    need_restore_group = true;
                }
            } else {
                original_mask = SetThreadAffinityMask(GetCurrentThread(),
                    config.thread_affinity.processor_mask);
                if (original_mask != 0 || GetLastError() == ERROR_SUCCESS) {
                    need_restore_mask = true;
                }
            }
        }

        // Start Quill backend
        quill::Backend::start(backend_opts);

        // AA-M07: Wire error notifier callback (if provided)
        if (config.error_notifier) {
            quill::Backend::set_error_notifier(
                [cb = config.error_notifier](std::string const& error) {
                    cb(error);
                });
        }

        // Restore calling thread's original affinity
        if (need_restore_group) {
            SetThreadGroupAffinity(GetCurrentThread(), &original_ga, nullptr);
        } else if (need_restore_mask) {
            SetThreadAffinityMask(GetCurrentThread(), original_mask);
        }

        // Create sinks for root logger
        std::vector<std::shared_ptr<quill::Sink>> sinks;

        if (config.console.enabled) {
            quill::ConsoleSinkConfig console_cfg;
            console_cfg.set_stream(config.console.stream);
            console_cfg.set_colour_mode(
                config.console.colored
                    ? quill::ConsoleSinkConfig::ColourMode::Automatic
                    : quill::ConsoleSinkConfig::ColourMode::Never);
            auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>(
                "console", console_cfg);
            sinks.push_back(console_sink);
        }

        if (config.stderr_sink.enabled) {
            quill::ConsoleSinkConfig stderr_cfg;
            stderr_cfg.set_stream("stderr");
            stderr_cfg.set_colour_mode(
                config.stderr_sink.colored
                    ? quill::ConsoleSinkConfig::ColourMode::Automatic
                    : quill::ConsoleSinkConfig::ColourMode::Never);
            auto stderr_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>(
                "stderr", stderr_cfg);
            sinks.push_back(stderr_sink);
        }

        if (config.eventlog.enabled) {
            auto eventlog_sink = quill::Frontend::create_or_get_sink<sinks::EventLogSink>(
                "eventlog",
                config.eventlog.source_name,
                setup::CategoryFromLoggerName("root")
            );
            sinks.push_back(eventlog_sink);
        }

        if (config.file.enabled && !config.file.rotating) {
            auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(
                config.file.filename);
            sinks.push_back(file_sink);
        }

        if (config.file.enabled && config.file.rotating) {
            quill::RotatingFileSinkConfig rotating_cfg;
            rotating_cfg.set_rotation_max_file_size(config.file.max_file_size);
            rotating_cfg.set_max_backup_files(config.file.max_files);
            auto file_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
                config.file.filename, rotating_cfg);
            sinks.push_back(file_sink);
        }

        if (config.json.enabled) {
            auto json_sink = quill::Frontend::create_or_get_sink<quill::JsonFileSink>(
                config.json.filename, quill::FileSinkConfig{});
            sinks.push_back(json_sink);
        }

        // Create root logger if sinks configured
        if (!sinks.empty()) {
            quill::PatternFormatterOptions root_pattern;
            try {
                root_pattern = MakePatternFormatter(config.console_pattern);
            } catch (const std::invalid_argument& e) {
                ::OutputDebugStringA((std::string("Pattern validation failed for root logger: ") + e.what() + "\n").c_str());
                root_pattern = quill::PatternFormatterOptions{};
            }
            detail::root_logger_ptr() = quill::Frontend::create_or_get_logger(
                "root", sinks, root_pattern);
            detail::root_logger_ptr()->set_log_level(
                ToQuillLogLevel(config.default_log_level));
            setup::LoggerRegistry::Register("root", detail::root_logger_ptr());
        }

        // Create named loggers (AA-C01)
        for (auto entry : config.loggers) {
            std::string error;
            auto result = setup::ValidateLoggerEntry(entry, config, error);

            switch (result) {
                case setup::ValidationResult::Pass:
                case setup::ValidationResult::Fail_LevelOutOfRange: {
                    if (result == setup::ValidationResult::Fail_LevelOutOfRange) {
                        ::OutputDebugStringA(("Logger '" + entry.name +
                            "': log_level out of range, clamping\n").c_str());
                    }

                    auto entry_sinks = setup::CreateSinksForLogger(entry, config);
                    
                    quill::PatternFormatterOptions pattern;
                    try {
                        bool has_file = false;
                        bool has_stderr = false;
                        bool has_console = false;
                        for (const auto& sname : entry.sink_names) {
                            if (sname == "file") has_file = true;
                            else if (sname == "stderr") has_stderr = true;
                            else if (sname == "console") has_console = true;
                        }
                        
                        if (has_console) {
                            pattern = MakePatternFormatter(config.console_pattern);
                        } else if (has_stderr) {
                            pattern = MakePatternFormatter(config.stderr_pattern);
                        } else if (has_file) {
                            pattern = MakePatternFormatter(config.file_pattern);
                        } else {
                            pattern = MakePatternFormatter(config.console_pattern);
                        }
                    } catch (const std::invalid_argument& e) {
                        ::OutputDebugStringA((std::string("Pattern validation failed for logger '") + entry.name + "': " + e.what() + "\n").c_str());
                        pattern = quill::PatternFormatterOptions{};
                    }

                    auto* logger = quill::Frontend::create_or_get_logger(
                        entry.name, entry_sinks, pattern);
                    logger->set_log_level(entry.log_level);

                    // Wire backtrace (AA-C03)
                    if (entry.backtrace_enabled) {
                        logger->init_backtrace(
                            entry.backtrace_capacity,
                            entry.backtrace_flush_level);
                    }
                    // Register logger in our registry (AA-C01)
                    setup::LoggerRegistry::Register(entry.name, logger);
                    break;
                }
                case setup::ValidationResult::Fail_EmptyName:
                    ::OutputDebugStringA("Skipping logger with empty name\n");
                    break;
                case setup::ValidationResult::Fail_SinkNotFound:
                    ::OutputDebugStringA(("Logger '" + entry.name +
                        "': sink not found, skipping\n").c_str());
                    break;
                case setup::ValidationResult::Fail_NoSinks:
                    ::OutputDebugStringA(("Logger '" + entry.name +
                        "': sink_names is empty, skipping\n").c_str());
                    break;
                case setup::ValidationResult::Fail_DuplicateName:
                    ::OutputDebugStringA(("Duplicate logger name '" + entry.name +
                        "', keeping first definition\n").c_str());
                    break;
                case setup::ValidationResult::Fail_EmergencyInvalid:
                    ::OutputDebugStringA(("FATAL: Emergency logger config invalid: " +
                        error + "\n").c_str());
                    ok = false;
                    return false;
            }
        }
    } catch (...) {
        ok = false;
    }

    if (ok) {
        detail::initialized().store(true, std::memory_order_release);
    }
    return ok;
}

/// @brief Get the default (root) logger
inline quill::Logger* GetDefaultLogger() {
    return detail::root_logger_ptr();
}

/// @brief Get a named logger with optional level override
inline quill::Logger* GetLogger(const char* name,
                               quill::LogLevel level = quill::LogLevel::Debug) {
#ifndef NDEBUG
    assert(!detail::shutting_down().load(std::memory_order_acquire));
#endif
    auto* logger = setup::LoggerRegistry::Get(name);
    if (!logger) {
        logger = GetDefaultLogger();
    }
    if (level != quill::LogLevel::None && logger) {
        logger->set_log_level(level);
    }
    return logger;
}

/// @brief Reset logging state for test isolation (test-only API)
inline void ResetLoggingForTesting() {
    detail::initialized().store(false, std::memory_order_release);
    detail::was_shutdown().store(false, std::memory_order_release);
    detail::shutting_down().store(false, std::memory_order_release);
    detail::root_logger_ptr() = nullptr;
    setup::LoggerRegistry::ResetForTesting();
}

} // namespace Logger_Adapter::logging