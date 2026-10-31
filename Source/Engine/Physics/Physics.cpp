#include "Engine/Physics/Physics.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/ECS/RegistryOps.hpp"

#include "Engine/ECS/Schedule.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fby
{
namespace
{
[[nodiscard]] b2Pos ToBox(Vector2D InValue, float InPixelsPerMeter) noexcept
{
    b2Pos ScopedResult{};
    ScopedResult.x = static_cast<decltype(ScopedResult.x)>(InValue.X / InPixelsPerMeter);
    ScopedResult.y = static_cast<decltype(ScopedResult.y)>(InValue.Y / InPixelsPerMeter);
    return ScopedResult;
}

[[nodiscard]] Vector2D ToPixels(b2Pos InValue, float InPixelsPerMeter) noexcept
{
    return Vector2D{static_cast<float>(InValue.x) * InPixelsPerMeter, static_cast<float>(InValue.y) * InPixelsPerMeter};
}

[[nodiscard]] b2Vec2 ToBoxVelocity(Vector2D InValue, float InPixelsPerMeter) noexcept
{
    return b2Vec2{InValue.X / InPixelsPerMeter, InValue.Y / InPixelsPerMeter};
}

[[nodiscard]] Vector2D ToPixelsVelocity(b2Vec2 InValue, float InPixelsPerMeter) noexcept
{
    return Vector2D{InValue.x * InPixelsPerMeter, InValue.y * InPixelsPerMeter};
}

[[nodiscard]] Entity EntityFromBody(b2BodyId InBody) noexcept
{
    if (!b2Body_IsValid(InBody))
    {
        return entt::null;
    }
    const auto* ScopedRaw = static_cast<const std::uint8_t*>(b2Body_GetUserData(InBody));
    if (ScopedRaw == nullptr)
    {
        return entt::null;
    }
    return static_cast<Entity>(reinterpret_cast<std::uintptr_t>(ScopedRaw) - 1u);
}

// Contact events outlive the shape that produced them: destroying a body with
// contacts (a scene reload, Play/Stop, a spawned entity despawning) pushes
// end-touch events whose shape ids are already dead by the time the events are
// read. Box2D asserts on a stale shape id, so every event lookup is guarded.
[[nodiscard]] Entity EntityFromShape(b2ShapeId InShape) noexcept
{
    if (!b2Shape_IsValid(InShape))
    {
        return entt::null;
    }
    return EntityFromBody(b2Shape_GetBody(InShape));
}

[[nodiscard]] void* BodyUserData(Entity InEntity) noexcept
{
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(entt::to_integral(InEntity)) + 1u);
}

[[nodiscard]] b2BodyType ToBoxType(BodyType InType) noexcept
{
    switch (InType)
    {
        case BodyType::Static:
            return b2_staticBody;
        case BodyType::Kinematic:
            return b2_kinematicBody;
        case BodyType::Dynamic:
            return b2_dynamicBody;
    }
    return b2_staticBody;
}

[[nodiscard]] float LerpAngle(float InFrom, float InTo, float InAlpha) noexcept
{
    float ScopedDelta = std::remainder(InTo - InFrom, 6.28318530718f);
    return InFrom + ScopedDelta * InAlpha;
}
}

PhysicsSystem::~PhysicsSystem()
{
    Shutdown();
}

bool PhysicsSystem::Initialize(const PhysicsDesc& InDesc)
{
    Shutdown();
    MDesc = InDesc;
    if (!(MDesc.pixelsPerMeter > 0.0f))
    {
        MDesc.pixelsPerMeter = 50.0f;
    }
    b2WorldDef ScopedDefinition = b2DefaultWorldDef();
    ScopedDefinition.gravity = b2Vec2{MDesc.gravity.X, MDesc.gravity.Y};
    const b2WorldId ScopedWorld = b2CreateWorld(&ScopedDefinition);
    if (!b2World_IsValid(ScopedWorld))
    {
        return false;
    }
    MWorld = b2StoreWorldId(ScopedWorld);
    // The ownership table is the rollback sweep's source of truth. Reserve once
    MBodies.reserve(kMaxTrackedPhysicsBodies);
    return true;
}

void PhysicsSystem::Shutdown()
{
    if (MWorld != 0)
    {
        b2DestroyWorld(b2LoadWorldId(MWorld));
        MWorld = 0;
    }
    MBodies.clear();
    MEventCount = 0;
}

