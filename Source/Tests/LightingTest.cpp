// FemboyEngine2D - 2D lighting tests: falloff, ambient, emissive, tonemap, SDF.
#include "Engine/ECS/Lighting.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cmath>
#include <cstdio>

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

[[nodiscard]] bool NearByte(std::uint32_t InColor, std::uint32_t InShift, int InExpected, int InTolerance = 2)
{
    const int ScopedValue = static_cast<int>((InColor >> InShift) & 0xFFu);
    return std::abs(ScopedValue - InExpected) <= InTolerance;
}

// The lighting system writes Light2DResultComponent; the render extract composes
// it with the authored sprite colour. The tests read the system output.
[[nodiscard]] std::uint32_t LightColor(Fby::Ecs::Registry& InRegistry, Fby::Entity InEntity)
{
    const auto* ScopedResult = InRegistry.try_get<Fby::Light2DResultComponent>(InEntity);
    return ScopedResult != nullptr ? ScopedResult->Color.rgba : 0xFFFFFFFFu;
}

void TestFalloff()
{
    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedSprite = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedSprite, Fby::Transform2D{Fby::Vector2D{100.0f, 0.0f}, {1.0f, 1.0f}, 0.0f});
    ScopedRegistry.emplace<Fby::Sprite2D>(ScopedSprite);
    Fby::Light2DTargetComponent ScopedTarget;
    ScopedTarget.BaseColor = Fby::Color32::White();
    ScopedRegistry.emplace<Fby::Light2DTargetComponent>(ScopedSprite, ScopedTarget);

    const Fby::Entity ScopedLight = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedLight, Fby::Transform2D{});
    Fby::Light2DComponent ScopedLightComponent;
    ScopedLightComponent.Radius = 200.0f;
    ScopedLightComponent.Intensity = 1.0f;
    ScopedRegistry.emplace<Fby::Light2DComponent>(ScopedLight, ScopedLightComponent);

    Fby::LightingSystem ScopedSystem;
    Fby::LightingSettings ScopedSettings;
    ScopedSettings.Ambient = Fby::Color32::FromBytes(0, 0, 0);
    ScopedSettings.AmbientIntensity = 0.0f;
    ScopedSettings.Tonemap = false;
    FBY_CHECK(ScopedSystem.Run(ScopedRegistry, ScopedSettings) == 1);
    FBY_CHECK(ScopedSystem.LightCount() == 1);

    const std::uint32_t ScopedColor = LightColor(ScopedRegistry, ScopedSprite);
    FBY_CHECK(NearByte(ScopedColor, 0, 64));
    FBY_CHECK(NearByte(ScopedColor, 8, 64));
    FBY_CHECK(NearByte(ScopedColor, 16, 64));
    FBY_CHECK(((ScopedColor >> 24) & 0xFFu) == 255u);

    // Directly under the light the sprite saturates to white.
    ScopedRegistry.get<Fby::Transform2D>(ScopedSprite).Position = Fby::Vector2D{0.0f, 0.0f};
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    const std::uint32_t ScopedBright = LightColor(ScopedRegistry, ScopedSprite);
    FBY_CHECK(NearByte(ScopedBright, 0, 255, 0));

    // Two lights add up and clamp.
    const Fby::Entity ScopedSecond = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedSecond, Fby::Transform2D{});
    ScopedRegistry.emplace<Fby::Light2DComponent>(ScopedSecond, ScopedLightComponent);
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(ScopedSystem.LightCount() == 2);
    FBY_CHECK(NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 255, 0));

    // A disabled light is ignored.
    ScopedRegistry.get<Fby::Light2DComponent>(ScopedLight).Flags = 0;
    ScopedRegistry.get<Fby::Light2DComponent>(ScopedSecond).Flags = 0;
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(ScopedSystem.LightCount() == 0);
    FBY_CHECK(NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 0, 0));
}

