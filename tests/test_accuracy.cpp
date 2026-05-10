#include "eureka/circuits.hpp"
#include <iostream>
#include <cmath>
#include <array>
#include <vector>
#include <cassert>

int main() {
    std::cout << "[AARCHGATE EUREKA TEST] Starting accuracy verification..." << std::endl;

    // Generate 1000 test points between -pi and pi
    constexpr int NUM_TESTS = 1024; // Power of 2 fits perfectly into blocks of 64
    std::vector<double> test_inputs(NUM_TESTS);
    for (int i = 0; i < NUM_TESTS; ++i) {
        test_inputs[i] = -M_PI + (2.0 * M_PI * i) / (NUM_TESTS - 1);
    }

    double max_sin_error = 0.0;
    double max_cos_error = 0.0;

    int num_blocks = NUM_TESTS / 64;
    for (int b = 0; b < num_blocks; ++b) {
        std::array<int64_t, 64> x_raw;
        for (int i = 0; i < 64; ++i) {
            x_raw[i] = static_cast<int64_t>(test_inputs[b * 64 + i] * 1000000.0);
        }

        // Slice inputs
        eureka::BitSliced X = eureka::Circuits::Slice(x_raw);

        // Run bit-sliced Sin and Cos circuits
        eureka::BitSliced Y_sin = eureka::Circuits::Sin(X);
        eureka::BitSliced Y_cos = eureka::Circuits::Cos(X);

        // Unslice outputs
        std::array<int64_t, 64> sin_results = eureka::Circuits::Unslice(Y_sin);
        std::array<int64_t, 64> cos_results = eureka::Circuits::Unslice(Y_cos);

        // Validate accuracy against STL
        for (int i = 0; i < 64; ++i) {
            double x_val = test_inputs[b * 64 + i];
            double actual_sin = std::sin(x_val);
            double actual_cos = std::cos(x_val);

            double approx_sin = static_cast<double>(sin_results[i]) / 1000000.0;
            double approx_cos = static_cast<double>(cos_results[i]) / 1000000.0;

            double sin_err = std::abs(approx_sin - actual_sin);
            double cos_err = std::abs(approx_cos - actual_cos);

            if (sin_err > max_sin_error) max_sin_error = sin_err;
            if (cos_err > max_cos_error) max_cos_error = cos_err;
        }
    }

    std::cout << "[AARCHGATE EUREKA TEST] Max Sin Error: " << (max_sin_error * 100.0) << "% (Absolute error: " << max_sin_error << ")" << std::endl;
    std::cout << "[AARCHGATE EUREKA TEST] Max Cos Error: " << (max_cos_error * 100.0) << "% (Absolute error: " << max_cos_error << ")" << std::endl;

    // Validate that errors are strictly under the 1% threshold
    const double error_threshold = 0.01; // 1.0%
    if (max_sin_error >= error_threshold) {
        std::cerr << "ERROR: Sin error " << (max_sin_error * 100.0) << "% exceeds the 1.0% limit!" << std::endl;
        return 1;
    }
    if (max_cos_error >= error_threshold) {
        std::cerr << "ERROR: Cos error " << (max_cos_error * 100.0) << "% exceeds the 1.0% limit!" << std::endl;
        return 1;
    }

    std::cout << "[AARCHGATE EUREKA TEST] SUCCESS: All accuracy checks passed (Max error < 1.0%)!" << std::endl;
    return 0;
}
