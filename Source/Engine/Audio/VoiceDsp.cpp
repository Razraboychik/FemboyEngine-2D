// FemboyEngine2D - microphone processing chain implementation.
#include "Engine/Audio/VoiceDsp.hpp"

#include <algorithm>
#include <cmath>

#if defined(FBY_HAS_RNNOISE)
#include "rnnoise.h"
#endif

namespace Fby
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kEpsilon = 1e-9f;

[[nodiscard]] float DbToLinear(float InDb) noexcept
{
    return std::pow(10.0f, InDb * 0.05f);
}

[[nodiscard]] float LinearToDb(float InLinear) noexcept
{
    return 20.0f * std::log10(std::max(InLinear, kEpsilon));
}

[[nodiscard]] float SmoothingCoefficient(float InMilliseconds, std::uint32_t InSampleRate) noexcept
{
    if (InMilliseconds <= 0.0f)
    {
        return 1.0f;
    }
    const float ScopedSamples = InMilliseconds * 0.001f * static_cast<float>(InSampleRate);
    return 1.0f - std::exp(-1.0f / std::max(ScopedSamples, 1.0f));
}
}

bool VoiceRnnoiseAvailable() noexcept
{
#if defined(FBY_HAS_RNNOISE)
    return true;
#else
    return false;
#endif
}

VoiceDspChain::~VoiceDspChain()
{
#if defined(FBY_HAS_RNNOISE)
    if (MRnnoiseState != nullptr)
    {
        rnnoise_destroy(static_cast<DenoiseState*>(MRnnoiseState));
    }
#endif
    MRnnoiseState = nullptr;
}

void VoiceDspSettings::Clamp() noexcept
{
    if (Denoise != VoiceDenoiseTier::None && Denoise != VoiceDenoiseTier::Gate && Denoise != VoiceDenoiseTier::Spectral &&
        Denoise != VoiceDenoiseTier::Rnnoise)
    {
        Denoise = VoiceDenoiseTier::Spectral;
    }
    GateThresholdDb = std::clamp(GateThresholdDb, -90.0f, 0.0f);
    GateAttackMs = std::clamp(GateAttackMs, 0.1f, 100.0f);
    GateReleaseMs = std::clamp(GateReleaseMs, 5.0f, 2000.0f);
    GateHoldMs = std::clamp(GateHoldMs, 0.0f, 2000.0f);
    DenoiseStrength = std::clamp(DenoiseStrength, 0.0f, 2.0f);
    DenoiseFloorDb = std::clamp(DenoiseFloorDb, -60.0f, 0.0f);
    AgcTargetDbfs = std::clamp(AgcTargetDbfs, -40.0f, -3.0f);
    AgcMaxGainDb = std::clamp(AgcMaxGainDb, 0.0f, 48.0f);
    AgcAttackMs = std::clamp(AgcAttackMs, 0.5f, 200.0f);
    AgcReleaseMs = std::clamp(AgcReleaseMs, 10.0f, 2000.0f);
    HighPassHz = std::clamp(HighPassHz, 20.0f, 400.0f);
    LimiterCeilingDb = std::clamp(LimiterCeilingDb, -12.0f, 0.0f);
}

const char* VoiceDspSettings::Issue() const noexcept
{
    VoiceDspSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    if (!(ScopedCopy == *this))
    {
        return "voice processing settings were clamped into range";
    }
    return nullptr;
}

