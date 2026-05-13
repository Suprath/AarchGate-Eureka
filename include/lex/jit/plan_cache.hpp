#pragma once

#include <cstdint>
#include <unordered_map>
#include <list>
#include <mutex>
#include <functional>

namespace eureka {
namespace lex {
namespace jit {

typedef std::function<uint64_t(const uint8_t*)> CompiledScanFunc;

struct PlanEntry {
    uint64_t subset_hash;
    CompiledScanFunc func;
    uint32_t hit_count{0};
};

class PlanCache {
private:
    size_t capacity;
    std::mutex mtx;
    std::list<PlanEntry> lru_list;
    std::unordered_map<uint64_t, decltype(lru_list.begin())> cache_map;

public:
    explicit PlanCache(size_t max_plans = 1000) : capacity(max_plans) {}

    bool lookup_plan(uint64_t subset_hash, CompiledScanFunc& out_func);

    void insert_plan(uint64_t subset_hash, CompiledScanFunc func);
};

} // namespace jit
} // namespace lex
} // namespace eureka
