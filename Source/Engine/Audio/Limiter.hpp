// FemboyEngine2D - allocation-free output limiter.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Fby
{

struct LimiterDesc
{
    // Detector threshold
    float thresholdDb = -1.0f;
    // Hard output ceiling
    float ceilingDb = -0.1f;
    // Gain reduction speed
    float attackMs = 2.0f;
    // Gain recovery speed
    float releaseMs = 180.0f;
};

class AudioLimiter
{
public:
    void Configure(std::uint32_t InSampleRate, const LimiterDesc& InDesc = {}) noexcept
    {
        MSampleRate = InSampleRate == 0 ? 48000 : InSampleRate;
        MDesc = InDesc;
        MDesc.thresholdDb = std::min(MDesc.thresholdDb, MDesc.ceilingDb);
        MThreshold = DbToLinear(MDesc.thresholdDb);
        MCeiling = DbToLinear(MDesc.ceilingDb);
        MAttackCoefficient = TimeCoefficient(MDesc.attackMs, MSampleRate);
        MReleaseCoefficient = TimeCoefficient(MDesc.releaseMs, MSampleRate);
        Reset();
    }

    void Reset() noexcept
    {
        MGain = 1.0f;
        MMinimumGain = 1.0f;
    }

    [[nodiscard]] bool IsConfigured() const noexcept
    {
        return MSampleRate != 0;
    }

    // Interleaved float samples in [-1, 1] by convention; values above the
    void Process(float* InInterleaved, std::uint32_t InFrameCount, std::uint32_t InChannels) noexcept
    {
        if (InInterleaved == nullptr || InFrameCount == 0 || InChannels == 0)
        {
            return;
        }
        const std::uint32_t ScopedStride = InChannels;
        for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
        {
            float ScopedPeak = 0.0f;
            const std::uint32_t ScopedBase = ScopedFrame * ScopedStride;
            for (std::uint32_t ScopedChannel = 0; ScopedChannel < ScopedStride; ++ScopedChannel)
            {
                ScopedPeak = std::max(ScopedPeak, std::fabs(InInterleaved[ScopedBase + ScopedChannel]));
            }

            const float ScopedTarget = ScopedPeak > MThreshold ? std::min(1.0f, MCeiling / ScopedPeak) : 1.0f;
            const float ScopedCoefficient = ScopedTarget < MGain ? MAttackCoefficient : MReleaseCoefficient;
            MGain = ScopedTarget + (MGain - ScopedTarget) * ScopedCoefficient;
            MGain = std::clamp(MGain, 0.0f, 1.0f);
            MMinimumGain = std::min(MMinimumGain, MGain);

            for (std::uint32_t ScopedChannel = 0; ScopedChannel < ScopedStride; ++ScopedChannel)
            {
                float ScopedSample = InInterleaved[ScopedBase + ScopedChannel] * MGain;
                ScopedSample = std::clamp(ScopedSample, -MCeiling, MCeiling);
                InInterleaved[ScopedBase + ScopedChannel] = ScopedSample;
            }
        }
    }

    [[nodiscard]] float Gain() const noexcept
    {
        return MGain;
    }
    [[nodiscard]] float GainReductionDb() const noexcept
    {
        return MMinimumGain <= 0.0f ? -144.0f : 20.0f * std::log10(MMinimumGain);
    }
    void ResetPeakMeter() noexcept
    {
        MMinimumGain = MGain;
    }
    [[nodiscard]] std::uint32_t SampleRate() const noexcept
    {
        return MSampleRate;
    }
    [[nodiscard]] const LimiterDesc& Desc() const noexcept
    {
        return MDesc;
    }

private:
    [[nodiscard]] static float DbToLinear(float InDecibels) noexcept
    {
        return std::pow(10.0f, InDecibels / 20.0f);
    }
    [[nodiscard]] static float TimeCoefficient(float InMilliseconds, std::uint32_t InSampleRate) noexcept
    {
        if (InMilliseconds <= 0.0f)
        {
            return 0.0f;
        }
        const float ScopedSeconds = InMilliseconds * 0.001f;
        return std::exp(-1.0f / (ScopedSeconds * static_cast<float>(InSampleRate)));
    }

    std::uint32_t MSampleRate = 0;
    LimiterDesc MDesc{};
    float MThreshold = 0.89f;
    float MCeiling = 0.988f;
    float MAttackCoefficient = 0.01f;
    float MReleaseCoefficient = 0.999f;
    float MGain = 1.0f;
    float MMinimumGain = 1.0f;
};
}
