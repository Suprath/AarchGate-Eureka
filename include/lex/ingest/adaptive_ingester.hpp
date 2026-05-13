#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "../storage/row_group.hpp"

namespace eureka {
namespace lex {
namespace ingest {

class AdaptiveIngester {
private:
    std::unordered_map<std::string, uint8_t> field_to_slot_map;
    uint8_t next_hot_slot{0};
    uint8_t next_warm_slot{0};

public:
    AdaptiveIngester() = default;

    // Ingests a raw NDJSON log chunk into an MVCC RowGroup.
    // Dynamically classifies fields into Hot (numeric) vs Warm (string) slots.
    std::shared_ptr<storage::RowGroup> ingest_chunk(const std::vector<std::string>& json_lines);

    // Zero-Copy Deferred Transcoding Fast-Path
    std::shared_ptr<storage::RowGroup> append_raw_batch(const std::vector<std::string>& raw_lines);
    void async_background_transcode(std::shared_ptr<storage::RowGroup> rg);

    // Expert Review Pipeline Instrumentation
    std::atomic<size_t> active_buffer_depth{0};
    size_t transcode_batch_now(std::shared_ptr<storage::RowGroup> rg);

    uint8_t get_slot_for_field(const std::string& field_name) const {
        auto it = field_to_slot_map.find(field_name);
        if (it != field_to_slot_map.end()) {
            return it->second;
        }
        return 0xFF; // Not found
    }
};

} // namespace ingest
} // namespace lex
} // namespace eureka
