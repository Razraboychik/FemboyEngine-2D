// FemboyEngine2D - Project Doctor, incident bundle, provenance and replay tests.
#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Assets/LicenseRegistry.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Replay.hpp"
#include "Engine/Legal/LegalService.hpp"
#include "Engine/Ops/Ops.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main()
{
    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    if (!ScopedVfs.MountDirectory("assets", 0, &ScopedError))
    {
        std::fprintf(stderr, "FAIL: mount assets: %s\n", ScopedError.c_str());
        return 1;
    }
    Fby::Assets::LicenseRegistry ScopedLicenses;
    if (!ScopedLicenses.LoadFromVfs(ScopedVfs, "Licenses", &ScopedError))
    {
        std::fprintf(stderr, "FAIL: load licenses: %s\n", ScopedError.c_str());
        return 1;
    }
    Fby::Legal::LegalService ScopedLegal;
    if (!ScopedLegal.LoadFromVfs(ScopedVfs, "Legal", &ScopedError))
    {
        std::fprintf(stderr, "FAIL: load legal: %s\n", ScopedError.c_str());
        return 1;
    }
    const Fby::Ops::DoctorReport ScopedReport =
        Fby::Ops::ProjectDoctor::Run(ScopedVfs, ScopedLicenses, &ScopedLegal);
    if (ScopedReport.HasErrors())
    {
        for (const Fby::Ops::DoctorIssue& ScopedIssue : ScopedReport.Issues)
        {
            std::fprintf(stderr, "FAIL: doctor %s: %s\n", ScopedIssue.Code.c_str(), ScopedIssue.Message.c_str());
        }
        return 1;
    }

    Fby::Assets::AssetMeta ScopedAsset;
    ScopedAsset.kind = Fby::AssetKind::Texture;
    ScopedAsset.valid = true;
    std::snprintf(ScopedAsset.sourcePath, sizeof(ScopedAsset.sourcePath), "%s", "images/hero.png");
    std::snprintf(ScopedAsset.author, sizeof(ScopedAsset.author), "%s", "Example Artist");
    std::snprintf(ScopedAsset.license, sizeof(ScopedAsset.license), "%s", "Noto-OFL");
    std::snprintf(ScopedAsset.attribution, sizeof(ScopedAsset.attribution), "%s", "Example Studio");
    const std::array<Fby::Assets::AssetMeta, 1> ScopedAssets{ScopedAsset};
    const std::string ScopedCredits = Fby::Ops::BuildCreditsMarkdown(ScopedAssets, ScopedLicenses);
    if (ScopedCredits.find("Example Artist") == std::string::npos || ScopedCredits.find("Noto-OFL") == std::string::npos)
    {
        std::fprintf(stderr, "FAIL: credits do not contain provenance\n");
        return 1;
    }

    std::string ScopedMetadata;
    if (!Fby::Assets::WriteAssetMetaFile(ScopedAsset, {}, &ScopedMetadata, &ScopedError))
    {
        std::fprintf(stderr, "FAIL: write metadata: %s\n", ScopedError.c_str());
        return 1;
    }
    Fby::Assets::AssetMeta ScopedParsed;
    Fby::Assets::AssetDependency ScopedDependencies[4]{};
    std::uint32_t ScopedDependencyCount = 0;
    if (!Fby::Assets::ParseAssetMetaFile(ScopedMetadata, &ScopedParsed, ScopedDependencies, 4,
                                        &ScopedDependencyCount, &ScopedError) ||
        std::string(ScopedParsed.author) != "Example Artist" || std::string(ScopedParsed.license) != "Noto-OFL")
    {
        std::fprintf(stderr, "FAIL: metadata provenance round-trip: %s\n%s\nauthor=%s license=%s\n",
                     ScopedError.c_str(), ScopedMetadata.c_str(), ScopedParsed.author, ScopedParsed.license);
        return 1;
    }

    Fby::Core::ReplaySession ScopedReplay;
    ScopedReplay.BeginRecord(2);
    const std::array<std::byte, 2> ScopedPayload{std::byte{1}, std::byte{2}};
    ScopedReplay.RecordFrame(1, ScopedPayload, 0x1234);
    ScopedReplay.RecordFrame(2, ScopedPayload, 0x5678);
    ScopedReplay.EndRecord();
    const std::string ScopedReplayPath = "build/ops_replay_test.fbyrply";
    if (!Fby::Ops::ReplayArchive::Save(ScopedReplayPath, ScopedReplay, &ScopedError))
    {
        std::fprintf(stderr, "FAIL: save replay: %s\n", ScopedError.c_str());
        return 1;
    }
    Fby::Core::ReplaySession ScopedLoadedReplay;
    if (!Fby::Ops::ReplayArchive::Load(ScopedReplayPath, &ScopedLoadedReplay, &ScopedError) ||
        ScopedLoadedReplay.FrameCount() != 2)
    {
        std::fprintf(stderr, "FAIL: load replay: %s\n", ScopedError.c_str());
        return 1;
    }
    {
        const std::filesystem::path ScopedBigReplay = "build/ops_replay_oversized.fbyrply";
        {
            std::ofstream ScopedBigFile(ScopedBigReplay, std::ios::binary | std::ios::trunc);
            ScopedBigFile.seekp(static_cast<std::streamoff>(256ull * 1024ull * 1024ull));
            ScopedBigFile.put('X');
        }
        Fby::Core::ReplaySession ScopedOversizedReplay;
        std::string ScopedOversizedError;
        if (Fby::Ops::ReplayArchive::Load(ScopedBigReplay.string(), &ScopedOversizedReplay, &ScopedOversizedError))
        {
            std::fprintf(stderr, "FAIL: oversized replay was accepted");
            return 1;
        }
        std::filesystem::remove(ScopedBigReplay);
    }
    if (!Fby::Ops::ReplayPolicy{2, 2, true}.ShouldStop(2) || Fby::Ops::ReplayPolicy{3, 2, true}.ShouldStop(2))
    {
        std::fprintf(stderr, "FAIL: replay frame budget\n");
        return 1;
    }
    std::vector<std::byte> ScopedReplayBytes;
    ScopedReplay.Save(&ScopedReplayBytes);
    Fby::Ops::IncidentRequest ScopedIncident;
    ScopedIncident.Directory = "build/ops_incident_test";
    std::string ScopedRawReason = "test ";
    ScopedRawReason.push_back(static_cast<char>(34));
    ScopedRawReason += "quoted";
    ScopedRawReason.push_back(static_cast<char>(34));
    ScopedRawReason.push_back(static_cast<char>(10));
    ScopedRawReason += "line";
    ScopedIncident.Reason = ScopedRawReason;
    ScopedIncident.BuildInfo = "test build";
    ScopedIncident.SettingsText = "x = 1";
    ScopedIncident.LogText = "log line";
    ScopedIncident.ArchivePath = "build/ops_incident_test.zip";
    ScopedIncident.ReplayBytes = ScopedReplayBytes;
    ScopedIncident.Licenses = &ScopedLicenses;
    ScopedIncident.Legal = &ScopedLegal;
    const Fby::Ops::IncidentResult ScopedBundle = Fby::Ops::WriteIncidentBundle(ScopedIncident, &ScopedError);
    if (!ScopedBundle.Ok || !std::filesystem::exists("build/ops_incident_test/manifest.toml") ||
        !std::filesystem::exists("build/ops_incident_test/replay.fbyrply") ||
        !std::filesystem::exists("build/ops_incident_test/Licenses") ||
        !std::filesystem::exists("build/ops_incident_test.zip"))
    {
        std::fprintf(stderr, "FAIL: incident bundle: %s", ScopedError.c_str());
        return 1;
    }
    {
        std::ifstream ScopedManifestFile("build/ops_incident_test/manifest.toml", std::ios::binary);
        const std::string ScopedManifestText((std::istreambuf_iterator<char>(ScopedManifestFile)),
                                             std::istreambuf_iterator<char>());
        if (ScopedManifestText.find(ScopedRawReason) != std::string::npos)
        {
            std::fprintf(stderr, "FAIL: manifest reason was not TOML-escaped");
            return 1;
        }
        std::ifstream ScopedZipFile("build/ops_incident_test.zip", std::ios::binary);
        const std::string ScopedZipText((std::istreambuf_iterator<char>(ScopedZipFile)),
                                        std::istreambuf_iterator<char>());
        if (ScopedZipText.find("README.txt") == std::string::npos ||
            ScopedZipText.find("ops_incident_test") != std::string::npos)
        {
            std::fprintf(stderr, "FAIL: incident ZIP stores absolute paths");
            return 1;
        }
    }
    std::filesystem::create_directories("build/ops_crash_test/1000");
    Fby::Ops::WriteCrashMarker("build/ops_crash_test/1000", "fatal signal");
    if (!std::filesystem::exists("build/ops_crash_test/1000/incident-request.txt"))
    {
        std::fprintf(stderr, "FAIL: crash marker was not written\n");
        return 1;
    }
    if (Fby::Ops::CollectPendingCrashIncidents("build/ops_crash_test", "test", "x", &ScopedError) != 1 ||
        !std::filesystem::exists("build/ops_crash_test/1000/incident/README.txt") ||
        std::filesystem::exists("build/ops_crash_test/1000/incident-request.txt"))
    {
        std::fprintf(stderr, "FAIL: crash marker collection: %s\n", ScopedError.c_str());
        return 1;
    }
#if !defined(_WIN32)
    {
        std::filesystem::create_directories("build/ops_crash_symlink_target");
        Fby::Ops::WriteCrashMarker("build/ops_crash_symlink_target", "fatal signal");
        std::filesystem::create_directories("build/ops_crash_symlink");
        std::filesystem::create_directory_symlink("build/ops_crash_symlink_target", "build/ops_crash_symlink/link");
        if (Fby::Ops::CollectPendingCrashIncidents("build/ops_crash_symlink", "test", "x", &ScopedError) != 0)
        {
            std::fprintf(stderr, "FAIL: crash collection followed a symlink");
            return 1;
        }
        std::filesystem::remove_all("build/ops_crash_symlink");
        std::filesystem::remove_all("build/ops_crash_symlink_target");
    }
#endif
    if (!Fby::Ops::MarkSessionActive("build/ops_session_test.lock", "test", &ScopedError) ||
        Fby::Ops::CollectUncleanShutdown("build/ops_session_test.lock", "build/ops_session_bundle", "test", "x",
                                         &ScopedError) != 1 ||
        std::filesystem::exists("build/ops_session_test.lock") ||
        !std::filesystem::exists("build/ops_session_bundle/README.txt"))
    {
        std::fprintf(stderr, "FAIL: unclean shutdown collection: %s\n", ScopedError.c_str());
        return 1;
    }
    std::printf("ops: OK (%zu doctor issues, %zu incident files, %zu credits bytes)\n", ScopedReport.Issues.size(),
                ScopedBundle.Files.size(), ScopedCredits.size());
    return 0;
}
