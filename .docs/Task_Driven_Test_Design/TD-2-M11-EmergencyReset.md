# Test Design: Emergency Reset State Machine

## Under Spec
- AA File: `AA-M11-EmergencyReset.md`
- Phase: 2
- Key Requirements:
  - State machine: Normal (0) → Degraded (1) → Recovering (2) → Normal (0); Fatal (3) is terminal (abort/exit)
  - `Reset()` transitions Degraded → Recovering → Normal, flushes backtraces, increments epoch, invokes recovery callbacks, records in HealthProbe
  - `NotifyEmergency()` transitions Normal → Degraded (not from Fatal)
  - `FatalError()` transitions to Fatal and terminates the process
  - `IsEmergencyMode()` returns true if NOT Normal (covers Degraded, Recovering, Fatal)
  - `GetRecoveryEpoch()` returns a monotonically increasing epoch counter
  - Recovery callbacks receive the new epoch on `Reset()`
  - HealthProbe tracks recovery count + timestamp

## Test Harness
- **Fixture**: 
  - `EmergencyConfig` with `flush_logs_on_crash = false`, `abort_on_fatal = false` (for non-terminating tests of FatalError — or don't test FatalError's termination directly, test state transition only)
  - No Quill lifecycle needed for state machine tests (they test statics, not sinks)
  - For backtrace flushing test: Quill logger must exist, so `Backend::start()` + a logger with backtrace enabled is needed
  - A RAII test guard that resets global state between tests (since `EmergencyManager` is all statics, test order matters)
  - For concurrent tests: `std::thread` pool that calls `NotifyEmergency` and `Reset` simultaneously
- **Mocked vs Real**: Real `EmergencyManager`, real `HealthProbe`. For termination-avoidance in FatalError tests, mock `std::abort`/`std::exit` via `#define` or linker hook. For backtrace tests, real Quill logger.
- **Preconditions**: `EmergencyManager::Initialize()` called with a valid config before each test (since statics may be dirty). Test isolation is critical — state leaks between tests will cause spurious failures.

## Scenarios

### Positive Cases
- `GetState()` returns `Normal` initially (after `Initialize`)
- `NotifyEmergency(ErrorContext{ErrorCode::QueueFull})` transitions from Normal to Degraded
- `Reset()` on Degraded state: transitions Degraded → Recovering (briefly) → Normal; epoch increments by exactly 1
- `Reset()` on Normal state: no-op (or maintains Normal) — behavior not explicitly defined, but should not crash
- `IsEmergencyMode()` returns `true` after `NotifyEmergency()` (Degraded state)
- `IsEmergencyMode()` returns `false` after a full Reset() cycle completes (Normal state)
- `IsEmergencyMode()` returns `true` in Fatal state
- `GetRecoveryEpoch()` returns 0 initially, increments by 1 on each Reset() (uint64_t, no wrap-around concern)
- Recovery callbacks registered via `RegisterRecoveryCallback()` are invoked during `Reset()` with the correct new epoch value
- `HealthProbe::RecordRecovery()` increments recovery count
- Backtrace flushed during `Reset()` when a valid logger with backtrace is configured
- `FatalError()` transitions to Fatal state (verify before termination point)

