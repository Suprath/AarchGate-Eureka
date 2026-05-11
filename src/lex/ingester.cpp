#include "lex/ingester.hpp"
#include "apex/AarchGate.hpp"
#include "apex/jit/ir.hpp"
#include "simdjson.h"

#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/qos.h>
#include <chrono>

namespace eureka::lex {

// Thread QoS Binder to force interactive Performance Core prioritization
static void set_performance_core_qos() noexcept {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

Ingester::Ingester() noexcept {
    head_ = 0;
    tail_ = 0;
    queue_size_ = 0;
    ingest_complete_ = false;
    match_count_ = 0;
}

Ingester::~Ingester() noexcept {
    close_file();
}

bool Ingester::open_file(const std::string& filepath) noexcept {
    filepath_ = filepath;
    int fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "[-] Failed to open file: " << filepath << std::endl;
        return false;
    }
    // Get size
    off_t size = ::lseek(fd, 0, SEEK_END);
    if (size < 0) {
        ::close(fd);
        return false;
    }
    file_size_ = static_cast<size_t>(size);
    ::close(fd);
    return true;
}

void Ingester::close_file() noexcept {
    filepath_.clear();
}


struct LogRecord {
    uint64_t status;
    uint64_t latency;
    uint64_t padding[6];
};

void Ingester::processing_worker_loop() noexcept {
    set_performance_core_qos();

    // STEP 1: Compile the JIT compiled evaluation expression
    apex::ApexEngine engine;

    std::vector<apex::core::FieldDescriptor> fields = {
        {"status", (uint32_t)offsetof(LogRecord, status), 64, apex::core::DataType::UINT64},
        {"latency", (uint32_t)offsetof(LogRecord, latency), 64, apex::core::DataType::UINT64}
    };
    engine.register_schema("LogSchema", fields, sizeof(LogRecord));

    auto s_load = apex::builder::Load("status");
    auto s_const = apex::builder::Const(status_filter_);
    auto s_eq = apex::builder::EQ(s_load, s_const);

    auto l_load = apex::builder::Load("latency");
    auto l_const = apex::builder::Const(latency_filter_);
    auto l_gt = apex::builder::GT(l_load, l_const);

    auto root = apex::builder::And(s_eq, l_gt);
    engine.set_expression("LogSchema", root, apex::ExecutionMode::BIT_SLICED);

    uint64_t processed_matches = 0;
    std::vector<LogRecord> batch(64);

    while (true) {
        LogChunk chunk;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_can_consume_.wait(lock, [this]() {
                return queue_size_ > 0 || ingest_complete_.load();
            });

            if (queue_size_ == 0 && ingest_complete_.load()) {
                break;
            }

            // Pop chunk from ring buffer queue
            chunk = chunks_[tail_];
            tail_ = (tail_ + 1) % kQueueCapacity;
            queue_size_--;
            cv_can_produce_.notify_one();
        }

        // Unpack ColumnBuffer back into row-major aligned records for the engine
        for (size_t r = 0; r < chunk.count; ++r) {
            batch[r].status = chunk.columns[0].data[r];
            batch[r].latency = chunk.columns[1].data[r];
        }

        // Execute JIT kernel
        processed_matches += engine.execute(batch.data(), chunk.count);
    }

    match_count_ += processed_matches;
}

uint64_t Ingester::run_query(uint64_t status_target, uint64_t latency_target) noexcept {
    status_filter_ = status_target;
    latency_filter_ = latency_target;
    ingest_complete_ = false;
    match_count_ = 0;
    head_ = 0;
    tail_ = 0;
    queue_size_ = 0;

    if (filepath_.empty()) {
        return 0;
    }

    // Map the file using simdjson::padded_memory_map for guaranteed padding and SIMD safety
    simdjson::padded_memory_map map(filepath_.c_str());
    if (!map.is_valid()) {
        std::cerr << "[-] Error mapping file via simdjson: " << filepath_ << std::endl;
        return 0;
    }
    file_size_ = map.view().size();

    // Stream parse using simdjson on Thread 0 / Core 0
    simdjson::ondemand::parser parser;
    std::thread consumer_thread;
    try {
        simdjson::padded_string_view json_view = map.view();
        auto docs_res = parser.iterate_many(json_view);
        if (docs_res.error()) {
            std::cerr << "[-] Error starting document stream: " << docs_res.error() << std::endl;
            return 0;
        }
        auto& docs = docs_res.value();

        // Launch Consumer Thread on Core 1 / Performance Core
        consumer_thread = std::thread(&Ingester::processing_worker_loop, this);

        set_performance_core_qos();

        size_t current_count = 0;
        LogChunk current_chunk;

        for (auto doc_res : docs) {
            if (doc_res.error()) {
                continue;
            }
            auto doc = doc_res.value();

            // Extract status
            uint64_t status_val = 0;
            auto status_res = doc["status"].get_uint64();
            if (!status_res.error()) {
                status_val = status_res.value();
            }

            // Extract latency
            uint64_t latency_val = 0;
            auto latency_res = doc["latency"].get_uint64();
            if (!latency_res.error()) {
                latency_val = latency_res.value();
            }

            // Fill ColumnBuffers (index 0: status, index 1: latency)
            current_chunk.columns[0].data[current_count] = status_val;
            current_chunk.columns[1].data[current_count] = latency_val;
            current_count++;

            if (current_count == LogChunk::kBlockSize) {
                current_chunk.count = LogChunk::kBlockSize;

                // Push filled chunk into double-buffered queue
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    cv_can_produce_.wait(lock, [this]() {
                        return queue_size_ < kQueueCapacity;
                    });

                    chunks_[head_] = current_chunk;
                    head_ = (head_ + 1) % kQueueCapacity;
                    queue_size_++;
                    cv_can_consume_.notify_one();
                }

                current_count = 0;
            }
        }

        // Push final partial chunk if there is remaining data
        if (current_count > 0) {
            current_chunk.count = current_count;
            // Pad remaining spaces with zero
            for (size_t i = current_count; i < LogChunk::kBlockSize; ++i) {
                current_chunk.columns[0].data[i] = 0;
                current_chunk.columns[1].data[i] = 0;
            }

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                cv_can_produce_.wait(lock, [this]() {
                    return queue_size_ < kQueueCapacity;
                });

                chunks_[head_] = current_chunk;
                head_ = (head_ + 1) % kQueueCapacity;
                queue_size_++;
                cv_can_consume_.notify_one();
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[-] Error during simdjson streaming: " << e.what() << std::endl;
    }

    // Signal Completeness to Consumer Thread
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        ingest_complete_ = true;
        cv_can_consume_.notify_all();
    }

    if (consumer_thread.joinable()) {
        consumer_thread.join();
    }
    return match_count_.load();
}

} // namespace eureka::lex
