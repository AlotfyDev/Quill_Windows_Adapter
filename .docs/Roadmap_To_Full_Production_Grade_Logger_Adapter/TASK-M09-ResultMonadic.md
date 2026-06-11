# M09 — Result\<T\> Monadic API

- **Priority**: 🟡 Medium
- **Est. Effort**: 1 hour
- **Depends on**: None

---

## Problem

The current `Result<T>` class provides only:
```cpp
result.IsOk() / IsError() / Error() / Value() / operator bool() / operator*()
```

No monadic operations:
- `value_or(default)` — return value or fallback
- `and_then(fn)` — chain: if Ok, apply fn; if Error, propagate error
- `or_else(fn)` — if Error, apply fn to error code
- `map(fn)` — transform Ok value

This forces verbose error checking patterns:

```cpp
auto result = parseOrder(data);
if (!result) return result.Error();
processOrder(result.Value());
```

Instead of:

```cpp
parseOrder(data)
    .and_then(processOrder)
    .or_else([](ErrorCode e) { LOG_ERR(logger, "failed: {}", ErrorCodeToString(e)); });
```

---

## Implementation

**File**: `Logger_Adapter/error/Result.hpp`

Add to `Result<T>`:

```cpp
template <typename U>
T value_or(U&& default_value) const {
    return has_value_ ? value_ : static_cast<T>(std::forward<U>(default_value));
}

template <typename Fn>
auto and_then(Fn&& fn) const -> std::invoke_result_t<Fn, T> {
    using ResultType = std::invoke_result_t<Fn, T>;
    static_assert(std::is_same_v<typename ResultType::error_type, ErrorCode>,
                  "and_then must return a Result type with ErrorCode");
    if (!has_value_) return ResultType(code_);
    return std::forward<Fn>(fn)(value_);
}

template <typename Fn>
Result or_else(Fn&& fn) const {
    if (!has_value_) { std::forward<Fn>(fn)(code_); }
    return *this;
}

template <typename Fn>
auto map(Fn&& fn) const -> Result<std::invoke_result_t<Fn, T>> {
    using U = std::invoke_result_t<Fn, T>;
    if (!has_value_) return Result<U>(code_);
    return Result<U>(std::forward<Fn>(fn)(value_));
}
```

Also add `using error_type = ErrorCode` to both `Result<T>` and `ResultVoid` for `and_then` SFINAE.

---

## Acceptance Criteria

- [ ] `result.value_or(42)` returns the value if Ok, 42 if Error
- [ ] `result.and_then(fn)` chains operations and short-circuits on error
- [ ] `result.map(fn)` transforms Ok value without nesting Result
- [ ] `result.or_else(callback)` invokes callback with ErrorCode on failure
- [ ] Build succeeds Debug|x64
