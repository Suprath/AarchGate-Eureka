#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include "zone_map.hpp"
#include "bloom_filter.hpp"

namespace eureka {
namespace lex {
namespace storage {

struct ColumnMetadata {
    std::string field_name;
    uint8_t physical_slot;
    DataType data_type;
    uint8_t section; // 0 = HOT, 1 = WARM, 2 = COLD
};

class RowGroup {
private:
    std::atomic<int32_t> ref_count{0};

public:
    uint64_t fingerprint_hash{0};
    uint32_t hot_column_count{0};
    uint32_t warm_column_count{0};
    
    // Each column has 1024 blocks of 64 rows (65,536 rows total).
    // A null map requires 1024 bits = 16 uint64_t words per column.
    std::vector<std::vector<uint64_t>> null_maps;
    std::vector<ZoneMap> zone_maps;
    BloomFilter string_bloom;

    // Bit-planes: each hot column has 64 bit-planes of 65,536 bits (8192 bytes each).
    // Total hot data size = hot_column_count * 64 * 8192 bytes.
    std::vector<uint8_t> hot_data_planes;

    // Warm & Cold sidecar simulation buffers
    std::vector<std::string> warm_dict_strings;
    std::vector<uint8_t> cold_sidecar_lz4_frames;

    RowGroup(uint32_t hot_cols, uint32_t warm_cols) 
        : hot_column_count(hot_cols), warm_column_count(warm_cols), string_bloom(8192) {
        size_t total_cols = hot_cols + warm_cols;
        null_maps.assign(total_cols, std::vector<uint64_t>(16, 0));
        zone_maps.assign(total_cols, ZoneMap(DataType::UINT64));
        hot_data_planes.assign(hot_cols * 64 * 8192, 0);
    }

    void retain() {
        ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    void release() {
        if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // In a full MVCC engine, this unmaps or deletes the underlying file blocks.
        }
    }

    int32_t get_ref_count() const {
        return ref_count.load(std::memory_order_relaxed);
    }

    void set_block_presence(uint32_t col_idx, uint32_t block_idx) {
        if (col_idx < null_maps.size() && block_idx < 1024) {
            uint32_t word_idx = block_idx / 64;
            uint32_t bit_idx = block_idx & 63;
            null_maps[col_idx][word_idx] |= (1ULL << bit_idx);
        }
    }

    bool check_block_presence(uint32_t col_idx, uint32_t block_idx) const {
        if (col_idx >= null_maps.size() || block_idx >= 1024) return false;
        uint32_t word_idx = block_idx / 64;
        uint32_t bit_idx = block_idx & 63;
        return (null_maps[col_idx][word_idx] & (1ULL << bit_idx)) != 0;
    }
};

} // namespace storage
} // namespace lex
} // namespace eureka
