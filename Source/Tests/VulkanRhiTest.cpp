// FemboyEngine2D - Vulkan RHI device/swapchain/FrameComposer smoke.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/RenderPasses.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <span>

int main()
{
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        std::fprintf(stderr, "vulkan_rhi: window init failed\n");
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - Vulkan RHI";
    ScopedWindowDesc.width = 320;
    ScopedWindowDesc.height = 240;
    ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        std::fprintf(stderr, "vulkan_rhi: window create failed\n");
        return 1;
    }
    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Fby::Rhi::Backend::Vulkan;
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {320, 240};
    ScopedDesc.vsync = false;
    ScopedDesc.enableValidation = true;
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::fprintf(stderr, "vulkan_rhi: device unavailable\n");
        return 0;
    }
    if (ScopedGpu->GetBackend() != Fby::Rhi::Backend::Vulkan || ScopedGpu->Info().name[0] == '\0')
    {
        std::fprintf(stderr, "vulkan_rhi: backend info mismatch\n");
        return 1;
    }
    // The spatial upscale chain must initialise; a descriptor-pool regression used
    // to leave FSR1/RCAS silently unavailable. The pass also exposes the bicubic
    // filter, so the capability must be reported.
    if (!ScopedGpu->Info().upscalers.fsr1 || !ScopedGpu->Info().upscalers.fsr2 ||
        !ScopedGpu->Info().upscalers.bicubic)
    {
        std::fprintf(stderr, "vulkan_rhi: spatial upscale chain unavailable (fsr1=%d fsr2=%d bicubic=%d)\n",
                     ScopedGpu->Info().upscalers.fsr1 ? 1 : 0, ScopedGpu->Info().upscalers.fsr2 ? 1 : 0,
                     ScopedGpu->Info().upscalers.bicubic ? 1 : 0);
        return 1;
    }

    std::array<std::byte, 8> ScopedWhite{};
    ScopedWhite.fill(std::byte{0xFF});
    Fby::Rhi::TextureArrayDesc ScopedTextureDesc;
    ScopedTextureDesc.size = {1, 1};
    ScopedTextureDesc.layers = 2;
    ScopedTextureDesc.srgb = false;
    const Fby::Rhi::TextureArrayHandle ScopedTexture =
        ScopedGpu->CreateTextureArray(ScopedTextureDesc, std::span<const std::byte>(ScopedWhite));
    if (!ScopedTexture.IsValid())
    {
        std::fprintf(stderr, "vulkan_rhi: texture array failed\n");
        return 1;
    }

    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedSprite = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedSprite, Fby::Transform2D{Fby::Vector2D{0.0f, 0.0f}});
    ScopedRegistry.emplace<Fby::Sprite2D>(ScopedSprite, Fby::Sprite2D{});
    ScopedRegistry.emplace<Fby::DrawOrder>(ScopedSprite, Fby::DrawOrder{0u, 0.0f});
    Fby::Rhi::FrameComposer ScopedComposer;
    Fby::Rhi::CameraData ScopedCamera;
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), ScopedTexture.id);
    const Fby::Rhi::FramePacket& ScopedPacket = ScopedComposer.Packet();
    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        if (!ScopedGpu->BeginFrame())
        {
            std::fprintf(stderr, "vulkan_rhi: BeginFrame failed\n");
            return 1;
        }
        ScopedGpu->Submit(ScopedPacket);
        ScopedGpu->EndFrame();
    }
    ScopedGpu->WaitIdle();

    // Exercise the offscreen, motion, spatial and post-FX paths of the backend so
    // the validation layer sees them, not only the direct sprite pass.
    Fby::Rhi::RenderTargetDesc ScopedTargetDesc;
    ScopedTargetDesc.size = {320, 240};
    ScopedTargetDesc.srgb = false;
    const Fby::Rhi::RenderTargetHandle ScopedSourceTarget = ScopedGpu->CreateRenderTarget(ScopedTargetDesc);
    const Fby::Rhi::RenderTargetHandle ScopedDestinationTarget = ScopedGpu->CreateRenderTarget(ScopedTargetDesc);
    const Fby::Rhi::RenderTargetHandle ScopedMotionTarget = ScopedGpu->CreateMotionVectorTarget({320, 240});
    if (!ScopedSourceTarget.IsValid() || !ScopedDestinationTarget.IsValid() || !ScopedMotionTarget.IsValid())
    {
        std::fprintf(stderr, "vulkan_rhi: offscreen targets failed\n");
        return 1;
    }
    Fby::Rhi::FramePacket ScopedRichPacket = ScopedPacket;
    std::array<Fby::Rhi::MotionVectorItem, 1> ScopedMotionItems{};
    ScopedMotionItems[0].position = {0.0f, 0.0f};
    ScopedMotionItems[0].size = {32.0f, 32.0f};
    ScopedMotionItems[0].previousPosition = {-4.0f, 0.0f};
    ScopedMotionItems[0].previousSize = {32.0f, 32.0f};
    ScopedMotionItems[0].screenDelta = {4.0f, 0.0f};
    Fby::Rhi::PostFxSettings ScopedFx;
    ScopedFx.flags = Fby::Rhi::PostFxBloom | Fby::Rhi::PostFxCrt;
    ScopedRichPacket.postFxEnabled = true;
    ScopedRichPacket.postFx = ScopedFx;
    // Kitchen-sink frame: clustered lights, SDF shadows, GI probes and the
    // accessibility composite all run in the same validated frame.
    std::array<Fby::Rhi::GpuLight, 2> ScopedLights{};
    ScopedLights[0].PositionAndRadius = {0.0f, 0.0f, 64.0f, 0.0f};
    ScopedLights[0].ColorAndIntensity = {1.0f, 0.5f, 0.25f, 1.0f};
    ScopedLights[1].PositionAndRadius = {40.0f, 20.0f, 32.0f, 0.0f};
    ScopedLights[1].ColorAndIntensity = {0.25f, 0.5f, 1.0f, 0.75f};
    std::array<Fby::Rhi::GpuGiProbe, 1> ScopedProbes{};
    ScopedProbes[0].PositionAndRadius = {0.0f, 0.0f, 16.0f, 0.0f};
    ScopedProbes[0].Radiance = {0.1f, 0.1f, 0.1f, 1.0f};
    ScopedProbes[0].Occlusion = {0.5f, 0.5f, 0.0f, 0.0f};
    ScopedRichPacket.lights = ScopedLights;
    ScopedRichPacket.gpuLighting = true;
    ScopedRichPacket.giProbes = ScopedProbes;
    ScopedRichPacket.giVersion = 1;
    ScopedRichPacket.giHeader.OriginAndCell = {0.0f, 0.0f, 16.0f, 16.0f};
    ScopedRichPacket.giHeader.Extent = {1.0f, 1.0f, 64.0f, 1.0f};
    ScopedRichPacket.sdf.enabled = true;
    ScopedRichPacket.sdf.camera = {0.0f, 0.0f, 1.0f, 0.0f};
    ScopedRichPacket.sdf.viewport = {320.0f, 240.0f, 0.0f, 0.0f};
    ScopedRichPacket.sdf.fieldVersion = 1;
    ScopedRichPacket.accessibility.flags =
        Fby::Rhi::AccessibilityFlashGuard | Fby::Rhi::AccessibilityColorVision;
    ScopedRichPacket.accessibility.colorVision = 1;
    if (!ScopedGpu->BeginFrame())
    {
        std::fprintf(stderr, "vulkan_rhi: rich BeginFrame failed\n");
        return 1;
    }
    (void)ScopedGpu->UploadMotionVectors(ScopedMotionItems);
    (void)ScopedGpu->ClearMotionVectors(ScopedMotionTarget);
    (void)ScopedGpu->RenderMotionVectors(ScopedMotionTarget, 1, Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{-4.0f, 0.0f},
                                         1.0f, 1.0f, Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{0.0f, 0.0f},
                                         Fby::Vector2D{320.0f, 240.0f});
    (void)ScopedGpu->RenderToTarget(ScopedSourceTarget, ScopedRichPacket);
    // The present-target blit needs TRANSFER_DST on the swapchain image.
    ScopedGpu->PresentRenderTarget(ScopedSourceTarget);
    (void)ScopedGpu->UpscaleRenderTarget(ScopedSourceTarget, ScopedDestinationTarget,
                                         Fby::Rhi::SpatialUpscaleMode::Bilinear);
    (void)ScopedGpu->RcasSharpen(ScopedDestinationTarget, ScopedSourceTarget, 0.2f);
    // LUT pass: the render graph path copies the final image and re-renders it.
    Fby::Rhi::ColorGradingConstants ScopedGrading{};
    ScopedGrading.params0 = Fby::Vector4D{32.0f, 1.0f, 1.0f, 1.0f};
    ScopedGrading.params1 = Fby::Vector4D{0.0f, static_cast<float>(ScopedTexture.id), 0.0f, 0.0f};
    if (!ScopedGpu->RecordGraphicsPass("color_grading.frag", &ScopedGrading, sizeof(ScopedGrading),
                                       Fby::Rhi::Extent2D{320, 240}, 1))
    {
        std::fprintf(stderr, "vulkan_rhi: colour grading record failed\n");
        return 1;
    }
    ScopedGpu->Submit(ScopedRichPacket);
    ScopedGpu->EndFrame();
    std::array<std::byte, 320u * 240u * 4u> ScopedCapturedPixels{};
    Fby::Rhi::Extent2D ScopedCapturedSize{};
    if (!ScopedGpu->CaptureRenderTarget(ScopedSourceTarget, ScopedCapturedPixels, &ScopedCapturedSize))
    {
        std::fprintf(stderr, "vulkan_rhi: capture failed\n");
        return 1;
    }
    if (!ScopedGpu->SetVSync(true) || !ScopedGpu->VSyncEnabled())
    {
        std::fprintf(stderr, "vulkan_rhi: vsync toggle failed\n");
        return 1;
    }
    (void)ScopedGpu->SetVSync(false);
    ScopedGpu->Resize({400, 300});
    ScopedGpu->WaitIdle();
    ScopedGpu->DestroyRenderTarget(ScopedSourceTarget);
    ScopedGpu->DestroyRenderTarget(ScopedDestinationTarget);
    ScopedGpu->DestroyRenderTarget(ScopedMotionTarget);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    std::printf("vulkan_rhi: device=%s api=%u.%u.%u tier=%s frames=3\n", ScopedGpu->Info().name,
                (ScopedGpu->Info().apiVersion >> 22) & 0x7Fu, (ScopedGpu->Info().apiVersion >> 12) & 0x3FFu,
                ScopedGpu->Info().apiVersion & 0xFFFu, Fby::Rhi::GpuTierName(ScopedGpu->Info().tier));
    ScopedGpu->Shutdown();
    return 0;
}