void TestAmbientEmissiveAndTonemap()
{
    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedSprite = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedSprite, Fby::Transform2D{});
    ScopedRegistry.emplace<Fby::Sprite2D>(ScopedSprite);
    Fby::Light2DTargetComponent ScopedTarget;
    ScopedTarget.BaseColor = Fby::Color32::FromBytes(128, 64, 32);
    ScopedRegistry.emplace<Fby::Light2DTargetComponent>(ScopedSprite, ScopedTarget);

    Fby::LightingSystem ScopedSystem;
    Fby::LightingSettings ScopedSettings;
    ScopedSettings.Ambient = Fby::Color32::FromBytes(0, 0, 0);
    ScopedSettings.AmbientIntensity = 0.0f;
    ScopedSettings.Tonemap = false;
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 0, 0));

    // White light exactly on the sprite: the authored colour comes back.
    const Fby::Entity ScopedLight = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedLight, Fby::Transform2D{});
    Fby::Light2DComponent ScopedLightComponent;
    ScopedLightComponent.Radius = 100.0f;
    ScopedRegistry.emplace<Fby::Light2DComponent>(ScopedLight, ScopedLightComponent);
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    const std::uint32_t ScopedLit = LightColor(ScopedRegistry, ScopedSprite);
    // The result is a light tint; composing it with the authored base restores
    // the authored colour.
    FBY_CHECK(NearByte(ScopedLit, 0, 255, 0));
    FBY_CHECK(NearByte(ScopedLit, 8, 255, 0));
    FBY_CHECK(NearByte(ScopedLit, 16, 255, 0));
    FBY_CHECK(Fby::LightingSystem::Compose(ScopedTarget.BaseColor, Fby::Color32{ScopedLit}).rgba ==
              ScopedTarget.BaseColor.rgba);

    // Emissive sprites ignore the lighting entirely.
    ScopedRegistry.get<Fby::Light2DTargetComponent>(ScopedSprite).Emissive = 1.0f;
    ScopedRegistry.get<Fby::Transform2D>(ScopedLight).Position = Fby::Vector2D{10000.0f, 0.0f};
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 255, 0));

    // Tonemapping maps 1.0 to 0.5.
    ScopedRegistry.get<Fby::Light2DTargetComponent>(ScopedSprite).Emissive = 0.0f;
    ScopedRegistry.get<Fby::Transform2D>(ScopedLight).Position = Fby::Vector2D{};
    ScopedSettings.Tonemap = true;
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 128, 2));

    // An entity without the target component gets no light result and its
    // sprite colour is never touched by the lighting system.
    const Fby::Entity ScopedPlain = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedPlain, Fby::Transform2D{});
    Fby::Sprite2D ScopedPlainSprite;
    ScopedPlainSprite.Color = Fby::Color32::FromBytes(10, 20, 30);
    ScopedRegistry.emplace<Fby::Sprite2D>(ScopedPlain, ScopedPlainSprite);
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(ScopedRegistry.try_get<Fby::Light2DResultComponent>(ScopedPlain) == nullptr);
    FBY_CHECK(ScopedRegistry.get<Fby::Sprite2D>(ScopedPlain).Color.rgba ==
              Fby::Color32::FromBytes(10, 20, 30).rgba);
}

// A wall SDF used by the shadow tests: solid for x in [wallMin, wallMax] across
// the whole height, with world cell size 16 and origin (0, 0).
struct TestSdf
{
    static constexpr std::uint32_t kWidth = 48;
    static constexpr std::uint32_t kHeight = 48;

    std::array<float, kWidth * kHeight> Samples{};
    Fby::SdfField Field;

