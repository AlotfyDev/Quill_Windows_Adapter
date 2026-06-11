# Test Design: Log Sanitization Pipeline

## Under Spec
- AA File: `AA-M18-LogSanitization.md`
- Phase: 5
- Key Requirements:
  - `SanitizingSink` wraps a Quill `Sink`, overrides `write()` to scrub patterns before delegating to inner sink
  - Pattern matching uses Aho-Corasick or hand-rolled scanner — NOT `std::regex` (avoids MSVC exponential worst-case, meets <1µs per 500-byte message)
  - Sanitization runs on Quill's **backend thread** via `Sink::write()` — frontend latency unchanged
  - Rules are immutable after construction (no mutex on `write()`); runtime updates via `atomic<shared_ptr<const Rules>>` if needed
  - `SanitizationConfig::enabled = false` adds zero overhead (sink not wrapped in factory)
  - Binary data limitation is documented: `std::string` truncates at null bytes, binary may leak secrets

## Test Harness
- **Fixture**: 
  - `TempFileSink` — a test sink that captures sanitized output to a `std::ostringstream` or temp file, for verification
  - Quill lifecycle: `Backend::start()` + `Frontend::create_or_get_logger()` with the `SanitizingSink` as the sole sink
  - For no-Quill unit tests: instantiate `SanitizingSink` directly, mock the inner sink with a test double, call `write()` directly
  - For integration tests: full Quill pipeline, log a message via the frontend, verify the output on the captured sink
- **Mocked vs Real**: Inner sink is a test spy (captures final output string). The Aho-Corasick matcher is real (it's what we're testing). Quill backend is real for integration tests, but for unit tests we can call `write()` directly.
- **Preconditions**: Aho-Corasick library available (or hand-rolled fallback compiled in). Some test messages should contain known secrets. `SanitizationConfig` with 3+ active rules.

## Scenarios

### Positive Cases
- Log message `"API key: sk-abcdef0123456789abcdef0123456789"` is scrubbed to `"API key: sk-***"` in the captured sink output
- Log message `"password = supersecret123"` is scrubbed to `"password = ***"` (case-insensitive, `$1:***` replacement captures the key name)
- Log message `"card: 4111-1111-1111-1111"` is scrubbed to `"card: ****-****-****-****"`
- Log message with no secrets passes through unchanged
- `SanitizationConfig::enabled = false` — `SanitizingSink` is not created; log message goes directly to inner sink unchanged
- `SanitizationConfig::enabled = true` with empty rules vector — log message passes through with minimal overhead (pattern scan returns immediately)
- Multiple occurrences of the same pattern in one message are all scrubbed
- Multiple different pattern types in one message are each scrubbed
- Sequential rule ordering: rule A earlier in vector takes effect; rule B later in vector does NOT re-scan already-replaced regions
- Leftmost-longest-match within a single rule: `"sk-abc sk-abcdef0123456789abcdef0123456789"` — longer match takes precedence
- `reject_on_null_byte = true`: message `"secret\x00sk-abc..."` is REJECTED (not written to inner sink, warning emitted)
- Fallback prefix matcher: pattern `"sk-"` prefix + hex digits matches `"sk-abc123..."` even in hand-rolled fallback mode

