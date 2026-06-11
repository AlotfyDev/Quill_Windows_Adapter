# AA-M06 — Custom Pattern Per Sink (After-Audit Corrected)

> **Phase**: 4 — 🧩 Macros & Sinks  
> **Effort**: 1-2 h  
> **Depends on**: AA-C01 (logger creation via SinkFactory)  
> **v1.x Reference**: TASK-M06-CustomPatternPerSink.md  
> **Audit Issues**: M06-A (Quill per-logger limitation) ✅, M06-B (no standard pattern) ✅, M06-C (silent parse errors) ✅ (fix applied)
> **Fix Applied**: REV-M06-CustomPattern_ValidationLogic.md — added pattern validation function, error handling, and Quill behavior documentation

---

## Problem

All logs use Quill's default format. No way to customize timestamp format, timezone, or field selection per sink type.

**Quill limitation**: `PatternFormatterOptions` is per-logger, not per-sink. Workaround: create separate loggers with different patterns.

> **Design Tradeoff**: Quill also supports per-sink pattern override via `FileSinkConfig::set_override_pattern_formatter_options(...)` and `ConsoleSinkConfig::set_override_pattern_formatter_options(...)`. The current AA approach (per-logger patterns via `create_or_get_logger`) is simpler to implement and avoids sink-config mutation. Re-evaluate for per-sink overrides in a future iteration if more granular control is needed.

---

## Corrected Implementation Plan

### Step 1 — Publish Standard Logger_Adapter Pattern Grammar

```cpp
// Standard patterns:
//   Console:    %(time) [%(log_level)] %(logger) %(message)   (human-readable)
//   File:       %(time) [%(log_level)] [%(thread_id)] %(file_name):%(line_number) %(message)  (detailed)
//   JSON:       (handled by JsonFileSink — no pattern needed)

// Pattern tokens (Quill v10.0.1 — PatternFormatterOptions.h):
//   %(time)                  → timestamp (default: %H:%M:%S.%Qns)
//   %(log_level)             → log level name (e.g., INFO, ERROR)
//   %(log_level_short_code)  → abbreviated log level
//   %(logger)                → logger name
//   %(thread_id)             → thread ID
//   %(thread_name)           → thread name (must be set before first log)
//   %(file_name)             → source file name
//   %(full_path)             → full source file path
//   %(line_number)           → source line number
//   %(message)               → log message
//   %(caller_function)       → calling function name
//   %(process_id)            → process ID
//   %(source_location)       → "file:line" as single string
//   %(short_source_location) → shortened "file:line"
//   %(tags)                  → custom tags (when _TAGS macros used)
//   %(named_args)            → key-value pairs (LOGV output)
```

### Step 2 — Populate `config/PatternConfig.hpp` (was stub)

```cpp
#pragma once
#include <string>

namespace Logger_Adapter::config {

struct PatternConfig {
    std::string format;      // pattern string (e.g., "%(time) [%(log_level)] %(message)")
    std::string timestamp_format = "%H:%M:%S.%Qns";  // Quill timestamp format (ns precision)
    bool utc = true;          // UTC vs local timezone
};

} // namespace Logger_Adapter::config
```

### Step 3 — Add PatternConfig to LoggingConfig

```cpp
struct LoggingConfig {
    // ... existing fields ...
    config::PatternConfig console_pattern;      // default: human-readable
    config::PatternConfig file_pattern;         // default: detailed
    // _json pattern not needed — JsonFileSink uses its own format
};
```

### Step 4 — Create make_pattern helper + validation (FIXED: M06-C validation implemented)

