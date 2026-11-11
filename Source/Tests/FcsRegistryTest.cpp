// FemboyEngine2D - FCS registry adapter smoke test.
#include "Engine/ECS/FCS/Registry.hpp"

#include <cstdio>

namespace
{

struct Position
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct Velocity
{
    float X = 0.0f;
    float Y = 0.0f;
};

int Failures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("fcs_registry_test: FAIL: %s\n", InMessage);
        ++Failures;
    }
}

}

int main()
{
    Fby::Fcs::Registry ScopedRegistry;
    const Fby::Entity ScopedFirst = ScopedRegistry.create();
    const Fby::Entity ScopedSecond = ScopedRegistry.create();
    ScopedRegistry.emplace<Position>(ScopedFirst, Position{1.0f, 2.0f});
    ScopedRegistry.emplace<Velocity>(ScopedFirst, Velocity{3.0f, 4.0f});
    ScopedRegistry.emplace<Position>(ScopedSecond, Position{5.0f, 6.0f});

    Check(ScopedRegistry.valid(ScopedFirst), "created entity is valid");
    Check(ScopedRegistry.all_of<Position, Velocity>(ScopedFirst), "both components are present");
    Check(!ScopedRegistry.all_of<Position, Velocity>(ScopedSecond), "the second entity has no velocity");
    Check(ScopedRegistry.size() == 2, "registry size counts live entities");

    std::size_t ScopedPairCount = 0;
    float ScopedVelocitySum = 0.0f;
    for (auto [ScopedEntity, ScopedPosition, ScopedVelocity] : ScopedRegistry.view<Position, Velocity>().each())
    {
        (void)ScopedEntity;
        ScopedPosition.X += ScopedVelocity.X;
        ScopedVelocitySum += ScopedVelocity.X;
        ++ScopedPairCount;
    }
    Check(ScopedPairCount == 1, "view.each sees the pair archetype");
    Check(ScopedVelocitySum == 3.0f, "view.each exposes component references");
    Check(ScopedRegistry.view<Position>().get<Position>(ScopedFirst).X == 4.0f, "view.get reads the mutated value");

    Check(ScopedRegistry.remove<Velocity>(ScopedFirst), "remove succeeds");
    Check(!ScopedRegistry.all_of<Velocity>(ScopedFirst), "velocity is gone");
    Check(ScopedRegistry.get<Position>(ScopedFirst).X == 4.0f, "position survives the archetype move");

    const auto& ScopedPosition = ScopedRegistry.get_or_emplace<Position>(ScopedSecond);
    Check(ScopedPosition.X == 5.0f, "get_or_emplace keeps the existing value");
    ScopedRegistry.emplace_or_replace<Position>(ScopedSecond, Position{8.0f, 9.0f});
    Check(ScopedRegistry.get<Position>(ScopedSecond).X == 8.0f, "emplace_or_replace updates the value");

    ScopedRegistry.ctx().emplace<int>(42);
    Check(ScopedRegistry.ctx().find<int>() != nullptr && *ScopedRegistry.ctx().find<int>() == 42,
          "context stores a value");
    ScopedRegistry.ctx().insert_or_assign(int{7});
    Check(*ScopedRegistry.ctx().find<int>() == 7, "context assigns a value");

    std::size_t ScopedStorageCount = 0;
    for (const Fby::Entity ScopedEntity : ScopedRegistry.storage<Fby::Entity>())
    {
        (void)ScopedEntity;
        ++ScopedStorageCount;
    }
    Check(ScopedStorageCount == 2, "storage iterates live entities");

    ScopedRegistry.destroy(ScopedSecond);
    Check(!ScopedRegistry.valid(ScopedSecond), "destroyed entity is invalid");
    Check(ScopedRegistry.size() == 1, "size drops after destroy");

    ScopedRegistry.clear();
    Check(ScopedRegistry.empty(), "clear drops every entity");

    if (Failures == 0)
    {
        std::printf("fcs_registry_test: OK\n");
        return 0;
    }
    return 1;
}
