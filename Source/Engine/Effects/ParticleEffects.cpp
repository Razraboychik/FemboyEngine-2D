// FemboyEngine2D - fixed-state particle emitters and presets.
#include "Engine/Effects/ParticleEffects.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace Fby
{
namespace
{
constexpr float kParticlePi = 3.14159265358979323846f;
constexpr float kParticleEpsilon = 1.0e-4f;
constexpr float kParticleMaxStep = 0.1f;

[[nodiscard]] float RandomRange(Pcg32& InRng, float InMin, float InMax) noexcept
{
    return InMin + (InMax - InMin) * InRng.NextFloat();
}

[[nodiscard]] Vector2D RotateVector(Vector2D InValue, float InRadians) noexcept
{
    const float ScopedCos = std::cos(InRadians);
    const float ScopedSin = std::sin(InRadians);
    return Vector2D{InValue.X * ScopedCos - InValue.Y * ScopedSin, InValue.X * ScopedSin + InValue.Y * ScopedCos};
}

[[nodiscard]] Vector2D NormalizedOr(Vector2D InValue, Vector2D InFallback) noexcept
{
    const float ScopedLengthSquared = InValue.X * InValue.X + InValue.Y * InValue.Y;
    if (ScopedLengthSquared <= kParticleEpsilon)
    {
        return InFallback;
    }
    const float ScopedInverse = 1.0f / std::sqrt(ScopedLengthSquared);
    return Vector2D{InValue.X * ScopedInverse, InValue.Y * ScopedInverse};
}

[[nodiscard]] std::uint8_t LerpByte(std::uint8_t InFrom, std::uint8_t InTo, float InT) noexcept
{
    const float ScopedValue =
        static_cast<float>(InFrom) + (static_cast<float>(InTo) - static_cast<float>(InFrom)) * InT;
    return static_cast<std::uint8_t>(std::clamp(ScopedValue, 0.0f, 255.0f));
}

[[nodiscard]] Color32 LerpColor(Color32 InFrom, Color32 InTo, float InT) noexcept
{
    return Color32::FromBytes(LerpByte(InFrom.R(), InTo.R(), InT), LerpByte(InFrom.G(), InTo.G(), InT),
                              LerpByte(InFrom.B(), InTo.B(), InT), LerpByte(InFrom.A(), InTo.A(), InT));
}

[[nodiscard]] ParticleEffectSettings MakeSettings(ParticleShape InShape, Color32 InStart, Color32 InEnd,
                                                  float InGravityY, float InSpeedMin, float InSpeedMax, float InLifeMin,
                                                  float InLifeMax, float InStartSize, float InEndSize, float InRate,
                                                  bool InAdditive)
{
    ParticleEffectSettings ScopedSettings{};
    ScopedSettings.Shape = InShape;
    ScopedSettings.StartColor = InStart;
    ScopedSettings.EndColor = InEnd;
    ScopedSettings.Gravity = Vector2D{0.0f, InGravityY};
    ScopedSettings.SpeedMin = InSpeedMin;
    ScopedSettings.SpeedMax = InSpeedMax;
    ScopedSettings.LifetimeMin = InLifeMin;
    ScopedSettings.LifetimeMax = InLifeMax;
    ScopedSettings.StartSize = InStartSize;
    ScopedSettings.EndSize = InEndSize;
    ScopedSettings.EmissionRate = InRate;
    ScopedSettings.Additive = InAdditive;
    return ScopedSettings;
}

[[nodiscard]] std::array<ParticleEffectPreset, 16> MakePresets()
{
    std::array<ParticleEffectPreset, 16> ScopedPresets{};
    std::uint32_t ScopedIndex = 0;
    const auto ScopedAdd = [&ScopedPresets, &ScopedIndex](const char* InId, const ParticleEffectSettings& InSettings)
    {
        ScopedPresets[ScopedIndex].Id = InId;
        ScopedPresets[ScopedIndex].Settings = InSettings;
        ++ScopedIndex;
    };

    ParticleEffectSettings ScopedSparks = MakeSettings(ParticleShape::Spark, Color32::FromBytes(0xFF, 0xE0, 0x60, 0xFF),
                                                       Color32::FromBytes(0xFF, 0x60, 0x20, 0x00), 300.0f, 120.0f,
                                                       260.0f, 0.25f, 0.60f, 2.0f, 0.0f, 160.0f, true);
    ScopedSparks.SpreadRadians = 0.9f;
    ScopedSparks.Drag = 1.5f;
    ScopedAdd("Particle.Sparks", ScopedSparks);

    ParticleEffectSettings ScopedBlood = MakeSettings(ParticleShape::Circle, Color32::FromBytes(0xB0, 0x10, 0x18, 0xFF),
                                                      Color32::FromBytes(0x50, 0x08, 0x0C, 0x00), 500.0f, 60.0f, 220.0f,
                                                      0.40f, 0.90f, 3.0f, 1.0f, 90.0f, false);
    ScopedBlood.SpreadRadians = 0.7f;
    ScopedAdd("Particle.Blood", ScopedBlood);

    ParticleEffectSettings ScopedDust = MakeSettings(ParticleShape::Circle, Color32::FromBytes(0xA0, 0x98, 0x90, 0xB0),
                                                     Color32::FromBytes(0x70, 0x68, 0x60, 0x00), 10.0f, 10.0f, 40.0f,
                                                     1.0f, 2.5f, 2.0f, 4.0f, 25.0f, false);
    ScopedDust.Wind = Vector2D{12.0f, 0.0f};
    ScopedDust.SpreadRadians = 1.2f;
    ScopedAdd("Particle.Dust", ScopedDust);

    ParticleEffectSettings ScopedSmoke = MakeSettings(ParticleShape::Smoke, Color32::FromBytes(0x70, 0x70, 0x78, 0x90),
                                                      Color32::FromBytes(0x30, 0x30, 0x38, 0x00), -15.0f, 10.0f, 30.0f,
                                                      1.5f, 3.5f, 6.0f, 24.0f, 18.0f, false);
    ScopedSmoke.Drag = 1.5f;
    ScopedSmoke.SpreadRadians = 0.9f;
    ScopedSmoke.MaxParticles = 128;
    ScopedAdd("Particle.Smoke", ScopedSmoke);

    ParticleEffectSettings ScopedFire = MakeSettings(ParticleShape::Glow, Color32::FromBytes(0xFF, 0xD0, 0x40, 0xFF),
                                                     Color32::FromBytes(0xE0, 0x30, 0x10, 0x00), -80.0f, 30.0f, 80.0f,
                                                     0.35f, 0.70f, 10.0f, 2.0f, 200.0f, true);
    ScopedFire.SpreadRadians = 0.5f;
    ScopedFire.MaxParticles = 512;
    ScopedAdd("Particle.Fire", ScopedFire);

    ParticleEffectSettings ScopedEmbers = MakeSettings(ParticleShape::Spark, Color32::FromBytes(0xFF, 0x90, 0x30, 0xFF),
                                                       Color32::FromBytes(0xC0, 0x20, 0x00, 0x00), -20.0f, 20.0f, 60.0f,
                                                       1.0f, 2.5f, 1.0f, 2.0f, 40.0f, true);
    ScopedEmbers.Wind = Vector2D{8.0f, 0.0f};
    ScopedEmbers.SpreadRadians = 0.8f;
    ScopedAdd("Particle.Embers", ScopedEmbers);

    ParticleEffectSettings ScopedBubbles = MakeSettings(
        ParticleShape::Bubble, Color32::FromBytes(0x80, 0xD0, 0xFF, 0xC0), Color32::FromBytes(0x30, 0x70, 0xC0, 0x00),
        -90.0f, 20.0f, 50.0f, 0.6f, 1.4f, 2.0f, 5.0f, 45.0f, false);
    ScopedBubbles.SpreadRadians = 0.5f;
    ScopedAdd("Particle.Bubbles", ScopedBubbles);

    ParticleEffectSettings ScopedSplash = MakeSettings(
        ParticleShape::Circle, Color32::FromBytes(0x90, 0xC8, 0xFF, 0xD0), Color32::FromBytes(0x30, 0x60, 0xA0, 0x00),
        450.0f, 100.0f, 220.0f, 0.30f, 0.60f, 3.0f, 0.0f, 0.0f, false);
    ScopedSplash.SpreadRadians = 1.1f;
    ScopedSplash.BurstCount = 40;
    ScopedSplash.Loop = false;
    ScopedAdd("Particle.Splash", ScopedSplash);

    ParticleEffectSettings ScopedSnow = MakeSettings(
        ParticleShape::Snowflake, Color32::FromBytes(0xF0, 0xF8, 0xFF, 0xE0),
        Color32::FromBytes(0xC0, 0xD8, 0xFF, 0x00), 15.0f, 8.0f, 25.0f, 3.0f, 6.0f, 2.0f, 3.0f, 25.0f, false);
    ScopedSnow.Wind = Vector2D{10.0f, 0.0f};
    ScopedSnow.SpreadRadians = 1.4f;
    ScopedSnow.MaxParticles = 512;
    ScopedAdd("Particle.Snow", ScopedSnow);

    ParticleEffectSettings ScopedRain = MakeSettings(ParticleShape::Square, Color32::FromBytes(0x80, 0xB0, 0xE0, 0xB0),
                                                     Color32::FromBytes(0x40, 0x60, 0x90, 0x00), 700.0f, 300.0f, 420.0f,
                                                     0.5f, 0.8f, 1.0f, 1.0f, 240.0f, false);
    ScopedRain.SpreadRadians = 0.1f;
    ScopedRain.MaxParticles = 1024;
    ScopedAdd("Particle.Rain", ScopedRain);

    ParticleEffectSettings ScopedLeaves = MakeSettings(ParticleShape::Leaf, Color32::FromBytes(0xD0, 0xA0, 0x30, 0xFF),
                                                       Color32::FromBytes(0x60, 0x40, 0x10, 0x00), 40.0f, 20.0f, 60.0f,
                                                       2.0f, 4.0f, 4.0f, 3.0f, 12.0f, false);
    ScopedLeaves.Wind = Vector2D{25.0f, 0.0f};
    ScopedLeaves.Swirl = 30.0f;
    ScopedLeaves.RotationSpeed = 3.0f;
    ScopedLeaves.SpreadRadians = 1.0f;
    ScopedAdd("Particle.Leaves", ScopedLeaves);

    ParticleEffectSettings ScopedConfetti = MakeSettings(
        ParticleShape::Square, Color32::FromBytes(0xFF, 0x50, 0xB0, 0xFF), Color32::FromBytes(0x40, 0xD0, 0xFF, 0x00),
        120.0f, 80.0f, 220.0f, 1.0f, 2.0f, 3.0f, 1.0f, 120.0f, false);
    ScopedConfetti.Drag = 0.5f;
    ScopedConfetti.RotationSpeed = 8.0f;
    ScopedConfetti.SpreadRadians = 1.5f;
    ScopedAdd("Particle.Confetti", ScopedConfetti);

    ParticleEffectSettings ScopedMagic = MakeSettings(ParticleShape::Star, Color32::FromBytes(0xB0, 0x80, 0xFF, 0xFF),
                                                      Color32::FromBytes(0x40, 0xE0, 0xFF, 0x00), 0.0f, 20.0f, 70.0f,
                                                      0.8f, 1.6f, 4.0f, 0.0f, 50.0f, true);
    ScopedMagic.Turbulence = 40.0f;
    ScopedMagic.SpreadRadians = 1.2f;
    ScopedAdd("Particle.Magic", ScopedMagic);

    ParticleEffectSettings ScopedExplosion = MakeSettings(
        ParticleShape::Glow, Color32::FromBytes(0xFF, 0xC0, 0x60, 0xFF), Color32::FromBytes(0x80, 0x10, 0x00, 0x00),
        80.0f, 200.0f, 420.0f, 0.30f, 0.70f, 8.0f, 0.0f, 0.0f, true);
    ScopedExplosion.SpreadRadians = kParticlePi;
    ScopedExplosion.BurstCount = 120;
    ScopedExplosion.MaxParticles = 512;
    ScopedExplosion.Loop = false;
    ScopedAdd("Particle.Explosion", ScopedExplosion);

    ParticleEffectSettings ScopedPoison = MakeSettings(ParticleShape::Ring, Color32::FromBytes(0x80, 0xE0, 0x40, 0xC0),
                                                       Color32::FromBytes(0x20, 0x60, 0x10, 0x00), -10.0f, 10.0f, 40.0f,
                                                       1.0f, 2.0f, 4.0f, 1.0f, 22.0f, false);
    ScopedPoison.Turbulence = 25.0f;
    ScopedPoison.SpreadRadians = 1.0f;
    ScopedAdd("Particle.Poison", ScopedPoison);

    ParticleEffectSettings ScopedFrost = MakeSettings(
        ParticleShape::Diamond, Color32::FromBytes(0xC0, 0xF0, 0xFF, 0xE0), Color32::FromBytes(0x50, 0x90, 0xC0, 0x00),
        30.0f, 20.0f, 50.0f, 0.8f, 1.5f, 3.0f, 1.0f, 40.0f, false);
    ScopedFrost.SpreadRadians = 0.9f;
    ScopedAdd("Particle.Frost", ScopedFrost);

    return ScopedPresets;
}
}

const ParticleShapeFrame& ParticleShapeAtlas::Frame(ParticleShape InShape) const noexcept
{
    const std::uint32_t ScopedIndex = static_cast<std::uint32_t>(InShape);
    if (ScopedIndex >= kParticleShapeCount)
    {
        return Frames[0];
    }
    return Frames[ScopedIndex];
}

ParticleShapeAtlas ParticleShapeAtlas::Defaults() noexcept
{
    ParticleShapeAtlas ScopedAtlas{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kParticleShapeCount; ++ScopedIndex)
    {
        ScopedAtlas.Frames[ScopedIndex].Layer = ScopedIndex;
        ScopedAtlas.Frames[ScopedIndex].UvMin = Vector2D{0.0f, 0.0f};
        ScopedAtlas.Frames[ScopedIndex].UvMax = Vector2D{1.0f, 1.0f};
    }
    return ScopedAtlas;
}

void ParticleEffectSettings::Clamp() noexcept
{
    MaxParticles = std::clamp(MaxParticles, 1u, kParticleEffectMaxParticles);
    SpreadRadians = std::clamp(SpreadRadians, 0.0f, kParticlePi);
    SpeedMin = std::clamp(SpeedMin, 0.0f, 2000.0f);
    SpeedMax = std::clamp(SpeedMax, SpeedMin, 2000.0f);
    LifetimeMin = std::clamp(LifetimeMin, 0.01f, 30.0f);
    LifetimeMax = std::clamp(LifetimeMax, LifetimeMin, 30.0f);
    StartSize = std::clamp(StartSize, 0.0f, 256.0f);
    EndSize = std::clamp(EndSize, 0.0f, 256.0f);
    Drag = std::clamp(Drag, 0.0f, 20.0f);
    Turbulence = std::clamp(Turbulence, 0.0f, 2000.0f);
    Swirl = std::clamp(Swirl, -2000.0f, 2000.0f);
    EmissionRate = std::clamp(EmissionRate, 0.0f, 4000.0f);
    BurstCount = std::min(BurstCount, 1024u);
    BurstInterval = std::clamp(BurstInterval, 0.0f, 10.0f);
    Duration = std::clamp(Duration, 0.0f, 60.0f);
}

std::span<const ParticleEffectPreset> ParticleEffectPresets() noexcept
{
    static const std::array<ParticleEffectPreset, 16> ScopedPresets = MakePresets();
    return std::span<const ParticleEffectPreset>(ScopedPresets);
}

const ParticleEffectPreset* FindParticleEffectPreset(std::string_view InId) noexcept
{
    for (const ParticleEffectPreset& ScopedPreset : ParticleEffectPresets())
    {
        if (InId == ScopedPreset.Id)
        {
            return &ScopedPreset;
        }
    }
    return nullptr;
}

namespace
{
[[nodiscard]] float SmoothStep(float InEdge0, float InEdge1, float InValue) noexcept
{
    const float ScopedT = std::clamp((InValue - InEdge0) / (InEdge1 - InEdge0), 0.0f, 1.0f);
    return ScopedT * ScopedT * (3.0f - 2.0f * ScopedT);
}

[[nodiscard]] float ShapeAlpha(ParticleShape InShape, float InU, float InV) noexcept
{
    const float ScopedRadius = std::sqrt(InU * InU + InV * InV);
    switch (InShape)
    {
        case ParticleShape::Square:
        {
            const float ScopedExtent = std::max(std::fabs(InU), std::fabs(InV));
            return 1.0f - SmoothStep(0.90f, 0.98f, ScopedExtent);
        }
        case ParticleShape::Circle:
        {
            return 1.0f - SmoothStep(0.88f, 0.98f, ScopedRadius);
        }
        case ParticleShape::Ring:
        {
            return SmoothStep(0.55f, 0.65f, ScopedRadius) * (1.0f - SmoothStep(0.85f, 0.95f, ScopedRadius));
        }
        case ParticleShape::Glow:
        {
            const float ScopedFalloff = 1.0f - SmoothStep(0.0f, 1.0f, std::min(ScopedRadius, 1.0f));
            return ScopedFalloff * ScopedFalloff;
        }
        case ParticleShape::Star:
        {
            const float ScopedAngle = std::atan2(InV, InU) - kParticlePi * 0.5f;
            const float ScopedStar = 0.52f + 0.34f * std::cos(5.0f * ScopedAngle);
            return 1.0f - SmoothStep(ScopedStar - 0.05f, ScopedStar + 0.05f, ScopedRadius);
        }
        case ParticleShape::Diamond:
        {
            const float ScopedExtent = std::fabs(InU) + std::fabs(InV);
            return 1.0f - SmoothStep(0.85f, 0.98f, ScopedExtent);
        }
        case ParticleShape::Spark:
        {
            const float ScopedBody =
                (1.0f - SmoothStep(0.06f, 0.14f, std::fabs(InU))) * (1.0f - SmoothStep(0.80f, 0.98f, std::fabs(InV)));
            return ScopedBody;
        }
        case ParticleShape::Smoke:
        {
            const float ScopedFalloff = 1.0f - SmoothStep(0.15f, 1.0f, ScopedRadius);
            const float ScopedNoise = 0.10f * std::sin(9.0f * InU + 1.7f) * std::sin(7.0f * InV - 0.6f);
            return std::clamp(ScopedFalloff * 0.85f + ScopedNoise, 0.0f, 1.0f);
        }
        case ParticleShape::Bubble:
        {
            const float ScopedShell =
                SmoothStep(0.52f, 0.62f, ScopedRadius) * (1.0f - SmoothStep(0.86f, 0.96f, ScopedRadius));
            const float ScopedHighlightX = InU + 0.32f;
            const float ScopedHighlightY = InV - 0.32f;
            const float ScopedHighlight =
                1.0f - SmoothStep(0.05f, 0.16f,
                                  std::sqrt(ScopedHighlightX * ScopedHighlightX + ScopedHighlightY * ScopedHighlightY));
            return std::max(ScopedShell, ScopedHighlight * 0.85f);
        }
        case ParticleShape::Leaf:
        {
            const float ScopedCos = 0.70710678f;
            const float ScopedX = InU * ScopedCos + InV * ScopedCos;
            const float ScopedY = -InU * ScopedCos + InV * ScopedCos;
            const float ScopedBody = (ScopedX * ScopedX) / (0.35f * 0.35f) + (ScopedY * ScopedY) / (0.80f * 0.80f);
            return 1.0f - SmoothStep(0.80f, 1.0f, ScopedBody);
        }
        case ParticleShape::Snowflake:
        {
            float ScopedArms = 0.0f;
            for (std::uint32_t ScopedArm = 0; ScopedArm < 6; ++ScopedArm)
            {
                const float ScopedAngle = static_cast<float>(ScopedArm) * kParticlePi / 3.0f;
                const float ScopedDistance = std::fabs(ScopedRadius * std::sin(std::atan2(InV, InU) - ScopedAngle));
                ScopedArms = std::max(ScopedArms, 1.0f - SmoothStep(0.03f, 0.07f, ScopedDistance));
            }
            return ScopedArms * (1.0f - SmoothStep(0.85f, 0.98f, ScopedRadius));
        }
        case ParticleShape::Heart:
        {
            const float ScopedX = InU * 1.15f;
            const float ScopedY = -InV * 1.15f + 0.20f;
            const float ScopedA = ScopedX * ScopedX + ScopedY * ScopedY - 1.0f;
            const float ScopedValue = ScopedA * ScopedA * ScopedA - ScopedX * ScopedX * ScopedY * ScopedY * ScopedY;
            return 1.0f - SmoothStep(-0.08f, 0.08f, ScopedValue);
        }
        case ParticleShape::Count:
            break;
    }
    return 0.0f;
}
}

std::uint32_t BuildParticleShapeMask(ParticleShape InShape, std::uint32_t InSize,
                                     std::span<std::uint8_t> OutAlpha) noexcept
{
    if (InSize == 0)
    {
        return 0;
    }
    const std::uint64_t ScopedPixels = static_cast<std::uint64_t>(InSize) * static_cast<std::uint64_t>(InSize);
    if (ScopedPixels > OutAlpha.size() || ScopedPixels > 0xFFFFFFFFull)
    {
        return 0;
    }
    const float ScopedHalf = 1.0f / static_cast<float>(InSize);
    for (std::uint32_t ScopedY = 0; ScopedY < InSize; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InSize; ++ScopedX)
        {
            const float ScopedU = (static_cast<float>(ScopedX) + 0.5f) * ScopedHalf * 2.0f - 1.0f;
            const float ScopedV = (static_cast<float>(ScopedY) + 0.5f) * ScopedHalf * 2.0f - 1.0f;
            const float ScopedAlpha = std::clamp(ShapeAlpha(InShape, ScopedU, ScopedV), 0.0f, 1.0f);
            OutAlpha[static_cast<std::size_t>(ScopedY) * InSize + ScopedX] =
                static_cast<std::uint8_t>(ScopedAlpha * 255.0f + 0.5f);
        }
    }
    return static_cast<std::uint32_t>(ScopedPixels);
}

