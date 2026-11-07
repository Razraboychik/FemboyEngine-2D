// FemboyEngine2D - data-oriented fallback physics backend.
#include "Engine/Physics/DodPhysicsBackend.hpp"

#include "Engine/ECS/Presentation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fby
{
namespace
{
// Frames below the linear threshold before a body is treated as asleep. At the
// 60 Hz default this is two seconds of rest.
constexpr std::uint32_t kSleepFrameThreshold = 120;
constexpr std::uint32_t kInvalidBody = std::numeric_limits<std::uint32_t>::max();
}

std::uint64_t DodPhysicsBackend::PairKey(Entity InA, Entity InB) noexcept
{
    const std::uint32_t ScopedA = entt::to_integral(InA);
    const std::uint32_t ScopedB = entt::to_integral(InB);
    return (static_cast<std::uint64_t>(std::min(ScopedA, ScopedB)) << 32) | std::max(ScopedA, ScopedB);
}

Entity DodPhysicsBackend::EntityFromKey(std::uint32_t InRaw) noexcept
{
    return static_cast<Entity>(InRaw);
}

void DodPhysicsBackend::ClearColumns() noexcept
{
    MColumns.Entities.clear();
    MColumns.Positions.clear();
    MColumns.PreviousPositions.clear();
    MColumns.Velocities.clear();
    MColumns.Accelerations.clear();
    MColumns.Rotations.clear();
    MColumns.PreviousRotations.clear();
    MColumns.AngularVelocities.clear();
    MColumns.InverseMasses.clear();
    MColumns.GravityScales.clear();
    MColumns.HalfWidths.clear();
    MColumns.HalfHeights.clear();
    MColumns.Categories.clear();
    MColumns.Masks.clear();
    MColumns.Flags.clear();
    MColumns.Types.clear();
    MColumns.Sensors.clear();
    MColumns.ContactEvents.clear();
    MColumns.SleepFrames.clear();
}

std::uint32_t DodPhysicsBackend::AcquireBodySlot() noexcept
{
    if (!MFreeIndices.empty())
    {
        const std::uint32_t ScopedIndex = MFreeIndices.back();
        MFreeIndices.pop_back();
        ResetBodySlot(ScopedIndex);
        return ScopedIndex;
    }
    const std::uint32_t ScopedIndex = BodyCount();
    MColumns.Entities.push_back(entt::null);
    MColumns.Positions.push_back(Vector2D{});
    MColumns.PreviousPositions.push_back(Vector2D{});
    MColumns.Velocities.push_back(Vector2D{});
    MColumns.Accelerations.push_back(Vector2D{});
    MColumns.Rotations.push_back(0.0f);
    MColumns.PreviousRotations.push_back(0.0f);
    MColumns.AngularVelocities.push_back(0.0f);
    MColumns.InverseMasses.push_back(0.0f);
    MColumns.GravityScales.push_back(1.0f);
    MColumns.HalfWidths.push_back(0.5f);
    MColumns.HalfHeights.push_back(0.5f);
    MColumns.Categories.push_back(1u);
    MColumns.Masks.push_back(0xFFFFFFFFu);
    MColumns.Flags.push_back(0u);
    MColumns.Types.push_back(BodyType::Dynamic);
    MColumns.Sensors.push_back(0u);
    MColumns.ContactEvents.push_back(1u);
    MColumns.SleepFrames.push_back(0u);
    return ScopedIndex;
}

void DodPhysicsBackend::ResetBodySlot(std::uint32_t InIndex) noexcept
{
    MColumns.Entities[InIndex] = entt::null;
    MColumns.Positions[InIndex] = Vector2D{};
    MColumns.PreviousPositions[InIndex] = Vector2D{};
    MColumns.Velocities[InIndex] = Vector2D{};
    MColumns.Accelerations[InIndex] = Vector2D{};
    MColumns.Rotations[InIndex] = 0.0f;
    MColumns.PreviousRotations[InIndex] = 0.0f;
    MColumns.AngularVelocities[InIndex] = 0.0f;
    MColumns.InverseMasses[InIndex] = 0.0f;
    MColumns.GravityScales[InIndex] = 1.0f;
    MColumns.HalfWidths[InIndex] = 0.5f;
    MColumns.HalfHeights[InIndex] = 0.5f;
    MColumns.Categories[InIndex] = 1u;
    MColumns.Masks[InIndex] = 0xFFFFFFFFu;
    // A free slot must not simulate or enter the broadphase: an enabled default
    // body at the origin would collide with real bodies until reuse.
    MColumns.Flags[InIndex] = 0u;
    MColumns.Types[InIndex] = BodyType::Dynamic;
    MColumns.Sensors[InIndex] = 0u;
    MColumns.ContactEvents[InIndex] = 1u;
    MColumns.SleepFrames[InIndex] = 0u;
}

Vector2D DodPhysicsBackend::MinCorner(std::uint32_t InIndex) const noexcept
{
    return Vector2D{MColumns.Positions[InIndex].X - MColumns.HalfWidths[InIndex],
                    MColumns.Positions[InIndex].Y - MColumns.HalfHeights[InIndex]};
}

Vector2D DodPhysicsBackend::MaxCorner(std::uint32_t InIndex) const noexcept
{
    return Vector2D{MColumns.Positions[InIndex].X + MColumns.HalfWidths[InIndex],
                    MColumns.Positions[InIndex].Y + MColumns.HalfHeights[InIndex]};
}

bool DodPhysicsBackend::Overlaps(std::uint32_t InA, std::uint32_t InB) const noexcept
{
    return std::abs(MColumns.Positions[InA].X - MColumns.Positions[InB].X) <=
               MColumns.HalfWidths[InA] + MColumns.HalfWidths[InB] &&
           std::abs(MColumns.Positions[InA].Y - MColumns.Positions[InB].Y) <=
               MColumns.HalfHeights[InA] + MColumns.HalfHeights[InB];
}

bool DodPhysicsBackend::Initialize(const PhysicsDesc& InDesc)
{
    MDesc = InDesc;
    MEvents.clear();
    MEvents.reserve(std::max<std::uint32_t>(InDesc.maxContactEvents, 1u));
    ClearColumns();
    MColumns.Entities.reserve(4096);
    MColumns.Positions.reserve(4096);
    MColumns.Velocities.reserve(4096);
    MColumns.PreviousPositions.reserve(4096);
    MColumns.Accelerations.reserve(4096);
    MColumns.Rotations.reserve(4096);
    MColumns.PreviousRotations.reserve(4096);
    MColumns.AngularVelocities.reserve(4096);
    MColumns.InverseMasses.reserve(4096);
    MColumns.GravityScales.reserve(4096);
    MColumns.HalfWidths.reserve(4096);
    MColumns.HalfHeights.reserve(4096);
    MColumns.Categories.reserve(4096);
    MColumns.Masks.reserve(4096);
    MColumns.Flags.reserve(4096);
    MColumns.Types.reserve(4096);
    MColumns.Sensors.reserve(4096);
    MColumns.ContactEvents.reserve(4096);
    MColumns.SleepFrames.reserve(4096);
    MBodyTimeScale.clear();
    MFreeIndices.clear();
    MEntityToIndex.clear();
    MGrid.clear();
    MPairKeys.clear();
    MCurrentContacts.clear();
    MPreviousPairKeys.clear();
    MEventCount = 0;
    MSequence = 0;
    MAliveCount = 0;
    MInitialized = true;
    return true;
}

void DodPhysicsBackend::Shutdown()
{
    ClearColumns();
    MBodyTimeScale.clear();
    MFreeIndices.clear();
    MEntityToIndex.clear();
    MGrid.clear();
    MPairKeys.clear();
    MCurrentContacts.clear();
    MPreviousPairKeys.clear();
    MEvents.clear();
    MEventCount = 0;
    MAliveCount = 0;
    MInitialized = false;
}

void DodPhysicsBackend::PruneDeadBodies(Ecs::Registry& InRegistry)
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < BodyCount(); ++ScopedIndex)
    {
        const Entity ScopedEntity = MColumns.Entities[ScopedIndex];
        if (ScopedEntity == entt::null)
        {
            continue;
        }
        if (!InRegistry.valid(ScopedEntity))
        {
            MEntityToIndex.erase(ScopedEntity);
            ResetBodySlot(ScopedIndex);
            MFreeIndices.push_back(ScopedIndex);
            if (MAliveCount != 0)
            {
                --MAliveCount;
            }
        }
    }
}

