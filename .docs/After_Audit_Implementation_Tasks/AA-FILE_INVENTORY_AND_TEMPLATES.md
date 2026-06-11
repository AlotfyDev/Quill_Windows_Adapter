
# Inventory & Build Plan for Logger_Adapter Project Files

> **Generated**: 2026-06-11  
> **Scope**: Structural alignment of Logger_Adapter project and test projects  
> **Verification**: Resolves conflicts between Quill APIs and existing modular design (`RateLimiter`, `FileSinkHandler`, `StderrSink`, `PatternValidator`)

---

## 1. Resolved Key Architectural Conflicts

Following a detailed filesystem code search, we have identified that the project **already has structured modular components** that map perfectly to the After-Audit (AA-*) tasks. Implementing these tasks should **populate and extend these existing files** rather than creating new competing ones:

### 1.1 `FileSinkHandler.hpp/.cpp` (AA-M01 & AA-M02)
- **Verdict**: Populated and integrated.
- **Role**: This is the dedicated file-handling helper.
  - `AA-M01` (Append Mode): Implemented via `PrepareTruncate()` in `FileSinkHandler.cpp` (renames previous log to `.prev` rather than racy delete).
  - `AA-M02` (Daily Rotation): Implemented via `FileRotationHandler` class in `FileSinkHandler.cpp` (computes time-based rotation paths, handles zlib compression level, enforces retention limits).
- **Integration**: `setup/SinkFactory.hpp` delegates file sink preparation and naming to `FileSinkHandler`.

### 1.2 `rate_limiter/RateLimiter.hpp/.cpp` (AA-M05)
- **Verdict**: Populated and integrated.
- **Role**: Provides the unified rate-limiting engine.
  - `Allow()` implements time-based rate limiting with epoch/atomic counters and burst allowance.
  - `GetGlobalRateLimiter()` returns a thread-safe singleton, enforcing `LOG_GLOBAL_LIMIT` across all subsystems.
- **Integration**: `macros/RateLimited.hpp` wraps the `RateLimiter` class in macro wrappers for performance, keeping clean separation of concerns.

### 1.3 `sinks/StderrSink.hpp` (AA-M13)
- **Verdict**: Integrated.
- **Role**: Declares the `StderrSinkConfig` POD.
- **Reasoning**: We use Quill's native `ConsoleSinkConfig::set_stream("stderr")` to build the physical sink (ensures zero overhead, C++17 compliance, and colored output support). `sinks/StderrSink.hpp` simply hosts the configuration structure.

### 1.4 `config/PatternValidator.hpp/.cpp` (AA-M06)
- **Verdict**: Integrated.
- **Role**: Sanitizes and validates user-provided PatternFormatter strings at initialization.
- **Integration**: `setup/SinkFactory.hpp` passes raw config patterns to `PatternValidator::Validate()` before creating sinks, preventing silent formatting errors.

---

## 2. Complete File Inventory for Logger_Adapter (45 Files)

Every file in the workspace is organized into explicit Visual Studio filters. There are **0 orphans** and **0 missing files**.

### 2.1 Configuration Layer (6 Files)
- `Logger_Adapter/config/BacktraceConfig.hpp` 📝 (AA-C03) — Backtrace ring buffer config POD.
- `Logger_Adapter/config/LoggerEntry.hpp` 📝 (AA-C01) — Named logger specification POD (using sink-name referencing).
- `Logger_Adapter/config/PatternConfig.hpp` 📝 (AA-M06) — Log format config POD.
- `Logger_Adapter/config/QueueConfig.hpp` 📝 (AA-M08) — Bounded/Unbounded queue config.
- `Logger_Adapter/config/PatternValidator.hpp` 🔧 (AA-M06) — Pattern validation interface.
- `Logger_Adapter/config/PatternValidator.cpp` 🔧 (AA-M06) — Pattern validation implementation.

### 2.2 System & Emergency Layer (5 Files)
- `Logger_Adapter/emergency/CrashHandler.hpp` 🔧 (AA-M12) — Structured SEH/signal crash interceptor.
- `Logger_Adapter/emergency/EmergencyConfig.hpp` 🔧 — Callbacks and flags for fatal state.
- `Logger_Adapter/emergency/EmergencyManager.hpp` 🔧 (AA-M11) — State machine (`Normal` → `Degraded` → `Recovering` → `Fatal`).
- `Logger_Adapter/emergency/GracefulShutdown.hpp` 🔧 — Order execution safe flush logic.
- `Logger_Adapter/emergency/HealthProbe.hpp` 🔧 (AA-M11) — Symmetric atomic statistics (`RecordEmergency()` / `RecordRecovery()`).

### 2.3 Error Handling Layer (4 Files)
- `Logger_Adapter/error/ErrorCode.hpp` 🔧 — Unified trading error codes.
- `Logger_Adapter/error/ErrorContext.hpp` 🔧 — File/line error capture.
- `Logger_Adapter/error/ErrorMacros.hpp` 🔧 — Macro wrappers for quick error context generation.
- `Logger_Adapter/error/Result.hpp` 🔧 (AA-M09, AA-M10) — Monadic `Result<T>` with guarded accessors.

