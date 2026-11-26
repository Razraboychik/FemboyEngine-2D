// FemboyEngine2D - developer operations.
#include "Engine/Ops/Ops.hpp"

#include "Engine/Text/BaseFonts.hpp"

#include "miniz.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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


std::string EscapeToml(std::string_view InText)
{
    std::string Scoped;
    Scoped.reserve(InText.size());
    for (const char ScopedChar : InText)
    {
        const int ScopedCode = static_cast<unsigned char>(ScopedChar);
        if (ScopedCode == 92)
        {
            Scoped.push_back(static_cast<char>(92));
            Scoped.push_back(static_cast<char>(92));
        }
        else if (ScopedChar == '"')
        {
            Scoped.push_back(static_cast<char>(92));
            Scoped.push_back('"');
        }
        else if (ScopedCode == 10)
        {
            Scoped.push_back(static_cast<char>(92));
            Scoped.push_back('n');
        }
        else if (ScopedCode == 13)
        {
            Scoped.push_back(static_cast<char>(92));
            Scoped.push_back('r');
        }
        else if (ScopedCode == 9)
        {
            Scoped.push_back(static_cast<char>(92));
            Scoped.push_back('t');
        }
        else if (ScopedCode < 0x20 || ScopedCode == 0x7F)
        {
            char ScopedEscape[8] = {};
            std::snprintf(ScopedEscape, sizeof(ScopedEscape), "%c%04X", 92, ScopedCode);
            Scoped += ScopedEscape;
        }
        else
        {
            Scoped.push_back(ScopedChar);
        }
    }
    return Scoped;
}

bool MakeSafeRelative(std::string_view InPath, std::string* OutNormalized)
{
    if (InPath.empty() || InPath.find(static_cast<char>(0)) != std::string_view::npos)
    {
        return false;
    }
    std::string ScopedText(InPath);
    std::replace(ScopedText.begin(), ScopedText.end(), static_cast<char>(92), '/');
    if (ScopedText.empty() || ScopedText.front() == '/')
    {
        return false;
    }
    if (ScopedText.find(':') != std::string::npos)
    {
        return false;
    }
    std::filesystem::path ScopedPath(ScopedText);
    if (ScopedPath.is_absolute() || ScopedPath.has_root_name() || ScopedPath.has_root_directory())
    {
        return false;
    }
    for (const std::filesystem::path& ScopedPart : ScopedPath)
    {
        if (ScopedPart == ".." || ScopedPart == ".")
        {
            return false;
        }
    }
    if (OutNormalized != nullptr)
    {
        *OutNormalized = ScopedPath.generic_string();
    }
    return true;
}

std::string ReadMarkerReason(const std::filesystem::path& InPath)
{
    std::ifstream ScopedFile(InPath, std::ios::binary);
    if (!ScopedFile)
    {
        return {};
    }
    std::array<char, 4096> ScopedBuffer{};
    ScopedFile.read(ScopedBuffer.data(), static_cast<std::streamsize>(ScopedBuffer.size()));
    const std::streamsize ScopedRead = ScopedFile.gcount();
    return std::string(ScopedBuffer.data(), static_cast<std::size_t>(ScopedRead));
}

constexpr std::uintmax_t kMaxReplayBytes = 256ull * 1024ull * 1024ull;

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

std::uint32_t Crc32(std::string_view InData)
{
    std::uint32_t ScopedCrc = 0xFFFFFFFFu;
    for (const char ScopedChar : InData)
    {
        ScopedCrc ^= static_cast<std::uint8_t>(ScopedChar);
        for (std::uint32_t ScopedBit = 0; ScopedBit < 8; ++ScopedBit)
        {
            ScopedCrc = (ScopedCrc >> 1) ^ (0xEDB88320u & (0u - (ScopedCrc & 1u)));
        }
    }
    return ScopedCrc ^ 0xFFFFFFFFu;
}

void Append16(std::vector<std::byte>& OutBytes, std::uint16_t InValue)
{
    OutBytes.push_back(static_cast<std::byte>(InValue & 0xFFu));
    OutBytes.push_back(static_cast<std::byte>((InValue >> 8) & 0xFFu));
}

void Append32(std::vector<std::byte>& OutBytes, std::uint32_t InValue)
{
    for (std::uint32_t ScopedByte = 0; ScopedByte < 4; ++ScopedByte)
    {
        OutBytes.push_back(static_cast<std::byte>((InValue >> (ScopedByte * 8)) & 0xFFu));
    }
}

