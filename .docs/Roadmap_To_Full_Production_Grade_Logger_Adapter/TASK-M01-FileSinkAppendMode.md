# M01 — FileSink Append/Truncate Mode

- **Priority**: 🟡 Medium
- **Est. Effort**: 30 minutes
- **Depends on**: None

---

## Problem

Quill's `FileSinkConfig::set_open_mode()` controls whether the log file is **appended to** or **truncated** on each application start. Logger_Adapter currently passes empty/default `FileSinkConfig{}`, which means Quill's default behavior applies (append = true).

For a trading system, different files need different modes:
- `logs/assembler.log` — should **append** (keep history across restarts)
- `logs/debug_session.log` — should **truncate** (fresh each run)

---

## Implementation

Add `bool append = true` to the `file` sub-config in `LoggingConfig.hpp`, and pass it to `FileSinkConfig::set_open_mode()` in `LoggerSetup.hpp`:

```cpp
// LoggingConfig.hpp
struct {
    // ...
    bool append = true;  // true=append, false=truncate
} file;

// LoggerSetup.hpp — before create_or_get_sink
if (!config.file.append) {
    quill::FileSinkConfig file_cfg;
    // FileSinkConfig doesn't have set_open_mode in v10.0.1 — alternative: remove file before start
}
```

**Note**: Quill v10.0.1's `FileSinkConfig` may not expose `set_open_mode()` directly. Alternative approach: use `std::filesystem::remove(filename)` before creating the sink when `append == false`.

---

## Acceptance Criteria

- [ ] `config.file.append = false` results in a fresh empty log file on startup
- [ ] `config.file.append = true` preserves previous log content
- [ ] Build succeeds Debug|x64
