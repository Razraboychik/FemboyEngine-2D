// FemboyEngine2D - full-band acoustic echo canceller implementation.
#include "Engine/Audio/VoiceAec.hpp"

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{
constexpr float kEpsilon = 1e-9f;
constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kTrackEveryBlocks = 64;
constexpr std::uint32_t kTrackHalfWindow = 96;
constexpr std::uint32_t kTrackWindow = 1024;

[[nodiscard]] std::uint32_t ReverseBits(std::uint32_t InValue) noexcept
{
    std::uint32_t ScopedResult = 0;
    for (std::uint32_t ScopedBit = 0; ScopedBit < 8; ++ScopedBit)
    {
        ScopedResult = (ScopedResult << 1) | (InValue & 1u);
        InValue >>= 1;
    }
    return ScopedResult;
}

// In-place radix-2 FFT. The twiddle is advanced per butterfly, so the only
// trigonometric calls are once per stage.
void Transform(std::array<float, kVoiceAecFftSize>& InRe, std::array<float, kVoiceAecFftSize>& InIm,
               bool InInverse) noexcept
{
    constexpr std::uint32_t kSize = kVoiceAecFftSize;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSize; ++ScopedIndex)
    {
        const std::uint32_t ScopedReversed = ReverseBits(ScopedIndex);
        if (ScopedReversed > ScopedIndex)
        {
            std::swap(InRe[ScopedIndex], InRe[ScopedReversed]);
            std::swap(InIm[ScopedIndex], InIm[ScopedReversed]);
        }
    }

    for (std::uint32_t ScopedLength = 2; ScopedLength <= kSize; ScopedLength <<= 1)
    {
        const double ScopedAngle = 2.0 * static_cast<double>(kPi) / static_cast<double>(ScopedLength) *
                                   (InInverse ? 1.0 : -1.0);
        const float ScopedStepRe = static_cast<float>(std::cos(ScopedAngle));
        const float ScopedStepIm = static_cast<float>(std::sin(ScopedAngle));
        for (std::uint32_t ScopedStart = 0; ScopedStart < kSize; ScopedStart += ScopedLength)
        {
            float ScopedTwiddleRe = 1.0f;
            float ScopedTwiddleIm = 0.0f;
            for (std::uint32_t ScopedOffset = 0; ScopedOffset < ScopedLength / 2; ++ScopedOffset)
            {
                const std::uint32_t ScopedA = ScopedStart + ScopedOffset;
                const std::uint32_t ScopedB = ScopedA + ScopedLength / 2;
                const float ScopedProductRe =
                    InRe[ScopedB] * ScopedTwiddleRe - InIm[ScopedB] * ScopedTwiddleIm;
                const float ScopedProductIm =
                    InRe[ScopedB] * ScopedTwiddleIm + InIm[ScopedB] * ScopedTwiddleRe;
                InRe[ScopedB] = InRe[ScopedA] - ScopedProductRe;
                InIm[ScopedB] = InIm[ScopedA] - ScopedProductIm;
                InRe[ScopedA] += ScopedProductRe;
                InIm[ScopedA] += ScopedProductIm;
                const float ScopedNextRe = ScopedTwiddleRe * ScopedStepRe - ScopedTwiddleIm * ScopedStepIm;
                ScopedTwiddleIm = ScopedTwiddleRe * ScopedStepIm + ScopedTwiddleIm * ScopedStepRe;
                ScopedTwiddleRe = ScopedNextRe;
            }
        }
    }

    if (InInverse)
    {
        constexpr float kScale = 1.0f / static_cast<float>(kVoiceAecFftSize);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSize; ++ScopedIndex)
        {
            InRe[ScopedIndex] *= kScale;
            InIm[ScopedIndex] *= kScale;
        }
    }
}
}

void VoiceAecSettings::Clamp() noexcept
{
    TailMs = std::clamp(TailMs, 8.0f, 160.0f);
    MaxDelayMs = std::clamp(MaxDelayMs, 0.0f, 120.0f);
    StepSize = std::clamp(StepSize, 0.01f, 1.0f);
}

const char* VoiceAecSettings::Issue() const noexcept
{
    VoiceAecSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "AEC settings were clamped into range";
}

