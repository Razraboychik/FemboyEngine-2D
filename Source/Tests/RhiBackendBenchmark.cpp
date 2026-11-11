// FemboyEngine2D - backend-agnostic RHI frame benchmark.
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/RHI/RHI.hpp"

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
struct Options
{
    Fby::Rhi::Backend backend = Fby::Rhi::Backend::Vulkan;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t sprites = 10000;
    std::uint32_t frames = 120;
    std::uint32_t framesInFlight = 3;
    bool vsync = false;
    bool fullView = true;
    bool animate = true;
};

void AddSprites(Fby::Ecs::Registry& InRegistry, std::uint32_t InCount)
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        const Fby::Entity ScopedEntity = InRegistry.create();
        InRegistry.emplace<Fby::Transform2D>(
            ScopedEntity, Fby::Transform2D{Fby::Vector2D{static_cast<float>(ScopedIndex % 160u) * 4.0f,
                                                         static_cast<float>(ScopedIndex / 160u) * 4.0f}});
        Fby::Sprite2D ScopedSprite;
        ScopedSprite.Size = Fby::Vector2D{24.0f, 24.0f};
        ScopedSprite.Layer = ScopedIndex % 4u;
        InRegistry.emplace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
        InRegistry.emplace<Fby::DrawOrder>(ScopedEntity,
                                           Fby::DrawOrder{static_cast<std::uint16_t>(ScopedIndex % 4u), 0.0f});
    }
}

void ParseBackend(std::string_view InText, Fby::Rhi::Backend& OutBackend, bool& OutValid)
{
    OutValid = true;
    if (InText == "vulkan")
    {
        OutBackend = Fby::Rhi::Backend::Vulkan;
    }
    else if (InText == "metal")
    {
        OutBackend = Fby::Rhi::Backend::Metal;
    }
    else if (InText == "null")
    {
        OutBackend = Fby::Rhi::Backend::Null;
    }
    else
    {
        OutValid = false;
    }
}
}

