// FemboyEngine2D - normal-map lighting visual contract.
//
// Renders one quad twice through the real RHI: a flat normal layer and a
// tangent normal layer lit by the same point light. The captured centre pixel
// must differ; a shader that ignores the normal map returns identical pixels.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
constexpr std::uint32_t kExtent = 64;
constexpr std::uint32_t kCenter = kExtent / 2;

struct Pixel
{
    std::uint8_t R;
    std::uint8_t G;
    std::uint8_t B;
    std::uint8_t A;
};

Pixel CaptureCenter(Fby::Rhi::RHI& InGpu, Fby::Rhi::RenderTargetHandle InTarget,
                    Fby::Rhi::FramePacket InPacket)
{
    if (!InGpu.BeginFrame() || !InGpu.RenderToTarget(InTarget, InPacket))
    {
        return Pixel{};
    }
    InGpu.PresentRenderTarget(InTarget);
    InGpu.EndFrame();
    InGpu.WaitIdle();
    std::array<std::byte, kExtent * kExtent * 4> ScopedPixels{};
    if (!InGpu.CaptureRenderTarget(InTarget, ScopedPixels))
    {
        return Pixel{};
    }
    const std::size_t ScopedOffset = (static_cast<std::size_t>(kCenter) * kExtent + kCenter) * 4u;
    return Pixel{static_cast<std::uint8_t>(ScopedPixels[ScopedOffset + 0u]),
                 static_cast<std::uint8_t>(ScopedPixels[ScopedOffset + 1u]),
                 static_cast<std::uint8_t>(ScopedPixels[ScopedOffset + 2u]),
                 static_cast<std::uint8_t>(ScopedPixels[ScopedOffset + 3u])};
}

bool RenderNormal(Fby::Rhi::Backend InBackend, std::uint32_t InNormalLayer, Pixel* OutPixel,
                  bool* OutDeviceCreated)
{
    *OutDeviceCreated = false;
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return false;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - normal map visual";
    ScopedWindowDesc.width = kExtent;
    ScopedWindowDesc.height = kExtent;
    ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return false;
    }
    Fby::Rhi::DeviceDesc ScopedDevice;
    ScopedDevice.backend = InBackend;
    ScopedDevice.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDevice.size = {kExtent, kExtent};
    ScopedDevice.vsync = false;
    const std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDevice);
    if (!ScopedGpu)
    {
        return false;
    }
    *OutDeviceCreated = true;

    std::array<std::byte, 3u * 4u> ScopedTexels{};
    const auto SetTexel = [&ScopedTexels](std::uint32_t InLayer, std::uint8_t InR, std::uint8_t InG,
                                          std::uint8_t InB, std::uint8_t InA)
    {
        ScopedTexels[InLayer * 4u + 0u] = static_cast<std::byte>(InR);
        ScopedTexels[InLayer * 4u + 1u] = static_cast<std::byte>(InG);
        ScopedTexels[InLayer * 4u + 2u] = static_cast<std::byte>(InB);
        ScopedTexels[InLayer * 4u + 3u] = static_cast<std::byte>(InA);
    };
    SetTexel(0u, 255u, 255u, 255u, 255u);
    SetTexel(1u, 128u, 128u, 255u, 255u);
    SetTexel(2u, 255u, 128u, 128u, 255u);

    Fby::Rhi::TextureArrayDesc ScopedTextureDesc;
    ScopedTextureDesc.size = {1, 1};
    ScopedTextureDesc.layers = 3;
    ScopedTextureDesc.srgb = false;
    const Fby::Rhi::TextureArrayHandle ScopedTexture =
        ScopedGpu->CreateTextureArray(ScopedTextureDesc, std::span<const std::byte>(ScopedTexels));
    if (!ScopedTexture.IsValid())
    {
        return false;
    }

    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Transform2D{Fby::Vector2D{0.0f, 0.0f}});
    Fby::Sprite2D ScopedSprite;
    ScopedSprite.Size = Fby::Vector2D{static_cast<float>(kExtent), static_cast<float>(kExtent)};
    ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
    ScopedRegistry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{0u, 0.0f});
    ScopedRegistry.emplace<Fby::NormalMap2D>(ScopedEntity, Fby::NormalMap2D{InNormalLayer, 1.0f});
    Fby::Light2DComponent ScopedLight{};
    ScopedLight.Flags = Fby::kLight2DFlagEnabled;
    ScopedLight.Radius = 100.0f;
    ScopedLight.Intensity = 1.0f;
    ScopedLight.Color = Fby::Color32::White();
    const Fby::Entity ScopedLamp = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedLamp, Fby::Transform2D{Fby::Vector2D{20.0f, 0.0f}});
    ScopedRegistry.emplace<Fby::Light2DComponent>(ScopedLamp, ScopedLight);

    Fby::Rhi::FrameComposer ScopedComposer;
    ScopedComposer.SetGpuLighting(true);
    Fby::Rhi::CameraData ScopedCamera;
    ScopedCamera.viewport = Fby::Vector2D{static_cast<float>(kExtent), static_cast<float>(kExtent)};
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), ScopedTexture.id);
    const Fby::Rhi::FramePacket ScopedPacket = ScopedComposer.Packet();

    Fby::Rhi::RenderTargetDesc ScopedTargetDesc;
    ScopedTargetDesc.size = {kExtent, kExtent};
    ScopedTargetDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedTarget = ScopedGpu->CreateRenderTarget(ScopedTargetDesc);
    if (!ScopedTarget.IsValid())
    {
        ScopedGpu->DestroyTextureArray(ScopedTexture);
        return false;
    }
    *OutPixel = CaptureCenter(*ScopedGpu, ScopedTarget, ScopedPacket);
    ScopedGpu->DestroyRenderTarget(ScopedTarget);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    return true;
}

