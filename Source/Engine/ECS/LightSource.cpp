// FemboyEngine2D - unified light source solver with a static contribution cache.
#include "Engine/ECS/LightSource.hpp"

#include "Engine/ECS/Presentation.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Fby
{
namespace
{
constexpr std::uint32_t kMaxLightSamples = 128;
constexpr std::uint32_t kMaxLightTargets = 1024;
constexpr float kFullTurn = kFullTurnRadians;

struct LightSample
{
    Vector2D Position{};
    Vector2D Direction{1.0f, 0.0f};
    float Radius = 1.0f;
    float Intensity = 1.0f;
    float Red = 1.0f;
    float Green = 1.0f;
    float Blue = 1.0f;
    float Aperture = kFullTurn;
    float Softness = 0.0f;
    float Falloff = 2.0f;
    bool IsStatic = false;
};

// One dynamic SDF shadow caster sampled by the lighting pass.
struct OccluderSample
{
    Vector2D Center{};
    Vector2D HalfExtents{};
    float Radius = 0.0f;
    std::uint32_t Flags = 0;
    Entity Owner = entt::null;
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
struct CombinedField
{
    const SdfField* Static = nullptr;
    const OccluderSample* Occluders = nullptr;
    std::uint32_t OccluderCount = 0;
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

// SDF soft shadow for one light/target pair, with a bounded step budget.
[[nodiscard]] float SdfShadowFactor(const CombinedField& InField, Vector2D InFrom, Vector2D InTo, std::uint32_t InMaxSteps,
                                    float InSoftness, std::uint32_t* Samples) noexcept
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
        ScopedRatio = std::min(ScopedRatio, ScopedSample / (ScopedSoftness * ScopedT));
        ScopedT += std::max(ScopedSample * 0.8f, 0.5f);
    }
    return std::clamp(ScopedRatio, 0.0f, 1.0f);
}

// Distance-to-solid ratio around a target: zero in a crease, one in open space.
[[nodiscard]] float SdfOcclusion(const CombinedField& InField, Vector2D InPoint, float InRadius) noexcept
{
    const float ScopedDistance = InField.Sample(InPoint);
    if (ScopedDistance <= 0.0f)
    {
        return 0.0f;
    }
    return std::clamp(ScopedDistance / std::max(InRadius, 1.0f), 0.0f, 1.0f);
}

// Angular term of a cone light. Direction is the transform heading and the
// target vector points from the light to the receiver.
[[nodiscard]] float ConeFactor(const LightSample& InLight, Vector2D InTarget) noexcept
{
    if (InLight.Aperture >= kFullTurn)
    {
        return 1.0f;
    }
    const Vector2D ScopedToTarget = InTarget - InLight.Position;
    const float ScopedCross = InLight.Direction.X * ScopedToTarget.Y - InLight.Direction.Y * ScopedToTarget.X;
    const float ScopedDot = InLight.Direction.X * ScopedToTarget.X + InLight.Direction.Y * ScopedToTarget.Y;
    const float ScopedAngle = std::atan2(ScopedCross, ScopedDot);
    return LightConeAttenuation(ScopedAngle, InLight.Aperture, InLight.Softness);
}

// Adds every light of one class (static or dynamic) to a target's colour.
[[nodiscard]] std::uint32_t AccumulateLights(const LightSample* InLights, std::uint32_t InCount, Vector2D InTarget,
                                             bool InStaticClass, bool InShadows, const CombinedField& InField,
                                             const LightSourceSettings& InSettings, float* OutRed,
                                             float* OutGreen, float* OutBlue) noexcept
{
    std::uint32_t ScopedShadows = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < InCount; ++ScopedI)
    {
        const LightSample& ScopedLight = InLights[ScopedI];
        if (ScopedLight.IsStatic != InStaticClass)
        {
            continue;
        }
        const float ScopedDistance = std::sqrt((InTarget - ScopedLight.Position).LengthSquared());
        float ScopedAttenuation = LightDistanceAttenuation(ScopedDistance, ScopedLight.Radius, ScopedLight.Falloff);
        if (ScopedAttenuation <= 0.0f)
        {
            continue;
        }
        ScopedAttenuation *= ConeFactor(ScopedLight, InTarget) * ScopedLight.Intensity;
        if (ScopedAttenuation <= 0.0f)
        {
            continue;
        }
        if (InShadows && InSettings.SdfShadows && InField.Active())
        {
            ScopedAttenuation *= SdfShadowFactor(InField, InTarget, ScopedLight.Position, InSettings.SdfMaxSteps,
                                                 InSettings.SdfSoftness, &ScopedShadows);
        }
        *OutRed += ScopedLight.Red * ScopedAttenuation;
        *OutGreen += ScopedLight.Green * ScopedAttenuation;
        *OutBlue += ScopedLight.Blue * ScopedAttenuation;
    }
    return ScopedShadows;
}

[[nodiscard]] std::uint64_t MixValue(std::uint64_t InHash, float InValue) noexcept
{
    std::uint32_t ScopedBits = 0;
    std::memcpy(&ScopedBits, &InValue, sizeof(ScopedBits));
    return (InHash ^ ScopedBits) * 1099511628211ull;
}

[[nodiscard]] std::uint64_t MixEntity(std::uint64_t InHash, Entity InEntity) noexcept
{
    return (InHash ^ static_cast<std::uint64_t>(entt::to_integral(InEntity))) * 1099511628211ull;
}
}

