// FemboyEngine2D - transport group attachment, nesting and detach tests.
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/TransportGroups.hpp"
#include "Engine/Script/ScriptSystem.hpp"
#include "Engine/Script/TextScript.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

[[nodiscard]] Fby::Entity MakeEntity(Fby::Ecs::Registry& InRegistry, Fby::Vector2D InPosition)
{
    const Fby::Entity ScopedEntity = InRegistry.create();
    Fby::Transform2D ScopedTransform{};
    ScopedTransform.Position = InPosition;
    InRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
    return ScopedEntity;
}

void TestRigidAndNestedGroups()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    PhysicsSystem ScopedPhysics;
    TransportGroupSystem ScopedGroups;

    const Entity ScopedTrain = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    const Entity ScopedLift = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    const Entity ScopedCrate = MakeEntity(ScopedRegistry, Vector2D{10.0f, 0.0f});

    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedTrain, 1));
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedLift, 2, 1));
    FBY_CHECK(ScopedGroups.SetParentGroup(ScopedRegistry, 2, 1));
    FBY_CHECK(ScopedGroups.Attach(ScopedRegistry, ScopedCrate, 2, kCarrierModeRigid, kCarrierFlagInheritRotation));

    ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    FBY_CHECK(ScopedRegistry.get<Transform2D>(ScopedCrate).Position.X > 9.9f);

    ScopedRegistry.get<Transform2D>(ScopedTrain).Position = Vector2D{10.0f, 0.0f};
    ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    FBY_CHECK(std::fabs(ScopedRegistry.get<Transform2D>(ScopedLift).Position.X - 10.0f) < 1e-4f);
    FBY_CHECK(std::fabs(ScopedRegistry.get<Transform2D>(ScopedCrate).Position.X - 20.0f) < 1e-4f);

    ScopedRegistry.get<Transform2D>(ScopedTrain).Rotation = 1.5707963f;
    ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    const Transform2D& ScopedCrateTransform = ScopedRegistry.get<Transform2D>(ScopedCrate);
    FBY_CHECK(std::fabs(ScopedCrateTransform.Position.X - 10.0f) < 1e-3f);
    FBY_CHECK(std::fabs(ScopedCrateTransform.Position.Y - 10.0f) < 1e-3f);
    FBY_CHECK(std::fabs(ScopedCrateTransform.Rotation - 1.5707963f) < 1e-3f);
}

void TestDetachKeepsVelocity()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    PhysicsSystem ScopedPhysics;
    TransportGroupSystem ScopedGroups;

    const Entity ScopedCarrier = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    const Entity ScopedCargo = MakeEntity(ScopedRegistry, Vector2D{4.0f, 0.0f});
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedCarrier, 7));
    FBY_CHECK(ScopedGroups.Attach(ScopedRegistry, ScopedCargo, 7, kCarrierModeCarriedDynamic, 0));

    RigidBody2D ScopedBody{};
    ScopedBody.Type = BodyType::Dynamic;
    ScopedBody.Body = 1;
    ScopedBody.LinearVelocity = Vector2D{120.0f, 0.0f};
    ScopedRegistry.emplace<RigidBody2D>(ScopedCargo, ScopedBody);
    FBY_CHECK(ScopedGroups.Detach(ScopedRegistry, ScopedCargo, true));
    FBY_CHECK(ScopedRegistry.try_get<CarrierMemberComponent>(ScopedCargo) == nullptr);
    FBY_CHECK(std::fabs(ScopedRegistry.get<RigidBody2D>(ScopedCargo).LinearVelocity.X - 120.0f) < 1e-4f);
}

void TestBoundsDetach()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    PhysicsSystem ScopedPhysics;
    TransportGroupSystem ScopedGroups;

    const Entity ScopedCarrier = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    const Entity ScopedCargo = MakeEntity(ScopedRegistry, Vector2D{6.0f, 0.0f});
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedCarrier, 3, 0, kTransportRotationFixed,
                                       kTransportGroupEnabled | kTransportGroupAutoDetachOutside));
    TransportGroupComponent* ScopedGroup = ScopedRegistry.try_get<TransportGroupComponent>(ScopedCarrier);
    FBY_CHECK(ScopedGroup != nullptr);
    ScopedGroup->BoundsMin = Vector2D{-2.0f, -2.0f};
    ScopedGroup->BoundsMax = Vector2D{2.0f, 2.0f};
    FBY_CHECK(ScopedGroups.Attach(ScopedRegistry, ScopedCargo, 3, kCarrierModeRigid, 0));

    ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    FBY_CHECK(ScopedRegistry.try_get<CarrierMemberComponent>(ScopedCargo) == nullptr);
}

