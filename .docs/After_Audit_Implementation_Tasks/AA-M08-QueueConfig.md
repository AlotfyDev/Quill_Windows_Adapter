# AA-M08 — Queue Configuration (After-Audit Corrected)

> **Phase**: 1 — ⚙️ Foundation  
> **Effort**: 1 h  
> **Depends on**: AA-0.5 (stub `config/QueueConfig.hpp` exists)  
> **v1.x Reference**: TASK-M08-QueueConfig.md  
> **Audit Issues**: M08-A (stub file), M08-B (unbounded OOM risk), M08-C (sizing guidance), M08-D (hot-reload gap)

---

## Problem

Quill supports configuring the backend queue type (`BoundedBlockingQueue` or `UnboundedBlockingQueue`) and capacity, but Logger_Adapter exposes none of this. The stub `config/QueueConfig.hpp` exists but is empty.

---

## Corrected Implementation Plan

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

### Step 1 — Populate `config/QueueConfig.hpp`

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace Logger_Adapter::config {

enum class QueueType : uint8_t {
    Bounded,    // Fixed-size, blocks producer when full (recommended for production)
    Unbounded   // Grows dynamically, OOM risk if producer outruns consumer
};

struct QueueConfig {
    QueueType type = QueueType::Bounded;

    // Capacity in log messages (not bytes).
    // Minimum valid value: 1024 (values below 1024 are clamped at runtime).
    // Trading systems: 8192 (default)
    // Batch processing: 65536
    // Real-time with strict latency: 4096
    // NOTE: capacity is ignored when type == Unbounded (the queue grows dynamically).
    //   If capacity differs from default with Unbounded, InitializeLogging() warns.
    size_t capacity = 8192;

    // If true, warn at startup if unbounded is selected for Release build.
    bool warn_on_unbounded_release = true;
};

} // namespace Logger_Adapter::config
```

### Step 2 — Wire into `LoggerSetup.hpp` InitializeLogging

```cpp
#include <cstdio>
#include "../config/QueueConfig.hpp"

inline bool InitializeLogging(LoggingConfig const& config) {
    // ... existing code ...
    
    // After creating BackendOptions:
    auto queue_config = config.queue;  // add QueueConfig to LoggingConfig
    if (queue_config.type == QueueType::Bounded) {
        backend_opts.queue_type = quill::QueueType::BoundedBlocking;
    } else {
        backend_opts.queue_type = quill::QueueType::UnboundedBlocking;
    }
    backend_opts.bounded_blocking_queue_capacity = queue_config.capacity;
    
    // Warn if capacity is set but unused (Unbounded mode ignores capacity)
    if (queue_config.type == QueueType::Unbounded && queue_config.capacity != 8192) {
        fprintf(stderr, "INFO [Logger_Adapter]: capacity=%zu ignored for Unbounded queue (Unbounded grows dynamically)\n",
                queue_config.capacity);
    }

    // Clamp capacity to minimum valid value
    constexpr size_t MIN_QUEUE_CAPACITY = 1024;
    if (queue_config.capacity < MIN_QUEUE_CAPACITY) {
        fprintf(stderr, "WARNING [Logger_Adapter]: capacity=%zu is below minimum %zu, clamping to %zu\n",
                queue_config.capacity, MIN_QUEUE_CAPACITY, MIN_QUEUE_CAPACITY);
        queue_config.capacity = MIN_QUEUE_CAPACITY;
    }

    // Release-build warning for unbounded:
    #ifndef NDEBUG  // standard C++ macro (not MSVC-specific _DEBUG)
    // Intentionally empty — Debug build skips the warning
    #else
    if (queue_config.type == QueueType::Unbounded && queue_config.warn_on_unbounded_release) {
        // Log via stderr since logger isn't initialized yet
        fprintf(stderr, "WARNING [Logger_Adapter]: Unbounded queue selected in Release build — OOM risk!\n");
    }
    #endif
    
    // ... rest of InitializeLogging ...
}
```

### Step 3 — Add `queue` field to `LoggingConfig`

```cpp
// In LoggingConfig.hpp
#include "../config/QueueConfig.hpp"

struct LoggingConfig {
    // ... existing fields ...
    config::QueueConfig queue;  // NEW
};

### Step 3b — Document: Queue Config is Startup-Only

> **WARNING (ordering contract)**: QueueConfig MUST be set on `BackendOptions` before `quill::Backend::start()`. If set after `start()`, the configuration is silently ignored and Quill defaults (Bounded/8192) apply. See code comment at the wire-up site for the ordering requirement.

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

### Test Isolation Note

Queue configuration is **global Quill backend state** — once `quill::Backend::start()` is called with a queue config, that configuration is baked into the backend thread for the lifetime of the process. There is no `reset` or `re-init` path.

**Consequence for testing**: Each test process can exercise at most one queue configuration. Tests for different queue configurations (Bounded vs Unbounded, different capacities) MUST run in separate processes. Sharing a process requires subprocess orchestration (e.g., `std::system()` or dedicated test launcher), which makes CI more expensive.

**Recommendation for test authors**:
- Use a parameterized `RUN_QUEUE_CONFIG_TEST(bounded | unbounded, capacity, test_logic)` macro that launches a subprocess per configuration.
- Keep capacity tests minimal (2-3 configurations) to bound CI runtime.
- Document per-process test overhead in the test project README.

---

## Acceptance Criteria

- [ ] `config/QueueConfig.hpp` populated with `QueueType` enum + `QueueConfig` struct
- [ ] Default is `Bounded` with capacity 8192
- [ ] Release build warns if unbounded is selected
- [ ] Queue config is wired into `InitializeLogging`
- [ ] `capacity < MIN_QUEUE_CAPACITY (1024)` is clamped gracefully — no crash or deadlock
- [ ] Build succeeds Debug|x64 with zero new warnings
- [ ] Backward compatible: existing code that doesn't set `queue` gets Bounded/8192

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/config/QueueConfig.hpp` | Populate from stub |
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add `config::QueueConfig queue` field |
| `Logger_Adapter/logging/LoggerSetup.hpp` | Wire queue config into BackendOptions |