void DodPhysicsBackend::EnsureBodies(Ecs::Registry& InRegistry)
{
    PruneDeadBodies(InRegistry);
    auto ScopedView = InRegistry.view<RigidBody2D, Collider2D, Transform2D>();
    for (auto [ScopedEntity, ScopedBody, ScopedCollider, ScopedTransform] : ScopedView.each())
    {
        std::uint32_t ScopedExisting = kInvalidBody;
        if (ScopedBody.Body != 0 && ScopedBody.Body <= static_cast<std::uint64_t>(BodyCount()))
        {
            const auto ScopedCandidate = static_cast<std::uint32_t>(ScopedBody.Body - 1u);
            if (MColumns.Entities[ScopedCandidate] == ScopedEntity)
            {
                ScopedExisting = ScopedCandidate;
            }
        }
        if (ScopedExisting == kInvalidBody)
        {
            const auto ScopedFound = MEntityToIndex.find(ScopedEntity);
            if (ScopedFound != MEntityToIndex.end())
            {
                ScopedExisting = ScopedFound->second;
            }
        }
        if (ScopedExisting != kInvalidBody)
        {
            // A record found through the map may not carry the packed handle yet;
            // repairing it avoids a map lookup on every later step.
            ScopedBody.Body = static_cast<std::uint64_t>(ScopedExisting) + 1u;
            if ((ScopedBody.Flags & RigidBodyTeleport) != 0)
            {
                MColumns.Positions[ScopedExisting] = ScopedTransform.Position;
                MColumns.PreviousPositions[ScopedExisting] = ScopedTransform.Position;
                MColumns.Rotations[ScopedExisting] = ScopedTransform.Rotation;
                MColumns.PreviousRotations[ScopedExisting] = ScopedTransform.Rotation;
                ScopedBody.Flags &= ~RigidBodyTeleport;
            }
            MColumns.Types[ScopedExisting] = ScopedBody.Type;
            MColumns.Flags[ScopedExisting] = ScopedBody.Flags;
            MColumns.GravityScales[ScopedExisting] = ScopedBody.GravityScale;
            MColumns.HalfWidths[ScopedExisting] = std::max(ScopedCollider.HalfExtents.X, 0.001f);
            MColumns.HalfHeights[ScopedExisting] = std::max(ScopedCollider.HalfExtents.Y, 0.001f);
            MColumns.Categories[ScopedExisting] = ScopedCollider.CategoryBits;
            MColumns.Masks[ScopedExisting] = ScopedCollider.MaskBits;
            MColumns.Sensors[ScopedExisting] = ScopedCollider.Sensor ? 1u : 0u;
            MColumns.ContactEvents[ScopedExisting] = ScopedCollider.ContactEvents ? 1u : 0u;
            continue;
        }

        const std::uint32_t ScopedIndex = AcquireBodySlot();
        MColumns.Entities[ScopedIndex] = ScopedEntity;
        MColumns.Positions[ScopedIndex] = ScopedTransform.Position;
        MColumns.PreviousPositions[ScopedIndex] = ScopedTransform.Position;
        MColumns.Rotations[ScopedIndex] = ScopedTransform.Rotation;
        MColumns.PreviousRotations[ScopedIndex] = ScopedTransform.Rotation;
        MColumns.Velocities[ScopedIndex] = ScopedBody.LinearVelocity;
        MColumns.AngularVelocities[ScopedIndex] = ScopedBody.AngularVelocity;
        MColumns.Types[ScopedIndex] = ScopedBody.Type;
        MColumns.Flags[ScopedIndex] = ScopedBody.Flags;
        MColumns.GravityScales[ScopedIndex] = ScopedBody.GravityScale;
        MColumns.HalfWidths[ScopedIndex] = std::max(ScopedCollider.HalfExtents.X, 0.001f);
        MColumns.HalfHeights[ScopedIndex] = std::max(ScopedCollider.HalfExtents.Y, 0.001f);
        MColumns.Categories[ScopedIndex] = ScopedCollider.CategoryBits;
        MColumns.Masks[ScopedIndex] = ScopedCollider.MaskBits;
        MColumns.Sensors[ScopedIndex] = ScopedCollider.Sensor ? 1u : 0u;
        MColumns.ContactEvents[ScopedIndex] = ScopedCollider.ContactEvents ? 1u : 0u;
        MColumns.InverseMasses[ScopedIndex] =
            ScopedBody.Type == BodyType::Dynamic ? 1.0f / std::max(ScopedCollider.Density, 0.001f) : 0.0f;
        MEntityToIndex[ScopedEntity] = ScopedIndex;
        ScopedBody.Body = static_cast<std::uint64_t>(ScopedIndex) + 1u;
        ScopedCollider.Shape = 0;
        ++MAliveCount;
    }
}

