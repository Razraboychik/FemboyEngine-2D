// FemboyEngine2D - global weather integrator.
#include "Engine/Weather/Weather.hpp"

#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Physics/Cloth.hpp"

#include <algorithm>
#include <cmath>

namespace Fby::Weather
{
namespace
{

[[nodiscard]] Vector2D Rotate(Vector2D InValue, float InRadians) noexcept
{
    const float ScopedCos = std::cos(InRadians);
    const float ScopedSin = std::sin(InRadians);
    return Vector2D{InValue.X * ScopedCos - InValue.Y * ScopedSin,
                    InValue.X * ScopedSin + InValue.Y * ScopedCos};
}
}

void WeatherSystem::Initialize(const WeatherDesc& InDesc) noexcept
{
    MDesc = InDesc;
    MNoise.Reseed(InDesc.seed);
    MTimeSeconds = 0.0f;
    MWindField = WeatherWindField{};
}

void WeatherSystem::Update(Ecs::SystemContext& InContext) noexcept
{
    const float ScopedDt = std::clamp(InContext.dt, 0.0f, 0.25f);
    MTimeSeconds += ScopedDt;
    auto ScopedView = InContext.registry.view<WeatherContextComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        WeatherContextComponent& ScopedContext = ScopedView.get<WeatherContextComponent>(ScopedEntity);
        ScopedContext.timeSeconds += ScopedDt;
        const float ScopedTime = ScopedContext.timeSeconds;
        const float ScopedSlow = MNoise.Fbm2D(ScopedTime * std::max(ScopedContext.gustFrequency, 0.001f), 0.0f, 4);
        const float ScopedSide =
            MNoise.Fbm2D(11.7f, ScopedTime * std::max(ScopedContext.gustFrequency, 0.001f) * 1.7f, 4);
        const float ScopedGust = std::clamp(0.5f + ScopedSlow * 0.5f, 0.0f, 1.0f);
        const float ScopedTurbulence = std::clamp(ScopedContext.turbulence, 0.0f, 1.0f);
        Vector2D ScopedDirection{ScopedContext.windDirectionX, ScopedContext.windDirectionY};
        const float ScopedDirectionLength = std::sqrt(ScopedDirection.LengthSquared());
        if (ScopedDirectionLength < 1.0e-5f)
        {
            ScopedDirection = MDesc.baseDirection;
            const float ScopedBaseLength = std::sqrt(ScopedDirection.LengthSquared());
            ScopedDirection = ScopedBaseLength < 1.0e-5f ? Vector2D{1.0f, 0.0f} : ScopedDirection / ScopedBaseLength;
        }
        else
        {
            ScopedDirection = ScopedDirection / ScopedDirectionLength;
        }
        ScopedDirection = Rotate(ScopedDirection, ScopedSide * ScopedTurbulence * 0.85f);
        const float ScopedSpeed =
            std::max(ScopedContext.windSpeed * (1.0f + ScopedTurbulence * (ScopedGust * 2.0f - 1.0f)), 0.0f);
        ScopedContext.resolvedWindX = ScopedDirection.X * ScopedSpeed;
        ScopedContext.resolvedWindY = ScopedDirection.Y * ScopedSpeed;
        ScopedContext.gust = ScopedGust;
        ScopedContext.wetness = std::clamp(ScopedContext.wetness + ScopedDt * (ScopedContext.rainIntensity * 0.4f - 0.05f),
                                           0.0f, 1.0f);
        ScopedContext.flags &= ~(WeatherRain | WeatherSnow | WeatherFire);
        if (ScopedContext.cloudCoverage > 0.01f)
        {
            ScopedContext.flags |= WeatherClouds | WeatherCloudShadows;
        }
        if (ScopedContext.fogDensity > 0.001f)
        {
            ScopedContext.flags |= WeatherFog;
        }
        if (ScopedContext.rainIntensity > 0.01f)
        {
            ScopedContext.flags |= WeatherRain;
        }
        if (ScopedContext.snowIntensity > 0.01f)
        {
            ScopedContext.flags |= WeatherSnow;
        }
        if (ScopedContext.fireIntensity > 0.01f)
        {
            ScopedContext.flags |= WeatherFire;
        }
        MWindField.windAndGust =
            Vector4D{ScopedContext.resolvedWindX, ScopedContext.resolvedWindY, ScopedGust, ScopedTurbulence};
        MWindField.atmosphere = Vector4D{ScopedContext.atmosphereDensity, ScopedContext.airViscosity,
                                         ScopedContext.gravityScale, ScopedContext.temperatureCelsius};
        MWindField.cloud = Vector4D{ScopedContext.cloudCoverage, ScopedContext.cloudSpeed, ScopedContext.cloudHeight,
                                    ScopedContext.cloudShadowStrength};
        MWindField.precipitation = Vector4D{ScopedContext.rainIntensity, ScopedContext.snowIntensity,
                                            ScopedContext.fireIntensity, ScopedContext.wetness};
        MWindField.fog = Vector4D{ScopedContext.fogDensity, ScopedContext.fogFalloff, ScopedTime,
                                  static_cast<float>(ScopedContext.flags)};
    }
    if (InContext.cloth != nullptr)
    {
        InContext.cloth->SetAmbientWind(Vector2D{MWindField.windAndGust.X, MWindField.windAndGust.Y});
    }
}

Vector2D WeatherSystem::WindAt(Vector2D InWorld, float InTimeSeconds) const noexcept
{
    const float ScopedScale = 0.0025f;
    const float ScopedForward = MNoise.Fbm2D(InWorld.X * ScopedScale + InTimeSeconds * 0.07f,
                                             InWorld.Y * ScopedScale, 4);
    const float ScopedAcross = MNoise.Fbm2D(InWorld.Y * ScopedScale + 17.3f,
                                            InWorld.X * ScopedScale - InTimeSeconds * 0.055f, 4);
    const float ScopedSpeed = std::max(MDesc.baseSpeed, 0.0f) * (1.0f + ScopedForward * 0.6f);
    return Vector2D{ScopedSpeed, ScopedAcross * ScopedSpeed * 0.45f};
}

bool WeatherSystem::Register(Ecs::SystemGraph& InGraph, WeatherSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::PreUpdate, "WeatherSystem", &WeatherSystem::Tick, &InSystem);
}

void WeatherSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    static_cast<WeatherSystem*>(InUser)->Update(InContext);
}
}