ParticleEffectSystem::ParticleEffectSystem() noexcept
{
    ResetFreeList();
}

void ParticleEffectSystem::ResetFreeList() noexcept
{
    MParticles = {};
    MEmitters = {};
    MParticleCounts = {};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kParticleEffectMaxParticles; ++ScopedIndex)
    {
        MFreeParticles[ScopedIndex] = ScopedIndex;
    }
    MFreeCount = kParticleEffectMaxParticles;
    MActiveParticles = 0;
    MActiveEmitters = 0;
}

void ParticleEffectSystem::Clear() noexcept
{
    ResetFreeList();
}

std::uint32_t ParticleEffectSystem::Spawn(const ParticleEffectSettings& InSettings, Vector2D InPosition,
                                          float InRotation) noexcept
{
    ParticleEffectSettings ScopedSettings = InSettings;
    ScopedSettings.Clamp();
    std::uint32_t ScopedEmitter = kInvalidParticleEmitter;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kParticleEffectMaxEmitters; ++ScopedIndex)
    {
        if (!MEmitters[ScopedIndex].Active)
        {
            ScopedEmitter = ScopedIndex;
            break;
        }
    }
    if (ScopedEmitter == kInvalidParticleEmitter)
    {
        return kInvalidParticleEmitter;
    }

    MEmitters[ScopedEmitter].Active = true;
    ++MActiveEmitters;
    InitializeEmitter(ScopedEmitter, ScopedSettings, InPosition, InRotation);
    return ScopedEmitter;
}

