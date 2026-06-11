# Impact Analysis: Dynamic Log Level

## Summary
Total GAPs: 5 | P0: 0 | P1: 1 | P2: 4 | API Changes: 1

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-3-C04-1 | ⚠️ P1 | No parameter validation for `quill::LogLevel` value in `SetLogLevel`. Invalid enum produces UB — possible silent incorrect filtering or assertion. | Passing invalid LogLevel could silently corrupt filtering, causing unexpected message suppression or flooding during crisis debugging. | Yes — add validation wrapper | 🛠️ Fix now |
| GAP-3-C04-2 | 🟡 P2 | `GetLogLevel` returns `None` for both non-existent and "not set" loggers — callers cannot distinguish. | Monitoring tools may show confusing "None" state, indistinguishable from intentionally-unset logger. | Yes (suggested) — std::optional or error_code out-param | 📝 Document only |
| GAP-3-C04-3 | 🟡 P2 | Visibility delay of level change is undefined. On weakly-ordered architectures, old level may persist microseconds. | Operators may expect immediate suppression; eventual consistency is acceptable but undocumented. | No | 📝 Document only |
| GAP-3-C04-4 | 🟡 P2 | No bulk/wildcard `SetLogLevel` for crisis response — operators must set 5+ loggers individually. | Wastes precious seconds during incident response. Not required for v0.2.0. | Yes (future) | ❌ Accept |
| GAP-3-C04-5 | 🟡 P2 | No spec on whether `SetLogLevel` persists across future hot-reload — runtime changes could be silently overwritten. | Operators debug a live issue, config hot-reload resets levels — debugging session disrupted. | No | 📝 Document only |

## Recommended AA Changes
- GAP-3-C04-1 🛠️ Fix now: Add validation: check `level` is in valid enum range [Trace..Critical] ∪ {None} before calling Quill. Return `false` for invalid values.
- GAP-3-C04-2 📝 Document only: Document that `None` return value means logger does not exist. Accept API as-is for v0.2.0.
- GAP-3-C04-3 📝 Document only: Add note: "Level change visible to all threads within bounded time (microseconds, architecture-dependent). No memory barrier issued. Sequential consistency not guaranteed."
- GAP-3-C04-4 ❌ Accept: Not required for v0.2.0. Note as future enhancement.
- GAP-3-C04-5 📝 Document only: Document that runtime level changes are ephemeral and overwritten by config reload.