    void Initialize(float InWallMinX, float InWallMaxX)
    {
        for (std::uint32_t ScopedY = 0; ScopedY < kHeight; ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < kWidth; ++ScopedX)
            {
                const float ScopedWorldX = (static_cast<float>(ScopedX) + 0.5f) * 16.0f;
                const float ScopedWorldY = (static_cast<float>(ScopedY) + 0.5f) * 16.0f;
                const float ScopedCenterX = (InWallMinX + InWallMaxX) * 0.5f;
                const float ScopedHalfX = (InWallMaxX - InWallMinX) * 0.5f;
                const float ScopedCenterY = 384.0f;
                const float ScopedHalfY = 384.0f;
                const float ScopedQx = std::fabs(ScopedWorldX - ScopedCenterX) - ScopedHalfX;
                const float ScopedQy = std::fabs(ScopedWorldY - ScopedCenterY) - ScopedHalfY;
                const float ScopedOutX = std::max(ScopedQx, 0.0f);
                const float ScopedOutY = std::max(ScopedQy, 0.0f);
                const float ScopedOutside = std::sqrt(ScopedOutX * ScopedOutX + ScopedOutY * ScopedOutY);
                const float ScopedInside = std::min(std::max(ScopedQx, ScopedQy), 0.0f);
                Samples[static_cast<std::size_t>(ScopedY) * kWidth + ScopedX] = ScopedOutside + ScopedInside;
            }
        }
        Field.samples = Samples.data();
        Field.width = kWidth;
        Field.height = kHeight;
        Field.origin = Fby::Vector2D{0.0f, 0.0f};
        Field.cellSize = Fby::Vector2D{16.0f, 16.0f};
        Field.outsideDistance = 1000.0f;
    }
};

void TestSdfShadows()
{
    TestSdf ScopedWall;
    ScopedWall.Initialize(300.0f, 340.0f);
    FBY_CHECK(ScopedWall.Field.Valid());

    // Sprite between a wall and a lamp behind it.
    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedSprite = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedSprite,
                                             Fby::Transform2D{Fby::Vector2D{100.0f, 300.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedRegistry.emplace<Fby::Sprite2D>(ScopedSprite);
    Fby::Light2DTargetComponent ScopedTarget;
    ScopedRegistry.emplace<Fby::Light2DTargetComponent>(ScopedSprite, ScopedTarget);

    const Fby::Entity ScopedLight = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedLight,
                                             Fby::Transform2D{Fby::Vector2D{520.0f, 300.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    Fby::Light2DComponent ScopedLightComponent;
    ScopedLightComponent.Radius = 900.0f;
    ScopedLightComponent.Intensity = 1.0f;
    ScopedRegistry.emplace<Fby::Light2DComponent>(ScopedLight, ScopedLightComponent);

    Fby::LightingSystem ScopedSystem;
    Fby::LightingSettings ScopedSettings;
    ScopedSettings.Ambient = Fby::Color32::FromBytes(0, 0, 0);
    ScopedSettings.AmbientIntensity = 0.0f;
    ScopedSettings.Tonemap = false;
    ScopedSettings.SdfShadows = true;
    ScopedSettings.SdfMaxSteps = 48;
    ScopedSettings.SdfSoftness = 8.0f;
    ScopedSettings.SdfAmbientOcclusion = 0.0f;

    // No field: the distance falloff is all there is.
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    const std::uint32_t ScopedDistanceOnly = LightColor(ScopedRegistry, ScopedSprite);
    FBY_CHECK(ScopedSystem.ShadowedSampleCount() == 0);
    FBY_CHECK(!NearByte(ScopedDistanceOnly, 0, 0, 1));

    // With the wall the lamp is occluded.
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings, &ScopedWall.Field);
    const std::uint32_t ScopedShadowed = LightColor(ScopedRegistry, ScopedSprite);
    FBY_CHECK(ScopedSystem.ShadowedSampleCount() > 0);
    FBY_CHECK(NearByte(ScopedShadowed, 0, 0, 3));

    // A lamp on the open side lights the sprite.
    ScopedRegistry.get<Fby::Transform2D>(ScopedLight).Position = Fby::Vector2D{100.0f, 620.0f};
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings, &ScopedWall.Field);
    const std::uint32_t ScopedOpen = LightColor(ScopedRegistry, ScopedSprite);
    FBY_CHECK(!NearByte(ScopedOpen, 0, 0, 3));

    // Ambient occlusion darkens a sprite that sits in the wall's crease.
    ScopedSettings.SdfAmbientOcclusion = 1.0f;
    ScopedSettings.SdfOcclusionRadius = 64.0f;
    ScopedRegistry.get<Fby::Transform2D>(ScopedSprite).Position = Fby::Vector2D{260.0f, 300.0f};
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings, &ScopedWall.Field);
    const std::uint32_t ScopedNearWall = LightColor(ScopedRegistry, ScopedSprite);
    ScopedRegistry.get<Fby::Transform2D>(ScopedSprite).Position = Fby::Vector2D{60.0f, 300.0f};
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings, &ScopedWall.Field);
    const std::uint32_t ScopedFarFromWall = LightColor(ScopedRegistry, ScopedSprite);
    const auto ScopedRed = [](std::uint32_t InColor)
    {
        return static_cast<int>(InColor & 0xFFu);
    };
    FBY_CHECK(ScopedRed(ScopedNearWall) < ScopedRed(ScopedFarFromWall));

    // Shadows can be disabled without dropping the AO path.
    ScopedSettings.SdfShadows = false;
    ScopedSettings.SdfAmbientOcclusion = 0.0f;
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings, &ScopedWall.Field);
    FBY_CHECK(ScopedSystem.ShadowedSampleCount() == 0);
}