void TestRequestedDetach()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    PhysicsSystem ScopedPhysics;
    TransportGroupSystem ScopedGroups;

    const Entity ScopedCarrier = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    const Entity ScopedCargo = MakeEntity(ScopedRegistry, Vector2D{1.0f, 0.0f});
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedCarrier, 4));
    FBY_CHECK(ScopedGroups.Attach(ScopedRegistry, ScopedCargo, 4, kCarrierModeSupport, 0));
    CarrierMemberComponent* ScopedMember = ScopedRegistry.try_get<CarrierMemberComponent>(ScopedCargo);
    FBY_CHECK(ScopedMember != nullptr);
    ScopedMember->Flags |= kCarrierFlagDetachRequested;
    ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 0.01f);
    FBY_CHECK(ScopedRegistry.try_get<CarrierMemberComponent>(ScopedCargo) == nullptr);
}

void TestPhysicsCarry()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    PhysicsSystem ScopedPhysics;
    PhysicsDesc ScopedPhysicsDesc{};
    ScopedPhysicsDesc.gravity = Vector2D{0.0f, 0.0f};
    FBY_CHECK(ScopedPhysics.Initialize(ScopedPhysicsDesc));

    const Entity ScopedCarrier = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    RigidBody2D ScopedCarrierBody{};
    ScopedCarrierBody.Type = BodyType::Kinematic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedCarrier, ScopedCarrierBody);
    Collider2D ScopedCarrierCollider{};
    ScopedCarrierCollider.HalfExtents = Vector2D{16.0f, 4.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedCarrier, ScopedCarrierCollider);

    const Entity ScopedCargo = MakeEntity(ScopedRegistry, Vector2D{0.0f, 40.0f});
    RigidBody2D ScopedCargoBody{};
    ScopedCargoBody.Type = BodyType::Dynamic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedCargo, ScopedCargoBody);
    Collider2D ScopedCargoCollider{};
    ScopedCargoCollider.HalfExtents = Vector2D{4.0f, 4.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedCargo, ScopedCargoCollider);
    ScopedPhysics.EnsureBodies(ScopedRegistry);

    TransportGroupSystem ScopedGroups;
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedCarrier, 20));
    FBY_CHECK(ScopedGroups.Attach(ScopedRegistry, ScopedCargo, 20, kCarrierModeCarriedDynamic, 0));
    ScopedPhysics.SetLinearVelocity(ScopedRegistry, ScopedCarrier, Vector2D{120.0f, 0.0f}, true);

    for (std::uint32_t ScopedStep = 0; ScopedStep < 60; ++ScopedStep)
    {
        ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
        ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
    }
    ScopedPhysics.Interpolate(ScopedRegistry, 1.0f);
    const Transform2D& ScopedCargoTransform = ScopedRegistry.get<Transform2D>(ScopedCargo);
    FBY_CHECK(ScopedCargoTransform.Position.X > 20.0f);
    FBY_CHECK(std::fabs(ScopedCargoTransform.Position.Y - 40.0f) < 5.0f);
    ScopedPhysics.Shutdown();
}

