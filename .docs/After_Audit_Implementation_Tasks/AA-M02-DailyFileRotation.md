# AA-M02 — Daily File Rotation (After-Audit Corrected — Complete Redesign)

> **Phase**: 2 — 🔧 Redesign  
> **Effort**: 1h design + 1.5h implementation = 2.5h total  
> **Depends on**: AA-C01 (needs multi-logger sink infrastructure)  
> **v1.x Reference**: TASK-M02-DailyFileRotation.md  
> **Audit Issues**: M02-A (timezone), M02-B (naming), M02-C (retention), M02-D (compression), M02-E (midnight atomicity)  
> **Audit Verdict**: ❌ Fail — Requires Complete Redesign

---

## Problem

Logger_Adapter only supports size-based rotation (`RotatingFileSink` with `max_file_size` + `max_files`). Daily time-based rotation is needed for operational log retention (e.g., "keep 30 days of daily logs").

The original TASK-M02 was **underdesigned** — missing timezone, naming convention, retention policy, compression, and atomicity documentation.

---

## Corrected Design

### 1. Configuration

Add a `daily_rotation` sub-struct to the `file` config block:

```cpp
struct {
    bool enabled = false;
    std::string filename = "logs/assembler.log";
    bool rotating = true;
    size_t max_file_size = 10 * 1024 * 1024;
    uint32_t max_files = 5;

    // NEW: Daily rotation
    struct {
        bool enabled = false;                       // enables daily rotation
        bool utc = true;                            // UTC vs local timezone
        std::string naming_pattern = "{base}.{YYYY-MM-DD}.log";  // naming convention
        uint32_t max_archive_days = 30;             // retention: days to keep
        bool gzip_on_rotation = false;              // gzip after rollover
        uint32_t gzip_level = 1;                    // zlib compression level 0-9 (1=fastest)
    } daily;
} file;
```

### 2. Naming Convention

Pattern: `{base}.{YYYY-MM-DD}.log`
- Example: `logs/assembler.2026-06-11.log`
- The first log file before first rotation is `logs/assembler.log`
- After midnight, the previous day's file is renamed to include the date

### 3. Timezone Handling

- Default: UTC (for trading systems operating across timezones)
- When `utc = false`, use local time via `std::chrono::current_zone()`
- The rotation check fires on the backend thread at a configurable interval (default: every n seconds)

### 4. Retention Policy

