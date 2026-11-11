// FemboyEngine2D - runtime clip sampling and N-way pose blending implementation.
#include "Engine/ECS/AnimationClipPose.hpp"

#include <algorithm>
#include <cmath>

namespace Fby::Animation
{
namespace
{

[[nodiscard]] std::uint16_t PoseBoneCount(const SkeletonComponent& InSkeleton, const AnimationPose& InPose) noexcept
{
    std::uint32_t ScopedCount = std::min<std::uint32_t>(InSkeleton.BoneCount, kMaxBones);
    if (InPose.BoneCount != 0)
    {
        ScopedCount = std::min<std::uint32_t>(ScopedCount, InPose.BoneCount);
    }
    return static_cast<std::uint16_t>(ScopedCount);
}

[[nodiscard]] Vector2D RestPosition(const SkeletonComponent& InSkeleton, std::uint16_t InBone) noexcept
{
    return InSkeleton.RestPoseCaptured ? InSkeleton.RestPositions[InBone] : InSkeleton.LocalPositions[InBone];
}

[[nodiscard]] float RestRotation(const SkeletonComponent& InSkeleton, std::uint16_t InBone) noexcept
{
    return InSkeleton.RestPoseCaptured ? InSkeleton.RestRotations[InBone] : InSkeleton.LocalRotations[InBone];
}

[[nodiscard]] float RestScale(const SkeletonComponent& InSkeleton, std::uint16_t InBone) noexcept
{
    return InSkeleton.RestPoseCaptured ? InSkeleton.RestScales[InBone] : InSkeleton.LocalScales[InBone];
}

void AccumulateAtTime(const CompressedClip& InClip, const AnimationClipBinding& InBinding,
                      const SkeletonComponent& InSkeleton, float InTime, float InWeight, AnimationPose& InPose) noexcept
{
    if (!InBinding.IsValid || !(InWeight > 0.0f) || !std::isfinite(InWeight) || !(InClip.Duration > 0.0f))
    {
        return;
    }
    const std::uint16_t ScopedCount = PoseBoneCount(InSkeleton, InPose);
    if (ScopedCount == 0)
    {
        return;
    }
    if (InPose.BoneCount == 0)
    {
        InPose.BoneCount = ScopedCount;
    }

    // Every source contributes the rest pose first. A driven track then
    // replaces its channel by adding the delta from rest, which keeps the
    // accumulator exact for tracks that cover only part of the skeleton.
    std::array<float, kMaxBones> ScopedRestSin{};
    std::array<float, kMaxBones> ScopedRestCos{};
    for (std::uint16_t ScopedBone = 0; ScopedBone < ScopedCount; ++ScopedBone)
    {
        const Vector2D ScopedRestPosition = RestPosition(InSkeleton, ScopedBone);
        const float ScopedRestRotation = RestRotation(InSkeleton, ScopedBone);
        const float ScopedRestScale = RestScale(InSkeleton, ScopedBone);
        ScopedRestSin[ScopedBone] = Sim::Sin(ScopedRestRotation);
        ScopedRestCos[ScopedBone] = Sim::Cos(ScopedRestRotation);
        InPose.Positions[ScopedBone] += ScopedRestPosition * InWeight;
        InPose.Scales[ScopedBone] += ScopedRestScale * InWeight;
        InPose.RotationSin[ScopedBone] += ScopedRestSin[ScopedBone] * InWeight;
        InPose.RotationCos[ScopedBone] += ScopedRestCos[ScopedBone] * InWeight;
    }
    InPose.Weight += InWeight;

    const std::uint16_t ScopedTracks = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(std::min<std::uint32_t>(InBinding.TrackCount, InClip.TrackCount), kMaxTimelineTracks));
    for (std::uint16_t ScopedTrack = 0; ScopedTrack < ScopedTracks; ++ScopedTrack)
    {
        const std::uint16_t ScopedBone = InBinding.TrackBones[ScopedTrack];
        if (ScopedBone >= ScopedCount)
        {
            continue;
        }
        const CompressedTrack& ScopedCompressed = InClip.Tracks[ScopedTrack];
        if (ScopedCompressed.Kind == TrackKind::SpriteClip)
        {
            continue;
        }
        const Vector2D ScopedValue = EvaluateCompressedTrack(InClip, ScopedTrack, InTime);
        switch (ScopedCompressed.Kind)
        {
            case TrackKind::BonePosition:
                InPose.Positions[ScopedBone] += (ScopedValue - RestPosition(InSkeleton, ScopedBone)) * InWeight;
                break;
            case TrackKind::BoneRotation:
            {
                const float ScopedSampleSin = Sim::Sin(ScopedValue.X);
                const float ScopedSampleCos = Sim::Cos(ScopedValue.X);
                InPose.RotationSin[ScopedBone] += (ScopedSampleSin - ScopedRestSin[ScopedBone]) * InWeight;
                InPose.RotationCos[ScopedBone] += (ScopedSampleCos - ScopedRestCos[ScopedBone]) * InWeight;
                break;
            }
            case TrackKind::BoneScale:
                InPose.Scales[ScopedBone] += (ScopedValue.X - RestScale(InSkeleton, ScopedBone)) * InWeight;
                break;
            case TrackKind::SpriteClip:
            case TrackKind::Count:
                break;
        }
    }
}
}

