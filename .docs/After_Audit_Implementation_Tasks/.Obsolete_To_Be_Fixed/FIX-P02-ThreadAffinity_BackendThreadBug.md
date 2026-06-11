# FIX-P02-ThreadAffinity — Backend Thread Targeting Bug

## Severity
❌ Fail | 🔴 Critical

## Description
AA-P02 calls `SetThreadAffinity(GetCurrentThread())` from `InitializeLogging()` (Step 3). This runs on the **main/calling thread**, not the Quill backend thread. Since `Backend::start()` already spawned the backend thread before affinity is set, `GetCurrentThread()` returns the main thread handle — the backend thread is completely unaffected. The result: **thread affinity has zero effect on logging performance**.

Additionally, Quill v10.0.1 already provides `BackendOptions::cpu_affinity` (BackendOptions.h:146) which correctly targets the backend thread. The AA-P02 duplicates this functionality with the wrong-thread bug.

## Root Cause
- The original TASK-P02 had the same bug and AA-P02 claimed "Corrected" but blindly carried it forward
- No cross-reference to AA-C05 (Thread Model) — the thread ownership topology was never analyzed
- `GetCurrentThread()` was assumed without verifying which thread executes the code path
- Quill's `BackendOptions::cpu_affinity` field was not audited; if it had been, the redundancy and the thread-targeting issue would have been obvious

## Exact Fix
Three options, ordered by preference:

### Option A (Recommended): Set affinity before `Backend::start()`, then restore
On Windows, child threads inherit the parent thread's affinity mask. Set the main thread's affinity before starting Quill, then restore it after:

```cpp
// In InitializeLogging():
void InitializeLogging(const LoggingConfig& config) {
    DWORD_PTR original_mask = 0;
    bool need_restore = false;

    if (config.thread_affinity.processor_mask != 0) {
        // Save current affinity
        original_mask = SetThreadAffinityMask(GetCurrentThread(), config.thread_affinity.processor_mask);
        need_restore = (original_mask != 0);
    }

    // Start Quill AFTER setting affinity — backend thread inherits it
    quill::Backend::start(config.backend_options);

    // Restore main thread's original affinity
    if (need_restore) {
        SetThreadAffinityMask(GetCurrentThread(), original_mask);
    }
}
```

**Variant**: For NUMA (group affinity), use `SetThreadGroupAffinity` before `start()` and `SetThreadGroupAffinity` to restore.

### Option B: Use Quill's `BackendOptions::cpu_affinity`
Check if Quill v10.0.1's `cpu_affinity` actually works on Windows. The AA-P02 claim that it's "no-op on Windows" must be verified. If it works, simply set:

```cpp
config.backend_options.cpu_affinity = target_processor_id;
quill::Backend::start(config.backend_options);
```

If truly no-op, this option requires a Quill patch or workaround.

### Option C: Use `ManualBackendWorker` for custom thread management
Quill provides `Backend::acquire_manual_backend_worker()` (Backend.h:204) for advanced users. The custom thread can then have its affinity set directly. **Not recommended** — adds complexity and `ManualBackendWorker` explicitly doesn't support `cpu_affinity`, `thread_name`, `sleep_duration`, and `enable_yield_when_idle` (Backend.h:183-184).

### Code changes summary
1. **Remove** `windows::SetThreadAffinity()` call from `LoggerSetup.hpp` Step 3 (it targets wrong thread)
2. **Add** affinity-set-before-start logic in `InitializeLogging()` (or use `BackendOptions::cpu_affinity`)
3. **Update** `LoggingConfig::thread_affinity` — ensure it documents "applied to main thread before backend start; backend inherits"
4. **Reference** AA-C05 (Thread Model) in the implementation file

## Impact if NOT fixed
- Thread affinity is silently a no-op — no actual performance isolation for the backend thread
- NUMA-awareness claim is false (NUMA pinning on main thread has zero effect on logging)
- All acceptance criteria ("Backend thread runs on specified processor") are unmet despite claiming otherwise
- The bug propagates to any downstream system relying on thread-pinned logging performance

## Verification
1. After applying fix, spawn a test that calls `SetThreadAffinity` before `Backend::start()`
2. In the backend thread (logged via `Backend::get_thread_id()`), call `GetCurrentProcessorNumber()` or `GetThreadIdealProcessorEx()` to confirm the thread is on the expected core
3. Before fix: main thread is pinned but backend thread is not (check via `GetCurrentThreadId()` comparison)
4. After fix: both main thread (temporarily) and backend thread (permanently) show pinned processor
5. Unit test: `TEST(P02, ThreadAffinityOnBackend)` — verify via processor number API
