#include "eureka/evaluator.hpp"
#include <thread>
#include <future>
#include <cmath>
#include <iostream>

namespace eureka {

DiscoveryResult Evaluator::EvaluateGrid(
    const std::vector<double>& data_x,
    const std::vector<double>& data_y,
    float* raw_mse_grid
) noexcept {
    const size_t N = data_x.size();
    if (N == 0 || !raw_mse_grid) return {};

    const int num_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    std::vector<std::future<DiscoveryResult>> futures;
    futures.reserve(num_threads);

    const int rows_per_thread = GRID_SIZE / num_threads;

    // MASTERPIECE OPTIMIZATION: Mathematical Factorization & Precomputation!
    // Since sin(b * x) does not depend on Amplitude (a), we precompute the sin(b_v * x_k) matrix.
    // This reduces the transcendental complexity of the 1,000,000-formula grid from:
    // O(GRID_SIZE * GRID_SIZE * N) down to O(GRID_SIZE * N)!
    // This reduces sine calls from 100,000,000 down to exactly 100,000 per frame!
    std::vector<std::vector<double>> SinMatrix(GRID_SIZE, std::vector<double>(N));
    for (int v = 0; v < GRID_SIZE; ++v) {
        double b_val = 0.0 + 2.0 * v / (GRID_SIZE - 1);
        for (size_t k = 0; k < N; ++k) {
            SinMatrix[v][k] = std::sin(b_val * data_x[k]);
        }
    }

    // Launch parallel threads to compute the remaining cheap multiply-accumulates
    for (int t = 0; t < num_threads; ++t) {
        int start_u = t * rows_per_thread;
        int end_u = (t == num_threads - 1) ? GRID_SIZE : (t + 1) * rows_per_thread;

        futures.push_back(std::async(std::launch::async, [start_u, end_u, &SinMatrix, &data_y, N, raw_mse_grid]() {
            DiscoveryResult thread_best;
            thread_best.min_mse = 1e9;

            for (int u = start_u; u < end_u; ++u) {
                double a_val = -2.0 + 4.0 * u / (GRID_SIZE - 1);

                for (int v = 0; v < GRID_SIZE; ++v) {
                    double b_val = 0.0 + 2.0 * v / (GRID_SIZE - 1);
                    double err_sum = 0.0;

                    // Extremely fast register-level SIMD multiply-accumulate hot path
                    for (size_t k = 0; k < N; ++k) {
                        double pred = a_val * SinMatrix[v][k];
                        double err = pred - data_y[k];
                        err_sum += err * err;
                    }

                    double mse = err_sum / N;
                    raw_mse_grid[u * GRID_SIZE + v] = static_cast<float>(mse);

                    if (mse < thread_best.min_mse) {
                        thread_best.min_mse = mse;
                        thread_best.best_u = u;
                        thread_best.best_v = v;
                        thread_best.best_a = a_val;
                        thread_best.best_b = b_val;
                    }
                }
            }
            return thread_best;
        }));
    }

    // Parallel reduction of results
    DiscoveryResult global_best;
    global_best.min_mse = 1e9;

    for (auto& fut : futures) {
        DiscoveryResult thread_res = fut.get();
        if (thread_res.min_mse < global_best.min_mse) {
            global_best = thread_res;
        }
    }

    return global_best;
}

} // namespace eureka

extern "C" {
    struct CEurekaResult {
        int best_u;
        int best_v;
        double best_a;
        double best_b;
        double min_mse;
    };

    CEurekaResult eureka_evaluate_grid(
        const double* data_x,
        const double* data_y,
        int count,
        float* raw_mse_grid
    ) {
        std::vector<double> x_vec(data_x, data_x + count);
        std::vector<double> y_vec(data_y, data_y + count);

        eureka::DiscoveryResult res = eureka::Evaluator::EvaluateGrid(x_vec, y_vec, raw_mse_grid);

        CEurekaResult out;
        out.best_u = res.best_u;
        out.best_v = res.best_v;
        out.best_a = res.best_a;
        out.best_b = res.best_b;
        out.min_mse = res.min_mse;
        return out;
    }
}
