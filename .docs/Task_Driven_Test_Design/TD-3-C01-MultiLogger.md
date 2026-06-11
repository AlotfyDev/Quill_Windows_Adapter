# Test Design: Multi-Logger Shim with Validation Decision Tree

## Under Spec
- AA File: `AA-C01-MultiLogger.md`
- Phase: 3
- Key Requirements:
  - `GetLogger("Risk") != GetLogger("OrderExecution")` — distinct named logger instances with independent log levels and sink lists
  - `LoggerEntry` uses sink-name referencing: `std::vector<std::string> sink_names` references named sinks in config
  - Validation decision tree: empty name (skip), sink not found (skip), level out of range (clamp 0-7), duplicate name (first wins), Emergency failure (fatal)
  - Pre-backend diagnostics via `OutputDebugStringA`, post-backend via `LOG_WARNING(root, ...)`
  - Thread-safe `GetLogger()` via Quill internal synchronization; UB if called after `ShutdownLogging()`
  - Empty `loggers` list creates only root logger for backward compatibility

## Test Harness
- **Fixture setup**: `InitializeLogging()` with a `LoggingConfig` containing an in-memory sink (e.g., `JsonSink` writing to a `std::ostringstream` or custom `BufferSink`) plus a temporary file sink; `LoggerRegistry` resets between tests via fresh process or test-isolated singleton (add `LoggerRegistry::ResetForTesting()` if not in spec). Config is populated with varied `LoggerEntry` values to exercise the decision tree.
- **Mock vs real**: Quill backend is real (started/stopped per test). Sinks are real but use in-memory/temp-file destinations. `LoggerRegistry` is real. Validation functions (`ValidateLoggerEntry`) are the real implementation under test. `OutputDebugStringA` output captured via Win32 `SetDebugStringHook` or verified indirectly via behavior.
- **Precondition requirements**: Quill backend not started (tests call `InitializeLogging()`). For tests requiring a clean registry, use a fresh process or a test-only reset mechanism. Registry is empty before each test.

## Scenarios

