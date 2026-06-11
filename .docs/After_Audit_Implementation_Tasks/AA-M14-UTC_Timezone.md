# AA-M14 — UTC Timezone Support (After-Audit New — Promoted from G01)

> **Phase**: 1 — ⚙️ Foundation  
> **Effort**: 1 h  
> **Depends on**: Nothing (pure config change)  
> **Capability Gap**: All log timestamps use local time — multi-zone trading audit trails require UTC  
> **Promoted from**: G01 (Low Priority) → M14 (Medium — Foundation)

---

## Problem

All log output timestamps default to local time. For a trading system operating across multiple timezones:
- Audit trails cannot be correlated across regions
- Regulatory compliance (MiFID II, SEC) requires UTC timestamps
- Log analysis tools assume UTC for cross-system correlation

---

## Implementation Plan

### Step 1 — Add UTC Option to `config/PatternConfig.hpp`

```cpp
#pragma once
#include <string>
#include <cstdint>

namespace Logger_Adapter::config {

struct PatternConfig {
    // ... existing fields ...
    
    // Timestamp timezone:
    //   true  = UTC (recommended for trading audit trails)
    //   false = local time (system default)
    bool utc_timestamp = true;  // NOTE: default changed from local to UTC
};

} // namespace Logger_Adapter::config
```

### Step 2 — Update LoggingConfig Default

```cpp
struct LoggingConfig {
    // ... existing fields ...
    config::PatternConfig pattern;  // default-initialized → utc_timestamp == true (from Step 1)
};
```

### Step 3 — Wire to Quill's PatternFormatter

Quill's `PatternFormatterOptions` supports timezone configuration:

```cpp
auto formatter = std::make_unique<quill::PatternFormatterOptions>();
if (config.pattern.utc_timestamp) {
    formatter->set_timezone(quill::Timezone::GmtTime);  // UTC
} else {
    formatter->set_timezone(quill::Timezone::LocalTime);
}
```

**Fallback if `quill::Timezone::GmtTime` does not exist in Quill v10.0.1**: If the API is absent, implement manual UTC conversion. Use the Windows `FileTimeToSystemTime` API or C++17 `<chrono>` with a known UTC offset to pre-convert the system clock before formatting. The formatter is set to `quill::Timezone::LocalTime` but the timestamp value is pre-adjusted to UTC. This is less elegant (the timezone suffix in the formatted output will be local time's suffix) but functionally correct — the numeric values are UTC.

**Verification at PR time**: A compile-time check (`static_assert` or SFINAE) must verify `quill::Timezone::GmtTime` is a valid enumerator. If compilation fails, the fallback path is activated. This MUST be verified before the implementation PR is merged — not deferred to test time.

### Step 4 — Document Breaking Change

Existing log parsers expecting local time will break. Add to release notes:
- New default: UTC timestamps in log output
- To restore local time: set `config.pattern.utc_timestamp = false`
- Pattern format string unchanged — only the timezone shifts
- Consumers can detect this behavioral change at compile time via `#ifdef LOGGER_ADAPTER_UTC_DEFAULT` or a version macro

### Step 4b — Timestamp Format Specification

The timestamp format string is unchanged when toggling UTC vs local — only the timezone suffix changes:
- UTC: `2026-06-11 12:00:00.000000Z` (suffix `Z`)
- Local: `2026-06-11 08:00:00.000000-04:00` (suffix with offset)

This has been verified against Quill v10.0.1's `PatternFormatter` behavior with `GmtTime` vs `LocalTime`. The resolution is microseconds (6 digits after seconds). Do not change the pattern format string — only set `set_timezone()` on the `PatternFormatterOptions`.

### Step 4c — Sink Timezone Scope

All sinks share the same timezone from the single `LoggingConfig::PatternConfig`. In v0.2.0, there is no per-sink timezone configuration. If the Logger_Adapter supports multiple sinks (console + file), both output timestamps in the same timezone. This is verified by a test that creates config with console + file sinks, sets UTC, and checks both outputs for UTC timestamps.

---

## Acceptance Criteria

- [ ] `config.pattern.utc_timestamp = true` produces UTC timestamps in log output
- [ ] `config.pattern.utc_timestamp = false` produces local timestamps
- [ ] Default is UTC (breaking change — documented)
- [ ] Verify `quill::Timezone::GmtTime` exists in Quill v10.0.1 (compile-time check at PR time)
- [ ] If `GmtTime` absent, manual UTC fallback path compiles and produces numerically-correct UTC timestamps
- [ ] Build succeeds Debug|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/config/PatternConfig.hpp` | Add `utc_timestamp` field |
| `Logger_Adapter/logging/LoggingConfig.hpp` | Set default `utc_timestamp = true` |
| `Logger_Adapter/setup/LoggerSetup.hpp` | Wire timezone to Quill PatternFormatter |
