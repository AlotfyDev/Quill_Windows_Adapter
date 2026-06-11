# M04 — LOGJ Structured Logging (Upgrade to Deferred JSON)

- **Priority**: 🟡 Medium
- **Est. Effort**: 1 hour
- **Depends on**: None

---

## Problem

Current `LogStructured` builds a JSON **string at runtime in the hot path**, then passes it as a `{}` format argument:

```cpp
LogStructured(logger, LogLevel::Info, "OrderExec",
    "order_id", order_id, "symbol", symbol);
// Builds: {"event":"OrderExec","order_id":"42","symbol":"AAPL"}
// Then: QUILL_LOG_INFO(logger, "{}", json_string)
```

Problems:
1. **Hot path overhead** — JSON stringification happens on the calling thread
2. **Double encoding** — On a `JsonFileSink`, the resulting file has JSON-escaped JSON strings
3. **No deferred formatting** — Cannot leverage Quill's backend formatting

Quill's `LOGJ` macros accept named arguments and defer formatting to the backend thread:

```cpp
LOGJ_INFO(logger, order_id, symbol);
// Backend formats: {"order_id":"42","symbol":"AAPL"}
```

---

## Implementation

### Step 1 — Add LOGJ macro wrappers

**File**: `Logger_Adapter/logging/HotPathLogger.hpp`

```cpp
#define LOGJ_TRACE(logger, ...)  QUILL_LOGJ_TRACE_L3(logger, ##__VA_ARGS__)
#define LOGJ_DEBUG(logger, ...)  QUILL_LOGJ_DEBUG(logger, ##__VA_ARGS__)
#define LOGJ_INFO(logger, ...)   QUILL_LOGJ_INFO(logger, ##__VA_ARGS__)
#define LOGJ_WARN(logger, ...)   QUILL_LOGJ_WARNING(logger, ##__VA_ARGS__)
#define LOGJ_ERR(logger, ...)    QUILL_LOGJ_ERROR(logger, ##__VA_ARGS__)
#define LOGJ_CRIT(logger, ...)   QUILL_LOGJ_CRITICAL(logger, ##__VA_ARGS__)
```

### Step 2 — Refactor LogStructured (optional)

`LogStructured` can be kept for backward compatibility, but mark it as deprecated in favor of `LOGJ_*`. Alternatively, rewrite `LogStructured` to use `LOGJ_CRITICAL` internally for deferred formatting.

---

## Acceptance Criteria

- [ ] `LOGJ_INFO(logger, x, y)` produces JSON output without double encoding
- [ ] When `JsonFileSink` is active, LOGJ output is valid parseable JSON
- [ ] `LogStructured` still compiles (backward compat)
- [ ] Build succeeds Debug|x64