VoiceDspSettings VoicePresetSettings(VoicePresetKind InPreset) noexcept
{
    VoiceDspSettings ScopedSettings{};
    switch (InPreset)
    {
        case VoicePresetKind::Headset:
            ScopedSettings.Denoise = VoiceDenoiseTier::Spectral;
            ScopedSettings.GateThresholdDb = -50.0f;
            ScopedSettings.DenoiseStrength = 0.8f;
            ScopedSettings.AgcEnabled = true;
            ScopedSettings.AgcTargetDbfs = -18.0f;
            break;
        case VoicePresetKind::LaptopMic:
            ScopedSettings.Denoise = VoiceDenoiseTier::Spectral;
            ScopedSettings.GateThresholdDb = -42.0f;
            ScopedSettings.DenoiseStrength = 1.2f;
            ScopedSettings.DenoiseFloorDb = -15.0f;
            ScopedSettings.AgcEnabled = true;
            ScopedSettings.AgcMaxGainDb = 30.0f;
            break;
        case VoicePresetKind::NoisyRoom:
            ScopedSettings.Denoise = VoiceDenoiseTier::Spectral;
            ScopedSettings.GateThresholdDb = -38.0f;
            ScopedSettings.DenoiseStrength = 1.8f;
            ScopedSettings.DenoiseFloorDb = -12.0f;
            ScopedSettings.AgcEnabled = true;
            ScopedSettings.AgcTargetDbfs = -20.0f;
            break;
        case VoicePresetKind::LowBandwidth:
            ScopedSettings.Denoise = VoiceDenoiseTier::Gate;
            ScopedSettings.GateThresholdDb = -40.0f;
            ScopedSettings.AgcEnabled = true;
            ScopedSettings.AgcTargetDbfs = -20.0f;
            ScopedSettings.AgcMaxGainDb = 18.0f;
            break;
        case VoicePresetKind::Count:
            break;
    }
    ScopedSettings.Clamp();
    return ScopedSettings;
}

void VoiceDspChain::Configure(const VoiceDspSettings& InSettings, std::uint32_t InSampleRate) noexcept
{
    MSettings = InSettings;
    MSettings.Clamp();
    MSampleRate = InSampleRate == 0 ? 48000 : InSampleRate;

    // RBJ second-order high-pass at the configured cutoff.
    const float ScopedW0 = 2.0f * kPi * MSettings.HighPassHz / static_cast<float>(MSampleRate);
    const float ScopedCos = std::cos(ScopedW0);
    const float ScopedAlpha = std::sin(ScopedW0) / (2.0f * 0.70710678f);
    const float ScopedA0 = 1.0f + ScopedAlpha;
    MHpB0 = (1.0f + ScopedCos) * 0.5f / ScopedA0;
    MHpB1 = -(1.0f + ScopedCos) / ScopedA0;
    MHpB2 = MHpB0;
    MHpA1 = -2.0f * ScopedCos / ScopedA0;
    MHpA2 = (1.0f - ScopedAlpha) / ScopedA0;

    MGateAttackCoeff = SmoothingCoefficient(MSettings.GateAttackMs, MSampleRate);
    MGateReleaseCoeff = SmoothingCoefficient(MSettings.GateReleaseMs, MSampleRate);
    MGateHoldSeconds = MSettings.GateHoldMs * 0.001f;
    MAgcAttackCoeff = SmoothingCoefficient(MSettings.AgcAttackMs, MSampleRate);
    MAgcReleaseCoeff = SmoothingCoefficient(MSettings.AgcReleaseMs, MSampleRate);
    MLimiterCeiling = DbToLinear(MSettings.LimiterCeilingDb);

#if defined(FBY_HAS_RNNOISE)
    if (MSettings.Denoise == VoiceDenoiseTier::Rnnoise && MRnnoiseState == nullptr)
    {
        MRnnoiseState = rnnoise_create(nullptr);
    }
#endif
    MRnnoiseActive = MSettings.Denoise == VoiceDenoiseTier::Rnnoise && MRnnoiseState != nullptr;

    if (!MTablesReady)
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceDspFrame; ++ScopedIndex)
        {
            MWindow[ScopedIndex] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(ScopedIndex) /
                                                          static_cast<float>(kVoiceDspFrame - 1));
        }
        for (std::uint32_t ScopedK = 0; ScopedK <= kVoiceDspFrame / 2; ++ScopedK)
        {
            for (std::uint32_t ScopedN = 0; ScopedN < kVoiceDspFrame; ++ScopedN)
            {
                const float ScopedAngle = 2.0f * kPi * static_cast<float>(ScopedK * ScopedN) /
                                          static_cast<float>(kVoiceDspFrame);
                MCosTable[ScopedK * kVoiceDspFrame + ScopedN] = std::cos(ScopedAngle);
                MSinTable[ScopedK * kVoiceDspFrame + ScopedN] = std::sin(ScopedAngle);
            }
        }
        MTablesReady = true;
    }
    Reset();
}

