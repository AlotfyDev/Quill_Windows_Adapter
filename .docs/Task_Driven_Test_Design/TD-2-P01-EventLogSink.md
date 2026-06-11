# Test Design: Windows EventLog Sink (ReportEventW)

## Under Spec
- AA File: `AA-P01-EventLogSink.md`
- Phase: 2 (Design) + 3 (Implementation)
- Key Requirements:
  - `EventLogSink` extends `quill::Sink` and calls `ReportEventW` with correct Event ID, Event Type, and Unicode message
  - Event IDs map: Trace/Debug/Info=0-2 (Information), Warning=3 (Warning), Error/Critical=4-5 (Error)
  - Category maps logger name to subsystem ID (1-5)
  - `RegisterEventSourceA` called once in constructor, `DeregisterEventSource` in destructor; `HANDLE` cached
  - `buffer.format(metadata)` API is UNVERIFIED against Quill v10.0.1 — test design must cover all 3 fallback outcomes
  - Missing source registration does NOT crash — `ReportEventW` returns `FALSE`, message silently dropped
  - UTF-8 to UTF-16 conversion via `MultiByteToWideChar`

## Test Harness
- **Fixture setup**: For unit tests without Windows EventLog: create a test double that wraps `ReportEventW` via hook/detour (Microsoft Detours or inline hook) to capture calls. For integration tests: run on Win10+ x64, register test source via `Register-EventLog`, verify events in Event Viewer via `Get-WinEvent`. For the unregistered-source scenario: use a source name not registered in the registry. For buffer API verification: inspect `packages/quill.10.0.1/include/quill/Buffer.h` to determine which extraction path (A/B/C) applies; set the corresponding `#define` for compile-time path selection.
- **Mock vs real**: `ReportEventW` is real Win32 API; EventLog registration is real (requires admin for setup/teardown in integration tests); `buffer.format(metadata)` is real Quill v10.0.1 API — if it doesn't exist, fallback uses `buffer.data()` + `buffer.size()` or `PatternFormatter`. The sink's `write()` override is the real implementation under test.
- **Precondition requirements**: Windows 10+ x64; test source registered in EventLog for integration tests; for format-api-verification tests: Quill v10.0.1 NuGet package installed and `quill/Buffer.h` available for inspection.

## Scenarios

### Positive Cases
- `EventLogSink` with valid registered source: calling `write()` with an Info-level log produces a Windows EventLog entry with Event ID 2, EventType `EVENTLOG_INFORMATION_TYPE`, and the correct message text
- `EventLogSink` with Warning-level log: Event ID 3, EventType `EVENTLOG_WARNING_TYPE`
- `EventLogSink` with Critical-level log: Event ID 5, EventType `EVENTLOG_ERROR_TYPE`
- Category ID correctly set from constructor (default 1 = System; per-logger category via `CategoryFromLoggerName`)
- Unicode UTF-16 message string contains the correct log message after UTF-8 → UTF-16 conversion (test with ASCII, Arabic, Chinese, Japanese characters)
- Multiple writes to the sink produce multiple EventLog entries with correct sequential ordering

### Negative / Error Cases
- **Outcome A**: `buffer.format(metadata)` exists and is public — `std::string message(buffer.format(metadata))` compiles and produces correct output
- **Outcome B**: `format()` does not exist — `std::string(static_cast<char const*>(buffer.data()), buffer.size())` compiles and produces correct output
- **Outcome C**: Neither `format()` nor `data()/size()` available — `quill::PatternFormatter` cached in sink produces correct output via `formatter.format(metadata, buffer)`
- Source not registered: `RegisterEventSourceA` returns `NULL`, `registered_` is `false`, `ReportEventW` never called — no crash, no exception; `GetLastError()` diagnostics written via `OutputDebugStringA`
- Message exceeds 32KB after UTF-16 conversion: sink truncates to `MAX_EVENT_SIZE` (32768 - 256), writes truncated message, one-time warning via `compare_exchange_strong` on `OutputDebugStringA`
- Source registered but `ReportEventW` returns `FALSE` (e.g., EventLog full): sink silently drops the message — no error propagation
- `RegisterEventSourceA` returns `NULL` and `DeregisterEventSource` called in destructor: guarded by `registered_` atomic, correctly skipped
- Null UTF-8 input to `Utf8ToWide`: returns empty `wstring` — `ReportEventW` called with empty string, no crash
- `MultiByteToWideChar` fails (invalid UTF-8 sequence): returns empty `wstring` — message silently dropped, no crash

