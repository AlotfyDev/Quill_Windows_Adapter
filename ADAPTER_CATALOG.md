# Logger_Adapter Catalog — Quill Windows Adapter

## Overview

This adapter bridges **platform gaps** in Quill v10.0.1 on Windows while providing **enhanced capabilities** missing from Quill across all platforms.

---

## Quill Windows Limitations vs. Logger_Adapter Solutions

| Feature | Quill Status on Windows | Logger_Adapter Solution |
|---------|------------------------|------------------------|
| **SyslogSink** | ❌ Won't compile (`syslog.h` not available) | N/A (use EventLogSink instead) |
| **SystemdSink** | ❌ Won't compile (`systemd/sd-journal.h` not available) | N/A (use EventLogSink instead) |
| **AndroidSink** | ❌ Won't compile (`android/log.h` not available) | N/A (Windows-specific) |
| **/dev/null null sink** | ⚠️ Silently creates file (StreamSink.h:76) | ✅ `NullSink` - explicit null sink |
| **Daily rotation with gzip** | ❌ Not supported | ✅ `DailyRotatingFileSink` with zlib compression |
| **Custom archive naming** | ❌ Not supported | ✅ `{stem}.{YYYY-MM-DD}.{ext}` naming |
| **Retention by days** | ❌ Not supported | ✅ `max_archive_days` configuration |
| **Log message sanitization** | ❌ Filter only accepts/rejects, never modifies | ✅ `SanitizingSink` with Aho-Corasick pattern replacement |
| **Message modification pipeline** | ❌ No message transformation capability | ✅ `FilterChain` with `LogFilter` interface |

---

## APIs Provided

### Core Namespace: `Logger_Adapter::logging`

| Function | Signature | Purpose |
|----------|-----------|---------|
| `InitializeLogging` | `bool(LoggingConfig const&)` | Thread-safe init; wires error_notifier |
| `ShutdownLogging` | `void()` | Graceful shutdown |
| `GetDefaultLogger` | `quill::Logger*()` | Root logger access |
| `GetLogger` | `quill::Logger*(const char*, LogLevel)` | Named logger + optional level override |
| `SetLogLevel` | `bool(const char*, LogLevel)` | Runtime level change with validation |
| `GetLogLevel` | `LogLevel(const char*)` | Query current level |
| `ResetLoggingForTesting` | `void()` | Test isolation (DEBUG only) |

---

### Sink Namespace: `Logger_Adapter::sinks`

| Class | Quill Gap Filled |
|-------|------------------|
| `DailyRotatingFileSink` | Daily rotation, gzip compression, day-based retention |
| `SanitizingSink` | Message sanitization via `FileEventNotifier::before_write` |
| `NullSink` | Windows null-sink (Quill's `/dev/null` doesn't work on Windows) |
| `EventLogSink` | Native Windows Event Log integration (Quill has none) |

---

### Filter Namespace: `Logger_Adapter::filters`

| Class | Quill Gap Filled |
|-------|------------------|
| `LogFilter` | Abstract interface with message modification (vs. Quill's read-only Filter) |
| `FilterChain` | Sequential filter application with reject support |
| `SanitizationLogFilter` | Wraps `SanitizationFilter` into filter chain |

---

### Config Namespace: `Logger_Adapter::config`

| Struct | AA Acceptance Criterion |
|--------|------------------------|
| `DailyRotationConfig` | AA-M02: gzip, utc, retention_days |
| `SanitizationConfig` | AA-M18: enabled, rules, binary data handling |
| `SanitizationRule` | Pattern + replacement definition |
| `SanitizationFilter` | Aho-Corasick compiled rules |
| `PatternConfig` | Format string validation (prevents runtime crashes) |

---

## Architecture: Multi-Tier Compliance

```
Layer 1 (Toolbox):     Stateless utilities (ToQuillLogLevel, MakePatternFormatter)
Layer 2 (PODs):        Configuration structs (DailyRotationConfig, SanitizationConfig)
Layer 3 (Stateful):      Sinks, filters, registry
Layer 4 (Composition):   LoggerSetup orchestration
```

---

## Build Requirements

```json
{
  "dependencies": ["quill@10.0.1", "zlib@1.3.1"]
}
```

See [vcpkg.json](vcpkg.json) for exact versions.