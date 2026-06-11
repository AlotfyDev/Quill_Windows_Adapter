# AA-M10 — Result Guarded Access (After-Audit Corrected)

> **Phase**: 5 — 🛡️ Safety & Ergonomics  
> **Effort**: 1 h  
> **Depends on**: AA-M09 (Result Monadic — modifies same file)  
> **v1.x Reference**: TASK-M10-ResultGuardedAccess.md  
> **Audit Issues**: M10-A (policy choice), M10-B (ValueUnsafe hot path)  
> **⚠️ Validation Note**: AUDIT-M10 issues were completely mismatched with holistic (DLL ODR, noexcept claims not from audit). This AA file uses the holistic audit issues exclusively.

---

## Problem

`Result<T>::Value()` and `Result<T>::Error()` have **no guards**. Calling `Value()` on an error result or `Error()` on a value result is **undefined behavior** — the union member is not the active one.

---

## Corrected Implementation Plan

### Step 1 — Choose Error-Handling Policy

```cpp
// Policy for guarded access:
//   Debug builds:   assert(has_value_) — catches bugs in dev
//   Release builds: std::terminate — clean death rather than UB or silent corruption
//
// Rationale:
//   - Assert in Debug catches the developer error immediately
//   - std::terminate in Release: calling Value() on error is a PROGRAMMING BUG
//     that must be fixed, not hidden. Returning a default silently masks the bug.
//   - std::optional<T> is NOT used as a return for the default path because it
//     would change the API return type from T& to std::optional<T>&, breaking
//     all existing callers.
//   - No exception (std::logic_error) because Result is used in noexcept paths.
//   - static default_value<T> is AVOIDED — it causes ODR violations in DLL builds
//     (each DLL gets its own instance of the static local).
//
// ⚠️ MSVC note: `assert()` is controlled by `_DEBUG`, not `NDEBUG`.
//   The `!defined(NDEBUG)` check works with standard MSVC Debug configs
//   (`_DEBUG` is set, `NDEBUG` is not), but non-standard builds (e.g.,
//   `/O2` without defining `NDEBUG`) may have assert active in what the
//   spec calls "Release." If you use a non-standard MSVC config, adjust
//   the guard to `#ifdef _DEBUG`.
```

Implementation:

```cpp
template<typename T>
class Result {
public:
    T& Value() noexcept {
        if (!has_value_) {
#if !defined(NDEBUG)
            assert(has_value_ && "Result::Value() called on error — check IsOk() first");
#else
            // Release: programming bug → terminate cleanly
            std::terminate();
            __assume(false); // compiler barrier: prevent speculative load of value_
#endif
        }
        return value_;
    }

    const T& Value() const noexcept {
        if (!has_value_) {
#if !defined(NDEBUG)
            assert(has_value_ && "Result::Value() called on error — check IsOk() first");
#else
            std::terminate();
            __assume(false); // compiler barrier: prevent speculative load of value_
#endif
        }
        return value_;
    }

    // Rvalue overload: returns T by value to avoid dangling references on temporaries.
    T Value() && noexcept {
        if (!has_value_) {
#if !defined(NDEBUG)
            assert(has_value_ && "Result::Value() called on error — check IsOk() first");
#else
            std::terminate();
            __assume(false); // compiler barrier: prevent speculative load of value_
#endif
        }
        return std::move(value_);
    }
};
```

### Step 2 — Add `ValueUnsafe()` for Hot Paths

```cpp
template<typename T>
class Result {
public:
    // Unchecked accessor — skips all guards.
    // USE ONLY when correctness is proven (e.g., after IsOk() check).
    // Undefined behavior if called on an error result.
    // Hot-path trading code that has already verified IsOk() should use
    // this to avoid the atomic/assert overhead.
    T& ValueUnsafe() noexcept {
        return value_;
    }

    const T& ValueUnsafe() const noexcept {
        return value_;
    }
};
```

### Step 3 — Apply Same Pattern to `Result`

The existing `Error()` method already returns `code_` which is always valid (the union has `code_` initialized in the error constructor). No guards needed.

However, add a note:

```cpp
// Note: Error() is always safe to call — ErrorCode is valid in all states.
// On an ok result, Error() returns the last error code stored in the union,
// which may be ErrorCode::Success (0) or an arbitrary value — always check IsError() first.
ErrorCode Error() const noexcept {
    return code_;
}
```

### Step 4 — Add `static_assert` for non-throwing default construction

Since `Value()` is `noexcept`, the default construction must also be `noexcept`:

```cpp
template<typename T>
class Result {
    static_assert(std::is_nothrow_default_constructible_v<T>,
        "Result<T> requires T to be nothrow default constructible");
    // ... rest of class ...
};
```

If `T` is not nothrow default constructible, the user must use `ValueUnsafe()` instead — but since `Value()` only constructs `T` via the user's constructor (not default), this assert may be too restrictive. **Remove the static_assert** — it's not needed because `Value()` never default-constructs `T`. The original union-style Result stores `T` directly, and the guard path calls `std::terminate()` which doesn't construct anything.

---

## Acceptance Criteria

- [ ] Debug build: `Value()` on error result triggers assert + diagnostic message
- [ ] Release build: `Value()` on error result calls `std::terminate()` — no UB, no crash, clean death
- [ ] `ValueUnsafe()` skips all guards (for proven hot paths)
- [ ] No `static T default_value{}` — no DLL ODR violation
- [ ] Build succeeds Debug|x64 + Release|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/error/Result.hpp` | Add guarded `Value()`/`Error()` + `ValueUnsafe()` |