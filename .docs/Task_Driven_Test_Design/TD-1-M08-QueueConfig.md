# Test Design: Queue Configuration

## Under Spec
- AA File: `AA-M08-QueueConfig.md`
- Phase: 1
- Key Requirements:
  - Populate `config/QueueConfig.hpp` with `QueueType` enum (Bounded, Unbounded) and `QueueConfig` struct (`type`, `capacity`, `warn_on_unbounded_release`)
  - Default is `Bounded` with capacity 8192; backward-compatible for callers that don't set `queue`
  - Wire queue config into `InitializeLogging()` via `quill::BackendOptions` before `quill::Backend::start()`
  - Capacity minimum clamping: values below 1024 are silently clamped to 1024 at runtime (no crash or deadlock)
  - Unbounded with non-default capacity emits info message: "capacity=%zu ignored for Unbounded queue"
  - Release build (`#ifndef NDEBUG`) prints OOM warning to stderr if unbounded queue selected with `warn_on_unbounded_release == true`
  - Queue configuration is startup-only — no hot-reload in v0.2.0
  - Queue config targets `BackendOptions` (MPMC collector), not `FrontendOptions` (per-thread SPSC) — resolving the architectural shift from TASK-M08

## Test Harness
- **Fixture**: A test that creates a temporary directory, constructs a `LoggingConfig` with a file sink pointing to a temp file, calls `InitializeLogging()`, writes N log messages, calls `ShutdownLogging()`, then inspects the output file. Each test case uses a fresh fixture — Quill's `std::call_once` guard prevents re-init within the same process, so each test MUST run as a separate process or use linker-level isolation.
- **Mock vs Real**: Real Quill v10.0.1 backend (no mocking — queue behavior is a Quill-internal property). A test helper binary is compiled per scenario (or use Google Test with `--gtest_repeat=0` and process-spawn isolation via `::testing::InitGoogleMock` with death tests). The stderr warning is captured by redirecting stderr to a stringstream or temp file.
- **Precondition Requirements**: Quill v10.0.1 installed via vcpkg. `config/QueueConfig.hpp` populated. `LoggingConfig` has `queue` field. `InitializeLogging()` wires to `BackendOptions`. No prior `quill::Backend::start()` call in the process (each test is isolated).

## Scenarios

### Positive Cases
- **Default config**: `LoggingConfig{}` produces `Bounded` queue with capacity 8192. `InitializeLogging` succeeds, log messages are written to the sink file without blocking under normal load.
- **Explicit Bounded/8192**: Setting `queue.type = QueueType::Bounded, queue.capacity = 8192` produces identical behavior to default — confirms backward compatibility path.
- **Bounded/4096**: Queue capacity = 4096. Producer thread writes 5000 messages faster than consumer. After 4096, the producer thread blocks (Bounded blocking queue). Test verifies all 5000 messages eventually reach the sink (no loss).
- **Unbounded/8192**: Queue type = Unbounded. Producer writes 100k messages without blocking. All messages arrive at sink. Verifies unbounded does not block producer.
- **Large capacity Bounded/65536**: Batch processing scenario. Writes 100k messages with Bounded/65536. No blocking observed (queue never fills).
- **Release-build warning**: Build in Release (`#ifndef _DEBUG`), set `queue.type = Unbounded, queue.warn_on_unbounded_release = true`. Stderr contains the string "WARNING [Logger_Adapter]: Unbounded queue selected in Release build".
- **Release-build warning suppressed**: Build in Release, set `queue.warn_on_unbounded_release = false`. Stderr does NOT contain the warning.
- **Debug-build no warning**: Build in Debug, set `queue.type = Unbounded`. Stderr does NOT contain the warning (guarded by `#ifndef _DEBUG`).

### Negative / Error Cases
- **Zero capacity clamped to minimum**: `queue.capacity = 0` with `Bounded`. AA specifies clamping to MIN_QUEUE_CAPACITY (1024). Test verifies `InitializeLogging()` succeeds, stderr contains "WARNING [Logger_Adapter]: capacity=0 is below minimum 1024, clamping to 1024", and queue behaves as Bounded/1024 (producer blocks after 1024 enqueues).
- **Capacity below minimum (512)**: `queue.capacity = 512`. Clamped to 1024. Same warning assertion as zero case. Verify no deadlock or crash.
- **Capacity saturation with slow consumer**: Bounded/16 capacity with a file sink on a slow (network) drive. Producer writes at high speed. After 16 messages, producer blocks. Test verifies the blocked thread eventually unblocks when consumer drains. Timeout guard (5 s) to detect deadlock.
- **Unbounded OOM simulation**: Unbounded queue with producer writing exponentially faster than consumer (e.g., `while(true) LOG_INFO(...)`). Not a deterministic test — document as manual/stress test. In CI, limit to N=1M messages and verify no crash.
- **Missing `queue` field**: Code that constructs `LoggingConfig{}` without setting `.queue` must compile and behave identically to Bounded/8192. Static assertion test.
- **Unbounded with non-default capacity warns**: Set `queue.type = Unbounded, queue.capacity = 16384`. Stderr must contain "INFO [Logger_Adapter]: capacity=16384 ignored for Unbounded queue". Writes 100k messages — all must arrive at sink without blocking.
- **Unbounded with default capacity (8192) no warning**: Set `queue.type = Unbounded` but leave `capacity = 8192` (default). Stderr must NOT contain the capacity-ignored message (since capacity matches default, no warning needed).

