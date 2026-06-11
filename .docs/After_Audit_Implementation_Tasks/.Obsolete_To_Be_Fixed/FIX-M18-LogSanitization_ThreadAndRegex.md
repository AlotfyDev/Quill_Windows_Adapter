# FIX-M18-LogSanitization — Thread Model, Regex Performance, and Binary Data

## Severity
❌ Fail | 🔴 High

## Description
AA-M18 has four architectural and production-critical issues:

1. **Wrong thread documentation (🔴 High)**: AA-M18 Step 4 states "Filter runs on the frontend thread before enqueue". This is **incorrect**. `SanitizingSink::write()` overrides `quill::Sink::write()`, which is called by Quill's backend thread. SinkManager confirms: "The sinks are used by the backend thread" (SinkManager.h:55). Sanitization runs on the **backend**, not the frontend. This means:
   - Frontend latency is **unaffected** (better than stated) — the AA understates the performance benefit
   - But a slow regex blocks **all** sink processing on the backend (worse than stated)
   - Backend jitter from regex directly impacts all loggers, not just the sanitized sink

2. **`std::regex` latency (🔴 High)**: MSVC STL `std::regex` uses backtracking with exponential worst-case complexity. Credit-card patterns (`\b\d{4}[-]?\d{4}[-]?\d{4}[-]?\d{4}\b`) on realistic input can take 10–50µs, not the claimed <1µs. On the backend thread, this causes queue buildup, lost log entries, and systemic latency spikes.

3. **Binary data undefined behavior (🟡 Medium)**: `std::regex` operates on null-terminated strings. Log messages containing binary data (network buffers, serialized protobufs, encrypted payloads) will be truncated at the first null byte. Secrets embedded in binary data pass through unsanitized. There is no documented limitation or handling mechanism.

4. **`std::mutex` in write path (🟡 Medium)**: `rules_mutex_` in `SanitizingSink::write()` (line 81) introduces locking on the backend's hot path. If `SetRules()` is called during high-throughput logging, priority inversion occurs — `write()` blocks on the mutex while the backend thread could be processing other sinks.

## Root Cause
- AA-M18 was created as a "new" task (no AUDIT baseline) and skipped architectural review
- No cross-reference to AA-C05 (Thread Model) — the sink execution context was never verified
- The author assumed "sink decorator = pre-processing = frontend" without checking Quill's architecture
- `std::regex` was chosen for convenience without benchmarking MSVC's implementation or considering alternatives
- Binary data was an overlooked edge case common in trading systems (FIX messages, raw market data)

## Exact Fix

### Fix 1: Correct thread documentation
Replace Step 4 comment block with:

```cpp
// Performance contract:
// - Each rule regex is pre-compiled at SanitizingSink construction (no compilation on hot path)
// - write() runs on Quill's BACKEND thread via Sink::write() — NOT the frontend
//   (Ref: quill::core::SinkManager confirms "sinks are used by the backend thread")
// - Implications:
//   (a) Frontend latency is UNCHANGED by sanitization — no overhead on calling thread
//   (b) Backend thread does all regex work — if a regex is slow, ALL sink processing stalls
//   (c) For ultra-low-latency paths, disable sanitization or use allow-listed loggers
// - Benchmark target: <1µs per message with 3 active rules on 500-byte input
//   (Note: std::regex on MSVC CANNOT meet this target; use Aho-Corasick or SIMD scanner instead)
```

Also update acceptance criterion 5: "Sanitization runs on **backend** thread — does not affect frontend throughput; benchmark regex latency to ensure backend jitter is bounded"

### Fix 2: Replace `std::regex` with a pre-compiled DFA or Aho-Corasick matcher

**Do NOT use `std::regex` in production** on MSVC for the log hot path. Replace with:

