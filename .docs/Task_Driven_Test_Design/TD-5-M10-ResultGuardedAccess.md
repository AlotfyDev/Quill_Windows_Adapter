# Test Design: Result Guarded Access

## Under Spec
- AA File: `AA-M10-ResultGuardedAccess.md`
- Phase: 5
- Key Requirements:
  - Debug build: `Value()` on error result triggers `assert()` with diagnostic message
  - Release build: `Value()` on error result calls `std::terminate()` — no UB, clean death
  - `ValueUnsafe()` skips all guards (bare `return value_`) — for proven hot paths
  - `Error()` is always safe to call (ErrorCode union member is always initialized)
  - No `static T default_value{}` — avoids DLL ODR violations
  - `Value()` is `noexcept` — no exceptions thrown on programming error

## Test Harness
- **Fixture**: Pure compile-time and runtime tests of `error/Result.hpp`. No Quill, no filesystem. Two build configurations required: Debug (assert active) and Release (`NDEBUG` defined, `std::terminate` active).
- **Mocked vs Real**: Real `Result<T>`. `std::terminate` handler should NOT be mocked — the test verifies that `std::terminate` IS called (death test).
- **Preconditions**: Separate test binaries or `#ifdef` guards for Debug vs Release assertion testing. For `std::terminate` testing, use a child-process death test pattern (`ASSERT_DEATH` equivalent) or verify via `std::set_terminate` hook.

## Scenarios

### Positive Cases
- `Result<int>(42).Value()` returns `int&` = 42 (ok path, no guard triggered)
- `Result<int>(42).Value()` returns `const int&` on const ref (ok path)
- `Result<std::string>("ok").Value()` returns `std::string&` = "ok"
- `ValueUnsafe()` on ok Result returns the correct value — no guards tripped
- `Error()` on ok Result returns Success (0) — always valid
- `Error()` on error `Result<int>(ErrorCode::ConnectorTimeout)` returns `ErrorCode::ConnectorTimeout`
- `ValueUnsafe()` on error Result still compiles (it's the caller's responsibility to check IsOk() first)
- Mutation via `T& Value()` reference: `auto& v = result.Value(); v = 99;` — changes propagate to the stored value
- Rvalue `Value() &&` overload: `Result<std::string>("temp").Value()` returns `std::string` by value, not dangling reference
- `__assume(false)` after `std::terminate()`: verify via code review or static analysis that compiler barrier is present in all Release guard paths

### Negative / Error Cases
- Debug build: `Result<int>(ErrorCode::Unknown).Value()` triggers `assert(has_value_)` — test via `_CrtSetReportHook` to detect the assert
- Release build: `Result<int>(ErrorCode::Unknown).Value()` calls `std::terminate()` — verify via death test (child process exits with abort signal)
- `ValueUnsafe()` on error Result returns uninitialized/corrupt data — by design UB, not testable deterministically, but documented
- Calling `Value().` on a Result whose T is non-trivial (e.g., `std::string`) and error state — must terminate, not attempt to use the union as string (no double-free)

### Production Realities
- Hot path with `IsOk()` + `ValueUnsafe()` in a tight loop (1M iterations) — measure that `ValueUnsafe()` has zero overhead vs raw union access
- `Value()` guarded path in a timing-sensitive context — verify assert/terminate check adds minimal (but non-zero) overhead via microbenchmark
- Result returned from a `noexcept` function and `Value()` called — must not throw; `std::terminate` is acceptable because this is a programming error, not runtime error handling
- DLL boundary crossing: verify no `static T default_value` exists by linker error (if one were added, two DLLs would have duplicate symbols)

### Thread Safety
- `Value()` has no synchronization — safe for concurrent reads of the same Result, unsafe if one thread writes and another reads
- `ValueUnsafe()` has no atomic fence — same constraints as `Value()`, but the caller is explicitly claiming safety
- The guard check in `Value()` is a plain `if (!has_value_)` — no atomic load. Correct by design: if another thread mutates the Result concurrently, UB exists regardless of guards
- `Error()` reads `code_` from the union — safe if the Result is not concurrently mutated

