#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/DodPhysicsBackend.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/ECS/RegistryOps.hpp"

#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/SimMath.hpp"
#include "Engine/Core/ThreadAffinity.hpp"

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

// Shape creation lives in one place so a runtime collider edit can recreate the
// shape with the same definition the initial body used.
[[nodiscard]] b2ShapeId CreateColliderShape(b2BodyId InBody, const Collider2D& InCollider,
                                            float InPixelsPerMeter) noexcept
{
    b2ShapeDef ScopedDefinition = b2DefaultShapeDef();
    ScopedDefinition.density = InCollider.Density;
    ScopedDefinition.material.friction = InCollider.Friction;
    ScopedDefinition.material.restitution = InCollider.Restitution;
    ScopedDefinition.filter.categoryBits = InCollider.CategoryBits;
    ScopedDefinition.filter.maskBits = InCollider.MaskBits;
    ScopedDefinition.isSensor = InCollider.Sensor;
    ScopedDefinition.enableSensorEvents = InCollider.Sensor;
    ScopedDefinition.enableContactEvents = InCollider.ContactEvents;
    ScopedDefinition.enableHitEvents = InCollider.ContactEvents;
    const b2Polygon ScopedBox = b2MakeBox(std::max(InCollider.HalfExtents.X / InPixelsPerMeter, 0.001f),
                                          std::max(InCollider.HalfExtents.Y / InPixelsPerMeter, 0.001f));
    return b2CreatePolygonShape(InBody, &ScopedDefinition, &ScopedBox);
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
    const float ScopedDelta = Sim::WrapPi(InTo - InFrom);
    return InFrom + ScopedDelta * InAlpha;
}

// Nearest hit of a Box2D ray query. The callback filters the ignored entity and
// tracks the smallest fraction instead of trusting the first callback order.
struct RayCastCollector
{
    Entity ignore = entt::null;
    bool found = false;
    float fraction = 1.0f;
    b2ShapeId shape{};
    b2Pos point{};
    b2Vec2 normal{};
};

float CollectRayHit(b2ShapeId InShape, b2Pos InPoint, b2Vec2 InNormal, float InFraction, void* InContext)
{
    auto* ScopedCollector = static_cast<RayCastCollector*>(InContext);
    const Entity ScopedEntity = EntityFromShape(InShape);
    if (ScopedEntity == entt::null || ScopedEntity == ScopedCollector->ignore)
    {
        return -1.0f;
    }
    if (!ScopedCollector->found || InFraction < ScopedCollector->fraction)
    {
        ScopedCollector->found = true;
        ScopedCollector->fraction = InFraction;
        ScopedCollector->shape = InShape;
        ScopedCollector->point = InPoint;
        ScopedCollector->normal = InNormal;
    }
    return 1.0f;
}
}

namespace
{
constexpr std::size_t kMaxLivePhysicsSystems = 64;

struct LivePhysicsRecord
{
    const PhysicsSystem* System = nullptr;
    std::uint64_t Instance = 0;
};

struct LivePhysicsRegistry
{
    std::array<LivePhysicsRecord, kMaxLivePhysicsSystems> Records{};
    std::size_t Count = 0;
};

[[nodiscard]] LivePhysicsRegistry& LivePhysics() noexcept
{
    static LivePhysicsRegistry ScopedRegistry;
    return ScopedRegistry;
}

[[nodiscard]] std::uint64_t NextPhysicsInstance() noexcept
{
    static std::uint64_t ScopedNext = 0;
    return ++ScopedNext;
}
}

PhysicsSystem::PhysicsSystem()
    : MInstanceId(NextPhysicsInstance())
{
    LivePhysicsRegistry& ScopedRegistry = LivePhysics();
    if (ScopedRegistry.Count < ScopedRegistry.Records.size())
    {
        ScopedRegistry.Records[ScopedRegistry.Count++] = LivePhysicsRecord{this, MInstanceId};
    }
}

