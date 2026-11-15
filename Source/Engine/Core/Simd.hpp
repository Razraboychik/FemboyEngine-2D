// FemboyEngine2D - portable float batch primitives.
//
// The engine baseline is C++23. When the build selects the experimental C++26
// mode and the standard library provides <simd>, these helpers use std::simd;
// otherwise they fall back to straight loops that every supported compiler
// auto-vectorises. Call sites use the same API in both modes.
#pragma once

#include <cstddef>

namespace Fby::Simd
{
// True when a vector backend is compiled in. The order is: std::simd when the
// experimental C++26 mode provides it, then the compiler's SSE2/NEON vector
// path (available in C++23), then straight scalar loops.
[[nodiscard]] constexpr bool Native() noexcept
{
#if (defined(FBY_HAS_STD_SIMD) && defined(FBY_CXX_STANDARD) && FBY_CXX_STANDARD >= 26) ||     defined(__SSE2__) || defined(_M_X64) || defined(__ARM_NEON)
    return true;
#else
    return false;
#endif
}

// Largest absolute sample value; returns 0 for an empty range.
[[nodiscard]] float PeakAbs(const float* InData, std::size_t InCount) noexcept;

// Inout += InSource element-wise. The two ranges may not overlap unless equal.
void AddInPlace(float* Inout, const float* InSource, std::size_t InCount) noexcept;

// Inout *= InGain element-wise.
void Scale(float* Inout, float InGain, std::size_t InCount) noexcept;
}
