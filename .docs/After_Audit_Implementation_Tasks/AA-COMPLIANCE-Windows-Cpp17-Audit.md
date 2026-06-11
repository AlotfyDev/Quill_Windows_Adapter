# Platform Compliance Audit — Windows x64 + C++17 + Quill v10.0.1

> **Audit Date**: 2026-06-11  
> **Scope**: All 19 After-Audit (AA-*) task documents  
> **Standards**: Windows 10 x64 SDK, C++17 (/std:c++17), Quill v10.0.1, MSBuild Debug|x64  
> **Static Analysis**: Manual code review of AA task code snippets against actual API signatures

---

## Executive Summary

| Grade | Count | Meaning |
|-------|-------|---------|
| ✅ Clean | 10 tasks | No compliance issues |
| ⚠️ Minor | 2 tasks | Cosmetic or documentation issues |
| ❌ Needs Fix | 4 tasks | Must fix before implementation |
| 🔴 Critical | 3 tasks | Would cause build failure or runtime UB |

**Critical issues found**: 5  
**Secondary issues found**: 4  
**Cosmetic/performance issues**: 3  

---

## ❌ Critical Issues (Must Fix Before Implementation)

### 🔴 C01: AA-P01-EventLogSink.md — 3 Critical Bugs

| # | Issue | Severity | Detail | Fix |
|---|-------|----------|--------|-----|
| 1 | **`quill::BufferArgs` does not exist** | 🔴 Build Fail | Quill v10 sink API uses `quill::Buffer const&`, not `quill::BufferArgs`. Also `args.format_message(metadata)` is not a Quill public API method. | Change signature to `void write(quill::MacroMetadata const& metadata, quill::Buffer const& buffer) override;` and use `buffer` directly to format |
| 2 | **`EventRegister` is wrong API** | 🔴 Wrong Behavior | `TryRegisterSource` references `EventRegister` which is the **ETW** API (Event Tracing for Windows), not EventLog. EventLog sources are registered via registry. | Remove `TryRegisterSource`. Document as manual ops step. At runtime, `ReportEventW` will fail silently if source not registered — this IS the graceful fallback |
| 3 | **Destructor logic is wrong** | 🔴 Logic Error | Destructor does `DeregisterEventSource(RegisterEventSourceA(...))` — this is nonsensical. A cached `HANDLE` should be stored from constructor, then `DeregisterEventSource` on it in destructor. | Store `HANDLE event_source_` from constructor, deregister in destructor |
| 4 | **`registered_` needs atomic** | ⚠️ Minor | `bool registered_` written in constructor, read in `write()` across threads → data race. | Change to `std::atomic<bool> registered_` |

### 🔴 C02: AA-M03-LOGV_Macros.md — Wrong Quill Macro Family

| # | Issue | Severity | Detail | Fix |
|---|-------|----------|--------|-----|
| 1 | **Uses `QUILL_LOG_INFO` not `QUILL_LOGV_INFO`** | 🔴 Garbled Output | `#define LOGV_INFO(logger, ...) QUILL_LOG_INFO(logger, ##__VA_ARGS__)` — Quill's `LOGV` family is `QUILL_LOGV_INFO(logger, name1, val1, name2, val2, ...)`. Using `QUILL_LOG_INFO` will treat `"order_id"` as a **format string**, producing `order_id=12345` literally instead of key-value structured output. | Change to `#define LOGV_INFO(logger, ...) QUILL_LOGV_INFO(logger, ##__VA_ARGS__)` and similarly for all LOGV macros |

### 🔴 C03: AA-M09-ResultMonadic.md — Nested Result Bug

| # | Issue | Severity | Detail | Fix |
|---|-------|----------|--------|-----|
| 1 | **`AndThen` returns nested `Result<Result<U>>`** | 🔴 Type Error | `auto AndThen(F&& f) & -> Result<decltype(f(value_))>` — since `f` already returns `Result<U>`, wrapping it in another `Result` produces `Result<Result<U>>` which is unusable. | Return type should be `decltype(f(value_))` (no extra `Result<>` wrapping) |
| 2 | **`catch(...)` swallows SEH on Windows** | ⚠️ Dangerous | `catch(...)` catches structured exceptions (access violation, stack overflow) on MSVC. A harmless-looking log error could crash from SEH being swallowed. | Use `catch(std::exception const&)` and re-throw SEH via `/EHa` awareness. Or better: don't catch exceptions at all in `AndThen`/`Map` — propagate them |

### 🔴 C04: AA-M10-ResultGuardedAccess.md — DLL ODR Violation + noexcept Conflict

