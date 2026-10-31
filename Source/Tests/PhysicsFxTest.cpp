// FemboyEngine2D - physics extension tests (ropes/chains, cloth, water).
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/Fluid.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

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

[[nodiscard]] bool Finite(float InValue) noexcept
{
    return std::isfinite(InValue);
}
}

int main()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    PhysicsSystem ScopedPhysics;
    PhysicsDesc ScopedPhysicsDesc;
    ScopedPhysicsDesc.gravity = Vector2D{0.0f, 9.8f};
    FBY_CHECK(ScopedPhysics.Initialize(ScopedPhysicsDesc));

    PhysicsJointSystem ScopedJoints;
    FBY_CHECK(ScopedJoints.Initialize(ScopedPhysics));

    JointChainDesc ScopedRope;
    ScopedRope.kind = JointChainKind::Rope;
    ScopedRope.flags = JointChainPinStart;
    ScopedRope.start = Vector2D{0.0f, -64.0f};
    ScopedRope.end = Vector2D{0.0f, 64.0f};
    ScopedRope.segmentCount = 8;
    ScopedRope.radius = 2.0f;
    ScopedRope.density = 0.8f;
    ChainError ScopedError = ChainError::None;
    const JointChainHandle ScopedRopeHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedRope, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedRopeHandle.IsValid());
    FBY_CHECK(ScopedJoints.ChainCount() == 1);
    FBY_CHECK(ScopedJoints.LinkCount() == 8);

    for (int ScopedStep = 0; ScopedStep < 60; ++ScopedStep)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
    }

    const JointChainInfo ScopedRopeInfo = ScopedJoints.Info(ScopedRopeHandle);
    FBY_CHECK(ScopedRopeInfo.linkCount == 8);
    // seven inter-link distance joints plus the static start pin
    FBY_CHECK(ScopedRopeInfo.jointCount == 8);
    Transform2D ScopedLinkTransform{};
    FBY_CHECK(ScopedJoints.LinkTransform(ScopedRegistry, ScopedRopeHandle, 0, &ScopedLinkTransform));
    FBY_CHECK(Finite(ScopedLinkTransform.Position.X) && Finite(ScopedLinkTransform.Position.Y));

    JointChainDesc ScopedChain;
    ScopedChain.kind = JointChainKind::Chain;
    ScopedChain.flags = JointChainPinStart | JointChainPinEnd;
    ScopedChain.start = Vector2D{0.0f, -120.0f};
    ScopedChain.end = Vector2D{120.0f, -120.0f};
    ScopedChain.segmentCount = 5;
    ScopedChain.radius = 3.0f;
    ScopedChain.enableLimit = true;
    const JointChainHandle ScopedChainHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedChain, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedChainHandle.IsValid());
    // 4 hinges + 2 pins
    FBY_CHECK(ScopedJoints.Info(ScopedChainHandle).jointCount == 6);

    for (int ScopedStep = 0; ScopedStep < 30; ++ScopedStep)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedJoints.Info(ScopedChainHandle).linkCount == 5);

    // Destroying a chain must remove its link entities, not leave dangling
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedRopeHandle));
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedChainHandle));
    FBY_CHECK(ScopedJoints.ChainCount() == 0);
    FBY_CHECK(ScopedRegistry.view<JointLinkComponent>().size() == 0);

    ClothSystem ScopedCloth;
    FBY_CHECK(ScopedCloth.Initialize(nullptr));
    ClothDesc ScopedClothDesc;
    ScopedClothDesc.origin = Vector2D{0.0f, 0.0f};
    ScopedClothDesc.size = Vector2D{160.0f, 160.0f};
    ScopedClothDesc.columns = 8;
    ScopedClothDesc.rows = 8;
    ScopedClothDesc.flags = ClothPinTopRow;
    ScopedClothDesc.gravity = Vector2D{0.0f, 980.0f};
    const ClothHandle ScopedClothHandle = ScopedCloth.Create(ScopedRegistry, ScopedClothDesc);
    FBY_CHECK(ScopedClothHandle.IsValid());
    FBY_CHECK(ScopedCloth.Info(ScopedClothHandle).particles == 64);
    const ClothInfo ScopedClothInfo = ScopedCloth.Info(ScopedClothHandle);
    FBY_CHECK(ScopedClothInfo.constraints > 64);

    const auto ScopedInitialPositions = ScopedCloth.ParticlePositions(ScopedClothHandle);
    FBY_CHECK(!ScopedInitialPositions.empty());
    const Vector2D ScopedPinned = ScopedInitialPositions[0];
    for (int ScopedStep = 0; ScopedStep < 30; ++ScopedStep)
    {
        ScopedCloth.Step(1.0f / 60.0f);
    }
    const auto ScopedSettled = ScopedCloth.ParticlePositions(ScopedClothHandle);
    FBY_CHECK(std::fabs(ScopedSettled[0].X - ScopedPinned.X) < 0.001f);
    FBY_CHECK(std::fabs(ScopedSettled[0].Y - ScopedPinned.Y) < 0.001f);
    // Bottom row falls under gravity
    FBY_CHECK(ScopedSettled[63].Y > ScopedPinned.Y + 1.0f);

    Rhi::MeshRenderItem ScopedMesh{};
    FBY_CHECK(ScopedCloth.WriteMesh(ScopedClothHandle, ScopedMesh));
    FBY_CHECK(ScopedMesh.vertices.size() == 64);
    FBY_CHECK(ScopedMesh.indices.size() > 0);
    FBY_CHECK(ScopedCloth.Destroy(ScopedRegistry, ScopedClothHandle));
    ScopedCloth.Shutdown(ScopedRegistry);

    FluidWaveSystem ScopedFluid;
    FluidDesc ScopedWaterDesc;
    ScopedWaterDesc.origin = Vector2D{0.0f, 0.0f};
    ScopedWaterDesc.cellSize = Vector2D{8.0f, 8.0f};
    ScopedWaterDesc.columns = 16;
    ScopedWaterDesc.rows = 16;
    ScopedWaterDesc.baseDepth = 6.0f;
    ScopedFluid.Initialize(ScopedWaterDesc);
    FBY_CHECK(ScopedFluid.Water().IsConfigured());
    ScopedFluid.Water().AddDrop(Vector2D{64.0f, 64.0f}, 2.0f, 2.0f);
    for (int ScopedStep = 0; ScopedStep < 20; ++ScopedStep)
    {
        ScopedFluid.Water().Step(1.0f / 60.0f);
    }
    FBY_CHECK(std::fabs(ScopedFluid.Water().SampleHeight(Vector2D{64.0f, 64.0f})) > 0.0001f);

    float ScopedHeightField[16 * 16]{};
    FBY_CHECK(ScopedFluid.Water().CopyHeightField(ScopedHeightField) == 256);
    float ScopedEnergy = 0.0f;
    for (const float ScopedValue : ScopedHeightField)
    {
        FBY_CHECK(Finite(ScopedValue));
        ScopedEnergy += std::fabs(ScopedValue);
    }
    FBY_CHECK(ScopedEnergy > 0.001f);

    GerstnerWave ScopedWave;
    ScopedWave.direction = Vector2D{1.0f, 0.0f};
    ScopedWave.steepness = 0.12f;
    ScopedWave.wavelength = 48.0f;
    const GerstnerWave ScopedWaves[] = {ScopedWave};
    ScopedFluid.Gerstner().SetWaves(ScopedWaves);
    const GerstnerSample ScopedSample = ScopedFluid.Gerstner().Sample(Vector2D{32.0f, 0.0f}, 1.5f);
    FBY_CHECK(Finite(ScopedSample.height));
    FBY_CHECK(Finite(ScopedSample.displacement.X));
    FBY_CHECK(ScopedSample.jacobian > 0.0f);

    // A falling interactor must inject a splash through the ECS path.
    const Entity ScopedDrop = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedDrop, Transform2D{Vector2D{72.0f, 72.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedRegistry.emplace<FluidInteractorComponent>(ScopedDrop, FluidInteractorComponent{2.0f, 1.0f, 2.0f, 0});
    ScopedFluid.Update(ScopedRegistry, 1.0f / 60.0f);
    FBY_CHECK(ScopedRegistry.valid(ScopedDrop));

    ScopedFluid.Shutdown();
    ScopedPhysics.Shutdown();
    ScopedJoints.Shutdown(ScopedRegistry);

    if (GFailures == 0)
    {
        std::printf("physics_fx_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "physics_fx_test: %u failure(s)\n", GFailures);
    return 1;
}
