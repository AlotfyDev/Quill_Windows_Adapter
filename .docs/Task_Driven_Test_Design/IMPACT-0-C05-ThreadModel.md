# Impact Analysis: Thread Model & Shutdown Sequence

## Summary
Total GAPs: 6 | P0: 0 | P1: 2 | P2: 4 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-0-C05-1 | 🟡 P2 | No debug assert guard for GetLogger() after shutdown — UB is silent during development | Developers may not discover UB until production, where it manifests as a Heisenbug | No | 🛠️ Fix now |
| GAP-0-C05-2 | 🟡 P2 | Shutdown flag storage not specified — ODR violations possible across TUs | Multiple translation units may each have their own flag; the flag would never be globally visible | No | 📝 Document only |
| GAP-0-C05-3 | ⚠️ P1 | No documented guarantee for messages enqueued during shutdown | Partial message loss during shutdown — no guarantee for trading audit trail completeness | No | 🛠️ Fix now |
| GAP-0-C05-4 | 🟡 P2 | No escape hatch for re-init — std::call_once blocks it permanently | Future hot-reload config completely blocked with no design path | No | 📝 Document only |
| GAP-0-C05-5 | ⚠️ P1 | Sink creation/destruction thread safety not defined | Concurrent sink creation and sink writing may race, causing data corruption or crash | No | 🛠️ Fix now |
| GAP-0-C05-6 | 🟡 P2 | UB tradeoff lacks benchmark justification | Design decision made on untested assumption about atomic check overhead | No | 📝 Document only |

## Recommended AA Changes
1. [🛠️ GAP-1] Add debug assert in GetLogger() design decision section
2. [📝 GAP-2] Add precise storage location specification for shutdown flag
3. [🛠️ GAP-3] Add explicit shutdown guarantee section for messages during shutdown
4. [📝 GAP-4] Add re-init escape hatch design note
5. [🛠️ GAP-5] Add sink creation/destruction thread safety section
6. [📝 GAP-6] Add benchmark requirement note to M19 cross-reference
