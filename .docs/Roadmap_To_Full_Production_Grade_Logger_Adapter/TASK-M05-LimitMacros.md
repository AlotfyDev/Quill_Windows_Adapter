# M05 — Rate-Limited Macros

- **Priority**: 🟡 Medium
- **Est. Effort**: 1 hour
- **Depends on**: None

---

## Problem

In a trading system, certain events can produce log floods:
- Market data ticks at 10,000 msg/sec
- Connection retry every 100ms during an outage
- HealthProbe checks every second

Without rate limiting, these can:
- Saturate the SPSC queue and block backpressure
- Fill disk with repetitive messages
- Make it impossible to find the actual error among the noise

---

## Implementation

**File**: `Logger_Adapter/logging/HotPathLogger.hpp`

Add wrappers for rate-limited macros:

```cpp
// Limit: max once per min_interval
#define LOG_INFO_LIMIT(min_interval, logger, fmt, ...) \
    QUILL_LOG_INFO_LIMIT(min_interval, logger, fmt, ##__VA_ARGS__)

// Every N: log once every n_occurrences calls
#define LOG_INFO_EVERY_N(n_occurrences, logger, fmt, ...) \
    QUILL_LOG_INFO_LIMIT_EVERY_N(n_occurrences, logger, fmt, ##__VA_ARGS__)
```

Repeat for all 6 levels: `LOG_TRACE_LIMIT`, `LOG_DEBUG_LIMIT`, etc.

Also add function template wrappers:

```cpp
template <typename... Args>
inline void LogInfoLimit(std::chrono::seconds min_interval, quill::Logger* logger,
                         char const* fmt, Args&&... args)
{
    QUILL_LOG_INFO_LIMIT(min_interval, logger, fmt, std::forward<Args>(args)...);
}
```

---

## Acceptance Criteria

- [ ] `LOG_INFO_LIMIT(100ms, logger, "tick")` in a tight loop prints at most 1 message per 100ms
- [ ] `LOG_INFO_EVERY_N(100, logger, "msg")` in a loop of 1000 prints exactly 10 messages
- [ ] Build succeeds Debug|x64