void DodPhysicsBackend::Step(Ecs::Registry& InRegistry, float InFixedDt)
{
    if (!MInitialized)
    {
        return;
    }
    EnsureBodies(InRegistry);
    MEventCount = 0;
    MEvents.clear();
    MCurrentContacts.clear();

    // Per-entity time scale is sparse; only entities that own the component pay
    // for a lookup, and the integration loop stays a flat array read.
    MBodyTimeScale.assign(BodyCount(), 1.0f);
    auto ScopedTimeScaleView = InRegistry.view<TimeScaleComponent>();
    for (auto [ScopedEntity, ScopedTimeScale] : ScopedTimeScaleView.each())
    {
        const std::uint32_t ScopedIndex = FindBody(ScopedEntity);
        if (ScopedIndex != kInvalidBody)
        {
            MBodyTimeScale[ScopedIndex] = ScopedTimeScale.Clamped();
        }
    }

    const float ScopedDt = std::max(InFixedDt, 0.0f);
    const Vector2D ScopedGravity = MDesc.gravity * MDesc.pixelsPerMeter;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < BodyCount(); ++ScopedIndex)
    {
        if (MColumns.Entities[ScopedIndex] == entt::null)
        {
            continue;
        }
        MColumns.PreviousPositions[ScopedIndex] = MColumns.Positions[ScopedIndex];
        MColumns.PreviousRotations[ScopedIndex] = MColumns.Rotations[ScopedIndex];
        const float ScopedScale = MBodyTimeScale[ScopedIndex];
        if (ScopedScale <= 0.0f)
        {
            // A frozen body neither integrates nor keeps motion; the counter is
            // reset so a later thaw starts moving immediately.
            MColumns.Velocities[ScopedIndex] = Vector2D{};
            MColumns.AngularVelocities[ScopedIndex] = 0.0f;
            MColumns.Accelerations[ScopedIndex] = Vector2D{};
            MColumns.SleepFrames[ScopedIndex] = 0;
            continue;
        }
        if (MColumns.Types[ScopedIndex] != BodyType::Dynamic ||
            (MColumns.Flags[ScopedIndex] & RigidBodyEnabled) == 0)
        {
            MColumns.Accelerations[ScopedIndex] = Vector2D{};
            continue;
        }
        // A resting body keeps its pose until an impulse, a velocity change or a
        // contact in Resolve resets the counter.
        if (MColumns.SleepFrames[ScopedIndex] >= kSleepFrameThreshold)
        {
            MColumns.Velocities[ScopedIndex] = Vector2D{};
            MColumns.AngularVelocities[ScopedIndex] = 0.0f;
            MColumns.Accelerations[ScopedIndex] = Vector2D{};
            continue;
        }
        const float ScopedStepDt = ScopedDt * ScopedScale;
        MColumns.Velocities[ScopedIndex] +=
            (ScopedGravity * MColumns.GravityScales[ScopedIndex] + MColumns.Accelerations[ScopedIndex]) *
            ScopedStepDt;
        MColumns.Accelerations[ScopedIndex] = Vector2D{};
        MColumns.Positions[ScopedIndex] += MColumns.Velocities[ScopedIndex] * ScopedStepDt;
        MColumns.Rotations[ScopedIndex] += MColumns.AngularVelocities[ScopedIndex] * ScopedStepDt;
    }

    constexpr float kCellSize = 64.0f;
    const auto ScopedCellKey = [](std::int32_t InX, std::int32_t InY) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(InX)) << 32) |
               static_cast<std::uint32_t>(InY);
    };

    MGrid.clear();
    MGrid.reserve(BodyCount());
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < BodyCount(); ++ScopedIndex)
    {
        if (MColumns.Entities[ScopedIndex] == entt::null ||
            (MColumns.Flags[ScopedIndex] & RigidBodyEnabled) == 0)
        {
            continue;
        }
        const Vector2D ScopedMin = MinCorner(ScopedIndex);
        const Vector2D ScopedMax = MaxCorner(ScopedIndex);
        const std::int32_t ScopedMinX = static_cast<std::int32_t>(std::floor(ScopedMin.X / kCellSize));
        const std::int32_t ScopedMaxX = static_cast<std::int32_t>(std::floor(ScopedMax.X / kCellSize));
        const std::int32_t ScopedMinY = static_cast<std::int32_t>(std::floor(ScopedMin.Y / kCellSize));
        const std::int32_t ScopedMaxY = static_cast<std::int32_t>(std::floor(ScopedMax.Y / kCellSize));
        for (std::int32_t ScopedX = ScopedMinX; ScopedX <= ScopedMaxX; ++ScopedX)
        {
            for (std::int32_t ScopedY = ScopedMinY; ScopedY <= ScopedMaxY; ++ScopedY)
            {
                MGrid.push_back(GridEntry{ScopedCellKey(ScopedX, ScopedY), ScopedIndex});
            }
        }
    }
    std::sort(MGrid.begin(), MGrid.end(), [](const GridEntry& InA, const GridEntry& InB)
              { return InA.key != InB.key ? InA.key < InB.key : InA.index < InB.index; });

    MPairKeys.clear();
    for (std::size_t ScopedBegin = 0; ScopedBegin < MGrid.size();)
    {
        std::size_t ScopedEnd = ScopedBegin + 1;
        while (ScopedEnd < MGrid.size() && MGrid[ScopedEnd].key == MGrid[ScopedBegin].key)
        {
            ++ScopedEnd;
        }
        for (std::size_t ScopedI = ScopedBegin; ScopedI < ScopedEnd; ++ScopedI)
        {
            const std::uint32_t ScopedA = MGrid[ScopedI].index;
            for (std::size_t ScopedJ = ScopedI + 1; ScopedJ < ScopedEnd; ++ScopedJ)
            {
                const std::uint32_t ScopedB = MGrid[ScopedJ].index;
                if (ScopedA == ScopedB || (MColumns.Categories[ScopedA] & MColumns.Masks[ScopedB]) == 0 ||
                    (MColumns.Categories[ScopedB] & MColumns.Masks[ScopedA]) == 0)
                {
                    continue;
                }
                const std::uint32_t ScopedLow = std::min(ScopedA, ScopedB);
                const std::uint32_t ScopedHigh = std::max(ScopedA, ScopedB);
                MPairKeys.push_back((static_cast<std::uint64_t>(ScopedLow) << 32) | ScopedHigh);
            }
        }
        ScopedBegin = ScopedEnd;
    }
    std::sort(MPairKeys.begin(), MPairKeys.end());
    MPairKeys.erase(std::unique(MPairKeys.begin(), MPairKeys.end()), MPairKeys.end());
    for (const std::uint64_t ScopedPair : MPairKeys)
    {
        const auto ScopedA = static_cast<std::uint32_t>(ScopedPair >> 32);
        const auto ScopedB = static_cast<std::uint32_t>(ScopedPair & 0xFFFFFFFFu);
        if (MColumns.Types[ScopedA] == BodyType::Dynamic || MColumns.Types[ScopedB] == BodyType::Dynamic)
        {
            Resolve(ScopedA, ScopedB);
        }
    }
    EmitContactEvents();

    // Sleep is decided after the solver ran: a body resting on the ground still
    // gains one step of gravity velocity during integration, so measuring it
    // before Resolve would keep every grounded body awake forever.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < BodyCount(); ++ScopedIndex)
    {
        if (MColumns.Entities[ScopedIndex] == entt::null ||
            MColumns.Types[ScopedIndex] != BodyType::Dynamic ||
            (MColumns.Flags[ScopedIndex] & RigidBodyEnabled) == 0)
        {
            continue;
        }
        const float ScopedSpeed =
            std::sqrt(MColumns.Velocities[ScopedIndex].Dot(MColumns.Velocities[ScopedIndex]));
        if (ScopedSpeed < 1.0f && std::abs(MColumns.AngularVelocities[ScopedIndex]) < 0.05f)
        {
            ++MColumns.SleepFrames[ScopedIndex];
        }
        else
        {
            MColumns.SleepFrames[ScopedIndex] = 0;
        }
    }

    auto ScopedView = InRegistry.view<RigidBody2D, Transform2D>();
    for (auto [ScopedEntity, ScopedBody, ScopedTransform] : ScopedView.each())
    {
        std::uint32_t ScopedIndex = kInvalidBody;
        if (ScopedBody.Body != 0 && ScopedBody.Body <= static_cast<std::uint64_t>(BodyCount()))
        {
            const auto ScopedCandidate = static_cast<std::uint32_t>(ScopedBody.Body - 1u);
            if (MColumns.Entities[ScopedCandidate] == ScopedEntity)
            {
                ScopedIndex = ScopedCandidate;
            }
        }
        if (ScopedIndex == kInvalidBody)
        {
            ScopedIndex = FindBody(ScopedEntity);
        }
        if (ScopedIndex == kInvalidBody)
        {
            continue;
        }
        ScopedBody.Position = MColumns.Positions[ScopedIndex];
        ScopedBody.PreviousPosition = MColumns.PreviousPositions[ScopedIndex];
        ScopedBody.Rotation = MColumns.Rotations[ScopedIndex];
        ScopedBody.PreviousRotation = MColumns.PreviousRotations[ScopedIndex];
        ScopedBody.LinearVelocity = MColumns.Velocities[ScopedIndex];
        ScopedBody.AngularVelocity = MColumns.AngularVelocities[ScopedIndex];
        ScopedTransform.Position = MColumns.Positions[ScopedIndex];
        ScopedTransform.Rotation = MColumns.Rotations[ScopedIndex];
    }
}