PhysicsSystem::~PhysicsSystem()
{
    Shutdown();
    LivePhysicsRegistry& ScopedRegistry = LivePhysics();
    for (std::size_t ScopedI = 0; ScopedI < ScopedRegistry.Count; ++ScopedI)
    {
        if (ScopedRegistry.Records[ScopedI].System == this &&
            ScopedRegistry.Records[ScopedI].Instance == MInstanceId)
        {
            ScopedRegistry.Records[ScopedI] = ScopedRegistry.Records[--ScopedRegistry.Count];
            break;
        }
    }
}

bool PhysicsSystem::IsLive(const PhysicsSystem* InSystem, std::uint64_t InInstanceId) noexcept
{
    if (InSystem == nullptr)
    {
        return false;
    }
    const LivePhysicsRegistry& ScopedRegistry = LivePhysics();
    for (std::size_t ScopedI = 0; ScopedI < ScopedRegistry.Count; ++ScopedI)
    {
        if (ScopedRegistry.Records[ScopedI].System == InSystem &&
            ScopedRegistry.Records[ScopedI].Instance == InInstanceId)
        {
            return true;
        }
    }
    return false;
}

void PhysicsSystem::SelectBackend(PhysicsBackendKind InKind) noexcept
{
    if (IsValid())
    {
        return;
    }
    MRequestedBackend = InKind;
}

void PhysicsSystem::SetProjectPolicy(PhysicsBackendKind InKind, std::uint32_t InBodyBudget) noexcept
{
    MRequestedBackend = InKind;
    MDesc.backend = InKind;
    MDesc.bodyBudget = InBodyBudget;
}

PhysicsCapabilities PhysicsSystem::Capabilities() const noexcept
{
    PhysicsCapabilities ScopedResult;
    if (MBackendKind == PhysicsBackendKind::Dod)
    {
        ScopedResult.Joints = false;
        ScopedResult.Ccd = false;
        ScopedResult.Deterministic = true;
        ScopedResult.LargeWorlds = true;
        ScopedResult.MaxRecommendedBodies = kMaxTrackedPhysicsBodies;
        return ScopedResult;
    }
    ScopedResult.Joints = true;
    ScopedResult.Ccd = true;
    ScopedResult.LargeWorlds = false;
    ScopedResult.MaxRecommendedBodies = 10000;
    return ScopedResult;
}

bool PhysicsSystem::Initialize(const PhysicsDesc& InDesc)
{
    Shutdown();
    MDesc = InDesc;
    if (!(MDesc.pixelsPerMeter > 0.0f))
    {
        MDesc.pixelsPerMeter = 50.0f;
    }

    PhysicsBackendKind ScopedKind =
        MRequestedBackend != PhysicsBackendKind::Auto ? MRequestedBackend : MDesc.backend;
    if (ScopedKind == PhysicsBackendKind::Auto)
    {
        const bool ScopedLarge = MDesc.bodyBudget >= 4096 || MDesc.deterministic;
        ScopedKind = MDesc.requireJoints ? PhysicsBackendKind::Box2D
                                        : (ScopedLarge ? PhysicsBackendKind::Dod : PhysicsBackendKind::Box2D);
    }
    if (ScopedKind == PhysicsBackendKind::Dod && !MDesc.requireJoints)
    {
        MDod = std::make_unique<DodPhysicsBackend>();
        if (!MDod->Initialize(MDesc))
        {
            MDod.reset();
            return false;
        }
        MBackendKind = PhysicsBackendKind::Dod;
        MWorld = 0;
        ++MGeneration;
        if (MGeneration == 0)
        {
            MGeneration = 1;
        }
        return true;
    }

    b2WorldDef ScopedDefinition = b2DefaultWorldDef();
    ScopedDefinition.gravity = b2Vec2{MDesc.gravity.X, MDesc.gravity.Y};
    const b2WorldId ScopedWorld = b2CreateWorld(&ScopedDefinition);
    if (!b2World_IsValid(ScopedWorld))
    {
        return false;
    }
    MDod.reset();
    MBackendKind = PhysicsBackendKind::Box2D;
    MWorld = b2StoreWorldId(ScopedWorld);
    // The ownership table is the rollback sweep's source of truth. Reserve once
    MBodies.reserve(kMaxTrackedPhysicsBodies);
    ++MGeneration;
    if (MGeneration == 0)
    {
        MGeneration = 1;
    }
    return true;
}

