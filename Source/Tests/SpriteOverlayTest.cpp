// FemboyEngine2D - sprite overlay crossfade layer tests.
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"

#include <array>
#include <cstdio>

namespace
{

int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

std::uint8_t AlphaOf(const Fby::Rhi::RenderItem& InItem)
{
    return static_cast<std::uint8_t>((InItem.color >> 24) & 0xFFu);
}
}

int main()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    const Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedEntity, Transform2D{Vector2D{0.0f, 0.0f}});
    Sprite2D ScopedSprite;
    ScopedSprite.Size = {32.0f, 32.0f};
    ScopedRegistry.emplace<Sprite2D>(ScopedEntity, ScopedSprite);
    ScopedRegistry.emplace<DrawOrder>(ScopedEntity, DrawOrder{0u, 0.0f});

    SpriteOverlay2D ScopedOverlay;
    ScopedOverlay.UVMin = {0.5f, 0.0f};
    ScopedOverlay.UVMax = {1.0f, 1.0f};
    ScopedOverlay.Layer = 3;
    ScopedOverlay.Weight = 0.5f;
    ScopedRegistry.emplace<SpriteOverlay2D>(ScopedEntity, ScopedOverlay);

    Rhi::FrameComposer ScopedComposer;
    const Rhi::CameraData ScopedCamera{Vector2D{0.0f, 0.0f}, Vector2D{1280.0f, 720.0f}, 1.0f};
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    const auto ScopedItems = ScopedComposer.Packet().items;
    Check(ScopedItems.size() == 2, "the crossfade layer emits a second sprite item");

    bool ScopedFoundBase = false;
    bool ScopedFoundOverlay = false;
    for (const Rhi::RenderItem& ScopedItem : ScopedItems)
    {
        if (AlphaOf(ScopedItem) == 255)
        {
            ScopedFoundBase = true;
        }
        if (AlphaOf(ScopedItem) == 128 && ScopedItem.layer == 3)
        {
            ScopedFoundOverlay = ScopedItem.uvMin.X == 0.5f && ScopedItem.uvMax.X == 1.0f;
        }
    }
    Check(ScopedFoundBase, "the base sprite keeps full alpha");
    Check(ScopedFoundOverlay, "the overlay applies its weight as alpha and keeps its own cell");

    ScopedRegistry.get<SpriteOverlay2D>(ScopedEntity).Weight = 0.0f;
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().items.size() == 1, "a zero weight skips the overlay");

    ScopedRegistry.remove<SpriteOverlay2D>(ScopedEntity);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().items.size() == 1, "an entity without an overlay stays a single item");

    if (GFailures != 0)
    {
        std::printf("%d sprite overlay test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sprite_overlay_test: all tests passed\n");
    return 0;
}
