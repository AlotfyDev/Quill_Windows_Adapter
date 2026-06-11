#pragma once
#include "EmergencyConfig.hpp"
#include "../error/ErrorContext.hpp"
#include "../logging/HotPathLogger.hpp"
#include <quill/Frontend.h>
#include <quill/Backend.h>
#include <atomic>
#include <cstdlib>

namespace Logger_Adapter::emergency {

class EmergencyManager {
public:
    static void Initialize(const EmergencyConfig& config);

    static const EmergencyConfig& GetConfig() noexcept;

    static bool IsEmergencyMode() noexcept;

    [[noreturn]] static void FatalError(const connector_errors::ErrorContext& ctx);

    static void NotifyEmergency(const connector_errors::ErrorContext& ctx);

private:
    static std::atomic<bool> emergency_mode_;
    static EmergencyConfig config_;
};

inline std::atomic<bool> EmergencyManager::emergency_mode_{false};
inline EmergencyConfig EmergencyManager::config_{};

inline void EmergencyManager::Initialize(const EmergencyConfig& config) {
    config_ = config;
    emergency_mode_.store(false, std::memory_order_relaxed);
}

inline const EmergencyConfig& EmergencyManager::GetConfig() noexcept {
    return config_;
}

inline bool EmergencyManager::IsEmergencyMode() noexcept {
    return emergency_mode_.load(std::memory_order_acquire);
}

inline void EmergencyManager::NotifyEmergency(const connector_errors::ErrorContext& ctx) {
    emergency_mode_.store(true, std::memory_order_release);

    quill::Logger* logger = nullptr;
    if (config_.emergency_logger_name) {
        logger = quill::Frontend::get_logger(config_.emergency_logger_name);
    }
    if (!logger) {
        logger = quill::Frontend::get_valid_logger();
    }

    if (logger) {
        do {
    if ((logger->template should_log_statement<quill::LogLevel::Critical>())) {
        static constexpr quill::MacroMetadata macro_metadata{ __FILE__ ":" "61", __FUNCTION__, "EMERGENCY: {} at {}:{} [{}]", nullptr, quill::LogLevel::Critical, quill::MacroMetadata::Event::Log }; logger->template log_statement<1>(&macro_metadata, connector_errors::ErrorCodeToString(ctx.code), ctx.file ? ctx.file : "?", ctx.line, ctx.message ? ctx.message : "");
    }
} while (0);
    }

    if (config_.flush_logs_on_crash && logger) {
        logger->flush_log(0);
    }
}

inline void EmergencyManager::FatalError(const connector_errors::ErrorContext& ctx) {
    emergency_mode_.store(true, std::memory_order_release);
    if (config_.pre_shutdown_callback) config_.pre_shutdown_callback();
    NotifyEmergency(ctx);
    if (config_.post_shutdown_callback) config_.post_shutdown_callback();

    if (config_.flush_logs_on_shutdown || config_.flush_logs_on_crash) {
        quill::Backend::stop();
    }

    if (config_.abort_on_fatal) {
        std::abort();
    }
    std::exit(EXIT_FAILURE);
}

} // namespace Logger_Adapter::emergency
