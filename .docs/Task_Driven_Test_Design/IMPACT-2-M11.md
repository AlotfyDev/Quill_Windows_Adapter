# Impact Analysis: Emergency Reset State Machine

## Summary
Total GAPs: 6 | P0: 1 | P1: 3 | P2: 2 | API Changes: 1

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-2-M11-1 | 🟡 P2 | During `Reset()`, state is `Recovering` while callbacks execute — inconsistent subsystem view | Low risk: callbacks see `Recovering` (correct), but if callback takes too long, state flips to `Normal` before it returns | No | 📝 Document only |
| GAP-2-M11-2 | ⚠️ P1 | `RegisterRecoveryCallback` not thread-safe w.r.t `Reset()` — callback vector iterated while modifiable | Race: callback added during Reset() may be missed or iterated partially — recovery action skipped | No | 🛠️ Fix now |
| GAP-2-M11-3 | ⚠️ P1 | No transition defined for `NotifyEmergency` during `Recovering` state | New error during recovery has undefined behavior; subsystems may not enter emergency mode | No | 🛠️ Fix now |
| GAP-2-M11-4 | 🟡 P2 | `HealthProbe::RecordRecovery()` integration not verified at compile time | Compile-time mismatch caught at build — low runtime risk | No | 🛠️ Fix now |
| GAP-2-M11-5 | 🔴 P0 | `Backend::stop()` in `FatalError` crashes if `Backend::start()` was never called | Process crash during fatal error handling — worst-case scenario (crash during crash handler) | No | 🛠️ Fix now |
| GAP-2-M11-6 | 🟡 P2 | `uint32_t` epoch wraps in ~49 days at 1000 resets/sec | After wrap, `GetRecoveryEpoch() == 0` again — stale state detection fails | Yes | 🛠️ Fix now |

## Recommended AA Changes
- **GAP-1**: Document: "During Reset(), state is Recovering while callbacks execute. Subsystems must use the epoch parameter, not IsEmergencyMode(), to determine whether to re-arm."
- **GAP-2**: Implement copy-then-release: `Reset()` copies the callback vector under mutex, releases mutex, then iterates the copy
- **GAP-3**: Define `Recovering → Degraded` transition (allow new error to preempt recovery)
- **GAP-4**: Add `static_assert` that `HealthProbe::RecordRecovery()` exists and matches expected signature
- **GAP-5**: Add guard: `if (quill::Backend::is_running()) { Backend::stop(); }` before calling `Backend::stop()`
- **GAP-6**: Change `recovery_epoch_` from `uint32_t` to `uint64_t`
