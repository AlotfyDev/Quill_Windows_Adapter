# AA-C02 — Compile-Time Log Level (After-Audit Corrected)

> **Phase**: 1 — ⚙️ Foundation  
> **Effort**: 30 min  
> **Depends on**: AA-M12, AA-0.5  
> **v1.x Reference**: TASK-C02-CompileTimeLogLevel.md  
> **Audit Issues**: C02-A (all 4 configs), C02-B (all .vcxproj files), C02-C (CI validation)

---

## Problem

`QUILL_COMPILE_ACTIVE_LOG_LEVEL` is not defined anywhere. Every log level (TraceL3 through Critical) compiles into Release binaries, wasting CPU cycles, inflating binary size, and adding branch prediction pressure.

---

## Corrected Implementation Plan

### Step 0 — Prerequisite: Ensure All 4 Configurations Exist

Before adding preprocessor definitions, verify that each `.vcxproj` has `ItemDefinitionGroup` blocks for all four configurations: **Debug**, **Release**, **RelWithDebInfo**, and **MinSizeRel**. If any configuration is missing (e.g., RelWithDebInfo and MinSizeRel are not present by default in some projects), add them first:

```xml
<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='RelWithDebInfo|x64'">
  <ClCompile>
    <Optimization>MaxSpeed</Optimization>
    <WholeProgramOptimization>true</WholeProgramOptimization>
  </ClCompile>
</ItemDefinitionGroup>
<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='MinSizeRel|x64'">
  <ClCompile>
    <Optimization>MinSpace</Optimization>
    <WholeProgramOptimization>true</WholeProgramOptimization>
  </ClCompile>
</ItemDefinitionGroup>
```

### Step 1 — Define for All 4 MSBuild Configurations

In **every** `.vcxproj` file that includes Logger_Adapter headers, including **test project vcxproj files**:

```xml
<!-- Logger_Adapter.vcxproj, Experimental_Console.vcxproj, Cplspls_To_Cross_Lang_Connector.vcxproj -->
<ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
  <ClCompile>
    <PreprocessorDefinitions>QUILL_COMPILE_ACTIVE_LOG_LEVEL=3;%(PreprocessorDefinitions)</PreprocessorDefinitions>
  </ClCompile>
</ItemDefinitionGroup>
<ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
  <ClCompile>
    <PreprocessorDefinitions>QUILL_COMPILE_ACTIVE_LOG_LEVEL=4;%(PreprocessorDefinitions)</PreprocessorDefinitions>
  </ClCompile>
</ItemDefinitionGroup>
<ItemDefinitionGroup Condition="'$(Configuration)'=='RelWithDebInfo'">
  <ClCompile>
    <PreprocessorDefinitions>QUILL_COMPILE_ACTIVE_LOG_LEVEL=4;%(PreprocessorDefinitions)</PreprocessorDefinitions>
  </ClCompile>
</ItemDefinitionGroup>
<ItemDefinitionGroup Condition="'$(Configuration)'=='MinSizeRel'">
  <ClCompile>
    <PreprocessorDefinitions>QUILL_COMPILE_ACTIVE_LOG_LEVEL=5;%(PreprocessorDefinitions)</PreprocessorDefinitions>
  </ClCompile>
</ItemDefinitionGroup>
```

**Level mapping**: Quill uses `QUILL_ACTIVE_LOG_LEVEL_*` constants internally:
- 3 = `QUILL_ACTIVE_LOG_LEVEL_DEBUG` (strips TraceL1-L3)
- 4 = `QUILL_ACTIVE_LOG_LEVEL_INFO` (strips Trace + Debug)
- 5 = `QUILL_ACTIVE_LOG_LEVEL_WARNING` (strips Trace + Debug + Info)

**Note for test projects**: The test vcxproj must also define `QUILL_COMPILE_ACTIVE_LOG_LEVEL`. Without it, Release tests run with all log levels enabled (no stripping), making test performance unrepresentative of production. Add the same 4-configuration blocks to any test project that compiles Logger_Adapter headers.

### Rationale: RelWithDebInfo = 4 (Info Level)

RelWithDebInfo is used for profiling. The choice between level 3 (Debug) and level 4 (Info) involves a trade-off:

| Level | Strips | Binary Size | Profile Accuracy | Visibility |
|-------|--------|-------------|------------------|------------|
| 3 (Debug) | TraceL1-L3 | Larger | Less representative (Debug branches inflate I-cache pressure) | Debug logs visible during profiling |
| 4 (Info) | Trace + Debug | Smaller | More representative production profile | Debug logs hidden |

**Chosen: 4 (Info)** — The priority is measuring production-realistic performance. Debug-level log statements are rarely hit during profiling (they guard infrequent events). If a specific profiling session requires Debug visibility, temporarily override to `QUILL_COMPILE_ACTIVE_LOG_LEVEL=3` in the vcxproj or pass as a compiler flag.

**Future work**: Consider making this a vcxproj property so developers can override without editing the file:
```xml
<QUILL_COMPILE_ACTIVE_LOG_LEVEL Condition="'$(QuillLogLevel)'!=''">$(QuillLogLevel)</QUILL_COMPILE_ACTIVE_LOG_LEVEL>
```

### Mapping from TASK-C02: Profiling → MinSizeRel

The original task referenced a custom "Profiling" configuration (level 6 = Warning). This AA replaces it with the MSBuild-standard **MinSizeRel** configuration:

