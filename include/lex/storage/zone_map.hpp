#pragma once

#include <cstdint>
#include <limits>
#include <algorithm>
#include <variant>

namespace eureka {
namespace lex {
namespace storage {

enum class DataType {
    UINT64,
    INT64,
    DOUBLE,
    STRING
};

struct ZoneMap {
    DataType type;
    union {
        struct { uint64_t min_val; uint64_t max_val; } u64;
        struct { int64_t min_val; int64_t max_val; } i64;
        struct { double min_val; double max_val; } dbl;
    } data;
    bool has_values{false};

    ZoneMap() : type(DataType::UINT64) {
        data.u64.min_val = std::numeric_limits<uint64_t>::max();
        data.u64.max_val = 0;
    }

    explicit ZoneMap(DataType t) : type(t) {
        reset();
    }

    void reset() {
        has_values = false;
        if (type == DataType::UINT64) {
            data.u64.min_val = std::numeric_limits<uint64_t>::max();
            data.u64.max_val = 0;
        } else if (type == DataType::INT64) {
            data.i64.min_val = std::numeric_limits<int64_t>::max();
            data.i64.max_val = std::numeric_limits<int64_t>::min();
        } else if (type == DataType::DOUBLE) {
            data.dbl.min_val = std::numeric_limits<double>::max();
            data.dbl.max_val = std::numeric_limits<double>::lowest();
        }
    }

    void update_u64(uint64_t val) {
        has_values = true;
        data.u64.min_val = std::min(data.u64.min_val, val);
        data.u64.max_val = std::max(data.u64.max_val, val);
    }

    void update_i64(int64_t val) {
        has_values = true;
        data.i64.min_val = std::min(data.i64.min_val, val);
        data.i64.max_val = std::max(data.i64.max_val, val);
    }

    void update_dbl(double val) {
        has_values = true;
        data.dbl.min_val = std::min(data.dbl.min_val, val);
        data.dbl.max_val = std::max(data.dbl.max_val, val);
    }

    bool check_in_range_u64(uint64_t query_min, uint64_t query_max) const {
        if (!has_values) return false;
        if (type != DataType::UINT64) return false;
        return !(query_max < data.u64.min_val || query_min > data.u64.max_val);
    }
};

} // namespace storage
} // namespace lex
} // namespace eureka
