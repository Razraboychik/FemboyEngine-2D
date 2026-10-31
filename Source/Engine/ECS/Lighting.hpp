// FemboyEngine2D - 2D lighting: ambient + point lights resolved per sprite.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/SdfCollision.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby
{

namespace Rhi
{
struct SdfLightingFrame;
}

// Builds the renderer's SDF shadow frame from the world's sun and the dynamic
// occluders. Returns false when there is no enabled sun or no valid field, which
// leaves the GPU path off and the CPU lighting untouched.
[[nodiscard]] bool BuildSdfLightingFrame(Ecs::Registry& InRegistry, const SdfField* InField,
                                         Rhi::SdfLightingFrame* OutFrame) noexcept;

inline constexpr std::uint32_t kLight2DFlagEnabled = 1u << 0;
inline constexpr std::uint32_t kLight2DFlagCastNoShadow = 1u << 1;

// A radial light. The light position is the entity's Transform2D position plus
// `Offset`, so a torch light can be parented to a moving character without a
// second transform component.
struct Light2DComponent
{
    Color32 Color = Color32::White();
    // World units where the light reaches zero.
    float Radius = 256.0f;
    float Intensity = 1.0f;
    Vector2D Offset{};
    std::uint32_t Flags = kLight2DFlagEnabled;
    float Reserved = 0.0f;
};

static_assert(std::is_trivially_copyable_v<Light2DComponent>);
static_assert(sizeof(Light2DComponent) == 28);

// Opts a sprite into the lighting solve. The system writes the resolved tint
// into Sprite2D::Color and never touches an entity without this component.
struct Light2DTargetComponent
{
    // Authored colour, before lighting.
    Color32 BaseColor = Color32::White();
    // 1 = fully self-lit: ambient and lights are ignored.
    float Emissive = 0.0f;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<Light2DTargetComponent>);
static_assert(sizeof(Light2DTargetComponent) == 12);

// Derived light colour for one sprite. The system owns this component and never
// writes Sprite2D::Color: the render extract multiplies the authored sprite
// colour by this value, so a system only ever writes its own data (DOD) and the
// same result can feed a different renderer without recomputing lighting.
struct Light2DResultComponent
{
    Color32 Color = Color32::White();
};

static_assert(std::is_trivially_copyable_v<Light2DResultComponent>);
static_assert(sizeof(Light2DResultComponent) == 4);

inline constexpr std::uint32_t kSdfSunFlagDisabled = 1u << 0;

// Directional SDF light. The static field and the moving casters are solved per
// pixel by the renderer; this component is the authored sun that drives it.
struct SdfSunComponent
{
    // Direction towards the light, normalized by the renderer.
    Vector2D Direction{0.25f, -1.0f};
    float Softness = 24.0f;
    float Ambient = 0.30f;
    float OcclusionRadius = 32.0f;
    float OcclusionStrength = 0.30f;
    std::uint32_t Steps = 16;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<SdfSunComponent>);
static_assert(std::is_standard_layout_v<SdfSunComponent>);
static_assert(sizeof(SdfSunComponent) == 32);

inline constexpr std::uint32_t kLight2DOccluderFlagBox = 1u << 0;
inline constexpr std::uint32_t kLight2DOccluderFlagDisabled = 1u << 1;
// Fixed capacity of dynamic shadow casters gathered per lighting pass.
inline constexpr std::uint32_t kMaxDynamicOccluders = 64;

// A dynamic SDF shadow caster. The static SDF field still models baked terrain;
// this component lets moving entities cast soft shadows too: the lighting pass
// combines the static field distance with the occluder shapes and sphere-traces
// the combination. Circles use Radius, boxes use HalfExtents.
struct Light2DOccluderComponent
{
    Vector2D HalfExtents{8.0f, 8.0f};
    float Radius = 8.0f;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<Light2DOccluderComponent>);
static_assert(std::is_standard_layout_v<Light2DOccluderComponent>);
static_assert(sizeof(Light2DOccluderComponent) == 16);

struct LightingSettings
{
    Color32 Ambient = Color32::FromBytes(40, 44, 60);
    float AmbientIntensity = 0.25f;
    std::uint32_t MaxLights = 128;
    // Tone-map the accumulated light so overlapping lamps do not clip to white.
    bool Tonemap = true;

    // SDF shadowing. When a field is supplied and this is on, every
    // light/sprite pair sphere-traces the field toward the light: a ray that
    // hits solid geometry before reaching the light is in shadow, and the
    // closest approach drives the penumbra. Steps are fixed per ray, so a frame
    // costs a bounded number of samples and never allocates.
    bool SdfShadows = true;
    std::uint32_t SdfMaxSteps = 16;
    // Penumbra width in world units; larger values soften the contact edge.
    float SdfSoftness = 24.0f;
    // 0 disables, 1 fully darkens creases near solid geometry.
    float SdfAmbientOcclusion = 0.35f;
    float SdfOcclusionRadius = 32.0f;
    std::uint32_t Reserved = 0;
};

// Resolves lighting for every Light2DTargetComponent + Sprite2D pair. The pass
// runs in PostUpdate, before the render extract, and is allocation-free.
class LightingSystem
{
public:
    // `InField` is optional: with it the solver adds SDF soft shadows and
    // ambient occlusion, without it lighting stays distance-only.
    [[nodiscard]] std::uint32_t Run(Ecs::Registry& InRegistry, const LightingSettings& InSettings,
                                    const SdfField* InField = nullptr) noexcept;

    [[nodiscard]] std::uint32_t LightCount() const noexcept
    {
        return MLightCount;
    }
    [[nodiscard]] std::uint32_t LitSpriteCount() const noexcept
    {
        return MLitCount;
    }
    [[nodiscard]] std::uint32_t ShadowedSampleCount() const noexcept
    {
        return MShadowSamples;
    }

    // Field used by the graph-driven Tick path. The field must outlive the
    // system (it is a non-owning SdfField view).
    void SetSdfField(const SdfField* InField) noexcept
    {
        MSdfField = InField;
    }
    [[nodiscard]] const SdfField* SdfFieldView() const noexcept
    {
        return MSdfField;
    }

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, LightingSystem& InSystem);

    // Blend helper shared by the render extracts: authored colour * light result.
    [[nodiscard]] static Color32 Compose(const Color32& InBase, const Color32& InLight) noexcept;

    LightingSettings Settings{};

private:
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    const SdfField* MSdfField = nullptr;
    std::uint32_t MLightCount = 0;
    std::uint32_t MLitCount = 0;
    std::uint32_t MShadowSamples = 0;
};
}

