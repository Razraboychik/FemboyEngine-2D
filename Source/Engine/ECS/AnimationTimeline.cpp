// FemboyEngine2D - animation timeline evaluation.
#include "Engine/ECS/AnimationTimeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fby::Animation
{
namespace
{
constexpr std::uint16_t kInvalidIndex = 0xFFFFu;

[[nodiscard]] float Clamp01(float InValue) noexcept
{
    return InValue < 0.0f ? 0.0f : (InValue > 1.0f ? 1.0f : InValue);
}

[[nodiscard]] float CubicBezierChannel(const TimelineKey& InFrom, const TimelineKey& InTo, float InTime,
                                       int InChannel) noexcept
{
    const float ScopedX0 = InFrom.time;
    const float ScopedX3 = InTo.time;
    const float ScopedX1 = ScopedX0 + InFrom.outTangent.X;
    const float ScopedX2 = ScopedX3 + InTo.inTangent.X;
    const float ScopedV0 = InChannel == 0 ? InFrom.value.X : InFrom.value.Y;
    const float ScopedV1 = InChannel == 0 ? InTo.value.X : InTo.value.Y;
    const float ScopedY0 = ScopedV0;
    const float ScopedY1 = ScopedV0 + (InChannel == 0 ? InFrom.outTangent.Y : InFrom.outTangent.Y);
    const float ScopedY2 = ScopedV1 + InTo.inTangent.Y;
    const float ScopedY3 = ScopedV1;

    // Solve x(u) = time by bisection. Eight iterations on a per-segment basis is
    float ScopedLow = 0.0f;
    float ScopedHigh = 1.0f;
    float ScopedU = 0.0f;
    for (int ScopedIteration = 0; ScopedIteration < 20; ++ScopedIteration)
    {
        ScopedU = 0.5f * (ScopedLow + ScopedHigh);
        const float ScopedInverse = 1.0f - ScopedU;
        const float ScopedX = ScopedInverse * ScopedInverse * ScopedInverse * ScopedX0 +
                              3.0f * ScopedInverse * ScopedInverse * ScopedU * ScopedX1 +
                              3.0f * ScopedInverse * ScopedU * ScopedU * ScopedX2 +
                              ScopedU * ScopedU * ScopedU * ScopedX3;
        if (ScopedX < InTime)
        {
            ScopedLow = ScopedU;
        }
        else
        {
            ScopedHigh = ScopedU;
        }
    }
    const float ScopedInverse = 1.0f - ScopedU;
    return ScopedInverse * ScopedInverse * ScopedInverse * ScopedY0 +
           3.0f * ScopedInverse * ScopedInverse * ScopedU * ScopedY1 +
           3.0f * ScopedInverse * ScopedU * ScopedU * ScopedY2 + ScopedU * ScopedU * ScopedU * ScopedY3;
}

[[nodiscard]] float EvaluateLane(const TimelineKey& InFrom, const TimelineKey& InTo, float InTime,
                                 int InChannel) noexcept
{
    if (InTime <= InFrom.time)
    {
        return InChannel == 0 ? InFrom.value.X : InFrom.value.Y;
    }
    if (InTime >= InTo.time)
    {
        return InChannel == 0 ? InTo.value.X : InTo.value.Y;
    }
    switch (InFrom.interpolation)
    {
        case Interpolation::Step:
            return InChannel == 0 ? InFrom.value.X : InFrom.value.Y;
        case Interpolation::Linear:
        {
            const float ScopedSpan = InTo.time - InFrom.time;
            const float ScopedT = ScopedSpan > 1.0e-6f ? (InTime - InFrom.time) / ScopedSpan : 0.0f;
            const float ScopedA = InChannel == 0 ? InFrom.value.X : InFrom.value.Y;
            const float ScopedB = InChannel == 0 ? InTo.value.X : InTo.value.Y;
            return ScopedA + (ScopedB - ScopedA) * ScopedT;
        }
        case Interpolation::Bezier:
            return CubicBezierChannel(InFrom, InTo, InTime, InChannel);
    }
    return InChannel == 0 ? InFrom.value.X : InFrom.value.Y;
}
}

void AnimationTimeline::Clear() noexcept
{
    std::memset(name, 0, sizeof(name));
    duration = 5.0f;
    fps = 24.0f;
    flags = 0;
    tracks.fill(TimelineTrack{});
    keys.fill(TimelineKey{});
    events.fill(TimelineEvent{});
    trackCount = 0;
    keyCount = 0;
    eventCount = 0;
}

std::uint16_t AnimationTimeline::AddTrack(TrackKind InKind, std::uint64_t InBoneHash,
                                          std::uint32_t InColorRgba) noexcept
{
    if (trackCount >= kMaxTimelineTracks || InKind >= TrackKind::Count)
    {
        return kInvalidIndex;
    }
    TimelineTrack& ScopedTrack = tracks[trackCount];
    ScopedTrack = {};
    ScopedTrack.kind = InKind;
    ScopedTrack.boneHash = InBoneHash;
    ScopedTrack.color = Color32{InColorRgba};
    ScopedTrack.firstKey = static_cast<std::uint16_t>(keyCount);
    return static_cast<std::uint16_t>(trackCount++);
}

std::int16_t AnimationTimeline::FindTrack(TrackKind InKind, std::uint64_t InBoneHash) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < trackCount; ++ScopedI)
    {
        if (tracks[ScopedI].kind == InKind && tracks[ScopedI].boneHash == InBoneHash)
        {
            return static_cast<std::int16_t>(ScopedI);
        }
    }
    return -1;
}

