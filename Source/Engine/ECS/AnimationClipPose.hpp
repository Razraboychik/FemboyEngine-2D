// FemboyEngine2D - runtime clip sampling and N-way pose blending.
#pragma once

#include "Engine/ECS/AnimationCompression.hpp"
#include "Engine/ECS/DirectionalBlend.hpp"
#include "Engine/ECS/Skeleton.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby::Animation
{

// Tracks that carry a sprite range rather than a bone have no pose slot.
inline constexpr std::uint16_t kNoPoseBone = 0xFFFFu;
inline constexpr float kPoseWeightEpsilon = 1.0e-6f;

// Resolves the authored bone hashes of a clip to indices in one skeleton. The
// binding is baked once per (clip, skeleton) pair, so the frame path walks the
// track array instead of hashing or searching every bone.
struct AnimationClipBinding
{
    std::array<std::uint16_t, kMaxTimelineTracks> TrackBones{};
    std::uint16_t TrackCount = 0;
    bool IsValid = false;
};

static_assert(std::is_trivially_copyable_v<AnimationClipBinding>);
static_assert(std::is_standard_layout_v<AnimationClipBinding>);

// Fails on an unknown bone hash, a duplicate (track kind, bone) pair and a
// track count past the fixed capacity.
[[nodiscard]] bool BakeAnimationClipBinding(const CompressedClip& InClip, const SkeletonComponent& InSkeleton,
                                            AnimationClipBinding* OutBinding) noexcept;

// Weighted SoA accumulator. Rotations are stored as weighted sine and cosine,
// because a linear blend of 350 and 10 degrees would swing through 180 instead
// of crossing zero. The caller resets the pose, adds every source, then resolves
// it once into the skeleton.
struct AnimationPose
{
    std::array<Vector2D, kMaxBones> Positions{};
    std::array<float, kMaxBones> Scales{};
    std::array<float, kMaxBones> RotationSin{};
    std::array<float, kMaxBones> RotationCos{};
    float Weight = 0.0f;
    std::uint16_t BoneCount = 0;
};

static_assert(std::is_trivially_copyable_v<AnimationPose>);
static_assert(std::is_standard_layout_v<AnimationPose>);

// Clears the accumulator and fixes its bone count to the skeleton. Every
// sampling call expects a reset pose, so a source set never leaks into the
// next frame.
void ResetAnimationPose(AnimationPose& InPose, const SkeletonComponent& InSkeleton) noexcept;

// Adds one clip at a normalized phase in [0, 1]. The phase is shared across a
// directional set, so eight run cycles keep the same foot phase even when their
// authored durations differ. A channel the clip does not author contributes its
// rest value, so every source is a complete pose. The skeleton must carry a
// captured rest pose; AnimGraphCaptureRestPose does that before the first
// driven tick.
void AccumulateAnimationClip(const CompressedClip& InClip, const AnimationClipBinding& InBinding,
                             const SkeletonComponent& InSkeleton, float InPhase, float InWeight,
                             AnimationPose& InPose) noexcept;

// Adds one clip at an absolute time. A caller that owns its own clock uses this
// instead of the shared phase.
void AccumulateAnimationClipAtTime(const CompressedClip& InClip, const AnimationClipBinding& InBinding,
                                   const SkeletonComponent& InSkeleton, float InTime, float InWeight,
                                   AnimationPose& InPose) noexcept;

// Divides the accumulated channels by the total weight and writes the local
// pose into the skeleton, then runs one hierarchy evaluation. A zero total
// weight leaves the skeleton untouched.
void ResolveAnimationPose(const AnimationPose& InPose, SkeletonComponent& InSkeleton) noexcept;

// Advances one clip clock. Looping wraps into [0, duration]; a non-looping clip
// clamps at both ends. A non-positive duration returns zero.
[[nodiscard]] float AdvanceAnimationClipTime(float InTime, float InDt, float InDuration, bool InIsLooping) noexcept;

// Advances the shared normalized phase. A phase is one cycle, so the same
// network of directional clips stays synchronized.
[[nodiscard]] float AdvanceAnimationPhase(float InPhase, float InDt, float InCycleDuration, bool InIsLooping) noexcept;
}
