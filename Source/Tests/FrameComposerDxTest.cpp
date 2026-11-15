// FemboyEngine2D - DX FrameComposer batch smoke.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

int main()
{
#if defined(_WIN32)
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - FrameComposer DX";
    ScopedWindowDesc.width = 320;
    ScopedWindowDesc.height = 240;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }
    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Fby::Rhi::Backend::D3D12;
    if (const char* ScopedBackend = std::getenv("FBY_FRAME_COMPOSER_BACKEND"); ScopedBackend != nullptr &&
        std::strcmp(ScopedBackend, "d3d11") == 0)
    {
        ScopedDesc.backend = Fby::Rhi::Backend::D3D11;
    }
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {320, 240};
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::printf("frame_composer_dx: skipped\n");
        return 0;
    }
    std::array<std::byte, 32> ScopedWhite{};
    ScopedWhite.fill(std::byte{0xFF});
    Fby::Rhi::TextureArrayDesc ScopedTextureDesc;
    ScopedTextureDesc.size = {1, 1};
    ScopedTextureDesc.layers = 8;
    ScopedTextureDesc.srgb = false;
    const Fby::Rhi::TextureArrayHandle ScopedTexture =
        ScopedGpu->CreateTextureArray(ScopedTextureDesc, std::span<const std::byte>(ScopedWhite));
    if (!ScopedTexture.IsValid())
    {
        std::fprintf(stderr, "frame_composer_dx: texture failed\n");
        return 1;
    }
    Fby::Ecs::Registry ScopedRegistry;
    for (int ScopedIndex = 0; ScopedIndex < 64; ++ScopedIndex)
    {
        const Fby::Entity ScopedEntity = ScopedRegistry.create();
        Fby::Transform2D ScopedTransform;
        ScopedTransform.Position = {static_cast<float>(ScopedIndex % 8) * 24.0f - 84.0f,
                                    static_cast<float>(ScopedIndex / 8) * 24.0f - 84.0f};
        ScopedTransform.Rotation = static_cast<float>(ScopedIndex) * 0.05f;
        ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
        Fby::Sprite2D ScopedSprite;
        ScopedSprite.Size = {20.0f, 20.0f};
        ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
        ScopedRegistry.emplace<Fby::DrawOrder>(
            ScopedEntity, Fby::DrawOrder{static_cast<std::uint16_t>(ScopedIndex % 4),
                                         static_cast<float>(ScopedIndex % 4)});
    }
    Fby::Rhi::FrameComposer ScopedComposer;
    Fby::Rhi::CameraData ScopedCamera;
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), ScopedTexture.id);
    Fby::Rhi::FramePacket ScopedPacket = ScopedComposer.Packet();
    std::array<Fby::Rhi::MeshVertex, 3> ScopedMeshVertices{};
    ScopedMeshVertices[0].position = {-0.5f, -0.5f};
    ScopedMeshVertices[0].uv = {0.0f, 0.0f};
    ScopedMeshVertices[1].position = {0.5f, -0.5f};
    ScopedMeshVertices[1].uv = {1.0f, 0.0f};
    ScopedMeshVertices[2].position = {0.0f, 0.5f};
    ScopedMeshVertices[2].uv = {0.5f, 1.0f};
    std::array<std::uint16_t, 3> ScopedMeshIndices{0, 1, 2};
    std::array<Fby::Mat4, 1> ScopedBoneMatrices{};
    ScopedBoneMatrices[0] = Fby::Mat4::Identity();
    for (Fby::Rhi::MeshVertex& ScopedVertex : ScopedMeshVertices)
    {
        ScopedVertex.boneIndices[0] = 0;
        ScopedVertex.boneWeights[0] = 1.0f;
    }
    Fby::Rhi::MeshDesc ScopedMeshDesc;
    ScopedMeshDesc.vertices = ScopedMeshVertices;
    ScopedMeshDesc.indices = ScopedMeshIndices;
    const Fby::Rhi::MeshHandle ScopedMesh = ScopedGpu->CreateMesh(ScopedMeshDesc);
    Fby::Rhi::MeshRenderItem ScopedMeshItem;
    ScopedMeshItem.mesh = ScopedMesh;
    ScopedMeshItem.textureArray = ScopedTexture.id;
    ScopedMeshItem.boneOffset = 0;
    ScopedMeshItem.boneCount = 1;
    ScopedPacket.meshItems = std::span<const Fby::Rhi::MeshRenderItem>(&ScopedMeshItem, 1);
    ScopedPacket.boneMatrices = ScopedBoneMatrices;
    if (ScopedPacket.items.empty())
    {
        std::fprintf(stderr, "frame_composer_dx: empty packet\n");
        return 1;
    }
    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        if (!ScopedGpu->BeginFrame())
        {
            std::fprintf(stderr, "frame_composer_dx: BeginFrame failed\n");
            return 1;
        }
        ScopedGpu->Submit(ScopedPacket);
        ScopedGpu->EndFrame();
    }
    ScopedGpu->WaitIdle();
    std::printf("frame_composer_dx: backend=%s items=%zu frames=3\n",
                ScopedGpu->GetBackend() == Fby::Rhi::Backend::D3D11 ? "d3d11" : "d3d12",
                ScopedPacket.items.size());
    ScopedGpu->DestroyMesh(ScopedMesh);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    ScopedGpu->Shutdown();
    return 0;
#else
    std::printf("frame_composer_dx: skipped (non-Windows)\n");
    return 0;
#endif
}
