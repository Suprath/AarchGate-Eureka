#include <lex/jit/query_planner.hpp>

namespace eureka {
namespace lex {
namespace jit {

bool QueryPlanner::evaluate_pruning(const storage::RowGroup& rg, const PredicateNode& node) const {
    if (node.op == OpType::AND) {
        bool left_res = node.left ? evaluate_pruning(rg, *node.left) : true;
        if (!left_res) return false; // Prune early
        return node.right ? evaluate_pruning(rg, *node.right) : true;
    }

    if (node.op == OpType::GT) {
        // Find field slot
        uint32_t slot = 0;
        if (node.field == "latency") slot = 1;

        if (slot < rg.zone_maps.size()) {
            const auto& zm = rg.zone_maps[slot];
            if (zm.has_values && zm.type == storage::DataType::UINT64) {
                if (zm.data.u64.max_val <= node.val_u64) {
                    return false; // Pruned by Zone Map!
                }
            }
        }
        return true;
    }

    if (node.op == OpType::EQ) {
        if (!node.val_str.empty()) {
            // String equality: check Bloom filter
            if (!rg.string_bloom.contains(node.val_str)) {
                return false; // Pruned by Bloom Filter!
            }
        } else {
            uint32_t slot = 0;
            if (node.field == "status") slot = 0;

            if (slot < rg.zone_maps.size()) {
                const auto& zm = rg.zone_maps[slot];
                if (zm.has_values && zm.type == storage::DataType::UINT64) {
                    if (node.val_u64 < zm.data.u64.min_val || node.val_u64 > zm.data.u64.max_val) {
                        return false; // Pruned by Zone Map!
                    }
                }
            }
        }
        return true;
    }

    return true;
}

} // namespace jit
} // namespace lex
} // namespace eureka
