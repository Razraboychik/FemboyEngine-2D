// FemboyEngine2D - ECS adapter and scene system for the CPU particle effects.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/CoreComponents.hpp"
#include "Engine/Effects/ParticleEffects.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby
{

enum ParticleEffectComponentFlags : std::uint32_t
{
    ParticleEffectComponentPlaying = 1u << 0,
};

// Scene-facing copy of ParticleEffectSettings. The fields are flat so Mirror,
// the inspector and scene TOML see each value without a nested-struct codec.
struct ParticleEffectComponent
{
    std::uint32_t Flags = ParticleEffectComponentPlaying;
    std::uint32_t MaxParticles = 256;
    std::uint32_t Seed = 0x9E3779B9u;
    // ParticleShape and ParticleEmissionShape as their enum values.
    std::uint32_t Shape = 1;
    std::uint32_t EmissionShape = 0;
    Vector2D EmissionExtents{0.0f, 0.0f};
    Vector2D Direction{0.0f, -1.0f};
    Vector2D Gravity{0.0f, 160.0f};
    Vector2D Wind{0.0f, 0.0f};
    float SpreadRadians = 0.35f;
    float SpeedMin = 40.0f;
    float SpeedMax = 140.0f;
    float LifetimeMin = 0.4f;
    float LifetimeMax = 1.2f;
    float StartSize = 8.0f;
    float EndSize = 2.0f;
    float StartRotation = 0.0f;
    float RotationSpeed = 1.0f;
    float Drag = 0.0f;
    float Turbulence = 0.0f;
    float Swirl = 0.0f;
    float EmissionRate = 60.0f;
    std::uint32_t BurstCount = 0;
    float BurstInterval = 0.0f;
    float Duration = 0.0f;
    bool Loop = true;
    bool Additive = false;
    Color32 StartColor = Color32::FromBytes(0xFF, 0x4F, 0xA8, 0xFF);
    Color32 EndColor = Color32::FromBytes(0xFF, 0x4F, 0xA8, 0x00);
    // Bump to respawn without changing any other field.
    std::uint32_t RestartCounter = 0;

    [[nodiscard]] bool Playing() const noexcept
    {
        return (Flags & ParticleEffectComponentPlaying) != 0;
    }
    [[nodiscard]] ParticleEffectSettings ToSettings() const noexcept;
    void FromSettings(const ParticleEffectSettings& InSettings) noexcept;
};

// Preset loading stays free so the reflected body contains only value fields.
[[nodiscard]] bool FillParticleEffectComponent(ParticleEffectComponent& OutComponent,
                                              std::string_view InPresetId) noexcept;

// Scans entities with ParticleEffectComponent + Transform2D and keeps one
// emitter per entity. The system owns the pool, so a game creates one instance
// (on the heap) and passes it to its render extraction.
class ParticleSceneSystem
{
public:
    ParticleSceneSystem() noexcept;

    void Update(Fcs::Registry& InRegistry, float InDeltaSeconds) noexcept;
    [[nodiscard]] std::uint32_t BuildRenderItems(std::span<Rhi::RenderItem> Out,
                                                 const ParticleShapeAtlas& InAtlas) const noexcept;

    [[nodiscard]] ParticleEffectSystem& Particles() noexcept
    {
        return MParticles;
    }
    [[nodiscard]] const ParticleEffectSystem& Particles() const noexcept
    {
        return MParticles;
    }

private:
    struct Binding
    {
        Entity Entity = kNullEntity;
        std::uint32_t Emitter = kInvalidParticleEmitter;
        std::uint32_t RestartCounter = 0;
        bool Active = false;
        bool Seen = false;
    };

    [[nodiscard]] Binding* FindBinding(Entity InEntity) noexcept;
    [[nodiscard]] Binding* AcquireBinding() noexcept;
    void StopBinding(Binding& InBinding) noexcept;

    ParticleEffectSystem MParticles{};
    std::array<Binding, kParticleEffectMaxEmitters> MBindings{};
};
}

FBY_REFLECT_COMPONENT(
    Fby::ParticleEffectComponent, "ParticleEffectComponent", FBY_FIELD(Fby::ParticleEffectComponent, Flags, UInt32),
    FBY_FIELD(Fby::ParticleEffectComponent, MaxParticles, UInt32),
    FBY_FIELD(Fby::ParticleEffectComponent, Seed, UInt32), FBY_FIELD(Fby::ParticleEffectComponent, Shape, UInt32),
    FBY_FIELD(Fby::ParticleEffectComponent, EmissionShape, UInt32),
    FBY_FIELD(Fby::ParticleEffectComponent, EmissionExtents, Vector2D),
    FBY_FIELD(Fby::ParticleEffectComponent, Direction, Vector2D),
    FBY_FIELD(Fby::ParticleEffectComponent, Gravity, Vector2D), FBY_FIELD(Fby::ParticleEffectComponent, Wind, Vector2D),
    FBY_FIELD(Fby::ParticleEffectComponent, SpreadRadians, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, SpeedMin, Float), FBY_FIELD(Fby::ParticleEffectComponent, SpeedMax, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, LifetimeMin, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, LifetimeMax, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, StartSize, Float), FBY_FIELD(Fby::ParticleEffectComponent, EndSize, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, StartRotation, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, RotationSpeed, Float), FBY_FIELD(Fby::ParticleEffectComponent, Drag, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, Turbulence, Float), FBY_FIELD(Fby::ParticleEffectComponent, Swirl, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, EmissionRate, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, BurstCount, UInt32),
    FBY_FIELD(Fby::ParticleEffectComponent, BurstInterval, Float),
    FBY_FIELD(Fby::ParticleEffectComponent, Duration, Float), FBY_FIELD(Fby::ParticleEffectComponent, Loop, Bool),
    FBY_FIELD(Fby::ParticleEffectComponent, Additive, Bool),
    FBY_FIELD(Fby::ParticleEffectComponent, StartColor, Color32),
    FBY_FIELD(Fby::ParticleEffectComponent, EndColor, Color32),
    FBY_FIELD(Fby::ParticleEffectComponent, RestartCounter, UInt32))
