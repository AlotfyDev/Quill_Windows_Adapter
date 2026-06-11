# AA-0.5 — Stub Reconciliation (After-Audit Corrected)

> **Phase**: 0.5 — 🔄 Reconciliation  
> **Effort**: 30 min  
> **Depends on**: AA-M12 (Dead Code Cleanup)  
> **Audit Issue**: Section 21.1 — 14+ stub files mismatch task document assumptions

---

## Problem

The codebase contains **14+ files that are 2-byte stubs** (empty or near-empty). The original task documents (TASK-*) assume these files don't exist and propose creating them. This creates a serious integrity gap: implementing from the task docs will conflict with the existing file structure.

---

## Corrected Implementation Plan

### Step 1 — Delete Orphan Stubs (No Task Reference)

These files have **no task document** referencing them. Delete immediately:

```powershell
Remove-Item "Logger_Adapter/macros/Core.hpp"
Remove-Item "Logger_Adapter/macros/Standard.hpp"
```

Also remove from `.vcxproj` and `.vcxproj.filters`.

### Step 2 — Reconcile Remaining Stubs

Update the manifest of which files exist and which task populates them:

| Stub File | Task Doc | AA Plan | Populated In |
|-----------|----------|---------|--------------|
| `config/BacktraceConfig.hpp` | C03 | ✅ Keep, populate with backtrace config struct | AA-C03 |
| `config/LoggerEntry.hpp` | C01 | ✅ Keep, populate with sink-name-referencing design | AA-C01 |
| `config/PatternConfig.hpp` | M06 | ✅ Keep, populate with format string + UTC fields | AA-M06 |
| `config/QueueConfig.hpp` | M08 | ✅ Keep, populate with QueueType + capacity | AA-M08 |
| `macros/Backtrace.hpp` | C03 | ✅ Keep, populate with backtrace macro defs | AA-C03 |
| `macros/RateLimited.hpp` | M05 | ✅ Keep, populate with time-based limiters | AA-M05 |
| `macros/Structured.hpp` | M04 | ✅ Keep, populate with LOGJ macros | AA-M04 |
| `macros/VariableArgs.hpp` | M03 | ✅ Keep, populate with LOGV macros | AA-M03 |
| `setup/LoggerRegistry.hpp` | C01 | ✅ Keep, populate with GetLogger() + registry | AA-C01 |
| `setup/SinkFactory.hpp` | C01 | ✅ Keep, populate with sink creation abstraction | AA-C01 |
| `sinks/EventLogSink.cpp` | P01 | ✅ Keep, implement after P01 redesign | AA-P01 |
| `sinks/EventLogSink.hpp` | P01 | ✅ Keep, header for EventLogSink | AA-P01 |
| `windows/ThreadAffinity.hpp` | P02 | ✅ Keep, populate with SetThreadGroupAffinity | AA-P02 |

### Step 3 — Verify Build Integrity

After deleting orphans, ensure the build still succeeds:

```powershell
msbuild Logger_Adapter\Logger_Adapter.vcxproj /p:Configuration=Debug /p:Platform=x64
msbuild Experimental_Console\Experimental_Console.vcxproj /p:Configuration=Debug /p:Platform=x64
```

---

## Acceptance Criteria

- [ ] `macros/Core.hpp` deleted
- [ ] `macros/Standard.hpp` deleted
- [ ] All remaining stubs accounted for against their AA task
- [ ] Build succeeds Debug|x64 after deletions
- [ ] `.vcxproj` and `.vcxproj.filters` updated