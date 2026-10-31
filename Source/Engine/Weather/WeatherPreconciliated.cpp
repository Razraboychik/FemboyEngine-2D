// FemboyEngine2D - GPU weather pass implementation.
#include "Engine/Weather/WeatherPreconciliated.hpp"

#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <cmath>

namespace Fby::Weather
{
namespace
{

[[nodiscard]] std::uint32_t GroupCount(std::uint32_t InThreads, std::uint32_t InGroupSize) noexcept
{
    return (InThreads + InGroupSize - 1u) / InGroupSize;
}
}

void WeatherPreconciliatedSystem::Initialize(const WeatherPreconciliatedDesc& InDesc) noexcept
{
    MDesc = InDesc;
    MDesc.particleCapacity = std::min(InDesc.particleCapacity, 1u << 22);
    MDesc.splashCapacity = std::min(InDesc.splashCapacity, 1u << 20);
    MStats = WeatherGpuStats{};
    MStats.splashCapacity = MDesc.splashCapacity;
    MConstants = WeatherGpuConstants{};
    MSnowParticleCount = 0;
}

void WeatherPreconciliatedSystem::SetSdfField(const SdfField& InField) noexcept
{
    MField = InField;
}

void WeatherPreconciliatedSystem::SetResources(const WeatherGpuResources& InResources) noexcept
{
    MResources = InResources;
}

bool WeatherPreconciliatedSystem::Update(Ecs::SystemContext& InContext) noexcept
{
    auto ScopedView = InContext.registry.view<WeatherContextComponent>();
    const WeatherContextComponent* ScopedWeather = nullptr;
    for (const Entity ScopedEntity : ScopedView)
    {
        ScopedWeather = &ScopedView.get<WeatherContextComponent>(ScopedEntity);
        break;
    }
    if (ScopedWeather == nullptr)
    {
        MStats.particleCount = 0;
        return false;
    }
    const float ScopedRain = std::clamp(ScopedWeather->rainIntensity, 0.0f, 1.0f);
    const float ScopedSnow = std::clamp(ScopedWeather->snowIntensity, 0.0f, 1.0f);
    const float ScopedIntensity = std::max(ScopedRain, ScopedSnow);
    const std::uint32_t ScopedParticles =
        static_cast<std::uint32_t>(static_cast<float>(MDesc.particleCapacity) * ScopedIntensity);
    MSnowParticleCount = static_cast<std::uint32_t>(static_cast<float>(MDesc.particleCapacity) * ScopedSnow);
    MStats.particleCount = ScopedParticles;
    MStats.dispatchGroups = GroupCount(ScopedParticles, 64);
    // The CPU contract: never read a particle. The shader owns the buffer.
    MStats.cpuParticleReads = 0;
    MStats.liveSplashesEstimate = std::min(ScopedParticles / 4u, MDesc.splashCapacity);
    if (ScopedParticles == 0)
    {
        return false;
    }
    const float ScopedTime = ScopedWeather->timeSeconds;
    const float ScopedDt = std::max(InContext.dt, 0.0f);
    MConstants.windAndTime =
        Vector4D{ScopedWeather->resolvedWindX, ScopedWeather->resolvedWindY, ScopedTime, ScopedDt};
    MConstants.atmosphere =
        Vector4D{ScopedWeather->atmosphereDensity, ScopedWeather->airViscosity, ScopedWeather->gravityScale,
                 ScopedWeather->temperatureCelsius};
    MConstants.clouds = Vector4D{ScopedWeather->cloudCoverage, ScopedWeather->cloudSpeed, ScopedWeather->cloudHeight,
                                 ScopedWeather->cloudShadowStrength};
    MConstants.precipitation =
        Vector4D{ScopedRain, ScopedSnow, ScopedWeather->fireIntensity, ScopedWeather->wetness};
    MConstants.fog = Vector4D{ScopedWeather->fogDensity, ScopedWeather->fogFalloff, ScopedTime,
                              static_cast<float>(ScopedWeather->flags)};
    MConstants.fieldTransform = MField.Valid()
                                    ? Vector4D{MField.origin.X, MField.origin.Y, MField.cellSize.X, MField.cellSize.Y}
                                    : Vector4D{0.0f, 0.0f, 1.0f, 1.0f};
    MConstants.fieldExtent =
        Vector4D{static_cast<float>(MField.width), static_cast<float>(MField.height),
                 static_cast<float>(ScopedParticles), static_cast<float>(MDesc.splashCapacity)};
    MConstants.volume = Vector4D{MDesc.areaWidth, MDesc.areaHeight, MDesc.rainFallSpeed, MDesc.snowFallSpeed};
    return true;
}

bool WeatherPreconciliatedSystem::BuildRenderGraph(RenderGraph& InGraph)
{
    MRainResource = RenderResourceHash("weather.rain.particles");
    MSplashResource = RenderResourceHash("weather.rain.splash");
    MSdfResource = RenderResourceHash("sdf.terrain");

    RenderPassDesc ScopedRain{};
    ScopedRain.name = "WeatherRainSimulation";
    ScopedRain.kind = RenderPassKind::Compute;
    ScopedRain.execute = &WeatherPreconciliatedSystem::ExecuteRain;
    ScopedRain.user = this;
    ScopedRain.reads[0] = MSdfResource;
    ScopedRain.readCount = 1;
    ScopedRain.writes[0] = MRainResource;
    ScopedRain.writeCount = 1;
    if (!InGraph.AddPass(ScopedRain))
    {
        return false;
    }

    RenderPassDesc ScopedSplashSpawn{};
    ScopedSplashSpawn.name = "WeatherRainSplashSpawn";
    ScopedSplashSpawn.kind = RenderPassKind::Compute;
    ScopedSplashSpawn.execute = &WeatherPreconciliatedSystem::ExecuteSplashSpawn;
    ScopedSplashSpawn.user = this;
    ScopedSplashSpawn.reads[0] = MRainResource;
    ScopedSplashSpawn.reads[1] = MSdfResource;
    ScopedSplashSpawn.readCount = 2;
    ScopedSplashSpawn.writes[0] = MSplashResource;
    ScopedSplashSpawn.writeCount = 1;
    if (!InGraph.AddPass(ScopedSplashSpawn))
    {
        return false;
    }

    RenderPassDesc ScopedLines{};
    ScopedLines.name = "WeatherRainLines";
    ScopedLines.kind = RenderPassKind::Graphics;
    ScopedLines.execute = &WeatherPreconciliatedSystem::ExecuteRainLines;
    ScopedLines.user = this;
    ScopedLines.reads[0] = MRainResource;
    ScopedLines.readCount = 1;
    if (!InGraph.AddPass(ScopedLines))
    {
        return false;
    }

    RenderPassDesc ScopedSplashSprites{};
    ScopedSplashSprites.name = "WeatherRainSplashSprites";
    ScopedSplashSprites.kind = RenderPassKind::Graphics;
    ScopedSplashSprites.execute = &WeatherPreconciliatedSystem::ExecuteSplashSprites;
    ScopedSplashSprites.user = this;
    ScopedSplashSprites.reads[0] = MSplashResource;
    ScopedSplashSprites.readCount = 1;
    return InGraph.AddPass(ScopedSplashSprites);
}

void WeatherPreconciliatedSystem::ExecuteRain(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<WeatherPreconciliatedSystem*>(InUser);
    const RenderCommandRecord ScopedRecord =
        MakeComputeRecord("weather_rain.comp", ScopedSelf->MConstants,
                          std::max(ScopedSelf->MStats.dispatchGroups, 1u));
    (void)InContext.Push(ScopedRecord);
}

void WeatherPreconciliatedSystem::ExecuteSplashSpawn(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<WeatherPreconciliatedSystem*>(InUser);
    const RenderCommandRecord ScopedRecord =
        MakeComputeRecord("weather_rain_splash.comp", ScopedSelf->MConstants,
                          std::max(ScopedSelf->MStats.dispatchGroups, 1u));
    (void)InContext.Push(ScopedRecord);
}

void WeatherPreconciliatedSystem::ExecuteRainLines(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<WeatherPreconciliatedSystem*>(InUser);
    const RenderCommandRecord ScopedRecord =
        MakeGraphicsRecord("weather_rain.vert", ScopedSelf->MConstants, InContext.extent.width, InContext.extent.height);
    (void)InContext.Push(ScopedRecord);
}

void WeatherPreconciliatedSystem::ExecuteSplashSprites(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<WeatherPreconciliatedSystem*>(InUser);
    const RenderCommandRecord ScopedRecord =
        MakeGraphicsRecord("weather_splash.frag", ScopedSelf->MConstants, InContext.extent.width,
                           InContext.extent.height);
    (void)InContext.Push(ScopedRecord);
}

bool WeatherPreconciliatedSystem::Register(Ecs::SystemGraph& InGraph, WeatherPreconciliatedSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::PreUpdate, "WeatherPreconciliatedSystem", &WeatherPreconciliatedSystem::Tick,
                       &InSystem);
}

void WeatherPreconciliatedSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    (void)static_cast<WeatherPreconciliatedSystem*>(InUser)->Update(InContext);
}

bool WeatherAtmospherePass::Build(RenderGraph& InGraph, WeatherSystem& InWeather)
{
    MWeather = &InWeather;
    MCloudResource = RenderResourceHash("weather.clouds");
    MCloudShadowResource = RenderResourceHash("weather.cloudShadow");
    MFogResource = RenderResourceHash("weather.fog");
    MFireResource = RenderResourceHash("weather.fire");
    MColorResource = RenderResourceHash("scene.color");

    RenderPassDesc ScopedClouds{};
    ScopedClouds.name = "WeatherClouds";
    ScopedClouds.kind = RenderPassKind::Compute;
    ScopedClouds.execute = &WeatherAtmospherePass::ExecuteClouds;
    ScopedClouds.user = this;
    ScopedClouds.writes[0] = MCloudResource;
    ScopedClouds.writeCount = 1;
    if (!InGraph.AddPass(ScopedClouds))
    {
        return false;
    }

    RenderPassDesc ScopedShadow{};
    ScopedShadow.name = "WeatherCloudShadows";
    ScopedShadow.kind = RenderPassKind::Compute;
    ScopedShadow.execute = &WeatherAtmospherePass::ExecuteCloudShadow;
    ScopedShadow.user = this;
    ScopedShadow.reads[0] = MCloudResource;
    ScopedShadow.readCount = 1;
    ScopedShadow.writes[0] = MCloudShadowResource;
    ScopedShadow.writeCount = 1;
    if (!InGraph.AddPass(ScopedShadow))
    {
        return false;
    }

    RenderPassDesc ScopedFog{};
    ScopedFog.name = "WeatherVolumetricFog";
    ScopedFog.kind = RenderPassKind::Compute;
    ScopedFog.execute = &WeatherAtmospherePass::ExecuteFog;
    ScopedFog.user = this;
    ScopedFog.writes[0] = MFogResource;
    ScopedFog.writeCount = 1;
    if (!InGraph.AddPass(ScopedFog))
    {
        return false;
    }

    RenderPassDesc ScopedFire{};
    ScopedFire.name = "WeatherFireSmoke";
    ScopedFire.kind = RenderPassKind::Compute;
    ScopedFire.execute = &WeatherAtmospherePass::ExecuteFire;
    ScopedFire.user = this;
    ScopedFire.writes[0] = MFireResource;
    ScopedFire.writeCount = 1;
    if (!InGraph.AddPass(ScopedFire))
    {
        return false;
    }

    RenderPassDesc ScopedComposite{};
    ScopedComposite.name = "WeatherComposite";
    ScopedComposite.kind = RenderPassKind::Graphics;
    ScopedComposite.execute = &WeatherAtmospherePass::ExecuteComposite;
    ScopedComposite.user = this;
    ScopedComposite.reads[0] = MCloudResource;
    ScopedComposite.reads[1] = MCloudShadowResource;
    ScopedComposite.reads[2] = MFogResource;
    ScopedComposite.reads[3] = MFireResource;
    ScopedComposite.readCount = 4;
    ScopedComposite.writes[0] = MColorResource;
    ScopedComposite.writeCount = 1;
    return InGraph.AddPass(ScopedComposite);
}