## Assertions
- Debug: `assert` fires exactly once when `Value()` is called on an error Result, with a message containing "Value() called on error"
- Release: `std::terminate()` is invoked when `Value()` is called on an error Result (death test: process exits with SIGABRT or equivalent terminate signal)
- `Value()` on ok Result returns a valid reference to the stored value (not a copy, not a default)
- `ValueUnsafe()` compiles in both Debug and Release and does not call assert/terminate
- `Error()` returns the correct ErrorCode in all states
- No linker errors for `static T default_value{}` (linker verifies no such symbol exists)
- `noexcept` is present on all guard functions — verified by `static_assert(noexcept(result.Value()))`
- Rvalue `Value() &&` returns `T` by value, verified by `decltype` on a temporary Result
- `__assume(false)` present after each `std::terminate()` in Release guard path — verified by code inspection or AST match

## Failure Mode
- A test failure where Debug assert doesn't fire: **silent corruption in dev** — developers shipping bugs because their unit tests didn't catch misuse
- A test failure where Release `std::terminate` is not called: **undefined behavior in production** — corrupted memory from reading the wrong union member, potentially exploitable
- A test failure where `Value()` on ok returns wrong value: **data corruption** in all builds
- A test failure where `ValueUnsafe()` has different behavior in Debug vs Release (e.g., someone adds assert to it): **performance regression** in hot paths — hot path code silently acquires guard overhead
- A test failure in ODR verification: **hard-to-diagnose production crashes** where two DLLs have different static default values, leading to Heisenbugs

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| MSVC `_DEBUG` / `NDEBUG` documentation for assert behavior in Debug vs Release | Step 1 — Choose Error-Handling Policy | GAP-5-M10-1 resolved; add MSVC-specific Debug vs Release test note |
| `__assume(false)` compiler barrier added after `std::terminate()` in Release guard path | Step 1 — Choose Error-Handling Policy | GAP-5-M10-2 resolved; compiler barrier prevents UB reordering |
| Rvalue-qualified `Value() &&` returning `T` by value to prevent dangling references | Step 1 — Add guarded `Value()` | GAP-5-M10-3 resolved; add scenario for rvalue Value() returning by value not by reference |
| `static_assert` for nothrow default constructible removed (confirmed unnecessary) | Step 4 — Remove static_assert | Documentation update — GAP-5-M10-2 context |

## Spec Gap Notes (SGN)

| Gap ID | Issue | Architectural Impact | Recommendation | Status |
|--------|-------|---------------------|----------------|--------|
| GAP-5-M10-1 | AA spec says "Debug builds: assert(has_value_)" but MSVC's assert is controlled by `_DEBUG`, not `!defined(NDEBUG)`. The spec uses `!defined(NDEBUG)` which matches gcc/clang behavior, but on MSVC, `assert()` is active only when `_DEBUG` is defined. If the project compiles Debug with `/MDd`, `_DEBUG` is set and `NDEBUG` is NOT defined — they work together. But if a user compiles with `/O2` but without `NDEBUG`, `assert` is active in what the spec calls "Release". | Incorrect debug/release detection on MSVC | Use `#ifdef _DEBUG` on MSVC or accept that `!defined(NDEBUG)` works only with the default MSVC config. Document the MSVC-specific behavior. | ✅ RESOLVED — AA spec now documents MSVC-specific `_DEBUG` vs `NDEBUG` behavior with adjustment guidance |
| GAP-5-M10-2 | AA spec Step 4 adds then removes a `static_assert` for nothrow default constructible. The spec says "Remove the static_assert — it's not needed." But `Value()` on the guard path calls `std::terminate()` BEFORE returning `value_` — it never constructs a default. However, the `return value_` statement still requires `T` to be valid (the union member must be the active one). If `has_value_` is false, `value_` is unininitialized memory — returning a reference to it is UB, even if the function then calls terminate. The compiler can reorder the return before the terminate. | Undefined behavior in the guard path — compiler UB optimization could skip the terminate check | Restructure: `if (!has_value_) { std::terminate(); /* unreachable */ } return value_;` — add `__assume(false)` or `std::unreachable()` after terminate so the compiler knows the return is only reached on the ok path | ✅ RESOLVED — `__assume(false)` added after `std::terminate()` in all Release guard paths; `static_assert` removed from Step 4 |
| GAP-5-M10-3 | The spec does not define behavior for `const T& Value() const` vs `T& Value()` on a temporary Result. Rvalue Result calling `Value()` returns a dangling reference. | Potential use-after-free if callers chain `f().Value()` | Add documentation or an rvalue-qualified `Value()` overload returning `T` by value | ✅ RESOLVED — `T Value() &&` rvalue overload added returning by value to prevent dangling references |