void PhysicsSystem::Shutdown()
{
    if (MDod != nullptr)
    {
        MDod->Shutdown();
        MDod.reset();
    }
    if (MWorld != 0)
    {
        b2DestroyWorld(b2LoadWorldId(MWorld));
        MWorld = 0;
    }
    MBodies.clear();
    MTrackOverflowWarned = false;
    MEventCount = 0;
}

std::uint32_t PhysicsSystem::LiveBodyCount() const noexcept
{
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        return MDod->LiveBodyCount();
    }
    return static_cast<std::uint32_t>(MBodies.size());
}

std::span<const PhysicsContactEvent> PhysicsSystem::Contacts() const noexcept
{
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        return MDod->Contacts();
    }
    return {MEvents.data(), MEventCount};
}

bool PhysicsSystem::IsValid() const noexcept
{
    if (MBackendKind == PhysicsBackendKind::Dod)
    {
        return MDod != nullptr && MDod->IsValid();
    }
    return MWorld != 0;
}

void PhysicsSystem::TrackBody(Entity InEntity, std::uint64_t InPackedBody)
{
    if (InPackedBody == 0)
    {
        return;
    }
    if (MBodies.size() >= kMaxTrackedPhysicsBodies)
    {
        if (!MTrackOverflowWarned)
        {
            MTrackOverflowWarned = true;
            LogWarn("physics", "body tracking table is full ({}); extra native bodies stay outside the prune list",
                    kMaxTrackedPhysicsBodies);
        }
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
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        return MDod->DestroyAllBodies(InRegistry);
    }
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
    if (MBackendKind == PhysicsBackendKind::Dod)
    {
        return;
    }
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
            const b2BodyId ScopedExisting = b2LoadBodyId(ScopedBody.Body);
            // A handle from a destroyed or re-created world stays packed in the
            // component; recreating the body keeps the entity simulated.
            if (b2Body_IsValid(ScopedExisting))
            {
                // Body-level edits are cheap to compare and must reach Box2D in
                // the same tick the gameplay or inspector changed them.
                if (std::fabs(b2Body_GetGravityScale(ScopedExisting) - ScopedBody.GravityScale) > 1.0e-6f)
                {
                    b2Body_SetGravityScale(ScopedExisting, ScopedBody.GravityScale);
                }
                if (std::fabs(b2Body_GetLinearDamping(ScopedExisting) - ScopedBody.LinearDamping) > 1.0e-6f)
                {
                    b2Body_SetLinearDamping(ScopedExisting, ScopedBody.LinearDamping);
                }
                if (std::fabs(b2Body_GetAngularDamping(ScopedExisting) - ScopedBody.AngularDamping) > 1.0e-6f)
                {
                    b2Body_SetAngularDamping(ScopedExisting, ScopedBody.AngularDamping);
                }
                const bool ScopedBullet = (ScopedBody.Flags & RigidBodyBullet) != 0;
                if (b2Body_IsBullet(ScopedExisting) != ScopedBullet)
                {
                    b2Body_SetBullet(ScopedExisting, ScopedBullet);
                }
                const b2BodyType ScopedDesiredType = ToBoxType(ScopedBody.Type);
                if (b2Body_GetType(ScopedExisting) != ScopedDesiredType)
                {
                    b2Body_SetType(ScopedExisting, ScopedDesiredType);
                }
                const bool ScopedFixedRotation = (ScopedBody.Flags & RigidBodyFixedRotation) != 0;
                b2MotionLocks ScopedLocks = b2Body_GetMotionLocks(ScopedExisting);
                if (ScopedLocks.angularZ != ScopedFixedRotation)
                {
                    ScopedLocks.angularZ = ScopedFixedRotation;
                    b2Body_SetMotionLocks(ScopedExisting, ScopedLocks);
                }

                if (ScopedCollider.Shape == 0)
                {
                    const b2ShapeId ScopedNewShape =
                        CreateColliderShape(ScopedExisting, ScopedCollider, MDesc.pixelsPerMeter);
                    ScopedCollider.Shape =
                        b2Shape_IsValid(ScopedNewShape) ? b2StoreShapeId(ScopedNewShape) : 0;
                    ApplyShapeFilter(ScopedCollider);
                }
                else
                {
                    const b2ShapeId ScopedShape = b2LoadShapeId(ScopedCollider.Shape);
                    if (!b2Shape_IsValid(ScopedShape))
                    {
                        ScopedCollider.Shape = 0;
                    }
                    else
                    {
                        bool ScopedShapeChanged = false;
                        // Joint links and other systems may create circles with a
                        // Collider2D marker; only polygons are described by half
                        // extents, so the size compare is polygon-only.
                        if (b2Shape_GetType(ScopedShape) == b2_polygonShape)
                        {
                            const b2Polygon ScopedPolygon = b2Shape_GetPolygon(ScopedShape);
                            float ScopedActualHalfX = 0.0f;
                            float ScopedActualHalfY = 0.0f;
                            for (int ScopedVertex = 0; ScopedVertex < ScopedPolygon.count; ++ScopedVertex)
                            {
                                ScopedActualHalfX =
                                    std::max(ScopedActualHalfX, std::fabs(ScopedPolygon.vertices[ScopedVertex].x));
                                ScopedActualHalfY =
                                    std::max(ScopedActualHalfY, std::fabs(ScopedPolygon.vertices[ScopedVertex].y));
                            }
                            const float ScopedExpectedHalfX =
                                std::max(ScopedCollider.HalfExtents.X, 0.0f) / MDesc.pixelsPerMeter;
                            const float ScopedExpectedHalfY =
                                std::max(ScopedCollider.HalfExtents.Y, 0.0f) / MDesc.pixelsPerMeter;
                            ScopedShapeChanged =
                                ScopedShapeChanged ||
                                b2Shape_IsSensor(ScopedShape) != ScopedCollider.Sensor ||
                                std::fabs(ScopedActualHalfX - ScopedExpectedHalfX) > 1.0e-3f ||
                                std::fabs(ScopedActualHalfY - ScopedExpectedHalfY) > 1.0e-3f;
                        }
                        if (ScopedShapeChanged)
                        {
                            // Sensor and size are creation-time properties; a
                            // missing recreation left the old shape active.
                            b2DestroyShape(ScopedShape, true);
                            const b2ShapeId ScopedNewShape =
                                CreateColliderShape(ScopedExisting, ScopedCollider, MDesc.pixelsPerMeter);
                            ScopedCollider.Shape =
                                b2Shape_IsValid(ScopedNewShape) ? b2StoreShapeId(ScopedNewShape) : 0;
                            ApplyShapeFilter(ScopedCollider);
                        }
                        else
                        {
                            if (std::fabs(b2Shape_GetDensity(ScopedShape) - ScopedCollider.Density) > 1.0e-6f)
                            {
                                b2Shape_SetDensity(ScopedShape, ScopedCollider.Density, true);
                            }
                            if (std::fabs(b2Shape_GetFriction(ScopedShape) - ScopedCollider.Friction) > 1.0e-6f)
                            {
                                b2Shape_SetFriction(ScopedShape, ScopedCollider.Friction);
                            }
                            if (std::fabs(b2Shape_GetRestitution(ScopedShape) - ScopedCollider.Restitution) >
                                1.0e-6f)
                            {
                                b2Shape_SetRestitution(ScopedShape, ScopedCollider.Restitution);
                            }
                            const b2Filter ScopedFilter = b2Shape_GetFilter(ScopedShape);
                            if (ScopedFilter.categoryBits != ScopedCollider.CategoryBits ||
                                ScopedFilter.maskBits != ScopedCollider.MaskBits)
                            {
                                b2Filter ScopedNewFilter = b2DefaultFilter();
                                ScopedNewFilter.categoryBits = ScopedCollider.CategoryBits;
                                ScopedNewFilter.maskBits = ScopedCollider.MaskBits;
                                ScopedNewFilter.groupIndex = 0;
                                b2Shape_SetFilter(ScopedShape, ScopedNewFilter);
                            }
                        }
                    }
                }
                continue;
            }
            ScopedBody.Body = 0;
            ScopedCollider.Shape = 0;
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

        const b2ShapeId ScopedShapeId = CreateColliderShape(ScopedBodyId, ScopedCollider, MDesc.pixelsPerMeter);

        ScopedBody.Body = b2StoreBodyId(ScopedBodyId);
        ScopedCollider.Shape = b2Shape_IsValid(ScopedShapeId) ? b2StoreShapeId(ScopedShapeId) : 0;
        TrackBody(ScopedEntity, ScopedBody.Body);
        ApplyShapeFilter(ScopedCollider);
        ScopedBody.Position = ScopedTransform.Position;
        ScopedBody.PreviousPosition = ScopedTransform.Position;
        ScopedBody.Rotation = ScopedTransform.Rotation;
        ScopedBody.PreviousRotation = ScopedTransform.Rotation;
    }
}