| Original (TASK-C02) | Current (AA-C02) | Reason |
|---------------------|-------------------|--------|
| Profiling = 6 | MinSizeRel = 5 | "Profiling" is not an MSBuild standard config. MinSizeRel is the closest standard equivalent. Level 5 (Warning) strips Trace, Debug, and Info. This is _more aggressive_ than the original's level 6 (which only strips Trace + Debug + Info), producing a smaller binary. Level 5 is correct for MinSizeRel's goal: smallest possible binary. |

**Value difference**: TASK-C02 used Quill's symbolic constant `QUILL_ACTIVE_LOG_LEVEL_WARNING`, which = 6 in some Quill versions. AA-C02 uses the numeric value 5, which corresponds to `QUILL_ACTIVE_LOG_LEVEL_WARNING` in Quill v10.0.x. The numeric value changed due to Quill version differences, not a semantic change. The behavior is identical: strip Trace, Debug, and Info; keep Warning, Error, Critical.

### Step 2 — Document in pch.h

Add a comment in `Logger_Adapter/pch.h`:

```cpp
// Compile-time log level is defined per-configuration in .vcxproj files:
//   Debug=3, Release=4, RelWithDebInfo=4, MinSizeRel=5
// See AA-C02 for details.
#ifndef QUILL_COMPILE_ACTIVE_LOG_LEVEL
#define QUILL_COMPILE_ACTIVE_LOG_LEVEL 3  // safe default for Debug
#endif
```

### Step 3 — Add CI Validation

**Primary check (precise)**: Use `dumpbin /DISASM` or compile a test file with `#pragma message("LOG_LEVEL=" #QUILL_COMPILE_ACTIVE_LOG_LEVEL)` to emit the active level at compile time. Parse the build log for the expected value per configuration. This verifies the correct define is applied without relying on binary size heuristics.

**Secondary check (advisory)**: Binary size heuristic — verify Release binary is at least 20% smaller than Debug binary. This is a heuristic only, not a guarantee:

- **Dependency churn** can inflate Release binaries without log-level issues (e.g., Quill adds a new sink implementation)
- **Compiler optimizations** affect Debug vs Release ratio differently across MSVC versions
- **Linker settings** (e.g., /LTCG, /GL) change the ratio independently of log level

**If the heuristic check triggers unexpectedly**:
1. Verify the binary was built with the correct `QUILL_COMPILE_ACTIVE_LOG_LEVEL`
2. Check if external dependencies changed since the last baseline
3. Consider recomputing the threshold: measure Release/Debug ratio with known-good builds over 3-5 CI runs, then set threshold to `max_ratio * 1.10`

```powershell
# Primary check: compile-time log level verification
# Build a test file that emits the active level via #pragma message
# Example test TU: #pragma message("QUILL_COMPILE_ACTIVE_LOG_LEVEL=" #QUILL_COMPILE_ACTIVE_LOG_LEVEL)
msbuild /p:Configuration=Release /p:Platform=x64 2>&1 | Select-String "QUILL_COMPILE_ACTIVE_LOG_LEVEL=4"
if ($LASTEXITCODE -ne 0) { Write-Error "Release build should define level 4"; exit 1 }

# Secondary check: binary size heuristic (advisory)
$binary = "x64\Release\Experimental_Console.exe"
if (Test-Path $binary) {
    $debugSize = (Get-Item "x64\Debug\Experimental_Console.exe").Length
    $releaseSize = (Get-Item $binary).Length
    if ($releaseSize -ge ($debugSize * 0.8)) {
        Write-Warning "Release binary suspiciously large ($releaseSize vs Debug $debugSize) — check QUILL_COMPILE_ACTIVE_LOG_LEVEL"
    }
}
```

---

## Acceptance Criteria

- [ ] `QUILL_COMPILE_ACTIVE_LOG_LEVEL=3` defined for Debug configuration
- [ ] `QUILL_COMPILE_ACTIVE_LOG_LEVEL=4` defined for Release configuration
- [ ] `QUILL_COMPILE_ACTIVE_LOG_LEVEL=4` defined for RelWithDebInfo configuration
- [ ] `QUILL_COMPILE_ACTIVE_LOG_LEVEL=5` defined for MinSizeRel configuration
- [ ] Consumer contract documented: "External projects consuming Logger_Adapter must define `QUILL_COMPILE_ACTIVE_LOG_LEVEL` in their own build system. The pch.h fallback (level 3) is a safety net for Debug builds; Release consumers should explicitly set their desired level."
- [ ] `#pragma message` or `static_assert` added to warn if level is not explicitly set by consumer
- [ ] All `.vcxproj` files updated (Logger_Adapter, Experimental_Console, Cplspls_To_Cross_Lang_Connector, plus any test vcxproj files)
- [ ] Documented in `pch.h`
- [ ] Release binary is measurably smaller than Debug binary
- [ ] Build succeeds with zero warnings

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/Logger_Adapter.vcxproj` | Add 4 PreprocessorDefinitions blocks |
| `Experimental_Console/Experimental_Console.vcxproj` | Add 4 PreprocessorDefinitions blocks |
| `Cplspls_To_Cross_Lang_Connector/Cplspls_To_Cross_Lang_Connector.vcxproj` | Add 4 PreprocessorDefinitions blocks |
| `Logger_Adapter/pch.h` | Add documentation comment |