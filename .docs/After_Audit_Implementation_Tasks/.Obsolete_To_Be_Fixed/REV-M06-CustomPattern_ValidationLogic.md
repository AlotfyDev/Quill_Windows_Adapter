# REV-M06-CustomPattern — Missing Pattern Validation Logic

## Severity
⚠️ Revision | 🔴 High

## Description
AA-M06 claims to address M06-C ("silent parse errors") and lists "Invalid pattern string produces clear error (not a parse-silently-fail)" as an acceptance criterion (AA-M06:105). However, the `MakePatternFormatter` function in Step 4 contains **zero validation logic**:

```cpp
quill::PatternFormatterOptions MakePatternFormatter(const config::PatternConfig& cfg) {
    quill::PatternFormatterOptions opts;
    if (!cfg.format.empty()) {
        opts.set_pattern(cfg.format);           // <-- NO VALIDATION
    }
    if (!cfg.timestamp_format.empty()) {
        opts.set_timestamp_format(cfg.timestamp_format);  // <-- NO VALIDATION
    }
    opts.set_utc_timestamp(cfg.utc);
    return opts;
}
```

The function simply passes the pattern string to Quill's `set_pattern()` without:
- Checking for valid format specifiers (%(time), %(level), etc.)
- Rejecting empty or malformed patterns
- Handling Quill's behavior for invalid patterns (does it throw? silently ignore? produce garbage?)
- Documenting what constitutes a valid pattern

The AA file says "FIXED" for M06-C but the validation does not exist — not even as a stub.

## Root Cause
- M06-C validation was listed as an Acceptance Criterion but never mapped to an implementation step
- Step 4 is titled "Create make_pattern helper + validation" but only creates the helper — the "validation" part is absent
- The AA author appears to have considered `if (!cfg.format.empty())` as sufficient validation, which is inadequate
- No Quill API research was done to determine what `set_pattern()` accepts and how it errors
- The "FIXED" claim was carried forward from the AUDIT without verifying the implementation existed

## Exact Fix

### Fix 1: Implement actual pattern validation in `MakePatternFormatter`

Replace the no-op helper with a validated version:

```cpp
#include <quill/core/PatternFormatterOptions.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Logger_Adapter::config {

// Accepted pattern tokens (Quill v10.0.1)
inline const std::unordered_set<std::string_view> kValidPatternTokens = {
    "%(time)", "%(level)", "%(logger_id)", "%(thread)",
    "%(file)", "%(line)", "%(message)", "%(function_name)",
    "%(level_name)", "%(level_id)", "%(level_short_name)",
    "%(logger_name)", "%(thread_name)", "%(file_path)",
};

// Validate a pattern string against known tokens.
// Returns a descriptive error message on failure, empty string on success.
inline std::string ValidatePattern(const std::string& format) {
    if (format.empty()) {
        return "pattern format string must not be empty";
    }

    // Scan for %(...) tokens and validate each one
    std::size_t pos = 0;
    while (pos < format.size()) {
        auto pct = format.find('%', pos);
        if (pct == std::string::npos) break;

        auto open = format.find('(', pct);
        if (open == std::string::npos || open != pct + 1) {
            ++pos;  // literal '%' or malformed, continue
            continue;
        }

        auto close = format.find(')', open);
        if (close == std::string::npos) {
            return "unterminated pattern token starting at position "
                   + std::to_string(pct);
        }

        std::string_view token(format.data() + pct, close - pct + 1);
        if (kValidPatternTokens.find(token) == kValidPatternTokens.end()) {
            return "unknown pattern token '" + std::string(token)
                   + "' at position " + std::to_string(pct);
        }

        pos = close + 1;
    }

    return {};  // valid
}

// Quill behavior notes for invalid patterns (v10.0.1):
// - set_pattern() with an invalid token: Quill will NOT error at call time.
//   The invalid token is rendered literally (e.g., "%(bad)" appears as text).
//   This is the "silent parse" behavior M06-C aims to eliminate.
// - set_timestamp_format() with invalid specifiers: behavior depends on the
//   underlying format library; typically produces garbage or throws std::format_error.
// - Empty format string: Quill uses default pattern — not necessarily wrong,
//   but may surprise callers expecting custom output.
```

