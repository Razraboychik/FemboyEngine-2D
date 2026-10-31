// FemboyEngine2D - viewport mode metadata, heat ramp and presenter behaviour.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/LightSource.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/Physics/SdfCollision.hpp"
#include "Engine/RHI/ViewportModes.hpp"
#include "Engine/RHI/ViewportPresenter.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

int TestMetadata()
{
    using namespace Fby;
    const std::span<const Rhi::ViewportModeDescriptor> ScopedModes = Rhi::AllViewportModes();
    FBY_CHECK(ScopedModes.size() == Rhi::kViewportModeCount);
    for (const Rhi::ViewportModeDescriptor& ScopedDescriptor : ScopedModes)
    {
        FBY_CHECK(ScopedDescriptor.Label != nullptr && ScopedDescriptor.Label[0] != '\0');
        FBY_CHECK(ScopedDescriptor.ShortLabel != nullptr && ScopedDescriptor.ShortLabel[0] != '\0');
        FBY_CHECK(ScopedDescriptor.Summary != nullptr && ScopedDescriptor.Summary[0] != '\0');
        FBY_CHECK(ScopedDescriptor.Paragraph1 != nullptr && ScopedDescriptor.Paragraph1[0] != '\0');
        FBY_CHECK(ScopedDescriptor.Paragraph2 != nullptr && ScopedDescriptor.Paragraph2[0] != '\0');
        FBY_CHECK(ScopedDescriptor.Legend != nullptr && ScopedDescriptor.LegendCount >= 3);
        Rhi::ViewportMode ScopedParsed = Rhi::ViewportMode::Standard;
        FBY_CHECK(Rhi::ParseViewportMode(ScopedDescriptor.ShortLabel, &ScopedParsed));
        FBY_CHECK(ScopedParsed == ScopedDescriptor.Mode);
        FBY_CHECK(&Rhi::DescribeViewportMode(ScopedDescriptor.Mode) == &ScopedDescriptor);
    }
    FBY_CHECK(Rhi::CycleViewportMode(Rhi::ViewportMode::Standard, -1) == Rhi::ViewportMode::SdfVisualizer);
    FBY_CHECK(Rhi::CycleViewportMode(Rhi::ViewportMode::SdfVisualizer, 1) == Rhi::ViewportMode::Standard);
    FBY_CHECK(Rhi::CycleViewportMode(Rhi::ViewportMode::Standard, static_cast<std::int32_t>(Rhi::kViewportModeCount)) ==
              Rhi::ViewportMode::Standard);
    FBY_CHECK(Rhi::ViewportModeIsOverlay(Rhi::ViewportMode::CollisionOverlap));
    FBY_CHECK(!Rhi::ViewportModeIsOverlay(Rhi::ViewportMode::Standard));
    FBY_CHECK(Rhi::ViewportModeNeedsUnlit(Rhi::ViewportMode::UnlitWireframe));
    FBY_CHECK(Rhi::ViewportModeNeedsSdf(Rhi::ViewportMode::SdfVisualizer));
    FBY_CHECK(!Rhi::ViewportModeNeedsSdf(Rhi::ViewportMode::LightComplexity));

    const Color32 ScopedCold = Rhi::ComplexityHeatColor(0.0f);
    const Color32 ScopedHot = Rhi::ComplexityHeatColor(1.0f);
    FBY_CHECK(ScopedCold.G() > 200 && ScopedCold.R() < 10);
    FBY_CHECK(ScopedHot.R() > 200 && ScopedHot.G() < 60);
    FBY_CHECK(Rhi::ComplexityHeatColor(-5.0f).rgba == Rhi::ComplexityHeatColor(0.0f).rgba);
    FBY_CHECK(Rhi::ComplexityHeatColor(5.0f).rgba == Rhi::ComplexityHeatColor(1.0f).rgba);
    return 0;
}

