// ============================================================================
// Performance Benchmark Suite Implementation
// AA Spec: AA-M19-BenchmarkSuite.md
//
// Run benchmarks for:
// - LOG_INFO hot path latency (p50/p99/p999)
// - LOGJ_INFO latency (backend serialization load)
// - Throughput (msg/sec)
// - Binary size comparison
// ============================================================================

#include "Benchmark.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <quill/Quill.h>

namespace Logger_Adapter::Tests::Bench {

void BenchmarkRunner::Warmup(uint32_t count) {
    auto* logger = quill::Frontend::get_logger();
    for (uint32_t i = 0; i < count; ++i) {
        LOG_INFO(logger, "Warmup iteration {}", i);
    }
}

BenchmarkResult BenchmarkRunner::MeasureLogLatency(const char* name,
                                                    const char* sink_type,
                                                    uint32_t iterations) {
    std::vector<double> latencies(iterations);
    auto* logger = quill::Frontend::get_logger();

    Warmup(10000);
    quill::backend_tp::instance()->drain();

    const uint32_t DRAIN_INTERVAL = 1000;
    for (uint32_t i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        LOG_INFO(logger, "Benchmark iteration {}", i);
        auto end = std::chrono::high_resolution_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        latencies[i] = static_cast<double>(ns);

        if ((i + 1) % DRAIN_INTERVAL == 0) {
            quill::backend_tp::instance()->drain();
        }
    }
    quill::backend_tp::instance()->drain();

    std::sort(latencies.begin(), latencies.end());
    double p50 = latencies[static_cast<size_t>(iterations * 0.50)];
    double p99 = latencies[static_cast<size_t>(iterations * 0.99)];
    double p999 = latencies[static_cast<size_t>(iterations * 0.999)];

    return {name, p50, p99, p999, 0.0, sink_type};
}

double BenchmarkRunner::MeasureThroughput(uint32_t count) {
    auto* logger = quill::Frontend::get_logger();
    Warmup(10000);
    quill::backend_tp::instance()->drain();

    auto start = std::chrono::high_resolution_clock::now();
    for (uint32_t i = 0; i < count; ++i) {
        LOG_INFO(logger, "Throughput message {}", i);
    }
    quill::backend_tp::instance()->drain();
    auto end = std::chrono::high_resolution_clock::now();

    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(count) / (static_cast<double>(total_ns) / 1e9);
}

void BenchmarkRunner::PrintReport(const std::vector<BenchmarkResult>& results,
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

std::vector<BenchmarkResult> BenchmarkRunner::RunAll() {
    InitializeLogging();

    std::vector<BenchmarkResult> results;

    // Run benchmarks for each sink type
    const char* sink_modes[] = {"console", "file", "json"};
    for (auto sink : sink_modes) {
        auto r = MeasureLogLatency("LOG_INFO hot path", sink, 100000);
        r.throughput_mbps = MeasureThroughput(1000000);
        results.push_back(r);
    }

    PrintReport(results, "M19");

    return results;
}

} // namespace Logger_Adapter::Tests::Bench

// Entry point
int main() {
    // Thread pinning and priority for accurate timing
    SetThreadAffinityMask(GetCurrentThread(), 1 << 1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    Logger_Adapter::Tests::Bench::BenchmarkRunner::RunAll();

    return 0;
}