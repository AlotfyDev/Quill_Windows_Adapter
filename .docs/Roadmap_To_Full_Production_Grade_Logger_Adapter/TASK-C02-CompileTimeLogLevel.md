# C02 — Compile-Time Log Level

- **Priority**: 🔴 Critical
- **Est. Effort**: 30 minutes
- **Depends on**: None (independent)

---

## Problem

Quill provides `QUILL_COMPILE_ACTIVE_LOG_LEVEL` which strips log statements below a threshold at compile time. Without it:

- `LOG_TRACE` and `LOG_DEBUG` calls remain in the Release binary even if never printed
- Each trace-level call still evaluates arguments and builds `MacroMetadata` (branch prediction, string computation)
- Binary size is unnecessarily large

For a trading system, Release builds should exclude all Debug/Trace level logging by default.

---

## Current Behavior

No `QUILL_COMPILE_ACTIVE_LOG_LEVEL` is defined anywhere in the solution. Quill defaults to `-1`, meaning **all levels are compiled in**:

```
// Quill default when no define is set
#if !defined(QUILL_COMPILE_ACTIVE_LOG_LEVEL)
  #define QUILL_COMPILE_ACTIVE_LOG_LEVEL -1  // ALL levels active
#endif
```

## Desired Behavior

| Configuration | `QUILL_COMPILE_ACTIVE_LOG_LEVEL` | Levels Removed |
|---|---|---|
| Debug | `QUILL_COMPILE_ACTIVE_LOG_LEVEL_DEBUG` (3) | TraceL3, TraceL2, TraceL1 |
| Release | `QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO` (4) | TraceL3, TraceL2, TraceL1, Debug |
| Profiling | `QUILL_COMPILE_ACTIVE_LOG_LEVEL_WARNING` (6) | TraceL3 → Info |

---

## Implementation Plan

### Step 1 — Add Preprocessor Definitions to vcxproj files

**File**: `Logger_Adapter/Logger_Adapter.vcxproj`
**File**: `Experimental_Console/Experimental_Console.vcxproj`
**File**: `Cplspls_To_Cross_Lang_Connector/Cplspls_To_Cross_Lang_Connector.vcxproj`

In each `ItemDefinitionGroup`, add:

```xml
<!-- Debug|x64 — only strip trace L3/L2/L1 -->
<PreprocessorDefinitions>QUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
```

```xml
<!-- Release|x64 — strip trace + debug -->
<PreprocessorDefinitions>QUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO;%(PreprocessorDefinitions)</PreprocessorDefinitions>
```

### Step 2 — Verify in HotPathLogger.hpp

No changes needed in code — Quill's existing `#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= ...` guards handle it automatically. But confirm by inspecting `LogMacros.h`:

```cpp
// line 556
#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_DEBUG
  #define QUILL_LOG_DEBUG(logger, fmt, ...) ... // real impl
#else
  #define QUILL_LOG_DEBUG(logger, fmt, ...) (void)0 // no-op
#endif
```

### Step 3 — Add documented defaults to LoggingConfig

**File**: `Logger_Adapter/logging/LoggingConfig.hpp`

Add a comment documenting the expected compile-time levels:

```cpp
/// Compile-time log level thresholds (set via preprocessor defines):
///   Debug:   QUILL_COMPILE_ACTIVE_LOG_LEVEL=3  (strip TraceL3/L2/L1)
///   Release: QUILL_COMPILE_ACTIVE_LOG_LEVEL=4  (strip TraceL3/L2/L1 + Debug)
///   Profile: QUILL_COMPILE_ACTIVE_LOG_LEVEL=6  (strip TraceL3→Info)
```

---

## Acceptance Criteria

- [ ] Debug build: `LOG_TRACE("test")` produces **zero assembly** in Release configuration (verify via dumpbin /disasm or size comparison)
- [ ] Release build: `LOG_DEBUG` and below produce `(void)0` — binary size decreases measurably
- [ ] All projects compile without new warnings
- [ ] `Experimental_Console.exe` still prints `LOG_INFO` messages in Release

---

## Verification

```powershell
# Debug build — trace should work
msbuild /p:Configuration=Debug /p:Platform=x64 /v:minimal
.\x64\Debug\Experimental_Console.exe | findstr "Hello"  # should print

# Release build — trace should be stripped
msbuild /p:Configuration=Release /p:Platform=x64 /v:minimal
.\x64\Release\Experimental_Console.exe | findstr "Hello"  # should NOT print
```
