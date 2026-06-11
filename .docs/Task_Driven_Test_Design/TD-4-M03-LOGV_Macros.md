# Test Design: LOGV Variable-Name Macros

## Under Spec
- AA File: `AA-M03-LOGV_Macros.md`
- Phase: 4
- Key Requirements:
  - `LOGV_*` macros wrap `QUILL_LOGV_*` macros in `macros/VariableArgs.hpp`
  - Structured key-value output with variable names auto-extracted via preprocessor stringification
  - All severity levels: TRACE, DEBUG, INFO, WARN, ERR, CRIT
  - Performance warning documented — DIAGNOSTIC USE ONLY, not for hot path
  - Compile-time filtering via `QUILL_COMPILE_ACTIVE_LOG_LEVEL` works for LOGV too

## Test Harness
- **Fixture**: Initialize Quill backend + frontend with a `FileSink` writing to a temp file. `quill::Backend::start()` in `SetUp`, `quill::Backend::stop()` in `TearDown`. Temp file path captured per test.
- **Real vs mock**: Real Quill, real `FileSink`. Read actual file output to verify structured content. No mocking — we need to verify Quill's actual LOGV output format.
- **Preconditions**: `QUILL_DISABLE_NON_PREFIXED_MACROS` is NOT defined (so `LOGV_INFO` etc. are available from Quill directly). Logger_Adapter's `LOGV_*` macros are included after `quill/LogMacros.h`.

## Scenarios

### Positive Cases
- LOGV_INFO with 1 variable: `LOGV_INFO(logger, "count", count)` where `int count = 42`. Output must contain `count: 42`.
- LOGV_INFO with 2 variables: `LOGV_INFO(logger, "trade", order_id, price)` where `int order_id=12345; double price=150.25`. Output must contain `order_id: 12345, price: 150.25`.
- LOGV_INFO with 0 variables (description text only): `LOGV_INFO(logger, "hello")`. Output must contain the text "hello".
- LOGV_WARN: `LOGV_WARN(logger, "low balance", balance)`. Output must respect WARN level severity.
- LOGV_ERR: `LOGV_ERR(logger, "fail", errno, msg)`. Output must contain `errno: <value>, msg: <value>`.
- LOGV_CRIT: `LOGV_CRIT(logger, "fatal", code)` with `int code=-1`. Output must contain `code: -1`.
- String values: `LOGV_INFO(logger, "user", name)` where `std::string name = "alice"`. Output must contain `name: alice`.
- Mixed types: `LOGV_INFO(logger, "record", id, label, ratio)` with `int, std::string, double`. All three values appear with correct names.
- Copy-paste verification: The header `macros/VariableArgs.hpp` includes `#include <quill/LogMacros.h>` and defines all 6 macros exactly as specified.
- QUILL_DISABLE_NON_PREFIXED_MACROS: When defined, Quill's `LOGV_INFO` etc. are unavailable. Verify that Logger_Adapter's `LOGV_*` wrappers in `VariableArgs.hpp` compile and produce correct output under this configuration.
- Future LOGV_LIMIT: AA spec notes `LOGV_LIMIT`, `LOGV_BACKTRACE`, `LOGV_TAGS` wrappers are deferred — verify that `QUILL_LOGV_INFO_LIMIT(...)` can be used directly as a fallback.

### Negative / Error Cases
- No logger (nullptr dereference): `LOGV_INFO(nullptr, "test", x)`. This is UB — document that caller must ensure valid logger. NOT tested at runtime, but STATIC_ASSERT or contract annotation should be considered.
- Empty variant args: `LOGV_INFO(logger, "")`. Output must not crash; Quill should handle empty format string gracefully (uses literal "").
- Very large values: `std::string huge(100000, 'A')` as a LOGV value. Frontend must copy the string into internal buffer, backend must flush it without OOM. Use bounded memory test (verify process RSS does not grow unbounded after 10k large LOGV calls).
- Compile-time disabled LOGV: Set `QUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_WARNING`. `LOGV_DEBUG(logger, "x", x)` must expand to `(void)0` — verify no codegen for debug LOGV.

### Production Realities
- High-frequency LOGV misuse: Call `LOGV_INFO` at 100k msg/sec in tight loop for 1 second. Measure CPU usage vs equivalent `LOG_INFO`. LOGV must be measurably more expensive (validates the "diagnostic only" warning). Document the ratio.
- Mixed LOG and LOGV on same logger: Interleave calls to `LOG_INFO` and `LOGV_INFO`. All output must be correctly interleaved without corruption or interleaved metadata.
- Perf collection with LIMIT macros: `LOGV_INFO_LIMIT(std::chrono::milliseconds(100), logger, "perf", latency)` — only 1 message per 100ms should appear. Verify suppressed count appears in the allowed message.

