// FemboyEngine2D - D3D11 RHI device/swapchain/FrameComposer smoke.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdio>
#include <span>

int main()
{
#if defined(_WIN32)
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        std::fprintf(stderr, "d3d11_rhi: window init failed\n");
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - D3D11 RHI";
    ScopedWindowDesc.width = 320;
    ScopedWindowDesc.height = 240;
    ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        std::fprintf(stderr, "d3d11_rhi: window create failed\n");
        return 1;
    }
    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Fby::Rhi::Backend::D3D11;
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {320, 240};
    ScopedDesc.vsync = false;
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::fprintf(stderr, "d3d11_rhi: device unavailable\n");
        return 0;
    }
    if (ScopedGpu->GetBackend() != Fby::Rhi::Backend::D3D11 || ScopedGpu->Info().name[0] == '\0')
    {
        std::fprintf(stderr, "d3d11_rhi: backend info mismatch\n");
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
        std::fprintf(stderr, "d3d11_rhi: texture array failed\n");
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
    std::array<Fby::Rhi::MotionVectorItem, 1> ScopedMotionItems{};
    if (!ScopedGpu->UploadMotionVectors(std::span<const Fby::Rhi::MotionVectorItem>(ScopedMotionItems)))
    {
        std::fprintf(stderr, "UploadMotionVectors failed\n");
        return 1;
    }
    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        if (!ScopedGpu->BeginFrame())
        {
            std::fprintf(stderr, "d3d11_rhi: BeginFrame failed\n");
            return 1;
        }
        ScopedGpu->Submit(ScopedPacket);
        ScopedGpu->EndFrame();
    }
    ScopedGpu->WaitIdle();
    Fby::Rhi::RenderTargetDesc ScopedOffscreenDesc;
    ScopedOffscreenDesc.size = {8, 8};
    ScopedOffscreenDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedOffscreen = ScopedGpu->CreateRenderTarget(ScopedOffscreenDesc);
    if (!ScopedOffscreen.IsValid())
    {
        std::fprintf(stderr, "d3d11_rhi: offscreen target failed\n");
        return 1;
    }
    if (!ScopedGpu->BeginFrame() || !ScopedGpu->RenderToTarget(ScopedOffscreen, ScopedPacket))
    {
        std::fprintf(stderr, "d3d11_rhi: RenderToTarget failed\n");
        return 1;
    }
    ScopedGpu->EndFrame();
    std::array<std::byte, 8 * 8 * 4> ScopedOffscreenPixels{};
    if (!ScopedGpu->CaptureRenderTarget(ScopedOffscreen, ScopedOffscreenPixels))
    {
        std::fprintf(stderr, "d3d11_rhi: offscreen capture failed\n");
        return 1;
    }
    ScopedGpu->DestroyRenderTarget(ScopedOffscreen);

    const Fby::Rhi::RenderTargetHandle ScopedMotion = ScopedGpu->CreateMotionVectorTarget({160, 90});
    if (!ScopedMotion.IsValid())
    {
        std::fprintf(stderr, "d3d11_rhi: motion render target failed\n");
        return 1;
    }
    if (!ScopedGpu->RenderMotionVectors(ScopedMotion, 1, {}, {}, 1.0f, 1.0f, {}, {}, {320.0f, 240.0f}))
    {
        std::fprintf(stderr, "d3d11_rhi: RenderMotionVectors failed\n");
        return 1;
    }
    ScopedGpu->DestroyRenderTarget(ScopedMotion);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    std::printf("d3d11_rhi: device=%s frames=3 render_target=1 texture=1 submit=1\n", ScopedGpu->Info().name);
    Fby::Rhi::RenderTargetDesc ScopedCaptureDesc;
    ScopedCaptureDesc.size = {8, 8};
    ScopedCaptureDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedCaptureTarget = ScopedGpu->CreateRenderTarget(ScopedCaptureDesc);
    std::array<std::byte, 8 * 8 * 4> ScopedPixels{};
    Fby::Rhi::Extent2D ScopedCapturedSize{};
    if (!ScopedCaptureTarget.IsValid() || !ScopedGpu->CaptureRenderTarget(ScopedCaptureTarget, ScopedPixels, &ScopedCapturedSize))
    {
        std::fprintf(stderr, "d3d11_rhi: capture failed\n");
        return 1;
    }
    const Fby::Rhi::RenderTargetHandle ScopedCopyTarget = ScopedGpu->CreateRenderTarget(ScopedCaptureDesc);
    if (!ScopedCopyTarget.IsValid() || !ScopedGpu->CopyColorImage(ScopedCaptureTarget, ScopedCopyTarget))
    {
        std::fprintf(stderr, "d3d11_rhi: CopyColorImage failed\n");
        return 1;
    }
    ScopedGpu->DestroyRenderTarget(ScopedCopyTarget);
    Fby::Rhi::NativeTextureView ScopedDepthView;
    if (!ScopedGpu->RenderTargetDepthTexture(ScopedCaptureTarget, ScopedDepthView) || ScopedDepthView.image == nullptr)
    {
        std::fprintf(stderr, "depth texture missing\n");
        return 1;
    }
    ScopedGpu->DestroyRenderTarget(ScopedCaptureTarget);
    Fby::Rhi::RenderTargetDesc ScopedTemporalDesc;
    ScopedTemporalDesc.size = {8, 8};
    ScopedTemporalDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedCurrent = ScopedGpu->CreateRenderTarget(ScopedTemporalDesc);
    const Fby::Rhi::RenderTargetHandle ScopedHistory = ScopedGpu->CreateRenderTarget(ScopedTemporalDesc);
    const Fby::Rhi::RenderTargetHandle ScopedResolved = ScopedGpu->CreateRenderTarget(ScopedTemporalDesc);
    if (!ScopedCurrent.IsValid() || !ScopedHistory.IsValid() || !ScopedResolved.IsValid() ||
        !ScopedGpu->TemporalResolve(ScopedCurrent, ScopedHistory, {}, {}, {}, ScopedResolved, 0.5f, 1.0f, false))
    {
        std::fprintf(stderr, "d3d11_rhi: TemporalResolve failed\n");
        return 1;
    }
    std::array<std::byte, 8 * 8 * 4> ScopedResolvedPixels{};
    if (!ScopedGpu->CaptureRenderTarget(ScopedResolved, ScopedResolvedPixels))
    {
        std::fprintf(stderr, "d3d11_rhi: temporal capture failed\n");
        return 1;
    }
    Fby::Rhi::RenderTargetDesc ScopedUpscaleDesc;
    ScopedUpscaleDesc.size = {16, 16};
    ScopedUpscaleDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedUpscaled = ScopedGpu->CreateRenderTarget(ScopedUpscaleDesc);
    if (!ScopedUpscaled.IsValid() ||
        !ScopedGpu->UpscaleRenderTarget(ScopedCurrent, ScopedUpscaled, Fby::Rhi::SpatialUpscaleMode::Bilinear))
    {
        std::fprintf(stderr, "d3d11_rhi: UpscaleRenderTarget failed\n");
        return 1;
    }
    ScopedGpu->DestroyRenderTarget(ScopedUpscaled);
    Fby::Rhi::RenderTargetDesc ScopedSharpenDesc;
    ScopedSharpenDesc.size = {8, 8};
    ScopedSharpenDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedSharpened = ScopedGpu->CreateRenderTarget(ScopedSharpenDesc);
    if (!ScopedSharpened.IsValid() || !ScopedGpu->RcasSharpen(ScopedCurrent, ScopedSharpened, 0.25f))
    {
        std::fprintf(stderr, "d3d11_rhi: RcasSharpen failed\n");
        return 1;
    }
    std::array<std::byte, 8 * 8 * 4> ScopedSharpenedPixels{};
    if (!ScopedGpu->CaptureRenderTarget(ScopedSharpened, ScopedSharpenedPixels))
    {
        std::fprintf(stderr, "d3d11_rhi: sharpen capture failed\n");
        return 1;
    }
    ScopedGpu->DestroyRenderTarget(ScopedSharpened);
    ScopedGpu->DestroyRenderTarget(ScopedCurrent);
    ScopedGpu->DestroyRenderTarget(ScopedHistory);
    ScopedGpu->DestroyRenderTarget(ScopedResolved);
    Fby::Rhi::RenderTargetDesc ScopedPresentDesc;
    ScopedPresentDesc.size = {320, 240};
    ScopedPresentDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedPresentTarget = ScopedGpu->CreateRenderTarget(ScopedPresentDesc);
    if (!ScopedPresentTarget.IsValid() || !ScopedGpu->BeginFrame() ||
        !ScopedGpu->RenderToTarget(ScopedPresentTarget, ScopedPacket))
    {
        std::fprintf(stderr, "d3d11_rhi: present target failed\n");
        return 1;
    }
    ScopedGpu->PresentRenderTarget(ScopedPresentTarget);
    ScopedGpu->EndFrame();
    Fby::Rhi::RenderTargetDesc ScopedHudDesc;
    ScopedHudDesc.size = {320, 240};
    ScopedHudDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedHudTarget = ScopedGpu->CreateRenderTarget(ScopedHudDesc);
    if (!ScopedHudTarget.IsValid() || !ScopedGpu->BeginFrame() ||
        !ScopedGpu->RenderToTarget(ScopedHudTarget, ScopedPacket))
    {
        std::fprintf(stderr, "d3d11_rhi: HUD target failed\n");
        return 1;
    }
    ScopedGpu->SetHudTarget(ScopedHudTarget);
    if (!ScopedGpu->RenderHudTarget() || !ScopedGpu->CompositeHud())
    {
        std::fprintf(stderr, "d3d11_rhi: HUD composite failed\n");
        return 1;
    }
    ScopedGpu->EndFrame();
    ScopedGpu->DestroyRenderTarget(ScopedHudTarget);
    ScopedGpu->DestroyRenderTarget(ScopedPresentTarget);
    Fby::Rhi::RenderTargetDesc ScopedOitDesc;
    ScopedOitDesc.size = {8, 8};
    ScopedOitDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    ScopedOitDesc.blend = Fby::Rhi::RenderBlendMode::Additive;
    const Fby::Rhi::RenderTargetHandle ScopedOitTarget = ScopedGpu->CreateRenderTarget(ScopedOitDesc);
    if (!ScopedOitTarget.IsValid() || !ScopedGpu->BeginFrame() ||
        !ScopedGpu->RenderToTarget(ScopedOitTarget, ScopedPacket))
    {
        std::fprintf(stderr, "d3d11_rhi: OIT target failed\n");
        return 1;
    }
    ScopedGpu->EndFrame();
    ScopedGpu->DestroyRenderTarget(ScopedOitTarget);
    ScopedGpu->Shutdown();
    return 0;
#else
    std::printf("d3d11_rhi: skipped (non-Windows)\n");
    return 0;
#endif
}
