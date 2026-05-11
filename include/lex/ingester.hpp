#pragma once

#include "apex/common.hpp"
#include "apex/compute/column_buffer.hpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace eureka::lex {

struct alignas(64) LogChunk {
    static constexpr size_t kBlockSize = 64;
    
    // Extracted numerical column values for a block of 64 log records
    // index 0: status, index 1: latency, etc.
    apex::compute::ColumnBuffer columns[4];
    size_t count = 0;
};

class Ingester {
public:
    Ingester() noexcept;
    ~Ingester() noexcept;

    // Initialize memory-mapped file access
    bool open_file(const std::string& filepath) noexcept;
    void close_file() noexcept;

    // Run the double-buffered streaming ingest & JIT query evaluation pipeline
    // Query parameters:
    // - status_target: value to compare "status" field with
    // - latency_target: value to compare "latency" field with
    // Returns: total matching record count
    uint64_t run_query(uint64_t status_target, uint64_t latency_target) noexcept;

    // Retrieve file statistics
    size_t get_file_size() const noexcept { return file_size_; }

private:
    // Background thread runner that executes the JIT compiled evaluation kernels
    void processing_worker_loop() noexcept;

    std::string filepath_;
    size_t file_size_ = 0;

    // Double-buffered synchronization queues
    static constexpr size_t kQueueCapacity = 16;
    LogChunk chunks_[kQueueCapacity];
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t queue_size_ = 0;

    std::mutex queue_mutex_;
    std::condition_variable cv_can_produce_;
    std::condition_variable cv_can_consume_;
    std::atomic<bool> ingest_complete_{false};

    // Parallel JIT Execution Engine settings
    uint64_t status_filter_ = 0;
    uint64_t latency_filter_ = 0;
    std::atomic<uint64_t> match_count_{0};
};

} // namespace eureka::lex
