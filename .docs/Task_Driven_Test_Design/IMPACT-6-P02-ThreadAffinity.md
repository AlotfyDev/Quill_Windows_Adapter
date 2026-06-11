# Impact Analysis: Windows Thread Affinity

## Summary
Total GAPs: 4 | P0: 0 | P1: 1 | P2: 3 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-6-P02-1 | 🟡 P2 | AA spec uses "main thread" but code uses "calling thread." If a non-main thread calls `InitializeLogging()`, the backend inherits THAT thread's affinity. | Documentation misleading; correct behavior (calling thread affinity is inherited) is actually correct by design. | No | 📝 Document only |
| GAP-6-P02-2 | ⚠️ P1 | Save/restore of original affinity: `SetThreadAffinityMask` returns 0 both on failure and when previous mask was "any processor" (mask=0). If mask was 0, restore is skipped and calling thread is permanently pinned. | **Calling thread performance degradation.** If the calling thread had no explicit affinity (mask=0, common default), the restore is skipped and all subsequent work on that thread is permanently pinned to one processor. Cascading latency impact on the entire process. | No | 🛠️ Fix now |
| GAP-6-P02-3 | 🟡 P2 | No mechanism for runtime affinity change during incident response. | Operational inflexibility but acceptable tradeoff — restart with new config is the workaround. | Yes (new API) | ❌ Accept |
| GAP-6-P02-4 | 🟡 P2 | >64 CPU systems require `use_group_affinity = true` but spec treats it as optional. On large systems with `use_group_affinity = false`, backend stays on group 0. | Cross-NUMA penalties on large systems; suboptimal log backend performance. | No | 🛠️ Fix now |

## Recommended AA Changes
- **GAP-6-P02-1**: Rename "main thread" to "calling thread" throughout the spec; add note that `InitializeLogging()` must be called from the thread whose affinity should be inherited
- **GAP-6-P02-2**: Fix save/restore to properly handle `SetThreadAffinityMask` returning 0 (distinguish failure from "any processor" via `GetLastError`); update `InitializeLogging` code
- **GAP-6-P02-3**: Add "Design Tradeoff" note explaining why runtime affinity change is not implemented (complexity vs. restart workaround)
- **GAP-6-P02-4**: Add documentation: for >64 CPU systems, `use_group_affinity = true` is mandatory; add runtime check or static_assert recommendation