void TestRigidWeld()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    PhysicsSystem ScopedPhysics;
    PhysicsDesc ScopedPhysicsDesc{};
    ScopedPhysicsDesc.gravity = Vector2D{0.0f, 0.0f};
    FBY_CHECK(ScopedPhysics.Initialize(ScopedPhysicsDesc));

    const Entity ScopedCarrier = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    RigidBody2D ScopedCarrierBody{};
    ScopedCarrierBody.Type = BodyType::Kinematic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedCarrier, ScopedCarrierBody);
    Collider2D ScopedCarrierCollider{};
    ScopedCarrierCollider.HalfExtents = Vector2D{16.0f, 4.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedCarrier, ScopedCarrierCollider);

    const Entity ScopedCargo = MakeEntity(ScopedRegistry, Vector2D{0.0f, 40.0f});
    RigidBody2D ScopedCargoBody{};
    ScopedCargoBody.Type = BodyType::Dynamic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedCargo, ScopedCargoBody);
    Collider2D ScopedCargoCollider{};
    ScopedCargoCollider.HalfExtents = Vector2D{4.0f, 4.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedCargo, ScopedCargoCollider);
    ScopedPhysics.EnsureBodies(ScopedRegistry);

    TransportGroupSystem ScopedGroups;
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedCarrier, 30));
    FBY_CHECK(ScopedGroups.Attach(ScopedRegistry, ScopedCargo, 30, kCarrierModeRigid, kCarrierFlagInheritRotation));
    ScopedPhysics.SetLinearVelocity(ScopedRegistry, ScopedCarrier, Vector2D{120.0f, 0.0f}, true);

    for (std::uint32_t ScopedStep = 0; ScopedStep < 60; ++ScopedStep)
    {
        ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
        ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
    }
    ScopedPhysics.Interpolate(ScopedRegistry, 1.0f);
    const CarrierMemberComponent* ScopedMember = ScopedRegistry.try_get<CarrierMemberComponent>(ScopedCargo);
    FBY_CHECK(ScopedMember != nullptr);
    if (ScopedMember != nullptr)
    {
        FBY_CHECK(ScopedMember->Joint != 0);
    }
    FBY_CHECK(ScopedRegistry.get<Transform2D>(ScopedCargo).Position.X > 20.0f);
    ScopedPhysics.Shutdown();
}

void TestDynamicRoot()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    PhysicsSystem ScopedPhysics;
    PhysicsDesc ScopedPhysicsDesc{};
    ScopedPhysicsDesc.gravity = Vector2D{0.0f, 0.0f};
    FBY_CHECK(ScopedPhysics.Initialize(ScopedPhysicsDesc));

    const Entity ScopedRoot = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    RigidBody2D ScopedRootBody{};
    ScopedRootBody.Type = BodyType::Dynamic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedRoot, ScopedRootBody);
    Collider2D ScopedRootCollider{};
    ScopedRootCollider.HalfExtents = Vector2D{8.0f, 8.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedRoot, ScopedRootCollider);
    ScopedPhysics.EnsureBodies(ScopedRegistry);

    TransportGroupSystem ScopedGroups;
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedRoot, 31, 0, kTransportRotationFixed));
    ScopedPhysics.SetLinearVelocity(ScopedRegistry, ScopedRoot, Vector2D{2400.0f, 0.0f}, true);
    ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
    const RigidBody2D& ScopedAfterPre = ScopedRegistry.get<RigidBody2D>(ScopedRoot);
    FBY_CHECK((ScopedAfterPre.Flags & RigidBodyFixedRotation) != 0);
    FBY_CHECK((ScopedAfterPre.Flags & RigidBodyBullet) != 0);
    ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
    ScopedPhysics.Shutdown();
}

