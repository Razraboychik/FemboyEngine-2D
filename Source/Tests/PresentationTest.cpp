// FemboyEngine2D - presentation components, collision policy, time scale and
// static/dynamic light sources.
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/LightSource.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/Physics/Physics.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

using Fby::BodyType;
using Fby::Collider2D;
using Fby::CollisionMode;
using Fby::CollisionPolicyComponent;
using Fby::Ecs::Registry;
using Fby::Entity;
using Fby::Light2DTargetComponent;
using Fby::LightSourceComponent;
using Fby::LightSourceSystem;
using Fby::PhysicsDesc;
using Fby::PhysicsSystem;
using Fby::RenderMaskComponent;
using Fby::RigidBody2D;
using Fby::SleepingStaticComponent;
using Fby::Sprite2D;
using Fby::TimeScaleComponent;
using Fby::Transform2D;
using Fby::Vector2D;

Entity AddGround(Registry& InRegistry, float InY, bool InSleepingStatic)
{
    const Entity ScopedEntity = InRegistry.create();
    Transform2D ScopedTransform{};
    ScopedTransform.Position = Vector2D{0.0f, InY};
    InRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
    RigidBody2D ScopedBody{};
    ScopedBody.Type = BodyType::Static;
    InRegistry.emplace<RigidBody2D>(ScopedEntity, ScopedBody);
    Collider2D ScopedCollider{};
    ScopedCollider.HalfExtents = Vector2D{200.0f, 10.0f};
    InRegistry.emplace<Collider2D>(ScopedEntity, ScopedCollider);
    if (InSleepingStatic)
    {
        InRegistry.emplace<SleepingStaticComponent>(ScopedEntity, SleepingStaticComponent{});
    }
    return ScopedEntity;
}

Entity AddFaller(Registry& InRegistry, float InY, bool InDynamic, CollisionMode InMode, bool InSetPolicy)
{
    const Entity ScopedEntity = InRegistry.create();
    Transform2D ScopedTransform{};
    ScopedTransform.Position = Vector2D{0.0f, InY};
    InRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
    RigidBody2D ScopedBody{};
    ScopedBody.Type = InDynamic ? BodyType::Dynamic : BodyType::Static;
    InRegistry.emplace<RigidBody2D>(ScopedEntity, ScopedBody);
    Collider2D ScopedCollider{};
    ScopedCollider.HalfExtents = Vector2D{8.0f, 8.0f};
    InRegistry.emplace<Collider2D>(ScopedEntity, ScopedCollider);
    if (InSetPolicy)
    {
        CollisionPolicyComponent ScopedPolicy{};
        ScopedPolicy.SetMode(InMode);
        InRegistry.emplace<CollisionPolicyComponent>(ScopedEntity, ScopedPolicy);
    }
    return ScopedEntity;
}

