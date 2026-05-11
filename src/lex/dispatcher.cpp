#include "lex/dispatcher.hpp"
#include <sstream>
#include <algorithm>
#include <iostream>

namespace eureka::lex {

static std::string trim(const std::string& s) {
    auto start = std::find_if_not(s.begin(), s.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

ParsedQuery QueryDispatcher::parse_query(const std::string& query_str) noexcept {
    ParsedQuery result;
    std::string q = trim(query_str);

    // If string contains comparisons (==, >, <) then it is a complex numerical logic filter
    bool is_numerical = (q.find("==") != std::string::npos ||
                         q.find(">") != std::string::npos ||
                         q.find("<") != std::string::npos);

    if (!is_numerical) {
        // Fallback to Scalar String Search
        result.type = QueryType::SCALAR_STRING_SEARCH;
        // Strip quotes if they exist, e.g. contains("error") or "error"
        if (q.rfind("contains(", 0) == 0 && q.back() == ')') {
            std::string inner = q.substr(9, q.size() - 10);
            inner = trim(inner);
            if (inner.front() == '"' && inner.back() == '"') {
                inner = inner.substr(1, inner.size() - 2);
            }
            result.string_pattern = inner;
        } else {
            if (q.front() == '"' && q.back() == '"') {
                q = q.substr(1, q.size() - 2);
            }
            result.string_pattern = q;
        }
        return result;
    }

    // Parse numerical comparison terms
    result.type = QueryType::BIT_SLICED_NUMERICAL_LOGIC;
    result.status_target = 0;
    result.latency_target = 0;

    // Helper token stream tokenizer
    std::replace(q.begin(), q.end(), '&', ' ');
    std::replace(q.begin(), q.end(), '|', ' ');
    
    // Replace AND/OR logic operators to clean tokens
    auto replace_all_substrings = [](std::string& str, const std::string& from, const std::string& to) {
        size_t start_pos = 0;
        while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
    };
    replace_all_substrings(q, "AND", " ");
    replace_all_substrings(q, "and", " ");

    std::stringstream ss(q);
    std::string word;
    while (ss >> word) {
        if (word.find("status") != std::string::npos) {
            // Find status value
            size_t pos_eq = word.find("==");
            if (pos_eq != std::string::npos) {
                std::string val_str = word.substr(pos_eq + 2);
                if (val_str.empty()) {
                    ss >> val_str; // grab next word
                }
                try { result.status_target = std::stoull(val_str); } catch(...) {}
            } else {
                // Check if the assignment is separate e.g. status == 500
                std::string op, val;
                if (ss >> op >> val) {
                    try { result.status_target = std::stoull(val); } catch(...) {}
                }
            }
        } else if (word.find("latency") != std::string::npos) {
            // Find latency value
            size_t pos_gt = word.find(">");
            if (pos_gt != std::string::npos) {
                std::string val_str = word.substr(pos_gt + 1);
                if (val_str.empty()) {
                    ss >> val_str;
                }
                try { result.latency_target = std::stoull(val_str); } catch(...) {}
            } else {
                std::string op, val;
                if (ss >> op >> val) {
                    try { result.latency_target = std::stoull(val); } catch(...) {}
                }
            }
        }
    }

    return result;
}

} // namespace eureka::lex
