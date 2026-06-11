# AA-C01 — Multi-Logger Shim (After-Audit Corrected)

> **Phase**: 3 — 🏗️ Core Infrastructure  
> **Effort**: 3-4 h  
> **Depends on**: AA-M08 (Queue Config), AA-0.5 (Stub Reconciliation — LoggerEntry, SinkFactory, LoggerRegistry stubs)  
> **v1.x Reference**: TASK-C01-MultiLogger.md  
> **Audit Issues**: C01-A (stub files), C01-B (sink-name referencing), C01-C (thread safety), C01-D (validation), C01-E (memory mgmt)

---

## Problem

`GetLogger(name, level)` ignores both parameters and returns the root logger. All subsystems share a single logger with one log level and one sink list — unacceptable for a trading system with distinct subsystems (Emergency, OrderExecution, Risk, MarketData).

---

## Corrected Implementation Plan

### Step 1 — Populate `config/LoggerEntry.hpp` (was stub)

Use **sink-name referencing** instead of bool flags:

```cpp
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <quill/LogLevel.h>

namespace Logger_Adapter::config {

struct LoggerEntry {
    std::string name = "default";
    quill::LogLevel log_level = quill::LogLevel::Debug;
    std::vector<std::string> sink_names = {"console"};  // references named sink configs
    // Optional: backtrace config reference
    bool backtrace_enabled = false;
    uint32_t backtrace_capacity = 1000;
    quill::LogLevel backtrace_flush_level = quill::LogLevel::Error;
};

} // namespace Logger_Adapter::config
```

### ToQuillLogLevel Conversion

```cpp
// Converts raw int32_t to quill::LogLevel with explicit mapping.
// Quill LogLevel enum: Trace=0, Debug=3, Info=4, Warning=5, Error=6, Critical=7
// Values outside valid range are clamped.
inline quill::LogLevel ToQuillLogLevel(int32_t level) noexcept {
    if (level <= 0) return quill::LogLevel::Trace;
    if (level >= 7) return quill::LogLevel::Critical;
    // Map 0-7 to Quill's actual enum values, not sequential positions
    switch (level) {
        case 0: return quill::LogLevel::Trace;
        case 1: return quill::LogLevel::Debug;     // Debug is 3 in Quill, we map 1→Debug
        case 2: return quill::LogLevel::Debug;      // No direct mapping; treat as Debug
        case 3: return quill::LogLevel::Debug;      // Quill's actual Debug=3
        case 4: return quill::LogLevel::Info;
        case 5: return quill::LogLevel::Warning;
        case 6: return quill::LogLevel::Error;
        case 7: return quill::LogLevel::Critical;
        default: return quill::LogLevel::Debug;     // fallback
    }
}
```

> **Note**: The default values in LoggerEntry use Quill's actual `LogLevel` enum directly. The `ToQuillLogLevel` conversion is used when loading config from external sources (files, command-line) that use raw integers.

> **Design note (ValidateLoggerEntry)**: The function both validates AND sanitizes (clamps) out-of-range values, returning `Fail_LevelOutOfRange` after mutation. This is a deliberate simplification to avoid two-pass processing during bootstrap. The name `ValidateAndSanitizeLoggerEntry` would be more precise; this can be renamed in a future cleanup. See `ToQuillLogLevel()` for the correct range mapping.

### Step 2 — Populate `setup/SinkFactory.hpp` (was stub)

```cpp
#pragma once
#include <quill/Sink.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/FileSink.h>
#include <quill/sinks/RotatingFileSink.h>
#include <quill/sinks/JsonSink.h>
#include <memory>
#include <string>
#include <vector>

#include "../logging/LoggingConfig.hpp"

namespace Logger_Adapter::setup {

// Creates a named sink based on configuration. Returns nullptr if not enabled.
std::shared_ptr<quill::Sink> CreateSink(const std::string& name,
                                         const LoggingConfig& config);

// Creates all sinks referenced in a LoggerEntry's sink_names list.
std::vector<std::shared_ptr<quill::Sink>> CreateSinksForLogger(
    const config::LoggerEntry& entry, const LoggingConfig& config);

// Validates that all sink names in a LoggerEntry exist in config.
bool ValidateLoggerEntry(const config::LoggerEntry& entry,
                         const LoggingConfig& config,
                         std::string& error_out);

} // namespace Logger_Adapter::setup
```

### Step 3 — Populate `setup/LoggerRegistry.hpp` (was stub)

```cpp
#pragma once
#include <quill/Logger.h>
#include <string>
#include <unordered_map>
#include <mutex>

namespace Logger_Adapter::setup {

class LoggerRegistry {
public:
    static quill::Logger* GetOrCreate(const std::string& name);
    static quill::Logger* Get(const std::string& name);
    static quill::Logger* GetDefault();
    static bool Exists(const std::string& name);

    // Test-only: clears registry. NOT thread-safe. For test use only.
    #ifdef _DEBUG
    static void ResetForTesting();
    #endif

private:
    static std::unordered_map<std::string, quill::Logger*>& Registry();
    static std::mutex& RegistryMutex();
};

} // namespace Logger_Adapter::setup
```

