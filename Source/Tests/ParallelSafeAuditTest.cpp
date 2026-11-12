// FemboyEngine2D - ParallelSafe audit for AI, animation and physics systems.
#include "Engine/AI/AIController.hpp"
#include "Engine/ECS/AnimGraph.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/ECS/StateTree.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Physics/Gravity.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/SdfCollision.hpp"

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
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

using Fby::Ecs::ComponentAccessMask;
using Fby::Ecs::SystemGraph;

void RunNothing(Fby::Ecs::SystemContext&, void*)
{
}

[[nodiscard]] ComponentAccessMask Mask() noexcept
{
    return {};
}

template <typename... Components> [[nodiscard]] ComponentAccessMask MaskOf() noexcept
{
    return Fby::Ecs::MakeComponentAccessMask<Components...>();
}

void CheckAccess(const SystemGraph& InGraph, std::uint32_t InIndex, const ComponentAccessMask& InReads,
                 const ComponentAccessMask& InWrites, bool InParallelSafe, const char* InName)
{
    const Fby::Ecs::GraphSystem& ScopedSystem = InGraph.At(InIndex);
    const Fby::Ecs::SystemAccess& ScopedAccess = ScopedSystem.access;
    if (ScopedAccess.ComponentReads != InReads || ScopedAccess.ComponentWrites != InWrites ||
        ScopedAccess.ParallelSafe != InParallelSafe)
    {
        std::fprintf(stderr, "FAIL: %s has an unexpected access contract\n", InName);
        ++GFailures;
        return;
    }
    // A parallel system must not claim an engine service; services are the
    // mutable state the graph cannot reason about.
    if (InParallelSafe && (ScopedAccess.Reads != 0 || ScopedAccess.Writes != 0))
    {
        std::fprintf(stderr, "FAIL: %s is parallel-safe but declares a service\n", InName);
        ++GFailures;
    }
}

[[nodiscard]] std::uint32_t AddNode(SystemGraph& InGraph, const char* InName, const ComponentAccessMask& InReads,
                                    const ComponentAccessMask& InWrites)
{
    Fby::Ecs::SystemAccess ScopedAccess;
    ScopedAccess.ComponentReads = InReads;
    ScopedAccess.ComponentWrites = InWrites;
    ScopedAccess.ParallelSafe = true;
    if (InReads == ComponentAccessMask{} && InWrites == ComponentAccessMask{})
    {
        ScopedAccess.NoEcsAccess = true;
    }
    if (!InGraph.Add(Fby::Ecs::Phase::FixedUpdate, InName, &RunNothing, nullptr, 0, 0, ScopedAccess))
    {
        return 0xFFFFFFFFu;
    }
    return InGraph.Count() - 1u;
}
}