int TestPresenter()
{
    using namespace Fby;
    Ecs::Registry ScopedRegistry;
    auto ScopedAddSprite = [&ScopedRegistry](Vector2D InPosition, Color32 InColor)
    {
        const Entity ScopedEntity = ScopedRegistry.create();
        Transform2D ScopedTransform{};
        ScopedTransform.Position = InPosition;
        ScopedRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
        Sprite2D ScopedSprite{};
        ScopedSprite.Size = Vector2D{32.0f, 32.0f};
        ScopedSprite.Color = InColor;
        ScopedRegistry.emplace<Sprite2D>(ScopedEntity, ScopedSprite);
        return ScopedEntity;
    };

    const Entity ScopedComplex = ScopedAddSprite(Vector2D{0.0f, 0.0f}, Color32::White());
    CollisionPolicyComponent ScopedPolicy{};
    ScopedPolicy.SetMode(CollisionMode::ComplexStatic);
    ScopedRegistry.emplace<CollisionPolicyComponent>(ScopedComplex, ScopedPolicy);
    const Entity ScopedSimple = ScopedAddSprite(Vector2D{16.0f, 0.0f}, Color32::White());
    Collider2D ScopedCollider{};
    ScopedRegistry.emplace<Collider2D>(ScopedSimple, ScopedCollider);
    const Entity ScopedMasked = ScopedAddSprite(Vector2D{32.0f, 0.0f}, Color32::White());
    RenderMaskComponent ScopedMask{};
    ScopedMask.ExcludePass(RenderPassSprites);
    ScopedRegistry.emplace<RenderMaskComponent>(ScopedMasked, ScopedMask);

    FBY_CHECK(EntityAllowsRenderPass(ScopedRegistry, ScopedComplex, RenderPassSprites));
    FBY_CHECK(!EntityAllowsRenderPass(ScopedRegistry, ScopedMasked, RenderPassSprites));
    FBY_CHECK(EntityAllowsRenderPass(ScopedRegistry, ScopedMasked, RenderPassLighting));
    FBY_CHECK(EntityTimeScale(ScopedRegistry, ScopedComplex) == 1.0f);
    TimeScaleComponent ScopedFrozen{};
    ScopedFrozen.Scale = 0.0f;
    ScopedRegistry.emplace<TimeScaleComponent>(ScopedComplex, ScopedFrozen);
    FBY_CHECK(EntityTimeFrozen(ScopedRegistry, ScopedComplex));

    Rhi::ViewportPresenter ScopedPresenter;
    ScopedPresenter.SetMode(Rhi::ViewportMode::CollisionOverlap);
    ScopedPresenter.Prepare(ScopedRegistry);
    FBY_CHECK(ScopedPresenter.Stats().VisibleSprites == 2);
    FBY_CHECK(ScopedPresenter.Stats().TintedSprites == 2);
    std::uint32_t ScopedColor = 0;
    FBY_CHECK(ScopedPresenter.TintForEntity(ScopedComplex, &ScopedColor));
    FBY_CHECK(ScopedColor == Rhi::ViewportPresenter::CollisionModeColor(static_cast<std::uint32_t>(
                                   CollisionMode::ComplexStatic))
                                  .rgba);
    FBY_CHECK(ScopedPresenter.TintForEntity(ScopedSimple, &ScopedColor));
    FBY_CHECK(ScopedColor == Rhi::ViewportPresenter::CollisionModeColor(static_cast<std::uint32_t>(
                                   CollisionMode::SimpleDynamic))
                                  .rgba);
    FBY_CHECK(!ScopedPresenter.TintForEntity(ScopedMasked, &ScopedColor));

    // Shader complexity: two overlapping sprites at one light cost above the
    // zero point of the green start of the ramp.
    ScopedPresenter.SetMode(Rhi::ViewportMode::ShaderComplexity);
    ScopedPresenter.SetComplexityBudget(1);
    ScopedPresenter.Prepare(ScopedRegistry);
    FBY_CHECK(ScopedPresenter.TintForEntity(ScopedComplex, &ScopedColor));
    FBY_CHECK(ScopedColor == Rhi::ComplexityHeatColor(1.0f).rgba);

    // Light complexity: a nearby light makes the sprite warm, a far sprite with
    // no light keeps the ambient blue.
    Ecs::Registry ScopedLightRegistry;
    const Entity ScopedNear = ScopedLightRegistry.create();
    Transform2D ScopedNearTransform{};
    ScopedNearTransform.Position = Vector2D{0.0f, 0.0f};
    ScopedLightRegistry.emplace<Transform2D>(ScopedNear, ScopedNearTransform);
    Light2DTargetComponent ScopedTarget{};
    ScopedLightRegistry.emplace<Light2DTargetComponent>(ScopedNear, ScopedTarget);
    Sprite2D ScopedNearSprite{};
    ScopedLightRegistry.emplace<Sprite2D>(ScopedNear, ScopedNearSprite);
    const Entity ScopedFar = ScopedLightRegistry.create();
    Transform2D ScopedFarTransform{};
    ScopedFarTransform.Position = Vector2D{1000.0f, 0.0f};
    ScopedLightRegistry.emplace<Transform2D>(ScopedFar, ScopedFarTransform);
    Light2DTargetComponent ScopedFarTarget{};
    ScopedLightRegistry.emplace<Light2DTargetComponent>(ScopedFar, ScopedFarTarget);
    Sprite2D ScopedFarSprite{};
    ScopedLightRegistry.emplace<Sprite2D>(ScopedFar, ScopedFarSprite);
    const Entity ScopedLamp = ScopedLightRegistry.create();
    Transform2D ScopedLampTransform{};
    ScopedLampTransform.Position = Vector2D{24.0f, 0.0f};
    ScopedLightRegistry.emplace<Transform2D>(ScopedLamp, ScopedLampTransform);
    LightSourceComponent ScopedSource{};
    ScopedSource.Radius = 128.0f;
    ScopedLightRegistry.emplace<LightSourceComponent>(ScopedLamp, ScopedSource);
    ScopedPresenter.SetMode(Rhi::ViewportMode::LightComplexity);
    ScopedPresenter.Prepare(ScopedLightRegistry);
    FBY_CHECK(ScopedPresenter.TintForEntity(ScopedNear, &ScopedColor));
    FBY_CHECK(ScopedColor != Color32::FromBytes(90, 140, 255).rgba);
    FBY_CHECK(ScopedPresenter.TintForEntity(ScopedFar, &ScopedColor));
    FBY_CHECK(ScopedColor == Color32::FromBytes(90, 140, 255).rgba);

    // SDF visualizer: a negative field sample paints the sprite as solid.
    std::vector<float> ScopedSamples(64, -8.0f);
    SdfField ScopedField{};
    ScopedField.samples = ScopedSamples.data();
    ScopedField.width = 8;
    ScopedField.height = 8;
    ScopedField.origin = Vector2D{-64.0f, -64.0f};
    ScopedField.cellSize = Vector2D{16.0f, 16.0f};
    ScopedPresenter.SetMode(Rhi::ViewportMode::SdfVisualizer);
    ScopedPresenter.SetSdfField(&ScopedField);
    ScopedPresenter.Prepare(ScopedRegistry);
    FBY_CHECK(ScopedPresenter.TintForEntity(ScopedComplex, &ScopedColor));
    FBY_CHECK(ScopedColor == Color32::FromBytes(230, 60, 60).rgba);
    return 0;
}
}

int main()
{
    (void)TestMetadata();
    (void)TestPresenter();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "viewport_mode: %u failure(s)\n", GFailures);
        return 1;
    }
    std::printf("viewport_mode: ok\n");
    return 0;
}
