# AA-M19 — Performance Benchmark Suite (After-Audit New)

> **Phase**: 0.5 — 🔄 Infrastructure & Reconciliation  
> **Effort**: 1 h  
> **Depends on**: Nothing (standalone tool)  
> **Capability Gap**: No way to measure latency regression across implementation phases

---

## Problem

Every phase gate in the implementation plan requires "no regression" verification, but there is **no benchmark tool** to measure latency, throughput, or binary size. Without a baseline:
- A well-intentioned change could double hot-path latency silently
- Compile-time log level optimizations (C02) cannot be verified
- Queue config tuning (M08) has no performance target
- CI cannot catch regressions automatically

---

## Implementation Plan

### Step 1 — Create Benchmark Project

New project or test file: `tests/bench/Benchmark.cpp`

### Step 2 — Baseline Metrics

| Metric | How | What It Catches |
|--------|-----|-----------------|
| **LOG_INFO latency p50/p99/p999** | Spin loop: `LOG_INFO` → high-res timer → repeat 100k iterations | Hot-path slowdown from any change |
| **LOGJ_INFO latency p50/p99/p999** | Same as LOG_INFO but with JSON field construction | JSON-logging path regression (AA-M04) |
| **Compile-time stripped latency** | `LOG_INFO` when `QUILL_LOG_LEVEL > Info` — compiler removes body | C02 correctness: stripped vs unstripped should be ~0ns |
| **Throughput (msg/sec)** | Burst: emit 1M messages, measure drain time | Queue contention, backend saturation |
| **Binary size (Debug/Release)** | CI script: `[System.IO.FileInfo]::new("Logger_Adapter.lib").Length` | C02 compile-level verification, code bloat |
| **Queue drain time** | High-priority flush: emit 100k msgs → `quill::backend_tp::drain()` | M08 queue config effectiveness |
| **Multi-threaded contention (4 thr)** | 4 threads each LOG_INFO 100k times, measure per-thread p50/p99 + total throughput | Producer-producer contention, backend saturation |
| **Backpressure characterization** | Slow consumer (1ms delay), bounded queue full, unbounded memory growth | Queue backpressure correctness (M08) |
| **Startup/shutdown timing** | Cold InitLogging, warm InitLogging, StopLogging flush times | Deployment reliability, hang detection |

### Step 3 — Hot-Path Latency Benchmark

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
    const char* sink_type;  // "null", "file", "memory"
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
                                   const char* sink_type,
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

    return {name, p50, p99, p999, 0.0 /* throughput measured separately */, sink_type};
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

### Step 4 — Print Report

```cpp
void PrintReport(const std::vector<BenchmarkResult>& results,
                 const char* phase) {
    std::cout << "\n=== Logger_Adapter Benchmark Report ===\n";
    std::cout << "Phase: " << (phase ? phase : "unknown") << "\n";
    std::cout << "Date:  " << __DATE__ << " " << __TIME__ << "\n\n";
    std::cout << std::left << std::setw(30) << "Test"
              << std::right << std::setw(12) << "P50 (ns)"
              << std::setw(12) << "P99 (ns)"
              << std::setw(12) << "P999 (ns)"
              << std::setw(14) << "Msg/sec" << "\n";
    std::cout << std::string(80, '-') << "\n";
    for (auto& r : results) {
        std::cout << std::left << std::setw(30) << r.name
                  << std::right << std::setw(12) << r.p50_ns
                  << std::setw(12) << r.p99_ns
                  << std::setw(12) << r.p999_ns
                  << std::setw(14) << r.throughput_mbps << "\n";
    }
    std::cout << std::string(80, '-') << "\n";
}

int main() {
    // Setup: thread pinning and priority
    SetThreadAffinityMask(GetCurrentThread(), 1 << 1);  // pin to core 1, not core 0
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    timeBeginPeriod(1);  // 1ms timer resolution

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart < 1000000) {
        fprintf(stderr, "WARNING: QPC frequency < 1 MHz (%lld ticks/sec)\n", freq.QuadPart);
    }

    InitializeLogging();
    auto* logger = GetDefaultLogger();

    std::vector<BenchmarkResult> results;

    // Run benchmarks for each sink type
    const char* sink_modes[] = {"null", "file", "memory"};
    for (auto sink : sink_modes) {
        auto r = MeasureLogLatency("LOG_INFO hot path", logger, sink, 100000);
        r.throughput_mbps = MeasureThroughput(logger, 1000000);
        results.push_back(r);
    }

    // Multi-threaded contention: 4 threads
    // (actual implementation spawns 4 threads, measures per-thread p50/p99)

    // Backpressure characterization (slow consumer, queue full)
    // (logged to separate section, not compared against thresholds)

    // Startup/shutdown timing
    // (cold init, warm init, flush with backlog)

    // Save baseline to file for CI regression comparison
    PrintReport(results, "phase1");

    // Write structured JSON for CI parsing
    // (to .docs/benchmarks/bench_results.json)

    timeEndPeriod(1);
    return 0;
}
```

### Step 5 — CI Integration

#### Regression Thresholds (Tiered)

