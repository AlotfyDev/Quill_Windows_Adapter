# REV-C01 — Validation Pattern Lacks Concrete Specification

**Severity**: ⚠️ Revision  
**AA File**: AA-C01-MultiLogger.md  
**Validation Source**: Phase2-3_AA_Validation_Map.md §2 (C01), §6 Gap Analysis, §8 Must-Fix Not Listed

---

## Description

AA-C01 Step 7 ("Input Validation") provides a validation pattern — `ValidateLoggerEntry` with 4 rules (non-empty name, valid sink names, log level 0-7, no duplicates) — but does not specify a **concrete validation spec** with unambiguous error handling semantics. Three critical ambiguities remain:

1. **What constitutes a "missing sink"?** — The AA currently says "all `sink_names` must reference an enabled sink in config." But what does "reference" mean? A string match against config sink names? What about default sinks (console, file) that always exist? Is this a runtime check during `InitializeLogging()` or a compile-time constraint?

2. **Error recovery strategy** — The validation loop shows `continue` (skip this logger), but the AA does not justify this choice or document alternatives. What if the Emergency logger has a missing sink? Should init abort? What if ALL loggers are invalid — continue with root logger only?

3. **Logging during validation** — The AA uses `OutputDebugStringA()` to report validation errors. But what if the error is in the logger that would report the error? Circular dependency: you need the logger subsystem to report validation errors, but validation happens during logger initialization. The current approach bypasses the issue by using Win32 `OutputDebugStringA`, but this is platform-specific and easily missed by production ops.

---

## Root Cause

The AA treated validation as a **procedural concern** (write a loop, check each entry) rather than an **architectural specification** (define error model, recovery semantics, diagnostic routing). The design was ported from a typical C++ "validate config → log error → continue" pattern without considering that the logging infrastructure is precisely what's being bootstrapped. The sink-name-string-matching approach was inherited from the sink-name-referencing design (Step 1) without defining the matching contract.

---

## Exact Fix

### 1. Define "Missing Sink" Precisely

Replace the vague rule with an exact specification:

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

### 2. Define Error Recovery with a Decision Tree

Replace the bare `continue` with a structured error recovery spec:

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
              |         Caller must decide to exit or use root logger only.
              |
              ├── Failure is: empty name
              |     └── ERROR: Skip this entry (cannot reference by empty name).
              |         Continue to next entry.
              |
              ├── Failure is: sink_name not found
              |     └── WARNING: Skip this entry.
              |         Continue to next entry.
              |         Logger will NOT be available via GetLogger(name).
              |         GetLogger(name) falls back to root logger.
              |
              ├── Failure is: log_level out of range (not 0-7)
              |     └── WARNING: Clamp to nearest valid value.
              |         level < 0 → Debug (0)
              |         level > 7 → Critical (7)
              |         Create logger with clamped level.
              |         CONTINUE (do not skip — level clamp is recoverable).
              |
              └── Failure is: duplicate name
                    └── ERROR: Skip this entry (duplicate).
                        Continue to next entry.
                        First definition wins.
                        Log warning: "Duplicate logger name 'X' — second definition ignored."

Post-loop:
    ├── LoggerRegistry is empty AND config.loggers was non-empty
    │     └── ERROR: All configured loggers failed validation.
    │         InitializeLogging() returns true (Quill backend started)
    │         but only root logger is available.
    │         GetLogger(name) falls back to root for any name.
    │
    └── LoggerRegistry has ≥ 1 entry
          └── SUCCESS: Named loggers available via GetLogger(name).