void VoiceAec::Configure(const VoiceAecSettings& InSettings, std::uint32_t InSampleRate) noexcept
{
    MSettings = InSettings;
    MSettings.Clamp();
    MSampleRate = InSampleRate == 0 ? 48000 : InSampleRate;
    const std::uint32_t ScopedTail = static_cast<std::uint32_t>(MSettings.TailMs * 0.001f *
                                                                static_cast<float>(MSampleRate));
    MPartitions = std::clamp<std::uint32_t>((ScopedTail + kVoiceAecBlock - 1) / kVoiceAecBlock, 1,
                                            kVoiceAecMaxPartitions);
    const std::uint32_t ScopedDelay = static_cast<std::uint32_t>(MSettings.MaxDelayMs * 0.001f *
                                                                 static_cast<float>(MSampleRate));
    MMaxDelay = std::min(ScopedDelay, kVoiceAecMaxDelaySamples);
    Reset();
}

void VoiceAec::Reset() noexcept
{
    for (std::uint32_t ScopedPart = 0; ScopedPart < kVoiceAecMaxPartitions; ++ScopedPart)
    {
        MWre[ScopedPart].fill(0.0f);
        MWim[ScopedPart].fill(0.0f);
        MXre[ScopedPart].fill(0.0f);
        MXim[ScopedPart].fill(0.0f);
    }
    MPower.fill(0.0f);
    MWritePart = 0;
    MRefPrev.fill(0.0f);
    MRefCur.fill(0.0f);
    MMicBlock.fill(0.0f);
    MResidual.fill(0.0f);
    MBlockFill = 0;
    MResidualRead = kVoiceAecBlock;
    MRe.fill(0.0f);
    MIm.fill(0.0f);
    MEre.fill(0.0f);
    MEim.fill(0.0f);
    MEchoBlock.fill(0.0f);
    MRefRaw.fill(0.0f);
    MMicHistory.fill(0.0f);
    MSampleCount = 0;
    MBlocksSinceTrack = 0;
    MMicPower = 0.0f;
    MErrorPower = 0.0f;
    MEchoPower = 0.0f;
    MErleDb = 0.0f;
    MDoubleTalk = false;
    MDelayLocked = false;
    MDelay = 0;
}

float VoiceAec::Correlate(std::uint32_t InWindow, std::uint32_t InLag) const noexcept
{
    constexpr std::uint32_t kRefMask = kVoiceAecRefRing - 1;
    constexpr std::uint32_t kMicMask = kVoiceAecMicHistory - 1;
    if (InLag + InWindow > MSampleCount)
    {
        return 0.0f;
    }
    double ScopedSum = 0.0;
    const std::uint64_t ScopedStart = MSampleCount - InWindow;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InWindow; ++ScopedIndex)
    {
        const std::uint64_t ScopedTime = ScopedStart + ScopedIndex;
        ScopedSum += static_cast<double>(MMicHistory[ScopedTime & kMicMask]) *
                     static_cast<double>(MRefRaw[(ScopedTime - InLag) & kRefMask]);
    }
    return static_cast<float>(std::fabs(ScopedSum));
}

void VoiceAec::LockDelay() noexcept
{
    float ScopedBest = -1.0f;
    std::uint32_t ScopedBestLag = 0;
    for (std::uint32_t ScopedLag = 0; ScopedLag <= MMaxDelay; ++ScopedLag)
    {
        const float ScopedValue = Correlate(kVoiceAecSearchWindow, ScopedLag);
        if (ScopedValue > ScopedBest)
        {
            ScopedBest = ScopedValue;
            ScopedBestLag = ScopedLag;
        }
    }
    MDelay = ScopedBestLag;
    MDelayLocked = true;
    // The filter starts from a clean slate at the locked delay.
    for (std::uint32_t ScopedPart = 0; ScopedPart < kVoiceAecMaxPartitions; ++ScopedPart)
    {
        MWre[ScopedPart].fill(0.0f);
        MWim[ScopedPart].fill(0.0f);
        MXre[ScopedPart].fill(0.0f);
        MXim[ScopedPart].fill(0.0f);
    }
    MPower.fill(0.0f);
    MRefPrev.fill(0.0f);
    MRefCur.fill(0.0f);
    MBlockFill = 0;
    MResidualRead = kVoiceAecBlock;
    MWritePart = 0;
}

