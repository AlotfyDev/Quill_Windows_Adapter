# Test Design: Backtrace/Crash Log Buffer with Emergency Shutdown Flush

## Under Spec
- AA File: `AA-C03-BacktraceLogging.md`
- Phase: 3
- Key Requirements:
  - `LOG_BACKTRACE` writes to Quill ring buffer, not visible in output until flush
  - `LOG_ERROR` (or configured `flush_on_level`) auto-flushes backtrace to output
  - `FLUSH_BACKTRACE(logger)` explicitly flushes backtrace to output
  - `ShutdownLogging()` iterates all loggers in registry and calls `flush_backtrace()` before `quill::Backend::stop()`
  - Backtrace capacity configurable per-logger via `BacktraceConfig` (Emergency=5000, HealthProbe=100)
  - `init_backtrace(capacity, flush_level)` called on logger during initialization if `entry.backtrace_enabled`

## Test Harness
- **Fixture setup**: `InitializeLogging()` with a config containing named loggers with `backtrace_enabled = true`; sinks use in-memory output (custom `BufferSink` that captures all written messages, or a `FileSink` writing to a temp file that is read after each test); output sink must be able to distinguish between normal log messages and backtrace-flushed messages.
- **Mock vs real**: Quill backend is real (the backtrace mechanism is Quill-internal). Sinks are real (temp file or in-memory). Logger objects are real Quill loggers. The test directly calls `LOG_BACKTRACE`, `LOG_INFO`, `FLUSH_BACKTRACE`, and `ShutdownLogging()`.
- **Precondition requirements**: Logger has `init_backtrace()` called before any `LOG_BACKTRACE` calls; output sink is configured and empty; Quill backend is running.

## Scenarios

### Positive Cases
- `LOG_BACKTRACE(order_log, "msg={}", 42)` called 5 times, followed by `FLUSH_BACKTRACE(order_log)`: all 5 messages appear in output after flush, in correct order
- `LOG_BACKTRACE` called 10 times with capacity=5: only the last 5 messages survive in ring buffer; flush outputs the last 5 (ring buffer evicts oldest)
- Auto-flush at threshold: `flush_on_level = Error`; 5 `LOG_BACKTRACE` calls followed by `LOG_ERROR(order_log, "trigger")`: the Error message AND all 5 backtrace messages appear in output (Error triggers flush, then Error is written as a normal message)
- `LOG_BACKTRACE` messages are NOT visible before flush: normal `LOG_INFO` messages appear immediately; backtrace messages do not appear until flush (verified by reading output between each step)
- `ShutdownLogging()` with 3 loggers each having backtrace data: all backtraces are flushed before `quill::Backend::stop()` completes (verified by reading output after shutdown returns)
- `Emergency` logger with `capacity=5000`: exactly 5000 backtrace entries can be stored without eviction; entry 5001 evicts entry 1

### Negative / Error Cases
- `LOG_BACKTRACE` called on a logger where `init_backtrace()` was NOT called: behavior is Quill-defined (likely no-op or silently ignored — verify against Quill v10.0.1 API)
- `FLUSH_BACKTRACE` called on a logger with no backtrace initialized: no-op, no crash
- `FLUSH_BACKTRACE` called when no backtrace messages have been logged: no-op, no crash, no output
- Backtrace capacity set to 0: Quill may treat this as "disabled" or "unlimited" — test and document the behavior
- `flush_on_level` set to `None` (disabled): auto-flush never triggers; only explicit `FLUSH_BACKTRACE` flushes backtrace
- `ShutdownLogging()` called with empty registry (no backtrace-enabled loggers): no crash, no output from backtrace flush loop

### Production Realities
- Crash during `LOG_BACKTRACE` write: the ring buffer write is a Quill-internal operation — if Quill's frontend enqueue fails (queue full, backend stopped), the backtrace message is silently dropped per Quill's documented behavior
- Crash immediately after `flush_on_level` trigger: the backtrace flush has already happened at the point the Error message is logged (flush is synchronous before the Error enqueue). Backtrace data is in the output queue and will be written when backend processes it — even if the process crashes before the backend drains, at most one backend cycle (~100μs) of data may be lost
- `ShutdownLogging()` called while another thread is concurrently calling `LOG_BACKTRACE`: Quill's frontend enqueue is thread-safe; the shutdown `flush_backtrace()` call is a separate operation — if a racing thread enqueues a backtrace AFTER the flush but BEFORE the backend stops, that message is lost. Acceptable: shutdown is a point-in-time snapshot.
- High-frequency backtrace logging (10,000/sec into a 1000-entry ring buffer): 90% of messages are evicted before flush. Only the last 1000 survive — ensures the critical pre-failure window is captured.
- `BacktraceConfig` memory footprint: Emergency=5,000 entries × ~200 B ≈ 1,000 KB; HealthProbe=100 entries × ~200 B ≈ 20 KB; total ~1,620 KB for all backtrace-enabled loggers — allocated once at init from process heap

### Thread Safety
- `LOG_BACKTRACE` is a wrapper around `QUILL_LOG_BACKTRACE` — Quill's frontend enqueue is lock-free SPSC, thread-safe for concurrent callers
- `FLUSH_BACKTRACE(logger)` calls `logger->flush_backtrace()` — Quill internally synchronizes this; safe to call from any thread
- `ShutdownLogging()` iterates registry and calls `flush_backtrace()` on each logger — registry is read-only at this point (no concurrent writers per AA-C05), safe
- Concurrent `LOG_BACKTRACE` and `FLUSH_BACKTRACE` on the same logger: Quill's `flush_backtrace()` locks the backend thread to drain the ring buffer; concurrent frontend calls enqueue to the SPSC queue without blocking. The flush captures a point-in-time snapshot of the ring buffer.
- `init_backtrace(capacity, level)` called during initialization only — no concurrent access at that point

