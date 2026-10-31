// FemboyEngine2D - animation clip compression implementation.
#include "Engine/ECS/AnimationCompression.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fby::Animation
{
namespace
{
constexpr float kTimeTicks = 65535.0f;
constexpr float kValueTicks = 32767.0f;

[[nodiscard]] float AbsMax(float InValue, float InLimit) noexcept
{
    const float ScopedValue = std::fabs(InValue);
    return ScopedValue > InLimit ? ScopedValue : InLimit;
}

[[nodiscard]] std::int16_t QuantizeValue(float InValue, float InScale) noexcept
{
    if (InScale <= 0.0f)
    {
        return 0;
    }
    const float ScopedValue = InValue / InScale * kValueTicks;
    const float ScopedClamped = std::clamp(ScopedValue, -kValueTicks, kValueTicks);
    return static_cast<std::int16_t>(std::lround(ScopedClamped));
}

[[nodiscard]] float DequantizeValue(std::int16_t InValue, float InScale) noexcept
{
    return static_cast<float>(InValue) / kValueTicks * InScale;
}

[[nodiscard]] std::uint16_t QuantizeTime(float InTime, float InDuration) noexcept
{
    if (InDuration <= 0.0f)
    {
        return 0;
    }
    const float ScopedNormalized = InTime / InDuration;
    const float ScopedClamped = std::clamp(ScopedNormalized, 0.0f, 1.0f);
    return static_cast<std::uint16_t>(std::lround(ScopedClamped * kTimeTicks));
}

[[nodiscard]] float DequantizeTime(std::uint16_t InTime, float InDuration) noexcept
{
    return static_cast<float>(InTime) / kTimeTicks * InDuration;
}

[[nodiscard]] bool KeepKey(const TimelineKey& InPrevious, const TimelineKey& InMiddle, const TimelineKey& InNext,
                           float InTolerance) noexcept
{
    const float ScopedSpan = InNext.time - InPrevious.time;
    if (ScopedSpan <= 0.0f)
    {
        return true;
    }
    const float ScopedWeight = (InMiddle.time - InPrevious.time) / ScopedSpan;
    const Vector2D ScopedPredicted =
        InPrevious.value * (1.0f - ScopedWeight) + InNext.value * ScopedWeight;
    const Vector2D ScopedDelta = InMiddle.value - ScopedPredicted;
    const float ScopedError = std::sqrt(ScopedDelta.X * ScopedDelta.X + ScopedDelta.Y * ScopedDelta.Y);
    return ScopedError > InTolerance;
}

[[nodiscard]] float SegmentError(const TimelineKey& InPrevious, const TimelineKey& InMiddle, const TimelineKey& InNext) noexcept
{
    const float ScopedSpan = InNext.time - InPrevious.time;
    if (ScopedSpan <= 0.0f)
    {
        return 0.0f;
    }
    const float ScopedWeight = (InMiddle.time - InPrevious.time) / ScopedSpan;
    const Vector2D ScopedPredicted =
        InPrevious.value * (1.0f - ScopedWeight) + InNext.value * ScopedWeight;
    const Vector2D ScopedDelta = InMiddle.value - ScopedPredicted;
    return std::sqrt(ScopedDelta.X * ScopedDelta.X + ScopedDelta.Y * ScopedDelta.Y);
}

[[nodiscard]] Vector2D EvaluateLinear(const CompressedKey& InFrom, const CompressedKey& InTo, float InTime,
                                      float InDuration, float InScaleX, float InScaleY) noexcept
{
    const float ScopedFromTime = DequantizeTime(InFrom.Time, InDuration);
    const float ScopedToTime = DequantizeTime(InTo.Time, InDuration);
    const Vector2D ScopedFromValue{DequantizeValue(InFrom.ValueX, InScaleX), DequantizeValue(InFrom.ValueY, InScaleY)};
    const Vector2D ScopedToValue{DequantizeValue(InTo.ValueX, InScaleX), DequantizeValue(InTo.ValueY, InScaleY)};
    const Interpolation ScopedMode = static_cast<Interpolation>(InFrom.Interpolation);
    if (ScopedMode == Interpolation::Step || ScopedToTime <= ScopedFromTime)
    {
        return ScopedFromValue;
    }
    const float ScopedWeight = std::clamp((InTime - ScopedFromTime) / (ScopedToTime - ScopedFromTime), 0.0f, 1.0f);
    return ScopedFromValue * (1.0f - ScopedWeight) + ScopedToValue * ScopedWeight;
}
}

bool CompressTimeline(const AnimationTimeline& InTimeline, float InTolerance, CompressedClip* OutClip,
                      CompressionReport* OutReport) noexcept
{
    if (OutClip == nullptr || InTimeline.duration <= 0.0f)
    {
        return false;
    }
    *OutClip = CompressedClip{};
    std::memcpy(OutClip->Name, InTimeline.name, sizeof(OutClip->Name));
    OutClip->Duration = InTimeline.duration;
    OutClip->Fps = InTimeline.fps;
    OutClip->Flags = InTimeline.flags;
    const float ScopedTolerance = InTolerance > 0.0f ? InTolerance : 0.0f;

    CompressionReport ScopedReport;
    ScopedReport.SourceKeys = InTimeline.keyCount;
    std::uint32_t ScopedMaxError = 0;

    const std::uint32_t ScopedTracks = std::min<std::uint32_t>(InTimeline.trackCount, kMaxTimelineTracks);
    for (std::uint32_t ScopedTrackIndex = 0; ScopedTrackIndex < ScopedTracks; ++ScopedTrackIndex)
    {
        const TimelineTrack& ScopedSource = InTimeline.tracks[ScopedTrackIndex];
        CompressedTrack ScopedTrack;
        ScopedTrack.Kind = ScopedSource.kind;
        ScopedTrack.BoneIndex = ScopedSource.boneIndex;
        ScopedTrack.Flags = ScopedSource.flags;
        ScopedTrack.BoneHash = ScopedSource.boneHash;
        ScopedTrack.Color = ScopedSource.color;

        const std::uint32_t ScopedFirst = std::min<std::uint32_t>(ScopedSource.firstKey, InTimeline.keyCount);
        const std::uint32_t ScopedAvailable = InTimeline.keyCount - ScopedFirst;
        const std::uint32_t ScopedCount = std::min<std::uint32_t>(ScopedSource.keyCount, ScopedAvailable);
        if (ScopedCount == 0)
        {
            OutClip->Tracks[OutClip->TrackCount++] = ScopedTrack;
            continue;
        }

        float ScopedScaleX = 0.0f;
        float ScopedScaleY = 0.0f;
        for (std::uint32_t ScopedKey = 0; ScopedKey < ScopedCount; ++ScopedKey)
        {
            const TimelineKey& ScopedValue = InTimeline.keys[ScopedFirst + ScopedKey];
            ScopedScaleX = AbsMax(ScopedValue.value.X, ScopedScaleX);
            ScopedScaleY = AbsMax(ScopedValue.value.Y, ScopedScaleY);
        }
        ScopedScaleX = AbsMax(1.0f, ScopedScaleX);
        ScopedScaleY = AbsMax(1.0f, ScopedScaleY);
        ScopedTrack.ScaleX = ScopedScaleX;
        ScopedTrack.ScaleY = ScopedScaleY;

        std::array<std::uint16_t, kMaxCompressedKeys> ScopedKept{};
        std::uint32_t ScopedKeptCount = 0;
        ScopedKept[ScopedKeptCount++] = 0;
        std::uint32_t ScopedLastKept = 0;
        for (std::uint32_t ScopedKey = 1; ScopedKey + 1 < ScopedCount; ++ScopedKey)
        {
            const TimelineKey& ScopedPrevious = InTimeline.keys[ScopedFirst + ScopedLastKept];
            const TimelineKey& ScopedMiddle = InTimeline.keys[ScopedFirst + ScopedKey];
            const TimelineKey& ScopedNext = InTimeline.keys[ScopedFirst + ScopedKey + 1];
            const float ScopedError = SegmentError(ScopedPrevious, ScopedMiddle, ScopedNext);
            const std::uint32_t ScopedErrorMilli = static_cast<std::uint32_t>(ScopedError * 1000.0f);
            ScopedMaxError = std::max(ScopedMaxError, ScopedErrorMilli);
            if (KeepKey(ScopedPrevious, ScopedMiddle, ScopedNext, ScopedTolerance))
            {
                if (ScopedKeptCount < kMaxCompressedKeys)
                {
                    ScopedKept[ScopedKeptCount++] = static_cast<std::uint16_t>(ScopedKey);
                }
                ScopedLastKept = ScopedKey;
            }
        }
        if (ScopedCount > 1)
        {
            if (ScopedKeptCount < kMaxCompressedKeys)
            {
                ScopedKept[ScopedKeptCount++] = static_cast<std::uint16_t>(ScopedCount - 1);
            }
        }

        if (OutClip->KeyCount + ScopedKeptCount > kMaxCompressedKeys)
        {
            ScopedReport.Truncated = true;
            ScopedKeptCount = kMaxCompressedKeys > OutClip->KeyCount ? kMaxCompressedKeys - OutClip->KeyCount : 0;
        }
        ScopedTrack.FirstKey = OutClip->KeyCount;
        ScopedTrack.KeyCount = static_cast<std::uint16_t>(ScopedKeptCount);
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedKeptCount; ++ScopedI)
        {
            const TimelineKey& ScopedSourceKey = InTimeline.keys[ScopedFirst + ScopedKept[ScopedI]];
            CompressedKey ScopedKey;
            ScopedKey.Time = QuantizeTime(ScopedSourceKey.time, InTimeline.duration);
            ScopedKey.ValueX = QuantizeValue(ScopedSourceKey.value.X, ScopedScaleX);
            ScopedKey.ValueY = QuantizeValue(ScopedSourceKey.value.Y, ScopedScaleY);
            ScopedKey.Interpolation = static_cast<std::uint8_t>(ScopedSourceKey.interpolation);
            OutClip->Keys[OutClip->KeyCount++] = ScopedKey;
        }
        if (OutClip->TrackCount >= kMaxTimelineTracks)
        {
            ScopedReport.Truncated = true;
            break;
        }
        OutClip->Tracks[OutClip->TrackCount++] = ScopedTrack;
        ScopedReport.KeptKeys += ScopedKeptCount;
    }

    ScopedReport.Tracks = OutClip->TrackCount;
    ScopedReport.MaxErrorX1000 = ScopedMaxError;
    if (OutReport != nullptr)
    {
        *OutReport = ScopedReport;
    }
    return true;
}

