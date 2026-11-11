// FemboyEngine2D - GPU timestamp A/B harness for optimization flags.

#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/StaticBakeRenderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct Scenario
{
    const char* name = "";
    bool depthPrepass = false;
    bool gpuCulling = false;
    bool hiZ = false;
    bool clustered = false;
    bool gpuLighting = false;
    bool staticCaches = false;
    bool opaqueFrontToBack = false;
    bool bake = false;
    bool animate = true;
};

void AddLights(Fby::Ecs::Registry& InRegistry, std::uint32_t InCount)
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        const Fby::Entity ScopedEntity = InRegistry.create();
        InRegistry.emplace<Fby::Transform2D>(
            ScopedEntity, Fby::Transform2D{Fby::Vector2D{static_cast<float>(ScopedIndex % 16u) * 40.0f,
                                                         static_cast<float>(ScopedIndex / 16u) * 40.0f}});
        Fby::Light2DComponent ScopedLight;
        ScopedLight.Flags = Fby::kLight2DFlagEnabled;
        ScopedLight.Radius = 96.0f;
        ScopedLight.Intensity = 0.8f;
        ScopedLight.Color = Fby::Color32::FromBytes(255, 180, 220, 255);
        InRegistry.emplace<Fby::Light2DComponent>(ScopedEntity, ScopedLight);
    }
}

void AddSprites(Fby::Ecs::Registry& InRegistry, std::uint32_t InCount)
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        const Fby::Entity ScopedEntity = InRegistry.create();
        const float ScopedX = static_cast<float>(ScopedIndex % 160u) * 4.0f;
        const float ScopedY = static_cast<float>(ScopedIndex / 160u) * 4.0f;
        InRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Transform2D{Fby::Vector2D{ScopedX, ScopedY}});
        Fby::Sprite2D ScopedSprite;
        ScopedSprite.Size = Fby::Vector2D{24.0f, 24.0f};
        ScopedSprite.Layer = ScopedIndex % 4u;
        InRegistry.emplace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
        InRegistry.emplace<Fby::DrawOrder>(ScopedEntity,
                                           Fby::DrawOrder{static_cast<std::uint16_t>(ScopedIndex % 4u), 0.0f});
    }
}
}

