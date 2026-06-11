# AA-M04 — LOGJ Structured Logging (After-Audit Corrected)

> **Phase**: 4 — 🧩 Macros & Sinks  
> **Effort**: 1-1.5 h  
> **Depends on**: AA-C01 (named loggers); audit existing `StructuredLogger.hpp` first  
> **v1.x Reference**: TASK-M04-LOGJ_StructuredLogging.md  
> **Audit Issues**: M04-A (existing StructuredLogger.hpp), M04-B (JSON schema), M04-C (hot-path JSON performance)
> **Fix Applied**: REV-M04-LOGJ_PerformanceRationale.md — corrected performance rationale (LOGJ defers JSON formatting to backend; frontend overhead = LOG)

---

## Problem

Logger_Adapter needs macros for structured JSON logging. The existing `StructuredLogger.hpp` is 2285 bytes — it may already implement features, or may conflict with Quill's `JsonFileSink`.

---

## Corrected Implementation Plan

### Step 0 — Audit Existing `StructuredLogger.hpp`

Before implementing, read and document what `StructuredLogger.hpp` already does:
- Does it use `JsonFileSink` or its own serialization?
- Are there any LOGJ-like macros already defined?
- Does it conflict with Quill's deferred JSON approach?

### Step 1 — Populate `macros/Structured.hpp` (was stub)

```cpp
#pragma once
#include <quill/LogMacros.h>

// LOGJ macros — write structured JSON log entries using Quill's native
// QUILL_LOGJ_* macros (deferred JSON formatting).
//
// PERFORMANCE: LOGJ macros use QUILL_LOGJ_* which produce named-format strings
// ("text {name}") at COMPILE TIME via QUILL_GENERATE_NAMED_FORMAT_STRING.
// At runtime, the frontend code path is identical to QUILL_LOG_* — just a level
// check and argument copy into the SPSC queue. NO JSON serialization on the
// frontend thread.
//
// JSON formatting is DEFERRED to the backend thread inside JsonFileSink::write().
// This means:
//   - Frontend overhead of LOGJ = same as LOG (zero JSON cost on calling thread)
//   - Backend thread does JSON string escaping, key-value formatting, and output
//   - LOGJ entries consume more queue space than LOG (named format string is longer)
//   - Backend CPU utilization is higher for LOGJ vs LOG output
//
// GUIDELINE: Use LOGJ for audit trails, compliance logs, and moderate-volume
// structured output. For high-frequency trading paths, prefer LOG with plaintext
// format to reduce backend serialization load and avoid queue pressure.
//
// Verified against Quill v10.0.1: QUILL_LOGJ_INFO at LogMacros.h:646 calls
// QUILL_LOGGER_CALL with QUILL_GENERATE_NAMED_FORMAT_STRING — same call path
// as QUILL_LOG_INFO at LogMacros.h:618 with QUILL_GENERATE_FORMAT_STRING.
//
// ⚠️ Uses QUILL_LOGJ_* macros (not QUILL_LOG_*) — plain-text QUILL_LOG_*
//    would NOT produce JSON output. Verified against Quill v10.0.1 API.
//
#define LOGJ_TRACE(logger, fmt, ...)   QUILL_LOGJ_TRACE_L3(logger, fmt, ##__VA_ARGS__)
#define LOGJ_DEBUG(logger, fmt, ...)   QUILL_LOGJ_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOGJ_INFO(logger, fmt, ...)    QUILL_LOGJ_INFO(logger, fmt, ##__VA_ARGS__)
#define LOGJ_WARN(logger, fmt, ...)    QUILL_LOGJ_WARNING(logger, fmt, ##__VA_ARGS__)
#define LOGJ_ERR(logger, fmt, ...)     QUILL_LOGJ_ERROR(logger, fmt, ##__VA_ARGS__)
#define LOGJ_CRIT(logger, fmt, ...)    QUILL_LOGJ_CRITICAL(logger, fmt, ##__VA_ARGS__)
```

### Step 2 — Define JSON Schema

Minimum required fields for all JSON log entries:

```json
{
  "timestamp": "2026-06-11T14:30:00.000Z",
  "level": "INFO",
  "logger": "OrderExecution",
  "thread_id": 12345,
  "file": "OrderManager.cpp",
  "line": 42,
  "message": "Order placed",
  // Optional fields:
  "order_id": 12345,
  "symbol": "AAPL",
  "price": 150.25
}
```

Document this schema alongside the implementation.

### Step 3 — Wire via existing JsonFileSink (Quill)

Quill's `JsonFileSink` already handles structured JSON output. The LOGJ macros just provide a semantic alias — they delegate to Quill's JSON macros.

---

## Acceptance Criteria

- [ ] Existing `StructuredLogger.hpp` is audited and findings documented
- [ ] `LOGJ_INFO(logger, ...)` produces valid JSON output
- [ ] JSON schema is documented alongside the implementation
- [ ] Performance rationale correctly describes deferred JSON on backend (frontend overhead = LOG, not extra)
- [ ] Performance warning documented (NOT for hot path, but for backend serialization load, not frontend CPU cost)
- [ ] Build succeeds Debug|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/macros/Structured.hpp` | Populate from stub |
| `Logger_Adapter/logging/StructuredLogger.hpp` | Audit — possibly refactor or keep as-is |