void ParticleEffectSystem::InitializeEmitter(std::uint32_t InEmitter, const ParticleEffectSettings& InSettings,
                                             Vector2D InPosition, float InRotation) noexcept
{
    EmitterState& ScopedState = MEmitters[InEmitter];
    ScopedState.Settings = InSettings;
    ScopedState.Position = InPosition;
    ScopedState.Rotation = InRotation;
    ScopedState.Age = 0.0f;
    ScopedState.SpawnAccumulator = 0.0f;
    ScopedState.BurstTimer = 0.0f;
    ScopedState.Rng = Pcg32::FromSeed(InSettings.Seed ^ (0x9E3779B9u * (InEmitter + 1u)));
    ScopedState.Emitting = false;

    if (InSettings.BurstInterval > 0.0f)
    {
        Emit(InEmitter, InSettings.BurstCount);
        ScopedState.Emitting = InSettings.BurstCount > 0u || InSettings.EmissionRate > 0.0f;
    }
    else if (InSettings.EmissionRate <= 0.0f)
    {
        Emit(InEmitter, InSettings.BurstCount);
        ScopedState.Emitting = false;
    }
    else
    {
        ScopedState.Emitting = true;
    }
}

bool ParticleEffectSystem::RestartEmitter(std::uint32_t InEmitter, const ParticleEffectSettings& InSettings,
                                          Vector2D InPosition, float InRotation) noexcept
{
    if (InEmitter >= kParticleEffectMaxEmitters || !MEmitters[InEmitter].Active)
    {
        return false;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kParticleEffectMaxParticles; ++ScopedIndex)
    {
        if (MParticles[ScopedIndex].Alive && MParticles[ScopedIndex].Emitter == InEmitter)
        {
            KillParticle(ScopedIndex);
        }
    }
    ParticleEffectSettings ScopedSettings = InSettings;
    ScopedSettings.Clamp();
    InitializeEmitter(InEmitter, ScopedSettings, InPosition, InRotation);
    return true;
}

