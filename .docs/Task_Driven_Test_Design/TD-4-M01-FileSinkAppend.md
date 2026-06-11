# Test Design: FileSink Append/Truncate Mode

## Under Spec
- AA File: `AA-M01-FileSinkAppendMode.md`
- Phase: 4
- Key Requirements:
  - Per-sink `append` bool on file sub-config only (console uses stream semantics — no append/truncate concept)
  - `config.file.append = false` → fresh/truncated file on startup via `FileSinkConfig::set_open_mode('w')`
  - File-rename fallback (`PrepareTruncate`) as safety net for older Quill versions
  - Rename approach (NOT delete) avoids race with EXTERNAL processes writing to the same file
  - Console config has no `append` field — stream semantics only

## Test Harness
- **Fixture**: Google Test suite with `SetUp`/`TearDown` creating a temp directory via `std::filesystem::temp_directory_path() / "quill_append_test_XXXXXX"`. Each test gets a unique subdirectory.
- **Quill lifecycle**: `quill::Frontend::create_or_get_logger(...)` with a `FileSink`, attach to `quill::Backend::start()`, then `quill::flush()` after each log statement. `quill::Backend::stop()` in teardown.
- **Real vs mock**: Real `FileSink`, real `FileSinkConfig`, real filesystem. Mock filesystem not needed — tests validate actual file content. Use `fs::rename` to simulate locked-file scenarios.
- **Preconditions**: Temp directory writable. No pre-existing log file for truncate tests (or known state from prior append tests).

## Scenarios

### Positive Cases
- `append=true` (default): Write 2 log entries, stop backend, read file — must contain both entries (data preserved across flushes).
- `append=false`: Write a log entry, stop backend, restart Quill backend with same filename and `append=false` — file must contain only the second session's entry (not the first).
- Per-sink isolation: File sink uses `append=true` (default). Console has no `append` — changing `config.file.append` must not affect console output stream behavior.
- `PrepareTruncate` rename fallback: When `set_open_mode('w')` is not used (simulate by calling `PrepareTruncate` directly before fopen), existing file renamed to `.prev` before new file created.

### Negative / Error Cases
- File locked by another process: `PrepareTruncate` rename fails (`fs::rename` returns error). System must log warning via `OutputDebugStringA` and continue in append mode — not crash.
- Invalid path: `filename` points to non-existent directory. `FileSink` construction should throw `QuillError`. Ensure error propagates, not silently swallowed.
- Read-only filesystem: `set_open_mode('w')` opens file, but `fopen` with `"w"` returns nullptr. `FileSink` constructor throws. Test that `PrepareTruncate` is never called if `append=true`.
- Empty filename: `FileSink` with empty string path — expect `QUILL_THROW(QuillError)`.

### Production Realities
- Concurrent access: Two processes logging to same file. `append=true` must not truncate the other process's data. Test by starting a separate thread that writes to same file via `fopen("a")` while Quill is running with `append=true`.
- Disk full: Simulate by setting quota on temp dir (platform-dependent). FileSink should not crash — `fwrite` fails, Quill backend recovers on next cycle. Verify via `FileEventNotifier` error callback if configured.
- Permission denied: Create file with read-only attribute before Quill starts. `fopen("a")` succeeds but `fopen("w")` fails. Verify correct behavior per append mode.
- Process crash during write: Kill process mid-write. On restart with `append=true`, remaining data in OS buffer should be present (no corruption). On restart with `append=false`, old data truncated, fresh file starts. Simulate by writing partial record and checking file integrity after restart.

### Thread Safety
- Frontend threads: Multiple user threads calling `LOG_INFO` concurrently. Quill's frontend is SPSC — each thread writes to its own queue. No race possible in frontend.
- Backend thread: Single thread reads all frontend queues. `open_file()` called once at construction — no contention after start.
- `PrepareTruncate` is called BEFORE `FileSink` construction (single-threaded during initialization). No concurrent calls.
- If `PrepareTruncate` races with another process also renaming the file, one rename will fail — handled by `std::error_code` path.

## Assertions
- `append=true` → `FileSinkConfig::_open_mode == 'a'` (verified at construction)
- `append=false` → `FileSinkConfig::_open_mode == 'w'` (or rename fallback invoked)
- After `append=false` restart: old content absent, new content present
- After `append=true` restart: old content present BEFORE first newline, new content appended after
- `PrepareTruncate` on locked file: no exception thrown, warning output via `OutputDebugStringA`
- Console config has no `append` — only `config.file.append` controls file truncation behavior; console always uses stream semantics
- When `append=true`, file size on restart >= size at end of previous session

## Failure Mode
- Test "old content missing in append mode" → **silent data loss** in production. This is the highest severity failure — it means `set_open_mode('w')` is being applied when `append=true`, or the rename fallback is deleting instead of renaming.
- Test "core dump on permission denied" → **crash** on startup. Means exception handling in `SinkFactory::CreateFileSink` is missing or incorrect.
- Test "locked file causes data loss" → **silent data loss**. Means `PrepareTruncate` falls back to delete instead of append.
- Test "per-skip config bleed" → **configuration corruption**. One sink's mode affecting another.

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| `set_open_mode('w')` as primary (remove compile-time check) | Step 1 — Use Quill v10.0.1 Native API | Under Spec updated; PrepareTruncate now safety net for older versions |
| Remove `append` from console config | Step 3 — Per-Sink Append Mode | Under Spec updated; console append scenario removed; per-sink independence assertion removed |
| Clarify rename race concern (external processes, not backend) | Step 2 — File-Rename Fallback | Under Spec updated; wording matches "external processes" |
| Spec Gap GAP-4-M01-1 | Native API usage | Marked ✅ RESOLVED |
| Spec Gap GAP-4-M01-2 | Append on console | Marked ✅ RESOLVED |
| Spec Gap GAP-4-M01-3 | Race justification | Marked ✅ RESOLVED |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-4-M01-1 | The AA spec proposes a custom `PrepareTruncate` rename fallback as the primary approach, but `FileSinkConfig::set_open_mode(char)` exists in the actual Quill v10.0.1 API (header `FileSink.h:121`) and defaults to `'a'`. The `#ifdef QUILL_HAS_FILE_SINK_OPEN_MODE` compile-time check is unnecessary — this API is always available in v10.0.1. | The rename fallback approach is slower, loses the original file (renames to .bak), and doesn't test the native Quill API path. | Remove the compile-time check. Use `set_open_mode('w')` directly when `append=false`. Keep `PrepareTruncate` as a documented fallback only if targeting older Quill versions. | ✅ RESOLVED |
| GAP-4-M01-2 | The AA spec adds `append` to the `console` sink config, but `ConsoleSink` (which wraps stderr/stdout) has no open-mode concept — it always opens the stream, not a file. The `console.append` flag is meaningless for stdout/stderr. | Misleading config field that will never be checked or applied for console sinks, creating confusion for operators who set it to `false` expecting a fresh console. | Remove `append` from the console config block, or document clearly that it only applies to file sinks. If kept, the implementation must silently ignore it for non-file sinks. | ✅ RESOLVED |
| GAP-4-M01-3 | The spec says "rename approach, NOT delete — avoids race with backend writer" but `rename` is called BEFORE `FileSink` construction, before any backend thread exists. There is no backend writer to race with at this point. | The stated justification is incorrect — the race would only exist if Quill backend were already running and writing to the file being manipulated. | Clarify that the rename race concern applies to external processes writing to the file, not Quill's own backend. Document this in the implementation comments. | ✅ RESOLVED |
