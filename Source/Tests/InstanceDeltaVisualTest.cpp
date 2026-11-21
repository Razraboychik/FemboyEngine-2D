// FemboyEngine2D - incremental instance upload visual equivalence.
//
// Renders a moving sprite through the Vulkan multi-frame dirty-range path and
// compares the captured frame with a full-upload reference for the same state.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
constexpr std::uint32_t kExtent = 64;

bool RenderAndCapture(Fby::Rhi::RHI& InGpu, Fby::Rhi::RenderTargetHandle InTarget,
                      const Fby::Rhi::FramePacket& InPacket, std::span<std::byte> OutPixels)
{
    if (!InGpu.BeginFrame() || !InGpu.RenderToTarget(InTarget, InPacket))
    {
        return false;
    }
    InGpu.PresentRenderTarget(InTarget);
    InGpu.EndFrame();
    InGpu.WaitIdle();
    return InGpu.CaptureRenderTarget(InTarget, OutPixels);
}
}

int main()
{
#if defined(_WIN32)
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - instance delta visual";
    ScopedWindowDesc.width = kExtent;
    ScopedWindowDesc.height = kExtent;
    ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }
    Fby::Rhi::DeviceDesc ScopedDevice;
    ScopedDevice.backend = Fby::Rhi::Backend::Vulkan;
    ScopedDevice.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDevice.size = {kExtent, kExtent};
    ScopedDevice.vsync = false;
    ScopedDevice.maxItemsPerFrame = 1024;
    const std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDevice);
    if (!ScopedGpu)
    {
        std::printf("instance_delta_visual: Vulkan unavailable\n");
        return 0;
    }

    std::array<std::byte, 4> ScopedTexel{};
    ScopedTexel.fill(std::byte{0xFF});
    Fby::Rhi::TextureArrayDesc ScopedTextureDesc;
    ScopedTextureDesc.size = {1, 1};
    ScopedTextureDesc.layers = 1;
    ScopedTextureDesc.srgb = false;
    const Fby::Rhi::TextureArrayHandle ScopedTexture =
        ScopedGpu->CreateTextureArray(ScopedTextureDesc, std::span<const std::byte>(ScopedTexel));
    if (!ScopedTexture.IsValid())
    {
        return 1;
    }

    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Transform2D{Fby::Vector2D{-20.0f, 0.0f}});
    Fby::Sprite2D ScopedSprite;
    ScopedSprite.Size = Fby::Vector2D{16.0f, 16.0f};
    ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
    ScopedRegistry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{0u, 0.0f});
    Fby::Rhi::CameraData ScopedCamera;
    ScopedCamera.viewport = Fby::Vector2D{static_cast<float>(kExtent), static_cast<float>(kExtent)};

    Fby::Rhi::FrameComposer ScopedDeltaComposer;
    ScopedDeltaComposer.SetInstanceDeltasEnabled(true);
    ScopedDeltaComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), ScopedTexture.id);

    Fby::Rhi::FrameComposer ScopedReferenceComposer;
    ScopedReferenceComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), ScopedTexture.id);

    Fby::Rhi::RenderTargetDesc ScopedTargetDesc;
    ScopedTargetDesc.size = {kExtent, kExtent};
    ScopedTargetDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedDeltaTarget = ScopedGpu->CreateRenderTarget(ScopedTargetDesc);
    const Fby::Rhi::RenderTargetHandle ScopedReferenceTarget = ScopedGpu->CreateRenderTarget(ScopedTargetDesc);
    if (!ScopedDeltaTarget.IsValid() || !ScopedReferenceTarget.IsValid())
    {
        return 1;
    }
    std::array<std::byte, kExtent * kExtent * 4> ScopedWarmup{};
    if (!RenderAndCapture(*ScopedGpu, ScopedDeltaTarget, ScopedDeltaComposer.Packet(), ScopedWarmup))
    {
        return 1;
    }

    ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X = 20.0f;
    ScopedDeltaComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), ScopedTexture.id);
    ScopedReferenceComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), ScopedTexture.id);
    if (!ScopedDeltaComposer.Packet().instanceRangesAreDeltas || ScopedDeltaComposer.Packet().instanceRanges.empty())
    {
        std::fprintf(stderr, "instance_delta_visual: moving sprite did not produce a dirty range\n");
        return 1;
    }

    std::array<std::byte, kExtent * kExtent * 4> ScopedDeltaPixels{};
    std::array<std::byte, kExtent * kExtent * 4> ScopedReferencePixels{};
    if (!RenderAndCapture(*ScopedGpu, ScopedDeltaTarget, ScopedDeltaComposer.Packet(), ScopedDeltaPixels) ||
        !RenderAndCapture(*ScopedGpu, ScopedReferenceTarget, ScopedReferenceComposer.Packet(), ScopedReferencePixels))
    {
        return 1;
    }
    if (std::memcmp(ScopedDeltaPixels.data(), ScopedReferencePixels.data(), ScopedDeltaPixels.size()) != 0)
    {
        std::fprintf(stderr, "instance_delta_visual: incremental frame differs from full upload\n");
        return 1;
    }
    std::printf("instance_delta_visual: incremental and full frames match\n");
    ScopedGpu->DestroyRenderTarget(ScopedDeltaTarget);
    ScopedGpu->DestroyRenderTarget(ScopedReferenceTarget);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    return 0;
#else
    std::printf("instance_delta_visual: skipped outside Windows\n");
    return 0;
#endif
}
