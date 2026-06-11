# REV-C05 — Thread Model: Missing `std::atomic` Memory Ordering Specification

**Severity**: ⚠️ Revision (design incomplete, performance concern)
**AA File**: `AA-C05-ThreadModel.md`
**Phase**: 0 — Design & Cleanup First
**Effort**: 5 min

---

## Description

`AA-C05-ThreadModel.md` §5 Design Decisions states: `"std::atomic<bool> for shutdown flag — Single cache-line, no false sharing"`. This is **correct** but **incomplete**.

For a low-latency trading system, the default memory ordering (`std::memory_order_seq_cst`) imposes a full memory barrier on every load and store. This is unnecessarily expensive for a shutdown flag that is:
- **Written once** (during shutdown)
- **Read rarely** (only by `GetLogger()` / initialization gates)

The design should specify **release/acquire semantics**:
- Store (shutdown) → `shutdown_flag.store(true, std::memory_order_release)`
- Load (readers) → `shutdown_flag.load(std::memory_order_acquire)`

This eliminates the full sequential consistency barrier on x64, replacing `mfence`/`lock` with a compiler barrier. On x64, `mov` already provides acquire/release — sequential consistency adds `mfence` or `lock cmpxchg` on the store side.

Additionally:
- No forward-looking note about registry read-only assumption being violated by **future** dynamic logger management (G01f)
- No recommendation for **application-level lifecycle coordination** — callers must not call `GetLogger()` after shutdown

---

## Root Cause

1. **Memory ordering is often overlooked** in high-level design docs. Authors default to "use atomic" without considering which ordering is appropriate.
2. **Default `seq_cst` is "safe"** — the design works, so the omission doesn't cause a bug. But it's suboptimal for the use case.
3. **The shutdown flag is not yet implemented** in code — the AA is a design doc. The ordering spec is cheaper to fix now than after implementation.

---

## Exact Fix

Replace §5 Design Decisions entry:

```markdown
| `std::atomic<bool>` for shutdown flag | Single cache-line, no false sharing |
```

With:

```markdown
| `std::atomic<bool>` with release/acquire ordering for shutdown flag | Single cache-line, no false sharing. **Ordering**: store uses `memory_order_release`, load uses `memory_order_acquire`. Default `seq_cst` is unnecessary — the flag is written once and read rarely, so a full memory barrier on every load is wasted cycles. On x64, acquire/release compiles to plain `mov` (no `mfence`), while seq_cst requires `mfence` or `lock`-prefixed store. |
```

Add a new forward-looking Design Decision:

```markdown
| Registry read-only assumption holds for v0.2.0 | The `std::unordered_map` logger registry has no concurrent writes after initialization. If dynamic logger management (G01f) is added in a future release, the registry will need a reader-writer lock (`std::shared_mutex`) or a lock-free concurrent hash map. |
```

Add a Lifecycle Coordination note after the Shutdown Sequence diagram:

```markdown
### Application Lifecycle Coordination

Callers **must not** call `GetLogger()` after `ShutdownLogging()` returns. The doc documents this as UB, but a production system should enforce this at the application level (not in the library):

- Recommended pattern: Set an application-level `std::atomic<bool> app_running` flag before shutdown
- All logging threads check `app_running.load(std::memory_order_acquire)` before calling `GetLogger()`
- This prevents UB at the source rather than detecting it in the library hot path (where a check would be too expensive)
```

---

## Impact if NOT Fixed

- **~50-100ns unnecessary latency** on every `GetLogger()` call that loads the shutdown flag (the `mfence` on seq_cst store, plus the full barrier on every load). For a trading system processing millions of log messages, this adds up.
- **No forward-looking guard** — when G01f (dynamic loggers) is eventually implemented, the registry read-only assumption will be violated silently
- **Application-level shutdown races** — without lifecycle coordination guidance, teams may call `GetLogger()` after shutdown and hit UB in production, which is hard to reproduce and debug

---

## Verification

1. Confirm the updated Design Decisions table contains the release/acquire spec
2. Confirm the forward-looking registry note exists under Design Decisions
3. Confirm the Application Lifecycle Coordination section exists
4. Code review: when shutdown flag is implemented, verify `store(release)` + `load(acquire)` in the actual implementation matches the design doc
5. Optional: run a microbenchmark comparing `seq_cst` vs `acquire/release` on the shutdown flag hot path — confirm measurable improvement on x64
