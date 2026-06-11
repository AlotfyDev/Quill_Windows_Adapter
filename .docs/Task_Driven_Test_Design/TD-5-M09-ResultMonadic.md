# Test Design: Result Monadic API

## Under Spec
- AA File: `AA-M09-ResultMonadic.md`
- Phase: 5
- Key Requirements:
  - `Result<T>::ValueOr(U&&)` returns value on ok, fallback on error; lazy (doesn't construct default on ok)
  - `ResultVoid::ValueOr(ErrorCode)` returns Success on ok, default_code on error
  - `Result<T>::AndThen(F&& f)` chains: Ok(x) -> f(x) returns `Result<U>`, Error(e) -> Error(e) propagated; no nested `Result<Result<U>>`
  - `Result<T>::Map(F&& f)` transforms: Ok(x) -> Ok(f(x)), Error(e) -> Error(e); f returns bare U, wrapped in Result
  - No `catch(...)` — exceptions propagate naturally; SEH (AV, stack overflow) is NOT caught and terminates

## Test Harness
- **Fixture**: No fixture needed — pure unit tests of `error/Result.hpp`. No Quill lifecycle, no filesystem, no threads.
- **Mocked vs Real**: Real `Result<T>` and `ResultVoid`. No mocks.
- **Preconditions**: Include `<CppUnitTest.h>`, `error/Result.hpp`, `error/ErrorCode.hpp`. Test types should include: `int`, `std::string` (non-trivial), a move-only type, and a type whose default constructor throws (to verify the "no default construction on ok" guarantee).

## Scenarios

### Positive Cases
- `Result<int>(42).ValueOr(-1)` returns 42; `Result<int>(ErrorCode::Timeout).ValueOr(-1)` returns -1
- `ResultVoid().ValueOr(ErrorCode::Timeout)` returns `ErrorCode::Success`; `ResultVoid(ErrorCode::Timeout).ValueOr(ErrorCode::Unknown)` returns `ErrorCode::Unknown`
- `Result<int>(42).AndThen([](int x) -> Result<int> { return Result<int>(x + 1); })` returns `Result<int>(43)`
- `Result<std::string>("hello").Map([](std::string const& s) { return s + " world"; })` returns `Result<std::string>("hello world")`
- Chaining: `Result<int>(2).Map([](int x){ return x * 3; }).AndThen([](int x) -> Result<std::string>{ return Result<std::string>(std::to_string(x)); })` returns `Result<std::string>("6")`
- ValueOr with non-default-constructible type: move-only `Result<UniqueResource>` returns the real value, not a default
- Rvalue overloads: `std::move(result).ValueOr(42)` and `std::move(result).Map(...)` compile and work
- Rvalue `AndThen`: `std::move(result).AndThen([](int&& x) -> Result<int> { return Result<int>(x + 1); })` moves value into lambda, no dangling references
- SFINAE constraint: `Result<int>(42).ValueOr(std::string("oops"))` — compile-time rejection at `static_assert` or SFINAE level (non-convertible U to T)
- ValueOr call-site evaluation: `Result<int>(42).ValueOr(counter())` — side-effect of `counter()` IS observable even on ok path; only move/copy to T is deferred
- `AndThen` with a lambda returning `Result<void>` — flat-maps correctly without wrapping

### Negative / Error Cases
- `Result<int>(ErrorCode::ConnectorTimeout).ValueOr(42)` returns 42 (error path)
- `Result<int>(ErrorCode::ResourceExhausted).AndThen([](int) -> Result<int> { return Result<int>(99); })` returns error `ResourceExhausted` — function not invoked
- `Result<int>(ErrorCode::ConfigInvalid).Map([](int x) { return x + 1; })` returns `Result<int>(ErrorCode::ConfigInvalid)` — function not invoked, error propagated
- `ResultVoid(ErrorCode::FatalError).ValueOr(ErrorCode::Success)` returns `ErrorCode::Success` (the default_code passed in, not the stored code)
- `AndThen` return type deduction: lambda returning `Result<std::string>` should NOT produce `Result<Result<std::string>>` — verify via `decltype` or static_assert

### Production Realities
- Result used in a hot loop (1M iterations) with `ValueOr` — no measurable allocation/latency regression vs raw if-else
- Result returned from a `noexcept` function — verifies the class is `noexcept` compatible (no exceptions in copy/move)
- Result stored in a `std::vector<Result<int>>` — verifies copy semantics work (union with non-trivial T)
- Result with `std::string` value — verifies proper destruction of the union member to avoid leaks

### Thread Safety
- `Result<T>` is NOT thread-safe by design — concurrent read/write of the same Result from multiple threads is UB. No atomic guarantees on `has_value_`.
- Single-threaded only — this is correct by design. If thread-safe Result is needed, callers must use external synchronization.
- `ValueOr`, `AndThen`, `Map` are all read-only on the error path (read `has_value_` once, return/forward) and read-write on ok path (read then invoke f). All safe for a single-threaded model.

## Assertions
- `ValueOr` returns the correct value on ok/error for `int`, `std::string`, move-only types
- `AndThen` skips f on error, invokes f on ok, propagates error type
- `Map` wraps f's return in `Result<U>`, skips on error
- `ResultVoid::ValueOr` returns Success on ok, the passed-in code on error
- No nested `Result<Result<U>>` — verified at compile time via `decltype(AndThen(...))` being `Result<U>`, not `Result<Result<U>>`
- No `catch(...)` in any monadic method — verified by code review / static analysis
- `noexcept` on all monadic methods — verified by `static_assert(noexcept(...))`
- SFINAE on `ValueOr`: non-convertible `U` to `T` rejected at compile time (static_assert or enable_if failure)
- Rvalue `AndThen`: value is moved into lambda, verified by move-only type test

## Failure Mode
- A test failure in `ValueOr` returning wrong default: **silent data corruption** in production — calling code may use a garbage fallback value thinking it's real
- A test failure in `AndThen` producing `Result<Result<U>>`: **compilation error** in production callers — code that compiled against the old API breaks silently at runtime if the extra wrapper is unwrapped incorrectly
- A test failure in `Map` not wrapping return: **type error** in production — callers get `Result<U>` instead of `U`, leading to incorrect API chaining
- A test failure in noexcept guarantees: **program termination** if an exception tries to propagate through a noexcept boundary

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| SFINAE `is_convertible_v` constraint on `ValueOr` to prevent slicing/nothrow violations | Step 1 — Add `value_or` for `Result<T>` | GAP-5-M09-1 resolved; add compile-time rejection test for non-convertible types |
| `&&`-qualified `AndThen` overload for rvalue Result chaining | Step 3 — Add `and_then` (flat-map) | GAP-5-M09-3 resolved; rvalue `AndThen` scenario added |
| Clarified `ValueOr` default_value evaluation semantics (expression always evaluated at call site; only move/copy to T is deferred) | Step 1 — Add `value_or` for `Result<T>` | GAP-5-M09-5 resolved; add scenario verifying call-site evaluation |
| `AndThen` non-Result return type guard still a recommendation (static_assert note present but not enforced in code) | Step 3 — Add `and_then` (flat-map) | GAP-5-M09-2 remains open — 🛠️ Fix noted in AA |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-5-M09-1 | AA spec shows `ValueOr` accepting `U&&` but casts to `T` via `static_cast<T>(forward<U>(...))`. If `U` is a different type, this slice-copies. If the cast throws (e.g., `std::stoi` on a string), the noexcept contract is violated. | Silent data loss (slicing) or program termination (noexcept violation) | Change `ValueOr` to return `T` via `static_cast<T>` only if `U` is implicitly convertible to `T`; otherwise use `static_cast` only after `is_nothrow_constructible_v<T, U>` check, or SFINAE-constrain to `std::is_convertible_v<U, T>` | ✅ RESOLVED — SFINAE `std::is_convertible_v<U&&, T>` constraint added to `ValueOr` in Step 1 |
| GAP-5-M09-2 | AA spec shows `AndThen` returns `decltype(f(value_))` which does NOT SFINAE-guard against f returning a non-Result type. If a caller passes `[](int) -> int`, the compile error is deep inside decltype. | Poor developer experience; hard-to-read error messages | Add `static_assert` or SFINAE requiring `f(value_)` returns `Result<U>` for some `U`, matching Rust's `and_then` contract | ⏸️ DEFERRED — AA has note recommending static_assert but no code change; feature gap remains |
| GAP-5-M09-3 | AA spec shows `AndThen` accepting `&`-qualified `f` but no `const&` or `&&` overloads for rvalue Result. | Rvalue Result chaining works but may produce dangling references if `f` captures by reference | Add `&&`-qualified overload that moves `value_` into `f` when Result is an rvalue | ✅ RESOLVED — `&&`-qualified overload added to `AndThen` in Step 3 that moves `value_` into `f` |
| GAP-5-M09-4 | AA spec has no `ResultVoid::AndThen` equivalent. If a void operation needs to chain, callers must manually check `IsOk()`. | Missing composability; void error handling remains imperative | Add `AndThen` to `ResultVoid` that invokes f only on ok, returns f's Result; or document why it's intentionally missing | ⏸️ DEFERRED — AA design note explains intentional omission; may add in future phase |
| GAP-5-M09-5 | No test for `ValueOr` lazy evaluation guarantee. The spec says "default_value is NOT constructed if IsOk()", but nothing enforces this in the signature. | If `ValueOr(throw_runtime_error())` is called, the argument IS evaluated before the function body — the spec's "lazy" claim is misleading | Rename parameter or add note: "The default_value expression is evaluated at the call site regardless of IsOk(). Only move/copy of the result to T is lazy." | ✅ RESOLVED — AA spec now documents default_value expression is evaluated at call site; only move/copy to T is deferred |
