# AA-C04 — Dynamic Log Level Reconfiguration (After-Audit New)

> **Phase**: 3 — 🏗️ Core Infrastructure  
> **Effort**: 1-2 h  
> **Depends on**: AA-C01 (named loggers), AA-C05 (thread model contract)  
> **Capability Gap**: 24/7 trading system cannot restart to change log levels

---

## Problem

Logger_Adapter initializes log levels at startup via `LoggingConfig`. There is no runtime API to change a logger's level without restarting the process. In a 24/7 trading system, this means:
- Debugging a live issue requires a full restart (lost context, connection state)
- Reducing verbosity during high-load periods is impossible
- Emergency log level escalation must be pre-configured

---

## Implementation Plan

### Step 1 — Add Runtime API

```cpp
namespace Logger_Adapter {

// Thread-safe: can be called concurrently with logging.
// Effect is visible to subsequent log calls only (no retroactive filtering).
// Returns false if logger_name does not exist OR level is invalid.
// Valid levels: Trace (0), Debug (3), Info (4), Warning (5), Error (6), Critical (7), None (disabled).
// Invalid levels (>7 or negative) return false and are not forwarded to Quill.
bool SetLogLevel(const char* logger_name, quill::LogLevel level) noexcept;

// Returns current level, or None if logger doesn't exist.
// NOTE: None is returned for BOTH "logger exists with level=None" and
//       "logger does not exist". Callers cannot distinguish these cases.
quill::LogLevel GetLogLevel(const char* logger_name) noexcept;

} // namespace Logger_Adapter
```

### Step 2 — Integration with LoggerRegistry

Add to `setup/LoggerRegistry.hpp`:

```cpp
class LoggerRegistry {
public:
    // ... existing GetOrCreate, Get, GetDefault ...

    // Runtime level change. Thread-safe with concurrent logging
    // because quill::Logger::set_log_level() is atomic internally.
    static bool SetLevel(const std::string& name, quill::LogLevel level);

    // Retrieve current level.
    static quill::LogLevel GetLevel(const std::string& name);
};
```

### Step 3 — Thread Safety

```cpp
// Thread safety contract (per AA-C05):
// - SetLogLevel() may be called from any thread at any time.
// - Quill's logger->set_log_level() uses std::atomic internally.
// - No lock needed on the hot path — level check is a single atomic read.
// - GetLogger() + SetLogLevel() are safe concurrently.
// - SetLogLevel() on a non-existent logger returns false (no creation).
//
// Visibility guarantee: Level change is visible to all threads within a
// bounded time (typically microseconds, architecture-dependent). No explicit
// memory barrier is issued beyond Quill's atomic store. Sequential consistency
// is not guaranteed — eventual visibility is sufficient for the operational
// use case (operator changing level during live debugging).
```

### Step 4 — Configuration Hot-Reload Scaffolding (Future)

For v0.2.0, `SetLogLevel()` is API-only. Future F01 (Configuration Hot-Reload) will wire this to a config file watcher or signal handler.

> **Important**: Runtime level changes via `SetLogLevel()` are **ephemeral** — they will be overwritten by any future configuration hot-reload. If F01 is implemented, a "runtime override" flag should be added to persist manual level changes across reloads.

> **Future gap**: There is no bulk `SetLogLevel("*", level)` or wildcard function for incident response. Operators must call `SetLogLevel()` individually for each named logger. This is acceptable for v0.2.0 but should be reevaluated if the number of named loggers grows beyond 5.

---

## Acceptance Criteria

- [ ] `SetLogLevel("Risk", quill::LogLevel::Warning)` suppresses Risk Debug messages immediately
- [ ] `SetLogLevel("Risk", quill::LogLevel::Debug)` restores Risk Debug messages
- [ ] `SetLogLevel("NonExistent", ...)` returns false (no crash, no creation)
- [ ] `GetLogLevel("Risk")` returns the level set by `SetLogLevel`
- [ ] `SetLogLevel` with invalid enum value (e.g., `static_cast<quill::LogLevel>(99)`) returns false
- [ ] Calling `SetLogLevel` concurrently with logging produces no UB
- [ ] Build succeeds Debug|x64 with zero new warnings

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/logging/LoggerSetup.hpp` | Add `SetLogLevel()`, `GetLogLevel()` |
| `Logger_Adapter/setup/LoggerRegistry.hpp` | Add `SetLevel()`, `GetLevel()` |
