# Test Design: Rate-Limited Macros (Per-Logger and Global)

## Under Spec
- AA File: `AA-M05-RateLimiting.md`
- Phase: 2
- Key Requirements:
  - `LOG_LIMIT_PER_SEC(logger, N, ...)` fires at most N times per second per call site (macro-local static)
  - `LOG_LIMIT_PER_MIN(logger, N, ...)` fires at most N times per minute per call site
  - Burst allowance: first log after each suppression window ALWAYS fires
  - `LOG_GLOBAL_LIMIT(logger, N, ...)` shares one counter across all TUs via `GetGlobalRateLimiter()` defined in `RateLimited.cpp`
  - All limiters use `std::atomic` with relaxed memory ordering
  - Existing `EVERY_N` and `LIMIT` macros remain unchanged

## Test Harness
- **Fixture setup**: Create a test TU that includes `RateLimited.hpp`; instantiate a `quill::Logger` (or mock logger that counts `LOG_INFO` invocations); use `std::chrono::steady_clock` for time measurement in timing tests; for per-call-site isolation tests, instantiate the macro at multiple call sites (functions or lambdas in the same TU).
- **Mock vs real**: Logger is real Quill logger but output is redirected to a null sink (no I/O). Rate limiter under test is the actual macro/code (real `std::atomic`). Time source is real steady clock for timing; unit tests advance time via `std::this_thread::sleep_for` for second/minute boundaries.
- **Precondition requirements**: Global rate limiter state reset between tests; use `GetGlobalRateLimiter().ResetForTesting()` which now exists as a test-only API per AA spec.

## Scenarios

### Positive Cases
- `LOG_LIMIT_PER_SEC(logger, 5, ...)` called 100 times in a tight loop: at most 5 `LOG_INFO` invocations in any 1-second wall-clock window
- `LOG_LIMIT_PER_MIN(logger, 60, ...)` called 6000 times over 60 seconds: at most 60 `LOG_INFO` invocations per minute
- Burst allowance: after 2 seconds of silence, the first call in the new second always fires, even if the previous second's rate was at capacity
- `LOG_GLOBAL_LIMIT(root_log, ...)` with `GetGlobalRateLimiter().SetMaxPerSecond(100)` called from 10 different functions in 10 different TUs: total `LOG_INFO` invocations across all call sites ≤ 100 per second
- `LOG_LIMIT_PER_SEC` defined at two different call sites (two different source locations): each has its own independent counter, both can fire up to N times per second independently
- Multiple rate values: rate=1 fires at most 1/second; rate=1000 fires at most 1000/second (up to `LOG_INFO` throughput)

### Negative / Error Cases
- `LOG_LIMIT_PER_SEC(logger, 0, ...)`: rate of 0 suppresses all messages (except bug? spec says "first after window always fires" — if window is infinite, never fires). Test determines behavior: macro should fire first call then suppress forever.
- Rate set to `UINT32_MAX`: no effective limit (rollover safe due to unsigned comparison with `<`)
- `LOG_GLOBAL_LIMIT` called with `SetMaxPerSecond()` set to different values from different call sites: `SetMaxPerSecond()` applies the last-set value to the singleton. Test confirms shared state behavior.
- `LOG_LIMIT_PER_SEC` used inside an inline function in a header, included by multiple TUs: each TU gets its own independent limiter (ODR isolation). Test verifies counters are NOT shared across TUs.
- Concurrent burst at second boundary: 16 threads all call `LOG_LIMIT_PER_SEC` simultaneously when `now != last` — only one thread's store of `_count.store(0)` actually resets the count; the first N threads to increment after reset pass. Acceptable best-effort behavior.
- `GetGlobalRateLimiter()` called concurrently from 4 threads on first invocation: C++11 guarantees function-local static initialization is thread-safe — only one instance created.

### Production Realities
- High-frequency logging loop (1M calls/sec) with `LOG_LIMIT_PER_SEC(logger, 1000, ...)`: atomic load/store per invocation adds ~2-5ns overhead — negligible compared to `LOG_INFO` itself
- Buggy subsystem floods `LOG_GLOBAL_LIMIT(logger, 10, ...)` from 1000 threads: only 10 messages/sec pass globally; the rest are cheap atomic increments — no I/O, no queue pressure
- `LOG_LIMIT_PER_SEC` used in signal handler (async-signal-safe functions only): macros use `std::atomic` which is not guaranteed signal-safe. Document: DO NOT use in signal handlers.
- Rate limiters used during shutdown: atomic operations remain valid (no heap access after `static` initialization) — safe during `ShutdownLogging()` from non-Quill threads

### Thread Safety
- All limiters use `std::atomic` with `memory_order_relaxed` — safe for counting, no inter-thread ordering guarantees needed
- `LOG_LIMIT_PER_SEC` macro: `_last_ts` and `_count` are separate atomics with no sequential consistency between the load of `_last_ts` and the store of `_count` — on non-x86 (ARM/Power), threads may observe stale `_last_ts` values, causing the rate to be slightly inaccurate (slightly more or fewer logs pass). This is acceptable for a rate limiter.
- Global rate limiter `TryAcquire()`: `epoch_` and `global_count_` are separate atomics — on ARM, thread may see updated `epoch_` but stale `global_count_`, leading to ~N extra messages per second (bounded by number of threads). Acceptable.
- Data race potential: two threads calling `LOG_LIMIT_PER_SEC` when `now != last` — one thread stores new `_last_ts` and resets `_count` while the other increments `_count` after the reset. Result: some messages in the transition second may be over- or under-counted. Best-effort.
- `GetGlobalRateLimiter()` is thread-safe per C++11 function-local static guarantee. No external synchronization needed.

