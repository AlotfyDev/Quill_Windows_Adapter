# M13 — StderrSink Option

- **Priority**: 🟡 Medium
- **Est. Effort**: 15 minutes
- **Depends on**: None

---

## Problem

Only stdout ConsoleSink is available (`config.console.stream = "stdout"`). There is no way to use `stderr` as a separate output channel.

In production systems, stderr is often treated differently by process managers (e.g., Windows Service event logging, docker log drivers).

---

## Implementation

**File**: `Logger_Adapter/logging/LoggingConfig.hpp`

Add:

```cpp
struct LoggingConfig {
    struct {
        bool enabled = false;
        bool colored = false;  // stderr typically not colored
    } stderr;
    // ...
};
```

**File**: `Logger_Adapter/logging/LoggerSetup.hpp`

```cpp
if (config.stderr.enabled) {
    quill::ConsoleSinkConfig stderr_cfg;
    stderr_cfg.set_stream("stderr");
    stderr_cfg.set_colour_mode(config.stderr.colored
        ? quill::ColourMode::Always : quill::ColourMode::Never);
    auto stderr_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>(
        "stderr_sink", stderr_cfg);
    sinks.push_back(stderr_sink);
}
```

---

## Acceptance Criteria

- [ ] When `stderr.enabled = true`, messages appear on stderr
- [ ] When `stderr.enabled = false`, no stderr sink is created (zero overhead)
- [ ] Build succeeds Debug|x64
