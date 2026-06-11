# Test Design: M19 Benchmark Methodology

## Under Spec
- AA File: `AA-M19-BenchmarkSuite.md`
- Phase: 0.5
- Key Requirements:
  - Benchmark compiles and runs with Debug|x64
  - Baseline report saved to `.docs/benchmarks/baseline_phase1.txt`
  - CI script can compare current vs baseline for P99 regression
  - Binary size measurement included in baseline
  - Benchmark takes <30 seconds to run (100k iterations)
  - Results deterministic within ±5% across runs (same machine, same load)
  - Metrics measured: LOG_INFO latency p50/p99/p999, LOGJ_INFO latency, compile-time stripped latency, throughput (msg/sec), binary size, queue drain time, multi-threaded contention (4 thr), backpressure characterization, startup/shutdown timing
  - Thread pinning: SetThreadAffinityMask to non-zero core, THREAD_PRIORITY_HIGHEST, HIGH_PRIORITY_CLASS
  - Timer resolution: timeBeginPeriod(1), QPC frequency verification
  - Three sink modes: null, file, in-memory — results tagged with sink_type
  - CI regression thresholds: P99 >10% increase (FAIL), P50 >15% (WARN), throughput >15% decrease (FAIL), binary size >5% (WARN)
  - CI failure tiers: FAIL (blocks PR), WARN (requires review), UNSTABLE (auto-retry, quarantine node)
  - Structured JSON output format for CI parsing (no regex)

## Measurement Protocol

### Warmup
- **Iterations**: 10,000 messages emitted before any measurement begins
- **What is discarded**: All warmup iterations — no timestamps recorded
- **What it stabilizes**:
  - L1/L2 data caches (the LOG_INFO hot path touches format string, logger state)
  - Branch predictor (format string parsing, level-check branch)
  - Quill backend thread scheduling (OS thread wake-up, SPSC queue lazy initialization)
  - Memory allocator (any initial heap allocations for arena blocks)
- **Verification**: After warmup, the first 100 measured iterations should not show a downward trend. If they do, increase warmup to 50,000.
- **Between runs**: A 100ms `Sleep()` between benchmark runs allows the OS to settle (thermal throttling, power state).

### Latency Measurement
- **Instrument**: `std::chrono::high_resolution_clock::now()` before and after `LOG_INFO`. On Windows this maps to `QueryPerformanceCounter` (QPC) which is HPET-based (~10ns resolution on modern hardware).
- **The queue drain problem**:
  - Quill's `BoundedBlockingQueue` has a default capacity of 8,192 (configurable per M08).
  - Without draining, the producer blocks at iteration 8,193, and every subsequent iteration measures **queue-blocking time**, not enqueue latency.
  - **Solution**: Call `backend->drain()` every 1,000 iterations. This empties the queue before it fills, ensuring the producer never blocks on a full queue.
  - Each `drain()` itself takes ~5-50µs. This is NOT subtracted from the iteration time — the drain is part of steady-state operation. A real workload also drains periodically.
  - Trade-off: More frequent draining = less queue buildup = more realistic latency, but adds overhead (~0.5-5% total). The 1,000-iteration interval balances this.
- **Valid sample vs outlier**:
  - Any sample > 10x the median of a preliminary 1,000-iteration pilot run is flagged as an outlier and logged separately.
  - Outliers are included in percentile calculations (they represent real events: context switches, page faults, DPCs).
  - A run with >1% outliers (1,000+ out of 100k) is discarded entirely and re-run — indicates system interference (anti-virus, Windows Update, etc.).
- **Timer resolution**: Call `timeBeginPeriod(1)` at benchmark start to set Windows timer to 1ms resolution. This affects `Sleep()`, `WaitForSingleObject`, and `std::this_thread::sleep_for`. QPC is unaffected but the 1ms period prevents coarser timer interactions.

