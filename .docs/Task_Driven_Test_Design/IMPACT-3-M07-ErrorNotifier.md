# Impact Analysis: Error Notifier Callback

## Summary
Total GAPs: 4 | P0: 1 | P1: 1 | P2: 2 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-3-M07-1 | 🔴 P0 | AA wires callback via lambda capturing by reference — reference may dangle after `InitializeLogging()` returns. Quill copies the functor, captured references must outlive backend. | Use-after-free: if user captures local by reference, reference dangles → UB/crash when Quill backend fires callback later. | No | 🛠️ Fix now |
| GAP-3-M07-2 | 🟡 P2 | "No guaranteed delivery" is untestable — cannot distinguish "best-effort didn't fire" from "bug". | Low — documentation gap on reliability contract. | No | 📝 Document only |
| GAP-3-M07-3 | 🟡 P2 | Not all Quill error paths call `set_error_notifier` — coverage gap in Quill v10.0.1. | Some Quill errors may silently bypass user's notifier without operator awareness. | No | 📝 Document only |
| GAP-3-M07-4 | ⚠️ P1 | No spec on callback behavior during `Backend::stop()`. If backend flushes remaining errors during shutdown, callback may fire during destruction of captured state. | Potential callback invocation during static destruction — use-after-free crash during shutdown. | No | 🛠️ Fix now |

## Recommended AA Changes
- GAP-3-M07-1 🛠️ Fix now: Document that callback must be safe to call for lifetime of backend. Change lambda to capture by copy (`[cb]`). Add lifetime warning: "The std::function must remain valid until after Backend::stop() completes."
- GAP-3-M07-2 📝 Document only: Add note acknowledging the coverage gap. Reference Quill source audit for confirmed error paths.
- GAP-3-M07-3 📝 Document only: Add note: "Error notifier only fires for errors Quill's backend explicitly reports. Not all errors reported. Use OS-level monitoring (ETW, Event Log) as complement."
- GAP-3-M07-4 🛠️ Fix now: Add documentation that `error_notifier` must remain valid until after `Backend::stop()` completes. Recommend `shared_ptr` wrapping for captured state.
