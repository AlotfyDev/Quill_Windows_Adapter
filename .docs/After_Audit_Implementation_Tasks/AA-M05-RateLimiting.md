# AA-M05 — Rate-Limited Macros (After-Audit Corrected — Complete Redesign)

> **Phase**: 2 — 🔧 Redesign  
> **Effort**: 1h design + 1h implementation = 2h total  
> **Depends on**: AA-C01 (needs named loggers)  
> **v1.x Reference**: TASK-M05-LimitMacros.md  
> **Audit Issues**: M05-A (time-based), M05-B (burst), M05-C (global), M05-D (thread safety)  
> **Audit Verdict**: ❌ Fail — Requires Complete Redesign

---

## Problem

Logger_Adapter only proposes Quill's count-based `EVERY_N` and `LIMIT` macros. In production:
- Count-based `EVERY_N` varies with throughput (worse for fast subsystems, better for slow ones)
- No time-based limits ("max 1 log per 10 seconds")
- No burst allowance (first log after suppression must always fire)
- No global rate limiter (a buggy subsystem can DoS the logger)

---

## Corrected Design

### 1. Time-Based Limiters (NEW)

Add time-based macros alongside existing count-based ones:

```cpp
// Count-based (existing Quill — keep)
LOG_LIMIT(logger, frequency, fmt, ...)
LOG_EVERY_N(logger, n, fmt, ...)

// Time-based (NEW)
LOG_LIMIT_PER_SEC(logger, rate, fmt, ...)   // max rate per second
LOG_LIMIT_PER_MIN(logger, rate, fmt, ...)   // max rate per minute

// First call after suppression window ALWAYS fires (burst allowance built-in)
```

Implementation: Uses `std::atomic<uint64_t>` with timestamp comparison:

```cpp
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
```

> **Known race at second boundary**: If two threads simultaneously see `now != last`, one resets `_count` while the other may increment the old value. During this transition, up to `(rate + thread_count)` messages may fire instead of `rate`. This is documented as **best-effort rate limiting during contention** — acceptable for a rate limiter in production trading systems.

### 2. Global Rate Limiter (NEW)

```cpp
// Global rate limiter — non-static; single instance lives in RateLimited.cpp
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
// Defined in RateLimited.cpp — NOT a header static local.
GlobalRateLimiter& GetGlobalRateLimiter();

// NOTE: The global rate limiter's max_per_second MUST be configured via
// SetMaxPerSecond() during application initialization, before any
// LOG_GLOBAL_LIMIT calls. The default constructor value (1000) applies
// if SetMaxPerSecond() is never called.
#define LOG_GLOBAL_LIMIT(logger, fmt, ...) \
    do { \
        if (Logger_Adapter::macros::GetGlobalRateLimiter().TryAcquire()) { \
            LOG_INFO(logger, fmt, ##__VA_ARGS__); \
        } \
    } while(0)
```

> **Global scope guarantee**: `GetGlobalRateLimiter()` is defined in `RateLimited.cpp`, not in the header. The `static` variable inside it lives in a single translation unit, so all call sites across the entire process share one counter. This is distinct from `LOG_LIMIT_PER_SEC` / `LOG_LIMIT_PER_MIN`, where per-call-site isolation via header-local `static` is intentional.

### 3. Burst Allowance

All limiters (count + time + global) follow this rule:
- **First log after window ALWAYS fires**
- Subsequent logs within the window are suppressed
- This ensures that an operator always sees at least the first occurrence of an issue

### 4. Thread Safety

| Limiter | Mechanism | Safe? |
|---------|-----------|-------|
| `EVERY_N` | `std::atomic<uint32_t>` | ✅ |
| `LIMIT` | `std::atomic<uint32_t>` + counter reset | ✅ |
| `LIMIT_PER_SEC` | `std::atomic<uint64_t>` (ts) + `std::atomic<uint32_t>` (count) | ✅ |
| `LIMIT_PER_MIN` | Same as above | ✅ |
| `GlobalRateLimiter` | `std::atomic<uint32_t>` + atomic epoch | ✅ |

All limiters use relaxed memory ordering — sequentially consistent ordering would add unnecessary overhead.

> **Architecture note (weakly-ordered CPUs)**: On ARM64 (AWS Graviton, Apple Silicon) and PowerPC, `memory_order_relaxed` means stores from one thread may not be visible to another thread in a timely manner. For rate limiting (approximate counting), this is acceptable — the occasional extra message within a transition window has no correctness impact. This relaxation would NOT be safe for precise synchronization (e.g., mutexes, handshakes).

> **Inline function warning**: `LOG_LIMIT_PER_SEC` and `LOG_LIMIT_PER_MIN` use function-local `static` variables inside the macro expansion. If these macros are used inside an `inline` function defined in a header, each translation unit that includes the header gets its OWN independent limiter. The "per-call-site" isolation guarantee applies to macro expansion sites in `.cpp` files only, not to ODR-used inline functions across TUs.

---

## Corrected Implementation Plan

### Step 1 — Populate `macros/RateLimited.hpp` and `macros/RateLimited.cpp`

```cpp
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

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
};

// Returns the single process-wide global rate limiter instance.
// Defined in RateLimited.cpp to guarantee exactly one instance
// across all translation units. Call SetMaxPerSecond() on the
// returned instance during initialization to configure the rate.
GlobalRateLimiter& GetGlobalRateLimiter();

} // namespace Logger_Adapter::macros

// Macros
// NOTE: LOG_LIMIT_PER_SEC and LOG_LIMIT_PER_MIN use function-local
// static variables inside the macro expansion. Each call site gets
// its own independent limiter — this is INTENTIONAL so that different
// log calls are throttled independently.
#define LOG_LIMIT_PER_SEC(logger, rate, fmt, ...)  /* ... */
#define LOG_LIMIT_PER_MIN(logger, rate, fmt, ...)   /* ... */
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
```

**Implementation** (`macros/RateLimited.cpp`):
```cpp
namespace Logger_Adapter::macros {

GlobalRateLimiter& GetGlobalRateLimiter() {
    // Single instance — this function is called from all TUs but
    // the static lives in ONE .cpp file, so there is exactly one instance.
    static GlobalRateLimiter limiter(1000);
    return limiter;
}

} // namespace Logger_Adapter::macros
```

### Step 2 — Keep existing `EVERY_N` and `LIMIT`

The existing Quill count-based macros remain unchanged. Time-based is an addition.

### Step 3 — Add to Experimental_Console test

```cpp
// Demonstrate rate limiting
LOG_LIMIT_PER_SEC(order_log, 5, "Rate-limited order status update");
LOG_GLOBAL_LIMIT(root_log, "Global limit test — should appear normally");
```

---

## Acceptance Criteria

- [ ] `LOG_LIMIT_PER_SEC(logger, 5, ...)` fires at most 5 times per second
- [ ] First log after each second always fires (burst allowance)
- [ ] `LOG_GLOBAL_LIMIT(root_log, 100, ...)` suppresses across all call sites when global count exceeds 100/s
- [ ] Existing `EVERY_N` and `LIMIT` still work unchanged
- [ ] Build succeeds Debug|x64 with zero new warnings
- [ ] All limiters are documented with thread safety guarantees

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/macros/RateLimited.hpp` | Populate from stub with time-based + global limiters |
| `Logger_Adapter/macros/RateLimited.cpp` | **NEW** — define `GetGlobalRateLimiter()` with single static instance |
| `Experimental_Console/Experimental_Console.cpp` | Add rate limit demo |