# Test Design: Custom Pattern Per Sink

## Under Spec
- AA File: `AA-M06-CustomPatternPerSink.md`
- Phase: 4
- Key Requirements:
  - `ValidatePattern()` checks pattern tokens against allowlist `kValidPatternTokens`
  - Invalid token → descriptive error message with position
  - Empty pattern → allowed (uses Quill default, not silently custom)
  - `MakePatternFormatter()` constructs `PatternFormatterOptions` from `PatternConfig`, throws `std::invalid_argument` on invalid pattern
  - Fallback: on invalid pattern, logger created with default pattern (no crash)
  - Console and file sinks use independently configured patterns
  - UTC timestamp option works independently per pattern
  - Token grammar documented with Quill v10.0.1 names: `%(time)`, `%(log_level)`, `%(log_level_short_code)`, `%(logger)`, `%(thread_id)`, `%(thread_name)`, `%(file_name)`, `%(full_path)`, `%(line_number)`, `%(message)`, `%(caller_function)`, `%(process_id)`, `%(source_location)`, `%(short_source_location)`, `%(tags)`, `%(named_args)`

## Test Harness
- **Fixture**: Unit tests for `ValidatePattern()` (static function, no Quill init needed). Integration tests for `MakePatternFormatter()` constructing real `PatternFormatterOptions`. End-to-end tests with Quill backend, temp FileSink, verifying actual output matches pattern.
- **Real vs mock**: `ValidatePattern` is pure logic — no mocking. `MakePatternFormatter` constructs real Quill `PatternFormatterOptions` (but does NOT apply it — that's done by `quill::Frontend::create_or_get_logger`). For end-to-end, use real Quill backend + FileSink.
- **Preconditions**: `PatternConfig` struct defaults match legacy behavior. `kValidPatternTokens` contains all documented tokens.

## Scenarios

### Positive Cases
- Validate valid tokens: Each token in `kValidPatternTokens` individually → `ValidatePattern` returns empty string.
- Validate composite pattern: `"%(time) [%(log_level)] %(logger) %(message)"` → returns empty.
- Validate file pattern: `"%(time) [%(log_level)] [%(thread_id)] %(file_name):%(line_number) %(message)"` → returns empty.
- Validate with literal `%` outside tokens: `"100%% done: %(message)"` → returns empty (bare `%` without `()` is skipped).
- `MakePatternFormatter` with valid pattern: Returns `PatternFormatterOptions` with `format_pattern` set to the string. Verify via `opts.format_pattern` (public member, not setter).
- `MakePatternFormatter` assigns public members directly: `opts.format_pattern = cfg.format`, `opts.timestamp_pattern = cfg.timestamp_format`, `opts.timestamp_timezone = cfg.utc ? Timezone::GmtTime : Timezone::LocalTime` — no setter method calls.
- `MakePatternFormatter` with UTC timestamp: `cfg.utc = true` → `opts.timestamp_timezone == Timezone::GmtTime`.
- `MakePatternFormatter` with local timestamp: `cfg.utc = false` → `opts.timestamp_timezone == Timezone::LocalTime`.
- `MakePatternFormatter` with custom timestamp format: `cfg.timestamp_format = "%Y-%m-%d %H:%M:%S"` → `opts.timestamp_pattern` set accordingly.
- End-to-end: Create logger with pattern `"TEST|%(message)|END"`, write `LOG_INFO(logger, "hello")`, flush. File must contain `TEST|hello|END`.
- End-to-end UTC: Pattern with `%(time)`, `cfg.utc = true`. Write log, read timestamp, verify it's UTC (compare with `std::gmtime`).
- Per-sink independent patterns: Create two loggers with different patterns. Write same message to both. Output files must have different formats.
- Fallback on invalid pattern: Pass invalid pattern string. `MakePatternFormatter` throws `std::invalid_argument`. Catch block creates logger without pattern. Logger uses Quill default — verify output looks like default format (contains `[thread_id]`, `LOG_`, `[logger_name]`).

### Negative / Error Cases
- Unknown token: `ValidatePattern("%(bad_token)")` → returns error string containing `unknown pattern token` and `bad_token` and position `0`.
- Unclosed token: `ValidatePattern("%(time")` → returns `unterminated pattern token starting at position 0`.
- Empty string: `ValidatePattern("")` → returns `pattern format string must not be empty`.
- Null byte in middle: `ValidatePattern("%(time)\0%(bad)")` → scans up to null, token `%(bad)` may or may not be found depending on `std::string` behavior. Must not crash.
- Extremely long pattern: `std::string(100000, 'x') + "%(message)"` → must not cause stack overflow (scanned linearly, not recursive).
- Pattern with only invalid content: `"%%%(z) %% %(message)"` → error for `%(z)` at position of `%(z)`.
- `MakePatternFormatter` with invalid pattern propagates exception: `EXPECT_THROW(MakePatternFormatter(cfg), std::invalid_argument)`.
- Timestamp format validation: The AA spec notes Quill does NOT validate `set_timestamp_format`. Pass invalid format like `"%%%Q"` — must not throw at `MakePatternFormatter` time (deferred to Quill formatting). Verify no exception.

### Production Realities
- Pattern applied to high-throughput logger: 1M messages with custom pattern. Measure throughput vs default pattern. Custom patterns (especially with `%(file):%(line)`) trigger more formatting work on the backend thread. Document overhead ratio.
- Large `%(named_args)` with LOGV: Pattern including `%(named_args)` with 10+ key-value pairs. All named args must be rendered correctly in output.
- Logger creation during runtime: `InitializeLogging` called after some log messages already written. New pattern applied correctly to subsequent messages without affecting already-formatted records.
- Multiple loggers sharing same sink: Pattern is per-logger (Quill limitation), not per-sink. Two loggers writing to same FileSink but with different patterns — output file will contain interleaved differently-formatted lines. Document that this is expected.

### Thread Safety
- `ValidatePattern`: Pure function, no mutable state. Thread-safe by construction. Test via concurrent calls from multiple threads.
- `MakePatternFormatter`: Allocates strings; no shared state between calls. Thread-safe.
- Pattern applied at logger creation only: `create_or_get_logger` with pattern. No concurrent modification possible — logger not yet published. No race.
- Multiple loggers with different patterns writing to same sink: Sink writes formatted records sequentially (backend is single-threaded). No interleaving within a single record.

## Assertions
- `ValidatePattern` returns empty string for all tokens in `kValidPatternTokens`
- `ValidatePattern("")` returns non-empty error
- `ValidatePattern("%(bad)")` returns error containing `bad` and position string
- `MakePatternFormatter` with invalid pattern throws `std::invalid_argument` whose `what()` contains the ValidatePattern error message
- After fallback, logger produces output matching Quill's default pattern format (check for `[thread_id]` and `LOG_` markers)
- `opts.timestamp_timezone == Timezone::GmtTime` when `cfg.utc == true`
- `opts.timestamp_timezone == Timezone::LocalTime` when `cfg.utc == false`
- Console and file pattern strings are stored independently in `LoggingConfig`
- End-to-end: `TEST|hello|END` appears literally in output file when pattern is `"TEST|%(message)|END"`

## Failure Mode
- `ValidatePattern` failing on valid tokens → **build regression** preventing any custom pattern from being used. Operators forced to use defaults.
- `ValidatePattern` passing on invalid tokens → **silent pattern corruption**. Quill renders unknown tokens literally (e.g., `%(bad)` appears as text `%(bad)`). Operators think pattern is valid but output is wrong.
- Fallback not working (exception propagates to caller) → **startup crash**. `InitializeLogging` fails, system doesn't start.
- Timestamp timezone ignored → **silent data corruption**. UTC timestamps rendered as local time, affecting audit trail and cross-region log correlation.
- Pattern bleed between loggers → **configuration corruption**. Console pattern applied to file sink. Operators see wrong format in wrong sink.

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| `MakePatternFormatter` uses public member vars (not setters) | Step 4 — Create make_pattern helper | Added assertion verifying public member assignment |
| Token names corrected to Quill v10.0.1 | Step 1 — Publish Pattern Grammar | Under Spec token names updated; scenario patterns updated |
| `ValidatePattern()` function added (M06-C fix) | Step 4 — validation logic | Scenarios already cover validation; assertion updated |
| Design Tradeoff note about per-sink overrides | Problem section | GAP-4-M06-2 updated to note partially addressed |
| Spec Gap GAP-4-M06-1 | Setter methods vs public members | Marked ✅ RESOLVED |
| Spec Gap GAP-4-M06-3 | Wrong token names | Marked ✅ RESOLVED |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-4-M06-1 | The AA spec calls `opts.set_pattern(cfg.format)`, `opts.set_timestamp_format(...)`, and `opts.set_utc_timestamp(cfg.utc)` on `PatternFormatterOptions`, but Quill v10.0.1's `PatternFormatterOptions` header (`PatternFormatterOptions.h:66-117`) exposes these as PUBLIC MEMBER VARIABLES (`format_pattern`, `timestamp_pattern`, `timestamp_timezone`), not setter methods. The spec code will not compile. | The implementation cannot be written as specified — every user of `MakePatternFormatter` would get a compile error. Blocking fix for the entire feature. | Change `MakePatternFormatter` to assign public members directly: `opts.format_pattern = cfg.format;`, `opts.timestamp_pattern = cfg.timestamp_format;`, and for timezone use `opts.timestamp_timezone = cfg.utc ? Timezone::GmtTime : Timezone::LocalTime;`. | ✅ RESOLVED |
| GAP-4-M06-2 | The AA spec says `PatternFormatterOptions` is "per-logger, not per-sink" but `FileSinkConfig` and `ConsoleSinkConfig` both have `set_override_pattern_formatter_options(...)` which IS per-sink. The workaround of creating separate loggers is unnecessary — individual sinks can have their own pattern override. | The architecture bypasses Quill's per-sink pattern override capability, forcing extra logger objects and complicating the LoggerAdapter config model. | Re-evaluate: either use `create_or_get_logger(entry.name, sinks, pattern)` for per-logger patterns (current AA approach) OR assign pattern overrides on individual sink configs. The per-sink override is cleaner for the common case of console vs file having different patterns. | 📝 Partially addressed — AA spec adds Design Tradeoff note documenting the per-sink alternative |
| GAP-4-M06-3 | The AA spec's `kValidPatternTokens` uses `%(logger_id)`, `%(file)`, `%(line)`, but the actual Quill `PatternFormatterOptions` header (`PatternFormatterOptions.h:47-62`) lists different token names: `%(logger)`, `%(file_name)`, `%(line_number)`, `%(full_path)`, `%(short_source_location)`, `%(source_location)`, `%(process_id)`, `%(named_args)`, `%(tags)`. Some spec tokens (`%(logger_id)`) don't exist in Quill v10.0.1. | Using spec tokens would always fail validation or produce literal text output instead of the expected substitution. | Update `kValidPatternTokens` to match actual Quill v10.0.1 token names. Add aliases if desired (e.g., `%(logger)` for `%(logger_id)`) by normalizing in `ValidatePattern`. | ✅ RESOLVED |
