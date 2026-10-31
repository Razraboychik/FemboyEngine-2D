// FemboyEngine2D - UI, accessibility and camera tests.
#include "Engine/UI/Accessibility.hpp"
#include "Engine/UI/AccessibilityOverlay.hpp"
#include "Engine/Audio/Limiter.hpp"
#include "Engine/Core/Pcg32.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Particles.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/RHI/CameraStack.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/UI/Hypertext.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>

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

[[nodiscard]] bool Near(float InA, float InB, float InTolerance = 0.001f) noexcept
{
    return std::fabs(InA - InB) <= InTolerance;
}
}

int main()
{
    using namespace Fby;

    Ui::WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{1280.0f, 720.0f});
    const Vector2D ScopedBaseOffset = ScopedCanvas.OledOffset();
    FBY_CHECK(ScopedBaseOffset.X == 0.0f && ScopedBaseOffset.Y == 0.0f);
    ScopedCanvas.AdvanceOledShift(59.9f);
    FBY_CHECK(ScopedCanvas.OledOffset().X == 0.0f);
    ScopedCanvas.AdvanceOledShift(0.2f);
    FBY_CHECK(ScopedCanvas.OledOffset().X == 2.0f && ScopedCanvas.OledOffset().Y == 0.0f);
    for (int ScopedI = 0; ScopedI < 8; ++ScopedI)
    {
        ScopedCanvas.AdvanceOledShift(60.0f);
    }
    FBY_CHECK(ScopedCanvas.OledOffset().X == 0.0f && ScopedCanvas.OledOffset().Y == 0.0f);
    // every orbit point is 1-2 integer pixels from the origin
    for (std::uint32_t ScopedStep = 0; ScopedStep < Ui::OledShiftState::kCycleSteps; ++ScopedStep)
    {
        const Vector2D ScopedPoint = Ui::OledOrbitOffset(ScopedStep);
        FBY_CHECK(std::fabs(ScopedPoint.X) <= 2.0f && std::fabs(ScopedPoint.Y) <= 2.0f);
        FBY_CHECK(ScopedPoint.X == std::floor(ScopedPoint.X) && ScopedPoint.Y == std::floor(ScopedPoint.Y));
    }

    Ui::WidgetDesc ScopedPanel{};
    ScopedPanel.kind = Ui::WidgetKind::Panel;
    ScopedPanel.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedPanel.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedPanel.offsetMin = Vector2D{10.0f, 10.0f};
    ScopedPanel.offsetMax = Vector2D{30.0f, 30.0f};
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedPanel) == 0);
    ScopedCanvas.oled.offset = Vector2D{2.0f, 0.0f};

    Ecs::Registry ScopedRegistry;
    JobSystem ScopedJobs(1);
    Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    std::array<Rhi::RenderItem, 32> ScopedItems{};
    std::array<Rhi::RenderKey, 32> ScopedKeys{};
    std::array<Rhi::DrawBatch, 4> ScopedBatches{};
    Ui::FrameOutput ScopedOutput{ScopedItems, ScopedKeys, ScopedBatches, 0};
    FBY_CHECK(Ui::WidgetCanvasSystem::BuildFrame(ScopedContext, ScopedCanvas, ScopedOutput) == Ui::BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 1);
    FBY_CHECK(ScopedOutput.batchesWritten == 1);
    FBY_CHECK(Near(ScopedItems[0].position.X, 22.0f));
    FBY_CHECK(Near(ScopedItems[0].position.Y, 20.0f));

    Ui::HypertextPool ScopedHypertext;
    const Ui::HypertextComponent ScopedText =
        ScopedHypertext.Intern("Press [E] to open the [inventory](ui://inventory) panel", 7, Ui::HypertextNoLocalize);
    FBY_CHECK(ScopedText.PlainLength > 0);
    FBY_CHECK((ScopedText.Flags & Ui::HypertextNoLocalize) != 0);
    const Ui::HypertextView ScopedView = ScopedHypertext.View(ScopedText);
    FBY_CHECK(ScopedView.text.find("inventory") != std::string_view::npos);
    const std::size_t ScopedLinkOffset = ScopedView.text.find("inventory");
    const Ui::HyperlinkRange* ScopedLink =
        ScopedHypertext.LinkAt(ScopedView, static_cast<std::uint32_t>(ScopedLinkOffset));
    FBY_CHECK(ScopedLink != nullptr);
    FBY_CHECK(ScopedHypertext.Target(*ScopedLink) == "ui://inventory");
    FBY_CHECK((ScopedLink->flags & Ui::HypertextOpenExternal) != 0);
    std::string_view ScopedTarget{};
    std::uint16_t ScopedLinkFlags = 0;
    FBY_CHECK(Ui::HyperlinkClicked(ScopedView, static_cast<std::uint32_t>(ScopedLinkOffset), &ScopedTarget,
                                   &ScopedLinkFlags, ScopedHypertext));
    FBY_CHECK(ScopedTarget == "ui://inventory");
    const Ui::HypertextComponent ScopedLiteral = ScopedHypertext.Intern("broken [link]( mark", 0, 0);
    FBY_CHECK(ScopedHypertext.View(ScopedLiteral).links.empty());
    FBY_CHECK(ScopedHypertext.View(ScopedLiteral).text.find("broken") == 0);

    NarrativeLog ScopedLog;
    FBY_CHECK(ScopedLog.Push(1, entt::null, Vector2D{1.0f, 2.0f}, "first"));
    FBY_CHECK(ScopedLog.Push(2, entt::null, Vector2D{3.0f, 4.0f}, "second"));
    NarrativeEvent ScopedEvent{};
    FBY_CHECK(ScopedLog.Pop(ScopedEvent));
    FBY_CHECK(ScopedEvent.code == 1);
    const std::string_view ScopedEventText{ScopedEvent.text, ScopedEvent.textLength};
    FBY_CHECK(ScopedEventText == "first");
    FBY_CHECK(ScopedLog.Count() == 1);

    AccessibilityKernel ScopedAccessibility;
    ScopedAccessibility.SetBlindMode(true);
    ScopedAccessibility.SetDeafMode(true);
    ScopedAccessibility.SetFlashGuard(true, 0.2f, 0.1f);
    ScopedAccessibility.SetColorVision(ColorVisionDeficiency::Deuteranopia, 0.75f);
    Rhi::FramePacket ScopedPacket{};
    ScopedAccessibility.ApplyToFrame(ScopedPacket);
    FBY_CHECK((ScopedPacket.accessibility.flags & Rhi::AccessibilityBlind) != 0);
    FBY_CHECK((ScopedPacket.accessibility.flags & Rhi::AccessibilityDeaf) != 0);
    FBY_CHECK((ScopedPacket.accessibility.flags & Rhi::AccessibilityFlashGuard) != 0);
    FBY_CHECK(ScopedPacket.accessibility.colorVision ==
              static_cast<std::uint32_t>(ColorVisionDeficiency::Deuteranopia));

    SoundTrigger ScopedTrigger{};
    ScopedTrigger.cueId = 9;
    ScopedTrigger.duration = 2.0f;
    ScopedAccessibility.PublishSoundTrigger(ScopedTrigger, "A door opens");
    FBY_CHECK(ScopedAccessibility.Subtitles().Count() == 1);
    Ui::WidgetCanvas ScopedSubtitleCanvas;
    FBY_CHECK(ScopedAccessibility.BuildSubtitleCanvas(ScopedSubtitleCanvas));
    // Panel + one label
    FBY_CHECK(ScopedSubtitleCanvas.widgetCount == 2);
    ScopedAccessibility.Subtitles().Tick(3.0f);
    FBY_CHECK(ScopedAccessibility.Subtitles().Count() == 0);

    // Caption composition: overlay items follow the game items, their batches are
    // offset by the game item count and carry the font texture array.
    Rhi::RenderItem ScopedGameItems[2]{};
    Rhi::RenderKey ScopedGameKeys[2]{Rhi::MakeRenderKey(0, 1.0f), Rhi::MakeRenderKey(0, 2.0f)};
    Rhi::DrawBatch ScopedGameBatches[1]{Rhi::DrawBatch{0, 2, 0, 0}};
    Rhi::FramePacket ScopedComposePacket;
    ScopedComposePacket.items = ScopedGameItems;
    ScopedComposePacket.keys = ScopedGameKeys;
    ScopedComposePacket.batches = ScopedGameBatches;

    Rhi::RenderItem ScopedOverlayItems[2]{};
    ScopedOverlayItems[0].color = 0x11223344u;
    ScopedOverlayItems[1].color = 0x55667788u;
    const Rhi::RenderKey ScopedOverlayKeys[2]{Rhi::MakeRenderKey(200, 0.0f), Rhi::MakeRenderKey(200, 1.0f)};
    const Rhi::DrawBatch ScopedOverlayBatches[2]{Rhi::DrawBatch{0, 1, 0, 0}, Rhi::DrawBatch{1, 1, 0, 0}};

    Rhi::RenderItem ScopedCombinedItems[8]{};
    Rhi::RenderKey ScopedCombinedKeys[8]{};
    Rhi::DrawBatch ScopedCombinedBatches[4]{};
    FBY_CHECK(ComposeUiOverlay(ScopedComposePacket, ScopedOverlayItems, ScopedOverlayKeys, ScopedOverlayBatches, 7u,
                               ScopedCombinedItems, ScopedCombinedKeys, ScopedCombinedBatches));
    FBY_CHECK(ScopedComposePacket.items.size() == 4);
    FBY_CHECK(ScopedComposePacket.batches.size() == 3);
    FBY_CHECK(ScopedComposePacket.items[2].color == 0x11223344u);
    FBY_CHECK(ScopedComposePacket.batches[1].firstItem == 2);
    FBY_CHECK(ScopedComposePacket.batches[1].textureArray == 7u);
    FBY_CHECK(!ScopedComposePacket.instanceRangesAreDeltas);

    // Too little storage is refused and the packet stays as it was.
    const std::size_t ScopedItemsBefore = ScopedComposePacket.items.size();
    Rhi::RenderItem ScopedTinyItems[3]{};
    Rhi::RenderKey ScopedTinyKeys[3]{};
    Rhi::DrawBatch ScopedTinyBatches[4]{};
    FBY_CHECK(!ComposeUiOverlay(ScopedComposePacket, ScopedOverlayItems, ScopedOverlayKeys, ScopedOverlayBatches, 0u,
                                ScopedTinyItems, ScopedTinyKeys, ScopedTinyBatches));
    FBY_CHECK(ScopedComposePacket.items.size() == ScopedItemsBefore);

    // An empty overlay leaves the frame untouched.
    FBY_CHECK(ComposeUiOverlay(ScopedComposePacket, {}, {}, {}, 0u, ScopedCombinedItems, ScopedCombinedKeys,
                               ScopedCombinedBatches));
    FBY_CHECK(ScopedComposePacket.items.size() == ScopedItemsBefore);

    TimedSoundCue ScopedTimedCue{};
    ScopedTimedCue.timeSeconds = 1.0f;
    ScopedTimedCue.trigger.cueId = 12;
    ScopedTimedCue.trigger.duration = 2.0f;
    const char ScopedBang[] = "Bang";
    for (std::size_t ScopedI = 0; ScopedI + 1 < sizeof(ScopedBang); ++ScopedI)
    {
        ScopedTimedCue.text[ScopedI] = ScopedBang[ScopedI];
    }
    ScopedTimedCue.textLength = 4;
    const TimedSoundCue ScopedTimeline[] = {ScopedTimedCue};
    ScopedAccessibility.ProcessTimeline(ScopedTimeline, 0.5f, 1.2f);
    FBY_CHECK(ScopedAccessibility.Subtitles().Count() == 1);
    ScopedAccessibility.Subtitles().Clear();

    const float ScopedContrast = ContrastRatio(Color32::White(), Color32::Black());
    FBY_CHECK(Near(ScopedContrast, 21.0f, 0.1f));
    const ColorMatrix3 ScopedDeuteranopia = ColorVisionMatrix(ColorVisionDeficiency::Deuteranopia);
    const Color32 ScopedCorrected = ApplyColorVision(Color32::FromBytes(220, 30, 30), ScopedDeuteranopia, 1.0f);
    FBY_CHECK(ScopedCorrected.rgba != 0);

    // One profile drives the kernel: defaults are the 1.0 policy, ApplyProfile
    // clamps hostile values, and the frame packet mirrors the runtime section.
    const AccessibilityProfile ScopedDefaultProfile = DefaultAccessibilityProfile();
    FBY_CHECK(AccessibilityHasFlag(ScopedDefaultProfile.runtime.flags, AccessibilityFlashGuard));
    FBY_CHECK(!AccessibilityHasFlag(ScopedDefaultProfile.runtime.flags, AccessibilityBlindMode));
    FBY_CHECK(ScopedDefaultProfile.presentation.oledPixelShift);
    FBY_CHECK(Near(ScopedDefaultProfile.presentation.motionScale, 1.0f));
    FBY_CHECK(Near(ScopedDefaultProfile.presentation.minimumContrastRatio, 4.5f));

    AccessibilityProfile ScopedHostile = ScopedDefaultProfile;
    ScopedHostile.runtime.colorVision = ColorVisionDeficiency::Deuteranopia;
    ScopedHostile.runtime.colorVisionStrength = 4.0f;
    ScopedHostile.runtime.flashThreshold = -1.0f;
    ScopedHostile.presentation.motionScale = -3.0f;
    ScopedHostile.presentation.minimumContrastRatio = 99.0f;
    AccessibilitySetFlag(&ScopedHostile.runtime.flags, AccessibilityBlindMode, true);
    AccessibilitySetFlag(&ScopedHostile.runtime.flags, AccessibilityColorVisionMode, true);
    AccessibilityKernel ScopedProfileKernel;
    ScopedProfileKernel.ApplyProfile(ScopedHostile);
    FBY_CHECK(Near(ScopedProfileKernel.Settings().colorVisionStrength, 1.0f));
    FBY_CHECK(ScopedProfileKernel.Settings().flashThreshold >= 0.02f);
    FBY_CHECK(Near(ScopedProfileKernel.Profile().presentation.motionScale, 0.0f));
    FBY_CHECK(ScopedProfileKernel.Profile().presentation.minimumContrastRatio <= 21.0f);
    FBY_CHECK(ScopedProfileKernel.BlindMode());

    Rhi::FramePacket ScopedProfilePacket;
    ScopedProfileKernel.ApplyToFrame(ScopedProfilePacket);
    FBY_CHECK((ScopedProfilePacket.accessibility.flags & Rhi::AccessibilityBlind) != 0);
    FBY_CHECK((ScopedProfilePacket.accessibility.flags & Rhi::AccessibilityColorVision) != 0);
    FBY_CHECK(ScopedProfilePacket.accessibility.colorVision == 2u);

    // ApplyPostFx maps the same profile onto the post-process policy the
    // composite pass reads: flags, matrix id and gain travel together.
    Rhi::PostFxSettings ScopedPostFx;
    ScopedPostFx.flags = Rhi::PostFxBloom;
    ScopedProfileKernel.ApplyPostFx(ScopedPostFx);
    FBY_CHECK((ScopedPostFx.flags & Rhi::PostFxFlashGuard) != 0);
    FBY_CHECK((ScopedPostFx.flags & Rhi::PostFxColorVision) != 0);
    FBY_CHECK((ScopedPostFx.flags & Rhi::PostFxBloom) != 0);
    FBY_CHECK(ScopedPostFx.colorVision == 2u);
    FBY_CHECK(Near(ScopedPostFx.flashGuardGain, 1.0f));
    // The residual gain is the guard's live output, so it only drops after a
    // detected flash instead of dimming every calm frame.
    ScopedProfileKernel.Guard().Configure(0.2f, 0.1f);
    (void)ScopedProfileKernel.Guard().Evaluate(0.1f, 1.0f / 60.0f);
    (void)ScopedProfileKernel.Guard().Evaluate(1.0f, 1.0f / 60.0f);
    ScopedProfileKernel.ApplyPostFx(ScopedPostFx);
    FBY_CHECK(ScopedPostFx.flashGuardGain < 1.0f);

    // A profile without the two flags clears them and leaves the rest alone.
    AccessibilityProfile ScopedPlain = DefaultAccessibilityProfile();
    AccessibilitySetFlag(&ScopedPlain.runtime.flags, AccessibilityFlashGuard, false);
    AccessibilityKernel ScopedPlainKernel;
    ScopedPlainKernel.ApplyProfile(ScopedPlain);
    ScopedPlainKernel.ApplyPostFx(ScopedPostFx);
    FBY_CHECK((ScopedPostFx.flags & Rhi::PostFxFlashGuard) == 0);
    FBY_CHECK((ScopedPostFx.flags & Rhi::PostFxColorVision) == 0);
    FBY_CHECK((ScopedPostFx.flags & Rhi::PostFxBloom) != 0);

    FlashGuard ScopedGuard;
    ScopedGuard.Configure(0.2f, 0.1f);
    (void)ScopedGuard.Evaluate(0.1f, 1.0f / 60.0f);
    const float ScopedFlashGain = ScopedGuard.Evaluate(1.0f, 1.0f / 60.0f);
    FBY_CHECK(ScopedFlashGain < 0.5f);

    // CPU colour fallback: a calm default profile leaves the frame untouched, an
    // explicit colour-vision profile corrects the clear colour and item colours.
    Rhi::RenderItem ScopedCpuItems[2]{};
    ScopedCpuItems[0].color = 0xFF3A6CC8u;
    ScopedCpuItems[1].color = 0xFF20C060u;
    const std::uint32_t ScopedCpuFirst = ScopedCpuItems[0].color;
    Color32 ScopedCpuClear = Color32::FromBytes(10, 20, 30);
    const std::uint32_t ScopedCpuClearBefore = ScopedCpuClear.rgba;

    AccessibilityKernel ScopedCpuOffKernel;
    ScopedCpuOffKernel.ApplyProfile(DefaultAccessibilityProfile());
    ScopedCpuOffKernel.ApplyCpuColorFallback(std::span<Rhi::RenderItem>(ScopedCpuItems), ScopedCpuClear);
    FBY_CHECK(ScopedCpuItems[0].color == ScopedCpuFirst);
    FBY_CHECK(ScopedCpuClear.rgba == ScopedCpuClearBefore);

    AccessibilityProfile ScopedCpuProfile = DefaultAccessibilityProfile();
    ScopedCpuProfile.runtime.colorVision = ColorVisionDeficiency::Deuteranopia;
    AccessibilitySetFlag(&ScopedCpuProfile.runtime.flags, AccessibilityColorVisionMode, true);
    AccessibilityKernel ScopedCpuOnKernel;
    ScopedCpuOnKernel.ApplyProfile(ScopedCpuProfile);
    ScopedCpuOnKernel.ApplyCpuColorFallback(std::span<Rhi::RenderItem>(ScopedCpuItems), ScopedCpuClear);
    FBY_CHECK(ScopedCpuItems[0].color != ScopedCpuFirst);
    FBY_CHECK(ScopedCpuClear.rgba != ScopedCpuClearBefore);

    const Entity ScopedTargetEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedTargetEntity,
                                        Transform2D{Vector2D{400.0f, 240.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    const Entity ScopedCameraEntity = ScopedRegistry.create();
    Camera2D ScopedAuthoredCamera;
    ScopedAuthoredCamera.ViewSize = Vector2D{800.0f, 600.0f};
    ScopedAuthoredCamera.Zoom = 1.0f;
    ScopedRegistry.emplace<Camera2D>(ScopedCameraEntity, ScopedAuthoredCamera);

    CameraStackSystem ScopedCameras;
    CameraStackDesc ScopedCameraDesc;
    ScopedCameraDesc.cameraEntity = ScopedCameraEntity;
    ScopedCameraDesc.followTarget = ScopedTargetEntity;
    ScopedCameraDesc.followSharpness = 8.0f;
    ScopedCameraDesc.tiltRadians = 0.35f;
    ScopedCameraDesc.perspectiveDistance = 900.0f;
    FBY_CHECK(ScopedCameras.Push(ScopedRegistry, ScopedCameraDesc));
    ScopedContext.dt = 1.0f / 60.0f;
    for (int ScopedFrame = 0; ScopedFrame < 60; ++ScopedFrame)
    {
        ScopedCameras.Update(ScopedContext);
    }
    FBY_CHECK(ScopedCameras.ActiveCamera().Center.X > 200.0f);
    FBY_CHECK(Near(ScopedCameras.ActiveCamera().Center.X, 400.0f, 5.0f));
    const Mat4 ScopedProjection = ScopedCameras.ViewProjection();
    FBY_CHECK(std::isfinite(ScopedProjection.m[0]) && std::isfinite(ScopedProjection.m[15]));
    ScopedCameras.AddTrauma(1.0f);
    ScopedCameras.Update(ScopedContext);
    const Mat4 ScopedBlended = ScopedCameras.InterpolatedProjection(0.5f);
    FBY_CHECK(std::isfinite(ScopedBlended.m[5]));
    // Shake must decay back to the followed base, not integrate into it.
    for (int ScopedFrame = 0; ScopedFrame < 180; ++ScopedFrame)
    {
        ScopedCameras.Update(ScopedContext);
    }
    FBY_CHECK(Near(ScopedCameras.ActiveCamera().Center.X, 400.0f, 1.0f));
    FBY_CHECK(Near(ScopedCameras.ActiveCamera().Rotation, 0.0f, 0.002f));
    FBY_CHECK(ScopedCameras.Pop());
    FBY_CHECK(ScopedCameras.Empty());

    AudioLimiter ScopedLimiter;
    ScopedLimiter.Configure(48000);
    std::array<float, 256> ScopedAudio{};
    for (float& ScopedSample : ScopedAudio)
    {
        ScopedSample = 2.0f;
    }
    ScopedLimiter.Process(ScopedAudio.data(), 128, 2);
    float ScopedPeak = 0.0f;
    for (const float ScopedSample : ScopedAudio)
    {
        ScopedPeak = std::max(ScopedPeak, std::fabs(ScopedSample));
    }
    FBY_CHECK(ScopedPeak <= 0.989f);
    FBY_CHECK(ScopedLimiter.GainReductionDb() < 0.0f);

    if (GFailures == 0)
    {
        std::printf("ui_accessibility_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "ui_accessibility_test: %u failure(s)\n", GFailures);
    return 1;
}
