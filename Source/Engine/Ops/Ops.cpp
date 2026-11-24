// FemboyEngine2D - developer operations.
#include "Engine/Ops/Ops.hpp"

#include "Engine/Text/BaseFonts.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace Fby::Ops
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

std::string ReadText(VirtualFileSystem& InVfs, std::string_view InPath)
{
    const std::optional<std::span<const std::byte>> ScopedBytes = InVfs.Read(InPath);
    return ScopedBytes ? std::string(reinterpret_cast<const char*>(ScopedBytes->data()), ScopedBytes->size())
                       : std::string{};
}

void AddIssue(DoctorReport& InReport, DoctorSeverity InSeverity, std::string InCode, std::string InPath,
              std::string InMessage)
{
    InReport.Issues.push_back({InSeverity, std::move(InCode), std::move(InPath), std::move(InMessage)});
}

bool EndsWith(std::string_view InText, std::string_view InSuffix)
{
    return InText.size() >= InSuffix.size() && InText.substr(InText.size() - InSuffix.size()) == InSuffix;
}

std::string EscapeMarkdown(std::string_view InText)
{
    std::string Scoped;
    for (const char ScopedChar : InText)
    {
        if (ScopedChar == '|' || ScopedChar == '\n')
        {
            Scoped.push_back(' ');
        }
        else
        {
            Scoped.push_back(ScopedChar);
        }
    }
    return Scoped;
}

bool WriteText(const std::filesystem::path& InPath, std::string_view InText, std::string* InError)
{
    std::ofstream ScopedFile(InPath, std::ios::binary | std::ios::trunc);
    if (!ScopedFile)
    {
        SetError(InError, "cannot write incident file: " + InPath.string());
        return false;
    }
    ScopedFile.write(InText.data(), static_cast<std::streamsize>(InText.size()));
    return static_cast<bool>(ScopedFile);
}
}

bool DoctorReport::HasErrors() const noexcept
{
    return std::any_of(Issues.begin(), Issues.end(),
                       [](const DoctorIssue& InIssue) { return InIssue.Severity == DoctorSeverity::Error; });
}

bool DoctorReport::HasWarnings() const noexcept
{
    return std::any_of(Issues.begin(), Issues.end(),
                       [](const DoctorIssue& InIssue) { return InIssue.Severity == DoctorSeverity::Warning; });
}

std::uint32_t DoctorReport::Count(DoctorSeverity InSeverity) const noexcept
{
    return static_cast<std::uint32_t>(
        std::count_if(Issues.begin(), Issues.end(),
                      [InSeverity](const DoctorIssue& InIssue) { return InIssue.Severity == InSeverity; }));
}

DoctorReport ProjectDoctor::Run(VirtualFileSystem& InVfs, const Assets::LicenseRegistry& InLicenses,
                                const Legal::LegalService* InLegal, const DoctorOptions& InOptions)
{
    DoctorReport ScopedReport;
    if (InVfs.EntryCount() == 0)
    {
        AddIssue(ScopedReport, DoctorSeverity::Error, "vfs.empty", {}, "The asset VFS has no entries");
        return ScopedReport;
    }
    if (InOptions.CheckLicenses)
    {
        if (InLicenses.Entries().empty())
        {
            AddIssue(ScopedReport, DoctorSeverity::Error, "licenses.empty", "Licenses",
                     "The license registry is empty");
        }
    }
    if (InOptions.CheckFonts)
    {
        for (const Text::BaseFontInfo& ScopedFont : Text::BaseFontCatalog())
        {
            if (!InVfs.Read(ScopedFont.Path))
            {
                AddIssue(ScopedReport, DoctorSeverity::Error, "font.missing", std::string(ScopedFont.Path),
                         "A bundled base font is missing");
            }
            if (InOptions.CheckLicenses && InLicenses.Find(ScopedFont.LicenseId) == nullptr)
            {
                AddIssue(ScopedReport, DoctorSeverity::Error, "font.license", std::string(ScopedFont.Path),
                         "The base font license is missing");
            }
        }
    }
    if (InOptions.CheckLegal)
    {
        if (InLegal == nullptr)
        {
            AddIssue(ScopedReport, DoctorSeverity::Warning, "legal.unloaded", "Legal",
                     "No legal service was supplied");
        }
        else if (InLegal->Manifest().Id.empty() || InLegal->Manifest().Documents.empty())
        {
            AddIssue(ScopedReport, DoctorSeverity::Error, "legal.manifest", "Legal/legal.toml",
                     "The legal manifest has no id or documents");
        }
    }
    if (InOptions.CheckAssets)
    {
        std::set<std::uint64_t> ScopedGuids;
        for (const std::string_view ScopedEntry : InVfs.List(""))
        {
            if (!EndsWith(ScopedEntry, ".femeta"))
            {
                continue;
            }
            Assets::AssetMeta ScopedMeta;
            Assets::AssetDependency ScopedDependencies[256]{};
            std::uint32_t ScopedDependencyCount = 0;
            std::string ScopedError;
            const std::string ScopedText = ReadText(InVfs, ScopedEntry);
            if (!Assets::ParseAssetMetaFile(ScopedText, &ScopedMeta, ScopedDependencies, 256,
                                            &ScopedDependencyCount, &ScopedError))
            {
                AddIssue(ScopedReport, DoctorSeverity::Error, "meta.parse", std::string(ScopedEntry), ScopedError);
                continue;
            }
            if (!ScopedGuids.insert(ScopedMeta.guid).second)
            {
                AddIssue(ScopedReport, DoctorSeverity::Error, "meta.duplicate_guid", std::string(ScopedEntry),
                         "Duplicate asset GUID");
            }
            if (ScopedMeta.license[0] != '\0' && InLicenses.Find(ScopedMeta.license) == nullptr)
            {
                AddIssue(ScopedReport, DoctorSeverity::Error, "meta.license", std::string(ScopedEntry),
                         "Asset references a missing license");
            }
        }
    }
    if (InOptions.Strict && ScopedReport.HasWarnings())
    {
        AddIssue(ScopedReport, DoctorSeverity::Error, "doctor.strict", {},
                 "Strict mode promotes warnings to errors");
    }
    return ScopedReport;
}

