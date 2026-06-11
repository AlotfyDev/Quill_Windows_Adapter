# AA-M07 — Error Notifier Callback (After-Audit Corrected)

> **Phase**: 3 — 🏗️ Core Infrastructure  
> **Effort**: 30 min  
> **Depends on**: AA-C01 (init flow)  
> **v1.x Reference**: TASK-M07-ErrorNotifier.md  
> **Audit Issues**: M07-A (backend thread blocking risk), M07-B (reliability contract)

---

## Problem

Quill has a backend error notification mechanism that fires when the backend thread encounters errors (e.g., file write failure). Logger_Adapter has no way for consumers to register for these notifications.

---

## Corrected Implementation Plan

### Step 1 — Add error_notifier to LoggingConfig

```cpp
// In LoggingConfig.hpp
#include <functional>

struct LoggingConfig {
    // ... existing fields ...
    
    // Callback invoked on backend thread when a logging error occurs.
    // WARNING: This callback runs on the Quill backend thread.
    //          It MUST be non-blocking (no I/O, no locks, no allocations).
    //          Blocking the callback stalls ALL logging.
    //          There is NO guaranteed delivery — if the backend is
    //          overwhelmed, the callback may not fire.
    //
    // LIFETIME: The std::function is copied by Quill internally. Captured
    //           state must remain valid for the entire backend lifetime
    //           (until after Backend::stop() completes). Prefer:
    //           - Standalone functions (no capture)
    //           - std::shared_ptr to shared state
    //           - Capture-by-copy for trivially-copyable state
    //           AVOID: capture-by-reference to stack/local variables.
    //
    // LIMITATION: Not all Quill internal error paths invoke this callback.
    //             For comprehensive error monitoring, complement with OS-level
    //             monitoring (ETW, Windows Event Log, health probes).
    std::function<void(std::string const&)> error_notifier;
};
```

### Step 2 — Wire in LoggerSetup.hpp InitializeLogging

```cpp
if (config.error_notifier) {
    quill::Backend::set_error_notifier(
        [cb = config.error_notifier](std::string const& error) {
            cb(error);
        }
    );
}
```

> **SAFETY**: The lambda captures `cb` by copy (the std::function is copied), not by reference. This ensures the user's callback object is owned by the lambda and lives as long as Quill holds the notifier. However, if `cb` itself holds references (e.g., a lambda capturing `[&]`), those references may still dangle — the user is responsible for those lifetimes. The capture-by-copy only guarantees the `std::function` wrapper itself is not dangling.

### Step 3 — Document in Experimental_Console

```cpp
// Example error notifier (non-blocking, just logs to debug output):
LoggingConfig cfg;
cfg.error_notifier = [](std::string const& error) {
    OutputDebugStringA(("Backend error: " + error + "\n").c_str());
};
InitializeLogging(cfg);
```

---

## Acceptance Criteria

- [ ] `error_notifier` callback is invoked on backend thread when Quill encounters errors
- [ ] Documentation warns that callback must be non-blocking
- [ ] Documentation states "no guaranteed delivery" — best effort only
- [ ] Documentation warns about callback lifetime (must outlive Backend::stop())
- [ ] Documentation acknowledges that not all Quill error paths trigger the callback
- [ ] Backward compatible: not setting `error_notifier` = no callback
- [ ] Build succeeds Debug|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add `error_notifier` field (with lifetime/lifetime docs) |
| `Logger_Adapter/logging/LoggerSetup.hpp` | Wire error notifier into Backend::start() (capture-by-copy) |