std::uint32_t ParticleEffectSystem::SpawnPreset(std::string_view InPresetId, Vector2D InPosition,
                                                float InRotation) noexcept
{
    const ParticleEffectPreset* ScopedPreset = FindParticleEffectPreset(InPresetId);
    if (ScopedPreset == nullptr)
    {
        return kInvalidParticleEmitter;
    }
    return Spawn(ScopedPreset->Settings, InPosition, InRotation);
}

bool ParticleEffectSystem::SetEmitterTransform(std::uint32_t InEmitter, Vector2D InPosition, float InRotation) noexcept
{
    if (InEmitter >= kParticleEffectMaxEmitters || !MEmitters[InEmitter].Active)
    {
        return false;
    }
    MEmitters[InEmitter].Position = InPosition;
    MEmitters[InEmitter].Rotation = InRotation;
    return true;
}

void ParticleEffectSystem::StopEmitter(std::uint32_t InEmitter) noexcept
{
    if (InEmitter >= kParticleEffectMaxEmitters || !MEmitters[InEmitter].Active)
    {
        return;
    }
    MEmitters[InEmitter].Emitting = false;
    if (MParticleCounts[InEmitter] == 0)
    {
        MEmitters[InEmitter].Active = false;
        --MActiveEmitters;
    }
}

