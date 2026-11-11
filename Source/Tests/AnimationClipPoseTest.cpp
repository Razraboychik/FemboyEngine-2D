// FemboyEngine2D - runtime clip sampling and pose blending tests.
#include "Engine/ECS/AnimGraph.hpp"
#include "Engine/ECS/AnimationClipPose.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace
{

int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void CheckClose(float InActual, float InExpected, float InTolerance, const char* InMessage)
{
    if (std::fabs(InActual - InExpected) > InTolerance)
    {
        std::printf("FAIL: %s (expected %f, got %f)\n", InMessage, static_cast<double>(InExpected),
                    static_cast<double>(InActual));
        ++GFailures;
    }
}

using Fby::Animation::AccumulateAnimationClip;
using Fby::Animation::AdvanceAnimationClipTime;
using Fby::Animation::AdvanceAnimationPhase;
using Fby::Animation::AnimationClipBinding;
using Fby::Animation::AnimationPose;
using Fby::Animation::BakeAnimationClipBinding;
using Fby::Animation::CompressedClip;
using Fby::Animation::CompressedKey;
using Fby::Animation::CompressedTrack;
using Fby::Animation::Interpolation;
using Fby::Animation::kNoPoseBone;
using Fby::Animation::ResetAnimationPose;
using Fby::Animation::ResolveAnimationPose;
using Fby::Animation::TrackKind;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = 0.5f * kPi;
constexpr float kDegreesToRadians = kPi / 180.0f;

std::int16_t Quantize(float InValue, float InScale)
{
    const float ScopedNormalized = std::clamp(InValue / InScale, -1.0f, 1.0f);
    return static_cast<std::int16_t>(std::lround(ScopedNormalized * 32767.0f));
}

float ScaleFor(float InA, float InB)
{
    return std::max(1.0f, std::max(std::fabs(InA), std::fabs(InB)));
}

CompressedClip MakeRotationClip(std::string_view InBone, float InFrom, float InTo, float InDuration)
{
    CompressedClip ScopedClip;
    ScopedClip.Duration = InDuration;
    ScopedClip.TrackCount = 1;
    CompressedTrack& ScopedTrack = ScopedClip.Tracks[0];
    ScopedTrack.Kind = TrackKind::BoneRotation;
    ScopedTrack.BoneHash = Fby::BoneNameHash(InBone);
    ScopedTrack.ScaleX = ScaleFor(InFrom, InTo);
    ScopedTrack.KeyCount = 2;
    ScopedClip.KeyCount = 2;
    ScopedClip.Keys[0] =
        CompressedKey{0, Quantize(InFrom, ScopedTrack.ScaleX), 0, static_cast<std::uint8_t>(Interpolation::Linear), 0};
    ScopedClip.Keys[1] = CompressedKey{65535, Quantize(InTo, ScopedTrack.ScaleX), 0,
                                       static_cast<std::uint8_t>(Interpolation::Linear), 0};
    return ScopedClip;
}

CompressedClip MakeConstantRotationClip(std::string_view InBone, float InRotation)
{
    CompressedClip ScopedClip;
    ScopedClip.Duration = 1.0f;
    ScopedClip.TrackCount = 1;
    CompressedTrack& ScopedTrack = ScopedClip.Tracks[0];
    ScopedTrack.Kind = TrackKind::BoneRotation;
    ScopedTrack.BoneHash = Fby::BoneNameHash(InBone);
    ScopedTrack.ScaleX = ScaleFor(InRotation, 0.0f);
    ScopedTrack.KeyCount = 1;
    ScopedClip.KeyCount = 1;
    ScopedClip.Keys[0] = CompressedKey{0, Quantize(InRotation, ScopedTrack.ScaleX), 0,
                                       static_cast<std::uint8_t>(Interpolation::Linear), 0};
    return ScopedClip;
}

CompressedClip MakePositionClip(std::string_view InBone, Fby::Vector2D InFrom, Fby::Vector2D InTo, float InDuration)
{
    CompressedClip ScopedClip;
    ScopedClip.Duration = InDuration;
    ScopedClip.TrackCount = 1;
    CompressedTrack& ScopedTrack = ScopedClip.Tracks[0];
    ScopedTrack.Kind = TrackKind::BonePosition;
    ScopedTrack.BoneHash = Fby::BoneNameHash(InBone);
    ScopedTrack.ScaleX = ScaleFor(InFrom.X, InTo.X);
    ScopedTrack.ScaleY = ScaleFor(InFrom.Y, InTo.Y);
    ScopedTrack.KeyCount = 2;
    ScopedClip.KeyCount = 2;
    ScopedClip.Keys[0] =
        CompressedKey{0, Quantize(InFrom.X, ScopedTrack.ScaleX), Quantize(InFrom.Y, ScopedTrack.ScaleY),
                      static_cast<std::uint8_t>(Interpolation::Linear), 0};
    ScopedClip.Keys[1] =
        CompressedKey{65535, Quantize(InTo.X, ScopedTrack.ScaleX), Quantize(InTo.Y, ScopedTrack.ScaleY),
                      static_cast<std::uint8_t>(Interpolation::Linear), 0};
    return ScopedClip;
}

CompressedClip MakeScaleClip(std::string_view InBone, float InFrom, float InTo, float InDuration)
{
    CompressedClip ScopedClip;
    ScopedClip.Duration = InDuration;
    ScopedClip.TrackCount = 1;
    CompressedTrack& ScopedTrack = ScopedClip.Tracks[0];
    ScopedTrack.Kind = TrackKind::BoneScale;
    ScopedTrack.BoneHash = Fby::BoneNameHash(InBone);
    ScopedTrack.ScaleX = ScaleFor(InFrom, InTo);
    ScopedTrack.KeyCount = 2;
    ScopedClip.KeyCount = 2;
    ScopedClip.Keys[0] =
        CompressedKey{0, Quantize(InFrom, ScopedTrack.ScaleX), 0, static_cast<std::uint8_t>(Interpolation::Linear), 0};
    ScopedClip.Keys[1] = CompressedKey{65535, Quantize(InTo, ScopedTrack.ScaleX), 0,
                                       static_cast<std::uint8_t>(Interpolation::Linear), 0};
    return ScopedClip;
}

Fby::SkeletonComponent MakeSkeleton()
{
    Fby::SkeletonComponent ScopedSkeleton;
    Fby::SkeletonReset(ScopedSkeleton);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "root", -1, {0.0f, 0.0f}, 0.0f);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "arm", 0, {10.0f, 0.0f}, 0.0f);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "hand", 1, {10.0f, 0.0f}, 0.0f);
    Fby::SkeletonBindPose(ScopedSkeleton);
    Fby::AnimGraphCaptureRestPose(ScopedSkeleton);
    return ScopedSkeleton;
}

