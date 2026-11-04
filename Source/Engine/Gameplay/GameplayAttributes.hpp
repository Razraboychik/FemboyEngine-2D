// FemboyEngine2D - manually registered gameplay attributes and modifiers.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/Components.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby
{

inline constexpr std::uint32_t kMaxGameplayAttributes = 32;
inline constexpr std::uint32_t kMaxGameplayAttributeName = 48;

// A manually registered attribute such as Health, Mana or MoveSpeed. Zero is
// invalid; ids start at one and index the component arrays directly.
struct GameplayAttribute
{
    std::uint32_t Id = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Id != 0;
    }
    [[nodiscard]] friend constexpr bool operator==(GameplayAttribute InA, GameplayAttribute InB) noexcept
    {
        return InA.Id == InB.Id;
    }
};

class GameplayAttributeRegistry
{
public:
    static constexpr std::uint32_t kCapacity = kMaxGameplayAttributes;

    [[nodiscard]] GameplayAttribute Register(std::string_view InName) noexcept;
    [[nodiscard]] GameplayAttribute Find(std::string_view InName) const noexcept;
    [[nodiscard]] std::string_view Name(GameplayAttribute InAttribute) const noexcept;
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    void Clear() noexcept;

private:
    struct Entry
    {
        std::array<char, kMaxGameplayAttributeName> Name{};
        std::uint32_t NameLength = 0;
    };

    std::array<Entry, kCapacity> MEntries{};
    std::uint32_t MCount = 0;
};

struct GameplayAttributeValues
{
    std::array<float, kMaxGameplayAttributes> Base{};
    std::array<float, kMaxGameplayAttributes> Current{};
};

struct GameplayAttributesComponent
{
    GameplayAttributeValues Values{};
    std::uint32_t Revision = 0;
};

static_assert(std::is_trivially_copyable_v<GameplayAttributesComponent>);
static_assert(std::is_standard_layout_v<GameplayAttributesComponent>);

enum class GameplayModifierOp : std::uint8_t
{
    Add = 0,
    Multiply = 1,
    Override = 2,
};

struct GameplayAttributeModifier
{
    GameplayAttribute Attribute{};
    GameplayModifierOp Op = GameplayModifierOp::Add;
    float Magnitude = 0.0f;
};

static_assert(!std::is_empty_v<GameplayAttributeModifier>);

// Rebuilds Current from Base and the supplied modifiers:
// current = (base + sum(Add)) * product(Multiply), then Override wins last.
// The pass is allocation-free and deterministic in modifier order for equal
// operations; Override is order-independent by design.
void RebuildGameplayAttributes(GameplayAttributesComponent& InComponent,
                               std::span<const GameplayAttributeModifier> InModifiers) noexcept;

// Applies instant modifiers to Base (damage, healing, permanent buffs). Stacks
// scale Add linearly and Multiply geometrically.
void ApplyInstantAttributeModifiers(GameplayAttributesComponent& InComponent,
                                    std::span<const GameplayAttributeModifier> InModifiers,
                                    std::uint16_t InStacks) noexcept;

[[nodiscard]] float GameplayAttributeValue(const GameplayAttributesComponent& InComponent,
                                           GameplayAttribute InAttribute) noexcept;

void SetGameplayAttributeBase(GameplayAttributesComponent& InComponent, GameplayAttribute InAttribute,
                              float InValue) noexcept;
}

FBY_REFLECT_COMPONENT(Fby::GameplayAttributesComponent, "GameplayAttributesComponent",
                      FBY_FIELD(Fby::GameplayAttributesComponent, Revision, UInt32))

namespace Fby::Reflect
{
template <> inline constexpr bool kRawCommandPayload<Fby::GameplayAttributesComponent> = true;
}
