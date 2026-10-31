// FemboyEngine2D - GPU particle emitter data.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/StableIds.hpp"
#include "Engine/Core/Mirror.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby
{

enum ParticleEmitterFlags : std::uint32_t
{
    ParticleEmitterNone = 0,
    ParticleEmitterLooping = 1u << 0,
    // Particles do not follow the emitter transform
    ParticleEmitterWorldSpace = 1u << 1,
    ParticleEmitterAdditive = 1u << 2,
    // Reserved: requires a GPU collision grid
    ParticleEmitterCollideWorld = 1u << 3,
};

// Per-emitter authoring data. Everything is a primitive so the inspector, scene
struct ParticleEmitterComponent
{
    EmitterId EmitterID{};
    std::uint32_t MaxParticles = 256;
    std::uint32_t Seed = 0x9E3779B9u;
    std::uint32_t Flags = ParticleEmitterLooping | ParticleEmitterWorldSpace;
    std::uint32_t TextureLayer = 0;
    std::uint32_t MaterialPipeline = 0xFFFFFFFFu;

    Vector2D Offset{};
    Vector2D Direction{0.0f, -1.0f};
    Vector2D Gravity{0.0f, 160.0f};
    float SpreadRadians = 0.35f;
    float SpeedMin = 40.0f;
    float SpeedMax = 140.0f;
    float LifetimeMin = 0.4f;
    float LifetimeMax = 1.2f;
    float StartSize = 8.0f;
    float EndSize = 2.0f;
    float StartRotation = 0.0f;
    float RotationSpeed = 1.0f;
    Color32 StartColor = Color32::FromBytes(0xFF, 0x4F, 0xA8, 0xFF);
    Color32 EndColor = Color32::FromBytes(0xFF, 0x4F, 0xA8, 0x00);

    // 0 means "continuous"; a positive value spawns one burst and lets the
    float BurstInterval = 0.0f;
    float AnimationTime = 0.0f;
    // Producer-side estimate; the GPU is authoritative
    std::uint32_t ActiveCount = 0;
};

static_assert(std::is_trivially_copyable_v<ParticleEmitterComponent>);
static_assert(std::is_standard_layout_v<ParticleEmitterComponent>);

// std430-compatible mirror of the emitter block consumed by
struct ParticleEmitterGpu
{
    // Xy = emitter position, z = spawn radius, w = time
    float origin[4]{};
    // Xy = direction, z = spread, w = speedMin
    float direction[4]{};
    // Xy = gravity, z = speedMax, w = lifetimeMin
    float gravity[4]{};
    // X = lifetimeMax, y = maxParticles, z = flags, w = seed
    float lifetime[4]{};
    // Rgba
    float colorStart[4]{};
    // Rgba
    float colorEnd[4]{};
    // X = start, y = end, z = rotation, w = rotationSpeed
    float size[4]{};
    // Reserved: emission rate, atlas frame, turbulence, padding
    float misc[4]{};
};

static_assert(sizeof(ParticleEmitterGpu) == 16 * 8);

// The only push constant. A single uint32 keeps the dispatch independent of
struct ParticleSeedPush
{
    std::uint32_t seed = 0;
};

static_assert(sizeof(ParticleSeedPush) == 4);

// One GPU particle, std430. Position is world-space like every other render
struct ParticleStateGpu
{
    // Xy position, z age, w life
    float position[4]{};
    // Xy velocity, z rotation, w size
    float velocity[4]{};
    // Rgba (premultiplied by the shader on spawn)
    float color[4]{};
    // X = id, y = emitterIndex, z flags, w reserved
    float meta[4]{};
};

static_assert(sizeof(ParticleStateGpu) == 64);
}

FBY_REFLECT_COMPONENT(Fby::ParticleEmitterComponent, "ParticleEmitterComponent",
                      FBY_FIELD(Fby::ParticleEmitterComponent, EmitterID, UInt32),
                      FBY_FIELD(Fby::ParticleEmitterComponent, MaxParticles, UInt32),
                      FBY_FIELD(Fby::ParticleEmitterComponent, Seed, UInt32),
                      FBY_FIELD(Fby::ParticleEmitterComponent, Flags, UInt32),
                      FBY_FIELD(Fby::ParticleEmitterComponent, TextureLayer, UInt32),
                      FBY_FIELD(Fby::ParticleEmitterComponent, MaterialPipeline, UInt32),
                      FBY_FIELD(Fby::ParticleEmitterComponent, Offset, Vector2D),
                      FBY_FIELD(Fby::ParticleEmitterComponent, Direction, Vector2D),
                      FBY_FIELD(Fby::ParticleEmitterComponent, Gravity, Vector2D),
                      FBY_FIELD(Fby::ParticleEmitterComponent, SpreadRadians, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, SpeedMin, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, SpeedMax, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, LifetimeMin, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, LifetimeMax, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, StartSize, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, EndSize, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, StartRotation, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, RotationSpeed, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, StartColor, Color32),
                      FBY_FIELD(Fby::ParticleEmitterComponent, EndColor, Color32),
                      FBY_FIELD(Fby::ParticleEmitterComponent, BurstInterval, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, AnimationTime, Float),
                      FBY_FIELD(Fby::ParticleEmitterComponent, ActiveCount, UInt32))