### Positive Cases
- Config with 3 named loggers ("Emergency", "Risk", "MarketData"): each returns a distinct pointer from `GetLogger()`; setting `Risk` to `Warning` level does not affect `Emergency` or `MarketData` level
- `LoggerEntry` with `sink_names = {"console", "file"}`: logger writes to both sinks; messages appear in console output AND file
- Config with empty `loggers` list: only root logger exists; `GetDefaultLogger()` returns a valid logger
- `LoggerEntry` with single sink name `"file"` and `file.enabled = true`: logger writes only to file
- `GetLogger("Risk", quill::LogLevel::Debug)` sets level on the returned logger; subsequent `GetLogger("Risk")` (without level) returns the same logger with previously set level
- Named loggers use different categories: `Emergency`→1, `OrderExecution`→2, `Risk`→3, `MarketData`→4, `HealthProbe`→5 via `CategoryFromLoggerName` mapping table in AA-C01
- `ToQuillLogLevel()` conversion: input 0→Trace, 1→Debug, 2→Debug, 3→Debug (Quill's Debug=3), 4→Info, 5→Warning, 6→Error, 7→Critical; inputs <0 clamp to Trace, >7 clamp to Critical
- Diagnostic routing: validation failure before Quill backend start emits `OutputDebugStringA`; after backend start emits `LOG_WARNING(root, ...)` — distinguishable by hooking debug output vs inspecting log output

### Negative / Error Cases
- Empty name in `LoggerEntry`: entry skipped, `OutputDebugStringA("Skipping logger with empty name\n")` called, no crash, initialization continues with remaining loggers
- `sink_names` contains a name not matching any enabled sink: entry skipped, warning output, initialization continues
- `log_level` = -1 (below 0): clamped to Trace per decision tree — logger created with level Trace
- `log_level` = 10 (above 7): clamped to Critical (7) per decision tree — logger created with level Critical
- Duplicate name in `loggers`: second entry skipped (first definition wins), warning output, initialization continues
- `Emergency` logger with invalid config (e.g., empty name, sink not found): `InitializeLogging()` returns `false`, no loggers created, emergency path fails fast
- All configured loggers fail validation (e.g., all have invalid sink names): `InitializeLogging()` returns `true` (Quill backend started) but only root logger available
- LoggerEntry with empty `sink_names` vector: decision tree specifies `Fail_NoSinks` → WARNING and skip with `OutputDebugStringA` diagnostic; spec now explicitly covers this case
- `GetLogger("NonExistent")` after init: returns root logger (fallback), not null

### Production Realities
- `GetLogger("Risk")` called from 8 concurrent threads on startup: all return the same pointer; Quill's `create_or_get_logger` or `get_logger` is internally synchronized
- `InitializeLogging()` with 50 named loggers, each with 3 sinks: backend starts successfully with 150 sink instances; startup time measured < 100ms
- After `ShutdownLogging()`, `GetLogger("Risk")` is UB — test verifies this is documented and the process doesn't crash (but no guarantee per spec). Production pattern: set `app_running=false` before shutdown, all threads check this before `GetLogger()`.
- Re-initialization after shutdown: spec says UB. Test confirms crash/UB is NOT guaranteed and must be prevented at application level.

### Thread Safety
- `LoggerRegistry::Registry()` returns a reference to `std::unordered_map<std::string, quill::Logger*>` — writes happen only during `InitializeLogging()`. Concurrent reads from `GetLogger()` are safe because no concurrent writes after init.
- `LoggerRegistry::RegistryMutex()` protects writes to the registry map only during initialization. Reads from `GetLogger()` do NOT take the lock — this is correct because the map is read-only after init.
- `Quill::Frontend::create_or_get_logger(name, sinks)` is thread-safe per Quill v10.0.1 documentation — the call is internally synchronized.
- `set_log_level()` on a named logger is atomic (`std::atomic` in Quill internals). Concurrent `SetLogLevel()` and `LOG_INFO()` on the same logger is safe.

## Assertions
- `GetLogger("Risk") != GetLogger("OrderExecution")` — pointer inequality
- `GetLogger("Risk")` called twice returns the same pointer
- `GetLogger("NonExistent")` returns `GetDefaultLogger()` (root logger fallback)
- `InitializeLogging()` with 3 valid loggers: `LoggerRegistry::Exists("Risk")` returns `true`
- `LoggerRegistry::Exists("NonExistent")` returns `false`
- After `InitializeLogging()` returns `false` (Emergency failure): `LoggerRegistry` is empty or contains only root logger
- Duplicate name in config: first entry's sink list is used, second entry ignored
- Level clamping: `log_level = -1` → logger created with `quill::LogLevel::Trace`; `log_level = 10` → logger created with `quill::LogLevel::Critical`
- Empty `loggers` vector: root logger is the only logger; `GetLogger("anything")` returns root logger
- `InitializeLogging()` with all-invalid loggers: returns `true`, root logger available
- `OutputDebugStringA` called exactly once per validation failure (verified via hook)

## Failure Mode
- Validation defect (invalid entry treated as valid): logger created with bad config (e.g., no sinks) — messages silently lost. Debugging this is hard because it looks like a config issue.
- Validation defect (valid entry treated as invalid): subsystem operates without its intended logger — falls back to root logger. Messages go to wrong sink. Operational confusion.
- Registry corruption (map mutated during concurrent read): crash or stale pointer read. UB. Mitigation: read-only after init.
- Level clamping defect (wrong clamp range): logger created with wrong level — either too verbose or too quiet. Degraded operational visibility.
- Emergency logger validation fails but init continues: spec says this is fatal (returns false) — if implemented incorrectly, Emergency subsystem may log to root instead of its dedicated sink, masking critical failures.

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Added `ToQuillLogLevel()` conversion with explicit 0–7 mapping | Step 1 — ToQuillLogLevel Conversion | Added positive/negative test scenarios; fixed clamping assertions (Debug→Trace) |
| Added diagnostic routing strategy (pre/post backend) | §7c Diagnostic Routing Strategy | Added scenario for OutputDebugStringA vs LOG_WARNING |
| Added `Fail_NoSinks` to decision tree for empty `sink_names` | §7b Error Recovery Decision Tree | Updated empty sink_names test scenario; marked GAP-3-C01-3 RESOLVED |
| Added `ResetForTesting()` for test isolation | Step 3 — LoggerRegistry | Updated test harness to reference `ResetForTesting()`; marked GAP-3-C01-4 RESOLVED |
| Added `CategoryFromLoggerName` mapping table | CategoryFromLoggerName Mapping | Fixed category IDs in positive test scenario; marked GAP-3-C01-5 RESOLVED |
| Added design note acknowledging ValidateLoggerEntry simplification | Step 1 — Design note | Marked GAP-3-C01-2 RESOLVED |
| Added explicit ToQuillLogLevel mapping and Quill LogLevel enum usage | Step 1 — LoggerEntry defaults | Marked GAP-3-C01-1 RESOLVED |

## Spec Gap Notes (SGN)

| Gap ID | Status | Issue | Architectural Impact | Recommendation |
|--------|--------|-------|---------------------|----------------|
| GAP-3-C01-1 | ✅ RESOLVED | `LoggerEntry::log_level` default is 3 with comment "Debug (0-7 matching ToQuillLogLevel)" but Quill's `LogLevel::Debug` is typically value 3 in the enum. The clamping rule says "level < 0 → Debug (0)" but 0 is `Trace` in Quill, not Debug. The numeric range 0-7 does not align with Quill's LogLevel enum values. | Level clamping maps `0` to the wrong level (Trace instead of Debug). Operators setting level=0 expecting "most verbose" get Trace (which may be filtered by default). | AA spec now uses Quill's `LogLevel` enum directly in `LoggerEntry` defaults, and added `ToQuillLogLevel(int32_t)` with explicit switch/map. 🛠️ Fix applied. |
| GAP-3-C01-2 | ✅ RESOLVED | Decision tree indicates `ValidateLoggerEntry` clamps the level and returns `Fail_LevelOutOfRange` — but the entry is already mutated (clamped) before the caller handles the result. This mixes validation with mutation. | If the validation logic changes, the caller might double-clamp or mis-handle already-mutated entries. The caller code says "entry.log_level already clamped by ValidateLoggerEntry" — this breaks the single-responsibility principle. | AA spec now includes a design note acknowledging this is a deliberate simplification and documenting potential future rename to `ValidateAndSanitizeLoggerEntry`. 🛠️ Fix applied (documented as intentional). |
| GAP-3-C01-3 | ✅ RESOLVED | Empty `sink_names` vector is not covered by the decision tree. | A LoggerEntry with no sinks is silently created — it logs to nowhere. Messages are processed by Quill but never emitted. | AA spec now includes `Fail_NoSinks` in the decision tree: skip entry with warning diagnostic. 🛠️ Fix applied. |
| GAP-3-C01-4 | ✅ RESOLVED | No testability hooks: `LoggerRegistry` is a singleton with static methods, no reset. State leaks between tests. | Integration tests must run one per process, or rely on fragile teardown. | AA spec now adds `LoggerRegistry::ResetForTesting()` guarded by `#ifdef _DEBUG`. 🛠️ Fix applied. |
| GAP-3-C01-5 | ✅ RESOLVED | `CategoryFromLoggerName()` mapping is referenced but not specified — the mapping from logger name to EventLog category ID (1-5) is undefined. | Without a defined mapping, the P01 EventLog sink cannot correctly categorize messages from different loggers. | AA spec now includes a CategoryFromLoggerName mapping table (Emergency→1, OrderExecution→2, Risk→3, MarketData→4, HealthProbe→5). 🛠️ Fix applied. |
