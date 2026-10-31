// FemboyEngine2D - sprite material buckets: sort key and batch pipeline routing.
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/ChangeMaskExtract.hpp"
#include "Engine/RHI/RenderItem.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

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

void TestKeyLayout()
{
    using namespace Fby::Rhi;

    // An item without a material keeps exactly the engine order.
    FBY_CHECK(MakeMaterialRenderKey(0, 3, 12.0f, 7) == MakeRenderKey(3, 12.0f, 7));

    // The material bucket dominates depth, so a material batch cannot be split
    // by a sprite that happens to sit behind it.
    FBY_CHECK(MakeMaterialRenderKey(1, 7, 1000.0f) > MakeMaterialRenderKey(0, 7, -1000.0f));
    // The sort layer still dominates the bucket.
    FBY_CHECK(MakeMaterialRenderKey(0, 2, 0.0f) > MakeMaterialRenderKey(255, 1, 0.0f));
    // Depth stays monotonic inside one bucket.
    FBY_CHECK(MakeMaterialRenderKey(4, 0, 1.0f) < MakeMaterialRenderKey(4, 0, 2.0f));

    // 16 layer, 8 material, 32 depth, 8 sequence.
    FBY_CHECK((MakeMaterialRenderKey(1, 0, 0.0f) >> 40u) == 1u);
    FBY_CHECK((MakeMaterialRenderKey(0, 1, 0.0f) >> 48u) == 1u);
    FBY_CHECK(kMaxMaterialBuckets == 256u);
}

void TestBatchRouting()
{
    Fby::Ecs::Registry ScopedRegistry;

    // Two sprites share a material bucket, one stays on the engine shader.
    const auto ScopedMakeEntity = [&ScopedRegistry](float InDepth, std::uint32_t InBucket)
    {
        const Fby::Entity ScopedEntity = ScopedRegistry.create();
        Fby::Transform2D ScopedTransform;
        ScopedTransform.Position = Fby::Vector2D{InDepth, 0.0f};
        ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
        Fby::Sprite2D ScopedSprite;
        ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
        Fby::DrawOrder ScopedOrder;
        ScopedOrder.Layer = 5;
        ScopedOrder.Depth = InDepth;
        ScopedOrder.MaterialBucket = InBucket;
        ScopedRegistry.emplace<Fby::DrawOrder>(ScopedEntity, ScopedOrder);
        Fby::Change::Ensure(ScopedRegistry, ScopedEntity);
        return ScopedEntity;
    };

    ScopedMakeEntity(10.0f, 1);
    ScopedMakeEntity(20.0f, 1);
    ScopedMakeEntity(30.0f, 0);

    Fby::Rhi::ChangeMaskRenderExtract<16> ScopedExtract;
    // Bucket 1 resolves to pipeline 42, bucket 0 to the engine quad pipeline.
    const std::array<std::uint32_t, 2> ScopedMaterials{0u, 42u};
    ScopedExtract.SetMaterialPipelines(ScopedMaterials);

    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    FBY_CHECK(ScopedExtract.ActiveCount() == 3);

    // Bucket 0 sorts first even though it is the deepest sprite, so the frame has
    // exactly two batches: the engine shader run, then the material run.
    const std::span<const Fby::Rhi::DrawBatch> ScopedBatches = ScopedExtract.Batches();
    FBY_CHECK(ScopedBatches.size() == 2);
    FBY_CHECK(ScopedBatches[0].pipeline == 0);
    FBY_CHECK(ScopedBatches[0].itemCount == 1);
    FBY_CHECK(ScopedBatches[1].pipeline == 42);
    FBY_CHECK(ScopedBatches[1].itemCount == 2);
    FBY_CHECK(ScopedBatches[1].firstItem == 1);

    // An out-of-range bucket cannot name a pipeline: the batch falls back.
    const std::array<std::uint32_t, 1> ScopedShortTable{0u};
    ScopedExtract.SetMaterialPipelines(ScopedShortTable);
    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    FBY_CHECK(ScopedExtract.Batches().size() == 2);
    FBY_CHECK(ScopedExtract.Batches()[1].pipeline == 0);

    // An empty table keeps every batch on the engine shader.
    ScopedExtract.SetMaterialPipelines({});
    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    for (const Fby::Rhi::DrawBatch& ScopedBatch : ScopedExtract.Batches())
    {
        FBY_CHECK(ScopedBatch.pipeline == 0);
    }
}
}

int main()
{
    TestKeyLayout();
    TestBatchRouting();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "sprite_material: %d/%d checks failed\n", GFailures, GChecks);
        return 1;
    }
    std::printf("sprite_material: %d checks passed\n", GChecks);
    return 0;
}
