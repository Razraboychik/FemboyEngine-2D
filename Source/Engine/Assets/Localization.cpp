#include "Engine/Assets/Localization.hpp"

#include "localization_generated.h"

#include <flatbuffers/verifier.h>

#include <cstring>

namespace Fby
{

LocalizationTable LocalizationTable::Open(std::span<const std::byte> InBytes, std::string* InError)
{
    LocalizationTable ScopedTable;
    if (InBytes.size() < 8)
    {
        if (InError != nullptr)
        {
            *InError = "localization buffer is too small";
        }
        return ScopedTable;
    }

    flatbuffers::Verifier ScopedVerifier(reinterpret_cast<const std::uint8_t*>(InBytes.data()), InBytes.size());
    if (!Loc::VerifyLocalizationPackBuffer(ScopedVerifier))
    {
        if (InError != nullptr)
        {
            *InError = "invalid localization pack (flatbuffers verification failed)";
        }
        return ScopedTable;
    }

    ScopedTable.MPack = Loc::GetLocalizationPack(InBytes.data());
    ScopedTable.SelectDefaultLocale();
    return ScopedTable;
}

const Fby::Loc::LocaleTable* LocalizationTable::LocaleAt(std::size_t InIndex) const noexcept
{
    if (MPack == nullptr || MPack->locales() == nullptr || InIndex >= MPack->locales()->size())
    {
        return nullptr;
    }
    return MPack->locales()->Get(static_cast<std::uint32_t>(InIndex));
}

std::uint32_t LocalizationTable::Version() const noexcept
{
    return MPack != nullptr ? MPack->version() : 0;
}

std::size_t LocalizationTable::LocaleCount() const noexcept
{
    return MPack != nullptr && MPack->locales() != nullptr ? static_cast<std::size_t>(MPack->locales()->size()) : 0;
}

std::string_view LocalizationTable::LocaleCode(std::size_t InIndex) const noexcept
{
    const Fby::Loc::LocaleTable* ScopedLocale = LocaleAt(InIndex);
    if (ScopedLocale == nullptr || ScopedLocale->code() == nullptr)
    {
        return {};
    }
    return ScopedLocale->code()->string_view();
}

void LocalizationTable::SelectDefaultLocale() noexcept
{
    if (MPack == nullptr)
    {
        return;
    }
    MFallback = nullptr;
    if (MPack->default_locale() != nullptr)
    {
        SelectLocale(MPack->default_locale()->string_view());
    }
    if (MFallback == nullptr && LocaleCount() > 0)
    {
        MFallback = LocaleAt(0);
    }
    if (MActive == nullptr)
    {
        MActive = MFallback;
    }
}

bool LocalizationTable::SelectLocale(std::string_view InCode) noexcept
{
    for (std::size_t ScopedIndex = 0; ScopedIndex < LocaleCount(); ++ScopedIndex)
    {
        const Fby::Loc::LocaleTable* ScopedLocale = LocaleAt(ScopedIndex);
        if (ScopedLocale == nullptr || ScopedLocale->code() == nullptr)
        {
            continue;
        }
        if (ScopedLocale->code()->string_view() == InCode)
        {
            MActive = ScopedLocale;
            if (MFallback == nullptr)
            {
                MFallback = ScopedLocale;
            }
            return true;
        }
    }
    return false;
}

std::string_view LocalizationTable::Find(std::uint32_t InId) const noexcept
{
    if (MActive == nullptr || MActive->entries() == nullptr)
    {
        return {};
    }
    // flatbuffers generates this as a binary search over the sorted vector: no
    const Fby::Loc::StringEntry* ScopedEntry = MActive->entries()->LookupByKey(InId);
    if (ScopedEntry == nullptr || ScopedEntry->text() == nullptr)
    {
        return {};
    }
    return ScopedEntry->text()->string_view();
}

std::string_view LocalizationTable::ActiveLocale() const noexcept
{
    if (MActive == nullptr || MActive->code() == nullptr)
    {
        return {};
    }
    return MActive->code()->string_view();
}

std::string_view LocalizationTable::FindVariant(std::uint32_t InBaseId, std::int64_t InCount,
                                                LocalizationGender InGender) const noexcept
{
    const std::int64_t ScopedCount = InCount < 0 ? -InCount : InCount;
    std::uint32_t ScopedPlural = 0;
    if (ScopedCount == 0)
    {
        ScopedPlural = 5;
    }
    else if (ActiveLocale() == "ru")
    {
        const std::int64_t ScopedMod10 = ScopedCount % 10;
        const std::int64_t ScopedMod100 = ScopedCount % 100;
        if (ScopedMod10 == 1 && ScopedMod100 != 11)
        {
            ScopedPlural = 1;
        }
        else if (ScopedMod10 >= 2 && ScopedMod10 <= 4 && !(ScopedMod100 >= 12 && ScopedMod100 <= 14))
        {
            ScopedPlural = 2;
        }
        else
        {
            ScopedPlural = 3;
        }
    }
    else if (ScopedCount == 1)
    {
        ScopedPlural = 1;
    }
    const std::uint32_t ScopedGender =
        static_cast<std::uint32_t>(InGender) <= 2 ? static_cast<std::uint32_t>(InGender) : 0;
    const auto ScopedLookup = [this](std::uint32_t InId) -> std::string_view
    {
        return FindWithFallback(InId);
    };
    if (const std::uint32_t ScopedCombined = InBaseId + 16u + ScopedGender * 6u + ScopedPlural;
        ScopedCombined > InBaseId)
    {
        if (const std::string_view ScopedText = ScopedLookup(ScopedCombined); !ScopedText.empty())
        {
            return ScopedText;
        }
    }
    if (const std::uint32_t ScopedPluralOnly = InBaseId + ScopedPlural; ScopedPluralOnly > InBaseId)
    {
        if (const std::string_view ScopedText = ScopedLookup(ScopedPluralOnly); !ScopedText.empty())
        {
            return ScopedText;
        }
    }
    if (ScopedPlural != 0)
    {
        if (const std::uint32_t ScopedGenderOnly = InBaseId + 8u + ScopedGender; ScopedGenderOnly > InBaseId)
        {
            if (const std::string_view ScopedText = ScopedLookup(ScopedGenderOnly); !ScopedText.empty())
            {
                return ScopedText;
            }
        }
    }
    return FindWithFallback(InBaseId);
}

std::string_view LocalizationTable::FindWithFallback(std::uint32_t InId) const noexcept
{
    const std::string_view ScopedPrimary = Find(InId);
    if (!ScopedPrimary.empty() || MFallback == nullptr || MFallback == MActive || MFallback->entries() == nullptr)
    {
        return ScopedPrimary;
    }
    const Fby::Loc::StringEntry* ScopedEntry = MFallback->entries()->LookupByKey(InId);
    if (ScopedEntry == nullptr || ScopedEntry->text() == nullptr)
    {
        return {};
    }
    return ScopedEntry->text()->string_view();
}
}
