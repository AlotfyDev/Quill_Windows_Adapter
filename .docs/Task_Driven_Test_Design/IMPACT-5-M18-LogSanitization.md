# Impact Analysis: Log Sanitization Pipeline

## Summary
Total GAPs: 5 | P0: 0 | P1: 2 | P2: 3 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-5-M18-1 | ⚠️ P1 | Pattern grammar undefined: regex-like patterns (`sk-[a-fA-F0-9]{32}`, `\b\d{4}...`) are incompatible with pure Aho-Corasick literal matching. Credit card detection CANNOT work with AC. | **Secrets leak through sanitization.** Credit card numbers and regex-based patterns silently pass through unscrubbed. False sense of security compliance. | No | 🛠️ Fix now |
| GAP-5-M18-2 | 🟡 P2 | Hand-rolled scanner fallback is underspecified — matching semantics unclear. | No impact if AC or proper regex engine is used; ambiguous spec for implementers. | No | 📝 Document only |
| GAP-5-M18-3 | ⚠️ P1 | Binary data limiation documented but no automatic protection. Null-byte truncation causes secrets after null to leak unsanitized. | **Silent production data leak.** Binary log payloads (network buffers, protobuf) containing secrets bypass sanitization through null-byte truncation. Real incident risk. | No | 🛠️ Fix now |
| GAP-5-M18-4 | 🟡 P2 | No benchmark suite or CI gate for <1µs latency target. | Latency regressions ship silently; backend thread stall causes global logging freeze. | No | 📝 Document only |
| GAP-5-M18-5 | 🟡 P2 | Overlapping pattern behavior not defined (which rule wins for nested/overlapping matches). | Inconsistent sanitization: secrets may be partially masked depending on rule composition. | No | 🛠️ Fix now |

## Recommended AA Changes
- **GAP-5-M18-1**: Add `PatternType` enum (`Literal`, `Regex`) to `SanitizationRule`; document that regex patterns use DFA-safe engine (ctre/re2), not std::regex; clarify Aho-Corasick is for literal patterns only
- **GAP-5-M18-2**: Specify exact matching semantics for hand-rolled scanner fallback (prefix/suffix support, character classes, or literal-only)
- **GAP-5-M18-3**: Add null-byte detection with warning mechanism; add `reject_on_null_byte` option; document binary-safe sink workaround
- **GAP-5-M18-4**: Add note that a CI benchmark test must be implemented alongside the code
- **GAP-5-M18-5**: Add documentation defining rule application order (sequential, first-match-wins) to `SanitizationConfig`
