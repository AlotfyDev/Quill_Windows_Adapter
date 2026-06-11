# Impact Analysis: Rate-Limited Macros

## Summary
Total GAPs: 5 | P0: 0 | P1: 1 | P2: 4 | API Changes: 2

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-2-M05-1 | 🟡 P2 | Race at second boundary: two threads see `now != last` simultaneously | May allow up to (N + thread_count) messages in transition window — acceptable for rate limiting | No | 📝 Document only |
| GAP-2-M05-2 | ⚠️ P1 | `LOG_GLOBAL_LIMIT` passes `max_per_sec` every call but only first-call value wins | Operators think different call sites use different limits; first-call-dependent silent misconfiguration | Yes | 🛠️ Fix now |
| GAP-2-M05-3 | 🟡 P2 | No `ResetForTesting()` — singleton can't isolate tests | Cannot write deterministic unit tests; must rely on flaky sleep-based integration tests | Yes | 🛠️ Fix now |
| GAP-2-M05-4 | 🟡 P2 | `memory_order_relaxed` consequences on ARM/Power not discussed | On ARM64 (increasingly common: Graviton, Apple Silicon), rate counts may be slightly inaccurate | No | 📝 Document only |
| GAP-2-M05-5 | 🟡 P2 | Inline functions with macro get different counters per TU (ODR surprise) | Unexpected behavior: two calls to same inline function from different TUs get independent limiters | No | 📝 Document only |

## Recommended AA Changes
- **GAP-1**: Add comment documenting the race explicitly as "best-effort rate limiting during contention"
- **GAP-2**: Remove `max_per_sec` parameter from `LOG_GLOBAL_LIMIT` macro; require explicit `SetMaxPerSecond()` call during initialization
- **GAP-3**: Add `void ResetForTesting() noexcept` to `GlobalRateLimiter`; document as test-only API
- **GAP-4**: Add architecture note: relaxed ordering is safe for approximate counting but NOT for precise synchronization
- **GAP-5**: Add documented warning that inline functions with `LOG_LIMIT_PER_SEC` do NOT share counters across TUs
