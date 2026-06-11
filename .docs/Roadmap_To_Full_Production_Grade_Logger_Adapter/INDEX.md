# Roadmap: Logger_Adapter → Full Production-Grade Windows Logger

**Quill v10.0.1** — C++17 — Windows x64 — Visual Studio 2022

## Purpose

Every gap between Quill's capability and Logger_Adapter's current state is cataloged below. Each task document describes a single feature gap, its implementation plan, and acceptance criteria. Tasks marked with a POSIX icon replace a Quill feature that is Linux-only with a Windows-native alternative.

---

## Priority Legend

| Icon | Category |
|:---:|---|
| 🔴 | **Critical** — Blocks production trading (multi-logger, compile-time level, backtrace) |
| 🟡 | **Medium** — Significant usability, debuggability, or config flexibility |
| 🟢 | **Low** — Nice-to-have polish or STL type support |
| POSIX 🔄 | **Windows Replacement** — Replaces a POSIX-only Quill feature |

---

## Task Index

### 🔴 Critical (3)

| ID | Task | Est. Effort | Dependencies |
|:---:|---|---|---|
| [C01](./TASK-C01-MultiLogger.md) | **Multi-Logger Shim** — Named Loggers + Per-Logger Level + Per-Logger Sinks | 3-4h | Current LoggerSetup.hpp structure |
| [C02](./TASK-C02-CompileTimeLogLevel.md) | **Compile-Time Log Level** — إزالة Debug/Trace من Binary | 30min | vcxproj + HotPathLogger.hpp |
| [C03](./TASK-C03-BacktraceLogging.md) | **Backtrace Logging** — Ring buffer + Auto-flush on ERROR | 2-3h | C01 (needs named loggers) |

### 🟡 Medium (13)

| ID | Task | Est. Effort | Dependencies |
|:---:|---|---|---|
| [M01](./TASK-M01-FileSinkAppendMode.md) | **Append/Truncate mode** for FileSink | 30min | None |
| [M02](./TASK-M02-DailyFileRotation.md) | **Daily time-based rotation** for file sinks | 1h | None |
| [M03](./TASK-M03-LOGV_Macros.md) | **LOGV macros** — Variable-name argument logging | 30min | None |
| [M04](./TASK-M04-LOGJ_StructuredLogging.md) | **LOGJ macros** — Upgrade StructuredLogger to deferred JSON | 1h | None |
| [M05](./TASK-M05-LimitMacros.md) | **Rate-limited macros** (LIMIT + EVERY_N) | 1h | None |
| [M06](./TASK-M06-CustomPatternPerSink.md) | **Custom PatternFormatter per sink** | 1-2h | None |
| [M07](./TASK-M07-ErrorNotifier.md) | **Backend error_notifier callback** in LoggingConfig | 30min | None |
| [M08](./TASK-M08-QueueConfig.md) | **Queue type + capacity config** (Bounded/Unbounded, size) | 1h | None |
| [M09](./TASK-M09-ResultMonadic.md) | **Result\<T\> monadic API** — value_or, and_then, map | 1h | None |
| [M10](./TASK-M10-ResultGuardedAccess.md) | **Result\<T\> guarded access** — Check on Value()/Error() | 30min | None |
| [M11](./TASK-M11-EmergencyReset.md) | **EmergencyManager::Reset()** — Return from emergency mode | 30min | None |
| [M12](./TASK-M12-DeadCodeCleanup.md) | **Dead code cleanup** — Remove unused SetupCrashLogger, shutdown_timeout_ms | 15min | None |
| [M13](./TASK-M13-StderrSink.md) | **StderrSink option** — Optional stderr output | 15min | None |

### POSIX 🔄 Windows Replacements (2)

| ID | Task | Replaces | Est. Effort |
|:---:|---|---|---|
| [P01](./TASK-P01-EventLogSink.md) | **Windows EventLog Sink** — Win32 ReportEvent API | `SyslogSink` | 2-3h |
| [P02](./TASK-P02-ThreadAffinity.md) | **Thread affinity config** — SetThreadAffinityMask | Quill `cpu_affinity` (no-op on Windows) | 30min |

### 🟢 Low Priority (grouped)

| ID | Task | Est. Effort |
|:---:|---|---|
| [G01](./TASK-G01-LowPriority.md) | **Low priority enhancements** — NullSink, STL types, ColourMode, UTC timezone, etc. | On-demand |

---

## Build Status

**Current**: ✅ Compiles Debug|x64 via MSBuild. Logger_Adapter.lib + Experimental_Console.exe.

**CI Target**: `msbuild /p:Configuration=Release /p:Platform=x64`

---

## Versioning

- Logger_Adapter follows Quill's major.minor.patch independently
- This roadmap targets **Adapter v0.2.0** (current: v0.1.0)
