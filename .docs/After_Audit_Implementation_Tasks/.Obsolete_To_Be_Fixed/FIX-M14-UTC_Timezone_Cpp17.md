# FIX-M14 — UTC Timezone: C++17 Designated Initializer Compliance

**Severity**: ❌ Fail (blocks compilation)
**AA File**: `AA-M14-UTC_Timezone.md`
**Phase**: 1 — Foundation
**Effort**: 5 min

---

## Description

`AA-M14-UTC_Timezone.md` Step 2 uses a **C++20 designated initializer** for aggregate initialization of `PatternConfig`:

```cpp
config::PatternConfig pattern{
    .utc_timestamp = true,   // ← C++20 syntax
    // ...
};
```

The project targets **C++17** (`/std:c++17`). Designated initializers (`{.field = value}`) are a C++20 feature. MSVC in C++17 mode will **reject this with a compile error**.

The C++17 compliance audit (`AA-COMPLIANCE-Windows-Cpp17-Audit.md`) concluded "No C++20 features are used anywhere" — but `AA-M14` was outside that audit's scope, so the violation was missed.

---

## Root Cause

1. **AA-M14 was promoted from G01** after the C++17 compliance audit was written. The audit never re-scanned to include the new file.
2. **No compile check was run** on the AA file's code snippets. Designated initializers are visually subtle — easy to miss in review.
3. **Author assumed C++20 availability** because the feature has been supported by MSVC since VS 2019 16.1, but MSVC only enables it under `/std:c++20` or later.

---

## Exact Fix

Replace the designated initializer in `AA-M14-UTC_Timezone.md` Step 2 with one of the following C++17-compatible approaches:

### Option A — Aggregate initialization in declaration order (recommended)

```cpp
struct LoggingConfig {
    config::PatternConfig pattern{
        true,   // utc_timestamp = true  (1st field in PatternConfig)
        /* ... remaining PatternConfig fields in declaration order ... */
    };
};
```

**Requirement**: `PatternConfig` fields must be declared with `utc_timestamp` first, and all subsequent fields must be provided in declaration order. If `PatternConfig` has other fields after `utc_timestamp`, they need explicit values.

### Option B — Setter after construction (most maintainable)

```cpp
struct LoggingConfig {
    config::PatternConfig pattern{};
    LoggingConfig() {
        pattern.utc_timestamp = true;
    }
};
```

### Option C — Default member initializer in PatternConfig (if changing the struct)

Since `PatternConfig` already declares `bool utc_timestamp = true;` (the default is already `true` in Step 1), Step 2 can simply be:

```cpp
struct LoggingConfig {
    config::PatternConfig pattern;  // default-initialized → utc_timestamp == true
};
```

This works because `PatternConfig::utc_timestamp` already defaults to `true` in the struct definition. No explicit initializer needed.

**Recommended: Option C** — simplest, no repetition, fully C++17.

Also verify: `AA-M14-UTC_Timezone.md` Step 1 already sets the default to `true`, so Option C requires **zero** additional initialization code in `LoggingConfig`.

---

## Impact if NOT Fixed

- **Build breaks**: MSVC emits `error C7555: designated initializers are not supported in this mode`
- Blocks all Phase 1 work that depends on M14
- Blocks CI pipelines on Debug|x64 builds
- A developer unaware of the C++17 constraint might "fix" by removing `/std:c++17`, silently introducing C++20 requirement across the entire project

---

## Verification

1. Confirm that `PatternConfig::utc_timestamp` defaults to `true` in Step 1 of AA-M14
2. Verify replacement compiles: `cl /std:c++17 /c <file>` — no C7555 error
3. If using Option C, confirm no explicit designated initializer syntax remains in any file touched by M14
4. Run `Select-String -Path "Logger_Adapter\**" -Pattern "\{\s*\.\w+\s*="` to verify no other designated initializers exist
