# REV-M05 — GlobalRateLimiter Per-TU Isolation (Not Truly Global)

**Severity**: ⚠️ Revision  
**AA File**: AA-M05-RateLimiting.md  
**Validation Source**: Phase2-3_AA_Validation_Map.md §2 (M05), §5 Hidden API Issues, §6 Gap Analysis

---

## Description

`GlobalRateLimiter` is intended to limit log output across **all call sites and all translation units** (a true process-wide throttle). However, the current AA-M05 design places it inside a macro using a function-local `static` variable:

```cpp
#define LOG_GLOBAL_LIMIT(logger, max_per_sec, fmt, ...) \
    do { \
        static Logger_Adapter::macros::GlobalRateLimiter _glim(max_per_sec); \
        if (_glim.TryAcquire()) { \
            LOG_INFO(logger, fmt, ##__VA_ARGS__); \
        } \
    } while(0)
```

In C++, function-local `static` variables have **per-translation-unit visibility**. Under MSVC with `/GL` (Whole Program Optimization), the linker may or may not merge them, but the C++ standard guarantees they are distinct objects. The result: **every `.cpp` file that uses `LOG_GLOBAL_LIMIT` gets its own independent `GlobalRateLimiter` instance**, each counting from 0. A burst of 1000 logs spread across 10 translation units would fire 10,000 times (1000 each) instead of the intended 1000 total.

The same per-TU problem applies to the per-second and per-minute limiters (`LOG_LIMIT_PER_SEC`, `LOG_LIMIT_PER_MIN`), though those are intentionally per-call-site so the isolation is correct for them. The contradiction is in the naming: `LOG_GLOBAL_LIMIT` claims to be global but is not.

---

## Root Cause

The AA designer correctly identified the need for a process-wide rate throttle but chose the familiar macro-local-static pattern (consistent with the other limiters) without considering C++ linkage rules for function-local statics. The macro is expanded in every translation unit that includes the header; the `static` keyword gives internal linkage but a **new instance** is created per function scope per TU. The AA-COMPLIANCE audit flagged this as a secondary issue but the finding was never incorporated back into AA-M05.

---

## Exact Fix

### 1. Move GlobalRateLimiter State to a Single Definition (`.cpp` file)

The `GlobalRateLimiter` must be a **named class with a single instance** accessed via a free function declared in the header and defined exactly once in a `.cpp` file.

**Header** (`macros/RateLimited.hpp`):
```cpp
namespace Logger_Adapter::macros {

// Global rate limiter — single process-wide instance.
// Implementation is in RateLimited.cpp to guarantee
// exactly one instance across all translation units.
class GlobalRateLimiter {
    // Non-static now — one instance in .cpp
    std::atomic<uint32_t> global_count_{0};
    std::atomic<uint64_t> epoch_{0};
    uint32_t max_per_second_;

public:
    explicit GlobalRateLimiter(uint32_t max_per_second = 1000) noexcept;
    bool TryAcquire() noexcept;
    void SetMaxPerSecond(uint32_t max) noexcept;
};

// Returns the single process-wide global rate limiter instance.
// Defined in RateLimited.cpp — NOT a static local.
GlobalRateLimiter& GetGlobalRateLimiter(uint32_t max_per_second = 1000);

} // namespace Logger_Adapter::macros
```

**Implementation** (`macros/RateLimited.cpp`):
```cpp
namespace Logger_Adapter::macros {

GlobalRateLimiter& GetGlobalRateLimiter(uint32_t max_per_second) {
    // Single instance — this function is called from all TUs but
    // the static lives in ONE .cpp file, so there is exactly one instance.
    static GlobalRateLimiter limiter(max_per_second);
    return limiter;
}

} // namespace Logger_Adapter::macros
```

### 2. Update the Macro

```cpp
#define LOG_GLOBAL_LIMIT(logger, max_per_sec, fmt, ...) \
    do { \
        if (Logger_Adapter::macros::GetGlobalRateLimiter(max_per_sec).TryAcquire()) { \
            LOG_INFO(logger, fmt, ##__VA_ARGS__); \
        } \
    } while(0)
```

### 3. Document the Design Decision

In AA-M05, add a prominent note beneath the `LOG_GLOBAL_LIMIT` declaration:

> **Global scope guarantee**: `GetGlobalRateLimiter()` is defined in `RateLimited.cpp`, not in the header. The `static` variable inside it lives in a single translation unit, so all call sites across the entire process share one counter. This is distinct from `LOG_LIMIT_PER_SEC` / `LOG_LIMIT_PER_MIN`, where per-call-site isolation via header-local `static` is intentional.

### 4. Also Update Per-Second and Per-Minute Limiters (Documentation Only)

For `LOG_LIMIT_PER_SEC` and `LOG_LIMIT_PER_MIN`, the per-TU `static` is **correct and intentional** — each call site gets an independent counter. Add a one-line comment:

```cpp
// NOTE: Each expansion of this macro gets its own independent limiter
// (function-local static). This is intentional — each call site is
// throttled independently.
```

---

## Impact if NOT Fixed

| Scenario | Current Behavior (Broken) | Correct Behavior |
|----------|--------------------------|------------------|
| 10 TUs each call `LOG_GLOBAL_LIMIT(root, 100, ...)` | Each TU fires 100/s → 1000/s total | All TUs share one counter → 100/s total |
| Buggy subsystem floods logs at 10k/s | Per-TU limiter allows 1000/s per TU; 5 subsystems = 5000/s | Single global limiter caps total at 1000/s, protecting the log system |
| Per-second limiter used in a template/inline function | Every instantiation gets its own counter (correct for per-call-site) | Same (correct) — no change needed |

The `LOG_GLOBAL_LIMIT` name is misleading if it doesn't provide global scope. An ops engineer configuring `LOG_GLOBAL_LIMIT(root, 100, "flood")` expects **at most 100 logs per second globally**, not 100 per call site.

---

## Verification

1. **Code review**: Confirm `GetGlobalRateLimiter()` is defined in `RateLimited.cpp` (not inline in the header).
2. **Unit test**: Create two test functions in separate `.cpp` files that both call `LOG_GLOBAL_LIMIT(root, 5, ...)` rapidly (100 iterations each in a tight loop). Verify total output across both call sites is ≤ 5 per second.
3. **Static analysis**: Use `dumpbin /SYMBOLS RateLimited.obj` to confirm `GetGlobalRateLimiter` is defined in exactly one object file.
4. **Build check**: Verify that `RateLimited.cpp` is included in the `.vcxproj` and compiled into the final binary. Confirm no linker errors for multiple definitions.
