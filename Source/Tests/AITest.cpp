// FemboyEngine2D - AI command, navigation shaping and tag database tests.
#include "Engine/AI/AIController.hpp"
#include "Engine/Core/FrameArena.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Gameplay/GameplayAttributes.hpp"
#include "Engine/Gameplay/GameplayEffects.hpp"
#include "Engine/Gameplay/GameplaySkills.hpp"
#include "Engine/Gameplay/GameplayTagDatabase.hpp"
#include "Engine/Navigation/NavMesh.hpp"
#include "Engine/Navigation/Navigation.hpp"

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
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

[[nodiscard]] bool Near(float InA, float InB, float InTolerance = 1.0e-3f) noexcept
{
    return std::fabs(InA - InB) <= InTolerance;
}

void TestTagDatabase()
{
    using namespace Fby;
    GameplayTagDatabase ScopedDb;
    FBY_CHECK(ScopedDb.Add("Character") >= 0);
    FBY_CHECK(ScopedDb.Add("Character.State") >= 0);
    FBY_CHECK(ScopedDb.Add("Character.State.Stunned") >= 0);
    FBY_CHECK(ScopedDb.Count == 3);
    FBY_CHECK(ScopedDb.Add("Character.State") == 1);
    FBY_CHECK(ScopedDb.Add("Bad Path") < 0);
    FBY_CHECK(!ScopedDb.LastError().empty());

    FBY_CHECK(ScopedDb.Rename(1, "Character.Status"));
    FBY_CHECK(ScopedDb.Find("Character.Status") >= 0);
    FBY_CHECK(ScopedDb.Find("Character.Status.Stunned") >= 0);
    FBY_CHECK(ScopedDb.Find("Character.State") < 0);
    FBY_CHECK(!ScopedDb.Rename(0, "Character.Status"));

    GameplayTagRegistry ScopedRegistry;
    ScopedDb.ApplyToRegistry(ScopedRegistry);
    const GameplayTag ScopedStunned = ScopedRegistry.Find("Character.Status.Stunned");
    const GameplayTag ScopedStatus = ScopedRegistry.Find("Character.Status");
    FBY_CHECK(ScopedStunned.IsValid() && ScopedStatus.IsValid());
    GameplayTagContainer ScopedTags;
    ScopedTags.Add(ScopedStunned);
    FBY_CHECK(GameplayTagContainerHas(ScopedTags, ScopedRegistry, ScopedStatus));

    FBY_CHECK(ScopedDb.RemoveAt(1));
    FBY_CHECK(ScopedDb.Count == 1);
    FBY_CHECK(ScopedDb.Find("Character") == 0);
}

