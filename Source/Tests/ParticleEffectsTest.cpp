// FemboyEngine2D - particle effect settings, presets and simulation tests.
#include "Engine/Effects/ParticleEffects.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>

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

[[nodiscard]] Fby::ParticleEffectSettings MakeLinearSettings()
{
    Fby::ParticleEffectSettings ScopedSettings{};
    ScopedSettings.MaxParticles = 8;
    ScopedSettings.EmissionRate = 0.0f;
    ScopedSettings.BurstCount = 1;
    ScopedSettings.SpeedMin = 0.0f;
    ScopedSettings.SpeedMax = 0.0f;
    ScopedSettings.LifetimeMin = 10.0f;
    ScopedSettings.LifetimeMax = 10.0f;
    ScopedSettings.StartSize = 4.0f;
    ScopedSettings.EndSize = 4.0f;
    ScopedSettings.StartRotation = 0.0f;
    ScopedSettings.RotationSpeed = 0.0f;
    ScopedSettings.SpreadRadians = 0.0f;
    ScopedSettings.Gravity = Fby::Vector2D{0.0f, 0.0f};
    ScopedSettings.StartColor = Fby::Color32::White();
    ScopedSettings.EndColor = Fby::Color32::White();
    return ScopedSettings;
}

void TestPresets()
{
    const std::span<const Fby::ParticleEffectPreset> ScopedPresets = Fby::ParticleEffectPresets();
    FBY_CHECK(ScopedPresets.size() == 16);
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedPresets.size(); ++ScopedIndex)
    {
        FBY_CHECK(ScopedPresets[ScopedIndex].Id[0] != '\0');
        FBY_CHECK(ScopedPresets[ScopedIndex].Settings.MaxParticles > 0);
        for (std::size_t ScopedOther = ScopedIndex + 1; ScopedOther < ScopedPresets.size(); ++ScopedOther)
        {
            FBY_CHECK(std::strcmp(ScopedPresets[ScopedIndex].Id, ScopedPresets[ScopedOther].Id) != 0);
        }
    }
    const Fby::ParticleEffectPreset* ScopedSparks = Fby::FindParticleEffectPreset("Particle.Sparks");
    FBY_CHECK(ScopedSparks != nullptr);
    FBY_CHECK(ScopedSparks != nullptr && ScopedSparks->Settings.Additive);
    FBY_CHECK(Fby::FindParticleEffectPreset("Particle.Missing") == nullptr);

    Fby::ParticleEffectSettings ScopedClamped{};
    ScopedClamped.MaxParticles = 0;
    ScopedClamped.SpeedMin = 500.0f;
    ScopedClamped.SpeedMax = 10.0f;
    ScopedClamped.LifetimeMin = 0.0f;
    ScopedClamped.SpreadRadians = 10.0f;
    ScopedClamped.EmissionRate = -5.0f;
    ScopedClamped.Clamp();
    FBY_CHECK(ScopedClamped.MaxParticles == 1);
    FBY_CHECK(ScopedClamped.SpeedMax >= ScopedClamped.SpeedMin);
    FBY_CHECK(ScopedClamped.LifetimeMin > 0.0f);
    FBY_CHECK(ScopedClamped.SpreadRadians <= 3.1415927f);
    FBY_CHECK(ScopedClamped.EmissionRate == 0.0f);
}

