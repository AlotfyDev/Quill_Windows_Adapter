# AA-C05 — Thread Model & Shutdown Sequence (After-Audit New)

> **Phase**: 0 — 📐 Design & Cleanup First  
> **Effort**: 1 h (design doc, no code)  
> **Depends on**: Nothing (guides all other phases)  
> **Capability Gap**: Documented UB in GetLogger() during re-init/after shutdown — no formal thread safety contract

---

## Problem

Logger_Adapter currently has **undefined behavior** in several concurrency scenarios:
1. `GetLogger()` during or after `ShutdownLogging()` — UB (documented but unguarded)
2. `InitializeLogging()` while named loggers are in use — no guard
3. Frontend (caller) thread vs backend (Quill worker) thread responsibilities — undocumented

A formal thread model is required **before** any multi-logger (C01) or dynamic level (C04) code is written.

---

## Design Deliverable

### 1. Thread Ownership Model

```
Frontend Threads (caller)           Backend Thread (Quill worker)
┌─────────────────────────┐         ┌──────────────────────────┐
│ GetLogger()             │         │ Process log messages     │
│ SetLogLevel() ← C04     │         │ Flush backtraces         │
│ LOG_INFO(...)           │         │ Sink writes (file, etc)  │
│ InitializeLogging()     │         │ Error notifier callback  │
│ ShutdownLogging()       │         │                          │
└─────────────────────────┘         └──────────────────────────┘
      ↑                                     ↑
      │ Quill frontend API                  │ Quill backend thread
      │ (thread-safe by design)             │ (owns all sink writes)
      └─────────────────────────────────────┘
```

### 1b. Shutdown Flag Storage

The shutdown flag is a single `std::atomic<bool>` defined as a function-local static in the translation unit that implements `ShutdownLogging()`. It is declared `extern std::atomic<bool> g_shutting_down;` in the internal header so all translation units share one instance. This prevents ODR violations where each TU would have its own flag.

### 2. Concurrency Contract

| Operation | Thread Safety | Notes |
|-----------|---------------|-------|
| `GetLogger(name)` | ✅ Fully thread-safe | Quill's `get_logger()` is internally synchronized |
| `LOG_INFO(logger, ...)` | ✅ Fully thread-safe | Enqueues to SPSC queue — lock-free |
| `SetLogLevel(name, level)` | ✅ Thread-safe with concurrent logging | `quill::Logger::set_log_level()` uses `std::atomic` |
| `InitializeLogging()` | ⚠️ Must be called once | `std::call_once` guarded. If re-init needed, must be exclusive with all GetLogger/SetLogLevel calls |
| `ShutdownLogging()` | ⚠️ Caller must ensure no concurrent GetLogger | After shutdown, the logger registry is invalid. Documented UB. |
| `GetLogger()` after shutdown | ❌ UB | Not guarded — performance cost of a check on every hot-path call is unacceptable |
| Registry modification during concurrent reads | ❌ UB | Registry is a `std::unordered_map` — not thread-safe for writes during reads |

### 3. Shutdown Sequence

```
ShutdownLogging() called
  │
  ├─▶ 1. Set global flag: shutting_down = true
  │       (any new GetLogger that misses the flag → UB but rare)
  │
  ├─▶ 2. Flush all backtraces (C03)
  │       Backtrace::flush_all() before backend stops
  │
  ├─▶ 3. Stop Quill backend
  │       quill::backend().stop() — drains queue
  │
  └─▶ 4. Invalidate registry (optional for cleanup)
        Clear map, loggers are still alive in Quill's internal storage
```

### 4. Re-Initialization Contract

If `InitializeLogging()` must be called again after shutdown:

```
InitializeLogging() after shutdown
  │
  ├─▶ 1. Check: is backend running? If yes → UB (caller bug)
  │
  ├─▶ 2. Create new registry
  │       Thread-local or atomically swapped to avoid stale pointer reads
  │
  ├─▶ 3. Start Quill backend
  │       quill::start()
  │
  └─▶ 4. Create named loggers per config
```

**Re-init is NOT supported in v0.2.0.** The design above is for future use. For v0.2.0, `InitializeLogging()` is called exactly once.

