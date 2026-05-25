#include "lex/ingester.hpp"
#include "lex/dispatcher.hpp"
#include "lex/exporter.hpp"
#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <iomanip>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

using namespace eureka::lex;

// Ultra-fast vectorized scalar substring counting over mapped files (Grep-Killer)
static uint64_t run_scalar_string_search(const std::string& filepath, const std::string& pattern, bool pin_memory) noexcept {
    int fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd < 0) return 0;

    struct stat sb;
    if (::fstat(fd, &sb) < 0 || sb.st_size <= 0) {
        ::close(fd);
        return 0;
    }
    size_t size = static_cast<size_t>(sb.st_size);

    void* mapped = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        ::close(fd);
        return 0;
    }
    ::madvise(mapped, size, MADV_SEQUENTIAL);

    if (pin_memory) {
        if (::mlock(mapped, size) != 0) {
            std::cerr << "[Warning] Failed to pin index pages in RAM: " << std::strerror(errno) << std::endl;
        } else {
            std::cout << "[+] Successfully pinned " << size << " bytes of index pages in RAM." << std::endl;
        }
    }

    const char* start = static_cast<const char*>(mapped);
    const char* end = start + size;
    uint64_t occurrences = 0;

    size_t pat_len = pattern.length();
    if (pat_len == 0) {
        if (pin_memory) ::munlock(mapped, size);
        ::munmap(mapped, size);
        ::close(fd);
        return 0;
    }

    const char* p = start;
    while (p + pat_len <= end) {
        // Fast memchr scan for the first letter of pattern
        const char* candidate = static_cast<const char*>(::memchr(p, pattern[0], end - p));
        if (!candidate) {
            break;
        }
        if (candidate + pat_len > end) {
            break;
        }
        if (::std::strncmp(candidate, pattern.c_str(), pat_len) == 0) {
            occurrences++;
            p = candidate + pat_len;
        } else {
            p = candidate + 1;
        }
    }

    if (pin_memory) ::munlock(mapped, size);
    ::munmap(mapped, size);
    ::close(fd);
    return occurrences;
}

#include "lex/ingest/adaptive_ingester.hpp"

