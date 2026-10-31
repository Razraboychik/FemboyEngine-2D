// FemboyEngine2D - authoritative weather and global wind.
#pragma once

#include "Engine/Core/NoiseGenerator.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby
{
class ClothSystem;
}

namespace Fby::Weather
{

enum WeatherFlags : std::uint32_t
{
    WeatherNone = 0,
    WeatherClouds = 1u << 0,
    WeatherCloudShadows = 1u << 1,
    WeatherFog = 1u << 2,
    WeatherRain = 1u << 3,
    WeatherSnow = 1u << 4,
    WeatherFire = 1u << 5,
    WeatherLightning = 1u << 6,
    WeatherWind = 1u << 7,
};

struct WeatherContextComponent
{
    std::uint64_t weatherId = 0;
    std::uint32_t flags = WeatherClouds | WeatherCloudShadows | WeatherFog | WeatherWind;
    std::uint32_t seed = 0x51ED270Bu;

    // Atmosphere constants. A non-Earth planet raises density or lowers gravity
    float atmosphereDensity = 1.0f;
    float airViscosity = 1.8e-5f;
    float gravityScale = 1.0f;
    float temperatureCelsius = 15.0f;

    // Global wind. Direction is normalized on update; speed is pixels/second.
    float windSpeed = 12.0f;
    float windDirectionX = 1.0f;
    float windDirectionY = 0.0f;
    float turbulence = 0.35f;
    float gustFrequency = 0.15f;

    float cloudCoverage = 0.55f;
    float cloudSpeed = 1.0f;
    float cloudHeight = 420.0f;
    float cloudShadowStrength = 0.45f;

    float fogDensity = 0.018f;
    float fogFalloff = 0.0022f;

    float rainIntensity = 0.0f;
    float snowIntensity = 0.0f;
    float fireIntensity = 0.0f;
    float wetness = 0.0f;

    // Integrator output. Gameplay reads this instead of recomputing noise.
    float timeSeconds = 0.0f;
    float resolvedWindX = 0.0f;
    float resolvedWindY = 0.0f;
    float gust = 0.0f;
};

static_assert(std::is_trivially_copyable_v<WeatherContextComponent>);
static_assert(std::is_standard_layout_v<WeatherContextComponent>);

// One std140-friendly block for cloth, plant and weather shaders.
struct WeatherWindField
{
    // xy resolved wind, z gust, w turbulence
    Vector4D windAndGust{};
    // x density, y viscosity, z gravity, w temperature
    Vector4D atmosphere{};
    // x coverage, y speed, z height, w shadow
    Vector4D cloud{};
    // x rain, y snow, z fire, w wetness
    Vector4D precipitation{};
    // x density, y falloff, z time, w flags as bits
    Vector4D fog{};
};

static_assert(std::is_trivially_copyable_v<WeatherWindField>);

struct WeatherDesc
{
    std::uint64_t seed = 0x51ED270Bu;
    Vector2D baseDirection{1.0f, 0.0f};
    float baseSpeed = 12.0f;
    float gravityScale = 1.0f;
};

class WeatherSystem
{
public:
    WeatherSystem() = default;

    void Initialize(const WeatherDesc& InDesc = {}) noexcept;
    // PreUpdate. Advances every WeatherContextComponent, derives the wind and
    void Update(Ecs::SystemContext& InContext) noexcept;
    // Same noise field, sampled at a world point. Used by a local foilage query
    [[nodiscard]] Vector2D WindAt(Vector2D InWorld, float InTimeSeconds) const noexcept;
    [[nodiscard]] const WeatherWindField& WindField() const noexcept
    {
        return MWindField;
    }
    [[nodiscard]] const Noise::NoiseGenerator& Noise() const noexcept
    {
        return MNoise;
    }
    [[nodiscard]] float TimeSeconds() const noexcept
    {
        return MTimeSeconds;
    }

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, WeatherSystem& InSystem);

private:
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    Noise::NoiseGenerator MNoise{};
    WeatherWindField MWindField{};
    float MTimeSeconds = 0.0f;
    WeatherDesc MDesc{};
};
}
