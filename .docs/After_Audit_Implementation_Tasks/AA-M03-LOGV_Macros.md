# AA-M03 — LOGV Variable-Name Macros (After-Audit Corrected)

> **Phase**: 4 — 🧩 Macros & Sinks  
> **Effort**: 30 min  
> **Depends on**: AA-C01 (named loggers)  
> **v1.x Reference**: TASK-M03-LOGV_Macros.md  
> **Audit Issues**: M03-A (hot path warning), M03-B (test coverage)

---

## Problem

Quill supports `LOGV_INFO(logger, "count", count, "price", price)` macros that log key-value pairs with variable names. Logger_Adapter doesn't expose these.

---

## Corrected Implementation Plan

### Step 1 — Populate `macros/VariableArgs.hpp` (was stub)

```cpp
#pragma once
#include <quill/LogMacros.h>

// LOGV macros — structured key-value logging with variable names.
//
// LOGV uses #var preprocessor stringification: pass BARE IDENTIFIERS, not
// string literals.  For example:
//   int order_id = 12345;
//   LOGV_INFO(logger, "trade", order_id, price);  // ✓ correct
// Passing "order_id" would produce escaped quotes in the key name.
//
// PERFORMANCE: These macros generate named-key-value pairs in Quill's
// internal metadata. For DIAGNOSTIC USE ONLY — NOT for the high-frequency
// trading hot path (market data ticks, order placement callbacks).
//
// Hot path: use basic LOG_INFO/LOG_DEBUG instead.
//
// NOTE: Quill's LOGV family uses QUILL_LOGV_* macros, NOT QUILL_LOG_*.
// Using QUILL_LOG_INFO for LOGV would treat the first argument as a format string,
// producing garbled output instead of structured key-value pairs.
//
// NOTE: If QUILL_DISABLE_NON_PREFIXED_MACROS is NOT defined, Quill already
// provides LOGV_INFO, LOGV_DEBUG, etc. directly via LogMacros.h.  These
// wrappers are only needed when the non-prefixed macros are disabled, or
// when a shortened naming convention (LOGV_ERR vs LOGV_ERROR) is desired.
//
#define LOGV_TRACE(logger, ...)   QUILL_LOGV_TRACE_L3(logger, ##__VA_ARGS__)
#define LOGV_DEBUG(logger, ...)   QUILL_LOGV_DEBUG(logger, ##__VA_ARGS__)
#define LOGV_INFO(logger, ...)    QUILL_LOGV_INFO(logger, ##__VA_ARGS__)
#define LOGV_WARN(logger, ...)    QUILL_LOGV_WARNING(logger, ##__VA_ARGS__)
#define LOGV_ERR(logger, ...)     QUILL_LOGV_ERROR(logger, ##__VA_ARGS__)
#define LOGV_CRIT(logger, ...)    QUILL_LOGV_CRITICAL(logger, ##__VA_ARGS__)
```

### Step 2 — Add Test in Experimental_Console

```cpp
// LOGV test:
// LOGV uses #var stringification — pass bare identifiers, NOT string literals.
// Quill v10.0.1 generates format: description [var_name: value, ...]
int order_id = 12345;
double price = 150.25;
LOGV_INFO(order_log, "trade", order_id, price);
// Output: trade [order_id: 12345, price: 150.25]
// (preceded by the default pattern prefix: timestamp [thread] source LOG_INFO logger)
```

---

## Acceptance Criteria

- [ ] `LOGV_INFO(order_log, "trade", order_id, price)` outputs `trade [order_id: 12345, price: 150.25]`
- [ ] Performance warning documented in code comments
- [ ] Build succeeds Debug|x64
- [ ] (Future) `LOGV_LIMIT`, `LOGV_BACKTRACE`, `LOGV_TAGS` wrappers — currently use Quill's `LOGV_INFO_LIMIT(...)` or `QUILL_LOGV_*` directly if needed

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/macros/VariableArgs.hpp` | Populate from stub |
| `Experimental_Console/Experimental_Console.cpp` | Add LOGV test |