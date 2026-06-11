# After-Audit: Dependency-Driven Implementation Order v2.0

**Version**: 2.0 | **Generated**: 2026-06-11 | **Status**: 🔴 Revised After Audit  
**Based On**: `AUDIT_Full_Production_Grade_Review.md` (source-code-grounded audit of all 22 tasks)

---

## 1. Executive Summary: The Audit Changed the Order

The original `Dependency_Analysis_Deriven_Implementation_Order.md` v1.0 contained several structural flaws that were exposed by the production-grade audit:

| v1.0 Flaw | Audit Finding | v2.0 Fix |
|-----------|---------------|----------|
| P01/P02 placed as independent Wave 4 | P01 depends on sink infrastructure from C01; P01 needs complete redesign (Event IDs, Unicode, source registration) | Moved P01 after C01 + C03; split P01 into design-first phase |
| M12 (Dead Code Cleanup) buried in Wave 5 | Dead code creates confusion during implementation | Promoted to **Phase 0** — clean before building |
| M02 (Daily Rotation) missing entirely | M02 needs **complete redesign** (timezone, naming, retention, compression) | Added as Phase 1 redesign task |
| M11 (Emergency Reset) listed as 30min | Binary flag is dangerous — needs full state machine | Promoted to redesign phase with proper state model |
| QueueConfig as POD in Wave 1 | Correct, but needs to be before C01 (unchanged) | Kept in Phase 1, emphasized dependency |
| Stub files ignored | 14+ stub files mismatch task document assumptions | Added **Phase 0.5: Stub Reconciliation** |
| M07 (Error Notifier) in Wave 5 | Error detection during multi-logger init is critical | Moved to Phase 3 (after C01) |
| No mention of cross-cutting concerns | Missing features (hot-reload, metrics, audit trail) | Documented as Future Phases |
| No dynamic log level reconfiguration | 24/7 trading system cannot restart to change log levels | Added **C04** as Phase 3 task (depends on C01 named loggers) |
| No thread model / shutdown safety spec | Documented UB in GetLogger() during re-init/after shutdown | Added **C05** as Phase 0 design doc (guides C01 implementation) |
| No log sanitization (PII/secrets) | Compliance risk for trading systems | Added **M18** as Phase 5 safety task |
| No performance benchmark suite | No way to measure latency regression across phases | Added **M19** as Phase 0.5 infrastructure task |
| UTC timezone miscategorized as Low | Affects all log output from day one — essential for audit trails | Promoted **M14** from Phase 7 → Phase 1 Foundation |

---

## 2. Structural Dependency Graph (Post-Audit)

