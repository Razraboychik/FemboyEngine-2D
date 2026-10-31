#include "Engine/Assets/LocalizationCompiler.hpp"

#include "localization_generated.h"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>
#include <toml.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>
#include <utility>

namespace Fby::Assets
{
namespace
{
void SetError(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
}

[[nodiscard]] bool ParseId(std::string_view InText, std::uint32_t& InId)
{
    const char* ScopedBegin = InText.data();
    const char* ScopedEnd = InText.data() + InText.size();
    const auto ScopedResult = std::from_chars(ScopedBegin, ScopedEnd, InId);
    return ScopedResult.ec == std::errc{} && ScopedResult.ptr == ScopedEnd;
}

[[nodiscard]] std::string EscapeToml(std::string_view InText)
{
    std::string ScopedOut;
    ScopedOut.reserve(InText.size() + 8);
    for (const char ScopedCharacter : InText)
    {
        switch (ScopedCharacter)
        {
            case '"':
                ScopedOut += "\\\"";
                break;
            case '\\':
                ScopedOut += "\\\\";
                break;
            case '\n':
                ScopedOut += "\\n";
                break;
            case '\r':
                ScopedOut += "\\r";
                break;
            case '\t':
                ScopedOut += "\\t";
                break;
            default:
                ScopedOut.push_back(ScopedCharacter);
                break;
        }
    }
    return ScopedOut;
}
}

std::vector<std::byte> CompileLocalization(std::string_view InTomlContent, std::string* InError)
{
    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(InTomlContent);
    }
    catch (const toml::parse_error& ScopedParseError)
    {
        SetError(InError, std::string("TOML parse error: ") + ScopedParseError.description().data());
        return {};
    }

    std::string ScopedDefaultLocale = "en";
    if (const toml::node* ScopedNode = ScopedRoot.get("default"); ScopedNode != nullptr && ScopedNode->is_string())
    {
        ScopedDefaultLocale = ScopedNode->as_string()->get();
    }

    // Locale code -> (id, text), sorted containers so the flatbuffers vectors
    std::map<std::string, std::vector<std::pair<std::uint32_t, std::string>>> ScopedLocales;

    for (const auto& [ScopedKey, ScopedNode] : ScopedRoot)
    {
        const std::string ScopedName(ScopedKey.str());
        if (ScopedName == "default")
        {
            continue;
        }
        if (!ScopedNode.is_table())
        {
            SetError(InError, "locale '" + ScopedName + "' must be a table of id = \"text\"");
            return {};
        }
        for (const auto& [ScopedIdKey, ScopedTextNode] : *ScopedNode.as_table())
        {
            std::uint32_t ScopedId = 0;
            if (!ParseId(ScopedIdKey.str(), ScopedId))
            {
                SetError(InError, "locale '" + ScopedName + "' has a non-numeric string id '" +
                                      std::string(ScopedIdKey.str()) + "'");
                return {};
            }
            if (!ScopedTextNode.is_string())
            {
                SetError(InError, "id " + std::to_string(ScopedId) + " in '" + ScopedName + "' is not a string");
                return {};
            }
            ScopedLocales[ScopedName].emplace_back(ScopedId, ScopedTextNode.as_string()->get());
        }
    }

    if (ScopedLocales.empty())
    {
        SetError(InError, "no locales found");
        return {};
    }

    flatbuffers::FlatBufferBuilder ScopedBuilder(4096);
    std::vector<flatbuffers::Offset<Fby::Loc::LocaleTable>> ScopedLocaleOffsets;
    ScopedLocaleOffsets.reserve(ScopedLocales.size());

    for (auto& [ScopedCode, ScopedEntries] : ScopedLocales)
    {
        std::sort(ScopedEntries.begin(), ScopedEntries.end(),
                  [](const auto& InLeft, const auto& InRight) { return InLeft.first < InRight.first; });

        std::vector<flatbuffers::Offset<Fby::Loc::StringEntry>> ScopedEntryOffsets;
        ScopedEntryOffsets.reserve(ScopedEntries.size());
        for (const auto& [ScopedId, ScopedText] : ScopedEntries)
        {
            ScopedEntryOffsets.push_back(
                Fby::Loc::CreateStringEntry(ScopedBuilder, ScopedId, ScopedBuilder.CreateString(ScopedText)));
        }

        ScopedLocaleOffsets.push_back(
            Fby::Loc::CreateLocaleTable(ScopedBuilder, ScopedBuilder.CreateString(ScopedCode),
                                        ScopedBuilder.CreateVectorOfSortedTables(&ScopedEntryOffsets)));
    }

    const auto ScopedPack =
        Fby::Loc::CreateLocalizationPack(ScopedBuilder, 1, ScopedBuilder.CreateString(ScopedDefaultLocale),
                                         ScopedBuilder.CreateVectorOfSortedTables(&ScopedLocaleOffsets));
    ScopedBuilder.Finish(ScopedPack, "FBYL");

    std::vector<std::byte> ScopedOut(ScopedBuilder.GetSize());
    std::memcpy(ScopedOut.data(), ScopedBuilder.GetBufferPointer(), ScopedBuilder.GetSize());
    return ScopedOut;
}

std::string DecompileLocalization(std::span<const std::byte> InBinary, std::string* InError)
{
    if (InBinary.size() < 8)
    {
        SetError(InError, "binary is too small");
        return {};
    }

    flatbuffers::Verifier ScopedVerifier(reinterpret_cast<const std::uint8_t*>(InBinary.data()), InBinary.size());
    if (!Fby::Loc::VerifyLocalizationPackBuffer(ScopedVerifier))
    {
        SetError(InError, "invalid localization pack");
        return {};
    }

    const Fby::Loc::LocalizationPack* ScopedPack = Fby::Loc::GetLocalizationPack(InBinary.data());
    if (ScopedPack == nullptr)
    {
        SetError(InError, "empty localization pack");
        return {};
    }

    std::string ScopedOut;
    ScopedOut += "default = \"";
    ScopedOut += EscapeToml(ScopedPack->default_locale() != nullptr ? ScopedPack->default_locale()->string_view()
                                                                    : std::string_view{});
    ScopedOut += "\"\n";

    if (ScopedPack->locales() == nullptr)
    {
        return ScopedOut;
    }

    for (const Fby::Loc::LocaleTable* ScopedLocale : *ScopedPack->locales())
    {
        if (ScopedLocale == nullptr)
        {
            continue;
        }
        ScopedOut += "\n[";
        ScopedOut +=
            ScopedLocale->code() != nullptr ? std::string(ScopedLocale->code()->string_view()) : std::string("??");
        ScopedOut += "]\n";
        if (ScopedLocale->entries() == nullptr)
        {
            continue;
        }
        for (const Fby::Loc::StringEntry* ScopedEntry : *ScopedLocale->entries())
        {
            if (ScopedEntry == nullptr)
            {
                continue;
            }
            ScopedOut += std::to_string(ScopedEntry->id());
            ScopedOut += " = \"";
            ScopedOut +=
                EscapeToml(ScopedEntry->text() != nullptr ? ScopedEntry->text()->string_view() : std::string_view{});
            ScopedOut += "\"\n";
        }
    }
    return ScopedOut;
}
}