void VoiceDspChain::Reset() noexcept
{
    MDcX = 0.0f;
    MDcY = 0.0f;
    MHpX1 = 0.0f;
    MHpX2 = 0.0f;
    MHpY1 = 0.0f;
    MHpY2 = 0.0f;
    MGateEnvelope = 1.0f;
    MGateHold = 0.0f;
    MGateOpen = true;
    MAgcGainDb = 0.0f;
    MLastLevelDb = -120.0f;
    ResetSpectral();
}

void VoiceDspChain::ResetSpectral() noexcept
{
    for (float& ScopedSample : MInHop)
    {
        ScopedSample = 0.0f;
    }
    for (float& ScopedSample : MAnalysis)
    {
        ScopedSample = 0.0f;
    }
    for (float& ScopedSample : MOverlap)
    {
        ScopedSample = 0.0f;
    }
    for (float& ScopedNoise : MNoise)
    {
        ScopedNoise = 0.0f;
    }
    MInHopCount = 0;
    MOutHopRead = kVoiceDspHop;
    MNoiseFrames = 0;
    MRnnoiseInputCount = 0;
    MRnnoiseRead = kVoiceRnnoiseFrame;
    MRnnoiseDry.fill(0.0f);
    MRnnoiseOutput.fill(0.0f);
    MRnnoiseProbability = 0.0f;
}

void VoiceDspChain::UpdateGate(float InSample) noexcept
{
    const float ScopedLevel = LinearToDb(std::fabs(InSample));
    if (ScopedLevel > MSettings.GateThresholdDb)
    {
        MGateHold = MGateHoldSeconds;
        MGateOpen = true;
    }
    else if (MGateHold > 0.0f)
    {
        MGateHold -= 1.0f / static_cast<float>(MSampleRate);
    }
    else
    {
        MGateOpen = false;
    }

    const float ScopedTarget = MGateOpen ? 1.0f : 0.0f;
    const float ScopedCoefficient = ScopedTarget > MGateEnvelope ? MGateAttackCoeff : MGateReleaseCoeff;
    MGateEnvelope += (ScopedTarget - MGateEnvelope) * ScopedCoefficient;
}

