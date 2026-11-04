// FemboyEngine2D - authored tag database and path validation.
#include "Engine/Gameplay/GameplayTagDatabase.hpp"

#include <algorithm>
#include <cstring>

namespace Fby
{
namespace
{
[[nodiscard]] std::string_view Trim(std::string_view InPath) noexcept
{
    while (!InPath.empty() && (InPath.front() == ' ' || InPath.front() == '\t'))
    {
        InPath.remove_prefix(1);
    }
    while (!InPath.empty() && (InPath.back() == ' ' || InPath.back() == '\t'))
    {
        InPath.remove_suffix(1);
    }
    return InPath;
}

[[nodiscard]] bool HasPrefix(std::string_view InPath, std::string_view InPrefix) noexcept
{
    return InPath.size() > InPrefix.size() && InPath.substr(0, InPrefix.size()) == InPrefix &&
           InPath[InPrefix.size()] == '.';
}
}

bool IsValidGameplayTagPath(std::string_view InPath) noexcept
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
        if (static_cast<unsigned char>(ScopedChar) <= 0x20 || ScopedChar == 0x7F || ScopedChar == '"' ||
            ScopedChar == '\\' || ScopedChar == ',')
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

void GameplayTagDatabase::Clear() noexcept
{
    Paths = {};
    Count = 0;
    ++Revision;
    ClearError();
}

int GameplayTagDatabase::Find(std::string_view InPath) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < Count; ++ScopedI)
    {
        if (PathAt(ScopedI) == InPath)
        {
            return static_cast<int>(ScopedI);
        }
    }
    return -1;
}

std::string_view GameplayTagDatabase::PathAt(std::uint32_t InIndex) const noexcept
{
    if (InIndex >= Count)
    {
        return {};
    }
    return std::string_view(Paths[InIndex].data());
}

int GameplayTagDatabase::AddEntry(std::string_view InPath) noexcept
{
    if (Count >= kCapacity)
    {
        SetError("tag database is full");
        return -1;
    }
    Paths[Count] = {};
    std::memcpy(Paths[Count].data(), InPath.data(), InPath.size());
    const std::uint32_t ScopedIndex = Count++;
    ++Revision;
    return static_cast<int>(ScopedIndex);
}

bool GameplayTagDatabase::EnsureParents(std::string_view InPath) noexcept
{
    const std::size_t ScopedDot = InPath.rfind('.');
    if (ScopedDot == std::string_view::npos)
    {
        return true;
    }
    const std::string_view ScopedParent = InPath.substr(0, ScopedDot);
    if (!IsValidGameplayTagPath(ScopedParent))
    {
        SetError("parent path is invalid");
        return false;
    }
    if (Find(ScopedParent) >= 0)
    {
        return true;
    }
    if (!EnsureParents(ScopedParent))
    {
        return false;
    }
    return AddEntry(ScopedParent) >= 0;
}

int GameplayTagDatabase::Add(std::string_view InPath) noexcept
{
    ClearError();
    const std::string_view ScopedPath = Trim(InPath);
    if (!IsValidGameplayTagPath(ScopedPath))
    {
        SetError("use a non-empty path without spaces, dots at the edges or '..'");
        return -1;
    }
    const int ScopedExisting = Find(ScopedPath);
    if (ScopedExisting >= 0)
    {
        return ScopedExisting;
    }
    if (!EnsureParents(ScopedPath))
    {
        return -1;
    }
    return AddEntry(ScopedPath);
}

bool GameplayTagDatabase::RemoveAt(std::uint32_t InIndex) noexcept
{
    ClearError();
    if (InIndex >= Count)
    {
        SetError("tag index is out of range");
        return false;
    }
    std::array<char, kPathCapacity> ScopedPath{};
    std::memcpy(ScopedPath.data(), Paths[InIndex].data(), kPathCapacity - 1);
    const std::string_view ScopedPrefix(ScopedPath.data());
    bool ScopedRemoved = false;
    std::uint32_t ScopedWrite = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < Count; ++ScopedI)
    {
        const std::string_view ScopedCurrent = PathAt(ScopedI);
        if (ScopedCurrent == ScopedPrefix || HasPrefix(ScopedCurrent, ScopedPrefix))
        {
            ScopedRemoved = true;
            continue;
        }
        if (ScopedWrite != ScopedI)
        {
            Paths[ScopedWrite] = Paths[ScopedI];
        }
        ++ScopedWrite;
    }
    if (!ScopedRemoved)
    {
        return false;
    }
    for (std::uint32_t ScopedI = ScopedWrite; ScopedI < Count; ++ScopedI)
    {
        Paths[ScopedI] = {};
    }
    Count = ScopedWrite;
    ++Revision;
    return true;
}

