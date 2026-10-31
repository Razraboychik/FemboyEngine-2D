// FemboyEngine2D - SDF shadow and post-process render passes.
#include "Engine/RHI/RenderPasses.hpp"

#include "Engine/UI/Accessibility.hpp"

#include "Engine/RHI/CameraStack.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fby::Rhi
{
namespace
{
// R8 field encoding shared with the Vulkan backend and the sprite shader.
constexpr float kSdfEncodeScale = 1024.0f;
}

bool SdfShadowPass::Build(RenderGraph& InGraph, std::string_view InSdfResource,
                          std::string_view InShadowResource) noexcept
{
    MSdfResource = RenderResourceHash(InSdfResource);
    MShadowResource = RenderResourceHash(InShadowResource);
    MColorResource = RenderResourceHash("scene.color");

    RenderPassDesc ScopedShadow{};
    ScopedShadow.name = "SdfShadow";
    ScopedShadow.kind = RenderPassKind::Compute;
    ScopedShadow.execute = &SdfShadowPass::ExecuteShadow;
    ScopedShadow.user = this;
    ScopedShadow.reads[0] = MSdfResource;
    ScopedShadow.readCount = 1;
    ScopedShadow.writes[0] = MShadowResource;
    ScopedShadow.writeCount = 1;
    if (!InGraph.AddPass(ScopedShadow))
    {
        return false;
    }

    return true;
}

void SdfShadowPass::RefreshConstants(const RenderGraphContext& InContext) noexcept
{
    MConstants = {};
    const FramePacket* ScopedFrame = InContext.frame;
    float ScopedWidth = static_cast<float>(InContext.extent.width);
    float ScopedHeight = static_cast<float>(InContext.extent.height);
    Vector2D ScopedCenter{};
    float ScopedZoom = 1.0f;
    if (ScopedFrame != nullptr)
    {
        ScopedCenter = ScopedFrame->camera.center;
        ScopedZoom = std::max(ScopedFrame->camera.zoom, 0.0001f);
        ScopedWidth = static_cast<float>(ScopedFrame->camera.viewport.X);
        ScopedHeight = static_cast<float>(ScopedFrame->camera.viewport.Y);
    }
    MConstants.camera = Vector4D{ScopedCenter.X, ScopedCenter.Y, ScopedZoom, 0.0f};
    MConstants.viewport = Vector4D{ScopedWidth, ScopedHeight, 0.0f, 0.0f};

    if (ScopedFrame == nullptr)
    {
        MConstants.encode = Vector4D{kSdfEncodeScale, 0.0f, 0.0f, 0.0f};
        return;
    }
    const SdfLightingFrame& ScopedSdf = ScopedFrame->sdf;
    const float ScopedLength = std::max(Vector2D{ScopedSdf.sunDirection.X, ScopedSdf.sunDirection.Y}.LengthSquared(),
                                        1.0e-6f);
    const float ScopedInverse = 1.0f / std::sqrt(ScopedLength);
    MConstants.sun = Vector4D{ScopedSdf.sunDirection.X * ScopedInverse, ScopedSdf.sunDirection.Y * ScopedInverse,
                              ScopedSdf.softness, ScopedSdf.ambient};
    MConstants.fieldBounds = Vector4D{ScopedSdf.origin.X, ScopedSdf.origin.Y,
                                      static_cast<float>(ScopedSdf.width) * ScopedSdf.cellSize.X,
                                      static_cast<float>(ScopedSdf.height) * ScopedSdf.cellSize.Y};
    MConstants.params = Vector4D{static_cast<float>(ScopedSdf.steps), static_cast<float>(ScopedSdf.occluderCount),
                                 ScopedSdf.occlusionRadius, ScopedSdf.occlusionStrength};
    MConstants.encode = Vector4D{kSdfEncodeScale, ScopedSdf.enabled ? 1.0f : 0.0f, 0.0f, 0.0f};
    const std::uint32_t ScopedCount = std::min(ScopedSdf.occluderCount, kMaxSdfGpuOccluders);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        MConstants.occluders[ScopedI * 2] = ScopedSdf.occluders[ScopedI].Shape;
        MConstants.occluders[ScopedI * 2 + 1] = ScopedSdf.occluders[ScopedI].Extents;
    }
}

