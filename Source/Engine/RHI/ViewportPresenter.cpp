// FemboyEngine2D - viewport view-mode diagnostic computation.
#include "Engine/RHI/ViewportPresenter.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/LightSource.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/Physics/Physics.hpp"

#include <algorithm>
#include <cmath>

namespace Fby::Rhi
{
namespace
{
[[nodiscard]] bool RangesOverlap(Vector2D InCenterA, Vector2D InHalfA, Vector2D InCenterB, Vector2D InHalfB) noexcept
{
    return std::fabs(InCenterA.X - InCenterB.X) < (InHalfA.X + InHalfB.X) &&
           std::fabs(InCenterA.Y - InCenterB.Y) < (InHalfA.Y + InHalfB.Y);
}

[[nodiscard]] std::uint32_t PackColor(const Color32& InColor) noexcept
{
    return InColor.rgba;
}
}

Color32 ViewportPresenter::CollisionModeColor(std::uint32_t InMode) noexcept
{
    switch (static_cast<CollisionMode>(InMode < kCollisionModeCount ? InMode : 0u))
    {
        case CollisionMode::NoCollision:
            return Color32::FromBytes(140, 140, 150);
        case CollisionMode::QueryOnly:
        case CollisionMode::TriggerOnly:
            return Color32::FromBytes(230, 90, 220);
        case CollisionMode::SimpleStatic:
        case CollisionMode::SimpleDynamic:
            return Color32::FromBytes(80, 220, 120);
        case CollisionMode::ComplexStatic:
        case CollisionMode::ComplexDynamic:
            return Color32::FromBytes(250, 210, 70);
    }
    return Color32::FromBytes(140, 140, 150);
}

Color32 ViewportPresenter::CollisionOverlayColor(const Ecs::Registry& InRegistry, Entity InEntity) noexcept
{
    const auto* ScopedPolicy = InRegistry.try_get<CollisionPolicyComponent>(InEntity);
    if (ScopedPolicy != nullptr)
    {
        return CollisionModeColor(ScopedPolicy->Mode);
    }
    if (InRegistry.try_get<Collider2D>(InEntity) != nullptr)
    {
        return CollisionModeColor(static_cast<std::uint32_t>(CollisionMode::SimpleDynamic));
    }
    return CollisionModeColor(static_cast<std::uint32_t>(CollisionMode::NoCollision));
}

std::uint32_t ViewportPresenter::CountReachingLights(Ecs::Registry& InRegistry, Entity InEntity,
                                                     Vector2D InPosition) const noexcept
{
    std::uint32_t ScopedCount = 0;
    auto ScopedView = InRegistry.view<LightSourceComponent>();
    for (auto [ScopedEntity, ScopedSource] : ScopedView.each())
    {
        if (InEntity == ScopedEntity || !ScopedSource.IsEnabled() ||
            !EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassLightSources))
        {
            continue;
        }
        if (ScopedSource.GetType() == LightSourceType::Global)
        {
            ++ScopedCount;
            continue;
        }
        const auto* ScopedTransform = InRegistry.try_get<Transform2D>(ScopedEntity);
        if (ScopedTransform == nullptr)
        {
            continue;
        }
        const Vector2D ScopedLightPosition = ScopedTransform->Position + ScopedSource.Offset;
        const float ScopedDistance = std::sqrt((InPosition - ScopedLightPosition).LengthSquared());
        if (ScopedDistance >= ScopedSource.Radius)
        {
            continue;
        }
        if (ScopedSource.GetType() == LightSourceType::SpotArc)
        {
            const Vector2D ScopedToTarget = InPosition - ScopedLightPosition;
            const Vector2D ScopedDirection{std::cos(ScopedTransform->Rotation), std::sin(ScopedTransform->Rotation)};
            const float ScopedCross = ScopedDirection.X * ScopedToTarget.Y - ScopedDirection.Y * ScopedToTarget.X;
            const float ScopedDot = ScopedDirection.X * ScopedToTarget.X + ScopedDirection.Y * ScopedToTarget.Y;
            if (LightConeAttenuation(std::atan2(ScopedCross, ScopedDot), ScopedSource.Aperture(), ScopedSource.ConeSoftness) <=
                0.0f)
            {
                continue;
            }
        }
        ++ScopedCount;
    }
    // Legacy radial lights participate too, so a scene that mixes the two
    // component generations still reports a truthful light count.
    auto ScopedLegacyView = InRegistry.view<Light2DComponent>();
    for (auto [ScopedEntity, ScopedLegacy] : ScopedLegacyView.each())
    {
        if ((ScopedLegacy.Flags & kLight2DFlagEnabled) == 0 ||
            !EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassLightSources))
        {
            continue;
        }
        const auto* ScopedTransform = InRegistry.try_get<Transform2D>(ScopedEntity);
        if (ScopedTransform == nullptr)
        {
            continue;
        }
        const Vector2D ScopedLightPosition = ScopedTransform->Position + ScopedLegacy.Offset;
        if (std::sqrt((InPosition - ScopedLightPosition).LengthSquared()) < ScopedLegacy.Radius)
        {
            ++ScopedCount;
        }
    }
    return ScopedCount;
}