// Velocity, rotation and position are outputs of the solver: they are written
// into the component by ReadBodiesIntoRegistry after the step. Copying the body
// velocity back here would overwrite a value the game assigned to the component
// before Step, so gameplay must use SetLinearVelocity/ApplyImpulse instead.
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
        // The begin event carries no manifold; query shape A's contact so
        // damage/audio consumers get a usable point and normal.
        b2ContactData ScopedContactData{};
        if (b2Shape_GetContactData(ScopedSource.shapeIdA, &ScopedContactData, 1) > 0)
        {
            ScopedEvent.normal =
                Vector2D{ScopedContactData.manifold.normal.x, ScopedContactData.manifold.normal.y};
            if (ScopedContactData.manifold.pointCount > 0)
            {
                const b2Vec2 ScopedAnchor = ScopedContactData.manifold.points[0].anchorA;
                const b2Pos ScopedCenter = b2Body_GetWorldCenter(b2Shape_GetBody(ScopedSource.shapeIdA));
                ScopedEvent.point =
                    ToPixels(b2Pos{static_cast<float>(ScopedCenter.x) + ScopedAnchor.x,
                                   static_cast<float>(ScopedCenter.y) + ScopedAnchor.y},
                             MDesc.pixelsPerMeter);
            }
        }
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
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->EnsureBodies(InRegistry);
        return;
    }
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
                const b2Filter ScopedCurrentFilter = b2Shape_GetFilter(ScopedShapeId);
                if (ScopedCurrentFilter.categoryBits != ScopedFilter.categoryBits ||
                    ScopedCurrentFilter.maskBits != ScopedFilter.maskBits)
                {
                    b2Shape_SetFilter(ScopedShapeId, ScopedFilter);
                }
                if (b2Shape_AreSensorEventsEnabled(ScopedShapeId) != ScopedCollider->ContactEvents)
                {
                    b2Shape_EnableSensorEvents(ScopedShapeId, ScopedCollider->ContactEvents);
                }
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
    if (!RequireMainThread("PhysicsSystem::Step"))
    {
        return;
    }
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        if (InFixedDt > 0.0f && MTimeScale > 0.0f)
        {
            MDod->Step(InRegistry, InFixedDt * MTimeScale);
        }
        return;
    }
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
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->Interpolate(InRegistry, InAlpha);
        return;
    }
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
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->ApplyCollisionFilterMatrix(InRegistry, InMatrix);
        return;
    }
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
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->ApplyImpulse(InRegistry, InEntity, InImpulse, InWake);
        return;
    }
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