void SdfShadowPass::ExecuteShadow(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<SdfShadowPass*>(InUser);
    if (InContext.frame == nullptr || !InContext.frame->sdf.enabled)
    {
        return;
    }
    ScopedSelf->RefreshConstants(InContext);
    // The mask is half resolution: the solve runs per shadow texel, not per pixel.
    const std::uint32_t ScopedWidth = std::max(InContext.extent.width / 2u, 1u);
    const std::uint32_t ScopedHeight = std::max(InContext.extent.height / 2u, 1u);
    const RenderCommandRecord ScopedRecord =
        MakeComputeRecord("sdf_shadow.comp", ScopedSelf->MConstants, (ScopedWidth + 7u) / 8u, (ScopedHeight + 7u) / 8u);
    (void)InContext.Push(ScopedRecord);
}

bool PostFxPass::Build(RenderGraph& InGraph, Fby::CameraStackSystem& InCameras,
                       std::string_view InColorResource) noexcept
{
    MCameras = &InCameras;
    MColorResource = RenderResourceHash(InColorResource);
    MBloomResource = RenderResourceHash("postfx.bloom");

    RenderPassDesc ScopedPrefilter{};
    ScopedPrefilter.name = "PostFxBloomPrefilter";
    ScopedPrefilter.kind = RenderPassKind::Compute;
    ScopedPrefilter.execute = &PostFxPass::ExecutePrefilter;
    ScopedPrefilter.user = this;
    ScopedPrefilter.reads[0] = MColorResource;
    ScopedPrefilter.readCount = 1;
    ScopedPrefilter.writes[0] = MBloomResource;
    ScopedPrefilter.writeCount = 1;
    if (!InGraph.AddPass(ScopedPrefilter))
    {
        return false;
    }

    RenderPassDesc ScopedBlur{};
    ScopedBlur.name = "PostFxBloomBlur";
    ScopedBlur.kind = RenderPassKind::Compute;
    ScopedBlur.execute = &PostFxPass::ExecuteBlur;
    ScopedBlur.user = this;
    ScopedBlur.reads[0] = MBloomResource;
    ScopedBlur.readCount = 1;
    ScopedBlur.writes[0] = MBloomResource;
    ScopedBlur.writeCount = 1;
    if (!InGraph.AddPass(ScopedBlur))
    {
        return false;
    }

    RenderPassDesc ScopedComposite{};
    ScopedComposite.name = "PostFxComposite";
    ScopedComposite.kind = RenderPassKind::Graphics;
    ScopedComposite.execute = &PostFxPass::ExecuteComposite;
    ScopedComposite.user = this;
    ScopedComposite.reads[0] = MColorResource;
    ScopedComposite.reads[1] = MBloomResource;
    ScopedComposite.readCount = 2;
    ScopedComposite.writes[0] = MColorResource;
    ScopedComposite.writeCount = 1;
    return InGraph.AddPass(ScopedComposite);
}

void PostFxPass::RefreshConstants(const RenderGraphContext& InContext) noexcept
{
    PostFxSettings ScopedSettings{};
    if (InContext.cameras != nullptr)
    {
        ScopedSettings = InContext.cameras->ActivePostFx();
    }
    else if (MCameras != nullptr)
    {
        ScopedSettings = MCameras->ActivePostFx();
    }
    MConstants.flagsAndCrt =
        Vector4D{static_cast<float>(ScopedSettings.flags), ScopedSettings.crtCurvature, ScopedSettings.scanlineStrength,
                 InContext.timeSeconds};
    MConstants.vignetteAndBloom =
        Vector4D{ScopedSettings.vignetteStrength, ScopedSettings.bloomThreshold, ScopedSettings.bloomIntensity,
                 ScopedSettings.exposure};
    ScopedSettings.colorVision = std::min<std::uint32_t>(ScopedSettings.colorVision, 3u);
    ScopedSettings.colorVisionStrength = std::clamp(ScopedSettings.colorVisionStrength, 0.0f, 1.0f);
    MConstants.exposureAndChromatic = Vector4D{ScopedSettings.chromaticAberration, ScopedSettings.filmGrain,
                                               ScopedSettings.flashGuardGain, ScopedSettings.colorVisionStrength};
    const ColorMatrix3 ScopedMatrix =
        ColorVisionMatrix(static_cast<ColorVisionDeficiency>(ScopedSettings.colorVision));
    MConstants.colorRow0 = Vector4D{ScopedMatrix.m[0], ScopedMatrix.m[1], ScopedMatrix.m[2], 0.0f};
    MConstants.colorRow1 = Vector4D{ScopedMatrix.m[3], ScopedMatrix.m[4], ScopedMatrix.m[5], 0.0f};
    MConstants.colorRow2 = Vector4D{ScopedMatrix.m[6], ScopedMatrix.m[7], ScopedMatrix.m[8], 0.0f};
    MConstants.grainAndFlash = Vector4D{static_cast<float>(InContext.extent.width),
                                        static_cast<float>(InContext.extent.height), 0.0f, 0.0f};
}