```cpp
// In LoggerSetup.hpp or a new helper:
#include <quill/core/PatternFormatterOptions.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Logger_Adapter::config {

// Accepted pattern tokens (Quill v10.0.1 — PatternFormatterOptions.h:47-62)
inline const std::unordered_set<std::string_view> kValidPatternTokens = {
    "%(time)", "%(log_level)", "%(log_level_short_code)", "%(logger)",
    "%(thread_id)", "%(thread_name)", "%(file_name)", "%(full_path)",
    "%(line_number)", "%(message)", "%(caller_function)", "%(process_id)",
    "%(source_location)", "%(short_source_location)", "%(tags)", "%(named_args)",
};

// Validate a pattern string against known tokens.
// Returns a descriptive error message on failure, empty string on success.
inline std::string ValidatePattern(const std::string& format) {
    if (format.empty()) {
        return "pattern format string must not be empty";
    }

    // Scan for %(...) tokens and validate each one
    std::size_t pos = 0;
    while (pos < format.size()) {
        auto pct = format.find('%', pos);
        if (pct == std::string::npos) break;

        auto open = format.find('(', pct);
        if (open == std::string::npos || open != pct + 1) {
            ++pos;  // literal '%' or malformed, continue
            continue;
        }

        auto close = format.find(')', open);
        if (close == std::string::npos) {
            return "unterminated pattern token starting at position "
                   + std::to_string(pct);
        }

        std::string_view token(format.data() + pct, close - pct + 1);
        if (kValidPatternTokens.find(token) == kValidPatternTokens.end()) {
            return "unknown pattern token '" + std::string(token)
                   + "' at position " + std::to_string(pct);
        }

        pos = close + 1;
    }

    return {};  // valid
}

// Quill behavior notes for invalid patterns (v10.0.1):
// - set_pattern() with an invalid token: Quill will NOT error at call time.
//   The invalid token is rendered literally (e.g., "%(bad)" appears as text).
//   This is the "silent parse" behavior M06-C aims to eliminate.
// - set_timestamp_format() with invalid specifiers: behavior depends on the
//   underlying format library; typically produces garbage or throws std::format_error.
// - Empty format string: Quill uses default pattern — not necessarily wrong,
//   but may surprise callers expecting custom output.

} // namespace Logger_Adapter::config

quill::PatternFormatterOptions MakePatternFormatter(const config::PatternConfig& cfg) {
    quill::PatternFormatterOptions opts;

    if (!cfg.format.empty()) {
        // VALIDATE before passing to Quill
        std::string error = ValidatePattern(cfg.format);
        if (!error.empty()) {
            throw std::invalid_argument(
                "Logger_Adapter::MakePatternFormatter: " + error);
        }
        opts.format_pattern = cfg.format;
    }
    // else: empty pattern is allowed — Quill uses default format

    if (!cfg.timestamp_format.empty()) {
        // Note: Quill's timestamp_pattern does NOT validate format specifiers.
        // Invalid specifiers produce runtime errors from the formatting library.
        // Consider adding a try-catch around Quill's internal formatting call
        // or validating timestamp format separately.
        opts.timestamp_pattern = cfg.timestamp_format;
    }

    // PatternFormatterOptions uses public member variables, not setter methods
    opts.timestamp_timezone = cfg.utc ? quill::Timezone::GmtTime : quill::Timezone::LocalTime;
    return opts;
}
```

### Step 5 — Wire in InitializeLogging

Pass the pattern when creating each logger, with error handling for invalid patterns:

```cpp
// For each named logger:
try {
    quill::PatternFormatterOptions pattern;
    if (entry.name == "console") {
        pattern = MakePatternFormatter(config.console_pattern);
    } else {
        pattern = MakePatternFormatter(config.file_pattern);
    }
    auto* logger = quill::Frontend::create_or_get_logger(entry.name, sinks, pattern);
} catch (const std::invalid_argument& e) {
    // Pattern validation failed — log via OutputDebugString and fall back to default
    OutputDebugStringA(e.what());
    // Use default pattern as fallback
    auto* logger = quill::Frontend::create_or_get_logger(entry.name, sinks);
}
```

---

## Acceptance Criteria

- [ ] Console and file sinks use different patterns
- [ ] UTC timestamp option works independently per pattern
- [ ] Invalid pattern string produces clear error at initialization (via `std::invalid_argument`)
- [ ] Invalid tokens are caught and reported with positions by `ValidatePattern()`
- [ ] Empty pattern uses Quill default (not silently accepted as custom)
- [ ] Pattern grammar documented (what tokens are supported) — completed in Step 1
- [ ] Timestamp format validation deferred to Quill (future: validate at call time)
- [ ] Backward compatible: default patterns match previous behavior
- [ ] Build succeeds Debug|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/config/PatternConfig.hpp` | Populate from stub |
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add PatternConfig fields for console + file |
| `Logger_Adapter/logging/LoggerSetup.hpp` | Add MakePatternFormatter helper; wire into logger creation |