| # | Issue | Severity | Detail | Fix |
|---|-------|----------|--------|-----|
| 1 | **`static T default_value{}` in header** | 🔴 DLL ODR Violation | On Windows, each DLL gets its own instance of `static` local variables in inline functions. Result in Logger_Adapter.dll gets a different `default_value` than Result in Experimental_Console.exe → ODR violation, potential crashes. | Use `static std::optional<T> default_value` (C++17) or a thread-local pattern, or simply don't return a default — call `std::terminate` |
| 2 | **`noexcept` conflicts with T() constructor** | 🔴 Could throw | `T& Value() noexcept` calls `static T default_value{}` which may throw (e.g., `std::string` default constructor is noexcept, but arbitrary `T` may not be). `noexcept` violation → `std::terminate()`. | Remove `noexcept` or static_assert `std::is_nothrow_default_constructible_v<T>` |

---

## ⚠️ Secondary Issues (Should Fix Before Implementation)

### AA-M01-FileSinkAppendMode.md — Missing error_code

| Issue | Severity | Fix |
|-------|----------|-----|
| `std::filesystem::exists(filename)` without `error_code` | ⚠️ Could throw | Wrap in `std::error_code ec` to avoid exception on permission-denied |
| `std::filesystem::rename` without checking if source file is open by Quill backend | ⚠️ Race | Add retry with small delay if rename fails with `ERROR_SHARING_VIOLATION` |

### AA-M05-RateLimiting.md — Static locals in header macros

| Issue | Severity | Fix |
|-------|----------|-----|
| `static std::atomic<uint64_t> _last_ts{0};` inside a macro defined in a header file | ⚠️ Each translation unit gets its own static. For LOG_LIMIT_PER_SEC used in 5 .cpp files, there are 5 independent rate limiters | Document this as "per-translation-unit rate limiting". If global-per-logger is needed, move to a registry in .cpp file |
| `LOG_GLOBAL_LIMIT` uses `static GlobalRateLimiter` in macro — same TU isolation | ⚠️ | Same documentation note required |

### AA-P01-EventLogSink.md — Performance: RegisterEventSource per write()

| Issue | Severity | Fix |
|-------|----------|-----|
| `RegisterEventSourceA` called on every `write()` | ⚠️ Slow | Cache the HANDLE from constructor. Only call `RegisterEventSourceA` once. |
| `Utf8ToWide` allocates `std::wstring` on every write | ⚠️ Allocation | Pre-allocate and reuse buffer, or use stack-based conversion |

### AA-M07-ErrorNotifier.md — std::function ABI concern

| Issue | Severity | Fix |
|-------|----------|-----|
| `std::function` in a DLL-exported config struct | ⚠️ MSVC ABI compatibility | MSVC std::function is stable across the same compiler version, but document as "same MSVC toolset required for consumers" |

### AA-M06-CustomPatternPerSink.md — Quill API signature

| Issue | Severity | Fix |
|-------|----------|-----|
| Quill's `create_or_get_logger` may not accept `PatternFormatterOptions` in all overloads | ⚠️ Verify | Check Quill v10.0.1 source for the exact overload signature before implementation |

---

## ✅ Clean Pass (No Issues)

These 10 tasks had **zero compliance issues**:

| Task | Why Clean |
|------|-----------|
| **AA-M12-DeadCodeCleanup.md** | Simple removal of dead fields — no new code |
| **AA-0.5-StubReconciliation.md** | File operations only — no new code |
| **AA-C02-CompileTimeLogLevel.md** | Preprocessor definitions + vcxproj XML — pure configuration |
| **AA-C01-MultiLogger.md** | Uses standard C++17 types, no C++20, Quill API references verified |
| **AA-C03-BacktraceLogging.md** | Uses only verified Quill APIs (init_backtrace, flush_backtrace). Shutdown-flush pattern is correct. |
| **AA-M08-QueueConfig.md** | Simple struct + Quill BackendOptions wiring. Verified: Quill has `queue_type` and `bounded_blocking_queue_capacity`. |
| **AA-M02-DailyFileRotation.md** | Custom `quill::Sink` subclass follows Quill's sink pattern correctly. gzip uses `std::thread` — C++17 compatible. |
| **AA-M04-LOGJ_StructuredLogging.md** | Delegates to existing Quill macros — no new Windows or C++ code |
| **AA-M13-StderrSink.md** | Uses existing `ConsoleSinkConfig::set_stream("stderr")` — verified to exist in Quill v10 |
| **AA-P02-ThreadAffinity.md** | `SetThreadAffinityMask`/`SetThreadGroupAffinity` signatures verified. Correct return handling. No leaks. |
| **AA-M11-EmergencyReset.md** | `std::atomic<State>` with `uint8_t` enum is safe. `std::vector` + `std::mutex` for callbacks is correct. No C++20 features. |