**Design tradeoff**: `std::call_once` blocks re-init permanently with no escape hatch. If re-init becomes a requirement (e.g., hot-reload config), replace `std::call_once` with a `std::atomic<bool> initialized` flag that can be reset under exclusive access (a mutex). The `std::call_once` approach is correct for v0.2.0 since re-init is out of scope.

### Application Lifecycle Coordination

Callers **must not** call `GetLogger()` after `ShutdownLogging()` returns. The doc documents this as UB, but a production system should enforce this at the application level (not in the library):

- Recommended pattern: Set an application-level `std::atomic<bool> app_running` flag before shutdown
- All logging threads check `app_running.load(std::memory_order_acquire)` before calling `GetLogger()`
- This prevents UB at the source rather than detecting it in the library hot path (where a check would be too expensive)

### 4b. Shutdown Message Guarantee

Messages enqueued before `ShutdownLogging()` returns are guaranteed persisted. Messages enqueued during shutdown (between the `shutting_down` flag set and backend stop) may be lost. This is Quill's contract — the SPSC queue drains up to `Backend::stop()` but any enqueue after the backend begins stopping has no consumer.

For trading systems that require zero-loss shutdown, the application should:
1. Stop accepting new work before calling `ShutdownLogging()`
2. Ensure all producer threads have flushed their SPSC queues before the shutdown sequence begins
3. Use the application-level `app_running` flag pattern (see Section 4c) to prevent new log calls during shutdown

### 4c. Sink Thread Safety

Sink creation and destruction is frontend-thread-safe — Quill's `create_or_get_sink()` is internally synchronized. Sink configuration changes (e.g., rotating file path, pattern format, log level filters) must be synchronized with the backend thread or performed before `quill::Backend::start()`.

The backend thread owns all sink writes. Frontend threads never touch a file handle. However, if a frontend thread destroys a sink while the backend is flushing it, undefined behavior occurs. The application must ensure sink lifetimes exceed the backend thread lifetime — typically by not destroying sinks until after `ShutdownLogging()` returns.

**Future work**: If per-sink dynamic reconfiguration becomes a requirement (e.g., changing a file sink's path at runtime), a reader-writer lock on the sink registry or a concurrent sink collection would be needed. Not required for v0.2.0.

### 5. Design Decisions

| Decision | Rationale |
|----------|-----------|
| No lock on `GetLogger()` after shutdown | Performance: every hot-path call would pay for a check that almost never triggers. However, a `DEBUG_ASSERT` (`assert(!g_shutting_down.load(std::memory_order_acquire))`) is added inside `GetLogger()` to catch development mistakes. The assert expands to nothing in Release (`NDEBUG`), so hot-path cost is zero. Developers see an assertion failure in Debug builds if they accidentally use the logger after shutdown, preventing silent UB during development. |
| Debug assert in GetLogger() | Zero cost in Release; catches UB during development before it reaches production |
| Registry is read-only after init | No concurrent writes during normal operation — only shutdown/re-init mutate |
| `std::atomic<bool>` with release/acquire ordering for shutdown flag | Single cache-line, no false sharing. **Ordering**: store uses `memory_order_release`, load uses `memory_order_acquire`. Default `seq_cst` is unnecessary — the flag is written once and read rarely, so a full memory barrier on every load is wasted cycles. On x64, acquire/release compiles to plain `mov` (no `mfence`), while seq_cst requires `mfence` or `lock`-prefixed store. |
| Backend thread owns all sink writes | Frontend never touches a file handle — no contention |
| Registry read-only assumption holds for v0.2.0 | The `std::unordered_map` logger registry has no concurrent writes after initialization. If dynamic logger management (G01f) is added in a future release, the registry will need a reader-writer lock (`std::shared_mutex`) or a lock-free concurrent hash map. |

---

## Acceptance Criteria

- [ ] Design doc reviewed and approved by team
- [ ] All concurrency scenarios documented (normal, shutdown, re-init)
- [ ] No code changes — this is a specification only
- [ ] AA-C01, AA-C03, AA-C04, AA-M07 each reference this doc for thread safety
- [ ] Add microbenchmark in AA-M19 that measures `GetLogger()` hot-path latency with and without the atomic assert check to validate the 0-cost assumption

---

## Files Changed

| File | Action |
|------|--------|
| (none) | Design doc only |
