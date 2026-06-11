# AA-P02 — Windows Thread Affinity (After-Audit Corrected)

> **Phase**: 6 — 🪟 Platform  
> **Effort**: 30 min  
> **Depends on**: AA-C01 (backend thread initialization), AA-C05 (Thread Model — thread ownership topology)
> **v1.x Reference**: TASK-P02-ThreadAffinity.md  
> **Audit Issues**: P02-A (stub file), P02-B (NUMA awareness), P02-C (frontend threads)
> **Fix Applied**: FIX-P02-ThreadAffinity_BackendThreadBug.md — corrected thread targeting (main→backend via pre-start affinity inheritance)

---

## Problem

Quill's `cpu_affinity` is no-op on Windows. Logger_Adapter needs a Windows-native replacement using `SetThreadAffinityMask` or `SetThreadGroupAffinity` for NUMA awareness.

---

## Corrected Implementation Plan

### Step 1 — Populate `windows/ThreadAffinity.hpp` (was stub)

```cpp
#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

namespace Logger_Adapter::windows {

struct ThreadAffinityConfig {
    // Processor mask (bit 0 = processor 0).
    // For NUMA systems, use group_affinity instead.
    DWORD_PTR processor_mask = 0;
    
    // NUMA group number (0-based). Only used if processor_mask is non-zero.
    WORD group = 0;
    
    // If true, use SetThreadGroupAffinity for NUMA support.
    // Requires Windows 7+.
    // ⚠️ On systems with >64 logical processors, use_group_affinity MUST be true.
    //   SetThreadAffinityMask only sets affinity within group 0, which may not
    //   be the optimal group for the backend. A runtime check should verify
    //   that bits beyond bit 63 in processor_mask force group affinity.
    bool use_group_affinity = false;
};

// Apply affinity to the calling thread.
// Returns true on success, false on failure (logs via OutputDebugString).
bool SetThreadAffinity(const ThreadAffinityConfig& config) noexcept;

// Convenience: pin to single processor.
bool PinToProcessor(uint16_t processor_id, WORD group = 0) noexcept;

} // namespace Logger_Adapter::windows
```

### Step 2 — Implement SetThreadAffinity (for calling thread save/restore)

```cpp
inline bool SetThreadAffinity(const ThreadAffinityConfig& config) noexcept {
    HANDLE hThread = GetCurrentThread();
    
    if (config.use_group_affinity) {
        GROUP_AFFINITY ga;
        ga.Mask = config.processor_mask;
        ga.Group = config.group;
        ga.Reserved[0] = 0;
        ga.Reserved[1] = 0;
        ga.Reserved[2] = 0;
        
        if (!SetThreadGroupAffinity(hThread, &ga, nullptr)) {
            OutputDebugStringA("SetThreadGroupAffinity failed\n");
            return false;
        }
        return true;
    }
    
    if (config.processor_mask != 0) {
        if (!SetThreadAffinityMask(hThread, config.processor_mask)) {
            OutputDebugStringA("SetThreadAffinityMask failed\n");
            return false;
        }
    }
    return true;
}
```

### Step 3 — Wire into LoggerSetup for Backend Thread (FIXED: pre-start approach)

On Windows, child threads inherit the parent thread's affinity mask. We set the calling thread's affinity BEFORE `Backend::start()` so the backend thread inherits it, then restore the calling thread's original affinity.

```cpp
// In InitializeLogging():
void InitializeLogging(const LoggingConfig& config) {
    DWORD_PTR original_mask = 0;
    bool need_restore = false;

    if (config.thread_affinity.processor_mask != 0) {
        // Save current affinity, then set desired mask on calling thread.
        // SetThreadAffinityMask returns 0 on failure OR if the previous mask
        // was 0 (any processor). Use GetLastError() to disambiguate.
        original_mask = SetThreadAffinityMask(GetCurrentThread(),
            config.thread_affinity.processor_mask);
        if (original_mask != 0 || GetLastError() == ERROR_SUCCESS) {
            // Success: original_mask holds previous mask (0 means "any processor")
            need_restore = true;
        }
        // else: failure — can't determine original mask, skip restore
    }

    // Start Quill AFTER setting affinity — backend thread inherits calling thread's mask
    quill::Backend::start(config.backend_options);

    // Restore calling thread's original affinity (backend thread keeps the pinned mask)
    if (need_restore) {
        SetThreadAffinityMask(GetCurrentThread(), original_mask);
    }
}
```

> **Thread model reference (AA-C05)**: `GetCurrentThread()` after `Backend::start()` returns the calling thread, NOT the Quill backend worker thread. The backend thread is already spawned by `Backend::start()`. Setting affinity after start would pin the calling thread, not the backend. The pre-start approach ensures the backend inherits affinity via Windows' `CreateThread` inheritance behavior.

> **Design Tradeoff**: Runtime affinity change (`SetBackendThreadAffinity`) is intentionally NOT implemented.
>   - Adding it would require a callback mechanism on the backend thread (via Quill's `try_callback` or similar).
>   - The use case (incident response / ops rebalancing) is rare enough that process restart with updated config
>     is the recommended workaround.
>   - If this becomes a requirement, implement as: post a callback that calls `SetThreadAffinity` on the
>     backend thread, using the existing `ThreadAffinityConfig` structure.

### Step 4 — Add Thread Affinity Config to LoggingConfig

```cpp
#include "../windows/ThreadAffinity.hpp"

struct LoggingConfig {
    // ... existing fields ...
    windows::ThreadAffinityConfig thread_affinity;  // NEW: backend thread affinity
};
```

---

## Acceptance Criteria

- [ ] Affinity set on calling thread BEFORE `Backend::start()` — backend thread inherits via Windows CreateThread inheritance
- [ ] `SetThreadAffinity` works on Windows 10+ x64
- [ ] Group affinity used when `use_group_affinity = true` (NUMA support)
- [ ] Backend thread runs on specified processor (verify via `GetCurrentProcessorNumber()` on backend thread)
- [ ] Calling thread restored to original affinity after backend start
- [ ] References AA-C05 (Thread Model) for thread ownership topology
- [ ] Build succeeds Debug|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/windows/ThreadAffinity.hpp` | Populate from stub |
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add `ThreadAffinityConfig` field |
| `Logger_Adapter/logging/LoggerSetup.hpp` | Wire affinity to backend thread |