void DodPhysicsBackend::Interpolate(Ecs::Registry& InRegistry, float InAlpha) const
{
    const float ScopedAlpha = std::clamp(InAlpha, 0.0f, 1.0f);
    auto ScopedView = InRegistry.view<RigidBody2D, Transform2D>();
    for (auto [ScopedEntity, ScopedBody, ScopedTransform] : ScopedView.each())
    {
        std::uint32_t ScopedIndex = kInvalidBody;
        if (ScopedBody.Body != 0 && ScopedBody.Body <= static_cast<std::uint64_t>(BodyCount()))
        {
            const auto ScopedCandidate = static_cast<std::uint32_t>(ScopedBody.Body - 1u);
            if (MColumns.Entities[ScopedCandidate] == ScopedEntity)
            {
                ScopedIndex = ScopedCandidate;
            }
        }
        if (ScopedIndex == kInvalidBody)
        {
            ScopedIndex = FindBody(ScopedEntity);
        }
        if (ScopedIndex == kInvalidBody)
        {
            continue;
        }
        ScopedTransform.Position =
            MColumns.PreviousPositions[ScopedIndex] +
            (MColumns.Positions[ScopedIndex] - MColumns.PreviousPositions[ScopedIndex]) * ScopedAlpha;
        ScopedTransform.Rotation =
            MColumns.PreviousRotations[ScopedIndex] +
            (MColumns.Rotations[ScopedIndex] - MColumns.PreviousRotations[ScopedIndex]) * ScopedAlpha;
        ScopedBody.Position = MColumns.Positions[ScopedIndex];
        ScopedBody.PreviousPosition = MColumns.PreviousPositions[ScopedIndex];
        ScopedBody.Rotation = MColumns.Rotations[ScopedIndex];
        ScopedBody.PreviousRotation = MColumns.PreviousRotations[ScopedIndex];
    }
}

