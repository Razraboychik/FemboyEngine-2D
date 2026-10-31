// FemboyEngine2D - engine-side material pipeline cache tests.
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialPipelineCache.hpp"
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

// Records what the cache asked the backend to do without touching a device. The
// Null backend deliberately refuses runtime pipelines, so a shipped-build test
// needs its own backend to prove the cache's reference counting.
class RecordingRhi final : public Fby::Rhi::RHI
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
        (void)InPacket;
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
        LastDomain = InDesc.domain;
        if (FailNextCreate)
        {
            FailNextCreate = false;
            return {};
        }
        ++Created;
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

    std::uint32_t Created = 0;
    std::uint32_t Destroyed = 0;
    bool FailNextCreate = false;
    Fby::Rhi::ShaderPipelineDomain LastDomain = Fby::Rhi::ShaderPipelineDomain::Mesh;

private:
    Fby::Rhi::DeviceInfo MInfo{};
    std::uint32_t NextId = 1;
};

// A five-word SPIR-V module. The cache never parses a stage, it only forwards it
std::span<const std::byte> MinimalStage()
{
    static const std::vector<std::uint32_t> GStage = {kSpirvMagicWord, 0u, 0u, 0u, 0u};
    return {reinterpret_cast<const std::byte*>(GStage.data()), GStage.size() * sizeof(std::uint32_t)};
}

CookedMaterialView MakeMaterial(std::uint64_t InHash, std::uint16_t InContract = MaterialContract::MaterialContractNone)
{
    CookedMaterialView ScopedMaterial;
    ScopedMaterial.graphHash = InHash;
    ScopedMaterial.contract = InContract;
    ScopedMaterial.fragmentSpirv = MinimalStage();
    return ScopedMaterial;
}

void TestSharedAcquire()
{
    RecordingRhi ScopedRhi;
    MaterialPipelineCache ScopedCache;

    const CookedMaterialView ScopedMaterial = MakeMaterial(0xABCDEF01u);
    const Fby::Rhi::ShaderPipelineHandle ScopedFirst = ScopedCache.Acquire(ScopedRhi, ScopedMaterial);
    const Fby::Rhi::ShaderPipelineHandle ScopedSecond = ScopedCache.Acquire(ScopedRhi, ScopedMaterial);

    FBY_CHECK(ScopedFirst.IsValid());
    FBY_CHECK(ScopedSecond.id == ScopedFirst.id);
    FBY_CHECK(ScopedRhi.Created == 1);
    FBY_CHECK(ScopedCache.LiveCount() == 1);
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedFirst) == 2);
    FBY_CHECK(ScopedCache.Stats().created == 1);
    FBY_CHECK(ScopedCache.Stats().reused == 1);

    FBY_CHECK(ScopedCache.Release(ScopedFirst));
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedFirst) == 1);
    FBY_CHECK(ScopedRhi.Destroyed == 0);
    FBY_CHECK(ScopedCache.LiveCount() == 1);

    FBY_CHECK(ScopedCache.Release(ScopedSecond));
    FBY_CHECK(ScopedCache.LiveCount() == 1);
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedFirst) == 0);
    FBY_CHECK(ScopedRhi.Destroyed == 0);
    FBY_CHECK(ScopedCache.Stats().released == 2);

    // A resident pipeline with no references is reused, not recreated.
    const Fby::Rhi::ShaderPipelineHandle ScopedThird = ScopedCache.Acquire(ScopedRhi, ScopedMaterial);
    FBY_CHECK(ScopedThird.id == ScopedFirst.id);
    FBY_CHECK(ScopedRhi.Created == 1);
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedThird) == 1);

    FBY_CHECK(ScopedCache.Release(ScopedThird));
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedFirst) == 0);

    // A second release of a handle the cache holds no reference for is refused.
    FBY_CHECK(!ScopedCache.Release(ScopedFirst));
    FBY_CHECK(ScopedCache.Stats().failures == 1);
    FBY_CHECK(ScopedCache.LastError()[0] != '\0');
}

void TestRefusalPaths()
{
    RecordingRhi ScopedRhi;
    MaterialPipelineCache ScopedCache;

    CookedMaterialView ScopedInvalid;
    ScopedInvalid.graphHash = 1u;
    FBY_CHECK(!ScopedCache.Acquire(ScopedRhi, ScopedInvalid).IsValid());
    FBY_CHECK(ScopedCache.Stats().failures == 1);
    FBY_CHECK(ScopedCache.LiveCount() == 0);

    ScopedRhi.FailNextCreate = true;
    FBY_CHECK(!ScopedCache.Acquire(ScopedRhi, MakeMaterial(0x11u)).IsValid());
    FBY_CHECK(ScopedRhi.Created == 0);
    FBY_CHECK(ScopedCache.Stats().failures == 2);
    FBY_CHECK(ScopedCache.LiveCount() == 0);

    // The same graph hash with a different contract is a different shader, so the
    // mesh and sprite variants of one graph stay resident side by side.
    const Fby::Rhi::ShaderPipelineHandle ScopedHandle = ScopedCache.Acquire(ScopedRhi, MakeMaterial(0x22u));
    FBY_CHECK(ScopedHandle.IsValid());
    const std::uint16_t ScopedOtherContract =
        static_cast<std::uint16_t>(MaterialContract::MaterialContractFragmentMaterialParams);
    const Fby::Rhi::ShaderPipelineHandle ScopedVariant =
        ScopedCache.Acquire(ScopedRhi, MakeMaterial(0x22u, ScopedOtherContract));
    FBY_CHECK(ScopedVariant.IsValid());
    FBY_CHECK(ScopedVariant.id != ScopedHandle.id);
    FBY_CHECK(ScopedCache.Stats().failures == 2);
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedHandle) == 1);
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedVariant) == 1);
    FBY_CHECK(ScopedCache.LiveCount() == 2);
}