### Thread Safety
- LOGV called from multiple threads: 4 threads each calling `LOGV_INFO` concurrently. No interleaving, no garbled key-value pairs, no data races.
- LOGV macro expansion: The `__VA_ARGS__` in variadic macros is evaluated as separate expressions. No shared mutable state in the arg expressions. Each arg expression is sequenced before the next.
- Counter increment as LOGV arg: `int ctr = 0; LOGV_INFO(logger, "ctr", ctr++, "val", get_value())`. Quill's frontend evaluates all args before enqueuing. Order of evaluation is unspecified between args — must not be relied upon.

## Assertions
- `LOGV_INFO(logger, "test", x)` output regex must match `.*test\s*\[x:\s*<value>.*\]` (Quill v10.0.1 uses `[x: value]` format within brackets, after description text)
- All 6 severity macros (`LOGV_TRACE` through `LOGV_CRIT`) compile and produce output at correct log level
- `QUILL_COMPILE_ACTIVE_LOG_LEVEL` filtering removes LOGV macros at compile time — verify with objdump/size that no LOGV string constants appear when level is above the macro's level
- Performance warning comment present in `VariableArgs.hpp` — grep for "HOT PATH" or "DIAGNOSTIC" in the file

## Failure Mode
- LOGV output missing variable names → **silent data loss** for structured logging. Operators cannot search/filter by field name.
- LOGV output garbled or interleaved → **data corruption**. Production debugging tools relying on structured output will parse garbage.
- LOGV crash with null logger → **crash** on any null logger call. Since LOGV is for diagnostics, this would kill production debugging sessions.
- Compile-time filtering not working → **performance regression**. LOGV would execute on hot path, slowing down trading throughput.

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| LOGV examples use bare identifiers (not string literals) | Step 1 — documentation | Scenario already uses bare identifiers; assertion confirmed |
| Expected output format confirmed `[var: value]` | Step 2 — Test output | Assertion confirmed as matching Quill v10.0.1 actual format |
| QUILL_DISABLE_NON_PREFIXED_MACROS behavior documented | Step 1 — NOTE | Added scenario for defined case |
| Spec Gap GAP-4-M03-1 | String literal usage | Marked ✅ RESOLVED |
| Spec Gap GAP-4-M03-2 | Wrong output format | Marked ✅ RESOLVED |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-4-M03-1 | AA spec example `LOGV_INFO(order_log, "order_id", order_id, "price", price)` is incorrect for Quill v10.0.1. The LOGV macros use `#var` preprocessor stringification on bare identifiers. Passing string literals like `"price"` results in `#"price"` → `"\"price\""` (quotes included in the key name). The correct call is `LOGV_INFO(order_log, "trade", order_id, price)` where bare identifiers `order_id` and `price` become key names via stringification. | The AA spec will produce incorrect output with double-quoted key names. This causes operator confusion — searching for `price=` in logs would miss entries because they appear as `"price":`. | Fix the AA spec example to use bare identifiers: `LOGV_INFO(order_log, "trade", order_id, price)`. Add a comment explaining that LOGV auto-extracts variable names via preprocessor `#`, so identifiers, not string literals, must be passed. | ✅ RESOLVED |
| GAP-4-M03-2 | The AA spec's expected output `[INFO] order_id=12345, price=150.25` does not match Quill v10.0.1's actual LOGV format. Quill generates `text [var_name: value, ...]` via `QUILL_GENERATE_FORMAT_STRING`. The actual format is `order_id [order_id: 12345, price: 150.25]` when called correctly, or garbled when called with string literals. | Relying on incorrect expected output means acceptance tests will falsely pass or fail. Operators depend on the format for log parsing. | Update expected output to match Quill v10.0.1 format. Document the actual Quill format `%(named_args)` pattern token behavior for LOGV output. | ✅ RESOLVED |
| GAP-4-M03-3 | The AA spec does not define `LOGV_BACKTRACE`, `LOGV_LIMIT`, or `LOGV_LIMIT_EVERY_N` wrappers, but Quill provides these macro families. Power users who want rate-limited structured logging have no Logger_Adapter interface. | Missing surface area limits adoption. Users who need both rate limiting and structured kv logging must drop down to raw Quill macros, bypassing Logger_Adapter's config. | Add a second tier of convenience macros: `LOGV_INFO_LIMIT(interval, logger, ...)`, `LOGV_WARN_LIMIT_EVERY_N(n, logger, ...)`, etc. Document these separately from the basic set. | 🔮 Future — AA spec defers to direct Quill macro usage |
