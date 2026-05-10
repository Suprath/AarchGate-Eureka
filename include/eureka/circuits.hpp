#pragma once

#include <cstdint>
#include <array>
#include <vector>

namespace eureka {

// A bit-sliced variable holds 64 values (one in each bit lane).
// The 64 elements of the array represent the 64 bit planes.
using BitSliced = std::array<uint64_t, 64>;

class Circuits {
public:
    // Basic Bit-Sliced Operations
    static BitSliced Add(const BitSliced& a, const BitSliced& b) noexcept;
    static BitSliced Sub(const BitSliced& a, const BitSliced& b) noexcept;
    static BitSliced MulSigned(const BitSliced& a, const BitSliced& b) noexcept;
    static BitSliced MulConst(const BitSliced& a, int64_t constant) noexcept;
    static BitSliced Shr40(const BitSliced& a) noexcept;
    static BitSliced MulFixed(const BitSliced& a, const BitSliced& b) noexcept;

    // Helper to broadcast a constant value to all 64 lanes
    static BitSliced FromConst(int64_t val) noexcept;
    
    // Helper to slice 64 standard scalar values into BitSliced representation
    static BitSliced Slice(const std::array<int64_t, 64>& values) noexcept;
    
    // Helper to unslice BitSliced representation back to 64 scalar values
    static std::array<int64_t, 64> Unslice(const BitSliced& sliced) noexcept;

    // Transcendental gates using Horner's Method (Inputs and outputs are scaled by 10^6)
    static BitSliced Sin(const BitSliced& x) noexcept;
    static BitSliced Cos(const BitSliced& x) noexcept;
};

} // namespace eureka
