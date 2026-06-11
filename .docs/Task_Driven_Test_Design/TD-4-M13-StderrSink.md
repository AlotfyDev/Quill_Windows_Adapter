# Test Design: Stderr Sink Option

## Under Spec
- AA File: `AA-M13-StderrSink.md`
- Phase: 4
- Key Requirements:
  - `config.stderr.enabled` → output on stderr (default: false); `config.stderr.colored` controls ANSI color mode
  - stderr config has `enabled` and `colored` fields only (no `append` — stream semantics, always appends)
  - stderr and stdout (console) are independent — both can be enabled simultaneously
  - `config.stderr_pattern` provides independent pattern formatting for stderr sinks
  - `ConsoleSinkConfig::set_stream("stderr")` creates stderr sink
  - Named sink "stderr" referenceable in `LoggerEntry` sink list (e.g., `{"Emergency", 3, {"console", "file", "stderr"}}`)

## Test Harness
- **Fixture**: Google Test with `SetUp` saving original stderr and stdout file descriptors, redirecting stderr to a temp file via `freopen`. `TearDown` restores original fds. Quill backend started/stopped per test.
- **Real vs mock**: Real `ConsoleSink` with `set_stream("stderr")`. Real Quill frontend/backend. Stderr captured via fd redirect to temp file. Stdout optionally captured via separate redirect to a second temp file.
- **Preconditions**: None beyond standard file access. Stderr not closed by prior test.

## Scenarios

### Positive Cases
- Stderr enabled, stdout disabled: `config.stderr.enabled = true; config.console.enabled = false`. Write `LOG_CRITICAL(logger, "oh no")`. Stderr file must contain the message. Stdout file (if checked) must be empty.
- Both stderr and stdout enabled: `LOG_INFO(logger, "normal")` goes to stdout, `LOG_CRITICAL(logger, "panic")` goes to both (logger configured with both sinks). Stdout file contains "normal" and "panic". Stderr file contains only "panic".
- Stderr color mode `Automatic`: `set_colour_mode(ColourMode::Automatic)` — colors enabled only if stderr is a terminal. In test environment (redirected to file), colors disabled. Verify output does NOT contain ANSI escape codes `\033[`.
- Stderr color mode `Always`: `set_colour_mode(ColourMode::Always)` — colors forced. Verify output contains ANSI escape codes even when writing to file.
- Stderr color mode `Never`: `set_colour_mode(ColourMode::Never)` — colors disabled. Verify output has no ANSI codes regardless of terminal status.
- Named sink reference: LoggerEntry `{"Emergency", {"console", "file", "stderr"}}` creates a logger with all three sinks. Verify each sink receives the message.
- Stderr-only logger: LoggerEntry `{"ErrorsOnly", {"stderr"}}` with only stderr sink. All messages go to stderr, nothing to stdout or file.
- Stderr pattern vs console pattern: Configure `stderr_pattern` with `"STDERR|%(message)"` and `console_pattern` with default. Write same message to a logger with both sinks. Stderr file contains `STDERR|...`, stdout file contains default format.

### Negative / Error Cases
- Stderr stream closed before sink creation: Call `fclose(stderr)` then create sink. `ConsoleSink` constructor passes `config.stream()` = `"stderr"` to `StreamSink` which calls `fopen`... Actually, `ConsoleSink` does NOT call `fopen` — it uses the pre-existing `stderr` `FILE*`. Looking at the source, `StreamSink` takes a filename or uses `stderr`/`stdout` FILE pointers. Let me verify... `StreamSink` constructor takes either a `FILE*` or a filename string. When given "stderr", it maps to the `stderr` global `FILE*`. If `stderr` is closed, writing to it is UB. Test must ensure handle is valid.
- Stderr color with non-tty: `ColourMode::Automatic` when stderr is a file (not tty). Colors disabled. Verify no escape codes.
- Massive stderr output: 100k messages to stderr. Ensure no fd leak, no buffer overflow, no crash. Stderr file must contain all messages.
- Empty stderr config: `config.stderr = {...}` with `enabled = false` (default). No stderr sink created. No performance impact on stdout/file sinks.

### Production Realities
- Stderr and stdout interleaving: When both sinks are on the same logger and `append = true`, Quill's backend formats and writes to both sinks sequentially. Order of writes to stdout vs stderr is deterministic per backend cycle. Verify that `stdout` content and `stderr` content are correctly ordered by timestamp.
- Stderr used for emergency/panic only: Production pattern — normal logs to stdout+file, errors/critical to stderr. Verify a logger configured with `{"stderr"}` for CRITICAL level + `{"console"}` for INFO level using dynamic log level. Actually, this requires per-sink log level filtering which the AA does not specify — if operators want this, they need separate loggers per level.
- Process crash recovery: Buffered stderr content may be lost on crash if `setvbuf` buffering is active. Document that stderr uses line-buffering by default in most implementations. Verify Quill does NOT change stderr buffering mode.
- Container/Docker environments: stderr is the idiomatic output stream for container logs. The stderr sink enables proper container log collection without file mounts. Verify that `config.stderr.enabled = true` with no console sink produces correct Docker-style log output.

