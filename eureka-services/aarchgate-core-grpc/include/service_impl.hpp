#pragma once

#include <grpcpp/grpcpp.h>
#include "aarchgate.grpc.pb.h"
#include <mutex>
#include <unordered_map>
#include <string>
#include <thread>

namespace eureka {
namespace aarchgate {

class AarchGateServiceImpl final : public AarchGateService::Service {
public:
    ~AarchGateServiceImpl() override;

    void PrewarmCache(const std::string& filepath, bool pin_memory);

    ::grpc::Status ExecuteQuery(::grpc::ServerContext* context, 
                                 const QueryRequest* request, 
                                 QueryResponse* response) override;

    ::grpc::Status StreamIngest(::grpc::ServerContext* context, 
                                 ::grpc::ServerReader<IngestRequest>* reader, 
                                 IngestResponse* response) override;

private:
    struct MappedFile {
        void* addr = nullptr;
        size_t size = 0;
        bool pinned = false;
        bool pin_in_progress = false;
    };

    std::mutex cache_mutex_;
    std::unordered_map<std::string, MappedFile> mapped_files_cache_;
};

} // namespace aarchgate
} // namespace eureka
