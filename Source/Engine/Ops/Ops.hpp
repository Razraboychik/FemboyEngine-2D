// FemboyEngine2D - developer operations: diagnostics, incident bundles,
// provenance export and replay archives.
#pragma once

#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Assets/LicenseRegistry.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Replay.hpp"
#include "Engine/Legal/LegalService.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Ops
{

enum class DoctorSeverity : std::uint8_t
{
    Info,
    Warning,
    Error
};

struct DoctorIssue
{
    DoctorSeverity Severity = DoctorSeverity::Info;
    std::string Code;
    std::string Path;
    std::string Message;
};

struct DoctorOptions
{
    bool CheckAssets = true;
    bool CheckLicenses = true;
    bool CheckLegal = true;
    bool CheckFonts = true;
    bool Strict = false;
};

struct DoctorReport
{
    std::vector<DoctorIssue> Issues;
    [[nodiscard]] bool HasErrors() const noexcept;
    [[nodiscard]] bool HasWarnings() const noexcept;
    [[nodiscard]] std::uint32_t Count(DoctorSeverity InSeverity) const noexcept;
};

// Read-only project health checks. It never repairs files and never writes.
class ProjectDoctor
{
public:
    [[nodiscard]] static DoctorReport Run(VirtualFileSystem& InVfs, const Assets::LicenseRegistry& InLicenses,
                                          const Legal::LegalService* InLegal, const DoctorOptions& InOptions = {});
};

struct IncidentRequest
{
    std::string Directory;
    std::string Reason = "manual";
    std::string BuildInfo;
    std::string SettingsText;
    std::string LogText;
    // Optional store-only ZIP written after the directory. Empty means no archive.
    std::string ArchivePath;
    std::span<const std::byte> ReplayBytes;
    const Assets::LicenseRegistry* Licenses = nullptr;
    const Legal::LegalService* Legal = nullptr;
    bool IncludeReplay = true;
    bool IncludeLicenses = true;
    std::size_t MaxLogBytes = 4u * 1024u * 1024u;
};

struct IncidentResult
{
    bool Ok = false;
    std::string Directory;
    std::vector<std::string> Files;
};

// Writes a local, inspectable incident directory. There is no network code here.
[[nodiscard]] IncidentResult WriteIncidentBundle(const IncidentRequest& InRequest, std::string* InError = nullptr);

// Signal-path marker. It writes one small file and performs no allocation, so a
// crash hook can call it while the process is dying.
void WriteCrashMarker(const char* InCrashDirectory, const char* InReason) noexcept;

// Scans crash directories for markers left by the previous run and builds their
// full incident bundles at a safe startup point. Returns the number collected.
[[nodiscard]] std::uint32_t CollectPendingCrashIncidents(std::string_view InCrashRoot, std::string_view InBuildInfo,
                                                         std::string_view InSettingsText,
                                                         std::string* InError = nullptr);

// Markdown credits generated from asset provenance and the license registry.
[[nodiscard]] std::string BuildCreditsMarkdown(std::span<const Assets::AssetMeta> InAssets,
                                               const Assets::LicenseRegistry& InLicenses);

// Replay policy is project data; the container stays Core::ReplaySession.
struct ReplayPolicy
{
    std::uint32_t MaxFrames = 216000;
    std::uint32_t KeyframeInterval = 60;
    bool VerifyOnStop = true;

    [[nodiscard]] bool ShouldStop(std::uint32_t InFrames) const noexcept;
};

class ReplayArchive
{
public:
    [[nodiscard]] static bool Save(std::string_view InPath, const Core::ReplaySession& InSession,
                                   std::string* InError = nullptr);
    [[nodiscard]] static bool Load(std::string_view InPath, Core::ReplaySession* OutSession,
                                   std::string* InError = nullptr);
};

}