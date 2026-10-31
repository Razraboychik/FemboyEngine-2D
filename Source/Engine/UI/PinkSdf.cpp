// FemboyEngine2D - procedural Pink UI primitive packing.
#include "Engine/UI/PinkSdf.hpp"

#include "Engine/UI/CardBoard.hpp"

#include <algorithm>

namespace Fby::Ui
{
namespace
{

[[nodiscard]] Vector4D ColorToVector(Color32 InColor) noexcept
{
    constexpr float kInverse = 1.0f / 255.0f;
    return Vector4D{static_cast<float>(InColor.R()) * kInverse, static_cast<float>(InColor.G()) * kInverse,
                    static_cast<float>(InColor.B()) * kInverse, static_cast<float>(InColor.A()) * kInverse};
}
}

std::uint32_t PackPinkSdfPrimitives(std::span<const PinkSdfPrimitive> InPrimitives,
                                    std::span<PinkSdfGpuPrimitive> OutGpu) noexcept
{
    if (OutGpu.size() < InPrimitives.size())
    {
        return 0;
    }
    for (std::size_t ScopedI = 0; ScopedI < InPrimitives.size(); ++ScopedI)
    {
        const PinkSdfPrimitive& ScopedSource = InPrimitives[ScopedI];
        PinkSdfGpuPrimitive& ScopedTarget = OutGpu[ScopedI];
        ScopedTarget.rect = Vector4D{ScopedSource.center.X, ScopedSource.center.Y, ScopedSource.size.X,
                                     ScopedSource.size.Y};
        ScopedTarget.params =
            Vector4D{ScopedSource.radius, ScopedSource.border, ScopedSource.rotation, ScopedSource.shadow};
        ScopedTarget.fill = ColorToVector(ScopedSource.fill);
        ScopedTarget.border = ColorToVector(ScopedSource.borderColor);
        ScopedTarget.border.W = static_cast<float>(ScopedSource.flags | (static_cast<std::uint32_t>(ScopedSource.shape) << 8));
    }
    return static_cast<std::uint32_t>(InPrimitives.size());
}

std::uint32_t BuildCardBoardPrimitives(Ecs::Registry& InRegistry, std::uint32_t InBoardId,
                                       std::span<PinkSdfPrimitive> OutPrimitives) noexcept
{
    if (OutPrimitives.empty())
    {
        return 0;
    }
    const CardBoardComponent* ScopedBoard = nullptr;
    auto ScopedBoards = InRegistry.view<CardBoardComponent>();
    for (const Entity ScopedEntity : ScopedBoards)
    {
        const CardBoardComponent& ScopedCandidate = ScopedBoards.get<CardBoardComponent>(ScopedEntity);
        if (ScopedCandidate.boardId == InBoardId)
        {
            ScopedBoard = &ScopedCandidate;
            break;
        }
    }
    if (ScopedBoard == nullptr)
    {
        return 0;
    }
    std::uint32_t ScopedWritten = 0;
    auto ScopedSlots = InRegistry.view<SlotComponent>();
    for (const Entity ScopedEntity : ScopedSlots)
    {
        if (ScopedWritten + 2 > OutPrimitives.size())
        {
            break;
        }
        const SlotComponent& ScopedSlot = ScopedSlots.get<SlotComponent>(ScopedEntity);
        if (ScopedSlot.boardId != InBoardId)
        {
            continue;
        }
        const SlotLayout ScopedLayout = ComputeSlotLayout(*ScopedBoard, ScopedSlot);
        const bool ScopedFaceUp = (ScopedSlot.flags & SlotFaceUp) != 0;
        const bool ScopedSelected = (ScopedSlot.flags & SlotSelected) != 0;
        const Color32 ScopedFill = ScopedFaceUp ? Color32::White() : ScopedBoard->backColor;
        PinkSdfPrimitive ScopedCard{};
        ScopedCard.center = ScopedLayout.position;
        ScopedCard.size = ScopedBoard->cardSize;
        ScopedCard.radius = std::min(ScopedBoard->cardSize.X, ScopedBoard->cardSize.Y) * 0.085f;
        ScopedCard.border = ScopedSelected ? 3.0f : 1.5f;
        ScopedCard.rotation = ScopedLayout.rotation;
        ScopedCard.shape = PinkSdfShape::Card;
        ScopedCard.flags = PinkSdfShadow | (ScopedSelected ? PinkSdfSelected : 0u);
        ScopedCard.fill = ScopedFill;
        ScopedCard.borderColor = ScopedSelected ? Color32::FromBytes(232, 93, 158) : Color32::FromBytes(40, 40, 48);
        OutPrimitives[ScopedWritten++] = ScopedCard;
        PinkSdfPrimitive ScopedShadow = ScopedCard;
        ScopedShadow.center = ScopedLayout.position + Vector2D{4.0f, 7.0f};
        ScopedShadow.radius = ScopedCard.radius + 2.0f;
        ScopedShadow.fill = Color32::FromBytes(0, 0, 0, 70);
        ScopedShadow.border = 0.0f;
        ScopedShadow.shadow = 0.0f;
        ScopedShadow.borderColor = Color32::FromBytes(0, 0, 0, 0);
        ScopedShadow.flags = PinkSdfShadow;
        OutPrimitives[ScopedWritten++] = ScopedShadow;
    }
    return ScopedWritten;
}
}
