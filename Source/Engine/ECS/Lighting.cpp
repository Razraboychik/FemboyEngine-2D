// FemboyEngine2D - 2D lighting resolution.
#include "Engine/ECS/Lighting.hpp"

#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/RHI/RHI.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Fby
{
namespace
{
constexpr std::uint32_t kMaxLightSamples = 128;
constexpr std::uint32_t kMaxLitSprites = 1024;

struct LitEntry
{
    Entity entity = entt::null;
    Color32 color{};
};

struct LightSample
{
    Vector2D position{};
    float radius = 1.0f;
    float intensity = 1.0f;
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
};

[[nodiscard]] float UnpackChannel(std::uint32_t InRgba, std::uint32_t InShift) noexcept
{
    return static_cast<float>((InRgba >> InShift) & 0xFFu) / 255.0f;
}

[[nodiscard]] std::uint32_t PackRgb(float InRed, float InGreen, float InBlue, std::uint32_t InAlpha) noexcept
{
    const auto ScopedByte = [](float InValue)
    {
        const float ScopedClamped = std::clamp(InValue, 0.0f, 1.0f);
        return static_cast<std::uint32_t>(std::lround(ScopedClamped * 255.0f));
    };
    return ScopedByte(InRed) | (ScopedByte(InGreen) << 8) | (ScopedByte(InBlue) << 16) | (InAlpha & 0xFF000000u);
}

[[nodiscard]] float Tonemap(float InValue) noexcept
{
    if (InValue <= 0.0f)
    {
        return 0.0f;
    }
    return InValue / (1.0f + InValue);
}

// One dynamic shadow caster sampled by the lighting pass.
struct OccluderSample
{
    Vector2D Center{};
    Vector2D HalfExtents{};
    float Radius = 0.0f;
    std::uint32_t Flags = 0;
    Entity Owner = entt::null;
};

[[nodiscard]] float BoxDistance(Vector2D InPoint, Vector2D InHalfExtents) noexcept
{
    const float ScopedQx = std::fabs(InPoint.X) - InHalfExtents.X;
    const float ScopedQy = std::fabs(InPoint.Y) - InHalfExtents.Y;
    const float ScopedOutX = std::max(ScopedQx, 0.0f);
    const float ScopedOutY = std::max(ScopedQy, 0.0f);
    return std::sqrt(ScopedOutX * ScopedOutX + ScopedOutY * ScopedOutY) + std::min(std::max(ScopedQx, ScopedQy), 0.0f);
}

[[nodiscard]] float OccluderDistance(const OccluderSample& InOccluder, Vector2D InPoint) noexcept
{
    const Vector2D ScopedLocal = InPoint - InOccluder.Center;
    if ((InOccluder.Flags & kLight2DOccluderFlagBox) != 0)
    {
        return BoxDistance(ScopedLocal, InOccluder.HalfExtents);
    }
    return std::sqrt(ScopedLocal.LengthSquared()) - InOccluder.Radius;
}

// The combined distance field: baked terrain plus this frame's dynamic casters.
// Static geometry stays in the baked SDFField, dynamic entities are re-gathered
// every frame, so a moving crate casts a moving shadow without a field rebake.
struct CombinedField
{
    const SdfField* Static = nullptr;
    const OccluderSample* Occluders = nullptr;
    std::uint32_t OccluderCount = 0;
    // The sprite being lit: its own occluder must not shadow it, otherwise an
    // entity that both receives light and casts shadows would be black.
    Entity Receiver = entt::null;

    [[nodiscard]] bool Active() const noexcept
    {
        return (Static != nullptr && Static->Valid()) || OccluderCount > 0;
    }
    [[nodiscard]] float Sample(Vector2D InPoint) const noexcept
    {
        float ScopedDistance = (Static != nullptr && Static->Valid()) ? Static->Sample(InPoint) : 1.0e6f;
        for (std::uint32_t ScopedI = 0; ScopedI < OccluderCount; ++ScopedI)
        {
            if (Occluders[ScopedI].Owner == Receiver)
            {
                continue;
            }
            ScopedDistance = std::min(ScopedDistance, OccluderDistance(Occluders[ScopedI], InPoint));
        }
        return ScopedDistance;
    }
};

// SDF soft shadow for one light/sprite pair. Sphere-tracing toward the light
// with a fixed step budget: a ray that enters solid geometry (negative
// distance) is fully occluded, and the smallest distance/step ratio along the
// way produces the penumbra. Deterministic and allocation-free.
[[nodiscard]] float SdfShadowFactor(const CombinedField& InField, Vector2D InFrom, Vector2D InTo,
                                    std::uint32_t InMaxSteps, float InSoftness, std::uint32_t* Samples) noexcept
{
    const Vector2D ScopedDelta = InTo - InFrom;
    const float ScopedDistance = std::sqrt(ScopedDelta.LengthSquared());
    if (ScopedDistance < 1.0e-3f)
    {
        return 1.0f;
    }
    const Vector2D ScopedDirection = ScopedDelta * (1.0f / ScopedDistance);
    const std::uint32_t ScopedSteps = std::clamp<std::uint32_t>(InMaxSteps, 1u, 64u);
    const float ScopedSoftness = std::max(InSoftness, 1.0e-3f);
    float ScopedRatio = 1.0f;
    float ScopedT = 0.5f;
    for (std::uint32_t ScopedStep = 0; ScopedStep < ScopedSteps && ScopedT < ScopedDistance; ++ScopedStep)
    {
        if (Samples != nullptr)
        {
            ++(*Samples);
        }
        const Vector2D ScopedPoint = InFrom + ScopedDirection * ScopedT;
        const float ScopedSample = InField.Sample(ScopedPoint);
        if (ScopedSample <= 0.0f)
        {
            return 0.0f;
        }
        const float ScopedPenumbra = ScopedSample / (ScopedSoftness * ScopedT);
        ScopedRatio = std::min(ScopedRatio, ScopedPenumbra);
        ScopedT += std::max(ScopedSample * 0.8f, 0.5f);
    }
    return std::clamp(ScopedRatio, 0.0f, 1.0f);
}

// Distance-to-solid ratio around a sprite: 0 in a crease, 1 in open space.
[[nodiscard]] float SdfOcclusion(const CombinedField& InField, Vector2D InPoint, float InRadius) noexcept
{
    const float ScopedDistance = InField.Sample(InPoint);
    if (ScopedDistance <= 0.0f)
    {
        return 0.0f;
    }
    return std::clamp(ScopedDistance / std::max(InRadius, 1.0f), 0.0f, 1.0f);
}
}

std::uint32_t LightingSystem::Run(Ecs::Registry& InRegistry, const LightingSettings& InSettings,
                                  const SdfField* InField) noexcept
{
    std::array<LightSample, kMaxLightSamples> ScopedLights{};
    std::uint32_t ScopedLightCount = 0;
    // Result components are derived state: clear first so a sprite that loses its
    // light target (or a removed light) cannot keep a stale tint.
    InRegistry.clear<Light2DResultComponent>();
    const std::uint32_t ScopedCapacity = std::min<std::uint32_t>(InSettings.MaxLights, kMaxLightSamples);

    // Dynamic shadow casters are gathered every frame; static terrain stays in
    // the baked SdfField, so moving entities shadow correctly without a rebake.
    std::array<OccluderSample, kMaxDynamicOccluders> ScopedOccluders{};
    std::uint32_t ScopedOccluderCount = 0;
    auto ScopedOccluderView = InRegistry.view<Transform2D, Light2DOccluderComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedOccluder] : ScopedOccluderView.each())
    {
        (void)ScopedEntity;
        if ((ScopedOccluder.Flags & kLight2DOccluderFlagDisabled) != 0 ||
            ScopedOccluderCount >= kMaxDynamicOccluders)
        {
            continue;
        }
        OccluderSample& ScopedSample = ScopedOccluders[ScopedOccluderCount++];
        ScopedSample.Center = ScopedTransform.Position;
        ScopedSample.HalfExtents = ScopedOccluder.HalfExtents;
        ScopedSample.Radius = std::max(ScopedOccluder.Radius, 0.0f);
        ScopedSample.Flags = ScopedOccluder.Flags;
        ScopedSample.Owner = ScopedEntity;
    }
    CombinedField ScopedField;
    ScopedField.Static = InField;
    ScopedField.Occluders = ScopedOccluders.data();
    ScopedField.OccluderCount = ScopedOccluderCount;
    const bool ScopedHasField = ScopedField.Active();

    auto ScopedLightView = InRegistry.view<Transform2D, Light2DComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedLight] : ScopedLightView.each())
    {
        (void)ScopedEntity;
        if ((ScopedLight.Flags & kLight2DFlagEnabled) == 0 || ScopedLightCount >= ScopedCapacity)
        {
            continue;
        }
        LightSample& ScopedSample = ScopedLights[ScopedLightCount++];
        ScopedSample.position = ScopedTransform.Position + ScopedLight.Offset;
        ScopedSample.radius = std::max(ScopedLight.Radius, 1.0f);
        ScopedSample.intensity = std::max(ScopedLight.Intensity, 0.0f);
        ScopedSample.red = UnpackChannel(ScopedLight.Color.rgba, 0);
        ScopedSample.green = UnpackChannel(ScopedLight.Color.rgba, 8);
        ScopedSample.blue = UnpackChannel(ScopedLight.Color.rgba, 16);
    }
    MLightCount = ScopedLightCount;

    const float ScopedAmbientStrength = std::max(InSettings.AmbientIntensity, 0.0f);
    const float ScopedAmbientRed = UnpackChannel(InSettings.Ambient.rgba, 0) * ScopedAmbientStrength;
    const float ScopedAmbientGreen = UnpackChannel(InSettings.Ambient.rgba, 8) * ScopedAmbientStrength;
    const float ScopedAmbientBlue = UnpackChannel(InSettings.Ambient.rgba, 16) * ScopedAmbientStrength;

    std::uint32_t ScopedLit = 0;
    std::uint32_t ScopedShadowSamples = 0;
    std::array<LitEntry, kMaxLitSprites> ScopedResults{};
    auto ScopedSpriteView = InRegistry.view<Transform2D, Sprite2D, Light2DTargetComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedSprite, ScopedTarget] : ScopedSpriteView.each())
    {
        ScopedField.Receiver = ScopedEntity;
        const float ScopedEmissive = std::clamp(ScopedTarget.Emissive, 0.0f, 1.0f);
        float ScopedRed = ScopedAmbientRed;
        float ScopedGreen = ScopedAmbientGreen;
        float ScopedBlue = ScopedAmbientBlue;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedLightCount; ++ScopedI)
        {
            const LightSample& ScopedSample = ScopedLights[ScopedI];
            const Vector2D ScopedDelta = ScopedTransform.Position - ScopedSample.position;
            const float ScopedDistance = std::sqrt(ScopedDelta.LengthSquared());
            if (ScopedDistance >= ScopedSample.radius)
            {
                continue;
            }
            const float ScopedFalloff = 1.0f - ScopedDistance / ScopedSample.radius;
            float ScopedAttenuation = ScopedFalloff * ScopedFalloff * ScopedSample.intensity;
            if (ScopedHasField && InSettings.SdfShadows)
            {
                const float ScopedShadow = SdfShadowFactor(ScopedField, ScopedTransform.Position, ScopedSample.position,
                                                           InSettings.SdfMaxSteps, InSettings.SdfSoftness,
                                                           &ScopedShadowSamples);
                ScopedAttenuation *= ScopedShadow;
            }
            ScopedRed += ScopedSample.red * ScopedAttenuation;
            ScopedGreen += ScopedSample.green * ScopedAttenuation;
            ScopedBlue += ScopedSample.blue * ScopedAttenuation;
        }
        if (ScopedHasField && InSettings.SdfAmbientOcclusion > 0.0f)
        {
            const float ScopedOcclusion = SdfOcclusion(ScopedField, ScopedTransform.Position, InSettings.SdfOcclusionRadius);
            const float ScopedStrength = std::clamp(InSettings.SdfAmbientOcclusion, 0.0f, 1.0f);
            const float ScopedScale = 1.0f - (1.0f - ScopedOcclusion) * ScopedStrength;
            ScopedRed *= ScopedScale;
            ScopedGreen *= ScopedScale;
            ScopedBlue *= ScopedScale;
        }
        if (InSettings.Tonemap)
        {
            ScopedRed = Tonemap(ScopedRed);
            ScopedGreen = Tonemap(ScopedGreen);
            ScopedBlue = Tonemap(ScopedBlue);
        }
        if (ScopedEmissive > 0.0f)
        {
            ScopedRed += (1.0f - ScopedRed) * ScopedEmissive;
            ScopedGreen += (1.0f - ScopedGreen) * ScopedEmissive;
            ScopedBlue += (1.0f - ScopedBlue) * ScopedEmissive;
        }
        if (ScopedLit < kMaxLitSprites)
        {
            ScopedResults[ScopedLit].entity = ScopedEntity;
            // The result is a light tint: alpha stays opaque so the extract keeps
            // the authored sprite alpha instead of multiplying it twice.
            ScopedResults[ScopedLit].color = Color32{PackRgb(ScopedRed, ScopedGreen, ScopedBlue, 0xFF000000u)};
        }
        ++ScopedLit;
    }

    // Structural pass: the component write happens outside the view iteration,
    // and the system never touches Sprite2D::Color. The extract composes.
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedLit && ScopedI < kMaxLitSprites; ++ScopedI)
    {
        InRegistry.emplace_or_replace<Light2DResultComponent>(ScopedResults[ScopedI].entity,
                                                              Light2DResultComponent{ScopedResults[ScopedI].color});
    }
    MLitCount = ScopedLit;
    MShadowSamples = ScopedShadowSamples;
    return ScopedLit;
}