### 2.4 Modular Helpers (12 Files)
- `Logger_Adapter/rate_limiter/RateLimiter.hpp` 📝 (AA-M05) — Time-based/Global limiter declaration.
- `Logger_Adapter/rate_limiter/RateLimiter.cpp` 📝 (AA-M05) — Atomic relaxed-ordering implementation.
- `Logger_Adapter/sanitize/SanitizationRule.hpp` 🔧 — Sensitive pattern matcher rule.
- `Logger_Adapter/sanitize/SanitizationFilter.hpp` 🔧 — Log scrubbing pipeline filter.
- `Logger_Adapter/sanitize/SanitizationFilter.cpp` 🔧 — Log scrubbing pipeline filter implementation.
- `Logger_Adapter/sanitize/AhoCorasick.hpp` 🔧 — High-performance multi-pattern string search.
- `Logger_Adapter/sanitize/AhoCorasick.cpp` 🔧 — Aho-Corasick implementation.
- `Logger_Adapter/windows/ThreadAffinity.hpp` 📝 (AA-P02) — NUMA group affinity wrappers.
- `Logger_Adapter/sinks/EventLogSink.hpp` 📝 (AA-P01) — Windows native EventLog header.
- `Logger_Adapter/sinks/EventLogSink.cpp` 📝 (AA-P01) — `ReportEventW` WCHAR implementation.
- `Logger_Adapter/sinks/FileSinkHandler.hpp` 📝 (AA-M01/M02) — File management helper header.
- `Logger_Adapter/sinks/FileSinkHandler.cpp` 📝 (AA-M01/M02) — Truncation and rotation logic.
- `Logger_Adapter/sinks/StderrSink.hpp` 🔧 (AA-M13) — StderrSink configuration struct.

### 2.5 Setup & Core Layer (12 Files)
- `Logger_Adapter/logging/HotPathLogger.hpp` 🔧 (AA-C02) — Basic zero-overhead macros.
- `Logger_Adapter/logging/LoggerSetup.hpp` 🔧 (AA-C01) — Initial bootstrap API.
- `Logger_Adapter/logging/LoggingConfig.hpp` 🔧 (AA-C01) — Core configuration container.
- `Logger_Adapter/logging/StructuredLogger.hpp` 🔧 (AA-M04) — Non-deferred JSON logger.
- `Logger_Adapter/setup/LoggerRegistry.hpp` 📝 (AA-C01) — Thread-safe singleton registry.
- `Logger_Adapter/setup/LoggerSetup.hpp` 📝 (AA-C01) — Refactored Initialize/Shutdown functions.
- `Logger_Adapter/setup/SinkFactory.hpp` 📝 (AA-C01) — Common and custom sink generator.
- `Logger_Adapter/macros/Backtrace.hpp` 📝 (AA-C03) — Backtrace logging macros.
- `Logger_Adapter/macros/RateLimited.hpp` 📝 (AA-M05) — Rate-limited logging macros.
- `Logger_Adapter/macros/Structured.hpp` 📝 (AA-M04) — Deferred JSON logging macros.
- `Logger_Adapter/macros/VariableArgs.hpp` 📝 (AA-M03) — Key-Value argument logging macros.
- `Logger_Adapter/framework.h` 🔧 — Win32 minimal header exclusions.
- `Logger_Adapter/pch.h` 🔧 — Precompiled header list.
- `Logger_Adapter/pch.cpp` 🔧 — Precompiled header generator.
- `Logger_Adapter/Logger_Adapter.cpp` 🔧 — Module entry-point.

---

## 3. Files to Create in Test Projects (21 Files)

To provide 100% verification coverage of all Windows-native and C++17 compliance designs, the test directory will be structured with specialized test files matching the implementation Waves:

```
Logger_Adapter_Tests/
├── BenchmarkTests/
│   └── Benchmark.cpp                🆕 Benchmark harness for queues and sanitization
├── UnitTests/
│   ├── Tests_DeadCodeCleanup.cpp    🆕 Verify stale configuration compilation errors
│   ├── Tests_CompileTimeLevel.cpp   🆕 Assert Debug logs are stripped from Release
│   ├── Tests_QueueConfig.cpp        🆕 Assert BoundedBlocking is thread-safe and respects size
│   ├── Tests_DailyRotation.cpp      🆕 Verify date renames, NTP rewind safeties, and zlib
│   ├── Tests_RateLimiting.cpp       🆕 Assert time-window limits and global limit thread-safety
│   ├── Tests_EmergencyStateMachine.cpp 🆕 Verify state machine transitions and callbacks
│   ├── Tests_EventLog.cpp           🆕 Assert ReportEventW matches log levels (requires Win10)
│   ├── Tests_MultiLogger.cpp        🆕 Assert complete logger configuration isolation
│   ├── Tests_MultiLoggerInit.cpp    🆕 Verify configuration validation errors
│   ├── Tests_Backtrace.cpp          🆕 Assert auto-flushes on error and shutdown-flush paths
│   ├── Tests_ErrorNotifier.cpp      🆕 Assert backend callbacks are invoked asynchronously
│   ├── Tests_FileSinkMode.cpp       🆕 Verify truncate-rename vs append mode
│   ├── Tests_LOGV.cpp               🆕 Verify key-value argument formatting
│   ├── Tests_LOGJ.cpp               🆕 Assert compliant JSON output matches schema
│   ├── Tests_PatternFormat.cpp      🆕 Verify custom formatting patterns per-logger
│   ├── Tests_StderrSink.cpp         🆕 Assert stderr colors and stream separation
│   ├── Tests_ResultMonadic.cpp      🆕 Verify composable value_or, map, and and_then
│   ├── Tests_ResultGuarded.cpp      🆕 Assert assert-in-debug and std::terminate-in-release
│   └── Tests_ThreadAffinity.cpp     🆕 Verify SetThreadGroupAffinity masks on NUMA
└── docs/
    ├── json-schema.json             🆕 LOGJ schema specification
    └── pattern-grammar.md           🆕 Custom pattern grammar syntax reference
```

