#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../storage/row_group.hpp"

namespace eureka {
namespace lex {
namespace jit {

enum class OpType {
    EQ,
    GT,
    AND
};

struct PredicateNode {
    OpType op;
    std::string field;
    uint64_t val_u64{0};
    std::string val_str;
    std::shared_ptr<PredicateNode> left;
    std::shared_ptr<PredicateNode> right;
};

class QueryPlanner {
public:
    QueryPlanner() = default;

    // Evaluates AST predicates against RowGroup metadata.
    // Returns false if the RowGroup can be completely skipped via Min-Max or Bloom Filter pruning.
    bool evaluate_pruning(const storage::RowGroup& rg, const PredicateNode& node) const;
};

} // namespace jit
} // namespace lex
} // namespace eureka
