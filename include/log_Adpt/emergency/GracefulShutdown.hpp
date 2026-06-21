#pragma once
#include "EmergencyConfig.hpp"
#include "../logging/HotPathLogger.hpp"
#include <quill/Frontend.h>
#include <quill/Backend.h>
#include <atomic>

namespace Logger_Adapter::emergency {

class GracefulShutdown {
public:
    static void RequestShutdown(int signal_number) noexcept;

    static bool IsShutdownRequested() noexcept;

    static int GetShutdownSignal() noexcept;

    static void Execute(const EmergencyConfig& config);

private:
    static std::atomic<bool> shutdown_requested_;
    static std::atomic<int> shutdown_signal_;
};

inline std::atomic<bool> GracefulShutdown::shutdown_requested_{false};
inline std::atomic<int> GracefulShutdown::shutdown_signal_{0};

inline void GracefulShutdown::RequestShutdown(int signal_number) noexcept {
    shutdown_signal_.store(signal_number, std::memory_order_relaxed);
    shutdown_requested_.store(true, std::memory_order_release);
}

inline bool GracefulShutdown::IsShutdownRequested() noexcept {
    return shutdown_requested_.load(std::memory_order_acquire);
}

inline int GracefulShutdown::GetShutdownSignal() noexcept {
    return shutdown_signal_.load(std::memory_order_relaxed);
}

inline void GracefulShutdown::Execute(const EmergencyConfig& config) {
    if (config.pre_shutdown_callback) {
        config.pre_shutdown_callback();
    }

    quill::Logger* logger = quill::Frontend::get_logger(
        config.emergency_logger_name ? config.emergency_logger_name : "emergency");
    if (logger) {
        LOG_INFO(logger, "Graceful shutdown initiated (signal: {})",
                 shutdown_signal_.load(std::memory_order_relaxed));
    }

    if (config.flush_logs_on_shutdown) {
        quill::Backend::stop();
    }

    if (config.post_shutdown_callback) {
        config.post_shutdown_callback();
    }
}

} // namespace