AnimationClipBinding BindOrFail(const CompressedClip& InClip, const Fby::SkeletonComponent& InSkeleton)
{
    AnimationClipBinding ScopedBinding;
    (void)BakeAnimationClipBinding(InClip, InSkeleton, &ScopedBinding);
    return ScopedBinding;
}

void TestBinding()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    const CompressedClip ScopedClip = MakeRotationClip("arm", 0.0f, kHalfPi, 1.0f);
    AnimationClipBinding ScopedBinding;
    Check(BakeAnimationClipBinding(ScopedClip, ScopedSkeleton, &ScopedBinding), "rotation clip binds");
    Check(ScopedBinding.IsValid && ScopedBinding.TrackCount == 1, "binding reports one track");
    Check(ScopedBinding.TrackBones[0] == 1, "arm track resolves to bone 1");

    const CompressedClip ScopedUnknown = MakeRotationClip("missing", 0.0f, 1.0f, 1.0f);
    Check(!BakeAnimationClipBinding(ScopedUnknown, ScopedSkeleton, &ScopedBinding), "unknown bone hash is rejected");
    Check(!ScopedBinding.IsValid, "failed binding is not valid");

    CompressedClip ScopedDuplicate = MakeRotationClip("arm", 0.0f, 1.0f, 1.0f);
    ScopedDuplicate.TrackCount = 2;
    ScopedDuplicate.Tracks[1] = ScopedDuplicate.Tracks[0];
    Check(!BakeAnimationClipBinding(ScopedDuplicate, ScopedSkeleton, &ScopedBinding),
          "duplicate kind and bone is rejected");

    CompressedClip ScopedSprite = MakeRotationClip("arm", 0.0f, 1.0f, 1.0f);
    ScopedSprite.Tracks[0].Kind = TrackKind::SpriteClip;
    ScopedSprite.Tracks[0].BoneHash = 0;
    Check(BakeAnimationClipBinding(ScopedSprite, ScopedSkeleton, &ScopedBinding), "sprite track binds");
    Check(ScopedBinding.TrackBones[0] == kNoPoseBone, "sprite track has no pose bone");
}

