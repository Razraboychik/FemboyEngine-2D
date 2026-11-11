// FemboyEngine2D - AI command consumer and navigation intent bridge.
#include "Engine/AI/AIController.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Fby::AI
{
namespace
{
constexpr std::uint32_t kMaxDeferredControllers = 256;
constexpr float kPerceptionCellSize = 64.0f;

[[nodiscard]] std::uint64_t PerceptionCellKey(std::int32_t InX, std::int32_t InY) noexcept
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(InX)) << 32) | static_cast<std::uint32_t>(InY);
}

[[nodiscard]] Vector2D EntityPosition(const Ecs::Registry& InRegistry, Entity InEntity, Vector2D InFallback) noexcept
{
    if (InEntity != Fby::kNullEntity)
    {
        if (const auto* ScopedTransform = InRegistry.try_get<Transform2D>(InEntity); ScopedTransform != nullptr)
        {
            return ScopedTransform->Position;
        }
    }
    return InFallback;
}

void StopAgent(Navigation::NavAgentSettings& InSettings, Navigation::NavAgentComponent& InAgent) noexcept
{
    InSettings.Flags &= ~Navigation::kNavAgentFlagActive;
    InAgent.Path.Count = 0;
    InAgent.PathIndex = 0;
    InAgent.Flags &= ~(Navigation::kNavAgentFlagBlocked | Navigation::kNavAgentFlagRepathDeferred);
}

void MoveAgent(Navigation::NavAgentSettings& InSettings, Navigation::NavAgentComponent& InAgent, Vector2D InTarget,
               bool InNewIntent) noexcept
{
    InSettings.Flags |= Navigation::kNavAgentFlagActive;
    InSettings.Target = InTarget;
    if (InNewIntent)
    {
        InAgent.Path.Count = 0;
        InAgent.PathIndex = 0;
        InAgent.RepathTimer = 0.0f;
        InAgent.Flags &= ~(Navigation::kNavAgentFlagArrived | Navigation::kNavAgentFlagBlocked);
    }
}
}

void AIControllerSystem::Update(Registry& InRegistry, float InDeltaSeconds) noexcept
{
    if (InDeltaSeconds < 0.0f)
    {
        InDeltaSeconds = 0.0f;
    }
    // Structural pass first: every entity that received a command gets the
    // runtime and authored components the controller needs. Doing this before
    // the main view avoids mutating the registry during iteration.
    std::array<Entity, kMaxDeferredControllers> ScopedPending{};
    std::uint32_t ScopedPendingCount = 0;
    auto ScopedCommandView = InRegistry.view<AICommandComponent>();
    for (const Entity ScopedEntity : ScopedCommandView)
    {
        if (ScopedPendingCount < ScopedPending.size())
        {
            ScopedPending[ScopedPendingCount++] = ScopedEntity;
        }
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedPendingCount; ++ScopedI)
    {
        const Entity ScopedEntity = ScopedPending[ScopedI];
        if (InRegistry.try_get<AIAgentSettings>(ScopedEntity) == nullptr)
        {
            (void)InRegistry.emplace<AIAgentSettings>(ScopedEntity);
        }
        if (InRegistry.try_get<Navigation::NavAgentSettings>(ScopedEntity) == nullptr)
        {
            (void)InRegistry.emplace<Navigation::NavAgentSettings>(ScopedEntity);
        }
        if (InRegistry.try_get<Navigation::NavAgentComponent>(ScopedEntity) == nullptr)
        {
            (void)InRegistry.emplace<Navigation::NavAgentComponent>(ScopedEntity);
        }
        if (InRegistry.try_get<AIControllerComponent>(ScopedEntity) == nullptr)
        {
            (void)InRegistry.emplace<AIControllerComponent>(ScopedEntity);
        }
    }

    auto ScopedView = InRegistry.view<AICommandComponent, AIAgentSettings, Navigation::NavAgentSettings,
                                      Navigation::NavAgentComponent, AIControllerComponent>();
    for (auto [ScopedEntity, ScopedCommand, ScopedSettings, ScopedNav, ScopedAgent, ScopedRuntime] : ScopedView.each())
    {
        const bool ScopedNewTask =
            ScopedCommand.TaskIndex != ScopedRuntime.LastTaskIndex || ScopedCommand.Kind != ScopedRuntime.LastKind;
        const AICommandKind ScopedKind = static_cast<AICommandKind>(ScopedCommand.Kind);
        const Vector2D ScopedTargetPosition = EntityPosition(InRegistry, ScopedCommand.Target, ScopedCommand.Position);

        if (ScopedNewTask)
        {
            ScopedRuntime.SkillRetryTimer = 0.0f;
        }
        // Tick the retry timer before evaluating it, not after, so the first
        // retry is not one fixed step early.
        if (ScopedSettings.SkillRetryInterval > 0.0f)
        {
            ScopedRuntime.SkillRetryTimer -= InDeltaSeconds;
        }

        switch (ScopedKind)
        {
            case AICommandKind::None:
                StopAgent(ScopedNav, ScopedAgent);
                break;
            case AICommandKind::MoveTo:
                ScopedRuntime.Target = ScopedCommand.Target;
                MoveAgent(ScopedNav, ScopedAgent, ScopedTargetPosition, ScopedNewTask);
                break;
            case AICommandKind::Stop:
                StopAgent(ScopedNav, ScopedAgent);
                ScopedRuntime.Target = Fby::kNullEntity;
                break;
            case AICommandKind::Action:
                // Data only. The engine keeps the command on the entity; a
                // game-owned ECS system decides what ActionId means.
                ScopedRuntime.Target = ScopedCommand.Target;
                break;
            case AICommandKind::ActivateSkill:
            {
                ScopedRuntime.Target = ScopedCommand.Target;
                // A StateTree leaf re-emits its command every tick. Skills are
                // one-shot per leaf by default; SkillRetryInterval opts into a
                // periodic retry for a leaf that stays active.
                const bool ScopedRetryDue =
                    ScopedSettings.SkillRetryInterval > 0.0f && ScopedRuntime.SkillRetryTimer <= 0.0f;
                if (ScopedNewTask || ScopedRetryDue)
                {
                    if (MSkills != nullptr)
                    {
                        const std::uint16_t ScopedSlot = static_cast<std::uint16_t>(ScopedCommand.SkillSlot);
                        (void)MSkills->TryActivate(InRegistry, ScopedEntity, ScopedSlot, ScopedCommand.Target);
                    }
                    ScopedRuntime.LastSkillSlot = ScopedCommand.SkillSlot;
                    ScopedRuntime.SkillRetryTimer =
                        ScopedSettings.SkillRetryInterval > 0.0f ? ScopedSettings.SkillRetryInterval : 0.0f;
                }
                break;
            }
        }

        ScopedRuntime.LastSequence = ScopedCommand.Sequence;
        ScopedRuntime.LastTaskIndex = ScopedCommand.TaskIndex;
        ScopedRuntime.LastKind = ScopedCommand.Kind;
    }
}

