#include "Engine/Modding/ModManifest.hpp"

#include "Engine/Core/PluginAPI.h"

#include <toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <set>
#include <unordered_map>

namespace Fby::Modding
{
namespace
{
void SetError(std::string* InError, std::string_view InMessage)
{
    if (InError != nullptr)
    {
        InError->assign(InMessage);
    }
}

[[nodiscard]] bool IsLowerHex(std::string_view InText) noexcept
{
    return std::all_of(InText.begin(), InText.end(),
                       [](char InChar)
                       {
                           return (InChar >= '0' && InChar <= '9') || (InChar >= 'a' && InChar <= 'f');
                       });
}

[[nodiscard]] bool ValidId(std::string_view InId) noexcept
{
    if (InId.empty() || InId.size() > kMaxModIdBytes)
    {
        return false;
    }
    for (const char ScopedChar : InId)
    {
        const unsigned char ScopedByte = static_cast<unsigned char>(ScopedChar);
        if (!(std::isalnum(ScopedByte) != 0 || ScopedChar == '.' || ScopedChar == '_' || ScopedChar == '-'))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidVersion(std::string_view InVersion) noexcept
{
    if (InVersion.empty() || InVersion.size() > 64)
    {
        return false;
    }
    std::size_t ScopedDots = 0;
    std::size_t ScopedNumberLength = 0;
    for (const char ScopedChar : InVersion)
    {
        if (std::isdigit(static_cast<unsigned char>(ScopedChar)) != 0)
        {
            ++ScopedNumberLength;
            continue;
        }
        if (ScopedChar == '.')
        {
            if (ScopedNumberLength == 0 || ScopedDots >= 2)
            {
                return false;
            }
            ++ScopedDots;
            ScopedNumberLength = 0;
            continue;
        }
        if (ScopedChar == '-' || ScopedChar == '+')
        {
            break;
        }
        return false;
    }
    return ScopedDots == 2 && ScopedNumberLength != 0;
}

[[nodiscard]] bool ValidHex(std::string_view InText, std::size_t InBytes) noexcept
{
    return InText.size() == InBytes * 2 && IsLowerHex(InText);
}

[[nodiscard]] bool ReadRequiredString(const toml::table& InTable, std::string_view InKey, std::string* OutValue,
                                      std::string* InError)
{
    const auto ScopedValue = InTable[InKey].value<std::string>();
    if (!ScopedValue || ScopedValue->empty())
    {
        SetError(InError, std::string("mod manifest: missing ") + std::string(InKey));
        return false;
    }
    *OutValue = *ScopedValue;
    return true;
}

[[nodiscard]] bool ReadOptionalString(const toml::table& InTable, std::string_view InKey, std::string* OutValue,
                                      std::string* InError)
{
    const toml::node* ScopedNode = InTable[InKey].node();
    if (ScopedNode == nullptr)
    {
        OutValue->clear();
        return true;
    }
    const auto ScopedValue = ScopedNode->value<std::string>();
    if (!ScopedValue)
    {
        SetError(InError, std::string("mod manifest: ") + std::string(InKey) + " must be a string");
        return false;
    }
    *OutValue = *ScopedValue;
    return true;
}

[[nodiscard]] bool ReadStringArray(const toml::table& InTable, std::string_view InKey,
                                   std::vector<std::string>* OutValues, std::string* InError)
{
    OutValues->clear();
    const toml::node* ScopedNode = InTable[InKey].node();
    if (ScopedNode == nullptr)
    {
        return true;
    }
    const toml::array* ScopedArray = ScopedNode->as_array();
    if (ScopedArray == nullptr || ScopedArray->size() > kMaxMods)
    {
        SetError(InError, std::string("mod manifest: ") + std::string(InKey) + " must be a bounded string array");
        return false;
    }
    for (const toml::node& ScopedItem : *ScopedArray)
    {
        const auto ScopedValue = ScopedItem.value<std::string>();
        if (!ScopedValue || ScopedValue->empty())
        {
            SetError(InError, std::string("mod manifest: ") + std::string(InKey) + " contains a non-string");
            return false;
        }
        OutValues->push_back(*ScopedValue);
    }
    return true;
}

[[nodiscard]] bool ParseSide(std::string_view InSide, ModSide* OutSide) noexcept
{
    if (InSide == "both")
    {
        *OutSide = ModSide::Both;
        return true;
    }
    if (InSide == "client")
    {
        *OutSide = ModSide::Client;
        return true;
    }
    if (InSide == "server")
    {
        *OutSide = ModSide::Server;
        return true;
    }
    return false;
}

[[nodiscard]] bool CheckKnownKeys(const toml::table& InTable, std::string* InError)
{
    static constexpr std::array<std::string_view, 14> kAllowed{
        "id", "name", "version", "author", "description", "engine_api", "side", "priority",
        "package", "plugin", "scripts", "dependencies", "hash", "signature",
    };
    for (const auto& ScopedEntry : InTable)
    {
        const std::string_view ScopedKey = ScopedEntry.first.str();
        if (std::find(kAllowed.begin(), kAllowed.end(), ScopedKey) == kAllowed.end())
        {
            SetError(InError, "mod manifest: unknown key '" + std::string(ScopedKey) + "'");
            return false;
        }
    }
    return true;
}
}

bool IsSafeModRelativePath(std::string_view InPath) noexcept
{
    if (InPath.empty() || InPath.size() > kMaxModPathBytes || InPath.front() == '/' || InPath.front() == '\\')
    {
        return false;
    }
    if (InPath.find('\0') != std::string_view::npos || InPath.find('\\') != std::string_view::npos ||
        InPath.find(':') != std::string_view::npos)
    {
        return false;
    }
    std::size_t ScopedStart = 0;
    while (ScopedStart <= InPath.size())
    {
        const std::size_t ScopedEnd = InPath.find('/', ScopedStart);
        const std::string_view ScopedPart =
            InPath.substr(ScopedStart, ScopedEnd == std::string_view::npos ? InPath.size() - ScopedStart
                                                                           : ScopedEnd - ScopedStart);
        if (ScopedPart.empty() || ScopedPart == "." || ScopedPart == "..")
        {
            return false;
        }
        if (ScopedEnd == std::string_view::npos)
        {
            break;
        }
        ScopedStart = ScopedEnd + 1;
    }
    return true;
}

bool ParseModManifest(std::string_view InToml, ModManifest* OutManifest, std::string* InError)
{
    if (OutManifest == nullptr || InToml.empty() || InToml.size() > kMaxModManifestBytes)
    {
        SetError(InError, "mod manifest is empty or too large");
        return false;
    }
    toml::table ScopedTable;
    try
    {
        ScopedTable = toml::parse(InToml);
    }
    catch (const toml::parse_error&)
    {
        SetError(InError, "mod manifest is not valid TOML");
        return false;
    }
    if (!CheckKnownKeys(ScopedTable, InError))
    {
        return false;
    }

    ModManifest ScopedManifest;
    if (!ReadRequiredString(ScopedTable, "id", &ScopedManifest.Id, InError) ||
        !ReadRequiredString(ScopedTable, "name", &ScopedManifest.Name, InError) ||
        !ReadRequiredString(ScopedTable, "version", &ScopedManifest.Version, InError))
    {
        return false;
    }
    if (!ValidId(ScopedManifest.Id))
    {
        SetError(InError, "mod manifest: id is invalid");
        return false;
    }
    if (!ValidVersion(ScopedManifest.Version))
    {
        SetError(InError, "mod manifest: version must be x.y.z");
        return false;
    }
    if (!ReadOptionalString(ScopedTable, "author", &ScopedManifest.Author, InError) ||
        !ReadOptionalString(ScopedTable, "description", &ScopedManifest.Description, InError) ||
        !ReadOptionalString(ScopedTable, "package", &ScopedManifest.Package, InError) ||
        !ReadOptionalString(ScopedTable, "plugin", &ScopedManifest.Plugin, InError) ||
        !ReadOptionalString(ScopedTable, "hash", &ScopedManifest.PackageHash, InError) ||
        !ReadOptionalString(ScopedTable, "signature", &ScopedManifest.Signature, InError))
    {
        return false;
    }

    const auto ScopedApi = ScopedTable["engine_api"].value<std::int64_t>();
    if (!ScopedApi || *ScopedApi <= 0 || *ScopedApi > FBY_PLUGIN_ABI_VERSION)
    {
        SetError(InError, "mod manifest: engine_api is missing or unsupported");
        return false;
    }
    ScopedManifest.ApiVersion = static_cast<std::uint32_t>(*ScopedApi);

    if (const toml::node* ScopedSideNode = ScopedTable["side"].node(); ScopedSideNode != nullptr)
    {
        const auto ScopedSide = ScopedSideNode->value<std::string>();
        if (!ScopedSide || !ParseSide(*ScopedSide, &ScopedManifest.Side))
        {
            SetError(InError, "mod manifest: side must be client, server or both");
            return false;
        }
    }
    if (const toml::node* ScopedPriorityNode = ScopedTable["priority"].node(); ScopedPriorityNode != nullptr)
    {
        const auto ScopedPriority = ScopedPriorityNode->value<std::int64_t>();
        if (!ScopedPriority || *ScopedPriority < -10000 || *ScopedPriority > 10000)
        {
            SetError(InError, "mod manifest: priority is out of range");
            return false;
        }
        ScopedManifest.Priority = static_cast<std::int32_t>(*ScopedPriority);
    }
    if (!ReadStringArray(ScopedTable, "scripts", &ScopedManifest.Scripts, InError) ||
        !ReadStringArray(ScopedTable, "dependencies", &ScopedManifest.Dependencies, InError))
    {
        return false;
    }
    for (const std::string& ScopedScript : ScopedManifest.Scripts)
    {
        if (!IsSafeModRelativePath(ScopedScript))
        {
            SetError(InError, "mod manifest: script path is unsafe");
            return false;
        }
    }
    for (const std::string& ScopedDependency : ScopedManifest.Dependencies)
    {
        if (!ValidId(ScopedDependency) || ScopedDependency == ScopedManifest.Id)
        {
            SetError(InError, "mod manifest: dependency id is invalid");
            return false;
        }
    }
    if (!ScopedManifest.Package.empty() && !IsSafeModRelativePath(ScopedManifest.Package))
    {
        SetError(InError, "mod manifest: package path is unsafe");
        return false;
    }
    if (!ScopedManifest.Plugin.empty() && !IsSafeModRelativePath(ScopedManifest.Plugin))
    {
        SetError(InError, "mod manifest: plugin path is unsafe");
        return false;
    }
    if (!ScopedManifest.PackageHash.empty() && !ValidHex(ScopedManifest.PackageHash, 32))
    {
        SetError(InError, "mod manifest: hash must be 64 lower-case hex characters");
        return false;
    }
    if (!ScopedManifest.Signature.empty() && !ValidHex(ScopedManifest.Signature, 64))
    {
        SetError(InError, "mod manifest: signature must be 128 lower-case hex characters");
        return false;
    }
    *OutManifest = std::move(ScopedManifest);
    return true;
}

bool LoadModManifestFile(const std::filesystem::path& InPath, ModManifest* OutManifest, std::string* InError)
{
    std::error_code ScopedCode;
    const std::uintmax_t ScopedSize = std::filesystem::file_size(InPath, ScopedCode);
    if (ScopedCode || ScopedSize == 0 || ScopedSize > kMaxModManifestBytes)
    {
        SetError(InError, "mod manifest file is missing or too large");
        return false;
    }
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        SetError(InError, "cannot open mod manifest");
        return false;
    }
    std::string ScopedText(static_cast<std::size_t>(ScopedSize), '\0');
    ScopedStream.read(ScopedText.data(), static_cast<std::streamsize>(ScopedText.size()));
    if (!ScopedStream || ScopedStream.gcount() != static_cast<std::streamsize>(ScopedText.size()))
    {
        SetError(InError, "cannot read the complete mod manifest");
        return false;
    }
    return ParseModManifest(ScopedText, OutManifest, InError);
}

bool ResolveModOrder(const std::vector<ModManifest>& InManifests, std::vector<std::uint32_t>* OutOrder,
                     std::string* InError)
{
    if (OutOrder == nullptr || InManifests.size() > kMaxMods)
    {
        SetError(InError, "mod set is too large");
        return false;
    }
    OutOrder->clear();
    std::unordered_map<std::string, std::uint32_t> ScopedById;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InManifests.size(); ++ScopedIndex)
    {
        if (!ScopedById.emplace(InManifests[ScopedIndex].Id, ScopedIndex).second)
        {
            SetError(InError, "duplicate mod id '" + InManifests[ScopedIndex].Id + "'");
            return false;
        }
    }

