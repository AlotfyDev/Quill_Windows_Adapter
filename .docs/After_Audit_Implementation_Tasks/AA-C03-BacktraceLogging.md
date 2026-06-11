# AA-C03 — Backtrace Logging (After-Audit Corrected)

> **Phase**: 3 — 🏗️ Core Infrastructure  
> **Effort**: 2-3 h  
> **Depends on**: AA-C01 (named loggers)  
> **v1.x Reference**: TASK-C03-BacktraceLogging.md  
> **Audit Issues**: C03-A (C01 dependency), C03-B (stub file), C03-C (shutdown flush), C03-D (capacity guidance), C03-E (allocation semantics)

---

## Problem

When a critical error occurs, the most important diagnostic information is what happened in the seconds before. Quill provides a ring-buffer backtrace mechanism, but Logger_Adapter exposes none of it.

---

## Corrected Implementation Plan

### Step 1 — Populate `config/BacktraceConfig.hpp` (was stub)

```cpp
#pragma once
#include <cstdint>

namespace Logger_Adapter::config {

struct BacktraceConfig {
    bool enabled = false;
    
    // Ring buffer capacity.
    //   Emergency: 5000 (full trade lifecycle)
    //   HealthProbe: 100 (heartbeats only)
    //   Trading subsystems: 1000
    //   Default: 1000
    uint32_t capacity = 1000;
    
    // Messages at this level and above auto-flush the backtrace.
    // Default: Error (6)
    uint32_t flush_on_level = 6;
};

} // namespace Logger_Adapter::config
```

### Step 2 — Populate `macros/Backtrace.hpp` (was stub)

```cpp
#pragma once
#include <quill/LogMacros.h>
#include <quill/Logger.h>

// Log a message to the backtrace ring buffer (not visible until flush).
// SAFETY: init_backtrace() must have been called on the logger before use.
//         Without init_backtrace(), behavior is undefined (Quill may no-op
//         or assert). Always pair with LoggerEntry::backtrace_enabled = true.
#define LOG_BACKTRACE(logger, fmt, ...) \
    QUILL_LOG_BACKTRACE(logger, fmt, ##__VA_ARGS__)

// Explicitly flush backtrace to output.
// Safe to call even if backtrace was not initialized (no-op).
// After flushing, the ring buffer is cleared by Quill; subsequent
// FLUSH_BACKTRACE calls produce no output until new LOG_BACKTRACE calls.
#define FLUSH_BACKTRACE(logger) \
    logger->flush_backtrace();
```

### Step 3 — Wire into LoggerSetup.hpp InitializeLogging

After creating each named logger in the C01 loop:

```cpp
if (entry.backtrace_enabled) {
    logger->init_backtrace(
        entry.backtrace_capacity,
        ToQuillLogLevel(entry.backtrace_flush_level)
    );
}
```

> **Validation**: `entry.backtrace_flush_level` must be in valid Quill LogLevel range [Trace..Critical]. Invalid values are clamped via `ToQuillLogLevel()`. This is implemented inside `InitializeLogging()` — no separate validation function is needed because the value is clamped at the call site.

### Step 4 — Add Shutdown-Flush Path

In `ShutdownLogging()`:

```cpp
inline void ShutdownLogging() {
    // Flush all backtraces before stopping backend
    auto& reg = setup::LoggerRegistry::Registry();
    for (auto& [name, logger] : reg) {
        if (logger) {
            logger->flush_backtrace();
        }
    }
    quill::Backend::stop();
}
```

> **Note on flush_backtrace semantics**: `flush_backtrace()` is synchronous — it blocks until the flushed content is enqueued in the backend's output queue. It does NOT guarantee the data is written to disk; that durability guarantee comes from `Backend::stop()` which drains the queue. The ring buffer is cleared by Quill after flushing; a subsequent flush produces no duplicate output.

### Step 5 — Add to Experimental_Console test

```cpp
LOG_BACKTRACE(order_log, "Order 12345: placed at price={}", 150.25);
LOG_BACKTRACE(order_log, "Order 12345: acknowledged");
LOG_ERR(order_log, "Order 12345: FILL REJECTED — backtrace auto-flushes above");
```

---

## Acceptance Criteria

- [ ] `LOG_BACKTRACE` messages not visible in output until flush
- [ ] `LOG_ERROR` (or configured `flush_on_level`) auto-flushes backtrace
- [ ] `ShutdownLogging()` flushes all backtraces before stopping backend
- [ ] Backtrace capacity configurable per-logger (Emergency=5000, HealthProbe=100)
- [ ] Invalid `backtrace_flush_level` clamped to valid range via `ToQuillLogLevel()`
- [ ] `FLUSH_BACKTRACE` on uninitialized logger is safe (no-op)
- [ ] Build succeeds Debug|x64 with zero new warnings

---

## Memory Footprint

Each backtrace ring buffer entry is sized by Quill's internal buffer allocation. At configured capacity:

| Logger       | Capacity | Est. Entry Size | Est. Total  |
|--------------|----------|-----------------|-------------|
| Emergency    | 5,000    | ~200 bytes      | ~1,000 KB   |
| OrderExecution | 1,000  | ~200 bytes      | ~200 KB     |
| Risk         | 1,000    | ~200 bytes      | ~200 KB     |
| MarketData   | 1,000    | ~200 bytes      | ~200 KB     |
| HealthProbe  | 100      | ~200 bytes      | ~20 KB      |

Total estimated backtrace memory: ~1,620 KB (all loggers with backtrace enabled). Ring buffer memory is allocated once at initialization from the process heap. The actual entry size depends on Quill's internal format (timestamp, thread ID, log level, message text).

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/config/BacktraceConfig.hpp` | Populate from stub |
| `Logger_Adapter/macros/Backtrace.hpp` | Populate from stub (with safety docs) |
| `Logger_Adapter/logging/LoggerSetup.hpp` | Wire backtrace init in named logger loop; add shutdown-flush |
| `Experimental_Console/Experimental_Console.cpp` | Add backtrace test |