# Impact Analysis: Backtrace Logging

## Summary
Total GAPs: 5 | P0: 0 | P1: 2 | P2: 3 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-3-C03-1 | ⚠️ P1 | Spec does not define behavior of `LOG_BACKTRACE` when `init_backtrace()` was not called. If Quill asserts/crashes, misconfigured logger can terminate the process. | Configuration error (backtrace_enabled=true but init_backtrace not called) could crash the trading system process. | No | 🛠️ Fix now |
| GAP-3-C03-2 | 🟡 P2 | `flush_backtrace()` semantics undefined: blocking (waits for backend) or non-blocking (enqueue only)? | Low — shutdown timing documentation gap. | No | 📝 Document only |
| GAP-3-C03-3 | 🟡 P2 | No spec on ring buffer content after flush — cleared or re-flushable? Second flush could produce duplicate output. | Low — duplicate output on double-flush is minor. | No | 📝 Document only |
| GAP-3-C03-4 | ⚠️ P1 | `BacktraceConfig::flush_on_level` is `uint32_t` with no validation — value 99 silently disables auto-flush. | Auto-flush never triggers; backtrace data stays in ring buffer and is never output. Operator sees Error but has no pre-crash context. | No | 🛠️ Fix now |
| GAP-3-C03-5 | 🟡 P2 | No discussion of memory allocation for ring buffer. Emergency=5000 × 200 bytes = 1MB per logger; 5 loggers = 5MB. | In memory-constrained trading system, undocumented allocations may cause unexpected pressure. | No | 📝 Document only |

## Recommended AA Changes
- GAP-3-C03-1 🛠️ Fix now: Add guard in `LOG_BACKTRACE` macro or document that init_backtrace must be called before use. Add `logger->is_backtrace_initialized()` check if available in Quill v10.0.1.
- GAP-3-C03-2 📝 Document only: Document that `flush_backtrace()` is synchronous (blocks until content is queued in backend). `Backend::stop()` provides durability guarantee.
- GAP-3-C03-3 📝 Document only: Document that Quill's `flush_backtrace()` clears ring buffer after flush. Verify with test: second flush produces no output.
- GAP-3-C03-4 🛠️ Fix now: Add validation for `flush_on_level` in [0-7] range. Clamp or warn on invalid values in `InitializeLogging()`.
- GAP-3-C03-5 📝 Document only: Add memory footprint note: "Each backtrace entry sized by Quill internal buffer. Total = sum(capacity_i × entry_size) for all backtrace-enabled loggers."
