// FemboyEngine2D - Vulkan RHI device/swapchain/FrameComposer smoke.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
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
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    std::printf("vulkan_rhi: device=%s api=%u.%u.%u tier=%s frames=3\n", ScopedGpu->Info().name,
                (ScopedGpu->Info().apiVersion >> 22) & 0x7Fu, (ScopedGpu->Info().apiVersion >> 12) & 0x3FFu,
                ScopedGpu->Info().apiVersion & 0xFFFu, Fby::Rhi::GpuTierName(ScopedGpu->Info().tier));
    ScopedGpu->Shutdown();
    return 0;
}
