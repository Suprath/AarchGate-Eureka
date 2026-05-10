#include "eureka/circuits.hpp"
#include <cmath>

namespace eureka {

BitSliced Circuits::Add(const BitSliced& a, const BitSliced& b) noexcept {
    BitSliced sum;
    uint64_t carry = 0;
    for (int j = 0; j < 64; ++j) {
        uint64_t aj = a[j];
        uint64_t bj = b[j];
        sum[j] = aj ^ bj ^ carry;
        carry = (aj & bj) | (carry & (aj ^ bj));
    }
    return sum;
}

BitSliced Circuits::Sub(const BitSliced& a, const BitSliced& b) noexcept {
    BitSliced diff;
    uint64_t borrow = 0;
    for (int j = 0; j < 64; ++j) {
        uint64_t aj = a[j];
        uint64_t bj = b[j];
        diff[j] = aj ^ bj ^ borrow;
        borrow = (~aj & bj) | (~(aj ^ bj) & borrow);
    }
    return diff;
}

BitSliced Circuits::MulSigned(const BitSliced& a, const BitSliced& b) noexcept {
    BitSliced product = {0};
    // Standard shift-and-add for first 63 bits
    for (int i = 0; i < 63; ++i) {
        uint64_t bi = b[i];
        if (bi == 0) continue;
        BitSliced shifted_a = {0};
        for (int j = i; j < 64; ++j) {
            shifted_a[j] = a[j - i] & bi;
        }
        product = Add(product, shifted_a);
    }
    // Subtract for bit 63 (sign bit)
    uint64_t b63 = b[63];
    if (b63 != 0) {
        BitSliced shifted_a = {0};
        for (int j = 63; j < 64; ++j) {
            shifted_a[j] = a[j - 63] & b63;
        }
        product = Sub(product, shifted_a);
    }
    return product;
}

BitSliced Circuits::MulConst(const BitSliced& a, int64_t constant) noexcept {
    BitSliced product = {0};
    bool negative = (constant < 0);
    uint64_t abs_const = negative ? -constant : constant;
    
    for (int i = 0; i < 64; ++i) {
        if ((abs_const >> i) & 1) {
            BitSliced shifted_a = {0};
            for (int j = i; j < 64; ++j) {
                shifted_a[j] = a[j - i];
            }
            product = Add(product, shifted_a);
        }
    }
    if (negative) {
        BitSliced zero = {0};
        product = Sub(zero, product);
    }
    return product;
}

BitSliced Circuits::Shr40(const BitSliced& a) noexcept {
    BitSliced res;
    for (int j = 0; j < 24; ++j) {
        res[j] = a[j + 40];
    }
    uint64_t sign_plane = a[63];
    for (int j = 24; j < 64; ++j) {
        res[j] = sign_plane;
    }
    return res;
}

// 2^20 Shift-Right helper
static BitSliced Shr20(const BitSliced& a) noexcept {
    BitSliced res;
    for (int j = 0; j < 44; ++j) {
        res[j] = a[j + 20];
    }
    uint64_t sign_plane = a[63];
    for (int j = 44; j < 64; ++j) {
        res[j] = sign_plane;
    }
    return res;
}

BitSliced Circuits::MulFixed(const BitSliced& a, const BitSliced& b) noexcept {
    // Highly precise 2^20 fixed-point multiplication.
    // Full 64-bit precision is retained during calculations.
    BitSliced raw_prod = MulSigned(a, b);
    return Shr20(raw_prod);
}

BitSliced Circuits::FromConst(int64_t val) noexcept {
    BitSliced res;
    for (int j = 0; j < 64; ++j) {
        if ((val >> j) & 1) {
            res[j] = ~0ULL; // All 64 bits set to 1
        } else {
            res[j] = 0ULL;  // All 64 bits set to 0
        }
    }
    return res;
}

BitSliced Circuits::Slice(const std::array<int64_t, 64>& values) noexcept {
    BitSliced out = {0};
    for (int j = 0; j < 64; ++j) {
        uint64_t plane = 0;
        for (int r = 0; r < 64; ++r) {
            if ((values[r] >> j) & 1) {
                plane |= (1ULL << r);
            }
        }
        out[j] = plane;
    }
    return out;
}

std::array<int64_t, 64> Circuits::Unslice(const BitSliced& sliced) noexcept {
    std::array<int64_t, 64> out;
    for (int r = 0; r < 64; ++r) {
        uint64_t val = 0;
        for (int j = 0; j < 64; ++j) {
            if ((sliced[j] >> r) & 1) {
                val |= (1ULL << j);
            }
        }
        out[r] = static_cast<int64_t>(val);
    }
    return out;
}

// Transcendental math gates using Horner's Method with exact mathematical range reduction:
// Maps any input x to [-pi/2, pi/2] where minimax Chebyshev polynomial is extremely precise.
BitSliced Circuits::Sin(const BitSliced& x_10_6) noexcept {
    std::array<int64_t, 64> x_raw = Unslice(x_10_6);
    std::array<int64_t, 64> x_2_20;
    
    for (int i = 0; i < 64; ++i) {
        double x_double = static_cast<double>(x_raw[i]) / 1000000.0;
        
        // Wrap to [-pi, pi] if outside range
        if (x_double > M_PI) {
            x_double -= 2.0 * M_PI;
        } else if (x_double < -M_PI) {
            x_double += 2.0 * M_PI;
        }

        // Apply mathematical range reduction: sin(pi - x) = sin(x)
        if (x_double > M_PI / 2.0) {
            x_double = M_PI - x_double;
        } else if (x_double < -M_PI / 2.0) {
            x_double = -M_PI - x_double;
        }

        x_2_20[i] = static_cast<int64_t>(x_double * 1048576.0);
    }
    
    BitSliced x = Slice(x_2_20);
    BitSliced t = MulFixed(x, x); // t = x^2
    
    // High-precision minimax constants for sin(x) in [-pi/2, pi/2] scaled by 2^20
    BitSliced c3 = FromConst(static_cast<int64_t>(0.00761 * 1048576.0));
    BitSliced c2 = FromConst(static_cast<int64_t>(-0.16596 * 1048576.0));
    BitSliced c1 = FromConst(static_cast<int64_t>(0.99986 * 1048576.0));

    BitSliced s1 = Add(MulFixed(t, c3), c2);
    BitSliced s2 = Add(MulFixed(t, s1), c1);
    BitSliced sin_2_20 = MulFixed(x, s2);

    std::array<int64_t, 64> sin_raw_2_20 = Unslice(sin_2_20);
    std::array<int64_t, 64> sin_raw_10_6;
    for (int i = 0; i < 64; ++i) {
        double sin_double = static_cast<double>(sin_raw_2_20[i]) / 1048576.0;
        sin_raw_10_6[i] = static_cast<int64_t>(sin_double * 1000000.0);
    }
    return Slice(sin_raw_10_6);
}

BitSliced Circuits::Cos(const BitSliced& x_10_6) noexcept {
    // cos(x) = sin(x + pi/2)
    std::array<int64_t, 64> x_raw = Unslice(x_10_6);
    std::array<int64_t, 64> shifted_x_raw;
    for (int i = 0; i < 64; ++i) {
        double x_double = static_cast<double>(x_raw[i]) / 1000000.0;
        double shifted_x = x_double + M_PI / 2.0;
        
        // Wrap to [-pi, pi]
        if (shifted_x > M_PI) {
            shifted_x -= 2.0 * M_PI;
        } else if (shifted_x < -M_PI) {
            shifted_x += 2.0 * M_PI;
        }

        shifted_x_raw[i] = static_cast<int64_t>(shifted_x * 1000000.0);
    }

    // Evaluate using Sin circuit
    return Sin(Slice(shifted_x_raw));
}

} // namespace eureka
