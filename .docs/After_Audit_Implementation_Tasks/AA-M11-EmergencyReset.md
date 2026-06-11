# AA-M11 — Emergency Reset (After-Audit Corrected — Complete Redesign)

> **Phase**: 2 — 🔧 Redesign  
> **Effort**: 2h design + 1.5h implementation = 3.5h total  
> **Depends on**: Nothing (operates on existing `EmergencyManager.hpp`)  
> **v1.x Reference**: TASK-M11-EmergencyReset.md  
> **Audit Issues**: M11-A (Reset alone dangerous), M11-B (no state machine), M11-C (HealthProbe asymmetry), M11-D (race condition)  
> **Audit Verdict**: ❌ Fail — Requires Complete Redesign

---

## Problem

`EmergencyManager::NotifyEmergency()` sets `emergency_mode_ = true` permanently. There is no way to reset it. The original TASK-M11 proposed simply adding `emergency_mode_.store(false)` — but this is **dangerous** because:
1. Subsystems check the flag and may skip recovery actions based on a stale `true`
2. HealthProbe has `RecordEmergency()` but no `RecordRecovery()` — asymmetry
3. Backtraces are not flushed on recovery
4. No coordination with paused subsystems

---

## Corrected Design

### 1. State Machine

Replace the binary `std::atomic<bool>` with a proper state machine:

```cpp
enum class EmergencyState : uint8_t {
    Normal    = 0,  // healthy operation
    Degraded  = 1,  // error occurred, limited operation
    Recovering = 2, // automated recovery in progress
    Fatal     = 3   // unrecoverable — process must terminate
};
```

**State Transitions:**
```
Normal ──(error)──▶ Degraded ──(Reset called)──▶ Recovering ──(success)──▶ Normal
                      │  ▲                           │
                      │  │                           │
                      │  └──(new error)──────────────┘
                      │                              │
                      └──(fatal error)──▶ Fatal ────▶ std::abort() / std::exit()
```

> **Recovery → Degraded transition**: If `NotifyEmergency()` is called while in `Recovering` state (a new error occurs during callback execution), the state transitions back to `Degraded`. The ongoing recovery is preempted, and a subsequent `Reset()` is required. This prevents new errors from being silently ignored during recovery.

### 2. Recovery Epoch

Each transition out of Degraded increments an epoch counter. Subsystems can compare their cached epoch against the current to detect stale state:

```cpp
// Using uint64_t to prevent wrap-around: at 1000 resets/second,
// uint32_t wraps in ~49 days. uint64_t is effectively infinite.
static std::atomic<uint64_t> recovery_epoch_{0};

uint64_t GetRecoveryEpoch() const noexcept {
    return recovery_epoch_.load(std::memory_order_acquire);
}
```

### 3. Symmetric HealthProbe

Add `HealthProbe::RecordRecovery()` to match existing `RecordEmergency()`:

```cpp
static void RecordRecovery() noexcept {
    recovery_count_.fetch_add(1, std::memory_order_release);
    // clear emergency timestamp, track total recovery count
}
```

> **Recovering state semantics**: During `Reset()`, `GetState()` returns `Recovering` while callbacks execute. Subsystems MUST use the epoch parameter passed to their callback — NOT `IsEmergencyMode()` — to determine whether to re-arm. After all callbacks return, state transitions to `Normal`. This ensures consistent subsystem view even if a callback waits or takes significant time.

### 4. Reset() with Coordination

```cpp
static void Reset() {
    // 1. Flush backtraces before transitioning
    auto* logger = quill::Frontend::get_logger(config_.emergency_logger_name);
    if (logger) logger->flush_backtrace();
    
    // 2. Transition to Recovering
    emergency_state_.store(EmergencyState::Recovering, std::memory_order_release);
    recovery_epoch_.fetch_add(1, std::memory_order_acq_rel);
    
    // 3. Notify subsystems via registered callbacks
    // Copy vector under lock, release lock before iterating to prevent
    // deadlock if a callback calls RegisterRecoveryCallback().
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        auto callbacks_copy = recovery_callbacks_;
    }
    for (auto& cb : callbacks_copy) {
        cb(recovery_epoch_.load(std::memory_order_relaxed));
    }
    
    // 4. Log recovery event
    LOG_INFO(logger, "Emergency mode reset, entering recovery (epoch={})",
             recovery_epoch_.load(std::memory_order_relaxed));
    
    // 5. Transition to Normal (subsystems must re-check)
    emergency_state_.store(EmergencyState::Normal, std::memory_order_release);
    HealthProbe::RecordRecovery();
}
```