void ParticleEffectSystem::Update(float InDeltaSeconds) noexcept
{
    const float ScopedDelta = std::clamp(InDeltaSeconds, 0.0f, kParticleMaxStep);
    for (std::uint32_t ScopedEmitter = 0; ScopedEmitter < kParticleEffectMaxEmitters; ++ScopedEmitter)
    {
        EmitterState& ScopedState = MEmitters[ScopedEmitter];
        if (!ScopedState.Active)
        {
            continue;
        }
        if (!ScopedState.Emitting && MParticleCounts[ScopedEmitter] == 0)
        {
            ScopedState.Active = false;
            --MActiveEmitters;
            continue;
        }
        if (!ScopedState.Emitting)
        {
            continue;
        }

        ScopedState.Age += ScopedDelta;
        if (ScopedState.Settings.Duration > 0.0f && ScopedState.Age >= ScopedState.Settings.Duration)
        {
            if (ScopedState.Settings.Loop)
            {
                ScopedState.Age = std::fmod(ScopedState.Age, ScopedState.Settings.Duration);
            }
            else
            {
                ScopedState.Emitting = false;
                ScopedState.Age = ScopedState.Settings.Duration;
            }
        }
        if (!ScopedState.Emitting)
        {
            continue;
        }

        if (ScopedState.Settings.EmissionRate > 0.0f)
        {
            ScopedState.SpawnAccumulator += ScopedState.Settings.EmissionRate * ScopedDelta;
            const std::uint32_t ScopedSpawn = static_cast<std::uint32_t>(ScopedState.SpawnAccumulator);
            ScopedState.SpawnAccumulator -= static_cast<float>(ScopedSpawn);
            Emit(ScopedEmitter, ScopedSpawn);
        }
        if (ScopedState.Settings.BurstInterval > 0.0f)
        {
            ScopedState.BurstTimer += ScopedDelta;
            while (ScopedState.BurstTimer >= ScopedState.Settings.BurstInterval)
            {
                ScopedState.BurstTimer -= ScopedState.Settings.BurstInterval;
                Emit(ScopedEmitter, ScopedState.Settings.BurstCount);
            }
        }
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kParticleEffectMaxParticles; ++ScopedIndex)
    {
        ParticleState& ScopedParticle = MParticles[ScopedIndex];
        if (!ScopedParticle.Alive)
        {
            continue;
        }
        ScopedParticle.Age += ScopedDelta;
        if (ScopedParticle.Age >= ScopedParticle.Lifetime)
        {
            KillParticle(ScopedIndex);
            continue;
        }
        EmitterState& ScopedEmitter = MEmitters[ScopedParticle.Emitter];
        Vector2D ScopedAcceleration = ScopedEmitter.Settings.Gravity + ScopedEmitter.Settings.Wind;
        if (ScopedEmitter.Settings.Turbulence > 0.0f)
        {
            const float ScopedTurbulence = ScopedEmitter.Settings.Turbulence;
            ScopedAcceleration.X += (ScopedEmitter.Rng.NextFloat() * 2.0f - 1.0f) * ScopedTurbulence;
            ScopedAcceleration.Y += (ScopedEmitter.Rng.NextFloat() * 2.0f - 1.0f) * ScopedTurbulence;
        }
        if (ScopedEmitter.Settings.Swirl != 0.0f)
        {
            const Vector2D ScopedRelative = ScopedParticle.Position - ScopedParticle.Origin;
            const float ScopedLengthSquared = ScopedRelative.X * ScopedRelative.X + ScopedRelative.Y * ScopedRelative.Y;
            if (ScopedLengthSquared > kParticleEpsilon)
            {
                const float ScopedInverse = 1.0f / std::sqrt(ScopedLengthSquared);
                ScopedAcceleration.X += -ScopedRelative.Y * ScopedInverse * ScopedEmitter.Settings.Swirl;
                ScopedAcceleration.Y += ScopedRelative.X * ScopedInverse * ScopedEmitter.Settings.Swirl;
            }
        }
        ScopedParticle.Velocity += ScopedAcceleration * ScopedDelta;
        if (ScopedEmitter.Settings.Drag > 0.0f)
        {
            const float ScopedDecay = std::exp(-ScopedEmitter.Settings.Drag * ScopedDelta);
            ScopedParticle.Velocity *= ScopedDecay;
        }
        ScopedParticle.Position += ScopedParticle.Velocity * ScopedDelta;
        ScopedParticle.Rotation += ScopedParticle.RotationSpeed * ScopedDelta;
    }

    for (std::uint32_t ScopedEmitter = 0; ScopedEmitter < kParticleEffectMaxEmitters; ++ScopedEmitter)
    {
        EmitterState& ScopedState = MEmitters[ScopedEmitter];
        if (ScopedState.Active && !ScopedState.Emitting && MParticleCounts[ScopedEmitter] == 0)
        {
            ScopedState.Active = false;
            --MActiveEmitters;
        }
    }
}

