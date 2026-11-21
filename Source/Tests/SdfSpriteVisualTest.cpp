// FemboyEngine2D - SDF sprite pipeline bind/visual smoke.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdio>
#include <memory>

int main()
{
#if defined(_WIN32)
    constexpr std::uint32_t kExtent = 64;
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - SDF sprite";
    ScopedWindowDesc.width = kExtent;
    ScopedWindowDesc.height = kExtent;
    ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }
    Fby::Rhi::DeviceDesc ScopedDevice;
    ScopedDevice.backend = Fby::Rhi::Backend::D3D12;
    ScopedDevice.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDevice.size = {kExtent, kExtent};
    ScopedDevice.vsync = false;
    ScopedDevice.maxItemsPerFrame = 1024;
    const std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDevice);
    if (!ScopedGpu)
    {
        std::printf("sdf_sprite_visual: D3D12 unavailable\n");
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
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Transform2D{Fby::Vector2D{0.0f, 0.0f}});
    Fby::Sprite2D ScopedSprite;
    ScopedSprite.Size = Fby::Vector2D{static_cast<float>(kExtent), static_cast<float>(kExtent)};
    ScopedSprite.Flags = Fby::Rhi::kRenderFlagSdfText;
    ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
    ScopedRegistry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{0u, 0.0f});
    Fby::Rhi::FrameComposer ScopedComposer;
    Fby::Rhi::CameraData ScopedCamera;
    ScopedCamera.viewport = Fby::Vector2D{static_cast<float>(kExtent), static_cast<float>(kExtent)};
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), ScopedTexture.id);
    Fby::Rhi::RenderTargetDesc ScopedTargetDesc;
    ScopedTargetDesc.size = {kExtent, kExtent};
    ScopedTargetDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedTarget = ScopedGpu->CreateRenderTarget(ScopedTargetDesc);
    if (!ScopedTarget.IsValid())
    {
        return 1;
    }
    if (!ScopedGpu->BeginFrame() || !ScopedGpu->RenderToTarget(ScopedTarget, ScopedComposer.Packet()))
    {
        std::printf("sdf_sprite_visual: render failed\n");
        return 1;
    }
    ScopedGpu->PresentRenderTarget(ScopedTarget);
    ScopedGpu->EndFrame();
    ScopedGpu->WaitIdle();
    std::array<std::byte, kExtent * kExtent * 4> ScopedPixels{};
    if (!ScopedGpu->CaptureRenderTarget(ScopedTarget, ScopedPixels))
    {
        return 1;
    }
    const std::size_t ScopedCenter = ((kExtent / 2) * kExtent + (kExtent / 2)) * 4u;
    const bool ScopedPainted = ScopedPixels[ScopedCenter + 0u] != std::byte{0} ||
                               ScopedPixels[ScopedCenter + 1u] != std::byte{0} ||
                               ScopedPixels[ScopedCenter + 2u] != std::byte{0};
    if (!ScopedPainted)
    {
        std::fprintf(stderr, "sdf_sprite_visual: SDF pipeline produced an empty frame\n");
        return 1;
    }
    std::printf("sdf_sprite_visual: SDF pipeline painted the sprite\n");
    ScopedGpu->DestroyRenderTarget(ScopedTarget);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    return 0;
#else
    return 0;
#endif
}