### Production Realities
- `ReportEventW` kernel transition takes 5-15 μs — at 100 events/sec, this is 0.5-1.5ms of backend-thread CPU time per second. Acceptable for alert-only usage. Test: verify throughput cap is documented and measured in CI benchmark.
- Source registration is manual (requires admin) — if a deploy script forgets this step, ALL EventLog messages silently disappear. This is by design but must be verified in deployment testing.
- `VerifyEventLogSource()` health check helper: call during application startup to verify source is registered and writable; produces a test event in Application EventLog — document as expected ops behavior
- Compile-time path selection for buffer extraction (`#if defined(QUILL_BUFFER_HAS_FORMAT)` / `#elif defined(QUILL_BUFFER_HAS_DATA_SIZE)` / `#else`): verify correct path is selected based on Quill v10.0.1 API inspection
- EventLog has a maximum event size (~32KB per event). Test with very long log messages (>32KB after UTF-16 conversion): `ReportEventW` returns `FALSE` — message silently dropped.
- Process crash during `ReportEventW` kernel transition: EventLog entry may be partially written; Windows EventLog service handles this gracefully (log may be truncated or missing).
- `DeregisterEventSource` called on shutdown while another thread might be in `write()` — but Quill's backend thread is the sole caller of `write()`, and destructor is called after backend thread is joined (per Quill lifecycle). No race in practice.
- Multiple `EventLogSink` instances writing to different sources: each has its own `HANDLE`, no shared state.

### Thread Safety
- `write()` called only from Quill's single backend thread — no concurrent calls to `ReportEventW` on the same sink from different threads
- `registered_` is `std::atomic<bool>` — written once in constructor, read on every `write()`. On x86, a plain `bool` would also work but the atomic ensures the compiler doesn't optimize away the read.
- `Utf8ToWide` is reentrant: no shared state, only local variables.
- The `buffer` parameter is owned by the backend thread for the duration of `write()` — no external mutation.
- Destructor runs on the thread that destroys the sink (typically the main thread after `ShutdownLogging()`). The `registered_` load uses `memory_order_relaxed` because there are no concurrent writers at destruction time.

## Assertions
- `write()` with Info level: `ReportEventW` called with `wType=EVENTLOG_INFORMATION_TYPE`, `wCategory=1`, `dwEventID=2`
- `write()` with Error level: `ReportEventW` called with `wType=EVENTLOG_ERROR_TYPE`, `dwEventID=4`
- Source not registered: `write()` returns without calling `ReportEventW` (verified via hook counter)
- `Utf8ToWide("Hello, World!")` returns `L"Hello, World!"` (ASCII pass-through)
- `Utf8ToWide("مرحبا بالعالم")` returns correct UTF-16 Arabic string (3-byte UTF-8 → UTF-16 surrogate pairs if needed)
- `Utf8ToWide(nullptr)` returns `L""` (empty, no crash)
- `Utf8ToWide("")` returns `L""` (empty, no crash)
- `buffer.format(metadata)` returns a non-empty string for a valid log record (Outcome A verification)