- `max_archive_days = 30` means: during initialization and after each rotation, delete any archive file older than 30 days
- A minimum of `max_archive_days = 1` is enforced (no auto-delete of today's file)
- Special value `0` = keep forever (no auto-delete)

### 5. Compression (Optional)

- `gzip_on_rotation = true`: after rotating, the archived file is gzip-compressed in a background task
- Compressed file: `{base}.{YYYYMMDD}.log.gz`
- Compression runs on a dedicated low-priority thread (`compression_thread_`) to avoid blocking the logging backend
- Thread lifecycle:
  - Created in constructor, runs `CompressionWorker()` which waits on `compression_cv_`
  - **Exception safety**: If `std::thread` constructor throws `std::system_error` (resources exhausted), the sink constructor catches it, sets `compression_enabled_ = false`, and continues without compression. Graceful degradation.
  - Signaled via `compression_cv_.notify_one()` when `EnqueueCompression()` adds a path to `compression_queue_`
  - On shutdown: `compression_shutdown_` is set to `true`, CV is notified, thread joins
- Destructor signals `compression_shutdown_ = true`, notifies CV, calls `compression_thread_.join()`
- `ShutdownLogging()` drains pending compression tasks via `DrainCompressionQueue()` before stopping the backend
- **Compression level**: Default is `Z_BEST_SPEED = 1` (fastest, minimal CPU impact). Configurable via `daily.gzip_level` (0-9, default 1).
- **Drain timeout**: `DrainCompressionQueue()` accepts a timeout parameter (default 5 seconds). If compression hangs (e.g., disk failure), the queue is abandoned after timeout — remaining tasks are skipped and a warning is logged. This prevents indefinite shutdown blocking.
- See AA-C05 (Thread Model) for shutdown sequencing contract

### 6. Midnight Atomicity

- The rotation check is time-based (polling on the backend thread, not event-driven)
- At midnight ± polling interval, the file transitions to the new day
- **Acceptable for trading**: missing the exact midnight boundary by a few milliseconds is tolerable
- Documented: "Daily rotation is approximate to within the backend's sleep duration (default 100 μs)"

### 7. Coexistence with Size-Based Rotation

- If both `rotating` and `daily.enabled` are true, the logger uses **daily rotation as the primary**, with size-based rotation as a secondary fallback
- If the daily file exceeds `max_file_size` before midnight, it rolls early with a sequence number: `assembler.2026-06-11.1.log`
- This is a **future enhancement** — v0.2.0 ships with `rotating` OR `daily`, not both simultaneously

### 8. Clock Adjustment Handling

Clock adjustments (NTP sync, DST transitions, manual time changes) can cause the system clock to jump backward. This must not corrupt archive files:

- Before rotating, compare the new date string against `current_date_str_`. If `new_date < current_date_str_` (clock moved backward), skip rotation and log a warning. Do NOT overwrite existing archives.
- If `new_date == current_date_str_` (clock rewound within same day), no rotation is needed — the current file is still valid.
- If the clock jumps forward past multiple days, a single rotation is performed to the current date. Intermediate "missing" days are not created (acceptable — gaps in archival are documented).

---

## Corrected Implementation Plan

### Step 1 — Create `DailyRotatingFileSink` (or extend existing)

Quill may or may not support daily rotation natively. Two approaches:

**Approach A**: If Quill supports it natively → wrap Quill's API
**Approach B**: If not → implement a custom `quill::Sink` subclass

Implementation sketch (Approach B):

```cpp
class DailyRotatingFileSink : public quill::Sink {
    std::string base_filename_;
    std::string naming_pattern_;
    bool utc_;
    uint32_t max_archive_days_;
    bool gzip_enabled_;
    std::unique_ptr<quill::FileSink> current_sink_;
    std::string current_date_str_;
    std::mutex mtx_;

    // Compression thread members (see §5 for lifecycle details)
    std::thread compression_thread_;
    std::mutex compression_mtx_;
    std::condition_variable compression_cv_;
    std::deque<std::string> compression_queue_;
    std::atomic<bool> compression_shutdown_{false};
    std::atomic<bool> compression_enabled_{true};
    uint32_t gzip_level_{1};

    std::string GenerateDateString();
    std::string GenerateArchiveFilename(const std::string& date);
    void Rotate();
    void CleanupOldArchives();
    void GzipArchive(const std::string& path);
    void CompressionWorker();            // dedicated thread loop
    void EnqueueCompression(const std::string& path);
    void DrainCompressionQueue(std::chrono::milliseconds timeout = std::chrono::seconds(5));
};
```

**Filesystem error safety**: All `std::filesystem` operations in `Rotate()`, `CleanupOldArchives()`, and `GenerateArchiveFilename()` MUST use the `std::error_code` overload to prevent unhandled exceptions on the backend thread:

```cpp
std::error_code ec;
std::filesystem::rename(old_path, new_path, ec);
if (ec) {
    // Log warning, do NOT throw on backend thread
}
```

**Sharing-violation retry**: On Windows, `rename()` may fail with `ERROR_SHARING_VIOLATION` if Quill briefly holds the file handle. Implement a retry loop (up to 3 retries, 10ms sleep between attempts):

```cpp
for (int retry = 0; retry < 3; ++retry) {
    std::filesystem::rename(current_path, archive_path, ec);
    if (!ec) break;
    if (ec.value() == ERROR_SHARING_VIOLATION) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
    }
    break;
}
```

The same `error_code` pattern applies to `std::filesystem::remove()` in `CleanupOldArchives()`. For `ERROR_FILE_NOT_FOUND`, log a debug message and continue; for other errors, log a warning. Do NOT throw.

**Archive overwrite protection**: Before `rename()` to archive path, check if the target archive file already exists (e.g., process restart on same day). If target exists, append a sequence number: `{base}.{YYYY-MM-DD}.{N}.log` where `N` increments until a free name is found (max 100 attempts, then skip rotation and log error).

### Step 2 — Add Config to SinkFactory

```cpp
// In setup/SinkFactory.hpp
std::shared_ptr<quill::Sink> CreateFileSink(const LoggingConfig::FileConfig& cfg) {
    if (cfg.daily.enabled) {
        return std::make_shared<DailyRotatingFileSink>(
            cfg.filename,
            cfg.daily.naming_pattern,
            cfg.daily.utc,
            cfg.daily.max_archive_days,
            cfg.daily.gzip_on_rotation,
            cfg.daily.gzip_level
        );
    }
    // ... existing rotating or plain file sink creation ...
}
```

### Step 3 — Wire into LoggerSetup

In `InitializeLogging`, check `config.file.daily.enabled` and create the appropriate sink.

---

## Acceptance Criteria

- [ ] `config.file.daily.enabled = true` creates a `DailyRotatingFileSink`
- [ ] File is renamed at midnight to `{base}.{YYYYMMDD}.log`
- [ ] UTC mode produces UTC-dated filenames (independent of system timezone)
- [ ] Local mode uses the system's local timezone
- [ ] Archives older than `max_archive_days` are deleted during rotation
- [ ] `gzip_on_rotation = true` produces `.log.gz` files
- [ ] Size-based rotation still works when `daily.enabled = false`
- [ ] Build succeeds Debug|x64 with zero new warnings
- [ ] `Experimental_Console.exe` verifies daily rotation with a shortened polling interval

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add `file.daily` sub-struct |
| `Logger_Adapter/setup/SinkFactory.hpp` | Add `CreateDailyRotatingFileSink` |
| `Logger_Adapter/sinks/` (new file) | Implement `DailyRotatingFileSink.hpp/.cpp` |
| `Logger_Adapter/logging/LoggerSetup.hpp` | Wire daily rotation config |
| `Logger_Adapter/Logger_Adapter.vcxproj` | Add new sink files |