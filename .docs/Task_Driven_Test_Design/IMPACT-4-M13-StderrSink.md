# Impact Analysis: Stderr Sink Option

## Summary
Total GAPs: 4 | P0: 0 | P1: 0 | P2: 4 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-4-M13-1 | 🟡 P2 | AA spec adds `bool append = true` to stderr config block, but `ConsoleSink` has no open-mode concept — stderr is a pre-existing stream. The flag is meaningless. | Operator sets `stderr.append = false` expecting "fresh stderr" — impossible with stream semantics. Silent no-op creates confusion. | No | 🛠️ Fix now — Remove `append` from stderr config block. Document that stderr uses stream semantics (always appends). |
| GAP-4-M13-2 | 🟡 P2 | AA spec defines `console_pattern` and `file_pattern` but no `stderr_pattern`. Operators routing CRITICAL to stderr with a shorter format have no way to configure it — the logger's single pattern is shared across all sinks. | Emergency messages on stderr use the same verbose format as INFO on file, cluttering operator consoles. Purely cosmetic/ergonomic issue. | No | 🛠️ Fix now — Add `config::PatternConfig stderr_pattern` to `LoggingConfig` alongside `console_pattern` and `file_pattern`. Wire it in `InitializeLogging`. |
| GAP-4-M13-3 | 🟡 P2 | AA spec doesn't specify that stderr and stdout use INDEPENDENT color detection. `ConsoleSink::_configure_colour_support()` is called per-instance — stderr may be a pipe while stdout is a TTY. | If the implementation accidentally reuses stdout's terminal detection for stderr, stderr would get ANSI codes in pipe/file output (data corruption for parsers). | No | 📝 Document only — Add note in AA spec: "Each ConsoleSink instance calls its own `_configure_colour_support()` — stderr and stdout use independent terminal detection." |
| GAP-4-M13-4 | 🟡 P2 | AA spec doesn't define whether named sinks (e.g., `"stderr"`) are shared singletons or recreated per logger. Quill's `create_or_get_sink` implies shared singletons. | If an operator accidentally mutates a shared sink config, ALL loggers using that sink are affected — unexpected side effects. | No | 📝 Document only — Add note: "Named sinks are shared singletons via `Frontend::create_or_get_sink`. Config changes to a shared sink affect all loggers referencing it." |

## Recommended AA Changes
- **GAP-4-M13-1**: Remove `bool append = true` from the stderr config struct in Step 1. Keep append only on file sinks.
- **GAP-4-M13-2**: Add `config::PatternConfig stderr_pattern;` to `LoggingConfig` struct in Step 1. Add wire-up in `InitializeLogging` code in Step 2.
- **GAP-4-M13-3**: Add documentation note about independent color detection per sink instance.
- **GAP-4-M13-4**: Add documentation note about named sinks being shared singletons.