## Assertions
- After 5 `LOG_BACKTRACE` + `FLUSH_BACKTRACE`: exactly 5 lines in output from the logger (the backtrace messages)
- After 10 `LOG_BACKTRACE` (capacity=5) + `FLUSH_BACKTRACE`: exactly 5 lines in output (the last 5 messages)
- After 3 `LOG_BACKTRACE` + `LOG_ERROR`: output contains 4 lines — the 3 backtrace messages followed by the Error message
- After `ShutdownLogging()`: output contains all backtrace messages from all backtrace-enabled loggers
- Before any flush: backtrace messages are NOT in output (only normal log messages are visible)
- `Logger::init_backtrace().capacity` returns the configured capacity (if Quill exposes this getter)
- Backtrace messages appear in FIFO order (oldest-to-newest within the ring buffer)
- After `FLUSH_BACKTRACE`, a second `FLUSH_BACKTRACE` produces no output (ring buffer cleared by Quill after flush)
- `backtrace_flush_level=99` (invalid) clamped to valid range via `ToQuillLogLevel()` — logger init does not fail

## Failure Mode
- `flush_backtrace()` not called before `Backend::stop()` during shutdown: backtrace data is silently lost. Data loss for pre-crash diagnostics.
- `flush_on_level` not working (e.g., wrong level comparison): critical error logs don't trigger backtrace flush — operator sees Error but has no context. Silent diagnostic gap.
- Ring buffer corruption (Quill bug): garbage data flushed to output. Data corruption, not crash.
- Backtrace never initialized: `LOG_BACKTRACE` is silently ignored — no crash, no output. Information loss.
- Overwriting ring buffer too aggressively (capacity too small for the workload): critical backtrace data is evicted before flush. Incomplete crash diagnostics.

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Documented `LOG_BACKTRACE` safety requirement (must call `init_backtrace()` first) | Step 2 — Backtrace.hpp safety docs | Marked GAP-3-C03-1 RESOLVED |
| Documented `flush_backtrace()` blocking semantics | Step 4 — flush_backtrace semantics note | Marked GAP-3-C03-2 RESOLVED |
| Documented ring buffer cleared after flush | Step 4 — flush_backtrace semantics note | Added assertion: second FLUSH_BACKTRACE produces no output; marked GAP-3-C03-3 RESOLVED |
| Added validation note: `backtrace_flush_level` clamped via `ToQuillLogLevel()` | Step 3 — Validation | Added test scenario for invalid flush_on_level; marked GAP-3-C03-4 RESOLVED |
| Added Memory Footprint section | Memory Footprint | Added production reality for memory usage; marked GAP-3-C03-5 RESOLVED |

## Spec Gap Notes (SGN)

| Gap ID | Status | Issue | Architectural Impact | Recommendation |
|--------|--------|-------|---------------------|----------------|
| GAP-3-C03-1 | ✅ RESOLVED | Spec does not define behavior of `LOG_BACKTRACE` when `init_backtrace()` was not called. Quill may silently no-op or may assert/UB. | If Quill asserts or crashes, a misconfigured logger (backtrace enabled in config but init not called) can terminate the process. | AA spec now documents safety requirement in Backtrace.hpp header: "init_backtrace() must have been called on the logger before use. Without init_backtrace(), behavior is undefined (Quill may no-op or assert). Always pair with LoggerEntry::backtrace_enabled = true." 🛠️ Fix applied. |
| GAP-3-C03-2 | ✅ RESOLVED | Spec does not specify whether `flush_backtrace()` blocks until the backend has written the flushed data to sinks, or returns immediately after enqueueing. | If non-blocking, `ShutdownLogging()` may return before backtrace data is actually written to disk. The data sits in the SPSC queue and is only drained by `Backend::stop()`. | AA spec now documents: "flush_backtrace() is synchronous — it blocks until the flushed content is enqueued in the backend's output queue. It does NOT guarantee the data is written to disk; that durability guarantee comes from Backend::stop() which drains the queue." 🛠️ Fix applied. |
| GAP-3-C03-3 | ✅ RESOLVED | No specification for what happens to the ring buffer content after `flush_backtrace()` — is it cleared, or can it be flushed again? | If ring buffer is not cleared after flush, a second `ShutdownLogging()` would re-flush the same data, producing duplicate output. | AA spec now documents: "The ring buffer is cleared by Quill after flushing; a subsequent flush produces no duplicate output." 🛠️ Fix applied. |
| GAP-3-C03-4 | ✅ RESOLVED | `BacktraceConfig::flush_on_level` is an `uint32_t` but the decision tree for C01 specifies clamping only for `LoggerEntry::log_level`. There's no validation for `BacktraceConfig` values. | Configuring `flush_on_level = 99` would silently produce no auto-flushes (level never reached). | AA spec now documents: "entry.backtrace_flush_level must be in valid Quill LogLevel range [Trace..Critical]. Invalid values are clamped via ToQuillLogLevel(). This is implemented inside InitializeLogging()." 🛠️ Fix applied. |
| GAP-3-C03-5 | ✅ RESOLVED | No discussion of memory allocation for the ring buffer. `capacity=5000` at, say, 200 bytes per entry = 1MB per logger. Five named loggers with backtrace = 5MB. | In a memory-constrained trading system, unexpected allocations can cause OOM. The ring buffer is allocated once at init time, but the allocation size should be documented. | AA spec now includes a Memory Footprint section with per-logger estimates and total (~1,620 KB). 🛠️ Fix applied. |
