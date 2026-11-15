// FemboyEngine2D - scene particle component and binding tests.
#include "Engine/Effects/ParticleScene.hpp"

#include <array>
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

Fby::ParticleEffectComponent MakeComponent(const char* InPreset)
{
    Fby::ParticleEffectComponent ScopedComponent{};
    FBY_CHECK(FillParticleEffectComponent(ScopedComponent, InPreset));
    ScopedComponent.MaxParticles = 64;
    ScopedComponent.EmissionRate = 120.0f;
    ScopedComponent.LifetimeMin = 10.0f;
    ScopedComponent.LifetimeMax = 10.0f;
    ScopedComponent.SpeedMin = 0.0f;
    ScopedComponent.SpeedMax = 0.0f;
    ScopedComponent.Gravity = Fby::Vector2D{0.0f, 0.0f};
    return ScopedComponent;
}
}

int main()
{
    Fby::Fcs::Registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(
        ScopedEntity, Fby::Transform2D{Fby::Vector2D{100.0f, 50.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedRegistry.emplace<Fby::ParticleEffectComponent>(ScopedEntity, MakeComponent("Particle.Fire"));

    Fby::ParticleSceneSystem ScopedScene;
    ScopedScene.Update(ScopedRegistry, 0.1f);
    FBY_CHECK(ScopedScene.Particles().ActiveEmitters() == 1);
    FBY_CHECK(ScopedScene.Particles().ActiveParticles() > 0);

    const Fby::ParticleShapeAtlas ScopedAtlas = Fby::ParticleShapeAtlas::Defaults();
    std::array<Fby::Rhi::RenderItem, 128> ScopedItems{};
    const std::uint32_t ScopedCount = ScopedScene.BuildRenderItems(ScopedItems, ScopedAtlas);
    FBY_CHECK(ScopedCount > 0);
    FBY_CHECK(ScopedItems[0].position.X >= 100.0f);
    FBY_CHECK(ScopedItems[0].position.Y >= 50.0f);

    ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position = Fby::Vector2D{300.0f, 200.0f};
    ScopedScene.Update(ScopedRegistry, 0.1f);
    (void)ScopedScene.BuildRenderItems(ScopedItems, ScopedAtlas);
    FBY_CHECK(ScopedScene.Particles().ActiveParticles() > 0);

    ScopedRegistry.get<Fby::ParticleEffectComponent>(ScopedEntity).Flags = 0;
    ScopedScene.Update(ScopedRegistry, 0.1f);
    FBY_CHECK(ScopedScene.Particles().ActiveEmitters() == 1);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 200; ++ScopedStep)
    {
        ScopedScene.Update(ScopedRegistry, 0.1f);
    }
    FBY_CHECK(ScopedScene.Particles().ActiveParticles() == 0);
    FBY_CHECK(ScopedScene.Particles().ActiveEmitters() == 0);

    Fby::ParticleEffectComponent& ScopedComponent = ScopedRegistry.get<Fby::ParticleEffectComponent>(ScopedEntity);
    ScopedComponent.Flags = Fby::ParticleEffectComponentPlaying;
    ScopedComponent.RestartCounter = 1;
    ScopedScene.Update(ScopedRegistry, 0.1f);
    FBY_CHECK(ScopedScene.Particles().ActiveEmitters() == 1);

    const std::uint32_t ScopedBefore = ScopedScene.Particles().ActiveParticles();
    ScopedComponent.RestartCounter = 2;
    ScopedScene.Update(ScopedRegistry, 0.01f);
    FBY_CHECK(ScopedScene.Particles().ActiveParticles() <= ScopedBefore + 1);

    ScopedRegistry.remove<Fby::Transform2D>(ScopedEntity);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 200; ++ScopedStep)
    {
        ScopedScene.Update(ScopedRegistry, 0.1f);
    }
    FBY_CHECK(ScopedScene.Particles().ActiveParticles() == 0);

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u particle scene failure(s)\n", GFailures);
        return 1;
    }
    std::printf("particle scene: all checks passed\n");
    return 0;
}
