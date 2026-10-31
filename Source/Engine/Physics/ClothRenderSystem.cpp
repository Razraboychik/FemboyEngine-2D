// FemboyEngine2D - cloth render bridge implementation.
#include "Engine/Physics/ClothRenderSystem.hpp"

#include "Engine/ECS/SystemGraph.hpp"

#include <cstdint>

namespace Fby
{
namespace
{
struct DrawKey
{
    std::uint16_t sortLayer = 0;
    float depth = 0.0f;
    std::uint32_t stableId = 0;
};

[[nodiscard]] bool KeyLess(const DrawKey& InLeft, const DrawKey& InRight) noexcept
{
    if (InLeft.sortLayer != InRight.sortLayer)
    {
        return InLeft.sortLayer < InRight.sortLayer;
    }
    if (InLeft.depth != InRight.depth)
    {
        return InLeft.depth < InRight.depth;
    }
    return InLeft.stableId < InRight.stableId;
}
}

void ClothRenderSystem::Collect(Ecs::SystemContext& InContext) noexcept
{
    MCount = 0;
    MTruncated = false;
    if (MCloth == nullptr)
    {
        MCloth = InContext.cloth;
    }
    if (MCloth == nullptr)
    {
        return;
    }

    auto ScopedView = InContext.registry.view<ClothComponent>();
    std::array<DrawKey, kMaxClothDraws> ScopedKeys{};
    for (const Entity ScopedEntity : ScopedView)
    {
        const ClothComponent& ScopedComponent = InContext.registry.get<ClothComponent>(ScopedEntity);
        const ClothHandle ScopedHandle{ScopedComponent.ClothIndex, ScopedComponent.Generation,
                                       ScopedComponent.ClothID};
        if (!MCloth->IsValid(ScopedHandle))
        {
            continue;
        }
        if (MCount >= kMaxClothDraws)
        {
            MTruncated = true;
            break;
        }

        Rhi::MeshRenderItem ScopedItem{};
        if (!MCloth->WriteMesh(ScopedHandle, ScopedItem))
        {
            continue;
        }
        // Belt and braces: the bridge is the one writer that knows this is
        ScopedItem.flags |= Rhi::MeshItemUnskinned;
        ScopedItem.boneOffset = 0;
        ScopedItem.boneCount = 0;

        DrawKey ScopedKey;
        ScopedKey.sortLayer = ScopedItem.sortLayer;
        ScopedKey.depth = ScopedItem.depth;
        ScopedKey.stableId = ScopedComponent.ClothID.id;

        // Insertion sort over at most 64 items: deterministic draw order without
        std::uint32_t ScopedPosition = MCount;
        while (ScopedPosition > 0 && KeyLess(ScopedKey, ScopedKeys[ScopedPosition - 1]))
        {
            MItems[ScopedPosition] = MItems[ScopedPosition - 1];
            ScopedKeys[ScopedPosition] = ScopedKeys[ScopedPosition - 1];
            --ScopedPosition;
        }
        MItems[ScopedPosition] = ScopedItem;
        ScopedKeys[ScopedPosition] = ScopedKey;
        ++MCount;
    }
}

bool ClothRenderSystem::Register(Ecs::SystemGraph& InGraph, ClothRenderSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::PostUpdate, "ClothRenderSystem", &ClothRenderSystem::Tick, &InSystem);
}

void ClothRenderSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    static_cast<ClothRenderSystem*>(InUser)->Collect(InContext);
}
}