void PhysicsSystem::SetLinearVelocity(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InVelocity, bool InWake)
{
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->SetLinearVelocity(InRegistry, InEntity, InVelocity, InWake);
        return;
    }
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
    b2Body_SetLinearVelocity(ScopedBodyId, ToBoxVelocity(InVelocity, MDesc.pixelsPerMeter));
    ScopedBody->LinearVelocity = InVelocity;
    if (InWake)
    {
        b2Body_SetAwake(ScopedBodyId, true);
    }
}

void PhysicsSystem::SetAngularVelocity(Ecs::Registry& InRegistry, Entity InEntity, float InAngularVelocity, bool InWake)
{
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->SetAngularVelocity(InRegistry, InEntity, InAngularVelocity, InWake);
        return;
    }
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
    b2Body_SetAngularVelocity(ScopedBodyId, InAngularVelocity);
    ScopedBody->AngularVelocity = InAngularVelocity;
    if (InWake)
    {
        b2Body_SetAwake(ScopedBodyId, true);
    }
}

void PhysicsSystem::SetFixedRotation(Ecs::Registry& InRegistry, Entity InEntity, bool InFixed) noexcept
{
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->SetFixedRotation(InRegistry, InEntity, InFixed);
        return;
    }
    if (MWorld == 0 || !InRegistry.valid(InEntity))
    {
        return;
    }
    RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity);
    if (ScopedBody == nullptr)
    {
        return;
    }
    if (InFixed)
    {
        ScopedBody->Flags |= RigidBodyFixedRotation;
    }
    else
    {
        ScopedBody->Flags &= ~RigidBodyFixedRotation;
    }
    if (ScopedBody->Body == 0 || ScopedBody->Type == BodyType::Static)
    {
        return;
    }
    const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody->Body);
    if (!b2Body_IsValid(ScopedBodyId))
    {
        return;
    }
    b2MotionLocks ScopedLocks = b2Body_GetMotionLocks(ScopedBodyId);
    ScopedLocks.angularZ = InFixed;
    b2Body_SetMotionLocks(ScopedBodyId, ScopedLocks);
    if (InFixed)
    {
        b2Body_SetAngularVelocity(ScopedBodyId, 0.0f);
        ScopedBody->AngularVelocity = 0.0f;
    }
}

