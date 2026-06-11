# Test Design: Error Notifier Callback

## Under Spec
- AA File: `AA-M07-ErrorNotifier.md`
- Phase: 3
- Key Requirements:
  - `error_notifier` callback invoked on the Quill backend thread when Quill encounters an error
  - Callback must be non-blocking (no I/O, no locks, no allocations) — runs on backend thread, blocking stalls ALL logging
  - "No guaranteed delivery" — best effort only, callback may not fire if backend is overwhelmed
  - Backward compatible: not setting `error_notifier` = no callback (default no-op)
  - Wiring via `quill::Backend::set_error_notifier()` in `InitializeLogging()`

## Test Harness
- **Fixture**: 
  - Quill lifecycle: `Backend::start()` + `Frontend::create_or_get_logger()` with a file sink (file errors are the most common Quill backend error source)
  - `LoggingConfig` with `error_notifier` set to a test spy (captures thread ID, error string, invocation count)
  - Forced Quill backend error: corrupt the log file path (e.g., set filename to an invalid path like `""` or `"\\.\Nonexistent"`) so the backend hits a write error
  - For integration tests: real Quill backend v10.0.1. For unit tests: the callback registration code path is tested separately from Quill's internal error generation.
- **Mocked vs Real**: Real `quill::Backend::set_error_notifier()` (it's a static API, can't be mocked easily). Quill backend is real for integration tests. The callback itself is a test spy.
- **Preconditions**: Log file path must be invalid enough that Quill's backend thread triggers an error callback. On Windows, a path like `"Z:\nonexistent\dir\file.log"` (where Z: doesn't exist) is reliable.

## Scenarios

### Positive Cases
- `error_notifier` set to a valid callback — when the backend encounters a write error (file unwritable), the callback is invoked with a non-empty error string
- `error_notifier` is called on the Quill backend thread — verify via `GetCurrentThreadId()` inside the callback vs the thread ID of the backend thread (captured via `quill::Backend::thread_id()` or a prior marker)
- `error_notifier` NOT set — no callback, no crash, no null pointer dereference in Quill internals
- Callback invoked multiple times for multiple errors (e.g., continuous write failures) — counter in the spy increments correctly
- Callback with minimal body (just a `std::atomic<int> counter++`) does not stall logging — backend continues processing messages after the error

### Negative / Error Cases
- Callback throws an exception — `quill::Backend::set_error_notifier` is called from user code, but Quill's internal invocation of the callback must not propagate exceptions. If it does, the backend thread crashes. Test: set a callback that throws and verify the process does not crash (or if it does, document it as a Quill bug).
- Callback performs blocking I/O (e.g., `Sleep(1000)`) — backend thread is blocked for 1 second. Verify that frontend log messages queue up and are NOT processed until the callback returns. This is a negative test demonstrating the documented warning.
- Multiple loggers, all configured with the same invalid sink — the callback fires once per write error. Verify no double-invocation or missed invocations.
- `Backend::start()` called twice — Quill's behavior is undefined; test that `set_error_notifier` after the second start does not crash (or document that it's unsupported)

### Production Realities
- Backend error during shutdown: if `Backend::stop()` is called while errors are being reported, the callback must not be invoked after the notifier is destroyed. Test: set a callback that captures by reference, stop the backend, verify no use-after-free.
- Callback lifetime: the `std::function` is copied by Quill; captured state must remain valid until after `Backend::stop()` completes. Test with a callback holding a `shared_ptr` to test state — verify no dangling reference after shutdown.
- Backend overwhelmed: if the frontend produces messages faster than the backend can write, the queue fills up. Quill may drop messages. The error notifier may NOT fire for dropped messages (best effort). Verify this is documented, not tested.
- Log file rotation error: disk full scenario. Force a disk full condition (via test disk quota or small max_file_size + continuous logging) and verify the error notifier fires.
- Network sink error: if the sink is a network socket (not yet implemented), network errors trigger the callback. This is a future test scenario.