int main()
{
    using namespace Fby;

    // The audit inspects the declarations the graph actually schedules. The
    // registered systems are not run; this is a contract test, not a world test.
    Ecs::SystemGraph Graph;
    const std::uint32_t ScopedAnim = Graph.Count();
    RegisterAnimGraphSystem(Graph);
    FBY_CHECK(Graph.Count() == ScopedAnim + 1u);
    CheckAccess(Graph, ScopedAnim,
                MaskOf<AnimGraphDefinitionComponent, AnimStateComponent, GameplayTagsComponent, SkeletonComponent,
                       SpriteAnimationComponent, SpriteSheetComponent, Facing2D, Sprite2D>(),
                MaskOf<AnimStateComponent, SkeletonComponent, SpriteAnimationComponent, Sprite2D>(), true,
                "anim_graph");

    const std::uint32_t ScopedSprite = Graph.Count();
    RegisterSpriteAnimationSystem(Graph);
    FBY_CHECK(Graph.Count() == ScopedSprite + 1u);
    CheckAccess(Graph, ScopedSprite,
                MaskOf<SpriteAnimationComponent, Sprite2D, SpriteSheetComponent>(),
                MaskOf<SpriteAnimationComponent, Sprite2D>(), true, "spriteAnimation");

    AI::AIControllerSystem ScopedController;
    const std::uint32_t ScopedControllerIndex = Graph.Count();
    FBY_CHECK(ScopedController.Register(Graph, ScopedController));
    FBY_CHECK(!Graph.At(ScopedControllerIndex).access.ParallelSafe);

    AI::AIPerceptionSystem ScopedPerception;
    const std::uint32_t ScopedPerceptionIndex = Graph.Count();
    FBY_CHECK(ScopedPerception.Register(Graph, ScopedPerception));
    FBY_CHECK(!Graph.At(ScopedPerceptionIndex).access.ParallelSafe);

    StateTreeSystem ScopedTree;
    const std::uint32_t ScopedTreeIndex = Graph.Count();
    RegisterStateTreeSystem(Graph, ScopedTree);
    FBY_CHECK(!Graph.At(ScopedTreeIndex).access.ParallelSafe);

    PhysicsSystem ScopedPhysics;
    const std::uint32_t ScopedPhysicsIndex = Graph.Count();
    FBY_CHECK(PhysicsFixedStepSystem::Register(Graph, ScopedPhysics, 0));
    FBY_CHECK(!Graph.At(ScopedPhysicsIndex).access.ParallelSafe);
    FBY_CHECK((Graph.At(ScopedPhysicsIndex).access.Writes & Ecs::kServicePhysics) != 0u);

    GravitySystem ScopedGravity;
    const std::uint32_t ScopedGravityIndex = Graph.Count();
    FBY_CHECK(ScopedGravity.Register(Graph, ScopedGravity));
    FBY_CHECK(!Graph.At(ScopedGravityIndex).access.ParallelSafe);
    FBY_CHECK((Graph.At(ScopedGravityIndex).access.Writes & Ecs::kServicePhysics) != 0u);

    const std::uint32_t ScopedSdfIndex = Graph.Count();
    SdfCollisionSystem ScopedSdf;
    FBY_CHECK(ScopedSdf.Register(Graph));
    FBY_CHECK(!Graph.At(ScopedSdfIndex).access.ParallelSafe);
    FBY_CHECK((Graph.At(ScopedSdfIndex).access.Writes & Ecs::kServicePhysics) != 0u);

    // A disjoint parallel node shares the wave with anim_graph. Without the
    // opt-in the graph would serialize it, so this checks the flag is live.
    {
        Ecs::SystemGraph ScopedGraph;
        RegisterAnimGraphSystem(ScopedGraph);
        const std::uint32_t ScopedProbe = AddNode(ScopedGraph, "audit_probe", Mask(), Mask());
        FBY_CHECK(ScopedProbe != 0xFFFFFFFFu);
        FBY_CHECK(ScopedGraph.Build());
        FBY_CHECK(ScopedGraph.SystemLevel(0) == ScopedGraph.SystemLevel(ScopedProbe));
    }

    // The masks must include the components the implementation touches: a
    // Facing2D or Sprite2D writer has to conflict with anim_graph.
    {
        Ecs::SystemGraph ScopedGraph;
        RegisterAnimGraphSystem(ScopedGraph);
        const std::uint32_t ScopedFacing = AddNode(ScopedGraph, "facing_writer", Mask(), MaskOf<Facing2D>());
        const std::uint32_t ScopedSpriteWriter = AddNode(ScopedGraph, "sprite_writer", Mask(), MaskOf<Sprite2D>());
        FBY_CHECK(ScopedFacing != 0xFFFFFFFFu && ScopedSpriteWriter != 0xFFFFFFFFu);
        FBY_CHECK(ScopedGraph.Build());
        FBY_CHECK(ScopedGraph.SystemLevel(0) != ScopedGraph.SystemLevel(ScopedFacing));
        FBY_CHECK(ScopedGraph.SystemLevel(0) != ScopedGraph.SystemLevel(ScopedSpriteWriter));
    }

    // SpriteAnimationSystem reads SpriteSheetComponent; a writer conflicts even
    // though the sheet is not part of its write mask.
    {
        Ecs::SystemGraph ScopedGraph;
        RegisterSpriteAnimationSystem(ScopedGraph);
        const std::uint32_t ScopedSheet = AddNode(ScopedGraph, "sheet_writer", Mask(), MaskOf<SpriteSheetComponent>());
        FBY_CHECK(ScopedSheet != 0xFFFFFFFFu);
        FBY_CHECK(ScopedGraph.Build());
        FBY_CHECK(ScopedGraph.SystemLevel(0) != ScopedGraph.SystemLevel(ScopedSheet));
    }

    if (GFailures == 0)
    {
        std::printf("parallel_safe_audit: OK (AI/state/physics serial, animation opt-in, masks complete)\n");
        return 0;
    }
    std::fprintf(stderr, "parallel_safe_audit: %u failure(s)\n", GFailures);
    return 1;
}