std::int16_t AnimationTimeline::FindTrackByBone(TrackKind InKind, std::int16_t InBoneIndex) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < trackCount; ++ScopedI)
    {
        if (tracks[ScopedI].kind == InKind && tracks[ScopedI].boneIndex == InBoneIndex)
        {
            return static_cast<std::int16_t>(ScopedI);
        }
    }
    return -1;
}

std::uint16_t AnimationTimeline::AddKey(std::uint16_t InTrackIndex, float InTime, Vector2D InValue,
                                        Interpolation InInterpolation) noexcept
{
    if (InTrackIndex >= trackCount || keyCount >= kMaxTimelineKeys)
    {
        return kInvalidIndex;
    }
    TimelineTrack& ScopedTrack = tracks[InTrackIndex];
    TimelineKey ScopedKey;
    ScopedKey.time = InTime;
    ScopedKey.value = InValue;
    ScopedKey.interpolation = InInterpolation;
    // Insert into the track's sorted range, then shift the following tracks.
    std::uint16_t ScopedInsert = ScopedTrack.firstKey;
    const std::uint16_t ScopedEnd = static_cast<std::uint16_t>(ScopedTrack.firstKey + ScopedTrack.keyCount);
    while (ScopedInsert < ScopedEnd && keys[ScopedInsert].time <= InTime)
    {
        ++ScopedInsert;
    }
    if (keyCount > ScopedInsert)
    {
        std::memmove(&keys[ScopedInsert + 1], &keys[ScopedInsert], (keyCount - ScopedInsert) * sizeof(TimelineKey));
    }
    keys[ScopedInsert] = ScopedKey;
    ++keyCount;
    ++ScopedTrack.keyCount;
    for (std::uint32_t ScopedI = 0; ScopedI < trackCount; ++ScopedI)
    {
        if (ScopedI != InTrackIndex && tracks[ScopedI].firstKey >= ScopedInsert)
        {
            ++tracks[ScopedI].firstKey;
        }
    }
    return ScopedInsert;
}

bool AnimationTimeline::RemoveKey(std::uint16_t InTrackIndex, std::uint16_t InKeyIndex) noexcept
{
    if (InTrackIndex >= trackCount)
    {
        return false;
    }
    TimelineTrack& ScopedTrack = tracks[InTrackIndex];
    if (InKeyIndex >= ScopedTrack.keyCount)
    {
        return false;
    }
    const std::uint16_t ScopedAbsolute = static_cast<std::uint16_t>(ScopedTrack.firstKey + InKeyIndex);
    if (ScopedAbsolute >= keyCount)
    {
        return false;
    }
    if (ScopedAbsolute + 1 < keyCount)
    {
        std::memmove(&keys[ScopedAbsolute], &keys[ScopedAbsolute + 1],
                     (keyCount - ScopedAbsolute - 1) * sizeof(TimelineKey));
    }
    --keyCount;
    --ScopedTrack.keyCount;
    for (std::uint32_t ScopedI = 0; ScopedI < trackCount; ++ScopedI)
    {
        if (ScopedI != InTrackIndex && tracks[ScopedI].firstKey > ScopedAbsolute)
        {
            --tracks[ScopedI].firstKey;
        }
    }
    return true;
}