void TestRateAndBurst()
{
    Fby::ParticleEffectSettings ScopedRate = MakeLinearSettings();

    Fby::ParticleEffectSystem ScopedPresetSystem;
    FBY_CHECK(ScopedPresetSystem.SpawnPreset("Particle.Sparks", Fby::Vector2D{0.0f, 0.0f}) !=
              Fby::kInvalidParticleEmitter);
    FBY_CHECK(ScopedPresetSystem.SpawnPreset("Particle.Missing", Fby::Vector2D{0.0f, 0.0f}) ==
              Fby::kInvalidParticleEmitter);
    ScopedRate.EmissionRate = 100.0f;
    ScopedRate.BurstCount = 0;
    ScopedRate.MaxParticles = 64;
    ScopedRate.LifetimeMin = 10.0f;
    ScopedRate.LifetimeMax = 10.0f;
    Fby::ParticleEffectSystem ScopedSystem;
    FBY_CHECK(ScopedSystem.Spawn(ScopedRate, Fby::Vector2D{0.0f, 0.0f}) != Fby::kInvalidParticleEmitter);
    FBY_CHECK(ScopedSystem.ActiveParticles() == 0);
    ScopedSystem.Update(0.1f);
    FBY_CHECK(ScopedSystem.ActiveParticles() == 10);
    ScopedSystem.Update(0.1f);
    FBY_CHECK(ScopedSystem.ActiveParticles() == 20);

    Fby::ParticleEffectSettings ScopedBurst = MakeLinearSettings();
    ScopedBurst.BurstCount = 7;
    ScopedBurst.MaxParticles = 64;
    Fby::ParticleEffectSystem ScopedBurstSystem;
    FBY_CHECK(ScopedBurstSystem.Spawn(ScopedBurst, Fby::Vector2D{0.0f, 0.0f}) != Fby::kInvalidParticleEmitter);
    FBY_CHECK(ScopedBurstSystem.ActiveParticles() == 7);
    ScopedBurstSystem.Update(0.1f);
    FBY_CHECK(ScopedBurstSystem.ActiveParticles() == 7);

    Fby::ParticleEffectSettings ScopedInterval = MakeLinearSettings();
    ScopedInterval.BurstCount = 3;
    ScopedInterval.BurstInterval = 0.1f;
    ScopedInterval.MaxParticles = 64;
    ScopedInterval.LifetimeMin = 10.0f;
    ScopedInterval.LifetimeMax = 10.0f;
    Fby::ParticleEffectSystem ScopedIntervalSystem;
    (void)ScopedIntervalSystem.Spawn(ScopedInterval, Fby::Vector2D{0.0f, 0.0f});
    ScopedIntervalSystem.Update(0.1f);
    FBY_CHECK(ScopedIntervalSystem.ActiveParticles() == 6);
}

void TestLifetimeAndLimits()
{
    Fby::ParticleEffectSettings ScopedSettings = MakeLinearSettings();
    ScopedSettings.LifetimeMin = 0.05f;
    ScopedSettings.LifetimeMax = 0.05f;
    Fby::ParticleEffectSystem ScopedSystem;
    (void)ScopedSystem.Spawn(ScopedSettings, Fby::Vector2D{0.0f, 0.0f});
    FBY_CHECK(ScopedSystem.ActiveEmitters() == 1);
    ScopedSystem.Update(0.1f);
    FBY_CHECK(ScopedSystem.ActiveParticles() == 0);
    FBY_CHECK(ScopedSystem.ActiveEmitters() == 0);

    Fby::ParticleEffectSettings ScopedCap = MakeLinearSettings();
    ScopedCap.MaxParticles = 5;
    ScopedCap.EmissionRate = 1000.0f;
    ScopedCap.BurstCount = 0;
    Fby::ParticleEffectSystem ScopedCapSystem;
    (void)ScopedCapSystem.Spawn(ScopedCap, Fby::Vector2D{0.0f, 0.0f});
    ScopedCapSystem.Update(0.1f);
    FBY_CHECK(ScopedCapSystem.ActiveParticles() == 5);

    Fby::ParticleShapeAtlas ScopedAtlas = Fby::ParticleShapeAtlas::Defaults();
    std::array<Fby::Rhi::RenderItem, 2> ScopedItems{};
    const std::uint32_t ScopedWritten = ScopedCapSystem.BuildRenderItems(ScopedItems, ScopedAtlas);
    FBY_CHECK(ScopedWritten == 2);

    ScopedCapSystem.Clear();
    FBY_CHECK(ScopedCapSystem.ActiveParticles() == 0);
    FBY_CHECK(ScopedCapSystem.ActiveEmitters() == 0);
}

void TestForces()
{
    Fby::ParticleEffectSettings ScopedGravity = MakeLinearSettings();
    ScopedGravity.Gravity = Fby::Vector2D{0.0f, 100.0f};
    Fby::ParticleEffectSystem ScopedGravitySystem;
    (void)ScopedGravitySystem.Spawn(ScopedGravity, Fby::Vector2D{0.0f, 0.0f});
    ScopedGravitySystem.Update(0.1f);
    Fby::ParticleShapeAtlas ScopedAtlas = Fby::ParticleShapeAtlas::Defaults();
    std::array<Fby::Rhi::RenderItem, 8> ScopedItems{};
    FBY_CHECK(ScopedGravitySystem.BuildRenderItems(ScopedItems, ScopedAtlas) == 1);
    FBY_CHECK(ScopedItems[0].position.Y > 0.5f);

    Fby::ParticleEffectSettings ScopedMove = MakeLinearSettings();
    ScopedMove.Direction = Fby::Vector2D{1.0f, 0.0f};
    ScopedMove.SpeedMin = 100.0f;
    ScopedMove.SpeedMax = 100.0f;
    Fby::ParticleEffectSystem ScopedNoDrag;
    Fby::ParticleEffectSystem ScopedDrag;
    ScopedMove.Drag = 0.0f;
    (void)ScopedNoDrag.Spawn(ScopedMove, Fby::Vector2D{0.0f, 0.0f});
    ScopedMove.Drag = 5.0f;
    (void)ScopedDrag.Spawn(ScopedMove, Fby::Vector2D{0.0f, 0.0f});
    ScopedNoDrag.Update(0.1f);
    ScopedDrag.Update(0.1f);
    std::array<Fby::Rhi::RenderItem, 8> ScopedNoDragItems{};
    std::array<Fby::Rhi::RenderItem, 8> ScopedDragItems{};
    (void)ScopedNoDrag.BuildRenderItems(ScopedNoDragItems, ScopedAtlas);
    (void)ScopedDrag.BuildRenderItems(ScopedDragItems, ScopedAtlas);
    FBY_CHECK(ScopedDragItems[0].position.X < ScopedNoDragItems[0].position.X);
    FBY_CHECK(ScopedDragItems[0].position.X > 0.0f);
}

