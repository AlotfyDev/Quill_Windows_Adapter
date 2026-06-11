#pragma once
#include <cstdint>
#include <atomic>
#include <chrono>

namespace Logger_Adapter::emergency {

class HealthProbe {
public:
    struct HealthStatus {
        bool backend_running;
        uint64_t uptime_ms;
        bool last_emergency_triggered;
        uint64_t last_emergency_timestamp_ms;
    };

    static void MarkStartup() noexcept;
    static void RecordEmergency() noexcept;
    static void SetBackendRunning(bool running) noexcept;
    static HealthStatus GetHealthStatus() noexcept;

private:
    static std::atomic<bool> backend_running_;
    static std::atomic<bool> emergency_triggered_;
    static std::atomic<uint64_t> emergency_timestamp_;
    static std::atomic<uint64_t> startup_time_ms_;
};

inline std::atomic<bool> HealthProbe::backend_running_{false};
inline std::atomic<bool> HealthProbe::emergency_triggered_{false};
inline std::atomic<uint64_t> HealthProbe::emergency_timestamp_{0};
inline std::atomic<uint64_t> HealthProbe::startup_time_ms_{0};

inline void HealthProbe::MarkStartup() noexcept {
    startup_time_ms_.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
        std::memory_order_release);
}

inline void HealthProbe::RecordEmergency() noexcept {
    emergency_triggered_.store(true, std::memory_order_release);
    emergency_timestamp_.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
        std::memory_order_release);
}

inline void HealthProbe::SetBackendRunning(bool running) noexcept {
    backend_running_.store(running, std::memory_order_release);
}

inline HealthProbe::HealthStatus HealthProbe::GetHealthStatus() noexcept {
    HealthStatus status;
    status.backend_running = backend_running_.load(std::memory_order_acquire);
    status.uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()
        - startup_time_ms_.load(std::memory_order_relaxed);
    status.last_emergency_triggered = emergency_triggered_.load(std::memory_order_acquire);
    status.last_emergency_timestamp_ms = emergency_timestamp_.load(std::memory_order_acquire);
    return status;
}

} // namespace