void TestSingleClipSampling()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    const CompressedClip ScopedClip = MakeRotationClip("arm", 0.0f, kHalfPi, 1.0f);
    const AnimationClipBinding ScopedBinding = BindOrFail(ScopedClip, ScopedSkeleton);

    AnimationPose ScopedPose;
    ResetAnimationPose(ScopedPose, ScopedSkeleton);
    Check(ScopedPose.BoneCount == 3, "reset pose fixes the bone count");
    CheckClose(ScopedPose.Weight, 0.0f, 0.0f, "reset pose has no weight");

    AccumulateAnimationClip(ScopedClip, ScopedBinding, ScopedSkeleton, 0.5f, 1.0f, ScopedPose);
    CheckClose(ScopedPose.Weight, 1.0f, 1.0e-6f, "single source accumulates its weight");
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalRotations[1], 0.25f * kPi, 1.0e-3f, "half phase samples half the rotation");
    CheckClose(ScopedSkeleton.LocalRotations[0], 0.0f, 1.0e-6f, "untracked root keeps its rotation");
    CheckClose(ScopedSkeleton.LocalRotations[2], 0.0f, 1.0e-6f, "untracked hand keeps its rotation");

    ResetAnimationPose(ScopedPose, ScopedSkeleton);
    AccumulateAnimationClip(ScopedClip, ScopedBinding, ScopedSkeleton, 0.0f, 1.0f, ScopedPose);
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalRotations[1], 0.0f, 1.0e-3f, "zero phase samples the first key");

    ResetAnimationPose(ScopedPose, ScopedSkeleton);
    AccumulateAnimationClip(ScopedClip, ScopedBinding, ScopedSkeleton, 1.0f, 1.0f, ScopedPose);
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalRotations[1], kHalfPi, 1.0e-3f, "full phase samples the last key");
}

void TestBlendAcrossWrap()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    const CompressedClip ScopedLeft = MakeConstantRotationClip("arm", 350.0f * kDegreesToRadians);
    const CompressedClip ScopedRight = MakeConstantRotationClip("arm", 10.0f * kDegreesToRadians);
    const AnimationClipBinding ScopedLeftBinding = BindOrFail(ScopedLeft, ScopedSkeleton);
    const AnimationClipBinding ScopedRightBinding = BindOrFail(ScopedRight, ScopedSkeleton);

    AnimationPose ScopedPose;
    ResetAnimationPose(ScopedPose, ScopedSkeleton);
    AccumulateAnimationClip(ScopedLeft, ScopedLeftBinding, ScopedSkeleton, 0.5f, 0.5f, ScopedPose);
    AccumulateAnimationClip(ScopedRight, ScopedRightBinding, ScopedSkeleton, 0.5f, 0.5f, ScopedPose);
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalRotations[1], 0.0f, 1.0e-3f, "350 and 10 degrees blend through zero");

    const CompressedClip ScopedZero = MakeConstantRotationClip("arm", 0.0f);
    const CompressedClip ScopedQuarter = MakeConstantRotationClip("arm", kHalfPi);
    const AnimationClipBinding ScopedZeroBinding = BindOrFail(ScopedZero, ScopedSkeleton);
    const AnimationClipBinding ScopedQuarterBinding = BindOrFail(ScopedQuarter, ScopedSkeleton);
    ResetAnimationPose(ScopedPose, ScopedSkeleton);
    AccumulateAnimationClip(ScopedZero, ScopedZeroBinding, ScopedSkeleton, 0.5f, 0.5f, ScopedPose);
    AccumulateAnimationClip(ScopedQuarter, ScopedQuarterBinding, ScopedSkeleton, 0.5f, 0.5f, ScopedPose);
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalRotations[1], 0.25f * kPi, 1.0e-3f, "0 and 90 degrees blend to 45");
}

