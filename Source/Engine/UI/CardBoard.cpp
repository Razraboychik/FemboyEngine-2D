// FemboyEngine2D - scene-less card board layout.
#include "Engine/UI/CardBoard.hpp"

#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Fby::Ui
{

SlotLayout ComputeSlotLayout(const CardBoardComponent& InBoard, const SlotComponent& InSlot) noexcept
{
    const std::uint32_t ScopedColumns = std::max(InBoard.columns, 1u);
    const std::uint32_t ScopedRows = std::max(InBoard.rows, 1u);
    const std::uint32_t ScopedIndex = std::min(InSlot.slotIndex, ScopedColumns * ScopedRows - 1u);
    if ((InSlot.flags & SlotAnchored) != 0)
    {
        return SlotLayout{InSlot.position, InBoard.rotation + InSlot.rotation};
    }
    const std::uint32_t ScopedColumn = ScopedIndex % ScopedColumns;
    const std::uint32_t ScopedRow = ScopedIndex / ScopedColumns;
    const Vector2D ScopedLocal{
        static_cast<float>(ScopedColumn) * (InBoard.cardSize.X + InBoard.spacing.X) + InBoard.cardSize.X * 0.5f,
        static_cast<float>(ScopedRow) * (InBoard.cardSize.Y + InBoard.spacing.Y) + InBoard.cardSize.Y * 0.5f};
    const float ScopedCos = std::cos(InBoard.rotation);
    const float ScopedSin = std::sin(InBoard.rotation);
    const Vector2D ScopedRotated{ScopedLocal.X * ScopedCos - ScopedLocal.Y * ScopedSin,
                                 ScopedLocal.X * ScopedSin + ScopedLocal.Y * ScopedCos};
    SlotLayout ScopedResult{};
    ScopedResult.position = InBoard.origin + ScopedRotated + InSlot.offset;
    ScopedResult.rotation = InBoard.rotation + InSlot.rotation;
    return ScopedResult;
}

void CardBoardSystem::Update(Ecs::SystemContext& InContext) noexcept
{
    struct BoardRef
    {
        std::uint32_t id = 0;
        const CardBoardComponent* board = nullptr;
    };
    std::array<BoardRef, kMaxBoards> ScopedBoards{};
    std::uint32_t ScopedBoardCount = 0;
    auto ScopedBoardView = InContext.registry.view<CardBoardComponent>();
    for (const Entity ScopedEntity : ScopedBoardView)
    {
        if (ScopedBoardCount >= ScopedBoards.size())
        {
            break;
        }
        const CardBoardComponent& ScopedBoard = ScopedBoardView.get<CardBoardComponent>(ScopedEntity);
        ScopedBoards[ScopedBoardCount].id = ScopedBoard.boardId;
        ScopedBoards[ScopedBoardCount].board = &ScopedBoard;
        ++ScopedBoardCount;
    }
    auto ScopedSlotView = InContext.registry.view<SlotComponent>();
    for (const Entity ScopedEntity : ScopedSlotView)
    {
        SlotComponent& ScopedSlot = ScopedSlotView.get<SlotComponent>(ScopedEntity);
        const CardBoardComponent* ScopedBoard = nullptr;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedBoardCount; ++ScopedI)
        {
            if (ScopedBoards[ScopedI].id == ScopedSlot.boardId)
            {
                ScopedBoard = ScopedBoards[ScopedI].board;
                break;
            }
        }
        if (ScopedBoard == nullptr)
        {
            continue;
        }
        const SlotLayout ScopedLayout = ComputeSlotLayout(*ScopedBoard, ScopedSlot);
        ScopedSlot.position = ScopedLayout.position;
        ScopedSlot.isOccupied = ScopedSlot.cardId != 0u;
        Transform2D& ScopedTransform = InContext.registry.get_or_emplace<Transform2D>(ScopedEntity);
        ScopedTransform.Position = ScopedLayout.position;
        ScopedTransform.Rotation = ScopedLayout.rotation;
        if (InContext.registry.try_get<DrawOrder>(ScopedEntity) == nullptr)
        {
            DrawOrder ScopedOrder{};
            ScopedOrder.Layer = ScopedSlot.layer;
            InContext.registry.emplace<DrawOrder>(ScopedEntity, ScopedOrder);
        }
    }
}

bool CardBoardSystem::Register(Ecs::SystemGraph& InGraph)
{
    return InGraph.Add(Ecs::Phase::PostUpdate, "CardBoardSystem", &CardBoardSystem::Tick, nullptr);
}

void CardBoardSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    (void)InUser;
    Update(InContext);
}
}
