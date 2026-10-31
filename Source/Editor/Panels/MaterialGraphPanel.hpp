// FemboyEngine2D - Material Graph editor + live SPIR-V pipeline.
#pragma once

#include "Editor/Infrastructure/MaterialGraphRuntime.hpp"
#include "Editor/Panels/NodeCanvasPanel.hpp"
#include "Engine/RHI/MaterialGraph.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"
#include "Engine/RHI/Mesh.hpp"
#include "Engine/RHI/RHI.hpp"

#include "Engine/RHI/MaterialAsset.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace Fby::Editor
{

class MaterialGraphPanel
{
public:
    // 0x4D415445 == "MATE"; the runtime reserves kDefaultMaterialPipeline.
    static constexpr std::uint32_t kDefaultMaterialId = 0x4D415445u;

    MaterialGraphPanel() = default;
    ~MaterialGraphPanel() = default;

    MaterialGraphPanel(const MaterialGraphPanel&) = delete;
    MaterialGraphPanel& operator=(const MaterialGraphPanel&) = delete;

    // Registers the graph as a CommandStack external buffer and, when a device
    [[nodiscard]] bool Initialize(CommandStack& InCommands, Rhi::RHI* InGpu,
                                  std::uint32_t InMaterialId = kDefaultMaterialId);
    // Destroys the runtime's cached GPU pipelines; call while `gpu` is alive.
    void Shutdown(Rhi::RHI& InGpu);

    void Draw(const char* InWindowTitle, bool* InOpen);

    // Graph -> GLSL -> SPIR-V -> pipeline. Returns false on validation/glslang/
    [[nodiscard]] bool ApplyShader();
    // Binds a MeshRenderItem; ApplyShader() writes
    void BindRenderItem(Rhi::MeshRenderItem* InItem) noexcept
    {
        MBoundItem = InItem;
    }
    // Applies the current pipeline id to any item, useful for a game viewport
    void ApplyPipeline(Rhi::MeshRenderItem& InItem) noexcept
    {
        InItem.materialPipeline = PipelineId();
    }

    // Mesh or sprite shader contract. The sprite variant keeps the engine quad
    // vertex stage and samples the item's atlas slice; the cook tool writes the
    // same variant from the same .fgraph with --sprite.
    void SetDomain(Render::MaterialDomain InDomain) noexcept
    {
        MRuntime.SetDomain(InDomain);
        MLastAppliedHash = 0;
    }
    [[nodiscard]] Render::MaterialDomain Domain() const noexcept
    {
        return MRuntime.Domain();
    }

    // Writes the graph as a .fgraph payload (the cooker input) and restores it.
    // A save/load pair keeps the editor and `fby_matc` on one format.
    [[nodiscard]] bool SaveGraph(const char* InPath);
    [[nodiscard]] bool LoadGraph(const char* InPath);

    [[nodiscard]] Render::MaterialGraph& Graph() noexcept
    {
        return MGraph;
    }
    [[nodiscard]] const Render::MaterialGraph& Graph() const noexcept
    {
        return MGraph;
    }
    [[nodiscard]] Rhi::ShaderPipelineHandle Pipeline() const noexcept
    {
        return MRuntime.Pipeline(MMaterialId);
    }
    [[nodiscard]] std::uint32_t PipelineId() const noexcept
    {
        return MRuntime.MaterialPipelineId(MMaterialId);
    }
    [[nodiscard]] const char* LastError() const noexcept
    {
        return MCompileError.data();
    }
    [[nodiscard]] const MaterialRuntimeStats& RuntimeStats() const noexcept
    {
        return MRuntime.Stats();
    }
    [[nodiscard]] std::uint32_t MaterialId() const noexcept
    {
        return MMaterialId;
    }
    [[nodiscard]] std::uint16_t Codec() const noexcept
    {
        return MCodec;
    }
    [[nodiscard]] NodeCanvasPanel& Canvas() noexcept
    {
        return MCanvas;
    }

private:
    static void SetError(char* InDestination, std::size_t InCapacity, const std::string& InText) noexcept;
    static void BuildDefaultGraph(Render::MaterialGraph& InGraph);
    static Render::MaterialNodeKind AddableKind(std::uint32_t InIndex) noexcept;
    static bool CopyNode(const Render::MaterialGraph& InSource, std::uint16_t InNode, Render::MaterialGraph& InTarget,
                         std::uint16_t* OutNode) noexcept;
    static bool RebuildWithoutNodes(Render::MaterialGraph& InGraph, std::span<const std::uint16_t> InNodes,
                                    std::string* InError);

    static std::uint32_t ModelNodeCount(void* InUser) noexcept;
    static const char* ModelNodeTitle(void* InUser, std::uint16_t InNode) noexcept;
    static ImVec2 ModelNodePosition(void* InUser, std::uint16_t InNode) noexcept;
    static void ModelSetNodePosition(void* InUser, std::uint16_t InNode, ImVec2 InPosition) noexcept;
    static std::uint8_t ModelInputCount(void* InUser, std::uint16_t InNode) noexcept;
    static const char* ModelInputName(void* InUser, std::uint16_t InNode, std::uint8_t InPin) noexcept;
    static std::uint8_t ModelOutputCount(void* InUser, std::uint16_t InNode) noexcept;
    static const char* ModelOutputName(void* InUser, std::uint16_t InNode, std::uint8_t InPin) noexcept;
    static std::uint32_t ModelLinkCount(void* InUser) noexcept;
    static void ModelLinkEndpoints(void* InUser, std::uint32_t InLink, std::uint16_t* InFromNode,
                                   std::uint8_t* InFromPin, std::uint16_t* InToNode, std::uint8_t* InToPin) noexcept;
    static bool ModelConnect(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin, std::uint16_t InFromNode,
                             std::uint8_t InFromPin, std::string* InError);
    static void ModelDisconnect(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin) noexcept;
    static bool ModelDeleteNodes(void* InUser, std::span<const std::uint16_t> InNodes, std::string* InError);
    static std::uint32_t ModelAddableCount(void* InUser) noexcept;
    static const char* ModelAddableName(void* InUser, std::uint32_t InAddable) noexcept;
    static bool ModelAddNode(void* InUser, std::uint32_t InAddable, ImVec2 InPosition, std::uint16_t* OutNode,
                             std::string* InError);
    static void ModelDrawNodeBody(void* InUser, std::uint16_t InNode) noexcept;
    static void ModelDrawPanelTools(void* InUser) noexcept;

    void DrawNodeInspector();
    void DrawShaderPreview();

    Render::MaterialGraph MGraph{};
    NodeCanvasPanel MCanvas{};
    MaterialGraphRuntime MRuntime{};
    Rhi::RHI* MGpu = nullptr;
    Rhi::MeshRenderItem* MBoundItem = nullptr;
    std::uint32_t MMaterialId = kDefaultMaterialId;
    std::uint16_t MCodec = 0xFFFFu;
    bool MRegistered = false;
    bool MInitialized = false;
    bool MAutoApply = false;
    std::uint64_t MLastAppliedHash = 0;
    std::array<char, 192> MGraphPath{};

    std::array<char, Render::kMaterialGlslFragmentBytes> MFragmentGlsl{};
    std::array<char, Render::kMaterialGlslVertexBytes> MVertexGlsl{};
    std::array<char, kCanvasStatusBytes> MCompileError{};
    Render::MaterialShaderProgram MProgram{};
    std::uint32_t MFragmentLength = 0;
    std::uint32_t MVertexLength = 0;
    bool MGlslValid = false;
};
}
