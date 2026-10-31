// FemboyEngine2D - scene-less card boards.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Ui
{

enum SlotFlags : std::uint32_t
{
    SlotNone = 0,
    SlotFaceUp = 1u << 0,
    SlotSelected = 1u << 1,
    SlotHovered = 1u << 2,
    SlotLocked = 1u << 3,
    SlotHighlight = 1u << 4,
    // Use `position` instead of the computed board cell (hard slot anchor)
    SlotAnchored = 1u << 5,
};

struct SlotComponent
{
    std::uint32_t boardId = 0;
    std::uint32_t slotIndex = 0;
    std::uint32_t cardId = 0;
    std::uint32_t flags = 0;
    // 0..3 for suit, 0..14 for rank; opaque to the layout system
    std::uint16_t suit = 0;
    std::uint16_t rank = 0;
    // Local nudge in board space, used for a lifted/selected card
    Vector2D offset{};
    float rotation = 0.0f;
    std::uint16_t layer = 0;
    std::uint16_t reserved = 0;
    // Resolved board-space position and occupancy. An anchored slot uses this
    Vector2D position{};
    bool isOccupied = false;
};

static_assert(std::is_trivially_copyable_v<SlotComponent>);
static_assert(std::is_standard_layout_v<SlotComponent>);

struct CardBoardComponent
{
    std::uint32_t boardId = 0;
    std::uint32_t columns = 8;
    std::uint32_t rows = 2;
    Vector2D origin{0.0f, 0.0f};
    Vector2D cardSize{72.0f, 104.0f};
    Vector2D spacing{12.0f, 12.0f};
    float rotation = 0.0f;
    std::uint32_t flags = 0;
    // Colour used by the procedural SDF card back
    Color32 backColor = Color32::White();
};

static_assert(std::is_trivially_copyable_v<CardBoardComponent>);

struct SlotLayout
{
    Vector2D position{};
    float rotation = 0.0f;
};

// Pure layout function shared by the ECS system and the UI packer. `slotIndex` is
[[nodiscard]] SlotLayout ComputeSlotLayout(const CardBoardComponent& InBoard, const SlotComponent& InSlot) noexcept;

class CardBoardSystem
{
public:
    static constexpr std::uint32_t kMaxBoards = 16;

    // PostUpdate. Materialises/updates Transform2D for every slot. A headless
    static void Update(Ecs::SystemContext& InContext) noexcept;
    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph);

private:
    static void Tick(Ecs::SystemContext& InContext, void* InUser);
};
}