FBY_REFLECT_COMPONENT(Fby::Light2DComponent, "Light2DComponent",
                      FBY_FIELD(Fby::Light2DComponent, Color, Color32),
                      FBY_FIELD(Fby::Light2DComponent, Radius, Float),
                      FBY_FIELD(Fby::Light2DComponent, Intensity, Float),
                      FBY_FIELD(Fby::Light2DComponent, Offset, Vector2D),
                      FBY_FIELD(Fby::Light2DComponent, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::Light2DTargetComponent, "Light2DTargetComponent",
                      FBY_FIELD(Fby::Light2DTargetComponent, BaseColor, Color32),
                      FBY_FIELD(Fby::Light2DTargetComponent, Emissive, Float),
                      FBY_FIELD(Fby::Light2DTargetComponent, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::Light2DOccluderComponent, "Light2DOccluderComponent",
                      FBY_FIELD(Fby::Light2DOccluderComponent, HalfExtents, Vector2D),
                      FBY_FIELD(Fby::Light2DOccluderComponent, Radius, Float),
                      FBY_FIELD(Fby::Light2DOccluderComponent, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::SdfSunComponent, "SdfSunComponent",
                      FBY_FIELD(Fby::SdfSunComponent, Direction, Vector2D),
                      FBY_FIELD(Fby::SdfSunComponent, Softness, Float),
                      FBY_FIELD(Fby::SdfSunComponent, Ambient, Float),
                      FBY_FIELD(Fby::SdfSunComponent, OcclusionRadius, Float),
                      FBY_FIELD(Fby::SdfSunComponent, OcclusionStrength, Float),
                      FBY_FIELD(Fby::SdfSunComponent, Steps, UInt32),
                      FBY_FIELD(Fby::SdfSunComponent, Flags, UInt32))
