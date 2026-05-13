#pragma once

#include <cstdint>
#include <vector>
#include <string_view>
#include <cstring>

namespace eureka {
namespace lex {
namespace storage {

class BloomFilter {
private:
    std::vector<uint64_t> bits;
    size_t num_words;

    uint64_t murmur_hash3(std::string_view key, uint32_t seed) const {
        uint64_t h = seed;
        uint64_t k;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(key.data());
        size_t len = key.size();

        for (size_t i = 0; i < len / 8; ++i) {
            std::memcpy(&k, data + i * 8, 8);
            k *= 0x87c37b91114253d5ULL;
            k = (k << 31) | (k >> 33);
            k *= 0x4cf5ad432745937fULL;
            h ^= k;
            h = (h << 27) | (h >> 37);
            h = h * 5 + 0x52dce729;
        }

        uint64_t k_rem = 0;
        size_t rem = len & 7;
        if (rem > 0) {
            std::memcpy(&k_rem, data + (len & ~7), rem);
            k_rem *= 0x87c37b91114253d5ULL;
            k_rem = (k_rem << 31) | (k_rem >> 33);
            k_rem *= 0x4cf5ad432745937fULL;
            h ^= k_rem;
        }

        h ^= len;
        h ^= (h >> 33);
        h *= 0xff51afd7ed558ccdULL;
        h ^= (h >> 33);
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= (h >> 33);
        return h;
    }

public:
    explicit BloomFilter(size_t size_in_bytes = 8192) {
        num_words = size_in_bytes / sizeof(uint64_t);
        if (num_words == 0) num_words = 1024;
        bits.assign(num_words, 0);
    }

    void add(std::string_view key) {
        uint64_t h1 = murmur_hash3(key, 0x13579bdf);
        uint64_t h2 = murmur_hash3(key, 0x2468ace0);

        uint64_t block_idx = (h1 % num_words);
        bits[block_idx] |= (1ULL << (h2 & 63));
        bits[(block_idx + 1) % num_words] |= (1ULL << ((h1 >> 6) & 63));
    }

    bool contains(std::string_view key) const {
        uint64_t h1 = murmur_hash3(key, 0x13579bdf);
        uint64_t h2 = murmur_hash3(key, 0x2468ace0);

        uint64_t block_idx = (h1 % num_words);
        if (!(bits[block_idx] & (1ULL << (h2 & 63)))) return false;
        if (!(bits[(block_idx + 1) % num_words] & (1ULL << ((h1 >> 6) & 63)))) return false;
        return true;
    }

    void reset() {
        std::fill(bits.begin(), bits.end(), 0);
    }
};

} // namespace storage
} // namespace lex
} // namespace eureka
