// FemboyEngine2D - deterministic procedural voice reaction SFX.
#include "Engine/Audio/VoiceSfxSynth.hpp"

#include "Engine/Core/Pcg32.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Fby
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] float DurationSeconds(VoiceSfxKind InKind) noexcept
{
    switch (InKind)
    {
        case VoiceSfxKind::BubbleSmall:
            return 0.18f;
        case VoiceSfxKind::BubbleBig:
            return 0.30f;
        case VoiceSfxKind::BubbleCluster:
            return 0.70f;
        case VoiceSfxKind::Splash:
            return 0.35f;
        case VoiceSfxKind::ExplosionMuffled:
            return 0.80f;
        case VoiceSfxKind::RadioClick:
            return 0.12f;
        case VoiceSfxKind::None:
        case VoiceSfxKind::Count:
            break;
    }
    return 0.0f;
}

[[nodiscard]] float Envelope(float InTime, float InAttack, float InDecay) noexcept
{
    if (InAttack > 0.0f && InTime < InAttack)
    {
        return InTime / InAttack;
    }
    return std::exp(-(InTime - InAttack) / std::max(InDecay, 1.0e-4f));
}

[[nodiscard]] float Bubble(float InTime, float InStart, float InLowHz, float InHighHz, float InDecay,
                           Pcg32& InRng) noexcept
{
    if (InTime < InStart)
    {
        return 0.0f;
    }
    const float ScopedTime = InTime - InStart;
    const float ScopedDuration = std::max(InDecay * 3.0f, 0.02f);
    if (ScopedTime > ScopedDuration)
    {
        return 0.0f;
    }
    const float ScopedRatio = InHighHz / std::max(InLowHz, 1.0f);
    const float ScopedFrequency = InLowHz * std::pow(ScopedRatio, ScopedTime / ScopedDuration);
    const float ScopedPhase = 2.0f * kPi * ScopedFrequency * ScopedTime;
    const float ScopedEnvelope = Envelope(ScopedTime, 0.006f, InDecay);
    const float ScopedNoise = (InRng.NextFloat() * 2.0f - 1.0f) * 0.08f;
    return (std::sin(ScopedPhase) + ScopedNoise) * ScopedEnvelope;
}

void NormalizePeak(std::span<float> InSamples, float InCeiling) noexcept
{
    float ScopedPeak = 0.0f;
    for (const float ScopedSample : InSamples)
    {
        ScopedPeak = std::max(ScopedPeak, std::fabs(ScopedSample));
    }
    if (ScopedPeak <= InCeiling || ScopedPeak <= 0.0f)
    {
        return;
    }
    const float ScopedScale = InCeiling / ScopedPeak;
    for (float& ScopedSample : InSamples)
    {
        ScopedSample *= ScopedScale;
    }
}
}

