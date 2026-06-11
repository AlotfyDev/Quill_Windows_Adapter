#pragma once

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "LoggingConfig.hpp"
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/FileSink.h>
#include <quill/sinks/RotatingFileSink.h>
#include <quill/sinks/JsonSink.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Logger_Adapter::logging {

namespace detail {
inline quill::Logger*& root_logger_ptr() {
    static quill::Logger* root = nullptr;
    return root;
}
inline std::once_flag& init_flag() {
    static std::once_flag flag;
    return flag;
}
} // namespace detail

inline quill::LogLevel ToQuillLogLevel(int level) {
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

inline bool InitializeLogging(LoggingConfig const& config) {
    bool ok = true;
    std::call_once(detail::init_flag(), [&ok, &config]() {
        try {
            quill::BackendOptions backend_opts;
            backend_opts.thread_name = config.backend_thread_name;
            backend_opts.sleep_duration = config.sleep_duration;
            backend_opts.enable_yield_when_idle = config.enable_yield_when_idle;
            quill::Backend::start(backend_opts);

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

            if (!sinks.empty()) {
                detail::root_logger_ptr() = quill::Frontend::create_or_get_logger(
                    "root", sinks);
                detail::root_logger_ptr()->set_log_level(
                    ToQuillLogLevel(config.default_log_level));
            }
        } catch (...) {
            ok = false;
        }
    });
    return ok;
}

inline quill::Logger* GetLogger(const char* /*name*/,
                               quill::LogLevel /*level*/ = quill::LogLevel::Debug) {
    return detail::root_logger_ptr();
}

inline quill::Logger* GetDefaultLogger() {
    return detail::root_logger_ptr();
}

inline void ShutdownLogging() {
    quill::Backend::stop();
}

} // namespace Logger_Adapter::logging
