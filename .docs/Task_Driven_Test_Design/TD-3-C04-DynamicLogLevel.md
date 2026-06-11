# Test Design: Dynamic Log Level (SetLogLevel / GetLogLevel)

## Under Spec
- AA File: `AA-C04-DynamicLogLevel.md`
- Phase: 3
- Key Requirements:
  - `SetLogLevel("Risk", quill::LogLevel::Warning)` suppresses Risk Debug messages immediately
  - `SetLogLevel("NonExistent", ...)` returns `false` (no crash, no logger creation)
  - `GetLogLevel("Risk")` returns the level set by `SetLogLevel`
  - `GetLogLevel("NonExistent")` returns `quill::LogLevel::None`
  - Effect is visible to subsequent log calls only (no retroactive filtering)
  - Thread-safe: `SetLogLevel` concurrent with `LOG_INFO` produces no UB (Quill's `set_log_level()` is atomic)
  - `SetLogLevel` may be called from any thread at any time

## Test Harness
- **Fixture setup**: `InitializeLogging()` with named loggers ("Risk", "OrderExecution"); sinks write to in-memory buffer or temp file; Quill backend running; each test starts with known per-logger levels set in config.
- **Mock vs real**: Quill backend is real (log level filtering is Quill-internal). Loggers are real Quill loggers. `LoggerRegistry` is real. The `SetLogLevel` / `GetLogLevel` under test are the actual API functions from `LoggerSetup.hpp`.
- **Precondition requirements**: Backend running; named loggers registered; for concurrency tests: at least 2-3 named loggers and 4+ worker threads.

## Scenarios

### Positive Cases
- `SetLogLevel("Risk", quill::LogLevel::Warning)` followed by `LOG_DEBUG(risk_logger, ...)`: the Debug message is suppressed (not in output). Subsequent `LOG_WARNING(risk_logger, ...)` appears in output.
- `SetLogLevel("Risk", quill::LogLevel::Debug)` restores Debug messages: `LOG_DEBUG(risk_logger, ...)` appears after the level change
- `GetLogLevel("Risk")` returns the level set by the most recent successful `SetLogLevel`
- Setting level on one logger does not affect other loggers: `Risk` set to `Warning`, `OrderExecution` remains at `Debug` — `LOG_DEBUG(order_logger, ...)` still fires
- Multiple level changes: `SetLogLevel("Risk", Warning)` → `SetLogLevel("Risk", Error)` → `SetLogLevel("Risk", Info)`: final call wins; `GetLogLevel` returns `Info`
- Setting to the same level: `SetLogLevel("Risk", Warning)` twice — both return `true`, second call is no-op

### Negative / Error Cases
- `SetLogLevel("NonExistent", quill::LogLevel::Warning)` returns `false` — no logger created, no crash
- `GetLogLevel("NonExistent")` returns `quill::LogLevel::None` — no crash
- `SetLogLevel("Risk", static_cast<quill::LogLevel>(999))`: API returns `false` (invalid level, per spec); no crash, no level change
- `SetLogLevel("Risk", quill::LogLevel::None)`: Quill's `LogLevel::None` may mean "disabled" — test and document behavior (all messages suppressed, or no-op).
- `GetLogger("Risk")` with level parameter after `SetLogLevel`: `GetLogger("Risk", quill::LogLevel::Debug)` overrides the runtime level — since `GetLogger` calls `logger->set_log_level(level)`. This is expected per C01 spec.
- `SetLogLevel` with level value < 0 or > 7 (e.g., `static_cast<quill::LogLevel>(-1)`): returns `false` — invalid level rejected before reaching Quill
- `GetLogLevel("NonExistent")` returns `None` — documented ambiguity: caller cannot distinguish "logger exists with level=None" from "logger does not exist"
- `SetLogLevel` effect is eventually visible (not immediately sequential consistent): level change visible to all threads within bounded time (microseconds), no memory barrier issued
- Ephemeral nature: `SetLogLevel` changes are overwritten by future config hot-reload (F01); no persistence mechanism in v0.2.0

### Production Realities
- Operator calls `SetLogLevel("Risk", quill::LogLevel::Warning)` during market hours while 20 threads are logging to `Risk` logger: the level change is atomic; all threads see the new level on their next log call (within microseconds). No in-flight log message is truncated or corrupted.
- `SetLogLevel("Emergency", quill::LogLevel::Trace)` during a crisis to capture maximum diagnostic data: works immediately; the Emergency logger's custom sink (EventLog, dedicated file) starts receiving all Trace-level messages.
- `SetLogLevel` called 1000 times/second from a monitoring thread: each call is an atomic write on the logger's level field — no contention on the logging hot path (the hot path only reads the atomic). The monitor thread's own log messages may be slightly delayed.
- `ShutdownLogging()` while `SetLogLevel` is being called: the logger objects are still alive in Quill's internal storage; `set_log_level()` on a logger whose backend is stopped should still work (no queue access). After shutdown, the logger can no longer log, but setting level is safe (no-op effect).

### Thread Safety
- `SetLogLevel` calls `quill::Logger::set_log_level(level)` which uses `std::atomic` internally — safe to call concurrently with `LOG_INFO` on the same logger
- `GetLogLevel` calls `quill::Logger::log_level()` which is a relaxed atomic load — safe to call concurrently with `SetLogLevel`
- `SetLogLevel` looks up logger in `LoggerRegistry::Registry()` — this is a read-only map access after initialization. Safe per AA-C05 contract (registry is read-only after init).
- `SetLogLevel` on a logger being concurrently destroyed (during re-init): this case is UB per AA-C05 — not guarded. Caller must ensure no concurrent `SetLogLevel` during shutdown/re-init.
- Memory ordering: `quill::Logger::set_log_level()` likely uses `memory_order_relaxed` or `memory_order_release`. The level is a simple integer comparison on the hot path — no ordering guarantees needed beyond atomicity.

## Assertions
- After `SetLogLevel("Risk", Warning)`: `LOG_DEBUG(risk_logger, "test")` does NOT appear in output; `LOG_WARNING(risk_logger, "test")` DOES appear in output
- After `SetLogLevel("Risk", Debug)`: `LOG_DEBUG(risk_logger, "test")` appears in output
- `SetLogLevel("Risk", Error)` returns `true`; `SetLogLevel("NonExistent", Error)` returns `false`
- `GetLogLevel("Risk")` returns the last successfully set level
- `GetLogLevel("NonExistent")` returns `quill::LogLevel::None`
- `OrderExecution` level unchanged after `SetLogLevel("Risk", ...)`: `GetLogLevel("OrderExecution")` returns its configured level
- Concurrent scenario: 4 threads call `LOG_INFO(risk_logger, ...)` in a loop while 2 threads call `SetLogLevel("Risk", varying_level)` — no crashes, no data races (verified by running under ThreadSanitizer or on Windows via Application Verifier)
- Level change is visible to new log calls within 1μs (measured via steady_clock before/after set + log dispatch)
- `SetLogLevel("Risk", static_cast<quill::LogLevel>(999))` returns `false` — invalid level is rejected
- `SetLogLevel("Risk", static_cast<quill::LogLevel>(-1))` returns `false` — negative level is rejected

## Failure Mode
- `SetLogLevel` on non-existent logger returns `false` — correct behavior, no failure mode
- `SetLogLevel` silently fails (returns `true` but level unchanged): operator believes they've reduced verbosity but messages continue at original rate. Operational confusion, potential log flooding.
- `LoggerRegistry::Get()` returns stale/null logger after re-init (UB scenario): `SetLogLevel` dereferences null pointer → process crash. Mitigation: document as UB, application-level coordination required.
- Invalid `quill::LogLevel` value passed through to Quill: Quill may clamp, ignore, or produce unexpected filtering. Corruption of log output (messages filtered incorrectly).

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Added parameter validation: invalid levels return `false` | Step 1 — SetLogLevel API | Updated invalid-level test scenario (now returns false); marked GAP-3-C04-1 RESOLVED |
| Documented `None` ambiguity for non-existent loggers | Step 1 — GetLogLevel NOTE | Added scenario documenting None ambiguity; marked GAP-3-C04-2 RESOLVED |
| Added visibility delay documentation | Step 3 — Thread Safety | Added scenario for eventual visibility; marked GAP-3-C04-3 RESOLVED |
| Documented future gap for bulk API | Step 4 — Future gap note | Kept as acknowledged gap; marked GAP-3-C04-4 RESOLVED |
| Added ephemeral nature / hot-reload overwrite warning | Step 4 — Important note | Added scenario for ephemeral SetLogLevel; marked GAP-3-C04-5 RESOLVED |

## Spec Gap Notes (SGN)

| Gap ID | Status | Issue | Architectural Impact | Recommendation |
|--------|--------|-------|---------------------|----------------|
| GAP-3-C04-1 | ✅ RESOLVED | No parameter validation for `quill::LogLevel` value passed to `SetLogLevel`. Quill's `set_log_level()` may not validate the enum value. | Passing an invalid `LogLevel` value produces undefined behavior — possible silent incorrect filtering or assertion. | AA spec now validates: "Invalid levels (>7 or negative) return false and are not forwarded to Quill." 🛠️ Fix applied. |
| GAP-3-C04-2 | ✅ RESOLVED | `GetLogLevel` returns `quill::LogLevel::None` for non-existent loggers — but `None` might be a valid internal Quill state (e.g., "not set"). Callers cannot distinguish between "logger exists with level=None" and "logger does not exist". | Monitoring tools that poll `GetLogLevel()` to display current levels may show "None" for non-existent loggers, which is indistinguishable from a logger whose level is intentionally unset. | AA spec now documents: "NOTE: None is returned for BOTH 'logger exists with level=None' and 'logger does not exist'. Callers cannot distinguish these cases." 🛠️ Fix applied (documentation). |
| GAP-3-C04-3 | ✅ RESOLVED | The spec says "effect is visible to subsequent log calls only (no retroactive filtering)" but does not define the visibility delay. | On weakly-ordered architectures, a thread may continue to use the old level for some microseconds after `SetLogLevel` returns due to store buffer delays. | AA spec now documents: "Level change is visible to all threads within a bounded time (typically microseconds, architecture-dependent). No explicit memory barrier is issued beyond Quill's atomic store. Sequential consistency is not guaranteed." 🛠️ Fix applied. |
| GAP-3-C04-4 | ✅ RESOLVED | No API for bulk or wildcard level changes (e.g., `SetLogLevel("*", Level::Warning)` to set all loggers). | During incident response, setting 5+ loggers individually wastes precious seconds. | AA spec acknowledges this as a future gap: "There is no bulk SetLogLevel function for incident response. Acceptable for v0.2.0 but should be reevaluated if named loggers grow beyond 5." 🛠️ Fix applied (acknowledged). |
| GAP-3-C04-5 | ✅ RESOLVED | The spec does not define whether `SetLogLevel` persists across hot-reload (future F01). If config hot-reload overwrites runtime level, operators' runtime changes are lost. | Operators set a logger to Debug for debugging, then config hot-reload resets it to Warning — debugging session disrupted. | AA spec now documents: "Runtime level changes via SetLogLevel() are ephemeral — they will be overwritten by any future configuration hot-reload." 🛠️ Fix applied (documentation). |