void TestShapesAndAtlas()
{
    Fby::ParticleEffectSettings ScopedSettings = MakeLinearSettings();
    ScopedSettings.Shape = Fby::ParticleShape::Star;
    ScopedSettings.Additive = true;
    Fby::ParticleEffectSystem ScopedSystem;
    (void)ScopedSystem.Spawn(ScopedSettings, Fby::Vector2D{0.0f, 0.0f});

    Fby::ParticleShapeAtlas ScopedAtlas = Fby::ParticleShapeAtlas::Defaults();
    ScopedAtlas.Frames[static_cast<std::uint32_t>(Fby::ParticleShape::Star)].Layer = 42;
    ScopedAtlas.Frames[static_cast<std::uint32_t>(Fby::ParticleShape::Star)].UvMin = Fby::Vector2D{0.25f, 0.0f};
    ScopedAtlas.Frames[static_cast<std::uint32_t>(Fby::ParticleShape::Star)].UvMax = Fby::Vector2D{0.5f, 1.0f};
    std::array<Fby::Rhi::RenderItem, 8> ScopedItems{};
    FBY_CHECK(ScopedSystem.BuildRenderItems(ScopedItems, ScopedAtlas) == 1);
    FBY_CHECK(ScopedItems[0].layer == 42);
    FBY_CHECK(std::fabs(ScopedItems[0].uvMin.X - 0.25f) < 0.001f);
    FBY_CHECK(std::fabs(ScopedItems[0].uvMax.X - 0.5f) < 0.001f);
    FBY_CHECK((ScopedItems[0].flags & static_cast<std::uint32_t>(Fby::Rhi::RenderFlags::Additive)) != 0);

    Fby::ParticleEffectSettings ScopedBox = MakeLinearSettings();
    ScopedBox.EmissionShape = Fby::ParticleEmissionShape::Box;
    ScopedBox.EmissionExtents = Fby::Vector2D{10.0f, 5.0f};
    Fby::ParticleEffectSystem ScopedBoxSystem;
    (void)ScopedBoxSystem.Spawn(ScopedBox, Fby::Vector2D{100.0f, 50.0f});
    ScopedBoxSystem.Update(0.01f);
    FBY_CHECK(ScopedBoxSystem.BuildRenderItems(ScopedItems, ScopedAtlas) == 1);
    FBY_CHECK(ScopedItems[0].position.X >= 90.0f && ScopedItems[0].position.X <= 110.0f);
    FBY_CHECK(ScopedItems[0].position.Y >= 45.0f && ScopedItems[0].position.Y <= 55.0f);
}

void TestShapeMasks()
{
    std::array<std::uint8_t, 64 * 64> ScopedMask{};
    FBY_CHECK(Fby::BuildParticleShapeMask(Fby::ParticleShape::Square, 64, ScopedMask) == 64 * 64);
    FBY_CHECK(ScopedMask[32 * 64 + 32] > 200);
    FBY_CHECK(ScopedMask[0] == 0);

    FBY_CHECK(Fby::BuildParticleShapeMask(Fby::ParticleShape::Circle, 64, ScopedMask) == 64 * 64);
    FBY_CHECK(ScopedMask[32 * 64 + 32] > 200);
    FBY_CHECK(ScopedMask[0] == 0);

    FBY_CHECK(Fby::BuildParticleShapeMask(Fby::ParticleShape::Ring, 64, ScopedMask) == 64 * 64);
    FBY_CHECK(ScopedMask[32 * 64 + 32] < 40);
    FBY_CHECK(ScopedMask[32 * 64 + 56] > 100);

    FBY_CHECK(Fby::BuildParticleShapeMask(Fby::ParticleShape::Glow, 64, ScopedMask) == 64 * 64);
    FBY_CHECK(ScopedMask[32 * 64 + 32] > ScopedMask[32 * 64 + 4]);

    FBY_CHECK(Fby::BuildParticleShapeMask(Fby::ParticleShape::Square, 8, std::span<std::uint8_t>()) == 0);
    for (std::uint8_t ScopedShape = 0; ScopedShape < static_cast<std::uint8_t>(Fby::ParticleShape::Count);
         ++ScopedShape)
    {
        FBY_CHECK(Fby::BuildParticleShapeMask(static_cast<Fby::ParticleShape>(ScopedShape), 32, ScopedMask) == 32 * 32);
        std::uint32_t ScopedNonZero = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 32 * 32; ++ScopedIndex)
        {
            ScopedNonZero += ScopedMask[ScopedIndex] > 0 ? 1u : 0u;
        }
        FBY_CHECK(ScopedNonZero > 0);
    }
}