void TestNavigationShaping()
{
    using namespace Fby;
    Navigation::NavMap ScopedMap;
    Grid::GridDesc ScopedGrid;
    ScopedGrid.CellSize = Vector2D{8.0f, 8.0f};
    Navigation::ClearMap(ScopedMap, 32, 32, ScopedGrid);
    for (std::int32_t ScopedY = 0; ScopedY < 32; ++ScopedY)
    {
        for (std::int32_t ScopedX = 0; ScopedX < 32; ++ScopedX)
        {
            FBY_CHECK(ScopedMap.SetCost(Grid::Cell{ScopedX, ScopedY}, 1));
        }
    }
    // A wall with one gap: raw A* has to march around it.
    for (std::int32_t ScopedY = 0; ScopedY < 32; ++ScopedY)
    {
        if (ScopedY != 12)
        {
            FBY_CHECK(ScopedMap.SetCost(Grid::Cell{16, ScopedY}, 0));
        }
    }

    Navigation::PathFinder ScopedFinder;
    const Grid::Cell ScopedStart{2, 2};
    const Grid::Cell ScopedGoal{28, 2};

    Navigation::NavPathOptions ScopedRaw;
    ScopedRaw.Smoothing = Navigation::NavSmoothingMode::None;
    Navigation::NavPath ScopedRawPath;
    FBY_CHECK(ScopedFinder.FindWithOptions(ScopedMap, ScopedStart, ScopedGoal, ScopedRaw, &ScopedRawPath));
    FBY_CHECK(!ScopedRawPath.HasPoints);
    FBY_CHECK(ScopedRawPath.Count > 4);

    Navigation::NavPathOptions ScopedLine;
    ScopedLine.Smoothing = Navigation::NavSmoothingMode::LineOfSight;
    Navigation::NavPath ScopedLinePath;
    FBY_CHECK(ScopedFinder.FindWithOptions(ScopedMap, ScopedStart, ScopedGoal, ScopedLine, &ScopedLinePath));
    FBY_CHECK(ScopedLinePath.HasPoints);
    FBY_CHECK(ScopedLinePath.Count < ScopedRawPath.Count);
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedLinePath.Count; ++ScopedI)
    {
        FBY_CHECK(ScopedMap.Passable(Grid::WorldToCell(ScopedGrid, ScopedLinePath.Points[ScopedI])));
    }

    Navigation::NavPathOptions ScopedSmooth;
    ScopedSmooth.Smoothing = Navigation::NavSmoothingMode::CatmullRom;
    ScopedSmooth.SmoothingIterations = 4;
    ScopedSmooth.SmoothingStrength = 1.0f;
    Navigation::NavPath ScopedSmoothPath;
    FBY_CHECK(ScopedFinder.FindWithOptions(ScopedMap, ScopedStart, ScopedGoal, ScopedSmooth, &ScopedSmoothPath));
    FBY_CHECK(ScopedSmoothPath.HasPoints);
    FBY_CHECK(ScopedSmoothPath.Count >= ScopedLinePath.Count);

    Navigation::NavPathOptions ScopedJitter;
    ScopedJitter.Smoothing = Navigation::NavSmoothingMode::LineOfSight;
    ScopedJitter.RandomWaypointCells = 0.25f;
    ScopedJitter.RandomSeed = 7;
    Navigation::NavPath ScopedJitterA;
    Navigation::NavPath ScopedJitterB;
    FBY_CHECK(ScopedFinder.FindWithOptions(ScopedMap, ScopedStart, ScopedGoal, ScopedJitter, &ScopedJitterA));
    FBY_CHECK(ScopedFinder.FindWithOptions(ScopedMap, ScopedStart, ScopedGoal, ScopedJitter, &ScopedJitterB));
    FBY_CHECK(ScopedJitterA.Count == ScopedJitterB.Count);

    Navigation::NavPathOptions ScopedRandomRoute;
    ScopedRandomRoute.RandomizeRoute = true;
    ScopedRandomRoute.RandomCost = 2.0f;
    ScopedRandomRoute.RandomSeed = 99;
    ScopedRandomRoute.Smoothing = Navigation::NavSmoothingMode::None;
    Navigation::NavPath ScopedRandomA;
    FBY_CHECK(ScopedFinder.FindWithOptions(ScopedMap, ScopedStart, ScopedGoal, ScopedRandomRoute, &ScopedRandomA));
    FBY_CHECK(ScopedRandomA.Count > 0);

    // Convex navmesh: greedy rectangles cover the walkable cells and the portal
    // funnel produces a valid taut path through the wall gap.
    Navigation::NavMesh ScopedMesh;
    FBY_CHECK(ScopedMesh.Build(ScopedMap));
    FBY_CHECK(ScopedMesh.Ready());
    FBY_CHECK(ScopedMesh.RectCount() > 0);
    FBY_CHECK(ScopedMesh.PortalCount() > 0);
    Navigation::NavPath ScopedMeshPath;
    Navigation::NavPathOptions ScopedMeshOptions;
    ScopedMeshOptions.Smoothing = Navigation::NavSmoothingMode::LineOfSight;
    FBY_CHECK(ScopedMesh.FindPath(Grid::CellToWorld(ScopedGrid, ScopedStart), Grid::CellToWorld(ScopedGrid, ScopedGoal),
                                  ScopedMeshOptions, &ScopedMeshPath));
    FBY_CHECK(ScopedMeshPath.HasPoints);
    FBY_CHECK(ScopedMeshPath.Count >= 2);
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedMeshPath.Count; ++ScopedI)
    {
        FBY_CHECK(ScopedMap.Passable(Grid::WorldToCell(ScopedGrid, ScopedMeshPath.Points[ScopedI])));
    }
}