int TestComponentContract()
{
    FBY_CHECK(sizeof(CollisionPolicyComponent) == 16);
    FBY_CHECK(sizeof(RenderMaskComponent) == 8);
    FBY_CHECK(sizeof(TimeScaleComponent) == 8);
    FBY_CHECK(sizeof(SleepingStaticComponent) == 16);
    FBY_CHECK(sizeof(LightSourceComponent) == 40);

    FBY_CHECK(CollisionModeIsValid(CollisionMode::NoCollision));
    FBY_CHECK(CollisionModeIsValid(CollisionMode::ComplexDynamic));
    FBY_CHECK(!CollisionModeIsValid(static_cast<CollisionMode>(200)));
    FBY_CHECK(CollisionModeIsStatic(CollisionMode::SimpleStatic));
    FBY_CHECK(!CollisionModeIsStatic(CollisionMode::NoCollision));
    FBY_CHECK(CollisionModeIsSensor(CollisionMode::QueryOnly));
    FBY_CHECK(CollisionModeIsSensor(CollisionMode::TriggerOnly));
    FBY_CHECK(!CollisionModeIsSensor(CollisionMode::SimpleDynamic));
    FBY_CHECK(CollisionModeEmitsEvents(CollisionMode::TriggerOnly));
    FBY_CHECK(CollisionModeUsesSdf(CollisionMode::ComplexStatic));
    FBY_CHECK(!CollisionModeUsesSdf(CollisionMode::SimpleDynamic));
    FBY_CHECK(std::strcmp(CollisionModeName(CollisionMode::ComplexDynamic), "ComplexDynamic") == 0);

    RenderMaskComponent ScopedMask{};
    FBY_CHECK(ScopedMask.Allows(Fby::RenderPassSprites));
    ScopedMask.ExcludePass(Fby::RenderPassLighting);
    FBY_CHECK(!ScopedMask.Allows(Fby::RenderPassLighting));
    ScopedMask.IncludePass(Fby::RenderPassLighting);
    FBY_CHECK(ScopedMask.Allows(Fby::RenderPassLighting));

    SleepingStaticComponent ScopedStatic{};
    FBY_CHECK(ScopedStatic.IsSleeping());
    FBY_CHECK(ScopedStatic.WakesOnContact());
    FBY_CHECK(!ScopedStatic.WakesOnTrigger());

    FBY_CHECK(Fby::LightConeAttenuation(0.0f, Fby::kFullTurnRadians, 0.2f) == 1.0f);
    FBY_CHECK(Fby::LightConeAttenuation(3.0f, Fby::kFullTurnRadians, 0.2f) == 1.0f);
    FBY_CHECK(Fby::LightConeAttenuation(0.1f, 1.0f, 0.1f) > 0.99f);
    FBY_CHECK(Fby::LightConeAttenuation(0.9f, 1.0f, 0.1f) == 0.0f);
    const float ScopedEdge = Fby::LightConeAttenuation(0.49f, 1.0f, 0.1f);
    FBY_CHECK(ScopedEdge > 0.0f && ScopedEdge < 1.0f);
    FBY_CHECK(Fby::LightDistanceAttenuation(0.0f, 100.0f, 2.0f) == 1.0f);
    FBY_CHECK(Fby::LightDistanceAttenuation(100.0f, 100.0f, 2.0f) == 0.0f);
    FBY_CHECK(std::fabs(Fby::LightDistanceAttenuation(50.0f, 100.0f, 2.0f) - 0.25f) < 1.0e-4f);

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    FBY_CHECK(ScopedComponents.Find("CollisionPolicyComponent") != nullptr);
    FBY_CHECK(ScopedComponents.Find("RenderMaskComponent") != nullptr);
    FBY_CHECK(ScopedComponents.Find("TimeScaleComponent") != nullptr);
    FBY_CHECK(ScopedComponents.Find("SleepingStaticComponent") != nullptr);
    FBY_CHECK(ScopedComponents.Find("LightSourceComponent") != nullptr);
    return 0;
}

int TestCollisionModes()
{
    PhysicsDesc ScopedDesc{};
    ScopedDesc.gravity = Vector2D{0.0f, 9.8f};
    ScopedDesc.pixelsPerMeter = 50.0f;

    {
        PhysicsSystem ScopedPhysics;
        FBY_CHECK(ScopedPhysics.Initialize(ScopedDesc));
        Registry ScopedRegistry;
        (void)AddGround(ScopedRegistry, 100.0f, false);
        const Entity ScopedFaller = AddFaller(ScopedRegistry, 0.0f, true, CollisionMode::NoCollision, true);
        for (int ScopedTick = 0; ScopedTick < 120; ++ScopedTick)
        {
            ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
        }
        // Transform2D is written by Interpolate; the solver's own position is
        // the authoritative value here.
        const float ScopedY = ScopedRegistry.get<RigidBody2D>(ScopedFaller).Position.Y;
        FBY_CHECK(ScopedY > 150.0f);
    }
    {
        PhysicsSystem ScopedPhysics;
        FBY_CHECK(ScopedPhysics.Initialize(ScopedDesc));
        Registry ScopedRegistry;
        (void)AddGround(ScopedRegistry, 100.0f, false);
        const Entity ScopedFaller = AddFaller(ScopedRegistry, 0.0f, true, CollisionMode::SimpleDynamic, true);
        for (int ScopedTick = 0; ScopedTick < 120; ++ScopedTick)
        {
            ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
        }
        const float ScopedY = ScopedRegistry.get<RigidBody2D>(ScopedFaller).Position.Y;
        FBY_CHECK(ScopedY > 60.0f && ScopedY < 95.0f);
    }
    return 0;
}

