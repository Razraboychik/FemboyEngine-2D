// FemboyEngine2D - two-bone and FABRIK solvers, retargeting and pose quantization.
#include "Engine/ECS/SkeletonIk.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fby::Skeletal
{
namespace
{
constexpr float kEpsilon = 1.0e-5f;
constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] Vector2D Origin(const SkeletonComponent& InSkeleton, std::uint16_t InBone) noexcept
{
    const Mat3& ScopedMatrix = InSkeleton.GlobalMatrices[InBone];
    return Vector2D{ScopedMatrix.m[6], ScopedMatrix.m[7]};
}

[[nodiscard]] float WorldRotation(const SkeletonComponent& InSkeleton, std::uint16_t InBone) noexcept
{
    const Mat3& ScopedMatrix = InSkeleton.GlobalMatrices[InBone];
    return std::atan2(ScopedMatrix.m[1], ScopedMatrix.m[0]);
}

[[nodiscard]] float WrapAngle(float InAngle) noexcept
{
    if (!std::isfinite(InAngle))
    {
        return 0.0f;
    }
    float ScopedAngle = InAngle;
    if (ScopedAngle > 2.0f * kPi || ScopedAngle < -2.0f * kPi)
    {
        ScopedAngle = std::fmod(ScopedAngle, 2.0f * kPi);
    }
    while (ScopedAngle > kPi)
    {
        ScopedAngle -= 2.0f * kPi;
    }
    while (ScopedAngle < -kPi)
    {
        ScopedAngle += 2.0f * kPi;
    }
    return ScopedAngle;
}

[[nodiscard]] float LerpAngle(float InFrom, float InTo, float InWeight) noexcept
{
    const float ScopedDelta = WrapAngle(InTo - InFrom);
    return InFrom + ScopedDelta * InWeight;
}

[[nodiscard]] float Length(Vector2D InValue) noexcept
{
    return std::sqrt(InValue.X * InValue.X + InValue.Y * InValue.Y);
}

[[nodiscard]] bool IsParentChain(const SkeletonComponent& InSkeleton, std::uint16_t InParent,
                                 std::uint16_t InChild) noexcept
{
    if (InChild >= InSkeleton.BoneCount || InParent >= InSkeleton.BoneCount)
    {
        return false;
    }
    return InSkeleton.ParentIndices[InChild] == static_cast<std::int16_t>(InParent);
}

[[nodiscard]] float ParentWorldRotation(const SkeletonComponent& InSkeleton, std::uint16_t InBone) noexcept
{
    const std::int16_t ScopedParent = InSkeleton.ParentIndices[InBone];
    if (ScopedParent < 0 || ScopedParent >= static_cast<std::int16_t>(InSkeleton.BoneCount))
    {
        return 0.0f;
    }
    return WorldRotation(InSkeleton, static_cast<std::uint16_t>(ScopedParent));
}

[[nodiscard]] bool ValidateChain(const SkeletonComponent& InSkeleton, const IkChain& InChain) noexcept
{
    if (InChain.BoneCount < 2 || InChain.BoneCount > kMaxIkBones)
    {
        return false;
    }
    if (InChain.Bones[0] >= InSkeleton.BoneCount)
    {
        return false;
    }
    for (std::uint16_t ScopedI = 1; ScopedI < InChain.BoneCount; ++ScopedI)
    {
        if (!IsParentChain(InSkeleton, InChain.Bones[ScopedI - 1], InChain.Bones[ScopedI]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Vector2D TipTarget(const SkeletonComponent& InSkeleton, const IkChain& InChain) noexcept
{
    const std::uint16_t ScopedTip = InChain.Bones[InChain.BoneCount - 1];
    const float ScopedRotation = WorldRotation(InSkeleton, ScopedTip);
    const float ScopedCosine = std::cos(ScopedRotation);
    const float ScopedSine = std::sin(ScopedRotation);
    return Vector2D{InChain.Target.X + InChain.TipOffset.X * ScopedCosine - InChain.TipOffset.Y * ScopedSine,
                    InChain.Target.Y + InChain.TipOffset.X * ScopedSine + InChain.TipOffset.Y * ScopedCosine};
}

bool SolveTwoBone(SkeletonComponent& InSkeleton, const IkChain& InChain) noexcept
{
    if (InChain.BoneCount != 3)
    {
        return false;
    }
    const std::uint16_t ScopedRoot = InChain.Bones[0];
    const std::uint16_t ScopedMid = InChain.Bones[1];
    const std::uint16_t ScopedTip = InChain.Bones[2];
    const Vector2D ScopedRootPosition = Origin(InSkeleton, ScopedRoot);
    const Vector2D ScopedMidPosition = Origin(InSkeleton, ScopedMid);
    const Vector2D ScopedTipPosition = Origin(InSkeleton, ScopedTip);
    const float ScopedLength1 = Length(ScopedMidPosition - ScopedRootPosition);
    const float ScopedLength2 = Length(ScopedTipPosition - ScopedMidPosition);
    if (ScopedLength1 < kEpsilon || ScopedLength2 < kEpsilon)
    {
        return false;
    }

    const Vector2D ScopedTarget = TipTarget(InSkeleton, InChain);
    const Vector2D ScopedToTarget = ScopedTarget - ScopedRootPosition;
    const float ScopedDistanceRaw = Length(ScopedToTarget);
    if (ScopedDistanceRaw < kEpsilon)
    {
        return false;
    }
    const float ScopedMinimum = std::fabs(ScopedLength1 - ScopedLength2) + kEpsilon;
    const float ScopedMaximum = ScopedLength1 + ScopedLength2 - kEpsilon;
    const float ScopedDistance = std::clamp(ScopedDistanceRaw, ScopedMinimum, std::max(ScopedMinimum, ScopedMaximum));
    const float ScopedBaseAngle = std::atan2(ScopedToTarget.Y, ScopedToTarget.X);

    // Circle intersection: the elbow lies `Along` down the root->target line and
    // `Perpendicular` to it; the pole sign picks which of the two solutions.
    const float ScopedAlong =
        (ScopedLength1 * ScopedLength1 - ScopedLength2 * ScopedLength2 + ScopedDistance * ScopedDistance) /
        (2.0f * ScopedDistance);
    const float ScopedHeightSquared = ScopedLength1 * ScopedLength1 - ScopedAlong * ScopedAlong;
    const float ScopedPerpendicular = ScopedHeightSquared > 0.0f ? std::sqrt(ScopedHeightSquared) : 0.0f;
    const float ScopedSign = InChain.PoleAngle >= 0.0f ? 1.0f : -1.0f;
    const float ScopedCosine = std::cos(ScopedBaseAngle);
    const float ScopedSine = std::sin(ScopedBaseAngle);
    const Vector2D ScopedDirection{ScopedCosine, ScopedSine};
    const Vector2D ScopedNormal{-ScopedSine, ScopedCosine};
    const Vector2D ScopedElbow = ScopedRootPosition + ScopedDirection * ScopedAlong +
                                 ScopedNormal * (ScopedPerpendicular * ScopedSign);

    const float ScopedRootWorld = std::atan2(ScopedElbow.Y - ScopedRootPosition.Y, ScopedElbow.X - ScopedRootPosition.X);
    const float ScopedMidWorld = std::atan2(ScopedTarget.Y - ScopedElbow.Y, ScopedTarget.X - ScopedElbow.X);
    const float ScopedRootParent = ParentWorldRotation(InSkeleton, ScopedRoot);
    const float ScopedWeight = std::clamp(InChain.Weight, 0.0f, 1.0f);

    InSkeleton.LocalRotations[ScopedRoot] = WrapAngle(
        LerpAngle(InSkeleton.LocalRotations[ScopedRoot], ScopedRootWorld - ScopedRootParent, ScopedWeight));
    InSkeleton.LocalRotations[ScopedMid] = WrapAngle(
        LerpAngle(InSkeleton.LocalRotations[ScopedMid], ScopedMidWorld - ScopedRootWorld, ScopedWeight));
    return true;
}

bool SolveFabrik(SkeletonComponent& InSkeleton, const IkChain& InChain) noexcept
{
    const std::uint16_t ScopedBoneCount = InChain.BoneCount;
    if (ScopedBoneCount < 2)
    {
        return false;
    }
    std::array<Vector2D, kMaxIkBones> ScopedPositions{};
    std::array<float, kMaxIkBones - 1> ScopedLengths{};
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedBoneCount; ++ScopedI)
    {
        ScopedPositions[ScopedI] = Origin(InSkeleton, InChain.Bones[ScopedI]);
    }
    float ScopedTotalLength = 0.0f;
    for (std::uint16_t ScopedI = 0; ScopedI + 1 < ScopedBoneCount; ++ScopedI)
    {
        ScopedLengths[ScopedI] = Length(ScopedPositions[ScopedI + 1] - ScopedPositions[ScopedI]);
        ScopedTotalLength += ScopedLengths[ScopedI];
    }
    if (ScopedTotalLength < kEpsilon)
    {
        return false;
    }

    const Vector2D ScopedRootPosition = ScopedPositions[0];
    const Vector2D ScopedTarget = TipTarget(InSkeleton, InChain);
    const float ScopedReach = Length(ScopedTarget - ScopedRootPosition);
    const std::uint32_t ScopedIterations = std::clamp<std::uint32_t>(InChain.Iterations, 1u, 32u);

    if (ScopedReach > ScopedTotalLength)
    {
        // Out of reach: the chain is a straight line toward the target.
        const Vector2D ScopedDirection = (ScopedTarget - ScopedRootPosition) * (1.0f / ScopedReach);
        for (std::uint16_t ScopedI = 1; ScopedI < ScopedBoneCount; ++ScopedI)
        {
            ScopedPositions[ScopedI] = ScopedPositions[ScopedI - 1] + ScopedDirection * ScopedLengths[ScopedI - 1];
        }
    }
    else
    {
        for (std::uint32_t ScopedIteration = 0; ScopedIteration < ScopedIterations; ++ScopedIteration)
        {
            ScopedPositions[ScopedBoneCount - 1] = ScopedTarget;
            for (std::uint16_t ScopedI = ScopedBoneCount - 1; ScopedI > 0; --ScopedI)
            {
                const Vector2D ScopedDelta = ScopedPositions[ScopedI - 1] - ScopedPositions[ScopedI];
                const float ScopedDistance = Length(ScopedDelta);
                if (ScopedDistance < kEpsilon)
                {
                    continue;
                }
                ScopedPositions[ScopedI - 1] =
                    ScopedPositions[ScopedI] + ScopedDelta * (ScopedLengths[ScopedI - 1] / ScopedDistance);
            }
            ScopedPositions[0] = ScopedRootPosition;
            for (std::uint16_t ScopedI = 0; ScopedI + 1 < ScopedBoneCount; ++ScopedI)
            {
                const Vector2D ScopedDelta = ScopedPositions[ScopedI + 1] - ScopedPositions[ScopedI];
                const float ScopedDistance = Length(ScopedDelta);
                if (ScopedDistance < kEpsilon)
                {
                    continue;
                }
                ScopedPositions[ScopedI + 1] =
                    ScopedPositions[ScopedI] + ScopedDelta * (ScopedLengths[ScopedI] / ScopedDistance);
            }
        }
    }

    const float ScopedWeight = std::clamp(InChain.Weight, 0.0f, 1.0f);
    float ScopedParentWorld = ParentWorldRotation(InSkeleton, InChain.Bones[0]);
    for (std::uint16_t ScopedI = 0; ScopedI + 1 < ScopedBoneCount; ++ScopedI)
    {
        const Vector2D ScopedDelta = ScopedPositions[ScopedI + 1] - ScopedPositions[ScopedI];
        if (Length(ScopedDelta) < kEpsilon)
        {
            ScopedParentWorld += InSkeleton.LocalRotations[InChain.Bones[ScopedI]];
            continue;
        }
        const float ScopedDesiredWorld = std::atan2(ScopedDelta.Y, ScopedDelta.X);
        const std::uint16_t ScopedBone = InChain.Bones[ScopedI];
        const float ScopedLocal = WrapAngle(LerpAngle(InSkeleton.LocalRotations[ScopedBone],
                                                      ScopedDesiredWorld - ScopedParentWorld, ScopedWeight));
        InSkeleton.LocalRotations[ScopedBone] = ScopedLocal;
        ScopedParentWorld = ScopedDesiredWorld;
    }
    return true;
}
}

bool MakeTwoBoneChain(const SkeletonComponent& InSkeleton, std::uint16_t InRoot, std::uint16_t InMid,
                      std::uint16_t InTip, Vector2D InTarget, IkChain* OutChain) noexcept
{
    if (OutChain == nullptr)
    {
        return false;
    }
    *OutChain = IkChain{};
    if (!IsParentChain(InSkeleton, InRoot, InMid) || !IsParentChain(InSkeleton, InMid, InTip))
    {
        return false;
    }
    OutChain->Bones[0] = InRoot;
    OutChain->Bones[1] = InMid;
    OutChain->Bones[2] = InTip;
    OutChain->BoneCount = 3;
    OutChain->Target = InTarget;
    OutChain->Solver = IkSolver::TwoBone;
    return true;
}

bool MakeFabrikChain(const SkeletonComponent& InSkeleton, const std::uint16_t* InBones, std::uint16_t InBoneCount,
                     Vector2D InTarget, IkChain* OutChain) noexcept
{
    if (OutChain == nullptr || InBones == nullptr)
    {
        return false;
    }
    if (InBoneCount < 2 || InBoneCount > kMaxIkBones)
    {
        return false;
    }
    *OutChain = IkChain{};
    for (std::uint16_t ScopedI = 0; ScopedI < InBoneCount; ++ScopedI)
    {
        if (InBones[ScopedI] >= InSkeleton.BoneCount)
        {
            return false;
        }
        if (ScopedI > 0 && !IsParentChain(InSkeleton, InBones[ScopedI - 1], InBones[ScopedI]))
        {
            return false;
        }
        OutChain->Bones[ScopedI] = InBones[ScopedI];
    }
    OutChain->BoneCount = InBoneCount;
    OutChain->Target = InTarget;
    OutChain->Solver = IkSolver::Fabrik;
    return true;
}

bool SolveIk(SkeletonComponent& InSkeleton, const IkChain& InChain) noexcept
{
    if (InChain.Weight <= 0.0f)
    {
        return false;
    }
    if (!ValidateChain(InSkeleton, InChain))
    {
        return false;
    }
    SkeletalAnimation::Evaluate(InSkeleton);
    bool ScopedSolved = false;
    if (InChain.Solver == IkSolver::Fabrik)
    {
        ScopedSolved = SolveFabrik(InSkeleton, InChain);
    }
    else
    {
        ScopedSolved = SolveTwoBone(InSkeleton, InChain);
    }
    if (ScopedSolved)
    {
        SkeletalAnimation::Evaluate(InSkeleton);
    }
    return ScopedSolved;
}

bool SolveIkGoal(SkeletonComponent& InSkeleton, const IkGoalComponent& InGoal) noexcept
{
    if (InGoal.Root < 0 || InGoal.Mid < 0 || InGoal.Tip < 0 || InGoal.Weight <= 0.0f)
    {
        return false;
    }
    const auto ScopedRoot = static_cast<std::uint16_t>(InGoal.Root);
    const auto ScopedMid = static_cast<std::uint16_t>(InGoal.Mid);
    const auto ScopedTip = static_cast<std::uint16_t>(InGoal.Tip);
    IkChain ScopedChain;
    if (InGoal.Solver == 1)
    {
        const std::uint16_t ScopedBones[3] = {ScopedRoot, ScopedMid, ScopedTip};
        if (!MakeFabrikChain(InSkeleton, ScopedBones, 3, InGoal.Target, &ScopedChain))
        {
            return false;
        }
    }
    else if (!MakeTwoBoneChain(InSkeleton, ScopedRoot, ScopedMid, ScopedTip, InGoal.Target, &ScopedChain))
    {
        return false;
    }
    ScopedChain.TipOffset = InGoal.TipOffset;
    ScopedChain.Weight = InGoal.Weight;
    ScopedChain.PoleAngle = InGoal.PoleAngle;
    ScopedChain.Iterations = static_cast<std::uint8_t>(std::clamp<std::int32_t>(InGoal.Iterations, 1, 32));
    return SolveIk(InSkeleton, ScopedChain);
}

std::uint32_t SolveIkComponent(SkeletonComponent& InSkeleton, const IkComponent& InComponent) noexcept
{
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(InComponent.ChainCount, kMaxIkChains);
    std::uint32_t ScopedSolved = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        if (SolveIk(InSkeleton, InComponent.Chains[ScopedI]))
        {
            ++ScopedSolved;
        }
    }
    return ScopedSolved;
}

void IkSystem(Ecs::SystemContext& InContext) noexcept
{
    auto ScopedView = InContext.registry.view<SkeletonComponent, IkComponent>();
    for (auto [ScopedEntity, ScopedSkeleton, ScopedIk] : ScopedView.each())
    {
        (void)ScopedEntity;
        (void)SolveIkComponent(ScopedSkeleton, ScopedIk);
    }
    // Authored goals are the serializable path. They run after the runtime chain
    // list so an inspector-authored goal wins over a code-built component.
    auto ScopedGoalView = InContext.registry.view<SkeletonComponent, IkGoalComponent>();
    for (auto [ScopedEntity, ScopedSkeleton, ScopedGoal] : ScopedGoalView.each())
    {
        (void)ScopedEntity;
        (void)SolveIkGoal(ScopedSkeleton, ScopedGoal);
    }
}

std::uint32_t BuildRetargetMap(const SkeletonComponent& InSource, const SkeletonComponent& InTarget,
                               float InGlobalScale, RetargetMap* OutMap) noexcept
{
    if (OutMap == nullptr)
    {
        return 0;
    }
    *OutMap = RetargetMap{};
    OutMap->GlobalScale = InGlobalScale > 0.0f ? InGlobalScale : 1.0f;
    std::uint32_t ScopedMatched = 0;
    for (std::uint16_t ScopedTargetBone = 0; ScopedTargetBone < InTarget.BoneCount; ++ScopedTargetBone)
    {
        const std::uint64_t ScopedHash = InTarget.BoneNameHashes[ScopedTargetBone];
        if (ScopedHash == 0)
        {
            continue;
        }
        const std::int16_t ScopedSourceBone = SkeletonFindBone(InSource, ScopedHash);
        if (ScopedSourceBone < 0)
        {
            continue;
        }
        if (OutMap->PairCount >= kMaxRetargetPairs)
        {
            break;
        }
        RetargetPair ScopedPair;
        ScopedPair.SourceHash = ScopedHash;
        ScopedPair.TargetHash = ScopedHash;
        const float ScopedSourceLength = Length(InSource.LocalPositions[static_cast<std::uint16_t>(ScopedSourceBone)]);
        const float ScopedTargetLength = Length(InTarget.LocalPositions[ScopedTargetBone]);
        if (ScopedSourceLength > kEpsilon)
        {
            ScopedPair.LengthScale = (ScopedTargetLength / ScopedSourceLength) * OutMap->GlobalScale;
        }
        else
        {
            ScopedPair.LengthScale = OutMap->GlobalScale;
        }
        OutMap->Pairs[OutMap->PairCount++] = ScopedPair;
        ++ScopedMatched;
    }
    return ScopedMatched;
}

std::uint32_t ApplyRetarget(const SkeletonComponent& InSource, const RetargetMap& InMap,
                            SkeletonComponent& InTarget) noexcept
{
    if (!InTarget.BindPoseValid || InTarget.BoneCount == 0)
    {
        return 0;
    }
    std::uint32_t ScopedApplied = 0;
    for (std::uint16_t ScopedTargetBone = 0; ScopedTargetBone < InTarget.BoneCount; ++ScopedTargetBone)
    {
        const std::uint64_t ScopedHash = InTarget.BoneNameHashes[ScopedTargetBone];
        if (ScopedHash == 0)
        {
            continue;
        }
        const std::int16_t ScopedSourceBone = SkeletonFindBone(InSource, ScopedHash);
        if (ScopedSourceBone < 0)
        {
            continue;
        }
        const std::uint16_t ScopedSource = static_cast<std::uint16_t>(ScopedSourceBone);
        const bool ScopedIsRoot = InTarget.ParentIndices[ScopedTargetBone] < 0;
        if (!ScopedIsRoot || InMap.CopyRootPosition)
        {
            float ScopedLengthScale = InMap.GlobalScale;
            for (std::uint32_t ScopedPair = 0; ScopedPair < InMap.PairCount; ++ScopedPair)
            {
                if (InMap.Pairs[ScopedPair].TargetHash == ScopedHash)
                {
                    ScopedLengthScale = InMap.Pairs[ScopedPair].LengthScale;
                    break;
                }
            }
            InTarget.LocalPositions[ScopedTargetBone] = InSource.LocalPositions[ScopedSource] * ScopedLengthScale;
        }
        float ScopedOffset = 0.0f;
        for (std::uint32_t ScopedPair = 0; ScopedPair < InMap.PairCount; ++ScopedPair)
        {
            if (InMap.Pairs[ScopedPair].TargetHash == ScopedHash)
            {
                ScopedOffset = InMap.Pairs[ScopedPair].RotationOffset;
                break;
            }
        }
        InTarget.LocalRotations[ScopedTargetBone] =
            WrapAngle(InSource.LocalRotations[ScopedSource] + ScopedOffset);
        InTarget.LocalScales[ScopedTargetBone] = InSource.LocalScales[ScopedSource];
        ++ScopedApplied;
    }
    SkeletalAnimation::Evaluate(InTarget);
    return ScopedApplied;
}

std::int16_t QuantizeRotation(float InRadians) noexcept
{
    const float ScopedValue = InRadians / kPoseRotationQuantum;
    const float ScopedClamped = std::clamp(ScopedValue, -32767.0f, 32767.0f);
    return static_cast<std::int16_t>(std::lround(ScopedClamped));
}

std::int16_t QuantizePosition(float InWorldUnits) noexcept
{
    const float ScopedValue = InWorldUnits / kPosePositionQuantum;
    const float ScopedClamped = std::clamp(ScopedValue, -32767.0f, 32767.0f);
    return static_cast<std::int16_t>(std::lround(ScopedClamped));
}

std::uint16_t QuantizeScale(float InScale) noexcept
{
    const float ScopedValue = InScale / kPoseScaleQuantum;
    const float ScopedClamped = std::clamp(ScopedValue, 0.0f, 65535.0f);
    return static_cast<std::uint16_t>(std::lround(ScopedClamped));
}

float DequantizeRotation(std::int16_t InValue) noexcept
{
    return static_cast<float>(InValue) * kPoseRotationQuantum;
}

float DequantizePosition(std::int16_t InValue) noexcept
{
    return static_cast<float>(InValue) * kPosePositionQuantum;
}

float DequantizeScale(std::uint16_t InValue) noexcept
{
    return static_cast<float>(InValue) * kPoseScaleQuantum;
}

void CompressPose(const SkeletonComponent& InSkeleton, QuantizedPose* OutPose) noexcept
{
    if (OutPose == nullptr)
    {
        return;
    }
    *OutPose = QuantizedPose{};
    const std::uint16_t ScopedCount = std::min(InSkeleton.BoneCount, kMaxBones);
    OutPose->BoneCount = ScopedCount;
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        OutPose->Rotations[ScopedI] = QuantizeRotation(InSkeleton.LocalRotations[ScopedI]);
        OutPose->PositionsX[ScopedI] = QuantizePosition(InSkeleton.LocalPositions[ScopedI].X);
        OutPose->PositionsY[ScopedI] = QuantizePosition(InSkeleton.LocalPositions[ScopedI].Y);
        OutPose->Scales[ScopedI] = QuantizeScale(InSkeleton.LocalScales[ScopedI]);
    }
}

bool DecompressPose(const QuantizedPose& InPose, SkeletonComponent* OutSkeleton) noexcept
{
    if (OutSkeleton == nullptr)
    {
        return false;
    }
    if (InPose.BoneCount > kMaxBones)
    {
        return false;
    }
    for (std::uint16_t ScopedI = 0; ScopedI < InPose.BoneCount; ++ScopedI)
    {
        OutSkeleton->LocalRotations[ScopedI] = DequantizeRotation(InPose.Rotations[ScopedI]);
        OutSkeleton->LocalPositions[ScopedI] =
            Vector2D{DequantizePosition(InPose.PositionsX[ScopedI]), DequantizePosition(InPose.PositionsY[ScopedI])};
        OutSkeleton->LocalScales[ScopedI] = DequantizeScale(InPose.Scales[ScopedI]);
    }
    SkeletalAnimation::Evaluate(*OutSkeleton);
    return true;
}
}
