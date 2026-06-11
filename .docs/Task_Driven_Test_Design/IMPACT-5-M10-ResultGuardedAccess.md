# Impact Analysis: Result Guarded Access

## Summary
Total GAPs: 3 | P0: 1 | P1: 1 | P2: 1 | API Changes: 1

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-5-M10-1 | 🟡 P2 | AA spec uses `!defined(NDEBUG)` but MSVC's `assert()` is controlled by `_DEBUG`. Works with default MSVC config but non-standard builds may behave incorrectly. | No impact with standard MSVC project settings. Non-standard configs (e.g., `/O2` without `NDEBUG`) may have assert active in release. | No | 📝 Document only |
| GAP-5-M10-2 | 🔴 P0 | Guard path has UB: compiler can reorder `return value_` before `std::terminate()` because `value_` (uninitialized on error path) is loaded speculatively. | **Data corruption or silent UB in production.** The entire guard is defeated — `Value()` on error Result returns garbage instead of terminating. Defeats the purpose of guarded access. | No | 🛠️ Fix now |
| GAP-5-M10-3 | ⚠️ P1 | No rvalue-qualified `Value()` overload. Calling `Value()` on a temporary Result returns a dangling reference. | Use-after-free if callers chain `f().Value()`. Data corruption in production, potentially exploitable. | Yes | 🛠️ Fix now |

## Recommended AA Changes
- **GAP-5-M10-1**: Add MSVC-specific note about `_DEBUG` vs `NDEBUG` in the policy comment block
- **GAP-5-M10-2**: Add `__assume(false)` or `std::unreachable()` after `std::terminate()` in both `Value()` overloads as compiler barrier; restructure guard to prevent speculative load of `value_`
- **GAP-5-M10-3**: Add `T Value() && noexcept` overload returning `std::move(value_)` for both const and non-const rvalue Result
