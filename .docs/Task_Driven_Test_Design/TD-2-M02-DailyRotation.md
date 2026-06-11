# Test Design: Daily File Rotation with Gzip Compression and Retention Policy

## Under Spec
- AA File: `AA-M02-DailyFileRotation.md`
- Phase: 2
- Key Requirements:
  - File renamed at midnight to `{base}.{YYYYMMDD}.log` pattern; UTC vs local timezone configurable
  - `max_archive_days=30` means delete archives older than 30 days on init and after each rotation; value `0` = keep forever
  - `gzip_on_rotation=true` compresses archived file via dedicated low-priority compression thread
  - Rotation is time-based polling on backend thread (approximate to within sleep duration)
  - All `std::filesystem` operations use `std::error_code` overloads; sharing-violation retry loop (3x, 10ms) on Windows rename
  - Coexistence with size-based rotation is future (v0.2.0); v0.1.0 ships `rotating` OR `daily`, not both

## Test Harness
- **Fixture setup**: Create temp directory via `std::filesystem::temp_directory_path()` + unique subdirectory; instantiate `DailyRotatingFileSink` with `max_archive_days`, `gzip_on_rotation`, and a shortened polling interval (100ms) for test speed; mock system clock via dependency injection if sink accepts a clock, otherwise use real clock and fast-forward via `std::chrono::time_point` manipulation on a mock clock wrapper.
- **Mock vs real**: Quill backend is real (sink writes are real file I/O). Compression thread is real (tests verify `.gz` output). Filesystem is real (temp dir). Clock is mocked via `Clock` policy parameter on the sink to enable deterministic time advancing.
- **Precondition requirements**: Temp directory is empty; sink is created but not yet started; no concurrent access during single-threaded tests.

## Scenarios

### Positive Cases
- Sink with `daily.enabled=true` creates initial file `{base}.log`; after advancing clock past midnight, `Rotate()` renames to `{base}.{YYYYMMDD}.log` and creates a fresh `{base}.log`
- `utc=true` with system in UTC+2 timezone produces filenames with UTC date (e.g., `assembler.20260610.log` at local midnight if UTC date is still previous day)
- `utc=false` with system in UTC-5 produces filenames matching local date
- `max_archive_days=7` with 10 archive files present deletes the 3 oldest after rotation
- `max_archive_days=0` with 100 archive files present deletes none after rotation
- `gzip_on_rotation=true` produces `{base}.{YYYYMMDD}.log.gz` and the `.log` file is removed after compression
- `gzip_on_rotation=false` leaves `.log` file uncompressed after rotation
- Successive rotations across 3 midnights produce 3 correctly named archive files + current active file
- `gzip_level` set to 0 (no compression), 1 (fastest/default), and 9 (best compression): compression thread produces valid `.gz` file with correct compression ratio
- Archive overwrite protection: target `{base}.{YYYY-MM-DD}.log` already exists (process restart on same day) → sink appends sequence number `{base}.{YYYY-MM-DD}.{N}.log` with N=1,2,...; after 100 failed attempts, rotation is skipped and error logged — no data loss
- Clock jumps forward past multiple days (e.g., June 10 → June 15): sink performs single rotation to current date; no intermediate "missing" day archives are created

### Negative / Error Cases
- Source registration missing: `RegisterEventSourceA` returns `NULL`, `registered_` is `false`, all writes silently dropped — no crash, no exception
- `ERROR_SHARING_VIOLATION` on rename: retry loop fires 3 times with 10ms sleep; after all retries fail, the rotation is skipped, warning logged (not thrown)
- `ERROR_FILE_NOT_FOUND` during `CleanupOldArchives`: logged as debug message, loop continues to next file
- Disk full during rotation `rename()`: `error_code` indicates no space, warning logged, rotation skipped
- Disk full during `gzip` compression: compression thread logs warning via `OutputDebugStringA` or stderr fallback, file left uncompressed
- Permission denied on archive directory: `remove()` / `rename()` fail with `ec`, warning logged, no throw
- Clock adjusted backward (e.g., system time goes from 00:05 back to 23:55): sink detects current date is before last archive date — skip rotation, log warning, avoid overwriting existing archive
- `max_archive_days` set to 0 but archive files with deletion timestamps matching "older than today" are NOT removed (correct behavior for keep-forever)
- `std::thread` constructor throws `std::system_error` during sink construction (resources exhausted): caught by constructor, `compression_enabled_` set to `false`, sink continues without compression — no crash, no exception propagation
- `DrainCompressionQueue()` timeout expires while compression hangs (disk failure): remaining compression tasks abandoned after default 5-second timeout, shutdown proceeds, warning logged