void PhysicsSystem::SetBullet(Ecs::Registry& InRegistry, Entity InEntity, bool InBullet) noexcept
{
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->SetBullet(InRegistry, InEntity, InBullet);
        return;
    }
    if (MWorld == 0 || !InRegistry.valid(InEntity))
    {
        return;
    }
    RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity);
    if (ScopedBody == nullptr)
    {
        return;
    }
    if (InBullet)
    {
        ScopedBody->Flags |= RigidBodyBullet;
    }
    else
    {
        ScopedBody->Flags &= ~RigidBodyBullet;
    }
    if (ScopedBody->Body == 0 || ScopedBody->Type == BodyType::Static)
    {
        return;
    }
    const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody->Body);
    if (b2Body_IsValid(ScopedBodyId))
    {
        b2Body_SetBullet(ScopedBodyId, InBullet);
    }
}

void PhysicsSystem::ApplyAcceleration(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InAcceleration, bool InWake)
{
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->ApplyAcceleration(InRegistry, InEntity, InAcceleration, InWake);
        return;
    }
    if (MWorld == 0 || !InRegistry.valid(InEntity))
    {
        return;
    }
    auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity);
    if (ScopedBody == nullptr || ScopedBody->Body == 0 || ScopedBody->Type != BodyType::Dynamic)
    {
        return;
    }
    const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody->Body);
    if (!b2Body_IsValid(ScopedBodyId))
    {
        return;
    }
    const float ScopedTimeScale = EntityTimeScale(InRegistry, InEntity);
    if (ScopedTimeScale <= 0.0f || MDesc.pixelsPerMeter <= 0.0f)
    {
        return;
    }
    // F = m * a; Box2D works in metres, so divide the pixel acceleration by ppm.
    const float ScopedMass = std::max(b2Body_GetMass(ScopedBodyId), 0.0f);
    const b2Vec2 ScopedForce{ScopedMass * InAcceleration.X / MDesc.pixelsPerMeter,
                             ScopedMass * InAcceleration.Y / MDesc.pixelsPerMeter};
    b2Body_ApplyForceToCenter(ScopedBodyId, ScopedForce * ScopedTimeScale, InWake);
}

