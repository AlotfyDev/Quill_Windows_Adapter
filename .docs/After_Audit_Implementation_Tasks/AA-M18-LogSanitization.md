# AA-M18 — Log Sanitization Pipeline (After-Audit New)

> **Phase**: 5 — 🛡️ Safety & Ergonomics  
> **Effort**: 1.5-2 h  
> **Depends on**: Phase 4 (macro infrastructure), AA-C05 (Thread Model — sanitization executes on the backend thread; thread-safety and jitter constraints flow from the backend execution model)
> **Capability Gap**: No PII/secret scrubbing — trading logs may leak API keys, passwords, trade secrets
> **Fix Applied**: FIX-M18-LogSanitization_ThreadAndRegex.md — corrected thread model, replaced std::regex, removed mutex, documented binary data limitation

---

## Problem

Trading log messages may contain:
- API keys (`sk-abc123...`) for exchange connections
- Trade secrets (proprietary strategy parameters)
- PII (trader IDs, client account numbers)
- Credentials (database connection strings)

No task currently addresses log sanitization. A configurable filter chain is needed that wraps Quill's sink and runs on the **backend** thread (via `Sink::write()` override).

---

## Implementation Plan

### Step 1 — Define Sanitization Patterns

```cpp
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Logger_Adapter::config {

enum class PatternType {
    Literal,   // Exact substring match via Aho-Corasick (fastest, O(n+m))
    Regex      // DFA-safe regex via ctre or re2 (no backtracking, NOT std::regex)
};

struct SanitizationRule {
    std::string name;              // human-readable name
    std::string pattern;           // pattern string — semantics depend on type:
                                   //   Literal: exact substring to match (e.g., "password")
                                   //   Regex:   DFA-safe regex (e.g., "sk-[a-fA-F0-9]{32}")
                                   //   See PatternType. No std::regex — use ctre/re2 for regex rules.
    std::string replacement;       // replacement text (e.g., "***")
    bool enabled = true;
    bool case_sensitive = true;
    PatternType type = PatternType::Literal;
};

// Rule application order: rules are applied SEQUENTIALLY in vector order.
// For each rule, all non-overlapping matches are replaced in the input.
// If a later rule's pattern matches within a previously replaced region,
// the later replacement is NOT applied (replaced regions are skipped).
// ⚠️ Overlapping matches within a single rule: leftmost-longest-match wins.
// ⚠️ If a Literal pattern is also a substring of a Regex pattern's match,
//    the Literal rule's replacement takes effect first (if it appears earlier).
//    Order rules from most-specific to least-specific for predictable behavior.

struct SanitizationConfig {
    bool enabled = false;          // master switch — off by default for perf
    std::vector<SanitizationRule> rules = {
        {"API key",    "sk-[a-fA-F0-9]{32}",       "sk-***", true,  true},
        {"Password",   "(password|passwd|pwd)\\s*[:=]\\s*\\S+", "$1:***", true, false},
        {"CreditCard", "\\b\\d{4}[-]?\\d{4}[-]?\\d{4}[-]?\\d{4}\\b", "****-****-****-****", true, true},
    };

    // ⚠️ Binary data limitation:
    // Log messages containing binary data (raw network buffers, protobuf, etc.)
    // may contain secrets that pass through unsanitized because:
    // - The underlying matcher operates on std::string, which truncates at null bytes
    // - Binary data may match patterns unintentionally or miss them
    //
    // Workarounds:
    // 1. Hex-encode or base64-encode binary data BEFORE logging
    // 2. Use a dedicated binary-sanitized sink that processes byte arrays
    // 3. Avoid logging raw binary data in production paths
    bool sanitize_binary = false;  // FUTURE: hex-encode + scan

    // If true, log messages containing null bytes are REJECTED (not written)
    // and a warning is emitted. This prevents silent secret leakage through
    // null-byte truncation. Default: false (backward compat).
    // Use in production environments where binary payloads are expected
    // and must be fully sanitized.
    bool reject_on_null_byte = false;
};

} // namespace Logger_Adapter::config
```

### Step 2 — Create SanitizingSink Decorator

Wrap Quill's sink with a sanitization layer:

