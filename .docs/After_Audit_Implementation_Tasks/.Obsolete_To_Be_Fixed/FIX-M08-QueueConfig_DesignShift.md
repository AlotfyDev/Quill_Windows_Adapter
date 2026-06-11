# FIX-M08 — Queue Config: Undocumented BackendOptions Design Shift & Missing M08-D

**Severity**: ❌ Fail (architectural contradiction, doc gap)
**AA File**: `AA-M08-QueueConfig.md`
**Phase**: 1 — Foundation
**Effort**: 10 min

---

## Description

Two critical issues:

### Issue 1: Undocumented BackendOptions shift (M08 Architectural Contradiction)

The original **TASK-M08** repeatedly emphasizes that Quill's queue configuration applies to **per-thread SPSC frontend queues** (`FrontendOptions`), which are **compile-time template parameters**. The task explicitly documents this limitation.

`AA-M08-QueueConfig.md` silently wires queue config into **`BackendOptions`** (the central backend queue that collects from all frontend queues). This is:
- A **superior design** — the backend queue is runtime-configurable, so capacity can change without recompiling
- But an **undocumented contradiction** — a developer reading TASK-M08 then AA-M08 will be confused: the AA appears to contradict the task's central insight

### Issue 2: M08-D (Hot-Reload / No Hot-Reload) Missing

The holistic AUDIT required `M08-D`: document that queue configuration cannot be changed at runtime. The AA file does **not** address this. The file needs an explicit section stating: "Queue configuration is applied at startup only. Runtime reconfiguration is not supported in v0.2.0."

### Issue 3 (Secondary): OOM warning invisible in production

The Release-build unbounded queue warning uses `OutputDebugStringA`, which only appears in a debugger. In production, this warning is invisible.

---

## Root Cause

1. **Author treated TASK-M08 as a loose reference** rather than a specification to correct. The shift from FrontendOptions (compile-time) to BackendOptions (runtime) was an intuitive design improvement, but the AA is supposed to *correct* the original task, not silently replace it.
2. **M08-D was deprioritized** because "startup-only" seems obvious. But the design intent was to document the limitation explicitly.
3. **`OutputDebugStringA` is the default go-to** for pre-initialization warnings because the logger isn't ready. The production visibility requirement was overlooked.

---

## Exact Fix

### Fix 1: Add Design Rationale Section to AA-M08

Insert a new section after the existing "Corrected Implementation Plan" header or before Acceptance Criteria:

```markdown
### Design Note: BackendOptions vs FrontendOptions

**Q: Why does this AA wire into BackendOptions when TASK-M08 discusses FrontendOptions?**

A deliberate architectural shift. Quill has two queue layers:

| Layer | Type | Configurable | Scope |
|-------|------|--------------|-------|
| **FrontendOptions** | Per-thread SPSC | Compile-time template param | One per caller thread |
| **BackendOptions** | Central MPMC collector | Runtime via struct | Single backend thread |

TASK-M08 correctly notes that `FrontendOptions` (SPSC queue types) are compile-time. However, the **operationally useful** configuration is the **backend queue** — controlling its capacity prevents the central collector from unbounded growth under load spikes.

**Resolution**: AA-M08 targets `BackendOptions` (the backend collector queue), not `FrontendOptions` (per-thread SPSC). This is documented here to resolve the apparent contradiction with TASK-M08. The original task's compile-time limitation note still applies to frontend SPSC queues — they remain compile-time in v0.2.0.

**Future work**: If frontend queue configuration becomes needed (e.g., different SPSC capacities per thread), revisit FrontendOptions. Not needed for v0.2.0.
```

### Fix 2: Add M08-D Hot-Reload Documentation

Insert a new subsection after the wire-up code:

```markdown
### Step 3b — Document: Queue Config is Startup-Only

Queue configuration is **applied once during `InitializeLogging()`** and **cannot be changed at runtime** in v0.2.0.

- Capacity changes require restart
- Queue type (Bounded ↔ Unbounded) changes require restart
- Reason: Quill does not support hot-reconfig of the backend queue after `quill::start()` has been called

If runtime queue reconfiguration becomes a requirement (e.g., adaptive capacity scaling), it would require:
1. Stopping the backend thread
2. Draining remaining in-flight messages
3. Creating a new backend queue with updated config
4. Starting the backend thread on the new queue

This is not planned for v0.2.0. If it becomes critical, file a new task.
```

### Fix 3: Replace OutputDebugStringA with stderr

In `LoggerSetup.hpp` (Step 2), replace:

```cpp
OutputDebugStringA("WARNING: Unbounded queue selected in Release build — OOM risk!\n");
```

With:

```cpp
fprintf(stderr, "WARNING [Logger_Adapter]: Unbounded queue selected in Release build — OOM risk!\n");
```

And add `#include <cstdio>` to the file if not already present.

---

## Impact if NOT Fixed

- **Developer confusion**: Contradiction between TASK-M08 and AA-M08 creates trust erosion in the documentation. A future developer might revert to FrontendOptions, breaking the design.
- **False assumptions about runtime reconfig**: Without M08-D documentation, a developer in v0.3.0+ might assume queue config can be hot-reloaded and introduce subtle bugs when it doesn't take effect.
- **Silent OOM in production**: The unbounded queue warning goes to `OutputDebugStringA`, which is ignored in production. A producer-overrun scenario could go undetected until memory exhaustion.

---

## Verification

1. Read AA-M08 side-by-side with TASK-M08: the contradiction should be explicitly resolved in the new Design Note
2. Verify M08-D text exists and clearly states "startup-only, no hot-reload in v0.2.0"
3. Confirm `fprintf(stderr, ...)` compiles: MSVC with `/std:c++17` and `<cstdio>` — no warnings
4. Run Release build with `QueueType::Unbounded` and confirm warning appears on stderr (not just debug output)
5. Confirm `#include <cstdio>` is present in `LoggerSetup.hpp`