bool AnimationTimeline::SetKeyTangents(std::uint16_t InTrackIndex, std::uint16_t InKeyIndex, Vector2D InInTangent,
                                       Vector2D OutTangent) noexcept
{
    if (InTrackIndex >= trackCount)
    {
        return false;
    }
    const TimelineTrack& ScopedTrack = tracks[InTrackIndex];
    if (InKeyIndex >= ScopedTrack.keyCount)
    {
        return false;
    }
    TimelineKey& ScopedKey = keys[ScopedTrack.firstKey + InKeyIndex];
    ScopedKey.inTangent = InInTangent;
    ScopedKey.outTangent = OutTangent;
    return true;
}

std::uint16_t AnimationTimeline::AddEvent(const TimelineEvent& InEvent) noexcept
{
    if (eventCount >= kMaxTimelineEvents)
    {
        return kInvalidIndex;
    }
    events[eventCount] = InEvent;
    return static_cast<std::uint16_t>(eventCount++);
}

bool AnimationTimeline::RemoveEvent(std::uint16_t InIndex) noexcept
{
    if (InIndex >= eventCount)
    {
        return false;
    }
    if (InIndex + 1 < eventCount)
    {
        std::memmove(&events[InIndex], &events[InIndex + 1], (eventCount - InIndex - 1) * sizeof(TimelineEvent));
    }
    --eventCount;
    return true;
}

Vector2D AnimationTimeline::EvaluateTrack(std::uint16_t InTrackIndex, float InTime, Vector2D InFallback) const noexcept
{
    if (InTrackIndex >= trackCount)
    {
        return InFallback;
    }
    const TimelineTrack& ScopedTrack = tracks[InTrackIndex];
    if (ScopedTrack.keyCount == 0)
    {
        return InFallback;
    }
    const TimelineKey* ScopedFirst = &keys[ScopedTrack.firstKey];
    if (InTime <= ScopedFirst[0].time)
    {
        return ScopedFirst[0].value;
    }
    const TimelineKey* ScopedLast = &keys[ScopedTrack.firstKey + ScopedTrack.keyCount - 1];
    if (InTime >= ScopedLast->time)
    {
        return ScopedLast->value;
    }
    std::uint16_t ScopedLow = 0;
    std::uint16_t ScopedHigh = static_cast<std::uint16_t>(ScopedTrack.keyCount - 1);
    while (ScopedHigh - ScopedLow > 1)
    {
        const std::uint16_t ScopedMid = static_cast<std::uint16_t>((ScopedLow + ScopedHigh) / 2);
        if (keys[ScopedTrack.firstKey + ScopedMid].time <= InTime)
        {
            ScopedLow = ScopedMid;
        }
        else
        {
            ScopedHigh = ScopedMid;
        }
    }
    const TimelineKey& ScopedFrom = keys[ScopedTrack.firstKey + ScopedLow];
    const TimelineKey& ScopedTo = keys[ScopedTrack.firstKey + ScopedLow + 1];
    return Vector2D{EvaluateLane(ScopedFrom, ScopedTo, InTime, 0), EvaluateLane(ScopedFrom, ScopedTo, InTime, 1)};
}

void AnimationTimeline::ApplyPose(SkeletonComponent& InSkeleton, float InTime) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < trackCount; ++ScopedI)
    {
        const TimelineTrack& ScopedTrack = tracks[ScopedI];
        if (ScopedTrack.kind == TrackKind::SpriteClip || ScopedTrack.keyCount == 0)
        {
            continue;
        }
        std::int16_t ScopedBone = ScopedTrack.boneIndex;
        if (ScopedBone < 0 || ScopedBone >= static_cast<std::int16_t>(InSkeleton.BoneCount))
        {
            ScopedBone = SkeletonFindBone(InSkeleton, ScopedTrack.boneHash);
        }
        if (ScopedBone < 0 || ScopedBone >= static_cast<std::int16_t>(InSkeleton.BoneCount))
        {
            continue;
        }
        const Vector2D ScopedValue = EvaluateTrack(static_cast<std::uint16_t>(ScopedI), InTime);
        if (ScopedTrack.kind == TrackKind::BonePosition)
        {
            InSkeleton.LocalPositions[ScopedBone] = ScopedValue;
        }
        else if (ScopedTrack.kind == TrackKind::BoneRotation)
        {
            InSkeleton.LocalRotations[ScopedBone] = ScopedValue.X;
        }
        else if (ScopedTrack.kind == TrackKind::BoneScale)
        {
            InSkeleton.LocalScales[ScopedBone] = ScopedValue.X;
        }
    }
    SkeletalAnimation::Evaluate(InSkeleton);
}