void TestParentWeld()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    PhysicsSystem ScopedPhysics;
    PhysicsDesc ScopedPhysicsDesc{};
    ScopedPhysicsDesc.gravity = Vector2D{0.0f, 0.0f};
    FBY_CHECK(ScopedPhysics.Initialize(ScopedPhysicsDesc));

    const Entity ScopedParent = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    RigidBody2D ScopedParentBody{};
    ScopedParentBody.Type = BodyType::Kinematic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedParent, ScopedParentBody);
    Collider2D ScopedParentCollider{};
    ScopedParentCollider.HalfExtents = Vector2D{8.0f, 4.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedParent, ScopedParentCollider);

    const Entity ScopedChild = MakeEntity(ScopedRegistry, Vector2D{0.0f, 20.0f});
    RigidBody2D ScopedChildBody{};
    ScopedChildBody.Type = BodyType::Dynamic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedChild, ScopedChildBody);
    Collider2D ScopedChildCollider{};
    ScopedChildCollider.HalfExtents = Vector2D{4.0f, 4.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedChild, ScopedChildCollider);
    ScopedPhysics.EnsureBodies(ScopedRegistry);

    TransportGroupSystem ScopedGroups;
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedParent, 40));
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedChild, 41, 40));
    FBY_CHECK(ScopedGroups.SetParentGroup(ScopedRegistry, 41, 40));
    ScopedPhysics.SetLinearVelocity(ScopedRegistry, ScopedParent, Vector2D{120.0f, 0.0f}, true);
    ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
    const TransportGroupComponent* ScopedChildGroup = ScopedRegistry.try_get<TransportGroupComponent>(ScopedChild);
    FBY_CHECK(ScopedChildGroup != nullptr);
    if (ScopedChildGroup != nullptr)
    {
        FBY_CHECK(ScopedChildGroup->ParentJoint != 0);
    }
    for (std::uint32_t ScopedStep = 0; ScopedStep < 30; ++ScopedStep)
    {
        ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
        ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
    }
    ScopedPhysics.Interpolate(ScopedRegistry, 1.0f);
    FBY_CHECK(ScopedRegistry.get<Transform2D>(ScopedChild).Position.X > 10.0f);
    ScopedPhysics.Shutdown();
}

void TestScriptAttach()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    TransportGroupSystem ScopedGroups;
    const Entity ScopedCarrier = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
    const Entity ScopedMember = MakeEntity(ScopedRegistry, Vector2D{4.0f, 0.0f});
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedCarrier, 1));
    FBY_CHECK(ScopedGroups.Attach(ScopedRegistry, ScopedMember, 1, 1));
    FBY_CHECK(ScopedGroups.Detach(ScopedRegistry, ScopedMember, true));

    Scene::ComponentRegistry ScopedComponents;
    Scene::RegisterBuiltInComponents(ScopedComponents);
    constexpr std::string_view ScopedSource = R"FBY(
event update {
    transport_attach(1, 1);
}
)FBY";
    std::string ScopedError;
    std::vector<std::byte> ScopedProgram;
    const bool ScopedCompiled =
        Script::CompileTextScript(ScopedSource, ScopedComponents, {}, &ScopedProgram, &ScopedError);
    if (!ScopedCompiled)
    {
        std::fprintf(stderr, "script compile: %s\n", ScopedError.c_str());
    }
    FBY_CHECK(ScopedCompiled);

    ScriptRuntime ScopedRuntime;
    FBY_CHECK(ScopedRuntime.Initialize());
    ScopedRuntime.SetTransportGroups(&ScopedGroups);
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedProgram, &ScopedError);
    FBY_CHECK(ScopedHandle != kScriptNoProgram);

    Script::VmContext ScopedContext{ScopedRegistry};
    ScopedContext.transport = &ScopedGroups;
    FBY_CHECK(RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedMember, Script::ScriptEvent::Update, ScopedContext));
    FBY_CHECK(ScopedRegistry.try_get<CarrierMemberComponent>(ScopedMember) != nullptr);
}

void TestCycleRejected()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    TransportGroupSystem ScopedGroups;
    const Entity ScopedA = MakeEntity(ScopedRegistry, Vector2D{});
    const Entity ScopedB = MakeEntity(ScopedRegistry, Vector2D{});
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedA, 10));
    FBY_CHECK(ScopedGroups.CreateGroup(ScopedRegistry, ScopedB, 11, 10));
    FBY_CHECK(!ScopedGroups.SetParentGroup(ScopedRegistry, 10, 11));
    FBY_CHECK(!ScopedGroups.SetParentGroup(ScopedRegistry, 10, 10));
}
}

int main()
{
    TestRigidAndNestedGroups();
    TestDetachKeepsVelocity();
    TestBoundsDetach();
    TestRequestedDetach();
    TestPhysicsCarry();
    TestRigidWeld();
    TestDynamicRoot();
    TestParentWeld();
    TestScriptAttach();
    TestCycleRejected();

    if (GFailures == 0)
    {
        std::printf("transport_groups_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "transport_groups_test: %u failure(s)\n", GFailures);
    return 1;
}