### Negative / Error Cases
- `NotifyEmergency()` in Degraded state: remains Degraded. Test: second `NotifyEmergency()` does not reset epoch or trigger callbacks.
- `NotifyEmergency()` called while in Recovering state: transitions back to Degraded (new error preempts ongoing recovery). Test that a subsequent `Reset()` is required to attempt recovery again.
- `NotifyEmergency()` in Fatal state: should be a no-op (process is about to terminate anyway)
- `FatalError()` in Normal, Degraded, or Recovering: transitions to Fatal, terminates
- `Reset()` with no registered recovery callbacks: no crash, transitions succeed
- `Reset()` called from within a recovery callback: potential infinite recursion. Verify the code guards against this (e.g., by setting state before invoking callbacks, or by using a reentrancy guard).
- `Reset()` when `emergency_logger_name` is invalid (logger doesn't exist): `quill::Frontend::get_logger()` returns null, skip backtrace flush, continue reset
- `FatalError()` called before `Backend::start()` (e.g., during initialization): guard `if (quill::Backend::is_running())` prevents crash on `Backend::stop()` — test by calling `FatalError()` without starting the backend
- `HealthProbe::RecordRecovery()` overflow: counter wraps around after 2^32 recoveries. Test wraps to 0 and continues without UB.

### Production Realities
- Concurrent emergency and reset: 10 threads call `NotifyEmergency()` simultaneously while 2 threads call `Reset()`. Verify eventual consistency (state is one of the valid values, no Fatal→Normal transition, epoch is monotonically increasing).
- `Reset()` during high-frequency logging: backtrace flush (`logger->flush_backtrace()`) may block if the backend is busy. Verify bounded wait time.
- Registering recovery callbacks after `Reset()` has been called once: callbacks are stored in a vector, so late-registered callbacks will fire on the NEXT Reset(). Test that late-registered callbacks don't fire retroactively.
- HealthProbe counters read by an external monitoring thread while emergency events occur: verify atomic reads see consistent counter values (no torn reads for `uint32_t` on x64 — atomic provides this).
- Process crash during `Reset()` between state transitions: on next process start, `EmergencyManager` initializes fresh (Normal, epoch=0). No persistent state.

### Thread Safety
- `EmergencyState` is `std::atomic<uint8_t>` — lock-free on all platforms. Memory ordering: `NotifyEmergency` uses `release`, `GetState` uses `acquire`. Verify the ordering guarantees work: a thread calling `GetState()` after observing side effects of `NotifyEmergency()` must see the state transition.
- `recovery_epoch_` is `std::atomic<uint32_t>` — `load(acquire)`, `store(release)`, `fetch_add(acq_rel)`.
- `recovery_callbacks_` is a `std::vector` protected by `std::mutex`. `RegisterRecoveryCallback` locks the mutex; `Reset()` copies the vector under the lock, then releases the lock before iterating the copy — preventing deadlock if a callback calls `RegisterRecoveryCallback()`.
- `HealthProbe::recovery_count_` is `std::atomic<uint32_t>` — safe for concurrent increment.
- Race condition: thread A calls `NotifyEmergency()` (Normal→Degraded), thread B calls `Reset()` simultaneously. If B reads Degraded before A writes it, B may skip reset. Acceptable — this is a "best effort" coordination.

## Assertions
- State machine follows valid transitions only (see state diagram in spec). No Degraded→Fatal without error.
- Epoch is strictly monotonically increasing (uint64_t) — observed order may not be total due to concurrency, but the counter value itself must never decrease and cannot wrap in practice
- Recovery callback receives `epoch` that matches `GetRecoveryEpoch()` at the time of the call
- `HealthProbe::GetRecoveryCount()` matches the number of successful `Reset()` completions
- Backtrace flush is called exactly once per `Reset()` — verify via a counter in a test logger spy
- No deadlock when `NotifyEmergency` is called from within a recovery callback (if such reentrancy is possible)
- `Reset()` from Normal state: state stays Normal, epoch unchanged, callbacks not invoked

## Failure Mode
- A test failure where state transition is incorrect: **production emergency logic broken** — the system may not detect emergencies, may not recover, or may incorrectly report healthy state during an emergency
- A test failure where epoch does not increment: **stale state detection fails** — subsystems that check epoch cannot distinguish pre- vs post-recovery state, potentially skipping re-initialization
- A test failure where recovery callbacks are not invoked: **subsystems never re-arm** — safety mechanisms remain disabled, trading may proceed without risk controls
- A test failure where concurrent access causes state corruption: **process crash** during emergency — the worst time for a crash
- A test failure where backtrace is not flushed: **lost diagnostic data** — the events leading up to the emergency are not captured in logs, making post-mortem impossible

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Recovering state semantics documented | §3 Symmetric HealthProbe | GAP-2-M11-1: ✅ RESOLVED |
| Copy-under-lock pattern for callbacks | §4 Reset() with Coordination | GAP-2-M11-2: ✅ RESOLVED; updated thread safety section |
| Recovery→Degraded transition defined | §1 State Machine (transition note) | GAP-2-M11-3: ✅ RESOLVED; added NotifyEmergency-during-Recovery scenario |
| static_assert HealthProbe API integration check | §3 (compile-time note) | GAP-2-M11-4: ✅ RESOLVED |
| Backend::is_running() guard in FatalError | §2 Step 2 (guard detail) | GAP-2-M11-5: ✅ RESOLVED; added early-FatalError scenario |
| Epoch changed from uint32_t to uint64_t | §2 Recovery Epoch | GAP-2-M11-6: ✅ RESOLVED; updated assertions |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-2-M11-1 | AA spec shows `Reset()` stores `Recovering` state, invokes callbacks, then stores `Normal`. If a callback inspects `GetState()` during invocation, it sees `Recovering` (not `Normal`). But the spec's intent says "subsystems must re-check" — the callback epoch is the trigger, not the state. However, a callback that calls `IsEmergencyMode()` during `Recovering` gets `true`, which is correct (recovery is still in progress). This is fine but underspecified — what if a callback takes so long that `IsEmergencyMode()` returns `false` before it returns? | Inconsistent subsystem view during multi-callback reset | Document: "During Reset(), state is Recovering while callbacks execute. Subsystems must use the epoch parameter, not IsEmergencyMode(), to determine whether to re-arm. After all callbacks return, state transitions to Normal." | ✅ RESOLVED — AA spec §3 adds Recovering state semantics documentation |
| GAP-2-M11-2 | `RegisterRecoveryCallback` is not thread-safe with respect to `Reset()`. If a callback is registered while `Reset()` is iterating the vector (mutex helps, but the callback vector could be modified during iteration if the mutex is released). The spec uses a `std::mutex` but does not specify the locking strategy for iteration vs registration. | Race: callback added during Reset() may be missed or iterated partially | Document or implement: registration takes the mutex (write lock), Reset() copies the vector under the mutex then releases before iterating (read-then-release pattern). Add test for concurrent register+reset. | ✅ RESOLVED — AA spec §4 implements copy-under-lock pattern |
| GAP-2-M11-3 | The spec adds `EmergencyState::Recovering` but does not specify what happens if `NotifyEmergency` is called during Recovery. Should it transition to Degraded? Remain Recovering? Go to Fatal? The state diagram shows only Fatal from Degraded. | Missing transition: Recovery → Degraded if a new error occurs during reset | Define the transition: either (a) allow Recovery → Degraded (new error preempts recovery), or (b) stay Recovery and queue the emergency for after Normal (complex), or (c) go to Fatal if a new error occurs during recovery (conservative). Recommend (a). | ✅ RESOLVED — AA spec §1 defines Recovery→Degraded transition |
| GAP-2-M11-4 | `HealthProbe::RecordRecovery()` is defined in the spec but the existing `HealthProbe` class is not shown. The integration point between `EmergencyManager::Reset()` and `HealthProbe::RecordRecovery()` is not verified. | HealthProbe may not have the expected API | Add a `static_assert` or compilation test that `HealthProbe::RecordRecovery()` exists and matches the expected signature. | ✅ RESOLVED — AA spec §3 adds static_assert integration check |
| GAP-2-M11-5 | `Backend::stop()` is called in `FatalError()` but `EmergencyManager` does not own the Backend lifecycle. If `FatalError` is called before `Backend::start()` (e.g., during initialization), `Backend::stop()` may crash. | Process crash during fatal error handling — the worst-case scenario | Add a guard: `if (quill::Backend::is_running()) { Backend::stop(); }` or use a flag to track backend state. | ✅ RESOLVED — AA spec §2 adds Backend::is_running() guard |
| GAP-2-M11-6 | The epoch is `uint32_t`. At 1000 resets/second, it wraps in ~49 days. After wrap, `GetRecoveryEpoch() == 0` again. Subsystems that compare epoch to detect stale state may incorrectly think no recovery has occurred. | Stale state detection fails after ~49 days of rapid cycling | Change epoch to `uint64_t` (effectively infinite at any realistic reset rate), or document the wrap-around behavior and recommend epoch comparison using `!=`, not `<`. | ✅ RESOLVED — AA spec §2 changes epoch to uint64_t |