std::uint32_t SynthesizeVoiceSfx(VoiceSfxKind InKind, std::uint32_t InSampleRate, std::span<float> Out) noexcept
{
    const float ScopedDuration = DurationSeconds(InKind);
    if (ScopedDuration <= 0.0f || InSampleRate == 0)
    {
        return 0;
    }
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(ScopedDuration * static_cast<float>(InSampleRate) + 0.5f), kVoiceSfxMaxSamples);
    if (ScopedCount == 0 || Out.size() < ScopedCount)
    {
        return 0;
    }
    std::span<float> ScopedOut = Out.first(ScopedCount);
    std::fill(ScopedOut.begin(), ScopedOut.end(), 0.0f);
    Pcg32 ScopedRng = Pcg32::FromSeed(static_cast<std::uint64_t>(InKind) * 0x9E3779B97F4A7C15ull);
    const float ScopedRate = static_cast<float>(InSampleRate);

    switch (InKind)
    {
        case VoiceSfxKind::BubbleSmall:
        case VoiceSfxKind::BubbleBig:
        {
            const float ScopedLow = InKind == VoiceSfxKind::BubbleSmall ? 300.0f : 120.0f;
            const float ScopedHigh = InKind == VoiceSfxKind::BubbleSmall ? 900.0f : 400.0f;
            const float ScopedDecay = InKind == VoiceSfxKind::BubbleSmall ? 0.05f : 0.10f;
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
            {
                const float ScopedTime = static_cast<float>(ScopedIndex) / ScopedRate;
                ScopedOut[ScopedIndex] = Bubble(ScopedTime, 0.0f, ScopedLow, ScopedHigh, ScopedDecay, ScopedRng);
            }
            break;
        }
        case VoiceSfxKind::BubbleCluster:
        {
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
            {
                const float ScopedTime = static_cast<float>(ScopedIndex) / ScopedRate;
                float ScopedSample = Bubble(ScopedTime, 0.0f, 280.0f, 820.0f, 0.05f, ScopedRng);
                ScopedSample += Bubble(ScopedTime, 0.11f, 180.0f, 520.0f, 0.08f, ScopedRng);
                ScopedSample += Bubble(ScopedTime, 0.25f, 360.0f, 1100.0f, 0.04f, ScopedRng);
                ScopedSample += Bubble(ScopedTime, 0.42f, 140.0f, 430.0f, 0.10f, ScopedRng);
                ScopedOut[ScopedIndex] = ScopedSample * 0.55f;
            }
            break;
        }
        case VoiceSfxKind::Splash:
        {
            float ScopedLowPass = 0.0f;
            const float ScopedAlpha = 1.0f - std::exp(-2.0f * kPi * 2400.0f / ScopedRate);
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
            {
                const float ScopedTime = static_cast<float>(ScopedIndex) / ScopedRate;
                const float ScopedNoise = ScopedRng.NextFloat() * 2.0f - 1.0f;
                ScopedLowPass += ScopedAlpha * (ScopedNoise - ScopedLowPass);
                const float ScopedEnvelope = Envelope(ScopedTime, 0.004f, 0.09f);
                const float ScopedThump =
                    std::sin(2.0f * kPi * 170.0f * ScopedTime) * Envelope(ScopedTime, 0.002f, 0.05f);
                ScopedOut[ScopedIndex] = (ScopedNoise - ScopedLowPass) * ScopedEnvelope + ScopedThump * 0.35f;
            }
            break;
        }
        case VoiceSfxKind::ExplosionMuffled:
        {
            float ScopedLowPass = 0.0f;
            const float ScopedAlpha = 1.0f - std::exp(-2.0f * kPi * 420.0f / ScopedRate);
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
            {
                const float ScopedTime = static_cast<float>(ScopedIndex) / ScopedRate;
                const float ScopedNoise = ScopedRng.NextFloat() * 2.0f - 1.0f;
                ScopedLowPass += ScopedAlpha * (ScopedNoise - ScopedLowPass);
                const float ScopedEnvelope = Envelope(ScopedTime, 0.006f, 0.22f);
                const float ScopedThump =
                    std::sin(2.0f * kPi * 45.0f * ScopedTime) * Envelope(ScopedTime, 0.004f, 0.15f);
                ScopedOut[ScopedIndex] = ScopedLowPass * ScopedEnvelope * 1.4f + ScopedThump * 0.6f;
            }
            break;
        }
        case VoiceSfxKind::RadioClick:
        {
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
            {
                const float ScopedTime = static_cast<float>(ScopedIndex) / ScopedRate;
                const float ScopedNoise = ScopedRng.NextFloat() * 2.0f - 1.0f;
                const float ScopedClick = ScopedTime < 0.005f ? ScopedNoise * 0.9f : 0.0f;
                const float ScopedTail = ScopedNoise * 0.12f * Envelope(ScopedTime, 0.005f, 0.03f);
                ScopedOut[ScopedIndex] = ScopedClick + ScopedTail;
            }
            break;
        }
        case VoiceSfxKind::None:
        case VoiceSfxKind::Count:
            return 0;
    }

    NormalizePeak(ScopedOut, 0.8f);
    return ScopedCount;
}
}