## Assertions
- `LOG_LIMIT_PER_SEC(logger, 5, ...)` called 1000 times: count of actual log invocations in any 1-second window ≤ 5 (measured via counting sink)
- First call after 1-second idle period: always fires (in any 2-second interval with idle gap, at least 2 messages fire — one in each second)
- `LOG_LIMIT_PER_MIN(logger, 30, ...)` called 100 times: count per 60-second window ≤ 30
- `LOG_GLOBAL_LIMIT(logger, 10, ...)` called from 3 different source locations, 100 calls each: total log invocations per second ≤ 10
- Two different `LOG_LIMIT_PER_SEC(logger, 5, ...)` call sites: each independently achieves 5 calls/second
- `GetGlobalRateLimiter()` returns same address across all calls within process: verified via pointer comparison across TUs

## Failure Mode
- Rate limiter fails to suppress (atomic bug, e.g., missing `fetch_add`): excessive log volume, but no data loss or crash. Degraded performance.
- Rate limiter over-suppresses (count reset never happens): operator misses important log messages. Silent data loss (information loss, not data corruption).
- Global rate limiter creates multiple instances (static-in-header bug): the per-TU guarantee is broken — each DLL/SO gets its own instance. Silent degradation of the global guarantee. This is a linker-level test failure — verify with pointer identity test across TUs.
- Relaxed atomic reordering on ARM: rate limiter allows ~N extra messages per second per thread. Degraded performance, not a correctness failure.

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Race at second boundary documented as acceptable | §1 (note after macro) | GAP-2-M05-1: ✅ RESOLVED |
| LOG_GLOBAL_LIMIT no longer takes rate parameter | §2 Global Rate Limiter | GAP-2-M05-2: ✅ RESOLVED; updated scenarios |
| ResetForTesting() added to GlobalRateLimiter | §2 GlobalRateLimiter class | GAP-2-M05-3: ✅ RESOLVED; updated test harness |
| Weakly-ordered CPU architecture note added | §4 Thread Safety (architecture note) | GAP-2-M05-4: ✅ RESOLVED |
| Inline function / header static warning documented | §4 Thread Safety (inline warning) | GAP-2-M05-5: ✅ RESOLVED; added header-isolation scenario |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-2-M05-1 | `LOG_LIMIT_PER_SEC` macro has a race at second boundary: two threads see `now != last` simultaneously; one resets `_count` while the other increments the old value. | Count is approximate during high-contention second transitions — may allow up to (N + thread_count) messages in a transition window. | Document the race explicitly as "best-effort rate limiting during contention"; or use `compare_exchange_weak` on `_last_ts` to ensure only one thread resets per second. | ✅ RESOLVED — AA spec §1 documents race as best-effort |
| GAP-2-M05-2 | `LOG_GLOBAL_LIMIT` passes `max_per_sec` to `GetGlobalRateLimiter()` on every call, but the parameter is only used on first-call initialization. If two TUs pass different values, the first-call value wins silently. | Operators may believe different call sites use different limits; the actual behavior is undefined (first-call-dependent). | Remove `max_per_sec` from the macro; require explicit `SetMaxPerSecond()` call during initialization. Or document that the first-call value is authoritative. | ✅ RESOLVED — AA spec §2 removes rate param from LOG_GLOBAL_LIMIT; uses SetMaxPerSecond() during init |
| GAP-2-M05-3 | No testability interface: `GlobalRateLimiter` is a singleton with no reset path. Tests in the same process cannot isolate scenarios. | Cannot write deterministic unit tests; must rely on integration tests with sleep-based timing, which are flaky. | Add `void ResetForTesting() noexcept` or make `max_per_second_` and counters publicly resettable. Document as test-only API. | ✅ RESOLVED — AA spec §2 adds ResetForTesting() method |
| GAP-2-M05-4 | `memory_order_relaxed` usage is documented as intentional but the spec does not discuss the consequences on weakly-ordered architectures (ARM, Power). | Code that works on x86 may produce incorrect counts on ARM64, which is increasingly common in trading (AWS Graviton, Apple Silicon). | Add comment noting that relaxed ordering is safe for rate limiting (approximate counting) but would NOT be safe for precise synchronization. Consider `memory_order_acq_rel` on architecture-specific path. | ✅ RESOLVED — AA spec §4 adds architecture note for weakly-ordered CPUs |
| GAP-2-M05-5 | Macro uses `static` local variables in macro expansion — each call site in a header or inline function gets its own counter. This is intentional but surprising: two calls to the same inline function from different TUs get DIFFERENT limiters. | ODR-used inline functions may get merged per TU, breaking the "independent per-call-site" mental model. | Add a documented warning that inline functions with `LOG_LIMIT_PER_SEC` do NOT share counters across TUs. | ✅ RESOLVED — AA spec §4 adds inline function warning |