void PostFxPass::ExecutePrefilter(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<PostFxPass*>(InUser);
    ScopedSelf->RefreshConstants(InContext);
    if ((static_cast<std::uint32_t>(ScopedSelf->MConstants.flagsAndCrt.X) & PostFxBloom) == 0)
    {
        return;
    }
    const std::uint32_t ScopedGroups = (InContext.extent.width + 15u) / 16u;
    const RenderCommandRecord ScopedRecord =
        MakeComputeRecord("postfx_bloom_prefilter.comp", ScopedSelf->MConstants, ScopedGroups,
                          (InContext.extent.height + 15u) / 16u);
    (void)InContext.Push(ScopedRecord);
}

void PostFxPass::ExecuteBlur(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<PostFxPass*>(InUser);
    if ((static_cast<std::uint32_t>(ScopedSelf->MConstants.flagsAndCrt.X) & PostFxBloom) == 0)
    {
        return;
    }
    const std::uint32_t ScopedGroups = (InContext.extent.width + 15u) / 16u;
    const RenderCommandRecord ScopedRecord =
        MakeComputeRecord("postfx_bloom_blur.comp", ScopedSelf->MConstants, ScopedGroups,
                          (InContext.extent.height + 15u) / 16u);
    (void)InContext.Push(ScopedRecord);
}

void PostFxPass::ExecuteComposite(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<PostFxPass*>(InUser);
    ScopedSelf->RefreshConstants(InContext);
    const RenderCommandRecord ScopedRecord =
        MakeGraphicsRecord("postfx_composite.frag", ScopedSelf->MConstants, InContext.extent.width,
                           InContext.extent.height);
    (void)InContext.Push(ScopedRecord);
}

bool ColorGradingPass::Build(RenderGraph& InGraph, const ColorGradingSettings& InSettings,
                              std::string_view InColorResource) noexcept
{
    MSettings = InSettings;
    MColorResource = RenderResourceHash(InColorResource);
    Refresh();
    RenderPassDesc ScopedPass{};
    ScopedPass.name = "ColorGrading";
    ScopedPass.kind = RenderPassKind::Graphics;
    ScopedPass.execute = &ColorGradingPass::Execute;
    ScopedPass.user = this;
    ScopedPass.reads[0] = MColorResource;
    ScopedPass.readCount = 1;
    ScopedPass.writes[0] = MColorResource;
    ScopedPass.writeCount = 1;
    return InGraph.AddPass(ScopedPass);
}

void ColorGradingPass::Refresh() noexcept
{
    MConstants.params0 = Vector4D{static_cast<float>(MSettings.lutSize), MSettings.intensity, MSettings.saturation,
                                  MSettings.contrast};
    MConstants.params1 = Vector4D{MSettings.brightness,
                                  static_cast<float>(MSettings.lutTexture & 0xFFFFFFFFull),
                                  static_cast<float>(MSettings.lutTexture >> 32),
                                  static_cast<float>(MSettings.flags)};
}

void ColorGradingPass::Execute(RenderGraphContext& InContext, void* InUser)
{
    auto* ScopedSelf = static_cast<ColorGradingPass*>(InUser);
    if (ScopedSelf->MSettings.intensity <= 0.0f)
    {
        return;
    }
    ScopedSelf->Refresh();
    const RenderCommandRecord ScopedRecord =
        MakeGraphicsRecord("color_grading.frag", ScopedSelf->MConstants, InContext.extent.width,
                           InContext.extent.height);
    (void)InContext.Push(ScopedRecord);
}
}