void WeatherAtmospherePass::Refresh(const RenderGraphContext& InContext) noexcept
{
    static const WeatherWindField ScopedEmpty{};
    const WeatherWindField& ScopedField = MWeather != nullptr ? MWeather->WindField() : ScopedEmpty;
    MConstants.windAndTime =
        Vector4D{ScopedField.windAndGust.X, ScopedField.windAndGust.Y, InContext.timeSeconds, 1.0f / 60.0f};
    MConstants.atmosphere = ScopedField.atmosphere;
    MConstants.clouds = ScopedField.cloud;
    MConstants.precipitation = ScopedField.precipitation;
    MConstants.fog = ScopedField.fog;
    MConstants.fieldExtent = Vector4D{static_cast<float>(InContext.extent.width),
                                      static_cast<float>(InContext.extent.height), 0.0f, 0.0f};
}

void WeatherAtmospherePass::ExecuteClouds(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<WeatherAtmospherePass*>(InUser);
    ScopedSelf->Refresh(InContext);
    if (ScopedSelf->MConstants.clouds.X <= 0.01f)
    {
        return;
    }
    const RenderCommandRecord ScopedRecord =
        MakeComputeRecord("weather_clouds.comp", ScopedSelf->MConstants, (InContext.extent.width + 7u) / 8u,
                          (InContext.extent.height + 7u) / 8u);
    (void)InContext.Push(ScopedRecord);
}

