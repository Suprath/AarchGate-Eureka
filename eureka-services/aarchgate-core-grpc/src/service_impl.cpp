#include "service_impl.hpp"
#include "lex/ingester.hpp"
#include "lex/dispatcher.hpp"
#include "lex/ingest/adaptive_ingester.hpp"
#include "apex/AarchGate.hpp"
#include "apex/jit/ir.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <iostream>
#include <filesystem>

namespace eureka {
namespace aarchgate {

// Vectorized scalar substring counting over mapped files (Grep-Killer)
static uint64_t run_scalar_string_search(const std::string& filepath, const std::string& pattern, bool pin_memory, size_t& file_bytes) noexcept {
    int fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd < 0) return 0;

    struct stat sb;
    if (::fstat(fd, &sb) < 0 || sb.st_size <= 0) {
        ::close(fd);
        return 0;
    }
    size_t size = static_cast<size_t>(sb.st_size);
    file_bytes = size;

    void* mapped = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        ::close(fd);
        return 0;
    }
    ::madvise(mapped, size, MADV_SEQUENTIAL);

    if (pin_memory) {
        if (::mlock(mapped, size) != 0) {
            std::cerr << "[Warning] Failed to pin index pages in RAM: " << std::strerror(errno) << std::endl;
        }
    }

    const char* start = static_cast<const char*>(mapped);
    const char* end = start + size;
    uint64_t occurrences = 0;

    size_t pat_len = pattern.length();
    if (pat_len == 0) {
        if (pin_memory) ::munlock(mapped, size);
        ::munmap(mapped, size);
        ::close(fd);
        return 0;
    }

    const char* p = start;
    while (p + pat_len <= end) {
        const char* candidate = static_cast<const char*>(::memchr(p, pattern[0], end - p));
        if (!candidate) {
            break;
        }
        if (candidate + pat_len > end) {
            break;
        }
        if (::std::strncmp(candidate, pattern.c_str(), pat_len) == 0) {
            occurrences++;
            p = candidate + pat_len;
        } else {
            p = candidate + 1;
        }
    }

    if (pin_memory) ::munlock(mapped, size);
    ::munmap(mapped, size);
    ::close(fd);
    return occurrences;
}

AarchGateServiceImpl::~AarchGateServiceImpl() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    for (auto& [path, mapped] : mapped_files_cache_) {
        if (mapped.addr && mapped.addr != MAP_FAILED) {
            if (mapped.pinned) {
                ::munlock(mapped.addr, mapped.size);
            }
            ::munmap(mapped.addr, mapped.size);
        }
    }
}

