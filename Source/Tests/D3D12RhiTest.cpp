// FemboyEngine2D - D3D12 RHI device/swapchain/texture smoke.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/RenderPasses.hpp"
#include "Engine/RHI/Shaders/DxShaderBlobs.hpp"

#include <array>
#include <cstdio>
#include <span>

int main()
{
#if defined(_WIN32)
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        std::fprintf(stderr, "d3d12_rhi: window init failed\n");
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - D3D12 RHI";
    ScopedWindowDesc.width = 320;
    ScopedWindowDesc.height = 240;
    ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        std::fprintf(stderr, "d3d12_rhi: window create failed\n");
        return 1;
    }
    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Fby::Rhi::Backend::D3D12;
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {320, 240};
    ScopedDesc.vsync = false;
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::fprintf(stderr, "d3d12_rhi: device unavailable\n");
        return 0;
    }
    if (ScopedGpu->GetBackend() != Fby::Rhi::Backend::D3D12 || ScopedGpu->Info().name[0] == '\0')
    {
        std::fprintf(stderr, "d3d12_rhi: backend info mismatch\n");
        return 1;
    }

    const Fby::Rhi::RenderTargetHandle ScopedMotion = ScopedGpu->CreateMotionVectorTarget({160, 90});
    if (!ScopedMotion.IsValid())
    {
        std::fprintf(stderr, "d3d12_rhi: motion render target failed\n");
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
        std::fprintf(stderr, "d3d12_rhi: texture array failed\n");
        return 1;
    }

    // Streaming update: 2 layers, 2 mips; mip 0 is 16 bytes and mip 1 is 4.
    std::array<std::byte, 40> ScopedStreamPixels{};
    Fby::Rhi::TextureArrayDesc ScopedStreamDesc;
    ScopedStreamDesc.size = {2, 2};
    ScopedStreamDesc.layers = 2;
    ScopedStreamDesc.mipLevels = 2;
    const Fby::Rhi::TextureArrayHandle ScopedStream =
        ScopedGpu->CreateTextureArray(ScopedStreamDesc, std::span<const std::byte>(ScopedStreamPixels));
    if (!ScopedStream.IsValid() ||
        !ScopedGpu->UpdateTextureArrayLayer(ScopedStream, 1, 0, std::span<const std::byte>(ScopedStreamPixels.data(), 16)) ||
        !ScopedGpu->UpdateTextureArrayLayer(ScopedStream, 0, 1, std::span<const std::byte>(ScopedStreamPixels.data(), 4)) ||
        ScopedGpu->UpdateTextureArrayLayer(ScopedStream, 2, 0, std::span<const std::byte>(ScopedStreamPixels.data(), 16)) ||
        ScopedGpu->UpdateTextureArrayLayer(ScopedStream, 0, 2, std::span<const std::byte>(ScopedStreamPixels.data(), 4)))
    {
        std::fprintf(stderr, "texture array streaming failed\n");
        return 1;
    }
    ScopedGpu->DestroyTextureArray(ScopedStream);

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
    Fby::Rhi::ShaderPipelineDesc ScopedMaterialDesc;
    ScopedMaterialDesc.domain = Fby::Rhi::ShaderPipelineDomain::Sprite;
    ScopedMaterialDesc.fragmentDxil = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(Fby::Rhi::Shaders::Dx::D3D12MaterialSpritePsSm5.data()),
        Fby::Rhi::Shaders::Dx::D3D12MaterialSpritePsSm5.size());
    const Fby::Rhi::ShaderPipelineHandle ScopedMaterial = ScopedGpu->CreateShaderPipeline(ScopedMaterialDesc);
    if (!ScopedMaterial.IsValid())
    {
        std::fprintf(stderr, "d3d12_rhi: material pipeline failed\n");
        return 1;
    }
    Fby::Rhi::FramePacket ScopedMaterialPacket = ScopedPacket;
    const std::array<Fby::Rhi::DrawBatch, 1> ScopedMaterialBatches{{{0, 1, ScopedMaterial.id, 0}}};
    ScopedMaterialPacket.batches = ScopedMaterialBatches;
    std::array<Fby::Rhi::MeshVertex, 3> ScopedMeshVertices{};
    ScopedMeshVertices[0].position = {-0.5f, -0.5f};
    ScopedMeshVertices[1].position = {0.5f, -0.5f};
    ScopedMeshVertices[2].position = {0.0f, 0.5f};
    std::array<std::uint16_t, 3> ScopedMeshIndices{0, 1, 2};
    Fby::Rhi::MeshRenderItem ScopedMeshItem;
    ScopedMeshItem.vertices = ScopedMeshVertices;
    ScopedMeshItem.indices = ScopedMeshIndices;
    ScopedMeshItem.textureArray = ScopedTexture.id;
    ScopedMeshItem.materialPipeline = ScopedMaterial.id;
    // One inline mesh plus one cached static mesh covers both mesh upload paths.
    Fby::Rhi::MeshDesc ScopedStaticMeshDesc;
    ScopedStaticMeshDesc.vertices = ScopedMeshVertices;
    ScopedStaticMeshDesc.indices = ScopedMeshIndices;
    const Fby::Rhi::MeshHandle ScopedStaticMesh = ScopedGpu->CreateMesh(ScopedStaticMeshDesc);
    Fby::Rhi::MeshRenderItem ScopedStaticMeshItem;
    ScopedStaticMeshItem.mesh = ScopedStaticMesh;
    ScopedStaticMeshItem.textureArray = ScopedTexture.id;
    std::array<Fby::Rhi::MeshRenderItem, 2> ScopedMeshItems{ScopedMeshItem, ScopedStaticMeshItem};
    ScopedMaterialPacket.meshItems = std::span<const Fby::Rhi::MeshRenderItem>(ScopedMeshItems);
    // One frame exercises the offscreen scene + fullscreen post-FX composite.
    Fby::Rhi::FramePacket ScopedPostFxPacket = ScopedMaterialPacket;
    ScopedPostFxPacket.postFxEnabled = true;
    ScopedPostFxPacket.postFx.flags =
        Fby::Rhi::PostFxCrt | Fby::Rhi::PostFxVignette | Fby::Rhi::PostFxColorVision | Fby::Rhi::PostFxBloom;
    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        if (!ScopedGpu->BeginFrame())
        {
            std::fprintf(stderr, "d3d12_rhi: BeginFrame failed\n");
            return 1;
        }
        if (!ScopedGpu->RenderMotionVectors(ScopedMotion, 1, {}, {}, 1.0f, 1.0f, {}, {}, {160.0f, 90.0f}))
        {
            std::fprintf(stderr, "d3d12_rhi: RenderMotionVectors failed\n");
            return 1;
        }
        if (ScopedFrame == 1)
        {
            Fby::Rhi::PostFxConstants ScopedPostFxGraph{};
            ScopedPostFxGraph.flagsAndCrt = Fby::Vector4D{
                static_cast<float>(static_cast<std::uint32_t>(Fby::Rhi::PostFxCrt | Fby::Rhi::PostFxVignette)),
                0.0f, 0.0f, 0.0f};
            ScopedPostFxGraph.vignetteAndBloom = Fby::Vector4D{0.35f, 0.8f, 1.0f, 1.0f};
            if (!ScopedGpu->RecordGraphicsPass("postfx_composite.frag", &ScopedPostFxGraph,
                                               sizeof(ScopedPostFxGraph), Fby::Rhi::Extent2D{320, 240}, 1))
            {
                std::fprintf(stderr, "post-FX graph record failed\n");
                return 1;
            }
        }
        if (ScopedFrame == 2)
        {
            Fby::Rhi::ColorGradingConstants ScopedGrading{};
            ScopedGrading.params0 = Fby::Vector4D{32.0f, 1.0f, 1.0f, 1.0f};
            ScopedGrading.params1 = Fby::Vector4D{0.0f, static_cast<float>(ScopedTexture.id), 0.0f, 0.0f};
            if (!ScopedGpu->RecordGraphicsPass("color_grading.frag", &ScopedGrading, sizeof(ScopedGrading),
                                               Fby::Rhi::Extent2D{320, 240}, 1))
            {
                std::fprintf(stderr, "colour grading record failed\n");
                return 1;
            }
        }
        ScopedGpu->Submit(ScopedFrame == 2 ? ScopedPostFxPacket : ScopedMaterialPacket);
        ScopedGpu->EndFrame();
    }
    ScopedGpu->WaitIdle();
    Fby::Rhi::RenderTargetDesc ScopedOffscreenDesc;
    ScopedOffscreenDesc.size = {8, 8};
    ScopedOffscreenDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedOffscreen = ScopedGpu->CreateRenderTarget(ScopedOffscreenDesc);
    if (!ScopedOffscreen.IsValid() || !ScopedGpu->BeginFrame() ||
        !ScopedGpu->RenderToTarget(ScopedOffscreen, ScopedPacket))
    {
        std::fprintf(stderr, "d3d12_rhi: RenderToTarget failed\n");
        return 1;
    }
    ScopedGpu->EndFrame();
    std::array<std::byte, 8 * 8 * 4> ScopedOffscreenPixels{};
    if (!ScopedGpu->CaptureRenderTarget(ScopedOffscreen, ScopedOffscreenPixels))
    {
        std::fprintf(stderr, "d3d12_rhi: offscreen capture failed\n");
        return 1;
    }
    // Wrap the rendered target as a one-layer array and draw it as a sprite.
    const Fby::Rhi::TextureArrayHandle ScopedBaked =
        ScopedGpu->CreateTextureArrayFromRenderTarget(ScopedOffscreen);
    if (!ScopedBaked.IsValid())
    {
        std::fprintf(stderr, "texture array from render target failed\n");
        return 1;
    }
    Fby::Rhi::FramePacket ScopedBakedPacket = ScopedPacket;
    ScopedBakedPacket.textureArray = ScopedBaked.id;
    if (!ScopedGpu->BeginFrame())
    {
        return 1;
    }
    ScopedGpu->Submit(ScopedBakedPacket);
    ScopedGpu->EndFrame();
    ScopedGpu->WaitIdle();
    ScopedGpu->DestroyTextureArray(ScopedBaked);
    ScopedGpu->DestroyRenderTarget(ScopedOffscreen);
    Fby::Rhi::RenderTargetDesc ScopedCaptureDesc;
    ScopedCaptureDesc.size = {8, 8};
    ScopedCaptureDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedCaptureTarget = ScopedGpu->CreateRenderTarget(ScopedCaptureDesc);
    std::array<std::byte, 8 * 8 * 4> ScopedPixels{};
    Fby::Rhi::Extent2D ScopedCapturedSize{};
    if (!ScopedCaptureTarget.IsValid() || !ScopedGpu->CaptureRenderTarget(ScopedCaptureTarget, ScopedPixels, &ScopedCapturedSize))
    {
        std::fprintf(stderr, "d3d12_rhi: capture failed\n");
        return 1;
    }
    const Fby::Rhi::RenderTargetHandle ScopedCopyTarget = ScopedGpu->CreateRenderTarget(ScopedCaptureDesc);
    if (!ScopedCopyTarget.IsValid() || !ScopedGpu->CopyColorImage(ScopedCaptureTarget, ScopedCopyTarget))
    {
        std::fprintf(stderr, "d3d12_rhi: CopyColorImage failed\n");
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
    ScopedGpu->DestroyRenderTarget(ScopedMotion);

    ScopedGpu->DestroyMesh(ScopedStaticMesh);
    ScopedGpu->DestroyShaderPipeline(ScopedMaterial);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    std::printf("d3d12_rhi: device=%s frames=3 render_target=1 texture=1 submit=1 motion=1 capture=1 temporal=1\n", ScopedGpu->Info().name);
    Fby::Rhi::RenderTargetDesc ScopedTemporalDesc;
    ScopedTemporalDesc.size = {8, 8};
    ScopedTemporalDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedCurrent = ScopedGpu->CreateRenderTarget(ScopedTemporalDesc);
    const Fby::Rhi::RenderTargetHandle ScopedHistory = ScopedGpu->CreateRenderTarget(ScopedTemporalDesc);
    const Fby::Rhi::RenderTargetHandle ScopedResolved = ScopedGpu->CreateRenderTarget(ScopedTemporalDesc);
    if (!ScopedCurrent.IsValid() || !ScopedHistory.IsValid() || !ScopedResolved.IsValid() ||
        !ScopedGpu->BeginFrame() ||
        !ScopedGpu->TemporalResolve(ScopedCurrent, ScopedHistory, {}, {}, {}, ScopedResolved, 0.5f, 1.0f, false))
    {
        std::fprintf(stderr, "d3d12_rhi: TemporalResolve failed\n");
        return 1;
    }
    Fby::Rhi::RenderTargetDesc ScopedUpscaleDesc;
    ScopedUpscaleDesc.size = {16, 16};
    ScopedUpscaleDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedUpscaled = ScopedGpu->CreateRenderTarget(ScopedUpscaleDesc);
    if (!ScopedUpscaled.IsValid() ||
        !ScopedGpu->UpscaleRenderTarget(ScopedCurrent, ScopedUpscaled, Fby::Rhi::SpatialUpscaleMode::Bilinear) ||
        !ScopedGpu->UpscaleRenderTarget(ScopedCurrent, ScopedUpscaled, Fby::Rhi::SpatialUpscaleMode::Nearest) ||
        !ScopedGpu->UpscaleRenderTarget(ScopedCurrent, ScopedUpscaled, Fby::Rhi::SpatialUpscaleMode::Bicubic) ||
        !ScopedGpu->UpscaleRenderTarget(ScopedCurrent, ScopedUpscaled, Fby::Rhi::SpatialUpscaleMode::Fsr1))
    {
        std::fprintf(stderr, "d3d12_rhi: UpscaleRenderTarget failed\n");
        return 1;
    }
    Fby::Rhi::RenderTargetDesc ScopedSharpenDesc;
    ScopedSharpenDesc.size = {8, 8};
    ScopedSharpenDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedSharpened = ScopedGpu->CreateRenderTarget(ScopedSharpenDesc);
    if (!ScopedSharpened.IsValid() || !ScopedGpu->RcasSharpen(ScopedCurrent, ScopedSharpened, 0.25f))
    {
        std::fprintf(stderr, "d3d12_rhi: RcasSharpen failed\n");
        return 1;
    }
    ScopedGpu->EndFrame();
    std::array<std::byte, 8 * 8 * 4> ScopedResolvedPixels{};
    if (!ScopedGpu->CaptureRenderTarget(ScopedResolved, ScopedResolvedPixels))
    {
        std::fprintf(stderr, "d3d12_rhi: temporal capture failed\n");
        return 1;
    }
    ScopedGpu->DestroyRenderTarget(ScopedCurrent);
    ScopedGpu->DestroyRenderTarget(ScopedHistory);
    ScopedGpu->DestroyRenderTarget(ScopedResolved);
    ScopedGpu->DestroyRenderTarget(ScopedUpscaled);
    ScopedGpu->DestroyRenderTarget(ScopedSharpened);
    Fby::Rhi::RenderTargetDesc ScopedPresentDesc;
    // Presenting a differently sized target exercises the scaling blit.
    ScopedPresentDesc.size = {160, 120};
    ScopedPresentDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedPresentTarget = ScopedGpu->CreateRenderTarget(ScopedPresentDesc);
    if (!ScopedPresentTarget.IsValid() || !ScopedGpu->BeginFrame() ||
        !ScopedGpu->RenderToTarget(ScopedPresentTarget, ScopedPacket))
    {
        std::fprintf(stderr, "d3d12_rhi: present target failed\n");
        return 1;
    }
    ScopedGpu->PresentRenderTarget(ScopedPresentTarget);
    ScopedGpu->EndFrame();
    Fby::Rhi::RenderTargetDesc ScopedHudDesc;
    // A differently sized HUD target exercises the scaling overlay.
    ScopedHudDesc.size = {160, 120};
    ScopedHudDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedHudTarget = ScopedGpu->CreateRenderTarget(ScopedHudDesc);
    if (!ScopedHudTarget.IsValid() || !ScopedGpu->BeginFrame() ||
        !ScopedGpu->RenderToTarget(ScopedHudTarget, ScopedPacket))
    {
        std::fprintf(stderr, "d3d12_rhi: HUD target failed\n");
        return 1;
    }
    ScopedGpu->SetHudTarget(ScopedHudTarget);
    if (!ScopedGpu->RenderHudTarget() || !ScopedGpu->CompositeHud())
    {
        std::fprintf(stderr, "d3d12_rhi: HUD composite failed\n");
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
        std::fprintf(stderr, "d3d12_rhi: OIT target failed\n");
        return 1;
    }
    ScopedGpu->EndFrame();
    ScopedGpu->DestroyRenderTarget(ScopedOitTarget);
    // sRGB targets keep the same four-byte payload and must still capture.
    Fby::Rhi::RenderTargetDesc ScopedSrgbDesc;
    ScopedSrgbDesc.size = {8, 8};
    ScopedSrgbDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Srgb;
    const Fby::Rhi::RenderTargetHandle ScopedSrgbTarget = ScopedGpu->CreateRenderTarget(ScopedSrgbDesc);
    std::array<std::byte, 8 * 8 * 4> ScopedSrgbPixels{};
    if (!ScopedSrgbTarget.IsValid() || !ScopedGpu->CaptureRenderTarget(ScopedSrgbTarget, ScopedSrgbPixels))
    {
        std::fprintf(stderr, "d3d12_rhi: sRGB capture failed\n");
        return 1;
    }
    ScopedGpu->DestroyRenderTarget(ScopedSrgbTarget);
    // CopyDepth must copy the depth attachment, not only the colour image.
    Fby::Rhi::RenderTargetDesc ScopedDepthCopySourceDesc;
    ScopedDepthCopySourceDesc.size = {8, 8};
    Fby::Rhi::RenderTargetDesc ScopedDepthCopyTargetDesc = ScopedDepthCopySourceDesc;
    const Fby::Rhi::RenderTargetHandle ScopedDepthCopySource =
        ScopedGpu->CreateRenderTarget(ScopedDepthCopySourceDesc);
    const Fby::Rhi::RenderTargetHandle ScopedDepthCopyTarget =
        ScopedGpu->CreateRenderTarget(ScopedDepthCopyTargetDesc);
    if (!ScopedDepthCopySource.IsValid() || !ScopedDepthCopyTarget.IsValid() || !ScopedGpu->BeginFrame() ||
        !ScopedGpu->RenderToTarget(ScopedDepthCopySource, ScopedPacket) ||
        !ScopedGpu->CopyDepth(ScopedDepthCopySource, ScopedDepthCopyTarget))
    {
        std::fprintf(stderr, "d3d12_rhi: depth copy failed\n");
        return 1;
    }
    ScopedGpu->EndFrame();
    ScopedGpu->DestroyRenderTarget(ScopedDepthCopySource);
    ScopedGpu->DestroyRenderTarget(ScopedDepthCopyTarget);
    // VSync and resize must reach the swapchain, not just the flags.
    if (!ScopedGpu->SetVSync(true) || !ScopedGpu->VSyncEnabled())
    {
        std::fprintf(stderr, "d3d12_rhi: vsync toggle failed\n");
        return 1;
    }
    (void)ScopedGpu->SetVSync(false);
    ScopedGpu->Resize({400, 300});
    ScopedGpu->WaitIdle();
    if (!ScopedGpu->BeginFrame())
    {
        std::fprintf(stderr, "d3d12_rhi: frame after resize failed\n");
        return 1;
    }
    ScopedGpu->Submit(ScopedPacket);
    ScopedGpu->EndFrame();
    const Fby::Rhi::DeviceInfo& ScopedInfo = ScopedGpu->Info();
    if (!ScopedInfo.upscalers.motionVectors || !ScopedInfo.upscalers.fsr1 || ScopedInfo.platform != 0 ||
        ScopedInfo.gpuVendor == 0 || ScopedInfo.apiVersion < 0xB000u || ScopedInfo.driver[0] == '\0')
    {
        std::fprintf(stderr, "d3d12_rhi: capability reporting failed\n");
        return 1;
    }
    ScopedGpu->Shutdown();
    return 0;
#else
    std::printf("d3d12_rhi: skipped (non-Windows)\n");
    return 0;
#endif
}
