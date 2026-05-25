#include "service_impl.hpp"
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/security/server_credentials.h>
#include <iostream>
#include <string>
#include <filesystem>

int main(int argc, char** argv) {
    std::string server_address("127.0.0.1:50052");
    if (argc >= 2) {
        server_address = argv[1];
    }

    eureka::aarchgate::AarchGateServiceImpl service;

    // Scan for and pre-warm any existing .agb files in the working directory on startup
    try {
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".agb") {
                std::string filepath = std::filesystem::absolute(entry.path()).string();
                std::cout << "[gRPC] Pre-warming workspace index on startup: " << filepath << std::endl;
                service.PrewarmCache(filepath, true);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Warning] Failed to scan directory for pre-warming: " << e.what() << std::endl;
    }

    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
    std::cout << "[+] AarchGate gRPC Server listening on " << server_address << std::endl;

    server->Wait();
    return 0;
}
