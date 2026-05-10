#pragma once

#include "eureka/circuits.hpp"
#include <vector>
#include <array>
#include <cstdint>
#include <string>

namespace eureka {

struct DiscoveryResult {
    int best_u = 0;       // Grid vertical coordinate (amplitude a)
    int best_v = 0;       // Grid horizontal coordinate (frequency b)
    double best_a = 0.0;  // Actual amplitude
    double best_b = 0.0;  // Actual frequency
    double min_mse = 1e9; // Lowest Mean Squared Error
};

class Evaluator {
public:
    // Grid settings
    static constexpr int GRID_SIZE = 1000;
    
    // Evaluate 1,000,000 formulas in parallel against the dataset (X and Y vectors).
    // Populates raw_mse_grid (a pre-allocated array of 1,000,000 floats).
    // Returns the DiscoveryResult of the brightest pixel (lowest MSE).
    static DiscoveryResult EvaluateGrid(
        const std::vector<double>& data_x,
        const std::vector<double>& data_y,
        float* raw_mse_grid
    ) noexcept;
};

} // namespace eureka