### Throughput Measurement
- **Method**: Direct wall-clock measurement of emitting N messages then calling `backend->drain()`. Formula: `N / total_seconds`.
- **Why not derived**: `1e9 / avg_ns` overestimates throughput because it ignores drain latency and queue contention. The queue drains in batches (typically 512-4098 messages per batch), so per-message cost in the backend is not uniform.
- **Batch sizes**:

| Batch Size | Purpose | Expected Behavior |
|------------|---------|-------------------|
| 1 | Per-message overhead with full queue round-trip | Highest per-message cost |
| 10 | Small burst, backend stays idle between bursts | Still high per-message cost |
| 100 | Medium burst, backend amortizes batch overhead | Transition region |
| 1,000 | Large burst, backend reaches steady-state | Near-optimal throughput |
| 10,000 | Saturation, queue fills up | Tests backpressure with default capacity |

- Each batch size runs 3 times; report the median.

### Report Format

JSON output written to `bench_results.json` for structured CI parsing:

```json
{
  "phase": "phase1",
  "timestamp": "Jun 11 2026 14:30:00",
  "machine": "ci-runner-42",
  "build_config": "Release|x64",
  "latency_ns": {
    "p50": 85.0,
    "p99": 320.0,
    "p999": 1250.0,
    "max": 5800.0,
    "mean": 112.0,
    "stddev": 45.0,
    "outlier_count": 12,
    "total_samples": 100000
  },
  "throughput_msgs_per_sec": {
    "batch_1": 1250000,
    "batch_10": 3400000,
    "batch_100": 5800000,
    "batch_1000": 7200000,
    "batch_10000": 7500000
  },
  "binary_size_bytes": {
    "debug": 2457600,
    "release": 1310720
  },
  "ci_metadata": {
    "median_of_runs": 5,
    "run_times_ms": [4210, 3890, 4120, 3950, 4050],
    "p99_values_raw": [315.0, 325.0, 310.0, 330.0, 320.0],
    "p99_median": 320.0
  },
  "sink_type": "file"
}
```

Console output (human-readable) mirrors the AA spec's table format but adds the JSON path line:
```
Results written to: .docs/benchmarks/bench_results.json
```

## CI Regression Gate Design

### Thresholds
- **P99**: >10% increase from baseline → **FAIL** (hard gate)
- **P50**: >15% increase from baseline → **WARN** (soft gate, requires manual review)
- **Throughput (batch_1000)**: >15% decrease → **FAIL**
- **Binary size (Release)**: >5% increase → **WARN** (could be legitimate new functionality)

### Handling CI Noise
- **Median of 5 runs**: The benchmark runs 5 complete times. The median P99 (sorted, index 2) is the reported value. This eliminates the single-run noise from background OS activity.
- **Absolute thresholds are dangerous** across different CI hardware. Only relative comparison (same binary on same machine vs baseline from same machine) is valid.
- **Baseline per machine**: Store baseline in `.docs/benchmarks/baseline_{MACHINE_NAME}_{BUILD_CONFIG}.json`. On first run, always save baseline. On subsequent runs, compare against machine-specific baseline.
- **Environmental guard**: Before each run, query `Get-CimInstance Win32_Processor | Select-Object LoadPercentage`. If CPU load > 20% at start, skip and retry after 10s (up to 3 retries). If still busy, mark run as `untrusted` in output.
- **Power plan**: CI pipeline should set Windows power plan to `High Performance` before running benchmarks:
  ```powershell
  powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
  ```

### Failure Behavior
- **FAIL (P99 regression)**: Pipeline fails. Block PR merge. Developer must run benchmark locally to confirm, then either fix regression or update baseline (if the change intentionally affects performance and is justified).
- **WARN (P50 regression)**: Pipeline passes but annotates the PR with a warning comment. Requires human review before merge. The reviewer checks if the change is expected to add frontend cost (e.g., new formatting feature, additional level check).
- **Unstable CI node (run discarded)**: Pipeline reports `UNSTABLE` — the build is not failed but the benchmark results are flagged as untrusted. A second CI run is triggered automatically. If 2 consecutive runs are untrusted, the CI node is quarantined.
- **Baseline does not exist yet**: Pipeline runs benchmark, saves baseline, passes unconditionally. The commit message for the baseline file notes "initial baseline".
- **Binary size WARN**: Does not block CI. Logged in build artifacts. Reviewed weekly.

