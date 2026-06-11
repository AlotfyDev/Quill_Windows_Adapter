# AA-M13 — Stderr Sink Option (After-Audit Corrected)

> **Phase**: 4 — 🧩 Macros & Sinks  
> **Effort**: 30 min  
> **Depends on**: AA-C01 (sink infrastructure via SinkFactory)  
> **v1.x Reference**: TASK-M13-StderrSink.md  
> **Audit Issues**: M13-A (color mode), M13-B (stderr + stdout independent)

---

## Problem

Logger_Adapter supports stdout console output but not stderr. Some operators want errors on stderr and normal logs on stdout.

---

## Corrected Implementation Plan

### Step 1 — Add `stderr` config block (independent, not exclusive)

```cpp
struct LoggingConfig {
    struct {
        bool enabled = true;
        std::string stream = "stdout";
        bool colored = true;
        // NOTE: console uses stream semantics — append/truncate not applicable
    } console;

    // NEW: stderr — independent from console (stdout)
    // NOTE: stderr uses stream semantics — always appends, no append/truncate flag
    // Color detection is per-instance: stderr may be a pipe while stdout is a TTY
    struct {
        bool enabled = false;
        bool colored = true;    // color mode for stderr
    } stderr;

    config::PatternConfig console_pattern;      // pattern for console/stdout sinks
    config::PatternConfig file_pattern;         // pattern for file sinks
    config::PatternConfig stderr_pattern;       // pattern for stderr sinks
};
```

> **Named sink sharing**: Sinks are shared singletons via `Frontend::create_or_get_sink`. All loggers referencing the same named sink share the same instance. Config mutations affect all users of that sink.

### Step 2 — Create stderr sink via Quill

```cpp
// In SinkFactory or LoggerSetup:
if (config.stderr.enabled) {
    quill::ConsoleSinkConfig stderr_cfg;
    stderr_cfg.set_stream("stderr");
    // Each ConsoleSink calls its own _configure_colour_support() — stderr and
    // stdout use INDEPENDENT terminal detection.  This is correct: stderr may
    // be redirected to a pipe/file while stdout is still a TTY.
    stderr_cfg.set_colour_mode(
        config.stderr.colored
            ? quill::ConsoleSinkConfig::ColourMode::Automatic
            : quill::ConsoleSinkConfig::ColourMode::Never);
    auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>(
        "stderr", stderr_cfg);
    sinks.push_back(sink);
}
```

### Step 3 — Use named sink "stderr" in LoggerEntry

Named sinks are shared singletons (via `Frontend::create_or_get_sink`). All loggers using `"stderr"` share the same sink instance.

```cpp
// A logger can reference both console and stderr:
{"Emergency", 3, {"console", "file", "stderr"}, true, 5000, 6}
```

> **Sharing semantics**: Because sinks are singletons, changing `config.stderr.colored` after the sink is created will NOT affect the already-created sink. Restart `InitializeLogging` to pick up config changes.

---

## Acceptance Criteria

- [ ] `config.stderr.enabled = true` produces output on stderr
- [ ] stderr and stdout (console) can both be enabled independently
- [ ] stderr supports colored output
- [ ] Build succeeds Debug|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add `stderr` config block |
| `Logger_Adapter/logging/LoggerSetup.hpp` | Wire stderr sink creation |