| Metric | Soft Gate (WARN) | Hard Gate (FAIL) |
|--------|------------------|------------------|
| P50 latency | >15% increase | — |
| P99 latency | — | >10% increase |
| Throughput (batch_1000) | — | >15% decrease |
| Binary size (Release) | >5% increase | — |

#### CI Regression Gate Flow

```
            ┌─────────────────────────────┐
            │ P99 > baseline * 1.10 ?     │
            └──────────┬──────────────────┘
                       │
                ┌──────┴──────┐
                │ YES         │ NO
                │             │
         ┌──────┴──────┐      │ PASS
         │ Median of 10│      │
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

- **FAIL**: Pipeline fails, blocks PR merge. Developer must fix regression or update baseline with documented trade-off.
- **WARN**: Pipeline passes but annotates PR with warning. Requires human review before merge.
- **UNSTABLE**: Benchmark results flagged as untrusted (CI node interference). Auto-retry once. If 2 consecutive runs untrusted, quarantine CI node.

#### CI Script

```powershell
# Binary size measurement (CI script, not benchmark C++ code)
$debugSize = (Get-Item "x64\Debug\Experimental_Console.exe").Length
$releaseSize = (Get-Item "x64\Release\Experimental_Console.exe").Length

# Parse structured JSON (not fragile regex on plaintext)
$resultsFile = ".docs\benchmarks\bench_results.json"
$currentJson = Get-Content $resultsFile -Raw | ConvertFrom-Json

# Load machine-specific baseline
$machine = $env:COMPUTERNAME
$baselineFile = ".docs\benchmarks\baseline_${machine}_Release.json"
if (-not (Test-Path $baselineFile)) {
    # First run — save baseline and pass unconditionally
    Copy-Item $resultsFile $baselineFile
    Write-Host "PASS: Initial baseline saved to $baselineFile"
    exit 0
}

$baselineJson = Get-Content $baselineFile -Raw | ConvertFrom-Json

# Compare P99 (hard gate)
$p99_new = $currentJson.latency_ns.p99
$p99_old = $baselineJson.latency_ns.p99
if ($p99_new -gt ($p99_old * 1.10)) {
    # Re-run median of 10 for confirmation
    $p99_values = @()
    1..10 | ForEach-Object {
        & .\x64\Release\Benchmark.exe | Out-Null
        $runJson = Get-Content $resultsFile -Raw | ConvertFrom-Json
        $p99_values += $runJson.latency_ns.p99
    }
    $p99_median = ($p99_values | Sort-Object)[4]  # median of 10
    if ($p99_median -gt ($p99_old * 1.10)) {
        Write-Error "REGRESSION: P99 median $p99_median > baseline $p99_old (110% threshold)"
        exit 1
    } else {
        Write-Warning "P99 single-run spike detected but median of 10 within threshold"
    }
}
Write-Host "PASS: P99 within threshold"

# Environmental guard: CPU load check
$cpuLoad = (Get-CimInstance Win32_Processor | Select-Object -First 1).LoadPercentage
if ($cpuLoad -gt 20) {
    Write-Warning "UNTRUSTED: CPU load ${cpuLoad}% at benchmark start"
    # Mark in results but don't fail
}
```

#### Output Format

Benchmark writes structured JSON to `.docs/benchmarks/bench_results.json`:

```json
{
  "phase": "phase1",
  "timestamp": "Jun 11 2026 14:30:00",
  "machine": "ci-runner-42",
  "build_config": "Release|x64",
  "latency_ns": {
    "p50": 85.0, "p99": 320.0, "p999": 1250.0,
    "max": 5800.0, "mean": 112.0, "stddev": 45.0,
    "outlier_count": 12, "total_samples": 100000,
    "timer_overhead_ns": 25.0
  },
  "throughput_msgs_per_sec": {
    "batch_1": 1250000, "batch_10": 3400000,
    "batch_100": 5800000, "batch_1000": 7200000,
    "batch_10000": 7500000
  },
  "binary_size_bytes": {
    "debug": 2457600, "release": 1310720
  },
  "sink_type": "file",
  "ci_metadata": {
    "median_of_runs": 10,
    "run_times_ms": [4210, 3890, 4120, 3950, 4050, 4100, 3880, 4150, 4020, 4080],
    "p99_values_raw": [315.0, 325.0, 310.0, 330.0, 320.0, 318.0, 322.0, 315.0, 328.0, 319.0],
    "p99_median": 319.0,
    "cpu_load_pct": 5,
    "untrusted": false
  }
}
```

CI parses JSON — no regex fragility. Human-readable table printed to console for developer inspection.

---

## Acceptance Criteria

- [ ] Benchmark compiles and runs with Debug|x64
- [ ] Baseline report saved to `.docs/benchmarks/baseline_phase1.txt`
- [ ] CI script can compare current vs baseline for P99 regression
- [ ] Binary size measurement included in baseline
- [ ] Benchmark takes <30 seconds to run (100k iterations)
- [ ] Results are deterministic within ±5% across runs (same machine, same load)

---

## Files Changed

| File | Action |
|------|--------|
| `Logger_Adapter/tests/bench/Benchmark.cpp` | New file |
| `Logger_Adapter/tests/bench/Benchmark.hpp` | New file |
| `Logger_Adapter/docs/benchmarks/baseline_phase1.txt` | New (baseline output) |