### Thread Safety
- Stderr sink creation: Single-threaded, during initialization. No race.
- Concurrent log writes to stderr: Multiple frontend threads enqueue messages. Backend thread writes to stderr sequentially via `safe_fwrite`. No concurrent writes to stderr from Quill (single backend thread). However, if user code also writes to `fprintf(stderr, ...)` concurrently, interleaving may occur. Document that mixing Quill stderr sink with direct stderr writes is unsafe.
- Stderr `FILE*` is NOT thread-safe in general. Quill's backend serializes writes, so Quill's own output is safe. But external writes to `stderr` from signal handlers or other threads may interleave.
- Signal safety: `safe_fwrite` is not async-signal-safe. Writing LOGV_CRIT from a signal handler via stderr sink may deadlock if the signal interrupted `fwrite`. Document that emergency logging from signal handlers must use async-signal-safe mechanisms (write(2) directly), not Quill.

## Assertions
- `config.stderr.enabled = true` → `ConsoleSink` created with `config.stream() == "stderr"` (verify via inspecting `ConsoleSinkConfig::stream() == "stderr"`)
- Output to stderr file when stderr sink is active: file non-empty, contains expected log messages
- Output to stderr file ABSENT when stderr sink is disabled and only console/file sinks exist
- Color mode `Never` → stderr output has zero `\033[` escape sequences
- Color mode `Always` → stderr output contains `\033[3` (ANSI color codes)
- Both stderr and stdout sinks present → two output files, each containing the messages routed to them
- Stderr sink can be the only sink for a logger (no console, no file)

## Failure Mode
- Stderr sink producing no output → **silent data loss**. Emergency/critical messages sent to stderr are lost. In container environments, all logging appears lost.
- Stderr sink producing garbled output → **data corruption**. ANSI codes in file output when colors should be disabled; control characters in log analysis tools.
- Stderr permission denied (unlikely on standard stderr) → **crash** on startup if `ConsoleSink` construction fails. Means the process's stderr has been closed or redirected incorrectly before Quill start.
- Color mode `Automatic` not detecting redirected file → **data corruption**. ANSI escape codes in log files when output is to file (not terminal). Log parsers break.
- Stderr and stdout interleaved out of order → **timeline corruption**. Operators see panic message before the normal log entry that triggered it, although timestamps would still be correct.
- Stderr pattern misconfigured as `append` → **no effect** — stderr has no append/truncate concept, field correctly absent from config.

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Removed `append` from stderr config block | Step 1 — Add `stderr` config block | Under Spec updated; stderr has no append field |
| Added `stderr_pattern` config field | Step 1 — Add `stderr` config block | Under Spec updated; scenario added for stderr_pattern |
| Named sink sharing documented | Step 3 — Use named sink | GAP-4-M13-4 notes partially addressed |
| Color detection per-instance documented | Step 2 — Create stderr sink | GAP-4-M13-3 notes partially addressed |
| Spec Gap GAP-4-M13-1 | Append on stderr | Marked ✅ RESOLVED |
| Spec Gap GAP-4-M13-2 | Missing stderr_pattern | Marked ✅ RESOLVED |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-4-M13-1 | The AA spec adds `bool append = true` to the `stderr` config block, but `ConsoleSink` (used for stderr) has no open-mode concept — it writes to the already-open `stderr` FILE stream. The `append` flag is meaningless for stderr. | Misleading config field. Operators setting `append=false` on stderr will expect "fresh stderr" (impossible — stderr is a stream, not a file). | Remove `append` from the stderr config block, or document that it is silently ignored for stderr sinks. Alternatively, repurpose it as "clear stderr on startup" which would need to flush the `stderr` buffer (also not well-defined). | ✅ RESOLVED |
| GAP-4-M13-2 | The AA spec defines stderr config independently but does NOT define a `stderr_pattern` field, only `console_pattern` and `file_pattern`. If operators want different output formats on stderr (e.g., shorter format for emergency messages), there is no supported mechanism. | Operators routing CRITICAL to stderr with `%(time) %(message)` and INFO to stdout with full details cannot configure this — both use the logger's single pattern. | Add `stderr_pattern: PatternConfig` to `LoggingConfig` alongside `console_pattern` and `file_pattern`. Wire it in `InitializeLogging` for stderr sinks. | ✅ RESOLVED |
| GAP-4-M13-3 | The AA spec does not specify what happens to stderr color output when both console (stdout) and stderr are enabled. Since `ConsoleSink` constructor configures color support per sink, each sink uses its own `_config._colours._configure_colour_support()`. STDERR color detection uses `_is_terminal_output(stderr)` which may differ from `_is_terminal_output(stdout)`. | Stderr may be a pipe/file while stdout is a TTY, or vice versa. Color support must be independently detected per sink — the implementation must NOT reuse stdout's terminal detection for stderr. | Verify in tests that each ConsoleSink instance calls its own `_configure_colour_support()`. Document that stderr and stdout use independent color detection. | 📝 Partially addressed — AA spec documents per-instance color detection |
| GAP-4-M13-4 | The AA spec references `LoggerEntry` with sink names and implies that `{"Emergency", 3, {"console", "file", "stderr"}}` creates a logger with three sinks. However, the spec doesn't define how named sinks map to actual Quill sink instances (are they shared or recreated?). If `create_or_get_sink` is used, they're shared singletons. If `create_sink` is used, they're duplicated. Shared vs exclusive semantics affect configuration mutation and memory management. | If sinks are shared singletons (via `create_or_get_sink`), changing config for one logger's sink affects all loggers referencing it. If sinks are duplicated, config isolation is guaranteed but memory usage grows. | Clarify that sinks are shared singletons (as Quill's `create_or_get_sink` implies). Document this sharing semantics in `InitializeLogging` so operators understand config mutation effects. | 📝 Partially addressed — AA spec documents shared singleton semantics |
