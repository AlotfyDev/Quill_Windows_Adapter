# Impact Analysis: Result Monadic API

## Summary
Total GAPs: 5 | P0: 0 | P1: 1 | P2: 4 | API Changes: 2

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-5-M09-1 | ⚠️ P1 | `ValueOr` casts via `static_cast<T>` without SFINAE guard. If `U` is a different type, slicing or noexcept violation occurs. | Silent data loss (slicing) or program termination (noexcept violation) when `ValueOr` is called with a mismatched type in a noexcept context. | No | 🛠️ Fix now |
| GAP-5-M09-2 | 🟡 P2 | `AndThen` has no SFINAE/static_assert for non-Result return type — poor compiler errors. | None at runtime; poor developer experience (deep template errors). | No | 📝 Document only |
| GAP-5-M09-3 | 🟡 P2 | No `&&`-qualified `AndThen` overload for rvalue Result. | Dangling reference risk if `f` captures by reference when chaining on a temporary Result. | Yes | 🛠️ Fix now |
| GAP-5-M09-4 | 🟡 P2 | No `ResultVoid::AndThen` — void error handling remains imperative. | Missing composability for void-returning chains; callers must manually check `IsOk()`. | Yes | 📝 Document only |
| GAP-5-M09-5 | 🟡 P2 | "Lazy" evaluation claim for `ValueOr` is misleading — argument IS evaluated at call site. | None; C++ standard behavior, not a bug. | No | 📝 Document only |

## Recommended AA Changes
- **GAP-5-M09-1**: Add `std::enable_if_t<std::is_convertible_v<U&&, T>>` SFINAE constraint to both `ValueOr` overloads
- **GAP-5-M09-2**: Add `static_assert` comment/warning near `AndThen` for non-Result return types
- **GAP-5-M09-3**: Add `&&`-qualified `AndThen` overload that moves `value_` into `f`
- **GAP-5-M09-4**: Add "Design Note: ResultVoid::AndThen intentionally omitted" after `ResultVoid::ValueOr` section
- **GAP-5-M09-5**: Update comment on `ValueOr` to clarify argument is evaluated at call site
