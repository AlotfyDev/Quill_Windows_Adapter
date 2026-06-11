# Test Design: UTC Timezone Support

## Under Spec
- AA File: `AA-M14-UTC_Timezone.md`
- Phase: 1
- Key Requirements:
  - Add `bool utc_timestamp = true` to `config::PatternConfig` (C++17-compatible default member initializer)
  - Wire to Quill's `PatternFormatterOptions::set_timezone()` using `quill::Timezone::GmtTime` (UTC) or `quill::Timezone::LocalTime`
  - Fallback if `GmtTime` absent: manual UTC conversion via Windows `FileTimeToSystemTime` or C++17 `<chrono>`; formatter set to LocalTime but timestamp pre-adjusted to UTC
  - Compile-time check (`static_assert` or SFINAE) at PR time to verify `GmtTime` exists — NOT deferred to test time
  - Default is UTC (breaking change from previous local-time behavior)
  - Timestamp format string unchanged — only timezone suffix changes: UTC=`Z`, Local=offset (e.g., `-04:00`). Microsecond resolution.
  - All sinks share the same timezone from the single `LoggingConfig::PatternConfig` (no per-sink TZ in v0.2.0)
  - Consumer detection: `#ifdef LOGGER_ADAPTER_UTC_DEFAULT` or version macro

## Test Harness
- **Fixture**: A test that creates a `LoggingConfig` with a console sink (or temp file sink), calls `InitializeLogging()`, writes one log message with a known timestamp marker, captures the output, and parses the timestamp portion to verify timezone. Each test case requires process isolation (same Quill singleton constraint as M08).
- **Mock vs Real**: Real Quill v10.0.1. The `PatternFormatter` is a Quill internal — no mocking. Timestamp capture uses a file sink redirected to a temp file or a string-sink if Quill v10.0.1 supports it. If no string-sink exists, use a rotating file sink to a temp directory, then read the file.
- **Precondition Requirements**: Quill v10.0.1 installed. `PatternConfig.hpp` has `utc_timestamp` field defaulting to `true`. `InitializeLogging()` wires the field to `PatternFormatterOptions::set_timezone()`. System timezone is set to a non-UTC zone (e.g., EST/UTC-5 or IST/UTC+5:30) to make the difference observable.

## Scenarios

### Positive Cases
- **Default is UTC**: `LoggingConfig{}` produces log timestamps in UTC. Capture current UTC time via `std::chrono::system_clock::now()` before writing the log message. The log output timestamp differs from local system time by the expected offset.
- **Explicit UTC**: `config.pattern.utc_timestamp = true` — timestamp is UTC. Same assertion as default case.
- **Explicit Local Time**: `config.pattern.utc_timestamp = false` — timestamp matches local system time. Assert that the log timestamp differs from UTC by the system's timezone offset.
- **Time format string preserved**: The pattern format string (e.g., `"%(time) [%(thread)] %(message)"`) is unchanged when toggling UTC vs local. Only the timezone of the timestamp component changes.
- **Cross-DST boundary**: Set system timezone to one that observes DST (e.g., US Eastern). Log with `utc_timestamp = true` during DST and during standard time. UTC offset is 0 in both cases. Local time offset differs by 1 h. (Manual orchestration — requires system timezone change.)

### Negative / Error Cases
- **Invalid timezone enum**: Verifies that `quill::Timezone::GmtTime` and `quill::Timezone::LocalTime` are valid enumerators in Quill v10.0.1. Compile-time check — the test itself is a `static_assert` or a small compilation unit. If the enum does not exist, compilation fails with a clear error (blocking concern per spec).
- **After `quill::Backend::start()`**: The `PatternFormatterOptions` must be configured before the backend starts. Verify that `InitializeLogging()` sets timezone on the formatter BEFORE calling `quill::Backend::start()`. Per spec, this is already the case since timezone is part of `LoggingConfig` which is consumed by `InitializeLogging` before `start()`. If the order is reversed, timestamps are local. (Negative: introduce a deliberate fork to document the ordering constraint.)
- **Manual UTC fallback (GmtTime absent)**: If `quill::Timezone::GmtTime` does not exist, the fallback path must compile and produce numerically-correct UTC timestamps. Test: set system to non-UTC timezone, set `utc_timestamp = true`, write log message. Parse timestamp — it should match UTC (not local). The suffix in the formatted output may show local offset (since formatter uses LocalTime), but the numeric values must be UTC. Document this trade-off.
- **Sink timezone scope (all sinks)**: Create config with console + file sinks, set `utc_timestamp = true`. Both sinks must output UTC timestamps. Create config with `utc_timestamp = false`. Both sinks must output local timestamps. Verify via capturing output from both sinks.
- **Timestamp format specification**: With `utc_timestamp = true`, timestamp suffix must be `Z` (e.g., `2026-06-11 12:00:00.000000Z`). With `utc_timestamp = false`, suffix must be the local offset (e.g., `2026-06-11 08:00:00.000000-04:00`). Resolution must be microseconds (6 digits). Pattern format string must be identical in both cases.
- **Pattern not set**: If no pattern is explicitly configured (empty `PatternConfig`), UTC is the default. Verify that an unset pattern still results in UTC timestamps (default member initializer is correct).
- **Multiple sinks with different patterns**: If the Logger_Adapter supports per-sink pattern config in the future, each sink's timezone should be independently configurable. For v0.2.0, all sinks share the same timezone (the model is one `LoggingConfig` for all sinks). Verify this assumption holds.