void PhysicsSystem::TrackBody(Entity InEntity, std::uint64_t InPackedBody) noexcept
{
    if (InPackedBody == 0 || MBodies.size() >= kMaxTrackedPhysicsBodies)
    {
        return;
    }
    // O(1) append. A duplicate record is harmless: DestroyAllBodies() validates
    MBodies.push_back(BodyRecord{InEntity, InPackedBody});
}

void PhysicsSystem::UntrackBody(std::uint64_t InPackedBody) noexcept
{
    if (InPackedBody == 0)
    {
        return;
    }
    for (std::size_t ScopedI = 0; ScopedI < MBodies.size(); ++ScopedI)
    {
        if (MBodies[ScopedI].body == InPackedBody)
        {
            MBodies[ScopedI] = MBodies.back();
            MBodies.pop_back();
            return;
        }
    }
}

void PhysicsSystem::PruneDeadBodies(Ecs::Registry& InRegistry) noexcept
{
    std::size_t ScopedI = 0;
    while (ScopedI < MBodies.size())
    {
        const BodyRecord ScopedRecord = MBodies[ScopedI];
        const bool ScopedEntityGone = ScopedRecord.entity != entt::null && !InRegistry.valid(ScopedRecord.entity);
        const bool ScopedBodyGone = !b2Body_IsValid(b2LoadBodyId(ScopedRecord.body));
        if (!ScopedEntityGone && !ScopedBodyGone)
        {
            ++ScopedI;
            continue;
        }
        if (!ScopedBodyGone)
        {
            b2DestroyBody(b2LoadBodyId(ScopedRecord.body));
        }
        MBodies[ScopedI] = MBodies.back();
        MBodies.pop_back();
    }
}

std::uint32_t PhysicsSystem::DestroyAllBodies(Ecs::Registry* InRegistry) noexcept
{
    if (MWorld == 0)
    {
        // A failed or already-finished world must still leave no stale handles
        // in the registry.
        if (InRegistry != nullptr)
        {
            for (auto [ScopedEntity, ScopedBody] : InRegistry->view<RigidBody2D>().each())
            {
                (void)ScopedEntity;
                ScopedBody.Body = 0;
            }
            for (auto [ScopedEntity, ScopedCollider] : InRegistry->view<Collider2D>().each())
            {
                (void)ScopedEntity;
                ScopedCollider.Shape = 0;
            }
        }
        MBodies.clear();
        MEventCount = 0;
        return 0;
    }
    std::uint32_t ScopedDestroyed = 0;

    // 1) Every body this system or an extension explicitly registered. This is
    for (const BodyRecord& ScopedRecord : MBodies)
    {
        if (ScopedRecord.body == 0)
        {
            continue;
        }
        const b2BodyId ScopedBody = b2LoadBodyId(ScopedRecord.body);
        if (b2Body_IsValid(ScopedBody))
        {
            b2DestroyBody(ScopedBody);
            ++ScopedDestroyed;
        }
    }
    MBodies.clear();

    // 2) Registry-owned bodies that were never tracked (joint-chain link bodies
    if (InRegistry != nullptr)
    {
        auto ScopedBodies = InRegistry->view<RigidBody2D>();
        for (auto [ScopedEntity, ScopedBody] : ScopedBodies.each())
        {
            (void)ScopedEntity;
            if (ScopedBody.Body != 0)
            {
                const b2BodyId ScopedId = b2LoadBodyId(ScopedBody.Body);
                if (b2Body_IsValid(ScopedId))
                {
                    b2DestroyBody(ScopedId);
                    ++ScopedDestroyed;
                }
                ScopedBody.Body = 0;
            }
        }
        auto ScopedColliders = InRegistry->view<Collider2D>();
        for (auto [ScopedEntity, ScopedCollider] : ScopedColliders.each())
        {
            (void)ScopedEntity;
            ScopedCollider.Shape = 0;
        }
    }
    MEventCount = 0;
    return ScopedDestroyed;
}

void PhysicsSystem::BindToRegistry(Ecs::Registry& InRegistry) noexcept
{
    Scene::SetNativeCleanupHook(
        InRegistry,
        [](void* InUser, Ecs::Registry& InWorld) noexcept
        {
            static_cast<PhysicsSystem*>(InUser)->DestroyAllBodies(&InWorld);
        },
        this);
}