void ParticleEffectSystem::Emit(std::uint32_t InEmitter, std::uint32_t InCount) noexcept
{
    EmitterState& ScopedEmitter = MEmitters[InEmitter];
    for (std::uint32_t ScopedSpawn = 0; ScopedSpawn < InCount; ++ScopedSpawn)
    {
        if (MParticleCounts[InEmitter] >= ScopedEmitter.Settings.MaxParticles)
        {
            break;
        }
        SpawnParticle(InEmitter);
    }
}

void ParticleEffectSystem::SpawnParticle(std::uint32_t InEmitter) noexcept
{
    if (MFreeCount == 0)
    {
        return;
    }
    const std::uint32_t ScopedIndex = MFreeParticles[--MFreeCount];
    ParticleState& ScopedParticle = MParticles[ScopedIndex];
    EmitterState& ScopedEmitter = MEmitters[InEmitter];
    const ParticleEffectSettings& ScopedSettings = ScopedEmitter.Settings;

    Vector2D ScopedOffset{};
    switch (ScopedSettings.EmissionShape)
    {
        case ParticleEmissionShape::Circle:
        {
            const float ScopedRadius = std::max(ScopedSettings.EmissionExtents.X, 0.0f);
            const float ScopedAngle = ScopedEmitter.Rng.NextFloat() * 2.0f * kParticlePi;
            const float ScopedDistance = ScopedRadius * std::sqrt(ScopedEmitter.Rng.NextFloat());
            ScopedOffset = Vector2D{std::cos(ScopedAngle), std::sin(ScopedAngle)} * ScopedDistance;
            break;
        }
        case ParticleEmissionShape::Box:
        {
            ScopedOffset = Vector2D{
                RandomRange(ScopedEmitter.Rng, -ScopedSettings.EmissionExtents.X, ScopedSettings.EmissionExtents.X),
                RandomRange(ScopedEmitter.Rng, -ScopedSettings.EmissionExtents.Y, ScopedSettings.EmissionExtents.Y)};
            break;
        }
        case ParticleEmissionShape::Line:
        {
            ScopedOffset = Vector2D{
                RandomRange(ScopedEmitter.Rng, -ScopedSettings.EmissionExtents.X, ScopedSettings.EmissionExtents.X),
                0.0f};
            break;
        }
        case ParticleEmissionShape::Point:
        case ParticleEmissionShape::Count:
            break;
    }
    ScopedOffset = RotateVector(ScopedOffset, ScopedEmitter.Rotation);

    Vector2D ScopedDirection = NormalizedOr(ScopedSettings.Direction, Vector2D{0.0f, -1.0f});
    ScopedDirection = RotateVector(ScopedDirection, ScopedEmitter.Rotation);
    const float ScopedBaseAngle = std::atan2(ScopedDirection.Y, ScopedDirection.X);
    const float ScopedAngle =
        ScopedBaseAngle + (ScopedEmitter.Rng.NextFloat() * 2.0f - 1.0f) * ScopedSettings.SpreadRadians;
    const Vector2D ScopedVelocityDirection{std::cos(ScopedAngle), std::sin(ScopedAngle)};
    const float ScopedSpeed = RandomRange(ScopedEmitter.Rng, ScopedSettings.SpeedMin, ScopedSettings.SpeedMax);

    ScopedParticle = ParticleState{};
    ScopedParticle.Position = ScopedEmitter.Position + ScopedOffset;
    ScopedParticle.Velocity = ScopedVelocityDirection * ScopedSpeed;
    ScopedParticle.Origin = ScopedEmitter.Position;
    ScopedParticle.Age = 0.0f;
    ScopedParticle.Lifetime = RandomRange(ScopedEmitter.Rng, ScopedSettings.LifetimeMin, ScopedSettings.LifetimeMax);
    ScopedParticle.StartSize = ScopedSettings.StartSize;
    ScopedParticle.EndSize = ScopedSettings.EndSize;
    ScopedParticle.Rotation = ScopedSettings.StartRotation + ScopedEmitter.Rng.NextFloat() * 2.0f * kParticlePi;
    ScopedParticle.RotationSpeed = ScopedSettings.RotationSpeed;
    ScopedParticle.StartColor = ScopedSettings.StartColor;
    ScopedParticle.EndColor = ScopedSettings.EndColor;
    ScopedParticle.Emitter = InEmitter;
    ScopedParticle.Shape = static_cast<std::uint8_t>(ScopedSettings.Shape);
    ScopedParticle.Alive = true;
    ++MParticleCounts[InEmitter];
    ++MActiveParticles;
}

