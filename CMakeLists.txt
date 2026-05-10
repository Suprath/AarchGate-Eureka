cmake_minimum_required(VERSION 3.18)
project(AarchGate_Eureka VERSION 1.0.0 LANGUAGES CXX OBJCXX)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Optimization Flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math -fno-finite-math-only -Wall -Wextra")
if(APPLE)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mcpu=apple-m3")
endif()

# Thread Support
find_package(Threads REQUIRED)

# Add Submodules/Libraries
add_subdirectory(external/AarchGate-ML)

# Add our custom include directories
include_directories(
    include
    external/AarchGate-ML/external/AarchGate/include
    external/AarchGate-ML/external/AarchGate/external/asmjit/src
    external/AarchGate-ML/external/AarchGate/external/highway
)

# Core Eureka Library
add_library(eureka_core SHARED
    src/circuits.cpp
    src/evaluator.cpp
)

target_link_libraries(eureka_core PUBLIC
    aarchgate
    Threads::Threads
)

# Set target properties
set_target_properties(eureka_core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# If Apple, link with Metal Frameworks
if(APPLE)
    target_link_libraries(eureka_core PRIVATE
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
    )
endif()

# Tests
option(EUREKA_BUILD_TESTS "Build Eureka tests" ON)
if(EUREKA_BUILD_TESTS)
    enable_testing()
    add_executable(test_accuracy tests/test_accuracy.cpp)
    target_link_libraries(test_accuracy PRIVATE eureka_core)
    add_test(NAME test_accuracy COMMAND test_accuracy)
endif()