### Negative / Error Cases
- Log message containing binary data (null bytes) — the `std::string_view` passed to `write()` is truncated at the first null. Secrets after the null are NOT sanitized. Verify via a test with `"secret\x00sk-abc..."` — the null-truncated prefix is scrubbed but the suffix leaks.
- `reject_on_null_byte = false` (default): same behavior as above — null-byte truncation causes silent leak (documented limitation)
- `reject_on_null_byte = true`: message containing null byte is REJECTED — verify inner sink's `write()` is NOT called and warning is emitted
- Pattern with extremely long input (10 MB string) — `write()` must not allocate O(n) temporaries; verify bounded memory usage
- Pattern that matches the entire message (e.g., rule with `.*`) — verify correct replacement and no runaway allocation
- Empty log message (zero-length) — `write()` must handle gracefully without crash
- Inner sink `write()` throws — `SanitizingSink::write()` must let the exception propagate (Quill's backend catches exceptions from sinks)
- Replacement text longer than original (e.g., pad to fixed width) — verify output is not truncated

### Production Realities
- `SanitizingSink` with 3 rules on 500-byte messages — measure wall-clock time per `write()` call; must be <1µs average, <5µs p99.9
- `SanitizingSink` with 20 rules — verify latency is still bounded (O(n*m) worst case with naive scanner)
- Backend thread stall: if sanitization is slow, ALL other sinks are blocked. Test with a deliberately slow rule to verify the performance contract
- Memory allocation during `write()` — verify no heap allocations on the hot path (pattern state is stack-allocated). Aho-Corasick trie traversal should be allocation-free
- Log message with Unicode (UTF-8) characters — verify matching works correctly on multi-byte sequences (pattern bytes vs characters)
- LLM-generated patterns: patterns defined externally may cause catastrophic backtracking — but since std::regex is NOT used, this risk is eliminated

### Thread Safety
- `write()` is called from Quill's SINGLE backend thread — no concurrent `write()` invocations. This is Quill v10.0.1's threading model.
- If runtime rule updates are implemented via `atomic<shared_ptr<const Rules>>`:
  - `write()` reads the shared_ptr atomically (no lock)
  - Updater creates a new `Rules` object, atomically swaps the shared_ptr
  - The old rules are destroyed when the last reader releases the shared_ptr (lock-free for readers)
  - Test: update rules while backend is logging (1M messages), verify no crash, no stale reads corrupting output
- `SanitizingSink` construction happens before `Backend::start()` — no race between construction and first `write()`
- The inner sink `shared_ptr` is read-only after construction — no concurrent mutation

## Assertions
- Every secret pattern in the input is replaced by its corresponding replacement in the output
- Zero secrets leak due to overlapping patterns (e.g., a credit card number inside a password field — longest match wins, or both are applied)
- `SanitizingSink::write()` does not allocate memory on the hot path (verify via `_CrtSetAllocHook` or custom allocator tracking)
- Backend thread ID inside `write()` matches `GetCurrentThreadId()` before `Backend::start()` — verifies it runs on backend thread
- Latency: <1µs per 500-byte message with 3 rules (measured over 10K iterations, exclude first call)
- Null-byte truncation: binary data after the first null byte is NOT sanitized (documented limitation — test proves it)
- `SanitizingConfig::enabled = false`: zero calls to any matcher function (verified by static branch analysis or coverage instrumentation)
- `reject_on_null_byte = true`: message with null bytes is rejected — inner sink `write()` never called
- Sequential rule application: output depends on rule vector order — earlier rule's replacement region is skipped by later rules
- Benchmark CI gate: p99 latency < 5µs over 10K iterations of 500-byte message with 3 active rules (CI must fail if exceeded)

## Failure Mode
- A test failure where `sk-abc123` is not scrubbed: **PII/credential leak in production** — API keys written to log files, potentially shipped to log aggregators
- A test failure where sanitization takes >10µs: **backend thread stall** — all logging pauses, frontend queue fills up, memory pressure increases, eventually OOM or dropped messages
- A test failure where rule update races cause a crash: **process termination** during runtime reconfiguration
- A test failure where binary data leaks secrets: **known documented gap** — not a regression but a capability limitation
- A test failure where enabled=false still invokes matching: **performance regression** in all deployments — users who disabled sanitization pay the cost anyway

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Pattern type grammar defined via `PatternType` enum (Literal/Regex) with Aho-Corasick/ctre/re2 | Step 1 — Define Sanitization Patterns | GAP-5-M18-1 resolved; `PatternType` distinguishes literal from DFA-regex rules |
| Fallback scanner semantics documented (prefix/suffix, digit-sequence, no Unicode, case-insensitive byte folding) | Step 2 — SanitizingSink Decorator | GAP-5-M18-2 resolved; fallback capability bounds defined |
| `reject_on_null_byte` and `sanitize_binary` fields added to `SanitizationConfig` | Step 1 — Define Sanitization Patterns | GAP-5-M18-3 resolved; add scenarios for null-byte rejection |
| CI benchmark gate: p99 latency must not exceed 5µs; benchmark MUST ship with sanitization code | Step 4 — Performance Constraints | GAP-5-M18-4 resolved; add CI gate assertion + benchmark scenario |
| Sequential rule application documented (order matters, replaced regions skipped, leftmost-longest-match within a rule) | Step 1 — Define Sanitization Patterns | GAP-5-M18-5 resolved; add scenarios for rule ordering and non-overlapping replacement |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-5-M18-1 | AA spec defines patterns with regex syntax (`sk-[a-fA-F0-9]{32}`) but says "NOT std::regex." The pattern grammar is undefined — is it PCRE, glob, Aho-Corasick literal? Aho-Corasick does exact string matching, not pattern matching. The credit card pattern `\b\d{4}[-]?\d{4}...` with word boundaries and optional hyphens is NOT a fixed string — it CANNOT be implemented with pure Aho-Corasick. | The pattern grammar is incompatible with the stated algorithm. Either patterns must be literal strings (AC-compatible) or a different algorithm (DFA regex, SIMD regex) must be used. | Define the pattern grammar explicitly. If regex-lite patterns are needed, use a DFA-based regex engine (like `re2` or `ctre`) to avoid backtracking. If pure AC, patterns must be literal substrings. | ✅ RESOLVED — `PatternType` enum separates Literal (Aho-Corasick) from Regex (ctre/re2) patterns; credit card uses Regex type |
| GAP-5-M18-2 | AA spec mentions "hand-rolled byte scanner for fixed patterns" as fallback but gives no specification for what that scanner matches. If it's a digit-sequence matcher, it won't match `sk-[a-fA-F0-9]{32}` which has a prefix `sk-` and hex digits. | The fallback scanner capability is underspecified, making the test design ambiguous | Define the exact matching semantics for the hand-rolled scanner: does it support prefixes/suffixes, character classes, bounded repetition? Or is it purely literal multi-string matching? | ✅ RESOLVED — Fallback semantics defined: prefix/suffix byte sequences, digit-sequence detection, case-insensitive byte folding, no Unicode support |
| GAP-5-M18-3 | Binary data limitation is documented but not tested. The spec says "hex-encode or base64-encode binary data BEFORE logging" but provides no automatic mechanism. If a caller forgets, secrets silently leak through null-byte truncation. | Silent data leak for binary log payloads — a production incident waiting to happen | Add a compile-time or runtime check: if log message contains null bytes, emit a warning or reject the message. Or add an opt-in binary sanitization path. | ✅ RESOLVED — `reject_on_null_byte` field added to `SanitizationConfig`; `sanitize_binary` future field for hex-encode-then-scan |
| GAP-5-M18-4 | The <1µs latency target per 500-byte message is defined but there's no benchmark suite or CI gate specified. Without a performance test in CI, latency regressions will ship silently. | Performance regression in backend thread = global logging stall | Define a benchmark test (e.g., Google Benchmark or hand-rolled) that runs in CI and fails if p99 exceeds 5µs | ✅ RESOLVED — AA spec now mandates benchmark ships with code; CI gate fails if p99 > 5µs |
| GAP-5-M18-5 | Overlapping pattern behavior is not defined. If a password field contains `sk-abc...`, does the password rule or the API key rule win? What about nested matches? | Inconsistent sanitization — secrets may be partially masked or incompletely removed | Define rule application order (first match wins? longest match wins? all rules applied sequentially?). Add test for overlapping patterns. | ✅ RESOLVED — Sequential rule application defined: rules applied in vector order, replaced regions skipped, leftmost-longest-match within a rule |
