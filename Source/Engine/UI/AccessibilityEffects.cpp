// FemboyEngine2D - accessibility palette extensions and vision-effect resources.
#include "Engine/UI/AccessibilityEffects.hpp"

#include <algorithm>
#include <cstring>

#include "Engine/UI/AccessibilityTheme.hpp"

namespace Fby::Ui
{
namespace
{
void CopyText(char* InDestination, std::size_t InCapacity, std::string_view InText) noexcept
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min(InText.size(), InCapacity - 1);
    std::memcpy(InDestination, InText.data(), ScopedLength);
    InDestination[ScopedLength] = '\0';
}

bool Reject(const char* InMessage, std::string* InError)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
    return false;
}
}

bool AccessibilityExtensionRegistry::Register(const AccessibilityPaletteExtension& InExtension, const void* InOwner,
                                               std::string* InError)
{
    if (InExtension.Id[0] == '\0')
    {
        return Reject("palette extension id is empty", InError);
    }
    if (MCount >= kMaxAccessibilityExtensions)
    {
        return Reject("palette extension registry is full", InError);
    }
    if (Find(InExtension.Id) != nullptr)
    {
        return Reject("palette extension id already registered", InError);
    }
    MExtensions[MCount] = InExtension;
    MOwners[MCount] = InOwner;
    ++MCount;
    return true;
}

void AccessibilityExtensionRegistry::RemoveOwner(const void* InOwner) noexcept
{
    std::size_t ScopedWrite = 0;
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MOwners[ScopedI] == InOwner)
        {
            continue;
        }
        if (ScopedWrite != ScopedI)
        {
            MExtensions[ScopedWrite] = MExtensions[ScopedI];
            MOwners[ScopedWrite] = MOwners[ScopedI];
        }
        ++ScopedWrite;
    }
    MCount = ScopedWrite;
}

const AccessibilityPaletteExtension* AccessibilityExtensionRegistry::Find(std::string_view InId) const noexcept
{
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (std::string_view(MExtensions[ScopedI].Id) == InId)
        {
            return &MExtensions[ScopedI];
        }
    }
    return nullptr;
}

std::span<const AccessibilityPaletteExtension> AccessibilityExtensionRegistry::Extensions() const noexcept
{
    return std::span<const AccessibilityPaletteExtension>(MExtensions.data(), MCount);
}

bool AccessibilityEffectRegistry::Register(const AccessibilityEffectProfile& InEffect, const void* InOwner,
                                           std::string* InError)
{
    if (InEffect.Id[0] == '\0')
    {
        return Reject("effect profile id is empty", InError);
    }
    if (MCount >= kMaxAccessibilityEffects)
    {
        return Reject("effect registry is full", InError);
    }
    if (Find(InEffect.Id) != nullptr)
    {
        return Reject("effect profile id already registered", InError);
    }
    MEffects[MCount] = InEffect;
    MOwners[MCount] = InOwner;
    if (MCount == 0)
    {
        MActive = 0;
    }
    ++MCount;
    return true;
}

bool AccessibilityEffectRegistry::SetActive(std::string_view InId, std::string* InError)
{
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (std::string_view(MEffects[ScopedI].Id) == InId)
        {
            MActive = ScopedI;
            return true;
        }
    }
    return Reject("unknown effect profile", InError);
}

void AccessibilityEffectRegistry::RemoveOwner(const void* InOwner) noexcept
{
    std::size_t ScopedWrite = 0;
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MOwners[ScopedI] == InOwner)
        {
            continue;
        }
        if (ScopedWrite != ScopedI)
        {
            MEffects[ScopedWrite] = MEffects[ScopedI];
            MOwners[ScopedWrite] = MOwners[ScopedI];
        }
        ++ScopedWrite;
    }
    MCount = ScopedWrite;
    MActive = MCount == 0 ? 0 : std::min(MActive, MCount - 1);
}

void AccessibilityEffectRegistry::Clear() noexcept
{
    MCount = 0;
    MActive = 0;
}

const AccessibilityEffectProfile* AccessibilityEffectRegistry::Find(std::string_view InId) const noexcept
{
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (std::string_view(MEffects[ScopedI].Id) == InId)
        {
            return &MEffects[ScopedI];
        }
    }
    return nullptr;
}

const AccessibilityEffectProfile* AccessibilityEffectRegistry::Active() const noexcept
{
    return MCount == 0 ? nullptr : &MEffects[MActive];
}

std::span<const AccessibilityEffectProfile> AccessibilityEffectRegistry::Effects() const noexcept
{
    return std::span<const AccessibilityEffectProfile>(MEffects.data(), MCount);
}
}
