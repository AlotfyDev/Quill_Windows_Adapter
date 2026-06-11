#pragma once
#include <cstdint>

namespace Logger_Adapter::emergency {

struct EmergencyConfig {
    // Signal handling
    bool enable_signal_handler = true;
    bool enable_crash_logging = true;
    uint32_t signal_timeout_seconds = 20; // Windows: ignored (alarm() is POSIX-only)

    // Graceful shutdown
    uint32_t shutdown_timeout_ms = 5000;
    bool flush_logs_on_shutdown = true;
    bool flush_logs_on_crash = true;

    // Emergency logger name
    const char* emergency_logger_name = "emergency";

    // Behaviour
    bool abort_on_fatal = true;

    // Callbacks (optional)
    using ShutdownCallback = void(*)();
    ShutdownCallback pre_shutdown_callback = nullptr;
    ShutdownCallback post_shutdown_callback = nullptr;
};

} // namespace Logger_Adapter::emergency
