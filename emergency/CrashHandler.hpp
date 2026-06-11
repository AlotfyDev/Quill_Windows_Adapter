#pragma once
#include "EmergencyConfig.hpp"
#include <quill/Backend.h>
#include <quill/backend/SignalHandler.h>

#include <csignal> // Required for signal constants (SIGTERM, SIGINT, etc.)

namespace Logger_Adapter::emergency {

inline quill::SignalHandlerOptions MakeSignalHandlerOptions(const EmergencyConfig& config) {
    quill::SignalHandlerOptions opts;
    opts.timeout_seconds = config.signal_timeout_seconds; // Linux-only: alarm() is POSIX, no-op on Windows
    
    opts.logger= config.emergency_logger_name ? config.emergency_logger_name : "emergency";
    return opts;
}

inline bool SetupCrashLogger(const EmergencyConfig& config) {
    if (!config.enable_crash_logging || !config.enable_signal_handler) {
        return false;
    }
    return config.emergency_logger_name != nullptr;
}

} // namespace
