# M08 — Queue Type & Capacity Configuration

- **Priority**: 🟡 Medium
- **Est. Effort**: 1 hour
- **Depends on**: None

---

## Problem

Quill's `FrontendOptions` struct controls the per-thread SPSC queue:

```cpp
struct FrontendOptions {
    static constexpr QueueType queue_type = QueueType::UnboundedBlocking;
    static constexpr size_t initial_queue_capacity = 128u * 1024u; // 128 KiB
    static constexpr size_t unbounded_queue_max_capacity = 2ull * 1024u * 1024u * 1024u; // 2 GiB
    static constexpr uint32_t blocking_queue_retry_interval_ns = 800;
};
```

All of these are **compile-time constants** in Quill. Logger_Adapter has no mechanism to configure or override them.

For a trading system:
- **MarketData thread**: High volume → larger initial queue, unbounded with dropping policy
- **OrderExecution thread**: Low volume, low latency → bounded blocking to prevent memory growth
- **Backend options**: Overflow policy, notification on drop

---

## Implementation

### Step 1 — Add QueueConfig to LoggingConfig

**File**: `Logger_Adapter/logging/LoggingConfig.hpp`

```cpp
enum class QueueOverflowPolicy {
    Blocking,   // Wait for space
    Dropping    // Drop new messages
};

struct QueueConfig {
    QueueType type = QueueType::UnboundedBlocking;
    size_t initial_capacity = 128 * 1024; // 128 KiB
    size_t max_capacity = 2ULL * 1024 * 1024 * 1024; // 2 GiB (unbounded only)
    uint32_t retry_interval_ns = 800;
};
```

### Step 2 — Document requirement in INDEX

Quill's `FrontendOptions` are compile-time template parameters, not runtime-settable. To change them requires either:
- A custom `FrontendOptions` specialization (if using Quill's template API)
- Or recompilation with different macros

Document this limitation and provide build-configuration presets:

```xml
<!-- Debug: unbounded, larger capacity for development -->
<!-- Release: bounded, moderate capacity for production -->
```

For truly runtime queue config, we would need to customize Quill's `FrontendOptions` template — evaluate feasibility during implementation.

---

## Acceptance Criteria

- [ ] Queue parameters are documented in `LoggingConfig.hpp`
- [ ] Build-config presets for Debug vs Release queue behavior
- [ ] Build succeeds Debug|x64
