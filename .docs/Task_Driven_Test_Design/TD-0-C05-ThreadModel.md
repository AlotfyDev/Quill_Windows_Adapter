# Test Design: Thread Model & Shutdown Sequence

## Under Spec
- AA File: `AA-C05-ThreadModel.md`
- Phase: 0 — Design & Cleanup First (design doc only, no code yet)
- Key Requirements:
  - Frontend threads (caller) own `GetLogger()`, `SetLogLevel()`, `LOG_*()`, `InitializeLogging()`, `ShutdownLogging()`. Backend thread (Quill worker) owns sink writes, backtrace flush, error notifier callbacks.
  - Concurrency contract: `GetLogger()` fully thread-safe (Quill internal sync); `LOG_*()` lock-free SPSC enqueue; `SetLogLevel()` uses `std::atomic`; `InitializeLogging()` `std::call_once` guarded; `GetLogger()` after shutdown is UB (no lock for performance, but `DEBUG_ASSERT` added in Debug builds).
  - Shutdown sequence: (1) set `shutting_down = true` (release store), (2) flush backtraces, (3) `quill::Backend::stop()`, (4) invalidate registry.
  - Memory ordering: shutdown flag uses `memory_order_release` store and `memory_order_acquire` load (not `seq_cst`).
  - Shutdown flag is a single `std::atomic<bool>` function-local static, declared `extern` in internal header to prevent ODR violations across TUs.
  - Shutdown message guarantee: messages enqueued before `ShutdownLogging()` returns are guaranteed persisted; messages enqueued during shutdown may be lost.
  - Sink creation/destruction is frontend-thread-safe (Quill internally synchronized). Sink config changes must be synchronized with backend thread or done before `start()`.
  - Re-init not supported in v0.2.0 — `InitializeLogging()` called exactly once. `std::call_once` has no escape hatch; if re-init is needed later, replace with `std::atomic<bool>` + mutex.
  - Application-level `std::atomic<bool> app_running` flag recommended for external shutdown safety.

