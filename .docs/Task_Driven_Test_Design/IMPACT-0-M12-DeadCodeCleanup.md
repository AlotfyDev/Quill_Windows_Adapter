# Impact Analysis: Dead Code Cleanup

## Summary
Total GAPs: 4 | P0: 0 | P1: 1 | P2: 3 | API Changes: 1

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-0-M12-1 | 🟡 P2 | No negative compilation test to prevent re-introduction of dead symbols | Future commits could silently re-add SetupCrashLogger without detection | No | 📝 Document only |
| GAP-0-M12-2 | ⚠️ P1 | No ABI compatibility check — struct layout change breaks external consumers | Silent memory corruption if new DLL used with old EXE (wrong field offsets) | Yes | 🛠️ Fix now |
| GAP-0-M12-3 | 🟡 P2 | "Proven still referenced" check is vague — transitive includes ambiguous | Dead file may be kept unnecessarily; definition of "referenced" is unclear | No | 📝 Document only |
| GAP-0-M12-4 | 🟡 P2 | No config file compatibility test for stale JSON/YAML fields | Production config files with stale fields may cause startup failure | No | 🛠️ Fix now |

## Recommended AA Changes
1. [📝 GAP-1] Add note recommending negative compilation test file
2. [🛠️ GAP-2] Add ABI compatibility warning and static_assert recommendation
3. [📝 GAP-3] Clarify "referenced" definition — public symbols, not file includes
4. [🛠️ GAP-4] Add config file compatibility requirement