bool BakeAnimationClipBinding(const CompressedClip& InClip, const SkeletonComponent& InSkeleton,
                              AnimationClipBinding* OutBinding) noexcept
{
    if (OutBinding == nullptr)
    {
        return false;
    }
    *OutBinding = AnimationClipBinding{};
    const std::uint16_t ScopedTracks =
        static_cast<std::uint16_t>(std::min<std::uint32_t>(InClip.TrackCount, kMaxTimelineTracks));
    for (std::uint16_t ScopedTrack = 0; ScopedTrack < ScopedTracks; ++ScopedTrack)
    {
        const CompressedTrack& ScopedCompressed = InClip.Tracks[ScopedTrack];
        if (ScopedCompressed.Kind == TrackKind::SpriteClip)
        {
            OutBinding->TrackBones[ScopedTrack] = kNoPoseBone;
            continue;
        }
        if (ScopedCompressed.BoneHash == 0)
        {
            return false;
        }
        const std::int16_t ScopedBone = SkeletonFindBone(InSkeleton, ScopedCompressed.BoneHash);
        if (ScopedBone < 0)
        {
            return false;
        }
        for (std::uint16_t ScopedPrevious = 0; ScopedPrevious < ScopedTrack; ++ScopedPrevious)
        {
            if (InClip.Tracks[ScopedPrevious].Kind == ScopedCompressed.Kind &&
                OutBinding->TrackBones[ScopedPrevious] == static_cast<std::uint16_t>(ScopedBone))
            {
                return false;
            }
        }
        OutBinding->TrackBones[ScopedTrack] = static_cast<std::uint16_t>(ScopedBone);
    }
    OutBinding->TrackCount = ScopedTracks;
    OutBinding->IsValid = true;
    return true;
}

void ResetAnimationPose(AnimationPose& InPose, const SkeletonComponent& InSkeleton) noexcept
{
    InPose = AnimationPose{};
    InPose.BoneCount = static_cast<std::uint16_t>(std::min<std::uint32_t>(InSkeleton.BoneCount, kMaxBones));
}

void AccumulateAnimationClip(const CompressedClip& InClip, const AnimationClipBinding& InBinding,
                             const SkeletonComponent& InSkeleton, float InPhase, float InWeight,
                             AnimationPose& InPose) noexcept
{
    const float ScopedPhase = std::clamp(std::isfinite(InPhase) ? InPhase : 0.0f, 0.0f, 1.0f);
    AccumulateAtTime(InClip, InBinding, InSkeleton, ScopedPhase * InClip.Duration, InWeight, InPose);
}

void AccumulateAnimationClipAtTime(const CompressedClip& InClip, const AnimationClipBinding& InBinding,
                                   const SkeletonComponent& InSkeleton, float InTime, float InWeight,
                                   AnimationPose& InPose) noexcept
{
    const float ScopedTime = std::isfinite(InTime) ? InTime : 0.0f;
    AccumulateAtTime(InClip, InBinding, InSkeleton, ScopedTime, InWeight, InPose);
}

void ResolveAnimationPose(const AnimationPose& InPose, SkeletonComponent& InSkeleton) noexcept
{
    if (!(InPose.Weight > kPoseWeightEpsilon))
    {
        return;
    }
    const std::uint16_t ScopedCount = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(std::min<std::uint32_t>(InPose.BoneCount, InSkeleton.BoneCount), kMaxBones));
    if (ScopedCount == 0)
    {
        return;
    }
    const float ScopedInverseWeight = 1.0f / InPose.Weight;
    for (std::uint16_t ScopedBone = 0; ScopedBone < ScopedCount; ++ScopedBone)
    {
        InSkeleton.LocalPositions[ScopedBone] = InPose.Positions[ScopedBone] * ScopedInverseWeight;
        InSkeleton.LocalScales[ScopedBone] = InPose.Scales[ScopedBone] * ScopedInverseWeight;
        InSkeleton.LocalRotations[ScopedBone] =
            Sim::Atan2(InPose.RotationSin[ScopedBone], InPose.RotationCos[ScopedBone]);
    }
    SkeletalAnimation::Evaluate(InSkeleton);
}

float AdvanceAnimationClipTime(float InTime, float InDt, float InDuration, bool InIsLooping) noexcept
{
    if (!(InDuration > 0.0f) || !std::isfinite(InDuration))
    {
        return 0.0f;
    }
    const float ScopedDt = std::isfinite(InDt) ? InDt : 0.0f;
    float ScopedTime = (std::isfinite(InTime) ? InTime : 0.0f) + ScopedDt;
    if (InIsLooping)
    {
        ScopedTime = std::fmod(ScopedTime, InDuration);
        if (ScopedTime < 0.0f)
        {
            ScopedTime += InDuration;
        }
        return ScopedTime;
    }
    return std::clamp(ScopedTime, 0.0f, InDuration);
}

float AdvanceAnimationPhase(float InPhase, float InDt, float InCycleDuration, bool InIsLooping) noexcept
{
    if (!(InCycleDuration > 0.0f) || !std::isfinite(InCycleDuration))
    {
        return 0.0f;
    }
    const float ScopedDt = std::isfinite(InDt) ? InDt : 0.0f;
    float ScopedPhase = (std::isfinite(InPhase) ? InPhase : 0.0f) + ScopedDt / InCycleDuration;
    if (InIsLooping)
    {
        ScopedPhase -= std::floor(ScopedPhase);
        return ScopedPhase;
    }
    return std::clamp(ScopedPhase, 0.0f, 1.0f);
}
}