### Distinguishing Regression from Noise
| Signal | Likely Regression | Likely Noise |
|--------|-------------------|--------------|
| P50 up 5%, P99 flat | Maybe | Probably noise (frontend cost shouldn't affect tail) |
| P50 flat, P99 up 30% | Regression | Unlikely — tail growth indicates queue or backend issue |
| All percentiles +12%, throughput -12% | Regression | Consistent shift across metrics = real |
| One batch size drops, others flat | Might be noise | Retry; if reproducible, batch-specific regression |
| Single run spike, median unchanged | Noise | Median filter eliminates it |
| Monotonic increase across 3+ CI runs | Regression | Cross-run trend beats single-run noise |

## Test Harness

### Thread Pinning
- **Frontend (measurement) thread**: Pin to a single physical core (`SetThreadAffinityMask` or `SetThreadIdealProcessor`). Do NOT use core 0 (OS scheduler affinity, DPCs, interrupts) — use core 1 or the last physical core.
- **Backend (Quill) thread**: Leave unpinned. The backend thread is I/O-bound (file writes) and benefits from free scheduling. Pinning it to the same core as the frontend would cause contention.
- **Measurement thread priority**: `THREAD_PRIORITY_HIGHEST` for the duration of the benchmark. Restore to normal after. This reduces involuntary context switches.
- **Process priority class**: `HIGH_PRIORITY_CLASS` via `SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)`.

### Timer Resolution
- Call `timeBeginPeriod(1)` once at startup; `timeEndPeriod(1)` at shutdown.
- Verify QPC frequency: `QueryPerformanceFrequency(&freq)`. Should be >1 MHz on modern hardware (typically ~10 MHz = 100ns tick). If <500 kHz, emit a warning.
- **Measurement overhead correction**: The timer calls themselves take ~20-50ns. For sub-microsecond measurements, this is significant. Measure the timer call overhead (empty loop, just start/stop) and subtract from each sample. Store as `timer_overhead_ns` in report metadata.

### Filesystem
- **Three sink modes**, selected via compile-time or runtime flag:

| Mode | Implementation | Use Case |
|------|----------------|----------|
| Null sink | `quill::Sink` subclass that discards all output | Measure pure frontend overhead, no I/O |
| File sink | `quill::FileSink` writing to `bench_output.log` (deleted between runs) | Realistic full-stack latency |
| In-memory sink | Custom `Sink` collecting formatted messages in a `std::vector<std::string>` | Measure formatting cost without filesystem jitter |

- **Why three modes**: If a regression appears only in file-sink mode but not null-sink mode, the regression is in the backend I/O path, not the frontend. This isolates the root cause.
- **File sink cleanup**: Delete `bench_output.log` before each run. Use `CREATE_ALWAYS` or remove after each run. Old data skews filesystem cache behavior.
- **Temp directory**: Write to a RAM-backed volume if available (e.g., `%TMP%` on Azure DevOps has SSD-backed temp storage). If on HDD, emit a warning — filesystem latency variance is 100-1000x higher on HDD.

## Scenarios

### Baseline

| Sub-scenario | What It Measures | Expected Range (Null sink, Release) |
|--------------|------------------|-------------------------------------|
| No logging active | `if (level >= LevelFilter)` check cost without body | ~5-15ns |
| `LOG_INFO` (level enabled) | Full frontend: level check + format string parse + args + spsc enqueue | ~40-100ns |
| `LOGJ_INFO` (level enabled) | JSON frontend: same as LOG but with JSON field construction | ~80-200ns |
| Compile-time stripped | `LOG_INFO` when `QUILL_LOG_LEVEL` > LogLevel::Info — compiler removes entire body | Should be ~0ns or one branch instruction |
| `LOG_INFO` file sink | Full stack frontend + backend formatting + file write | ~200-1000ns |

- **Verification**: Compile-time stripped should be indistinguishable from a no-op. If it measures >10ns, the optimizer is not removing the log body — investigate.

### Single-Threaded Throughput
- **Setup**: 1 thread, file sink, `LOG_INFO` with a simple format string and 2 int args.
- **Procedure**: Run `MeasureThroughput` at each burst size (1, 10, 100, 1000, 10000) × 3 runs.
- **What to watch**: The relationship between burst size and throughput should be sub-linear (bigger bursts → higher throughput due to batch amortization). If it's linear or worse, the backend thread is CPU-bound.
- **Expected latency** (file sink, Release, 100k iterations):
  - p50: 200-600ns
  - p99: 800-3000ns
  - p999: 2000-10000ns (file I/O creates tail variance)
- **Acceptance**: ±5% across 5 runs on same hardware.

### Multi-Threaded Contention
- **Setup**: 4 threads, each calling `LOG_INFO` 100k times, single shared `quill::Logger*`.
- **Key metric**: p99 compared to single-threaded. SPSC (single-producer single-consumer) queue per thread means no producer-producer contention at the queue level. However, Quill uses thread-local SPSC queues, so each thread has its own queue. Contention is at the backend (one consumer thread draining all queues).
- **What to watch for**:
  - SPSC head-of-line blocking: If one thread fills its queue and blocks while others are idle, throughput drops. The periodic drain in the measurement loop mitigates this during latency tests.
  - Backend saturation: 4 producers × 100k messages = 400k messages drained by 1 backend thread. If backend falls behind, memory grows. Monitor `quill::backend_tp::instance()->queue_size()`.
- **Expected**: Multi-threaded throughput should be 2-3x single-threaded (not 4x, because backend is a bottleneck).
- **Failure condition**: If multi-threaded p99 > 10x single-threaded p99, there is queue or backend contention that needs investigation.

### Backpressure Scenarios

| Scenario | How | Expected Behavior |
|----------|-----|-------------------|
| Slow consumer | Artificially delay the backend thread (`std::this_thread::sleep_for(1ms)` per batch) | Producer blocks when queue is full. Measure blocking time. Bounded queue should keep memory bounded. |
| Blocking queue full | Emit burst of 100k with no drain, default BoundedBlockingQueue (8192 cap) | After ~8k iterations, producer blocks. Latency spikes from ~100ns to ~5000ns. This is expected — the block is the backpressure mechanism. |
| Unbounded queue | Switch to `quill::Queue::Unbounded` (if supported), repeat slow consumer | Memory grows unboundedly. Measure growth rate. This tests the unbounded mode is correct but highlights the danger. |
| Rate-limited backend | Set rate limiter to 1000 msg/sec via M05, emit 100k burst | Actual throughput should be capped near 1000 msg/sec. Measure drain time: should be ~100 seconds. |

- **Interpretation**: Backpressure scenarios are NOT regression tests. They are characterization tests to document behavior. They should not block CI. Results are logged but not compared against thresholds. Run them weekly, not on every commit.

### Startup/Shutdown
- **InitializeLogging() cold start**: Measure from call to return on first invocation. Includes: thread creation, sink setup, queue allocation, file open. Expect ~1-50ms.
- **InitializeLogging() warm start**: Second call (if supported re-initialization). Should be faster (~100µs-1ms).
- **StopLogging() flush time with empty queue**: Should be near-zero (<1ms).
- **StopLogging() flush time with backlog**: Emit 10k messages, call StopLogging without prior drain. Measure time for flush to complete. Expect <100ms.
- **Shutdown with slow consumer**: Emit 10k messages, introduce 1ms backend delay, then StopLogging. Flush could take seconds. This is a safety test — does StopLogging have a timeout or does it block indefinitely?

## Failure Mode

### What a Benchmark Regression Indicates

| Regression Pattern | Likely Cause | Investigation Path |
|-------------------|-------------|-------------------|
| P50 up, P99 flat | Frontend hot-path change (extra branch, heavier format parse, more args) | Diff the LOG_INFO call site. Check for new branches before the spsc enqueue. |
| P50 flat, P99 up | Queue contention, backend slowdown, or infrequent OS interference | Check queue config (M08). Check backend thread logic. Look for new I/O calls on the backend. |
| All latencies +throughput | Everything slower — could be compiler flags, optimization change, or build config shift | Check Release build flags (`/O2`, whole program opt). Check if `/Ob2` or LTCG was disabled. |
| Throughput down, latencies flat | Backend bottleneck (slower formatting, I/O, or drain) | Profile the backend thread. Check formatting functions. Check file sink write calls. |
| Only batch_1000 drops | Queue batch-size-specific regression | Check quill batch drain logic. Check if batch_size or queue_capacity changed. |
| Binary size up | New code, templates instantiated, RTTI enabled | Run `dumpbin /symbols` or `link /MAP`. Check for unexpected `inline` expansion. |

### Real Regression vs CI Noise — Decision Tree

```
           ┌─────────────────────────────┐
           │ P99 > baseline * 1.10 ?     │
           └──────────┬──────────────────┘
                      │
               ┌──────┴──────┐
               │ YES         │ NO
               │             │
        ┌──────┴──────┐      │ PASS
        │ Median of 5 │      │
        │ still >10%? │      │
        └──────┬──────┘      │
               │             │
         ┌─────┴─────┐       │
         │ YES       │ NO    │
         │           │       │
   ┌─────┴─────┐     │ WARN  │
   │ Isolated  │     │ (pass │
   │ or trend? │     │ but   │
   └─────┬─────┘     │ flag) │
         │           │       │
    ┌────┴────┐      │       │
    │Trend    │Isolated      │
    │(3+ runs)│(1 run)│      │
    │         │      │       │
    │ FAIL    │ WARN │       │
    │ Block   │ Require      │
    │ PR      │ review│      │
    └─────────┴───────┘      │
                             │
```

### Recovery Actions
- **False positive (real CI noise)**: The developer re-runs the CI pipeline. If the 2nd run passes, the first is logged as a flaky run and the PR is unblocked.
- **True regression**: The developer must either:
  1. Fix the regression and push a new commit (CI re-runs automatically).
  2. Update the baseline if the regression is an acceptable trade-off (e.g., adding a new feature intentionally adds 2% frontend latency). This requires a comment explaining the trade-off.
  3. Split the change: land the refactor that caused the regression separately from the feature, so the regression can be investigated independently.
- **Baseline drift**: Over time, baseline values shift due to library updates, compiler updates, or hardware changes. Re-baseline after:
  - Compiler toolchain update (MSVC version change)
  - Quill library version update
  - CI runner hardware change
  - Windows feature update (affects timer resolution, scheduler)

## Impact Sync

This test design was updated to reflect Impact Analysis applied to the AA spec on 2026-06-11.

| Change | AA Spec Section | TD Update |
|--------|----------------|-----------|
| Three sink modes with `sink_type` result tagging | Step 2 (BenchmarkResult struct), Step 4 (main) | Resolved GAP-05-M19-1 |
| Thread pinning and priority settings | Step 4 main() | Resolved GAP-05-M19-2; updated Key Requirements |
| `timeBeginPeriod(1)` timer resolution | Step 4 main() | Resolved GAP-05-M19-3; updated Key Requirements |
| Tiered regression thresholds (P50 soft, P99 hard) | § Regression Thresholds | Resolved GAP-05-M19-4; updated Key Requirements |
| Binary size measured by CI script, not C++ | § CI Script (binary size measurement) | Resolved GAP-05-M19-5 |
| Multi-threaded contention (4 thr) | Step 2 table, Step 4 main() | Resolved GAP-05-M19-6; updated Key Requirements |
| Backpressure characterization | Step 2 table | Resolved GAP-05-M19-7; updated Key Requirements |
| Startup/shutdown timing | Step 2 table | Resolved GAP-05-M19-8; updated Key Requirements |
| Median-of-10 confirmation for regression | § CI Regression Gate Flow | Resolved GAP-05-M19-9 (partially) |
| Structured JSON output format | § Output Format | Resolved GAP-05-M19-10; updated Key Requirements |
| FAIL / WARN / UNSTABLE CI tiers | § CI Integration | Resolved GAP-05-M19-11; updated Key Requirements |
| LOGJ_INFO latency baseline | Step 2 table | Resolved GAP-05-M19-12 |
| Compile-time stripped latency measurement | Step 2 table | Resolved GAP-05-M19-13; updated Key Requirements |

## Spec Gap Notes (SGN)

### Resolved Gaps

These GAPs were raised during test design and subsequently resolved by Impact Analysis fixes applied to the AA spec.

| Gap ID | Issue | Resolution | AA Spec Section |
|--------|-------|------------|-----------------|
| GAP-05-M19-1 | Sink type not specified in scenarios | ✅ RESOLVED — `sink_type` field in BenchmarkResult; main() loops over null/file/memory sink modes. | Step 2 (BenchmarkResult), Step 4 (main) |
| GAP-05-M19-2 | No thread affinity or priority | ✅ RESOLVED — `SetThreadAffinityMask`, `SetThreadPriority`, `SetPriorityClass` added to main(). | Step 4 (main) |
| GAP-05-M19-3 | Timer resolution not addressed | ✅ RESOLVED — `timeBeginPeriod(1)` at startup, `QueryPerformanceFrequency` verification. | Step 4 (main) |
| GAP-05-M19-4 | Single fixed regression threshold | ✅ RESOLVED — Tiered thresholds: P50 >15% soft, P99 >10% hard, throughput >15% hard. | § Regression Thresholds |
| GAP-05-M19-5 | Binary size mechanism undefined | ✅ RESOLVED — Binary size measured in CI script via `Get-Item`, not in C++ benchmark. | § CI Script |
| GAP-05-M19-6 | No multi-threaded contention scenario | ✅ RESOLVED — Added to Step 2 metrics table; commented placeholder in main(). | Step 2 table |
| GAP-05-M19-7 | No backpressure scenarios | ✅ RESOLVED — Added "Backpressure characterization" to metrics table. | Step 2 table |
| GAP-05-M19-8 | No startup/shutdown timing | ✅ RESOLVED — Added "Startup/shutdown timing" to metrics table. | Step 2 table |
| GAP-05-M19-9 | ±5% determinism lacks statistical backing | ⚠️ PARTIALLY — AA adds median-of-10 confirmation runs but does not specify CI or 10-run baseline. TD retains the recommendation for stronger statistical method. | § CI Regression Gate Flow |
| GAP-05-M19-10 | Plaintext parsing fragility | ✅ RESOLVED — Structured JSON output specified. CI parses JSON, not regex. | § Output Format |
| GAP-05-M19-11 | CI failure behavior undefined | ✅ RESOLVED — Tiered FAIL / WARN / UNSTABLE with auto-retry and node quarantine. | § CI Integration |
| GAP-05-M19-12 | No LOGJ_INFO baseline | ✅ RESOLVED — LOGJ_INFO latency added to metrics. | Step 2 table |
| GAP-05-M19-13 | No compile-time stripped measurement | ✅ RESOLVED — Compile-time stripped latency added to metrics. | Step 2 table |