IncidentResult WriteIncidentBundle(const IncidentRequest& InRequest, std::string* InError)
{
    IncidentResult ScopedResult;
    if (InRequest.Directory.empty())
    {
        SetError(InError, "incident directory is empty");
        return ScopedResult;
    }
    std::error_code ScopedCode;
    std::filesystem::create_directories(InRequest.Directory, ScopedCode);
    if (ScopedCode)
    {
        SetError(InError, "cannot create incident directory: " + ScopedCode.message());
        return ScopedResult;
    }
    const std::filesystem::path ScopedRoot(InRequest.Directory);
    const auto ScopedAdd = [&ScopedResult](std::string InPath)
    {
        ScopedResult.Files.push_back(std::move(InPath));
    };
    const auto ScopedWrite = [&ScopedRoot, &ScopedAdd, InError](std::string_view InRelative, std::string_view InText)
    {
        const std::filesystem::path ScopedPath = ScopedRoot / std::filesystem::path(InRelative);
        std::error_code ScopedCreate;
        std::filesystem::create_directories(ScopedPath.parent_path(), ScopedCreate);
        if (!WriteText(ScopedPath, InText, InError))
        {
            return false;
        }
        ScopedAdd(std::string(InRelative));
        return true;
    };
    std::string ScopedReason = InRequest.Reason;
    if (ScopedReason.size() > 512)
    {
        ScopedReason.resize(512);
    }
    if (!ScopedWrite("README.txt", "FemboyEngine2D incident bundle\nReason: " + ScopedReason + "\n"))
    {
        return ScopedResult;
    }
    if (!ScopedWrite("build.txt", InRequest.BuildInfo))
    {
        return ScopedResult;
    }
    std::string ScopedLog = InRequest.LogText;
    if (ScopedLog.size() > InRequest.MaxLogBytes)
    {
        ScopedLog.erase(0, ScopedLog.size() - InRequest.MaxLogBytes);
    }
    if (!ScopedWrite("engine.log", ScopedLog))
    {
        return ScopedResult;
    }
    if (!ScopedWrite("settings.toml", InRequest.SettingsText))
    {
        return ScopedResult;
    }
    if (InRequest.IncludeReplay && !InRequest.ReplayBytes.empty())
    {
        const std::filesystem::path ScopedPath = ScopedRoot / "replay.fbyrply";
        std::ofstream ScopedFile(ScopedPath, std::ios::binary | std::ios::trunc);
        if (!ScopedFile)
        {
            SetError(InError, "cannot write replay");
            return ScopedResult;
        }
        ScopedFile.write(reinterpret_cast<const char*>(InRequest.ReplayBytes.data()),
                         static_cast<std::streamsize>(InRequest.ReplayBytes.size()));
        ScopedAdd("replay.fbyrply");
    }
    if (InRequest.IncludeLicenses && InRequest.Licenses != nullptr)
    {
        for (const Assets::LicenseEntry& ScopedEntry : InRequest.Licenses->Entries())
        {
            const std::string ScopedRelative = "Licenses/" + ScopedEntry.Name + ".txt";
            if (!ScopedWrite(ScopedRelative, ScopedEntry.Text))
            {
                return ScopedResult;
            }
        }
    }
    if (InRequest.Legal != nullptr)
    {
        for (const Legal::LegalDocument& ScopedDocument : InRequest.Legal->Manifest().Documents)
        {
            const std::string ScopedRelative = "Legal/" + ScopedDocument.Language + ".txt";
            if (!ScopedWrite(ScopedRelative, ScopedDocument.Text))
            {
                return ScopedResult;
            }
        }
    }
    std::ostringstream ScopedManifest;
    ScopedManifest << "id = \"incident\"\n";
    ScopedManifest << "reason = \"" << ScopedReason << "\"\n";
    ScopedManifest << "file_count = " << ScopedResult.Files.size() << "\n";
    if (!ScopedWrite("manifest.toml", ScopedManifest.str()))
    {
        return ScopedResult;
    }
    ScopedResult.Ok = true;
    ScopedResult.Directory = InRequest.Directory;
    return ScopedResult;
}