void DodPhysicsBackend::Resolve(std::uint32_t InA, std::uint32_t InB) noexcept
{
    if (!Overlaps(InA, InB))
    {
        return;
    }
    if (MColumns.Sensors[InA] != 0 || MColumns.Sensors[InB] != 0)
    {
        if (MColumns.ContactEvents[InA] != 0 || MColumns.ContactEvents[InB] != 0)
        {
            MCurrentContacts.push_back(ContactRecord{PairKey(MColumns.Entities[InA], MColumns.Entities[InB]),
                                                     (MColumns.Positions[InA] + MColumns.Positions[InB]) * 0.5f,
                                                     Vector2D{0.0f, -1.0f}, 0.0f});
        }
        return;
    }

    const Vector2D ScopedDelta = MColumns.Positions[InB] - MColumns.Positions[InA];
    const float ScopedOverlapX =
        MColumns.HalfWidths[InA] + MColumns.HalfWidths[InB] - std::abs(ScopedDelta.X);
    const float ScopedOverlapY =
        MColumns.HalfHeights[InA] + MColumns.HalfHeights[InB] - std::abs(ScopedDelta.Y);
    if (ScopedOverlapX <= 0.0f || ScopedOverlapY <= 0.0f)
    {
        return;
    }

    Vector2D ScopedNormal{0.0f, 0.0f};
    float ScopedDepth = 0.0f;
    if (ScopedOverlapX < ScopedOverlapY)
    {
        ScopedNormal.X = ScopedDelta.X < 0.0f ? -1.0f : 1.0f;
        ScopedDepth = ScopedOverlapX;
    }
    else
    {
        ScopedNormal.Y = ScopedDelta.Y < 0.0f ? -1.0f : 1.0f;
        ScopedDepth = ScopedOverlapY;
    }

    const float ScopedInvMassSum = MColumns.InverseMasses[InA] + MColumns.InverseMasses[InB];
    if (ScopedInvMassSum <= 0.0f)
    {
        return;
    }
    const Vector2D ScopedCorrection = ScopedNormal * (ScopedDepth / ScopedInvMassSum);
    MColumns.Positions[InA] -= ScopedCorrection * MColumns.InverseMasses[InA];
    MColumns.Positions[InB] += ScopedCorrection * MColumns.InverseMasses[InB];

    const Vector2D ScopedRelative = MColumns.Velocities[InB] - MColumns.Velocities[InA];
    const float ScopedNormalSpeed = ScopedRelative.Dot(ScopedNormal);
    if (ScopedNormalSpeed < 0.0f)
    {
        const float ScopedImpulse = -ScopedNormalSpeed / ScopedInvMassSum;
        MColumns.Velocities[InA] -= ScopedNormal * (ScopedImpulse * MColumns.InverseMasses[InA]);
        MColumns.Velocities[InB] += ScopedNormal * (ScopedImpulse * MColumns.InverseMasses[InB]);
    }
    if (MColumns.ContactEvents[InA] != 0 || MColumns.ContactEvents[InB] != 0)
    {
        MCurrentContacts.push_back(ContactRecord{PairKey(MColumns.Entities[InA], MColumns.Entities[InB]),
                                                 (MColumns.Positions[InA] + MColumns.Positions[InB]) * 0.5f,
                                                 ScopedNormal, ScopedNormalSpeed});
    }
    // Only a real impact wakes a resting body. The per-step gravity correction
    // also produces a small negative normal speed, and waking on it would prevent
    // a body that rests on the ground from ever sleeping.
    constexpr float kWakeImpactSpeed = 32.0f;
    if (ScopedNormalSpeed < -kWakeImpactSpeed)
    {
        if (MColumns.Types[InA] == BodyType::Dynamic)
        {
            MColumns.SleepFrames[InA] = 0;
        }
        if (MColumns.Types[InB] == BodyType::Dynamic)
        {
            MColumns.SleepFrames[InB] = 0;
        }
    }
}

