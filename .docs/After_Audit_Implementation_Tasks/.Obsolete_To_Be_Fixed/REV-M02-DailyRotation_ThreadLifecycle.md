# REV-M02 — Compression Thread Lifecycle & Filesystem Error Safety

**Severity**: ⚠️ Revision  
**AA File**: AA-M02-DailyFileRotation.md  
**Validation Source**: Phase2-3_AA_Validation_Map.md §2 (M02), §6 Gap Analysis

---

## Description

`DailyRotatingFileSink` specifies gzip compression on rotation via a "dedicated low-priority thread" (AA-M02 §5) but provides **no thread lifecycle management**:
- No `std::thread` member in the class sketch — compression thread is mentioned but never stored or referenced.
- No destructor logic to join or detach the thread on shutdown.
- No coordination with `ShutdownLogging()` to drain pending compression tasks before stopping the backend.

Additionally, all `std::filesystem::rename()` and `std::filesystem::remove()` calls in rotation, retention cleanup, and archive deletion use the **throwing overloads**. On Windows, these can throw `std::filesystem::filesystem_error` on permission denied, sharing violations, or disk-full conditions. An unhandled exception on Quill's backend thread **terminates the logging system**.

---

## Root Cause

The AA correctly identified the need for a compression thread but treated it as a **secondary concern** — thread creation was implied but never specified as a member that must be managed. The class sketch shows `void GzipArchive(...)` but no `std::thread compression_thread_` or `std::atomic<bool> shutdown_requested_`. Thread lifecycle (join/detach) was deferred to implementation without documented requirements.

The `std::error_code` omission is a common blind spot: most C++ examples use the throwing overloads, and on POSIX the distinction matters less because filesystem operations throw less frequently. On Windows, however, `ERROR_SHARING_VIOLATION` (Quill may hold the file handle briefly) is a real scenario that must be handled.

---

## Exact Fix

### 1. Add Thread Members to `DailyRotatingFileSink`

```cpp
class DailyRotatingFileSink : public quill::Sink {
    // ... existing members ...
    bool gzip_enabled_;
    std::thread compression_thread_;              // NEW
    std::mutex compression_mtx_;                  // NEW
    std::condition_variable compression_cv_;       // NEW
    std::deque<std::string> compression_queue_;    // NEW
    std::atomic<bool> compression_shutdown_{false}; // NEW
    bool compression_busy_{false};                 // NEW (under mtx)

    void CompressionWorker();                      // NEW
    void EnqueueCompression(const std::string& path); // NEW
    void DrainCompressionQueue();                  // NEW
};
```

### 2. CompressionWorker — Dedicated Thread Loop

```cpp
void DailyRotatingFileSink::CompressionWorker() {
    // Set low priority — compression must not compete with logging
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    std::unique_lock lock(compression_mtx_);
    while (!compression_shutdown_.load(std::memory_order_acquire)) {
        compression_cv_.wait(lock, [this] {
            return !compression_queue_.empty() || compression_shutdown_.load();
        });
        while (!compression_queue_.empty()) {
            auto path = std::move(compression_queue_.front());
            compression_queue_.pop_front();
            compression_busy_ = true;
            lock.unlock();

            // Use error_code overload — do NOT throw on failure
            DoGzipCompress(path);  // internal, uses error_code

            lock.lock();
            compression_busy_ = false;
        }
    }
}
```

### 3. Enqueue on Rotation

```cpp
void DailyRotatingFileSink::Rotate() {
    // ... rename current file ...
    if (gzip_enabled_) {
        EnqueueCompression(archive_path);
    }
    CleanupOldArchives();  // uses error_code overloads
}
```

### 4. Destructor — Ordered Shutdown

```cpp
DailyRotatingFileSink::~DailyRotatingFileSink() {
    if (!compression_thread_.joinable()) return;

    // Signal shutdown
    {
        std::lock_guard lock(compression_mtx_);
        compression_shutdown_.store(true, std::memory_order_release);
    }
    compression_cv_.notify_one();
    compression_thread_.join();
}
```

### 5. ShutdownLogging Coordination

In `ShutdownLogging()`, before stopping the backend:
```cpp
// Drain pending compression tasks before stopping the backend.
// This prevents the backend from closing files that compression
// threads are still reading.
for (auto& sink : all_sinks) {
    if (auto* daily = dynamic_cast<DailyRotatingFileSink*>(sink.get())) {
        daily->DrainCompressionQueue();
    }
}
// Then proceed with Quill backend shutdown...
```

`DrainCompressionQueue()` blocks until the compression queue is empty and the worker is idle.

### 6. Add `std::error_code` to ALL Filesystem Operations

Replace every throwing call:
```cpp
// BEFORE (throws on failure):
std::filesystem::rename(old_path, new_path);
std::filesystem::remove(archive_path);

// AFTER (returns error_code, no throw):
std::error_code ec;
std::filesystem::rename(old_path, new_path, ec);
if (ec) {
    // Log warning, do NOT throw on backend thread
    // If ERROR_SHARING_VIOLATION, optionally retry after 10ms
    // Maximum 3 retries, then give up
}

std::filesystem::remove(archive_path, ec);
if (ec && ec.value() != ERROR_FILE_NOT_FOUND) {
    // Log warning for unexpected errors
}
```

Apply to: `Rotate()`, `CleanupOldArchives()`, `GenerateArchiveFilename()` (rename), and any `std::filesystem::exists()` calls.

### 7. Retry Logic for Sharing Violations

```cpp
// In Rotate(), when renaming the daily file:
std::error_code ec;
for (int retry = 0; retry < 3; ++retry) {
    std::filesystem::rename(current_path, archive_path, ec);
    if (!ec) break;
    if (ec.value() == ERROR_SHARING_VIOLATION) {
        // Quill may still hold the file handle — brief wait
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
    }
    break;  // non-retryable error
}
```

---

## Impact if NOT Fixed

| Issue | Consequence |
|-------|-------------|
| Compression thread never joined | Thread handle leak; potential access of freed `DailyRotatingFileSink` members after destruction |
| Compression thread continues during shutdown | Thread may try to gzip a file that Quill's backend is closing — data race or access violation |
| `remove()` throws on archive cleanup | Unhandled exception on backend thread terminates the logging system |
| `rename()` throws on rotation | Midnight rotation fails with unhandled exception; all subsequent logging to that sink crashes |
| No sharing-violation retry | Rotation silently fails on first `ERROR_SHARING_VIOLATION`; file stays open with old name |

---

## Verification

1. **Code review**: Confirm `compression_thread_` member exists, is joined in destructor, and has a shutdown flag.
2. **Code review**: Confirm every `std::filesystem::` call uses the `std::error_code` overload.
3. **Code review**: Confirm `DailyRotatingFileSink` destructor signals `compression_shutdown_ = true`, notifies CV, and calls `thread_.join()`.
4. **Stress test**: Run with `gzip_on_rotation = true` and rapid rotation (shortened interval). Verify clean shutdown in Process Explorer — no orphaned threads, no handle leaks.
5. **Error injection**: Use a mock filesystem or ACL to deny write permissions on the archive directory. Verify `DailyRotatingFileSink` logs a warning and continues (does not crash).
6. **Integration test**: Call `ShutdownLogging()` while compression is in-flight. Verify all pending compressions complete before `Quill::Backend::stop()` is called.