---

## 4. Unified Folder & Project Structure Template

When developers switch to **ACT MODE**, the files must be laid out in MSBuild and Solution Explorer according to this final structure:

```
Cross_Language_Trading_System/ (Solution)
├── Logger_Adapter/ (Static Library project)
│   ├── config/ (Filter)
│   │   ├── BacktraceConfig.hpp
│   │   ├── LoggerEntry.hpp
│   │   ├── PatternConfig.hpp
│   │   ├── PatternValidator.hpp
│   │   ├── PatternValidator.cpp
│   │   └── QueueConfig.hpp
│   ├── emergency/ (Filter)
│   │   ├── CrashHandler.hpp
│   │   ├── EmergencyConfig.hpp
│   │   ├── EmergencyManager.hpp
│   │   ├── GracefulShutdown.hpp
│   │   └── HealthProbe.hpp
│   ├── error/ (Filter)
│   │   ├── ErrorCode.hpp
│   │   ├── ErrorContext.hpp
│   │   ├── ErrorMacros.hpp
│   │   └── Result.hpp
│   ├── logging/ (Filter)
│   │   ├── HotPathLogger.hpp
│   │   ├── LoggerSetup.hpp
│   │   ├── LoggingConfig.hpp
│   │   └── StructuredLogger.hpp
│   ├── macros/ (Filter)
│   │   ├── Backtrace.hpp
│   │   ├── RateLimited.hpp
│   │   ├── Structured.hpp
│   │   └── VariableArgs.hpp
│   ├── rate_limiter/ (Filter)
│   │   ├── RateLimiter.hpp
│   │   └── RateLimiter.cpp
│   ├── sanitize/ (Filter)
│   │   ├── AhoCorasick.hpp
│   │   ├── AhoCorasick.cpp
│   │   ├── SanitizationFilter.hpp
│   │   ├── SanitizationFilter.cpp
│   │   └── SanitizationRule.hpp
│   ├── setup/ (Filter)
│   │   ├── LoggerRegistry.hpp
│   │   ├── LoggerSetup.hpp
│   │   └── SinkFactory.hpp
│   ├── sinks/ (Filter)
│   │   ├── EventLogSink.hpp
│   │   ├── EventLogSink.cpp
│   │   ├── FileSinkHandler.hpp
│   │   ├── FileSinkHandler.cpp
│   │   └── StderrSink.hpp
│   ├── windows/ (Filter)
│   │   └── ThreadAffinity.hpp
│   ├── framework.h
│   ├── Logger_Adapter.cpp
│   ├── pch.h
│   └── pch.cpp
└── Logger_Adapter_Tests/ (Console Test project)
    ├── BenchmarkTests/ (Filter)
    │   └── Benchmark.cpp
    ├── UnitTests/ (Filter)
    │   ├── Tests_DeadCodeCleanup.cpp
    │   ├── Tests_CompileTimeLevel.cpp
    │   ├── Tests_QueueConfig.cpp
    │   ├── Tests_DailyRotation.cpp
    │   ├── Tests_RateLimiting.cpp
    │   ├── Tests_EmergencyStateMachine.cpp
    │   ├── Tests_EventLog.cpp
    │   ├── Tests_MultiLogger.cpp
    │   ├── Tests_MultiLoggerInit.cpp
    │   ├── Tests_Backtrace.cpp
    │   ├── Tests_ErrorNotifier.cpp
    │   ├── Tests_FileSinkMode.cpp
    │   ├── Tests_LOGV.cpp
    │   ├── Tests_LOGJ.cpp
    │   ├── Tests_PatternFormat.cpp
    │   ├── Tests_StderrSink.cpp
    │   ├── Tests_ResultMonadic.cpp
    │   ├── Tests_ResultGuarded.cpp
    │   └── Tests_ThreadAffinity.cpp
    ├── docs/ (Filter)
    │   ├── json-schema.json
    │   └── pattern-grammar.md
    ├── Tests_Emergency.cpp
    ├── Tests_ErrorHandling.cpp
    └── Tests_LoggingInit.cpp
```

*This directory layout completely preserves Separation of Concerns (SoC), ensures C++17 static library compatibility, and provides a clear audit trail for the implementation phase.*