void DodPhysicsBackend::EmitContactEvents() noexcept
{
    std::sort(MCurrentContacts.begin(), MCurrentContacts.end(),
              [](const ContactRecord& InLeft, const ContactRecord& InRight) { return InLeft.key < InRight.key; });
    const auto ScopedHasCurrent = [this](std::uint64_t InKey) noexcept
    {
        const auto ScopedIt = std::lower_bound(
            MCurrentContacts.begin(), MCurrentContacts.end(), InKey,
            [](const ContactRecord& InRecord, std::uint64_t InValue) { return InRecord.key < InValue; });
        return ScopedIt != MCurrentContacts.end() && ScopedIt->key == InKey;
    };
    for (const ContactRecord& ScopedContact : MCurrentContacts)
    {
        if (std::binary_search(MPreviousPairKeys.begin(), MPreviousPairKeys.end(), ScopedContact.key))
        {
            continue;
        }
        EmitContact(EntityFromKey(static_cast<std::uint32_t>(ScopedContact.key >> 32)),
                    EntityFromKey(static_cast<std::uint32_t>(ScopedContact.key & 0xFFFFFFFFu)), ScopedContact.point,
                    ScopedContact.normal, ScopedContact.speed, PhysicsEventType::BeginTouch);
    }
    for (const std::uint64_t ScopedPrevious : MPreviousPairKeys)
    {
        if (ScopedHasCurrent(ScopedPrevious))
        {
            continue;
        }
        EmitContact(EntityFromKey(static_cast<std::uint32_t>(ScopedPrevious >> 32)),
                    EntityFromKey(static_cast<std::uint32_t>(ScopedPrevious & 0xFFFFFFFFu)), Vector2D{},
                    Vector2D{0.0f, -1.0f}, 0.0f, PhysicsEventType::EndTouch);
    }
    MPreviousPairKeys.clear();
    MPreviousPairKeys.reserve(MCurrentContacts.size());
    for (const ContactRecord& ScopedContact : MCurrentContacts)
    {
        MPreviousPairKeys.push_back(ScopedContact.key);
    }
}