### Step 4 — Update `logging/LoggingConfig.hpp`

Add `loggers` vector and `sink_names` to existing config blocks:

```cpp
#include "../config/LoggerEntry.hpp"

struct LoggingConfig {
    // ... existing fields ...
    
    bool enable_named_loggers = true;
    std::vector<config::LoggerEntry> loggers = {
        {"Emergency",       3, {"console", "file"},  true,   5000, 6},
        {"OrderExecution",  3, {"console", "file"},  false,  1000, 6},
        {"Risk",            4, {"console", "file"},  false,  1000, 6},
        {"MarketData",      3, {"console"},          false,  1000, 6},
        {"HealthProbe",     4, {"console"},          false,   100, 6}
    };
};
```

### Step 5 — Update `logging/LoggerSetup.hpp`

Key changes to `InitializeLogging()`:
1. After creating common sinks, iterate `config.loggers`
2. For each entry, call `setup::ValidateLoggerEntry()` first
3. Create sinks via `setup::CreateSinksForLogger()`
4. Create named logger via `quill::Frontend::create_or_get_logger(name, sinks)`
5. Register in `LoggerRegistry`

### Step 6 — Update `GetLogger()`

```cpp
inline quill::Logger* GetLogger(const char* name,
                                quill::LogLevel level = quill::LogLevel::Debug) {
    auto* logger = setup::LoggerRegistry::Get(name);
    if (!logger) {
        // Fallback to root — no UB, clearly documented
        logger = GetDefaultLogger();
    }
    if (level != quill::LogLevel::None) {
        logger->set_log_level(level);
    }
    return logger;
}
```

**Thread safety note**: `GetLogger()` is thread-safe via Quill's internal synchronization. However:
1. Calling `GetLogger()` during or after `ShutdownLogging()` is UB — documented but not guarded for performance.
2. Calling `InitializeLogging()` while named loggers are in use requires an **atomic registry swap** or an exclusive lock. The current design documents this as UB rather than adding a mutex to the hot path. If runtime re-initialization is needed (not just shutdown), implement a reader-writer lock or RCU-style pointer swap on the registry. See AA-C05 (Thread Model) for the full concurrency contract.

### Step 7 — Input Validation with Decision Tree

In `InitializeLogging()`, validate all `LoggerEntry` objects using the following structured error recovery spec:

#### 7a. Sink Name Resolution Rule

```cpp
// Validation Rule #2: Sink Name Resolution
// A sink_name is VALID if:
//   (a) It matches the name of an enabled sink in LoggingConfig::sinks
//       (case-sensitive string comparison), OR
//   (b) It matches one of the built-in sink names that are always
//       available: "console", "file", "rotating_file", "json" (these names
//       correspond to Quill's built-in sink types and are created by
//       SinkFactory even without explicit config), OR
//   (c) It is empty and the LoggerEntry has exactly one sink_name entry
//       (treated as "use default sinks from LoggingConfig").
//
// A sink_name is INVALID if it does not match any of the above.
// Invalid sink names are detected at RUNTIME during InitializeLogging().
// Compile-time validation is NOT supported for sink names.
```

#### 7b. Error Recovery Decision Tree

```
InitializeLogging() Error Recovery Decision Tree
================================================

For each LoggerEntry e in config.loggers:

    ValidateLoggerEntry(e, config)
        |
        ├── PASS → create logger, register in LoggerRegistry
        |
        └── FAIL → evaluate failure:
              |
              ├── e.name == "Emergency" (special subsystem)
              |     └── LOG_FATAL: Abort initialization.
              |         Emergency logger cannot be degraded silently.
              |         Return false from InitializeLogging().
              |
              ├── Failure is: empty name
              |     └── ERROR: Skip this entry (cannot reference by empty name).
              |         Continue to next entry.
              |
              ├── Failure is: sink_name not found
              |     └── WARNING: Skip this entry.
              |         Continue to next entry.
              |
              ├── Failure is: sink_names is empty
              |     └── WARNING: Skip this entry (no sinks configured).
              |         Messages would be logged to nowhere. Continue to next entry.
              |
              ├── Failure is: log_level out of range (not 0-7)
              |     └── WARNING: Clamp to nearest valid value.
              |         level < 0 → Trace; level > 7 → Critical.
              |         Use ToQuillLogLevel() for correct Quill enum mapping.
              |         Create logger with clamped level. CONTINUE.
              |
              └── Failure is: duplicate name
                    └── ERROR: Skip this entry (duplicate).
                        First definition wins. Continue to next entry.

Post-loop:
    ├── LoggerRegistry is empty AND config.loggers was non-empty
    │     └── ERROR: All configured loggers failed validation.
    │         InitializeLogging() returns true (Quill backend started)
    │         but only root logger is available.
    │
    └── LoggerRegistry has ≥ 1 entry
          └── SUCCESS: Named loggers available via GetLogger(name).
```