void VoiceAec::TrackDelay() noexcept
{
    const std::uint32_t ScopedLow = MDelay > kTrackHalfWindow ? MDelay - kTrackHalfWindow : 0;
    const std::uint32_t ScopedHigh = std::min(MDelay + kTrackHalfWindow, MMaxDelay);
    float ScopedBest = -1.0f;
    std::uint32_t ScopedBestLag = MDelay;
    for (std::uint32_t ScopedLag = ScopedLow; ScopedLag <= ScopedHigh; ++ScopedLag)
    {
        const float ScopedValue = Correlate(kTrackWindow, ScopedLag);
        if (ScopedValue > ScopedBest)
        {
            ScopedBest = ScopedValue;
            ScopedBestLag = ScopedLag;
        }
    }
    const float ScopedCurrent = Correlate(kTrackWindow, MDelay);
    // Only move the bulk delay when the new peak is clearly better; small
    // wobble would otherwise restart the filter every second.
    if (ScopedBestLag != MDelay && ScopedBest > ScopedCurrent * 1.05f)
    {
        MDelay = ScopedBestLag;
    }
}

void VoiceAec::ProcessBlock() noexcept
{
    // Reference spectrum: [previous block, current block] (overlap-save).
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceAecBlock; ++ScopedIndex)
    {
        MRe[ScopedIndex] = MRefPrev[ScopedIndex];
        MIm[ScopedIndex] = 0.0f;
        MRe[kVoiceAecBlock + ScopedIndex] = MRefCur[ScopedIndex];
        MIm[kVoiceAecBlock + ScopedIndex] = 0.0f;
    }
    Transform(MRe, MIm, false);

    const std::uint32_t ScopedNewPart = MWritePart;
    MXre[ScopedNewPart] = MRe;
    MXim[ScopedNewPart] = MIm;

    // Echo estimate, plus the smoothed reference power per bin.
    for (std::uint32_t ScopedBin = 0; ScopedBin < kVoiceAecFftSize; ++ScopedBin)
    {
        float ScopedPower = kEpsilon;
        for (std::uint32_t ScopedPart = 0; ScopedPart < MPartitions; ++ScopedPart)
        {
            const std::uint32_t ScopedIndex =
                (ScopedNewPart + MPartitions - ScopedPart) % MPartitions;
            ScopedPower += MXre[ScopedIndex][ScopedBin] * MXre[ScopedIndex][ScopedBin] +
                           MXim[ScopedIndex][ScopedBin] * MXim[ScopedIndex][ScopedBin];
        }
        MPower[ScopedBin] = MPower[ScopedBin] * 0.85f + ScopedPower * 0.15f;

        float ScopedEchoRe = 0.0f;
        float ScopedEchoIm = 0.0f;
        for (std::uint32_t ScopedPart = 0; ScopedPart < MPartitions; ++ScopedPart)
        {
            const std::uint32_t ScopedIndex =
                (ScopedNewPart + MPartitions - ScopedPart) % MPartitions;
            ScopedEchoRe += MXre[ScopedIndex][ScopedBin] * MWre[ScopedPart][ScopedBin] -
                            MXim[ScopedIndex][ScopedBin] * MWim[ScopedPart][ScopedBin];
            ScopedEchoIm += MXre[ScopedIndex][ScopedBin] * MWim[ScopedPart][ScopedBin] +
                            MXim[ScopedIndex][ScopedBin] * MWre[ScopedPart][ScopedBin];
        }
        MRe[ScopedBin] = ScopedEchoRe;
        MIm[ScopedBin] = ScopedEchoIm;
    }
    Transform(MRe, MIm, true);

    double ScopedMicPower = 0.0;
    double ScopedEchoPower = 0.0;
    double ScopedErrorPower = 0.0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceAecBlock; ++ScopedIndex)
    {
        const float ScopedEcho = MRe[kVoiceAecBlock + ScopedIndex];
        const float ScopedMic = MMicBlock[ScopedIndex];
        const float ScopedResidual = ScopedMic - ScopedEcho;
        MEchoBlock[ScopedIndex] = ScopedEcho;
        MResidual[ScopedIndex] = ScopedResidual;
        ScopedMicPower += static_cast<double>(ScopedMic) * ScopedMic;
        ScopedEchoPower += static_cast<double>(ScopedEcho) * ScopedEcho;
        ScopedErrorPower += static_cast<double>(ScopedResidual) * ScopedResidual;
    }
    const float ScopedInvBlock = 1.0f / static_cast<float>(kVoiceAecBlock);
    const float ScopedMicBlockPower = static_cast<float>(ScopedMicPower) * ScopedInvBlock;
    const float ScopedEchoBlockPower = static_cast<float>(ScopedEchoPower) * ScopedInvBlock;
    const float ScopedErrorBlockPower = static_cast<float>(ScopedErrorPower) * ScopedInvBlock;

    MMicPower = MMicPower * 0.9f + ScopedMicBlockPower * 0.1f;
    MErrorPower = MErrorPower * 0.9f + ScopedErrorBlockPower * 0.1f;
    MEchoPower = ScopedEchoBlockPower;
    MErleDb = 10.0f * std::log10((MMicPower + kEpsilon) / (MErrorPower + kEpsilon));
    MDoubleTalk = MEchoPower > 1e-7f && ScopedMicBlockPower > 6.0f * MEchoPower;

    // Error spectrum for the weight update: [zeros, residual].
    MEre.fill(0.0f);
    MEim.fill(0.0f);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceAecBlock; ++ScopedIndex)
    {
        MEre[kVoiceAecBlock + ScopedIndex] = MResidual[ScopedIndex];
    }
    Transform(MEre, MEim, false);

    if (!MDoubleTalk)
    {
        for (std::uint32_t ScopedBin = 0; ScopedBin < kVoiceAecFftSize; ++ScopedBin)
        {
            const float ScopedNormalizer = 1.0f / (MPower[ScopedBin] + kEpsilon);
            for (std::uint32_t ScopedPart = 0; ScopedPart < MPartitions; ++ScopedPart)
            {
                const std::uint32_t ScopedIndex =
                    (ScopedNewPart + MPartitions - ScopedPart) % MPartitions;
                // E * conj(X): the NLMS gradient for this partition.
                const float ScopedGradientRe = MEre[ScopedBin] * MXre[ScopedIndex][ScopedBin] +
                                               MEim[ScopedBin] * MXim[ScopedIndex][ScopedBin];
                const float ScopedGradientIm = MEim[ScopedBin] * MXre[ScopedIndex][ScopedBin] -
                                               MEre[ScopedBin] * MXim[ScopedIndex][ScopedBin];
                MWre[ScopedPart][ScopedBin] +=
                    MSettings.StepSize * ScopedGradientRe * ScopedNormalizer;
                MWim[ScopedPart][ScopedBin] +=
                    MSettings.StepSize * ScopedGradientIm * ScopedNormalizer;
            }
        }
    }

    MRefPrev = MRefCur;
    MWritePart = (ScopedNewPart + 1) % MPartitions;
    ++MBlocksSinceTrack;
    if (MBlocksSinceTrack >= kTrackEveryBlocks)
    {
        MBlocksSinceTrack = 0;
        TrackDelay();
    }
}

