# M07 — Backend error_notifier Callback

- **Priority**: 🟡 Medium
- **Est. Effort**: 30 minutes
- **Depends on**: None

---

## Problem

Quill's `BackendOptions::error_notifier` is called when the backend encounters errors (e.g., unbounded queue reallocation, bounded queue full, etc.). Currently, the default implementation just prints to `stderr`:

```cpp
std::fprintf(stderr, "%s\n", error_message.data());
```

For a trading system, we need to:
1. Route backend errors through the Logger_Adapter emergency subsystem
2. Notify the `HealthProbe` that the backend had errors
3. Optionally trigger `EmergencyManager` if the error is critical (e.g., queue full, dropped messages)

---

## Implementation

**File**: `Logger_Adapter/logging/LoggingConfig.hpp`

```cpp
struct LoggingConfig {
    // ...
    std::function<void(std::string const&)> error_callback = nullptr;
    // If nullptr, use default stderr behavior
};
```

**File**: `Logger_Adapter/logging/LoggerSetup.hpp`

```cpp
backend_opts.error_notifier = [config](std::string const& msg) {
    if (config.error_callback) {
        config.error_callback(msg);
    } else {
        // Default: stderr
        std::fprintf(stderr, "%s\n", msg.data());
    }

    // Always notify HealthProbe
    emergency::HealthProbe::RecordBackendError();
};
```

---

## Acceptance Criteria

- [ ] Custom `error_callback` receives backend error messages
- [ ] `error_callback = nullptr` falls back to stderr (no behavioral change)
- [ ] Build succeeds Debug|x64