void PhysicsSystem::CreateMissingBodies(Ecs::Registry& InRegistry)
{
    // Recycle records for entities destroyed since the last step. Without this
    PruneDeadBodies(InRegistry);
    auto ScopedView = InRegistry.view<RigidBody2D, Collider2D, Transform2D>();
    for (auto [ScopedEntity, ScopedBody, ScopedCollider, ScopedTransform] : ScopedView.each())
    {
        if (ScopedBody.Body != 0)
        {
            continue;
        }

        b2BodyDef ScopedBodyDefinition = b2DefaultBodyDef();
        ScopedBodyDefinition.type = ToBoxType(ScopedBody.Type);
        ScopedBodyDefinition.position = ToBox(ScopedTransform.Position, MDesc.pixelsPerMeter);
        ScopedBodyDefinition.rotation = b2MakeRot(ScopedTransform.Rotation);
        ScopedBodyDefinition.linearVelocity = ToBoxVelocity(ScopedBody.LinearVelocity, MDesc.pixelsPerMeter);
        ScopedBodyDefinition.angularVelocity = ScopedBody.AngularVelocity;
        ScopedBodyDefinition.gravityScale = ScopedBody.GravityScale;
        ScopedBodyDefinition.linearDamping = ScopedBody.LinearDamping;
        ScopedBodyDefinition.angularDamping = ScopedBody.AngularDamping;
        ScopedBodyDefinition.isAwake = (ScopedBody.Flags & RigidBodyAwake) != 0;
        ScopedBodyDefinition.isEnabled = (ScopedBody.Flags & RigidBodyEnabled) != 0;
        ScopedBodyDefinition.isBullet = (ScopedBody.Flags & RigidBodyBullet) != 0;
        ScopedBodyDefinition.motionLocks.angularZ = (ScopedBody.Flags & RigidBodyFixedRotation) != 0;
        ScopedBodyDefinition.userData = BodyUserData(ScopedEntity);

        const b2BodyId ScopedBodyId = b2CreateBody(b2LoadWorldId(MWorld), &ScopedBodyDefinition);
        if (!b2Body_IsValid(ScopedBodyId))
        {
            continue;
        }

        b2ShapeDef ScopedShapeDefinition = b2DefaultShapeDef();
        ScopedShapeDefinition.density = ScopedCollider.Density;
        ScopedShapeDefinition.material.friction = ScopedCollider.Friction;
        ScopedShapeDefinition.material.restitution = ScopedCollider.Restitution;
        ScopedShapeDefinition.filter.categoryBits = ScopedCollider.CategoryBits;
        ScopedShapeDefinition.filter.maskBits = ScopedCollider.MaskBits;
        ScopedShapeDefinition.isSensor = ScopedCollider.Sensor;
        ScopedShapeDefinition.enableSensorEvents = ScopedCollider.Sensor;
        ScopedShapeDefinition.enableContactEvents = ScopedCollider.ContactEvents;
        ScopedShapeDefinition.enableHitEvents = ScopedCollider.ContactEvents;
        const b2Polygon ScopedBox = b2MakeBox(std::max(ScopedCollider.HalfExtents.X / MDesc.pixelsPerMeter, 0.001f),
                                              std::max(ScopedCollider.HalfExtents.Y / MDesc.pixelsPerMeter, 0.001f));
        const b2ShapeId ScopedShapeId = b2CreatePolygonShape(ScopedBodyId, &ScopedShapeDefinition, &ScopedBox);

        ScopedBody.Body = b2StoreBodyId(ScopedBodyId);
        ScopedCollider.Shape = b2StoreShapeId(ScopedShapeId);
        TrackBody(ScopedEntity, ScopedBody.Body);
        ApplyShapeFilter(ScopedCollider);
        ScopedBody.Position = ScopedTransform.Position;
        ScopedBody.PreviousPosition = ScopedTransform.Position;
        ScopedBody.Rotation = ScopedTransform.Rotation;
        ScopedBody.PreviousRotation = ScopedTransform.Rotation;
    }
}