void TestDynamicOccluders()
{
    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedSprite = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(
        ScopedSprite, Fby::Transform2D{Fby::Vector2D{100.0f, 300.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedRegistry.emplace<Fby::Sprite2D>(ScopedSprite);
    ScopedRegistry.emplace<Fby::Light2DTargetComponent>(ScopedSprite, Fby::Light2DTargetComponent{});

    const Fby::Entity ScopedLight = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(
        ScopedLight, Fby::Transform2D{Fby::Vector2D{500.0f, 300.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    Fby::Light2DComponent ScopedLightComponent;
    ScopedLightComponent.Radius = 900.0f;
    ScopedRegistry.emplace<Fby::Light2DComponent>(ScopedLight, ScopedLightComponent);

    Fby::LightingSystem ScopedSystem;
    Fby::LightingSettings ScopedSettings;
    ScopedSettings.Ambient = Fby::Color32::FromBytes(0, 0, 0);
    ScopedSettings.AmbientIntensity = 0.0f;
    ScopedSettings.Tonemap = false;
    ScopedSettings.SdfShadows = true;
    ScopedSettings.SdfMaxSteps = 48;
    ScopedSettings.SdfSoftness = 8.0f;
    ScopedSettings.SdfAmbientOcclusion = 0.0f;

    // No static field and no occluders: distance falloff only.
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 79, 3));

    // A dynamic circle between the sprite and the lamp casts a shadow.
    const Fby::Entity ScopedCircle = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(
        ScopedCircle, Fby::Transform2D{Fby::Vector2D{300.0f, 300.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    Fby::Light2DOccluderComponent ScopedOccluder;
    ScopedOccluder.Radius = 24.0f;
    ScopedRegistry.emplace<Fby::Light2DOccluderComponent>(ScopedCircle, ScopedOccluder);
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 0, 2));
    FBY_CHECK(ScopedSystem.ShadowedSampleCount() > 0);

    // Moving the caster away restores the light: the field is re-gathered per frame.
    ScopedRegistry.get<Fby::Transform2D>(ScopedCircle).Position = Fby::Vector2D{300.0f, 900.0f};
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(!NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 0, 2));

    // A box caster works the same way, using half extents.
    ScopedRegistry.get<Fby::Transform2D>(ScopedCircle).Position = Fby::Vector2D{300.0f, 300.0f};
    Fby::Light2DOccluderComponent ScopedBox;
    ScopedBox.HalfExtents = Fby::Vector2D{20.0f, 120.0f};
    ScopedBox.Flags = Fby::kLight2DOccluderFlagBox;
    ScopedRegistry.emplace_or_replace<Fby::Light2DOccluderComponent>(ScopedCircle, ScopedBox);
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 0, 2));

    // A disabled caster is skipped, and an entity that casts a shadow does not
    // shadow itself.
    ScopedBox.Flags = Fby::kLight2DOccluderFlagBox | Fby::kLight2DOccluderFlagDisabled;
    ScopedRegistry.emplace_or_replace<Fby::Light2DOccluderComponent>(ScopedCircle, ScopedBox);
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(!NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 0, 2));
    Fby::Light2DOccluderComponent ScopedSelfOccluder;
    ScopedSelfOccluder.Radius = 24.0f;
    ScopedRegistry.emplace<Fby::Light2DOccluderComponent>(ScopedSprite, ScopedSelfOccluder);
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(!NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 0, 2));
    ScopedBox.Flags = Fby::kLight2DOccluderFlagBox;
    ScopedRegistry.emplace_or_replace<Fby::Light2DOccluderComponent>(ScopedCircle, ScopedBox);
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 0, 2));
    ScopedSettings.SdfShadows = false;
    (void)ScopedSystem.Run(ScopedRegistry, ScopedSettings);
    FBY_CHECK(!NearByte(LightColor(ScopedRegistry, ScopedSprite), 0, 0, 2));
}