void TestAIPipeline()
{
    using namespace Fby;
    Navigation::NavMap ScopedMap;
    Grid::GridDesc ScopedGrid;
    ScopedGrid.CellSize = Vector2D{8.0f, 8.0f};
    Navigation::ClearMap(ScopedMap, 32, 32, ScopedGrid);
    for (std::int32_t ScopedY = 0; ScopedY < 32; ++ScopedY)
    {
        for (std::int32_t ScopedX = 0; ScopedX < 32; ++ScopedX)
        {
            (void)ScopedMap.SetCost(Grid::Cell{ScopedX, ScopedY}, 1);
        }
    }

    Ecs::Registry ScopedRegistry;
    const Entity ScopedAgent = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedAgent, Transform2D{Vector2D{8.0f, 8.0f}});
    const Entity ScopedTarget = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedTarget, Transform2D{Vector2D{200.0f, 8.0f}});

    auto& ScopedCommand = ScopedRegistry.emplace<AICommandComponent>(ScopedAgent);
    ScopedCommand.Kind = static_cast<std::uint32_t>(AICommandKind::MoveTo);
    ScopedCommand.Target = ScopedTarget;
    ScopedCommand.Position = Vector2D{200.0f, 8.0f};
    ScopedCommand.Speed = 64.0f;
    ScopedCommand.Sequence = 1;

    AI::AIControllerSystem ScopedAI;
    ScopedAI.Update(ScopedRegistry, 0.1f);
    auto* ScopedNav = ScopedRegistry.try_get<Navigation::NavAgentSettings>(ScopedAgent);
    FBY_CHECK(ScopedNav != nullptr);
    if (ScopedNav != nullptr)
    {
        FBY_CHECK((ScopedNav->Flags & Navigation::kNavAgentFlagActive) != 0);
        FBY_CHECK(Near(ScopedNav->Target.X, 200.0f));
    }

    Navigation::NavMesh ScopedAgentMesh;
    FBY_CHECK(ScopedAgentMesh.Build(ScopedMap));
    Navigation::NavigationSystem ScopedNavigation;
    ScopedNavigation.SetMap(&ScopedMap);
    ScopedNavigation.SetNavigationMesh(&ScopedAgentMesh);
    ScopedNavigation.MaxRepathsPerFrame = 8;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 200; ++ScopedStep)
    {
        ScopedAI.Update(ScopedRegistry, 0.05f);
        ScopedNavigation.Update(ScopedRegistry, 0.05f);
    }
    const auto& ScopedTransform = ScopedRegistry.get<Transform2D>(ScopedAgent);
    FBY_CHECK(Near(ScopedTransform.Position.X, 200.0f, 10.0f));

    // Action is data. The controller leaves the command intact; a game-owned
    // ECS system builds its own action infrastructure over it.
    ScopedCommand.Kind = static_cast<std::uint32_t>(AICommandKind::Action);
    ScopedCommand.ActionId = 77;
    ScopedCommand.Target = ScopedTarget;
    ScopedCommand.Sequence = 2;
    ScopedAI.Update(ScopedRegistry, 0.05f);
    int ScopedActionCount = 0;
    auto ScopedCommandView = ScopedRegistry.view<AICommandComponent>();
    for (const Entity ScopedViewed : ScopedCommandView)
    {
        const AICommandComponent& ScopedSeen = ScopedCommandView.get<AICommandComponent>(ScopedViewed);
        if (ScopedSeen.Kind == static_cast<std::uint32_t>(AICommandKind::Action) && ScopedSeen.ActionId == 77)
        {
            ++ScopedActionCount;
        }
    }
    FBY_CHECK(ScopedActionCount == 1);

    // Skill intent: the AI calls GameplaySkillSystem and does not apply an
    // effect itself.
    GameplayTagRegistry ScopedTags;
    GameplayAttributeRegistry ScopedAttributes;
    GameplayEffectSystem ScopedEffects;
    ScopedEffects.Initialize(ScopedTags, ScopedAttributes);
    GameplaySkillSystem ScopedSkills;
    ScopedSkills.Initialize(ScopedTags, ScopedAttributes, ScopedEffects);
    GameplaySkillDef ScopedSkill;
    ScopedSkill.Id = 7;
    FBY_CHECK(ScopedSkills.AddDef(ScopedSkill));
    FBY_CHECK(ScopedSkills.SetSlot(ScopedRegistry, ScopedAgent, 0, 7));
    ScopedAI.SetSkillSystem(&ScopedSkills);
    ScopedCommand.Kind = static_cast<std::uint32_t>(AICommandKind::ActivateSkill);
    ScopedCommand.Target = ScopedTarget;
    ScopedCommand.Flags = 0;
    ScopedCommand.Sequence = 3;
    ScopedAI.Update(ScopedRegistry, 0.05f);
    FBY_CHECK(!ScopedSkills.Events().empty());

    // Perception: the nearest entity carrying the configured tag becomes the
    // StateTree target.
    GameplayTagRegistry ScopedPerceptionTags;
    const GameplayTag ScopedPlayerTag = ScopedPerceptionTags.Register("Team.Player");
    Ecs::Registry ScopedPerceptionRegistry;
    const Entity ScopedSpotter = ScopedPerceptionRegistry.create();
    ScopedPerceptionRegistry.emplace<Transform2D>(ScopedSpotter, Transform2D{Vector2D{0.0f, 0.0f}});
    ScopedPerceptionRegistry.emplace<StateTreeComponent>(ScopedSpotter);
    auto& ScopedPerceptionSettings = ScopedPerceptionRegistry.emplace<AI::AIPerceptionSettings>(ScopedSpotter);
    ScopedPerceptionSettings.Radius = 100.0f;
    ScopedPerceptionSettings.Interval = 0.1f;
    ScopedPerceptionSettings.TargetTagId = ScopedPlayerTag.Id;
    const Entity ScopedNear = ScopedPerceptionRegistry.create();
    ScopedPerceptionRegistry.emplace<Transform2D>(ScopedNear, Transform2D{Vector2D{30.0f, 0.0f}});
    auto& ScopedNearTags = ScopedPerceptionRegistry.emplace<GameplayTagsComponent>(ScopedNear);
    ScopedNearTags.BaseTags.Add(ScopedPlayerTag);
    ScopedNearTags.Tags.Add(ScopedPlayerTag);
    const Entity ScopedFar = ScopedPerceptionRegistry.create();
    ScopedPerceptionRegistry.emplace<Transform2D>(ScopedFar, Transform2D{Vector2D{80.0f, 0.0f}});
    auto& ScopedFarTags = ScopedPerceptionRegistry.emplace<GameplayTagsComponent>(ScopedFar);
    ScopedFarTags.BaseTags.Add(ScopedPlayerTag);
    ScopedFarTags.Tags.Add(ScopedPlayerTag);
    AI::AIPerceptionSystem ScopedPerception;
    ScopedPerception.Update(ScopedPerceptionRegistry, &ScopedPerceptionTags, 0.1f);
    FBY_CHECK(ScopedPerceptionRegistry.get<StateTreeComponent>(ScopedSpotter).Target == ScopedNear);
}
}

