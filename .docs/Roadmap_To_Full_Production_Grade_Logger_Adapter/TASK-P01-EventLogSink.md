# P01 — Windows EventLog Sink (Syslog Replacement)

- **Priority**: 🟡 Medium
- **Est. Effort**: 2-3 hours
- **Depends on**: None

---

## Problem

Quill's `SyslogSink` is POSIX-only (sends to `/dev/log` or `syslog()`). On Windows, there is no equivalent.

For a production trading system on Windows, the Windows Event Log (`ReportEvent` API) is the standard destination for system-level diagnostic events:

```cpp
// Win32 API
HANDLE h = RegisterEventSourceW(nullptr, L"TradingSystem");
ReportEventW(h, EVENTLOG_ERROR_TYPE, 0, 0x1000, nullptr, 0, 0, nullptr, nullptr);
DeregisterEventSource(h);
```

---

## Implementation

### Step 1 — Create `EventLogSink` adapter

**File**: `Logger_Adapter/sinks/EventLogSink.hpp`

```cpp
#pragma once
#include <quill/sinks/Sink.h>
#include <string>

namespace Logger_Adapter::sinks {

class EventLogSink : public quill::Sink
{
public:
    explicit EventLogSink(std::string const& source_name);
    ~EventLogSink() override;

    void write(quill::MacroMetadata const* metadata, uint64_t timestamp,
               std::string_view thread_id, std::string_view thread_name,
               std::string_view logger_name, quill::LogLevel log_level,
               std::string_view log_message, std::string_view log_statement,
               std::string_view log_location) override;

    void flush_sink() noexcept override;

private:
    HANDLE event_log_;
    std::string source_name_;
};

} // namespace Logger_Adapter::sinks
```

### Step 2 — Implement

Map Quill `LogLevel` to EventLog event type:

| Quill Level | EventLog Type |
|---|---|
| Critical | `EVENTLOG_ERROR_TYPE` |
| Error | `EVENTLOG_ERROR_TYPE` |
| Warning | `EVENTLOG_WARNING_TYPE` |
| Info+Debug+Trace | `EVENTLOG_INFORMATION_TYPE` |

### Step 3 — Add to LoggingConfig

```cpp
struct {
    bool enabled = false;
    std::string source_name = "TradingSystem";
} eventlog;
```

### Step 4 — Wire in LoggerSetup

```cpp
if (config.eventlog.enabled) {
    auto eventlog_sink = quill::Frontend::create_or_get_sink<EventLogSink>(
        "eventlog", config.eventlog.source_name);
    sinks.push_back(eventlog_sink);
}
```

---

## Acceptance Criteria

- [ ] Messages appear in Windows Event Viewer under `Windows Logs → Application`
- [ ] Error and Critical messages show as `Error` type
- [ ] Source name is registered correctly
- [ ] Build succeeds Debug|x64
