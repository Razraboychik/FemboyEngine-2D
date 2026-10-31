// FemboyEngine2D - GPU weather: rain, snow, splashes, clouds, fog, fire.
#pragma once

#include "Engine/Physics/SdfCollision.hpp"
#include "Engine/RHI/RenderGraph.hpp"
#include "Engine/Weather/Weather.hpp"

#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Weather
{

// The weather passes are written against the RHI vocabulary (RenderGraph,
using namespace Fby::Rhi;

struct WeatherGpuResources
{
    // Backend-owned buffer handles. The engine keeps them opaque so this header
    std::uint64_t rainBuffer = 0;
    std::uint64_t splashBuffer = 0;
    std::uint64_t cloudTexture = 0;
    std::uint64_t cloudShadowTexture = 0;
    std::uint64_t fogTexture = 0;
    std::uint64_t fireTexture = 0;
    std::uint64_t sdfTexture = 0;
    std::uint32_t sdfWidth = 0;
    std::uint32_t sdfHeight = 0;
};

struct WeatherPreconciliatedDesc
{
    std::uint32_t particleCapacity = 262144;
    std::uint32_t splashCapacity = 65536;
    std::uint32_t seed = 0x51ED270Bu;
    // World-space rain volume, pixels
    float areaWidth = 4096.0f;
    float areaHeight = 2304.0f;
    // Pixels / second
    float rainFallSpeed = 980.0f;
    float snowFallSpeed = 140.0f;
    float splashLifetime = 0.55f;
};

struct WeatherGpuConstants
{
    // xy wind, z time, w dt
    Vector4D windAndTime{};
    // x density, y viscosity, z gravity, w temperature
    Vector4D atmosphere{};
    // x coverage, y speed, z height, w shadow strength
    Vector4D clouds{};
    // x rain, y snow, z fire, w wetness
    Vector4D precipitation{};
    // x density, y falloff, z time, w flags bits
    Vector4D fog{};
    // x origin x, y origin y, z cell x, w cell y
    Vector4D fieldTransform{};
    // x width, y height, z particle count, w splash count
    Vector4D fieldExtent{};
    // x area width, y area height, z rain speed, w snow speed
    Vector4D volume{};
};

static_assert(std::is_trivially_copyable_v<WeatherGpuConstants>);

struct WeatherGpuStats
{
    std::uint32_t particleCount = 0;
    std::uint32_t dispatchGroups = 0;
    std::uint32_t splashCapacity = 0;
    // Proof of the CPU contract: this counter is never incremented by rain
    std::uint32_t cpuParticleReads = 0;
    std::uint32_t liveSplashesEstimate = 0;
};

class WeatherPreconciliatedSystem
{
public:
    WeatherPreconciliatedSystem() = default;

    void Initialize(const WeatherPreconciliatedDesc& InDesc = {}) noexcept;
    void SetSdfField(const SdfField& InField) noexcept;
    void SetResources(const WeatherGpuResources& InResources) noexcept;
    // PreUpdate (after WeatherSystem). Computes constants and dispatch sizes.
    [[nodiscard]] bool Update(Ecs::SystemContext& InContext) noexcept;

    [[nodiscard]] bool BuildRenderGraph(RenderGraph& InGraph);
    [[nodiscard]] const WeatherGpuConstants& Constants() const noexcept
    {
        return MConstants;
    }
    [[nodiscard]] const WeatherGpuStats& Stats() const noexcept
    {
        return MStats;
    }
    [[nodiscard]] const WeatherGpuResources& Resources() const noexcept
    {
        return MResources;
    }
    [[nodiscard]] std::uint32_t SnowParticleCount() const noexcept
    {
        return MSnowParticleCount;
    }

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, WeatherPreconciliatedSystem& InSystem);

private:
    static void ExecuteRain(RenderGraphContext& InContext, void* InUser);
    static void ExecuteSplashSpawn(RenderGraphContext& InContext, void* InUser);
    static void ExecuteRainLines(RenderGraphContext& InContext, void* InUser);
    static void ExecuteSplashSprites(RenderGraphContext& InContext, void* InUser);
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    WeatherPreconciliatedDesc MDesc{};
    WeatherGpuResources MResources{};
    WeatherGpuConstants MConstants{};
    WeatherGpuStats MStats{};
    SdfField MField{};
    std::uint32_t MSnowParticleCount = 0;
    std::uint64_t MRainResource = 0;
    std::uint64_t MSplashResource = 0;
    std::uint64_t MSdfResource = 0;
};

// Clouds, moving cloud shadows, volumetric fog and fire/smoke. All four are
class WeatherAtmospherePass
{
public:
    WeatherAtmospherePass() = default;

    [[nodiscard]] bool Build(RenderGraph& InGraph, WeatherSystem& InWeather);
    [[nodiscard]] const WeatherGpuConstants& Constants() const noexcept
    {
        return MConstants;
    }

private:
    static void ExecuteClouds(RenderGraphContext& InContext, void* InUser);
    static void ExecuteCloudShadow(RenderGraphContext& InContext, void* InUser);
    static void ExecuteFog(RenderGraphContext& InContext, void* InUser);
    static void ExecuteFire(RenderGraphContext& InContext, void* InUser);
    static void ExecuteComposite(RenderGraphContext& InContext, void* InUser);
    void Refresh(const RenderGraphContext& InContext) noexcept;

    WeatherSystem* MWeather = nullptr;
    WeatherGpuConstants MConstants{};
    std::uint64_t MCloudResource = 0;
    std::uint64_t MCloudShadowResource = 0;
    std::uint64_t MFogResource = 0;
    std::uint64_t MFireResource = 0;
    std::uint64_t MColorResource = 0;
};
}
