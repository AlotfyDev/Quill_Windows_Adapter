# M06 — Custom PatternFormatter per Sink

- **Priority**: 🟡 Medium
- **Est. Effort**: 1-2 hours
- **Depends on**: None

---

## Problem

All loggers currently use Quill's default format pattern. There is no way to customize:

- Timestamp format (e.g., `%H:%M:%S` vs `%Y-%m-%d %H:%M:%S.%Qms`)
- Timezone (Local vs UTC)
- Which fields appear (thread_id, file_name, logger name, etc.)
- Per-sink formatting (console vs file may need different patterns)

Quill's `Frontend::create_or_get_logger()` accepts a `PatternFormatterOptions` parameter that controls all of this.

---

## Implementation

### Step 1 — Add PatternFormatterOptions to LoggingConfig

**File**: `Logger_Adapter/logging/LoggingConfig.hpp`

```cpp
struct PatternConfig {
    std::string format = "%(time) [%(thread_id)] %(short_source_location:<28) LOG_%(log_level:<9) %(logger:<12) %(message)";
    std::string timestamp = "%H:%M:%S.%Qns";
    bool utc = false;  // false = LocalTime, true = UTC
};

struct LoggingConfig {
    // ...
    PatternConfig console_pattern;
    PatternConfig file_pattern;
    PatternConfig json_pattern;  // usually minimal for JSON
};
```

### Step 2 — Pass to create_or_get_logger

**File**: `Logger_Adapter/logging/LoggerSetup.hpp`

```cpp
quill::PatternFormatterOptions make_pattern(PatternConfig const& cfg) {
    quill::PatternFormatterOptions opts;
    opts.format_pattern = cfg.format;
    opts.timestamp_pattern = cfg.timestamp;
    opts.timestamp_timezone = cfg.utc ? quill::Timezone::GmtTime : quill::Timezone::LocalTime;
    return opts;
}

// For each named logger:
auto pattern = make_pattern(entry.pattern_override);  // or default based on sink type
auto* logger = quill::Frontend::create_or_get_logger(entry.name, sinks, pattern);
```

Note: `PatternFormatterOptions` is per-logger, not per-sink. To have truly per-sink formatting, create separate loggers for each sink combination.

---

## Acceptance Criteria

- [ ] Console logger shows timestamps in `%H:%M:%S` format when configured
- [ ] File logger shows UTC timestamps when `utc=true`
- [ ] Per-logger pattern override works
- [ ] Build succeeds Debug|x64
