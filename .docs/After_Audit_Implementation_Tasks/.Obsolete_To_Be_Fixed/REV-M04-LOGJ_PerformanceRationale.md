# REV-M04-LOGJ — Deferred JSON Performance Rationale

## Severity
⚠️ Revision | 🟡 Medium

## Description
AA-M04 Step 1 contains a misleading performance rationale. The comment block reads:

```
// PERFORMANCE: JSON string escaping is CPU-intensive. For low-volume
// diagnostic and audit logging only. NOT for high-frequency paths.
```

While the warning ("not for hot path") is sound, the technical reasoning is **incorrect** for Quill v10.0.1:

- LOGJ macros (`QUILL_LOGJ_INFO`, etc.) use `QUILL_GENERATE_NAMED_FORMAT_STRING` which is a **compile-time only** operation (LogMacros.h:252-266). The format string is generated entirely at compile time — e.g., `LOGJ_INFO(logger, "Order", order_id, symbol)` expands to produce the format string `"Order {order_id}, {symbol}"`.
- At runtime, `QUILL_LOGJ_INFO` follows the exact same code path as `QUILL_LOG_INFO` — it calls `QUILL_LOGGER_CALL` which does a level check + copy-arguments-into-SPSC-queue. There is **zero JSON serialization on the frontend**.
- JSON formatting occurs **deferred on the backend thread** inside `JsonFileSink::write()`. The sink receives the named-format string and arguments, then serializes to JSON.
- Therefore: **frontend overhead of LOGJ is identical to LOG**. The CPU cost of JSON escaping does NOT affect the calling thread.

The warning "NOT for high-frequency paths" is still correct, but for these reasons:
- Larger queue entries (named format string uses more bytes per argument than positional)
- Higher backend CPU utilization from JSON serialization (can cause backend queue buildup)
- Larger output file size (JSON verbosity)

## Root Cause
- The author assumed "JSON = expensive = blocks frontend" without checking Quill's deferred serialization model
- No code-path analysis was done on `QUILL_LOGJ_INFO` vs `QUILL_LOG_INFO` — both invoke `QUILL_LOGGER_CALL` with identical mechanics
- The `QUILL_GENERATE_NAMED_FORMAT_STRING` macro was not examined; its compile-time nature was missed
- The AA was fixated on the AUDIT's M04-C claim ("hot-path JSON performance") without independently verifying the mechanism

## Exact Fix
Replace the performance comment block in `macros/Structured.hpp` (AA-M04 Step 1) with:

```cpp
// PERFORMANCE: LOGJ macros use QUILL_LOGJ_* which produce named-format strings
// ("text {name}") at COMPILE TIME via QUILL_GENERATE_NAMED_FORMAT_STRING.
// At runtime, the frontend code path is identical to QUILL_LOG_* — just a level
// check and argument copy into the SPSC queue. NO JSON serialization on the
// frontend thread.
//
// JSON formatting is DEFERRED to the backend thread inside JsonFileSink::write().
// This means:
//   - Frontend overhead of LOGJ = same as LOG (zero JSON cost on calling thread)
//   - Backend thread does JSON string escaping, key-value formatting, and output
//   - LOGJ entries consume more queue space than LOG (named format string is longer)
//   - Backend CPU utilization is higher for LOGJ vs LOG output
//
// GUIDELINE: Use LOGJ for audit trails, compliance logs, and moderate-volume
// structured output. For high-frequency trading paths, prefer LOG with plaintext
// format to reduce backend serialization load and avoid queue pressure.
//
// Verified against Quill v10.0.1: QUILL_LOGJ_INFO at LogMacros.h:646 calls
// QUILL_LOGGER_CALL with QUILL_GENERATE_NAMED_FORMAT_STRING — same call path
// as QUILL_LOG_INFO at LogMacros.h:618 with QUILL_GENERATE_FORMAT_STRING.
```

Also update AA-M04's Acceptance Criteria:
- Add: "Performance rationale correctly describes deferred JSON on backend"
- Remove the misleading implication that LOGJ adds frontend CPU cost

## Impact if NOT fixed
- Developers may over-constrain LOGJ usage, wrongly believing it adds frontend latency
- Performance tuning decisions will be made on incorrect assumptions (e.g., "LOGJ is 10x slower than LOG on the calling thread")
- Future maintainers lack accurate understanding of Quill's deferred formatting model, leading to repeated wrong assumptions
- Code review conversations devolve: "Why is LOGJ slow on frontend?" ... "It isn't"

## Verification
1. **Code path trace**: Verify `QUILL_LOGJ_INFO` at LogMacros.h:646 expands to `QUILL_LOGGER_CALL(QUILL_LIKELY, logger, nullptr, quill::LogLevel::Info, QUILL_GENERATE_NAMED_FORMAT_STRING(fmt, ...), ...)`. Confirm this is structurally identical to `QUILL_LOG_INFO` at LogMacros.h:618.
2. **Frontend benchmark**: Write a benchmark that compares `LOG_INFO` vs `LOGJ_INFO` on the calling thread only (backend stopped). Measure minimum, median, and maximum call duration. Both should be within noise (~same ns/call).
3. **Backend benchmark**: Re-enable backend with `JsonFileSink`. Compare `LOG_INFO` (plaintext file) vs `LOGJ_INFO` (JSON file) throughput. LOGJ should show lower backend throughput due to JSON serialization.
4. **Review**: Confirm the corrected rationale matches Quill v10.0.1's actual codebase behavior.