```
Phase 0: DESIGN & CLEANUP
  C05 (Thread Model & Shutdown) ──────────────────────────► design doc: thread safety contract for all phases
  M12 (Dead Code Cleanup) ────────────────────────────────► removes old code paths

Phase 0.5: INFRASTRUCTURE & RECONCILIATION
  M19 (Benchmark Suite) ─────────────────────────────────► perf baseline + regression detection
  Stub Reconciliation ───────────────────────────────────► aligns codebase with final design

Phase 1: FOUNDATION (No Code Dependencies, Critical Format)
  C02 (Compile-Time Log Level) ───────────────────────────► independent, affects all builds
  M08 (Queue Config) ────────────────────────────────────► affects backend behavior
  M14 (UTC Timezone) ────────────────────────────────────► affects all log output format from day one
  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

Phase 2: REDESIGN (Tasks that Failed Audit — Must Redesign First)
  M02 (Daily Rotation Redesign) ──────────────────────────► timezone, naming, retention, compression
  M05 (Rate Limiting Redesign) ───────────────────────────► time-based + global + burst
  M11 (Emergency Reset Redesign) ─────────────────────────► state machine (Normal→Degraded→Recovering)
  P01 (EventLog Sink Redesign) ───────────────────────────► Event IDs, source registration, Unicode
  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

Phase 3: CORE INFRASTRUCTURE (Depends on Phase 1)
  C01 (Multi-Logger Shim) ────────────────────────────────► LoggerEntry + SinkFactory + LoggingConfig
    │
    ├──▶ C03 (Backtrace Logging) ─────────────────────────► depends on named loggers from C01
    ├──▶ C04 (Dynamic Log Level) ─────────────────────────► runtime reconfiguration, depends on C01
    ├──▶ P01 (EventLog Implementation) ──────────────────► depends on sink architecture from C01
    └──▶ M07 (Error Notifier) ───────────────────────────► backend callback, depends on C01 init flow
  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

Phase 4: MACROS & SINKS (Depends on Phase 3)
  M01 (FileSink Append Mode) ─────────────────────────────► per-sink + verify Quill API
  M03 (LOGV Macros) ──────────────────────────────────────► simple macro wrapper
  M04 (LOGJ Structured Logging) ──────────────────────────► audit existing StructuredLogger first
  M06 (Custom Pattern Per Sink) ──────────────────────────► accept Quill per-logger limitation
  M13 (Stderr Sink) ──────────────────────────────────────► independent + color mode
  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

Phase 5: SAFETY & ERGONOMICS
  M09 (Result Monadic API) ───────────────────────────────► value_or, and_then, map
  M10 (Result Guarded Access) ────────────────────────────► guarded Value() + ValueUnsafe()
  M18 (Log Sanitization) ────────────────────────────────► PII/secret scrubbing pipeline
  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

Phase 6: PLATFORM (Depends on Phase 3)
  P02 (Thread Affinity) ──────────────────────────────────► NUMA-aware SetThreadGroupAffinity
  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

Phase 7: LOW PRIORITY (Post-v0.2.0)
  G01 Remaining (ColourMode, NullSink, STL Types) ───────► M15, M16, M17
  F01-F04 Future (Hot-Reload, Metrics, Audit Trail, etc.)► post-v0.2.0
```

---

## 3. Phase-by-Phase Task Details

---

### Phase 0: 📐 Design & Cleanup First (1-1.5h)

| # | Task | Effort | Files | Why First |
|---|------|--------|-------|-----------|
| 0.1 | **C05** — Thread Model & Shutdown Sequence | 1h | Design doc (no code) | Define thread safety contract (frontend/backend ownership, GetLogger concurrency, shutdown sequence) **before** any implementation touches threads. Guides C01, C03, M07 design. |
| 0.2 | **M12** — Dead Code Cleanup | 15 min | `LoggingConfig.hpp`, `LoggerSetup.hpp`, `CrashHandler.hpp`? | Remove `SetupCrashLogger`, `shutdown_timeout_ms` before adding new code. Prevents confusion. |

**C05 Design Deliverable:**
- Thread ownership model: frontend (caller) vs backend (Quill worker)
- GetLogger() concurrency contract: read-only after init, re-init is exclusive
- ShutdownLogging() sequence: drain queue → flush backtraces → stop backend → invalidate registry
- Race documentation: what happens during concurrent Init/GetLogger/Shutdown

**Pre-Implementation Checklist:**
- [ ] C05 design doc reviewed and approved
- [ ] `grep -r "SetupCrashLogger" Logger_Adapter/` — verify zero callers
- [ ] `grep -r "shutdown_timeout_ms" Logger_Adapter/` — verify zero callers
- [ ] `grep -r "MakeSignalHandlerOptions" Logger_Adapter/` — verify zero callers
- [ ] Remove from `LoggingConfig` struct
- [ ] Remove `CrashHandler.hpp` if unused
- [ ] Build succeeds Debug|x64

---

### Phase 0.5: 🔄 Infrastructure & Reconciliation (1.5-2h)