void DodPhysicsBackend::EmitContact(Entity InA, Entity InB, Vector2D InPoint, Vector2D InNormal, float InSpeed,
                                    PhysicsEventType InType) noexcept
{
    if (MEvents.size() >= std::max<std::uint32_t>(MDesc.maxContactEvents, 1u))
    {
        return;
    }
    PhysicsContactEvent ScopedEvent;
    ScopedEvent.a = InA;
    ScopedEvent.b = InB;
    ScopedEvent.point = InPoint;
    ScopedEvent.normal = InNormal;
    ScopedEvent.speed = InSpeed;
    ScopedEvent.type = InType;
    ScopedEvent.sequence = MSequence++;
    MEvents.push_back(ScopedEvent);
    MEventCount = static_cast<std::uint32_t>(MEvents.size());
}

std::uint32_t DodPhysicsBackend::FindBody(Entity InEntity) const noexcept
{
    const auto ScopedFound = MEntityToIndex.find(InEntity);
    return ScopedFound == MEntityToIndex.end() ? kInvalidBody : ScopedFound->second;
}

std::uint32_t DodPhysicsBackend::EnsureBodyIndex(Ecs::Registry& InRegistry, Entity InEntity)
{
    const std::uint32_t ScopedIndex = FindBody(InEntity);
    if (ScopedIndex != kInvalidBody)
    {
        return ScopedIndex;
    }
    // The entity has no record yet, which happens when a game applies an impulse
    // before the first Step. Only this path pays for a full synchronization.
    EnsureBodies(InRegistry);
    return FindBody(InEntity);
}

void DodPhysicsBackend::ApplyImpulse(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InImpulse, bool InWake)
{
    const std::uint32_t ScopedIndex = EnsureBodyIndex(InRegistry, InEntity);
    if (ScopedIndex == kInvalidBody)
    {
        return;
    }
    // A frozen entity ignores impulses, matching the Box2D path that disables the
    // body; a scaled clock scales the authored impulse.
    const float ScopedScale = EntityTimeScale(InRegistry, InEntity);
    if (ScopedScale <= 0.0f)
    {
        return;
    }
    MColumns.Velocities[ScopedIndex] += InImpulse * (MColumns.InverseMasses[ScopedIndex] * ScopedScale);
    MColumns.SleepFrames[ScopedIndex] = 0;
    if (InWake)
    {
        MColumns.Flags[ScopedIndex] |= RigidBodyAwake;
    }
}

void DodPhysicsBackend::SetLinearVelocity(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InVelocity, bool InWake)
{
    const std::uint32_t ScopedIndex = EnsureBodyIndex(InRegistry, InEntity);
    if (ScopedIndex == kInvalidBody)
    {
        return;
    }
    MColumns.Velocities[ScopedIndex] = InVelocity;
    MColumns.SleepFrames[ScopedIndex] = 0;
    if (InWake)
    {
        MColumns.Flags[ScopedIndex] |= RigidBodyAwake;
    }
}

