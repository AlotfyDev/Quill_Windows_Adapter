#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <csignal>
#include <functional>
#include "config/QueueConfig.hpp"
#include "config/LoggerEntry.hpp"
#include "config/PatternConfig.hpp"
#include "sinks/DailyRotatingFileSink.hpp"
#include "windows/ThreadAffinity.hpp"

namespace Logger_Adapter::logging {

/// @struct LoggingConfig
/// @brief Main configuration for Logger_Adapter initialization
/// @details Layer 2 (POD) - Cross-language configuration DTO
///          AA-C01: Contains queue and named logger configurations
///          AA-M08: QueueConfig is startup-only (no hot-reload)
///          AA-P02: ThreadAffinityConfig for pinning the backend thread
///          AA-M06: PatternConfig per sink type
///          AA-P01: EventLog config block
///          AA-M13: Stderr config block (renamed to stderr_sink to avoid Windows macro conflict)
struct LoggingConfig {
    /// Backend thread configuration
    std::string backend_thread_name = "LogBackend";
    std::chrono::microseconds sleep_duration{100};
    bool enable_yield_when_idle = false;

    /// Signal handler configuration
    bool enable_signal_handler = true;
    std::vector<int> catchable_signals = {SIGTERM, SIGINT, SIGABRT, SIGFPE, SIGSEGV};

    /// Console (stdout) sink configuration
    struct {
        bool enabled = true;
        std::string stream = "stdout";
        bool colored = true;
    } console;

    /// File sink configuration
    struct {
        bool enabled = false;
        std::string filename = "logs/assembler.log";
        bool rotating = true;
        size_t max_file_size = 10 * 1024 * 1024;
        uint32_t max_files = 5;

        /// Daily rotation (AA-M02)
        sinks::DailyRotationConfig daily;
    } file;

    /// JSON sink configuration
    struct {
        bool enabled = false;
        std::string filename = "logs/assembler.json";
    } json;

    /// Stderr sink configuration (AA-M13)
    struct {
        bool enabled = false;
        bool colored = true;
    } stderr_sink;

    /// EventLog sink configuration (AA-P01)
    struct {
        bool enabled = false;
        std::string source_name = "Logger_Adapter_Trading_System";
    } eventlog;

    /// Patterns (AA-M06)
    config::PatternConfig console_pattern{"%(time) [%(log_level)] %(logger) %(message)", "%H:%M:%S.%Qns", true};
    config::PatternConfig file_pattern{"%(time) [%(log_level)] [%(thread_id)] %(file_name):%(line_number) %(message)", "%H:%M:%S.%Qns", true};
    config::PatternConfig stderr_pattern{"%(time) [%(log_level)] %(logger) %(message)", "%H:%M:%S.%Qns", true};

    /// Thread affinity for backend thread (AA-P02)
    windows::ThreadAffinityConfig thread_affinity;

    /// Sanitization configuration (AA-M18)
    config::SanitizationConfig sanitization;

    /// Error notifier callback (AA-M07)
    /// Callback invoked on backend thread when Quill encounters an error.
    /// WARNING: This callback runs on the Quill backend thread.
    ///          It MUST be non-blocking (no I/O, no locks, no allocations).
    ///          Blocking the callback stalls ALL logging.
    ///          There is NO guaranteed delivery — errors may be silently dropped.
    /// LIFETIME: The std::function is copied by Quill internally.
    std::function<void(std::string const&)> error_notifier = nullptr;

    /// Default log level (integer mapped to Quill LogLevel via ToQuillLogLevel)
    int default_log_level = 3;

    /// Backend queue configuration
    config::QueueConfig queue;

    /// Named logger configuration (AA-C01: Multi-Logger feature)
    bool enable_named_loggers = true;
    std::vector<config::LoggerEntry> loggers;
};

} // namespace Logger_Adapter::logging