void ViewportPresenter::Prepare(Ecs::Registry& InRegistry) noexcept
{
    MStats = ViewportPresenterStats{};
    MTintCount = 0;
    MSpriteCount = 0;

    auto ScopedView = InRegistry.view<Transform2D, Sprite2D>();
    for (auto [ScopedEntity, ScopedTransform, ScopedSprite] : ScopedView.each())
    {
        if (!EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassSprites) ||
            InRegistry.all_of<Hidden>(ScopedEntity) || MSpriteCount >= kMaxPresentedSprites)
        {
            continue;
        }
        VisibleSprite& ScopedVisible = MSprites[MSpriteCount++];
        ScopedVisible.Owner = ScopedEntity;
        ScopedVisible.Center = ScopedTransform.Position;
        ScopedVisible.HalfExtents = ScopedSprite.Size * ScopedTransform.Scale * 0.5f;
        ScopedVisible.BaseColor = ScopedSprite.Color.rgba;
    }
    MStats.VisibleSprites = MSpriteCount;

    std::uint32_t ScopedColliderCount = 0;
    auto ScopedColliderView = InRegistry.view<Collider2D>();
    for (const Entity ScopedColliderEntity : ScopedColliderView)
    {
        (void)ScopedColliderEntity;
        ++ScopedColliderCount;
    }
    MStats.ColliderCount = ScopedColliderCount;
    if (MMode == ViewportMode::Standard || MMode == ViewportMode::UnlitWireframe)
    {
        return;
    }

    if (MMode == ViewportMode::CollisionOverlap)
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MSpriteCount; ++ScopedI)
        {
            TintEntry& ScopedTint = MTints[MTintCount++];
            ScopedTint.Owner = MSprites[ScopedI].Owner;
            ScopedTint.Color = PackColor(CollisionOverlayColor(InRegistry, MSprites[ScopedI].Owner));
        }
        MStats.TintedSprites = MTintCount;
        return;
    }

    if (MMode == ViewportMode::SdfVisualizer)
    {
        constexpr float kSurfaceBand = 16.0f;
        for (std::uint32_t ScopedI = 0; ScopedI < MSpriteCount; ++ScopedI)
        {
            TintEntry& ScopedTint = MTints[MTintCount++];
            ScopedTint.Owner = MSprites[ScopedI].Owner;
            if (MSdfField == nullptr || !MSdfField->Valid())
            {
                ScopedTint.Color = PackColor(Color32::FromBytes(80, 210, 220));
                continue;
            }
            const float ScopedDistance = MSdfField->Sample(MSprites[ScopedI].Center);
            if (ScopedDistance <= 0.0f)
            {
                ScopedTint.Color = PackColor(Color32::FromBytes(230, 60, 60));
            }
            else if (ScopedDistance < kSurfaceBand)
            {
                ScopedTint.Color = PackColor(Color32::FromBytes(250, 210, 70));
            }
            else
            {
                ScopedTint.Color = PackColor(Color32::FromBytes(80, 210, 220));
            }
        }
        MStats.TintedSprites = MTintCount;
        return;
    }

    // Complexity modes: per-sprite overlap and light counts feed one heat ramp.
    for (std::uint32_t ScopedI = 0; ScopedI < MSpriteCount; ++ScopedI)
    {
        std::uint32_t ScopedOverlap = 1;
        for (std::uint32_t ScopedJ = 0; ScopedJ < MSpriteCount; ++ScopedJ)
        {
            if (ScopedI == ScopedJ)
            {
                continue;
            }
            if (RangesOverlap(MSprites[ScopedI].Center, MSprites[ScopedI].HalfExtents, MSprites[ScopedJ].Center,
                              MSprites[ScopedJ].HalfExtents))
            {
                ++ScopedOverlap;
            }
        }
        const std::uint32_t ScopedLights =
            CountReachingLights(InRegistry, MSprites[ScopedI].Owner, MSprites[ScopedI].Center);
        MStats.PeakOverdraw = std::max(MStats.PeakOverdraw, ScopedOverlap);
        MStats.MaxLightsPerSprite = std::max(MStats.MaxLightsPerSprite, ScopedLights);
        TintEntry& ScopedTint = MTints[MTintCount++];
        ScopedTint.Owner = MSprites[ScopedI].Owner;
        if (MMode == ViewportMode::LightComplexity)
        {
            if (ScopedLights == 0)
            {
                ScopedTint.Color = PackColor(Color32::FromBytes(90, 140, 255));
                continue;
            }
            const float ScopedNormalized = static_cast<float>(ScopedLights - 1u) / 3.0f;
            ScopedTint.Color = PackColor(ComplexityHeatColor(std::max(ScopedNormalized, 0.0f)));
            continue;
        }
        const std::uint32_t ScopedCost = ScopedOverlap * (ScopedLights + 1u);
        const float ScopedNormalized =
            static_cast<float>(ScopedCost) / static_cast<float>(std::max(MComplexityBudget, 1u));
        ScopedTint.Color = PackColor(ComplexityHeatColor(ScopedNormalized));
    }
    MStats.TintedSprites = MTintCount;
}

bool ViewportPresenter::TintForEntity(Entity InEntity, std::uint32_t* OutColor) const noexcept
{
    if (OutColor == nullptr)
    {
        return false;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MTintCount; ++ScopedI)
    {
        if (MTints[ScopedI].Owner == InEntity)
        {
            *OutColor = MTints[ScopedI].Color;
            return true;
        }
    }
    return false;
}
}
