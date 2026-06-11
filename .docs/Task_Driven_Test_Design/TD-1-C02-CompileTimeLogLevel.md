# Test Design: Compile-Time Log Level Stripping

## Under Spec
- AA File: `AA-C02-CompileTimeLogLevel.md`
- Phase: 1
- Key Requirements:
  - Define `QUILL_COMPILE_ACTIVE_LOG_LEVEL` in all `.vcxproj` files (Logger_Adapter, Experimental_Console, Cplspls_To_Cross_Lang_Connector, plus test projects) for all 4 MSBuild configurations: Debug=3, Release=4, RelWithDebInfo=4, MinSizeRel=5
  - Prerequisite: All vcxproj files must have all 4 configuration `ItemDefinitionGroup` blocks (RelWithDebInfo and MinSizeRel added if absent)
  - Document the defines in `Logger_Adapter/pch.h` with a safe default fallback (`#ifndef QUILL_COMPILE_ACTIVE_LOG_LEVEL -> #define 3`)
  - Primary CI validation: compile-time `#pragma message` check emits the active level; build log is parsed for expected value per configuration
  - Secondary CI validation (advisory): Release binary size heuristic (≥20% smaller than Debug)
  - Consumer contract documented: "External consumers must define `QUILL_COMPILE_ACTIVE_LOG_LEVEL` in their own build system; pch.h fallback is Debug-only safety net"
  - Test project vcxproj files also define `QUILL_COMPILE_ACTIVE_LOG_LEVEL` for representative Release test performance
  - Build succeeds with zero new warnings
  - Level 3 strips TraceL1-L3; Level 4 strips Trace+Debug; Level 5 strips Trace+Debug+Info

## Test Harness
- **Fixture**: A build-system-level test, not a runtime test. Verify by compiling a known set of log statements at each level and checking which survive into the object code. Because this involves different compiler flags, each level is tested in a separate build invocation. Use MSBuild from command line: `msbuild /p:Configuration=<Config> /p:Platform=x64`.
- **Mock vs Real**: No mocking. Real MSBuild, real MSVC compiler, real Quill v10.0.1 headers. The test compiles a small translation unit that logs at every level (TraceL3 through Critical) and uses a post-build step (or `dumpbin /DISASM`) to inspect which log calls survive.
- **Precondition Requirements**: All 3 `.vcxproj` files updated with the 4 `PreprocessorDefinitions` blocks. `pch.h` updated. AA-M12 and AA-0.5 completed (dead code removed, stubs reconciled). MSBuild available on PATH. Visual Studio 2022 (v143 toolset).

## Scenarios

### Positive Cases
- **Debug=3 strips TraceL1-L3**: Build Debug|x64. Compile a test file that calls `LOG_TRACE_L3`, `LOG_TRACE_L2`, `LOG_TRACE_L1`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`, `LOG_CRITICAL`. The TraceL1-L3 calls are compiled to no-ops (zero instructions in the object file). Debug through Critical survive.
- **Release=4 strips Trace+Debug**: Build Release|x64. TraceL1-L3 and Debug are no-ops. Info through Critical survive.
- **RelWithDebInfo=4 strips Trace+Debug**: Build RelWithDebInfo|x64. Same as Release — Trace+Debug stripped.
- **MinSizeRel=5 strips Trace+Debug+Info**: Build MinSizeRel|x64. TraceL1-L3, Debug, Info are no-ops. Warning through Critical survive.
- **All 3 vcxproj files updated**: Build Logger_Adapter.vcxproj, Experimental_Console.vcxproj, and Cplspls_To_Cross_Lang_Connector.vcxproj at each configuration. All succeed with zero warnings.
- **pch.h fallback**: Compile a translation unit that includes `pch.h` without having passed `QUILL_COMPILE_ACTIVE_LOG_LEVEL` via the compiler. The `#ifndef` guard defines level 3. Build succeeds, Debug-level stripping applies.
- **Release binary size vs Debug**: Release binary is at least 20% smaller than Debug binary (heuristic). Measure via `(Get-Item "x64\Release\*.exe").Length` vs `(Get-Item "x64\Debug\*.exe").Length`.
- **All 4 configurations present in vcxproj**: grep each `.vcxproj` file for `ItemDefinitionGroup` blocks with `Condition` strings `'Debug|x64'`, `'Release|x64'`, `'RelWithDebInfo|x64'`, `'MinSizeRel|x64'`. All 4 must exist with `QUILL_COMPILE_ACTIVE_LOG_LEVEL` defined before the defines are applied.
- **dumpbin primary CI validation**: Build a test TU that emits `#pragma message("QUILL_COMPILE_ACTIVE_LOG_LEVEL=" #QUILL_COMPILE_ACTIVE_LOG_LEVEL)`. Parse build log for each configuration: Debug should show `=3`, Release `=4`, RelWithDebInfo `=4`, MinSizeRel `=5`.
- **Consumer contract detection**: Compile a test that does NOT define `QUILL_COMPILE_ACTIVE_LOG_LEVEL` before including Logger_Adapter headers. Verify the pch.h fallback activates (level 3). The build should emit a `#pragma message` or `static_assert` warning that the level was not explicitly set by the consumer.
- **Test project vcxproj defines**: Verify that test project `.vcxproj` files (e.g., `Logger_Adapter_Tests.vcxproj`) also contain `QUILL_COMPILE_ACTIVE_LOG_LEVEL` definitions for all 4 configurations. Without these, Release test runs would have full logging, skewing performance measurements.

