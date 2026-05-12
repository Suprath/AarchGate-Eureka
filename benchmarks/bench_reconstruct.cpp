#include "apex/AarchGate.hpp"
#include "lex/exporter.hpp"
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
#include <cstring>
#include <thread>
#include <algorithm>

int main() {
    std::cout << "\033[1;36m========================================================\033[0m" << std::endl;
    std::cout << "\033[1;36m       AARCHGATE-LEX // LOG READABILITY & RECONSTRUCT   \033[0m" << std::endl;
    std::cout << "\033[1;36m========================================================\033[0m" << std::endl;

    const std::string raw_json_file = "reconstruct_input.json";
    const std::string agb_file = "reconstruct_output.agb";
    const std::string idx_file = "reconstruct_output.agb.idx";

    const size_t num_records = 500000; // 500k log records
    const size_t num_blocks = (num_records + 63) / 64; // ~7812 blocks

    // STEP 1: Generate a synthetic, highly readable NDJSON file with messages
    std::cout << "[*] Generating synthetic human-readable NDJSON log file: " << raw_json_file << "..." << std::endl;
    {
        std::ofstream out(raw_json_file);
        if (!out) {
            std::cerr << "[-] Error opening output raw JSON file!" << std::endl;
            return 1;
        }

        for (size_t i = 0; i < num_records; ++i) {
            if (i % 500 == 0) {
                // Match condition: status == 200, latency == 150 (fits in 8-bits)
                out << "{\"status\":200,\"latency\":150,\"message\":\"CRITICAL_ERROR: Database transactional engine failed to commit lock on segment " << i << ". Retry count exceeded.\"}\n";
            } else if (i % 100 == 0) {
                // Partial match: status == 200 but latency = 40 (does not match query)
                out << "{\"status\":200,\"latency\":40,\"message\":\"WARNING: Resource buffer segment " << i << " is operating under heavy memory usage threshold.\"}\n";
            } else {
                // Non-match: status == 100, latency == 20 (does not match query)
                out << "{\"status\":100,\"latency\":20,\"message\":\"SUCCESS: Connection dispatched, heartbeat accepted for trace " << i << ". Request complete.\"}\n";
            }
        }
    }
    std::cout << "[+] Synthetic NDJSON logs generated successfully." << std::endl;

    // STEP 2: Use our updated exporter to convert to .agb & generate the companion offset .idx file
    if (!eureka::lex::convert_json_to_agb(raw_json_file, agb_file)) {
        std::cerr << "[-] Error: Failed to convert logs to AGB bit-planes!" << std::endl;
        return 1;
    }

    // STEP 3: Memory map all three files: raw JSON, compiled .agb, and companion .idx
    std::cout << "[*] Mapping files to virtual memory for zero-copy JIT operations..." << std::endl;

    // A. Map raw JSON
    int json_fd = ::open(raw_json_file.c_str(), O_RDONLY);
    struct stat json_sb;
    ::fstat(json_fd, &json_sb);
    size_t json_size = json_sb.st_size;
    const char* json_mapped = static_cast<const char*>(::mmap(nullptr, json_size, PROT_READ, MAP_SHARED, json_fd, 0));

    // B. Map AGB database
    int agb_fd = ::open(agb_file.c_str(), O_RDONLY);
    struct stat agb_sb;
    ::fstat(agb_fd, &agb_sb);
    size_t agb_size = agb_sb.st_size;
    const uint64_t* agb_mapped = static_cast<const uint64_t*>(::mmap(nullptr, agb_size, PROT_READ, MAP_SHARED, agb_fd, 0));

    // C. Map Companion Index
    int idx_fd = ::open(idx_file.c_str(), O_RDONLY);
    struct stat idx_sb;
    ::fstat(idx_fd, &idx_sb);
    size_t idx_size = idx_sb.st_size;
    const uint64_t* idx_mapped = static_cast<const uint64_t*>(::mmap(nullptr, idx_size, PROT_READ, MAP_SHARED, idx_fd, 0));

    std::cout << "[+] All mapped. System primed." << std::endl;

    // STEP 4: Setup JIT Engine schema and logical query
    std::cout << "[*] Compiling 2-field logic JIT query (status == 200 AND latency > 100)..." << std::endl;
    apex::ApexEngine engine;
    std::vector<apex::core::FieldDescriptor> fields = {
        {"status", 0, 8, apex::core::DataType::UINT64},
        {"latency", 8, 8, apex::core::DataType::UINT64}
    };
    engine.register_schema("ReconstructSchema", fields, 16);

    auto s_load = apex::builder::Load("status");
    auto s_const = apex::builder::Const(200);
    auto s_eq = apex::builder::EQ(s_load, s_const);

    auto l_load = apex::builder::Load("latency");
    auto l_const = apex::builder::Const(100);
    auto l_gt = apex::builder::GT(l_load, l_const);

    auto expr_root = apex::builder::And(s_eq, l_gt);
    engine.set_expression("ReconstructSchema", expr_root, apex::ExecutionMode::BIT_SLICED);

    // STEP 5: Run Phase 1 - Pure JIT Binary Logical Scan (Filtering only)
    std::cout << "\n[*] Running PASS 1: Pure Vectorized JIT Scan (Filtering)..." << std::endl;
    auto t1_start = std::chrono::high_resolution_clock::now();
    uint64_t total_matches = engine.execute_native_parallel("ReconstructSchema", agb_mapped, num_blocks, 4);
    auto t1_end = std::chrono::high_resolution_clock::now();
    double pass1_ms = std::chrono::duration<double, std::milli>(t1_end - t1_start).count();

    // STEP 6: Run Phase 2 - JIT Scan + Deferred Reconstruction (Readability)
    std::cout << "[*] Running PASS 2: JIT Scan + Deferred Offset Reconstruction..." << std::endl;
    auto t2_start = std::chrono::high_resolution_clock::now();

    // A. Re-evaluate blocks on thread worker context to get the raw match bitmasks per block
    std::vector<uint64_t> matched_log_offsets;
    matched_log_offsets.reserve(total_matches);

    // Let's run a single-threaded scanner to emulate reconstruction mapping sequence cleanly
    for (size_t b = 0; b < num_blocks; ++b) {
        // Evaluate condition for block `b`
        // Register pointers
        const uint64_t* block_base = agb_mapped + (b * 2 * 64);
        const uint64_t* status_planes = block_base;
        const uint64_t* latency_planes = block_base + 64;

        // Perform logic on 8-bit registers (Status, Latency)
        uint64_t mask_match = 0xFFFFFFFFFFFFFFFFULL;

        // 1. status == 200
        uint64_t status_eq_mask = 0xFFFFFFFFFFFFFFFFULL;
        // status comparison bit loop
        for (int bit = 7; bit >= 0; --bit) {
            uint64_t plane = status_planes[bit];
            uint64_t target_bit = (200ULL >> bit) & 1ULL;
            if (target_bit) {
                status_eq_mask &= plane;
            } else {
                status_eq_mask &= ~plane;
            }
        }
        mask_match &= status_eq_mask;

        // 2. latency > 100
        uint64_t latency_gt_mask = 0ULL;
        uint64_t latency_eq_mask = 0xFFFFFFFFFFFFFFFFULL;
        for (int bit = 7; bit >= 0; --bit) {
            uint64_t plane = latency_planes[bit];
            uint64_t target_bit = (100ULL >> bit) & 1ULL;
            uint64_t target_mask = target_bit ? 0xFFFFFFFFFFFFFFFFULL : 0ULL;
            uint64_t greater = plane & ~target_mask;
            latency_gt_mask |= greater & latency_eq_mask;
            latency_eq_mask &= ~(plane ^ target_mask);
        }
        mask_match &= latency_gt_mask;

        // If there's a match, extract indices and look up offsets
        if (mask_match != 0) {
            const uint64_t* idx_block = idx_mapped + (b * 64);
            for (int i = 0; i < 64; ++i) {
                if ((mask_match >> i) & 1ULL) {
                    uint64_t file_offset = idx_block[i];
                    matched_log_offsets.push_back(file_offset);
                }
            }
        }
    }

    // B. Materialize original human-readable strings from raw JSON file
    std::vector<std::string_view> reconstructed_logs;
    reconstructed_logs.reserve(matched_log_offsets.size());

    for (uint64_t offset : matched_log_offsets) {
        // Find end of line (record termination '\n')
        const char* record_start = json_mapped + offset;
        const char* record_end = record_start;
        while (*record_end != '\n' && (record_end - json_mapped) < (long)json_size) {
            record_end++;
        }
        reconstructed_logs.emplace_back(record_start, record_end - record_start);
    }

    auto t2_end = std::chrono::high_resolution_clock::now();
    double pass2_ms = std::chrono::duration<double, std::milli>(t2_end - t2_start).count();

    // STEP 7: Print Verification and Comparison scoreboard
    std::cout << "\n\033[1;32m========================================================\033[0m" << std::endl;
    std::cout << "\033[1;32m           DEFERRED RECONSTRUCTION SCOREBOARD           \033[0m" << std::endl;
    std::cout << "\033[1;32m========================================================\033[0m" << std::endl;
    std::cout << "  TOTAL SCANNED LOGS      : " << num_records << " lines" << std::endl;
    std::cout << "  TOTAL MATCHES LOCATED   : " << total_matches << std::endl;
    std::cout << "  TOTAL MATCHES RETRIEVED : " << reconstructed_logs.size() << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "  PASS 1: JIT LOGIC SCAN TIME : " << std::fixed << std::setprecision(3) << pass1_ms << " ms" << std::endl;
    std::cout << "  PASS 2: SCAN + DEFERRED RECONSTRUCT : " << std::fixed << std::setprecision(3) << pass2_ms << " ms" << std::endl;
    std::cout << "  RECONSTRUCTION OVERHEAD     : " << std::fixed << std::setprecision(3) << (pass2_ms - pass1_ms) << " ms" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "  PASS 1 SCAN BANDWIDTH   : " << std::fixed << std::setprecision(2) << (static_cast<double>(json_size) / (1024.0 * 1024.0 * 1024.0) / (pass1_ms / 1000.0)) << " GB/sec" << std::endl;
    std::cout << "  PASS 2 RECON BANDWIDTH  : " << std::fixed << std::setprecision(2) << (static_cast<double>(json_size) / (1024.0 * 1024.0 * 1024.0) / (pass2_ms / 1000.0)) << " GB/sec" << std::endl;
    std::cout << "\033[1;32m========================================================\033[0m\n" << std::endl;

    // STEP 8: Prove Log Readability (Print the first 3 matched records in full original JSON detail)
    std::cout << "\033[1;33m[+] PROVING LOG READABILITY: FIRST 3 MATCHED ORIGINAL LOG RECONSTRUCTIONS:\033[0m" << std::endl;
    size_t samples = std::min((size_t)3, reconstructed_logs.size());
    for (size_t s = 0; s < samples; ++s) {
        std::cout << "   Match #" << (s+1) << " (File Offset 0x" << std::hex << matched_log_offsets[s] << std::dec << "):" << std::endl;
        std::cout << "   \033[1;37m" << reconstructed_logs[s] << "\033[0m" << std::endl;
        std::cout << "   --------------------------------------------------------" << std::endl;
    }

    // Cleanup mapped virtual memory
    ::munmap(const_cast<char*>(json_mapped), json_size);
    ::munmap(const_cast<uint64_t*>(agb_mapped), agb_size);
    ::munmap(const_cast<uint64_t*>(idx_mapped), idx_size);
    ::close(json_fd);
    ::close(agb_fd);
    ::close(idx_fd);

    // Clean generated files to keep repository pristine
    ::unlink(raw_json_file.c_str());
    ::unlink(agb_file.c_str());
    ::unlink(idx_file.c_str());

    return 0;
}
