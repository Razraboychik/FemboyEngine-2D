// FemboyEngine2D - data-oriented fallback physics backend.
#include "Engine/Physics/DodPhysicsBackend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fby
{
Vector2D DodPhysicsBackend::MinCorner(const Body& InBody) noexcept
{
    return Vector2D{InBody.position.X - InBody.halfWidth, InBody.position.Y - InBody.halfHeight};
}

Vector2D DodPhysicsBackend::MaxCorner(const Body& InBody) noexcept
{
    return Vector2D{InBody.position.X + InBody.halfWidth, InBody.position.Y + InBody.halfHeight};
}

bool DodPhysicsBackend::Initialize(const PhysicsDesc& InDesc)
{
    MDesc = InDesc;
    MEvents.clear();
    MEvents.reserve(std::max<std::uint32_t>(InDesc.maxContactEvents, 1u));
    MBodies.clear();
    MBodies.reserve(4096);
    MFreeIndices.clear();
    MEntityToIndex.clear();
    MGrid.clear();
    MPairKeys.clear();
    MEventCount = 0;
    MSequence = 0;
    MInitialized = true;
    return true;
}

void DodPhysicsBackend::Shutdown()
{
    MBodies.clear();
    MFreeIndices.clear();
    MEntityToIndex.clear();
    MGrid.clear();
    MPairKeys.clear();
    MEvents.clear();
    MEventCount = 0;
    MInitialized = false;
}

void DodPhysicsBackend::EnsureBodies(Ecs::Registry& InRegistry)
{
    PruneDeadBodies(InRegistry);
    auto ScopedView = InRegistry.view<RigidBody2D, Collider2D, Transform2D>();
    for (auto [ScopedEntity, ScopedBody, ScopedCollider, ScopedTransform] : ScopedView.each())
    {
        std::uint32_t ScopedExisting = std::numeric_limits<std::uint32_t>::max();
        if (ScopedBody.Body != 0 && ScopedBody.Body <= MBodies.size())
        {
            const auto ScopedCandidate = static_cast<std::uint32_t>(ScopedBody.Body - 1u);
            if (MBodies[ScopedCandidate].entity == ScopedEntity)
            {
                ScopedExisting = ScopedCandidate;
            }
        }
        if (ScopedExisting == std::numeric_limits<std::uint32_t>::max())
        {
            const auto ScopedFound = MEntityToIndex.find(ScopedEntity);
            if (ScopedFound != MEntityToIndex.end())
            {
                ScopedExisting = ScopedFound->second;
            }
        }
        if (ScopedExisting != std::numeric_limits<std::uint32_t>::max())
        {
            Body& ScopedRecord = MBodies[ScopedExisting];
            if ((ScopedBody.Flags & RigidBodyTeleport) != 0)
            {
                ScopedRecord.position = ScopedTransform.Position;
                ScopedRecord.previousPosition = ScopedTransform.Position;
                ScopedRecord.rotation = ScopedTransform.Rotation;
                ScopedRecord.previousRotation = ScopedTransform.Rotation;
                ScopedBody.Flags &= ~RigidBodyTeleport;
            }
            ScopedRecord.type = ScopedBody.Type;
            ScopedRecord.flags = ScopedBody.Flags;
            ScopedRecord.halfWidth = std::max(ScopedCollider.HalfExtents.X, 0.001f);
            ScopedRecord.halfHeight = std::max(ScopedCollider.HalfExtents.Y, 0.001f);
            ScopedRecord.category = ScopedCollider.CategoryBits;
            ScopedRecord.mask = ScopedCollider.MaskBits;
            ScopedRecord.sensor = ScopedCollider.Sensor;
            ScopedRecord.contactEvents = ScopedCollider.ContactEvents;
            continue;
        }

        std::uint32_t ScopedIndex = 0;
        if (!MFreeIndices.empty())
        {
            ScopedIndex = MFreeIndices.back();
            MFreeIndices.pop_back();
            MBodies[ScopedIndex] = Body{};
        }
        else
        {
            ScopedIndex = static_cast<std::uint32_t>(MBodies.size());
            MBodies.push_back(Body{});
        }

        Body& ScopedAdded = MBodies[ScopedIndex];
        ScopedAdded.entity = ScopedEntity;
        ScopedAdded.position = ScopedTransform.Position;
        ScopedAdded.previousPosition = ScopedTransform.Position;
        ScopedAdded.rotation = ScopedTransform.Rotation;
        ScopedAdded.previousRotation = ScopedTransform.Rotation;
        ScopedAdded.velocity = ScopedBody.LinearVelocity;
        ScopedAdded.angularVelocity = ScopedBody.AngularVelocity;
        ScopedAdded.type = ScopedBody.Type;
        ScopedAdded.flags = ScopedBody.Flags;
        ScopedAdded.halfWidth = std::max(ScopedCollider.HalfExtents.X, 0.001f);
        ScopedAdded.halfHeight = std::max(ScopedCollider.HalfExtents.Y, 0.001f);
        ScopedAdded.category = ScopedCollider.CategoryBits;
        ScopedAdded.mask = ScopedCollider.MaskBits;
        ScopedAdded.sensor = ScopedCollider.Sensor;
        ScopedAdded.contactEvents = ScopedCollider.ContactEvents;
        ScopedAdded.inverseMass = ScopedBody.Type == BodyType::Dynamic ? 1.0f / std::max(ScopedCollider.Density, 0.001f) : 0.0f;
        MEntityToIndex[ScopedEntity] = ScopedIndex;
        ScopedBody.Body = static_cast<std::uint64_t>(ScopedIndex) + 1u;
        ScopedCollider.Shape = 0;
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

    const float ScopedDt = std::max(InFixedDt, 0.0f);
    for (Body& ScopedBody : MBodies)
    {
        ScopedBody.previousPosition = ScopedBody.position;
        ScopedBody.previousRotation = ScopedBody.rotation;
        if (ScopedBody.type != BodyType::Dynamic || (ScopedBody.flags & RigidBodyEnabled) == 0)
        {
            continue;
        }
        ScopedBody.velocity += MDesc.gravity * (MDesc.pixelsPerMeter * ScopedDt);
        ScopedBody.position += ScopedBody.velocity * ScopedDt;
        ScopedBody.rotation += ScopedBody.angularVelocity * ScopedDt;
        const float ScopedSpeed = std::sqrt(ScopedBody.velocity.Dot(ScopedBody.velocity));
        ScopedBody.sleepFrames = (ScopedSpeed < 1.0f && std::abs(ScopedBody.angularVelocity) < 0.05f)
                                     ? ScopedBody.sleepFrames + 1u
                                     : 0u;
    }

    constexpr float kCellSize = 64.0f;
    const auto ScopedCellKey = [](std::int32_t InX, std::int32_t InY) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(InX)) << 32) |
               static_cast<std::uint32_t>(InY);
    };

    MGrid.clear();
    MGrid.reserve(MBodies.size());
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MBodies.size(); ++ScopedIndex)
    {
        const Body& ScopedBody = MBodies[ScopedIndex];
        if ((ScopedBody.flags & RigidBodyEnabled) == 0)
        {
            continue;
        }
        const Vector2D ScopedMin = MinCorner(ScopedBody);
        const Vector2D ScopedMax = MaxCorner(ScopedBody);
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
            const Body& ScopedBodyA = MBodies[ScopedA];
            for (std::size_t ScopedJ = ScopedI + 1; ScopedJ < ScopedEnd; ++ScopedJ)
            {
                const std::uint32_t ScopedB = MGrid[ScopedJ].index;
                if (ScopedA == ScopedB || (ScopedBodyA.category & MBodies[ScopedB].mask) == 0 ||
                    (MBodies[ScopedB].category & ScopedBodyA.mask) == 0)
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
        if (MBodies[ScopedA].type == BodyType::Dynamic || MBodies[ScopedB].type == BodyType::Dynamic)
        {
            Resolve(MBodies[ScopedA], MBodies[ScopedB], ScopedA, ScopedB);
        }
    }

    auto ScopedView = InRegistry.view<RigidBody2D, Transform2D>();
    for (auto [ScopedEntity, ScopedBody, ScopedTransform] : ScopedView.each())
    {
        std::uint32_t ScopedIndex = std::numeric_limits<std::uint32_t>::max();
        if (ScopedBody.Body != 0 && ScopedBody.Body <= MBodies.size())
        {
            ScopedIndex = static_cast<std::uint32_t>(ScopedBody.Body - 1u);
        }
        if (ScopedIndex == std::numeric_limits<std::uint32_t>::max() || MBodies[ScopedIndex].entity != ScopedEntity)
        {
            ScopedIndex = FindBody(ScopedEntity);
        }
        if (ScopedIndex == std::numeric_limits<std::uint32_t>::max())
        {
            continue;
        }
        const Body& ScopedData = MBodies[ScopedIndex];
        ScopedBody.Position = ScopedData.position;
        ScopedBody.PreviousPosition = ScopedData.previousPosition;
        ScopedBody.Rotation = ScopedData.rotation;
        ScopedBody.PreviousRotation = ScopedData.previousRotation;
        ScopedBody.LinearVelocity = ScopedData.velocity;
        ScopedBody.AngularVelocity = ScopedData.angularVelocity;
        ScopedTransform.Position = ScopedData.position;
        ScopedTransform.Rotation = ScopedData.rotation;
    }
}