void TestSdfLightingFrame()
{
    // A tiny 8x8 field so the frame builder has a valid view.
    std::array<float, 64> ScopedSamples{};
    Fby::SdfField ScopedField;
    ScopedField.samples = ScopedSamples.data();
    ScopedField.width = 8;
    ScopedField.height = 8;
    ScopedField.origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedField.cellSize = Fby::Vector2D{16.0f, 16.0f};

    Fby::Ecs::Registry ScopedRegistry;
    Fby::Rhi::SdfLightingFrame ScopedFrame;
    FBY_CHECK(!Fby::BuildSdfLightingFrame(ScopedRegistry, &ScopedField, &ScopedFrame));

    const Fby::Entity ScopedSun = ScopedRegistry.create();
    Fby::SdfSunComponent ScopedSunComponent;
    ScopedSunComponent.Direction = Fby::Vector2D{0.0f, -1.0f};
    ScopedSunComponent.Softness = 12.0f;
    ScopedSunComponent.Ambient = 0.4f;
    ScopedSunComponent.Steps = 24;
    ScopedRegistry.emplace<Fby::SdfSunComponent>(ScopedSun, ScopedSunComponent);

    const Fby::Entity ScopedCaster = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(
        ScopedCaster, Fby::Transform2D{Fby::Vector2D{32.0f, 48.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    Fby::Light2DOccluderComponent ScopedOccluder;
    ScopedOccluder.HalfExtents = Fby::Vector2D{10.0f, 20.0f};
    ScopedOccluder.Flags = Fby::kLight2DOccluderFlagBox;
    ScopedRegistry.emplace<Fby::Light2DOccluderComponent>(ScopedCaster, ScopedOccluder);
    const Fby::Entity ScopedDisabled = ScopedRegistry.create();
    Fby::Light2DOccluderComponent ScopedDisabledOccluder;
    ScopedDisabledOccluder.Flags = Fby::kLight2DOccluderFlagDisabled;
    ScopedRegistry.emplace<Fby::Light2DOccluderComponent>(ScopedDisabled, ScopedDisabledOccluder);

    FBY_CHECK(Fby::BuildSdfLightingFrame(ScopedRegistry, &ScopedField, &ScopedFrame));
    FBY_CHECK(ScopedFrame.enabled);
    FBY_CHECK(ScopedFrame.field == ScopedSamples.data());
    FBY_CHECK(ScopedFrame.width == 8 && ScopedFrame.height == 8);
    FBY_CHECK(ScopedFrame.steps == 24);
    FBY_CHECK(ScopedFrame.softness == 12.0f);
    FBY_CHECK(ScopedFrame.ambient == 0.4f);
    FBY_CHECK(ScopedFrame.occluderCount == 1);
    FBY_CHECK(ScopedFrame.occluders[0].Extents.Z == 1.0f);
    FBY_CHECK(ScopedFrame.occluders[0].Shape.X == 32.0f);
    FBY_CHECK(ScopedFrame.fieldVersion != 0);

    // A disabled sun switches the GPU path off again.
    ScopedRegistry.get<Fby::SdfSunComponent>(ScopedSun).Flags = Fby::kSdfSunFlagDisabled;
    FBY_CHECK(!Fby::BuildSdfLightingFrame(ScopedRegistry, &ScopedField, &ScopedFrame));
}
}

int main()
{
    TestFalloff();
    TestAmbientEmissiveAndTonemap();
    TestSdfShadows();
    TestDynamicOccluders();
    TestSdfLightingFrame();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u lighting test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("lighting tests passed\n");
    return 0;
}