```cpp
// Option A: Aho-Corasick (recommended for keyword/password patterns)
#include <vector>
#include <string>
#include <memory>

namespace Logger_Adapter::sinks {

class AhoCorasickMatcher {
public:
    explicit AhoCorasickMatcher(const std::vector<config::SanitizationRule>& rules);
    // Returns sanitized output; runs in O(n + m) where n = input length, m = total pattern length
    std::string apply(const std::string& input) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Option B: SIMD-based scanner for fixed patterns (credit cards, API key formats)
// Use a simple byte-by-byte scanner with manual state machine — no backtracking
// Example: credit card scan is just digit-sequence matching, not a full regex
}
```

Replace `CompiledRule` in SanitizingSink:

```cpp
struct CompiledRule {
    // REPLACE: std::regex pattern;
    // WITH:
    std::string pattern_str;        // original pattern (for documentation)
    std::string replacement;
    // Use AhoCorasickMatcher or hand-rolled scanner
};
```

Remove `<regex>` include from SanitizationConfig.hpp.

### Fix 3: Remove mutex from write path
Make rules immutable after construction. Remove `SetRules()` and the mutex:

```cpp
class SanitizingSink : public quill::Sink {
public:
    explicit SanitizingSink(std::shared_ptr<quill::Sink> inner,
                            const config::SanitizationConfig& cfg);
    void write(quill::MacroMetadata const& metadata,
               quill::Buffer const& buffer) override;
private:
    std::shared_ptr<quill::Sink> inner_;
    // RULES ARE IMMUTABLE after construction
    const std::vector<CompiledRule> rules_;  // const — no mutex needed
};
```

If runtime rule changes are absolutely required, use `std::shared_ptr<const Rules>` with atomic swap:

```cpp
std::shared_ptr<const std::vector<CompiledRule>> rules_;
// write() copies shared_ptr (atomic load), then uses const rules
// Updates: build new vector, atomic store to shared_ptr
```

### Fix 4: Document binary data limitation
Add to `SanitizationConfig`:

```cpp
struct SanitizationConfig {
    // ... existing fields ...
    
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
};
```

### Fix 5: Add AA-C05 cross-reference
In the AA-M18 file header, add dependency:
```
Depends on: AA-C05 (Thread Model) — sanitization executes on the backend thread;
            thread-safety and jitter constraints flow from the backend execution model
```

## Impact if NOT fixed
- **Performance**: `std::regex` on MSVC causes 10-50x backend jitter vs claimed <1µs, leading to dropped log entries and systemic latency in the logging pipeline
- **Security**: Binary-embedded secrets bypass sanitization — a compliance failure for trading systems that handle FIX messages with embedded credentials
- **Livelock**: `std::mutex` contention in `write()` during rule updates causes priority inversion on the backend thread, stalling all sinks
- **Misleading documentation**: Developers are told sanitization is "frontend" (low-risk) when it is actually "backend" (high-risk for jitter). Incorrect thread model leads to wrong performance expectations and debugging confusion

## Verification
1. **Thread model test**: Insert `OutputDebugStringA` with `GetCurrentThreadId()` in `SanitizingSink::write()`. Verify the thread ID differs from the calling thread's ID and matches `Backend::get_thread_id()`
2. **Regex benchmark**: Run `std::regex` (sk-api-key pattern + credit-card pattern + password pattern) on 500-byte messages with 1M iterations. Measure p50, p99, p99.9, max. If max > 5µs, consider regex replacement mandatory (expected on MSVC)
3. **Binary data test**: Log a message with `"password=secret\0\x00\x01\x02"`. Verify that `password=secret` is NOT sanitized due to null truncation. Apply Fix 4 documentation
4. **Mutex-free verification**: After Fix 3, verify no mutex is held in `write()` path by inspecting call graph. Test concurrent log + config update under TSAN
5. **Compilation test**: Verify `SanitizingSink` compiles as a subclass of `quill::Sink` (v10.0.1). Check for pure-virtual overrides and constructor compatibility