bool Different(const Pixel& InA, const Pixel& InB)
{
    const int ScopedDelta = std::abs(static_cast<int>(InA.R) - static_cast<int>(InB.R)) +
                            std::abs(static_cast<int>(InA.G) - static_cast<int>(InB.G)) +
                            std::abs(static_cast<int>(InA.B) - static_cast<int>(InB.B));
    return ScopedDelta > 4;
}
}

int main()
{
#if defined(_WIN32)
    int ScopedFailures = 0;
    for (const Fby::Rhi::Backend ScopedBackend :
         {Fby::Rhi::Backend::D3D11, Fby::Rhi::Backend::D3D12, Fby::Rhi::Backend::Vulkan})
    {
        Pixel ScopedFlat{};
        Pixel ScopedTangent{};
        bool ScopedFlatCreated = false;
        bool ScopedTangentCreated = false;
        if (!RenderNormal(ScopedBackend, 1u, &ScopedFlat, &ScopedFlatCreated) ||
            !RenderNormal(ScopedBackend, 2u, &ScopedTangent, &ScopedTangentCreated))
        {
            if (!ScopedFlatCreated && !ScopedTangentCreated)
            {
                std::fprintf(stderr, "normal_map_visual: backend %u is unavailable; skipped\n",
                             static_cast<unsigned>(ScopedBackend));
                continue;
            }
            std::fprintf(stderr, "normal_map_visual: backend %u render/capture failed\n",
                         static_cast<unsigned>(ScopedBackend));
            ++ScopedFailures;
            continue;
        }
        if (!Different(ScopedFlat, ScopedTangent))
        {
            std::fprintf(stderr, "normal_map_visual: backend %u ignored the normal map (%u,%u,%u vs %u,%u,%u)\n",
                         static_cast<unsigned>(ScopedBackend), ScopedFlat.R, ScopedFlat.G, ScopedFlat.B,
                         ScopedTangent.R, ScopedTangent.G, ScopedTangent.B);
            ++ScopedFailures;
        }
    }
    if (ScopedFailures == 0)
    {
        std::printf("normal_map_visual: all available backends responded to the normal map\n");
        return 0;
    }
    return 1;
#else
    std::printf("normal_map_visual: skipped outside Windows\n");
    return 0;
#endif
}
