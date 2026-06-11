# AA-M09 — Result Monadic API (After-Audit Corrected)

> **Phase**: 5 — 🛡️ Safety & Ergonomics  
> **Effort**: 1-1.5 h  
> **Depends on**: Existing `error/Result.hpp` (already implemented)  
> **v1.x Reference**: TASK-M09-ResultMonadic.md  
> **Audit Issues**: M09-A (ResultVoid value_or), M09-B (and_then error type invariance), M09-C (naming consistency)  
> **⚠️ Validation Note**: AUDIT-M09 had fabricated issue (M09-C SEH catch) and drifted M09-B — this AA file is re-aligned with the holistic audit only.

---

## Problem

`Result<T>` exists at `error/Result.hpp` (82 lines) with `IsOk()`, `IsError()`, `Value()`, `Error()`. It lacks monadic operations (`value_or`, `and_then`, `map`) that enable composable error handling without manual if-checks.

---

## Corrected Implementation Plan

### Step 1 — Add `value_or` for `Result<T>`

```cpp
template<typename T>
class Result {
public:
    // Returns value on success, default_value on error.
    // default_value expression IS evaluated at the call site regardless of IsOk().
    // Only the move/copy of the result into T is deferred.
    // SFINAE-constrained: U must be convertible to T to prevent slicing.
    // ⚠️ If U→T conversion throws, noexcept is violated — caller must ensure
    //    nothrow-convertibility or avoid noexcept context.
    template<typename U, std::enable_if_t<std::is_convertible_v<U&&, T>, int> = 0>
    T ValueOr(U&& default_value) const& noexcept {
        return has_value_ ? value_ : static_cast<T>(std::forward<U>(default_value));
    }

    template<typename U, std::enable_if_t<std::is_convertible_v<U&&, T>, int> = 0>
    T ValueOr(U&& default_value) && noexcept {
        return has_value_ ? std::move(value_) : static_cast<T>(std::forward<U>(default_value));
    }
};
```

### Step 2 — Add `value_or` for `ResultVoid`

```cpp
class ResultVoid {
public:
    // Returns Success on ok, default_code on error.
    ErrorCode ValueOr(ErrorCode default_code) const noexcept {
        return IsOk() ? ErrorCode::Success : default_code;
    }
};

> **Design Note**: `ResultVoid::AndThen` is intentionally omitted. Void error handling remains imperative (`IsOk()` check). Adding `AndThen` to `ResultVoid` would require a lambda taking no arguments and returning another `Result` — functionally equivalent to a guarded call. If this pattern becomes frequent, add `ResultVoid::AndThen` in a future phase.
```

### Step 3 — Add `and_then` (flat-map)

```cpp
template<typename T>
class Result {
public:
    // Chains: Ok(x) → f(x) must return Result<U>
    // Error(e) → Error(e) propagated
    // f must return Result<U> — not wrapped in another Result.
    //
    // NOTE: No try-catch. Exceptions propagate naturally.
    // SEH (access violation, stack overflow) is NOT caught — they
    // terminate as they should on Windows.
    template<typename F>
    auto AndThen(F&& f) & -> decltype(f(value_)) {
        if (has_value_) {
            return f(value_);
        }
        return code_;
    }

    // Rvalue overload: moves value_ into f.
    template<typename F>
    auto AndThen(F&& f) && -> decltype(f(std::move(value_))) {
        if (has_value_) {
            return f(std::move(value_));
        }
        return code_;
    }

**Critical**: The return type is `decltype(f(value_))` — NOT `Result<decltype(f(value_))>`. `f` already returns `Result<U>`, so an extra `Result<>` wrapper would produce `Result<Result<U>>` which is unusable.

> **⚠️ Note**: If `f` returns a non-`Result` type, the compiler error is deep inside `decltype`. Implementers should add a `static_assert` or SFINAE guard requiring that `f(value_)` returns `Result<U>` for some `U`.

### Step 4 — Add `map` (functor map)

```cpp
template<typename T>
class Result {
public:
    // Transforms: Ok(x) → Ok(f(x)), Error(e) → Error(e)
    // f must return U (not Result<U>).
    template<typename F>
    auto Map(F&& f) & -> Result<decltype(f(value_))> {
        if (has_value_) {
            return Result<decltype(f(value_))>(f(value_));  // f returns U, wrap in Result
        }
        return code_;
    }
};
```

### Step 5 — Verify naming consistency

`and_then` vs `Bind`, `map` vs `fmap` — check existing codebase for precedence. If no existing pattern, use Rust convention (and_then / map) which is well-understood.

---

## Acceptance Criteria

- [ ] `Result<int>::ValueOr(42)` returns value on ok, 42 on error
- [ ] `ResultVoid::ValueOr(ErrorCode::Timeout)` returns Success on ok, Timeout on error
- [ ] `Result<int>::AndThen(f)` chains: Ok(x) → f(x) returns `Result<U>`, Error(e) → Error(e)
- [ ] No nested `Result<Result<U>>` from `AndThen`
- [ ] `Result<int>::Map(f)` transforms: Ok(x) → Ok(f(x)), Error(e) → Error(e)
- [ ] No `catch(...)` — exceptions propagate naturally (SEH-safe)
- [ ] Build succeeds Debug|x64

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/error/Result.hpp` | Add value_or, and_then, map methods |