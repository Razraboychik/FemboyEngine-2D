// FemboyEngine2D - batch primitives: scalar equivalence and a microbenchmark.
#include "Engine/Core/Simd.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
volatile float GSink = 0.0f;
int GChecks = 0;
int GFailures = 0;

void Check(bool InCondition, const char* InExpression, int InLine)
{
    ++GChecks;
    if (!InCondition)
    {
        ++GFailures;
        std::fprintf(stderr, "FAIL %d: %s\n", InLine, InExpression);
    }
}

#define FBY_CHECK(expression) Check((expression), #expression, __LINE__)

float ScalarPeak(const std::vector<float>& InData)
{
    float ScopedPeak = 0.0f;
    for (const float ScopedValue : InData)
    {
        ScopedPeak = std::max(ScopedPeak, std::fabs(ScopedValue));
    }
    return ScopedPeak;
}

std::vector<float> MakeData(std::size_t InCount, unsigned InSeed)
{
    std::vector<float> ScopedData(InCount);
    unsigned ScopedState = InSeed;
    for (float& ScopedValue : ScopedData)
    {
        ScopedState = ScopedState * 1664525u + 1013904223u;
        ScopedValue = static_cast<float>(static_cast<int>(ScopedState >> 8u) - 0x7FFFFF) / 8388608.0f;
    }
    return ScopedData;
}

bool NearlyEqual(float InLeft, float InRight)
{
    return std::fabs(InLeft - InRight) <= 1.0e-6f;
}

void CheckPrimitives()
{
    for (const std::size_t ScopedCount : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{4},
                                          std::size_t{5}, std::size_t{16}, std::size_t{17}, std::size_t{255},
                                          std::size_t{256}, std::size_t{257}})
    {
        const std::vector<float> ScopedLeft = MakeData(ScopedCount, 1);
        const std::vector<float> ScopedRight = MakeData(ScopedCount, 2);
        FBY_CHECK(NearlyEqual(Fby::Simd::PeakAbs(ScopedLeft.data(), ScopedLeft.size()), ScalarPeak(ScopedLeft)));

        std::vector<float> ScopedSum(ScopedLeft);
        Fby::Simd::AddInPlace(ScopedSum.data(), ScopedRight.data(), ScopedSum.size());
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            FBY_CHECK(NearlyEqual(ScopedSum[ScopedIndex], ScopedLeft[ScopedIndex] + ScopedRight[ScopedIndex]));
        }

        std::vector<float> ScopedScaled(ScopedLeft);
        Fby::Simd::Scale(ScopedScaled.data(), 0.5f, ScopedScaled.size());
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            FBY_CHECK(NearlyEqual(ScopedScaled[ScopedIndex], ScopedLeft[ScopedIndex] * 0.5f));
        }
    }
}

void Benchmark()
{
    constexpr std::size_t kCount = 1u << 20;
    constexpr int kRounds = 64;
    constexpr float kGain = 0.5f;
    const std::vector<float> ScopedLeft = MakeData(kCount, 3);
    const std::vector<float> ScopedRight = MakeData(kCount, 4);

    std::vector<float> ScopedScalarSum = MakeData(kCount, 3);
    const auto ScopedSumStart = std::chrono::steady_clock::now();
    for (int ScopedRound = 0; ScopedRound < kRounds; ++ScopedRound)
    {
        for (std::size_t ScopedIndex = 0; ScopedIndex < kCount; ++ScopedIndex)
        {
            ScopedScalarSum[ScopedIndex] += ScopedRight[ScopedIndex];
        }
    }
    const auto ScopedSumEnd = std::chrono::steady_clock::now();
    std::vector<float> ScopedVectorSum = MakeData(kCount, 3);
    for (int ScopedRound = 0; ScopedRound < kRounds; ++ScopedRound)
    {
        Fby::Simd::AddInPlace(ScopedVectorSum.data(), ScopedRight.data(), kCount);
    }
    const auto ScopedVectorSumEnd = std::chrono::steady_clock::now();

    float ScopedScalarPeak = 0.0f;
    const auto ScopedPeakStart = std::chrono::steady_clock::now();
    for (int ScopedRound = 0; ScopedRound < kRounds; ++ScopedRound)
    {
        ScopedScalarPeak = ScalarPeak(ScopedLeft);
        GSink = ScopedScalarPeak;
    }
    const auto ScopedPeakEnd = std::chrono::steady_clock::now();
    float ScopedVectorPeak = 0.0f;
    for (int ScopedRound = 0; ScopedRound < kRounds; ++ScopedRound)
    {
        ScopedVectorPeak = Fby::Simd::PeakAbs(ScopedLeft.data(), kCount);
        GSink = ScopedVectorPeak;
    }
    const auto ScopedVectorPeakEnd = std::chrono::steady_clock::now();

    std::vector<float> ScopedScalarScale = ScopedLeft;
    const auto ScopedScaleStart = std::chrono::steady_clock::now();
    for (int ScopedRound = 0; ScopedRound < kRounds; ++ScopedRound)
    {
        for (float& ScopedValue : ScopedScalarScale)
        {
            ScopedValue *= kGain;
        }
    }
    const auto ScopedScaleEnd = std::chrono::steady_clock::now();
    std::vector<float> ScopedVectorScale = ScopedLeft;
    for (int ScopedRound = 0; ScopedRound < kRounds; ++ScopedRound)
    {
        Fby::Simd::Scale(ScopedVectorScale.data(), kGain, kCount);
    }
    const auto ScopedVectorScaleEnd = std::chrono::steady_clock::now();

    const auto Ms = [](auto InStart, auto InEnd)
    { return std::chrono::duration<double, std::milli>(InEnd - InStart).count(); };
    const double ScopedScalarSumMs = Ms(ScopedSumStart, ScopedSumEnd);
    const double ScopedVectorSumMs = Ms(ScopedSumEnd, ScopedVectorSumEnd);
    const double ScopedScalarPeakMs = Ms(ScopedPeakStart, ScopedPeakEnd);
    const double ScopedVectorPeakMs = Ms(ScopedPeakEnd, ScopedVectorPeakEnd);
    const double ScopedScalarScaleMs = Ms(ScopedScaleStart, ScopedScaleEnd);
    const double ScopedVectorScaleMs = Ms(ScopedScaleEnd, ScopedVectorScaleEnd);
    std::printf("simd: native=%d add %.2f/%.2f ms (%.2fx) peak %.2f/%.2f ms (%.2fx) scale %.2f/%.2f ms (%.2fx)\n",
                Fby::Simd::Native() ? 1 : 0, ScopedScalarSumMs, ScopedVectorSumMs,
                ScopedVectorSumMs > 0.0 ? ScopedScalarSumMs / ScopedVectorSumMs : 0.0, ScopedScalarPeakMs,
                ScopedVectorPeakMs, ScopedVectorPeakMs > 0.0 ? ScopedScalarPeakMs / ScopedVectorPeakMs : 0.0,
                ScopedScalarScaleMs, ScopedVectorScaleMs,
                ScopedVectorScaleMs > 0.0 ? ScopedScalarScaleMs / ScopedVectorScaleMs : 0.0);
    (void)ScopedScalarPeak;
    (void)ScopedVectorPeak;
    (void)ScopedVectorSum;
    (void)ScopedVectorScale;
}
}

int main()
{
    CheckPrimitives();
    Benchmark();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "simd: %d/%d checks failed\n", GFailures, GChecks);
        return 1;
    }
    std::printf("simd: %d checks passed\n", GChecks);
    return 0;
}
