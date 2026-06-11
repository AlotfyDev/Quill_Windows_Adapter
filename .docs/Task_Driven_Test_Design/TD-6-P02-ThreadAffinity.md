# Test Design: Windows Thread Affinity

## Under Spec
- AA File: `AA-P02-ThreadAffinity.md`
- Phase: 6
- Key Requirements:
  - Affinity set on main thread BEFORE `Backend::start()` — backend thread inherits via Windows `CreateThread` affinity inheritance
  - `SetThreadAffinity()` and `PinToProcessor()` use `SetThreadAffinityMask` or `SetThreadGroupAffinity` (NUMA)
  - Main thread affinity is saved before `Backend::start()` and restored after
  - Backend thread runs on the specified processor (verified via `GetCurrentProcessorNumber()`)
  - Works on Windows 10+ x64

## Test Harness
- **Fixture**: 
  - Quill lifecycle: `Backend::start()` with `BackendOptions` (minimal sleep duration for fast test)
  - `InitializeLogging()` from `LoggerSetup.hpp` with `ThreadAffinityConfig`
  - A test helper that runs code on the backend thread via `quill::Backend::try_callback()` or by logging from a frontend logger and inspecting thread state in a custom sink
  - Windows API directly for affinity mask verification (`GetThreadAffinityMask`, `GetCurrentProcessorNumber`, `GetCurrentThread`)
- **Mocked vs Real**: Real Quill Backend v10.0.1. Real Windows API calls. No mocking of OS primitives (affinity is an OS property). A test-only sink that captures the backend thread's processor number must be injected.
- **Preconditions**: Test process must have at least 2 logical processors available. Test must run with appropriate privileges (no special privilege required for `SetThreadAffinityMask` on Windows, but the thread must not be restricted by job object or process affinity mask). Run as administrator is NOT required — `SetThreadAffinityMask` works for user threads within the process affinity mask.

## Scenarios

### Positive Cases
- `ThreadAffinityConfig{ .processor_mask = 1 << 2 }` (CPU 2): after `InitializeLogging`, the backend thread's current processor number is 2 (or a processor within the mask). Verify via callback on backend thread that calls `GetCurrentProcessorNumber()`.
- `PinToProcessor(0)`: backend thread is pinned to CPU 0 — verify via `GetCurrentProcessorNumber()` in backend thread callback
- Main thread affinity is restored: after `InitializeLogging`, main thread's affinity mask equals its original mask (save + restore). Verify via `SetThreadAffinityMask(GetCurrentThread(), 0)` returning the current mask, then compare.
- `use_group_affinity = true`: NUMA group affinity set correctly (requires multi-group system; on single-group systems, falls back to legacy API)
- `processor_mask = 0` (no affinity): backend thread inherits default process affinity — no crash, no change
- Backend thread logs successfully after affinity is set — no Quill internal assertions due to affinity misconfiguration
- Calling thread (not necessarily the process main thread) calls `InitializeLogging()` with affinity — backend thread inherits that calling thread's affinity, verified via thread ID comparison
- Pre-start inheritance: verify `SetThreadAffinityMask` is called BEFORE `Backend::start()` — confirmed by inspecting call order in test code
- Original mask = 0 (thread allowed on any processor) — `SetThreadAffinityMask` returns 0, `GetLastError() == ERROR_SUCCESS` confirms success; restore path uses the saved 0 correctly

### Negative / Error Cases
- Invalid `processor_mask` (e.g., bit 63 on a 4-CPU system) — `SetThreadAffinityMask` returns 0 (failure), `SetThreadAffinity` returns false. Logging still works (graceful degradation).
- `group` out of range on a single-group system — `SetThreadGroupAffinity` fails, function returns false. Logging continues.
- Thread affinity set after `Backend::start()` (the OLD buggy pattern) — verifies the OLD behavior does NOT pin the backend thread. A regression test to prevent re-introduction.
- Process affinity mask restricts the desired CPU — `SetThreadAffinityMask` respects the process mask; verify the resulting thread affinity is the intersection of requested and allowed