Color32 LightingSystem::Compose(const Color32& InBase, const Color32& InLight) noexcept
{
    const auto ScopedMul = [](std::uint32_t InA, std::uint32_t InB)
    {
        return (InA * InB + 127u) / 255u;
    };
    // RGB is multiplied; the base alpha is preserved so a light cannot change
    // the sprite's transparency.
    const std::uint32_t ScopedRgb = ScopedMul(InBase.rgba & 0xFFu, InLight.rgba & 0xFFu) |
                                    (ScopedMul((InBase.rgba >> 8) & 0xFFu, (InLight.rgba >> 8) & 0xFFu) << 8) |
                                    (ScopedMul((InBase.rgba >> 16) & 0xFFu, (InLight.rgba >> 16) & 0xFFu) << 16);
    return Color32{ScopedRgb | (InBase.rgba & 0xFF000000u)};
}

bool BuildSdfLightingFrame(Ecs::Registry& InRegistry, const SdfField* InField, Rhi::SdfLightingFrame* OutFrame) noexcept
{
    if (OutFrame == nullptr)
    {
        return false;
    }
    *OutFrame = Rhi::SdfLightingFrame{};
    if (InField == nullptr || !InField->Valid())
    {
        return false;
    }
    const SdfSunComponent* ScopedSun = nullptr;
    auto ScopedSunView = InRegistry.view<SdfSunComponent>();
    for (auto [ScopedEntity, ScopedComponent] : ScopedSunView.each())
    {
        (void)ScopedEntity;
        if ((ScopedComponent.Flags & kSdfSunFlagDisabled) == 0)
        {
            ScopedSun = &ScopedComponent;
            break;
        }
    }
    if (ScopedSun == nullptr)
    {
        return false;
    }

    OutFrame->field = InField->samples;
    OutFrame->width = InField->width;
    OutFrame->height = InField->height;
    OutFrame->origin = InField->origin;
    OutFrame->cellSize = InField->cellSize;
    OutFrame->sunDirection = ScopedSun->Direction;
    OutFrame->softness = std::max(ScopedSun->Softness, 1.0e-3f);
    OutFrame->ambient = std::clamp(ScopedSun->Ambient, 0.0f, 1.0f);
    OutFrame->occlusionRadius = std::max(ScopedSun->OcclusionRadius, 1.0f);
    OutFrame->occlusionStrength = std::clamp(ScopedSun->OcclusionStrength, 0.0f, 1.0f);
    OutFrame->steps = std::clamp(ScopedSun->Steps, 1u, 64u);

    // Field identity for the backend's upload cache: the mask only re-uploads when
    // the view changes. A caller that edits the samples in place bumps the
    // version by swapping the field (new pointer/dimensions).
    const std::uint64_t ScopedPointer = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(InField->samples));
    OutFrame->fieldVersion = ScopedPointer ^ (static_cast<std::uint64_t>(InField->width) << 32) ^
                             static_cast<std::uint64_t>(InField->height);

    std::uint32_t ScopedCount = 0;
    auto ScopedOccluderView = InRegistry.view<Transform2D, Light2DOccluderComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedOccluder] : ScopedOccluderView.each())
    {
        (void)ScopedEntity;
        if ((ScopedOccluder.Flags & kLight2DOccluderFlagDisabled) != 0 ||
            ScopedCount >= Rhi::kMaxSdfGpuOccluders)
        {
            continue;
        }
        const bool ScopedBox = (ScopedOccluder.Flags & kLight2DOccluderFlagBox) != 0;
        Rhi::SdfLightingOccluder& ScopedGpu = OutFrame->occluders[ScopedCount++];
        ScopedGpu.Shape = Vector4D{ScopedTransform.Position.X, ScopedTransform.Position.Y,
                                   ScopedBox ? 0.0f : std::max(ScopedOccluder.Radius, 0.0f), 0.0f};
        ScopedGpu.Extents = Vector4D{ScopedOccluder.HalfExtents.X, ScopedOccluder.HalfExtents.Y, ScopedBox ? 1.0f : 0.0f,
                                     0.0f};
    }
    OutFrame->occluderCount = ScopedCount;
    OutFrame->enabled = true;
    return true;
}

void LightingSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    if (InUser == nullptr)
    {
        return;
    }
    auto* ScopedSelf = static_cast<LightingSystem*>(InUser);
    (void)ScopedSelf->Run(InContext.registry, ScopedSelf->Settings, ScopedSelf->SdfFieldView());
}

bool LightingSystem::Register(Ecs::SystemGraph& InGraph, LightingSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::PostUpdate, "LightingSystem", &LightingSystem::Tick, &InSystem);
}
}