bool DecompressTimeline(const CompressedClip& InClip, AnimationTimeline* OutTimeline) noexcept
{
    if (OutTimeline == nullptr || InClip.Duration <= 0.0f)
    {
        return false;
    }
    OutTimeline->Clear();
    std::memcpy(OutTimeline->name, InClip.Name, sizeof(OutTimeline->name));
    OutTimeline->duration = InClip.Duration;
    OutTimeline->fps = InClip.Fps;
    OutTimeline->flags = InClip.Flags;

    const std::uint32_t ScopedTracks = std::min<std::uint32_t>(InClip.TrackCount, kMaxTimelineTracks);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedTracks; ++ScopedIndex)
    {
        const CompressedTrack& ScopedTrack = InClip.Tracks[ScopedIndex];
        const std::uint16_t ScopedHandle =
            OutTimeline->AddTrack(ScopedTrack.Kind, ScopedTrack.BoneHash, ScopedTrack.Color.rgba);
        if (ScopedHandle == 0xFFFFu)
        {
            return false;
        }
        OutTimeline->tracks[ScopedHandle].boneIndex = ScopedTrack.BoneIndex;
        OutTimeline->tracks[ScopedHandle].flags = ScopedTrack.Flags;
        const std::uint32_t ScopedFirst = std::min<std::uint32_t>(ScopedTrack.FirstKey, InClip.KeyCount);
        const std::uint32_t ScopedAvailable = InClip.KeyCount - ScopedFirst;
        const std::uint32_t ScopedCount = std::min<std::uint32_t>(ScopedTrack.KeyCount, ScopedAvailable);
        for (std::uint32_t ScopedKey = 0; ScopedKey < ScopedCount; ++ScopedKey)
        {
            const CompressedKey& ScopedSource = InClip.Keys[ScopedFirst + ScopedKey];
            const Vector2D ScopedValue{DequantizeValue(ScopedSource.ValueX, ScopedTrack.ScaleX),
                                       DequantizeValue(ScopedSource.ValueY, ScopedTrack.ScaleY)};
            const float ScopedTime = DequantizeTime(ScopedSource.Time, InClip.Duration);
            const auto ScopedMode = static_cast<Interpolation>(ScopedSource.Interpolation);
            if (OutTimeline->AddKey(ScopedHandle, ScopedTime, ScopedValue, ScopedMode) == 0xFFFFu)
            {
                return false;
            }
        }
    }
    return true;
}