int TestTimeScale()
{
    PhysicsDesc ScopedDesc{};
    ScopedDesc.gravity = Vector2D{0.0f, 0.0f};
    ScopedDesc.pixelsPerMeter = 50.0f;
    PhysicsSystem ScopedPhysics;
    FBY_CHECK(ScopedPhysics.Initialize(ScopedDesc));
    Registry ScopedRegistry;
    const Entity ScopedFrozenBody = AddFaller(ScopedRegistry, 0.0f, true, CollisionMode::SimpleDynamic, false);
    ScopedRegistry.get<RigidBody2D>(ScopedFrozenBody).LinearVelocity = Vector2D{100.0f, 0.0f};
    TimeScaleComponent ScopedFrozen{};
    ScopedFrozen.Scale = 0.0f;
    ScopedRegistry.emplace<TimeScaleComponent>(ScopedFrozenBody, ScopedFrozen);
    const Entity ScopedFreeBody = AddFaller(ScopedRegistry, 0.0f, true, CollisionMode::SimpleDynamic, false);
    ScopedRegistry.get<RigidBody2D>(ScopedFreeBody).LinearVelocity = Vector2D{100.0f, 0.0f};
    ScopedPhysics.EnsureBodies(ScopedRegistry);
    for (int ScopedTick = 0; ScopedTick < 30; ++ScopedTick)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
    }
    FBY_CHECK(std::fabs(ScopedRegistry.get<RigidBody2D>(ScopedFrozenBody).Position.X) < 1.0f);
    FBY_CHECK(ScopedRegistry.get<RigidBody2D>(ScopedFreeBody).Position.X > 10.0f);

    ScopedPhysics.SetTimeScale(0.0f);
    const float ScopedBefore = ScopedRegistry.get<RigidBody2D>(ScopedFreeBody).Position.X;
    for (int ScopedTick = 0; ScopedTick < 30; ++ScopedTick)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
    }
    FBY_CHECK(std::fabs(ScopedRegistry.get<RigidBody2D>(ScopedFreeBody).Position.X - ScopedBefore) < 0.001f);
    ScopedPhysics.SetTimeScale(1.0f);
    for (int ScopedTick = 0; ScopedTick < 30; ++ScopedTick)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
    }
    FBY_CHECK(ScopedRegistry.get<RigidBody2D>(ScopedFreeBody).Position.X > ScopedBefore + 10.0f);
    return 0;
}

int TestSleepingStaticWake()
{
    PhysicsDesc ScopedDesc{};
    ScopedDesc.gravity = Vector2D{0.0f, 9.8f};
    ScopedDesc.pixelsPerMeter = 50.0f;
    PhysicsSystem ScopedPhysics;
    FBY_CHECK(ScopedPhysics.Initialize(ScopedDesc));
    Registry ScopedRegistry;
    const Entity ScopedGround = AddGround(ScopedRegistry, 100.0f, true);
    (void)AddFaller(ScopedRegistry, 0.0f, true, CollisionMode::SimpleDynamic, false);
    for (int ScopedTick = 0; ScopedTick < 120; ++ScopedTick)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
    }
    const SleepingStaticComponent& ScopedState = ScopedRegistry.get<SleepingStaticComponent>(ScopedGround);
    FBY_CHECK(!ScopedState.IsSleeping());
    FBY_CHECK(ScopedState.WakeCount >= 1);
    FBY_CHECK(ScopedState.LastWakeReason == Fby::kStaticWakeContact);
    return 0;
}