void TestPositionAndScale()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    const CompressedClip ScopedPosition = MakePositionClip("hand", {0.0f, 0.0f}, {30.0f, 0.0f}, 1.0f);
    const CompressedClip ScopedScale = MakeScaleClip("hand", 1.0f, 2.0f, 1.0f);
    const AnimationClipBinding ScopedPositionBinding = BindOrFail(ScopedPosition, ScopedSkeleton);
    const AnimationClipBinding ScopedScaleBinding = BindOrFail(ScopedScale, ScopedSkeleton);

    AnimationPose ScopedPose;
    ResetAnimationPose(ScopedPose, ScopedSkeleton);
    AccumulateAnimationClip(ScopedPosition, ScopedPositionBinding, ScopedSkeleton, 0.5f, 1.0f, ScopedPose);
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalPositions[2].X, 15.0f, 0.05f, "half phase samples half the position");
    CheckClose(ScopedSkeleton.LocalPositions[2].Y, 0.0f, 1.0e-3f, "position track keeps the other axis");

    ResetAnimationPose(ScopedPose, ScopedSkeleton);
    AccumulateAnimationClip(ScopedScale, ScopedScaleBinding, ScopedSkeleton, 0.5f, 1.0f, ScopedPose);
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalScales[2], 1.5f, 1.0e-3f, "half phase samples half the scale");
}

void TestWeightedSources()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    const CompressedClip ScopedNegative = MakeConstantRotationClip("arm", -20.0f * kDegreesToRadians);
    const CompressedClip ScopedRest = MakeConstantRotationClip("arm", 0.0f);
    const CompressedClip ScopedPositive = MakeConstantRotationClip("arm", 20.0f * kDegreesToRadians);
    const AnimationClipBinding ScopedNegativeBinding = BindOrFail(ScopedNegative, ScopedSkeleton);
    const AnimationClipBinding ScopedRestBinding = BindOrFail(ScopedRest, ScopedSkeleton);
    const AnimationClipBinding ScopedPositiveBinding = BindOrFail(ScopedPositive, ScopedSkeleton);

    AnimationPose ScopedPose;
    ResetAnimationPose(ScopedPose, ScopedSkeleton);
    AccumulateAnimationClip(ScopedNegative, ScopedNegativeBinding, ScopedSkeleton, 0.5f, 0.25f, ScopedPose);
    AccumulateAnimationClip(ScopedRest, ScopedRestBinding, ScopedSkeleton, 0.5f, 0.5f, ScopedPose);
    AccumulateAnimationClip(ScopedPositive, ScopedPositiveBinding, ScopedSkeleton, 0.5f, 0.25f, ScopedPose);
    CheckClose(ScopedPose.Weight, 1.0f, 1.0e-6f, "three sources accumulate to one");
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalRotations[1], 0.0f, 1.0e-3f, "symmetric sources blend to the middle");
}

