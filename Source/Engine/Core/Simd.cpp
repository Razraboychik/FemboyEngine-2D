// FemboyEngine2D - portable float batch primitives implementation.
#include "Engine/Core/Simd.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(FBY_HAS_STD_SIMD) && defined(FBY_CXX_STANDARD) && FBY_CXX_STANDARD >= 26
#include <simd>
#define FBY_SIMD_BACKEND_STD 1
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define FBY_SIMD_BACKEND_SSE 1
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#define FBY_SIMD_BACKEND_NEON 1
#else
#define FBY_SIMD_BACKEND_SCALAR 1
#endif

namespace Fby::Simd
{
namespace
{
[[nodiscard]] float ScalarPeakAbs(const float* InData, std::size_t InCount) noexcept
{
    float ScopedPeak = 0.0f;
    for (std::size_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        ScopedPeak = std::max(ScopedPeak, std::fabs(InData[ScopedIndex]));
    }
    return ScopedPeak;
}
}

float PeakAbs(const float* InData, std::size_t InCount) noexcept
{
    if (InData == nullptr || InCount == 0)
    {
        return 0.0f;
    }
#if defined(FBY_SIMD_BACKEND_STD)
    using FloatBatch = std::simd<float>;
    constexpr std::size_t kLanes = FloatBatch::size();
    FloatBatch ScopedPeak(0.0f);
    std::size_t ScopedIndex = 0;
    for (; ScopedIndex + kLanes <= InCount; ScopedIndex += kLanes)
    {
        FloatBatch ScopedBatch;
        ScopedBatch.copy_from(InData + ScopedIndex);
        ScopedPeak = std::max(ScopedPeak, std::abs(ScopedBatch));
    }
    float ScopedResult = 0.0f;
    for (std::size_t ScopedLane = 0; ScopedLane < kLanes; ++ScopedLane)
    {
        ScopedResult = std::max(ScopedResult, ScopedPeak[ScopedLane]);
    }
    return std::max(ScopedResult, ScalarPeakAbs(InData + ScopedIndex, InCount - ScopedIndex));
#elif defined(FBY_SIMD_BACKEND_SSE)
    __m128 ScopedPeak = _mm_setzero_ps();
    const __m128 ScopedAbsMask = _mm_set1_ps(-0.0f);
    std::size_t ScopedIndex = 0;
    for (; ScopedIndex + 4 <= InCount; ScopedIndex += 4)
    {
        const __m128 ScopedBatch = _mm_loadu_ps(InData + ScopedIndex);
        const __m128 ScopedMagnitude = _mm_andnot_ps(ScopedAbsMask, ScopedBatch);
        ScopedPeak = _mm_max_ps(ScopedPeak, ScopedMagnitude);
    }
    alignas(16) float ScopedLanes[4]{};
    _mm_storeu_ps(ScopedLanes, ScopedPeak);
    float ScopedResult = std::max(std::max(ScopedLanes[0], ScopedLanes[1]), std::max(ScopedLanes[2], ScopedLanes[3]));
    return std::max(ScopedResult, ScalarPeakAbs(InData + ScopedIndex, InCount - ScopedIndex));
#elif defined(FBY_SIMD_BACKEND_NEON)
    float32x4_t ScopedPeak = vdupq_n_f32(0.0f);
    std::size_t ScopedIndex = 0;
    for (; ScopedIndex + 4 <= InCount; ScopedIndex += 4)
    {
        const float32x4_t ScopedBatch = vld1q_f32(InData + ScopedIndex);
        ScopedPeak = vmaxq_f32(ScopedPeak, vabsq_f32(ScopedBatch));
    }
    alignas(16) float ScopedLanes[4]{};
    vst1q_f32(ScopedLanes, ScopedPeak);
    float ScopedResult = std::max(std::max(ScopedLanes[0], ScopedLanes[1]), std::max(ScopedLanes[2], ScopedLanes[3]));
    return std::max(ScopedResult, ScalarPeakAbs(InData + ScopedIndex, InCount - ScopedIndex));
#else
    return ScalarPeakAbs(InData, InCount);
#endif
}

void AddInPlace(float* Inout, const float* InSource, std::size_t InCount) noexcept
{
    if (Inout == nullptr || InSource == nullptr || InCount == 0)
    {
        return;
    }
#if defined(FBY_SIMD_BACKEND_STD)
    constexpr std::size_t kLanes = std::simd<float>::size();
    std::size_t ScopedIndex = 0;
    for (; ScopedIndex + kLanes <= InCount; ScopedIndex += kLanes)
    {
        std::simd<float> ScopedDestination;
        std::simd<float> ScopedSource;
        ScopedDestination.copy_from(Inout + ScopedIndex);
        ScopedSource.copy_from(InSource + ScopedIndex);
        ScopedDestination += ScopedSource;
        ScopedDestination.copy_to(Inout + ScopedIndex);
    }
    for (; ScopedIndex < InCount; ++ScopedIndex)
    {
        Inout[ScopedIndex] += InSource[ScopedIndex];
    }
#else
    for (std::size_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        Inout[ScopedIndex] += InSource[ScopedIndex];
    }
#endif
}

void Scale(float* Inout, float InGain, std::size_t InCount) noexcept
{
    if (Inout == nullptr || InCount == 0)
    {
        return;
    }
#if defined(FBY_SIMD_BACKEND_STD)
    constexpr std::size_t kLanes = std::simd<float>::size();
    const std::simd<float> ScopedGain(InGain);
    std::size_t ScopedIndex = 0;
    for (; ScopedIndex + kLanes <= InCount; ScopedIndex += kLanes)
    {
        std::simd<float> ScopedBatch;
        ScopedBatch.copy_from(Inout + ScopedIndex);
        ScopedBatch *= ScopedGain;
        ScopedBatch.copy_to(Inout + ScopedIndex);
    }
    for (; ScopedIndex < InCount; ++ScopedIndex)
    {
        Inout[ScopedIndex] *= InGain;
    }
#else
    for (std::size_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        Inout[ScopedIndex] *= InGain;
    }
#endif
}

}