void TestDomainRouting()
{
    RecordingRhi ScopedRhi;
    MaterialPipelineCache ScopedCache;

    // A mesh contract links the mesh vertex stage; only the sprite contract bit
    // selects the quad vertex stage and the sprite pipeline layout.
    const Fby::Rhi::ShaderPipelineHandle ScopedMesh = ScopedCache.Acquire(ScopedRhi, MakeMaterial(0x31u));
    FBY_CHECK(ScopedMesh.IsValid());
    FBY_CHECK(ScopedRhi.LastDomain == Fby::Rhi::ShaderPipelineDomain::Mesh);

    const std::uint16_t ScopedSpriteContract =
        static_cast<std::uint16_t>(MaterialContract::MaterialContractSpriteDomain);
    const Fby::Rhi::ShaderPipelineHandle ScopedSprite =
        ScopedCache.Acquire(ScopedRhi, MakeMaterial(0x32u, ScopedSpriteContract));
    FBY_CHECK(ScopedSprite.IsValid());
    FBY_CHECK(ScopedSprite.id != ScopedMesh.id);
    FBY_CHECK(ScopedRhi.LastDomain == Fby::Rhi::ShaderPipelineDomain::Sprite);
    FBY_CHECK(ScopedCache.ReferenceCount(ScopedSprite) == 1);

    FBY_CHECK(ScopedCache.Release(ScopedSprite));
    FBY_CHECK(ScopedCache.Release(ScopedMesh));
}

void TestEviction()
{
    RecordingRhi ScopedRhi;
    MaterialPipelineCache ScopedCache;

    // Fill the table with unreferenced entries so the next acquire can evict.
    for (std::uint32_t ScopedIndex = 1; ScopedIndex <= kMaxCachedMaterialPipelines; ++ScopedIndex)
    {
        const Fby::Rhi::ShaderPipelineHandle ScopedHandle = ScopedCache.Acquire(ScopedRhi, MakeMaterial(ScopedIndex));
        FBY_CHECK(ScopedHandle.IsValid());
        FBY_CHECK(ScopedCache.Release(ScopedHandle));
    }
    FBY_CHECK(ScopedCache.LiveCount() == kMaxCachedMaterialPipelines);
    FBY_CHECK(ScopedRhi.Destroyed == 0);

    const Fby::Rhi::ShaderPipelineHandle ScopedExtra =
        ScopedCache.Acquire(ScopedRhi, MakeMaterial(kMaxCachedMaterialPipelines + 1u));
    FBY_CHECK(ScopedExtra.IsValid());
    FBY_CHECK(ScopedCache.Stats().evicted == 1);
    FBY_CHECK(ScopedRhi.Destroyed == 1);
    FBY_CHECK(ScopedCache.LiveCount() == kMaxCachedMaterialPipelines);
}

void TestExhaustion()
{
    RecordingRhi ScopedRhi;
    MaterialPipelineCache ScopedCache;

    // Every entry stays referenced, so the cache must refuse instead of
    for (std::uint32_t ScopedIndex = 1; ScopedIndex <= kMaxCachedMaterialPipelines; ++ScopedIndex)
    {
        FBY_CHECK(ScopedCache.Acquire(ScopedRhi, MakeMaterial(ScopedIndex)).IsValid());
    }
    FBY_CHECK(ScopedCache.LiveCount() == kMaxCachedMaterialPipelines);

    const Fby::Rhi::ShaderPipelineHandle ScopedRefused =
        ScopedCache.Acquire(ScopedRhi, MakeMaterial(kMaxCachedMaterialPipelines + 1u));
    FBY_CHECK(!ScopedRefused.IsValid());
    FBY_CHECK(ScopedCache.Stats().evicted == 0);
    FBY_CHECK(ScopedCache.Stats().failures == 1);
    FBY_CHECK(ScopedRhi.Created == kMaxCachedMaterialPipelines);

    ScopedCache.ReleaseAll(ScopedRhi);
    FBY_CHECK(ScopedCache.LiveCount() == 0);
    FBY_CHECK(ScopedRhi.Destroyed == kMaxCachedMaterialPipelines);

    // After ReleaseAll the cache accepts the same graph hash again.
    FBY_CHECK(ScopedCache.Acquire(ScopedRhi, MakeMaterial(1u)).IsValid());
    FBY_CHECK(ScopedRhi.Created == kMaxCachedMaterialPipelines + 1u);

    ScopedCache.Shutdown(ScopedRhi);
    FBY_CHECK(ScopedCache.LiveCount() == 0);
    FBY_CHECK(ScopedCache.Stats().created == 0);
    FBY_CHECK(ScopedCache.LastError()[0] == '\0');
}
}

int main()
{
    TestSharedAcquire();
    TestRefusalPaths();
    TestDomainRouting();
    TestEviction();
    TestExhaustion();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "material_pipeline_cache: %d/%d checks failed\n", GFailures, GChecks);
        return 1;
    }
    std::printf("material_pipeline_cache: %d checks passed\n", GChecks);
    return 0;
}
