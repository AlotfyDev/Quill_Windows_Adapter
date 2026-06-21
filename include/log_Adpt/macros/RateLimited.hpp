// ============================================================================
// Rate-Limited Macros
// AA Spec: AA-M05-RateLimiting.md
//
// REQUIREMENT: All limiters use std::atomic with relaxed memory ordering.
// Time-based macros fire at most N times per second/minute.
// Global rate limiter shares one counter across all TUs via GetGlobalRateLimiter().
// ============================================================================

#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <quill/LogMacros.h>

namespace Logger_Adapter::macros {

// Global rate limiter — max logs per second across ALL loggers.
// Single instance lives in RateLimited.cpp (not a header static).
class GlobalRateLimiter {
    std::atomic<uint32_t> global_count_{0};
    std::atomic<uint64_t> epoch_{0};
    uint32_t max_per_second_;

public:
    explicit GlobalRateLimiter(uint32_t max_per_second = 1000) noexcept;
    bool TryAcquire() noexcept;
    void SetMaxPerSecond(uint32_t max) noexcept;
    void ResetForTesting() noexcept;  // test-only: resets counters without affecting max_per_second_
};

// Returns the single process-wide global rate limiter instance.
// Defined in RateLimited.cpp to guarantee exactly one instance
// across all translation units. Call SetMaxPerSecond() on the
// returned instance during initialization to configure the rate.
GlobalRateLimiter& GetGlobalRateLimiter();

} // namespace Logger_Adapter::macros

// Count-based (existing Quill — keep)
// LOG_LIMIT and LOG_EVERY_N are Quill's native macros, NOT redefined here.

// Time-based (NEW)
// LOG_LIMIT_PER_SEC and LOG_LIMIT_PER_MIN use function-local static
// variables inside the macro expansion. Each call site gets
// its own independent limiter — this is INTENTIONAL so that different
// log calls are throttled independently.
#define LOG_LIMIT_PER_SEC(logger, rate, fmt, ...) \
    do { \
        static std::atomic<uint64_t> _last_ts{0}; \
        static std::atomic<uint32_t> _count{0}; \
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>( \
            std::chrono::steady_clock::now().time_since_epoch()).count(); \
        uint64_t last = _last_ts.load(std::memory_order_relaxed); \
        if (now != last) { _last_ts.store(now, std::memory_order_relaxed); _count.store(0, std::memory_order_relaxed); } \
        if (_count.fetch_add(1, std::memory_order_relaxed) < rate) { \
            LOG_INFO(logger, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_LIMIT_PER_MIN(logger, rate, fmt, ...) \
    do { \
        static std::atomic<uint64_t> _last_ts{0}; \
        static std::atomic<uint32_t> _count{0}; \
        uint64_t now = std::chrono::duration_cast<std::chrono::minutes>( \
            std::chrono::steady_clock::now().time_since_epoch()).count(); \
        uint64_t last = _last_ts.load(std::memory_order_relaxed); \
        if (now != last) { _last_ts.store(now, std::memory_order_relaxed); _count.store(0, std::memory_order_relaxed); } \
        if (_count.fetch_add(1, std::memory_order_relaxed) < rate) { \
            LOG_INFO(logger, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// First call after suppression window ALWAYS fires (burst allowance built-in)
// NOTE: LOG_GLOBAL_LIMIT uses GetGlobalRateLimiter() which returns a
// single instance defined in RateLimited.cpp. All call sites across
// all translation units share ONE counter — this is the "global" guarantee.
// The max rate MUST be configured during initialization via
// GetGlobalRateLimiter().SetMaxPerSecond(). The default is 1000/s.
#define LOG_GLOBAL_LIMIT(logger, fmt, ...) \
    do { \
        if (Logger_Adapter::macros::GetGlobalRateLimiter().TryAcquire()) { \
            LOG_INFO(logger, fmt, ##__VA_ARGS__); \
        } \
    } while(0)