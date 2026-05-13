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
