# AA-M12 — Dead Code Cleanup (After-Audit Corrected)

> **Phase**: 0 — 🧹 Cleanup First  
> **Effort**: 15 min  
> **Depends on**: Nothing  
> **v1.x Reference**: TASK-M12-DeadCodeCleanup.md  
> **Audit Issue**: M12-A (verify callers), M12-B (deprecation path)  
> **Validation Correction**: Added `MakeSignalHandlerOptions` as third dead symbol (was missing from original task)

---

## Problem

`LoggingConfig` contains three dead fields:
- `SetupCrashLogger` — no longer used, all crash handling migrated to `EmergencyManager`
- `shutdown_timeout_ms` — no longer referenced, timing handled by Quill backend
- `MakeSignalHandlerOptions` — defined in `CrashHandler.hpp` but not referenced anywhere

These create confusion for new developers and generate dead code paths.

> **Warning — ABI break**: Removing fields from `LoggingConfig` changes its size and layout. If Logger_Adapter is built as a shared library (DLL), any external binary compiled against the old `LoggingConfig` will have wrong field offsets, causing silent memory corruption. A `static_assert(sizeof(LoggingConfig) == expected_size, "ABI break — consumers must recompile")` guard should be added after cleanup, or this must be documented as a breaking change requiring recompilation of all consumers.

---

## Corrected Implementation Plan

### Step 1 — Audit Callers (grep)

```powershell
# Before deleting, verify zero callers
Select-String -Path "Logger_Adapter\**" -Pattern "SetupCrashLogger" -CaseSensitive
Select-String -Path "Logger_Adapter\**" -Pattern "shutdown_timeout_ms" -CaseSensitive
Select-String -Path "Experimental_Console\**" -Pattern "SetupCrashLogger"
Select-String -Path "Experimental_Console\**" -Pattern "shutdown_timeout_ms"
Select-String -Path "Logger_Adapter_Tests\**" -Pattern "SetupCrashLogger"
Select-String -Path "Logger_Adapter_Tests\**" -Pattern "shutdown_timeout_ms"
Select-String -Path "Logger_Adapter\**" -Pattern "MakeSignalHandlerOptions" -CaseSensitive
Select-String -Path "Experimental_Console\**" -Pattern "MakeSignalHandlerOptions"
Select-String -Path "Logger_Adapter_Tests\**" -Pattern "MakeSignalHandlerOptions"
```

If any references exist, document them and update before deletion.

### Step 2 — Remove from LoggingConfig.hpp and CrashHandler.hpp

```cpp
// REMOVE these fields from the LoggingConfig struct:
// bool enable_crash_handler = true;        // was SetupCrashLogger
// uint32_t shutdown_timeout_ms = 5000;     // no longer used
// Remove MakeSignalHandlerOptions from CrashHandler.hpp if file is kept
```

### Step 3 — Remove CrashHandler.hpp if Unreferenced

> **Note**: "Referenced" means its **public symbols** (`MakeSignalHandlerOptions` or `CrashHandler` class) are used somewhere, not "the file is #included." If the file is #included but nothing from it is used, delete the include and the file.

```powershell
Select-String -Path "Logger_Adapter\**" -Pattern "CrashHandler" -CaseSensitive
```

If zero references outside of the file itself, delete `emergency/CrashHandler.hpp`.

### Step 4 — Build & Test

- Build `Logger_Adapter.vcxproj` Debug|x64
- Build `Experimental_Console.vcxproj` Debug|x64
- Ensure zero warnings (excluding MSB4011)

### Step 4b — Add Negative Compilation Guard

Add a dedicated test file `Tests_DeadCodeNegative.cpp` that references the three dead symbols inside `#ifdef NEGATIVE_TEST_DEAD_SYMBOLS` blocks. This file is expected to fail compilation. A future commit that accidentally re-adds any of the symbols will be caught by this test.

```cpp
// Tests_DeadCodeNegative.cpp — EXPECTED TO FAIL COMPILATION
#ifdef NEGATIVE_TEST_DEAD_SYMBOLS
#include "logging/LoggingConfig.hpp"
void test_dead_symbols() {
    LoggingConfig cfg;
    bool x = cfg.SetupCrashLogger;              // Must not compile
    uint32_t y = cfg.shutdown_timeout_ms;        // Must not compile
}
#endif
```

### Step 5 — Mark Old Task as Superseded

Add a note to `TASK-M12-DeadCodeCleanup.md`:
> **Superseded by**: `AA-M12-DeadCodeCleanup.md`

---

## Acceptance Criteria

- [ ] `grep -r "SetupCrashLogger"` returns zero results
- [ ] `grep -r "shutdown_timeout_ms"` returns zero results
- [ ] `grep -r "MakeSignalHandlerOptions"` returns zero results
- [ ] `emergency/CrashHandler.hpp` is either deleted or proven still referenced
- [ ] Build succeeds Debug|x64 with zero new warnings
- [ ] `Experimental_Console.exe` runs without regression
- [ ] Parser ignores unknown config fields: a JSON/YAML config containing `SetupCrashLogger` or `shutdown_timeout_ms` does not cause startup failure

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/logging/LoggingConfig.hpp` | Remove stale fields |
| `Logger_Adapter/emergency/CrashHandler.hpp` | Possibly delete |
| `Logger_Adapter/Logger_Adapter.vcxproj` | Remove if file deleted |
| `Logger_Adapter/Logger_Adapter.vcxproj.filters` | Remove if file deleted |