## Test Harness
- **Fixture**: A test that spawns N frontend threads, calls `InitializeLogging()`, writes M log messages per thread, then calls `ShutdownLogging()` and waits for drain. The fixture is per-scenario with process isolation (same Quill singleton constraint as M08/M14). Uses Google Test with death-test subprocess isolation (`EXPECT_EXIT` / `EXPECT_DEATH`) for UB scenarios.
- **Mock vs Real**: Real Quill v10.0.1 backend for sink/queue behavior. The logger registry (`std::unordered_map` in Logger_Adapter's `detail::loggers()` or equivalent) is a real container — its thread-safety is tested with concurrent access patterns. A fake slow sink (with artificial `std::this_thread::sleep_for`) is used to simulate backend congestion. A `std::atomic<int>` write counter on the fake sink tracks message delivery.
- **Precondition Requirements**: `InitializeLogging()`, `GetLogger()`, `GetDefaultLogger()`, `ShutdownLogging()` implemented per AA-C05 contract. Quill v10.0.1 installed. The shutdown flag (`shutting_down`) is a `std::atomic<bool>` with release/acquire semantics, visible to all translation units.

## Scenarios

### Positive Cases
- **Normal operation**: 4 frontend threads each call `GetLogger("root")` and `LOG_INFO(...)` 1000 times. Backend thread processes all 4000 messages. `ShutdownLogging()` drains the queue. All messages are written to the sink.
- **Concurrent GetLogger**: 8 threads call `GetLogger("root")` simultaneously (200k iterations total). No crash, no data race. The same logger pointer is returned to all callers (verified by address comparison).
- **Concurrent SetLogLevel during logging**: Thread A calls `SetLogLevel(level)` while Threads B-E call `LOG_INFO(...)`. After `SetLogLevel` returns, subsequent log calls respect the new level (within the bounded delay of the SPSC queue — eventual consistency). Uses atomic exchange for the level.
- **Shutdown drains queue**: 2 producer threads emit 5000 messages each (10000 total). `ShutdownLogging()` blocks until all 10000 are written to sink. Sink write counter == 10000 after shutdown returns.
- **Shutdown backtrace flush**: Register a backtrace logger (C03), write 10 backtrace messages, then shutdown. The backtrace is flushed before the backend stops (verified by sink having the 10 backtrace entries).
- **InitializeLogging called once**: Call `InitializeLogging()` twice. The second call is a no-op (guarded by `std::call_once`). No crash, no duplicate backend thread. `GetLogger()` returns the same pointer before and after.
- **Shutdown flag memory ordering**: Set up a test where thread A checks `shutting_down.load(memory_order_acquire)` and thread B sets `shutting_down.store(true, memory_order_release)`. All writes by B before the store are visible to A after the load. Verify via a synchronized counter. (Low-level test — use a standalone atomic ordering test, not through the logger.)
- **Shutdown flag ODR verification**: Compile two translation units that each reference `g_shutting_down`. Verify via linker that both resolve to the same address (single definition). Use a test that reads the address of `g_shutting_down` from two different `.cpp` files and compares pointers at runtime.
- **Shutdown message guarantee**: Enqueue 10000 messages, call `ShutdownLogging()`. All 10000 must be persisted (sink write counter == 10000). Then enqueue 1000 messages AFTER `ShutdownLogging()` returns — these may be lost (accepted per spec). Test verifies the first batch is fully drained.
- **Sink thread safety**: Spawn 2 threads that each call `quill::create_or_get_sink()` for different sink types while a third thread performs `LOG_INFO()`. No crash, no data race. Verify both sinks receive output.
- **DEBUG_ASSERT in GetLogger() after shutdown**: Build in Debug mode. Call `ShutdownLogging()`, then `GetLogger()`. The `DEBUG_ASSERT` should trigger (observable as `abort()` or `__debugbreak()`). Build in Release mode — same sequence must not trigger any assert (no-op). Use separate compiled binaries for Debug vs Release.
- **Application lifecycle app_running pattern**: Set an application-level `std::atomic<bool> app_running = false` before shutdown. Thread A polls `app_running.load(acquire)` before each `GetLogger()`. Thread B sets `app_running.store(false, release)` then calls `ShutdownLogging()`. Verify no thread calls `GetLogger()` after `app_running` becomes false.

### Negative / Error Cases
- **GetLogger after ShutdownLogging**: Call `ShutdownLogging()`, then call `GetLogger()`. This is documented UB. Test verifies UB is observable (e.g., null pointer, stale pointer to freed registry, or crash). Use `EXPECT_DEATH` or `EXPECT_EXIT` — if the process does NOT crash, the UB may have silently produced a dangerous dangling pointer.
- **Concurrent ShutdownLogging from 2 threads**: Threads A and B both call `ShutdownLogging()` simultaneously. Quill's `Backend::stop()` should be idempotent or internally synchronized. The second call should be a no-op or safe. Test verifies no double-free, no crash.
- **LOG_INFO during ShutdownLogging**: Thread A calls `ShutdownLogging()`. Thread B calls `LOG_INFO()` concurrently (before backend fully stops). Messages enqueued after the shutdown flag but before backend stop may or may not be processed. Test documents the observable loss rate (some messages may be drained, some lost). This is accepted behavior per AA-C05.
- **InitializeLogging after ShutdownLogging**: In v0.2.0 this is not supported. Test verifies that calling `InitializeLogging()` after `ShutdownLogging()` results in observable UB or a no-op (depending on whether `std::call_once` blocks re-init). Per spec, the `std::call_once` flag is never reset — re-init is silently ignored. Test verifies this: no new backend thread is created, no new sinks are opened.
- **SetLogLevel on invalid logger name**: `SetLogLevel("nonexistent_logger", level)`. Should be a no-op or return an error code. Must not crash or UB. (This is more relevant to C01/C04 but belongs in the thread model.)

### Production Realities
- **SIGTERM during log write**: Signal handler calls `ShutdownLogging()` (or sets a flag). Producer thread is in the middle of `LOG_INFO()`. The SPSC enqueue is lock-free and signal-safe per Quill's design. Test: raise `SIGTERM` from a timer while another thread is logging. Verify no corruption of the queue.
- **Backend thread crash**: If the backend thread terminates abnormally (e.g., segfault in a custom sink), frontend threads continue to enqueue to the SPSC queue. The queue grows unboundedly (or blocks if bounded). Test: inject a crash into a custom sink (e.g., `int* p = nullptr; *p = 0;`). Verify frontend threads survive but the backend is dead. (This tests the robustness of the thread separation — frontend must not be affected by backend crashes.)
- **Resource exhaustion (thread creation)**: Spawning 1000 frontend threads that each call `GetLogger()` and `LOG_INFO()`. Quill creates per-thread SPSC queues. Test verifies that Quill handles the thread-local storage correctly without exhausting system resources (may fail gracefully with `std::system_error` for thread creation, which is acceptable).
- **ATEXIT / static destructor ordering**: If `ShutdownLogging()` is not called explicitly before `main()` exits, static destructors may destroy Quill-internal singletons while frontend threads are still running. Test: let the process exit without calling `ShutdownLogging()`. Verify no crash during static teardown (or document that this is UB and the application MUST call `ShutdownLogging()` before exit).

### Thread Safety
- **InitializeLogging vs GetLogger race**: Thread A calls `InitializeLogging()`, Thread B calls `GetLogger()` immediately after. Per AA-C05, `InitializeLogging()` is `std::call_once` guarded. Thread B's `GetLogger()` may get `nullptr` if the root logger hasn't been set yet. Test verifies `GetLogger()` returns `nullptr` only before init, never returns dangling pointer during or after init. Use `std::atomic_thread_fence` or spin-loop until root logger is non-null.
- **ShutdownLogging vs concurrent GetLogger**: Thread A calls `ShutdownLogging()`. Thread B calls `GetLogger()` in a tight loop. The shutdown flag transition (release store) must be visible to Thread B's acquire load. Test: Thread B polls `shutting_down.load(acquire)` until it sees `true`. After that, `GetLogger()` returns the stale pointer (UB by design). The test documents which pointer value is returned (should not be a freed pointer if the registry is intentionally leaked, which is a valid design choice).
- **LOG_INFO memory ordering**: Frontend enqueue to SPSC must use appropriate memory ordering (typically `release` on the producer side, `acquire` on the consumer). Quill handles this internally. Test verifies no torn reads/writes on log messages: use a message with a sequence number and verify all messages are received intact and in order (per-SPSC queue — each frontend thread's messages are ordered).
- **False sharing**: The shutdown flag (`std::atomic<bool>`) occupies one byte. If adjacent to hot variables, it may cause cache-line bouncing. Per AA-C05, this is acceptable since the flag is written once and read rarely. Test: measure cache misses on the flag under concurrent logging. This is a benchmark, not a unit test. Document in M19 (Benchmark Suite) if needed.

## Assertions
- `InitializeLogging()` returns `true` once; subsequent calls return `true` but are no-ops.
- `GetLogger()` called before `InitializeLogging()` returns `nullptr`.
- `GetLogger()` called after `InitializeLogging()` returns a valid `quill::Logger*`.
- `GetLogger()` called after `ShutdownLogging()` may return a non-null pointer (UB by design — it is NOT null-checked). If death-test mode is enabled, the process may crash.
- `ShutdownLogging()` blocks until all enqueued messages are written to sinks (verified by sink write counter).
- Calling `ShutdownLogging()` multiple times is safe (second call is a no-op).
- `SetLogLevel("root", level)` changes the log level atomically — subsequent `LOG_*()` calls on the root logger use the new level.
- With N frontend threads each writing M messages, exactly N*M messages arrive at the sink (no loss, no duplication).
- Concurrent `GetLogger()` from 8 threads × 100k iterations produces no data races (detected by ThreadSanitizer or equivalent — MSVC `/analyze` does not catch this; use relacy or a manual stress test).
- The shutdown flag uses `memory_order_release` store and `memory_order_acquire` load (verified by code review; cannot be tested at runtime since `mfence` vs `mov` are indistinguishable in behavior on x64).

## Failure Mode
- **TestConcurrentGetLogger fails**: Data race in logger registry lookup. **Impact: Potential corruption of logger pointers in production, leading to log messages written to the wrong logger or crash. This is a CRITICAL correctness bug.**
- **TestShutdownDrains fails**: `ShutdownLogging()` returns before all messages are written. **Impact: Silent data loss — audit trail entries missing.**
- **TestShutdownFlagOrdering fails (if testable)**: Memory ordering is too weak (e.g., `relaxed` instead of `release/acquire`). **Impact: Shutdown sequence appears correct but on weakly-ordered hardware (ARM) the writes may not be visible. x64 is forgiving — this failure would mean the code has a latent ARM bug.**
- **TestGetLoggerAfterShutdown fails unexpectedly**: `GetLogger()` after shutdown does NOT crash or return null — the pointer is valid but the registry may be in an undefined state. **Impact: Silent use-after-free if the registry is actually destroyed. If the registry is intentionally leaked (valid design choice), this is expected and not a failure. The test must distinguish between intentional leak and accidental access.**
- **TestConcurrentShutdown fails**: Double `ShutdownLogging()` causes double-free or crash. **Impact: Production SIGTERM handler that calls shutdown twice crashes the process before it can exit cleanly.**
- **TestBackendThreadCrashIsolation fails**: Frontend thread crashes when backend thread dies. **Impact: Backend crash takes down the entire process — the thread ownership model is violated.**
- **TestSignalSafety fails**: Signal handler calling shutdown leads to queue corruption. **Impact: Trading system receiving SIGTERM during peak load may corrupt log output, losing audit trail data.**

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Added shutdown flag storage specification (extern function-local static) | §1b Shutdown Flag Storage | Added scenario "Shutdown flag ODR verification"; updated Key Requirements |
| Added shutdown message guarantee contract | §4b Shutdown Message Guarantee | Added scenario "Shutdown message guarantee"; updated Key Requirements |
| Added sink creation/destruction thread safety | §4c Sink Thread Safety | Added scenario "Sink thread safety"; updated Key Requirements |
| Added DEBUG_ASSERT in GetLogger() for Debug builds | §5 Design Decisions | Added scenario "DEBUG_ASSERT in GetLogger() after shutdown"; updated Key Requirements |
| Added application lifecycle coordination pattern | §4 Application Lifecycle Coordination | Added scenario "Application lifecycle app_running pattern" |
| Added re-init escape hatch design note | §4 Re-Initialization Contract | Updated Key Requirements with std::call_once limitation |
| Added M19 microbenchmark for no-lock validation | Acceptance Criteria | Resolved GAP-0-C05-6 |
| All 6 GAPs resolved by Impact Analysis fixes | Various | Moved to Resolved Gaps subsection |

## Spec Gap Notes (SGN)

### Resolved Gaps

These GAPs were raised during test design and subsequently resolved by Impact Analysis fixes applied to the AA spec. The resolutions are now reflected in this TD.

| Gap ID | Issue | Resolution | AA Spec Section |
|--------|-------|------------|-----------------|
| GAP-0-C05-1 | No debug guard-rail for `GetLogger()` after shutdown | ✅ RESOLVED — AA added `DEBUG_ASSERT` inside `GetLogger()` as a zero-cost Release guard. Design Decision table documents rationale. | §5 Design Decisions |
| GAP-0-C05-2 | Shutdown flag ODR — not specified as extern | ✅ RESOLVED — AA §1b specifies function-local `static std::atomic<bool>` declared `extern` in internal header. | §1b Shutdown Flag Storage |
| GAP-0-C05-3 | No shutdown message loss guarantee | ✅ RESOLVED — AA §4b documents: "Messages enqueued before `ShutdownLogging()` returns are guaranteed persisted. Messages enqueued during shutdown may be lost." | §4b Shutdown Message Guarantee |
| GAP-0-C05-4 | `std::call_once` has no escape hatch for re-init | ✅ RESOLVED — AA adds design tradeoff note: replace with `std::atomic<bool>` + mutex if re-init becomes required. | §4 Re-Initialization Contract |
| GAP-0-C05-5 | Sink creation/destruction thread safety undocumented | ✅ RESOLVED — AA §4c documents frontend-thread-safe sink creation, backend-owned writes, and synchronization requirements. | §4c Sink Thread Safety |
| GAP-0-C05-6 | No benchmark data justifying no-lock decision | ✅ RESOLVED — AA acceptance criteria adds microbenchmark in M19 to validate 0-cost assumption. | Acceptance Criteria (M19 cross-ref) |
