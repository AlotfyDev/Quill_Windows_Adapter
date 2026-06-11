# P02 — Windows Thread Affinity (cpu_affinity Replacement)

- **Priority**: 🟡 Medium
- **Est. Effort**: 30 minutes
- **Depends on**: None

---

## Problem

Quill's `BackendOptions::cpu_affinity` uses POSIX `pthread_setaffinity_np`. On Windows, the stored value is ignored and never applied — Quill does not call `SetThreadAffinityMask` on Windows.

For low-latency trading, pinning the backend thread to a dedicated CPU core prevents cache thrashing and reduces tail latency.

---

## Implementation

### Step 1 — Add Windows-native affinity config

**File**: `Logger_Adapter/windows/ThreadAffinity.hpp`

```cpp
#pragma once
#include <windows.h>
#include <cstdint>

namespace Logger_Adapter::windows {

/// Set thread affinity mask for the current thread.
/// mask: bitmask where bit N = CPU core N (e.g., 0x04 = CPU core 2)
inline bool SetCurrentThreadAffinity(uintptr_t cpu_mask)
{
    auto* handle = GetCurrentThread();
    DWORD_PTR previous = SetThreadAffinityMask(handle, static_cast<DWORD_PTR>(cpu_mask));
    return previous != 0;
}

/// Set thread affinity by core number.
inline bool PinCurrentThreadToCore(uint16_t core_index)
{
    if (core_index >= sizeof(uintptr_t) * 8) return false;
    return SetCurrentThreadAffinity(static_cast<uintptr_t>(1) << core_index);
}

} // namespace Logger_Adapter::windows
```

### Step 2 — Add to LoggingConfig

```cpp
struct LoggingConfig {
    // ...
    uint16_t backend_cpu_affinity = (std::numeric_limits<uint16_t>::max)();
    // std::numeric_limits<uint16_t>::max() = "do not set"
};
```

### Step 3 — Apply after Backend::start()

**File**: `Logger_Adapter/logging/LoggerSetup.hpp`

```cpp
quill::Backend::start(backend_opts);

if (config.backend_cpu_affinity != (std::numeric_limits<uint16_t>::max)()) {
    windows::PinCurrentThreadToCore(config.backend_cpu_affinity);
}
```

---

## Acceptance Criteria

- [ ] Setting `backend_cpu_affinity = 2` pins backend thread to CPU core 2
- [ ] Default value (`max()`) does not change affinity
- [ ] Build succeeds Debug|x64
