#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "../storage/row_group.hpp"

namespace eureka {
namespace lex {
namespace ingest {

struct FieldHeat {
    std::string field_name;
    uint32_t access_count{0};
    uint8_t current_section{1}; // 1 = WARM, 2 = COLD
    double data_density{0.05};
};

class PromotionDaemon {
private:
    std::unordered_map<std::string, FieldHeat> heat_registry;

public:
    PromotionDaemon() = default;

    void record_query_access(const std::string& field_name);

    bool check_promotion_threshold(const std::string& field_name) const;

    // Compaction rewrite: migrates a Warm/Cold sidecar field into a Hot AVX-512 bit-plane
    bool execute_compaction_promotion(std::shared_ptr<storage::RowGroup>& rg, const std::string& field_name, uint8_t target_hot_slot);
};

} // namespace ingest
} // namespace lex
} // namespace eureka
