// FemboyEngine2D - MaterialGraph -> DXIL -> D3D12 material draw end-to-end.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialGraph.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

Fby::Render::MaterialGraph BuildGraph()
{
    using namespace Fby::Render;
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedUv = ScopedGraph.AddInputNode(MaterialNodeKind::Uv);
    const std::uint16_t ScopedSample = ScopedGraph.AddTextureSampleNode(0);
    const std::uint16_t ScopedTint = ScopedGraph.AddColorNode(0.8f, 0.4f, 0.2f, 1.0f);
    const std::uint16_t ScopedMultiply = ScopedGraph.AddBinaryNode(MaterialNodeKind::Mul);
    (void)ScopedGraph.Connect(ScopedSample, 0, ScopedUv);
    (void)ScopedGraph.Connect(ScopedMultiply, 0, ScopedSample);
    (void)ScopedGraph.Connect(ScopedMultiply, 1, ScopedTint);
    (void)ScopedGraph.Connect(ScopedGraph.OutputNode(), 0, ScopedMultiply);
    return ScopedGraph;
}

bool WriteBytes(const std::filesystem::path& InPath, std::span<const std::byte> InBytes)
{
    std::ofstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
    return static_cast<bool>(ScopedStream);
}

bool ReadBytes(const std::filesystem::path& InPath, std::vector<std::byte>* OutBytes)
{
    std::ifstream ScopedStream(InPath, std::ios::binary | std::ios::ate);
    if (!ScopedStream)
    {
        return false;
    }
    const std::streamsize ScopedSize = ScopedStream.tellg();
    if (ScopedSize <= 0)
    {
        return false;
    }
    OutBytes->resize(static_cast<std::size_t>(ScopedSize));
    ScopedStream.seekg(0, std::ios::beg);
    ScopedStream.read(reinterpret_cast<char*>(OutBytes->data()), ScopedSize);
    return static_cast<bool>(ScopedStream);
}
}

int main()
{
    using namespace Fby;
    const std::filesystem::path ScopedRoot = std::filesystem::current_path() / "fby_material_dx12_e2e";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot, ScopedError);
    if (ScopedError)
    {
        return 1;
    }
    _putenv_s("FBY_D3D12_PSO_CACHE", (ScopedRoot / "pso.bin").string().c_str());
    const Render::MaterialGraph ScopedGraph = BuildGraph();
    std::vector<std::byte> ScopedGraphBytes;
    Check(Render::EncodeMaterialGraph(ScopedGraph, &ScopedGraphBytes), "graph encodes");
    const std::filesystem::path ScopedGraphPath = ScopedRoot / "tinted.fgraph";
    const std::filesystem::path ScopedMaterialPath = ScopedRoot / "tinted.femmat";
    Check(WriteBytes(ScopedGraphPath, ScopedGraphBytes), "graph writes");

    const std::string ScopedCommand = std::string(FBY_MATC_PATH) + " \"" + ScopedGraphPath.string() + "\" -o \"" +
                                      ScopedMaterialPath.string() + "\" --dx12";
    Check(std::system(ScopedCommand.c_str()) == 0, "matc dx12 succeeds");

    std::vector<std::byte> ScopedMaterialBytes;
    Check(ReadBytes(ScopedMaterialPath, &ScopedMaterialBytes), "material reads");
    Render::CookedMaterialView ScopedView;
    Check(Render::DecodeCookedMaterial(ScopedMaterialBytes, &ScopedView), "material decodes");
    if (!ScopedView.IsValid() || ScopedView.fragmentDxil.empty())
    {
    if (GFailures == 0)
    {
        std::filesystem::remove_all(ScopedRoot, ScopedError);
    }
        std::printf("material_dx12_e2e: skipped (dxc/DXIL unavailable)\n");
        std::filesystem::remove_all(ScopedRoot, ScopedError);
        return GFailures == 0 ? 0 : 1;
    }

    WindowManager ScopedWindow;
    Check(ScopedWindow.Initialize(), "window initializes");
    WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - material DX12";
    ScopedWindowDesc.width = 320;
    ScopedWindowDesc.height = 240;
    Check(ScopedWindow.Create(ScopedWindowDesc), "window creates");
    Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Rhi::Backend::D3D12;
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {320, 240};
    std::unique_ptr<Rhi::RHI> ScopedGpu = Rhi::RHI::Create(ScopedDesc);
    Check(ScopedGpu != nullptr, "D3D12 initializes");
    if (!ScopedGpu)
    {
        std::filesystem::remove_all(ScopedRoot, ScopedError);
        return 1;
    }
    Rhi::ShaderPipelineDesc ScopedPipelineDesc;
    ScopedPipelineDesc.domain = Rhi::ShaderPipelineDomain::Sprite;
    ScopedPipelineDesc.fragmentDxil = ScopedView.fragmentDxil;
    ScopedPipelineDesc.debugName = "tinted e2e";
    const Rhi::ShaderPipelineHandle ScopedPipeline = ScopedGpu->CreateShaderPipeline(ScopedPipelineDesc);
    Check(ScopedPipeline.IsValid(), "material pipeline creates");

    std::array<std::byte, 32> ScopedWhite{};
    ScopedWhite.fill(std::byte{0xFF});
    Rhi::TextureArrayDesc ScopedTextureDesc;
    ScopedTextureDesc.size = {1, 1};
    ScopedTextureDesc.layers = 1;
    ScopedTextureDesc.srgb = false;
    const Rhi::TextureArrayHandle ScopedTexture =
        ScopedGpu->CreateTextureArray(ScopedTextureDesc, std::span<const std::byte>(ScopedWhite));
    Check(ScopedTexture.IsValid(), "texture creates");
    std::array<Rhi::RenderItem, 1> ScopedItems{};
    ScopedItems[0].position = {0.0f, 0.0f};
    ScopedItems[0].size = {64.0f, 64.0f};
    const std::array<Rhi::DrawBatch, 1> ScopedBatches{{{0, 1, ScopedPipeline.id, 0}}};
    Rhi::FramePacket ScopedPacket;
    ScopedPacket.items = ScopedItems;
    ScopedPacket.batches = ScopedBatches;
    ScopedPacket.textureArray = ScopedTexture.id;
    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        Check(ScopedGpu->BeginFrame(), "BeginFrame");
        ScopedGpu->Submit(ScopedPacket);
        ScopedGpu->EndFrame();
    }
    ScopedGpu->WaitIdle();
    ScopedGpu->DestroyShaderPipeline(ScopedPipeline);
    ScopedGpu->DestroyTextureArray(ScopedTexture);
    ScopedGpu->Shutdown();
    if (GFailures == 0)
    {
        std::filesystem::remove_all(ScopedRoot, ScopedError);
    }
    std::printf("material_dx12_e2e: %s\n", GFailures == 0 ? "ok" : "failed");
    return GFailures == 0 ? 0 : 1;
}