void AIControllerSystem::Update(const Ecs::GameplaySystemView& InView) noexcept
{
    if (InView.gameplay.gameplaySkills != nullptr)
    {
        MSkills = InView.gameplay.gameplaySkills;
    }
    Update(InView.registry, InView.dt);
}

namespace
{
void AIControllerSystemThunk(const Ecs::GameplaySystemView& InView, void* InUser)
{
    static_cast<AIControllerSystem*>(InUser)->Update(InView);
}
}

bool AIControllerSystem::Register(Ecs::SystemGraph& InGraph, AIControllerSystem& InSystem)
{
    Ecs::SystemAccess ScopedAccess;
    ScopedAccess.ComponentReads =
        Ecs::MakeComponentAccessMask<AICommandComponent, AIAgentSettings, AIControllerComponent, AIPerceptionComponent,
                                     Navigation::NavAgentSettings, Navigation::NavAgentComponent, GameplayTagsComponent,
                                     Transform2D>();
    ScopedAccess.ComponentWrites =
        Ecs::MakeComponentAccessMask<AIControllerComponent, AIAgentSettings, Navigation::NavAgentComponent>();
    return InGraph.Add(Ecs::Phase::FixedUpdate, "AIControllerSystem",
                       Ecs::BindGameplaySystem<&AIControllerSystemThunk>(), &InSystem, 0, 0, ScopedAccess);
}