void PhysicsSystem::SyncBodiesFromRegistry(Ecs::Registry& InRegistry)
{
    auto ScopedView = InRegistry.view<RigidBody2D, Transform2D>();
    for (auto [ScopedEntity, ScopedBody, ScopedTransform] : ScopedView.each())
    {
        (void)ScopedEntity;
        if (ScopedBody.Body == 0)
        {
            continue;
        }
        const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody.Body);
        if (!b2Body_IsValid(ScopedBodyId))
        {
            continue;
        }
        if ((ScopedBody.Flags & RigidBodyTeleport) != 0)
        {
            b2Body_SetTransform(ScopedBodyId, ToBox(ScopedTransform.Position, MDesc.pixelsPerMeter),
                                b2MakeRot(ScopedTransform.Rotation));
            ScopedBody.Position = ScopedTransform.Position;
            ScopedBody.PreviousPosition = ScopedTransform.Position;
            ScopedBody.Rotation = ScopedTransform.Rotation;
            ScopedBody.PreviousRotation = ScopedTransform.Rotation;
            ScopedBody.Flags &= ~RigidBodyTeleport;
        }
        if (ScopedBody.Type == BodyType::Dynamic)
        {
            ScopedBody.LinearVelocity = ToPixelsVelocity(b2Body_GetLinearVelocity(ScopedBodyId), MDesc.pixelsPerMeter);
            ScopedBody.AngularVelocity = b2Body_GetAngularVelocity(ScopedBodyId);
        }
        ScopedBody.PreviousPosition = ScopedBody.Position;
        ScopedBody.PreviousRotation = ScopedBody.Rotation;
    }
}

void PhysicsSystem::ReadBodiesIntoRegistry(Ecs::Registry& InRegistry)
{
    auto ScopedView = InRegistry.view<RigidBody2D>();
    for (auto [ScopedEntity, ScopedBody] : ScopedView.each())
    {
        (void)ScopedEntity;
        if (ScopedBody.Body == 0 || ScopedBody.Type == BodyType::Static)
        {
            continue;
        }
        const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody.Body);
        if (!b2Body_IsValid(ScopedBodyId))
        {
            continue;
        }
        const b2WorldTransform ScopedTransform = b2Body_GetTransform(ScopedBodyId);
        ScopedBody.Position = ToPixels(ScopedTransform.p, MDesc.pixelsPerMeter);
        ScopedBody.Rotation = b2Rot_GetAngle(ScopedTransform.q);
        ScopedBody.LinearVelocity = ToPixelsVelocity(b2Body_GetLinearVelocity(ScopedBodyId), MDesc.pixelsPerMeter);
        ScopedBody.AngularVelocity = b2Body_GetAngularVelocity(ScopedBodyId);
    }
}

void PhysicsSystem::ReadContactEvents(Ecs::Registry& InRegistry)
{
    (void)InRegistry;
    if (MWorld == 0)
    {
        return;
    }
    const b2ContactEvents ScopedContacts = b2World_GetContactEvents(b2LoadWorldId(MWorld));
    const auto ScopedPush = [this](PhysicsContactEvent InEvent)
    {
        if (MEventCount < MEvents.size())
        {
            InEvent.sequence = MSequence++;
            MEvents[MEventCount++] = InEvent;
        }
    };
    for (int ScopedI = 0; ScopedI < ScopedContacts.beginCount; ++ScopedI)
    {
        const b2ContactBeginTouchEvent& ScopedSource = ScopedContacts.beginEvents[ScopedI];
        PhysicsContactEvent ScopedEvent;
        ScopedEvent.type = PhysicsEventType::BeginTouch;
        ScopedEvent.a = EntityFromShape(ScopedSource.shapeIdA);
        ScopedEvent.b = EntityFromShape(ScopedSource.shapeIdB);
        if (ScopedEvent.a != entt::null || ScopedEvent.b != entt::null)
        {
            ScopedPush(ScopedEvent);
        }
    }
    for (int ScopedI = 0; ScopedI < ScopedContacts.endCount; ++ScopedI)
    {
        const b2ContactEndTouchEvent& ScopedSource = ScopedContacts.endEvents[ScopedI];
        PhysicsContactEvent ScopedEvent;
        ScopedEvent.type = PhysicsEventType::EndTouch;
        ScopedEvent.a = EntityFromShape(ScopedSource.shapeIdA);
        ScopedEvent.b = EntityFromShape(ScopedSource.shapeIdB);
        if (ScopedEvent.a != entt::null || ScopedEvent.b != entt::null)
        {
            ScopedPush(ScopedEvent);
        }
    }
    for (int ScopedI = 0; ScopedI < ScopedContacts.hitCount; ++ScopedI)
    {
        const b2ContactHitEvent& ScopedSource = ScopedContacts.hitEvents[ScopedI];
        PhysicsContactEvent ScopedEvent;
        ScopedEvent.type = PhysicsEventType::Hit;
        ScopedEvent.a = EntityFromShape(ScopedSource.shapeIdA);
        ScopedEvent.b = EntityFromShape(ScopedSource.shapeIdB);
        ScopedEvent.point = ToPixels(ScopedSource.point, MDesc.pixelsPerMeter);
        ScopedEvent.normal = Vector2D{ScopedSource.normal.x, ScopedSource.normal.y};
        ScopedEvent.speed = ScopedSource.approachSpeed * MDesc.pixelsPerMeter;
        if (ScopedEvent.a != entt::null || ScopedEvent.b != entt::null)
        {
            ScopedPush(ScopedEvent);
        }
    }
}

