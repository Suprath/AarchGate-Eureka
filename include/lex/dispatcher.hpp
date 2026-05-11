#pragma once

#include <string>
#include <vector>

namespace eureka::lex {

enum class QueryType {
    SCALAR_STRING_SEARCH,
    BIT_SLICED_NUMERICAL_LOGIC
};

struct ParsedQuery {
    QueryType type;
    
    // Substring to find in scalar string mode
    std::string string_pattern;
    
    // Numeric targets for bit-sliced mode
    uint64_t status_target = 0;
    uint64_t latency_target = 0;
};

class QueryDispatcher {
public:
    static ParsedQuery parse_query(const std::string& query_str) noexcept;
};

} // namespace eureka::lex