bool PhysicsSystem::RayCast(Ecs::Registry& InRegistry, Vector2D InFrom, Vector2D InTo, Entity InIgnore,
                             std::uint32_t InMask, PhysicsRayHit& OutHit) const noexcept
{
    const Vector2D ScopedDelta = InTo - InFrom;
    const float ScopedLength = std::sqrt(ScopedDelta.Dot(ScopedDelta));
    if (!(ScopedLength > 0.0001f))
    {
        return false;
    }

    // The Box2D backend answers the query from its broadphase, so the ray sees
    // rotated shapes and scaled bodies instead of the registry AABB approximation.
    if (MBackendKind == PhysicsBackendKind::Box2D && MWorld != 0)
    {
        b2QueryFilter ScopedFilter = b2DefaultQueryFilter();
        ScopedFilter.categoryBits = 0xFFFFFFFFFFFFFFFFull;
        ScopedFilter.maskBits = InMask;
        RayCastCollector ScopedCollector;
        ScopedCollector.ignore = InIgnore;
        (void)b2World_CastRay(b2LoadWorldId(MWorld), ToBox(InFrom, MDesc.pixelsPerMeter),
                              b2Vec2{ScopedDelta.X / MDesc.pixelsPerMeter, ScopedDelta.Y / MDesc.pixelsPerMeter},
                              ScopedFilter, &CollectRayHit, &ScopedCollector);
        if (!ScopedCollector.found)
        {
            return false;
        }
        OutHit.Entity = EntityFromShape(ScopedCollector.shape);
        OutHit.Point = ToPixels(ScopedCollector.point, MDesc.pixelsPerMeter);
        OutHit.Normal = Vector2D{ScopedCollector.normal.x, ScopedCollector.normal.y};
        OutHit.Distance = ScopedCollector.fraction * ScopedLength;
        return true;
    }

    bool ScopedFound = false;
    float ScopedBest = std::numeric_limits<float>::max();
    auto ScopedView = InRegistry.view<Collider2D, Transform2D>();
    for (auto [ScopedEntity, ScopedCollider, ScopedTransform] : ScopedView.each())
    {
        if (ScopedEntity == InIgnore || ScopedCollider.Sensor)
        {
            continue;
        }
        if ((ScopedCollider.CategoryBits & InMask) == 0)
        {
            continue;
        }
        const Vector2D ScopedHalf{
            std::max(ScopedCollider.HalfExtents.X, 0.001f),
            std::max(ScopedCollider.HalfExtents.Y, 0.001f),
        };
        // Rotate the ray into the collider's local frame so a rotated sprite
        // hits its oriented box instead of an axis-aligned approximation.
        const float ScopedCos = Sim::Cos(ScopedTransform.Rotation);
        const float ScopedSin = Sim::Sin(ScopedTransform.Rotation);
        const Vector2D ScopedRelative = InFrom - ScopedTransform.Position;
        const Vector2D ScopedOrigin{ScopedRelative.X * ScopedCos + ScopedRelative.Y * ScopedSin,
                                    -ScopedRelative.X * ScopedSin + ScopedRelative.Y * ScopedCos};
        const Vector2D ScopedLocalDirection{ScopedDelta.X * ScopedCos + ScopedDelta.Y * ScopedSin,
                                            -ScopedDelta.X * ScopedSin + ScopedDelta.Y * ScopedCos};
        float ScopedEnter = 0.0f;
        float ScopedExit = 1.0f;
        Vector2D ScopedNormal{};
        bool ScopedMiss = false;
        for (std::uint32_t ScopedAxis = 0; ScopedAxis < 2; ++ScopedAxis)
        {
            const float ScopedAxisOrigin = ScopedAxis == 0 ? ScopedOrigin.X : ScopedOrigin.Y;
            const float ScopedAxisDirection = ScopedAxis == 0 ? ScopedLocalDirection.X : ScopedLocalDirection.Y;
            const float ScopedLow = ScopedAxis == 0 ? -ScopedHalf.X : -ScopedHalf.Y;
            const float ScopedHigh = ScopedAxis == 0 ? ScopedHalf.X : ScopedHalf.Y;
            if (std::abs(ScopedAxisDirection) < 0.00001f)
            {
                if (ScopedAxisOrigin < ScopedLow || ScopedAxisOrigin > ScopedHigh)
                {
                    ScopedMiss = true;
                    break;
                }
                continue;
            }
            float ScopedNear = (ScopedLow - ScopedAxisOrigin) / ScopedAxisDirection;
            float ScopedFar = (ScopedHigh - ScopedAxisOrigin) / ScopedAxisDirection;
            float ScopedSign = -1.0f;
            if (ScopedNear > ScopedFar)
            {
                std::swap(ScopedNear, ScopedFar);
                ScopedSign = 1.0f;
            }
            if (ScopedNear > ScopedEnter)
            {
                ScopedEnter = ScopedNear;
                const Vector2D ScopedLocalNormal =
                    ScopedAxis == 0 ? Vector2D{ScopedSign, 0.0f} : Vector2D{0.0f, ScopedSign};
                ScopedNormal = Vector2D{ScopedLocalNormal.X * ScopedCos - ScopedLocalNormal.Y * ScopedSin,
                                        ScopedLocalNormal.X * ScopedSin + ScopedLocalNormal.Y * ScopedCos};
            }
            ScopedExit = std::min(ScopedExit, ScopedFar);
            if (ScopedEnter > ScopedExit)
            {
                ScopedMiss = true;
                break;
            }
        }
        if (ScopedMiss || ScopedEnter > 1.0f)
        {
            continue;
        }
        const float ScopedDistance = ScopedEnter * ScopedLength;
        if (ScopedDistance < ScopedBest)
        {
            ScopedBest = ScopedDistance;
            ScopedFound = true;
            OutHit.Entity = ScopedEntity;
            OutHit.Distance = ScopedDistance;
            OutHit.Point = InFrom + ScopedDelta * ScopedEnter;
            OutHit.Normal = ScopedNormal;
        }
    }
    return ScopedFound;
}