void DodPhysicsBackend::Interpolate(Ecs::Registry& InRegistry, float InAlpha) const
{
    const float ScopedAlpha = std::clamp(InAlpha, 0.0f, 1.0f);
    auto ScopedView = InRegistry.view<RigidBody2D, Transform2D>();
    for (auto [ScopedEntity, ScopedBody, ScopedTransform] : ScopedView.each())
    {
        std::uint32_t ScopedIndex = std::numeric_limits<std::uint32_t>::max();
        if (ScopedBody.Body != 0 && ScopedBody.Body <= MBodies.size())
        {
            ScopedIndex = static_cast<std::uint32_t>(ScopedBody.Body - 1u);
        }
        if (ScopedIndex == std::numeric_limits<std::uint32_t>::max() || MBodies[ScopedIndex].entity != ScopedEntity)
        {
            ScopedIndex = FindBody(ScopedEntity);
        }
        if (ScopedIndex == std::numeric_limits<std::uint32_t>::max())
        {
            continue;
        }
        const Body& ScopedData = MBodies[ScopedIndex];
        ScopedTransform.Position =
            ScopedData.previousPosition + (ScopedData.position - ScopedData.previousPosition) * ScopedAlpha;
        ScopedTransform.Rotation =
            ScopedData.previousRotation + (ScopedData.rotation - ScopedData.previousRotation) * ScopedAlpha;
        ScopedBody.Position = ScopedData.position;
        ScopedBody.PreviousPosition = ScopedData.previousPosition;
        ScopedBody.Rotation = ScopedData.rotation;
        ScopedBody.PreviousRotation = ScopedData.previousRotation;
    }
}

