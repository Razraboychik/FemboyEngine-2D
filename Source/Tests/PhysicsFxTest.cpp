// FemboyEngine2D - physics extension tests (joints, cloth, water).
#include "Engine/ECS/CommandLog.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Scene.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/Fluid.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"

#include <cmath>
#include <cstdint>
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
            ++GFailures;                                                                                               \
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
    ChainError ScopedError = ChainError::None;

    // A uniform rope keeps the original construction path.
    JointChainDesc ScopedRope;
    ScopedRope.Kind = JointChainKind::Rope;
    ScopedRope.Flags = JointChainPinStart;
    ScopedRope.Start = Vector2D{0.0f, -64.0f};
    ScopedRope.End = Vector2D{0.0f, 64.0f};
    ScopedRope.SegmentCount = 8;
    ScopedRope.Radius = 2.0f;
    ScopedRope.Density = 0.8f;
    const JointChainHandle ScopedRopeHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedRope, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedRopeHandle.IsValid());
    FBY_CHECK(ScopedJoints.ChainCount() == 1);
    FBY_CHECK(ScopedJoints.LinkCount() == 8);

    for (int ScopedStep = 0; ScopedStep < 60; ++ScopedStep)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
        ScopedJoints.Update(ScopedRegistry, 1.0f / 60.0f);
    }

    const JointChainInfo ScopedRopeInfo = ScopedJoints.Info(ScopedRopeHandle);
    FBY_CHECK(ScopedRopeInfo.LinkCount == 8);
    // seven inter-link distance joints plus the static start pin
    FBY_CHECK(ScopedRopeInfo.JointCount == 8);
    FBY_CHECK(ScopedRopeInfo.BrokenCount == 0);
    Transform2D ScopedLinkTransform{};
    FBY_CHECK(ScopedJoints.LinkTransform(ScopedRegistry, ScopedRopeHandle, 0, &ScopedLinkTransform));
    FBY_CHECK(Finite(ScopedLinkTransform.Position.X) && Finite(ScopedLinkTransform.Position.Y));
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedRopeHandle));

    // Authored rope tension: a non-breakable spring rope reports base + distance
    // tension and never emits a break even though MaxTension is small.
    JointChainDesc ScopedTensionRope;
    ScopedTensionRope.Kind = JointChainKind::Rope;
    ScopedTensionRope.Flags = JointChainPinStart | JointChainSpringRope;
    ScopedTensionRope.Start = Vector2D{0.0f, 0.0f};
    ScopedTensionRope.End = Vector2D{0.0f, 96.0f};
    ScopedTensionRope.SegmentCount = 4;
    ScopedTensionRope.Radius = 2.0f;
    ScopedTensionRope.BaseTension = 25.0f;
    ScopedTensionRope.MaxTension = 1.0f;
    ScopedTensionRope.TensionPerDistance = 0.5f;
    ScopedTensionRope.TensionDistance = 2.0f;
    ScopedTensionRope.StrengthEnabled = false;
    const JointChainHandle ScopedTensionHandle =
        ScopedJoints.CreateChain(ScopedRegistry, ScopedTensionRope, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None && ScopedTensionHandle.IsValid());
    for (int ScopedStep = 0; ScopedStep < 10; ++ScopedStep)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
        ScopedJoints.Update(ScopedRegistry, 1.0f / 60.0f);
    }
    JointConnectionInfo ScopedTensionInfo{};
    FBY_CHECK(ScopedJoints.ChainConnectionCount(ScopedTensionHandle) > 0);
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedTensionHandle, 0, &ScopedTensionInfo));
    FBY_CHECK(!ScopedTensionInfo.StrengthEnabled);
    FBY_CHECK(ScopedTensionInfo.CurrentTension >= ScopedTensionRope.BaseTension);
    FBY_CHECK(ScopedJoints.Info(ScopedTensionHandle).BrokenCount == 0);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedTensionHandle));

    // A chain that runs chain, rope and chain again. The rope section is springy,
    // so every connection it touches becomes a SpringRope tie.
    const JointChainSection ScopedSections[] = {
        {JointChainKind::Chain, 0, 4, 4.0f, 1.0f, 4000.0f, 500.0f},
        {JointChainKind::Rope, JointChainSpringRope, 6, 1.5f, 0.5f, 900.0f, 0.0f},
        {JointChainKind::Chain, 0, 4, 4.0f, 1.0f, 4000.0f, 500.0f},
    };
    JointChainDesc ScopedMixed;
    ScopedMixed.Kind = JointChainKind::Chain;
    ScopedMixed.Flags = JointChainPinStart | JointChainPinEnd;
    ScopedMixed.Start = Vector2D{-128.0f, 0.0f};
    ScopedMixed.End = Vector2D{128.0f, 0.0f};
    ScopedMixed.Sections = std::span<const JointChainSection>(ScopedSections);
    ScopedMixed.Radius = 4.0f;
    ScopedMixed.Density = 1.0f;
    const JointChainHandle ScopedMixedHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedMixed, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedMixedHandle.IsValid());
    const JointChainInfo ScopedMixedInfo = ScopedJoints.Info(ScopedMixedHandle);
    FBY_CHECK(ScopedMixedInfo.LinkCount == 14);
    // 13 inter-link connections plus two pins
    FBY_CHECK(ScopedMixedInfo.JointCount == 15);
    FBY_CHECK(ScopedJoints.LinkCount() == 14);

    JointConnectionInfo ScopedConnection{};
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedMixedHandle, 0, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Kind == JointConnectionKind::Hinge);
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedMixedHandle, 3, &ScopedConnection));
    // The boundary between the first chain run and the rope run is a rope knot.
    FBY_CHECK(ScopedConnection.Kind == JointConnectionKind::SpringRope);
    FBY_CHECK(std::fabs(ScopedConnection.BreakForce - 900.0f) < 1.0e-3f);
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedMixedHandle, 9, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Kind == JointConnectionKind::SpringRope);
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedMixedHandle, 10, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Kind == JointConnectionKind::Hinge);
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedMixedHandle, 13, &ScopedConnection));
    FBY_CHECK((ScopedConnection.Flags & JointConnectionPinned) != 0);
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedMixedHandle, 14, &ScopedConnection));
    FBY_CHECK((ScopedConnection.Flags & JointConnectionPinned) != 0);

    // Link radii follow the sections: the rope run is thinner than its neighbours.
    float ScopedThickRadius = 0.0f;
    float ScopedThinRadius = 0.0f;
    auto ScopedLinkView = ScopedRegistry.view<JointLinkComponent>();
    for (const Entity ScopedEntity : ScopedLinkView)
    {
        const JointLinkComponent& ScopedMarker = ScopedRegistry.get<JointLinkComponent>(ScopedEntity);
        if (ScopedMarker.LinkIndex == 1)
        {
            ScopedThickRadius = ScopedMarker.Radius;
        }
        if (ScopedMarker.LinkIndex == 5)
        {
            ScopedThinRadius = ScopedMarker.Radius;
        }
    }
    FBY_CHECK(ScopedThickRadius > ScopedThinRadius);
    FBY_CHECK(ScopedThinRadius > 0.0f);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedMixedHandle));

    // A taut chain pinned at both ends must rest without pre-compression, so a
    // break limit above the actual load stays intact.
    JointChainDesc ScopedTaut;
    ScopedTaut.Kind = JointChainKind::Rope;
    ScopedTaut.Flags = JointChainPinStart | JointChainPinEnd;
    ScopedTaut.Start = Vector2D{0.0f, -64.0f};
    ScopedTaut.End = Vector2D{0.0f, 64.0f};
    ScopedTaut.SegmentCount = 4;
    ScopedTaut.Radius = 2.0f;
    ScopedTaut.BreakForce = 5.0f;
    const JointChainHandle ScopedTautHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedTaut, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedTautHandle.IsValid());
    for (int ScopedStep = 0; ScopedStep < 30; ++ScopedStep)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
        ScopedJoints.Update(ScopedRegistry, 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedJoints.Info(ScopedTautHandle).BrokenCount == 0);
    FBY_CHECK(ScopedJoints.Info(ScopedTautHandle).JointCount == 5);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedTautHandle));

    // A break limit below the hanging weight must break the rope, keep the link
    // entities alive and report the event.
    JointChainDesc ScopedBreak;
    ScopedBreak.Kind = JointChainKind::Rope;
    ScopedBreak.Flags = JointChainPinStart | JointChainTrackStress;
    ScopedBreak.Start = Vector2D{0.0f, -64.0f};
    ScopedBreak.End = Vector2D{0.0f, 64.0f};
    ScopedBreak.SegmentCount = 4;
    ScopedBreak.Radius = 2.0f;
    ScopedBreak.Density = 1.0f;
    ScopedBreak.BreakForce = 0.05f;
    const JointChainHandle ScopedBreakHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedBreak, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedBreakHandle.IsValid());
    FBY_CHECK(ScopedJoints.Info(ScopedBreakHandle).JointCount == 4);
    for (int ScopedStep = 0; ScopedStep < 20; ++ScopedStep)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
        ScopedJoints.Update(ScopedRegistry, 1.0f / 60.0f);
    }
    FBY_CHECK(!ScopedJoints.Breaks().empty());
    const JointChainInfo ScopedBrokenInfo = ScopedJoints.Info(ScopedBreakHandle);
    FBY_CHECK(ScopedBrokenInfo.BrokenCount >= 1);
    FBY_CHECK(ScopedBrokenInfo.JointCount < 4);
    FBY_CHECK(ScopedBrokenInfo.LinkCount == 4);
    // Breaking must not destroy the links: they remain drawable debris.
    FBY_CHECK(ScopedRegistry.view<JointLinkComponent>().size() == 4);
    ScopedJoints.ClearBreaks();
    FBY_CHECK(ScopedJoints.Breaks().empty());
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedBreakHandle));
    FBY_CHECK(ScopedRegistry.view<JointLinkComponent>().size() == 0);

    // Cut, repair and stress telemetry on a strong hanging rope. Both ends pinned
    // would put the distance joints in compression and hide the stress signal.
    JointChainDesc ScopedCut;
    ScopedCut.Kind = JointChainKind::Rope;
    ScopedCut.Flags = JointChainPinStart | JointChainTrackStress;
    ScopedCut.Start = Vector2D{0.0f, -64.0f};
    ScopedCut.End = Vector2D{0.0f, 64.0f};
    ScopedCut.SegmentCount = 5;
    ScopedCut.Radius = 2.0f;
    ScopedCut.BreakForce = 1000.0f;
    const JointChainHandle ScopedCutHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedCut, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedCutHandle.IsValid());
    for (int ScopedStep = 0; ScopedStep < 6; ++ScopedStep)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
        ScopedJoints.Update(ScopedRegistry, 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedCutHandle, 0, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Stress > 0.0f);
    const std::uint32_t ScopedLiveJoints = ScopedJoints.Info(ScopedCutHandle).JointCount;
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedCutHandle, 2, &ScopedConnection));
    const JointConnectionHandle ScopedCutTarget = ScopedConnection.Handle;
    FBY_CHECK(ScopedJoints.CutConnection(ScopedRegistry, ScopedCutTarget));
    FBY_CHECK(ScopedJoints.Info(ScopedCutHandle).BrokenCount == 1);
    FBY_CHECK(ScopedJoints.Info(ScopedCutHandle).JointCount == ScopedLiveJoints - 1);
    FBY_CHECK(ScopedJoints.ConnectionInfo(ScopedCutTarget, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Broken);
    FBY_CHECK(ScopedJoints.RepairConnection(ScopedRegistry, ScopedCutTarget, &ScopedError));
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedJoints.Info(ScopedCutHandle).BrokenCount == 0);
    FBY_CHECK(ScopedJoints.Info(ScopedCutHandle).JointCount == ScopedLiveJoints);

    // A world-space cut picks the nearest live connection.
    Transform2D ScopedCutLinkA{};
    Transform2D ScopedCutLinkB{};
    FBY_CHECK(ScopedJoints.LinkTransform(ScopedRegistry, ScopedCutHandle, 2, &ScopedCutLinkA));
    FBY_CHECK(ScopedJoints.LinkTransform(ScopedRegistry, ScopedCutHandle, 3, &ScopedCutLinkB));
    const Vector2D ScopedCutPoint = (ScopedCutLinkA.Position + ScopedCutLinkB.Position) * 0.5f;
    FBY_CHECK(ScopedJoints.CutAtPoint(ScopedRegistry, ScopedCutHandle, ScopedCutPoint, 16.0f));
    FBY_CHECK(ScopedJoints.Info(ScopedCutHandle).BrokenCount == 1);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedCutHandle));

    // Two ropes tied together: the tie is a graph edge, not a chain member, and
    // destroying either chain removes it without leaving a stale joint.
    JointChainDesc ScopedTieA;
    ScopedTieA.Kind = JointChainKind::Rope;
    ScopedTieA.Flags = JointChainPinStart;
    ScopedTieA.Start = Vector2D{-64.0f, -64.0f};
    ScopedTieA.End = Vector2D{-64.0f, 64.0f};
    ScopedTieA.SegmentCount = 4;
    JointChainDesc ScopedTieB = ScopedTieA;
    ScopedTieB.Start = Vector2D{64.0f, -64.0f};
    ScopedTieB.End = Vector2D{64.0f, 64.0f};
    const JointChainHandle ScopedTieAHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedTieA, &ScopedError);
    const JointChainHandle ScopedTieBHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedTieB, &ScopedError);
    FBY_CHECK(ScopedTieAHandle.IsValid() && ScopedTieBHandle.IsValid());

    JointTieDesc ScopedTieDesc;
    ScopedTieDesc.Kind = JointConnectionKind::SpringRope;
    ScopedTieDesc.BreakForce = 10.0f;
    const JointConnectionHandle ScopedTieHandle =
        ScopedJoints.TieChains(ScopedRegistry, ScopedTieAHandle, 1, ScopedTieBHandle, 1, ScopedTieDesc, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedTieHandle.IsValid());
    FBY_CHECK(ScopedJoints.TieCount() == 1);
    FBY_CHECK(ScopedJoints.ConnectionInfo(ScopedTieHandle, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Kind == JointConnectionKind::SpringRope);
    FBY_CHECK((ScopedConnection.Flags & JointConnectionTie) != 0);
    FBY_CHECK(!ScopedConnection.Broken);
    FBY_CHECK(ScopedJoints.Untie(ScopedTieHandle));
    FBY_CHECK(ScopedJoints.TieCount() == 0);
    FBY_CHECK(
        ScopedJoints.TieChains(ScopedRegistry, ScopedTieAHandle, 2, ScopedTieBHandle, 2, ScopedTieDesc, &ScopedError)
            .IsValid());
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedTieAHandle));
    FBY_CHECK(ScopedJoints.TieCount() == 0);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedTieBHandle));

    // A springy hinge keeps the original chain kind but reports SpringHinge.
    JointChainDesc ScopedSpringHinge;
    ScopedSpringHinge.Kind = JointChainKind::Chain;
    ScopedSpringHinge.Flags = JointChainPinStart | JointChainSpringHinge;
    ScopedSpringHinge.Start = Vector2D{0.0f, -32.0f};
    ScopedSpringHinge.End = Vector2D{0.0f, 32.0f};
    ScopedSpringHinge.SegmentCount = 3;
    const JointChainHandle ScopedSpringHingeHandle =
        ScopedJoints.CreateChain(ScopedRegistry, ScopedSpringHinge, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedSpringHingeHandle.IsValid());
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedSpringHingeHandle, 0, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Kind == JointConnectionKind::SpringHinge);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedSpringHingeHandle));

    // The component authoring path keeps its sections and writes the stable id.
    JointChainComponent ScopedChainComponent;
    ScopedChainComponent.Kind = static_cast<std::uint32_t>(JointChainKind::Rope);
    ScopedChainComponent.Flags = JointChainPinStart;
    ScopedChainComponent.Start = Vector2D{0.0f, -32.0f};
    ScopedChainComponent.End = Vector2D{0.0f, 32.0f};
    ScopedChainComponent.SectionCount = 1;
    ScopedChainComponent.Sections[0] =
        JointChainSection{JointChainKind::Rope, JointChainSpringRope, 2, 2.0f, 1.0f, 0.0f, 0.0f};
    ScopedChainComponent.DefaultBreakForce = 500.0f;
    const JointChainHandle ScopedComponentHandle =
        ScopedJoints.CreateChain(ScopedRegistry, ScopedChainComponent, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedComponentHandle.IsValid());
    FBY_CHECK(ScopedChainComponent.ChainID == ScopedJoints.Info(ScopedComponentHandle).ChainID);
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedComponentHandle, 0, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Kind == JointConnectionKind::SpringRope);
    FBY_CHECK(ScopedConnection.BreakForce == 500.0f);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedComponentHandle));

    // Damage: a section that survives the instantaneous limit accumulates
    // damage under sustained overstress and snaps later with a Damage reason.
    JointChainSection ScopedDamageSection;
    ScopedDamageSection.Kind = JointChainKind::Rope;
    ScopedDamageSection.LinkCount = 4;
    ScopedDamageSection.Radius = 2.0f;
    ScopedDamageSection.BreakForce = 5.0f;
    ScopedDamageSection.DamageRate = 10.0f;
    ScopedDamageSection.DamageThreshold = 0.1f;
    JointChainDesc ScopedDamage;
    ScopedDamage.Kind = JointChainKind::Rope;
    ScopedDamage.Flags = JointChainPinStart;
    ScopedDamage.Start = Vector2D{0.0f, -64.0f};
    ScopedDamage.End = Vector2D{0.0f, 64.0f};
    ScopedDamage.Sections = std::span<const JointChainSection>(&ScopedDamageSection, 1);
    ScopedDamage.Radius = 2.0f;
    ScopedDamage.Density = 1.0f;
    const JointChainHandle ScopedDamageHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedDamage, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedDamageHandle.IsValid());
    ScopedJoints.ClearBreaks();
    for (int ScopedStep = 0; ScopedStep < 3; ++ScopedStep)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
        ScopedJoints.Update(ScopedRegistry, 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedJoints.Info(ScopedDamageHandle).BrokenCount == 0);
    for (int ScopedStep = 0; ScopedStep < 60; ++ScopedStep)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
        ScopedJoints.Update(ScopedRegistry, 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedJoints.Info(ScopedDamageHandle).BrokenCount >= 1);
    bool ScopedSawDamage = false;
    for (const ChainBreakEvent& ScopedEvent : ScopedJoints.Breaks())
    {
        ScopedSawDamage = ScopedSawDamage || ScopedEvent.Reason == JointBreakReason::Damage;
    }
    FBY_CHECK(ScopedSawDamage);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedDamageHandle));

    // BrokenMask persistence: capture, destroy, recreate from the component and
    // reapply the state. The second create must restore the same cut.
    JointChainComponent ScopedState;
    ScopedState.Kind = static_cast<std::uint32_t>(JointChainKind::Rope);
    ScopedState.Flags = JointChainPinStart;
    ScopedState.Start = Vector2D{0.0f, -64.0f};
    ScopedState.End = Vector2D{0.0f, 64.0f};
    ScopedState.SegmentCount = 5;
    ScopedState.Radius = 2.0f;
    const JointChainHandle ScopedStateHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedState, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedStateHandle.IsValid());
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedStateHandle, 2, &ScopedConnection));
    FBY_CHECK(ScopedJoints.CutConnection(ScopedRegistry, ScopedConnection.Handle));
    ScopedJoints.CaptureChainState(ScopedStateHandle, &ScopedState);
    FBY_CHECK((ScopedState.BrokenMaskLow & (std::uint64_t{1} << 2)) != 0);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedStateHandle));
    const JointChainHandle ScopedRestoredHandle = ScopedJoints.CreateChain(ScopedRegistry, ScopedState, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedRestoredHandle.IsValid());
    FBY_CHECK(ScopedJoints.Info(ScopedRestoredHandle).BrokenCount == 1);
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedRestoredHandle, 2, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Broken);
    FBY_CHECK(ScopedJoints.ApplyChainState(ScopedRegistry, ScopedRestoredHandle, ScopedState, &ScopedError));
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedJoints.Info(ScopedRestoredHandle).BrokenCount == 1);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedRestoredHandle));

    // Ties survive a save/restore cycle through JointTieSave and FindChain.
    JointChainDesc ScopedSavedTieA;
    ScopedSavedTieA.Kind = JointChainKind::Rope;
    ScopedSavedTieA.Flags = JointChainPinStart;
    ScopedSavedTieA.Start = Vector2D{-48.0f, -48.0f};
    ScopedSavedTieA.End = Vector2D{-48.0f, 48.0f};
    ScopedSavedTieA.SegmentCount = 3;
    JointChainDesc ScopedSavedTieB = ScopedSavedTieA;
    ScopedSavedTieB.Start = Vector2D{48.0f, -48.0f};
    ScopedSavedTieB.End = Vector2D{48.0f, 48.0f};
    const JointChainHandle ScopedSavedA = ScopedJoints.CreateChain(ScopedRegistry, ScopedSavedTieA, &ScopedError);
    const JointChainHandle ScopedSavedB = ScopedJoints.CreateChain(ScopedRegistry, ScopedSavedTieB, &ScopedError);
    FBY_CHECK(ScopedSavedA.IsValid() && ScopedSavedB.IsValid());
    FBY_CHECK(ScopedJoints.FindChain(ScopedJoints.Info(ScopedSavedA).ChainID) == ScopedSavedA);
    JointTieDesc ScopedSavedTieDesc;
    ScopedSavedTieDesc.Kind = JointConnectionKind::SpringRope;
    ScopedSavedTieDesc.BreakForce = 12.0f;
    ScopedSavedTieDesc.LowerSpringForce = -30.0f;
    ScopedSavedTieDesc.UpperSpringForce = 30.0f;
    const JointConnectionHandle ScopedSavedTie =
        ScopedJoints.TieChains(ScopedRegistry, ScopedSavedA, 1, ScopedSavedB, 1, ScopedSavedTieDesc, &ScopedError);
    FBY_CHECK(ScopedSavedTie.IsValid());
    JointTieSave ScopedTieSaves[4]{};
    const std::uint32_t ScopedTieSaveCount = ScopedJoints.CaptureTies(ScopedTieSaves);
    FBY_CHECK(ScopedTieSaveCount == 1);
    FBY_CHECK(ScopedTieSaves[0].ChainA == ScopedJoints.Info(ScopedSavedA).ChainID);
    FBY_CHECK(ScopedTieSaves[0].UpperSpringForce == 30.0f);
    FBY_CHECK(!ScopedTieSaves[0].Broken);
    FBY_CHECK(ScopedJoints.Untie(ScopedSavedTie));
    FBY_CHECK(ScopedJoints.TieCount() == 0);
    FBY_CHECK(ScopedJoints.RestoreTies(ScopedRegistry, std::span<const JointTieSave>(ScopedTieSaves, 1), &ScopedError));
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedJoints.TieCount() == 1);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedSavedA));
    FBY_CHECK(ScopedJoints.TieCount() == 0);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedSavedB));

    // Ties also connect plain rigid bodies; a destroyed body prunes its tie.
    const Entity ScopedBodyEntityA = ScopedRegistry.create();
    const Entity ScopedBodyEntityB = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedBodyEntityA,
                                        Transform2D{Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedRegistry.emplace<Transform2D>(ScopedBodyEntityB,
                                        Transform2D{Vector2D{16.0f, 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    RigidBody2D ScopedPlainBody{};
    ScopedPlainBody.Type = BodyType::Dynamic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedBodyEntityA, ScopedPlainBody);
    ScopedRegistry.emplace<RigidBody2D>(ScopedBodyEntityB, ScopedPlainBody);
    Collider2D ScopedPlainCollider;
    ScopedPlainCollider.HalfExtents = Vector2D{4.0f, 4.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedBodyEntityA, ScopedPlainCollider);
    ScopedRegistry.emplace<Collider2D>(ScopedBodyEntityB, ScopedPlainCollider);
    ScopedPhysics.EnsureBodies(ScopedRegistry);
    JointTieDesc ScopedBodyTieDesc;
    ScopedBodyTieDesc.Kind = JointConnectionKind::SpringRope;
    ScopedBodyTieDesc.BreakForce = 4.0f;
    const JointConnectionHandle ScopedBodyTie =
        ScopedJoints.TieBodies(ScopedRegistry, ScopedBodyEntityA, ScopedBodyEntityB, ScopedBodyTieDesc, &ScopedError);
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedBodyTie.IsValid());
    FBY_CHECK(ScopedJoints.TieCount() == 1);
    FBY_CHECK(ScopedJoints.ConnectionInfo(ScopedBodyTie, &ScopedConnection));
    FBY_CHECK(ScopedConnection.ChainA == kNoChain);
    FBY_CHECK(ScopedJoints.Untie(ScopedBodyTie));
    FBY_CHECK(ScopedJoints.TieCount() == 0);
    FBY_CHECK(
        ScopedJoints.TieBodies(ScopedRegistry, ScopedBodyEntityA, ScopedBodyEntityB, ScopedBodyTieDesc, &ScopedError)
            .IsValid());
    ScopedRegistry.destroy(ScopedBodyEntityB);
    ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
    ScopedJoints.Update(ScopedRegistry, 1.0f / 60.0f);
    FBY_CHECK(ScopedJoints.TieCount() == 0);
    ScopedRegistry.destroy(ScopedBodyEntityA);

    // Rebuild keeps the handle and the stable id while replacing the geometry.
    JointChainDesc ScopedRebuildBefore;
    ScopedRebuildBefore.Kind = JointChainKind::Rope;
    ScopedRebuildBefore.Flags = JointChainPinStart;
    ScopedRebuildBefore.Start = Vector2D{0.0f, -32.0f};
    ScopedRebuildBefore.End = Vector2D{0.0f, 32.0f};
    ScopedRebuildBefore.SegmentCount = 3;
    const JointChainHandle ScopedRebuildHandle =
        ScopedJoints.CreateChain(ScopedRegistry, ScopedRebuildBefore, &ScopedError);
    FBY_CHECK(ScopedRebuildHandle.IsValid());
    const ChainId ScopedRebuildId = ScopedJoints.Info(ScopedRebuildHandle).ChainID;
    JointChainDesc ScopedRebuildAfter;
    ScopedRebuildAfter.Kind = JointChainKind::Chain;
    ScopedRebuildAfter.Flags = JointChainPinStart;
    ScopedRebuildAfter.Start = Vector2D{0.0f, -48.0f};
    ScopedRebuildAfter.End = Vector2D{0.0f, 48.0f};
    ScopedRebuildAfter.SegmentCount = 5;
    FBY_CHECK(ScopedJoints.RebuildChain(ScopedRegistry, ScopedRebuildHandle, ScopedRebuildAfter, &ScopedError));
    FBY_CHECK(ScopedError == ChainError::None);
    FBY_CHECK(ScopedJoints.IsValid(ScopedRebuildHandle));
    FBY_CHECK(ScopedJoints.Info(ScopedRebuildHandle).ChainID == ScopedRebuildId);
    FBY_CHECK(ScopedJoints.Info(ScopedRebuildHandle).LinkCount == 5);
    FBY_CHECK(ScopedJoints.ChainConnection(ScopedRebuildHandle, 0, &ScopedConnection));
    FBY_CHECK(ScopedConnection.Kind == JointConnectionKind::Hinge);
    FBY_CHECK(ScopedJoints.DestroyChain(ScopedRegistry, ScopedRebuildHandle));

    // Scene TOML keeps the fixed arrays that reflection cannot express.
    Scene::ComponentRegistry ScopedComponents;
    Scene::RegisterBuiltInComponents(ScopedComponents);
    JointChainComponent ScopedSceneChain;
    ScopedSceneChain.Kind = static_cast<std::uint32_t>(JointChainKind::Rope);
    ScopedSceneChain.Flags = JointChainPinStart;
    ScopedSceneChain.PathCount = 2;
    ScopedSceneChain.Path[0] = Vector2D{1.5f, 2.5f};
    ScopedSceneChain.Path[1] = Vector2D{3.5f, 4.5f};
    ScopedSceneChain.SectionCount = 1;
    ScopedSceneChain.Sections[0] = JointChainSection{
        JointChainKind::Rope, JointChainSpringRope, 2, 2.0f, 1.0f, 0.0f, 0.0f, -5.0f, 5.0f, 3.5f, 0.4f};
    const Entity ScopedSceneEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<JointChainComponent>(ScopedSceneEntity, ScopedSceneChain);
    const std::string ScopedSceneToml = Scene::SaveSceneToml(ScopedRegistry, ScopedComponents);
    FBY_CHECK(ScopedSceneToml.find("SectionsHex") != std::string::npos);
    FBY_CHECK(ScopedSceneToml.find("PathHex") != std::string::npos);
    Scene::Registry ScopedReloadedRegistry;
    const auto ScopedSceneResult = Scene::LoadSceneToml(ScopedReloadedRegistry, ScopedComponents, ScopedSceneToml);
    FBY_CHECK(ScopedSceneResult.has_value());
    auto ScopedReloadedView = ScopedReloadedRegistry.view<JointChainComponent>();
    FBY_CHECK(ScopedReloadedView.size() == 1);
    for (const Entity ScopedReloaded : ScopedReloadedView)
    {
        const JointChainComponent& ScopedReloadedChain = ScopedReloadedView.get<JointChainComponent>(ScopedReloaded);
        FBY_CHECK(ScopedReloadedChain.SectionCount == 1);
        FBY_CHECK(ScopedReloadedChain.Sections[0].DamageRate == 3.5f);
        FBY_CHECK(ScopedReloadedChain.Sections[0].DamageThreshold == 0.4f);
        FBY_CHECK(ScopedReloadedChain.Sections[0].UpperSpringForce == 5.0f);
        FBY_CHECK(ScopedReloadedChain.PathCount == 2);
        FBY_CHECK(ScopedReloadedChain.Path[1].Y == 4.5f);
    }
    ScopedRegistry.destroy(ScopedSceneEntity);

    // A gameplay cut enters the deterministic command stream and replays through
    // the world callback with the stable chain id.
    Ecs::CommandLog ScopedCommandLog;
    const Entity ScopedLogEntity = ScopedRegistry.create();
    FBY_CHECK(ScopedCommandLog.CutChain(ScopedLogEntity, 0x1234u, 3, 7));
    struct CutProbe
    {
        std::uint32_t ChainId = 0;
        std::uint16_t Connection = 0;
        std::uint32_t Calls = 0;
    };
    CutProbe ScopedProbe;
    Ecs::CommandWorldCallbacks ScopedCallbacks;
    ScopedCallbacks.user = &ScopedProbe;
    ScopedCallbacks.cutChain = [](void* InUser, Entity, std::span<const std::byte> InPayload)
    {
        auto* ScopedTarget = static_cast<CutProbe*>(InUser);
        if (InPayload.size() != sizeof(Ecs::CommandCutChain))
        {
            return false;
        }
        Ecs::CommandCutChain ScopedRecord{};
        std::memcpy(&ScopedRecord, InPayload.data(), sizeof(ScopedRecord));
        ScopedTarget->ChainId = ScopedRecord.chainId;
        ScopedTarget->Connection = ScopedRecord.connection;
        ++ScopedTarget->Calls;
        return true;
    };
    const Ecs::SnapshotSchema ScopedLogSchema = Ecs::SnapshotSchema::BuiltIns();
    FBY_CHECK(ScopedCommandLog.Replay(ScopedRegistry, ScopedLogSchema, &ScopedCallbacks));
    FBY_CHECK(ScopedProbe.Calls == 1);
    FBY_CHECK(ScopedProbe.ChainId == 0x1234u);
    FBY_CHECK(ScopedProbe.Connection == 3);
    ScopedRegistry.destroy(ScopedLogEntity);

    FBY_CHECK(ScopedJoints.ChainCount() == 0);
    FBY_CHECK(ScopedJoints.TieCount() == 0);
    FBY_CHECK(ScopedJoints.LinkCount() == 0);
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
