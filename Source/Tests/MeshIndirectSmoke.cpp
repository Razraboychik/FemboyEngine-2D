// FemboyEngine2D - Vulkan multi-draw mesh smoke test.

#include "Engine/Core/Log.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace
{
Fby::Rhi::MeshVertex MakeVertex(float InX, float InY, float InU, float InV, std::uint32_t InColor)
{
    Fby::Rhi::MeshVertex ScopedVertex;
    ScopedVertex.position = {InX, InY};
    ScopedVertex.uv = {InU, InV};
    ScopedVertex.color = InColor;
    ScopedVertex.padding = 0;
    ScopedVertex.boneIndices[0] = 0;
    ScopedVertex.boneIndices[1] = 0;
    ScopedVertex.boneWeights[0] = 1.0f;
    ScopedVertex.boneWeights[1] = 0.0f;
    return ScopedVertex;
}
}

int main()
{
    Fby::SetLogLevel(Fby::LogLevel::Info);

    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        std::fprintf(stderr, "window init failed\n");
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - mesh indirect smoke";
    ScopedWindowDesc.width = 640;
    ScopedWindowDesc.height = 480;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        std::fprintf(stderr, "window create failed\n");
        return 1;
    }

    Fby::Rhi::DeviceDesc ScopedDeviceDesc;
    ScopedDeviceDesc.backend = Fby::Rhi::Backend::Vulkan;
    ScopedDeviceDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDeviceDesc.size = {640, 480};
    ScopedDeviceDesc.vsync = false;
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDeviceDesc);
    if (!ScopedGpu)
    {
        // A headless CI machine without Vulkan is not a mesh-path failure.
        std::fprintf(stderr, "no Vulkan device available; skipping\n");
        return 0;
    }

    constexpr std::uint16_t ScopedKIndices[6] = {0, 1, 2, 0, 2, 3};
    const std::array<Fby::Rhi::MeshVertex, 4> ScopedStaticVertices{
        MakeVertex(-80.0f, -80.0f, 0.0f, 0.0f, 0xFFFFFFFFu),
        MakeVertex(80.0f, -80.0f, 1.0f, 0.0f, 0xFFFFFFFFu),
        MakeVertex(80.0f, 80.0f, 1.0f, 1.0f, 0xFFFFFFFFu),
        MakeVertex(-80.0f, 80.0f, 0.0f, 1.0f, 0xFFFFFFFFu),
    };
    Fby::Rhi::MeshDesc ScopedStaticDesc;
    ScopedStaticDesc.vertices = ScopedStaticVertices;
    ScopedStaticDesc.indices = ScopedKIndices;
    ScopedStaticDesc.debugName = "smoke_static_quad";
    const Fby::Rhi::MeshHandle ScopedStaticMesh = ScopedGpu->CreateMesh(ScopedStaticDesc);
    if (!ScopedStaticMesh.IsValid())
    {
        std::fprintf(stderr, "CreateMesh failed\n");
        return 1;
    }

    const std::array<Fby::Rhi::MeshVertex, 4> ScopedDynamicVertices{
        MakeVertex(-40.0f, -120.0f, 0.0f, 0.0f, 0xFFFF8080u),
        MakeVertex(40.0f, -120.0f, 1.0f, 0.0f, 0xFFFF8080u),
        MakeVertex(40.0f, -40.0f, 1.0f, 1.0f, 0xFFFF8080u),
        MakeVertex(-40.0f, -40.0f, 0.0f, 1.0f, 0xFFFF8080u),
    };
    const std::array<Fby::Rhi::MeshVertex, 4> ScopedDynamicVertices2{
        MakeVertex(-20.0f, -180.0f, 0.0f, 0.0f, 0xFF80FF80u),
        MakeVertex(60.0f, -180.0f, 1.0f, 0.0f, 0xFF80FF80u),
        MakeVertex(60.0f, -140.0f, 1.0f, 1.0f, 0xFF80FF80u),
        MakeVertex(-20.0f, -140.0f, 0.0f, 1.0f, 0xFF80FF80u),
    };
    const std::array<Fby::Mat4, 2> ScopedPalette{Fby::Mat4::Identity(), Fby::Mat4::Translation({24.0f, 0.0f})};

    Fby::Rhi::MeshRenderItem ScopedStaticItem;
    ScopedStaticItem.mesh = ScopedStaticMesh;
    ScopedStaticItem.textureArray = 0xFFFFFFFFu;
    ScopedStaticItem.layer = 0;
    ScopedStaticItem.model = Fby::Mat4::Translation({-120.0f, 0.0f});

    Fby::Rhi::MeshRenderItem ScopedDynamicItem;
    ScopedDynamicItem.vertices = ScopedDynamicVertices;
    ScopedDynamicItem.indices = ScopedKIndices;
    ScopedDynamicItem.boneOffset = 0;
    ScopedDynamicItem.boneCount = 1;
    ScopedDynamicItem.textureArray = 0xFFFFFFFFu;
    ScopedDynamicItem.layer = 0;
    ScopedDynamicItem.model = Fby::Mat4::Translation({120.0f, 0.0f});

    Fby::Rhi::MeshRenderItem ScopedDynamicItem2;
    ScopedDynamicItem2.vertices = ScopedDynamicVertices2;
    ScopedDynamicItem2.indices = ScopedKIndices;
    ScopedDynamicItem2.boneOffset = 1;
    ScopedDynamicItem2.boneCount = 1;
    ScopedDynamicItem2.textureArray = 0xFFFFFFFFu;
    ScopedDynamicItem2.layer = 0;
    ScopedDynamicItem2.model = Fby::Mat4::Translation({120.0f, 40.0f});

    const std::array<Fby::Rhi::MeshRenderItem, 3> ScopedMeshItems{ScopedStaticItem, ScopedDynamicItem,
                                                                  ScopedDynamicItem2};
    Fby::Rhi::FramePacket ScopedPacket{};
    ScopedPacket.meshItems = ScopedMeshItems;
    ScopedPacket.boneMatrices = ScopedPalette;
    ScopedPacket.camera.viewport = {640.0f, 480.0f};
    ScopedPacket.camera.center = {0.0f, 0.0f};
    ScopedPacket.camera.zoom = 1.0f;
    ScopedPacket.clear = Fby::Color32::FromBytes(12, 14, 22);

    constexpr int ScopedKFrames = 30;
    int ScopedFrames = 0;
    while (ScopedFrames < ScopedKFrames && ScopedWindow.PumpEvents() && !ScopedWindow.ShouldClose())
    {
        if (!ScopedGpu->BeginFrame())
        {
            continue;
        }
        ScopedGpu->Submit(ScopedPacket);
        ScopedGpu->EndFrame();
        ++ScopedFrames;
    }
    ScopedGpu->WaitIdle();

    const Fby::Rhi::FrameStats ScopedStats = ScopedGpu->Stats();
    std::printf("mesh smoke: frames=%d items=%u draws=%u indirect=%u skinDispatches=%u skinned=%u\n", ScopedFrames,
                ScopedStats.meshItems, ScopedStats.meshDraws, ScopedStats.meshIndirectCalls, ScopedStats.skinDispatches,
                ScopedStats.skinnedVertices);
    ScopedGpu->DestroyMesh(ScopedStaticMesh);

    const bool ScopedOk = ScopedFrames == ScopedKFrames && ScopedStats.meshItems == 3 && ScopedStats.meshDraws == 3 &&
                          ScopedStats.meshIndirectCalls == 1 && ScopedStats.skinDispatches == 1 &&
                          ScopedStats.skinnedVertices == 8;
    return ScopedOk ? 0 : 2;
}