Vector2D EvaluateCompressedTrack(const CompressedClip& InClip, std::uint16_t InTrack, float InTime,
                                 Vector2D InFallback) noexcept
{
    if (InTrack >= InClip.TrackCount)
    {
        return InFallback;
    }
    const CompressedTrack& ScopedTrack = InClip.Tracks[InTrack];
    if (ScopedTrack.KeyCount == 0)
    {
        return InFallback;
    }
    const std::uint32_t ScopedFirst = std::min<std::uint32_t>(ScopedTrack.FirstKey, InClip.KeyCount);
    const std::uint32_t ScopedAvailable = InClip.KeyCount - ScopedFirst;
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(ScopedTrack.KeyCount, ScopedAvailable);
    if (ScopedCount == 0)
    {
        return InFallback;
    }
    const CompressedKey* ScopedKeys = &InClip.Keys[ScopedFirst];
    if (InTime <= DequantizeTime(ScopedKeys[0].Time, InClip.Duration))
    {
        return Vector2D{DequantizeValue(ScopedKeys[0].ValueX, ScopedTrack.ScaleX),
                        DequantizeValue(ScopedKeys[0].ValueY, ScopedTrack.ScaleY)};
    }
    const CompressedKey& ScopedLast = ScopedKeys[ScopedCount - 1];
    if (InTime >= DequantizeTime(ScopedLast.Time, InClip.Duration))
    {
        return Vector2D{DequantizeValue(ScopedLast.ValueX, ScopedTrack.ScaleX),
                        DequantizeValue(ScopedLast.ValueY, ScopedTrack.ScaleY)};
    }
    std::uint32_t ScopedLow = 0;
    std::uint32_t ScopedHigh = ScopedCount - 1;
    while (ScopedHigh - ScopedLow > 1)
    {
        const std::uint32_t ScopedMid = (ScopedLow + ScopedHigh) / 2;
        if (DequantizeTime(ScopedKeys[ScopedMid].Time, InClip.Duration) <= InTime)
        {
            ScopedLow = ScopedMid;
        }
        else
        {
            ScopedHigh = ScopedMid;
        }
    }
    return EvaluateLinear(ScopedKeys[ScopedLow], ScopedKeys[ScopedLow + 1], InTime, InClip.Duration,
                          ScopedTrack.ScaleX, ScopedTrack.ScaleY);
}
}
