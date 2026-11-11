// FemboyEngine2D - directional animation player implementation.
#include "Engine/ECS/DirectionalAnimationPlayer.hpp"

#include "Engine/Assets/AnimationClipAsset.hpp"
#include "Engine/Assets/VFS.hpp"

#include <cmath>

namespace Fby::Animation
{

std::int32_t DirectionalAnimationPlayer::FindClip(std::uint32_t InClipId) const noexcept
{
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < MClipCount; ++ScopedIndex)
    {
        if (MClipIds[ScopedIndex] == InClipId)
        {
            return static_cast<std::int32_t>(ScopedIndex);
        }
    }
    return -1;
}

bool DirectionalAnimationPlayer::SetClip(std::uint32_t InClipId, const CompressedClip& InClip) noexcept
{
    if (MClipCount >= kMaxDirectionalPlayerClips || FindClip(InClipId) >= 0)
    {
        return false;
    }
    MClips[MClipCount] = InClip;
    MClipIds[MClipCount] = InClipId;
    ++MClipCount;
    MBindingsValid = false;
    return true;
}

bool DirectionalAnimationPlayer::SetClipFromBytes(std::uint32_t InClipId, std::span<const std::byte> InBytes,
                                                  std::string* OutError)
{
    CompressedClip ScopedClip;
    if (!Asset::LoadClip(InBytes, &ScopedClip, OutError))
    {
        return false;
    }
    return SetClip(InClipId, ScopedClip);
}

bool DirectionalAnimationPlayer::LoadClip(VirtualFileSystem& InVfs, std::uint32_t InClipId, std::string_view InPath,
                                          std::string* OutError)
{
    const auto ScopedBytes = InVfs.Read(InPath);
    if (!ScopedBytes)
    {
        if (OutError != nullptr)
        {
            *OutError = "clip not found: " + std::string(InPath);
        }
        return false;
    }
    return SetClipFromBytes(InClipId, *ScopedBytes, OutError);
}

bool DirectionalAnimationPlayer::SetBlendTable(const DirectionalBlendTable& InTable) noexcept
{
    MTable = InTable;
    MHasTable = InTable.AnchorCount > 0;
    return MHasTable;
}

bool DirectionalAnimationPlayer::BuildBindings(const SkeletonComponent& InSkeleton) noexcept
{
    MBindingsValid = false;
    if (MClipCount == 0)
    {
        return false;
    }
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < MClipCount; ++ScopedIndex)
    {
        if (!BakeAnimationClipBinding(MClips[ScopedIndex], InSkeleton, &MBindings[ScopedIndex]))
        {
            return false;
        }
    }
    MBindingsValid = true;
    return true;
}

void DirectionalAnimationPlayer::SetPhase(float InPhase) noexcept
{
    if (!std::isfinite(InPhase))
    {
        MPhase = 0.0f;
        return;
    }
    MPhase = InPhase - std::floor(InPhase);
}

void DirectionalAnimationPlayer::AdvancePhase(float InDt, float InCycleDuration, bool InIsLooping) noexcept
{
    if (!(InCycleDuration > 0.0f) || !std::isfinite(InCycleDuration))
    {
        return;
    }
    MPhase = AdvanceAnimationPhase(MPhase, InDt, InCycleDuration, InIsLooping);
}

bool DirectionalAnimationPlayer::ApplyPose(SkeletonComponent& InSkeleton, Vector2D InFacing) noexcept
{
    if (!MHasTable || !MBindingsValid || InSkeleton.BoneCount == 0)
    {
        return false;
    }
    EvaluateDirectionalBlend(MTable, InFacing, &MSample);
    AnimationPose ScopedPose;
    ResetAnimationPose(ScopedPose, InSkeleton);
    float ScopedBestWeight = -1.0f;
    MDominantClip = kNoDirectionalClip;
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < MSample.Count; ++ScopedIndex)
    {
        const DirectionalBlendWeight& ScopedWeight = MSample.Weights[ScopedIndex];
        const std::int32_t ScopedClip = FindClip(ScopedWeight.ClipId);
        if (ScopedClip < 0)
        {
            continue;
        }
        AccumulateAnimationClip(MClips[ScopedClip], MBindings[ScopedClip], InSkeleton, MPhase, ScopedWeight.Weight,
                                ScopedPose);
        if (ScopedWeight.Weight > ScopedBestWeight)
        {
            ScopedBestWeight = ScopedWeight.Weight;
            MDominantClip = ScopedWeight.ClipId;
        }
    }
    if (MDominantClip == kNoDirectionalClip || !(ScopedPose.Weight > kPoseWeightEpsilon))
    {
        return false;
    }
    ResolveAnimationPose(ScopedPose, InSkeleton);
    return true;
}

bool DirectionalAnimationPlayer::ApplySprite(SpriteAnimationComponent& InSpriteAnimation) const noexcept
{
    const std::int32_t ScopedClip = FindClip(MDominantClip);
    if (ScopedClip < 0)
    {
        return false;
    }
    const CompressedClip& ScopedData = MClips[ScopedClip];
    for (std::uint16_t ScopedTrack = 0; ScopedTrack < ScopedData.TrackCount && ScopedTrack < kMaxTimelineTracks;
         ++ScopedTrack)
    {
        if (ScopedData.Tracks[ScopedTrack].Kind != TrackKind::SpriteClip)
        {
            continue;
        }
        const Vector2D ScopedValue = EvaluateCompressedTrack(ScopedData, ScopedTrack, MPhase * ScopedData.Duration);
        const std::uint32_t ScopedStart =
            ScopedValue.X > 0.0f ? static_cast<std::uint32_t>(std::lround(ScopedValue.X)) : 0u;
        const std::uint32_t ScopedCount =
            ScopedValue.Y > 0.0f ? static_cast<std::uint32_t>(std::lround(ScopedValue.Y)) : 0u;
        if (ScopedCount == 0)
        {
            return false;
        }
        InSpriteAnimation.StartFrame = ScopedStart;
        InSpriteAnimation.FrameCount = ScopedCount;
        if (InSpriteAnimation.CurrentFrame >= ScopedCount)
        {
            InSpriteAnimation.CurrentFrame = 0;
        }
        return true;
    }
    return false;
}
}
