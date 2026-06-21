// ============================================================================
// Rate-Limited Macros Implementation
// AA Spec: AA-M05-RateLimiting.md
//
// Defines GlobalRateLimiter and GetGlobalRateLimiter() for process-wide rate limiting.
// ============================================================================

#include "pch.h"
#include "log_Adpt/macros/RateLimited.hpp"
#include <atomic>
#include <cstdint>

namespace Logger_Adapter::macros {

GlobalRateLimiter& GetGlobalRateLimiter() {
    // Single instance — this function is called from all TUs but
    // the static lives in ONE .cpp file, so there is exactly one instance.
    static GlobalRateLimiter limiter(1000);
    return limiter;
}

GlobalRateLimiter::GlobalRateLimiter(uint32_t max_per_second) noexcept
    : max_per_second_(max_per_second) {
}

bool GlobalRateLimiter::TryAcquire() noexcept {
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    uint64_t epoch = epoch_.load(std::memory_order_relaxed);
    
    if (now != epoch) {
        epoch_.store(now, std::memory_order_relaxed);
        global_count_.store(0, std::memory_order_relaxed);
    }
    
    uint32_t current = global_count_.load(std::memory_order_relaxed);
    if (current < max_per_second_) {
        global_count_.store(current + 1, std::memory_order_relaxed);
        return true;
    }
    
    return false;
}

void GlobalRateLimiter::SetMaxPerSecond(uint32_t max) noexcept {
    max_per_second_ = max;
}

void GlobalRateLimiter::ResetForTesting() noexcept {
    epoch_.store(0, std::memory_order_relaxed);
    global_count_.store(0, std::memory_order_relaxed);
}

} // namespace Logger_Adapter::macros