# FIX-M19 — Benchmark Suite: Invalid Measurements & Fragile CI

**Severity**: ❌ Fail (results invalid, CI unreliable)
**AA File**: `AA-M19-BenchmarkSuite.md`
**Phase**: 0.5 — Infrastructure & Reconciliation
**Effort**: 45 min

---

## Description

The benchmark suite design has **4 critical defects** that invalidate its measurements and make the CI regression check unreliable:

### 1. Queue blocking skews latency measurements

The 100k-iteration loop enqueues to a `BoundedBlockingQueue` (default capacity 8192). At ~iteration 8192, the queue fills up and **blocks the producer**. Every subsequent iteration measures **blocking time**, not enqueue latency. The benchmark measures the wrong thing.

### 2. No warm-up phase

The first ~1000 iterations experience cold caches, cold branch predictor, and cold TLB. These are included in the sorted latency array and skew **all percentiles**, especially P999 (where the first cold iteration is likely the max).

### 3. Throughput calculation is incorrect

Current formula: `1e9 / avg_ns` derives throughput from average per-message latency. Sustained throughput needs **time-to-drain-N-messages**: emit a burst, measure total wall-clock drain time, then compute `N / total_seconds`.

### 4. CI regex is fragile

```powershell
$p99_old = [double]$baseline[1]   # ← bug: indexes line 2, not the capture group!
```

`Get-Content` returns an array of lines. `$baseline[1]` is **line 2** of the file (the header separator `---`), not the P99 value. The `-match` test above it populates `$Matches[1]` — that should be used instead. Also `CURRENT_PHASE` is undefined in `PrintReport`.

---

## Root Cause

1. **Benchmark design was not reviewed** against real queue behavior. The author assumed BoundedBlockingQueue would never block during 100k iterations, but default capacity is 8192.
2. **No systems-programming benchmarking experience** in the authoring path — warmup is a well-known requirement in C++ benchmarking (Google Benchmark, Hayai, etc.).
3. **Throughput formula was cargo-culted** from an average-latency-derived throughput, not a direct measurement.
4. **CI script was written as pseudocode** — `$baseline[1]` indexing is a common PowerShell mistake when `$Matches` auto-variable exists.
5. `CURRENT_PHASE` was intended as a compile-time macro but never defined.

---

## Exact Fix

Replace the `MeasureLogLatency` function in `AA-M19-BenchmarkSuite.md` Step 3 with:

```cpp
#include <algorithm>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <quill/Quill.h>

struct BenchmarkResult {
    const char* name;
    double p50_ns;
    double p99_ns;
    double p999_ns;
    double throughput_mbps;
};

// Warmup: run iterations without measurement
void Warmup(quill::Logger* logger, uint32_t count = 10000) {
    for (uint32_t i = 0; i < count; ++i) {
        LOG_INFO(logger, "Warmup iteration {}", i);
    }
}

// Pre-allocated latency array to avoid heap allocation in hot path
BenchmarkResult MeasureLogLatency(const char* name,
                                   quill::Logger* logger,
                                   uint32_t iterations = 100000) {
    std::vector<double> latencies(iterations);

    auto* backend = quill::backend_tp::instance();

    // Warmup phase: discard cold iterations
    Warmup(logger, 10000);
    backend->drain();

    // Measurement phase: drain periodically to prevent queue blocking
    const uint32_t DRAIN_INTERVAL = 1000;
    for (uint32_t i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        LOG_INFO(logger, "Benchmark iteration {}", i);
        auto end = std::chrono::high_resolution_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      end - start).count();
        latencies[i] = static_cast<double>(ns);

        // Drain periodically to avoid queue blocking skew
        if ((i + 1) % DRAIN_INTERVAL == 0) {
            backend->drain();
        }
    }

    backend->drain();  // final drain

    std::sort(latencies.begin(), latencies.end());
    auto p50  = latencies[static_cast<size_t>(iterations * 0.50)];
    auto p99  = latencies[static_cast<size_t>(iterations * 0.99)];
    auto p999 = latencies[static_cast<size_t>(iterations * 0.999)];

    return {name, p50, p99, p999, 0.0 /* throughput measured separately */};
}

// Direct throughput measurement: emit N, measure total drain time
double MeasureThroughput(quill::Logger* logger, uint32_t count = 1000000) {
    auto* backend = quill::backend_tp::instance();
    Warmup(logger, 10000);
    backend->drain();

    auto start = std::chrono::high_resolution_clock::now();
    for (uint32_t i = 0; i < count; ++i) {
        LOG_INFO(logger, "Throughput message {}", i);
    }
    backend->drain();
    auto end = std::chrono::high_resolution_clock::now();

    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end - start).count();
    return static_cast<double>(count) / (static_cast<double>(total_ns) / 1e9);
}
```