### Production Realities
- **System timezone change during runtime**: Operator changes the OS timezone while the process is running. Quill reads timezone once at formatter creation. UTC timestamps are unaffected by this change (correct). Local timestamps would shift mid-file, creating an inconsistent audit trail. Test: verify that local-time-mode log file has a discontinuity if TZ changes. Document this as a reason to prefer UTC in production.
- **DST transition (local time)**: Spring-forward (02:00 → 03:00). A log message at 01:59 and another at 02:01 (which is actually 03:01) both appear in sequence. UTC timestamps are monotonic regardless. Local time may have a gap or duplication. Test: verify UTC timestamps are strictly monotonic.
- **Leap second**: System clock may receive a leap second (23:59:60). Quill's time handling may or may not support this. UTC timestamps should continue to be monotonic; local time may exhibit a 1-second stall. Not critical for v0.2.0 — document as known limitation.

### Thread Safety
- **PatternConfig is read-once**: The `utc_timestamp` field is read during `InitializeLogging()` and applied to the `PatternFormatterOptions`. After `quill::Backend::start()`, it is not read again. No concurrent access to the config struct during normal operation.
- **Per AA-C05**: `InitializeLogging()` is `std::call_once` guarded — no concurrent calls. Any read of `PatternConfig` after init is a no-op (config is a copy passed by value).
- **No race on timezone change**: The timezone is baked into the formatter at creation. There is no runtime SetTimezone() call. Thread safety is trivially satisfied.

## Assertions
- Default `LoggingConfig{}` produces UTC timestamps.
- Setting `utc_timestamp = false` produces local timestamps that differ from UTC by the system's `std::chrono::current_zone()->get_info(std::chrono::system_clock::now()).offset` (C++20) or equivalent Win32 `GetTimeZoneInformation`.
- Timestamps in log output are parseable as ISO 8601 with timezone indicator (e.g., `2026-06-11 12:00:00.000Z` for UTC, `2026-06-11 08:00:00.000-04:00` for local).
- The pattern format string is unaffected by the timezone change.
- Compilation succeeds with `quill::Timezone::GmtTime` and `quill::Timezone::LocalTime` in Quill v10.0.1.
- UTC timestamps are strictly monotonic even across system DST transitions.
- If `quill::Timezone::GmtTime` is absent, the fallback path compiles and produces numerically-correct UTC timestamps (verified at PR compile-time check).
- All sinks (console + file) output the same timezone when sharing a single `LoggingConfig::PatternConfig`.
- Timestamp format suffix is `Z` for UTC, `±HH:MM` for local (verified against Quill v10.0.1 behavior).

## Failure Mode
- **TestDefaultUtcTimestamp fails**: Default is local time → audit trail corruption across regions. **Impact: Regulatory non-compliance (MiFID II, SEC). Silent data integrity issue.**
- **TestLocalTimestampCorrect fails**: Local time offset is wrong → all timestamps are wrong by a fixed offset. **Impact: Log analysis tools produce incorrect results.**
- **TestPatternUnchanged fails**: The format string is altered when timezone toggles. **Impact: Log parsers break.**
- **TestApiExists fails (compile-time)**: `quill::Timezone::GmtTime` does not exist in Quill v10.0.1 → UTC cannot be implemented without an alternative approach (e.g., manual time conversion before formatting). **Impact: Blocking — M14 cannot be implemented until an alternative timezone API is found.** This failure MUST be caught before the implementation PR is merged.
- **TestUtcMonotonic fails**: UTC timestamps go backwards or have gaps. **Impact: Audit trail integrity violation — time series queries return wrong results.**

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Added manual UTC fallback (FileTimeToSystemTime / C++17 chrono) | Step 3 (Fallback note) | Added scenario "Manual UTC fallback (GmtTime absent)"; updated Key Requirements |
| PR-time compile-time check for GmtTime existence | Step 3 (Verification at PR time) | Updated Key Requirements; ensured check is not deferred to test time |
| Timestamp format specification (Z suffix, microsecond resolution) | Step 4b | Added scenario "Timestamp format specification"; updated Assertions |
| All sinks share same timezone from single PatternConfig | Step 4c | Added scenario "Sink timezone scope (all sinks)"; updated Key Requirements |
| Consumer version detection macro | Step 4 | Updated Key Requirements with `#ifdef LOGGER_ADAPTER_UTC_DEFAULT` |

## Spec Gap Notes (SGN)

### Resolved Gaps

These GAPs were raised during test design and subsequently resolved by Impact Analysis fixes applied to the AA spec.

| Gap ID | Issue | Resolution | AA Spec Section |
|--------|-------|------------|-----------------|
| GAP-1-M14-1 | No fallback if GmtTime absent (blocking concern) | ✅ RESOLVED — AA Step 3 adds manual UTC fallback via Windows `FileTimeToSystemTime` or C++17 `<chrono>` with pre-adjusted clock. PR-time compile check verifies before merge. | Step 3 |
| GAP-1-M14-2 | Per-sink vs global timezone scope | ✅ RESOLVED — AA Step 4c documents all sinks share the same timezone from single `LoggingConfig::PatternConfig`. | Step 4c |
| GAP-1-M14-3 | Silent backward-compat break | ✅ RESOLVED — AA Step 4 documents breaking change with version macro (`LOGGER_ADAPTER_UTC_DEFAULT`) for consumer detection. | Step 4 |
| GAP-1-M14-4 | Timestamp format resolution unspecified | ✅ RESOLVED — AA Step 4b specifies microsecond resolution, `Z` suffix for UTC, offset suffix for local. Verified against Quill v10.0.1 behavior. | Step 4b |
