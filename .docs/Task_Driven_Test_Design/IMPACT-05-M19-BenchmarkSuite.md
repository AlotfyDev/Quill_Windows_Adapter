# Impact Analysis: Performance Benchmark Suite

## Summary
Total GAPs: 13 | P0: 0 | P1: 6 | P2: 7 | API Changes: 0

## GAP Details
| Gap ID | Severity | Issue | Production Impact | API Change? | Decision |
|--------|----------|-------|-------------------|-------------|----------|
| GAP-05-M19-1 | ⚠️ P1 | Sink type not specified in benchmark scenarios | Results not comparable across environments; regression detection invalid if sink type differs | No | 🛠️ Fix now |
| GAP-05-M19-2 | ⚠️ P1 | No thread affinity or priority specification | Measurements dominated by scheduler noise (10-100µs), making ±5% determinism impossible | No | 🛠️ Fix now |
| GAP-05-M19-3 | 🟡 P2 | Windows timer resolution not addressed | 15ms default tick inflates tail latencies in drain-based measurements | No | 🛠️ Fix now |
| GAP-05-M19-4 | 🟡 P2 | Single fixed 10% regression threshold | Blanket threshold misses real regressions or creates false positives across metrics with different noise profiles | No | 🛠️ Fix now |
| GAP-05-M19-5 | 🟡 P2 | Binary size measurement has no mechanism in C++ code | Cannot be measured by benchmark itself; must be in CI script | No | 🛠️ Fix now |
| GAP-05-M19-6 | ⚠️ P1 | No multi-threaded contention scenario | Change introducing producer-producer contention or backend saturation not caught until production | No | 🛠️ Fix now |
| GAP-05-M19-7 | ⚠️ P1 | No backpressure or slow-consumer scenarios | Queue config change (M08) that makes queue unbounded or changes capacity silently missed | No | 🛠️ Fix now |
| GAP-05-M19-8 | 🟡 P2 | No startup/shutdown timing | Change making StopLogging() block indefinitely not caught | No | 🛠️ Fix now |
| GAP-05-M19-9 | 🟡 P2 | ±5% determinism claim lacks statistical backing | False-negative rate for regression detection is unknown | No | 🛠️ Fix now |
| GAP-05-M19-10 | ⚠️ P1 | Plaintext output with regex parsing is fragile | Formatting change silently breaks CI regex, producing false PASS | No | 🛠️ Fix now |
| GAP-05-M19-11 | 🟡 P2 | CI failure behavior undefined | Flaky benchmark blocks development velocity; developers learn to ignore failures | No | 🛠️ Fix now |
| GAP-05-M19-12 | 🟡 P2 | No LOGJ_INFO baseline | JSON logging path regression (200ns+ per message) not caught | No | 🛠️ Fix now |
| GAP-05-M19-13 | ⚠️ P1 | No compile-time level stripping measurement | C02 correctness not verifiable — stripped vs unstripped latency indistinguishable | No | 🛠️ Fix now |

## Recommended AA Changes
1. [🛠️ GAP-1] Add sink_type parameter to benchmark functions; run all 3 sink types
2. [🛠️ GAP-2] Add thread pinning and priority specification
3. [🛠️ GAP-3] Add timeBeginPeriod(1) requirement and QPC frequency check
4. [🛠️ GAP-4] Add tiered regression thresholds (P50>15% soft, P99>10% hard)
5. [🛠️ GAP-5] Move binary size measurement to CI script; remove from C++ benchmark
6. [🛠️ GAP-6] Add 4-thread contention scenario
7. [🛠️ GAP-7] Add backpressure characterization tests
8. [🛠️ GAP-8] Add startup/shutdown timing measurements
9. [🛠️ GAP-9] Increase to 10 runs for baseline; document statistical method
10. [🛠️ GAP-10] Emit structured JSON alongside human-readable table
11. [🛠️ GAP-11] Define tiered CI failure behavior (FAIL/WARN/UNSTABLE)
12. [🛠️ GAP-12] Add LOGJ_INFO baseline scenario
13. [🛠️ GAP-13] Add compile-time level stripping scenario
