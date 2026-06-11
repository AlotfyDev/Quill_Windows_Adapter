# REV-C02 — Compile-Time Log Level: RelWithDebInfo Rationale & Profiling→MinSizeRel Mapping

**Severity**: ⚠️ Revision (design ambiguity, undocumented rationale)
**AA File**: `AA-C02-CompileTimeLogLevel.md`
**Phase**: 1 — Foundation
**Effort**: 5 min

---

## Description

Two issues:

### Issue 1: RelWithDebInfo=4 is debatable

`AA-C02` assigns `RelWithDebInfo=4` (strips Trace + Debug). RelWithDebInfo is used for **profiling** — developers profiling hot paths often want Debug-level logging enabled (level 3) to see what the code is doing during profiling sessions. Level 4 strips Debug logs, which may hide the very information needed during profiling.

**Trade-off**: Level 3 keeps Debug logs but adds code bloat (trace/debug branches compile into the binary). Level 4 produces leaner profiles but less visibility. Neither is objectively wrong, but the choice must be **documented with rationale** so a future developer can make an informed override.

### Issue 2: Profiling→MinSizeRel mapping is undocumented

The original `TASK-C02` references a custom "Profiling" configuration. `AA-C02` replaces this with MSBuild-standard `MinSizeRel=5`. This is **architecturally sound** (standard MSBuild > ad-hoc), but the mapping is not documented. A developer comparing the original task to the AA will see "Profiling=6" become "MinSizeRel=5" and wonder:
- Why was the value changed from 6 to 5?
- What configuration replaces Profiling?
- Is MinSizeRel semantically equivalent to Profiling?

### Issue 3 (Secondary): CI binary size heuristic is fragile

The CI check (`Release ≥ 80% of Debug → warning`) is a heuristic that can trigger false positives from:
- External dependency changes (new Quill version adds symbols)
- Compiler version differences (MSVC 2022 vs 2019 codegen)
- Linker flags (whole-program optimization affects size ratio)

---

## Root Cause

1. **AA author used Release as precedent** for RelWithDebInfo (= same level as Release) without considering the profiling use case.
2. **Profiling→MinSizeRel mapping was considered obvious** — "Profiling" is not an MSBuild standard config, so it was dropped. But the original task used it, so it must be addressed explicitly.
3. **CI heuristic was a first pass** — the 80% threshold has no empirical baseline.

---

## Exact Fix

### Fix 1: Document RelWithDebInfo Rationale

Add to AA-C02 after the level mapping in Step 1:

```markdown
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
```

### Fix 2: Document Profiling→MinSizeRel Mapping

Add after the RelWithDebInfo rationale:

```markdown
### Mapping from TASK-C02: Profiling → MinSizeRel

The original task referenced a custom "Profiling" configuration (level 6 = Warning). This AA replaces it with the MSBuild-standard **MinSizeRel** configuration:

| Original (TASK-C02) | Current (AA-C02) | Reason |
|---------------------|-------------------|--------|
| Profiling = 6 | MinSizeRel = 5 | "Profiling" is not an MSBuild standard config. MinSizeRel is the closest standard equivalent. Level 5 (Warning) strips Trace, Debug, and Info. This is _more aggressive_ than the original's level 6 (which only strips Trace + Debug + Info), producing a smaller binary. Level 5 is correct for MinSizeRel's goal: smallest possible binary. |

**Value difference**: TASK-C02 used Quill's symbolic constant `QUILL_ACTIVE_LOG_LEVEL_WARNING`, which = 6 in some Quill versions. AA-C02 uses the numeric value 5, which corresponds to `QUILL_ACTIVE_LOG_LEVEL_WARNING` in Quill v10.0.x. The numeric value changed due to Quill version differences, not a semantic change. The behavior is identical: strip Trace, Debug, and Info; keep Warning, Error, Critical.
```

### Fix 3: Document CI Heuristic Limitations

Add to Step 3 CI Validation:

```markdown
### CI Heuristic Limitations

The `Release ≥ 80% Debug` check is a **heuristic**, not a guarantee:

- **Dependency churn** can inflate Release binaries without log-level issues (e.g., Quill adds a new sink implementation)
- **Compiler optimizations** affect Debug vs Release ratio differently across MSVC versions
- **Linker settings** (e.g., /LTCG, /GL) change the ratio independently of log level

**If this check triggers unexpectedly**:
1. Verify the binary was built with the correct `QUILL_COMPILE_ACTIVE_LOG_LEVEL`
2. Check if external dependencies changed since the last baseline
3. Consider recomputing the threshold: measure Release/Debug ratio with known-good builds over 3-5 CI runs, then set threshold to `max_ratio * 1.10`

**Alternative**: A more robust check would be to compile a fixed set of log statements and verify they are eliminated in the Release binary using `dumpbin /SYMBOLS` or `dumpbin /DISASM`. This is more precise but slower.
```
### Summary of Changes to AA-C02

| Location | Change |
|----------|--------|
| After Step 1 level mapping | Add "Rationale: RelWithDebInfo = 4" section |
| After Step 1 level mapping (after RelWithDebInfo rationale) | Add "Mapping from TASK-C02: Profiling → MinSizeRel" section |
| Step 3 CI Validation | Add "CI Heuristic Limitations" subsection |

---

## Impact if NOT Fixed

- **Developer confusion**: A future developer profiling the system will set `RelWithDebInfo` and wonder why Debug logs are missing. They may waste time debugging the log level instead of the actual issue.
- **Trust erosion**: The undocumented Profiling→MinSizeRel mapping makes the AA appear inconsistent with the original task. A developer may "fix" it by adding Profiling back, creating a 5th configuration that nobody uses.
- **CI noise**: The 80% heuristic triggers a false-positive warning, developers learn to ignore CI log level warnings, and a real regression slips through.

---

## Verification

1. Read AA-C02: the RelWithDebInfo rationale section must be present with the trade-off table
2. Read AA-C02: the Profiling→MinSizeRel mapping section must be present with the value difference explained
3. Read AA-C02 Step 3: the CI heuristic limitations must be documented
4. Consider adding a cross-reference to AA-M19 (Benchmark Suite) for establishing a proper binary size baseline vs the heuristic threshold