### Negative / Error Cases
- **Undefined macro**: Remove `QUILL_COMPILE_ACTIVE_LOG_LEVEL` from one configuration. The `pch.h` fallback (`#define 3`) kicks in. Build succeeds but Debug configuration may strip unexpectedly. Test verifies build succeeds but warns if the intentional override is missing.
- **Conflicting defines**: If a user defines `QUILL_COMPILE_ACTIVE_LOG_LEVEL=6` in their own code before including `pch.h`, the pch.h `#ifndef` preserves the user value. Test verifies the user override takes precedence.
- **Non-existent level value**: `QUILL_COMPILE_ACTIVE_LOG_LEVEL=100`. Quill's behavior for out-of-range values is undefined. Test verifies Quill headers compile without error (likely all log levels are stripped). Document that values outside [3,5] are not supported configurations.
- **Mixed configurations**: Building some translation units with Debug and others with Release within the same project (not possible with MSBuild standard configs, but possible with manual compiler flags). Verify linker doesn't complain about inconsistent log level definitions (Quill uses macros that expand to nothing — no symbols to conflict).

### Production Realities
- **Compiler upgrade (MSVC v143 → v144)**: The binary size heuristic may change. Test records a baseline ratio per MSVC version. CI compares against the baseline, not against a fixed 80% threshold.
- **Quill version upgrade**: If Quill changes the behavior of `QUILL_COMPILE_ACTIVE_LOG_LEVEL` (e.g., new log levels, renamed constants), the numeric values 3/4/5 may shift. Test must be version-aware: check `quill::version::major/minor/patch` and adjust expectations.
- **Adding new vcxproj**: If a new project (e.g., a unit test project) is added, its vcxproj must also get the defines. There is no mechanism to enforce this — add a CI step that greps all `.vcxproj` files for `QUILL_COMPILE_ACTIVE_LOG_LEVEL` and fails if any is missing it.

### Thread Safety
- **Not applicable**: Compile-time preprocessor defines have no runtime thread-safety implications. All log level checks are resolved to compile-time constants; the stripped branches do not exist in the binary. No atomic operations or memory ordering needed.

## Assertions
- Debug|x64 build with level 3: `LOG_TRACE_L3("test")` produces zero instructions in the object file (confirmed via `dumpbin /DISASM /SECTION:.text` — the call site is absent).
- Release|x64 build with level 4: `LOG_DEBUG("test")` produces zero instructions.
- MinSizeRel|x64 build with level 5: `LOG_INFO("test")` produces zero instructions.
- All builds complete with zero warnings (`msbuild /p:Configuration=<C> /p:Platform=x64 2>&1 | Select-String -Pattern "warning"` returns no matches).
- Release binary size < 80% of Debug binary size for `Experimental_Console.exe` (heuristic, baseline-adjusted).
- `pch.h` contains comment documenting the level mapping per AA-C02.
- Every `.vcxproj` in the repository contains `QUILL_COMPILE_ACTIVE_LOG_LEVEL` in at least one `PreprocessorDefinitions`.
- `#pragma message` output confirms Debug=3, Release=4, RelWithDebInfo=4, MinSizeRel=5 in build logs.
- All `ItemDefinitionGroup` blocks for RelWithDebInfo and MinSizeRel exist in vcxproj files (with `Optimization` and `WholeProgramOptimization`).
- Consumer code that includes Logger_Adapter headers without defining `QUILL_COMPILE_ACTIVE_LOG_LEVEL` gets a `#pragma message` warning and defaults to level 3.
- Test project vcxproj files define `QUILL_COMPILE_ACTIVE_LOG_LEVEL` for all 4 configurations.