### Production Realities
- Backend thread calls `Rotate()` while frontend threads concurrently call `LOG_INFO`: Quill frontend enqueues to SPSC queue — no file handle contention during rename because backend owns all sink writes
- Compression thread runs concurrently with next rotation: `compression_queue_` mutex protects push/pop; rotation can proceed even if compression of previous day is still in progress
- Process crashes at midnight during `rename()` between `remove()` of old destination and `rename()` of current file: at most one file's data is lost or duplicated; next startup should handle partially-renamed files
- SIGTERM during gzip compression: `compression_shutdown_` atomic set true, `DrainCompressionQueue()` in `ShutdownLogging()` blocks until current compression finishes — test by sending shutdown mid-compression
- Multiple rapid rotations (polling interval shorter than rename duration): `Rotate()` checks `current_date_str_` vs current date to avoid double-rotation; second call within same date is no-op
- Windows path length > 260 characters uses `\\?\` prefix if sink supports long paths, otherwise fails gracefully

### Thread Safety
- Backend thread writes log messages to current sink; compression thread reads `compression_queue_`; main thread calls `Rotate()` from backend thread context
- `compression_mtx_` + `compression_cv_` guard the queue: `EnqueueCompression()` locks, pushes, notifies; `CompressionWorker()` locks, pops, processes
- `compression_shutdown_` is `std::atomic<bool>` — worker checks this after each pop to exit cleanly
- `current_date_str_` is mutated only on backend thread (single writer, no concurrent reads during write — the date check reads it from the same thread before deciding to rotate)

## Assertions
- After clock advance past midnight: archive file `{base}.{YYYYMMDD}.log` exists and has content identical to pre-rotation `{base}.log`
- After rotation: `{base}.log` is a new empty file (or has only post-rotation writes)
- After compression: `{base}.{YYYYMMDD}.log.gz` exists, `{base}.{YYYYMMDD}.log` does not exist
- After cleanup with `max_archive_days=7` and files from days 1-10 present: files from days 1-3 are deleted, files from days 4-10 remain
- `max_archive_days=0`: all archive files present before rotation remain present after rotation
- `error_code` overload called on every `std::filesystem` operation: verify via code review (test injects `ec` via simulated filesystem errors)
- Sharing-violation retry: `rename()` invoked exactly 3 times before giving up (verified via call counter on mock filesystem)
- UTC vs local filenames differ by timezone offset when system is not in UTC

## Failure Mode
- Rotation failure (rename fails): current log continues writing to `{base}.log` — no data loss, but next rotation will attempt same rename again. Degraded performance (missing daily archive boundaries).
- Compression failure: archive `.log` file remains uncompressed. Degraded performance (no disk space savings), no data loss.
- Cleanup failure (remove fails): stale archive files remain on disk. Data leak (disk fills over time), no crash.
- Retry exhaustion: same as rotation failure. No crash, no data loss.
- Mid-rename crash: at most one file boundary is lost on restart. Data loss possible for logs written during the rename window.

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Naming pattern clarified with dashes | §2 Naming Convention | GAP-2-M02-1: ✅ RESOLVED |
| Clock adjustment handling (backward + forward) | §8 Clock Adjustment Handling | GAP-2-M02-2: ✅ RESOLVED; added clock-jump-forward scenario |
| Archive overwrite protection with sequence numbering | §7 Coexistence (archive check) | GAP-2-M02-3: ✅ RESOLVED; added archive-overwrite scenario |
| Compression thread exception safety | §5 Compression (lifecycle) | GAP-2-M02-4: ✅ RESOLVED; added exception-safety scenario |
| gzip_level configuration (0-9, default 1) | §5 Compression (level detail) | GAP-2-M02-5: ✅ RESOLVED; added gzip-level scenario |
| DrainCompressionQueue timeout (default 5s) | §5 Compression (drain timeout) | GAP-2-M02-6: ✅ RESOLVED; added timeout scenario |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-2-M02-1 | Naming pattern `{base}.{YYYYMMDD}.log` example shows `assembler.2026-06-11.log` which uses dashes — contradicts pattern definition (no dashes). | Confusion during implementation: implementer doesn't know whether to use dashes or not. | Clarify pattern: change to `{base}.{YYYY-MM-DD}.log` or change example to `assembler.20260611.log`. Consistent pattern required for ops scripts that parse filenames. | ✅ RESOLVED — AA spec §2 now shows `{base}.{YYYY-MM-DD}.log` matching example |
| GAP-2-M02-2 | No specification for handling clock adjustments (NTP sync, DST transitions, manual time changes). | Backward clock jump could cause rotation to re-process an already-archived date, overwriting existing archive. | Add clock-jump detection: compare new date against `current_date_str_`; if new date < current, skip rotation and log warning. | ✅ RESOLVED — AA spec §8 adds clock adjustment handling |
| GAP-2-M02-3 | No specification for what happens when archive filename already exists (e.g., process restarted on same day, or clock went backward). | Data loss: `rename()` would overwrite existing archive. | Add existence check before rename; if target exists, append sequence number or skip. | ✅ RESOLVED — AA spec §7 adds archive overwrite protection with sequence numbering |
| GAP-2-M02-4 | Compression thread creation in constructor: no exception safety guarantee if thread creation fails. | `std::thread` constructor throws `std::system_error` if resources exhausted — this can propagate out of sink constructor, which may be called during `InitializeLogging()`, causing process abort. | Document that compression thread is optional and graceful degradation if thread creation fails; use `notify_all_at_thread_exit` or wrap in try-catch. | ✅ RESOLVED — AA spec §5 adds try-catch with graceful degradation |
| GAP-2-M02-5 | No mention of compression level for gzip (fastest vs best compression). | Default gzip level varies by implementation; on high-throughput systems, compression could block the thread for seconds. | Specify `zlib` compression level (e.g., Z_BEST_SPEED = 1) and make it configurable. | ✅ RESOLVED — AA spec §5 adds `gzip_level` (0-9, default 1) |
| GAP-2-M02-6 | `DrainCompressionQueue()` blocks shutdown until compression finishes, but no timeout specified. | If a compression operation hangs (e.g., disk failure), shutdown blocks indefinitely, preventing process termination. | Add optional timeout to `DrainCompressionQueue()`; after timeout, set a flag to abandon remaining compression tasks. | ✅ RESOLVED — AA spec §5 adds 5-second timeout parameter |
