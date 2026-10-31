// FemboyEngine2D - skeletal IK, retargeting and pose compression.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Skeleton.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby::Skeletal
{

inline constexpr std::uint32_t kMaxIkChains = 4;
inline constexpr std::uint32_t kMaxIkBones = 16;
inline constexpr std::uint32_t kMaxRetargetPairs = 64;

enum class IkSolver : std::uint8_t
{
    TwoBone = 0,
    Fabrik,
    Count
};

[[nodiscard]] constexpr const char* IkSolverName(IkSolver InSolver) noexcept
{
    switch (InSolver)
    {
        case IkSolver::TwoBone:
            return "two-bone";
        case IkSolver::Fabrik:
            return "fabrik";
        case IkSolver::Count:
            break;
    }
    return "?";
}

// One IK chain. `Bones` runs root -> ... -> tip in parent-before-child order and
// must be a real parent chain of the skeleton. TwoBone uses exactly three bones;
// Fabrik uses `BoneCount` bones and ignores the middle ordering beyond the chain.
struct IkChain
{
    std::array<std::uint16_t, kMaxIkBones> Bones{};
    std::uint16_t BoneCount = 0;
    // World-space goal of the tip, plus an offset applied in the tip's local
    // space (useful to plant a foot at the sole instead of the ankle).
    Vector2D Target{};
    Vector2D TipOffset{};
    // 0 = ignored, 1 = full pose override. Values in between blend with the
    // animated local rotations.
    float Weight = 1.0f;
    // Bend direction: positive bends the middle joint counter-clockwise.
    float PoleAngle = 0.0f;
    std::uint8_t Iterations = 8;
    IkSolver Solver = IkSolver::TwoBone;
    std::uint8_t Flags = 0;
};

// Up to four chains per skeleton. Solving runs in place on the skeleton's local
// rotations and re-evaluates the hierarchy, so it must run after the animation
// system and before the render extract.
struct IkComponent
{
    std::array<IkChain, kMaxIkChains> Chains{};
    std::uint32_t ChainCount = 0;
};

static_assert(std::is_trivially_copyable_v<IkChain>);
static_assert(std::is_trivially_copyable_v<IkComponent>);

// Authored single-chain goal. This is the reflected, scene-serializable form of
// an IK target: the solver still works on the fixed `IkChain` POD, so nothing
// allocation-bearing enters the component. Bone indices are int32 with -1 =
// unused, which mirrors the inspector and the scene writer.
struct IkGoalComponent
{
    std::int32_t Root = -1;
    std::int32_t Mid = -1;
    std::int32_t Tip = -1;
    Vector2D Target{};
    Vector2D TipOffset{};
    float Weight = 1.0f;
    float PoleAngle = 0.0f;
    std::int32_t Iterations = 8;
    // 0 = two-bone analytic, 1 = FABRIK
    std::int32_t Solver = 0;
};

static_assert(std::is_trivially_copyable_v<IkGoalComponent>);
static_assert(std::is_standard_layout_v<IkGoalComponent>);

// Builds the solver chain from an authored goal and solves it. Returns false for
// an invalid bone triple or a zero-weight goal.
[[nodiscard]] bool SolveIkGoal(SkeletonComponent& InSkeleton, const IkGoalComponent& InGoal) noexcept;

// Fills a two-bone chain for bones `InRoot`, `InMid`, `InTip` and validates that
// they form a parent chain.
[[nodiscard]] bool MakeTwoBoneChain(const SkeletonComponent& InSkeleton, std::uint16_t InRoot, std::uint16_t InMid,
                                    std::uint16_t InTip, Vector2D InTarget, IkChain* OutChain) noexcept;

// Fills a Fabrik chain from a bone list, validating the parent chain.
[[nodiscard]] bool MakeFabrikChain(const SkeletonComponent& InSkeleton, const std::uint16_t* InBones,
                                   std::uint16_t InBoneCount, Vector2D InTarget, IkChain* OutChain) noexcept;

// Solves one chain in place and re-evaluates the skeleton hierarchy.
[[nodiscard]] bool SolveIk(SkeletonComponent& InSkeleton, const IkChain& InChain) noexcept;

// Solves every chain of the component. Returns the number of chains solved.
[[nodiscard]] std::uint32_t SolveIkComponent(SkeletonComponent& InSkeleton, const IkComponent& InComponent) noexcept;

// System entry point: solves every entity with SkeletonComponent + IkComponent.
void IkSystem(Ecs::SystemContext& InContext) noexcept;

// Retargeting

struct RetargetPair
{
    std::uint64_t SourceHash = 0;
    std::uint64_t TargetHash = 0;
    // Uniform scale applied to the target's local positions.
    float LengthScale = 1.0f;
    // Added to the copied local rotation.
    float RotationOffset = 0.0f;
    // Position offset in the target's local space.
    Vector2D PositionOffset{};
    bool HasPositionOffset = false;
};

struct RetargetMap
{
    std::array<RetargetPair, kMaxRetargetPairs> Pairs{};
    std::uint32_t PairCount = 0;
    // Extra uniform scale for the whole rig (source -> target units).
    float GlobalScale = 1.0f;
    // Copy the root bone's local position from the source. Root motion transfer
    // is usually wanted, but a re-used rig sometimes must stay at its origin.
    bool CopyRootPosition = true;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<RetargetMap>);

// Builds a map by matching bone name hashes. Pairs are emitted in target bone
// order; unmatched target bones keep their authored pose. Returns the number of
// matched bones.
[[nodiscard]] std::uint32_t BuildRetargetMap(const SkeletonComponent& InSource, const SkeletonComponent& InTarget,
                                             float InGlobalScale, RetargetMap* OutMap) noexcept;

// Applies the map to a target skeleton and re-evaluates it. The source skeleton
// must already be evaluated (the caller runs SkeletalAnimation::Evaluate first).
[[nodiscard]] std::uint32_t ApplyRetarget(const SkeletonComponent& InSource, const RetargetMap& InMap,
                                          SkeletonComponent& InTarget) noexcept;

// Pose compression

// Quantized local pose. Rotations are int16 in units of 1e-4 rad, positions are
// int16 in units of 1/16 world unit and scales are uint16 in units of 1e-4.
// A 128-bone pose is 1 KiB instead of the full-precision 10 KiB component.
inline constexpr float kPoseRotationQuantum = 1.0e-4f;
inline constexpr float kPosePositionQuantum = 1.0f / 16.0f;
inline constexpr float kPoseScaleQuantum = 1.0e-4f;

struct QuantizedPose
{
    std::array<std::int16_t, kMaxBones> Rotations{};
    std::array<std::int16_t, kMaxBones> PositionsX{};
    std::array<std::int16_t, kMaxBones> PositionsY{};
    std::array<std::uint16_t, kMaxBones> Scales{};
    std::uint16_t BoneCount = 0;
    std::uint16_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<QuantizedPose>);

[[nodiscard]] std::int16_t QuantizeRotation(float InRadians) noexcept;
[[nodiscard]] std::int16_t QuantizePosition(float InWorldUnits) noexcept;
[[nodiscard]] std::uint16_t QuantizeScale(float InScale) noexcept;
[[nodiscard]] float DequantizeRotation(std::int16_t InValue) noexcept;
[[nodiscard]] float DequantizePosition(std::int16_t InValue) noexcept;
[[nodiscard]] float DequantizeScale(std::uint16_t InValue) noexcept;

void CompressPose(const SkeletonComponent& InSkeleton, QuantizedPose* OutPose) noexcept;
[[nodiscard]] bool DecompressPose(const QuantizedPose& InPose, SkeletonComponent* OutSkeleton) noexcept;

// Worst-case error the quantized pose can introduce.
[[nodiscard]] constexpr float PoseRotationTolerance() noexcept
{
    return kPoseRotationQuantum;
}
[[nodiscard]] constexpr float PosePositionTolerance() noexcept
{
    return kPosePositionQuantum;
}
}

FBY_REFLECT_COMPONENT(Fby::Skeletal::IkGoalComponent, "IkGoalComponent",
                      FBY_FIELD(Fby::Skeletal::IkGoalComponent, Root, Int32),
                      FBY_FIELD(Fby::Skeletal::IkGoalComponent, Mid, Int32),
                      FBY_FIELD(Fby::Skeletal::IkGoalComponent, Tip, Int32),
                      FBY_FIELD(Fby::Skeletal::IkGoalComponent, Target, Vector2D),
                      FBY_FIELD(Fby::Skeletal::IkGoalComponent, TipOffset, Vector2D),
                      FBY_FIELD(Fby::Skeletal::IkGoalComponent, Weight, Float),
                      FBY_FIELD(Fby::Skeletal::IkGoalComponent, PoleAngle, Float),
                      FBY_FIELD(Fby::Skeletal::IkGoalComponent, Iterations, Int32),
                      FBY_FIELD(Fby::Skeletal::IkGoalComponent, Solver, Int32))
