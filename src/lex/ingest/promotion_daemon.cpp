#include <lex/ingest/promotion_daemon.hpp>

namespace eureka {
namespace lex {
namespace ingest {

void PromotionDaemon::record_query_access(const std::string& field_name) {
    auto& heat = heat_registry[field_name];
    heat.field_name = field_name;
    heat.access_count++;
}

bool PromotionDaemon::check_promotion_threshold(const std::string& field_name) const {
    auto it = heat_registry.find(field_name);
    if (it == heat_registry.end()) return false;

    // Heat score = (query_frequency * 0.7) + (data_density * 0.3)
    double heat_score = (it->second.access_count * 0.7) + (it->second.data_density * 0.3);
    return heat_score > 0.15 && it->second.data_density > 0.01;
}

bool PromotionDaemon::execute_compaction_promotion(std::shared_ptr<storage::RowGroup>& rg, const std::string& field_name, uint8_t target_hot_slot) {
    if (!rg) return false;

    // Simulate compaction rewrite: promote warm dictionary values to hot bit-planes
    size_t plane_base = target_hot_slot * 64 * 8192;
    for (size_t i = 0; i < rg->warm_dict_strings.size(); ++i) {
        if (i >= 65536) break;
        // Parse numeric value if applicable, or hash code
        uint64_t val = rg->string_bloom.contains(rg->warm_dict_strings[i]) ? 1ULL : 0ULL;
        
        for (int bit = 0; bit < 64; ++bit) {
            if ((val >> bit) & 1ULL) {
                size_t byte_idx = i / 8;
                size_t bit_pos = i & 7;
                rg->hot_data_planes[plane_base + bit * 8192 + byte_idx] |= (1 << bit_pos);
            }
        }
        rg->set_block_presence(target_hot_slot, i / 64);
    }

    rg->fingerprint_hash ^= 0x999ULL; // Update fingerprint
    return true;
}

} // namespace ingest
} // namespace lex
} // namespace eureka
