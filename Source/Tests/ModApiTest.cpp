// FemboyEngine2D - safe mod data ABI tests.
#include "Engine/ECS/CoreComponents.hpp"
#include "Engine/ECS/FCS/Registry.hpp"
#include "Engine/Gameplay/GameplayEffects.hpp"
#include "Engine/Gameplay/GameplaySkills.hpp"
#include "Engine/Gameplay/GameplayTags.hpp"
#include "Engine/Modding/ModApiHost.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
std::uint32_t GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

struct EventCounter
{
    std::uint32_t Count = 0;
    std::uint32_t LastValue = 0;
};

void OnEvent(void* InUser, FbyModEventId, const void* InData, std::size_t InSize)
{
    auto* ScopedCounter = static_cast<EventCounter*>(InUser);
    ++ScopedCounter->Count;
    if (InData != nullptr && InSize == sizeof(std::uint32_t))
    {
        std::memcpy(&ScopedCounter->LastValue, InData, sizeof(std::uint32_t));
    }
}

void TestBasicApi()
{
    Fby::Fcs::Registry ScopedRegistry;
    Fby::Plugin::ComponentRegistry ScopedComponents;
    ScopedComponents.Register<Fby::Transform2D>("Transform2D");
    Fby::Modding::ModApiHost ScopedHost({&ScopedRegistry, &ScopedComponents});
    const FbyModApi* ScopedApi = ScopedHost.Api();
    Check(ScopedApi != nullptr && ScopedApi->struct_size == sizeof(FbyModApi), "api struct is versioned");
    Check(ScopedApi->abi_version == FBY_MOD_API_VERSION, "api version is exported");

    FbyModComponentId ScopedType = 0;
    Check(ScopedApi->component_find(ScopedApi->user, "Transform2D", &ScopedType) == FBY_MOD_OK,
          "component lookup by name");
    Check(ScopedType != 0, "component id is non-zero");
    std::uint32_t ScopedSize = 0;
    std::uint64_t ScopedLayout = 0;
    Check(ScopedApi->component_size(ScopedApi->user, ScopedType, &ScopedSize) == FBY_MOD_OK &&
              ScopedSize == sizeof(Fby::Transform2D),
          "component size");
    Check(ScopedApi->component_layout_hash(ScopedApi->user, ScopedType, &ScopedLayout) == FBY_MOD_OK &&
              ScopedLayout != 0,
          "component layout hash");

    const FbyModEntity ScopedEntity = ScopedApi->entity_create(ScopedApi->user);
    Check(ScopedEntity != Fby::kNullEntity && ScopedApi->entity_alive(ScopedApi->user, ScopedEntity) != 0,
          "entity create/alive");
    Fby::Transform2D ScopedTransform;
    ScopedTransform.Position = {12.0f, 34.0f};
    Check(ScopedApi->command_write(ScopedApi->user, ScopedEntity, ScopedType, &ScopedTransform,
                                   sizeof(ScopedTransform)) == FBY_MOD_OK,
          "queue component write");
    Check(ScopedApi->flush_commands(ScopedApi->user) == FBY_MOD_OK, "flush commands");
    Fby::Transform2D ScopedRead{};
    std::size_t ScopedReadSize = 0;
    Check(ScopedApi->component_read(ScopedApi->user, ScopedEntity, ScopedType, &ScopedRead, sizeof(ScopedRead),
                                    &ScopedReadSize) == FBY_MOD_OK,
          "read component");
    Check(ScopedReadSize == sizeof(ScopedRead) && ScopedRead.Position.X == 12.0f && ScopedRead.Position.Y == 34.0f,
          "component value roundtrips");

    std::array<std::byte, 1> ScopedTiny{};
    std::size_t ScopedRequired = 0;
    Check(ScopedApi->component_read(ScopedApi->user, ScopedEntity, ScopedType, ScopedTiny.data(), ScopedTiny.size(),
                                    &ScopedRequired) == FBY_MOD_WRONG_SIZE &&
              ScopedRequired == sizeof(Fby::Transform2D),
          "short read reports the required size");

    FbyModQuery ScopedQuery{};
    const FbyModComponentId ScopedTypes[] = {ScopedType};
    Check(ScopedApi->query_begin(ScopedApi->user, ScopedTypes, 1, &ScopedQuery) == FBY_MOD_OK, "query begins");
    FbyModEntity ScopedFound = 0;
    Check(ScopedApi->query_next(ScopedApi->user, &ScopedQuery, &ScopedFound) == FBY_MOD_OK &&
              ScopedFound == ScopedEntity,
          "query finds the entity");
    Check(ScopedApi->entity_create(ScopedApi->user) == Fby::kNullEntity,
          "structural changes are rejected during a query");
    Check(ScopedApi->flush_commands(ScopedApi->user) == FBY_MOD_FORBIDDEN,
          "command flush is rejected during a query");
    ScopedApi->query_end(ScopedApi->user, &ScopedQuery);

    Check(ScopedApi->entity_destroy(ScopedApi->user, ScopedEntity) == FBY_MOD_OK, "entity destroy");
    Check(ScopedApi->entity_alive(ScopedApi->user, ScopedEntity) == 0, "destroyed entity is invalid");
    Check(ScopedApi->component_read(ScopedApi->user, ScopedEntity, ScopedType, &ScopedRead, sizeof(ScopedRead),
                                    &ScopedReadSize) == FBY_MOD_STALE_ENTITY,
          "stale entity is rejected");

    FbyModEventId ScopedEvent = 0;
    Check(ScopedApi->event_register(ScopedApi->user, "Mod.Test", &ScopedEvent) == FBY_MOD_OK && ScopedEvent != 0,
          "event registration");
    EventCounter ScopedCounter;
    Check(ScopedApi->event_subscribe(ScopedApi->user, ScopedEvent, &OnEvent, &ScopedCounter) == FBY_MOD_OK,
          "event subscription");
    const std::uint32_t ScopedValue = 77;
    Check(ScopedApi->event_publish(ScopedApi->user, ScopedEvent, &ScopedValue, sizeof(ScopedValue)) == FBY_MOD_OK,
          "event publish");
    Check(ScopedCounter.Count == 1 && ScopedCounter.LastValue == 77, "event callback receives the payload");

    Fby::Ecs::SystemGraph ScopedGraph;
    Check(ScopedHost.InstallFlushSystem(ScopedGraph), "flush system registers");
    Check(ScopedGraph.Build(), "flush system graph builds");
}