void VoiceAec::Process(float* InMic, const float* InReference, std::uint32_t InFrameCount) noexcept
{
    if (!MSettings.Enabled || InMic == nullptr || InReference == nullptr || InFrameCount == 0)
    {
        return;
    }
    constexpr std::uint32_t kRefMask = kVoiceAecRefRing - 1;
    constexpr std::uint32_t kMicMask = kVoiceAecMicHistory - 1;

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrameCount; ++ScopedIndex)
    {
        const float ScopedMic = InMic[ScopedIndex];
        MRefRaw[MSampleCount & kRefMask] = InReference[ScopedIndex];
        MMicHistory[MSampleCount & kMicMask] = ScopedMic;
        ++MSampleCount;

        if (!MDelayLocked)
        {
            InMic[ScopedIndex] = ScopedMic;
            // The search needs a full mic window plus the largest delay.
            if (MSampleCount >= static_cast<std::uint64_t>(kVoiceAecSearchWindow) + MMaxDelay)
            {
                LockDelay();
            }
            continue;
        }

        if (MResidualRead < kVoiceAecBlock)
        {
            InMic[ScopedIndex] = MResidual[MResidualRead++];
        }
        else
        {
            InMic[ScopedIndex] = ScopedMic;
        }

        // Delayed reference sample for the current block.
        MRefCur[MBlockFill] = MRefRaw[(MSampleCount - 1 - MDelay) & kRefMask];
        MMicBlock[MBlockFill] = ScopedMic;
        ++MBlockFill;
        if (MBlockFill == kVoiceAecBlock)
        {
            ProcessBlock();
            MBlockFill = 0;
            MResidualRead = 0;
        }
    }
}
}
