# G01 — Low Priority Enhancements

- **Priority**: 🟢 Low
- **Est. Effort**: On-demand (grouped)
- **Depends on**: None

---

## Items in this Group

| # | Feature | Est. Effort | File Location |
|---|---|---|---|
| G01a | **NullSink** — Discard output, useful for testing or disabling logging | 15min | `logging/LoggerSetup.hpp` |
| G01b | **RotatingJsonFileSink** — JSON with daily/size rotation (extension of M02) | 30min | `logging/LoggerSetup.hpp` |
| G01c | **ColourMode::Always** — Force ANSI colors even on pipe/redirect | 5min | `logging/LoggerSetup.hpp` |
| G01d | **UTC Timestamp** — Option to use UTC instead of LocalTime in PatternFormatter | 15min | See M06 |
| G01e | **Logger hierarchy** — Child loggers inheriting from parent | When M01 is done | `logging/LoggerSetup.hpp` |
| G01f | **remove_logger / get_all_loggers** — Dynamic logger management | 30min | `logging/LoggerSetup.hpp` |
| G01g | **preallocate()** — Pre-allocate thread-local queue to avoid first-use allocation | 5min | `logging/LoggerSetup.hpp` |
| G01h | **Transit event buffer config** — `initial_capacity`, `soft_limit`, `hard_limit` | 15min | `logging/LoggingConfig.hpp` |
| G01i | **sink_min_flush_interval** — Control flush frequency | 10min | `logging/LoggingConfig.hpp` |
| G01j | **check_printable_char** callback config | 10min | `logging/LoggingConfig.hpp` |
| G01k | **check_backend_singleton_instance** — Detect multiple singletons (DLL+EXE) | 5min | `logging/LoggingConfig.hpp` |
| G01l | **log_timestamp_ordering_grace_period** — Strict ordering config | 10min | `logging/LoggingConfig.hpp` |
| G01m | **STL Type Headers** — Include `<quill/std/*.h>` for direct Vector, Map, etc. logging | 5min | `pch.h` or `LoggerSetup.hpp` |

---

## Note

These are intentionally deferred. They represent polishing and edge-case hardening, not core functionality. Implement as time permits, or when a specific need arises.

---

## Acceptance Criteria (per item)

- [ ] Feature compiles and builds without side effects
- [ ] Default behavior unchanged when feature is not enabled
- [ ] Build succeeds Debug|x64