void TestDataRegistration()
{
    Fby::Fcs::Registry ScopedRegistry;
    Fby::Plugin::ComponentRegistry ScopedComponents;
    Fby::GameplayTagRegistry ScopedTags;
    Fby::GameplayEffectSystem ScopedEffects;
    Fby::GameplaySkillSystem ScopedSkills;
    Fby::Modding::ModApiHost ScopedHost({&ScopedRegistry, &ScopedComponents, &ScopedTags, &ScopedEffects, &ScopedSkills});
    ScopedHost.SetNamespace("testmod");
    const FbyModApi* ScopedApi = ScopedHost.Api();

    std::uint32_t ScopedTagId = 0;
    Check(ScopedApi->register_tag(ScopedApi->user, "mod.testmod.Buff", &ScopedTagId) == FBY_MOD_OK &&
              ScopedTagId != 0,
          "namespaced tag registration");
    Check(ScopedApi->register_tag(ScopedApi->user, "Outside.Buff", &ScopedTagId) == FBY_MOD_FORBIDDEN,
          "tag outside the mod namespace is rejected");

    FbyModEffectDef ScopedEffect{};
    ScopedEffect.id = 41;
    ScopedEffect.flags = 1;
    ScopedEffect.duration = 0.0f;
    ScopedEffect.max_stacks = 1;
    Check(ScopedApi->register_effect(ScopedApi->user, &ScopedEffect) == FBY_MOD_OK &&
              ScopedEffects.FindDef(41) != nullptr,
          "effect definition registration");

    FbyModSkillDef ScopedSkill{};
    ScopedSkill.id = 42;
    Check(ScopedApi->register_skill(ScopedApi->user, &ScopedSkill) == FBY_MOD_OK &&
              ScopedSkills.FindDef(42) != nullptr,
          "skill definition registration");
}
void TestStress()
{
    Fby::Fcs::Registry ScopedRegistry;
    Fby::Plugin::ComponentRegistry ScopedComponents;
    ScopedComponents.Register<Fby::Transform2D>("Transform2D");
    Fby::Modding::ModApiHost ScopedHost({&ScopedRegistry, &ScopedComponents});
    const FbyModApi* ScopedApi = ScopedHost.Api();
    FbyModComponentId ScopedType = 0;
    Check(ScopedApi->component_find(ScopedApi->user, "Transform2D", &ScopedType) == FBY_MOD_OK,
          "stress component lookup");

    const FbyModEntity ScopedEntity = ScopedApi->entity_create(ScopedApi->user);
    Check(ScopedEntity != Fby::kNullEntity, "stress entity create");
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 100000; ++ScopedIteration)
    {
        Fby::Transform2D ScopedValue;
        ScopedValue.Position = {static_cast<float>(ScopedIteration), 1.0f};
        Check(ScopedApi->command_write(ScopedApi->user, ScopedEntity, ScopedType, &ScopedValue,
                                       sizeof(ScopedValue)) == FBY_MOD_OK,
              "stress command write");
        Check(ScopedApi->flush_commands(ScopedApi->user) == FBY_MOD_OK, "stress flush");
        Fby::Transform2D ScopedRead{};
        std::size_t ScopedSize = 0;
        Check(ScopedApi->component_read(ScopedApi->user, ScopedEntity, ScopedType, &ScopedRead, sizeof(ScopedRead),
                                        &ScopedSize) == FBY_MOD_OK,
              "stress read");
    }
    Check(ScopedApi->entity_destroy(ScopedApi->user, ScopedEntity) == FBY_MOD_OK, "stress destroy");
}
}

int main()
{
    TestBasicApi();
    TestDataRegistration();
    TestStress();
    if (GFailures == 0)
    {
        std::printf("mod_api_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "mod_api_test: %u failure(s)\n", GFailures);
    return 1;
}