bool WakeLightSource(LightSourceComponent& InLight, bool InFromContact) noexcept
{
    if ((InLight.Flags & LightSourceSleeping) == 0)
    {
        return false;
    }
    if (InFromContact && (InLight.Flags & LightSourceWakeOnContact) == 0)
    {
        return false;
    }
    InLight.Flags &= ~(LightSourceStatic | LightSourceSleeping);
    return true;
}

std::uint32_t LightSourceSystem::Run(Ecs::Registry& InRegistry, const LightSourceSettings& InSettings,
                                     const SdfField* InField) noexcept
{
    std::array<LightSample, kMaxLightSamples> ScopedLights{};
    std::array<OccluderSample, kMaxDynamicOccluders> ScopedOccluders{};
    std::uint32_t ScopedOccluderCount = 0;
    auto ScopedOccluderView = InRegistry.view<Transform2D, Light2DOccluderComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedOccluder] : ScopedOccluderView.each())
    {
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

    const float ScopedAmbientStrength = std::max(InSettings.AmbientIntensity, 0.0f);
    float ScopedBaseRed = UnpackChannel(InSettings.Ambient.rgba, 0) * ScopedAmbientStrength;
    float ScopedBaseGreen = UnpackChannel(InSettings.Ambient.rgba, 8) * ScopedAmbientStrength;
    float ScopedBaseBlue = UnpackChannel(InSettings.Ambient.rgba, 16) * ScopedAmbientStrength;

    // Every target is cleared first: a sprite that loses its light target keeps
    // no stale tint.
    InRegistry.clear<Light2DResultComponent>();

    std::uint32_t ScopedLightCount = 0;
    std::uint32_t ScopedStaticCount = 0;
    std::uint32_t ScopedDynamicCount = 0;
    const std::uint32_t ScopedCapacity = std::min<std::uint32_t>(InSettings.MaxLights, kMaxLightSamples);
    auto ScopedLightView = InRegistry.view<LightSourceComponent>();
    for (auto [ScopedEntity, ScopedSource] : ScopedLightView.each())
    {
        if (!ScopedSource.IsEnabled() ||
            !EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassLightSources))
        {
            continue;
        }
        const LightSourceType ScopedType = ScopedSource.GetType();
        if (ScopedType == LightSourceType::Global)
        {
            ScopedBaseRed += UnpackChannel(ScopedSource.Color.rgba, 0) * ScopedSource.Intensity;
            ScopedBaseGreen += UnpackChannel(ScopedSource.Color.rgba, 8) * ScopedSource.Intensity;
            ScopedBaseBlue += UnpackChannel(ScopedSource.Color.rgba, 16) * ScopedSource.Intensity;
            continue;
        }
        const auto* ScopedTransform = InRegistry.try_get<Transform2D>(ScopedEntity);
        if (ScopedTransform == nullptr || ScopedLightCount >= ScopedCapacity)
        {
            continue;
        }
        LightSample& ScopedLight = ScopedLights[ScopedLightCount++];
        ScopedLight.Position = ScopedTransform->Position + ScopedSource.Offset;
        ScopedLight.Direction = Vector2D{std::cos(ScopedTransform->Rotation), std::sin(ScopedTransform->Rotation)};
        ScopedLight.Radius = std::max(ScopedSource.Radius, 1.0f);
        ScopedLight.Intensity = std::max(ScopedSource.Intensity, 0.0f);
        ScopedLight.Red = UnpackChannel(ScopedSource.Color.rgba, 0);
        ScopedLight.Green = UnpackChannel(ScopedSource.Color.rgba, 8);
        ScopedLight.Blue = UnpackChannel(ScopedSource.Color.rgba, 16);
        ScopedLight.Aperture = ScopedType == LightSourceType::Omnidirectional ? kFullTurn : ScopedSource.Aperture();
        ScopedLight.Softness = std::max(ScopedSource.ConeSoftness, 0.0f);
        ScopedLight.Falloff = ScopedSource.Falloff;
        ScopedLight.IsStatic = ScopedSource.IsStatic();
        if (ScopedLight.IsStatic)
        {
            ++ScopedStaticCount;
        }
        else
        {
            ++ScopedDynamicCount;
        }
    }
    MLightCount = ScopedLightCount;
    MStaticLights = ScopedStaticCount;
    MDynamicLights = ScopedDynamicCount;

    std::uint64_t ScopedSignature = 1469598103934665603ull;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedLightCount; ++ScopedI)
    {
        if (!ScopedLights[ScopedI].IsStatic)
        {
            continue;
        }
        const LightSample& ScopedLight = ScopedLights[ScopedI];
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Position.X);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Position.Y);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Direction.X);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Direction.Y);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Radius);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Intensity);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Red);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Green);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Blue);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Aperture);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Softness);
        ScopedSignature = MixValue(ScopedSignature, ScopedLight.Falloff);
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedOccluderCount; ++ScopedI)
    {
        ScopedSignature = MixValue(ScopedSignature, ScopedOccluders[ScopedI].Center.X);
        ScopedSignature = MixValue(ScopedSignature, ScopedOccluders[ScopedI].Center.Y);
        ScopedSignature = MixValue(ScopedSignature, ScopedOccluders[ScopedI].Radius);
    }
    ScopedSignature = MixValue(ScopedSignature, InSettings.SdfSoftness);
    ScopedSignature = MixValue(ScopedSignature, InSettings.SdfAmbientOcclusion);
    ScopedSignature = MixValue(ScopedSignature, static_cast<float>(InSettings.SdfMaxSteps));
    ScopedSignature = MixValue(ScopedSignature, InSettings.SdfShadows ? 1.0f : 0.0f);
    // The ambient and global contribution is baked into each cached target, so
    // changing either must invalidate the cache as well.
    ScopedSignature = MixValue(ScopedSignature, ScopedBaseRed);
    ScopedSignature = MixValue(ScopedSignature, ScopedBaseGreen);
    ScopedSignature = MixValue(ScopedSignature, ScopedBaseBlue);
    ScopedSignature =
        MixValue(ScopedSignature, static_cast<float>(reinterpret_cast<std::uintptr_t>(InField) & 0xFFFFFFFFu));
    {
        // Target transforms are part of the static contribution: a moving
        // receiver must rebuild the cache even when every light stayed put.
        auto ScopedSignatureView = InRegistry.view<Transform2D, Sprite2D, Light2DTargetComponent>();
        for (auto [ScopedEntity, ScopedTransform, ScopedSprite, ScopedTarget] : ScopedSignatureView.each())
        {
            (void)ScopedSprite;
            (void)ScopedTarget;
            if (!EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassLighting))
            {
                continue;
            }
            ScopedSignature = MixEntity(ScopedSignature, ScopedEntity);
            ScopedSignature = MixValue(ScopedSignature, ScopedTransform.Position.X);
            ScopedSignature = MixValue(ScopedSignature, ScopedTransform.Position.Y);
        }
    }

    const bool ScopedCacheRequested = InSettings.CacheStaticLights && ScopedStaticCount > 0;
    std::uint32_t ScopedStaticSamples = 0;
    const bool ScopedHadValidCache = ScopedCacheRequested && MStaticValid && ScopedSignature == MStaticSignature;
    if (ScopedCacheRequested && !ScopedHadValidCache)
    {
        MStaticTargetCount = 0;
        MStaticSignature = ScopedSignature;
        MStaticValid = true;
        ++MStaticRebuilds;
        auto ScopedTargetView = InRegistry.view<Transform2D, Sprite2D, Light2DTargetComponent>();
        for (auto [ScopedEntity, ScopedTransform, ScopedSprite, ScopedTarget] : ScopedTargetView.each())
        {
            (void)ScopedSprite;
            if (!EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassLighting) ||
                MStaticTargetCount >= kMaxLightTargets)
            {
                continue;
            }
            CachedTarget& ScopedEntry = MStaticTargets[MStaticTargetCount++];
            ScopedEntry.Owner = ScopedEntity;
            ScopedEntry.Red = ScopedBaseRed;
            ScopedEntry.Green = ScopedBaseGreen;
            ScopedEntry.Blue = ScopedBaseBlue;
            ScopedField.Receiver = ScopedEntity;
            const bool ScopedShadows = EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassShadows);
            ScopedStaticSamples += AccumulateLights(ScopedLights.data(), ScopedLightCount, ScopedTransform.Position,
                                                    true, ScopedShadows, ScopedField, InSettings, &ScopedEntry.Red,
                                                    &ScopedEntry.Green, &ScopedEntry.Blue);
            if (ScopedField.Active() && InSettings.SdfAmbientOcclusion > 0.0f)
            {
                const float ScopedOcclusion =
                    SdfOcclusion(ScopedField, ScopedTransform.Position, InSettings.SdfOcclusionRadius);
                const float ScopedStrength = std::clamp(InSettings.SdfAmbientOcclusion, 0.0f, 1.0f);
                const float ScopedScale = 1.0f - (1.0f - ScopedOcclusion) * ScopedStrength;
                ScopedEntry.Red *= ScopedScale;
                ScopedEntry.Green *= ScopedScale;
                ScopedEntry.Blue *= ScopedScale;
            }
        }
    }
    if (ScopedHadValidCache)
    {
        ++MStaticHits;
    }
    const bool ScopedUsingCache = ScopedCacheRequested && MStaticValid;

    std::uint32_t ScopedLit = 0;
    std::uint32_t ScopedShadowSamples = ScopedStaticSamples;
    auto ScopedTargetView = InRegistry.view<Transform2D, Sprite2D, Light2DTargetComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedSprite, ScopedTarget] : ScopedTargetView.each())
    {
        (void)ScopedSprite;
        if (!EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassLighting))
        {
            continue;
        }
        ScopedField.Receiver = ScopedEntity;
        float ScopedRed = ScopedBaseRed;
        float ScopedGreen = ScopedBaseGreen;
        float ScopedBlue = ScopedBaseBlue;
        if (ScopedUsingCache)
        {
            for (std::uint32_t ScopedI = 0; ScopedI < MStaticTargetCount; ++ScopedI)
            {
                if (MStaticTargets[ScopedI].Owner == ScopedEntity)
                {
                    ScopedRed = MStaticTargets[ScopedI].Red;
                    ScopedGreen = MStaticTargets[ScopedI].Green;
                    ScopedBlue = MStaticTargets[ScopedI].Blue;
                    break;
                }
            }
        }
        else
        {
            ScopedShadowSamples += AccumulateLights(ScopedLights.data(), ScopedLightCount, ScopedTransform.Position,
                                                    true,
                                                    EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassShadows),
                                                    ScopedField, InSettings, &ScopedRed, &ScopedGreen, &ScopedBlue);
        }
        ScopedShadowSamples += AccumulateLights(ScopedLights.data(), ScopedLightCount, ScopedTransform.Position,
                                                false,
                                                EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassShadows),
                                                ScopedField, InSettings, &ScopedRed, &ScopedGreen, &ScopedBlue);
        if (!ScopedUsingCache && ScopedField.Active() && InSettings.SdfAmbientOcclusion > 0.0f)
        {
            const float ScopedOcclusion =
                SdfOcclusion(ScopedField, ScopedTransform.Position, InSettings.SdfOcclusionRadius);
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
        const float ScopedEmissive = std::clamp(ScopedTarget.Emissive, 0.0f, 1.0f);
        if (ScopedEmissive > 0.0f)
        {
            ScopedRed += (1.0f - ScopedRed) * ScopedEmissive;
            ScopedGreen += (1.0f - ScopedGreen) * ScopedEmissive;
            ScopedBlue += (1.0f - ScopedBlue) * ScopedEmissive;
        }
        InRegistry.emplace_or_replace<Light2DResultComponent>(
            ScopedEntity, Light2DResultComponent{Color32{PackRgb(ScopedRed, ScopedGreen, ScopedBlue, 0xFF000000u)}});
        ++ScopedLit;
    }
    MLitCount = ScopedLit;
    MShadowSamples = ScopedShadowSamples;
    return ScopedLit;
}

void LightSourceSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    if (InUser == nullptr)
    {
        return;
    }
    auto* ScopedSelf = static_cast<LightSourceSystem*>(InUser);
    (void)ScopedSelf->Run(InContext.registry, ScopedSelf->Settings, ScopedSelf->SdfFieldView());
}

bool LightSourceSystem::Register(Ecs::SystemGraph& InGraph, LightSourceSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::PostUpdate, "LightSourceSystem", &LightSourceSystem::Tick, &InSystem);
}
}
