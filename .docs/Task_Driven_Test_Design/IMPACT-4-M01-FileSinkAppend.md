# Impact Analysis: FileSink Append/Truncate Mode

## Summary
Total GAPs: 3 | P0: 0 | P1: 1 | P2: 2 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-4-M01-1 | ⚠️ P1 | `#ifdef QUILL_HAS_FILE_SINK_OPEN_MODE` compile check is unnecessary — `set_open_mode(char)` always exists in Quill v10.0.1 (`FileSink.h:121`). The `PrepareTruncate` rename fallback is slower, loses original file, and the native API path is never tested. | If the native API path is skipped by accident (e.g., compile check macro typo), all truncate requests fall back to rename fallback which renames to `.prev` — operators get `.prev` files accumulating, never actual truncation. Not a crash, but silent behavioral regression. | No | 🛠️ Fix now — Simplify Step 1: use `set_open_mode('w')` directly. Keep `PrepareTruncate` as documented fallback for pre-v10 Quill only. |
| GAP-4-M01-2 | 🟡 P2 | `append` added to console config block, but `ConsoleSink` has no open-mode concept — it writes to the already-open stdout/stderr `FILE*`. The flag is meaningless for console sinks. | Operator sets `console.append = false` expecting fresh console output; no error or warning is produced. Silent no-op creates false sense of control. Not a crash but misleading UX. | No | 🛠️ Fix now — Remove `append` from console config block. Console is always "append" (stream semantics). Document this in the config struct comment. |
| GAP-4-M01-3 | 🟡 P2 | Step 2 claims rename "avoids race with backend writer" but `PrepareTruncate` is called BEFORE `FileSink` construction and `quill::Backend::start()`. No Quill backend thread exists at that point. | None — the code works correctly. But the justification is wrong, which could mislead future maintainers into incorrectly applying the pattern elsewhere. | No | 📝 Document only — Correct the comment: race is with EXTERNAL processes writing to the file, not Quill's own backend. |

## Recommended AA Changes
- **GAP-4-M01-1**: Replace `#ifdef QUILL_HAS_FILE_SINK_OPEN_MODE` / `#else` block with direct `FileSinkConfig::set_open_mode('w')` call. Move `PrepareTruncate` to a documented fallback section.
- **GAP-4-M01-2**: Remove `bool append = true` from the console sub-config struct. Keep it only in the file sub-config. Add comment: "// NOTE: append applies only to file sinks; console/stderr always use stream semantics."
- **GAP-4-M01-3**: Change comment from "avoids race with backend writer" to "avoids race with external processes writing to the same file (backend not yet started at this point)".
