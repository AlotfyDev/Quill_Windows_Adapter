# C03 — Backtrace Logging

- **Priority**: 🔴 Critical
- **Est. Effort**: 2-3 hours
- **Depends on**: C01 (Multi-Logger) — needs named loggers for per-logger backtrace config

---

## Problem

When a critical error occurs (e.g., order rejection, connection drop, risk limit breach), the most important diagnostic information is **what happened in the seconds before the error**. Quill provides a ring-buffer backtrace mechanism:

- `Logger::init_backtrace(max_capacity, flush_level)` — allocate a ring buffer that stores the last N messages
- `LOG_BACKTRACE(logger, ...)` — write to the ring buffer only (not to output)
- `Logger::flush_backtrace()` — dump all buffered messages to the real sinks
- If a message at `flush_level` or higher is logged, the backtrace **auto-flushes**

Logger_Adapter exposes none of this.

Without backtrace:
- A crash handler can only say "SEGV at 0x7FF..." with no context
- An order rejection produces one ERROR line with no preceding state
- Debugging a trading incident requires reproducing the exact sequence

---

## Current Behavior

No `init_backtrace()`, `flush_backtrace()`, or `LOG_BACKTRACE` wrapper exists.

## Desired Behavior

```cpp
auto* order_log = GetLogger("OrderExecution");

// During initialization:
order_log->init_backtrace(256, quill::LogLevel::Error);
// Stores last 256 LOG_BACKTRACE messages; auto-flushes when any ERROR+ is logged

// During hot path:
LOG_BACKTRACE(order_log, "Processing order {} for symbol {}", order_id, symbol);

// If ERROR occurs — backtrace auto-flushes:
LOG_ERROR(order_log, "Order {} rejected: insufficient margin", order_id);
// Output: 256 backtrace lines + the ERROR line
```

---

## Implementation Plan

### Step 1 — Add BacktraceConfig to LoggingConfig

**File**: `Logger_Adapter/logging/LoggingConfig.hpp`

```cpp
struct BacktraceConfig {
    bool enabled = false;
    uint32_t capacity = 256;
    int flush_on_level = 7; // Error (0=TraceL3...7=Error...8=Critical)
};

// Inside LoggerEntry:
struct LoggerEntry {
    // ...existing...
    BacktraceConfig backtrace;
};
```

Default config for critical subsystems:

```cpp
std::vector<LoggerEntry> loggers = {
    {"Emergency",     3, true,  true,  true,  BacktraceConfig{true,  512, 7}},
    {"OrderExecution",3, true,  true,  false, BacktraceConfig{true,  256, 7}},
    {"Risk",          4, true,  true,  false, BacktraceConfig{true,  128, 6}},
    {"MarketData",    3, true,  false, false, BacktraceConfig{false, 0,   0}},
    {"HealthProbe",   4, true,  false, false, BacktraceConfig{false, 0,   0}},
};
```

### Step 2 — Initialize backtrace in LoggerSetup

**File**: `Logger_Adapter/logging/LoggerSetup.hpp`

After creating each named logger:

```cpp
if (entry.backtrace.enabled) {
    auto flush_level = ToQuillLogLevel(entry.backtrace.flush_on_level);
    logger->init_backtrace(entry.backtrace.capacity, flush_level);
}
```

### Step 3 — Add LOG_BACKTRACE and LOG_BACKTRACE macros

**File**: `Logger_Adapter/logging/HotPathLogger.hpp`

```cpp
#define LOG_BACKTRACE(logger, fmt, ...)  QUILL_LOG_BACKTRACE(logger, fmt, ##__VA_ARGS__)
```

Also function template:

```cpp
template <typename... Args>
inline void LogBacktrace(quill::Logger* logger, char const* fmt, Args&&... args)
{
    QUILL_LOG_BACKTRACE(logger, fmt, std::forward<Args>(args)...);
}
```

### Step 4 — Add manual flush_backtrace API

**File**: `Logger_Adapter/logging/LoggerSetup.hpp`

```cpp
inline void FlushBacktrace(quill::Logger* logger)
{
    logger->flush_backtrace();
}
```

### Step 5 — Wire backtrace flush into EmergencyManager::FatalError

**File**: `Logger_Adapter/emergency/EmergencyManager.hpp`

Before calling `abort()` or `exit()`, flush the backtrace:

```cpp
if (auto* logger = quill::Frontend::get_logger(config_.emergency_logger_name)) {
    logger->flush_backtrace();
}
```

---

## Acceptance Criteria

- [ ] `LOG_BACKTRACE` messages do **not** appear in output until backtrace is flushed
- [ ] After `LOG_ERROR` on a logger with `flush_on_level=Error`, all preceding `LOG_BACKTRACE` messages appear in the output **before** the ERROR line
- [ ] `FlushBacktrace(logger)` manually triggers backtrace dump
- [ ] `EmergencyManager::FatalError` flushes backtrace before abort
- [ ] Backtrace capacity is honored (only last N messages stored)
- [ ] Build succeeds Debug|x64

---

## Testing

```cpp
auto* logger = GetLogger("TestBacktrace", LogLevel::TraceL3);
logger->init_backtrace(4, quill::LogLevel::Warning);

LOG_BACKTRACE(logger, "step 1");
LOG_BACKTRACE(logger, "step 2");
LOG_BACKTRACE(logger, "step 3");
LOG_BACKTRACE(logger, "step 4");
LOG_BACKTRACE(logger, "step 5"); // evicts "step 1"

LOG_WARNING(logger, "trigger flush");
// Output should show: step 2, step 3, step 4, step 5, trigger flush
```