void TestStateTreeToNavigation()
{
    using namespace Fby;
    Navigation::NavMap ScopedMap;
    Grid::GridDesc ScopedGrid;
    ScopedGrid.CellSize = Vector2D{8.0f, 8.0f};
    Navigation::ClearMap(ScopedMap, 32, 32, ScopedGrid);
    for (std::int32_t ScopedY = 0; ScopedY < 32; ++ScopedY)
    {
        for (std::int32_t ScopedX = 0; ScopedX < 32; ++ScopedX)
        {
            (void)ScopedMap.SetCost(Grid::Cell{ScopedX, ScopedY}, 1);
        }
    }

    StateTreeDefinition ScopedTree;
    ScopedTree.TreeId = 42;
    const std::uint16_t ScopedRoot = StateTreeAddNode(ScopedTree, StateTreeNodeKind::Root);
    const std::uint16_t ScopedLeaf = StateTreeAddNode(ScopedTree, StateTreeNodeKind::Leaf, ScopedRoot);
    StateTreeEvaluator ScopedGuard;
    ScopedGuard.Kind = StateTreeEvaluatorKind::AlwaysTrue;
    StateTreeTask ScopedMove;
    ScopedMove.Kind = StateTreeTaskKind::MoveToTarget;
    ScopedMove.Speed = 64.0f;
    StateTreeSetLeaf(ScopedTree, ScopedLeaf, ScopedGuard, ScopedMove);

    StateTreeSystem ScopedStateTree;
    FBY_CHECK(ScopedStateTree.AddTree(ScopedTree));
    Ecs::Registry ScopedRegistry;
    std::array<std::byte, 64u << 10> ScopedArenaStorage{};
    FrameArena ScopedArena(ScopedArenaStorage);
    Ecs::CommandBuffer ScopedCommands;
    ScopedCommands.Initialize(&ScopedArena);

    const Entity ScopedAgent = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedAgent, Transform2D{Vector2D{8.0f, 8.0f}});
    auto& ScopedTreeState = ScopedRegistry.emplace<StateTreeComponent>(ScopedAgent);
    ScopedTreeState.TreeId = 42;
    ScopedTreeState.Active = true;
    auto& ScopedNavSettings = ScopedRegistry.emplace<Navigation::NavAgentSettings>(ScopedAgent);
    ScopedNavSettings.Speed = 64.0f;
    ScopedNavSettings.RepathInterval = 0.1f;
    ScopedNavSettings.RepathDistance = 1.0f;
    const Entity ScopedTarget = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedTarget, Transform2D{Vector2D{200.0f, 8.0f}});
    ScopedTreeState.Target = ScopedTarget;

    AI::AIControllerSystem ScopedAI;
    Navigation::NavigationSystem ScopedNavigation;
    ScopedNavigation.SetMap(&ScopedMap);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 160; ++ScopedStep)
    {
        ScopedStateTree.Run(ScopedRegistry, 0.05f, &ScopedCommands);
        ScopedCommands.Play(ScopedRegistry);
        ScopedAI.Update(ScopedRegistry, 0.05f);
        ScopedNavigation.Update(ScopedRegistry, 0.05f);
    }
    const auto& ScopedPosition = ScopedRegistry.get<Transform2D>(ScopedAgent).Position;
    FBY_CHECK(ScopedPosition.X > 150.0f);
    FBY_CHECK(std::fabs(ScopedPosition.Y - 8.0f) < 16.0f);
}

int main()
{
    TestTagDatabase();
    TestNavigationShaping();
    TestAIPipeline();
    TestStateTreeToNavigation();

    if (GFailures == 0)
    {
        std::printf("ai_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "ai_test: %u failure(s)\n", GFailures);
    return 1;
}
