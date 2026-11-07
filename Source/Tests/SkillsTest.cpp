// FemboyEngine2D - gameplay tags, attributes, effects and skills tests.
#include "Engine/Core/FrameArena.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/ECS/StateTree.hpp"
#include "Engine/Gameplay/GameplayEffects.hpp"
#include "Engine/Gameplay/GameplaySkills.hpp"
#include "Engine/Gameplay/GameplayTags.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

[[nodiscard]] bool Near(float InA, float InB) noexcept
{
    return std::fabs(InA - InB) <= 1.0e-4f;
}
}

int main()
{
    using namespace Fby;

    // Tags are created by path, not by an enum. Parents are implicit.
    GameplayTagRegistry ScopedTags;
    const GameplayTag ScopedCharacter = ScopedTags.Register("Character");
    const GameplayTag ScopedState = ScopedTags.Register("Character.State");
    const GameplayTag ScopedStunned = ScopedTags.Register("Character.State.Stunned");
    const GameplayTag ScopedBurning = ScopedTags.Register("Character.State.Burning");
    FBY_CHECK(ScopedCharacter.IsValid());
    FBY_CHECK(ScopedState.IsValid());
    FBY_CHECK(ScopedStunned.IsValid());
    FBY_CHECK(ScopedTags.Register("Character.State.Stunned") == ScopedStunned);
    FBY_CHECK(ScopedTags.Find("Character.State") == ScopedState);
    FBY_CHECK(ScopedTags.Parent(ScopedStunned) == ScopedState);
    FBY_CHECK(ScopedTags.Parent(ScopedState) == ScopedCharacter);
    FBY_CHECK(ScopedTags.Name(ScopedStunned) == std::string_view("Character.State.Stunned"));
    FBY_CHECK(ScopedTags.Descendants(ScopedCharacter).Test(ScopedStunned));

    GameplayTagContainer ScopedContainer;
    ScopedContainer.Add(ScopedStunned);
    FBY_CHECK(GameplayTagContainerHas(ScopedContainer, ScopedTags, ScopedStunned));
    FBY_CHECK(GameplayTagContainerHas(ScopedContainer, ScopedTags, ScopedState));
    FBY_CHECK(!ScopedContainer.HasExact(ScopedState));
    GameplayTagContainer ScopedRequired;
    ScopedRequired.Add(ScopedState);
    ScopedRequired.Add(ScopedStunned);
    FBY_CHECK(GameplayTagContainerHasAll(ScopedContainer, ScopedTags, ScopedRequired));
    GameplayTagContainer ScopedBlocked;
    ScopedBlocked.Add(ScopedBurning);
    FBY_CHECK(!GameplayTagContainerHasAny(ScopedContainer, ScopedTags, ScopedBlocked));
    ScopedContainer.Remove(ScopedStunned);
    FBY_CHECK(!GameplayTagContainerHas(ScopedContainer, ScopedTags, ScopedState));

    // Attributes are registered by name; effects aggregate modifiers.
    GameplayAttributeRegistry ScopedAttributes;
    const GameplayAttribute ScopedHealth = ScopedAttributes.Register("Health");
    const GameplayAttribute ScopedMaxHealth = ScopedAttributes.Register("MaxHealth");
    const GameplayAttribute ScopedMana = ScopedAttributes.Register("Mana");
    FBY_CHECK(ScopedHealth.IsValid() && ScopedMana.IsValid());
    FBY_CHECK(ScopedAttributes.Register("Health") == ScopedHealth);

    Ecs::Registry ScopedRegistry;
    const Entity ScopedActor = ScopedRegistry.create();
    auto& ScopedAttributeComponent = ScopedRegistry.emplace<GameplayAttributesComponent>(ScopedActor);
    SetGameplayAttributeBase(ScopedAttributeComponent, ScopedHealth, 100.0f);
    SetGameplayAttributeBase(ScopedAttributeComponent, ScopedMaxHealth, 100.0f);
    SetGameplayAttributeBase(ScopedAttributeComponent, ScopedMana, 20.0f);
    auto& ScopedTagComponent = ScopedRegistry.emplace<GameplayTagsComponent>(ScopedActor);
    ScopedTagComponent.BaseTags.Add(ScopedCharacter);

    GameplayEffectSystem ScopedEffects;
    ScopedEffects.Initialize(ScopedTags, ScopedAttributes);

    GameplayEffectDef ScopedBuff;
    ScopedBuff.Id = 1;
    ScopedBuff.Duration = 5.0f;
    ScopedBuff.ModifierCount = 1;
    ScopedBuff.Modifiers[0] = GameplayAttributeModifier{ScopedHealth, GameplayModifierOp::Add, 20.0f};
    ScopedBuff.GrantedTagCount = 1;
    ScopedBuff.GrantedTags[0] = ScopedBurning;
    FBY_CHECK(ScopedEffects.AddDef(ScopedBuff));
    FBY_CHECK(ScopedEffects.Apply(ScopedRegistry, ScopedActor, 1, ScopedActor));
    FBY_CHECK(Near(GameplayAttributeValue(ScopedAttributeComponent, ScopedHealth), 120.0f));
    FBY_CHECK(ScopedTagComponent.Tags.HasExact(ScopedBurning));
    ScopedEffects.Update(ScopedRegistry, 1.0f);
    FBY_CHECK(Near(GameplayAttributeValue(ScopedAttributeComponent, ScopedHealth), 120.0f));
    FBY_CHECK(ScopedEffects.Remove(ScopedRegistry, ScopedActor, 1));
    FBY_CHECK(Near(GameplayAttributeValue(ScopedAttributeComponent, ScopedHealth), 100.0f));
    FBY_CHECK(!ScopedTagComponent.Tags.HasExact(ScopedBurning));

    GameplayEffectDef ScopedDamage;
    ScopedDamage.Id = 2;
    ScopedDamage.Flags = GameplayEffectInstant;
    ScopedDamage.ModifierCount = 1;
    ScopedDamage.Modifiers[0] = GameplayAttributeModifier{ScopedHealth, GameplayModifierOp::Add, -30.0f};
    FBY_CHECK(ScopedEffects.AddDef(ScopedDamage));
    FBY_CHECK(ScopedEffects.Apply(ScopedRegistry, ScopedActor, 2, ScopedActor));
    FBY_CHECK(Near(GameplayAttributeValue(ScopedAttributeComponent, ScopedHealth), 70.0f));

    GameplayEffectDef ScopedBurn;
    ScopedBurn.Id = 3;
    ScopedBurn.Flags = GameplayEffectPeriodic;
    ScopedBurn.Duration = 1.0f;
    ScopedBurn.Period = 0.25f;
    ScopedBurn.ModifierCount = 1;
    ScopedBurn.Modifiers[0] = GameplayAttributeModifier{ScopedHealth, GameplayModifierOp::Add, -5.0f};
    FBY_CHECK(ScopedEffects.AddDef(ScopedBurn));
    ScopedEffects.ClearEvents();
    FBY_CHECK(ScopedEffects.Apply(ScopedRegistry, ScopedActor, 3, ScopedActor));
    for (int ScopedStep = 0; ScopedStep < 4; ++ScopedStep)
    {
        ScopedEffects.Update(ScopedRegistry, 0.25f);
    }
    FBY_CHECK(Near(GameplayAttributeValue(ScopedAttributeComponent, ScopedHealth), 50.0f));
    std::uint32_t ScopedPeriodicEvents = 0;
    for (const GameplayEffectEvent& ScopedEvent : ScopedEffects.Events())
    {
        if (ScopedEvent.Type == GameplayEffectEventType::Periodic)
        {
            ++ScopedPeriodicEvents;
        }
    }
    FBY_CHECK(ScopedPeriodicEvents == 4);

    GameplayEffectDef ScopedStack;
    ScopedStack.Id = 4;
    ScopedStack.Duration = 5.0f;
    ScopedStack.StackPolicy = GameplayEffectStackPolicy::Stack;
    ScopedStack.MaxStacks = 3;
    ScopedStack.ModifierCount = 1;
    ScopedStack.Modifiers[0] = GameplayAttributeModifier{ScopedHealth, GameplayModifierOp::Add, 10.0f};
    FBY_CHECK(ScopedEffects.AddDef(ScopedStack));
    ScopedEffects.ClearEvents();
    FBY_CHECK(ScopedEffects.Apply(ScopedRegistry, ScopedActor, 4, ScopedActor));
    FBY_CHECK(ScopedEffects.Apply(ScopedRegistry, ScopedActor, 4, ScopedActor));
    FBY_CHECK(Near(GameplayAttributeValue(ScopedAttributeComponent, ScopedHealth), 70.0f));
    FBY_CHECK(ScopedEffects.Events().size() == 2);
    FBY_CHECK(ScopedEffects.Events()[1].Stacks == 2);
    FBY_CHECK(ScopedEffects.Remove(ScopedRegistry, ScopedActor, 4));

    // Skills: tags, cost, cast, cooldown and effects.
    GameplaySkillSystem ScopedSkills;
    ScopedSkills.Initialize(ScopedTags, ScopedAttributes, ScopedEffects);
    GameplayEffectDef ScopedFireDamage;
    ScopedFireDamage.Id = 10;
    ScopedFireDamage.Flags = GameplayEffectInstant;
    ScopedFireDamage.ModifierCount = 1;
    ScopedFireDamage.Modifiers[0] = GameplayAttributeModifier{ScopedHealth, GameplayModifierOp::Add, -10.0f};
    FBY_CHECK(ScopedEffects.AddDef(ScopedFireDamage));
    GameplayEffectDef ScopedSelfBuff;
    ScopedSelfBuff.Id = 11;
    ScopedSelfBuff.Duration = 2.0f;
    ScopedSelfBuff.ModifierCount = 1;
    ScopedSelfBuff.Modifiers[0] = GameplayAttributeModifier{ScopedHealth, GameplayModifierOp::Add, 5.0f};
    FBY_CHECK(ScopedEffects.AddDef(ScopedSelfBuff));

    GameplayTag ScopedMage = ScopedTags.Register("Class.Mage");
    GameplaySkillDef ScopedFireball;
    ScopedFireball.Id = 100;
    ScopedFireball.Cooldown = 1.0f;
    ScopedFireball.CastTime = 0.5f;
    ScopedFireball.CostAttribute = ScopedMana;
    ScopedFireball.CostAmount = 10.0f;
    ScopedFireball.RequiredTags.Add(ScopedMage);
    ScopedFireball.TargetKind = GameplaySkillTargetKind::TargetEntity;
    ScopedFireball.TargetEffectCount = 1;
    ScopedFireball.TargetEffectIds[0] = 10;
    ScopedFireball.SelfEffectCount = 1;
    ScopedFireball.SelfEffectIds[0] = 11;
    FBY_CHECK(ScopedSkills.AddDef(ScopedFireball));

    const Entity ScopedCaster = ScopedRegistry.create();
    auto& ScopedCasterAttributes = ScopedRegistry.emplace<GameplayAttributesComponent>(ScopedCaster);
    SetGameplayAttributeBase(ScopedCasterAttributes, ScopedHealth, 100.0f);
    SetGameplayAttributeBase(ScopedCasterAttributes, ScopedMana, 20.0f);
    auto& ScopedCasterTags = ScopedRegistry.emplace<GameplayTagsComponent>(ScopedCaster);
    const Entity ScopedTarget = ScopedRegistry.create();
    auto& ScopedTargetAttributes = ScopedRegistry.emplace<GameplayAttributesComponent>(ScopedTarget);
    SetGameplayAttributeBase(ScopedTargetAttributes, ScopedHealth, 50.0f);

    FBY_CHECK(ScopedSkills.SetSlot(ScopedRegistry, ScopedCaster, 0, 100));
    FBY_CHECK(!ScopedSkills.TryActivate(ScopedRegistry, ScopedCaster, 0, ScopedTarget));
    ScopedCasterTags.BaseTags.Add(ScopedMage);
    ScopedCasterTags.Tags.Add(ScopedMage);
    ScopedSkills.ClearEvents();
    FBY_CHECK(ScopedSkills.TryActivate(ScopedRegistry, ScopedCaster, 0, ScopedTarget));
    FBY_CHECK(Near(GameplayAttributeValue(ScopedCasterAttributes, ScopedMana), 10.0f));
    const auto& ScopedCasterSkills = ScopedRegistry.get<GameplaySkillsComponent>(ScopedCaster);
    FBY_CHECK(ScopedCasterSkills.Slots[0].State == GameplaySkillState::Casting);
    FBY_CHECK(!ScopedSkills.TryActivate(ScopedRegistry, ScopedCaster, 0, ScopedTarget));
    ScopedSkills.Update(ScopedRegistry, 0.5f);
    FBY_CHECK(Near(GameplayAttributeValue(ScopedTargetAttributes, ScopedHealth), 40.0f));
    FBY_CHECK(ScopedCasterSkills.Slots[0].State == GameplaySkillState::Ready);
    FBY_CHECK(ScopedCasterSkills.Slots[0].CooldownRemaining > 0.0f);
    FBY_CHECK(!ScopedSkills.TryActivate(ScopedRegistry, ScopedCaster, 0, ScopedTarget));
    ScopedSkills.Update(ScopedRegistry, 1.0f);
    FBY_CHECK(ScopedCasterSkills.Slots[0].CooldownRemaining <= 0.0f);
    FBY_CHECK(ScopedSkills.TryActivate(ScopedRegistry, ScopedCaster, 0, ScopedTarget));

    // Snapshot: every gameplay component has a raw codec in BuiltIns.
    const Ecs::SnapshotSchema ScopedSchema = Ecs::SnapshotSchema::BuiltIns();
    std::size_t ScopedSnapshotBytes = Ecs::SnapshotSize(ScopedRegistry, ScopedSchema);
    FBY_CHECK(ScopedSnapshotBytes > 0);
    std::vector<std::byte> ScopedSnapshot(ScopedSnapshotBytes);
    DeterministicRng ScopedRng;
    FBY_CHECK(Ecs::CaptureSnapshot(ScopedRegistry, ScopedSchema, ScopedSnapshot, 1, ScopedRng,
                                   &ScopedSnapshotBytes));
    ScopedSnapshot.resize(ScopedSnapshotBytes);
    ScopedTagComponent.BaseTags.Clear();
    ScopedTagComponent.Tags.Clear();
    ScopedTargetAttributes.Values.Base[ScopedHealth.Id - 1u] = 999.0f;
    FBY_CHECK(Ecs::RestoreSnapshot(ScopedRegistry, ScopedSchema, ScopedSnapshot));
    FBY_CHECK(ScopedTagComponent.BaseTags.HasExact(ScopedCharacter));
    FBY_CHECK(Near(GameplayAttributeValue(ScopedTargetAttributes, ScopedHealth), 40.0f));

    // StateTree writes an ActivateSkill command; the gameplay layer executes it.
    StateTreeDefinition ScopedTree;
    ScopedTree.TreeId = 3;
    const std::uint16_t ScopedRoot = StateTreeAddNode(ScopedTree, StateTreeNodeKind::Root);
    const std::uint16_t ScopedLeaf = StateTreeAddNode(ScopedTree, StateTreeNodeKind::Leaf, ScopedRoot);
    StateTreeEvaluator ScopedEvaluator;
    ScopedEvaluator.Kind = StateTreeEvaluatorKind::AlwaysTrue;
    StateTreeTask ScopedTask;
    ScopedTask.Kind = StateTreeTaskKind::ActivateSkill;
    ScopedTask.SkillSlot = 1;
    StateTreeSetLeaf(ScopedTree, ScopedLeaf, ScopedEvaluator, ScopedTask);
    StateTreeSystem ScopedStateTree;
    FBY_CHECK(ScopedStateTree.AddTree(ScopedTree));
    ScopedStateTree.SetTagRegistry(&ScopedTags);
    GameplaySkillDef ScopedInstant;
    ScopedInstant.Id = 101;
    FBY_CHECK(ScopedSkills.AddDef(ScopedInstant));
    FBY_CHECK(ScopedSkills.SetSlot(ScopedRegistry, ScopedCaster, 1, 101));
    auto& ScopedTreeState = ScopedRegistry.emplace<StateTreeComponent>(ScopedCaster);
    ScopedTreeState.TreeId = 3;
    ScopedTreeState.Active = true;
    std::array<std::byte, 64u << 10> ScopedArenaStorage{};
    FrameArena ScopedArena(ScopedArenaStorage);
    Ecs::CommandBuffer ScopedCommands;
    ScopedCommands.Initialize(&ScopedArena);
    JobSystem ScopedJobs(1);
    Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.commands = &ScopedCommands;
    ScopedContext.gameplayTags = &ScopedTags;
    ScopedStateTree.Run(ScopedContext.GameplayView());
    ScopedCommands.Play(ScopedRegistry);
    const auto* ScopedCommand = ScopedRegistry.try_get<AICommandComponent>(ScopedCaster);
    FBY_CHECK(ScopedCommand != nullptr);
    if (ScopedCommand != nullptr)
    {
        FBY_CHECK(ScopedCommand->Kind == static_cast<std::uint32_t>(AICommandKind::ActivateSkill));
        FBY_CHECK(ScopedCommand->Flags == 1);
    }

    if (GFailures == 0)
    {
        std::printf("skills_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "skills_test: %u failure(s)\n", GFailures);
    return 1;
}