void ParticleEffectSystem::KillParticle(std::uint32_t InIndex) noexcept
{
    ParticleState& ScopedParticle = MParticles[InIndex];
    if (!ScopedParticle.Alive)
    {
        return;
    }
    ScopedParticle.Alive = false;
    --MParticleCounts[ScopedParticle.Emitter];
    --MActiveParticles;
    MFreeParticles[MFreeCount++] = InIndex;
}

std::uint32_t ParticleEffectSystem::BuildRenderItems(std::span<Rhi::RenderItem> Out,
                                                     const ParticleShapeAtlas& InAtlas) const noexcept
{
    std::uint32_t ScopedCount = 0;
    for (const ParticleState& ScopedParticle : MParticles)
    {
        if (!ScopedParticle.Alive || ScopedCount >= Out.size())
        {
            continue;
        }
        const float ScopedT = std::clamp(ScopedParticle.Age / ScopedParticle.Lifetime, 0.0f, 1.0f);
        const float ScopedSize =
            ScopedParticle.StartSize + (ScopedParticle.EndSize - ScopedParticle.StartSize) * ScopedT;
        if (ScopedSize <= 0.0f)
        {
            continue;
        }
        const EmitterState& ScopedEmitter = MEmitters[ScopedParticle.Emitter];
        const ParticleShapeFrame& ScopedFrame = InAtlas.Frame(static_cast<ParticleShape>(ScopedParticle.Shape));
        Rhi::RenderItem ScopedItem =
            Rhi::RenderItem::Quad(ScopedParticle.Position, Vector2D{ScopedSize, ScopedSize},
                                  LerpColor(ScopedParticle.StartColor, ScopedParticle.EndColor, ScopedT).rgba);
        ScopedItem.uvMin = ScopedFrame.UvMin;
        ScopedItem.uvMax = ScopedFrame.UvMax;
        ScopedItem.layer = ScopedFrame.Layer;
        ScopedItem.rotation = ScopedParticle.Rotation;
        if (ScopedEmitter.Settings.Additive)
        {
            ScopedItem.flags |= static_cast<std::uint32_t>(Rhi::RenderFlags::Additive);
        }
        Out[ScopedCount++] = ScopedItem;
    }
    return ScopedCount;
}
}
