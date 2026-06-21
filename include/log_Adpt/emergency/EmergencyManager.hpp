#pragma once
#include "EmergencyConfig.hpp"
#include "../error/ErrorContext.hpp"
#include "../logging/HotPathLogger.hpp"
#include "HealthProbe.hpp"  // For RecordEmergency/RecordRecovery
#include <quill/Frontend.h>
#include <quill/Backend.h>
#include <atomic>
#include <cstdlib>
#include <vector>

namespace Logger_Adapter::emergency {

/// @enum EmergencyState
/// @brief State machine for emergency lifecycle management
/// @see AA-M11-EmergencyReset.md
enum class EmergencyState : uint8_t {
    Normal = 0,     ///< Healthy operation
    Degraded = 1,   ///< Error occurred, limited operation
    Recovering = 2, ///< Automated recovery in progress
    Fatal = 3       ///< Unrecoverable — process must terminate
};

/// @typedef RecoveryCallback
/// @brief Callback invoked during Reset() transition
/// @param epoch Recovery epoch identifier (incremented on each transition)
using RecoveryCallback = void(*)(uint64_t epoch);

class EmergencyManager {
public:
    /// @brief Recovery callback management
    static void RegisterRecoveryCallback(RecoveryCallback cb);

    /// @brief Initialize the emergency manager
    /// @param config Configuration settings
    static void Initialize(const EmergencyConfig& config);

    /// @brief Get current configuration
    static const EmergencyConfig& GetConfig() noexcept;

    /// @brief Query current emergency state
    /// @return Current state from state machine
    static EmergencyState GetState() noexcept;

    /// @brief Check if in emergency mode (Degraded, Recovering, or Fatal)
    static bool IsEmergencyMode() noexcept;

    /// @brief Get current recovery epoch
    /// @return Monotonic counter incremented on each recovery transition
    static uint64_t GetRecoveryEpoch() noexcept;

    /// @brief Notify emergency state (Normal -> Degraded)
    /// @param ctx Error context describing the emergency
    static void NotifyEmergency(const connector_errors::ErrorContext& ctx);

    /// @brief Reset from Degraded -> Recovering -> Normal
    /// @details AA-M11 Step 3: Flushes backtraces, invokes recovery callbacks
    static void Reset();