### Production Realities
- Concurrent backend thread affinity with frontend logging: while backend thread is pinned to CPU 2, frontend threads log on CPUs 0, 1, 3. Verify no crash, no data race, no queue corruption.
- NUMA scenario: backend thread pinned to NUMA node 0. Memory allocations for log buffers should prefer node 0. Verify via `GetNumaNodeProcessorMask` that the thread's affinity is within node 0's mask.
- Process with >64 logical processors: `SetThreadAffinityMask` uses `DWORD_PTR` (64 bits). For >64 CPUs, must use `SetThreadGroupAffinity`. Test with `processor_mask` value >2^32 to verify the 64-bit mask works.
- >64 CPU with `use_group_affinity = false`: verify that bits beyond bit 63 are ignored by `SetThreadAffinityMask`, forcing affinity within group 0 — the runtime check recommendation in AA spec is noted but not enforced in code yet
- `BackendOptions::cpu_affinity` on Windows (Quill's built-in, which is a no-op): verify it remains a no-op and our replacement takes effect
- System sleep/resume: after system resume, thread affinities may be reset by the OS. The backend thread must still be functional (no affinity-dependent crash). This is a system-level behavior, not directly testable in unit tests, but noted for manual QA.

### Thread Safety
- Affinity is set on the main thread, before the backend thread exists. No concurrent access to the affinity API.
- `GetCurrentThread()` in the pre-start block refers to the main thread (correct)
- After `Backend::start()`, the backend thread's affinity is inherited and immutable until the thread exits. No runtime affinity changes are needed.
- If runtime affinity change is needed later, `SetThreadAffinity` on the backend thread would require a callback mechanism. The AA spec does not address runtime changes.
- The save/restore of main thread affinity uses `SetThreadAffinityMask` which returns the previous mask atomically — safe even if another thread in the process changes affinity concurrently (unlikely but OS-guaranteed atomic)

## Assertions
- Backend thread's `GetCurrentProcessorNumber()` returns a value within the requested `processor_mask` bits
- Main thread's affinity mask after `InitializeLogging` equals its affinity mask before `InitializeLogging` (saved and restored)
- `SetThreadAffinity` returns `true` when the mask is valid, `false` when invalid (kernel rejects it)
- `PinToProcessor(0)` succeeds and pins the backend thread to CPU 0 (single bit in mask)
- No crash or Quill assertion failure when affinity is set — Quill does not read or verify thread affinity internally
- `GetCurrentProcessorNumber()` is called from the backend thread context, verified by thread ID comparison with `GetCurrentThreadId()`
- Pre-start approach: `SetThreadAffinityMask` called BEFORE `Backend::start()` — confirmed by log/trace of call order
- Original mask = 0 correctly restored: `SetThreadAffinityMask` returns 0, `GetLastError() == ERROR_SUCCESS` confirms valid save; restore passes 0 back successfully

## Failure Mode
- A test failure where backend affinity is NOT set: **production logging on wrong CPU** — cache misses, TLB pollution, increased latency for both the logger and the intended CPU's workload
- A test failure where main thread affinity is NOT restored: **main thread performance degradation** — all subsequent work on the main thread is pinned to one CPU, potentially causing cascading latency issues
- A test failure where affinity inheritance doesn't work (the OLD bug pattern re-emerges): **backend thread runs on arbitrary CPU** — root cause of the original P02-A audit issue
- A test failure where invalid mask crashes: **process termination** in production if a config typo specifies CPU 128 on an 8-CPU machine — the code must gracefully fall back, not crash
- A test failure where NUMA group affinity is silently ignored: **NUMA-unaware logging** on large systems — cross-NUMA memory access penalties for the log backend

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Terminology corrected from "main thread" to "calling thread" throughout spec + thread model reference (AA-C05) | Step 3 — Wire into LoggerSetup (pre-start approach) | GAP-6-P02-1 resolved; scenarios updated for non-main calling thread |
| `GetLastError()` disambiguation added for `SetThreadAffinityMask` return value 0 (failure vs "any processor") | Step 3 — Wire into LoggerSetup (pre-start approach) | GAP-6-P02-2 resolved; save/restore now handles edge case |
| Design Tradeoff section documents why runtime affinity change is intentionally NOT implemented | Step 3 — Wire into LoggerSetup (pre-start approach) | GAP-6-P02-3 deferred; workaround documented (process restart with updated config) |
| >64 CPU group affinity constraint documented with runtime check recommendation | Step 1 — `ThreadAffinityConfig` struct | GAP-6-P02-4 resolved; add scenario for >64 CPU group affinity |
| Pre-start affinity inheritance pattern implemented (set calling thread before `Backend::start()`, restore after) | Step 3 — Wire into LoggerSetup (pre-start approach) | Add scenario verifying inheritance, not post-start pinning |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-6-P02-1 | AA spec uses `GetCurrentThread()` before `Backend::start()` on the main thread. But `InitializeLogging()` may be called from any user thread, not necessarily the main thread. The spec assumes "main thread" throughout but the code uses "calling thread." | If a non-main thread calls `InitializeLogging()`, the backend thread inherits THAT thread's affinity, not the main thread's. The "main thread" documentation is misleading. | Rename to "calling thread" throughout the spec, or explicitly document that `InitializeLogging()` must be called from the thread whose affinity should be inherited. | ✅ RESOLVED — AA spec now consistently uses "calling thread" terminology with AA-C05 thread model reference |
| GAP-6-P02-2 | AA spec does not handle the case where `SetThreadAffinityMask` fails in the save step. If saving the original mask fails (returns 0), the code skips restore (`need_restore = (original_mask != 0)`). But `SetThreadAffinityMask` can return 0 both on failure AND if the current mask is 0 (meaning thread can run on any processor). | If the current mask is 0 (any processor), the restore is skipped, and the main thread permanently runs on the pinned processor. | Use `GetThreadAffinityMask` (or query before mutation) to distinguish "mask is 0" from "API failed". Or save via `GetProcessAffinityMask` + `GetThreadAffinityMask` combo. | ✅ RESOLVED — `GetLastError()` disambiguation added: `if (original_mask != 0 \|\| GetLastError() == ERROR_SUCCESS)` correctly handles the "mask is 0" vs "failure" distinction |
| GAP-6-P02-3 | No mechanism for runtime affinity change. If ops needs to move the logging backend to a different processor during incident response, there's no API. | Zero-day operational flexibility — affinity is frozen at init. | Add `SetBackendThreadAffinity()` that posts a callback to the backend thread (via Quill's `try_callback` or similar) to change affinity at runtime. | ⏸️ DEFERRED — AA Design Tradeoff section explains intentional omission; recommended workaround is process restart with updated config |
| GAP-6-P02-4 | `SetThreadGroupAffinity` requires Windows 7+, and the AA spec targets Windows 10+. This is fine. But `SetThreadAffinityMask` is deprecated on systems with >64 processors. The spec should document that for >64 CPU systems, `use_group_affinity = true` is MANDATORY, not optional. | On >64 CPU systems with `use_group_affinity = false`, `SetThreadAffinityMask` only sets affinity within group 0, which may not be the optimal group for the backend. | Add a static_assert or runtime check: if `processor_mask` has bits set beyond 64, force group affinity. Document the constraint. | ✅ RESOLVED — AA spec documents >64 CPU constraint with runtime check recommendation to force group affinity when bits beyond bit 63 are set |
