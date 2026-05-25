#include <lex/ingest/adaptive_ingester.hpp>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cctype>
#include <mutex>
#include <shared_mutex>
#include "simdjson.h"

namespace eureka {
namespace lex {
namespace ingest {

std::shared_ptr<storage::RowGroup> AdaptiveIngester::ingest_chunk(const std::vector<std::string>& json_lines) {
    auto rg = std::make_shared<storage::RowGroup>(32, 32);

    uint8_t status_slot = 0;
    uint8_t latency_slot = 1;
    uint8_t trace_slot = 32;

    {
        std::lock_guard<std::mutex> lock(schema_mutex);
        if (field_to_slot_map.find("status") == field_to_slot_map.end()) {
            field_to_slot_map["status"] = next_hot_slot++;
        }
        status_slot = field_to_slot_map["status"];

        if (field_to_slot_map.find("latency") == field_to_slot_map.end()) {
            field_to_slot_map["latency"] = next_hot_slot++;
        }
        latency_slot = field_to_slot_map["latency"];

        if (field_to_slot_map.find("trace_id") == field_to_slot_map.end()) {
            field_to_slot_map["trace_id"] = 32 + next_warm_slot++;
        }
        trace_slot = field_to_slot_map["trace_id"];
    }

    size_t total_lines = json_lines.size();
    size_t num_blocks = (total_lines + 63) / 64;
    if (num_blocks > 1024) num_blocks = 1024; // row group size limit (65536 lines)

    apex::compute::BitSlicer slicer;
    simdjson::ondemand::parser parser;

    for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx) {
        alignas(64) uint64_t status_buf[64] = {0};
        alignas(64) uint64_t latency_buf[64] = {0};

        rg->set_block_presence(status_slot, block_idx);
        rg->set_block_presence(latency_slot, block_idx);
        rg->set_block_presence(trace_slot, block_idx);

        for (int i = 0; i < 64; ++i) {
            size_t row_idx = block_idx * 64 + i;
            if (row_idx >= total_lines) break;

            const auto& line = json_lines[row_idx];

            try {
                simdjson::padded_string padded(line);
                auto doc = parser.iterate(padded);

                // 1. Status parsing via simdjson
                uint64_t status_val = 0;
                auto status_res = doc["status"].get_uint64();
                if (!status_res.error()) {
                    status_val = status_res.value();
                }
                status_buf[i] = status_val;
                rg->zone_maps[status_slot].update_u64(status_val);

                // 2. Latency parsing via simdjson
                uint64_t lat_val = 0;
                auto latency_res = doc["latency"].get_uint64();
                if (latency_res.error()) {
                    latency_res = doc["latency_ms"].get_uint64();
                }
                if (!latency_res.error()) {
                    lat_val = latency_res.value();
                }
                latency_buf[i] = lat_val;
                rg->zone_maps[latency_slot].update_u64(lat_val);

                // 3. Trace ID / Service parsing via simdjson
                auto trace_res = doc["trace_id"].get_string();
                if (trace_res.error()) {
                    trace_res = doc["service"].get_string();
                }
                if (!trace_res.error()) {
                    std::string_view trace_str = trace_res.value();
                    rg->string_bloom.add(trace_str);
                    rg->warm_dict_strings.push_back(std::string(trace_str));
                }
            } catch (const std::exception& e) {
                // Ignore exceptions on malformed lines
            }
        }

        // Vectorized slice and direct block write
        alignas(64) uint64_t status_planes[64];
        alignas(64) uint64_t latency_planes[64];

        slicer.slice_n(status_buf, 64, status_planes, 64);
        slicer.slice_n(latency_buf, 64, latency_planes, 64);

        size_t status_plane_base = status_slot * 64 * 8192;
        size_t latency_plane_base = latency_slot * 64 * 8192;

        uint64_t* status_dest = reinterpret_cast<uint64_t*>(&rg->hot_data_planes[status_plane_base]);
        uint64_t* latency_dest = reinterpret_cast<uint64_t*>(&rg->hot_data_planes[latency_plane_base]);

        for (int bit = 0; bit < 64; ++bit) {
            status_dest[bit * 1024 + block_idx] = status_planes[bit];
            latency_dest[bit * 1024 + block_idx] = latency_planes[bit];
        }
    }

    rg->fingerprint_hash = 0xABC123987ULL;
    return rg;
}

std::shared_ptr<storage::RowGroup> AdaptiveIngester::append_raw_batch(const std::vector<std::string>& raw_lines) {
    append_wal_entry(raw_lines); // 1. Persist sequentially to WAL before producer acknowledgment
    auto rg = std::make_shared<storage::RowGroup>(32, 32);
    rg->raw_chunk_buffer = raw_lines;
    rg->is_compacted.store(false, std::memory_order_release);
    rg->fingerprint_hash = 0xABC123987ULL;
    active_buffer_depth.fetch_add(raw_lines.size(), std::memory_order_relaxed);
    return rg;
}

void AdaptiveIngester::async_background_transcode(std::shared_ptr<storage::RowGroup>& rg) {
    transcode_batch_now(rg);
}

size_t AdaptiveIngester::transcode_batch_now(std::shared_ptr<storage::RowGroup>& rg) {
    auto old_rg = std::atomic_load(&rg);
    if (!old_rg || old_rg->is_compacted.load(std::memory_order_acquire)) return 0;

    size_t lines_drained = old_rg->raw_chunk_buffer.size();

    // Transcode raw chunk buffer into Hot bit-planes and Warm dictionary strings
    auto transcoded_rg = ingest_chunk(old_rg->raw_chunk_buffer);

    transcoded_rg->is_compacted.store(true, std::memory_order_release);

    // Swap buffers atomically under MVCC pointer rules (RCU-style)
    std::atomic_store(&rg, transcoded_rg);

    active_buffer_depth.fetch_sub(lines_drained, std::memory_order_relaxed);

    // Truncate WAL segment upon successful RowGroup commit
    truncate_wal();
    return lines_drained;
}

bool AdaptiveIngester::append_wal_entry(const std::vector<std::string>& batch) {
    std::lock_guard<std::mutex> lock(wal_mutex);
    std::ofstream wal(current_wal_file, std::ios::app | std::ios::binary);
    if (!wal) return false;
    std::stringstream ss;
    for (const auto& line : batch) {
        ss << line << "\n";
    }
    wal << ss.rdbuf();
    wal.flush();
    return true;
}

void AdaptiveIngester::truncate_wal() {
    std::lock_guard<std::mutex> lock(wal_mutex);
    std::ofstream wal(current_wal_file, std::ios::trunc | std::ios::binary);
}

std::vector<std::string> AdaptiveIngester::recover_from_wal() {
    std::lock_guard<std::mutex> lock(wal_mutex);
    std::vector<std::string> recovered;
    std::ifstream wal(current_wal_file, std::ios::binary);
    if (!wal) return recovered;
    std::string line;
    while (std::getline(wal, line)) {
        if (!line.empty()) {
            if (line.back() == '\r') line.pop_back();
            recovered.push_back(std::move(line));
        }
    }
    return recovered;
}

} // namespace ingest
} // namespace lex
} // namespace eureka