### Thread Safety
- `error_notifier` is stored in `LoggingConfig` (user thread), read by `InitializeLogging()` (user thread), and passed to `quill::Backend::set_error_notifier()` which stores it internally (Quill backend thread safety is Quill's responsibility).
- The callback is invoked from Quill's backend thread. If the callback accesses shared state, the user must synchronize. The test spy uses a lock-free `std::atomic<int>` for the count.
- `LoggingConfig` must not be modified after `InitializeLogging()` — the callback pointer is copied. Standard config immutability pattern.
- Race: if `set_error_notifier` is called while the backend thread is already running and processing errors, the old callback may still be in flight. Quill v10.0.1's API does not document atomic swap semantics for `set_error_notifier`. Test: call `set_error_notifier` twice in succession and verify the second callback is eventually used.

## Assertions
- Callback is invoked at least once when a backend write error occurs (non-deterministic due to "best effort" — retry or use a guaranteed-to-fail scenario)
- Callback receives a `std::string` containing a meaningful error message (system error code, file name, operation attempted)
- `GetCurrentThreadId()` inside the callback matches the Quill backend thread ID
- No crash if `error_notifier` is null (default state)
- No crash if callback throws (Quill must catch or the process must survive — test expected behavior)
- Frontend throughput is unaffected by the presence of the error notifier (measured: log 100K messages with and without notifier, <1% variance)

## Failure Mode
- A test failure where the callback is NOT invoked: **silent production error** — the process continues logging to a bad sink, operators are not alerted, data loss occurs silently
- A test failure where the callback is invoked on the wrong thread: **thread safety violation** — if the callback is on a user thread, the user's synchronization assumptions break, potentially causing data races
- A test failure where a slow callback stalls logging: **production logging stall** — the documented warning is violated but not caught by testing
- A test failure where callback throws crashes the process: **denial of service** — a buggy callback takes down the entire logging system and process

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Added lambda capture safety documentation (capture-by-copy, lifetime warnings) | Step 2 — SAFETY note | Added production reality for callback lifetime; marked GAP-3-M07-1 RESOLVED |
| Added LIMITATION note about not all Quill error paths triggering callback | Step 1 — LIMITATION note in LoggingConfig | Marked GAP-3-M07-2 and GAP-3-M07-3 RESOLVED |
| Added detailed lifetime documentation (must outlive Backend::stop()) | Step 1 — LIFETIME docs in LoggingConfig | Added test scenario for shared_ptr callback lifetime; marked GAP-3-M07-4 RESOLVED |

## Spec Gap Notes (SGN)

| Gap ID | Status | Issue | Architectural Impact | Recommendation |
|--------|--------|-------|---------------------|----------------|
| GAP-3-M07-1 | ✅ RESOLVED | AA spec wires `error_notifier` into `InitializeLogging()` by wrapping the user-provided callback in a lambda. If the user's callback captures a reference to a local variable, and `InitializeLogging()` stores the lambda in Quill's internal state, the reference may dangle after `InitializeLogging()` returns. | Use-after-free if callback captures local references | AA spec now includes a SAFETY note documenting capture-by-copy semantics, lifetime warnings, and examples of safe vs unsafe capture patterns. 🛠️ Fix applied. |
| GAP-3-M07-2 | ✅ RESOLVED | The AA spec says "no guaranteed delivery" but provides no mechanism to test this. | The reliability contract is untestable — we cannot distinguish "callback didn't fire because of best-effort" from "callback didn't fire because of a bug" | AA spec now includes a LIMITATION note: "Not all Quill internal error paths invoke this callback. For comprehensive error monitoring, complement with OS-level monitoring (ETW, Windows Event Log, health probes)." 🛠️ Fix applied (documentation). |
| GAP-3-M07-3 | ✅ RESOLVED | `quill::Backend::set_error_notifier()` is a relatively new addition to Quill v10.0.1. There is no guarantee that every internal Quill error path actually calls the notifier. | Coverage gap — some Quill errors may silently bypass the user's notifier | AA spec now includes a LIMITATION note acknowledging this coverage gap and recommending OS-level monitoring as a complement. 🛠️ Fix applied (documentation). |
| GAP-3-M07-4 | ✅ RESOLVED | The AA spec does not specify what happens to the callback during `Backend::stop()`. If the backend flushes remaining errors during shutdown, the callback may fire during destruction of captured state. | Potential callback invocation during static destruction order fiasco | AA spec now includes LIFETIME documentation: "Captured state must remain valid for the entire backend lifetime (until after Backend::stop() completes)." Prefer shared_ptr or standalone functions. 🛠️ Fix applied. |