```cpp
namespace Logger_Adapter::sinks {

// Aho-Corasick matcher for keyword/password patterns — O(n + m) runtime, no backtracking.
// Replaces std::regex which on MSVC has exponential worst-case and 10-50µs latency.
#ifndef __has_include(<ahocorasick/aho_corasick.hpp>)
// Fallback: hand-rolled byte scanner for fixed patterns (credit cards, API key formats).
// Simple digit-sequence matching, no regex backtracking.
// Matching semantics:
//   - Literal-only: no character classes, no bounded repetition, no word boundaries
//   - Supports prefix/suffix byte sequences (e.g., "sk-" as prefix before hex digits)
//   - Digit-sequence detection for credit card-like numbers (consecutive digits with optional separators)
//   - Case-insensitive matching via byte folding (lower-casing each byte)
//   - No Unicode support in fallback mode — use Regex PatternType for UTF-8 patterns
#endif

class SanitizingSink : public quill::Sink {
public:
    explicit SanitizingSink(std::shared_ptr<quill::Sink> inner,
                            const config::SanitizationConfig& cfg);

    // Override Quill's write method — sanitize before delegating.
    // Called on Quill's BACKEND thread via SinkManager — NOT the frontend.
    // Ref: quill::core::SinkManager confirms "sinks are used by the backend thread"
    void write(quill::MacroMetadata const& metadata,
               quill::Buffer const& buffer) override;

private:
    std::shared_ptr<quill::Sink> inner_;
    struct CompiledRule {
        std::string pattern_str;     // original pattern string
        std::string replacement;
        // Compiled matcher state (AhoCorasick node ID or hand-rolled scanner)
    };
    // RULES ARE IMMUTABLE after construction — no mutex needed on write() path.
    // If runtime rule changes are required, use std::shared_ptr<const Rules> with atomic swap.
    const std::vector<CompiledRule> rules_;
};

} // namespace Logger_Adapter::sinks
```

### Step 3 — Wire in SinkFactory

```cpp
// In SinkFactory::CreateSink():
if (config.sanitization.enabled) {
    sink = std::make_shared<SanitizingSink>(sink, config.sanitization);
}
```

### Step 4 — Performance Constraints

```cpp
// Performance contract:
// - Each rule is pre-compiled at SanitizingSink construction (no compilation on hot path)
// - Each check is O(input_length) worst case — acceptable for typical log messages (<1KB)
// - write() runs on Quill's BACKEND thread via Sink::write() — NOT the frontend
//   (Ref: quill::core::SinkManager: "sinks are used by the backend thread")
// - Implications:
//   (a) Frontend latency is UNCHANGED by sanitization — no overhead on calling thread
//   (b) Backend thread does all matching work — if a scanner is slow, ALL sink processing stalls
//   (c) For ultra-low-latency paths, disable sanitization or use allow-listed loggers
// - Benchmark target: <1µs per message with 3 active rules on 500-byte input
//   (Note: std::regex on MSVC CANNOT meet this target; use Aho-Corasick or SIMD scanner instead)
// - 🧪 CI gate: A benchmark test (Google Benchmark or equivalent) MUST be implemented
//   alongside the sanitization code. CI must fail if p99 latency exceeds 5µs.
//   Without this, latency regressions ship silently — see GAP-5-M18-4.
```

---

## Acceptance Criteria

- [ ] API key pattern `sk-[a-fA-F0-9]{32}` is scrubbed from log output (replaced with `sk-***`)
- [ ] Credit card number pattern is scrubbed
- [ ] `SanitizationConfig::enabled = false` adds zero overhead (no filter applied)
- [ ] `SanitizationConfig::enabled = true` with empty rules list adds minimal overhead (passthrough)
- [ ] Sanitization runs on **backend** thread via `Sink::write()` — does not affect frontend throughput; benchmark scanner latency to ensure backend jitter is bounded
- [ ] No `std::mutex` in write path — rules are immutable after construction (or use atomic shared_ptr swap for runtime updates)
- [ ] Binary data limitation documented in `SanitizationConfig` with recommended workarounds
- [ ] Benchmark: <1µs per 500-byte message with 3 active rules (using Aho-Corasick or hand-rolled scanner, NOT std::regex)
- [ ] References AA-C05 (Thread Model) for backend thread execution contract
- [ ] Build succeeds Debug|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/config/SanitizationConfig.hpp` | New file |
| `Logger_Adapter/sinks/SanitizingSink.hpp` | New file |
| `Logger_Adapter/sinks/SanitizingSink.cpp` | New file |
| `Logger_Adapter/setup/SinkFactory.hpp` | Wire sanitization decorator |
| `Logger_Adapter/logging/LoggingConfig.hpp` | Add `SanitizationConfig` field |