## Failure Mode
- `buffer.format(metadata)` doesn't compile (Outcome B or C): the sink won't build. This is a compile-time failure, caught before any test runs. Risk: medium — requires changing `format()` call to `data()+size()` or `PatternFormatter`.
- Source not registered: EventLog goes dark silently. No crash, no data loss (other sinks still work). Operator impact: missed alerts. Detection: periodic test write + `Get-WinEvent` health check.
- `ReportEventW` fails at runtime: silent message drop. No crash, no corruption. Degraded monitoring.
- UTF-8 to UTF-16 conversion fails for malformed input: empty message written to EventLog instead of the actual message. Silent information loss.
- Event Log full: Windows drops oldest events to make room; sink continues to write, newest events overwrite oldest. No crash, rolling window.

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Buffer extraction API verification with 3 outcomes + compile-time path selection | §2a API Verification | GAP-2-P01-1: ✅ RESOLVED; updated test harness |
| Explicit switch statement for LogLevelToEventId | §5 LogLevelToEventId | GAP-2-P01-2: ✅ RESOLVED |
| 32KB message truncation with one-time compare_exchange_strong warning | §5 write() implementation | GAP-2-P01-3: ✅ RESOLVED; added truncation scenario |
| GetLastError diagnostics on RegisterEventSourceA failure | §5 constructor diagnostics | GAP-2-P01-4: ✅ RESOLVED; added diagnostic scenario |
| VerifyEventLogSource() health check helper | §6 EventLog Source Verification | GAP-2-P01-5: ✅ RESOLVED; added health-check scenario |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-2-P01-1 | `buffer.format(metadata)` is UNVERIFIED against Quill v10.0.1. The entire sink implementation depends on this single API call. | If the API does not exist or is not public, the implementation must use a different extraction strategy (data+size or PatternFormatter). This affects the `write()` method's core logic. | Add a pre-implementation verification step: open `quill/Buffer.h` and search for `format`, `data`, `size`. Define a compile-time trait or `#ifdef` to select the correct extraction path. Document the chosen outcome in the code. | ✅ RESOLVED — AA spec §2a adds API verification with 3 outcomes and compile-time path selection |
| GAP-2-P01-2 | `LogLevelToEventId` casts `static_cast<uint8_t>(level)` assuming Quill's `LogLevel` enum has specific integer values 0-5. If Quill changes enum values, the mapping breaks silently. | Silent regression: Critical logs written as Event ID 0 (Information) instead of 5 (Error). Operators may miss critical alerts because EventLog filters use Event ID ranges. | Use an explicit switch statement mapping each Quill `LogLevel` enumerator to a fixed Event ID. Add a `static_assert` or compile-time check that the enum values match expectations. | ✅ RESOLVED — AA spec §5 uses explicit switch mapping for LogLevelToEventId |
| GAP-2-P01-3 | No maximum message size handling. `ReportEventW` has a 32KB limit for the total event; messages exceeding this cause `FALSE` return. | Long log messages (stack traces, full order dumps) silently fail to appear in EventLog. | Truncate the UTF-16 message to 32KB - header overhead before calling `ReportEventW`. Log a warning on the first truncation per sink instance. | ✅ RESOLVED — AA spec §5 adds 32KB truncation + one-time compare_exchange_strong warning |
| GAP-2-P01-4 | `RegisterEventSourceA` returns a HANDLE that should be treated as opaque, but the spec caches it in a raw `HANDLE` member. | No issue with the HANDLE itself, but there's no mention of `GetLastError()` diagnostics if registration fails. | Log the `GetLastError()` value via `OutputDebugStringA` when registration fails, to aid ops debugging "why am I not seeing EventLog entries?". | ✅ RESOLVED — AA spec §5 adds GetLastError diagnostics via OutputDebugStringA |
| GAP-2-P01-5 | No mention of EventLog source registration persistence. Source registration is per-machine, lost on OS reinstall, and not restored by application installer. | The ops manual dependency is a deployment gap — no automated check for source presence before starting the application. | Add a `Logger_Adapter_VerifyEventLogSource()` helper function that attempts a write and checks the return value; call it during application health check. | ✅ RESOLVED — AA spec §6 adds VerifyEventLogSource() health check helper |
