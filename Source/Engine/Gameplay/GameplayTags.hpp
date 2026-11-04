// FemboyEngine2D - hierarchical gameplay tags with manual registration.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/Components.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby
{

inline constexpr std::uint32_t kMaxGameplayTags = 512;
inline constexpr std::uint32_t kGameplayTagWords = kMaxGameplayTags / 64;
inline constexpr std::uint32_t kMaxGameplayTagPath = 64;

// A manually registered tag. Zero is invalid; ids start at one.
struct GameplayTag
{
    std::uint32_t Id = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Id != 0;
    }
    [[nodiscard]] friend constexpr bool operator==(GameplayTag InA, GameplayTag InB) noexcept
    {
        return InA.Id == InB.Id;
    }
};

// One bit per registered tag id. Every operation is O(words), independent of
// how many tags a project authors.
struct GameplayTagMask
{
    std::array<std::uint64_t, kGameplayTagWords> Words{};

    constexpr void Set(GameplayTag InTag) noexcept
    {
        if (InTag.Id != 0 && InTag.Id <= kMaxGameplayTags)
        {
            const std::uint32_t ScopedIndex = InTag.Id - 1u;
            Words[ScopedIndex / 64u] |= std::uint64_t{1} << (ScopedIndex % 64u);
        }
    }
    constexpr void Clear(GameplayTag InTag) noexcept
    {
        if (InTag.Id != 0 && InTag.Id <= kMaxGameplayTags)
        {
            const std::uint32_t ScopedIndex = InTag.Id - 1u;
            Words[ScopedIndex / 64u] &= ~(std::uint64_t{1} << (ScopedIndex % 64u));
        }
    }
    [[nodiscard]] constexpr bool Test(GameplayTag InTag) const noexcept
    {
        if (InTag.Id == 0 || InTag.Id > kMaxGameplayTags)
        {
            return false;
        }
        const std::uint32_t ScopedIndex = InTag.Id - 1u;
        return (Words[ScopedIndex / 64u] & (std::uint64_t{1} << (ScopedIndex % 64u))) != 0;
    }
    [[nodiscard]] constexpr bool Any(const GameplayTagMask& InOther) const noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < kGameplayTagWords; ++ScopedI)
        {
            if ((Words[ScopedI] & InOther.Words[ScopedI]) != 0)
            {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] constexpr bool All(const GameplayTagMask& InOther) const noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < kGameplayTagWords; ++ScopedI)
        {
            if ((Words[ScopedI] & InOther.Words[ScopedI]) != InOther.Words[ScopedI])
            {
                return false;
            }
        }
        return true;
    }
    [[nodiscard]] constexpr bool None(const GameplayTagMask& InOther) const noexcept
    {
        return !Any(InOther);
    }
    constexpr void Union(const GameplayTagMask& InOther) noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < kGameplayTagWords; ++ScopedI)
        {
            Words[ScopedI] |= InOther.Words[ScopedI];
        }
    }
    constexpr void Intersect(const GameplayTagMask& InOther) noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < kGameplayTagWords; ++ScopedI)
        {
            Words[ScopedI] &= InOther.Words[ScopedI];
        }
    }
    [[nodiscard]] constexpr std::uint32_t Count() const noexcept
    {
        std::uint32_t ScopedCount = 0;
        for (std::uint32_t ScopedI = 0; ScopedI < kGameplayTagWords; ++ScopedI)
        {
            ScopedCount += std::popcount(Words[ScopedI]);
        }
        return ScopedCount;
    }
    [[nodiscard]] constexpr bool IsEmpty() const noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < kGameplayTagWords; ++ScopedI)
        {
            if (Words[ScopedI] != 0)
            {
                return false;
            }
        }
        return true;
    }
};

// The project-owned tag table. Tags are created by path, so a game invents its
// own vocabulary ("Character.State.Stunned", "Ability.Fireball.Cast") instead
// of choosing from a fixed enum. Registering "A.B" also registers "A".
class GameplayTagRegistry
{
public:
    static constexpr std::uint32_t kCapacity = kMaxGameplayTags;

    // Returns the existing tag for the path or creates it. Zero means the path
    // is empty, too long or the table is full.
    [[nodiscard]] GameplayTag Register(std::string_view InPath) noexcept;
    [[nodiscard]] GameplayTag Find(std::string_view InPath) const noexcept;
    [[nodiscard]] std::string_view Name(GameplayTag InTag) const noexcept;
    [[nodiscard]] GameplayTag Parent(GameplayTag InTag) const noexcept;
    // Mask of the tag and every descendant, used for hierarchical queries.
    [[nodiscard]] const GameplayTagMask& Descendants(GameplayTag InTag) const noexcept;
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    void Clear() noexcept;

private:
    struct Entry
    {
        std::array<char, kMaxGameplayTagPath> Path{};
        std::uint32_t PathLength = 0;
        std::uint32_t Parent = 0;
        std::uint64_t NameHash = 0;
        GameplayTagMask Descendants{};
    };

    [[nodiscard]] GameplayTag AddEntry(std::string_view InPath, GameplayTag InParent) noexcept;
    [[nodiscard]] const Entry* EntryOf(GameplayTag InTag) const noexcept;

    std::array<Entry, kCapacity> MEntries{};
    std::uint32_t MCount = 0;
};

// An exact set of tags. Hierarchical queries take the registry because they
// need the descendant mask; exact queries do not.
struct GameplayTagContainer
{
    GameplayTagMask Bits{};

    void Add(GameplayTag InTag) noexcept
    {
        Bits.Set(InTag);
    }
    void Remove(GameplayTag InTag) noexcept
    {
        Bits.Clear(InTag);
    }
    void Clear() noexcept
    {
        Bits = GameplayTagMask{};
    }
    [[nodiscard]] bool HasExact(GameplayTag InTag) const noexcept
    {
        return Bits.Test(InTag);
    }
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return Bits.Count();
    }
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return Bits.IsEmpty();
    }
};

[[nodiscard]] bool GameplayTagContainerHas(const GameplayTagContainer& InContainer,
                                           const GameplayTagRegistry& InRegistry, GameplayTag InTag) noexcept;
[[nodiscard]] bool GameplayTagContainerHasAll(const GameplayTagContainer& InContainer,
                                              const GameplayTagRegistry& InRegistry,
                                              const GameplayTagContainer& InRequired) noexcept;
[[nodiscard]] bool GameplayTagContainerHasAny(const GameplayTagContainer& InContainer,
                                              const GameplayTagRegistry& InRegistry,
                                              const GameplayTagContainer& InRequired) noexcept;

// Core tag state of an entity. BaseTags are authored or granted by permanent
// systems; Tags is BaseTags plus every tag granted by active effects.
struct GameplayTagsComponent
{
    GameplayTagContainer Tags{};
    GameplayTagContainer BaseTags{};
    std::uint32_t Revision = 0;
    // Keeps the record padding-free for raw snapshot and command-log payloads.
    std::uint32_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<GameplayTagsComponent>);
static_assert(std::is_standard_layout_v<GameplayTagsComponent>);
static_assert(sizeof(GameplayTagsComponent) == 136);
}

FBY_REFLECT_COMPONENT(Fby::GameplayTagsComponent, "GameplayTagsComponent",
                      FBY_FIELD(Fby::GameplayTagsComponent, Revision, UInt32))

namespace Fby::Reflect
{
template <> inline constexpr bool kRawCommandPayload<Fby::GameplayTagsComponent> = true;
}