void DodPhysicsBackend::Resolve(Body& InA, Body& InB, std::uint32_t InAIndex, std::uint32_t InBIndex) noexcept
{
    (void)InAIndex;
    (void)InBIndex;
    if (!Overlaps(InA, InB))
    {
        return;
    }
    if (InA.sensor || InB.sensor)
    {
        if (InA.contactEvents || InB.contactEvents)
        {
            EmitContact(InA.entity, InB.entity, (InA.position + InB.position) * 0.5f, Vector2D{0.0f, -1.0f}, 0.0f,
                        PhysicsEventType::BeginTouch);
        }
        return;
    }

    const Vector2D ScopedDelta = InB.position - InA.position;
    const float ScopedOverlapX = InA.halfWidth + InB.halfWidth - std::abs(ScopedDelta.X);
    const float ScopedOverlapY = InA.halfHeight + InB.halfHeight - std::abs(ScopedDelta.Y);
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

    const float ScopedInvMassSum = InA.inverseMass + InB.inverseMass;
    if (ScopedInvMassSum <= 0.0f)
    {
        return;
    }
    const Vector2D ScopedCorrection = ScopedNormal * (ScopedDepth / ScopedInvMassSum);
    InA.position -= ScopedCorrection * InA.inverseMass;
    InB.position += ScopedCorrection * InB.inverseMass;

    const Vector2D ScopedRelative = InB.velocity - InA.velocity;
    const float ScopedNormalSpeed = ScopedRelative.Dot(ScopedNormal);
    if (ScopedNormalSpeed < 0.0f)
    {
        const float ScopedImpulse = -ScopedNormalSpeed / ScopedInvMassSum;
        InA.velocity -= ScopedNormal * (ScopedImpulse * InA.inverseMass);
        InB.velocity += ScopedNormal * (ScopedImpulse * InB.inverseMass);
    }
    if (InA.contactEvents || InB.contactEvents)
    {
        EmitContact(InA.entity, InB.entity, (InA.position + InB.position) * 0.5f, ScopedNormal, ScopedNormalSpeed,
                    PhysicsEventType::BeginTouch);
    }
}

bool DodPhysicsBackend::Overlaps(const Body& InA, const Body& InB) noexcept
{
    return std::abs(InA.position.X - InB.position.X) <= InA.halfWidth + InB.halfWidth &&
           std::abs(InA.position.Y - InB.position.Y) <= InA.halfHeight + InB.halfHeight;
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

void DodPhysicsBackend::PruneDeadBodies(Ecs::Registry& InRegistry)
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MBodies.size(); ++ScopedIndex)
    {
        Body& ScopedBody = MBodies[ScopedIndex];
        if (ScopedBody.entity == entt::null)
        {
            continue;
        }
        if (!InRegistry.valid(ScopedBody.entity))
        {
            MEntityToIndex.erase(ScopedBody.entity);
            ScopedBody = Body{};
            MFreeIndices.push_back(ScopedIndex);
        }
    }
}

std::uint32_t DodPhysicsBackend::FindBody(Entity InEntity) const noexcept
{
    const auto ScopedFound = MEntityToIndex.find(InEntity);
    return ScopedFound == MEntityToIndex.end() ? std::numeric_limits<std::uint32_t>::max() : ScopedFound->second;
}