bool DeflateRaw(const std::string& InData, std::string* OutData)
{
    std::size_t ScopedOutSize = 0;
    void* ScopedCompressed = tdefl_compress_mem_to_heap(InData.data(), InData.size(), &ScopedOutSize,
                                                        TDEFL_DEFAULT_MAX_PROBES);
    if (ScopedCompressed == nullptr)
    {
        return false;
    }
    OutData->assign(static_cast<const char*>(ScopedCompressed), ScopedOutSize);
    mz_free(ScopedCompressed);
    return true;
}

bool WriteStoreZip(std::string_view InPath, const std::filesystem::path& InRoot,
                   const std::vector<std::string>& InFiles, std::string* InError)
{
    struct Entry
    {
        std::string Name;
        std::string Data;
        std::string Payload;
        std::uint32_t Crc = 0;
        std::uint32_t Offset = 0;
        std::uint16_t Method = 0;
    };
    constexpr std::uintmax_t kMax32 = std::numeric_limits<std::uint32_t>::max();
    constexpr std::size_t kMax16 = std::numeric_limits<std::uint16_t>::max();
    std::vector<Entry> ScopedEntries;
    std::uintmax_t ScopedZipEstimate = 22;
    for (const std::string& ScopedRelative : InFiles)
    {
        std::string ScopedName;
        if (!MakeSafeRelative(ScopedRelative, &ScopedName))
        {
            SetError(InError, "unsafe incident archive entry: " + ScopedRelative);
            return false;
        }
        const std::filesystem::path ScopedSource = InRoot / std::filesystem::path(ScopedName);
        std::error_code ScopedSizeError;
        const std::uintmax_t ScopedSourceSize = std::filesystem::file_size(ScopedSource, ScopedSizeError);
        if (!ScopedSizeError && ScopedSourceSize > kMax32)
        {
            SetError(InError, "incident archive entry is too large: " + ScopedName);
            return false;
        }
        std::ifstream ScopedFile(ScopedSource, std::ios::binary);
        if (!ScopedFile)
        {
            SetError(InError, "cannot read incident archive entry: " + ScopedName);
            return false;
        }
        Entry ScopedEntry;
        ScopedEntry.Name = std::move(ScopedName);
        ScopedEntry.Data.assign(std::istreambuf_iterator<char>(ScopedFile), std::istreambuf_iterator<char>());
        if (ScopedEntry.Data.size() > kMax32 || ScopedEntry.Name.size() > kMax16)
        {
            SetError(InError, "incident archive entry exceeds ZIP limits: " + ScopedEntry.Name);
            return false;
        }
        ScopedEntry.Crc = Crc32(ScopedEntry.Data);
        std::string ScopedDeflated;
        if (DeflateRaw(ScopedEntry.Data, &ScopedDeflated) && ScopedDeflated.size() < ScopedEntry.Data.size())
        {
            ScopedEntry.Payload = std::move(ScopedDeflated);
            ScopedEntry.Method = 8;
        }
        else
        {
            ScopedEntry.Payload = ScopedEntry.Data;
        }
        if (ScopedEntry.Payload.size() > kMax32)
        {
            SetError(InError, "incident archive payload exceeds ZIP limits: " + ScopedEntry.Name);
            return false;
        }
        ScopedZipEstimate += 30 + ScopedEntry.Name.size() + ScopedEntry.Payload.size() + 46 + ScopedEntry.Name.size();
        if (ScopedZipEstimate > kMax32)
        {
            SetError(InError, "incident archive exceeds the ZIP32 size limit");
            return false;
        }
        ScopedEntries.push_back(std::move(ScopedEntry));
    }
    if (ScopedEntries.size() > kMax16)
    {
        SetError(InError, "incident archive has too many entries");
        return false;
    }
    std::vector<std::byte> ScopedZip;
    std::vector<std::byte> ScopedCentral;
    for (Entry& ScopedEntry : ScopedEntries)
    {
        ScopedEntry.Offset = static_cast<std::uint32_t>(ScopedZip.size());
        Append32(ScopedZip, 0x04034B50u);
        Append16(ScopedZip, 20);
        Append16(ScopedZip, 0);
        Append16(ScopedZip, ScopedEntry.Method);
        Append16(ScopedZip, 0);
        Append16(ScopedZip, 0);
        Append32(ScopedZip, ScopedEntry.Crc);
        Append32(ScopedZip, static_cast<std::uint32_t>(ScopedEntry.Payload.size()));
        Append32(ScopedZip, static_cast<std::uint32_t>(ScopedEntry.Data.size()));
        Append16(ScopedZip, static_cast<std::uint16_t>(ScopedEntry.Name.size()));
        Append16(ScopedZip, 0);
        for (const char ScopedChar : ScopedEntry.Name)
        {
            ScopedZip.push_back(static_cast<std::byte>(ScopedChar));
        }
        for (const char ScopedChar : ScopedEntry.Payload)
        {
            ScopedZip.push_back(static_cast<std::byte>(ScopedChar));
        }
    }
    for (const Entry& ScopedEntry : ScopedEntries)
    {
        Append32(ScopedCentral, 0x02014B50u);
        Append16(ScopedCentral, 20);
        Append16(ScopedCentral, 20);
        Append16(ScopedCentral, 0);
        Append16(ScopedCentral, ScopedEntry.Method);
        Append16(ScopedCentral, 0);
        Append16(ScopedCentral, 0);
        Append32(ScopedCentral, ScopedEntry.Crc);
        Append32(ScopedCentral, static_cast<std::uint32_t>(ScopedEntry.Payload.size()));
        Append32(ScopedCentral, static_cast<std::uint32_t>(ScopedEntry.Data.size()));
        Append16(ScopedCentral, static_cast<std::uint16_t>(ScopedEntry.Name.size()));
        Append16(ScopedCentral, 0);
        Append16(ScopedCentral, 0);
        Append16(ScopedCentral, 0);
        Append16(ScopedCentral, 0);
        Append32(ScopedCentral, 0);
        Append32(ScopedCentral, ScopedEntry.Offset);
        for (const char ScopedChar : ScopedEntry.Name)
        {
            ScopedCentral.push_back(static_cast<std::byte>(ScopedChar));
        }
    }
    const std::uint32_t ScopedCentralOffset = static_cast<std::uint32_t>(ScopedZip.size());
    ScopedZip.insert(ScopedZip.end(), ScopedCentral.begin(), ScopedCentral.end());
    Append32(ScopedZip, 0x06054B50u);
    Append16(ScopedZip, 0);
    Append16(ScopedZip, 0);
    Append16(ScopedZip, static_cast<std::uint16_t>(ScopedEntries.size()));
    Append16(ScopedZip, static_cast<std::uint16_t>(ScopedEntries.size()));
    Append32(ScopedZip, static_cast<std::uint32_t>(ScopedCentral.size()));
    Append32(ScopedZip, ScopedCentralOffset);
    Append16(ScopedZip, 0);
    std::ofstream ScopedOut(std::string(InPath), std::ios::binary | std::ios::trunc);
    if (!ScopedOut)
    {
        SetError(InError, "cannot open incident archive for writing");
        return false;
    }
    ScopedOut.write(reinterpret_cast<const char*>(ScopedZip.data()), static_cast<std::streamsize>(ScopedZip.size()));
    return static_cast<bool>(ScopedOut);
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
    const std::filesystem::path ScopedRoot(InRequest.Directory);
    std::error_code ScopedCode;
    const std::filesystem::file_status ScopedRootStatus = std::filesystem::symlink_status(ScopedRoot, ScopedCode);
    if (!ScopedCode && std::filesystem::is_symlink(ScopedRootStatus))
    {
        SetError(InError, "incident directory must not be a symlink");
        return ScopedResult;
    }
    ScopedCode.clear();
    std::filesystem::create_directories(ScopedRoot, ScopedCode);
    if (ScopedCode)
    {
        SetError(InError, "cannot create incident directory: " + ScopedCode.message());
        return ScopedResult;
    }
    const auto ScopedAdd = [&ScopedResult](std::string InPath)
    {
        ScopedResult.Files.push_back(std::move(InPath));
    };
    const auto ScopedWrite = [&ScopedRoot, &ScopedAdd, InError](std::string_view InRelative, std::string_view InText)
    {
        std::string ScopedNormalized;
        if (!MakeSafeRelative(InRelative, &ScopedNormalized))
        {
            SetError(InError, "unsafe incident path: " + std::string(InRelative));
            return false;
        }
        const std::filesystem::path ScopedPath = ScopedRoot / std::filesystem::path(ScopedNormalized);
        std::error_code ScopedStatusError;
        const std::filesystem::file_status ScopedStatus = std::filesystem::symlink_status(ScopedPath, ScopedStatusError);
        if (!ScopedStatusError && std::filesystem::is_symlink(ScopedStatus))
        {
            SetError(InError, "incident file must not be a symlink: " + ScopedNormalized);
            return false;
        }
        std::error_code ScopedParentError;
        const std::filesystem::file_status ScopedParentStatus =
            std::filesystem::symlink_status(ScopedPath.parent_path(), ScopedParentError);
        if (!ScopedParentError && std::filesystem::is_symlink(ScopedParentStatus))
        {
            SetError(InError, "incident directory must not be a symlink: " + ScopedNormalized);
            return false;
        }
        std::error_code ScopedCreate;
        std::filesystem::create_directories(ScopedPath.parent_path(), ScopedCreate);
        if (!WriteText(ScopedPath, InText, InError))
        {
            return false;
        }
        ScopedAdd(ScopedNormalized);
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
    ScopedManifest << "reason = \"" << EscapeToml(ScopedReason) << "\"\n";
    ScopedManifest << "file_count = " << ScopedResult.Files.size() << "\n";
    if (!ScopedWrite("manifest.toml", ScopedManifest.str()))
    {
        return ScopedResult;
    }
    if (!InRequest.ArchivePath.empty())
    {
        if (!WriteStoreZip(InRequest.ArchivePath, ScopedRoot, ScopedResult.Files, InError))
        {
            return ScopedResult;
        }
        ScopedAdd(InRequest.ArchivePath);
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


void WriteCrashMarker(const char* InCrashDirectory, const char* InReason) noexcept
{
    if (InCrashDirectory == nullptr || InCrashDirectory[0] == '\0')
    {
        return;
    }
    char ScopedPath[1200] = {};
    std::size_t ScopedPathLength = 0;
    for (const char* ScopedChar = InCrashDirectory; *ScopedChar != '\0' && ScopedPathLength + 1 < sizeof(ScopedPath);
         ++ScopedChar)
    {
        ScopedPath[ScopedPathLength++] = *ScopedChar;
    }
    static constexpr char kMarkerSuffix[] = "/incident-request.txt";
    for (const char* ScopedChar = kMarkerSuffix; *ScopedChar != '\0' && ScopedPathLength + 1 < sizeof(ScopedPath);
         ++ScopedChar)
    {
        ScopedPath[ScopedPathLength++] = *ScopedChar;
    }
    ScopedPath[ScopedPathLength] = '\0';
    const char* ScopedReason = InReason != nullptr ? InReason : "crash";
    const std::size_t ScopedLength = std::strlen(ScopedReason);
#if defined(_WIN32)
    const HANDLE ScopedFile = ::CreateFileA(ScopedPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (ScopedFile != INVALID_HANDLE_VALUE)
    {
        DWORD ScopedWritten = 0;
        (void)::WriteFile(ScopedFile, ScopedReason, static_cast<DWORD>(ScopedLength), &ScopedWritten, nullptr);
        (void)::CloseHandle(ScopedFile);
    }
#else
    const int ScopedFile = ::open(ScopedPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ScopedFile >= 0)
    {
        (void)::write(ScopedFile, ScopedReason, ScopedLength);
        (void)::close(ScopedFile);
    }
#endif
}


bool MarkSessionActive(std::string_view InLockPath, std::string_view InApplicationName, std::string* InError)
{
    const std::filesystem::path ScopedPath{std::string(InLockPath)};
    std::error_code ScopedCode;
    std::filesystem::create_directories(ScopedPath.parent_path(), ScopedCode);
    if (ScopedCode)
    {
        SetError(InError, "cannot create session lock directory");
        return false;
    }
    std::ofstream ScopedFile(ScopedPath, std::ios::binary | std::ios::trunc);
    if (!ScopedFile)
    {
        SetError(InError, "cannot write session lock");
        return false;
    }
    ScopedFile << "application = " << InApplicationName << '\n';
    return static_cast<bool>(ScopedFile);
}

void ClearSessionActive(std::string_view InLockPath) noexcept
{
    std::error_code ScopedError;
    std::filesystem::remove(std::filesystem::path(std::string(InLockPath)), ScopedError);
}

std::uint32_t CollectUncleanShutdown(std::string_view InLockPath, std::string_view InBundlePath,
                                     std::string_view InBuildInfo, std::string_view InSettingsText,
                                     std::string* InError)
{
    const std::filesystem::path ScopedLock{std::string(InLockPath)};
    if (!std::filesystem::exists(ScopedLock))
    {
        return 0;
    }
    IncidentRequest ScopedRequest;
    ScopedRequest.Directory = std::string(InBundlePath);
    ScopedRequest.ArchivePath = ScopedRequest.Directory + ".zip";
    ScopedRequest.Reason = "unclean shutdown";
    ScopedRequest.BuildInfo = std::string(InBuildInfo);
    ScopedRequest.SettingsText = std::string(InSettingsText);
    const IncidentResult ScopedResult = WriteIncidentBundle(ScopedRequest, InError);
    if (!ScopedResult.Ok)
    {
        return 0;
    }
    ClearSessionActive(InLockPath);
    return 1;
}

std::uint32_t CollectPendingCrashIncidents(std::string_view InCrashRoot, std::string_view InBuildInfo,
                                           std::string_view InSettingsText, std::string* InError)
{
    std::uint32_t ScopedCollected = 0;
    std::error_code ScopedIterationError;
    const std::filesystem::directory_iterator ScopedEnd;
    std::filesystem::directory_iterator ScopedIterator(std::filesystem::path(std::string(InCrashRoot)),
                                                       ScopedIterationError);
    while (!ScopedIterationError && ScopedIterator != ScopedEnd)
    {
        const std::filesystem::directory_entry ScopedEntry = *ScopedIterator;
        std::error_code ScopedEntryError;
        const std::filesystem::file_status ScopedEntryStatus = ScopedEntry.symlink_status(ScopedEntryError);
        if (!ScopedEntryError && std::filesystem::is_directory(ScopedEntryStatus) &&
            !std::filesystem::is_symlink(ScopedEntryStatus))
        {
            const std::filesystem::path ScopedMarker = ScopedEntry.path() / "incident-request.txt";
            std::error_code ScopedMarkerError;
            const std::filesystem::file_status ScopedMarkerStatus =
                std::filesystem::symlink_status(ScopedMarker, ScopedMarkerError);
            if (!ScopedMarkerError && std::filesystem::is_regular_file(ScopedMarkerStatus) &&
                !std::filesystem::is_symlink(ScopedMarkerStatus))
            {
                std::string ScopedReason = ReadMarkerReason(ScopedMarker);
                if (ScopedReason.empty())
                {
                    ScopedReason = "crash";
                }
                if (ScopedReason.size() > 512)
                {
                    ScopedReason.resize(512);
                }
                IncidentRequest ScopedRequest;
                ScopedRequest.Directory = (ScopedEntry.path() / "incident").string();
                ScopedRequest.ArchivePath = ScopedRequest.Directory + ".zip";
                ScopedRequest.Reason = ScopedReason;
                ScopedRequest.BuildInfo = std::string(InBuildInfo);
                ScopedRequest.SettingsText = std::string(InSettingsText);
                const IncidentResult ScopedResult = WriteIncidentBundle(ScopedRequest, InError);
                if (ScopedResult.Ok)
                {
                    std::error_code ScopedRemoveError;
                    std::filesystem::remove(ScopedMarker, ScopedRemoveError);
                    ++ScopedCollected;
                }
            }
        }
        ScopedIterator.increment(ScopedIterationError);
    }
    return ScopedCollected;
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
    if (ScopedBytes.size() > kMaxReplayBytes)
    {
        SetError(InError, "replay is too large to write");
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
    const std::filesystem::path ScopedPath{std::string(InPath)};
    std::error_code ScopedSizeError;
    const std::uintmax_t ScopedFileSize = std::filesystem::file_size(ScopedPath, ScopedSizeError);
    if (ScopedSizeError)
    {
        SetError(InError, "cannot stat replay for reading");
        return false;
    }
    if (ScopedFileSize > kMaxReplayBytes)
    {
        SetError(InError, "replay is too large to read");
        return false;
    }
    std::ifstream ScopedFile(ScopedPath, std::ios::binary);
    if (!ScopedFile)
    {
        SetError(InError, "cannot open replay for reading");
        return false;
    }
    std::vector<char> ScopedChars(static_cast<std::size_t>(ScopedFileSize));
    if (!ScopedChars.empty())
    {
        ScopedFile.read(ScopedChars.data(), static_cast<std::streamsize>(ScopedChars.size()));
        if (ScopedFile.gcount() != static_cast<std::streamsize>(ScopedChars.size()))
        {
            SetError(InError, "cannot read replay");
            return false;
        }
    }
    if (ScopedFile.peek() != std::char_traits<char>::eof())
    {
        SetError(InError, "replay changed while reading");
        return false;
    }
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