    std::vector<std::vector<std::uint32_t>> ScopedDependants(InManifests.size());
    std::vector<std::uint32_t> ScopedIndegree(InManifests.size(), 0);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InManifests.size(); ++ScopedIndex)
    {
        std::set<std::string> ScopedSeen;
        for (const std::string& ScopedDependency : InManifests[ScopedIndex].Dependencies)
        {
            if (!ScopedSeen.insert(ScopedDependency).second)
            {
                continue;
            }
            const auto ScopedFound = ScopedById.find(ScopedDependency);
            if (ScopedFound == ScopedById.end())
            {
                SetError(InError, "mod '" + InManifests[ScopedIndex].Id + "' depends on missing '" + ScopedDependency +
                                       "'");
                return false;
            }
            ScopedDependants[ScopedFound->second].push_back(ScopedIndex);
            ++ScopedIndegree[ScopedIndex];
        }
    }

    std::vector<std::uint32_t> ScopedReady;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InManifests.size(); ++ScopedIndex)
    {
        if (ScopedIndegree[ScopedIndex] == 0)
        {
            ScopedReady.push_back(ScopedIndex);
        }
    }
    const auto ScopedBefore = [&InManifests](std::uint32_t InA, std::uint32_t InB)
    {
        if (InManifests[InA].Priority != InManifests[InB].Priority)
        {
            return InManifests[InA].Priority < InManifests[InB].Priority;
        }
        return InManifests[InA].Id < InManifests[InB].Id;
    };
    while (!ScopedReady.empty())
    {
        const auto ScopedBest = std::min_element(ScopedReady.begin(), ScopedReady.end(), ScopedBefore);
        const std::uint32_t ScopedIndex = *ScopedBest;
        ScopedReady.erase(ScopedBest);
        OutOrder->push_back(ScopedIndex);
        for (const std::uint32_t ScopedDependant : ScopedDependants[ScopedIndex])
        {
            if (--ScopedIndegree[ScopedDependant] == 0)
            {
                ScopedReady.push_back(ScopedDependant);
            }
        }
    }
    if (OutOrder->size() != InManifests.size())
    {
        SetError(InError, "mod dependency cycle detected");
        OutOrder->clear();
        return false;
    }
    return true;
}
}
