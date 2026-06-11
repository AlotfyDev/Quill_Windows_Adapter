# C01 — Multi-Logger Shim

- **Priority**: 🔴 Critical
- **Est. Effort**: 3-4 hours
- **Depends on**: Current LoggerSetup.hpp structure

---

## Problem

`Logger_Adapter::logging::GetLogger(char const* name, quill::LogLevel level)` completely **ignores both parameters**. Every subsystem — Emergency, OrderExecution, Risk, MarketData — shares the same root logger with a single log level and a single sink list.

In a trading system this means:
- If you set `LOG_LEVEL(Error)` to reduce noise, you lose *all* Info/Debug from every subsystem
- If you add a file sink to capture OrderExecution logs, it also captures HealthProbe heartbeats
- No way to route different subsystems to different outputs

---

## Current Behavior

```cpp
// LoggerSetup.hpp:106-109
inline Logger_Adapter::logging::LogLevel* GetLogger(char const*, quill::LogLevel)
{
    return root_logger_ptr(); // always root, ignores args
}
```

## Desired Behavior

```cpp
// After fix:
auto* em_log    = GetLogger("Emergency",     LogLevel::Info,    {console_sink});
auto* order_log = GetLogger("OrderExecution", LogLevel::Debug,  {file_sink, console_sink});
auto* risk_log  = GetLogger("Risk",           LogLevel::Warning,{file_sink});
```

---

## Implementation Plan

### Step 1 — LoggingConfig extension
**File**: `Logger_Adapter/logging/LoggingConfig.hpp`

Add a `LoggerEntry` struct and optional `loggers` array to `LoggingConfig`:

```cpp
struct LoggerEntry {
    std::string name = "default";
    int log_level = 3; // Debug
    bool console_enabled = true;
    bool file_enabled = false;
    bool json_enabled = false;
    // optional pattern override
    // optional sink list
};

struct LoggingConfig {
    // ... existing fields ...
    bool enable_named_loggers = true;
    std::vector<LoggerEntry> loggers = {
        {"Emergency",     3, true,  true,  true},
        {"OrderExecution", 3, true,  true,  false},
        {"Risk",           4, true,  true,  false},
        {"MarketData",     3, true,  false, false},
        {"HealthProbe",    4, true,  false, false}
    };
};
```

### Step 2 — Named Logger creation in InitializeLogging
**File**: `Logger_Adapter/logging/LoggerSetup.hpp`

After creating the common sinks and the root logger, iterate `config.loggers`:

```cpp
for (auto const& entry : config.loggers) {
    std::vector<std::shared_ptr<Sink>> logger_sinks;
    if (entry.console_enabled) logger_sinks.push_back(console_sink);
    if (entry.file_enabled && file_sink) logger_sinks.push_back(file_sink);
    if (entry.json_enabled && json_sink) logger_sinks.push_back(json_sink);

    auto* logger = quill::Frontend::create_or_get_logger(entry.name, logger_sinks);
    logger->set_log_level(ToQuillLogLevel(entry.log_level));
}
```

### Step 3 — Update GetLogger
**File**: `Logger_Adapter/logging/LoggerSetup.hpp`

```cpp
inline quill::Logger* GetLogger(char const* name, quill::LogLevel level = quill::LogLevel::Info)
{
    auto* logger = quill::Frontend::get_logger(name);
    if (!logger) {
        // fallback to root
        logger = root_logger_ptr();
    }
    if (level != quill::LogLevel::None) {
        logger->set_log_level(level);
    }
    return logger;
}
```

### Step 4 — Update Emergency subsystem to use emergency logger
**File**: `Logger_Adapter/emergency/EmergencyManager.hpp`, `GracefulShutdown.hpp`

Replace hardcoded `quill::Frontend::get_logger("emergency")` with `GetLogger(config_.emergency_logger_name)`.

### Step 5 — Update Experimental_Console to exercise named loggers
**File**: `Experimental_Console/Experimental_Console.cpp`

```cpp
auto* em_log = GetLogger("Emergency");
LOG_INFO(em_log, "Emergency logger online");

auto* order_log = GetLogger("OrderExecution");
LOG_DEBUG(order_log, "Processing order 12345");
```

---

## Acceptance Criteria

- [ ] `GetLogger("OrderExecution")` returns a **different** `quill::Logger*` than `GetLogger("Risk")`
- [ ] Setting `Risk` logger to `Warning` does not suppress `OrderExecution`'s `Debug` messages
- [ ] Adding a file sink to `Emergency` logger does not write `MarketData` messages to the same file
- [ ] Backward compatible: `GetDefaultLogger()` still returns the root logger
- [ ] `InitializeLogging` with empty `loggers` list creates only the root logger (no behavioral change)
- [ ] Build succeeds Debug|x64 with zero warnings (excluding MSB4011)

---

## Testing

```
Experimental_Console.exe
  ├── [CONSOLE] Info: Emergency logger online       ← Green
  ├── [CONSOLE] Debug: Processing order 12345       ← White
  ├── [CONSOLE] Debug: MarketData tick AAPL 150.25  ← White
  ├── [FILE]    Warning: Risk check failed          ← logs/assembler.log only
  └── [FILE]    Info: Emergency: signal SIGTERM     ← logs/emergency.log + console
```