int main(int argc, char* argv[]) {
    std::cout << "\033[1;36m========================================================\033[0m" << std::endl;
    std::cout << "\033[1;36m       AARCHGATE-LEX // FAST SILICON LOG QUERY TOOL     \033[0m" << std::endl;
    std::cout << "\033[1;36m========================================================\033[0m" << std::endl;

    bool pin_memory = false;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--pin-memory") == 0) {
            pin_memory = true;
        } else {
            args.push_back(argv[i]);
        }
    }

    if (args.size() >= 3 && args[0] == "--convert") {
        std::string input_json = args[1];
        std::string output_agb = args[2];
        bool success = convert_json_to_agb(input_json, output_agb);
        return success ? 0 : 1;
    }

    if (args.size() >= 2 && args[0] == "--stream-ingest") {
        std::string output_agb = args[1];
        std::cout << "[+] Tailing live stdin log stream into AarchGate-Eureka v2 engine..." << std::endl;
        std::cout << "[+] Target DB: " << output_agb << std::endl;
        
        eureka::lex::ingest::AdaptiveIngester ingester;
        std::string line;
        std::vector<std::string> batch;
        size_t total_ingested = 0;
        
        while (std::getline(std::cin, line)) {
            batch.push_back(std::move(line));
            if (batch.size() >= 1000) {
                auto rg = ingester.append_raw_batch(batch);
                ingester.transcode_batch_now(rg);
                total_ingested += batch.size();
                batch.clear();
            }
        }
        if (!batch.empty()) {
            auto rg = ingester.append_raw_batch(batch);
            ingester.transcode_batch_now(rg);
            total_ingested += batch.size();
        }
        std::cout << "[+] Stream closed. Total live events ingested: " << total_ingested << std::endl;
        return 0;
    }

    if (args.size() < 2) {
        std::cerr << "Usage: " << argv[0] << " [--pin-memory] <query_string> <log_file_path>" << std::endl;
        std::cerr << "       " << argv[0] << " --convert <input_json_path> <output_agb_path>" << std::endl;
        std::cerr << "       " << argv[0] << " --stream-ingest <output_agb_path>" << std::endl;
        std::cerr << "Example (Logical): " << argv[0] << " \"status == 200 AND latency > 150\" web_logs.json" << std::endl;
        std::cerr << "Example (Scalar):  " << argv[0] << " \"contains(\\\"error\\\")\" web_logs.json" << std::endl;
        std::cerr << "Example (Stream):  cat logs.ndjson | " << argv[0] << " --stream-ingest live.agb" << std::endl;
        return 1;
    }

    std::string query_str = args[0];
    std::string filepath = args[1];

    std::cout << "[+] Loading File: " << filepath << std::endl;
    std::cout << "[+] Compiling & Analyzing Query: \"" << query_str << "\"" << std::endl;

    ParsedQuery query = QueryDispatcher::parse_query(query_str);

    uint64_t total_matches = 0;
    size_t file_bytes = 0;

    auto t_start = std::chrono::high_resolution_clock::now();

    if (query.type == QueryType::SCALAR_STRING_SEARCH) {
        std::cout << "[+] Dispatching Profile: [SCALAR TEXT PATTERN VECTOR ENGINE]" << std::endl;
        total_matches = run_scalar_string_search(filepath, query.string_pattern, pin_memory);
        
        // Get file size
        struct stat sb;
        if (::stat(filepath.c_str(), &sb) == 0) {
            file_bytes = sb.st_size;
        }
    } else {
        std::cout << "[+] Dispatching Profile: [BIT-SLICED PARALLEL JIT ENGINE]" << std::endl;
        std::cout << "    [+] Sub-Condition 1: [status == " << query.status_target << "]" << std::endl;
        std::cout << "    [+] Sub-Condition 2: [latency > " << query.latency_target << "]" << std::endl;

        Ingester ingester;
        ingester.set_pin_memory(pin_memory);
        if (!ingester.open_file(filepath)) {
            std::cerr << "[-] Error: Failed to memory map file: " << filepath << std::endl;
            return 1;
        }
        file_bytes = ingester.get_file_size();
        total_matches = ingester.run_query(query.status_target, query.latency_target);
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    double duration_sec = duration_ms / 1000.0;

    double file_gb = static_cast<double>(file_bytes) / (1024.0 * 1024.0 * 1024.0);
    double speed_gb_s = file_gb / (duration_sec > 0.0 ? duration_sec : 0.001);

    // Approximate total log records processed (average 120 bytes per JSON record in raw NDJSON files)
    size_t total_records = file_bytes / 120;
    if (total_records == 0) total_records = 1;

    // Apple M3 P-cores clock speed (4.05 GHz)
    double clock_hz = 4.05e9;
    double total_cycles = duration_sec * clock_hz;
    double cycles_per_record = total_cycles / total_records;

    std::cout << "\033[1;32m" << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << "                    PERFORMANCE METRICS                 " << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << "  TOTAL MATCHING RECORDS : " << total_matches << std::endl;
    std::cout << "  TOTAL DATA PROCESS     : " << std::fixed << std::setprecision(3) << file_gb << " GB (" << file_bytes << " bytes)" << std::endl;
    std::cout << "  TOTAL EXECUTION TIME   : " << std::fixed << std::setprecision(2) << duration_ms << " ms" << std::endl;
    std::cout << "  PROCESSING BANDWIDTH   : " << std::fixed << std::setprecision(2) << speed_gb_s << " GB/sec" << std::endl;
    std::cout << "  M3 CYCLES PER RECORD   : " << std::fixed << std::setprecision(2) << cycles_per_record << " cycles" << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << "\033[0m" << std::endl;

    return 0;
}