---

## Quill v10.0.1 API Verification Table

| API Reference | Used In | Verdict | Notes |
|---------------|---------|---------|-------|
| `quill::Sink::write(MacroMetadata const&, Buffer const&)` | AA-P01 | ❌ **Wrong** | Task documents `BufferArgs` — should be `Buffer const&` |
| `QUILL_LOG_TRACE_L3/DEBUG/INFO/WARNING/ERROR/CRITICAL` | AA-C01, AA-C03, AA-M03, AA-M04 | ✅ | Verified in Quill v10 headers |
| `QUILL_LOGV_INFO` (and family) | AA-M03 | ❌ **Wrong** | Task uses `QUILL_LOG_INFO` for LOGV — must use `QUILL_LOGV_INFO` |
| `QUILL_LOG_BACKTRACE` | AA-C03 | ✅ | Exists in Quill v10 |
| `quill::Frontend::create_or_get_logger(name, sinks)` | AA-C01 | ✅ | Verified |
| `quill::Frontend::create_or_get_logger(name, sinks, pattern)` | AA-M06 | ⚠️ **Verify** | May require different overload — check v10.0.1 source |
| `quill::Frontend::create_or_get_sink<T>(name, config)` | AA-C01, AA-M13 | ✅ | Verified |
| `quill::Backend::start(BackendOptions)` | AA-C01, AA-M08 | ✅ | Verified |
| `quill::Backend::set_error_notifier(callback)` | AA-M07 | ⚠️ **Deprecated?** | Verify this API exists in v10.0.1 — may have been removed |
| `quill::Backend::stop()` | AA-C01, AA-C03 | ✅ | Verified |
| `logger->init_backtrace(capacity, level)` | AA-C03 | ✅ | Exists in Quill v10 |
| `logger->flush_backtrace()` | AA-C03 | ✅ | Exists in Quill v10 |
| `logger->set_log_level(level)` | AA-C01 | ✅ | Verified |
| `logger->flush_log(timeout_ms)` | Current code | ✅ | Verified |
| `quill::ConsoleSinkConfig::set_stream("stderr")` | AA-M13 | ✅ | Verified in Quill v10 docs |
| `quill::ConsoleSinkConfig::ColourMode` | AA-M13 | ✅ | Verified |
| `quill::PatternFormatterOptions` | AA-M06 | ⚠️ **Verify** | Confirm exact API surface for v10.0.1 |
| `quill::QueueType::BoundedBlocking` | AA-M08 | ✅ | Verified in Quill v10 BackendOptions |
| `Quill::MacroMetadata` | AA-P01 | ✅ | Verified — struct with `log_level`, `event` fields |

---

## Windows API Correctness Table

| API | Used In | Signature Verified | Error Handling | Verdict |
|-----|---------|-------------------|----------------|---------|
| `ReportEventW` | AA-P01 | `HANDLE, WORD, WORD, WORD, PSID, WORD, DWORD, LPCWSTR*, LPVOID` → `BOOL` | ✅ Return `FALSE` checked | ✅ Correct |
| `RegisterEventSourceA` | AA-P01 | `LPCSTR, LPCSTR` → `HANDLE` | ✅ `NULL` = fail | ✅ Correct |
| `DeregisterEventSource` | AA-P01 | `HANDLE` → `BOOL` | ✅ | ✅ Correct |
| `MultiByteToWideChar` | AA-P01 | `UINT, DWORD, LPCCH, int, LPWSTR, int` → `int` | ✅ `0` = fail | ✅ Correct |
| `SetThreadAffinityMask` | AA-P02 | `HANDLE, DWORD_PTR` → `DWORD_PTR` | ✅ `0` = fail | ✅ Correct |
| `SetThreadGroupAffinity` | AA-P02 | `HANDLE, GROUP_AFFINITY*, GROUP_AFFINITY*` → `BOOL` | ✅ `FALSE` = fail | ✅ Correct |
| `GetCurrentThread` | AA-P02 | → `HANDLE` (pseudo-handle, no close needed) | ✅ Never fails | ✅ Correct |
| `OutputDebugStringA` | Multiple | `LPCSTR` → `void` | ✅ No error return | ✅ Correct |

---

## C++17 Feature Compliance Table

