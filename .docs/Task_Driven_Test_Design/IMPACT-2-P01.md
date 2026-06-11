# Impact Analysis: Windows EventLog Sink

## Summary
Total GAPs: 5 | P0: 1 | P1: 2 | P2: 2 | API Changes: 1

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-2-P01-1 | 🔴 P0 | `buffer.format(metadata)` UNVERIFIED against Quill v10.0.1 — entire sink depends on this single API | Won't build if API doesn't exist; build-time failure | No | 🛠️ Fix now |
| GAP-2-P01-2 | ⚠️ P1 | `LogLevelToEventId` cast assumes Quill enum values 0-5 — fragile | If Quill changes enum values, Critical logs appear as Event ID 0 (Information) — operators miss critical alerts | No | 🛠️ Fix now |
| GAP-2-P01-3 | ⚠️ P1 | No maximum message size handling — `ReportEventW` 32KB limit | Long log messages (stack traces, order dumps) silently fail to appear in EventLog | No | 🛠️ Fix now |
| GAP-2-P01-4 | 🟡 P2 | No `GetLastError()` diagnostics when `RegisterEventSourceA` fails | Ops can't diagnose "why am I not seeing EventLog entries?" | No | 🛠️ Fix now |
| GAP-2-P01-5 | 🟡 P2 | No automated check for EventLog source registration before startup | Source may be missing silently after OS reinstall — deployment gap | Yes | 🛠️ Fix now |

## Recommended AA Changes
- **GAP-1**: Add pre-implementation verification step; use compile-time trait or `#ifdef` to select correct extraction path (`format()`, `data()+size()`, or `PatternFormatter`)
- **GAP-2**: Replace `static_cast` with explicit `switch` statement mapping each Quill `LogLevel` to fixed Event ID; add `static_assert` guard
- **GAP-3**: Truncate UTF-16 message to 32KB - header overhead before calling `ReportEventW`; log warning on first truncation
- **GAP-4**: Log `GetLastError()` via `OutputDebugStringA` when `RegisterEventSourceA` fails
- **GAP-5**: Add `Logger_Adapter_VerifyEventLogSource()` helper that attempts write and checks return value; document as deployment health check
