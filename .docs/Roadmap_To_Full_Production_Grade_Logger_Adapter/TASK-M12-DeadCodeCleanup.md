# M12 — Dead Code Cleanup

- **Priority**: 🟡 Medium
- **Est. Effort**: 15 minutes
- **Depends on**: None

---

## Problem

Two functions and one config field exist but are never used:

| Declared At | Symbol | Status |
|---|---|---|
| `emergency/CrashHandler.hpp:10` | `MakeSignalHandlerOptions(EmergencyConfig const&)` | Defined, never called |
| `emergency/CrashHandler.hpp:18` | `SetupCrashLogger(EmergencyConfig const&)` | Defined, never called |
| `emergency/EmergencyConfig.hpp:13` | `shutdown_timeout_ms` | Declared, never read |

This confuses maintainers and increases code surface for no benefit.

---

## Implementation

### Option A — Wire them up (preferred)

1. Connect `MakeSignalHandlerOptions` → `CrashHandler.hpp` into `LoggerSetup.hpp`'s `InitializeLogging`, passing to `quill::Backend::start(backend_opts, signal_handler_opts)` overload
2. Connect `shutdown_timeout_ms` → `LoggerSetup::ShutdownLogging` or `GracefulShutdown::Execute` as a timeout guard

### Option B — Remove them (if wiring is not feasible)

1. Delete `ShutdownCallback` or move to `EmergencyConfig` entirely
2. Remove `shutdown_timeout_ms` from `EmergencyConfig`

---

## Acceptance Criteria

- [ ] No undefined-but-unused functions remain
- [ ] No declared-but-unread config fields remain
- [ ] Build succeeds Debug|x64