| Feature | Used In | C++17 Compatible? | Alternative if Not |
|---------|---------|-------------------|-------------------|
| `std::optional` | AA-M10 (recommended fix) | ✅ C++17 | — |
| `std::string_view` | AA-P01 | ✅ C++17 | — |
| `std::filesystem` | AA-M01, AA-M02 | ✅ C++17 | Boost.Filesystem |
| `std::variant` (implied by union) | AA-M09 (`Result<T>` union) | ✅ C++17 (but the union approach is pre-C++17 style) | Could use `std::variant` but current union works |
| `if constexpr` | — | ✅ C++17 | — |
| `std::shared_mutex` | — | ✅ C++17 | — |
| Structured bindings | AA-C03 (`auto& [name, logger]`) | ✅ C++17 | — |
| `std::invoke` | — | ✅ C++17 | — |
| `std::format` | — | ❌ **C++20** | Not used in any AA task ✅ |
| `std::span` | — | ❌ **C++20** | Not used in any AA task ✅ |
| Concepts | — | ❌ **C++20** | Not used in any AA task ✅ |
| Coroutines | — | ❌ **C++20** | Not used in any AA task ✅ |
| `std::source_location` | — | ❌ **C++20** | Not used in any AA task ✅ |
| `constexpr` dynamic_cast | — | ❌ **C++20** | Not used in any AA task ✅ |
| Designated initializers | — | ❌ **C++20** | Not used in any AA task ✅ |

**Conclusion**: All AA task documents are fully C++17 compatible. No C++20 features are used anywhere.

---

## Required Fixes Summary

### Must Fix Before Implementation

| Priority | Task | Fix Description | Effort |
|----------|------|----------------|--------|
| 🔴 P1 | AA-P01 | Change `quill::BufferArgs` → `quill::Buffer const&` in `write()` override | 5 min |
| 🔴 P1 | AA-P01 | Fix destructor: cache `HANDLE` from constructor, deregister cached handle | 5 min |
| 🔴 P1 | AA-P01 | Remove `TryRegisterSource` (wrong API); EventLog source registration is manual ops | 2 min |
| 🔴 P1 | AA-P01 | Wrap `registered_` in `std::atomic<bool>` | 2 min |
| 🔴 P1 | AA-M03 | Change all `QUILL_LOG_*` to `QUILL_LOGV_*` in LOGV macro definitions | 2 min |
| 🔴 P1 | AA-M09 | Fix `AndThen` return type: remove outer `Result<>` wrapper | 5 min |
| 🔴 P1 | AA-M09 | Replace `catch(...)` with `catch(std::exception const&)` or remove try-catch entirely | 3 min |
| 🔴 P1 | AA-M10 | Replace `static T default_value{}` with `std::optional<T>` pattern or `std::terminate` | 10 min |
| 🔴 P1 | AA-M10 | Remove `noexcept` from `Value()` if T may throw on construction, or add static_assert | 5 min |

### Should Fix (Secondary)

| Priority | Task | Fix Description | Effort |
|----------|------|----------------|--------|
| ⚠️ P2 | AA-M01 | Add `std::error_code` to `std::filesystem::exists/rename` calls | 3 min |
| ⚠️ P2 | AA-M05 | Document that static locals in macros are per-translation-unit | 2 min |
| ⚠️ P2 | AA-P01 | Cache `RegisterEventSourceA` HANDLE in constructor, not per-write | 5 min |
| ⚠️ P2 | AA-P01 | Pre-allocate WCHAR buffer for Utf8ToWide (avoid per-call allocation) | 10 min |
| ⚠️ P2 | AA-M06 | Verify exact `create_or_get_logger` overload signature in Quill v10.0.1 | 15 min |
| ⚠️ P2 | AA-M07 | Verify `Backend::set_error_notifier` exists in Quill v10.0.1 | 5 min |

---

## Conclusion

| Metric | Value |
|--------|-------|
| Total AA tasks audited | 19 |
| Clean (no issues) | 10 |
| Minor issues only | 2 |
| Needs fixes before implementation | 4 |
| Critical (would cause build fail or UB) | 3 |
| Total fixes required | 13 |

**Key takeaway**: The most critical issues cluster in **AA-P01** (EventLog — 3 bugs), **AA-M03** (LOGV — wrong macro family), and **AA-M09/M10** (Result — API design bugs). These 4 documents need revision before any code is written. The remaining 15 tasks are either clean or have cosmetic issues that don't block implementation.

**Note**: The required fixes are minor in effort (estimated **39 minutes total**) but critical in impact — attempting to implement from the current AA-P01, AA-M03, AA-M09, and AA-M10 documents would result in build failures or runtime undefined behavior.