```

### 3. Define Diagnostic Output Strategy

Replace `OutputDebugStringA` with a structured diagnostic strategy:

```cpp
// Diagnostic routing during LoggerEntry validation:
//
// Phase 1 — Pre-logger (Quill backend not yet started):
//   Use ::OutputDebugStringA(Win32) for early diagnostics.
//   This is always available on Windows.
//   Non-Windows: use fprintf(stderr, ...).
//
// Phase 2 — Post-backend (Quill backend started, root logger available):
//   After Quill::Backend::start() succeeds, switch to LOG_WARNING(root, ...)
//   for remaining validation diagnostics.
//
// Phase 3 — Initialization complete:
//   All further diagnostics use the named logger or root logger.
//
// Architecture note: Validation errors during Phase 1 CANNOT use any
// Logger_Adapter logging macro — the Quill backend may not be running.
// This is by design: validation runs during bootstrap.
// The ::OutputDebugStringA / fprintf(stderr) fallback is intentional and
// documented as "bootstrap diagnostics — not visible in production logs."
```

### 4. Update AA-C01 Step 7 with These Specifications

Replace the current 4-line validation loop with the full spec above. The code should be updated to match the decision tree:

```cpp
for (auto const& entry : config.loggers) {
    std::string error;
    auto result = setup::ValidateLoggerEntry(entry, config, error);

    switch (result) {
        case ValidationResult::Pass:
            // Create logger and register
            break;

        case ValidationResult::Fail_EmptyName:
            // ERROR: skip entry
            OutputDebugStringA(("Skipping logger with empty name\n").c_str());
            continue;

        case ValidationResult::Fail_SinkNotFound:
            // WARNING: skip entry
            OutputDebugStringA(("Logger '" + entry.name +
                "': sink not found, skipping\n").c_str());
            continue;

        case ValidationResult::Fail_LevelOutOfRange:
            // WARNING: clamp level
            OutputDebugStringA(("Logger '" + entry.name +
                "': log_level " + std::to_string(entry.log_level) +
                " out of range [0-7], clamping\n").c_str());
            break;  // entry.log_level already clamped by ValidateLoggerEntry

        case ValidationResult::Fail_DuplicateName:
            // ERROR: skip this duplicate
            OutputDebugStringA(("Duplicate logger name '" + entry.name +
                "', keeping first definition\n").c_str());
            continue;

        case ValidationResult::Fail_EmergencyInvalid:
            // FATAL: Emergency logger must be valid
            OutputDebugStringA(("FATAL: Emergency logger configuration invalid: " +
                error + "\n").c_str());
            return false;  // InitializeLogging() fails
    }
}
```

---

## Impact if NOT Fixed

| Ambiguity | Risk if Unresolved |
|-----------|-------------------|
| "Missing sink" undefined | Developer implements string match against all sinks; misses built-in ones. Feature request logs show "sink not found" errors for "console" which actually exists. |
| Error recovery: skip vs abort | Developer chooses abort for all errors. A missing MarketData sink on a dev machine prevents the Emergency logger from initializing — critical logging is crippled by a non-critical subsystem. |
| Error recovery: skip vs abort (inverse) | Developer chooses skip for all errors. Emergency logger with missing sink silently degrades. An operator sees no Emergency logs and assumes the system is healthy. |
| Logging during bootstrap | Developer logs validation errors via LOG_WARNING before Quill backend starts. Logs are silently lost. Operator sees no diagnostic output and cannot debug logger init failures. |
| All loggers invalid | Developer skips all but falls back to root. But the code doesn't handle "empty registry" — GetLogger(name) still works (falls back to root) but this is accidental, not documented. |

---

## Verification

1. **Code review**: Confirm `ValidateLoggerEntry` returns an enumerated result (not just `bool`) with clear failure categories matching the decision tree.
2. **Code review**: Confirm the `InitializeLogging()` loop implements the full decision tree — Emergency abort, level clamping, skip-on-duplicate.
3. **Code review**: Confirm Phase 1 diagnostics use `OutputDebugStringA`/`fprintf(stderr)`, NOT `LOG_*` macros.
4. **Unit test**: Create `LoggingConfig` with all valid entries → verify all loggers created.
5. **Unit test**: Create `LoggingConfig` with empty name entry → verify entry skipped, no crash, remaining loggers created.
6. **Unit test**: Create `LoggingConfig` with an Emergency logger pointing to non-existent sink → verify `InitializeLogging()` returns `false`.
7. **Unit test**: Create `LoggingConfig` with a duplicate name → verify first definition wins, second skipped, no crash.
8. **Unit test**: Create `LoggingConfig` where ALL entries are invalid → verify `InitializeLogging()` returns `true` (Quill backend is running) and `GetLogger("anything")` returns root logger.
