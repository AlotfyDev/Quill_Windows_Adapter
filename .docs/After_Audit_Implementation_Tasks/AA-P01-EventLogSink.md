# AA-P01 — Windows EventLog Sink (After-Audit Corrected — Complete Redesign)

> **Phase**: 2 (Design) + Phase 3 (Implementation) — 🔧 Redesign → 🏗️ Core  
> **Effort**: 1.5h design + 2h implementation = 3.5h total  
> **Depends on**: AA-C01 (needs named loggers for Category mapping)  
> **v1.x Reference**: TASK-P01-EventLogSink.md  
> **Audit Issues**: P01-A (stub file), P01-B (Event IDs), P01-C (source registration), P01-D (Unicode), P01-E (performance)  
> **Audit Verdict**: ❌ Fail — Requires Complete Redesign  
> **Platform Compliance Fixes Applied**: 🔧 BufferArgs→Buffer const&, 🔧 RegisterSource removed, 🔧 cached HANDLE, 🔧 atomic<bool>, 🔧 Buffer::format() API

---

## Problem

Logger_Adapter needs a Windows native EventLog sink (`ReportEventW`) as a replacement for Quill's POSIX-only `SyslogSink`. The existing stubs (`sinks/EventLogSink.cpp`, `sinks/EventLogSink.hpp`) are empty.

---

## Corrected Design

### 1. Event ID → LogLevel Mapping

| LogLevel | Event ID | Event Type | Description |
|----------|----------|------------|-------------|
| TraceL1-L3 | 0 | Information | Diagnostic tracing |
| Debug | 1 | Information | Debug output |
| Info | 2 | Information | Normal operational events |
| Warning | 3 | Warning | Recoverable issues |
| Error | 4 | Error | Non-fatal errors |
| Critical | 5 | Error | Critical failures |

### 2. Event Category Mapping

| Category ID | Name | Description |
|-------------|------|-------------|
| 1 | System | Logger infrastructure events |
| 2 | Emergency | EmergencyManager events (crash, shutdown) |
| 3 | OrderExecution | Order/trade lifecycle |
| 4 | Risk | Risk check results |
| 5 | MarketData | Market data feed status |

(Requires C01 named loggers to map logger name → category ID.)

### 3. Source Registration (Manual Only — No Runtime API)

EventLog source registration **requires admin privileges** and is done manually:

```powershell
# One-time setup (run as Administrator):
Register-EventLog -LogName Application -Source "Logger_Adapter_Trading_System"
```

