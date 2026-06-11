# Impact Analysis: Multi-Logger Shim

## Summary
Total GAPs: 5 | P0: 1 | P1: 2 | P2: 2 | API Changes: 1

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-3-C01-1 | 🔴 P0 | `LoggerEntry::log_level` numeric range 0-7 does not align with Quill's LogLevel enum values: 0 maps to Trace (not Debug). Level clamping rule maps to wrong level. | Operators setting level=0 expecting Debug get Trace, which may be filtered — silent diagnostic gap during crisis debugging. | No | 🛠️ Fix now |
| GAP-3-C01-2 | 🟡 P2 | `ValidateLoggerEntry` mixes validation with mutation (clamps level then returns fail code). Caller trusts already-mutated entry. | None if code is correct; code quality/maintainability issue. | No | 📝 Document only |
| GAP-3-C01-3 | ⚠️ P1 | Empty `sink_names` vector not covered by decision tree — logger created with no sinks, messages silently lost. | A misconfigured LoggerEntry silently drops all messages. Subsystem operates but logs go nowhere. | No | 🛠️ Fix now |
| GAP-3-C01-4 | 🟡 P2 | No testability hooks: `LoggerRegistry` is a singleton with static methods, no reset. State leaks between tests. | None (test infrastructure only). | Yes — add `ResetForTesting()` | 🛠️ Fix now |
| GAP-3-C01-5 | ⚠️ P1 | `CategoryFromLoggerName()` mapping referenced but not specified — EventLog sink cannot correctly categorize messages. | EventLog sink mis-categorizes all messages from named loggers; cannot filter by subsystem in Windows Event Viewer. | No | 🛠️ Fix now |

## Recommended AA Changes
- GAP-3-C01-1 🛠️ Fix now: Replace raw integer log_level with `quill::LogLevel` enum. Define `ToQuillLogLevel(int32_t)` with explicit switch/map. Update clamping rule to use Quill enum values directly.
- GAP-3-C01-2 📝 Document only: Rename function to `ValidateAndSanitizeLoggerEntry` in spec or add note that validation includes clamping as a sanctioned mutation.
- GAP-3-C01-3 🛠️ Fix now: Add `Fail_NoSinks` to decision tree. Skip entry with empty sink_names and emit warning diagnostic.
- GAP-3-C01-4 🛠️ Fix now: Add `LoggerRegistry::ResetForTesting()` guarded by `#ifdef _DEBUG`. Document as not thread-safe, for test use only.
- GAP-3-C01-5 🛠️ Fix now: Add mapping table: Emergency→1, OrderExecution→2, Risk→3, MarketData→4, HealthProbe→5.
