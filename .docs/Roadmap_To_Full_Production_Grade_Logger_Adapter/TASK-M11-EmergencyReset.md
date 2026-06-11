# M11 — EmergencyManager::Reset()

- **Priority**: 🟡 Medium
- **Est. Effort**: 30 minutes
- **Depends on**: None

---

## Problem

`EmergencyManager::NotifyEmergency()` sets an atomic flag `emergency_mode_ = true`. Once set, `IsEmergencyMode()` returns `true` permanently — there is no way to reset it.

For a trading system with automatic recovery, the emergency subsystem should support returning to normal mode after the fault is resolved (e.g., reconnection succeeded, risk limit recalculated).

---

## Implementation

**File**: `Logger_Adapter/emergency/EmergencyManager.hpp`

Add:

```cpp
static void Reset()
{
    emergency_mode_.store(false, std::memory_order_release);
}

static void SetEmergencyMode(bool enabled)
{
    emergency_mode_.store(enabled, std::memory_order_release);
}
```

Also add a `timestamp` parameter to `Reset()` to record the recovery time, and expose via HealthProbe:

```cpp
// In HealthProbe
static void RecordRecovery()
{
    recovery_timestamp_ms_.store(current_time_ms(), std::memory_order_release);
}
```

---

## Acceptance Criteria

- [ ] After `Reset()`, `IsEmergencyMode()` returns `false`
- [ ] `EmergencyManager::FatalError()` is NOT affected by Reset (it terminates)
- [ ] Build succeeds Debug|x64
