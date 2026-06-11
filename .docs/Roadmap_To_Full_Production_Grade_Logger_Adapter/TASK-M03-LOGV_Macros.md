# M03 — LOGV Macros

- **Priority**: 🟡 Medium
- **Est. Effort**: 30 minutes
- **Depends on**: None

---

## Problem

Current log calls require manual format strings:

```cpp
LOG_INFO(logger, "order_id={}, symbol={}", order_id, symbol);
```

Quill's `LOGV` macros automatically capture variable names at compile time:

```cpp
LOGV_INFO(logger, order_id, symbol);
// Output: "order_id=42, symbol=AAPL"
```

This reduces boilerplate and ensures the format string is always in sync with the variables.

---

## Implementation

**File**: `Logger_Adapter/logging/HotPathLogger.hpp`

Add wrappers for the 6 standard levels:

```cpp
#define LOGV_TRACE(logger, ...)  QUILL_LOGV_TRACE_L3(logger, ##__VA_ARGS__)
#define LOGV_DEBUG(logger, ...)  QUILL_LOGV_DEBUG(logger, ##__VA_ARGS__)
#define LOGV_INFO(logger, ...)   QUILL_LOGV_INFO(logger, ##__VA_ARGS__)
#define LOGV_WARN(logger, ...)   QUILL_LOGV_WARNING(logger, ##__VA_ARGS__)
#define LOGV_ERR(logger, ...)    QUILL_LOGV_ERROR(logger, ##__VA_ARGS__)
#define LOGV_CRIT(logger, ...)   QUILL_LOGV_CRITICAL(logger, ##__VA_ARGS__)
```

Also add function template variants:

```cpp
template <typename... Args>
inline void LogVInfo(quill::Logger* logger, Args&&... args)
{
    QUILL_LOGV_INFO(logger, std::forward<Args>(args)...);
}
```

---

## Acceptance Criteria

- [ ] `LOGV_INFO(logger, x, y)` produces output `x=42, y=hello`
- [ ] No compile-time overhead for unused variables
- [ ] Build succeeds Debug|x64