void DodPhysicsBackend::ApplyImpulse(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InImpulse, bool InWake)
{
    EnsureBodies(InRegistry);
    const std::uint32_t ScopedIndex = FindBody(InEntity);
    if (ScopedIndex == std::numeric_limits<std::uint32_t>::max())
    {
        return;
    }
    Body& ScopedBody = MBodies[ScopedIndex];
    ScopedBody.velocity += InImpulse * ScopedBody.inverseMass;
    ScopedBody.sleepFrames = 0;
    if (InWake)
    {
        ScopedBody.flags |= RigidBodyAwake;
    }
}

void DodPhysicsBackend::SetLinearVelocity(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InVelocity, bool InWake)
{
    EnsureBodies(InRegistry);
    const std::uint32_t ScopedIndex = FindBody(InEntity);
    if (ScopedIndex == std::numeric_limits<std::uint32_t>::max())
    {
        return;
    }
    MBodies[ScopedIndex].velocity = InVelocity;
    MBodies[ScopedIndex].sleepFrames = 0;
    if (InWake)
    {
        MBodies[ScopedIndex].flags |= RigidBodyAwake;
    }
}

void DodPhysicsBackend::SetAngularVelocity(Ecs::Registry& InRegistry, Entity InEntity, float InAngularVelocity, bool InWake)
{
    EnsureBodies(InRegistry);
    const std::uint32_t ScopedIndex = FindBody(InEntity);
    if (ScopedIndex == std::numeric_limits<std::uint32_t>::max())
    {
        return;
    }
    MBodies[ScopedIndex].angularVelocity = InAngularVelocity;
    MBodies[ScopedIndex].sleepFrames = 0;
    if (InWake)
    {
        MBodies[ScopedIndex].flags |= RigidBodyAwake;
    }
}

void DodPhysicsBackend::SetFixedRotation(Ecs::Registry& InRegistry, Entity InEntity, bool InFixed)
{
    EnsureBodies(InRegistry);
    const std::uint32_t ScopedIndex = FindBody(InEntity);
    if (ScopedIndex == std::numeric_limits<std::uint32_t>::max())
    {
        return;
    }
    if (InFixed)
    {
        MBodies[ScopedIndex].flags |= RigidBodyFixedRotation;
        MBodies[ScopedIndex].angularVelocity = 0.0f;
    }
    else
    {
        MBodies[ScopedIndex].flags &= ~RigidBodyFixedRotation;
    }
}

void DodPhysicsBackend::SetBullet(Ecs::Registry& InRegistry, Entity InEntity, bool InBullet)
{
    EnsureBodies(InRegistry);
    const std::uint32_t ScopedIndex = FindBody(InEntity);
    if (ScopedIndex == std::numeric_limits<std::uint32_t>::max())
    {
        return;
    }
    if (InBullet)
    {
        MBodies[ScopedIndex].flags |= RigidBodyBullet;
    }
    else
    {
        MBodies[ScopedIndex].flags &= ~RigidBodyBullet;
    }
}

void DodPhysicsBackend::ApplyAcceleration(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InAcceleration, bool InWake)
{
    EnsureBodies(InRegistry);
    const std::uint32_t ScopedIndex = FindBody(InEntity);
    if (ScopedIndex == std::numeric_limits<std::uint32_t>::max())
    {
        return;
    }
    MBodies[ScopedIndex].velocity += InAcceleration * (1.0f / 60.0f);
    MBodies[ScopedIndex].sleepFrames = 0;
    if (InWake)
    {
        MBodies[ScopedIndex].flags |= RigidBodyAwake;
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
        if (ScopedIndex == std::numeric_limits<std::uint32_t>::max())
        {
            continue;
        }
        MBodies[ScopedIndex].category = ScopedCollider.CategoryBits;
        MBodies[ScopedIndex].mask = InMatrix.MaskBits(CollisionFilterMatrix::LayerFromCategory(ScopedCollider.CategoryBits));
    }
}

std::uint32_t DodPhysicsBackend::DestroyAllBodies(Ecs::Registry* InRegistry) noexcept
{
    const std::uint32_t ScopedCount = static_cast<std::uint32_t>(MBodies.size());
    if (InRegistry != nullptr)
    {
        auto ScopedView = InRegistry->view<RigidBody2D>();
        for (auto [ScopedEntity, ScopedBody] : ScopedView.each())
        {
            ScopedBody.Body = 0;
        }
    }
    MBodies.clear();
    MFreeIndices.clear();
    MEntityToIndex.clear();
    MGrid.clear();
    MPairKeys.clear();
    MEvents.clear();
    MEventCount = 0;
    return ScopedCount;
}

}