| # | Task | Effort | Why Here |
|---|------|--------|----------|
| 0.3 | **M19** — Performance Benchmark Suite | 1h | Establish latency baseline **before** any new features change behavior. Every phase gate depends on having a benchmark to measure against. Build a simple hot-path benchmark: `LOG_INFO` latency p50/p99/p999, throughput (msg/sec), queue drain time. |
| 0.4 | **Stub Reconciliation** | 30 min | Align 14+ stub files with final design before populating. |

**M19 Benchmark Deliverable:**
- Latency: p50/p99/p999 for `LOG_INFO` on hot path (ns)
- Throughput: max sustained msg/sec before queue backpressure
- Binary size: Debug vs Release before/after C02 compile-level
- Regression gate: CI fails if p99 latency increases >10% vs baseline

**Stub Reconciliation — Critical pre-condition**: The codebase has 14+ stub files (2-byte empty). They must be reconciled before any implementation.

| Stub File | Task Doc | Action Required |
|-----------|----------|-----------------|
| `config/BacktraceConfig.hpp` | C03 | Keep — populate with actual fields from C03 redesign |
| `config/LoggerEntry.hpp` | C01 | Keep — populate with sink-name-referencing design from C01 |
| `config/PatternConfig.hpp` | M06 | Keep — populate with format string + UTC fields |
| `config/QueueConfig.hpp` | M08 | Keep — populate with type + capacity fields |
| `macros/Backtrace.hpp` | C03 | Keep — populate with backtrace macro definitions |
| `macros/Core.hpp` | — (orphan) | **Delete** — no task references it, remove |
| `macros/RateLimited.hpp` | M05 | Keep — populate with new time-based limiters |
| `macros/Standard.hpp` | — (orphan) | **Delete** — no task references it, remove |
| `macros/Structured.hpp` | M04 | Keep — populate with LOGJ macros |
| `macros/VariableArgs.hpp` | M03 | Keep — populate with LOGV macros |
| `setup/LoggerRegistry.hpp` | C01 | Keep — populate with GetLogger() + registry |
| `setup/SinkFactory.hpp` | C01 | Keep — populate with sink creation abstraction |
| `sinks/EventLogSink.cpp` | P01 | Keep — will implement after P01 redesign |
| `sinks/EventLogSink.hpp` | P01 | Keep — will implement after P01 redesign |
| `windows/ThreadAffinity.hpp` | P02 | Keep — populate with SetThreadGroupAffinity wrapper |

**Files to DELETE (orphans with no task reference):**
- `macros/Core.hpp`
- `macros/Standard.hpp` (unless `HotPathLogger.hpp` is being moved — verify)

**Pre-Implementation Checklist:**
- [ ] Delete orphan stubs: `macros/Core.hpp`, `macros/Standard.hpp`
- [ ] Confirm remaining stubs match final design before populating
- [ ] Update `.vcxproj` and `.vcxproj.filters` to reflect deletions/additions

---

### Phase 1: ⚙️ Foundation Layer — Independent, Critical (3.5-4h)

| # | Task | Effort | Files | Key Considerations |
|---|------|--------|-------|--------------------|
| 1.1 | **C02** — Compile-Time Log Level | 30 min | All `.vcxproj` files, `pch.h` | Define `QUILL_COMPILE_ACTIVE_LOG_LEVEL` for **all 4 configurations** (Debug/Release/RelWithDebInfo/MinSizeRel). Add CI binary-size check. Document in `pch.h`. |
| 1.2 | **M08** — Queue Config | 1 h | `config/QueueConfig.hpp`, `LoggerSetup.hpp` | Add Bounded/Unbounded + capacity. **Warn against unbounded in production** (OOM risk). Document sizing guidance: 8192 for trading, 65536 for batch. |
| 1.3 | **M14** — UTC Timezone | 1 h | `config/PatternConfig.hpp`, `LoggingConfig.hpp` | Add UTC timestamp option to log output pattern. Default: `true` for trading audit trails. Affects all log output from day one. |