**Critical**: Do NOT attempt runtime registration via `EventRegister` (that's the ETW API, unrelated to EventLog).
At runtime, if the source is not registered, `ReportEventW` returns `FALSE` — the sink gracefully drops the message.
No crash, no exception. **Documented in release notes as a manual ops step.**

### 4. Unicode (WCHAR) Conversion

Logger_Adapter uses `const char*` (UTF-8). EventLog API requires `LPCWSTR`:

```cpp
std::wstring Utf8ToWide(const char* utf8) {
    if (!utf8 || !*utf8) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring wstr(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &wstr[0], len);
    return wstr;
}
```

### 5. Performance Characteristics

```
ReportEventW latency: ~5-15 μs (kernel transition)
File logging latency: ~0.5-2 μs (userspace → kernel buffer)
```

**Critical rule**: EventLog is for ALERTS only — max 10 events/sec recommendation. Document this prominently.

---

## Corrected Implementation Plan

### Step 1 — Populate `sinks/EventLogSink.hpp`

```cpp
#pragma once
#include <quill/Sink.h>
#include <quill/MacroMetadata.h>
#include <quill/Buffer.h>
#include <string>
#include <string_view>
#include <atomic>
#include <windows.h>

namespace Logger_Adapter::sinks {

class EventLogSink : public quill::Sink {
    std::string source_name_;
    WORD category_id_;
    HANDLE event_source_;                     // cached HANDLE from constructor
    std::atomic<bool> registered_;            // thread-safe flag

public:
    explicit EventLogSink(
        std::string_view source_name = "Logger_Adapter_Trading_System",
        WORD category_id = 1                  // System category
    );

    ~EventLogSink() override;

    // Quill v10 sink API: write(MacroMetadata const&, Buffer const&)
    void write(quill::MacroMetadata const& metadata,
               quill::Buffer const& buffer) override;

private:
    static WORD LogLevelToEventType(quill::LogLevel level);
    static WORD LogLevelToEventId(quill::LogLevel level);
    static std::wstring Utf8ToWide(const char* utf8);
};

} // namespace Logger_Adapter::sinks
```

### Step 2 — Populate `sinks/EventLogSink.cpp`

#### 2a. API Verification (Pre-Implementation)

**IMPORTANT**: Before writing any `EventLogSink` code, verify the Quill v10.0.1 public API for extracting a formatted message string from `Buffer const&` inside a sink's `write()` override. Check `packages/quill.10.0.1/include/quill/Buffer.h` for:

1. `Buffer::format(MacroMetadata const&)` — preferred, if it exists and is public
2. `buffer.data()` + `buffer.size()` — if `format()` is not available, extract raw bytes
3. `quill::PatternFormatter` — last resort, re-format from metadata + buffer

**Three possible outcomes**:

| Outcome | API Used | Action |
|---------|----------|--------|
| A: `format()` exists and is public | `buffer.format(metadata)` | No change — add verification comment |
| B: `data()` + `size()` available | `std::string(static_cast<char const*>(buffer.data()), buffer.size())` | Replace format() call with data()+size() |
| C: Neither; Buffer is opaque | `quill::PatternFormatter` | Cache a formatter in the sink, call `formatter.format(metadata, buffer)` |

**Compile-time path selection**: Use a preprocessor `#if` or SFINAE trait to select the correct extraction path at compile time:

```cpp
// Example SFINAE-style detection (define before write()):
#if defined(QUILL_BUFFER_HAS_FORMAT)
    #define EXTRACT_MESSAGE(buffer, metadata) (buffer).format(metadata)
#elif defined(QUILL_BUFFER_HAS_DATA_SIZE)
    #define EXTRACT_MESSAGE(buffer, metadata) std::string( \
        static_cast<char const*>((buffer).data()), (buffer).size())
#else
    #define EXTRACT_MESSAGE(buffer, metadata) format_via_pattern_formatter(buffer, metadata)
#endif
```

> **Verification step**: Before implementation, inspect `packages/quill.10.0.1/include/quill/Buffer.h` for `format()`, `data()`, and `size()` members. Set the corresponding `#define` in a config header or build system macro.

#### 2b. Implementation (after API verification)

```cpp
#include "EventLogSink.hpp"

namespace Logger_Adapter::sinks {

EventLogSink::EventLogSink(std::string_view source_name, WORD category_id)
    : source_name_(source_name), category_id_(category_id)
    // Cached HANDLE — opened once in constructor, not per write()
    , event_source_(RegisterEventSourceA(nullptr, source_name.data()))
    , registered_(event_source_ != nullptr) {
    if (!registered_) {
        // Log diagnostics to debug output — aids ops debugging "why no EventLog entries?"
        DWORD err = GetLastError();
        std::string diag = "EventLogSink: RegisterEventSourceA failed for '" +
                           std::string(source_name) + "' (GLE=" + std::to_string(err) + ")\n";
        OutputDebugStringA(diag.c_str());
    }
}

EventLogSink::~EventLogSink() {
    if (registered_.load(std::memory_order_relaxed) && event_source_) {
        DeregisterEventSource(event_source_);
    }
}

void EventLogSink::write(quill::MacroMetadata const& metadata,
                         quill::Buffer const& buffer) {
    if (!registered_.load(std::memory_order_acquire)) return;

    // Extract formatted message — see §2a for API verification outcomes.
    // Verified against Quill v10.0.1: [OUTCOME: A/B/C — fill in after verification]
    std::string message(EXTRACT_MESSAGE(buffer, metadata));

    // Convert UTF-8 to UTF-16 (required by ReportEventW)
    std::wstring wmessage = Utf8ToWide(message.c_str());

    // Truncate to ReportEventW 32KB limit (32,768 bytes minus header overhead)
    constexpr size_t MAX_EVENT_SIZE = 32768 - 256;  // 256 bytes reserved for header
    if (wmessage.size() > MAX_EVENT_SIZE) {
        wmessage.resize(MAX_EVENT_SIZE);
        static std::atomic<bool> truncation_warned{false};
        bool expected = false;
        if (truncation_warned.compare_exchange_strong(expected, true)) {
            OutputDebugStringA("EventLogSink: message truncated to 32KB limit\n");
        }
    }

    LPCWSTR strings[1] = { wmessage.c_str() };

    ReportEventW(
        event_source_,
        LogLevelToEventType(metadata.log_level),
        category_id_,
        LogLevelToEventId(metadata.log_level),
        nullptr,          // user SID
        1,                // num strings
        0,                // raw data size
        strings,
        nullptr           // raw data
    );
}

WORD EventLogSink::LogLevelToEventType(quill::LogLevel level) {
    switch (level) {
        case quill::LogLevel::Warning:    return EVENTLOG_WARNING_TYPE;
        case quill::LogLevel::Error:
        case quill::LogLevel::Critical:   return EVENTLOG_ERROR_TYPE;
        default:                           return EVENTLOG_INFORMATION_TYPE;
    }
}

WORD EventLogSink::LogLevelToEventId(quill::LogLevel level) {
    // Explicit mapping: each Quill LogLevel enumerator → fixed Event ID.
    // Static cast from enum value is fragile — if Quill changes enum ordering,
    // Critical logs could be written as Event ID 0 (Information).
    switch (level) {
        case quill::LogLevel::TraceL1:
        case quill::LogLevel::TraceL2:
        case quill::LogLevel::TraceL3: return 0;
        case quill::LogLevel::Debug:    return 1;
        case quill::LogLevel::Info:     return 2;
        case quill::LogLevel::Warning:  return 3;
        case quill::LogLevel::Error:    return 4;
        case quill::LogLevel::Critical: return 5;
        default:                        return 0;  // unknown → Information
    }
}

std::wstring EventLogSink::Utf8ToWide(const char* utf8) {
    if (!utf8 || !*utf8) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring wstr(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &wstr[0], len);
    return wstr;
}

} // namespace Logger_Adapter::sinks
```

### 6. EventLog Source Verification Helper

Add a deployment health-check function to verify EventLog source registration at startup:

```cpp
// Returns true if EventLog source is registered and writable.
// Call during application health check / startup validation.
bool VerifyEventLogSource(const std::string& source_name) {
    HANDLE h = RegisterEventSourceA(nullptr, source_name.c_str());
    if (!h) return false;
    // Attempt a test write to verify the source works end-to-end
    LPCWSTR test_msg = L"Logger_Adapter EventLog source verification";
    BOOL ok = ReportEventW(h, EVENTLOG_INFORMATION_TYPE, 0, 0, nullptr, 1, 0, &test_msg, nullptr);
    DeregisterEventSource(h);
    return ok == TRUE;
}
```

> **Note**: This verification write produces a visible event in the Application EventLog. Document this so ops teams are not surprised by "mystery" test events.

### Step 3 — Wire into SinkFactory

```cpp
// In setup/SinkFactory.hpp
if (config.eventlog.enabled) {
    sinks.push_back(std::make_shared<EventLogSink>(
        config.eventlog.source_name,
        CategoryFromLoggerName(logger_name)
    ));
}
```

### Step 4 — Add EventLog config to LoggingConfig

```cpp
struct {
    bool enabled = false;
    std::string source_name = "Logger_Adapter_Trading_System";
} eventlog;
```

---

## Acceptance Criteria

- [ ] `EventLogSink` writes to Windows Event Viewer on Win10+ x64
- [ ] Event ID matches log level (Debug=1, Error=4, Critical=5)
- [ ] Category maps to Logger_Adapter subsystem
- [ ] Unicode messages (Arabic, Chinese, Japanese) render correctly
- [ ] Missing source registration does NOT crash — gracefully falls back to silent drop
- [ ] Source registration documented in ops manual
- [ ] Performance warning documented (alerts only, max 10/sec)
- [ ] Build succeeds Debug|x64 with zero new warnings

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/sinks/EventLogSink.hpp` | Populate from stub with full implementation |
| `Logger_Adapter/sinks/EventLogSink.cpp` | Populate from stub with `ReportEventW` logic |
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add `eventlog` config block |
| `Logger_Adapter/setup/SinkFactory.hpp` | Add `CreateEventLogSink` |