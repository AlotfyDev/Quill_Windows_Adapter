# Impact Analysis: Queue Configuration

## Summary
Total GAPs: 5 | P0: 0 | P1: 2 | P2: 3 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-1-M08-1 | ⚠️ P1 | Unbounded queue + capacity silently ignored | Operator thinks capacity is set but it has no effect; no warning | No | 🛠️ Fix now |
| GAP-1-M08-2 | 🟡 P2 | MSVC-specific `_DEBUG` instead of standard `NDEBUG` | Warning may fail to fire in Release on non-MSVC toolchains or fire incorrectly | No | 🛠️ Fix now |
| GAP-1-M08-3 | ⚠️ P1 | Zero/small capacity behavior undefined | Production deadlock on misconfiguration — no guardrails for minimum capacity | No | 🛠️ Fix now |
| GAP-1-M08-4 | 🟡 P2 | No enforcement of ordering contract (config before start) | Queue config silently ignored if ordered incorrectly after refactor | No | 🛠️ Fix now |
| GAP-1-M08-5 | 🟡 P2 | Test isolation using global Quill state not addressed | Tests cannot run in same process; each requires subprocess, making CI expensive | No | 📝 Document only |

## Recommended AA Changes
1. [🛠️ GAP-1] Add precondition check: warn/ignore capacity when Unbounded; document in struct
2. [🛠️ GAP-2] Replace `_DEBUG` with `NDEBUG` (standard C++ macro)
3. [🛠️ GAP-3] Add capacity validation: clamp to minimum (1024), document minimum in struct
4. [🛠️ GAP-4] Add ordering contract comment and runtime check
5. [📝 GAP-5] Document test isolation requirement for downstream test authors