int main(int argc, char** argv)
{
    Options ScopedOptions;
    for (int ScopedArg = 1; ScopedArg < argc; ++ScopedArg)
    {
        const std::string_view ScopedOption(argv[ScopedArg]);
        if (ScopedOption == "--vsync")
        {
            ScopedOptions.vsync = true;
            continue;
        }
        if (ScopedOption == "--static")
        {
            ScopedOptions.animate = false;
            continue;
        }
        if (ScopedArg + 1 >= argc)
        {
            break;
        }
        if (ScopedOption == "--backend")
        {
            bool ScopedValid = false;
            ParseBackend(argv[ScopedArg + 1], ScopedOptions.backend, ScopedValid);
            if (!ScopedValid)
            {
                std::fprintf(stderr, "unknown backend '%s'\n", argv[ScopedArg + 1]);
                return 2;
            }
            ++ScopedArg;
        }
        else if (ScopedOption == "--width")
        {
            ScopedOptions.width = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--height")
        {
            ScopedOptions.height = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--sprites")
        {
            ScopedOptions.sprites = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--frames")
        {
            ScopedOptions.frames = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
        else if (ScopedOption == "--frames-in-flight")
        {
            ScopedOptions.framesInFlight = static_cast<std::uint32_t>(std::strtoul(argv[ScopedArg + 1], nullptr, 10));
            ++ScopedArg;
        }
    }
    ScopedOptions.width = std::max(ScopedOptions.width, 64u);
    ScopedOptions.height = std::max(ScopedOptions.height, 64u);
    ScopedOptions.frames = std::max(ScopedOptions.frames, 1u);
    ScopedOptions.framesInFlight = std::clamp(ScopedOptions.framesInFlight, 1u, 3u);

    Fby::SetLogLevel(Fby::LogLevel::Warn);
    Fby::WindowManager ScopedWindow;
    const bool ScopedNeedWindow = ScopedOptions.backend != Fby::Rhi::Backend::Null;
    if (ScopedNeedWindow)
    {
        if (!ScopedWindow.Initialize())
        {
            std::fprintf(stderr, "window init failed\n");
            return 1;
        }
        Fby::WindowDesc ScopedWindowDesc;
        ScopedWindowDesc.title = "FemboyEngine - RHI benchmark";
        ScopedWindowDesc.width = static_cast<std::uint16_t>(ScopedOptions.width);
        ScopedWindowDesc.height = static_cast<std::uint16_t>(ScopedOptions.height);
        ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
        if (!ScopedWindow.Create(ScopedWindowDesc))
        {
            std::fprintf(stderr, "window create failed\n");
            return 1;
        }
    }

    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = ScopedOptions.backend;
    ScopedDesc.nativeWindow =
        ScopedOptions.backend == Fby::Rhi::Backend::Metal ? ScopedWindow.MetalLayer() : ScopedWindow.NativeWindow();
    ScopedDesc.size = {ScopedOptions.width, ScopedOptions.height};
    ScopedDesc.vsync = ScopedOptions.vsync;
    ScopedDesc.framesInFlight = ScopedOptions.framesInFlight;
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::printf("backend=%s,unavailable\n", Fby::Rhi::BackendName(ScopedOptions.backend));
        return 1;
    }

    const std::array<std::byte, 4> ScopedWhite{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    Fby::Rhi::TextureArrayDesc ScopedTextureDesc;
    ScopedTextureDesc.size = {1, 1};
    ScopedTextureDesc.layers = 1;
    ScopedTextureDesc.mipLevels = 1;
    ScopedTextureDesc.srgb = false;
    const Fby::Rhi::TextureArrayHandle ScopedTexture =
        ScopedGpu->CreateTextureArray(ScopedTextureDesc, std::span<const std::byte>(ScopedWhite));
    if (!ScopedTexture.IsValid())
    {
        std::printf("backend=%s,texture_unavailable\n", Fby::Rhi::BackendName(ScopedOptions.backend));
        return 1;
    }

    Fby::Rhi::RenderTargetDesc ScopedTargetDesc;
    ScopedTargetDesc.size = {ScopedOptions.width, ScopedOptions.height};
    const Fby::Rhi::RenderTargetHandle ScopedTarget = ScopedGpu->CreateRenderTarget(ScopedTargetDesc);
    if (!ScopedTarget.IsValid())
    {
        std::printf("backend=%s,target_unavailable\n", Fby::Rhi::BackendName(ScopedOptions.backend));
        ScopedGpu->DestroyTextureArray(ScopedTexture);
        return 1;
    }

    Fby::Ecs::Registry ScopedRegistry;
    AddSprites(ScopedRegistry, ScopedOptions.sprites);
    Fby::Entity ScopedAnimated = Fby::kNullEntity;
    for (const Fby::Entity ScopedEntity : ScopedRegistry.view<Fby::Transform2D>())
    {
        ScopedAnimated = ScopedEntity;
        break;
    }
    Fby::JobSystem ScopedJobs(8);
    Fby::Rhi::FrameComposer ScopedComposer;
    ScopedComposer.SetJobSystem(&ScopedJobs);
    ScopedComposer.SetInstanceDeltasEnabled(ScopedOptions.framesInFlight == 1);
    const Fby::Rhi::CameraData ScopedCamera{
        Fby::Vector2D{static_cast<float>(ScopedOptions.width) * 0.5f, static_cast<float>(ScopedOptions.height) * 0.5f},
        ScopedOptions.fullView
            ? Fby::Vector2D{100000.0f, 100000.0f}
            : Fby::Vector2D{static_cast<float>(ScopedOptions.width), static_cast<float>(ScopedOptions.height)},
        1.0f};

    const std::uint32_t ScopedWarmup = std::min(20u, ScopedOptions.frames / 4u);
    double ScopedCpuTotal = 0.0;
    double ScopedGpuTotal = 0.0;
    std::uint32_t ScopedGpuSamples = 0;
    Fby::Rhi::FrameStats ScopedLastStats{};
    bool ScopedRendered = true;
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < ScopedWarmup + ScopedOptions.frames; ++ScopedFrame)
    {
        if (ScopedNeedWindow && (!ScopedWindow.PumpEvents() || ScopedWindow.ShouldClose()))
        {
            break;
        }
        if (ScopedOptions.animate && ScopedAnimated != Fby::kNullEntity)
        {
            auto& ScopedTransform = ScopedRegistry.get<Fby::Transform2D>(ScopedAnimated);
            ScopedTransform.Position.X = 320.0f + static_cast<float>(ScopedFrame % 64u) * 0.5f;
        }
        if (!ScopedGpu->BeginFrame())
        {
            ScopedRendered = false;
            break;
        }
        const auto ScopedStart = std::chrono::steady_clock::now();
        ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), ScopedTexture.id);
        const bool ScopedFrameRendered = ScopedGpu->RenderToTarget(ScopedTarget, ScopedComposer.Packet());
        if (ScopedFrameRendered)
        {
            Fby::Rhi::FramePacket ScopedPresent;
            ScopedPresent.clear = Fby::Color32::Black();
            ScopedGpu->Submit(ScopedPresent);
            ScopedGpu->PresentRenderTarget(ScopedTarget);
        }
        const auto ScopedEnd = std::chrono::steady_clock::now();
        ScopedGpu->EndFrame();
        if (!ScopedFrameRendered)
        {
            ScopedRendered = false;
            break;
        }
        if (ScopedFrame >= ScopedWarmup)
        {
            ScopedCpuTotal += std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count();
            ScopedLastStats = ScopedGpu->Stats();
            if (ScopedLastStats.gpuFrameMilliseconds > 0.0f)
            {
                ScopedGpuTotal += ScopedLastStats.gpuFrameMilliseconds;
                ++ScopedGpuSamples;
            }
        }
    }
    ScopedGpu->WaitIdle();
    const double ScopedCpuMs = ScopedCpuTotal / static_cast<double>(ScopedOptions.frames);
    const double ScopedGpuMs = ScopedGpuSamples > 0 ? ScopedGpuTotal / static_cast<double>(ScopedGpuSamples) : 0.0;
    const double ScopedFps = ScopedGpuMs > 0.0 ? 1000.0 / ScopedGpuMs : 0.0;
    std::printf("backend,device,sprites,frames,cpu_ms,gpu_ms,fps,draws,items,batches,uploaded_bytes,rendered\n");
    std::printf("%s,%s,%u,%u,%.3f,%.3f,%.1f,%u,%u,%u,%llu,%d\n", Fby::Rhi::BackendName(ScopedOptions.backend),
                ScopedGpu->Info().name, ScopedOptions.sprites, ScopedOptions.frames, ScopedCpuMs, ScopedGpuMs,
                ScopedFps, ScopedLastStats.drawCalls, ScopedLastStats.items, ScopedLastStats.batches,
                static_cast<unsigned long long>(ScopedLastStats.uploadedBytes), ScopedRendered ? 1 : 0);

    ScopedGpu->DestroyRenderTarget(ScopedTarget);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    ScopedGpu.reset();
    return ScopedRendered ? 0 : 1;
}
