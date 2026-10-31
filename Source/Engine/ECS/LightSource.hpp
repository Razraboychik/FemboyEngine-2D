// FemboyEngine2D - unified 2D light sources: global, omnidirectional and
// cone-limited (spot or arc) lights with static/dynamic classification.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Lighting.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace Fby
{

// One structure covers every light shape: a global light ignores position and
// radius, an omnidirectional light uses the full circle, and a spot or arc
// light is the same omni light restricted by ConeAngle. A narrow cone is a
// spotlight; a wide soft cone is an arc lamp.
enum class LightSourceType : std::uint8_t
{
    Global = 0,
    Omnidirectional = 1,
    SpotArc = 2,
};

inline constexpr std::uint32_t kLightSourceTypeCount = 3;
inline constexpr float kFullTurnRadians = 6.28318530717958647692f;

enum LightSourceFlags : std::uint32_t
{
    LightSourceEnabled = 1u << 0,
    // Authored as static: the lighting solve caches its contribution until the
    // scene, the occluders or the light itself change.
    LightSourceStatic = 1u << 1,
    // The static light may wake on contact or on a program trigger. Waking
    // clears this bit and the static bit, so the light becomes dynamic.
    LightSourceSleeping = 1u << 2,
    LightSourceCastNoShadow = 1u << 3,
    LightSourceWakeOnContact = 1u << 4,
};

// Authoring data for a light. All fields are floats or integers so the Mirror
// reflection, TOML scenes and the inspector need no enum codec. Type stores the
// LightSourceType value.
struct LightSourceComponent
{
    Color32 Color = Color32::White();
    // World units where the light reaches zero; ignored by Global.
    float Radius = 256.0f;
    float Intensity = 1.0f;
    Vector2D Offset{};
    // Full aperture in radians. kFullTurnRadians is an omnidirectional light,
    // smaller values narrow the light into a spot or an arc.
    float ConeAngle = kFullTurnRadians;
    // Width of the angular falloff at the cone edge, in radians.
    float ConeSoftness = 0.35f;
    // Distance falloff exponent: 2 is inverse-square-like, 1 is linear.
    float Falloff = 2.0f;
    // LightSourceType value.
    std::uint32_t Type = static_cast<std::uint32_t>(LightSourceType::Omnidirectional);
    std::uint32_t Flags = LightSourceEnabled | LightSourceStatic | LightSourceSleeping;

    [[nodiscard]] constexpr LightSourceType GetType() const noexcept
    {
        return static_cast<LightSourceType>(Type < kLightSourceTypeCount ? Type : 0u);
    }
    constexpr void SetType(LightSourceType InType) noexcept
    {
        Type = static_cast<std::uint32_t>(InType);
    }
    [[nodiscard]] constexpr bool IsEnabled() const noexcept
    {
        return (Flags & LightSourceEnabled) != 0;
    }
    [[nodiscard]] constexpr bool IsStatic() const noexcept
    {
        return (Flags & LightSourceStatic) != 0;
    }
    [[nodiscard]] constexpr bool IsSleepingStatic() const noexcept
    {
        return (Flags & LightSourceStatic) != 0 && (Flags & LightSourceSleeping) != 0;
    }
    // Normalized aperture clamped to a full turn; the solver reads this so an
    // authored cone larger than a circle behaves as a circle.
    [[nodiscard]] constexpr float Aperture() const noexcept
    {
        return ConeAngle <= 0.0f ? 0.0f : (ConeAngle >= kFullTurnRadians ? kFullTurnRadians : ConeAngle);
    }
};

static_assert(std::is_trivially_copyable_v<LightSourceComponent>);
static_assert(std::is_standard_layout_v<LightSourceComponent>);
static_assert(sizeof(LightSourceComponent) == 40);

// Angular attenuation for a cone light. InAngle is the absolute angle between
// the light direction and the direction to the receiver. A cone whose aperture
// reaches a full turn returns one everywhere, so the same function serves omni
// and spot lights.
[[nodiscard]] inline float LightConeAttenuation(float InAngle, float InAperture, float InSoftness) noexcept
{
    if (InAperture >= kFullTurnRadians)
    {
        return 1.0f;
    }
    if (InAperture <= 0.0f)
    {
        return 0.0f;
    }
    const float ScopedHalf = InAperture * 0.5f;
    const float ScopedSoft = InSoftness < 0.0f ? 0.0f : (InSoftness > ScopedHalf ? ScopedHalf : InSoftness);
    const float ScopedAbsolute = std::fabs(InAngle);
    if (ScopedAbsolute >= ScopedHalf)
    {
        return 0.0f;
    }
    if (ScopedAbsolute <= ScopedHalf - ScopedSoft)
    {
        return 1.0f;
    }
    // Smoothstep over the soft band: no visible ring at the cone edge.
    const float ScopedT = (ScopedHalf - ScopedAbsolute) / (ScopedSoft > 1.0e-6f ? ScopedSoft : 1.0e-6f);
    return ScopedT * ScopedT * (3.0f - 2.0f * ScopedT);
}

// Radial attenuation, normalized so the authored radius is the zero point.
[[nodiscard]] inline float LightDistanceAttenuation(float InDistance, float InRadius, float InFalloff) noexcept
{
    if (InRadius <= 0.0f || InDistance >= InRadius)
    {
        return 0.0f;
    }
    const float ScopedDistance = InDistance < 0.0f ? 0.0f : InDistance;
    const float ScopedBase = 1.0f - ScopedDistance / InRadius;
    const float ScopedExponent = InFalloff > 0.0f ? InFalloff : 1.0f;
    return std::pow(ScopedBase, ScopedExponent);
}

// Wakes a sleeping light: the static and sleeping bits are cleared so the next
// lighting solve treats the source as dynamic. Returns false when the light was
// already dynamic or when the wake-on-trigger permission is missing.
[[nodiscard]] bool WakeLightSource(LightSourceComponent& InLight, bool InFromContact) noexcept;

// Solver settings. Ambient is the authored fill; global lights are added on top
// of it. StaticCache keeps the accumulated static-light contribution until the
// static lights, the targets, the occluders or the SDF field change.
struct LightSourceSettings
{
    Color32 Ambient = Color32::FromBytes(40, 44, 60);
    float AmbientIntensity = 0.25f;
    std::uint32_t MaxLights = 128;
    bool Tonemap = true;
    bool SdfShadows = true;
    std::uint32_t SdfMaxSteps = 16;
    float SdfSoftness = 24.0f;
    float SdfAmbientOcclusion = 0.35f;
    float SdfOcclusionRadius = 32.0f;
    bool CacheStaticLights = true;
    std::uint32_t Reserved = 0;
};

// Unified 2D lighting solve for LightSourceComponent. It writes the same
// Light2DResultComponent as LightingSystem, so the render extract composes the
// result without knowing which solver produced it. A scene uses one solver: the
// legacy LightingSystem for Light2DComponent, this system for LightSource.
class LightSourceSystem
{
public:
    [[nodiscard]] std::uint32_t Run(Ecs::Registry& InRegistry, const LightSourceSettings& InSettings,
                                    const SdfField* InField = nullptr) noexcept;

    void InvalidateStaticCache() noexcept
    {
        MStaticValid = false;
    }

    [[nodiscard]] std::uint32_t LightCount() const noexcept
    {
        return MLightCount;
    }
    [[nodiscard]] std::uint32_t LitSpriteCount() const noexcept
    {
        return MLitCount;
    }
    [[nodiscard]] std::uint32_t StaticLightCount() const noexcept
    {
        return MStaticLights;
    }
    [[nodiscard]] std::uint32_t DynamicLightCount() const noexcept
    {
        return MDynamicLights;
    }
    [[nodiscard]] std::uint32_t StaticRebuildCount() const noexcept
    {
        return MStaticRebuilds;
    }
    [[nodiscard]] std::uint32_t StaticCacheHitCount() const noexcept
    {
        return MStaticHits;
    }
    [[nodiscard]] std::uint32_t ShadowSampleCount() const noexcept
    {
        return MShadowSamples;
    }

    void SetSdfField(const SdfField* InField) noexcept
    {
        MSdfField = InField;
    }
    [[nodiscard]] const SdfField* SdfFieldView() const noexcept
    {
        return MSdfField;
    }

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, LightSourceSystem& InSystem);

    LightSourceSettings Settings{};

private:
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    // One target's pre-tonemap static contribution, cached across frames.
    struct CachedTarget
    {
        Entity Owner = entt::null;
        float Red = 0.0f;
        float Green = 0.0f;
        float Blue = 0.0f;
    };

    std::array<CachedTarget, 1024> MStaticTargets{};
    std::uint32_t MStaticTargetCount = 0;
    std::uint64_t MStaticSignature = 0;
    bool MStaticValid = false;
    std::uint32_t MLightCount = 0;
    std::uint32_t MStaticLights = 0;
    std::uint32_t MDynamicLights = 0;
    std::uint32_t MLitCount = 0;
    std::uint32_t MShadowSamples = 0;
    std::uint32_t MStaticRebuilds = 0;
    std::uint32_t MStaticHits = 0;
    const SdfField* MSdfField = nullptr;
};
}

FBY_REFLECT_COMPONENT(Fby::LightSourceComponent, "LightSourceComponent",
                      FBY_FIELD(Fby::LightSourceComponent, Color, Color32),
                      FBY_FIELD(Fby::LightSourceComponent, Radius, Float),
                      FBY_FIELD(Fby::LightSourceComponent, Intensity, Float),
                      FBY_FIELD(Fby::LightSourceComponent, Offset, Vector2D),
                      FBY_FIELD(Fby::LightSourceComponent, ConeAngle, Float),
                      FBY_FIELD(Fby::LightSourceComponent, ConeSoftness, Float),
                      FBY_FIELD(Fby::LightSourceComponent, Falloff, Float),
                      FBY_FIELD(Fby::LightSourceComponent, Type, UInt32),
                      FBY_FIELD(Fby::LightSourceComponent, Flags, UInt32))
