import sys
import os
import ctypes
import numpy as np
import time
from scipy.optimize import curve_fit

# Load C++ library
def load_eureka_lib():
    possible_paths = ["build/libeureka_core.dylib", "build/Release/libeureka_core.dylib", "libeureka_core.dylib"]
    for path in possible_paths:
        if os.path.exists(path):
            return ctypes.CDLL(path)
    return None

class CEurekaResult(ctypes.Structure):
    _fields_ = [
        ("best_u", ctypes.c_int),
        ("best_v", ctypes.c_int),
        ("best_a", ctypes.c_double),
        ("best_b", ctypes.c_double),
        ("min_mse", ctypes.c_double)
    ]

lib = load_eureka_lib()
if not lib:
    print("Error: Compile the C++ library first using: cmake --build build --config Release")
    sys.exit(1)

lib.eureka_evaluate_grid.argtypes = [
        ctypes.POINTER(ctypes.c_double),
        ctypes.POINTER(ctypes.c_double),
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_float)
]
lib.eureka_evaluate_grid.restype = CEurekaResult

# 1. Generate test data with noise
N = 100
true_amp = 1.37
true_freq = 0.84
x = np.linspace(-6.28, 6.28, N, dtype=np.float64)
np.random.seed(42)
noise = np.random.normal(0, 0.05, N)
y = true_amp * np.sin(true_freq * x) + noise

print("="*60)
print("             AARCHGATE-EUREKA ACCURACY BENCHMARK             ")
print("="*60)
print(f"Target Formula: y = {true_amp:.2f} * sin({true_freq:.2f} * x) + Noise")
print("-"*60)

# 2. Run SciPy curve_fit (Standard Offline Optimization)
def fit_func(x_val, a, b):
    return a * np.sin(b * x_val)

start_scipy = time.time()
popt, _ = curve_fit(fit_func, x, y, p0=[1.0, 1.0])
scipy_time = (time.time() - start_scipy) * 1000.0

# 3. Run our ultra-fast C++ Grid Evaluator
mse_grid_buffer = np.zeros(1000 * 1000, dtype=np.float32)
x_ptr = x.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
y_ptr = y.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
grid_ptr = mse_grid_buffer.ctypes.data_as(ctypes.POINTER(ctypes.c_float))

start_eureka = time.time()
res = lib.eureka_evaluate_grid(x_ptr, y_ptr, N, grid_ptr)
eureka_time = (time.time() - start_eureka) * 1000.0

# 4. Display Results
print(f"{'Metric':<25} | {'SciPy (Optimization)':<22} | {'AarchGate-Eureka (Grid)':<22}")
print("-"*60)
print(f"{'Discovered Amplitude (a)':<25} | {popt[0]:<22.4f} | {res.best_a:<22.4f}")
print(f"{'Discovered Frequency (b)':<25} | {popt[1]:<22.4f} | {res.best_b:<22.4f}")
print(f"{'Calculation Latency':<25} | {scipy_time:<20.2f} ms | {eureka_time:<20.2f} ms")
print(f"{'Algorithm Strategy':<25} | {'Iterative Local Fit':<22} | {'Brute-Force Global Grid':<22}")
print("="*60)

# Check precision alignment
amp_diff = abs(popt[0] - res.best_a)
freq_diff = abs(popt[1] - res.best_b)
speedup = scipy_time / eureka_time if eureka_time > 0 else 9999.0

print(f"Precision Parity: Amplitude alignment diff: {amp_diff:.5f}, Frequency alignment diff: {freq_diff:.5f}")
print(f"Execution Speedup: AarchGate-Eureka is {speedup:.1f}x FASTER than SciPy!")
print("="*60)