int TestLightSourceSystem()
{
    Registry ScopedRegistry;
    const Entity ScopedTargetEntity = ScopedRegistry.create();
    Transform2D ScopedTargetTransform{};
    ScopedTargetTransform.Position = Vector2D{0.0f, 0.0f};
    ScopedRegistry.emplace<Transform2D>(ScopedTargetEntity, ScopedTargetTransform);
    Sprite2D ScopedSprite{};
    ScopedSprite.Size = Vector2D{32.0f, 32.0f};
    ScopedRegistry.emplace<Sprite2D>(ScopedTargetEntity, ScopedSprite);
    Light2DTargetComponent ScopedTarget{};
    ScopedRegistry.emplace<Light2DTargetComponent>(ScopedTargetEntity, ScopedTarget);

    const Entity ScopedLamp = ScopedRegistry.create();
    Transform2D ScopedLampTransform{};
    ScopedLampTransform.Position = Vector2D{40.0f, 0.0f};
    ScopedRegistry.emplace<Transform2D>(ScopedLamp, ScopedLampTransform);
    LightSourceComponent ScopedSource{};
    ScopedSource.Radius = 200.0f;
    ScopedSource.Intensity = 1.0f;
    ScopedRegistry.emplace<LightSourceComponent>(ScopedLamp, ScopedSource);
    ScopedRegistry.emplace<SleepingStaticComponent>(ScopedLamp, SleepingStaticComponent{});

    LightSourceSystem ScopedSystem;
    Fby::LightSourceSettings ScopedSettings{};
    ScopedSettings.Tonemap = false;
    FBY_CHECK(ScopedSystem.Run(ScopedRegistry, ScopedSettings, nullptr) == 1);
    FBY_CHECK(ScopedSystem.LightCount() == 1);
    FBY_CHECK(ScopedSystem.StaticLightCount() == 1);
    FBY_CHECK(ScopedSystem.DynamicLightCount() == 0);
    FBY_CHECK(ScopedSystem.StaticRebuildCount() == 1);
    FBY_CHECK(ScopedSystem.StaticCacheHitCount() == 0);
    FBY_CHECK(ScopedRegistry.try_get<Fby::Light2DResultComponent>(ScopedTargetEntity) != nullptr);

    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings, nullptr);
    FBY_CHECK(ScopedSystem.StaticRebuildCount() == 1);
    FBY_CHECK(ScopedSystem.StaticCacheHitCount() == 1);

    ScopedRegistry.get<Transform2D>(ScopedLamp).Position = Vector2D{60.0f, 0.0f};
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings, nullptr);
    FBY_CHECK(ScopedSystem.StaticRebuildCount() == 2);

    Fby::WakeSleepingStatic(ScopedRegistry, ScopedLamp, Fby::kStaticWakeProgram);
    FBY_CHECK(ScopedRegistry.get<SleepingStaticComponent>(ScopedLamp).WakeCount == 1);
    FBY_CHECK((ScopedRegistry.get<LightSourceComponent>(ScopedLamp).Flags & Fby::LightSourceStatic) == 0);
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings, nullptr);
    FBY_CHECK(ScopedSystem.StaticLightCount() == 0);
    FBY_CHECK(ScopedSystem.DynamicLightCount() == 1);

    LightSourceComponent ScopedNoContact{};
    ScopedNoContact.Flags = Fby::LightSourceEnabled | Fby::LightSourceStatic | Fby::LightSourceSleeping;
    FBY_CHECK(!Fby::WakeLightSource(ScopedNoContact, true));
    FBY_CHECK(Fby::WakeLightSource(ScopedNoContact, false));
    FBY_CHECK(!Fby::WakeLightSource(ScopedNoContact, false));
    return 0;
}
}

int main()
{
    (void)TestComponentContract();
    (void)TestCollisionModes();
    (void)TestTimeScale();
    (void)TestSleepingStaticWake();
    (void)TestLightSourceSystem();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "presentation: %u failure(s)\n", GFailures);
        return 1;
    }
    std::printf("presentation: ok\n");
    return 0;
}
