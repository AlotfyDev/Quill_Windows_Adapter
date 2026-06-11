# M10 — Result\<T\> Guarded Access

- **Priority**: 🟡 Medium
- **Est. Effort**: 30 minutes
- **Depends on**: None

---

## Problem

Current `Result<T>` has undefined behavior when:
- `Value()` is called on an Error-holding `Result` — returns uninitialized garbage
- `Error()` is called on a Value-holding `Result` — returns uninitialized garbage

In a trading system, this is a crash waiting to happen. A corrupted order message could execute with garbage values.

---

## Implementation

**File**: `Logger_Adapter/error/Result.hpp`

Add debug-mode assertions:

```cpp
#ifdef _DEBUG
#define RESULT_CHECK_HAS_VALUE(has) \
    assert(has && "Result: accessed Value() on an Error result"); \
    if (!has) std::abort();  // fails fast even without debugger
#else
#define RESULT_CHECK_HAS_VALUE(has) \
    if (!has) std::unreachable();  // UB but at least hint to optimizer
#endif
```

Apply to:

```cpp
T& Value() {
    RESULT_CHECK_HAS_VALUE(has_value_);
    return value_;
}

ErrorCode Error() const {
    assert(!has_value_ && "Result: accessed Error() on a Value result");
    return code_;
}
```

---

## Acceptance Criteria

- [ ] Debug build: accessing `Value()` on an Error result triggers `assert` + `abort`
- [ ] Release build: no overhead (compiler sees `std::unreachable()`)
- [ ] Build succeeds Debug|x64 and Release|x64
