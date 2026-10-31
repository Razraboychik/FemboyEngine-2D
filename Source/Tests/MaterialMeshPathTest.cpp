// FemboyEngine2D - shipped-build material path: cooked bytes to mesh items.
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialPipelineCache.hpp"
#include "Engine/RHI/Mesh.hpp"
#include "Engine/RHI/RHI.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace
{

int GChecks = 0;
int GFailures = 0;

void Check(bool InCondition, const char* InExpression, const char* InFile, int InLine)
{
    ++GChecks;
    if (!InCondition)
    {
        ++GFailures;
        std::fprintf(stderr, "FAIL %s:%d: %s\n", InFile, InLine, InExpression);
    }
}

#define FBY_CHECK(expression) Check((expression), #expression, __FILE__, __LINE__)

using namespace Fby::Render;

// A backend that only records the material ids the frame packet carried. The
// point of the test is that an Engine-only build reaches this point at all.
class MaterialRecordingRhi final : public Fby::Rhi::RHI
{
public:
    bool Initialize(const Fby::Rhi::DeviceDesc& InDesc) override
    {
        (void)InDesc;
        return true;
    }
    void Shutdown() override
    {
    }
    void Resize(Fby::Rhi::Extent2D InSize) override
    {
        (void)InSize;
    }
    void WaitIdle() override
    {
    }
    bool BeginFrame() override
    {
        return true;
    }
    void Submit(const Fby::Rhi::FramePacket& InPacket) override
    {
        SubmittedPipelines.clear();
        for (const Fby::Rhi::MeshRenderItem& ScopedItem : InPacket.meshItems)
        {
            SubmittedPipelines.push_back(ScopedItem.materialPipeline);
        }
    }
    void EndFrame() override
    {
    }
    Fby::Rhi::TextureArrayHandle CreateTextureArray(const Fby::Rhi::TextureArrayDesc& InDesc,
                                                   std::span<const std::byte> InPixels) override
    {
        (void)InDesc;
        (void)InPixels;
        return {};
    }
    void DestroyTextureArray(Fby::Rhi::TextureArrayHandle InHandle) override
    {
        (void)InHandle;
    }
    Fby::Rhi::ShaderPipelineHandle CreateShaderPipeline(const Fby::Rhi::ShaderPipelineDesc& InDesc) override
    {
        (void)InDesc;
        return Fby::Rhi::ShaderPipelineHandle{NextId++};
    }
    void DestroyShaderPipeline(Fby::Rhi::ShaderPipelineHandle InHandle) override
    {
        (void)InHandle;
        ++Destroyed;
    }
    const Fby::Rhi::DeviceInfo& Info() const noexcept override
    {
        return MInfo;
    }
    Fby::Rhi::FrameStats Stats() const noexcept override
    {
        return {};
    }
    Fby::Rhi::Backend GetBackend() const noexcept override
    {
        return Fby::Rhi::Backend::Null;
    }
    bool SupportsRuntimeShaderPipelines() const noexcept override
    {
        return true;
    }
    std::size_t TransientCapacity() const noexcept override
    {
        return 0;
    }

    std::vector<std::uint32_t> SubmittedPipelines;
    std::uint32_t Destroyed = 0;

private:
    Fby::Rhi::DeviceInfo MInfo{};
    std::uint32_t NextId = 1;
};

std::span<const std::byte> MinimalStage()
{
    static const std::vector<std::uint32_t> GStage = {kSpirvMagicWord, 0u, 0u, 0u, 0u};
    return {reinterpret_cast<const std::byte*>(GStage.data()), GStage.size() * sizeof(std::uint32_t)};
}
}

int main()
{
    // The cook tool produces these bytes; a shipped build only sees the payload.
    std::vector<std::byte> ScopedPayloadBytes;
    FBY_CHECK(EncodeCookedMaterial(0x5EED5EEDull, MaterialContract::MaterialContractNone, {}, MinimalStage(),
                                   &ScopedPayloadBytes));

    CookedMaterialView ScopedMaterial;
    FBY_CHECK(DecodeCookedMaterial(ScopedPayloadBytes, &ScopedMaterial));
    FBY_CHECK(ScopedMaterial.IsValid());

    MaterialRecordingRhi ScopedRhi;
    MaterialPipelineCache ScopedCache;
    const Fby::Rhi::ShaderPipelineHandle ScopedPipeline = ScopedCache.Acquire(ScopedRhi, ScopedMaterial);
    FBY_CHECK(ScopedPipeline.IsValid());

    // Two meshes share the material; a third keeps the engine shader.
    Fby::Rhi::MeshRenderItem ScopedFirst;
    Fby::Rhi::MeshRenderItem ScopedSecond;
    Fby::Rhi::MeshRenderItem ScopedPlain;
    FBY_CHECK(ScopedFirst.materialPipeline == Fby::Rhi::kDefaultMaterialPipeline);
    ScopedFirst.materialPipeline = ScopedPipeline.id;
    ScopedSecond.materialPipeline = ScopedPipeline.id;

    const Fby::Rhi::MeshRenderItem ScopedItems[3] = {ScopedFirst, ScopedSecond, ScopedPlain};
    Fby::Rhi::FramePacket ScopedPacket;
    ScopedPacket.meshItems = ScopedItems;
    ScopedRhi.Submit(ScopedPacket);

    FBY_CHECK(ScopedRhi.SubmittedPipelines.size() == 3);
    FBY_CHECK(ScopedRhi.SubmittedPipelines[0] == ScopedPipeline.id);
    FBY_CHECK(ScopedRhi.SubmittedPipelines[1] == ScopedPipeline.id);
    FBY_CHECK(ScopedRhi.SubmittedPipelines[2] == Fby::Rhi::kDefaultMaterialPipeline);

    // A second acquire of the same graph shares the pipeline instead of
    // recompiling; the reference count is what keeps it alive.
    FBY_CHECK(ScopedCache.Acquire(ScopedRhi, ScopedMaterial).id == ScopedPipeline.id);
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedPipeline) == 2);
    FBY_CHECK(ScopedCache.Release(ScopedPipeline));
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedPipeline) == 1);
    FBY_CHECK(ScopedCache.LiveCount() == 1);

    ScopedCache.Shutdown(ScopedRhi);
    FBY_CHECK(ScopedCache.LiveCount() == 0);
    FBY_CHECK(ScopedRhi.Destroyed == 1);

    if (GFailures != 0)
    {
        std::fprintf(stderr, "material_mesh_path: %d/%d checks failed\n", GFailures, GChecks);
        return 1;
    }
    std::printf("material_mesh_path: %d checks passed\n", GChecks);
    return 0;
}