void PhysicsSystem::SetGravity(Vector2D InGravity) noexcept
{
    if (MBackendKind == PhysicsBackendKind::Dod && MDod != nullptr)
    {
        MDod->SetGravity(InGravity);
    }
    if (MDesc.gravity.X == InGravity.X && MDesc.gravity.Y == InGravity.Y)
    {
        return;
    }
    MDesc.gravity = InGravity;
    if (MWorld != 0)
    {
        b2World_SetGravity(b2LoadWorldId(MWorld), b2Vec2{InGravity.X, InGravity.Y});
    }
}

namespace
{
void PhysicsFixedStepThunk(Ecs::PhysicsSystemView& InView, void* InUser)
{
    auto* ScopedPhysics = static_cast<PhysicsSystem*>(InUser);
    if (ScopedPhysics != nullptr && ScopedPhysics->IsValid())
    {
        ScopedPhysics->Step(InView.registry, InView.dt);
    }
}
}

bool PhysicsFixedStepSystem::Register(Ecs::SystemGraph& InGraph, PhysicsSystem& InSystem,
                                      std::uint64_t InDependencies)
{
    return InGraph.Add(Ecs::Phase::FixedUpdate, "PhysicsFixedStepSystem",
                       Ecs::BindPhysicsSystem<&PhysicsFixedStepThunk>(), &InSystem, InDependencies);
}
}
