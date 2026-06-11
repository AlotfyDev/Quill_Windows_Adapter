// ============================================================================
// Performance Benchmark Suite
// AA Spec: AA-M19-BenchmarkSuite.md
//
// REQUIREMENT: Benchmark suite for latency, throughput, and binary size regression detection.
// Run as separate executable; integrate with CI for automated performance gates.
// ============================================================================

#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace Logger_Adapter::Tests::Bench {

struct BenchmarkResult {
    const char* name;
    double p50_ns;
    double p99_ns;
    double p999_ns;
    double throughput_mbps;
    const char* sink_type;

    // JSON output helpers
    void WriteJson(std::string& out) const;
};

class BenchmarkRunner {
public:
    static std::vector<BenchmarkResult> RunAll();
    static BenchmarkResult MeasureLogLatency(const char* name,
                                            const char* sink_type,
                                            uint32_t iterations = 100000);
    static double MeasureThroughput(uint32_t count = 1000000);
    
private:
    static void Warmup(uint32_t count = 10000);
    static void PrintReport(const std::vector<BenchmarkResult>& results,
                            const char* phase);
};

} // namespace Logger_Adapter::Tests::Bench