void PhysicsSystem::EnsureBodies(Ecs::Registry& InRegistry)
{
    if (MWorld == 0)
    {
        return;
    }
    CreateMissingBodies(InRegistry);
}

void PhysicsSystem::ApplyPresentationPolicies(Ecs::Registry& InRegistry) noexcept
{
    auto ScopedView = InRegistry.view<CollisionPolicyComponent>();
    for (auto [ScopedEntity, ScopedPolicy] : ScopedView.each())
    {
        ApplyCollisionPolicyToCollider(InRegistry, ScopedEntity, ScopedPolicy);
        auto* ScopedCollider = InRegistry.try_get<Collider2D>(ScopedEntity);
        if (ScopedCollider != nullptr && ScopedCollider->Shape != 0)
        {
            const b2ShapeId ScopedShapeId = b2LoadShapeId(ScopedCollider->Shape);
            if (b2Shape_IsValid(ScopedShapeId))
            {
                b2Filter ScopedFilter = b2DefaultFilter();
                ScopedFilter.categoryBits = ScopedCollider->CategoryBits;
                ScopedFilter.maskBits = ScopedCollider->MaskBits;
                ScopedFilter.groupIndex = 0;
                b2Shape_SetFilter(ScopedShapeId, ScopedFilter);
                b2Shape_EnableSensorEvents(ScopedShapeId, ScopedCollider->ContactEvents);
            }
        }
        auto* ScopedBody = InRegistry.try_get<RigidBody2D>(ScopedEntity);
        if (ScopedBody == nullptr || ScopedBody->Body == 0)
        {
            continue;
        }
        const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody->Body);
        if (!b2Body_IsValid(ScopedBodyId))
        {
            continue;
        }
        // A type change already wakes the body; an unchanged body is left to
        // Box2D's own sleep schedule so policy entities can still sleep.
        const b2BodyType ScopedDesired = ToBoxType(ScopedBody->Type);
        if (b2Body_GetType(ScopedBodyId) != ScopedDesired)
        {
            b2Body_SetType(ScopedBodyId, ScopedDesired);
        }
    }
}

void PhysicsSystem::ApplyEntityTimeScale(Ecs::Registry& InRegistry) noexcept
{
    auto ScopedView = InRegistry.view<TimeScaleComponent, RigidBody2D>();
    for (auto [ScopedEntity, ScopedScale, ScopedBody] : ScopedView.each())
    {
        (void)ScopedEntity;
        if (ScopedBody.Body == 0)
        {
            continue;
        }
        const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody.Body);
        if (!b2Body_IsValid(ScopedBodyId))
        {
            continue;
        }
        const bool ScopedFrozen = ScopedScale.Clamped() <= 0.0f;
        const bool ScopedWasFrozen = (ScopedBody.Flags & RigidBodyFrozen) != 0;
        if (ScopedFrozen && !ScopedWasFrozen)
        {
            b2Body_SetLinearVelocity(ScopedBodyId, b2Vec2{0.0f, 0.0f});
            b2Body_SetAngularVelocity(ScopedBodyId, 0.0f);
            b2Body_Disable(ScopedBodyId);
            ScopedBody.Flags |= RigidBodyFrozen;
            ScopedBody.LinearVelocity = Vector2D{};
            ScopedBody.AngularVelocity = 0.0f;
        }
        else if (!ScopedFrozen && ScopedWasFrozen)
        {
            if ((ScopedBody.Flags & RigidBodyEnabled) != 0)
            {
                b2Body_Enable(ScopedBodyId);
            }
            ScopedBody.Flags &= ~RigidBodyFrozen;
        }
    }
}

