// FemboyEngine2D - render-target to texture-array bake smoke test.
#include "Engine/Core/Log.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/OitPass.hpp"
#include "Engine/RHI/StaticBakeRenderer.hpp"
#include "Engine/RHI/TemporalResolvePass.hpp"
#include "Engine/RHI/Fsr2Pass.hpp"
#include "Engine/RHI/CustomFrameGenerator.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
Fby::Rhi::RHI* GHudTestGpu = nullptr;
Fby::Rhi::FramePacket GHudTestPacket{};

void HudTestRender(void*, void*)
{
    if (GHudTestGpu != nullptr)
    {
        (void)GHudTestGpu->DrawOverlay(GHudTestPacket);
    }
}
}

int main()
{
    Fby::SetLogLevel(Fby::LogLevel::Warn);
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        std::fprintf(stderr, "window init failed\n");
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - static bake gpu";
    ScopedWindowDesc.width = 256;
    ScopedWindowDesc.height = 256;
    ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        std::fprintf(stderr, "window create failed\n");
        return 1;
    }

    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Fby::Rhi::Backend::Vulkan;
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {256, 256};
    ScopedDesc.vsync = false;
    ScopedDesc.enableDepthPrepass = false;
    ScopedDesc.enableGpuCulling = false;
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::fprintf(stderr, "no Vulkan device; skipping\n");
        return 0;
    }

    Fby::Rhi::RHI::NativeDeviceHandles ScopedNative;
    std::printf("static_bake_gpu: caps fsr1=%d fsr2=%d motion=%d fg=%d\n",
                ScopedGpu->Info().upscalers.fsr1 ? 1 : 0, ScopedGpu->Info().upscalers.fsr2 ? 1 : 0,
                ScopedGpu->Info().upscalers.motionVectors ? 1 : 0,
                ScopedGpu->Info().upscalers.frameGeneration ? 1 : 0);
    if (!ScopedGpu->Info().upscalers.motionVectors || !ScopedGpu->Info().upscalers.fsr2 ||
        !ScopedGpu->Info().upscalers.frameGeneration)
    {
        std::fprintf(stderr, "Vulkan temporal capabilities are not reported\n");
        return 1;
    }
    std::printf("static_bake_gpu: native handles=%d\n",
                ScopedGpu->GetNativeDeviceHandles(ScopedNative) && ScopedNative.Device != nullptr ? 1 : 0);

    std::array<std::byte, 4> ScopedWhite{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    Fby::Rhi::TextureArrayDesc ScopedTextureDesc;
    ScopedTextureDesc.size = {1, 1};
    ScopedTextureDesc.layers = 1;
    ScopedTextureDesc.mipLevels = 1;
    ScopedTextureDesc.srgb = false;
    const Fby::Rhi::TextureArrayHandle ScopedTexture =
        ScopedGpu->CreateTextureArray(ScopedTextureDesc, std::span<const std::byte>(ScopedWhite));
    if (!ScopedTexture.IsValid())
    {
        std::fprintf(stderr, "texture array failed\n");
        return 1;
    }

    std::array<Fby::Rhi::RenderItem, 1> ScopedItems{};
    ScopedItems[0] = Fby::Rhi::RenderItem::Quad({128.0f, 128.0f}, {256.0f, 256.0f});
    ScopedItems[0].flags = Fby::Rhi::EncodeDepth(ScopedItems[0].flags, 0.5f, true);
    std::array<Fby::Rhi::RenderKey, 1> ScopedKeys{Fby::Rhi::MakeRenderKey(0, 0.0f)};
    std::array<Fby::Rhi::DrawBatch, 1> ScopedBatches{Fby::Rhi::DrawBatch{0, 1, 0, 0}};
    Fby::Rhi::FramePacket ScopedPacket;
    ScopedPacket.items = ScopedItems;
    ScopedPacket.keys = ScopedKeys;
    ScopedPacket.batches = ScopedBatches;
    ScopedPacket.textureArray = ScopedTexture.id;
    ScopedPacket.camera = Fby::Rhi::CameraData{Fby::Vector2D{128.0f, 128.0f}, Fby::Vector2D{256.0f, 256.0f}, 1.0f};

    Fby::Rhi::StaticBakeRenderer ScopedBake;
    if (!ScopedGpu->BeginFrame() || !ScopedBake.Bake(*ScopedGpu, ScopedPacket, {256, 256}))
    {
        std::fprintf(stderr, "bake record failed\n");
        return 1;
    }
    ScopedGpu->EndFrame();
    const bool ScopedOk = ScopedBake.Finalize(*ScopedGpu);
    std::printf("static_bake_gpu: target=%u baked=%u\n", ScopedBake.Target().id, ScopedBake.Texture().id);
    if (ScopedOk && ScopedGpu->BeginFrame())
    {
        std::printf("static_bake_gpu: command buffer=%d\n",
                    ScopedGpu->CurrentCommandBuffer() != nullptr ? 1 : 0);
        ScopedGpu->Submit(ScopedBake.OverlayPacket());
        ScopedGpu->EndFrame();
        ScopedGpu->WaitIdle();
        std::printf("static_bake_gpu: generated present=%d\n",
                    ScopedGpu->PresentGeneratedFrame(ScopedBake.Target()) ? 1 : 0);
        std::printf("static_bake_gpu: invalid generated present=%d\n",
                    ScopedGpu->PresentGeneratedFrame(Fby::Rhi::RenderTargetHandle{}) ? 1 : 0);
        std::printf("static_bake_gpu: overlay submitted\n");
    }

    Fby::Rhi::RenderTargetDesc ScopedOitDesc;
    ScopedOitDesc.size = {128, 128};
    ScopedOitDesc.blend = Fby::Rhi::RenderBlendMode::Additive;
    const Fby::Rhi::RenderTargetHandle ScopedOitTarget = ScopedGpu->CreateRenderTarget(ScopedOitDesc);
    if (ScopedOitTarget.IsValid() && ScopedGpu->BeginFrame())
    {
        Fby::Rhi::FramePacket ScopedOitPacket;
        ScopedOitPacket.items = ScopedItems;
        ScopedOitPacket.keys = ScopedKeys;
        ScopedOitPacket.batches = ScopedBatches;
        ScopedOitPacket.textureArray = ScopedTexture.id;
        ScopedOitPacket.camera = Fby::Rhi::CameraData{Fby::Vector2D{64.0f, 64.0f}, Fby::Vector2D{128.0f, 128.0f}, 1.0f};
        (void)ScopedGpu->RenderToTarget(ScopedOitTarget, ScopedOitPacket);
        ScopedGpu->EndFrame();
        ScopedGpu->WaitIdle();
        std::printf("static_bake_gpu: additive OIT target submitted\n");
        ScopedGpu->DestroyRenderTarget(ScopedOitTarget);
    }

    std::array<Fby::Rhi::RenderItem, 1> ScopedOitItems = ScopedItems;
    ScopedOitItems[0].flags = 0;
    Fby::Rhi::FramePacket ScopedOitPacket;
    ScopedOitPacket.items = ScopedOitItems;
    ScopedOitPacket.keys = ScopedKeys;
    ScopedOitPacket.batches = ScopedBatches;
    ScopedOitPacket.textureArray = ScopedTexture.id;
    ScopedOitPacket.camera = Fby::Rhi::CameraData{Fby::Vector2D{64.0f, 64.0f}, Fby::Vector2D{128.0f, 128.0f}, 1.0f};
    Fby::Rhi::OitPass ScopedOit;
    if (ScopedGpu->BeginFrame() && ScopedOit.Render(*ScopedGpu, ScopedOitPacket, {128, 128}))
    {
        Fby::Rhi::FramePacket ScopedResolvePacket;
        ScopedResolvePacket.oitAccumulation = ScopedOit.Accumulation().id;
        ScopedResolvePacket.oitRevealage = ScopedOit.Revealage().id;
        ScopedGpu->Submit(ScopedResolvePacket);
        ScopedGpu->EndFrame();
        ScopedGpu->WaitIdle();
        std::printf("static_bake_gpu: OIT accum/reveal/resolve submitted\n");
    }
    Fby::Rhi::RenderTargetDesc ScopedResolveDesc;
    ScopedResolveDesc.size = {128, 128};
    const Fby::Rhi::RenderTargetHandle ScopedResolveTarget = ScopedGpu->CreateRenderTarget(ScopedResolveDesc);
    if (ScopedResolveTarget.IsValid() && ScopedGpu->BeginFrame())
    {
        Fby::Rhi::FramePacket ScopedResolvePacket;
        ScopedResolvePacket.oitAccumulation = ScopedOit.Accumulation().id;
        ScopedResolvePacket.oitRevealage = ScopedOit.Revealage().id;
        (void)ScopedGpu->RenderToTarget(ScopedResolveTarget, ScopedResolvePacket);
        ScopedGpu->EndFrame();
        ScopedGpu->WaitIdle();
        std::printf("static_bake_gpu: OIT target resolve submitted\n");
        ScopedGpu->DestroyRenderTarget(ScopedResolveTarget);
    }

    Fby::Rhi::RenderTargetDesc ScopedSceneDesc;
    ScopedSceneDesc.size = {320, 180};
    const Fby::Rhi::RenderTargetHandle ScopedSceneTarget = ScopedGpu->CreateRenderTarget(ScopedSceneDesc);
    Fby::Rhi::RenderTargetDesc ScopedDisplayDesc;
    ScopedDisplayDesc.size = {640, 360};
    const Fby::Rhi::RenderTargetHandle ScopedDisplayTarget = ScopedGpu->CreateRenderTarget(ScopedDisplayDesc);
    ScopedDisplayDesc.size = {427, 240};
    const Fby::Rhi::RenderTargetHandle ScopedFsr075Target = ScopedGpu->CreateRenderTarget(ScopedDisplayDesc);
    ScopedDisplayDesc.size = {320, 180};
    const Fby::Rhi::RenderTargetHandle ScopedFsr05Target = ScopedGpu->CreateRenderTarget(ScopedDisplayDesc);
    if (ScopedSceneTarget.IsValid() && ScopedDisplayTarget.IsValid() && ScopedFsr075Target.IsValid() &&
        ScopedFsr05Target.IsValid() && ScopedGpu->BeginFrame())
    {
        Fby::Rhi::FramePacket ScopedScenePacket;
        ScopedScenePacket.clear = Fby::Color32::FromBytes(20, 30, 40);
        (void)ScopedGpu->RenderToTarget(ScopedSceneTarget, ScopedScenePacket);
        const bool ScopedUpscaled = ScopedGpu->UpscaleRenderTarget(
            ScopedSceneTarget, ScopedDisplayTarget, Fby::Rhi::SpatialUpscaleMode::Fsr1);
        std::printf("static_bake_gpu: FSR1 spatial upscale submitted=%d\n", ScopedUpscaled ? 1 : 0);
        const bool Scoped075 = ScopedGpu->UpscaleRenderTarget(
            ScopedSceneTarget, ScopedFsr075Target, Fby::Rhi::SpatialUpscaleMode::Fsr1);
        const bool Scoped05 = ScopedGpu->UpscaleRenderTarget(
            ScopedSceneTarget, ScopedFsr05Target, Fby::Rhi::SpatialUpscaleMode::Fsr1);
        const bool ScopedRcas = ScopedGpu->RcasSharpen(ScopedDisplayTarget, ScopedFsr075Target, 0.35f);
        std::printf("static_bake_gpu: FSR1 0.75=%d 0.50=%d RCAS=%d\n", Scoped075 ? 1 : 0, Scoped05 ? 1 : 0,
                    ScopedRcas ? 1 : 0);
        ScopedGpu->EndFrame();
        ScopedGpu->WaitIdle();
        ScopedGpu->DestroyRenderTarget(ScopedSceneTarget);
        ScopedGpu->DestroyRenderTarget(ScopedDisplayTarget);
        ScopedGpu->DestroyRenderTarget(ScopedFsr075Target);
        ScopedGpu->DestroyRenderTarget(ScopedFsr05Target);
    }

    const Fby::Rhi::RenderTargetHandle ScopedHudTarget = ScopedGpu->CreateHudTarget({640, 360});
    if (ScopedHudTarget.IsValid())
    {
        std::printf("static_bake_gpu: HUD target created\n");
        ScopedGpu->SetHudTarget(ScopedHudTarget);
        static const Fby::Rhi::RenderItem ScopedHudItem =
            Fby::Rhi::RenderItem::Quad({0.0f, 0.0f}, {32.0f, 32.0f});
        static const Fby::Rhi::RenderKey ScopedHudKey{};
        static const Fby::Rhi::DrawBatch ScopedHudBatch{0, 1, 0, 0};
        GHudTestPacket.items = std::span<const Fby::Rhi::RenderItem>(&ScopedHudItem, 1);
        GHudTestPacket.keys = std::span<const Fby::Rhi::RenderKey>(&ScopedHudKey, 1);
        GHudTestPacket.batches = std::span<const Fby::Rhi::DrawBatch>(&ScopedHudBatch, 1);
        GHudTestGpu = ScopedGpu.get();
        ScopedGpu->SetUiRenderer(&HudTestRender, nullptr);
        if (ScopedGpu->BeginFrame())
        {
            const bool ScopedHudRendered = ScopedGpu->RenderHudTarget();
            Fby::Rhi::FramePacket ScopedHudScene;
            ScopedGpu->Submit(ScopedHudScene);
            const bool ScopedHudComposited = ScopedGpu->CompositeHud();
            ScopedGpu->EndFrame();
            ScopedGpu->WaitIdle();
            std::printf("static_bake_gpu: HUD render=%d composite=%d\n", ScopedHudRendered ? 1 : 0,
                        ScopedHudComposited ? 1 : 0);
            const bool ScopedHudPresent = ScopedGpu->PresentGeneratedFrame(ScopedBake.Target());
            std::printf("static_bake_gpu: generated present with HUD=%d\n", ScopedHudPresent ? 1 : 0);
            if (!ScopedHudPresent)
            {
                std::fprintf(stderr, "generated present with HUD failed\n");
                return 1;
            }
            if (ScopedGpu->PresentGeneratedFrame(Fby::Rhi::RenderTargetHandle{}))
            {
                std::fprintf(stderr, "invalid generated target was presented\n");
                return 1;
            }
        }
        ScopedGpu->SetUiRenderer(nullptr, nullptr);
        GHudTestGpu = nullptr;
        ScopedGpu->SetHudTarget({});
        ScopedGpu->DestroyRenderTarget(ScopedHudTarget);
    }
    const Fby::Rhi::RenderTargetHandle ScopedMotionTarget =
        ScopedGpu->CreateMotionVectorTarget({160, 90});
    if (ScopedMotionTarget.IsValid())
    {
        std::printf("static_bake_gpu: motion vector target created\n");
        ScopedGpu->DestroyRenderTarget(ScopedMotionTarget);
    }
    std::array<Fby::Rhi::MotionVectorItem, 2> ScopedMotionItems{};
    ScopedMotionItems[0].position = {10.0f, 0.0f};
    ScopedMotionItems[1].position = {20.0f, 0.0f};
    std::printf("static_bake_gpu: motion upload=%d\n",
                ScopedGpu->UploadMotionVectors(ScopedMotionItems) ? 1 : 0);
    const Fby::Rhi::RenderTargetHandle ScopedMotionRenderTarget =
        ScopedGpu->CreateMotionVectorTarget({320, 180});
    if (ScopedMotionRenderTarget.IsValid() && ScopedGpu->BeginFrame())
    {
        const bool ScopedMotionRendered = ScopedGpu->RenderMotionVectors(
            ScopedMotionRenderTarget, 2, {0.0f, 0.0f}, {0.0f, 0.0f}, 1.0f, 1.0f, {0.0f, 0.0f},
            {0.0f, 0.0f}, {320.0f, 180.0f});
        ScopedGpu->EndFrame();
        ScopedGpu->WaitIdle();
        std::printf("static_bake_gpu: motion pass=%d\n", ScopedMotionRendered ? 1 : 0);
        ScopedGpu->DestroyRenderTarget(ScopedMotionRenderTarget);
    }

    const Fby::Rhi::RenderTargetHandle ScopedCurrentTarget =
        ScopedGpu->CreateRenderTarget(Fby::Rhi::RenderTargetDesc{{160, 90}, false});
    const Fby::Rhi::RenderTargetHandle ScopedHistoryTarget =
        ScopedGpu->CreateRenderTarget(Fby::Rhi::RenderTargetDesc{{160, 90}, false});
    const Fby::Rhi::RenderTargetHandle ScopedResolveMotionTarget =
        ScopedGpu->CreateMotionVectorTarget({160, 90});
    const Fby::Rhi::RenderTargetHandle ScopedResolvedTarget =
        ScopedGpu->CreateRenderTarget(Fby::Rhi::RenderTargetDesc{{160, 90}, false});
    Fby::Rhi::TemporalResolvePass ScopedHistory;
    const bool ScopedHistoryReady = ScopedHistory.Ensure(*ScopedGpu, {160, 90});
    std::printf("static_bake_gpu: temporal history ready=%d\n", ScopedHistoryReady ? 1 : 0);
    if (ScopedCurrentTarget.IsValid() && ScopedHistoryTarget.IsValid() &&
        ScopedResolveMotionTarget.IsValid() && ScopedResolvedTarget.IsValid() && ScopedGpu->BeginFrame())
    {
        (void)ScopedGpu->UploadMotionVectors(ScopedMotionItems);
        (void)ScopedGpu->RenderMotionVectors(ScopedResolveMotionTarget, 2, {0.0f, 0.0f}, {0.0f, 0.0f}, 1.0f, 1.0f,
                                             {0.0f, 0.0f}, {0.0f, 0.0f}, {160.0f, 90.0f});
        const bool ScopedResolved = ScopedGpu->TemporalResolve(
            ScopedCurrentTarget, ScopedHistoryTarget, ScopedResolveMotionTarget, ScopedCurrentTarget,
            ScopedHistoryTarget, ScopedResolvedTarget, 0.9f, 1.0f, false);
        Fby::Rhi::NativeTextureView ScopedDepthView;
        const bool ScopedDepthValid = ScopedGpu->RenderTargetDepthTexture(ScopedCurrentTarget, ScopedDepthView);
        const bool ScopedDepthCopied = ScopedGpu->CopyDepth(ScopedCurrentTarget, ScopedHistoryTarget);
        const bool ScopedColorCopied = ScopedGpu->CopyColorImage(ScopedCurrentTarget, ScopedHistoryTarget);
        std::printf("static_bake_gpu: temporal resolve=%d depth-view=%d depth-copy=%d color-copy=%d\n",
                    ScopedResolved ? 1 : 0, ScopedDepthValid ? 1 : 0, ScopedDepthCopied ? 1 : 0,
                    ScopedColorCopied ? 1 : 0);
        if (ScopedHistoryReady)
        {
            const bool ScopedFirst = ScopedHistory.Resolve(*ScopedGpu, ScopedCurrentTarget,
                                                           ScopedResolveMotionTarget, true);
            const bool ScopedSecond = ScopedHistory.Resolve(*ScopedGpu, ScopedCurrentTarget,
                                                            ScopedResolveMotionTarget, false);
            std::printf("static_bake_gpu: temporal history first=%d second=%d\n", ScopedFirst ? 1 : 0,
                        ScopedSecond ? 1 : 0);
        }
        Fby::Rhi::Fsr2Pass ScopedFsr2;
        if (ScopedFsr2.Ensure(*ScopedGpu, {160, 90}, {320, 180}))
        {
            const bool ScopedFsr2Resolved = ScopedFsr2.Resolve(
                *ScopedGpu, ScopedCurrentTarget, ScopedResolveMotionTarget, ScopedResolvedTarget, true, 0.35f);
            std::printf("static_bake_gpu: FSR2-compatible resolve=%d\n", ScopedFsr2Resolved ? 1 : 0);
            ScopedFsr2.Release(*ScopedGpu);
        }
        Fby::Rhi::CustomFrameGenerator ScopedFrameGenerator;
        if (ScopedFrameGenerator.Initialize(*ScopedGpu, {160, 90}, {160, 90}))
        {
            Fby::Rhi::FrameGenerationInput ScopedFrameInput;
            ScopedFrameInput.Color = ScopedCurrentTarget;
            ScopedFrameInput.Depth = ScopedCurrentTarget;
            ScopedFrameInput.Motion = ScopedResolveMotionTarget;
            ScopedFrameInput.Output = ScopedResolvedTarget;
            ScopedFrameInput.ResetHistory = true;
            ScopedFrameInput.HudSeparated = true;
            ScopedFrameInput.Fps = 60.0f;
            ScopedFrameInput.Policy.ProjectAllowed = true;
            ScopedFrameInput.Policy.BackendCompiled = true;
            ScopedFrameInput.Policy.DeviceSupported = true;
            ScopedFrameInput.Policy.HudSeparated = true;
            const bool ScopedFrameGenerated = ScopedFrameGenerator.Generate(*ScopedGpu, ScopedFrameInput);
            std::printf("static_bake_gpu: custom frame generation=%d\n", ScopedFrameGenerated ? 1 : 0);
            ScopedFrameGenerator.Shutdown(*ScopedGpu);
        }
        ScopedGpu->EndFrame();
        ScopedGpu->WaitIdle();
    }
    ScopedHistory.Release(*ScopedGpu);
    ScopedGpu->DestroyRenderTarget(ScopedCurrentTarget);
    ScopedGpu->DestroyRenderTarget(ScopedHistoryTarget);
    ScopedGpu->DestroyRenderTarget(ScopedResolveMotionTarget);
    ScopedGpu->DestroyRenderTarget(ScopedResolvedTarget);

    ScopedOit.Release(*ScopedGpu);

    ScopedBake.Release(*ScopedGpu);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    ScopedGpu->WaitIdle();
    std::fflush(stdout);
    std::_Exit(ScopedOk ? 0 : 1);
}