## Failure Mode
- **TestDebugStripsTrace fails**: TraceL3 survives in Debug binary → Quill's compile-time stripping is not working for level 3. **Impact: CPU waste, branch prediction pressure on every log statement in Debug builds.**
- **TestReleaseStripsDebug fails**: Debug log calls survive in Release binary → no stripping of debug info. **Impact: Production performance regression, larger binary. This is the PRIMARY failure mode this AA exists to prevent.**
- **TestMinSizeRelStripsInfo fails**: Info logs survive in MinSizeRel → `-Os` binary is larger than expected. **Impact: Embedded/space-constrained deployments may exceed size budgets.**
- **TestPchFallback fails**: Without compiler define, `QUILL_COMPILE_ACTIVE_LOG_LEVEL` is undefined → Quill defaults to compiling ALL log levels. **Impact: Debug builds behave unexpectedly (all levels enabled), Release builds may accidentally get full logging.**
- **TestAllVcxprojsUpdated fails**: A new vcxproj was added without the defines. **Impact: Silent performance regression in that project.**
- **TestCIRegression fails (binary size heuristic)**: CI heuristics trigger false positive. **Impact: Developer friction, CI pipeline disruption. The heuristic is explicitly documented as fragile — a failure requires manual investigation.**

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Added prerequisite: ensure all 4 MSBuild configurations exist in vcxproj | Step 0 | Added scenario "All 4 configurations present in vcxproj"; updated Key Requirements |
| Added primary CI validation via `#pragma message` compile-time check | Step 3 (CI) | Added scenario "dumpbin primary CI validation"; updated assertions; resolved GAP-1-C02-2 |
| Documented consumer contract with fallback warning | Step 3 (CI) + Acceptance Criteria | Added scenario "Consumer contract detection"; updated Key Requirements |
| Added note for test project vcxproj defines | Step 1 (test projects note) | Added scenario "Test project vcxproj defines"; updated Key Requirements |
| Added Rationale for RelWithDebInfo=4 vs 3 | § Rationale | Updated Key Requirements |
| Added Profiling→MinSizeRel mapping clarification | § Mapping from TASK-C02 | Updated Key Requirements |

## Spec Gap Notes (SGN)

### Resolved Gaps

These GAPs were raised during test design and subsequently resolved by Impact Analysis fixes applied to the AA spec.

| Gap ID | Issue | Resolution | AA Spec Section |
|--------|-------|------------|-----------------|
| GAP-1-C02-1 | RelWithDebInfo/MinSizeRel config blocks missing from vcxproj | ✅ RESOLVED — AA Step 0 adds prerequisite to add all 4 config blocks before defining PreprocessorDefinitions. | Step 0 |
| GAP-1-C02-2 | Fragile binary-size CI heuristic | ✅ RESOLVED — AA Step 3 adds primary `#pragma message` / `dumpbin` compile-time check; binary size is secondary advisory. | Step 3 |
| GAP-1-C02-3 | Consumer contract for external projects | ✅ RESOLVED — AA Acceptance Criteria documents consumer contract: "External projects must define QUILL_COMPILE_ACTIVE_LOG_LEVEL in their own build system. The pch.h fallback is a safety net for Debug." | Acceptance Criteria |
| GAP-1-C02-4 | Test project vcxproj not addressed | ✅ RESOLVED — AA Step 1 explicitly notes: "test vcxproj must also define QUILL_COMPILE_ACTIVE_LOG_LEVEL." | Step 1 (note) |