void PhysicsSystem::Step(Ecs::Registry& InRegistry, float InFixedDt)
{
    if (MWorld == 0 || !(InFixedDt > 0.0f))
    {
        return;
    }
    ApplyPresentationPolicies(InRegistry);
    ApplyEntityTimeScale(InRegistry);
    CreateMissingBodies(InRegistry);
    SyncBodiesFromRegistry(InRegistry);
    MEventCount = 0;
    const float ScopedStepSeconds = InFixedDt * MTimeScale;
    if (!(ScopedStepSeconds > 0.0f))
    {
        return;
    }
    b2World_Step(b2LoadWorldId(MWorld), ScopedStepSeconds, static_cast<int>(std::max(MDesc.subSteps, 1u)));
    ReadBodiesIntoRegistry(InRegistry);
    ReadContactEvents(InRegistry);
    const std::span<const PhysicsContactEvent> ScopedContacts(MEvents.data(), MEventCount);
    (void)WakeSleepingStaticFromContacts(InRegistry, ScopedContacts);
}

void PhysicsSystem::Interpolate(Ecs::Registry& InRegistry, float InAlpha) const
{
    const float ScopedClamped = std::clamp(InAlpha, 0.0f, 1.0f);
    auto ScopedView = InRegistry.view<RigidBody2D, Transform2D>();
    for (auto [ScopedEntity, ScopedBody, ScopedTransform] : ScopedView.each())
    {
        (void)ScopedEntity;
        if (ScopedBody.Body == 0 || ScopedBody.Type == BodyType::Static)
        {
            continue;
        }
        ScopedTransform.Position =
            ScopedBody.PreviousPosition + (ScopedBody.Position - ScopedBody.PreviousPosition) * ScopedClamped;
        ScopedTransform.Rotation = LerpAngle(ScopedBody.PreviousRotation, ScopedBody.Rotation, ScopedClamped);
    }
}

void PhysicsSystem::ApplyShapeFilter(Collider2D& InCollider) noexcept
{
    if (!MFilterActive || InCollider.Shape == 0)
    {
        return;
    }
    const std::uint32_t ScopedLayer = CollisionFilterMatrix::LayerFromCategory(InCollider.CategoryBits);
    InCollider.CategoryBits = MFilter.CategoryBits(ScopedLayer);
    InCollider.MaskBits = MFilter.MaskBits(ScopedLayer);
    const b2ShapeId ScopedShapeId = b2LoadShapeId(InCollider.Shape);
    if (!b2Shape_IsValid(ScopedShapeId))
    {
        return;
    }
    b2Filter ScopedFilter = b2DefaultFilter();
    ScopedFilter.categoryBits = InCollider.CategoryBits;
    ScopedFilter.maskBits = InCollider.MaskBits;
    ScopedFilter.groupIndex = 0;
    b2Shape_SetFilter(ScopedShapeId, ScopedFilter);
}

void PhysicsSystem::ApplyCollisionFilterMatrix(Ecs::Registry& InRegistry,
                                               const CollisionFilterMatrix& InMatrix) noexcept
{
    MFilter = InMatrix;
    MFilterActive = true;
    auto ScopedView = InRegistry.view<Collider2D>();
    for (const Entity ScopedEntity : ScopedView)
    {
        ApplyShapeFilter(ScopedView.get<Collider2D>(ScopedEntity));
    }
}

void PhysicsSystem::ApplyImpulse(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InImpulse, bool InWake)
{
    if (MWorld == 0 || !InRegistry.valid(InEntity))
    {
        return;
    }
    auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity);
    if (ScopedBody == nullptr || ScopedBody->Body == 0)
    {
        return;
    }
    const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody->Body);
    if (!b2Body_IsValid(ScopedBodyId))
    {
        return;
    }
    const float ScopedTimeScale = EntityTimeScale(InRegistry, InEntity);
    if (ScopedTimeScale <= 0.0f)
    {
        return;
    }
    b2Body_ApplyLinearImpulseToCenter(ScopedBodyId, ToBoxVelocity(InImpulse * ScopedTimeScale, MDesc.pixelsPerMeter),
                                      InWake);
}
}
