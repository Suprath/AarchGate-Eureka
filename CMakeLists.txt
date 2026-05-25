cmake_minimum_required(VERSION 3.18)
project(AarchGateEureka LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 1. Bring in the AarchGate core library from submodule
add_subdirectory(external/AarchGate-ML)

# 2. Define include directories
include_directories(
  include
  external/simdjson
  external/AarchGate-ML/external/AarchGate/include
)

# 3. Compile our custom Lex logic, double-buffered ingester, and standalone simdjson
add_library(eureka_lex STATIC
  src/lex/ingester.cpp
  src/lex/dispatcher.cpp
  src/lex/exporter.cpp
  src/lex/ingest/adaptive_ingester.cpp
  src/lex/ingest/promotion_daemon.cpp
  src/lex/jit/query_planner.cpp
  src/lex/jit/plan_cache.cpp
  external/simdjson/simdjson.cpp
)
target_link_libraries(eureka_lex PRIVATE aarchgate)

# 4. Compile the ag-lex CLI grep-killer binary
add_executable(ag-lex
  bin/ag-lex.cpp
)
target_link_libraries(ag-lex PRIVATE eureka_lex aarchgate)

# 5. Compile the high-performance native memory bus benchmark
add_executable(bench_native_scan
  benchmarks/bench_native_scan.cpp
)
target_link_libraries(bench_native_scan PRIVATE eureka_lex aarchgate)

# 6. Compile the deferred reconstruction (log readability) comparison benchmark
add_executable(bench_reconstruct
  benchmarks/bench_reconstruct.cpp
)
target_link_libraries(bench_reconstruct PRIVATE eureka_lex aarchgate)

add_executable(bench_v2_speeds
  benchmarks/bench_v2_speeds.cpp
)
target_link_libraries(bench_v2_speeds PRIVATE eureka_lex aarchgate)

add_executable(bench_production_p1
  benchmarks/bench_production_p1.cpp
)
target_link_libraries(bench_production_p1 PRIVATE eureka_lex aarchgate)

add_executable(bench_expert_review
  benchmarks/bench_expert_review.cpp
)
target_link_libraries(bench_expert_review PRIVATE eureka_lex aarchgate)

add_executable(bench_query_scaling
  benchmarks/bench_query_scaling.cpp
)
target_link_libraries(bench_query_scaling PRIVATE eureka_lex aarchgate)

# 7. Compile Adaptive Storage verification tests
add_executable(test_adaptive_storage
  tests/test_adaptive_storage.cpp
)
target_link_libraries(test_adaptive_storage PRIVATE eureka_lex aarchgate)

add_executable(test_pruned_jit
  tests/test_pruned_jit.cpp
)
target_link_libraries(test_pruned_jit PRIVATE eureka_lex aarchgate)

add_executable(test_production_fuzzer
  tests/test_production_fuzzer.cpp
)
target_link_libraries(test_production_fuzzer PRIVATE eureka_lex aarchgate)

# 8. Compile the AarchGate gRPC Core Server
find_program(PROTOC_EXEC NAMES protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN_EXEC NAMES grpc_cpp_plugin REQUIRED)

set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/eureka-services/proto")
set(GRPC_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gens")
file(MAKE_DIRECTORY "${GRPC_GEN_DIR}")

set(PROTO_PB_CC "${GRPC_GEN_DIR}/aarchgate.pb.cc")
set(PROTO_PB_H "${GRPC_GEN_DIR}/aarchgate.pb.h")
set(PROTO_GRPC_CC "${GRPC_GEN_DIR}/aarchgate.grpc.pb.cc")
set(PROTO_GRPC_H "${GRPC_GEN_DIR}/aarchgate.grpc.pb.h")

add_custom_command(
  OUTPUT "${PROTO_PB_CC}" "${PROTO_PB_H}" "${PROTO_GRPC_CC}" "${PROTO_GRPC_H}"
  COMMAND "${PROTOC_EXEC}"
  ARGS --proto_path="${PROTO_SRC_DIR}"
       --cpp_out="${GRPC_GEN_DIR}"
       --grpc_out="${GRPC_GEN_DIR}"
       --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN_EXEC}"
       "${PROTO_SRC_DIR}/aarchgate.proto"
  DEPENDS "${PROTO_SRC_DIR}/aarchgate.proto"
)

# Add Homebrew search paths for macOS
if(APPLE)
  include_directories(SYSTEM /opt/homebrew/include)
  link_directories(/opt/homebrew/lib)
endif()

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)

add_executable(aarchgate-core-grpc
  eureka-services/aarchgate-core-grpc/src/main.cpp
  eureka-services/aarchgate-core-grpc/src/service_impl.cpp
  "${PROTO_PB_CC}"
  "${PROTO_GRPC_CC}"
)

target_include_directories(aarchgate-core-grpc PRIVATE
  eureka-services/aarchgate-core-grpc/include
  "${GRPC_GEN_DIR}"
)

target_link_libraries(aarchgate-core-grpc PRIVATE
  eureka_lex
  aarchgate
  gRPC::grpc++
  protobuf::libprotobuf
)

# Enable custom release optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_BUILD_TYPE)
  target_compile_options(eureka_lex PRIVATE -O3 -ffast-math)
  target_compile_options(ag-lex PRIVATE -O3 -ffast-math)
  target_compile_options(bench_native_scan PRIVATE -O3 -ffast-math)
  target_compile_options(bench_reconstruct PRIVATE -O3 -ffast-math)
  target_compile_options(aarchgate-core-grpc PRIVATE -O3 -ffast-math)
endif()