void WeatherAtmospherePass::ExecuteCloudShadow(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<WeatherAtmospherePass*>(InUser);
    ScopedSelf->Refresh(InContext);
    const RenderCommandRecord ScopedRecord =
        MakeComputeRecord("weather_cloud_shadow.comp", ScopedSelf->MConstants, (InContext.extent.width + 7u) / 8u,
                          (InContext.extent.height + 7u) / 8u);
    (void)InContext.Push(ScopedRecord);
}

void WeatherAtmospherePass::ExecuteFog(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<WeatherAtmospherePass*>(InUser);
    ScopedSelf->Refresh(InContext);
    const RenderCommandRecord ScopedRecord =
        MakeComputeRecord("weather_fog.comp", ScopedSelf->MConstants, (InContext.extent.width + 7u) / 8u,
                          (InContext.extent.height + 7u) / 8u);
    (void)InContext.Push(ScopedRecord);
}

void WeatherAtmospherePass::ExecuteFire(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<WeatherAtmospherePass*>(InUser);
    ScopedSelf->Refresh(InContext);
    if (ScopedSelf->MConstants.precipitation.Z <= 0.01f)
    {
        return;
    }
    const RenderCommandRecord ScopedRecord =
        MakeComputeRecord("weather_fire.comp", ScopedSelf->MConstants, (InContext.extent.width + 7u) / 8u,
                          (InContext.extent.height + 7u) / 8u);
    (void)InContext.Push(ScopedRecord);
}

void WeatherAtmospherePass::ExecuteComposite(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<WeatherAtmospherePass*>(InUser);
    ScopedSelf->Refresh(InContext);
    const RenderCommandRecord ScopedRecord =
        MakeGraphicsRecord("weather_composite.frag", ScopedSelf->MConstants, InContext.extent.width,
                           InContext.extent.height);
    (void)InContext.Push(ScopedRecord);
}
}