bool GameplayTagDatabase::Rename(std::uint32_t InIndex, std::string_view InNewPath) noexcept
{
    ClearError();
    if (InIndex >= Count)
    {
        SetError("tag index is out of range");
        return false;
    }
    const std::string_view ScopedNewPath = Trim(InNewPath);
    if (!IsValidGameplayTagPath(ScopedNewPath))
    {
        SetError("the new path is invalid");
        return false;
    }
    std::array<char, kPathCapacity> ScopedOldStorage{};
    std::memcpy(ScopedOldStorage.data(), Paths[InIndex].data(), kPathCapacity - 1);
    const std::string_view ScopedOldPath(ScopedOldStorage.data());
    if (ScopedOldPath == ScopedNewPath)
    {
        return true;
    }
    if (Find(ScopedNewPath) >= 0)
    {
        SetError("another tag already uses that path");
        return false;
    }
    if (!EnsureParents(ScopedNewPath))
    {
        return false;
    }
    if (Find(ScopedNewPath) >= 0)
    {
        SetError("another tag already uses that path");
        return false;
    }

    std::array<bool, kCapacity> ScopedInSubtree{};
    for (std::uint32_t ScopedI = 0; ScopedI < Count; ++ScopedI)
    {
        const std::string_view ScopedCurrent = PathAt(ScopedI);
        if (ScopedCurrent == ScopedOldPath || HasPrefix(ScopedCurrent, ScopedOldPath))
        {
            ScopedInSubtree[ScopedI] = true;
        }
    }
    // Validate every target before mutating, so a rejected rename is atomic.
    for (std::uint32_t ScopedI = 0; ScopedI < Count; ++ScopedI)
    {
        if (!ScopedInSubtree[ScopedI])
        {
            continue;
        }
        const std::string_view ScopedCurrent = PathAt(ScopedI);
        const std::string_view ScopedSuffix = ScopedCurrent.substr(ScopedOldPath.size());
        if (ScopedNewPath.size() + ScopedSuffix.size() >= kMaxGameplayTagPath)
        {
            SetError("renamed descendant would exceed the path capacity");
            return false;
        }
        std::array<char, kPathCapacity> ScopedTarget{};
        std::memcpy(ScopedTarget.data(), ScopedNewPath.data(), ScopedNewPath.size());
        std::memcpy(ScopedTarget.data() + ScopedNewPath.size(), ScopedSuffix.data(), ScopedSuffix.size());
        const std::string_view ScopedTargetView(ScopedTarget.data());
        for (std::uint32_t ScopedOther = 0; ScopedOther < Count; ++ScopedOther)
        {
            if (!ScopedInSubtree[ScopedOther] && PathAt(ScopedOther) == ScopedTargetView)
            {
                SetError("renamed descendant would collide with another tag");
                return false;
            }
        }
    }

    for (std::uint32_t ScopedI = 0; ScopedI < Count; ++ScopedI)
    {
        if (!ScopedInSubtree[ScopedI])
        {
            continue;
        }
        const std::string_view ScopedCurrent = PathAt(ScopedI);
        const std::string_view ScopedSuffix = ScopedCurrent.substr(ScopedOldPath.size());
        std::array<char, kPathCapacity> ScopedTarget{};
        std::memcpy(ScopedTarget.data(), ScopedNewPath.data(), ScopedNewPath.size());
        std::memcpy(ScopedTarget.data() + ScopedNewPath.size(), ScopedSuffix.data(), ScopedSuffix.size());
        Paths[ScopedI] = ScopedTarget;
    }
    ++Revision;
    return true;
}

void GameplayTagDatabase::ApplyToRegistry(GameplayTagRegistry& InRegistry) const noexcept
{
    InRegistry.Clear();
    for (std::uint32_t ScopedI = 0; ScopedI < Count; ++ScopedI)
    {
        (void)InRegistry.Register(PathAt(ScopedI));
    }
}

void GameplayTagDatabase::SetError(std::string_view InError) noexcept
{
    LastErrorText = {};
    LastErrorLength = static_cast<std::uint32_t>(std::min(InError.size(), LastErrorText.size() - 1u));
    std::memcpy(LastErrorText.data(), InError.data(), LastErrorLength);
    LastErrorText[LastErrorLength] = '\0';
}

void GameplayTagDatabase::ClearError() noexcept
{
    LastErrorText = {};
    LastErrorLength = 0;
}
}