void AIPerceptionSystem::Update(Registry& InRegistry, const GameplayTagRegistry* InTags, float InDeltaSeconds)
{
    // Structural pass: settings-only entities get their runtime target state.
    std::array<Entity, kMaxDeferredControllers> ScopedPending{};
    std::uint32_t ScopedPendingCount = 0;
    auto ScopedSettingsView = InRegistry.view<AIPerceptionSettings>();
    for (const Entity ScopedEntity : ScopedSettingsView)
    {
        if (ScopedPendingCount < ScopedPending.size())
        {
            ScopedPending[ScopedPendingCount++] = ScopedEntity;
        }
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedPendingCount; ++ScopedI)
    {
        if (InRegistry.try_get<AIPerceptionComponent>(ScopedPending[ScopedI]) == nullptr)
        {
            (void)InRegistry.emplace<AIPerceptionComponent>(ScopedPending[ScopedI]);
        }
    }

    // Rebuild the perception broadphase once per pass. A cell holds its
    // entities by key; agents then visit only the cells their radius covers.
    MGrid.clear();
    auto ScopedAll = InRegistry.view<Transform2D>();
    for (auto [ScopedEntity, ScopedTransform] : ScopedAll.each())
    {
        const std::int32_t ScopedCellX =
            static_cast<std::int32_t>(std::floor(ScopedTransform.Position.X / kPerceptionCellSize));
        const std::int32_t ScopedCellY =
            static_cast<std::int32_t>(std::floor(ScopedTransform.Position.Y / kPerceptionCellSize));
        MGrid.push_back(GridEntry{PerceptionCellKey(ScopedCellX, ScopedCellY), ScopedEntity, ScopedTransform.Position});
    }
    std::sort(MGrid.begin(), MGrid.end(),
              [](const GridEntry& InLeft, const GridEntry& InRight) noexcept
              {
                  return InLeft.Key < InRight.Key;
              });

    auto ScopedView = InRegistry.view<AIPerceptionSettings, AIPerceptionComponent, StateTreeComponent, Transform2D>();
    for (auto [ScopedEntity, ScopedSettings, ScopedPerception, ScopedTree, ScopedTransform] : ScopedView.each())
    {
        ScopedPerception.Timer -= InDeltaSeconds;
        if (ScopedPerception.Timer > 0.0f)
        {
            continue;
        }
        ScopedPerception.Timer = std::max(ScopedSettings.Interval, 0.01f);
        const float ScopedRadius = std::max(ScopedSettings.Radius, 0.0f);
        Entity ScopedBest = Fby::kNullEntity;
        float ScopedBestDistance = ScopedRadius * ScopedRadius;
        const std::int32_t ScopedCenterCellX =
            static_cast<std::int32_t>(std::floor(ScopedTransform.Position.X / kPerceptionCellSize));
        const std::int32_t ScopedCenterCellY =
            static_cast<std::int32_t>(std::floor(ScopedTransform.Position.Y / kPerceptionCellSize));
        const std::int32_t ScopedCellRadius =
            std::clamp(static_cast<std::int32_t>(std::ceil(ScopedRadius / kPerceptionCellSize)), 0, 64);
        for (std::int32_t ScopedCellY = ScopedCenterCellY - ScopedCellRadius;
             ScopedCellY <= ScopedCenterCellY + ScopedCellRadius; ++ScopedCellY)
        {
            for (std::int32_t ScopedCellX = ScopedCenterCellX - ScopedCellRadius;
                 ScopedCellX <= ScopedCenterCellX + ScopedCellRadius; ++ScopedCellX)
            {
                const std::uint64_t ScopedKey = PerceptionCellKey(ScopedCellX, ScopedCellY);
                auto ScopedIt = std::lower_bound(MGrid.begin(), MGrid.end(), ScopedKey,
                                                 [](const GridEntry& InEntry, std::uint64_t InKey) noexcept
                                                 {
                                                     return InEntry.Key < InKey;
                                                 });
                for (; ScopedIt != MGrid.end() && ScopedIt->Key == ScopedKey; ++ScopedIt)
                {
                    const Entity ScopedCandidate = ScopedIt->Owner;
                    if (ScopedCandidate == ScopedEntity)
                    {
                        continue;
                    }
                    if (ScopedSettings.TargetTagId != 0)
                    {
                        const auto* ScopedCandidateTags = InRegistry.try_get<GameplayTagsComponent>(ScopedCandidate);
                        if (ScopedCandidateTags == nullptr || InTags == nullptr ||
                            !GameplayTagContainerHas(ScopedCandidateTags->Tags, *InTags,
                                                     GameplayTag{ScopedSettings.TargetTagId}))
                        {
                            continue;
                        }
                    }
                    const Vector2D ScopedDelta = ScopedIt->Position - ScopedTransform.Position;
                    const float ScopedDistance = ScopedDelta.X * ScopedDelta.X + ScopedDelta.Y * ScopedDelta.Y;
                    if (ScopedDistance <= ScopedBestDistance)
                    {
                        ScopedBestDistance = ScopedDistance;
                        ScopedBest = ScopedCandidate;
                    }
                }
            }
        }
        ScopedPerception.Target = ScopedBest;
        ScopedTree.Target = ScopedBest;
    }
}

void AIPerceptionSystem::Update(const Ecs::GameplaySystemView& InView)
{
    Update(InView.registry, InView.gameplay.gameplayTags, InView.dt);
}

namespace
{
void AIPerceptionSystemThunk(const Ecs::GameplaySystemView& InView, void* InUser)
{
    static_cast<AIPerceptionSystem*>(InUser)->Update(InView);
}
}

bool AIPerceptionSystem::Register(Ecs::SystemGraph& InGraph, AIPerceptionSystem& InSystem)
{
    Ecs::SystemAccess ScopedAccess;
    ScopedAccess.ComponentReads =
        Ecs::MakeComponentAccessMask<AIPerceptionComponent, AIPerceptionSettings, StateTreeComponent, Transform2D>();
    ScopedAccess.ComponentWrites =
        Ecs::MakeComponentAccessMask<AIPerceptionComponent, AIPerceptionSettings, StateTreeComponent>();
    return InGraph.Add(Ecs::Phase::FixedUpdate, "AIPerceptionSystem",
                       Ecs::BindGameplaySystem<&AIPerceptionSystemThunk>(), &InSystem, 0, 0, ScopedAccess);
}
}