void TestDeterminismAndTransform()
{
    Fby::ParticleEffectSettings ScopedSettings = MakeLinearSettings();
    ScopedSettings.EmissionRate = 50.0f;
    ScopedSettings.BurstCount = 0;
    ScopedSettings.MaxParticles = 512;
    ScopedSettings.LifetimeMin = 2.0f;
    ScopedSettings.LifetimeMax = 2.0f;
    ScopedSettings.Gravity = Fby::Vector2D{0.0f, 50.0f};
    ScopedSettings.Turbulence = 20.0f;

    Fby::ParticleEffectSystem ScopedFirst;
    Fby::ParticleEffectSystem ScopedSecond;
    const std::uint32_t ScopedFirstEmitter = ScopedFirst.Spawn(ScopedSettings, Fby::Vector2D{0.0f, 0.0f});
    const std::uint32_t ScopedSecondEmitter = ScopedSecond.Spawn(ScopedSettings, Fby::Vector2D{0.0f, 0.0f});
    FBY_CHECK(ScopedFirstEmitter != Fby::kInvalidParticleEmitter);
    FBY_CHECK(ScopedSecondEmitter != Fby::kInvalidParticleEmitter);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 30; ++ScopedStep)
    {
        ScopedFirst.Update(0.016f);
        ScopedSecond.Update(0.016f);
    }
    FBY_CHECK(ScopedFirst.ActiveParticles() == ScopedSecond.ActiveParticles());

    Fby::ParticleShapeAtlas ScopedAtlas = Fby::ParticleShapeAtlas::Defaults();
    std::array<Fby::Rhi::RenderItem, 512> ScopedFirstItems{};
    std::array<Fby::Rhi::RenderItem, 512> ScopedSecondItems{};
    const std::uint32_t ScopedFirstCount = ScopedFirst.BuildRenderItems(ScopedFirstItems, ScopedAtlas);
    const std::uint32_t ScopedSecondCount = ScopedSecond.BuildRenderItems(ScopedSecondItems, ScopedAtlas);
    FBY_CHECK(ScopedFirstCount == ScopedSecondCount);
    FBY_CHECK(ScopedFirstCount > 0);
    FBY_CHECK(std::memcmp(ScopedFirstItems.data(), ScopedSecondItems.data(),
                          ScopedFirstCount * sizeof(Fby::Rhi::RenderItem)) == 0);

    Fby::ParticleEffectSettings ScopedMoved = MakeLinearSettings();
    ScopedMoved.EmissionRate = 10.0f;
    ScopedMoved.BurstCount = 0;
    Fby::ParticleEffectSystem ScopedMover;
    const std::uint32_t ScopedMoverEmitter = ScopedMover.Spawn(ScopedMoved, Fby::Vector2D{0.0f, 0.0f});
    FBY_CHECK(ScopedMover.SetEmitterTransform(ScopedMoverEmitter, Fby::Vector2D{100.0f, 0.0f}, 0.0f));
    ScopedMover.Update(0.2f);
    std::array<Fby::Rhi::RenderItem, 8> ScopedMovedItems{};
    const std::uint32_t ScopedMovedCount = ScopedMover.BuildRenderItems(ScopedMovedItems, ScopedAtlas);
    FBY_CHECK(ScopedMovedCount > 0);
    FBY_CHECK(ScopedMovedItems[0].position.X >= 100.0f);
    FBY_CHECK(!ScopedMover.SetEmitterTransform(Fby::kInvalidParticleEmitter, Fby::Vector2D{}, 0.0f));
}
}

int main()
{
    TestPresets();
    TestRateAndBurst();
    TestLifetimeAndLimits();
    TestForces();
    TestShapesAndAtlas();
    TestShapeMasks();
    TestDeterminismAndTransform();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u particle effect failure(s)\n", GFailures);
        return 1;
    }
    std::printf("particle effects: all checks passed\n");
    return 0;
}