Replace `PrintReport` to accept `const char* phase` instead of undefined `CURRENT_PHASE`:

```cpp
void PrintReport(const std::vector<BenchmarkResult>& results,
                 const char* phase) {
    std::cout << "\n=== Logger_Adapter Benchmark Report ===\n";
    std::cout << "Phase: " << (phase ? phase : "unknown") << "\n";
    std::cout << "Date:  " << __DATE__ << " " << __TIME__ << "\n\n";
    // ... rest unchanged ...
}
```

Replace Step 5 CI script with robust regex extraction:

```powershell
# Save baseline after Phase 1
.\x64\Debug\Benchmark.exe > .docs\benchmarks\baseline_phase1.txt

# Compare after each phase
$baselineText = Get-Content .docs\benchmarks\baseline_phase1.txt -Raw
$currentText  = & .\x64\Debug\Benchmark.exe | Out-String

if ($currentText -match "P99 \(ns\)\s+(\d+)" -and
    $baselineText -match "P99 \(ns\)\s+(\d+)") {
    $p99_new = [double]$Matches[1]
    # Re-run match on baseline to get its capture group
    $null = $baselineText -match "P99 \(ns\)\s+(\d+)"
    $p99_old = [double]$Matches[1]
    if ($p99_new -gt ($p99_old * 1.10)) {
        Write-Error "REGRESSION: P99 increased by $([int]($p99_new/$p99_old*100-100))%"
        exit 1
    }
}
Write-Host "PASS: P99 within threshold"
```

### Additional Changes

1. **main()**: Call `MeasureThroughput` and store result in throughput field.
2. **CI script**: Add median-of-5-runs for deterministic results:
   ```powershell
   # Run benchmark 5 times, take median P99
   $p99_values = @()
   1..5 | ForEach-Object {
       $output = & .\x64\Debug\Benchmark.exe | Out-String
       if ($output -match "P99 \(ns\)\s+(\d+)") {
           $p99_values += [double]$Matches[1]
       }
   }
   $p99_median = ($p99_values | Sort-Object)[2]  # median of 5
   ```
3. Define `CURRENT_PHASE` in benchmark project as a compile-time macro or pass as constructor arg.

---

## Impact if NOT Fixed

- **All benchmark data from this tool is unreliable**: P50/P99/P999 values reflect queue-blocking latency, not enqueue latency
- **Phase gates cannot trust regression checks**: A change that makes logging faster could appear as a regression (or vice versa)
- **CI will pass/fail randomly**: `$baseline[1]` is line 2 (`---`), cast to `[double]` — either fails with non-numeric error or parses a stray number
- **Throughput numbers are wrong**: Average-latency-derived throughput overestimates real sustained throughput
- **±5% deterministic claim is aspirational** without median-of-runs

---

## Verification

1. Run benchmark: P50 should be stable within ±5% across 5 runs (warm + periodic drain removes the two largest variance sources)
2. P999 should drop significantly vs the unfixed version (cold-start max removed)
3. Throughput: 1M messages should complete in well under 1 second for Release build
4. CI script: run locally with known regression to confirm `$Matches[1]` extraction works:
   ```powershell
   "P99 (ns)            12345" -match "P99 \(ns\)\s+(\d+)"; $Matches[1]
   ```
5. Run benchmark under a debugger to verify queue never blocks: set breakpoint on quill's blocking queue wait — should not hit during warmup+measurement (only during final drain)