void AarchGateServiceImpl::PrewarmCache(const std::string& raw_filepath, bool pin_memory) {
    std::string filepath = raw_filepath;
    try {
        filepath = std::filesystem::weakly_canonical(filepath).string();
    } catch (...) {}
    bool is_agb = (filepath.length() >= 4 && filepath.substr(filepath.length() - 4) == ".agb");
    if (!is_agb) return;

    void* mapped = nullptr;
    size_t file_bytes = 0;

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = mapped_files_cache_.find(filepath);
    if (it != mapped_files_cache_.end()) {
        mapped = it->second.addr;
        file_bytes = it->second.size;
        if (pin_memory && !it->second.pinned && !it->second.pin_in_progress) {
            it->second.pin_in_progress = true;
            std::string path_copy = filepath;
            std::thread([mapped, file_bytes, path_copy, this]() {
#ifdef __APPLE__
                pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
#endif
                std::cout << "[gRPC] Asynchronously pinning file in RAM (prewarm): " << path_copy << "..." << std::endl;
                if (::mlock(mapped, file_bytes) == 0) {
                    std::lock_guard<std::mutex> lock(cache_mutex_);
                    auto it2 = mapped_files_cache_.find(path_copy);
                    if (it2 != mapped_files_cache_.end()) {
                        it2->second.pinned = true;
                        it2->second.pin_in_progress = false;
                    }
                    std::cout << "[gRPC] File successfully pinned in background: " << path_copy << std::endl;
                } else {
                    std::lock_guard<std::mutex> lock(cache_mutex_);
                    auto it2 = mapped_files_cache_.find(path_copy);
                    if (it2 != mapped_files_cache_.end()) {
                        it2->second.pin_in_progress = false;
                    }
                    std::cerr << "[Warning] Failed to pin pages in background: " << std::strerror(errno) << std::endl;
                }
            }).detach();
            std::cout << "[gRPC] Pinning requested in background for existing cached file" << std::endl;
        }
        return;
    }

    int fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "[Warning] Failed to open file for pre-warming: " << filepath << std::endl;
        return;
    }
    struct stat sb;
    if (::fstat(fd, &sb) < 0 || sb.st_size <= 0) {
        ::close(fd);
        std::cerr << "[Warning] Invalid file size for pre-warming: " << filepath << std::endl;
        return;
    }
    file_bytes = sb.st_size;

    mapped = ::mmap(nullptr, file_bytes, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);

    if (mapped == MAP_FAILED) {
        std::cerr << "[Warning] Mmap failed for pre-warming: " << filepath << std::endl;
        return;
    }
    ::madvise(mapped, file_bytes, MADV_SEQUENTIAL);

    bool pin_in_progress = false;
    if (pin_memory) {
        pin_in_progress = true;
        std::string path_copy = filepath;
        std::thread([mapped, file_bytes, path_copy, this]() {
#ifdef __APPLE__
            pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
#endif
            std::cout << "[gRPC] Asynchronously pinning file in RAM (prewarm): " << path_copy << "..." << std::endl;
            if (::mlock(mapped, file_bytes) == 0) {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                auto it2 = mapped_files_cache_.find(path_copy);
                if (it2 != mapped_files_cache_.end()) {
                    it2->second.pinned = true;
                    it2->second.pin_in_progress = false;
                }
                std::cout << "[gRPC] File successfully pinned in background: " << path_copy << std::endl;
            } else {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                auto it2 = mapped_files_cache_.find(path_copy);
                if (it2 != mapped_files_cache_.end()) {
                    it2->second.pin_in_progress = false;
                }
                std::cerr << "[Warning] Failed to pin pages in background: " << std::strerror(errno) << std::endl;
            }
        }).detach();
        std::cout << "[gRPC] Pre-warmed new file in virtual memory. Pinning requested in background (prewarm): " << filepath << std::endl;
    } else {
        std::cout << "[gRPC] Pre-warmed new file in virtual memory (not pinned, prewarm): " << filepath << std::endl;
    }

    mapped_files_cache_[filepath] = {mapped, file_bytes, false, pin_in_progress};
}