#### 7c. Diagnostic Routing Strategy

```cpp
// Diagnostic routing during LoggerEntry validation:
//
// Phase 1 — Pre-logger (Quill backend not yet started):
//   Use ::OutputDebugStringA (Win32) for early diagnostics.
//   Non-Windows: use fprintf(stderr, ...).
//
// Phase 2 — Post-backend (Quill backend started, root logger available):
//   After Quill::Backend::start() succeeds, switch to
//   LOG_WARNING(root, ...) for remaining validation diagnostics.
//
// Architecture note: Validation errors during Phase 1 CANNOT use any
// Logger_Adapter logging macro — the Quill backend may not be running.
// This is by design: validation runs during bootstrap.
```

#### 7d. Implementation Code

```cpp
for (auto const& entry : config.loggers) {
    std::string error;
    ValidationResult result = setup::ValidateLoggerEntry(entry, config, error);

    switch (result) {
        case ValidationResult::Pass:
            // Create logger and register in LoggerRegistry
            break;

        case ValidationResult::Fail_EmptyName:
            OutputDebugStringA("Skipping logger with empty name\n");
            continue;

        case ValidationResult::Fail_SinkNotFound:
            OutputDebugStringA(("Logger '" + entry.name +
                "': sink not found, skipping\n").c_str());
            continue;

        case ValidationResult::Fail_LevelOutOfRange:
            OutputDebugStringA(("Logger '" + entry.name +
                "': log_level " + std::to_string(static_cast<int>(entry.log_level)) +
                " out of range, clamping via ToQuillLogLevel()\n").c_str());
            break;  // entry.log_level already clamped by ValidateLoggerEntry

        case ValidationResult::Fail_NoSinks:
            OutputDebugStringA(("Logger '" + entry.name +
                "': sink_names is empty, skipping\n").c_str());
            continue;

        case ValidationResult::Fail_DuplicateName:
            OutputDebugStringA(("Duplicate logger name '" + entry.name +
                "', keeping first definition\n").c_str());
            continue;

        case ValidationResult::Fail_EmergencyInvalid:
            OutputDebugStringA(("FATAL: Emergency logger config invalid: " +
                error + "\n").c_str());
            return false;  // InitializeLogging() fails
    }
}
```

---

## Acceptance Criteria

- [ ] `GetLogger("Risk") != GetLogger("OrderExecution")` — different pointers
- [ ] `LoggerEntry` uses sink-name referencing (vector of strings) not bool flags
- [ ] Setting Risk logger to Warning does not suppress OrderExecution Debug
- [ ] Empty `loggers` list creates only root logger (backward compatible)
- [ ] Invalid `LoggerEntry` (empty name, missing sink) produces clear error
- [ ] Thread safety documented for `GetLogger()`
- [ ] `ToQuillLogLevel()` maps 0→Trace, 3→Debug, 4→Info, 5→Warning, 6→Error, 7→Critical
- [ ] Empty `sink_names` vector triggers skip with warning diagnostic
- [ ] `LoggerRegistry::ResetForTesting()` clears registry (debug builds only)
- [ ] Build succeeds Debug|x64 with zero new warnings
- [ ] `Experimental_Console.exe` exercises named loggers

---

## CategoryFromLoggerName Mapping

Named loggers map to Windows EventLog category IDs as follows:

| Logger Name        | Category ID | Description          |
|--------------------|-------------|----------------------|
| `Emergency`        | 1           | System-critical events |
| `OrderExecution`   | 2           | Order placement/acks   |
| `Risk`             | 3           | Risk check results     |
| `MarketData`       | 4           | Price feeds, quotes    |
| `HealthProbe`      | 5           | Heartbeat, liveness    |

> **Note**: This mapping is consumed by the P01 EventLog sink. Adding new named loggers in future versions requires updating this table and the corresponding EventLog message file.

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/config/LoggerEntry.hpp` | Populate from stub (sink-name referencing, Quill LogLevel enum) |
| `Logger_Adapter/setup/SinkFactory.hpp` | Populate from stub |
| `Logger_Adapter/setup/LoggerRegistry.hpp` | Populate from stub; add `ResetForTesting()` |
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add `loggers` vector, `sink_names` references |
| `Logger_Adapter/logging/LoggerSetup.hpp` | Rewrite `InitializeLogging()` with named logger loop; update `GetLogger()`; add `ToQuillLogLevel()` |