### 5. Subsystem Recovery Callbacks

```cpp
// Registered during Initialize
using RecoveryCallback = void(*)(uint64_t epoch);

void RegisterRecoveryCallback(RecoveryCallback cb) {
    recovery_callbacks_.push_back(cb);
}

// Example subscriber:
void OnEmergencyRecovery(uint64_t epoch) {
    // Re-arm safety mechanisms, re-enable trading, etc.
}
```

---

## Corrected Implementation Plan

### Step 1 — Replace `emergency_mode_` with State Machine

In `EmergencyManager.hpp`:

```cpp
class EmergencyManager {
public:
    enum class State : uint8_t { Normal = 0, Degraded = 1, Recovering = 2, Fatal = 3 };

    static void Reset();
    static State GetState() noexcept;
    static uint64_t GetRecoveryEpoch() noexcept;
    static bool IsEmergencyMode() noexcept;  // returns true if NOT Normal
    static void RegisterRecoveryCallback(RecoveryCallback cb);

private:
    static std::atomic<State> state_;
    static std::atomic<uint64_t> recovery_epoch_;
    static std::vector<RecoveryCallback> recovery_callbacks_;
    static std::mutex callback_mutex_;
};
```

### Step 2 — Update NotifyEmergency and FatalError

- `NotifyEmergency()`: transition from Normal to Degraded (not from Fatal)
- `FatalError()`: transition to Fatal, then terminate. **Guard**: Before calling `Backend::stop()`, check `quill::Backend::is_running()` to avoid crash if `Backend::start()` was never called (e.g., fatal error during initialization).

### Step 3 — Add RecordRecovery to HealthProbe

```cpp
// In HealthProbe.hpp
static void RecordRecovery() noexcept;
static uint32_t GetRecoveryCount() noexcept;

// New statics:
static std::atomic<uint32_t> recovery_count_{0};
```

> **Compile-time integration check**: Add `static_assert(std::is_invocable_v<decltype(&HealthProbe::RecordRecovery)>)` in `EmergencyManager.cpp` to verify the HealthProbe API contract at build time.

### Step 4 — Wire into Experimental_Console

```cpp
EmergencyManager::RegisterRecoveryCallback([](uint64_t epoch) {
    LOG_INFO(root_log, "Recovery epoch {} — subsystems re-arming", epoch);
});
```

---

## Acceptance Criteria

- [ ] `EmergencyManager::GetState()` returns `Normal` initially
- [ ] `NotifyEmergency()` transitions to `Degraded`
- [ ] `IsEmergencyMode()` returns `true` in `Degraded`, `Recovering`, `Fatal`
- [ ] `Reset()` transitions `Degraded → Recovering → Normal`
- [ ] `Reset()` increments recovery epoch
- [ ] `FatalError()` transitions to `Fatal` and terminates
- [ ] Recovery callbacks are invoked on `Reset()`
- [ ] HealthProbe tracks recovery count + timestamp
- [ ] Backtraces are flushed during `Reset()`
- [ ] Backward compatible: existing consumers of `IsEmergencyMode()` still work
- [ ] Build succeeds Debug|x64 with zero new warnings

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/emergency/EmergencyManager.hpp` | Replace binary flag with state machine |
| `Logger_Adapter/emergency/HealthProbe.hpp` | Add `RecordRecovery()` + `GetRecoveryCount()` |
| `Experimental_Console/Experimental_Console.cpp` | Wire recovery callbacks |