#include "apex/AarchGate.hpp"
#include "apex/compute/bit_slicer.hpp"
#include "apex/jit/ir.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <cstring>
#include <algorithm>
#include <numeric>

using namespace apex;

ir::Node* build_scaling_tree(int num_conditions) {
    auto s_load = builder::Load("status");
    auto s_const = builder::Const(500);
    auto s_eq = builder::EQ(s_load, s_const);

    auto l_load = builder::Load("latency");
    auto l_const = builder::Const(100);
    auto l_gt = builder::GT(l_load, l_const);

    auto sev_load = builder::Load("severity");
    auto sev_const = builder::Const(3);
    auto sev_eq = builder::EQ(sev_load, sev_const);

    std::vector<ir::Node*> base_conds = { s_eq, l_gt, sev_eq };

    ir::Node* root = base_conds[0];
    for (int i = 1; i < num_conditions; ++i) {
        ir::Node* next_cond = base_conds[i % 3];
        root = builder::And(root, next_cond);
    }
    return root;
}

int main() {
    std::cout << "\033[1;36m========================================================\033[0m" << std::endl;
    std::cout << "\033[1;36m   AARCHGATE-LEX // QUERY SCALING & SUSTAINED LOAD BENCH \033[0m" << std::endl;
    std::cout << "\033[1;36m========================================================\033[0m" << std::endl;

    const std::string agb_filename = "benchmark_native_scan.agb";
    const size_t num_blocks = 1300000; // 1.3 Million blocks = ~1.86 GB
    const size_t num_fields = 3;
    const size_t block_size_bytes = num_fields * 64 * sizeof(uint64_t); // 1536 bytes
    const size_t file_size_bytes = num_blocks * block_size_bytes;
    double file_gb_total = static_cast<double>(file_size_bytes) / (1024.0 * 1024.0 * 1024.0);

    // Step 1: Open existing dataset
    int fd = ::open(agb_filename.c_str(), O_RDONLY);
    if (fd < 0) {
        // Try looking in build directory
        fd = ::open("build/benchmark_native_scan.agb", O_RDONLY);
        if (fd < 0) {
            std::cerr << "[-] Error: Failed to open dataset benchmark_native_scan.agb! Please run bench_native_scan first to generate it." << std::endl;
            return 1;
        }
    }

    void* mapped = ::mmap(nullptr, file_size_bytes, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        std::cerr << "[-] Error: Failed to memory map file!" << std::endl;
        ::close(fd);
        return 1;
    }

    // Pin memory pages to physical RAM to completely eliminate paging spikes
    bool is_locked = false;
    if (::mlock(mapped, file_size_bytes) == 0) {
        std::cout << "[+] Memory pages successfully locked in physical RAM via mlock()." << std::endl;
        is_locked = true;
    } else {
        std::cerr << "[!] Warning: mlock() failed: " << std::strerror(errno) 
                  << ". Running without locked memory (paging overhead may occur)." << std::endl;
    }

    // Warm up RAM to pre-fault pages (essential if mlock failed)
    const char* mapped_ptr = static_cast<const char*>(mapped);
    volatile char dummy = 0;
    for (size_t i = 0; i < file_size_bytes; i += 4096) {
        dummy += mapped_ptr[i];
    }

    ApexEngine engine;
    std::vector<core::FieldDescriptor> fields = {
        {"status", 0, 8, core::DataType::UINT64},
        {"latency", 8, 8, core::DataType::UINT64},
        {"severity", 16, 8, core::DataType::UINT64}
    };
    engine.register_schema("BenchmarkSchema", fields, 24);

    // ─────────────────────────────────────────────────────────────────────────
    // TEST 1: QUERY COMPLEXITY SCALING (1 to 100 Conditions)
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n--------------------------------------------------------" << std::endl;
    std::cout << " TEST 1: QUERY COMPLEXITY SCALING (1 -> 100 Conditions)" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "  Conds | Time (ms) | Throughput (GB/s) | Million RPS" << std::endl;
    std::cout << "  ------|-----------|-------------------|------------" << std::endl;

    std::vector<int> test_conditions = { 1, 3, 5, 10, 20, 50, 100 };
    for (int num_cond : test_conditions) {
        ir::Node* expr_root = build_scaling_tree(num_cond);
        engine.set_expression("BenchmarkSchema", expr_root, ExecutionMode::BIT_SLICED);

        // Run benchmark
        auto t_start = std::chrono::high_resolution_clock::now();
        uint64_t total_matches = engine.execute_native_parallel("BenchmarkSchema", static_cast<const uint64_t*>(mapped), num_blocks, 4);
        auto t_end = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        double speed_gb_s = file_gb_total / (ms / 1000.0);
        size_t total_records = num_blocks * 64;
        double rps = static_cast<double>(total_records) / (ms / 1000.0);

        std::cout << "    " << std::setw(3) << num_cond << " | "
                  << std::setw(9) << std::fixed << std::setprecision(2) << ms << " | "
                  << std::setw(17) << std::fixed << std::setprecision(2) << speed_gb_s << " | "
                  << std::setw(11) << std::fixed << std::setprecision(1) << (rps / 1e6) << std::endl;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // TEST 2: SUSTAINED THROUGHPUT & LATENCY (100 Iterations of N=5 Query)
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n--------------------------------------------------------" << std::endl;
    std::cout << " TEST 2: SUSTAINED THROUGHPUT & THERMAL STABILITY" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "  Running 100 sequential scans over " << std::fixed << std::setprecision(2) << file_gb_total << " GB dataset..." << std::endl;

    ir::Node* p5_root = build_scaling_tree(5);
    engine.set_expression("BenchmarkSchema", p5_root, ExecutionMode::BIT_SLICED);

    std::vector<double> latencies_ms;
    latencies_ms.reserve(100);

    for (int i = 0; i < 100; ++i) {
        auto t_start = std::chrono::high_resolution_clock::now();
        engine.execute_native_parallel("BenchmarkSchema", static_cast<const uint64_t*>(mapped), num_blocks, 4);
        auto t_end = std::chrono::high_resolution_clock::now();
        
        double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        latencies_ms.push_back(ms);
    }

    // Sort to calculate statistics
    std::vector<double> sorted_latencies = latencies_ms;
    std::sort(sorted_latencies.begin(), sorted_latencies.end());

    double sum = std::accumulate(sorted_latencies.begin(), sorted_latencies.end(), 0.0);
    double avg_ms = sum / sorted_latencies.size();
    double p50_ms = sorted_latencies[50];
    double p95_ms = sorted_latencies[95];
    double p99_ms = sorted_latencies[99];

    // Compute average throughput as the mean of throughputs
    double throughput_sum = 0.0;
    for (double ms : latencies_ms) {
        throughput_sum += file_gb_total / (ms / 1000.0);
    }
    double avg_throughput_direct = throughput_sum / latencies_ms.size();
    double p50_throughput = file_gb_total / (p50_ms / 1000.0);

    // Thermal throttling analysis (compare first 10 runs vs last 10 runs)
    double first_10_sum = std::accumulate(latencies_ms.begin(), latencies_ms.begin() + 10, 0.0);
    double last_10_sum = std::accumulate(latencies_ms.end() - 10, latencies_ms.end(), 0.0);
    double first_10_avg = first_10_sum / 10.0;
    double last_10_avg = last_10_sum / 10.0;
    double throttle_percent = ((last_10_avg - first_10_avg) / first_10_avg) * 100.0;

    std::cout << "\n  STATISTICAL PERFORMANCE PROFILE:" << std::endl;
    std::cout << "    Average Throughput (Direct Mean) : " << std::fixed << std::setprecision(2) << avg_throughput_direct << " GB/sec" << std::endl;
    std::cout << "    Median Throughput (p50 Robust)   : " << std::fixed << std::setprecision(2) << p50_throughput << " GB/sec" << std::endl;
    std::cout << "    Throughput via Avg Latency       : " << std::fixed << std::setprecision(2) << (file_gb_total / (avg_ms / 1000.0)) << " GB/sec (arithmetic average skewed by outliers)" << std::endl;
    std::cout << "    Min Throughput                   : " << std::fixed << std::setprecision(2) << (file_gb_total / (sorted_latencies.back() / 1000.0)) << " GB/sec" << std::endl;
    std::cout << "    Max Throughput                   : " << std::fixed << std::setprecision(2) << (file_gb_total / (sorted_latencies.front() / 1000.0)) << " GB/sec" << std::endl;
    std::cout << "\n  LATENCY PERCENTILES:" << std::endl;
    std::cout << "    p50 Query Latency  : " << std::fixed << std::setprecision(2) << p50_ms << " ms" << std::endl;
    std::cout << "    p95 Query Latency  : " << std::fixed << std::setprecision(2) << p95_ms << " ms" << std::endl;
    std::cout << "    p99 Query Latency  : " << std::fixed << std::setprecision(2) << p99_ms << " ms" << std::endl;
    std::cout << "\n  THERMAL STABILITY:" << std::endl;
    std::cout << "    First 10 runs avg  : " << std::fixed << std::setprecision(2) << first_10_avg << " ms" << std::endl;
    std::cout << "    Last 10 runs avg   : " << std::fixed << std::setprecision(2) << last_10_avg << " ms" << std::endl;
    std::cout << "    Thermal Degradation: " << std::fixed << std::setprecision(1) << throttle_percent << "%" 
              << (throttle_percent > 10.0 ? " ⚠️ (Thermal throttling detected)" : " (Thermally stable)") << std::endl;
    std::cout << "========================================================" << std::endl;

    if (is_locked) {
        ::munlock(mapped, file_size_bytes);
    }
    ::munmap(mapped, file_size_bytes);
    ::close(fd);
    return 0;
}