void VoiceDspChain::RunSpectralHop() noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceDspHop; ++ScopedIndex)
    {
        MAnalysis[ScopedIndex] = MAnalysis[ScopedIndex + kVoiceDspHop];
        MAnalysis[ScopedIndex + kVoiceDspHop] = MInHop[ScopedIndex];
    }

    float ScopedRe[kVoiceDspFrame / 2 + 1]{};
    float ScopedIm[kVoiceDspFrame / 2 + 1]{};
    for (std::uint32_t ScopedK = 0; ScopedK <= kVoiceDspFrame / 2; ++ScopedK)
    {
        float ScopedReal = 0.0f;
        float ScopedImaginary = 0.0f;
        for (std::uint32_t ScopedN = 0; ScopedN < kVoiceDspFrame; ++ScopedN)
        {
            const float ScopedSample = MAnalysis[ScopedN] * MWindow[ScopedN];
            const float ScopedCos = MCosTable[ScopedK * kVoiceDspFrame + ScopedN];
            const float ScopedSin = MSinTable[ScopedK * kVoiceDspFrame + ScopedN];
            ScopedReal += ScopedSample * ScopedCos;
            ScopedImaginary += ScopedSample * ScopedSin;
        }
        ScopedRe[ScopedK] = ScopedReal;
        ScopedIm[ScopedK] = ScopedImaginary;
    }

    float ScopedGain[kVoiceDspFrame / 2 + 1]{};
    const float ScopedOverSubtraction = 0.5f + MSettings.DenoiseStrength * 1.5f;
    const float ScopedFloor = DbToLinear(MSettings.DenoiseFloorDb);
    for (std::uint32_t ScopedK = 0; ScopedK <= kVoiceDspFrame / 2; ++ScopedK)
    {
        const float ScopedMagnitude = std::sqrt(ScopedRe[ScopedK] * ScopedRe[ScopedK] +
                                                ScopedIm[ScopedK] * ScopedIm[ScopedK]);
        if (MNoiseFrames == 0)
        {
            MNoise[ScopedK] = ScopedMagnitude * 0.1f;
        }
        else if (ScopedMagnitude < MNoise[ScopedK])
        {
            MNoise[ScopedK] = MNoise[ScopedK] * 0.8f + ScopedMagnitude * 0.2f;
        }
        else
        {
            MNoise[ScopedK] *= 1.001f;
        }
        const float ScopedClean = std::max(ScopedMagnitude - ScopedOverSubtraction * MNoise[ScopedK],
                                           ScopedMagnitude * ScopedFloor);
        ScopedGain[ScopedK] = ScopedMagnitude > kEpsilon ? std::clamp(ScopedClean / ScopedMagnitude, 0.0f, 1.0f) : 1.0f;
    }
    ++MNoiseFrames;

    // Smooth the gain curve so isolated bins do not produce musical noise.
    for (std::uint32_t ScopedK = 0; ScopedK <= kVoiceDspFrame / 2; ++ScopedK)
    {
        const float ScopedLeft = ScopedK == 0 ? ScopedGain[0] : ScopedGain[ScopedK - 1];
        const float ScopedRight = ScopedK == kVoiceDspFrame / 2 ? ScopedGain[kVoiceDspFrame / 2]
                                                                : ScopedGain[ScopedK + 1];
        ScopedGain[ScopedK] = 0.25f * ScopedLeft + 0.5f * ScopedGain[ScopedK] + 0.25f * ScopedRight;
    }

    float ScopedSynth[kVoiceDspFrame]{};
    for (std::uint32_t ScopedN = 0; ScopedN < kVoiceDspFrame; ++ScopedN)
    {
        float ScopedSum = ScopedRe[0] * ScopedGain[0];
        for (std::uint32_t ScopedK = 1; ScopedK < kVoiceDspFrame / 2; ++ScopedK)
        {
            const float ScopedCos = MCosTable[ScopedK * kVoiceDspFrame + ScopedN];
            const float ScopedSin = MSinTable[ScopedK * kVoiceDspFrame + ScopedN];
            ScopedSum += 2.0f * (ScopedRe[ScopedK] * ScopedGain[ScopedK] * ScopedCos -
                                 ScopedIm[ScopedK] * ScopedGain[ScopedK] * ScopedSin);
        }
        ScopedSum += ScopedRe[kVoiceDspFrame / 2] * ScopedGain[kVoiceDspFrame / 2] *
                     ((ScopedN & 1u) != 0 ? -1.0f : 1.0f);
        ScopedSynth[ScopedN] = ScopedSum / static_cast<float>(kVoiceDspFrame);
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceDspHop; ++ScopedIndex)
    {
        MOutHop[ScopedIndex] =
            (MOverlap[ScopedIndex] + ScopedSynth[ScopedIndex] * MWindow[ScopedIndex]) / 1.5f;
        MOverlap[ScopedIndex] = ScopedSynth[ScopedIndex + kVoiceDspHop] * MWindow[ScopedIndex + kVoiceDspHop];
    }
}