**Pre-Implementation Checklist:**
- [ ] `QUILL_COMPILE_ACTIVE_LOG_LEVEL=3` for Debug, `=4` for Release
- [ ] All `.vcxproj` files updated (Logger_Adapter, Experimental_Console, Cplspls_To_Cross_Lang_Connector)
- [ ] QueueConfig struct: `enum class QueueType { Bounded, Unbounded }`, `size_t capacity`
- [ ] Default: Bounded, 8192
- [ ] M14: `PatternConfig::utc_timestamp = true` by default
- [ ] M14: Document UTC vs local in pattern grammar

---

### Phase 2: 🔧 Redesign Tasks — Must Redesign Before Implementation (3-5h total design work)

These tasks **failed the audit** and require architectural redesign before any code is written.

#### Task 2.1: M02 — Daily File Rotation Redesign

**Redesign Deliverable**: Write a new `TASK-M02-v2.md` covering:

| Missing Element | Requirement |
|----------------|-------------|
| **Timezone** | Add `bool utc = true` (default UTC for trading) + `std::string timezone` |
| **Naming Convention** | `{base}.{YYYYMMDD}.log` — e.g., `assembler.2026-06-11.log` |
| **Retention Policy** | `uint32_t max_archive_days = 30` — auto-delete older files |
| **Compression** | `bool gzip_on_rotation = false` — optional gzip after rotation |
| **Midnight Atomicity** | Document that time-check-on-write may miss exact midnight — acceptable for trading |
| **Coexistence** | Daily rotation must coexist with existing size-based rotation |

**Effort**: 1h design, 1.5h implementation = 2.5h total

#### Task 2.2: M05 — Rate Limiting Redesign

**Redesign Deliverable**: Write a new `TASK-M05-v2.md` covering:

| Missing Element | Requirement |
|----------------|-------------|
| **Time-based limits** | Add `LIMIT_PER_SEC(logger, rate, fmt, ...)` and `LIMIT_PER_MIN(logger, rate, fmt, ...)` |
| **Burst allowance** | First log after window always fires |
| **Global rate limiter** | Add `GlobalRateLimiter` class — max total logs/sec across all loggers |
| **Thread safety** | Document: `std::atomic<uint32_t>` for count-based, `std::atomic<uint64_t> + timestamp` for time-based |
| **Existing EVERY_N** | Keep as-is for count-based scenarios, document use case |

**Effort**: 1h design, 1h implementation = 2h total

#### Task 2.3: M11 — Emergency Reset Redesign

**Redesign Deliverable**: Write a new `TASK-M11-v2.md` covering:

| Missing Element | Requirement |
|----------------|-------------|
| **State Machine** | `enum class EmergencyState : uint8_t { Normal, Degraded, Recovering, Fatal }` with epoch counter |
| **State Transitions** | Normal → Degraded (on error), Degraded → Recovering (on Reset), Recovering → Normal (on success), Any → Fatal (on fatal) |
| **Recovery Epoch** | Increment epoch on each Reset() to detect stale state reads |
| **Downstream Coordination** | Subsystems must re-check state after recovery operations (callback list) |
| **HealthProbe Symmetry** | Add `RecordRecovery()` matching existing `RecordEmergency()` |
| **Backend Coordination** | Flush backtraces on Degraded→Recovering transition |

**Effort**: 2h design, 1.5h implementation = 3.5h total

#### Task 2.4: P01 — EventLog Sink Redesign

**Redesign Deliverable**: Write a new `TASK-P01-v2.md` covering:

