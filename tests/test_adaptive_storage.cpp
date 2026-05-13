#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <lex/ingest/adaptive_ingester.hpp>
#include <lex/ingest/promotion_daemon.hpp>

using namespace eureka::lex;

void test_adaptive_ingestion() {
    std::cout << "[*] Running Test: Adaptive Schemaless Ingestion..." << std::endl;
    ingest::AdaptiveIngester ingester;

    std::vector<std::string> json_chunk = {
        "{\"status\":200,\"latency\":45,\"trace_id\":\"abc-123\"}",
        "{\"status\":500,\"latency\":250,\"trace_id\":\"def-456\"}",
        "{\"status\":404,\"latency\":12,\"trace_id\":\"ghi-789\"}"
    };

    auto rg = ingester.ingest_chunk(json_chunk);
    assert(rg != nullptr);

    uint8_t s_slot = ingester.get_slot_for_field("status");
    uint8_t l_slot = ingester.get_slot_for_field("latency");
    assert(s_slot != 0xFF);
    assert(l_slot != 0xFF);

    // Verify Zone Maps
    assert(rg->zone_maps[s_slot].has_values);
    assert(rg->zone_maps[s_slot].data.u64.min_val == 200);
    assert(rg->zone_maps[s_slot].data.u64.max_val == 500);

    assert(rg->zone_maps[l_slot].data.u64.min_val == 12);
    assert(rg->zone_maps[l_slot].data.u64.max_val == 250);

    // Verify Bloom Filter
    assert(rg->string_bloom.contains("abc-123"));
    assert(rg->string_bloom.contains("def-456"));
    assert(!rg->string_bloom.contains("xyz-999"));

    std::cout << "[+] Adaptive Ingestion PASSED." << std::endl;
}

void test_promotion_compaction() {
    std::cout << "[*] Running Test: Adaptive Promotion Daemon..." << std::endl;
    ingest::PromotionDaemon daemon;

    // Simulate query access
    for (int i = 0; i < 50; ++i) {
        daemon.record_query_access("trace_id");
    }

    assert(daemon.check_promotion_threshold("trace_id"));

    ingest::AdaptiveIngester ingester;
    std::vector<std::string> json = {"{\"status\":200,\"trace_id\":\"abc-123\"}"};
    auto rg = ingester.ingest_chunk(json);

    bool res = daemon.execute_compaction_promotion(rg, "trace_id", 5);
    assert(res);

    std::cout << "[+] Adaptive Promotion PASSED." << std::endl;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "        AARCHGATE-EUREKA // ADAPTIVE STORAGE TESTS      \n";
    std::cout << "========================================================\n";
    test_adaptive_ingestion();
    test_promotion_compaction();
    std::cout << "[+] All Adaptive Storage Tests completed successfully.\n";
    return 0;
}