void VoiceDspChain::Process(float* InSamples, std::uint32_t InFrameCount) noexcept
{
    if (InSamples == nullptr)
    {
        return;
    }
    const bool ScopedRnnoise = MSettings.Denoise == VoiceDenoiseTier::Rnnoise && MRnnoiseActive;
    const bool ScopedSpectral = MSettings.Denoise == VoiceDenoiseTier::Spectral ||
                                (MSettings.Denoise == VoiceDenoiseTier::Rnnoise && !MRnnoiseActive);
    const float ScopedAgcTarget = DbToLinear(MSettings.AgcTargetDbfs);
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
    {
        float ScopedSample = InSamples[ScopedFrame];

        // DC blocker, then the high-pass biquad.
        const float ScopedDcOut = ScopedSample - MDcX + 0.995f * MDcY;
        MDcX = ScopedSample;
        MDcY = ScopedDcOut;
        ScopedSample = ScopedDcOut;
        const float ScopedHp = MHpB0 * ScopedSample + MHpB1 * MHpX1 + MHpB2 * MHpX2 - MHpA1 * MHpY1 -
                               MHpA2 * MHpY2;
        MHpX2 = MHpX1;
        MHpX1 = ScopedSample;
        MHpY2 = MHpY1;
        MHpY1 = ScopedHp;
        ScopedSample = ScopedHp;

        if (MSettings.Denoise != VoiceDenoiseTier::None && !ScopedRnnoise)
        {
            UpdateGate(ScopedSample);
            ScopedSample *= MGateEnvelope;
        }

        if (ScopedRnnoise)
        {
            // Emit the frame that was processed on the previous 480 samples and
            // mix it with its dry counterpart by DenoiseStrength. The first
            // frame has no output yet, so it stays silent (one frame, 10 ms).
            const float ScopedDryInput = ScopedSample;
            if (MRnnoiseRead < kVoiceRnnoiseFrame)
            {
                const float ScopedDry = MRnnoiseDry[MRnnoiseRead];
                const float ScopedWet = MRnnoiseOutput[MRnnoiseRead];
                const float ScopedMix = std::clamp(MSettings.DenoiseStrength, 0.0f, 1.0f);
                ScopedSample = ScopedDry + ScopedMix * (ScopedWet - ScopedDry);
                ++MRnnoiseRead;
            }
            else
            {
                ScopedSample = 0.0f;
            }

            MRnnoiseInput[MRnnoiseInputCount++] = ScopedDryInput;
            if (MRnnoiseInputCount == kVoiceRnnoiseFrame)
            {
                MRnnoiseInputCount = 0;
                MRnnoiseDry = MRnnoiseInput;
#if defined(FBY_HAS_RNNOISE)
                if (MRnnoiseState != nullptr)
                {
                    std::array<float, kVoiceRnnoiseFrame> ScopedScaled{};
                    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceRnnoiseFrame; ++ScopedIndex)
                    {
                        ScopedScaled[ScopedIndex] = MRnnoiseInput[ScopedIndex] * 32768.0f;
                    }
                    MRnnoiseProbability = rnnoise_process_frame(
                        static_cast<DenoiseState*>(MRnnoiseState), MRnnoiseOutput.data(), ScopedScaled.data());
                    MRnnoiseProbability = std::clamp(MRnnoiseProbability, 0.0f, 1.0f);
                    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceRnnoiseFrame; ++ScopedIndex)
                    {
                        MRnnoiseOutput[ScopedIndex] /= 32768.0f;
                    }
                }
                else
#endif
                {
                    MRnnoiseOutput = MRnnoiseInput;
                    MRnnoiseProbability = 0.0f;
                }
                MRnnoiseRead = 0;
            }
        }
        else if (ScopedSpectral)
        {
            MInHop[MInHopCount++] = ScopedSample;
            if (MInHopCount == kVoiceDspHop)
            {
                MInHopCount = 0;
                RunSpectralHop();
                MOutHopRead = 0;
            }
            ScopedSample = MOutHopRead < kVoiceDspHop ? MOutHop[MOutHopRead++] : 0.0f;
        }

        MLastLevelDb = LinearToDb(std::fabs(ScopedSample));
        if (MSettings.AgcEnabled)
        {
            const float ScopedDesiredGainDb = std::clamp(20.0f * std::log10(ScopedAgcTarget / std::max(std::fabs(ScopedSample), kEpsilon)),
                                                         -12.0f, MSettings.AgcMaxGainDb);
            const float ScopedCoefficient = ScopedDesiredGainDb < MAgcGainDb ? MAgcAttackCoeff : MAgcReleaseCoeff;
            MAgcGainDb += (ScopedDesiredGainDb - MAgcGainDb) * ScopedCoefficient;
            ScopedSample *= DbToLinear(MAgcGainDb);
        }

        InSamples[ScopedFrame] = std::clamp(ScopedSample, -MLimiterCeiling, MLimiterCeiling);
    }
}
}
