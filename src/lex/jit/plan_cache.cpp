#include <lex/jit/plan_cache.hpp>

namespace eureka {
namespace lex {
namespace jit {

bool PlanCache::lookup_plan(uint64_t subset_hash, CompiledScanFunc& out_func) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache_map.find(subset_hash);
    if (it != cache_map.end()) {
        it->second->hit_count++;
        // Move to front of LRU
        lru_list.splice(lru_list.begin(), lru_list, it->second);
        out_func = it->second->func;
        return true;
    }
    return false;
}

void PlanCache::insert_plan(uint64_t subset_hash, CompiledScanFunc func) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache_map.find(subset_hash);
    if (it != cache_map.end()) {
        it->second->func = func;
        lru_list.splice(lru_list.begin(), lru_list, it->second);
        return;
    }

    if (cache_map.size() >= capacity) {
        // Evict LRU tail
        auto last = lru_list.end();
        --last;
        cache_map.erase(last->subset_hash);
        lru_list.pop_back();
    }

    lru_list.push_front({subset_hash, func, 1});
    cache_map[subset_hash] = lru_list.begin();
}

} // namespace jit
} // namespace lex
} // namespace eureka
