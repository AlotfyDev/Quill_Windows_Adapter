# Impact Analysis: Custom Pattern Per Sink

## Summary
Total GAPs: 3 | P0: 2 | P1: 0 | P2: 1 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-4-M06-1 | 🔴 P0 | AA spec calls `opts.set_pattern(cfg.format)`, `opts.set_timestamp_format(...)`, `opts.set_utc_timestamp(cfg.utc)` — but Quill v10.0.1's `PatternFormatterOptions` has PUBLIC MEMBER VARIABLES (`format_pattern`, `timestamp_pattern`, `timestamp_timezone`), not setter methods. Code will NOT compile. | Entire custom pattern feature is blocked — every caller of `MakePatternFormatter` gets a compile error. Zero custom patterns possible. | No — internal API only | 🛠️ Fix now — Change to direct member assignment: `opts.format_pattern = cfg.format;`, `opts.timestamp_pattern = cfg.timestamp_format;`, `opts.timestamp_timezone = cfg.utc ? Timezone::GmtTime : Timezone::LocalTime;` |
| GAP-4-M06-2 | 🟡 P2 | AA spec says `PatternFormatterOptions` is "per-logger, not per-sink" but `FileSinkConfig` and `ConsoleSinkConfig` both have `set_override_pattern_formatter_options(...)` which IS per-sink. The workaround of creating separate loggers is unnecessary. | The current approach (per-logger patterns via separate loggers) works correctly but is less elegant. Not a bug — a missed optimization. | No — architectural | ❌ Accept — Current approach works. Document per-sink override as future optimization in a "Design Tradeoff" note. |
| GAP-4-M06-3 | 🔴 P0 | AA spec's `kValidPatternTokens` uses wrong token names: `%(logger_id)` should be `%(logger)`, `%(file)` should be `%(file_name)`, `%(line)` should be `%(line_number)`, `%(level)` should be `%(log_level)`, `%(function_name)` should be `%(caller_function)`, `%(level_short_name)` should be `%(log_level_short_code)`, `%(thread)` should be `%(thread_id)`, `%(file_path)` should be `%(full_path)`. Missing tokens: `%(source_location)`, `%(short_source_location)`, `%(tags)`, `%(named_args)`, `%(process_id)`. | Every custom pattern would fail validation. Every pattern with `%(logger_id)` etc. would be rejected with "unknown token" error. Feature completely broken. | No — internal validation logic only | 🛠️ Fix now — Replace `kValidPatternTokens` with actual Quill v10.0.1 token names from `PatternFormatterOptions.h:47-62`. Update Step 1 documentation examples. |

## Recommended AA Changes
- **GAP-4-M06-1**: Replace all `opts.set_*()` calls with direct member assignments in `MakePatternFormatter()`. Add `#include <quill/core/Timezone.h>` for `Timezone::GmtTime`/`Timezone::LocalTime`.
- **GAP-4-M06-2**: Add "Design Tradeoff" note below the Quill limitation paragraph: "Note: Quill also supports per-sink pattern override via `set_override_pattern_formatter_options()` on `FileSinkConfig`/`ConsoleSinkConfig` — this could simplify the architecture in a future iteration."
- **GAP-4-M06-3**: Replace entire `kValidPatternTokens` set with correct Quill v10.0.1 tokens. Update Step 1 pattern examples to use `%(logger)`, `%(file_name)`, `%(line_number)`, etc.