### Production Realities
- **Backend thread crash during queue drain**: Kill the backend thread (e.g., `std::thread::native_handle` + platform terminate). Frontend threads continue writing to the queue. Verify that after restart (not supported in v0.2.0, but document behavior) the queue may be in an inconsistent state.
- **Disk full**: File sink write fails. Bounded queue — producer continues to block until consumer can write. Unbounded queue — queue grows unboundedly until OOM. Test: fill disk to 100%, write one log message, verify Bounded blocks (timeout) and Unbounded grows memory (monitor RSS). Document as manual stress test.
- **Memory pressure**: Unbounded queue under rapid allocation. Use `SetProcessWorkingSetSize` to limit memory, then write aggressively. Verify `std::bad_alloc` is thrown on the producer thread and Quill's exception handling catches it (or the process terminates). Document behavior.

### Thread Safety
- **Frontend thread (caller)**: Calls `LOG_INFO()` (enqueues to SPSC). Must not block for Bounded unless queue full. Must never block for Unbounded.
- **Backend thread (Quill worker)**: Reads from backend MPMC collector, writes to sinks. AA-C05 designates this as the sole sink writer.
- **Queue type change during operation**: Not possible (startup-only). Verify that `InitializeLogging()` is called once and any subsequent call is a no-op (guarded by `std::call_once`). Attempting to re-init with different queue config after shutdown is UB per AA-C05 — test that the UB is observable (e.g., `GetLogger()` returns nullptr or stale pointer).
- **Concurrent shutdown and produce**: Thread A calls `ShutdownLogging()`, Thread B calls `LOG_INFO()`. Per AA-C05, UB after shutdown. Test documents the observable behavior (messages may be lost, backend may not drain them all).

## Assertions
- `InitializeLogging()` returns `true` for all valid queue configs.
- Default `QueueConfig` has `type == QueueType::Bounded` and `capacity == 8192`.
- Bounded queue with capacity N blocks producer after N enqueues (measured via thread timing — producer thread is suspended until consumer drains).
- Unbounded queue never blocks the producer (measurable: producer thread completes without waiting).
- All enqueued messages are written to the sink file after `ShutdownLogging()` (full drain).
- Stderr warning message contains exact text `"WARNING [Logger_Adapter]: Unbounded queue selected in Release build"` when conditions met.
- Zero capacity is handled gracefully — either rejected at init or clamped. No crash.
- `#ifndef NDEBUG` block is compiled out in Debug builds (stderr warning absent).

## Failure Mode
- **TestDefaultQueueType fails**: Default changed from Bounded to Unbounded → OOM risk in production. **Impact: Silent data loss or crash under load.**
- **TestBoundedBlocksAtCapacity fails**: Bounded queue does not block → messages silently dropped or queue grows beyond capacity. **Impact: Silent data loss (if Quill drops) or OOM (if unbounded behavior).**
- **TestUnboundedNeverBlocks fails**: Unbounded queue blocks → performance regression, potential deadlock. **Impact: Production stalls.**
- **TestReleaseWarning fails**: OOM warning not printed → operational blind spot. **Impact: OOM in production without operator awareness.**
- **TestBackwardCompat fails**: Existing unmodified `LoggingConfig` does not get Bounded/8192. **Impact: Behavior change for all existing callers.**

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Capacity minimum clamping (1024) with warning | Step 2 (clamp code) | Added scenarios "Zero capacity clamped to minimum" and "Capacity below minimum (512)"; updated Key Requirements |
| Unbounded with non-default capacity warns | Step 2 (warn code) | Added scenario "Unbounded with non-default capacity warns" |
| Uses `#ifndef NDEBUG` not `_DEBUG` | Step 2 (release warning) | Updated assertion from `_DEBUG` to `NDEBUG`; resolved GAP-1-M08-2 |
| Added BackendOptions vs FrontendOptions design note | § Design Note | Updated Key Requirements |
| Added startup-only ordering contract with documentation | Step 3b | Resolved GAP-1-M08-4 |
| GAP-1-M08-5 (test isolation) remains an open concern | N/A | Not addressed by AA; TD still handles via process isolation |

## Spec Gap Notes (SGN)

### Resolved Gaps

These GAPs were raised during test design and subsequently resolved by Impact Analysis fixes applied to the AA spec.

| Gap ID | Issue | Resolution | AA Spec Section |
|--------|-------|------------|-----------------|
| GAP-1-M08-1 | Capacity silently ignored for Unbounded | ✅ RESOLVED — AA Step 2 adds runtime warning when capacity differs from default with Unbounded type. | Step 2 |
| GAP-1-M08-2 | `_DEBUG` is MSVC-specific | ✅ RESOLVED — AA uses `#ifndef NDEBUG` (standard C++). | Step 2 |
| GAP-1-M08-3 | Zero/minimum capacity deadlock risk | ✅ RESOLVED — AA adds clamping to `MIN_QUEUE_CAPACITY = 1024` with stderr warning. | Step 2 |
| GAP-1-M08-4 | No ordering contract for BackendOptions before start() | ✅ RESOLVED — AA Step 3b adds startup-only documentation with ordering warning. | Step 3b |

### Open Gaps

| Gap ID | Issue | Architectural Impact | Recommendation |
|--------|-------|---------------------|----------------|
| GAP-1-M08-5 | AA-M08 does not address process-level test isolation. Quill uses global state (`std::call_once` guard, singleton `Backend`). Multiple test cases cannot run in the same process without interference. | Tests cannot be meaningfully composed — each requires a separate process, making CI expensive. | Document test isolation requirement. Consider a death-test pattern (Google Test `EXPECT_DEATH` for some scenarios, or use a test launcher that spawns one subprocess per scenario). For CI, limit to ~5 process-spawn tests. |