std::string BuildCreditsMarkdown(std::span<const Assets::AssetMeta> InAssets,
                                 const Assets::LicenseRegistry& InLicenses)
{
    std::string Scoped;
    Scoped += "# Credits\n\n";
    Scoped += "| Asset | Author | License | Attribution |\n";
    Scoped += "|---|---|---|---|\n";
    for (const Assets::AssetMeta& ScopedAsset : InAssets)
    {
        if (ScopedAsset.sourcePath[0] == '\0')
        {
            continue;
        }
        const std::string_view ScopedLicense = ScopedAsset.license;
        Scoped += "| " + EscapeMarkdown(ScopedAsset.sourcePath) + " | " + EscapeMarkdown(ScopedAsset.author) + " | " +
                  EscapeMarkdown(ScopedLicense) + " | " + EscapeMarkdown(ScopedAsset.attribution) + " |\n";
        if (!ScopedLicense.empty() && InLicenses.Find(ScopedLicense) == nullptr)
        {
            Scoped += "| ! | missing license `" + EscapeMarkdown(ScopedLicense) + "` | | |\n";
        }
    }
    Scoped += "\n## License texts\n\n";
    for (const Assets::LicenseEntry& ScopedEntry : InLicenses.Entries())
    {
        Scoped += "### " + ScopedEntry.Name + "\n\n```text\n" + ScopedEntry.Text + "\n```\n\n";
    }
    return Scoped;
}

bool ReplayPolicy::ShouldStop(std::uint32_t InFrames) const noexcept
{
    return MaxFrames != 0 && InFrames >= MaxFrames;
}

bool ReplayArchive::Save(std::string_view InPath, const Core::ReplaySession& InSession, std::string* InError)
{
    std::vector<std::byte> ScopedBytes;
    if (!InSession.Save(&ScopedBytes))
    {
        SetError(InError, "cannot serialize replay");
        return false;
    }
    std::ofstream ScopedFile(std::string(InPath), std::ios::binary | std::ios::trunc);
    if (!ScopedFile)
    {
        SetError(InError, "cannot open replay for writing");
        return false;
    }
    ScopedFile.write(reinterpret_cast<const char*>(ScopedBytes.data()),
                     static_cast<std::streamsize>(ScopedBytes.size()));
    return static_cast<bool>(ScopedFile);
}

bool ReplayArchive::Load(std::string_view InPath, Core::ReplaySession* OutSession, std::string* InError)
{
    if (OutSession == nullptr)
    {
        SetError(InError, "replay output is null");
        return false;
    }
    std::ifstream ScopedFile(std::string(InPath), std::ios::binary);
    if (!ScopedFile)
    {
        SetError(InError, "cannot open replay for reading");
        return false;
    }
    const std::vector<char> ScopedChars((std::istreambuf_iterator<char>(ScopedFile)),
                                        std::istreambuf_iterator<char>());
    std::vector<std::byte> ScopedBytes(ScopedChars.size());
    std::transform(ScopedChars.begin(), ScopedChars.end(), ScopedBytes.begin(),
                   [](char InByte) { return static_cast<std::byte>(static_cast<unsigned char>(InByte)); });
    Core::ReplaySession ScopedSession;
    if (!ScopedSession.Load(std::span<const std::byte>(ScopedBytes), InError))
    {
        return false;
    }
    *OutSession = std::move(ScopedSession);
    return true;
}
}
