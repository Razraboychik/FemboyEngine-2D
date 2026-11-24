// FemboyEngine2D - project legal documents and first-run acceptance.
#include "Engine/Legal/LegalService.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <format>
#include <string>

#include <toml.hpp>

namespace Fby::Legal
{
namespace
{
void SetError(std::string* InError, std::string_view InText)
{
    if (InError != nullptr)
    {
        *InError = std::string(InText);
    }
}

std::string Lower(std::string_view InText)
{
    std::string Scoped(InText);
    std::transform(Scoped.begin(), Scoped.end(), Scoped.begin(),
                   [](unsigned char InChar) { return static_cast<char>(std::tolower(InChar)); });
    return Scoped;
}

std::string_view LanguageKey(std::string_view InLanguage)
{
    const std::size_t ScopedDash = InLanguage.find_first_of("-_");
    return ScopedDash == std::string_view::npos ? InLanguage : InLanguage.substr(0, ScopedDash);
}

std::uint64_t HashText(std::string_view InText, std::uint64_t InHash = 1469598103934665603ull)
{
    std::uint64_t Scoped = InHash;
    for (const char ScopedChar : InText)
    {
        Scoped ^= static_cast<unsigned char>(ScopedChar);
        Scoped *= 1099511628211ull;
    }
    return Scoped;
}

bool ReadDocument(VirtualFileSystem& InVfs, const std::string& InPath, std::string* OutText)
{
    const std::optional<std::span<const std::byte>> ScopedBytes = InVfs.Read(InPath);
    if (!ScopedBytes)
    {
        return false;
    }
    OutText->assign(reinterpret_cast<const char*>(ScopedBytes->data()), ScopedBytes->size());
    return true;
}
}

bool LegalService::LoadFromVfs(VirtualFileSystem& InVfs, std::string_view InDirectory, std::string* InError)
{
    MManifest = {};
    const std::string ScopedManifestPath = std::string(InDirectory) + "/legal.toml";
    const std::optional<std::span<const std::byte>> ScopedManifestBytes = InVfs.Read(ScopedManifestPath);
    if (!ScopedManifestBytes)
    {
        SetError(InError, "legal manifest not found: " + ScopedManifestPath);
        return false;
    }
    try
    {
        const std::string ScopedText(reinterpret_cast<const char*>(ScopedManifestBytes->data()),
                                     ScopedManifestBytes->size());
        const toml::table ScopedRoot = toml::parse(ScopedText);
        const toml::table* ScopedAgreement = ScopedRoot["agreement"].as_table();
        if (ScopedAgreement == nullptr)
        {
            SetError(InError, "legal.toml has no [agreement] table");
            return false;
        }
        MManifest.Id = (*ScopedAgreement)["id"].value_or(std::string{});
        MManifest.Version = (*ScopedAgreement)["version"].value_or(std::string{});
        MManifest.Updated = (*ScopedAgreement)["updated"].value_or(std::string{});
        MManifest.Required = (*ScopedAgreement)["required"].value_or(true);
        const std::string ScopedDecline = Lower((*ScopedAgreement)["decline_action"].value_or(std::string("quit")));
        if (ScopedDecline == "continue")
        {
            MManifest.OnDecline = DeclineAction::Continue;
        }
        else if (ScopedDecline == "custom")
        {
            MManifest.OnDecline = DeclineAction::Custom;
        }
        else
        {
            MManifest.OnDecline = DeclineAction::Quit;
        }
        if (MManifest.Id.empty() || MManifest.Version.empty())
        {
            SetError(InError, "legal.toml needs agreement id and version");
            return false;
        }
        const toml::array* ScopedDocuments = ScopedRoot["document"].as_array();
        if (ScopedDocuments == nullptr || ScopedDocuments->empty())
        {
            SetError(InError, "legal.toml has no [[document]] entries");
            return false;
        }
        std::uint64_t ScopedHash = HashText(MManifest.Id);
        ScopedHash = HashText(MManifest.Version, ScopedHash);
        for (const toml::node& ScopedNode : *ScopedDocuments)
        {
            const toml::table* ScopedTable = ScopedNode.as_table();
            if (ScopedTable == nullptr)
            {
                SetError(InError, "legal document entry is not a table");
                return false;
            }
            LegalDocument ScopedDocument;
            ScopedDocument.Language = Lower((*ScopedTable)["language"].value_or(std::string("en")));
            ScopedDocument.Title = (*ScopedTable)["title"].value_or(std::string("Legal agreement"));
            ScopedDocument.Path = (*ScopedTable)["path"].value_or(std::string{});
            if (ScopedDocument.Language.empty() || ScopedDocument.Path.empty() ||
                !ReadDocument(InVfs, ScopedDocument.Path, &ScopedDocument.Text))
            {
                SetError(InError, "legal document is missing or unreadable: " + ScopedDocument.Path);
                return false;
            }
            ScopedHash = HashText(ScopedDocument.Language, ScopedHash);
            ScopedHash = HashText(ScopedDocument.Text, ScopedHash);
            MManifest.Documents.push_back(std::move(ScopedDocument));
        }
        MManifest.ContentHash = ScopedHash == 0 ? 1 : ScopedHash;
        return true;
    }
    catch (const toml::parse_error& ScopedError)
    {
        SetError(InError, std::string("legal.toml parse error: ") + ScopedError.what());
        return false;
    }
}

const LegalDocument* LegalService::DocumentFor(std::string_view InLanguage) const
{
    const std::string ScopedLower = Lower(InLanguage);
    const std::string_view ScopedKey = LanguageKey(ScopedLower);
    for (const LegalDocument& ScopedDocument : MManifest.Documents)
    {
        if (ScopedDocument.Language == ScopedKey)
        {
            return &ScopedDocument;
        }
    }
    for (const LegalDocument& ScopedDocument : MManifest.Documents)
    {
        if (ScopedDocument.Language == "en")
        {
            return &ScopedDocument;
        }
    }
    return MManifest.Documents.empty() ? nullptr : &MManifest.Documents.front();
}

std::string_view LegalService::Text(std::string_view InLanguage) const
{
    const LegalDocument* ScopedDocument = DocumentFor(InLanguage);
    return ScopedDocument != nullptr ? std::string_view(ScopedDocument->Text) : std::string_view{};
}

bool LegalService::NeedsAcceptance(const LegalAcceptance& InAcceptance) const
{
    if (!MManifest.Required)
    {
        return false;
    }
    return InAcceptance.Id != MManifest.Id || InAcceptance.Version != MManifest.Version ||
           InAcceptance.ContentHash != MManifest.ContentHash;
}

LegalAcceptance LegalService::Accept(std::string_view InTimestamp, std::string_view InLanguage) const
{
    LegalAcceptance Scoped;
    Scoped.Id = MManifest.Id;
    Scoped.Version = MManifest.Version;
    Scoped.ContentHash = MManifest.ContentHash;
    Scoped.AcceptedAt = std::string(InTimestamp);
    Scoped.Language = std::string(LanguageKey(Lower(InLanguage)));
    return Scoped;
}

bool LegalService::SaveAcceptance(std::string_view InPath, const LegalAcceptance& InAcceptance, std::string* InError)
{
    std::ofstream ScopedFile(std::string(InPath), std::ios::trunc);
    if (!ScopedFile)
    {
        SetError(InError, "cannot open legal acceptance for writing");
        return false;
    }
    const auto ScopedQuote = [](std::string_view InText)
    {
        std::string Scoped;
        for (const char ScopedChar : InText)
        {
            if (ScopedChar == '"' || ScopedChar == '\\')
            {
                Scoped.push_back('\\');
            }
            Scoped.push_back(ScopedChar);
        }
        return Scoped;
    };
    ScopedFile << "id = \"" << ScopedQuote(InAcceptance.Id) << "\"\n";
    ScopedFile << "version = \"" << ScopedQuote(InAcceptance.Version) << "\"\n";
    ScopedFile << "content_hash = \"0x" << std::format("{:016X}", InAcceptance.ContentHash) << "\"\n";
    ScopedFile << "accepted_at = \"" << ScopedQuote(InAcceptance.AcceptedAt) << "\"\n";
    ScopedFile << "language = \"" << ScopedQuote(InAcceptance.Language) << "\"\n";
    return static_cast<bool>(ScopedFile);
}

bool LegalService::LoadAcceptance(std::string_view InPath, LegalAcceptance* OutAcceptance, std::string* InError)
{
    if (OutAcceptance == nullptr)
    {
        SetError(InError, "legal acceptance output is null");
        return false;
    }
    try
    {
        const toml::table ScopedRoot = toml::parse_file(std::string(InPath));
        OutAcceptance->Id = ScopedRoot["id"].value_or(std::string{});
        OutAcceptance->Version = ScopedRoot["version"].value_or(std::string{});
        const std::string ScopedHash = ScopedRoot["content_hash"].value_or(std::string{});
        OutAcceptance->ContentHash = ScopedHash.empty() ? 0 : std::stoull(ScopedHash, nullptr, 16);
        OutAcceptance->AcceptedAt = ScopedRoot["accepted_at"].value_or(std::string{});
        OutAcceptance->Language = ScopedRoot["language"].value_or(std::string{});
        return true;
    }
    catch (const std::exception& ScopedError)
    {
        SetError(InError, std::string("legal acceptance read error: ") + ScopedError.what());
        return false;
    }
}

}