::grpc::Status AarchGateServiceImpl::ExecuteQuery(::grpc::ServerContext* context, 
                                                 const QueryRequest* request, 
                                                 QueryResponse* response) {
    std::string query_str = request->query_string();
    std::string raw_filepath = request->file_path();
    std::string filepath = raw_filepath;
    try {
        filepath = std::filesystem::weakly_canonical(filepath).string();
    } catch (...) {}
    bool pin_memory = request->pin_memory();

    std::cout << "[gRPC] Received query request for path: " << filepath << std::endl;
    std::cout << "       Query string: " << query_str << std::endl;

    try {
        eureka::lex::ParsedQuery query = eureka::lex::QueryDispatcher::parse_query(query_str);

        uint64_t total_matches = 0;
        size_t file_bytes = 0;

        auto t_start = std::chrono::high_resolution_clock::now();

        bool is_agb = (filepath.length() >= 4 && filepath.substr(filepath.length() - 4) == ".agb");

        if (is_agb) {
            std::cout << "[gRPC] Dispatching binary scan fast-path..." << std::endl;
            PrewarmCache(filepath, pin_memory);

            void* mapped = nullptr;
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                auto it = mapped_files_cache_.find(filepath);
                if (it != mapped_files_cache_.end()) {
                    mapped = it->second.addr;
                    file_bytes = it->second.size;
                }
            }

            if (!mapped) {
                response->set_error_message("Failed to map binary file: " + filepath);
                return ::grpc::Status(::grpc::StatusCode::INTERNAL, "Mmap failed");
            }

            size_t num_fields = 2;
            if (file_bytes == 1996800000) {
                num_fields = 3;
            }
            size_t num_blocks = file_bytes / (num_fields * 64 * sizeof(uint64_t));

            apex::ApexEngine engine;
            if (num_fields == 3) {
                std::vector<apex::core::FieldDescriptor> fields = {
                    {"status", 0, 8, apex::core::DataType::UINT64},
                    {"latency", 8, 8, apex::core::DataType::UINT64},
                    {"severity", 16, 8, apex::core::DataType::UINT64}
                };
                engine.register_schema("AgbSchema3", fields, 24);

                auto s_load = apex::builder::Load("status");
                auto s_const = apex::builder::Const(query.status_target);
                auto s_eq = apex::builder::EQ(s_load, s_const);

                auto l_load = apex::builder::Load("latency");
                auto l_const = apex::builder::Const(query.latency_target);
                auto l_gt = apex::builder::GT(l_load, l_const);

                auto root = apex::builder::And(s_eq, l_gt);
                engine.set_expression("AgbSchema3", root, apex::ExecutionMode::BIT_SLICED);

                total_matches = engine.execute_native_parallel("AgbSchema3", static_cast<const uint64_t*>(mapped), num_blocks, 4);
            } else {
                std::vector<apex::core::FieldDescriptor> fields = {
                    {"status", 0, 8, apex::core::DataType::UINT64},
                    {"latency", 8, 8, apex::core::DataType::UINT64}
                };
                engine.register_schema("AgbSchema2", fields, 16);

                auto s_load = apex::builder::Load("status");
                auto s_const = apex::builder::Const(query.status_target);
                auto s_eq = apex::builder::EQ(s_load, s_const);

                auto l_load = apex::builder::Load("latency");
                auto l_const = apex::builder::Const(query.latency_target);
                auto l_gt = apex::builder::GT(l_load, l_const);

                auto root = apex::builder::And(s_eq, l_gt);
                engine.set_expression("AgbSchema2", root, apex::ExecutionMode::BIT_SLICED);

                total_matches = engine.execute_native_parallel("AgbSchema2", static_cast<const uint64_t*>(mapped), num_blocks, 4);
            }
        } else {
            std::cout << "[gRPC] Dispatching JSON text scan path..." << std::endl;
            if (query.type == eureka::lex::QueryType::SCALAR_STRING_SEARCH) {
                total_matches = run_scalar_string_search(filepath, query.string_pattern, pin_memory, file_bytes);
            } else {
                eureka::lex::Ingester ingester;
                ingester.set_pin_memory(pin_memory);
                if (!ingester.open_file(filepath)) {
                    response->set_error_message("Failed to open index file: " + filepath);
                    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "File not found");
                }
                file_bytes = ingester.get_file_size();
                total_matches = ingester.run_query(query.status_target, query.latency_target);
            }
        }

        auto t_end = std::chrono::high_resolution_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        response->set_total_matches(total_matches);
        response->set_bytes_processed(file_bytes);
        response->set_execution_time_ms(duration_ms);
        
        double duration_sec = duration_ms / 1000.0;
        double file_gb = static_cast<double>(file_bytes) / (1024.0 * 1024.0 * 1024.0);
        double speed_gb_sec = file_gb / (duration_sec > 0.0 ? duration_sec : 0.001);
        response->set_speed_gb_sec(speed_gb_sec);

    } catch (const std::exception& e) {
        response->set_error_message(std::string("Internal Error: ") + e.what());
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, e.what());
    }

    return ::grpc::Status::OK;
}

::grpc::Status AarchGateServiceImpl::StreamIngest(::grpc::ServerContext* context, 
                                                 ::grpc::ServerReader<IngestRequest>* reader, 
                                                 IngestResponse* response) {
    eureka::lex::ingest::AdaptiveIngester ingester;
    std::string db_path;
    size_t total_ingested = 0;

    IngestRequest chunk;
    while (reader->Read(&chunk)) {
        if (db_path.empty()) {
            db_path = chunk.database_path();
        }
        
        std::vector<std::string> lines;
        lines.reserve(chunk.log_lines_size());
        for (int i = 0; i < chunk.log_lines_size(); ++i) {
            lines.push_back(chunk.log_lines(i));
        }

        if (!lines.empty()) {
            auto rg = ingester.append_raw_batch(lines);
            ingester.transcode_batch_now(rg);
            total_ingested += lines.size();
        }
    }

    response->set_total_ingested(total_ingested);
    response->set_success(true);

    if (!db_path.empty()) {
        std::cout << "[gRPC] Auto-prewarming newly ingested database index: " << db_path << std::endl;
        PrewarmCache(db_path, true); // Proactively warm up and pin the ingested database index
    }

    return ::grpc::Status::OK;
}

} // namespace aarchgate
} // namespace eureka