| Missing Element | Requirement |
|----------------|-------------|
| **Event ID → LogLevel Map** | `TRACE=0, DEBUG=1, INFO=2, WARNING=3, ERROR=4, CRITICAL=5` mapped to `WORD wEventId` |
| **Source Registration** | Document manual step: `wevtutil im` or installer script. Handle missing source gracefully (log to stderr, don't crash) |
| **Unicode (WCHAR)** | Convert `const char*` to `LPCWSTR` via `MultiByteToWideChar(CP_UTF8, ...)` |
| **Performance** | Document: `ReportEventW` is a kernel transition — **alerts only, not high-frequency**. Recommend max 10 events/sec. |
| **Category** | Map Logger_Adapter subsystems to Event Categories: 1=System, 2=Emergency, 3=OrderExecution, 4=Risk, 5=MarketData |

**Pre-implementation dependency**: Requires C01 (named loggers) for category mapping.

**Effort**: 1.5h design, 2h implementation = 3.5h total

---

### Phase 3: 🏗️ Core Infrastructure — Multi-Logger + Backtrace + Dynamic Control (8-11h)

| # | Task | Effort | Depends On | Key Changes | Audit Issues Addressed |
|---|------|--------|------------|-------------|----------------------|
| 3.1 | **C01** — Multi-Logger Shim | 3-4h | Phase 1 (M08), Phase 0.5 (stubs) | `LoggerEntry` with sink-name references (not bool flags), `SinkFactory`, `LoggerRegistry`, update `GetLogger()` | C01-B (sink names), C01-C (thread safety doc), C01-D (validation) |
| 3.2 | **C03** — Backtrace Logging | 2-3h | C01 (named loggers) | `BacktraceConfig` wiring, init_backtrace on named loggers, auto-flush ERROR, **shutdown-flush path** | C03-C (shutdown flush), C03-D (capacity guidance) |
| 3.3 | **C04** — Dynamic Log Level Reconfiguration | 1-2h | C01 (named loggers) | Add `SetLogLevel(name, level)` at runtime via Quill's `logger->set_log_level()`. Thread-safe — must use atomic or mutex for current level map. **Essential for 24/7 trading — no restart needed.** | Cross-cutting gap |
| 3.4 | **P01** — EventLog Implementation | 2-3h | C01 + P01 redesign (Phase 2.4) | Implement `EventLogSink` using `ReportEventW`, WCHAR conversion, Event ID mapping | P01-B (Event IDs), P01-C (source reg), P01-D (Unicode) |
| 3.5 | **M07** — Error Notifier | 30 min | C01 (init flow) | Add `error_notifier` callback to `LoggingConfig`, wire to Quill backend, **document non-blocking requirement** | M07-A (backend thread), M07-B (reliability) |

**Critical path**: C01 → C03 + C04 + P01 + M07 (C03/C04/P01/M07 can be done in parallel after C01).

**Pre-Implementation Checklist:**
- [ ] C01: LoggerEntry uses `std::vector<std::string> sink_names` not bool flags
- [ ] C01: Input validation on LoggerEntry (empty name, missing sink → clear error)
- [ ] C01: Documentation: GetLogger() is thread-safe via Quill, but re-init is UB
- [ ] C01: Runtime re-configuration guard — `InitializeLogging()` must not be called while named loggers are in use, or must atomically replace the registry
- [ ] C03: `ShutdownLogging()` flushes all backtraces before stopping backend
- [ ] C03: Default backtrace capacities: Emergency=5000, HealthProbe=100, others=1000
- [ ] C04: `SetLogLevel(logger_name, level)` must be thread-safe with concurrent logging
- [ ] C04: Document that dynamic level change affects subsequent log calls only (no retroactive filtering)
- [ ] P01: EventLogSink registers events only, not for high-frequency logging
- [ ] M07: Callback runs on backend thread — MUST be non-blocking

---

### Phase 4: 🧩 Macros & Sinks (3-5h)

| # | Task | Effort | Depends On | Key Considerations |
|---|------|--------|------------|-------------------|
| 4.1 | **M01** — FileSink Append Mode | 45 min | Phase 3 (sink infrastructure) | Verify `FileSinkConfig::set_open_mode()` in Quill v10.0.1 first. Use file-rename not file-delete for fallback. Per-sink append mode, not global. |
| 4.2 | **M03** — LOGV Macros | 30 min | Phase 3 (named loggers) | Pure macro wrapper. Document: for diagnostic use only, not hot path. |
| 4.3 | **M04** — LOGJ Structured Logging | 1-1.5h | Phase 3 + existing `StructuredLogger.hpp` | **Audit existing `StructuredLogger.hpp` first** — it's 2285 bytes and may already implement features. Define JSON schema. Warn against hot-path JSON. **⚠️ AA-M04 uses `QUILL_LOG_*` instead of `QUILL_LOGJ_*` — fix before implementation.** |
| 4.4 | **M06** — Custom Pattern Per Sink | 1-2h | Phase 3 (logger creation) | Accept Quill limitation (per-logger, not per-sink). Publish standard pattern grammar. Validate pattern strings at init. |
| 4.5 | **M13** — Stderr Sink | 30 min | Phase 4 (sink infrastructure) | Add color mode config. Support independent stdout + stderr (not exclusive). |

---

### Phase 5: 🛡️ Safety & Ergonomics (4-5h)

| # | Task | Effort | Depends On | Key Considerations |
|---|------|--------|------------|-------------------|
| 5.1 | **M09** — Result Monadic API | 1-1.5h | Existing `Result<T>` | Add `value_or()`, `and_then()`, `map()`. Add `value_or(ErrorCode)` for `ResultVoid`. Document error type invariance. **⚠️ AUDIT-M09 has fabricated issue (M09-C SEH catch) and drifted M09-B — re-align with holistic audit before using AA-M09.** |
| 5.2 | **M10** — Result Guarded Access | 1h | Existing `Result<T>` | Choose policy: Debug=assert, Release=log+return default. Add `ValueUnsafe()` for proven hot paths. **⚠️ AUDIT-M10 issues don't match holistic (DLL ODR, noexcept) — holistic issues (policy choice, ValueUnsafe) must be used instead.** |
| 5.3 | **M18** — Log Sanitization Pipeline | 1.5-2h | Phase 4 (macro infrastructure) | Add configurable filter chain to scrub PII/secrets before writing. Patterns: API keys (regex), trade secrets (keyword), credit cards (Luhn). Filter runs on frontend thread — must be O(1) per pattern, no allocations on hot path. |

---

### Phase 6: 🪟 Platform (30 min)

| # | Task | Effort | Depends On | Key Considerations |
|---|------|--------|------------|-------------------|
| 6.1 | **P02** — Thread Affinity | 30 min | Phase 3 (backend thread) | Use `SetThreadGroupAffinity` for NUMA. Add frontend thread affinity config option. |

---

### Phase 7: 🟢 Low Priority / Future (Post-v0.2.0)

| # | Task | Effort | Priority Rationale |
|---|------|--------|--------------------|
| 7.1 | **M14** — UTC Timezone (was G01) | 1h | **Promoted from Low** — essential for multi-zone trading audit trails |
| 7.2 | **M15** — ColourMode Enhancement | 30 min | Nice-to-have, already partially implemented |
| 7.3 | **M16** — NullSink | 15 min | Useful for testing |
| 7.4 | **M17** — STL Type Support | 1h | Depends on Quill's type support |
| 7.5 | **F01** — Configuration Hot-Reload | Future | SIGHUP-compatible reload for log levels, patterns, sinks |
| 7.6 | **F02** — Metrics Exposure | Future | Prometheus/StatsD-compatible queue depth, drops, latency |
| 7.7 | **F03** — Log Sanitization | Future | PII/secret scrubbing pipeline |
| 7.8 | **F04** — Audit Trail | Future | Tamper-evident append-only log files (MiFID II, SEC) |

---

## 4. Implementation Waves — Visual Timeline

```
Week 1                    Week 2                    Week 3                    Week 4
┌────────────────────────┐┌────────────────────────┐┌────────────────────────┐┌────────────────────────┐
│ Phase 0: Design+Clean  ││ Phase 2: Redesign Docs  ││ Phase 3: Core Infra    ││ Phase 5: Safety        │
│   C05 (thread model)   ││   M02-v2 (rotation)    ││   C01 (multi-logger)   ││   M09 (monadic)        │
│   M12 (dead code)      ││   M05-v2 (rate limit)  ││   C03 (backtrace)      ││   M10 (guarded access) │
│ Phase 0.5: Infra+Rec   ││   M11-v2 (emergency)   ││   C04 (dynamic level)  ││   M18 (sanitization)   │
│   M19 (benchmark)      ││   P01-v2 (EventLog)    ││   P01 (EventLog impl)  ││ Phase 6: Platform      │
│   Stub reconciliation  ││                        ││   M07 (error notifier) ││   P02 (affinity)       │
│ Phase 1: Foundation    ││                        ││ Phase 4: Macros/Sinks  ││ Phase 7: Low/Future    │
│   C02 (compile level)  ││                        ││   M01, M03, M04, M06,  ││   M15 (colour), etc.   │
│   M08 (queue config)   ││                        ││   M13                  ││                        │
│   M14 (UTC timezone)   ││                        ││                        ││                        │
└────────────────────────┘└────────────────────────┘└────────────────────────┘└────────────────────────┘
```

---

## 5. Risk & Mitigation Summary

| Risk | Phase | Probability | Impact | Mitigation |
|------|-------|-------------|--------|------------|
| C01 breaks existing consumers | Phase 3 | Medium | High | Backward-compatible default (empty loggers = root only). Run existing tests. |
| Quill v10.0.1 API missing features (FileSinkConfig, PatternFormatter) | Phase 4 | Medium | Medium | Verify API before coding. Have fallback plans. |
| EventLog source registration blocks deployment | Phase 3 | High (ops) | Medium | Document as manual setup step. Fail gracefully if missing. |
| Stub reconciliation reveals hidden dependencies | Phase 0.5 | Medium | Medium | Do reconciliation pass in dedicated branch, test build. |
| Emergency state machine too complex for initial release | Phase 2 | Low | High | Ship Degraded→Normal only in v0.2.0, add Recovering in v0.3.0. |
| UTC timezone change breaks existing log parsers | Phase 1 | Medium | Medium | Add as opt-in with opt-out flag. Document in release notes. |
| Backtrace memory usage in production | Phase 3 | Low | Medium | Capacity guidance per subsystem. Monitor via HealthProbe. |
| No thread model leads to race conditions | Phase 0 | High | Critical | C05 design doc must be reviewed and approved before Phase 3 starts. |
| Dynamic log level collides with concurrent init | Phase 3 | Medium | High | C04 must follow C05 thread model spec exactly. |
| Benchmark baseline too noisy for regression detection | Phase 0.5 | Medium | Medium | Run multiple iterations, use statistical comparison (Mann-Whitney), control for CPU throttling. |
| Log sanitization regex causes latency spikes | Phase 5 | Medium | High | Pre-compile patterns, O(1) per check, no allocations on hot path. Test with worst-case input. |

---

## 6. Build Configuration Impact

### Files to ADD to `.vcxproj`:
```
Phase 0.5: (reconcile existing stubs)
            M19: tests/bench/Benchmark.cpp, tests/bench/Benchmark.hpp
Phase 1:   (no new files — config changes only, but M14 affects PatternConfig)
Phase 3:   No new files — populate existing stubs
Phase 4:   No new files — populate existing stubs
Phase 5:   M18: config/SanitizationConfig.hpp, sinks/SanitizingSink.hpp (decorator)
            Modify existing error/Result.hpp for M09/M10
Phase 6:   No new files — populate existing windows/ThreadAffinity.hpp
```

### Files to REMOVE from `.vcxproj`:
```
macros/Core.hpp       (orphan — delete)
macros/Standard.hpp   (orphan — delete)
```

### Preprocessor Definitions:
```xml
<!-- All .vcxproj files consuming Logger_Adapter -->
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

---

## 7. Validation Gates (CI Checkpoints)

### Gate 1 — After Phase 1:
- [ ] Release binary size reduced (no Debug/Trace strings compiled in)
- [ ] Queue config compiles and default is Bounded/8192
- [ ] Log output timestamps are UTC by default (M14)
- [ ] Orphan stubs deleted, build succeeds
- [ ] Benchmark baseline recorded (latency p50/p99, throughput, binary size)

### Gate 2 — After Phase 3:
- [ ] `GetLogger("Risk") != GetLogger("OrderExecution")` in unit test
- [ ] Setting Risk logger to Warning does not suppress OrderExecution Debug
- [ ] `SetLogLevel("Risk", quill::LogLevel::Warning)` works at runtime without restart (C04)
- [ ] Backtrace auto-flushes on ERROR
- [ ] Backtrace flush during `ShutdownLogging()`
- [ ] EventLog sink writes to Windows Event Viewer (test on Win10+)
- [ ] Error notifier callback invoked on backend failure
- [ ] Benchmark shows no regression vs Phase 1 baseline

### Gate 3 — After Phase 4:
- [ ] `config.file.append = false` produces fresh empty file on restart
- [ ] LOGV output matches `key=value` format
- [ ] LOGJ output is valid JSON per schema
- [ ] Console sink uses different pattern than file sink
- [ ] Stderr output is colored when configured
- [ ] Benchmark shows no regression vs Gate 2 baseline

### Gate 4 — After Phase 5:
- [ ] `Result<int>::Value()` throws/logs on error result (not UB)
- [ ] `Result<int>::ValueUnsafe()` returns garbage on error (documented)
- [ ] `Result<int>::value_or(42)` returns 42 on error
- [ ] API key pattern `sk-[a-fA-F0-9]{32}` is scrubbed from log output (M18)
- [ ] Sanitization filter runs in <1µs on hot path (M18 benchmark)
- [ ] Benchmark shows no regression vs Gate 3 baseline

### Gate 5 — Final:
- [ ] All phases implemented, Debug|x64 + Release|x64 build with zero warnings
- [ ] `Experimental_Console.exe` exercises all new features
- [ ] Existing tests continue to pass

---

## 8. Appendix: v1.0 → v2.0 Migration Table

| v1.0 Wave | v2.0 Phase | Change Reason |
|-----------|------------|---------------|
| Wave 1: C02, stubs | Phase 1: C02, M08 | Stubs moved to Phase 0.5 reconciliation; M08 added |
| Wave 2: C01 | Phase 3: C01 | Moved after redesign phases and M08 (queue affects C01) |
| Wave 3: C03, M03-M05 | Phase 3 (C03) + Phase 4 (M03-M04) + Phase 2 redesign (M05) | M05 failed audit, promoted to redesign |
| Wave 4: P01, P02 | Phase 2 redesign (P01) + Phase 3 impl (P01) + Phase 6 (P02) | P01 failed audit; P02 delayed by dependency |
| Wave 5: M01, M06-M13 | Distributed across Phases 0, 1, 2, 4, 5 | Audit re-prioritized based on criticality |
| (missing) M02 | Phase 2 redesign | M02 was completely missing from v1.0 |
| (missing) G01 | Phase 7 | Split into individual tasks |
| (new) C04 Dynamic Log Level | Phase 3 | Capability gap: 24/7 trading needs runtime reconfig |
| (new) C05 Thread Model | Phase 0 | Capability gap: documented UB needs formal spec before coding |
| (new) M14 UTC Timezone | Phase 1 (was Phase 7) | Reprioritized: affects all log output, must be Foundation |
| (new) M18 Log Sanitization | Phase 5 | Capability gap: PII/secret compliance |
| (new) M19 Benchmark Suite | Phase 0.5 | Capability gap: no regression detection without baseline |

---

*End of After-Audit Implementation Order v2.0 — Based on `AUDIT_Full_Production_Grade_Review.md` findings grounded in actual source code analysis.*