void TestZeroWeightAndReset()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    ScopedSkeleton.LocalRotations[1] = 0.3f;
    AnimationPose ScopedPose;
    ResetAnimationPose(ScopedPose, ScopedSkeleton);
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalRotations[1], 0.3f, 1.0e-6f, "empty pose leaves the skeleton untouched");

    const CompressedClip ScopedClip = MakeConstantRotationClip("arm", 1.0f);
    const AnimationClipBinding ScopedBinding = BindOrFail(ScopedClip, ScopedSkeleton);
    AccumulateAnimationClip(ScopedClip, ScopedBinding, ScopedSkeleton, 0.5f, 0.0f, ScopedPose);
    CheckClose(ScopedPose.Weight, 0.0f, 0.0f, "zero weight does not accumulate");
    ResolveAnimationPose(ScopedPose, ScopedSkeleton);
    CheckClose(ScopedSkeleton.LocalRotations[1], 0.3f, 1.0e-6f, "zero weight leaves the skeleton untouched");

    CompressedClip ScopedEmpty;
    ScopedEmpty.Duration = 0.0f;
    const AnimationClipBinding ScopedEmptyBinding = BindOrFail(ScopedEmpty, ScopedSkeleton);
    AccumulateAnimationClip(ScopedEmpty, ScopedEmptyBinding, ScopedSkeleton, 0.5f, 1.0f, ScopedPose);
    CheckClose(ScopedPose.Weight, 0.0f, 0.0f, "zero-duration clip does not accumulate");
}

void TestAdvance()
{
    CheckClose(AdvanceAnimationClipTime(0.9f, 0.2f, 1.0f, true), 0.1f, 1.0e-5f, "looping clock wraps");
    CheckClose(AdvanceAnimationClipTime(0.9f, 0.2f, 1.0f, false), 1.0f, 1.0e-5f, "clamped clock stops at the end");
    CheckClose(AdvanceAnimationClipTime(0.1f, -0.2f, 1.0f, true), 0.9f, 1.0e-5f, "negative delta rewinds");
    CheckClose(AdvanceAnimationClipTime(0.5f, 0.5f, 0.0f, true), 0.0f, 0.0f, "zero duration returns zero");
    CheckClose(AdvanceAnimationPhase(0.9f, 0.2f, 1.0f, true), 0.1f, 1.0e-5f, "phase wraps");
    CheckClose(AdvanceAnimationPhase(0.5f, 0.6f, 2.0f, true), 0.8f, 1.0e-5f, "phase uses the cycle duration");
    CheckClose(AdvanceAnimationPhase(0.9f, 0.2f, 1.0f, false), 1.0f, 1.0e-5f, "clamped phase stops at one");
}

Fby::Animation::DirectionalBlendSpaceDocument MakeEightWayRing()
{
    Fby::Animation::DirectionalBlendSpaceDocument ScopedDocument;
    ScopedDocument.AnchorCount = 8;
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
    {
        const float ScopedAngle = static_cast<float>(ScopedIndex) * 45.0f * kDegreesToRadians;
        ScopedDocument.Anchors[ScopedIndex].Position = {std::cos(ScopedAngle), std::sin(ScopedAngle)};
        ScopedDocument.Anchors[ScopedIndex].ClipId = ScopedIndex;
    }
    return ScopedDocument;
}