Then update `MakePatternFormatter`:

```cpp
quill::PatternFormatterOptions MakePatternFormatter(const config::PatternConfig& cfg) {
    quill::PatternFormatterOptions opts;

    if (!cfg.format.empty()) {
        // VALIDATE before passing to Quill
        std::string error = ValidatePattern(cfg.format);
        if (!error.empty()) {
            throw std::invalid_argument(
                "Logger_Adapter::MakePatternFormatter: " + error);
        }
        opts.set_pattern(cfg.format);
    }
    // else: empty pattern is allowed — Quill uses default format

    if (!cfg.timestamp_format.empty()) {
        // Note: Quill's set_timestamp_format does NOT validate format specifiers.
        // Invalid specifiers produce runtime errors from the formatting library.
        // Consider adding a try-catch around Quill's internal formatting call
        // or validating timestamp format separately.
        opts.set_timestamp_format(cfg.timestamp_format);
    }

    opts.set_utc_timestamp(cfg.utc);
    return opts;
}
```

### Fix 2: Add error handling in `InitializeLogging` call site

The wire-up code (Step 5) should catch validation errors:

```cpp
// In InitializeLogging():
try {
    quill::PatternFormatterOptions pattern = MakePatternFormatter(config.console_pattern);
    auto* logger = quill::Frontend::create_or_get_logger(entry.name, sinks, pattern);
} catch (const std::invalid_argument& e) {
    // Pattern validation failed — log via OutputDebugString and fall back to default
    OutputDebugStringA(e.what());
    // Use default pattern as fallback
    auto* logger = quill::Frontend::create_or_get_logger(entry.name, sinks);
}
```

### Fix 3: Update acceptance criteria wording

```
- [ ] Invalid pattern string produces clear error at initialization (via std::invalid_argument)
- [ ] Invalid tokens are caught and reported with positions
- [ ] Empty pattern uses Quill default (not silently accepted as custom)
- [ ] Timestamp format validation (future: validate at call time; currently deferred to Quill)
- [ ] Pattern grammar documented (what tokens are supported) — completed in Step 1
```

### Fix 4: Remove "FIXED" claim for M06-C until validation is implemented
The AA file header and body claim all M06 issues are addressed. Update to:
```
Audit Issues: M06-A (per-logger limitation) ✅, M06-B (no pattern grammar) ✅, M06-C (silent parse errors) ⚠️ (validation pending)
```

## Impact if NOT fixed
- **Silent data corruption**: An invalid pattern token like `%(lvel)` instead of `%(level)` renders literal text `%(lvel)` in log output — the programmer sees no error and believes the format is correct
- **Hard-to-debug configuration errors**: Missing timestamp format specifiers produce garbage timestamps with no diagnostic
- **False compliance**: The acceptance criterion "Invalid pattern string produces clear error" is claimed as met but is completely unimplemented
- **No regression protection**: Future code changes that introduce invalid patterns pass CI without detection

## Verification
1. **Unit test**: `TEST(M06, ValidatePattern_ValidTokens)` — pass known valid tokens, expect empty error
2. **Unit test**: `TEST(M06, ValidatePattern_InvalidToken)` — pass `"%(lvel) %(message)"`, expect non-empty error mentioning `%(lvel)`
3. **Unit test**: `TEST(M06, ValidatePattern_Empty)` — pass `""`, expect non-empty error
4. **Integration test**: `TEST(M06, MakePatternFormatter_ThrowsOnInvalid)` — call `MakePatternFormatter` with invalid pattern, expect `std::invalid_argument`
5. **Integration test**: `TEST(M06, MakePatternFormatter_DefaultOnEmpty)` — pass empty pattern, verify Quill's default pattern is used
6. **Manual test**: Run Logger_Adapter with an intentionally invalid pattern in config. Verify:
   - A descriptive error is printed to debug output
   - The application falls back to default pattern (does not crash)
   - Log output uses the default format, not garbage
