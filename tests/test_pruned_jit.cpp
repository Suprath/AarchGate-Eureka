#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <lex/jit/query_planner.hpp>
#include <lex/jit/plan_cache.hpp>

using namespace eureka::lex;

void test_query_planner_pruning() {
    std::cout << "[*] Running Test: Query Planner Zone Map & Bloom Pruning..." << std::endl;
    storage::RowGroup rg(2, 2);
    rg.zone_maps[0].update_u64(200);
    rg.zone_maps[0].update_u64(404);
    rg.zone_maps[1].update_u64(10);
    rg.zone_maps[1].update_u64(85);
    rg.string_bloom.add("abc-123");

    jit::QueryPlanner planner;

    // Predicate 1: status == 500 (Out of bounds [200, 404])
    jit::PredicateNode p1{jit::OpType::EQ, "status", 500, "", nullptr, nullptr};
    bool run1 = planner.evaluate_pruning(rg, p1);
    assert(!run1); // Pruned!

    // Predicate 2: latency > 100 (Out of bounds max=85)
    jit::PredicateNode p2{jit::OpType::GT, "latency", 100, "", nullptr, nullptr};
    bool run2 = planner.evaluate_pruning(rg, p2);
    assert(!run2); // Pruned!

    // Predicate 3: trace_id == 'xyz-999' (Not in Bloom filter)
    jit::PredicateNode p3{jit::OpType::EQ, "trace_id", 0, "xyz-999", nullptr, nullptr};
    bool run3 = planner.evaluate_pruning(rg, p3);
    assert(!run3); // Pruned!

    // Predicate 4: status == 200 AND trace_id == 'abc-123' (Valid!)
    auto p4_l = std::make_shared<jit::PredicateNode>(jit::PredicateNode{jit::OpType::EQ, "status", 200, "", nullptr, nullptr});
    auto p4_r = std::make_shared<jit::PredicateNode>(jit::PredicateNode{jit::OpType::EQ, "trace_id", 0, "abc-123", nullptr, nullptr});
    jit::PredicateNode p4{jit::OpType::AND, "", 0, "", p4_l, p4_r};

    bool run4 = planner.evaluate_pruning(rg, p4);
    assert(run4); // Executed!

    std::cout << "[+] Query Planner Pruning PASSED." << std::endl;
}

void test_plan_cache() {
    std::cout << "[*] Running Test: JIT Subset LRU Plan Cache..." << std::endl;
    jit::PlanCache cache(2);

    auto f1 = [](const uint8_t*) { return 42ULL; };
    auto f2 = [](const uint8_t*) { return 100ULL; };
    auto f3 = [](const uint8_t*) { return 999ULL; };

    cache.insert_plan(0xA1, f1);
    cache.insert_plan(0xB2, f2);

    jit::CompiledScanFunc out;
    assert(cache.lookup_plan(0xA1, out));
    assert(out(nullptr) == 42ULL);

    // Insert 3rd plan -> evicts B2 (since A1 was just hit)
    cache.insert_plan(0xC3, f3);

    assert(!cache.lookup_plan(0xB2, out)); // Evicted!
    assert(cache.lookup_plan(0xC3, out));

    std::cout << "[+] JIT Plan Cache PASSED." << std::endl;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "        AARCHGATE-EUREKA // PRUNED JIT TESTS            \n";
    std::cout << "========================================================\n";
    test_query_planner_pruning();
    test_plan_cache();
    std::cout << "[+] All Pruned JIT Tests completed successfully.\n";
    return 0;
}