void DodPhysicsBackend::SetAngularVelocity(Ecs::Registry& InRegistry, Entity InEntity, float InAngularVelocity, bool InWake)
{
    const std::uint32_t ScopedIndex = EnsureBodyIndex(InRegistry, InEntity);
    if (ScopedIndex == kInvalidBody)
    {
        return;
    }
    MColumns.AngularVelocities[ScopedIndex] = InAngularVelocity;
    MColumns.SleepFrames[ScopedIndex] = 0;
    if (InWake)
    {
        MColumns.Flags[ScopedIndex] |= RigidBodyAwake;
    }
}

void DodPhysicsBackend::SetFixedRotation(Ecs::Registry& InRegistry, Entity InEntity, bool InFixed)
{
    const std::uint32_t ScopedIndex = EnsureBodyIndex(InRegistry, InEntity);
    if (ScopedIndex == kInvalidBody)
    {
        return;
    }
    if (InFixed)
    {
        MColumns.Flags[ScopedIndex] |= RigidBodyFixedRotation;
        MColumns.AngularVelocities[ScopedIndex] = 0.0f;
    }
    else
    {
        MColumns.Flags[ScopedIndex] &= ~RigidBodyFixedRotation;
    }
}

void DodPhysicsBackend::SetBullet(Ecs::Registry& InRegistry, Entity InEntity, bool InBullet)
{
    const std::uint32_t ScopedIndex = EnsureBodyIndex(InRegistry, InEntity);
    if (ScopedIndex == kInvalidBody)
    {
        return;
    }
    if (InBullet)
    {
        MColumns.Flags[ScopedIndex] |= RigidBodyBullet;
    }
    else
    {
        MColumns.Flags[ScopedIndex] &= ~RigidBodyBullet;
    }
}

void DodPhysicsBackend::ApplyAcceleration(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InAcceleration, bool InWake)
{
    const std::uint32_t ScopedIndex = EnsureBodyIndex(InRegistry, InEntity);
    if (ScopedIndex == kInvalidBody)
    {
        return;
    }
    // A frozen entity ignores acceleration; a scaled clock scales it like the
    // Box2D force path does.
    const float ScopedScale = EntityTimeScale(InRegistry, InEntity);
    if (ScopedScale <= 0.0f)
    {
        return;
    }
    MColumns.Accelerations[ScopedIndex] += InAcceleration * ScopedScale;
    MColumns.SleepFrames[ScopedIndex] = 0;
    if (InWake)
    {
        MColumns.Flags[ScopedIndex] |= RigidBodyAwake;
    }
}

void DodPhysicsBackend::SetGravity(Vector2D InGravity) noexcept
{
    MDesc.gravity = InGravity;
}

void DodPhysicsBackend::ApplyCollisionFilterMatrix(Ecs::Registry& InRegistry, const CollisionFilterMatrix& InMatrix) noexcept
{
    auto ScopedView = InRegistry.view<Collider2D>();
    for (auto [ScopedEntity, ScopedCollider] : ScopedView.each())
    {
        const std::uint32_t ScopedIndex = FindBody(ScopedEntity);
        if (ScopedIndex == kInvalidBody)
        {
            continue;
        }
        // The component is the source of truth: EnsureBodies copies it every step,
        // so a matrix baked only into the column would be overwritten immediately.
        const std::uint32_t ScopedLayer = CollisionFilterMatrix::LayerFromCategory(ScopedCollider.CategoryBits);
        ScopedCollider.CategoryBits = InMatrix.CategoryBits(ScopedLayer);
        ScopedCollider.MaskBits = InMatrix.MaskBits(ScopedLayer);
        MColumns.Categories[ScopedIndex] = ScopedCollider.CategoryBits;
        MColumns.Masks[ScopedIndex] = ScopedCollider.MaskBits;
    }
}

std::uint32_t DodPhysicsBackend::DestroyAllBodies(Ecs::Registry* InRegistry) noexcept
{
    const std::uint32_t ScopedCount = MAliveCount;
    if (InRegistry != nullptr)
    {
        auto ScopedView = InRegistry->view<RigidBody2D>();
        for (auto [ScopedEntity, ScopedBody] : ScopedView.each())
        {
            ScopedBody.Body = 0;
        }
    }
    ClearColumns();
    MBodyTimeScale.clear();
    MFreeIndices.clear();
    MEntityToIndex.clear();
    MGrid.clear();
    MPairKeys.clear();
    MCurrentContacts.clear();
    MPreviousPairKeys.clear();
    MEvents.clear();
    MEventCount = 0;
    MAliveCount = 0;
    return ScopedCount;
}

}