int main(int argc, char** argv)
{
    std::uint32_t ScopedWidth = 640;
    std::uint32_t ScopedHeight = 480;
    std::uint32_t ScopedFrames = 120;
    std::uint32_t ScopedSprites = 15000;
    std::uint32_t ScopedLights = 64;
    std::uint32_t ScopedFramesInFlight = 3;
    bool ScopedFullView = false;
    bool ScopedStatic = false;
    bool ScopedRenderOnly = false;
    bool ScopedUseTarget = true;
    float ScopedRenderScale = 1.0f;
    std::string ScopedOnly;
    for (int ScopedArg = 1; ScopedArg < argc; ++ScopedArg)
    {
        const std::string_view ScopedOption(argv[ScopedArg]);
        if (ScopedOption == "--swapchain")
        {
            ScopedUseTarget = false;
            continue;
        }
        if (ScopedOption == "--fullview")
        {
            ScopedFullView = true;
            continue;
        }
        if (ScopedOption == "--static")
        {
            ScopedStatic = true;
            continue;
        }
        if (ScopedOption == "--render-only")
        {
            ScopedRenderOnly = true;
            continue;
        }
        if (ScopedArg + 1 >= argc)
        {
            break;
        }
        if (ScopedOption == "--width")
        {
            ScopedWidth = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--height")
        {
            ScopedHeight = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--frames")
        {
            ScopedFrames = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--sprites")
        {
            ScopedSprites = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--lights")
        {
            ScopedLights = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--frames-in-flight")
        {
            ScopedFramesInFlight = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--scale")
        {
            ScopedRenderScale = static_cast<float>(std::strtod(argv[ScopedArg + 1], nullptr));
            ++ScopedArg;
        }
        else if (ScopedOption == "--only")
        {
            ScopedOnly = argv[ScopedArg + 1];
            ++ScopedArg;
        }
    }
    ScopedWidth = std::max(ScopedWidth, 64u);
    ScopedHeight = std::max(ScopedHeight, 64u);
    ScopedRenderScale = std::clamp(ScopedRenderScale, 0.25f, 1.0f);
    const std::uint32_t ScopedRenderWidth =
        std::max(static_cast<std::uint32_t>(static_cast<float>(ScopedWidth) * ScopedRenderScale), 64u);
    const std::uint32_t ScopedRenderHeight =
        std::max(static_cast<std::uint32_t>(static_cast<float>(ScopedHeight) * ScopedRenderScale), 64u);
    Fby::SetLogLevel(Fby::LogLevel::Warn);
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        std::fprintf(stderr, "window init failed\n");
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - optimization A/B";
    ScopedWindowDesc.width = ScopedUseTarget ? 640 : static_cast<std::uint16_t>(ScopedWidth);
    ScopedWindowDesc.height = ScopedUseTarget ? 480 : static_cast<std::uint16_t>(ScopedHeight);
    ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        std::fprintf(stderr, "window create failed\n");
        return 1;
    }

    Fby::Ecs::Registry ScopedRegistry;
    AddSprites(ScopedRegistry, ScopedSprites);
    AddLights(ScopedRegistry, ScopedLights);
    Fby::JobSystem ScopedJobs(8);

    const std::array<Scenario, 8> ScopedScenarios{{
        {"baseline", false, false, false, false, false, false, false, false, true},
        {"prepass", true, false, false, false, false, false, false, false, true},
        {"prepass+cull", true, true, false, false, false, false, false, false, true},
        {"prepass+cull+hiz", true, true, true, false, false, false, false, false, true},
        {"front-to-back", false, false, false, false, false, false, true, false, true},
        {"+cluster", true, true, true, true, true, false, false, false, true},
        {"static-caches", true, true, true, true, true, true, false, false, false},
        {"baked-layer", false, false, false, false, false, false, false, true, false},
    }};

    std::array<std::byte, 4> ScopedWhite{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    Fby::Rhi::TextureArrayDesc ScopedTextureDesc;
    ScopedTextureDesc.size = {1, 1};
    ScopedTextureDesc.layers = 1;
    ScopedTextureDesc.mipLevels = 1;
    ScopedTextureDesc.srgb = false;

    const auto ScopedActualWindow = ScopedWindow.Size();
    const std::uint64_t ScopedColorBytes = static_cast<std::uint64_t>(ScopedRenderWidth) * ScopedRenderHeight * 4ull;
    const std::uint64_t ScopedDepthBytes = static_cast<std::uint64_t>(ScopedRenderWidth) * ScopedRenderHeight * 4ull;
    std::printf("resolution=%ux%u render=%ux%u window=%ux%u\n", ScopedWidth, ScopedHeight, ScopedRenderWidth,
                ScopedRenderHeight, ScopedActualWindow.width, ScopedActualWindow.height);
    std::printf("target_bytes=color=%llu depth=%llu total=%llu\n", static_cast<unsigned long long>(ScopedColorBytes),
                static_cast<unsigned long long>(ScopedDepthBytes),
                static_cast<unsigned long long>(ScopedColorBytes + ScopedDepthBytes));
    std::printf("scenario,cpu_ms,gpu_ms,depth_ms,main_ms,cluster_ms,postfx_ms,fps,draws,depth_items,gpu_main_items,"
                "cull_hits,cluster_hits,lost,valid,timestamp_result,uploaded_bytes\n");
    for (const Scenario& ScopedScenario : ScopedScenarios)
    {
        if (!ScopedOnly.empty() && ScopedOnly != ScopedScenario.name)
        {
            continue;
        }
        Fby::Rhi::DeviceDesc ScopedDesc;
        ScopedDesc.backend = Fby::Rhi::Backend::Vulkan;
        ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
        ScopedDesc.size =
            ScopedUseTarget ? Fby::Rhi::Extent2D{640, 480} : Fby::Rhi::Extent2D{ScopedWidth, ScopedHeight};
        ScopedDesc.vsync = false;
        ScopedDesc.framesInFlight = std::clamp(ScopedFramesInFlight, 1u, 3u);
        ScopedDesc.enableDepthPrepass = ScopedScenario.depthPrepass;
        ScopedDesc.enableGpuCulling = ScopedScenario.gpuCulling;
        ScopedDesc.enableHiZ = ScopedScenario.hiZ;
        ScopedDesc.enableClusteredLighting = ScopedScenario.clustered;
        ScopedDesc.enableStaticCaches = ScopedScenario.staticCaches;

        std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
        if (!ScopedGpu)
        {
            std::printf("%s,unavailable\n", ScopedScenario.name);
            continue;
        }
        const Fby::Rhi::TextureArrayHandle ScopedTexture =
            ScopedGpu->CreateTextureArray(ScopedTextureDesc, std::span<const std::byte>(ScopedWhite));
        if (!ScopedTexture.IsValid())
        {
            std::printf("%s,texture-unavailable\n", ScopedScenario.name);
            ScopedGpu.reset();
            continue;
        }
        Fby::Rhi::RenderTargetHandle ScopedTarget{};
        if (ScopedUseTarget)
        {
            Fby::Rhi::RenderTargetDesc ScopedTargetDesc;
            ScopedTargetDesc.size = {ScopedRenderWidth, ScopedRenderHeight};
            ScopedTarget = ScopedGpu->CreateRenderTarget(ScopedTargetDesc);
            if (!ScopedTarget.IsValid())
            {
                std::printf("%s,target-unavailable\n", ScopedScenario.name);
                ScopedGpu->DestroyTextureArray(ScopedTexture);
                ScopedGpu.reset();
                continue;
            }
        }

        Fby::Rhi::FrameComposer ScopedComposer;
        ScopedComposer.SetJobSystem(&ScopedJobs);
        ScopedComposer.SetInstanceDeltasEnabled(ScopedDesc.framesInFlight == 1);
        ScopedComposer.SetGpuLighting(ScopedScenario.gpuLighting);
        ScopedComposer.SetOpaqueLayerModes(std::array<Fby::Assets::TextureAlphaMode, 4>{
            Fby::Assets::TextureAlphaMode::Opaque, Fby::Assets::TextureAlphaMode::Opaque,
            Fby::Assets::TextureAlphaMode::Opaque, Fby::Assets::TextureAlphaMode::Opaque});
        ScopedComposer.SetOpaqueFrontToBack(ScopedScenario.opaqueFrontToBack);
        Fby::Rhi::CameraData ScopedCamera{
            Fby::Vector2D{static_cast<float>(ScopedWidth) * 0.5f, static_cast<float>(ScopedHeight) * 0.5f},
            ScopedFullView ? Fby::Vector2D{100000.0f, 100000.0f}
                           : Fby::Vector2D{static_cast<float>(ScopedWidth), static_cast<float>(ScopedHeight)},
            1.0f};

        const std::uint32_t kMeasured = std::max(ScopedFrames, 1u);
        const std::uint32_t kWarmup = std::min(20u, kMeasured / 4u);
        if (ScopedScenario.bake)
        {
            Fby::Rhi::StaticBakeRenderer ScopedBake;
            if (!ScopedGpu->BeginFrame())
            {
                std::printf("%s,bake-begin-failed\n", ScopedScenario.name);
                ScopedGpu->DestroyTextureArray(ScopedTexture);
                ScopedGpu.reset();
                continue;
            }
            ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::FromBytes(12, 14, 22),
                                            ScopedTexture.id);
            const bool ScopedRecorded =
                ScopedBake.Bake(*ScopedGpu, ScopedComposer.Packet(), {ScopedWidth, ScopedHeight});
            ScopedGpu->EndFrame();
            const bool ScopedFinalized = ScopedRecorded && ScopedBake.Finalize(*ScopedGpu);
            std::printf("%s,bake=%d\n", ScopedScenario.name, ScopedFinalized ? 1 : 0);
            if (ScopedFinalized)
            {
                double ScopedCpuTotal = 0.0;
                double ScopedGpuTotal = 0.0;
                std::uint32_t ScopedGpuSamples = 0;
                Fby::Rhi::FrameStats ScopedLastStats{};
                for (std::uint32_t ScopedFrame = 0; ScopedFrame < kMeasured; ++ScopedFrame)
                {
                    if (!ScopedWindow.PumpEvents() || ScopedWindow.ShouldClose())
                    {
                        break;
                    }
                    if (!ScopedGpu->BeginFrame())
                    {
                        continue;
                    }
                    const auto ScopedStart = std::chrono::steady_clock::now();
                    ScopedGpu->Submit(ScopedBake.OverlayPacket());
                    ScopedGpu->EndFrame();
                    const auto ScopedEnd = std::chrono::steady_clock::now();
                    ScopedGpu->WaitIdle();
                    ScopedLastStats = ScopedGpu->Stats();
                    ScopedCpuTotal += std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count();
                    if (ScopedLastStats.gpuFrameMilliseconds > 0.0f)
                    {
                        ScopedGpuTotal += ScopedLastStats.gpuFrameMilliseconds;
                        ++ScopedGpuSamples;
                    }
                }
                const double ScopedCpuMs = ScopedCpuTotal / static_cast<double>(kMeasured);
                const double ScopedGpuMs =
                    ScopedGpuSamples > 0 ? ScopedGpuTotal / static_cast<double>(ScopedGpuSamples) : 0.0;
                std::printf("%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%u,%u,%u,%u,%u,%u,%u,%d\n", ScopedScenario.name,
                            ScopedCpuMs, ScopedGpuMs, 0.0, 0.0, 0.0, 0.0,
                            ScopedGpuMs > 0.0 ? 1000.0 / ScopedGpuMs : 0.0, ScopedLastStats.drawCalls, 0u, 1u, 0u, 0u,
                            0u, ScopedLastStats.gpuTimestampsValid ? 1u : 0u, ScopedLastStats.gpuTimestampResult);
            }
            ScopedBake.Release(*ScopedGpu);
            ScopedGpu->DestroyTextureArray(ScopedTexture);
            if (ScopedTarget.IsValid())
            {
                ScopedGpu->DestroyRenderTarget(ScopedTarget);
            }
            ScopedGpu.reset();
            continue;
        }
        double ScopedCpuTotal = 0.0;
        double ScopedGpuTotal = 0.0;
        std::uint32_t ScopedGpuSamples = 0;
        Fby::Rhi::FrameStats ScopedLastStats{};
        std::printf("running %s (%u frames%s)\n", ScopedScenario.name, kMeasured,
                    ScopedUseTarget ? ", offscreen target presented" : "");
        std::fflush(stdout);
        for (std::uint32_t ScopedFrame = 0; ScopedFrame < kWarmup + kMeasured; ++ScopedFrame)
        {
            if (ScopedScenario.animate && !ScopedStatic)
            {
                const Fby::Entity ScopedFirst =
                    ScopedRegistry.view<Fby::Transform2D>().begin() != ScopedRegistry.view<Fby::Transform2D>().end()
                        ? *ScopedRegistry.view<Fby::Transform2D>().begin()
                        : Fby::Entity{};
                if (ScopedFirst != Fby::Entity{})
                {
                    auto& ScopedTransform = ScopedRegistry.get<Fby::Transform2D>(ScopedFirst);
                    ScopedTransform.Position.X = 320.0f + static_cast<float>(ScopedFrame % 64u) * 0.5f;
                }
            }
            if (!ScopedWindow.PumpEvents() || ScopedWindow.ShouldClose())
            {
                break;
            }
            if (!ScopedGpu->BeginFrame())
            {
                continue;
            }
            const auto ScopedStart = std::chrono::steady_clock::now();
            ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::FromBytes(12, 14, 22),
                                            ScopedTexture.id);
            if (ScopedUseTarget)
            {
                if (!ScopedGpu->RenderToTarget(ScopedTarget, ScopedComposer.Packet()))
                {
                    std::printf("%s,target-render-failed\n", ScopedScenario.name);
                }
                Fby::Rhi::FramePacket ScopedPresentPacket;
                ScopedPresentPacket.clear = Fby::Color32::FromBytes(0, 0, 0);
                ScopedGpu->Submit(ScopedPresentPacket);
                if (!ScopedRenderOnly)
                {
                    ScopedGpu->PresentRenderTarget(ScopedTarget);
                }
            }
            else
            {
                ScopedGpu->Submit(ScopedComposer.Packet());
            }
            ScopedGpu->EndFrame();
            const auto ScopedEnd = std::chrono::steady_clock::now();
            ScopedGpu->WaitIdle();
            ScopedLastStats = ScopedGpu->Stats();
            if (ScopedFrame >= kWarmup)
            {
                ScopedCpuTotal += std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count();
                if (ScopedLastStats.gpuFrameMilliseconds > 0.0f)
                {
                    ScopedGpuTotal += ScopedLastStats.gpuFrameMilliseconds;
                    ++ScopedGpuSamples;
                }
            }
        }
        ScopedGpu->WaitIdle();
        const double ScopedCpuMs = ScopedCpuTotal / static_cast<double>(kMeasured);
        const double ScopedGpuMs = ScopedGpuSamples > 0 ? ScopedGpuTotal / static_cast<double>(ScopedGpuSamples) : 0.0;
        const double ScopedFps = ScopedGpuMs > 0.0 ? 1000.0 / ScopedGpuMs : 0.0;
        std::printf("%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%u,%u,%u,%u,%u,%u,%u,%d,%llu\n", ScopedScenario.name,
                    ScopedCpuMs, ScopedGpuMs, ScopedLastStats.gpuDepthMilliseconds, ScopedLastStats.gpuMainMilliseconds,
                    ScopedLastStats.gpuClusterMilliseconds, ScopedLastStats.gpuPostFxMilliseconds, ScopedFps,
                    ScopedLastStats.drawCalls, ScopedLastStats.depthItems, ScopedLastStats.gpuMainItems,
                    ScopedLastStats.cullCacheHits, ScopedLastStats.clusterCacheHits,
                    ScopedLastStats.deviceLost ? 1u : 0u, ScopedLastStats.gpuTimestampsValid ? 1u : 0u,
                    ScopedLastStats.gpuTimestampResult, static_cast<unsigned long long>(ScopedLastStats.uploadedBytes));
        if (ScopedTarget.IsValid())
        {
            ScopedGpu->DestroyRenderTarget(ScopedTarget);
        }
        ScopedGpu->DestroyTextureArray(ScopedTexture);
        ScopedGpu.reset();
    }

    std::fflush(stdout);
    std::_Exit(0);
}
