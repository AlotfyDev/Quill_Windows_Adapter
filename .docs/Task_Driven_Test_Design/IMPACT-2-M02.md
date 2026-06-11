# Impact Analysis: Daily File Rotation

## Summary
Total GAPs: 6 | P0: 1 | P1: 3 | P2: 2 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-2-M02-1 | 🟡 P2 | Naming pattern example uses dashes but pattern definition doesn't — implementer confusion | Ops scripts that parse filenames may break if pattern vs example inconsistent | No | 🛠️ Fix now |
| GAP-2-M02-2 | ⚠️ P1 | No clock-jump detection (NTP, DST, manual time changes) | Backward clock jump overwrites existing archive — data loss | No | 🛠️ Fix now |
| GAP-2-M02-3 | 🔴 P0 | No overwrite protection when archive filename already exists | Data loss: `rename()` overwrites existing archive silently | No | 🛠️ Fix now |
| GAP-2-M02-4 | ⚠️ P1 | Compression thread creation in constructor — no exception safety | Process abort during startup if thread resources exhausted | No | 🛠️ Fix now |
| GAP-2-M02-5 | 🟡 P2 | Compression level unspecified for gzip | On high-throughput systems, default gzip level may block thread for seconds | No | 🛠️ Fix now |
| GAP-2-M02-6 | ⚠️ P1 | `DrainCompressionQueue()` no timeout — blocks shutdown indefinitely | If compression hangs (disk failure), process can't terminate | No | 🛠️ Fix now |

## Recommended AA Changes
- **GAP-1**: Change naming example to `assembler.20260611.log` (no dashes) OR change pattern to `{base}.{YYYY-MM-DD}.log` — make pattern and example consistent
- **GAP-2**: Add clock-jump detection: compare new date against `current_date_str_`; if new date < current, skip rotation and log warning
- **GAP-3**: Add existence check before rename; if target exists, append sequence number or skip
- **GAP-4**: Document graceful degradation if compression thread creation fails; wrap `std::thread` constructor in try-catch
- **GAP-5**: Specify `Z_BEST_SPEED = 1` as default compression level; make configurable
- **GAP-6**: Add timeout parameter to `DrainCompressionQueue()`; after timeout, abandon remaining tasks