    /// @brief Fatal error handling (transitions to Fatal, then terminates)
    [[noreturn]] static void FatalError(const connector_errors::ErrorContext& ctx);

private:
    /// @brief Internal state access
    static std::atomic<EmergencyState>& state_();
    static std::atomic<uint64_t>& recovery_epoch_();
    static std::vector<RecoveryCallback>& recovery_callbacks_();
    static EmergencyConfig& config_();
};

/// @brief Internal state storage
inline std::atomic<EmergencyState>& EmergencyManager::state_() {
    static std::atomic<EmergencyState> s{EmergencyState::Normal};
    return s;
}

inline std::atomic<uint64_t>& EmergencyManager::recovery_epoch_() {
    static std::atomic<uint64_t> epoch{0};
    return epoch;
}

inline std::vector<EmergencyManager::RecoveryCallback>& EmergencyManager::recovery_callbacks_() {
    static std::vector<RecoveryCallback> callbacks;
    return callbacks;
}

inline EmergencyConfig& EmergencyManager::config_() {
    static EmergencyConfig cfg;
    return cfg;
}

/// @brief Inline implementations
inline void EmergencyManager::Initialize(const EmergencyConfig& config) {
    config_() = config;
    state_().store(EmergencyState::Normal, std::memory_order_release);
    recovery_epoch_().store(0, std::memory_order_relaxed);
}

inline const EmergencyConfig& EmergencyManager::GetConfig() noexcept {
    return config_();
}

inline EmergencyState EmergencyManager::GetState() noexcept {
    return state_().load(std::memory_order_acquire);
}

inline bool EmergencyManager::IsEmergencyMode() noexcept {
    return state_().load(std::memory_order_acquire) != EmergencyState::Normal;
}

inline uint64_t EmergencyManager::GetRecoveryEpoch() noexcept {
    return recovery_epoch_().load(std::memory_order_acquire);
}

/// @brief Register a recovery callback (called during Reset())
inline void EmergencyManager::RegisterRecoveryCallback(RecoveryCallback cb) {
    recovery_callbacks_().push_back(cb);
}

/// @brief Notify emergency (Normal -> Degraded transition)
inline void EmergencyManager::NotifyEmergency(const connector_errors::ErrorContext& ctx) {
    // Transition to Degraded if currently Normal
    EmergencyState expected = EmergencyState::Normal;
    state_().compare_exchange_strong(expected, EmergencyState::Degraded,
        std::memory_order_acq_rel);
    
    quill::Logger* logger = nullptr;
    if (config_().emergency_logger_name) {
        logger = quill::Frontend::get_logger(config_().emergency_logger_name);
    }
    if (!logger) {
        logger = quill::Frontend::get_valid_logger();
    }
    
    if (logger) {
        LOG_CRIT(logger, "EMERGENCY: {} at {}:{} [{}]",
            connector_errors::ErrorCodeToString(ctx.code),
            ctx.file ? ctx.file : "?",
            ctx.line,
            ctx.message ? ctx.message : "");
    }
    
    if (config_().flush_logs_on_crash && logger) {
        logger->flush_log(0);
    }
    
    // Record in HealthProbe
    HealthProbe::RecordEmergency();
}

/// @brief Reset from Degraded -> Recovering -> Normal
/// @see AA-M11 Step 3
inline void EmergencyManager::Reset() {
    // Only reset if we're in Degraded state
    EmergencyState current = state_().load(std::memory_order_acquire);
    if (current != EmergencyState::Degraded) {
        return; // No-op if not in degraded state
    }
    
    // 1. Transition to Recovering
    state_().store(EmergencyState::Recovering, std::memory_order_release);
    
    // 2. Increment recovery epoch
    recovery_epoch_().fetch_add(1, std::memory_order_acq_rel);
    uint64_t epoch = recovery_epoch_().load(std::memory_order_relaxed);
    
    // 3. Flush backtraces (C03)
    quill::Logger* emergency_logger = nullptr;
    if (config_().emergency_logger_name) {
        emergency_logger = quill::Frontend::get_logger(config_().emergency_logger_name);
    }
    if (emergency_logger) {
        emergency_logger->flush_backtrace();
    }
    
    // 4. Invoke recovery callbacks
    auto callbacks = recovery_callbacks_();
    for (auto& cb : callbacks) {
        if (cb) {
            cb(epoch);
        }
    }
    
    // 5. Log recovery event
    if (emergency_logger) {
        LOG_INFO(emergency_logger, "Emergency mode reset, entering recovery (epoch={})", epoch);
    }
    
    // 6. Transition to Normal
    state_().store(EmergencyState::Normal, std::memory_order_release);
    
    // 7. Record recovery in HealthProbe
    HealthProbe::RecordRecovery();
}

/// @brief Fatal error (transition to Fatal, then terminate)
inline void EmergencyManager::FatalError(const connector_errors::ErrorContext& ctx) {
    // Transition to Fatal state
    state_().store(EmergencyState::Fatal, std::memory_order_release);
    
    if (config_().pre_shutdown_callback) {
        config_().pre_shutdown_callback();
    }
    NotifyEmergency(ctx);
    if (config_().post_shutdown_callback) {
        config_().post_shutdown_callback();
    }
    
    // Guard against calling stop() if backend was never started
    if ((config_().flush_logs_on_shutdown || config_().flush_logs_on_crash) &&
        quill::Backend::is_running()) {
        quill::Backend::stop();
    }
    
    if (config_().abort_on_fatal) {
        std::abort();
    }
    std::exit(EXIT_FAILURE);
}

} // namespace Logger_Adapter::emergency