#include "lex/exporter.hpp"
#include "simdjson.h"
#include "apex/compute/bit_slicer.hpp"
#include <iostream>
#include <fstream>
#include <vector>

namespace eureka::lex {

bool convert_json_to_agb(const std::string& json_path, const std::string& agb_path) noexcept {
    std::cout << "[+] Converting NDJSON: " << json_path << " -> AGB: " << agb_path << std::endl;

    simdjson::padded_memory_map map(json_path.c_str());
    if (!map.is_valid()) {
        std::cerr << "[-] Error mapping input file: " << json_path << std::endl;
        return false;
    }

    std::ofstream out_file(agb_path, std::ios::binary | std::ios::trunc);
    if (!out_file) {
        std::cerr << "[-] Error creating output file: " << agb_path << std::endl;
        return false;
    }

    simdjson::ondemand::parser parser;
    size_t record_count = 0;
    size_t block_count = 0;

    try {
        simdjson::padded_string_view json_view = map.view();
        auto docs_res = parser.iterate_many(json_view);
        if (docs_res.error()) {
            std::cerr << "[-] Error starting document stream: " << docs_res.error() << std::endl;
            return false;
        }
        auto& docs = docs_res.value();

        apex::compute::BitSlicer slicer;
        std::vector<uint64_t> status_buf(64, 0);
        std::vector<uint64_t> latency_buf(64, 0);
        size_t current_count = 0;

        uint64_t status_planes[64];
        uint64_t latency_planes[64];

        for (auto doc_res : docs) {
            if (doc_res.error()) {
                continue;
            }
            auto doc = doc_res.value();

            // Extract status
            uint64_t status_val = 0;
            auto status_res = doc["status"].get_uint64();
            if (!status_res.error()) {
                status_val = status_res.value();
            }

            // Extract latency
            uint64_t latency_val = 0;
            auto latency_res = doc["latency"].get_uint64();
            if (!latency_res.error()) {
                latency_val = latency_res.value();
            }

            status_buf[current_count] = status_val;
            latency_buf[current_count] = latency_val;
            current_count++;
            record_count++;

            if (current_count == 64) {
                slicer.slice_n(status_buf.data(), 64, status_planes, 64);
                slicer.slice_n(latency_buf.data(), 64, latency_planes, 64);

                out_file.write(reinterpret_cast<const char*>(status_planes), 64 * sizeof(uint64_t));
                out_file.write(reinterpret_cast<const char*>(latency_planes), 64 * sizeof(uint64_t));

                current_count = 0;
                block_count++;
            }
        }

        // Handle partial final block with zero padding
        if (current_count > 0) {
            for (size_t i = current_count; i < 64; ++i) {
                status_buf[i] = 0;
                latency_buf[i] = 0;
            }

            slicer.slice_n(status_buf.data(), 64, status_planes, 64);
            slicer.slice_n(latency_buf.data(), 64, latency_planes, 64);

            out_file.write(reinterpret_cast<const char*>(status_planes), 64 * sizeof(uint64_t));
            out_file.write(reinterpret_cast<const char*>(latency_planes), 64 * sizeof(uint64_t));
            block_count++;
        }

    } catch (const std::exception& e) {
        std::cerr << "[-] Exception during conversion: " << e.what() << std::endl;
        return false;
    }

    std::cout << "[+] Conversion complete!" << std::endl;
    std::cout << "    - Total Records processed : " << record_count << std::endl;
    std::cout << "    - Total 64-record blocks  : " << block_count << std::endl;
    std::cout << "    - File Size written       : " << block_count * 128 * sizeof(uint64_t) << " bytes" << std::endl;

    return true;
}

} // namespace eureka::lex