void TestDirectionalIntegration()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    std::array<CompressedClip, 8> ScopedClips;
    std::array<AnimationClipBinding, 8> ScopedBindings;
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
    {
        ScopedClips[ScopedIndex] =
            MakeConstantRotationClip("arm", static_cast<float>(ScopedIndex) * 45.0f * kDegreesToRadians);
        ScopedBindings[ScopedIndex] = BindOrFail(ScopedClips[ScopedIndex], ScopedSkeleton);
    }
    // A sprite track shares the clip with the pose tracks: it binds to no bone
    // and must not disturb the skeletal sample.
    ScopedClips[0].Tracks[1].Kind = TrackKind::SpriteClip;
    ScopedClips[0].Tracks[1].FirstKey = 1;
    ScopedClips[0].Tracks[1].KeyCount = 1;
    ScopedClips[0].TrackCount = 2;
    ScopedClips[0].KeyCount = 2;
    ScopedClips[0].Keys[1] = CompressedKey{0, 0, 4, static_cast<std::uint8_t>(Interpolation::Step), 0};
    ScopedBindings[0] = BindOrFail(ScopedClips[0], ScopedSkeleton);

    Fby::Animation::DirectionalBlendTable ScopedTable;
    Check(Fby::Animation::BakeDirectionalBlendSpace(MakeEightWayRing(), &ScopedTable), "directional table bakes");

    for (const float ScopedHeadingDegrees : {22.5f, 200.0f})
    {
        const float ScopedHeading = ScopedHeadingDegrees * kDegreesToRadians;
        Fby::Animation::DirectionalBlendSample ScopedSample;
        Fby::Animation::EvaluateDirectionalBlend(ScopedTable, {std::cos(ScopedHeading), std::sin(ScopedHeading)},
                                                 &ScopedSample);
        Check(ScopedSample.Count >= 1, "directional sample resolves a source");
        AnimationPose ScopedPose;
        ResetAnimationPose(ScopedPose, ScopedSkeleton);
        for (std::uint8_t ScopedWeight = 0; ScopedWeight < ScopedSample.Count; ++ScopedWeight)
        {
            const std::uint32_t ScopedClip = ScopedSample.Weights[ScopedWeight].ClipId;
            Check(ScopedClip < 8, "directional clip id is inside the set");
            AccumulateAnimationClip(ScopedClips[ScopedClip], ScopedBindings[ScopedClip], ScopedSkeleton, 0.25f,
                                    ScopedSample.Weights[ScopedWeight].Weight, ScopedPose);
        }
        ResolveAnimationPose(ScopedPose, ScopedSkeleton);
        float ScopedDegrees = ScopedSkeleton.LocalRotations[1] * (180.0f / kPi);
        ScopedDegrees = std::fmod(ScopedDegrees, 360.0f);
        if (ScopedDegrees < 0.0f)
        {
            ScopedDegrees += 360.0f;
        }
        CheckClose(ScopedDegrees, ScopedHeadingDegrees, 1.0f, "directional weights drive the skeletal heading");
    }
}

void TestDeterminism()
{
    const Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    const CompressedClip ScopedClip = MakeRotationClip("arm", 0.0f, kHalfPi, 1.0f);
    const AnimationClipBinding ScopedBinding = BindOrFail(ScopedClip, ScopedSkeleton);

    AnimationPose ScopedFirst;
    AnimationPose ScopedSecond;
    ResetAnimationPose(ScopedFirst, ScopedSkeleton);
    ResetAnimationPose(ScopedSecond, ScopedSkeleton);
    AccumulateAnimationClip(ScopedClip, ScopedBinding, ScopedSkeleton, 0.37f, 0.8f, ScopedFirst);
    AccumulateAnimationClip(ScopedClip, ScopedBinding, ScopedSkeleton, 0.37f, 0.8f, ScopedSecond);
    Check(std::memcmp(&ScopedFirst, &ScopedSecond, sizeof(AnimationPose)) == 0, "pose accumulation is deterministic");

    Fby::SkeletonComponent ScopedFirstSkeleton = ScopedSkeleton;
    Fby::SkeletonComponent ScopedSecondSkeleton = ScopedSkeleton;
    ResolveAnimationPose(ScopedFirst, ScopedFirstSkeleton);
    ResolveAnimationPose(ScopedSecond, ScopedSecondSkeleton);
    CheckClose(ScopedFirstSkeleton.LocalRotations[1], ScopedSecondSkeleton.LocalRotations[1], 0.0f,
               "pose resolution is deterministic");
}
}

int main()
{
    TestBinding();
    TestSingleClipSampling();
    TestBlendAcrossWrap();
    TestPositionAndScale();
    TestWeightedSources();
    TestZeroWeightAndReset();
    TestAdvance();
    TestDirectionalIntegration();
    TestDeterminism();
    if (GFailures != 0)
    {
        std::printf("%d animation clip pose test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("animation_clip_pose_test: all tests passed\n");
    return 0;
}
