# Test Design: Dead Code Cleanup

## Under Spec
- AA File: `AA-M12-DeadCodeCleanup.md`
- Phase: 0
- Key Requirements:
  - Three dead symbols removed: `SetupCrashLogger`, `shutdown_timeout_ms`, `MakeSignalHandlerOptions`
  - grep verification: zero references to any of the three symbols in the entire codebase
  - Negative compilation guard: `Tests_DeadCodeNegative.cpp` references dead symbols inside `#ifdef NEGATIVE_TEST_DEAD_SYMBOLS` — expected to fail compilation
  - ABI guard: `static_assert(sizeof(LoggingConfig) == expected_size, "ABI break — consumers must recompile")` after cleanup
  - Build succeeds Debug|x64 with zero new warnings
  - `Experimental_Console.exe` runs without regression
  - `CrashHandler.hpp` either deleted or proven still referenced (referenced = public symbols used, not just #included)
  - Parser ignores unknown config fields: JSON/YAML with stale fields does not cause startup failure

## Test Harness
- **Fixture**: No runtime fixture needed — this is a compile-time + build-system verification.
  - For compilation tests: a minimal `.cpp` file that tries to reference the dead symbols (should fail to compile)
  - For grep verification: PowerShell `Select-String` across the entire source tree
  - For regression: existing test suite must still pass (no behavioral change from dead code removal)
- **Mocked vs Real**: Real codebase. No mocking possible — dead code is either present (compiling) or absent (not compiling).
- **Preconditions**: 
  - `Test-Compile-DeadSymbols.cpp` — a dedicated test file that `#include`s `LoggingConfig.hpp` and attempts to access `cfg.SetupCrashLogger`, `cfg.shutdown_timeout_ms`, and references `MakeSignalHandlerOptions`. This test is expected to FAIL compilation. It serves as a negative compilation guard.
  - A positive compilation guard: verify the new code compiles without these symbols (the entire build must succeed).

## Scenarios

### Positive Cases
- Build Logger_Adapter.vcxproj Debug|x64 — succeeds with zero errors, zero new warnings (MSB4011 ignored as per spec)
- Build Experimental_Console.vcxproj Debug|x64 — succeeds with zero errors
- Build Logger_Adapter_Tests.vcxproj Debug|x64 — succeeds with zero errors
- Experimental_Console.exe runs to completion without crash — log output is written, no missing-config-field errors
- Existing test suite (Tests_Emergency, Tests_ErrorHandling, Tests_LoggingInit) passes — no regression from field removal

### Negative / Error Cases (compilation negatives)
- Code referencing `SetupCrashLogger` fails to compile with error "class has no member 'SetupCrashLogger'" (or equivalent)
- Code referencing `shutdown_timeout_ms` fails to compile with error "class has no member 'shutdown_timeout_ms'"
- Code referencing `MakeSignalHandlerOptions` fails to compile with error "identifier not found"

### Production Realities
- Third-party code that depends on Logger_Adapter (not in this repo) may still reference the dead symbols. If the library is a DLL, the removed fields change the ABI of `LoggingConfig`. External consumers must recompile. This is a breaking change — document in release notes.
- If `CrashHandler.hpp` is deleted but an external consumer includes it, their build breaks. The AA spec addresses this only for in-tree code.
- Configuration files (JSON/YAML) that specify `SetupCrashLogger` or `shutdown_timeout_ms` should be updated or the parsing code should silently ignore unknown fields. Test: parse a config with unknown fields — must not crash.
- ABI compatibility: `static_assert(sizeof(LoggingConfig) == expected_size)` must be added after cleanup. Test: verify the static_assert is present and compiles.

### Thread Safety
- Dead code removal has no thread safety implications — it's a build-time change only.
- If `CrashHandler.hpp` contained thread-local or atomic state, its removal may reduce static initializer overhead, but this is a positive side effect, not a correctness concern.

## Assertions
- `Select-String -Pattern "SetupCrashLogger" -Path "Logger_Adapter"` returns zero matches
- `Select-String -Pattern "shutdown_timeout_ms" -Path "Logger_Adapter"` returns zero matches
- `Select-String -Pattern "MakeSignalHandlerOptions" -Path "Logger_Adapter"` returns zero matches
- `Select-String -Pattern "SetupCrashLogger" -Path "Experimental_Console"` returns zero matches
- `Select-String -Pattern "shutdown_timeout_ms" -Path "Experimental_Console"` returns zero matches
- `Select-String -Pattern "MakeSignalHandlerOptions" -Path "Experimental_Console"` returns zero matches
- Compilation of a negative test file that references dead symbols **fails** (ExitCode != 0)
- Compilation of the full solution **succeeds** (ExitCode == 0)
- `LoggingConfig` struct size decreased by at least `sizeof(bool) + sizeof(uint32_t)` (verifies fields are actually removed, not just renamed)
- `Experimental_Console.exe` exit code is 0
- Zero new compiler warnings (excluding MSB4011 for VS project upgrade)
- `static_assert(sizeof(LoggingConfig) == expected_size, ...)` is present in the codebase after cleanup
- Parser loading a config with `SetupCrashLogger: true` and `shutdown_timeout_ms: 5000` succeeds without error (unknown fields ignored)

## Failure Mode
- A test failure where a dead symbol is still grep-able: **dead code left in production** — confusion for new developers, potential use in new code
- A test failure where dead symbol still compiles: **compilation succeeded when it should have failed** — the removal didn't take effect (e.g., a different branch or stale build artifact)
- A test failure where build breaks due to removal: **incomplete audit** — a caller of the dead symbol was missed and must be updated before removal
- A test failure where Experimental_Console crashes: **regression in initialization** — removed fields may have been read in a default config path, causing null pointer or uninitialized memory access
- A test failure where test suite regresses: **behavioral change from cleanup** — a dead symbol was actually in use by the test suite (contradicts the audit)

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Added `MakeSignalHandlerOptions` as third dead symbol (was missing) | § Validation Correction (header) | Updated Key Requirements — three symbols |
| Added negative compilation guard `Tests_DeadCodeNegative.cpp` | § Step 4b | Added scenario for negative compilation test; resolved GAP-0-M12-1 |
| Added ABI `static_assert` guard after cleanup | § Warning box (ABI break) | Added scenario for ABI guard assertion; resolved GAP-0-M12-2 |
| Clarified "referenced" means public symbols used, not just #included | § Step 3 note | Resolved GAP-0-M12-3 |
| Added parser compatibility for unknown config fields | § Acceptance Criteria | Added assertion for parser unknown-field handling; resolved GAP-0-M12-4 |

## Spec Gap Notes (SGN)

### Resolved Gaps

These GAPs were raised during test design and subsequently resolved by Impact Analysis fixes applied to the AA spec.

| Gap ID | Issue | Resolution | AA Spec Section |
|--------|-------|------------|-----------------|
| GAP-0-M12-1 | No negative compilation guard for dead symbols | ✅ RESOLVED — AA Step 4b adds `Tests_DeadCodeNegative.cpp` with `#ifdef NEGATIVE_TEST_DEAD_SYMBOLS` guards. | § Step 4b |
| GAP-0-M12-2 | ABI compatibility not addressed | ✅ RESOLVED — AA adds `static_assert(sizeof(LoggingConfig) == expected_size)` guard and documents the breaking change. | § Warning box |
| GAP-0-M12-3 | "Referenced" definition vague for CrashHandler.hpp | ✅ RESOLVED — AA Step 3 explicitly defines "referenced" as public symbols used, not file #included. | § Step 3 |
| GAP-0-M12-4 | No config file compatibility test | ✅ RESOLVED — AA Acceptance Criteria includes "Parser ignores unknown config fields." | § Acceptance Criteria |
