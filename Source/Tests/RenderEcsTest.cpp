// FemboyEngine2D - render ECS chunk and packed-instance tests.
#include "Engine/ECS/RenderEcs.hpp"

#include <array>
#include <cmath>
#include <cstdio>

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

bool Near(float InA, float InB, float InTolerance = 0.002f)
{
    return std::fabs(InA - InB) <= InTolerance;
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Render;

    static_assert(sizeof(RenderChunk) == ChunkBytes);
    static_assert(sizeof(GpuSpriteInstance) == 32);
    static_assert(sizeof(PackedTransform2D) == 16);
    static_assert(sizeof(GpuEntity) == 8);

    Check(PackFloat16(0.0f) == 0x0000u, "positive zero packs to zero");
    Check(PackFloat16(-0.0f) == 0x8000u, "negative zero keeps its sign");
    Check(PackFloat16(1.0f) == 0x3C00u, "one packs to 0x3C00");
    Check(PackFloat16(-2.0f) == 0xC000u, "minus two packs to 0xC000");
    Check(PackFloat16(0.5f) == 0x3800u, "half packs to 0x3800");
    Check(UnpackFloat16(0x3C00u) == 1.0f, "0x3C00 unpacks to one");
    Check(UnpackFloat16(0xC000u) == -2.0f, "0xC000 unpacks to minus two");

    const float ScopedValues[] = {0.125f, 0.5f, 1.0f, -3.5f, 12.75f, 1000.0f, -0.001f, 65504.0f};
    for (const float ScopedValue : ScopedValues)
    {
        const float ScopedRoundTrip = UnpackFloat16(PackFloat16(ScopedValue));
        const float ScopedTolerance = std::fabs(ScopedValue) * 0.002f + 0.001f;
        Check(Near(ScopedRoundTrip, ScopedValue, ScopedTolerance), "half round-trip stays within tolerance");
    }

    const PackedTransform2D ScopedTransform =
        MakePackedTransform2D(Vector2D{128.0f, -64.0f}, Vector2D{2.0f, 0.5f}, 1.57079632679f);
    Check(Near(ScopedTransform.PositionX, 128.0f) && Near(ScopedTransform.PositionY, -64.0f),
          "packed position is preserved");
    Check(Near(UnpackFloat16(ScopedTransform.ScaleX), 2.0f), "packed scale x is preserved");
    Check(Near(UnpackFloat16(ScopedTransform.ScaleY), 0.5f), "packed scale y is preserved");
    Check(Near(UnpackFloat16(ScopedTransform.Cos), 0.0f), "cosine is packed");
    Check(Near(UnpackFloat16(ScopedTransform.Sin), 1.0f), "sine is packed");

    Rhi::RenderItem ScopedItem;
    ScopedItem.position = Vector2D{10.0f, 20.0f};
    ScopedItem.size = Vector2D{32.0f, 64.0f};
    ScopedItem.uvMin = Vector2D{0.125f, 0.25f};
    ScopedItem.uvMax = Vector2D{0.875f, 0.75f};
    ScopedItem.layer = 3u;
    ScopedItem.color = 0xFF00FF00u;
    ScopedItem.rotation = 1.57079632679f;
    ScopedItem.flags = 0u;

    const GpuSpriteInstance ScopedPacked = MakeGpuSpriteInstance(ScopedItem);
    Check(Near(ScopedPacked.PositionX, 10.0f) && Near(ScopedPacked.PositionY, 20.0f), "sprite position is packed");
    Check(Near(UnpackFloat16(ScopedPacked.SizeX), 32.0f), "sprite width is packed");
    Check(Near(UnpackFloat16(ScopedPacked.SizeY), 64.0f), "sprite height is packed");
    Check(Near(UnpackFloat16(ScopedPacked.UvMinX), 0.125f), "sprite uv min is packed");
    Check(Near(UnpackFloat16(ScopedPacked.UvMaxY), 0.75f), "sprite uv max is packed");
    Check(ScopedPacked.Layer == 3u, "sprite layer is packed");
    Check(ScopedPacked.Color == 0xFF00FF00u, "sprite colour is packed");
    Check(Near(UnpackFloat16(ScopedPacked.Cos), 0.0f), "sprite cosine is packed");
    Check(Near(UnpackFloat16(ScopedPacked.Sin), 1.0f), "sprite sine is packed");

    RenderEcs ScopedEcs;
    const GpuEntity ScopedFirst = ScopedEcs.AddSprite(ScopedItem);
    Check(ScopedFirst.IsValid(), "the first sprite gets a valid handle");
    Check(ScopedEcs.ActiveCount() == 1u, "one sprite is active");
    Check(ScopedEcs.ChunkCount() == 1u, "one chunk is active");
    Check(ScopedEcs.Sprite(ScopedFirst) != nullptr, "the sprite is readable");
    RenderChunk* ScopedChunk = ScopedEcs.ChunkFor(ScopedFirst);
    Check(ScopedChunk != nullptr && ScopedChunk->Header.DirtyBlockMask != 0u, "a write marks dirty blocks");
    if (ScopedChunk != nullptr)
    {
        ScopedChunk->Header.DirtyBlockMask = 0u;
    }

    std::array<ChunkCopyRange, 8> ScopedRanges{};
    MarkDirty(*ScopedChunk, 0u, 100u);
    MarkDirty(*ScopedChunk, 600u, 100u);
    std::uint32_t ScopedRangeCount = CollectDirtyRanges(*ScopedChunk, ScopedRanges);
    Check(ScopedRangeCount == 1u, "adjacent dirty blocks coalesce");
    Check(ScopedRanges[0].Offset == 0u && ScopedRanges[0].Bytes == DirtyBlockBytes * 2u,
          "the coalesced range covers both blocks");
    MarkDirty(*ScopedChunk, 16000u, 64u);
    ScopedRangeCount = CollectDirtyRanges(*ScopedChunk, ScopedRanges);
    Check(ScopedRangeCount == 2u, "a distant dirty block starts a second range");
    Check(ScopedRanges[1].Offset == (DirtyBlockCount - 1u) * DirtyBlockBytes, "the tail range is aligned");
    ClearDirty(*ScopedChunk);
    Check(CollectDirtyRanges(*ScopedChunk, ScopedRanges) == 0u, "clear resets the dirty mask");

    ScopedItem.position = Vector2D{30.0f, 40.0f};
    Check(ScopedEcs.UpdateSprite(ScopedFirst, ScopedItem), "the sprite updates");
    Check(ScopedEcs.Sprite(ScopedFirst)->PositionX == 30.0f, "the updated position is stored");
    Check(ScopedChunk->Header.DirtyBlockMask != 0u, "an update marks dirty blocks again");

    ScopedEcs.RemoveSprite(ScopedFirst);
    Check(ScopedEcs.ActiveCount() == 0u, "the sprite is released");
    Check(ScopedEcs.ChunkCount() == 0u, "an empty chunk returns to the free list");
    Check(ScopedEcs.FreeSlotCount() == ChunkCapacity, "all slots are free again");

    const GpuEntity ScopedReused = ScopedEcs.AddSprite(ScopedItem);
    Check(ScopedReused.IsValid(), "a freed chunk is reusable");
    Check(ScopedEcs.ChunkCount() == 1u, "the reused chunk is active again");
    ScopedEcs.RemoveSprite(ScopedReused);

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ChunkCapacity + 1u; ++ScopedIndex)
    {
        Check(ScopedEcs.AddSprite(ScopedItem).IsValid(), "a sprite is added until the chunk spills");
    }
    Check(ScopedEcs.ActiveCount() == ChunkCapacity + 1u, "the spill count is exact");
    Check(ScopedEcs.ChunkCount() == 2u, "the spill allocates a second chunk");

    if (GFailures == 0)
    {
        std::printf("render_ecs: OK\n");
        return 0;
    }
    std::fprintf(stderr, "render_ecs: %d failure(s)\n", GFailures);
    return 1;
}
