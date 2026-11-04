// FemboyEngine2D - project-owned tag database shared by the editor and runtime.
#pragma once

#include "Engine/Gameplay/GameplayTags.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace Fby
{

// A fixed, trivially copyable list of authored tag paths. Project settings own
// one instance; the runtime copies the paths into GameplayTagRegistry once at
// startup. The order is the id order, so a tag keeps its id until it is removed
// or the list is deliberately reordered.
struct GameplayTagDatabase
{
    static constexpr std::uint32_t kCapacity = kMaxGameplayTags;
    static constexpr std::uint32_t kPathCapacity = kMaxGameplayTagPath;

    std::array<std::array<char, kPathCapacity>, kCapacity> Paths{};
    std::uint32_t Count = 0;
    std::uint32_t Revision = 0;

    void Clear() noexcept;
    [[nodiscard]] int Find(std::string_view InPath) const noexcept;
    [[nodiscard]] std::string_view PathAt(std::uint32_t InIndex) const noexcept;

    // Adds the path and every missing ancestor. Returns the index, or -1 when
    // the path is invalid, duplicated with a different spelling, or the fixed
    // capacity is exhausted.
    [[nodiscard]] int Add(std::string_view InPath) noexcept;
    // Removes the tag and its whole subtree by path prefix.
    [[nodiscard]] bool RemoveAt(std::uint32_t InIndex) noexcept;
    // Renames the tag and every descendant, preserving their relative order.
    [[nodiscard]] bool Rename(std::uint32_t InIndex, std::string_view InNewPath) noexcept;

    // Rebuilds the runtime registry from the authored order. The registry must
    // outlive the world that uses the returned ids.
    void ApplyToRegistry(GameplayTagRegistry& InRegistry) const noexcept;

    // Human-readable reason for the last failed operation; empty on success.
    [[nodiscard]] std::string_view LastError() const noexcept
    {
        return std::string_view(LastErrorText.data(), LastErrorLength);
    }

    // Diagnostics are excluded: two databases with the same authored paths are
    // semantically equal even if the last failed edit set a message.
    [[nodiscard]] friend bool operator==(const GameplayTagDatabase& InA, const GameplayTagDatabase& InB) noexcept
    {
        return InA.Count == InB.Count && InA.Paths == InB.Paths;
    }

    // Diagnostics live in the same access section as the data: the database is
    // a standard-layout POD so ProjectSettings stays a trivially copyable blob.
    std::array<char, 128> LastErrorText{};
    std::uint32_t LastErrorLength = 0;

private:
    [[nodiscard]] int AddEntry(std::string_view InPath) noexcept;
    [[nodiscard]] bool EnsureParents(std::string_view InPath) noexcept;
    void SetError(std::string_view InError) noexcept;
    void ClearError() noexcept;
};

static_assert(std::is_trivially_copyable_v<GameplayTagDatabase>);
static_assert(std::is_standard_layout_v<GameplayTagDatabase>);

// The only path validation rule set used by both the database and the editor.
[[nodiscard]] bool IsValidGameplayTagPath(std::string_view InPath) noexcept;
}
