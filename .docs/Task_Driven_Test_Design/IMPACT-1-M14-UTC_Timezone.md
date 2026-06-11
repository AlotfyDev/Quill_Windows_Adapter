# Impact Analysis: UTC Timezone Support

## Summary
Total GAPs: 4 | P0: 1 | P1: 0 | P2: 3 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-1-M14-1 | 🔴 P0 | No fallback if quill::Timezone::GmtTime doesn't exist | UTC cannot be implemented; v0.2.0 ships with local-only timestamps; trading audit trails non-compliant | No | 🛠️ Fix now |
| GAP-1-M14-2 | 🟡 P2 | Per-sink vs global timezone unspecified | Partial UTC/local mix in output files if each sink has its own formatter | No | 🛠️ Fix now |
| GAP-1-M14-3 | 🟡 P2 | No mechanism for consumers to detect breaking default change | Existing log parsing pipelines break silently; no version-based detection | No | 📝 Document only |
| GAP-1-M14-4 | 🟡 P2 | Timestamp format resolution unspecified | Log parsers may break if format string changes beyond timezone suffix | No | 🛠️ Fix now |

## Recommended AA Changes
1. [🛠️ GAP-1] Add fallback implementation using manual UTC conversion (FileTimeToSystemTime)
2. [🛠️ GAP-2] Document that all sinks share one timezone from single PatternConfig; add test requirement
3. [📝 GAP-3] Add version macro consumers can check for behavioral change detection
4. [🛠️ GAP-4] Specify exact timestamp format and confirm only timezone suffix changes
