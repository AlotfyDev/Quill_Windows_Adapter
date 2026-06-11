# AA-M01 — FileSink Append/Truncate Mode (After-Audit Corrected)

> **Phase**: 4 — 🧩 Macros & Sinks  
> **Effort**: 45 min  
> **Depends on**: AA-C01 (sink infrastructure via SinkFactory)  
> **v1.x Reference**: TASK-M01-FileSinkAppendMode.md  
> **Audit Issues**: M01-A (file-delete race), M01-B (Quill API verification), M01-C (per-sink vs global)

---

## Problem

Logger_Adapter always appends to log files. Some debugging scenarios need a fresh file on each restart (truncate mode).

---

## Corrected Implementation Plan

### Step 1 — Use Quill v10.0.1 Native API

`FileSinkConfig::set_open_mode(char)` is always available in Quill v10.0.1 (`FileSink.h:121`, defaults to `'a'`). Use it directly when `append=false`:

```cpp
if (!config.file.append) {
    PrepareTruncate(config.file.filename);
}
// Create FileSink with the desired open mode:
auto sink_cfg = std::make_unique<quill::FileSinkConfig>();
sink_cfg->set_open_mode(config.file.append ? 'a' : 'w');
// Pass sink_cfg to FileSink constructor...
```

> **Note**: If targeting Quill versions older than v10 (where `set_open_mode` may not exist), use the `PrepareTruncate` rename fallback described in Step 2 as a safety net.

### Step 2 — File-Rename Fallback (NOT file-delete)

Racy: `std::filesystem::remove(filename)` while external processes write → undefined behavior on Windows.

**Safer approach**: Rename the existing file (append `.bak` or timestamp) before creating new one. Note: `PrepareTruncate` is called BEFORE `FileSink` construction and `quill::Backend::start()`, so no Quill backend writer exists yet — the race concern is with EXTERNAL processes writing to the same file.

```cpp
void PrepareTruncate(const std::string& filename) {
    namespace fs = std::filesystem;
    if (fs::exists(filename)) {
        // Rename, don't delete — avoids race with backend writer
        std::error_code ec;
        fs::rename(filename, filename + ".prev", ec);
        // If rename fails (file locked), log warning and continue
        if (ec) {
            OutputDebugStringA(("WARNING: Could not rename " + filename + " — will append\n").c_str());
        }
    }
}
```

### Step 3 — Per-Sink Append Mode (NOT global)

Add `append` to each sink sub-config, not just the global `file` block:

```cpp
struct {
    bool enabled = true;
    std::string stream = "stdout";
    bool colored = true;
    // NOTE: console uses stream semantics (stdout/stderr) — no append/truncate concept.
    // The `append` flag below only applies to file sinks.
} console;

struct {
    bool enabled = false;
    std::string filename = "logs/assembler.log";
    bool rotating = true;
    size_t max_file_size = 10 * 1024 * 1024;
    uint32_t max_files = 5;
    bool append = true;  // NEW: per-file-sink append mode (true=append, false=truncate)
} file;
```

### Step 4 — Wire in SinkFactory

```cpp
// In CreateFileSink():
if (!config.file.append) {
    PrepareTruncate(config.file.filename);
}
// Then create the sink as before (it will append to existing or create new)
```

---

## Acceptance Criteria

- [ ] `config.file.append = false` → fresh/truncated file on startup
- [ ] File-rename fallback does not race with external processes
- [ ] Per-sink append mode (file sink gets its own `append`; console uses stream semantics)
- [ ] Build succeeds Debug|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add per-sink `append` fields |
| `Logger_Adapter/setup/SinkFactory.hpp` | Wire truncate/append logic |