void AnimationTimeline::ApplySpriteClip(SpriteAnimationComponent& InSpriteAnimation, float InTime) const noexcept
{
    const std::int16_t ScopedTrackIndex = FindTrack(TrackKind::SpriteClip, 0);
    if (ScopedTrackIndex < 0)
    {
        return;
    }
    const Vector2D ScopedClip = EvaluateTrack(static_cast<std::uint16_t>(ScopedTrackIndex), InTime, Vector2D{});
    const auto ScopedStart = static_cast<std::uint32_t>(std::max(0.0f, ScopedClip.X));
    const auto ScopedCount = static_cast<std::uint32_t>(std::max(0.0f, ScopedClip.Y));
    if (ScopedCount == 0)
    {
        return;
    }
    InSpriteAnimation.StartFrame = ScopedStart;
    InSpriteAnimation.FrameCount = ScopedCount;
    const float ScopedFrameDuration = InSpriteAnimation.FrameDuration > 0.0f ? InSpriteAnimation.FrameDuration : 0.1f;
    const auto ScopedFrame = static_cast<std::uint32_t>(std::max(0.0f, InTime) / ScopedFrameDuration);
    InSpriteAnimation.CurrentFrame = ScopedStart + ScopedFrame % ScopedCount;
    InSpriteAnimation.ElapsedTime = std::fmod(std::max(0.0f, InTime), ScopedFrameDuration);
}

void AnimationTimeline::DispatchEvents(float InPreviousTime, float InTime, bool InLooped,
                                       const TimelineTriggerSink& InSink) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < eventCount; ++ScopedI)
    {
        const TimelineEvent& ScopedEvent = events[ScopedI];
        bool ScopedFire = false;
        if (!InLooped)
        {
            ScopedFire = ScopedEvent.time > InPreviousTime && ScopedEvent.time <= InTime;
        }
        else if (InTime >= InPreviousTime)
        {
            ScopedFire = ScopedEvent.time > InPreviousTime && ScopedEvent.time <= InTime;
        }
        else
        {
            // Wrapped playhead: fire the tail of the previous loop and the head
            ScopedFire = ScopedEvent.time > InPreviousTime || ScopedEvent.time <= InTime;
        }
        if (!ScopedFire)
        {
            continue;
        }
        if (ScopedEvent.kind == TimelineEventKind::Audio && InSink.playAudio != nullptr)
        {
            InSink.playAudio(InSink.user, ScopedEvent.audioAsset, ScopedEvent.volume);
        }
        else if (ScopedEvent.kind == TimelineEventKind::Script && InSink.runScript != nullptr)
        {
            InSink.runScript(InSink.user, ScopedEvent.scriptProgram, ScopedEvent.entity);
        }
    }
}

bool AnimationTimeline::Validate() const noexcept
{
    if (trackCount > kMaxTimelineTracks || keyCount > kMaxTimelineKeys || eventCount > kMaxTimelineEvents)
    {
        return false;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < trackCount; ++ScopedI)
    {
        const TimelineTrack& ScopedTrack = tracks[ScopedI];
        if (static_cast<std::uint32_t>(ScopedTrack.firstKey) + ScopedTrack.keyCount > keyCount)
        {
            return false;
        }
        for (std::uint32_t ScopedKey = 0; ScopedKey < ScopedTrack.keyCount; ++ScopedKey)
        {
            const TimelineKey& ScopedCurrent = keys[ScopedTrack.firstKey + ScopedKey];
            if (ScopedKey > 0 && keys[ScopedTrack.firstKey + ScopedKey - 1].time > ScopedCurrent.time)
            {
                return false;
            }
        }
    }
    for (std::uint32_t ScopedI = 1; ScopedI < eventCount; ++ScopedI)
    {
        if (events[ScopedI - 1].time > events[ScopedI].time)
        {
            return false;
        }
    }
    return true;
}

float AnimationTimeline::LastKeyTime() const noexcept
{
    float ScopedLatest = 0.0f;
    for (std::uint32_t ScopedI = 0; ScopedI < trackCount; ++ScopedI)
    {
        const TimelineTrack& ScopedTrack = tracks[ScopedI];
        for (std::uint32_t ScopedKey = 0; ScopedKey < ScopedTrack.keyCount; ++ScopedKey)
        {
            ScopedLatest = std::max(ScopedLatest, keys[ScopedTrack.firstKey + ScopedKey].time);
        }
    }
    return ScopedLatest;
}
}
