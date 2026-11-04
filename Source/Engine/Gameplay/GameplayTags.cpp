// FemboyEngine2D - gameplay tag registry and hierarchical queries.
#include "Engine/Gameplay/GameplayTags.hpp"

#include <algorithm>
#include <cstring>

namespace Fby
{
namespace
{
// FNV-1a over a path; stable across runs and used only for diagnostics and
// duplicate detection, never as the identity.
[[nodiscard]] std::uint64_t HashPath(std::string_view InPath) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (const char ScopedChar : InPath)
    {
        ScopedHash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ScopedChar));
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

[[nodiscard]] bool IsValidPath(std::string_view InPath) noexcept
{
    if (InPath.empty() || InPath.size() >= kMaxGameplayTagPath)
    {
        return false;
    }
    if (InPath.front() == '.' || InPath.back() == '.')
    {
        return false;
    }
    for (std::size_t ScopedI = 0; ScopedI < InPath.size(); ++ScopedI)
    {
        const char ScopedChar = InPath[ScopedI];
        if (ScopedChar == '\0' || static_cast<unsigned char>(ScopedChar) < 0x20)
        {
            return false;
        }
        if (ScopedChar == '.' && ScopedI + 1 < InPath.size() && InPath[ScopedI + 1] == '.')
        {
            return false;
        }
    }
    return true;
}

const GameplayTagMask kEmptyTagMask{};
}

GameplayTag GameplayTagRegistry::Register(std::string_view InPath) noexcept
{
    if (!IsValidPath(InPath))
    {
        return {};
    }
    const GameplayTag ScopedExisting = Find(InPath);
    if (ScopedExisting.IsValid())
    {
        return ScopedExisting;
    }
    if (MCount >= kCapacity)
    {
        return {};
    }
    const std::size_t ScopedDot = InPath.rfind('.');
    GameplayTag ScopedParent{};
    if (ScopedDot != std::string_view::npos)
    {
        ScopedParent = Register(InPath.substr(0, ScopedDot));
        if (!ScopedParent.IsValid())
        {
            return {};
        }
    }
    return AddEntry(InPath, ScopedParent);
}

GameplayTag GameplayTagRegistry::AddEntry(std::string_view InPath, GameplayTag InParent) noexcept
{
    if (MCount >= kCapacity)
    {
        return {};
    }
    Entry& ScopedEntry = MEntries[MCount];
    std::memcpy(ScopedEntry.Path.data(), InPath.data(), InPath.size());
    ScopedEntry.Path[InPath.size()] = '\0';
    ScopedEntry.PathLength = static_cast<std::uint32_t>(InPath.size());
    ScopedEntry.Parent = InParent.Id;
    ScopedEntry.NameHash = HashPath(InPath);
    const GameplayTag ScopedTag{MCount + 1u};
    ScopedEntry.Descendants.Set(ScopedTag);
    // Every ancestor gains this id in its descendant mask, so a parent query is
    // one mask test instead of a chain walk.
    std::uint32_t ScopedParent = InParent.Id;
    while (ScopedParent != 0)
    {
        MEntries[ScopedParent - 1u].Descendants.Set(ScopedTag);
        ScopedParent = MEntries[ScopedParent - 1u].Parent;
    }
    ++MCount;
    return ScopedTag;
}

GameplayTag GameplayTagRegistry::Find(std::string_view InPath) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        const Entry& ScopedEntry = MEntries[ScopedI];
        if (ScopedEntry.PathLength == InPath.size() &&
            std::string_view(ScopedEntry.Path.data(), ScopedEntry.PathLength) == InPath)
        {
            return GameplayTag{ScopedI + 1u};
        }
    }
    return {};
}

const GameplayTagRegistry::Entry* GameplayTagRegistry::EntryOf(GameplayTag InTag) const noexcept
{
    if (!InTag.IsValid() || InTag.Id > MCount)
    {
        return nullptr;
    }
    return &MEntries[InTag.Id - 1u];
}

std::string_view GameplayTagRegistry::Name(GameplayTag InTag) const noexcept
{
    const Entry* ScopedEntry = EntryOf(InTag);
    if (ScopedEntry == nullptr)
    {
        return {};
    }
    return std::string_view(ScopedEntry->Path.data(), ScopedEntry->PathLength);
}

GameplayTag GameplayTagRegistry::Parent(GameplayTag InTag) const noexcept
{
    const Entry* ScopedEntry = EntryOf(InTag);
    return ScopedEntry == nullptr ? GameplayTag{} : GameplayTag{ScopedEntry->Parent};
}

const GameplayTagMask& GameplayTagRegistry::Descendants(GameplayTag InTag) const noexcept
{
    const Entry* ScopedEntry = EntryOf(InTag);
    return ScopedEntry == nullptr ? kEmptyTagMask : ScopedEntry->Descendants;
}

void GameplayTagRegistry::Clear() noexcept
{
    MEntries = {};
    MCount = 0;
}

bool GameplayTagContainerHas(const GameplayTagContainer& InContainer, const GameplayTagRegistry& InRegistry,
                             GameplayTag InTag) noexcept
{
    if (!InTag.IsValid())
    {
        return false;
    }
    return InContainer.Bits.Any(InRegistry.Descendants(InTag));
}

bool GameplayTagContainerHasAll(const GameplayTagContainer& InContainer, const GameplayTagRegistry& InRegistry,
                                const GameplayTagContainer& InRequired) noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < InRegistry.Count(); ++ScopedI)
    {
        const GameplayTag ScopedTag{ScopedI + 1u};
        if (InRequired.Bits.Test(ScopedTag) && !GameplayTagContainerHas(InContainer, InRegistry, ScopedTag))
        {
            return false;
        }
    }
    return true;
}

bool GameplayTagContainerHasAny(const GameplayTagContainer& InContainer, const GameplayTagRegistry& InRegistry,
                                const GameplayTagContainer& InRequired) noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < InRegistry.Count(); ++ScopedI)
    {
        const GameplayTag ScopedTag{ScopedI + 1u};
        if (InRequired.Bits.Test(ScopedTag) && GameplayTagContainerHas(InContainer, InRegistry, ScopedTag))
        {
            return true;
        }
    }
    return false;
}
}
