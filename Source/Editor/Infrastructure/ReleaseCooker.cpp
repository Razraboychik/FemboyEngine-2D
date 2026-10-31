// FemboyEngine2D - Build & Pack Game orchestrator implementation.
#include "Engine/Core/Time.hpp"
#include "Editor/Infrastructure/ReleaseCooker.hpp"

#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Engine/Assets/FemCrypto.hpp"
#include "Engine/Core/SecureRng.hpp"
#include "Engine/Core/ThirdPartyNotices.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <sys/wait.h>
#endif

namespace Fby::Editor
{
namespace
{
using Fby::Assets::CookProgressRecord;
using Fby::Assets::CookProgressStage;

constexpr std::uint32_t kSeedBytes = 32;
constexpr std::size_t kCommandBytes = 4096;

void CopyText(char* InDestination, std::size_t InCapacity, std::string_view InText) noexcept
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedBytes = InText.size() < InCapacity - 1 ? InText.size() : InCapacity - 1;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedBytes; ++ScopedIndex)
    {
        InDestination[ScopedIndex] = InText[ScopedIndex];
    }
    InDestination[ScopedBytes] = '\0';
}

[[nodiscard]] bool FileExists(const char* InPath) noexcept
{
    if (InPath == nullptr || InPath[0] == '\0')
    {
        return false;
    }
    std::error_code ScopedError;
    return std::filesystem::is_regular_file(InPath, ScopedError);
}

// FNV-1a 64 of a file's bytes plus its size. Store manifests use it to prove the
// archive/plugin in a store folder is the one the cooker produced.
[[nodiscard]] bool HashFile(const std::filesystem::path& InPath, std::uint64_t* OutHash, std::uint64_t* OutBytes) noexcept
{
    if (OutHash == nullptr || OutBytes == nullptr)
    {
        return false;
    }
    std::ifstream ScopedFile(InPath, std::ios::binary);
    if (!ScopedFile)
    {
        return false;
    }
    std::uint64_t ScopedHash = 1469598103934665603ull;
    std::uint64_t ScopedBytes = 0;
    std::array<char, 65536> ScopedBuffer{};
    while (ScopedFile)
    {
        ScopedFile.read(ScopedBuffer.data(), static_cast<std::streamsize>(ScopedBuffer.size()));
        const std::streamsize ScopedRead = ScopedFile.gcount();
        for (std::streamsize ScopedI = 0; ScopedI < ScopedRead; ++ScopedI)
        {
            ScopedHash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ScopedBuffer[static_cast<std::size_t>(ScopedI)]));
            ScopedHash *= 1099511628211ull;
        }
        ScopedBytes += static_cast<std::uint64_t>(ScopedRead);
    }
    *OutHash = ScopedHash;
    *OutBytes = ScopedBytes;
    return true;
}

[[nodiscard]] std::string HashHex(std::uint64_t InHash)
{
    char ScopedBuffer[32] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%016llX", static_cast<unsigned long long>(InHash));
    return ScopedBuffer;
}

[[nodiscard]] bool ReadSeed(const char* InPath, std::array<std::byte, kSeedBytes>* OutSeed) noexcept
{
    std::FILE* ScopedFile = std::fopen(InPath, "rb");
    if (ScopedFile == nullptr)
    {
        return false;
    }
    const std::size_t ScopedRead = std::fread(OutSeed->data(), 1, OutSeed->size(), ScopedFile);
    (void)std::fclose(ScopedFile);
    return ScopedRead == OutSeed->size();
}

[[nodiscard]] bool WriteSeed(const char* InPath, const std::array<std::byte, kSeedBytes>& InSeed) noexcept
{
    std::FILE* ScopedFile = std::fopen(InPath, "wb");
    if (ScopedFile == nullptr)
    {
        return false;
    }
    const std::size_t ScopedWritten = std::fwrite(InSeed.data(), 1, InSeed.size(), ScopedFile);
    (void)std::fclose(ScopedFile);
    return ScopedWritten == InSeed.size();
}

[[nodiscard]] ReleaseCookStage MapStage(CookProgressStage InStage) noexcept
{
    switch (InStage)
    {
        case CookProgressStage::Idle:
            return ReleaseCookStage::PreparingKey;
        case CookProgressStage::Planning:
            return ReleaseCookStage::Cooking;
        case CookProgressStage::Cooking:
            return ReleaseCookStage::Cooking;
        case CookProgressStage::Packing:
            return ReleaseCookStage::Packing;
        case CookProgressStage::Compressing:
            return ReleaseCookStage::Compressing;
        case CookProgressStage::Encrypting:
            return ReleaseCookStage::Encrypting;
        case CookProgressStage::Succeeded:
            return ReleaseCookStage::Succeeded;
        case CookProgressStage::Failed:
            return ReleaseCookStage::Failed;
        case CookProgressStage::Cancelled:
            return ReleaseCookStage::Cancelled;
    }
    return ReleaseCookStage::Cooking;
}

[[nodiscard]] bool ReadProgressRecord(const char* InPath, CookProgressRecord* OutRecord) noexcept
{
    if (InPath == nullptr || InPath[0] == '\0' || OutRecord == nullptr)
    {
        return false;
    }
    for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < 4; ++ScopedAttempt)
    {
        std::FILE* ScopedFile = std::fopen(InPath, "rb");
        if (ScopedFile == nullptr)
        {
            return false;
        }
        CookProgressRecord ScopedFirst{};
        const std::size_t ScopedRead = std::fread(&ScopedFirst, sizeof(ScopedFirst), 1, ScopedFile);
        (void)std::fclose(ScopedFile);
        if (ScopedRead != 1 || std::memcmp(ScopedFirst.magic, Fby::Assets::kCookProgressMagic, 8) != 0 ||
            ScopedFirst.version != Fby::Assets::kCookProgressVersion)
        {
            return false;
        }
        ScopedFile = std::fopen(InPath, "rb");
        if (ScopedFile == nullptr)
        {
            return false;
        }
        CookProgressRecord ScopedSecond{};
        const std::size_t ScopedReadSecond = std::fread(&ScopedSecond, sizeof(ScopedSecond), 1, ScopedFile);
        (void)std::fclose(ScopedFile);
        if (ScopedReadSecond != 1)
        {
            continue;
        }
        if (std::memcmp(&ScopedFirst, &ScopedSecond, sizeof(ScopedFirst)) == 0)
        {
            *OutRecord = ScopedFirst;
            return true;
        }
    }
    return false;
}
}

bool BakeReleaseKey(const char* InSeedPath, const char* InKeyPath, const char* InHeaderPath, char* OutError,
                    std::size_t InErrorCapacity) noexcept
{
    const auto ScopedFail = [OutError, InErrorCapacity](const char* InMessage)
    {
        if (OutError != nullptr && InErrorCapacity != 0)
        {
            CopyText(OutError, InErrorCapacity, InMessage);
        }
        return false;
    };
    if (InSeedPath == nullptr || InKeyPath == nullptr || InHeaderPath == nullptr)
    {
        return ScopedFail("key paths must not be null");
    }

    std::array<std::byte, kSeedBytes> ScopedSeed{};
    if (!ReadSeed(InSeedPath, &ScopedSeed))
    {
        if (!Fby::SecureRng::Fill(ScopedSeed))
        {
            return ScopedFail("OS entropy source is unavailable");
        }
        if (!WriteSeed(InSeedPath, ScopedSeed))
        {
            return ScopedFail("cannot write the project release seed");
        }
    }
    if (!WriteSeed(InKeyPath, ScopedSeed))
    {
        return ScopedFail("cannot write the build-scoped release key");
    }

    std::array<std::uint8_t, kSeedBytes> ScopedMask{};
    std::array<std::uint8_t, kSeedBytes> ScopedMasked{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSeedBytes; ++ScopedIndex)
    {
        ScopedMask[ScopedIndex] = static_cast<std::uint8_t>(0x5Au ^ static_cast<std::uint8_t>(ScopedIndex * 29u + 7u));
        ScopedMasked[ScopedIndex] =
            static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(ScopedSeed[ScopedIndex]) ^ ScopedMask[ScopedIndex]);
    }

    std::FILE* ScopedHeader = std::fopen(InHeaderPath, "wb");
    if (ScopedHeader == nullptr)
    {
        return ScopedFail("cannot write the release key header");
    }
    std::fprintf(ScopedHeader, "#pragma once\n"
                               "#include <cstdint>\n"
                               "#if defined(_WIN32)\n"
                               "#define FBY_RELEASE_KEY_EXPORT extern \"C\" __declspec(dllexport)\n"
                               "#else\n"
                               "#define FBY_RELEASE_KEY_EXPORT extern \"C\" __attribute__((visibility(\"default\")))\n"
                               "#endif\n"
                               "FBY_RELEASE_KEY_EXPORT bool fby_release_archive_key(std::uint8_t* OutKey, "
                               "std::uint32_t InCapacity) noexcept\n"
                               "{\n"
                               "    if (OutKey == nullptr || InCapacity < 32u)\n"
                               "    {\n"
                               "        return false;\n"
                               "    }\n"
                               "    constexpr std::uint8_t kMasked[32] = {");
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSeedBytes; ++ScopedIndex)
    {
        std::fprintf(ScopedHeader, "%s%u", ScopedIndex == 0 ? "" : ", ",
                     static_cast<unsigned>(ScopedMasked[ScopedIndex]));
    }
    std::fprintf(ScopedHeader, "};\n    constexpr std::uint8_t kMask[32] = {");
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSeedBytes; ++ScopedIndex)
    {
        std::fprintf(ScopedHeader, "%s%u", ScopedIndex == 0 ? "" : ", ",
                     static_cast<unsigned>(ScopedMask[ScopedIndex]));
    }
    std::fprintf(ScopedHeader, "};\n"
                               "    for (std::uint32_t i = 0; i < 32u; ++i)\n"
                               "    {\n"
                               "        OutKey[i] = static_cast<std::uint8_t>(kMasked[i] ^ kMask[i]);\n"
                               "    }\n"
                               "    return true;\n"
                               "}\n");
    (void)std::fclose(ScopedHeader);
    return true;
}

ReleaseCooker::~ReleaseCooker()
{
    Cancel();
    if (MWorker.joinable())
    {
        MWorker.join();
    }
}

ReleaseCookConfig ReleaseCooker::FromProject(const ProjectPaths& InPaths, const ProjectSettings& InSettings,
                                             std::string_view InEngineRoot, std::string_view InPackerPath)
{
    ReleaseCookConfig ScopedConfig;
    CopyText(ScopedConfig.projectName, sizeof(ScopedConfig.projectName), InSettings.name);
    CopyText(ScopedConfig.projectRoot, sizeof(ScopedConfig.projectRoot), InPaths.root.generic_string());
    CopyText(ScopedConfig.assetsRoot, sizeof(ScopedConfig.assetsRoot), InPaths.assets.generic_string());
    // Output is project-local: <root>/build/release for intermediates, while the
    const std::string ScopedOutput = InPaths.release.generic_string();
    const std::string ScopedArchives = InPaths.archives.generic_string();
    CopyText(ScopedConfig.outputRoot, sizeof(ScopedConfig.outputRoot), ScopedOutput);
    CopyText(ScopedConfig.archivesRoot, sizeof(ScopedConfig.archivesRoot), ScopedArchives);
    CopyText(ScopedConfig.storesRoot, sizeof(ScopedConfig.storesRoot), InPaths.stores.generic_string());
    CopyText(ScopedConfig.aotOutputPath, sizeof(ScopedConfig.aotOutputPath), ScopedOutput + "/aot");
    CopyText(ScopedConfig.packPath, sizeof(ScopedConfig.packPath), ScopedOutput + "/assets.fempak");
    CopyText(ScopedConfig.archivePath, sizeof(ScopedConfig.archivePath), ScopedArchives + "/game.femarc");
    CopyText(ScopedConfig.packerPath, sizeof(ScopedConfig.packerPath), InPackerPath);
    CopyText(ScopedConfig.engineSourcePath, sizeof(ScopedConfig.engineSourcePath),
             std::string(InEngineRoot) + "/Source");
    CopyText(ScopedConfig.keySeedPath, sizeof(ScopedConfig.keySeedPath),
             (InPaths.root / ".fbyrelease.seed").generic_string());
    CopyText(ScopedConfig.keyHeaderPath, sizeof(ScopedConfig.keyHeaderPath),
             ScopedOutput + "/ReleaseKey.generated.hpp");
    CopyText(ScopedConfig.keyFileTempPath, sizeof(ScopedConfig.keyFileTempPath), ScopedOutput + "/release.key.tmp");
    CopyText(ScopedConfig.progressPath, sizeof(ScopedConfig.progressPath), ScopedOutput + "/cook.progress");
    CopyText(ScopedConfig.logPath, sizeof(ScopedConfig.logPath), ScopedOutput + "/release.log");
    ScopedConfig.profile = InSettings.build;
    ScopedConfig.cipher = 1;
    ScopedConfig.compileAot = true;
    ScopedConfig.keepAotSource = true;
    return ScopedConfig;
}

bool ReleaseCooker::Start(const ReleaseCookConfig& InConfig, const Script::LogicGraph* InGraph, char* OutError,
                          std::size_t InErrorCapacity)
{
    const auto ScopedSetError = [OutError, InErrorCapacity](const char* InMessage)
    {
        if (OutError != nullptr && InErrorCapacity != 0)
        {
            CopyText(OutError, InErrorCapacity, InMessage);
        }
    };

    if (MRunning.load(std::memory_order_acquire))
    {
        ScopedSetError("a release cook is already running");
        return false;
    }
    Poll();
    if (MWorker.joinable())
    {
        ScopedSetError("the previous release worker did not finish");
        return false;
    }
    if (InConfig.assetsRoot[0] == '\0' || InConfig.outputRoot[0] == '\0' || InConfig.packerPath[0] == '\0')
    {
        ScopedSetError("release config needs assetsRoot, outputRoot and packerPath");
        return false;
    }
    if (InConfig.cipher > 2)
    {
        ScopedSetError("release cipher must be 0, 1 (ChaCha20-Poly1305) or 2 (AES-256-GCM)");
        return false;
    }
    if (!FileExists(InConfig.packerPath))
    {
        ScopedSetError("fby_fempak not found at the configured packerPath");
        return false;
    }
    if ((InConfig.Encrypts() || InConfig.compileAot) && InConfig.keySeedPath[0] == '\0')
    {
        ScopedSetError("the release pipeline needs a project-local key seed path");
        return false;
    }

    std::error_code ScopedError;
    std::filesystem::create_directories(InConfig.outputRoot, ScopedError);
    std::filesystem::create_directories(InConfig.aotOutputPath, ScopedError);
    if (InConfig.archivesRoot[0] != '\0')
    {
        std::filesystem::create_directories(InConfig.archivesRoot, ScopedError);
        std::filesystem::create_directories(std::filesystem::path(InConfig.archivePath).parent_path(), ScopedError);
    }
    if (InConfig.storesRoot[0] != '\0')
    {
        for (const std::string_view ScopedStore : {"steam", "epic", "gog"})
        {
            std::filesystem::create_directories(std::filesystem::path(InConfig.storesRoot) / ScopedStore, ScopedError);
        }
    }
    const std::filesystem::path ScopedSeedParent = std::filesystem::path(InConfig.keySeedPath).parent_path();
    if (!ScopedSeedParent.empty())
    {
        std::filesystem::create_directories(ScopedSeedParent, ScopedError);
    }
    if (ScopedError)
    {
        ScopedSetError("cannot create the release output directory");
        return false;
    }

    MConfig = InConfig;
    MHasGraph = InGraph != nullptr && InGraph->nodeCount != 0;
    if (MHasGraph)
    {
        MGraph = *InGraph;
    }
    else
    {
        MGraph = Script::LogicGraph{};
    }
    MStopRequested.store(false, std::memory_order_release);
    MKeyBaked.store(false, std::memory_order_release);
    {
        std::lock_guard ScopedLock(MProgressMutex);
        MProgress = ReleaseCookProgress{};
        MResult = ReleaseCookResult{};
        MProgress.running = true;
        MProgress.stage = ReleaseCookStage::PreparingKey;
        CopyText(MProgress.message, sizeof(MProgress.message), "Build & Pack Game");
    }
    MRunning.store(true, std::memory_order_release);
    try
    {
        MWorker = std::jthread([this](std::stop_token InStop) { Run(InStop); });
    }
    catch (...)
    {
        MRunning.store(false, std::memory_order_release);
        ScopedSetError("could not start the release worker");
        return false;
    }
    return true;
}

void ReleaseCooker::Cancel() noexcept
{
    MStopRequested.store(true, std::memory_order_release);
    if (MWorker.joinable())
    {
        MWorker.request_stop();
    }
}

void ReleaseCooker::Poll() noexcept
{
    if (!MRunning.load(std::memory_order_acquire) && MWorker.joinable())
    {
        MWorker.join();
    }
}

ReleaseCookProgress ReleaseCooker::Progress() const noexcept
{
    std::lock_guard ScopedLock(MProgressMutex);
    return MProgress;
}

ReleaseCookResult ReleaseCooker::Result() const noexcept
{
    std::lock_guard ScopedLock(MProgressMutex);
    return MResult;
}

void ReleaseCooker::SetProgress(const ReleaseCookProgress& InProgress) noexcept
{
    std::lock_guard ScopedLock(MProgressMutex);
    MProgress = InProgress;
}

void ReleaseCooker::SetStage(ReleaseCookStage InStage, std::string_view InMessage) noexcept
{
    std::lock_guard ScopedLock(MProgressMutex);
    MProgress.stage = InStage;
    MProgress.running = InStage != ReleaseCookStage::Succeeded && InStage != ReleaseCookStage::Failed &&
                        InStage != ReleaseCookStage::Cancelled;
    MProgress.failed = InStage == ReleaseCookStage::Failed;
    CopyText(MProgress.message, sizeof(MProgress.message), InMessage);
}

void ReleaseCooker::Fail(std::string_view InMessage) noexcept
{
    std::lock_guard ScopedLock(MProgressMutex);
    MProgress.stage = ReleaseCookStage::Failed;
    MProgress.running = false;
    MProgress.failed = true;
    CopyText(MProgress.message, sizeof(MProgress.message), InMessage);
    MResult.success = false;
    CopyText(MResult.message, sizeof(MResult.message), InMessage);
}

void ReleaseCooker::PollProgressFile() noexcept
{
    CookProgressRecord ScopedRecord;
    if (!ReadProgressRecord(MConfig.progressPath, &ScopedRecord))
    {
        return;
    }
    std::lock_guard ScopedLock(MProgressMutex);
    MProgress.stage = MapStage(static_cast<CookProgressStage>(ScopedRecord.stage));
    MProgress.completed = ScopedRecord.completed;
    MProgress.total = ScopedRecord.total;
    MProgress.rawBytes = ScopedRecord.rawBytes;
    MProgress.storedBytes = ScopedRecord.storedBytes;
    MProgress.bytesPerSecond = ScopedRecord.bytesPerSecond;
    MProgress.cipher = ScopedRecord.cipher;
    MProgress.encrypted = ScopedRecord.Encrypted();
    MProgress.running = ScopedRecord.Active();
    MProgress.failed = ScopedRecord.Failed();
    CopyText(MProgress.asset, sizeof(MProgress.asset), ScopedRecord.asset);
    CopyText(MProgress.message, sizeof(MProgress.message), ScopedRecord.message);
}

void ReleaseCooker::WipeKey() noexcept
{
    const std::array<std::byte, kSeedBytes> ScopedZero{};
    if (MKeyFile[0] != '\0')
    {
        (void)WriteSeed(MKeyFile.data(), ScopedZero);
        std::error_code ScopedError;
        std::filesystem::remove(MKeyFile.data(), ScopedError);
    }
    if (MKeyHeader[0] != '\0')
    {
        std::error_code ScopedError;
        std::filesystem::remove(MKeyHeader.data(), ScopedError);
    }
}

bool ReleaseCooker::PrepareKey() noexcept
{
    CopyText(MKeyFile.data(), MKeyFile.size(), MConfig.keyFileTempPath);
    CopyText(MKeyHeader.data(), MKeyHeader.size(), MConfig.keyHeaderPath);
    return BakeReleaseKey(MConfig.keySeedPath, MKeyFile.data(), MKeyHeader.data());
}

bool ReleaseCooker::RunChild(const char* InCommandLine, ReleaseCookStage InStage, const char* InAsset,
                             std::uint32_t* OutExitCode)
{
    if (InCommandLine == nullptr || OutExitCode == nullptr)
    {
        return false;
    }
    *OutExitCode = 1;
    SetStage(InStage, InAsset);
#if defined(_WIN32)
    std::array<char, kCommandBytes> ScopedCommand{};
    CopyText(ScopedCommand.data(), ScopedCommand.size(), InCommandLine);
    HANDLE ScopedLog = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES ScopedAttributes{};
    ScopedAttributes.nLength = sizeof(ScopedAttributes);
    ScopedAttributes.bInheritHandle = TRUE;
    if (MConfig.logPath[0] != '\0')
    {
        ScopedLog = CreateFileA(MConfig.logPath, GENERIC_WRITE, FILE_SHARE_READ, &ScopedAttributes, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    STARTUPINFOA ScopedStartup{};
    ScopedStartup.cb = sizeof(ScopedStartup);
    if (ScopedLog != INVALID_HANDLE_VALUE)
    {
        ScopedStartup.dwFlags = STARTF_USESTDHANDLES;
        ScopedStartup.hStdOutput = ScopedLog;
        ScopedStartup.hStdError = ScopedLog;
        ScopedStartup.hStdInput = nullptr;
    }
    PROCESS_INFORMATION ScopedProcess{};
    const BOOL ScopedInherit = ScopedLog != INVALID_HANDLE_VALUE ? TRUE : FALSE;
    const BOOL ScopedCreated = CreateProcessA(MConfig.packerPath, ScopedCommand.data(), nullptr, nullptr, ScopedInherit,
                                              CREATE_NO_WINDOW, nullptr, nullptr, &ScopedStartup, &ScopedProcess);
    if (ScopedLog != INVALID_HANDLE_VALUE)
    {
        (void)CloseHandle(ScopedLog);
    }
    if (!ScopedCreated)
    {
        std::fprintf(stderr, "release: CreateProcess failed (%lu) for %s\n", static_cast<unsigned long>(GetLastError()),
                     ScopedCommand.data());
        return false;
    }
    for (;;)
    {
        const DWORD ScopedWait = WaitForSingleObject(ScopedProcess.hProcess, 50);
        PollProgressFile();
        if (MStopRequested.load(std::memory_order_acquire))
        {
            (void)TerminateProcess(ScopedProcess.hProcess, 2);
            (void)WaitForSingleObject(ScopedProcess.hProcess, 2000);
            (void)CloseHandle(ScopedProcess.hThread);
            (void)CloseHandle(ScopedProcess.hProcess);
            return false;
        }
        if (ScopedWait == WAIT_OBJECT_0)
        {
            break;
        }
    }
    DWORD ScopedExitCode = 1;
    (void)GetExitCodeProcess(ScopedProcess.hProcess, &ScopedExitCode);
    (void)CloseHandle(ScopedProcess.hThread);
    (void)CloseHandle(ScopedProcess.hProcess);
    PollProgressFile();
    *OutExitCode = static_cast<std::uint32_t>(ScopedExitCode);
    if (ScopedExitCode != 0)
    {
        std::fprintf(stderr, "release: child exited %lu: %s\n", static_cast<unsigned long>(ScopedExitCode),
                     ScopedCommand.data());
    }
    return ScopedExitCode == 0;
#else
    std::string ScopedCommand(InCommandLine);
    ScopedCommand += " >> ";
    ScopedCommand += MConfig.logPath;
    ScopedCommand += " 2>&1";
    const int ScopedStatus = std::system(ScopedCommand.c_str());
    PollProgressFile();
    if (ScopedStatus == -1)
    {
        return false;
    }
    *OutExitCode = static_cast<std::uint32_t>(WEXITSTATUS(ScopedStatus));
    return *OutExitCode == 0;
#endif
}

bool ReleaseCooker::StageStoreArchives(const char* InPluginPath) noexcept
{
    if (MConfig.storesRoot[0] == '\0')
    {
        // Hand-built configs (and the headless key test) may not own a project
        return true;
    }
    const std::filesystem::path ScopedArchive(MConfig.archivePath);
    const std::filesystem::path ScopedPlugin(InPluginPath != nullptr ? InPluginPath : "");
    std::error_code ScopedError;
    for (const std::string_view ScopedStore : {"steam", "epic", "gog"})
    {
        const std::filesystem::path ScopedDirectory = std::filesystem::path(MConfig.storesRoot) / ScopedStore;
        std::filesystem::create_directories(ScopedDirectory, ScopedError);
        if (ScopedError)
        {
            return false;
        }
        std::filesystem::copy_file(ScopedArchive, ScopedDirectory / ScopedArchive.filename(),
                                   std::filesystem::copy_options::overwrite_existing, ScopedError);
        if (ScopedError)
        {
            return false;
        }
        if (!ScopedPlugin.empty())
        {
            std::filesystem::copy_file(ScopedPlugin, ScopedDirectory / ScopedPlugin.filename(),
                                       std::filesystem::copy_options::overwrite_existing, ScopedError);
            if (ScopedError)
            {
                return false;
            }
        }
        std::string ScopedManifestError;
        if (!WriteStoreManifest(ScopedDirectory, ScopedStore, ScopedArchive, ScopedPlugin, MConfig.projectName,
                                MConfig.cipher, MKeyBaked.load(std::memory_order_acquire), &ScopedManifestError))
        {
            return false;
        }

        // Attribution travels with every store folder: the same document the
        // engine exposes through Fby::Notices, plus the MPL source offer.
        if (!WriteStoreNotices(ScopedDirectory))
        {
            return false;
        }
    }
    return true;
}

bool WriteStoreNotices(const std::filesystem::path& InStoreDirectory, std::string* InError)
{
    std::error_code ScopedError;
    std::filesystem::create_directories(InStoreDirectory, ScopedError);
    if (ScopedError)
    {
        if (InError != nullptr)
        {
            *InError = "store notices: cannot create the store directory";
        }
        return false;
    }

    const std::string_view ScopedNoticesText = Notices::AllText();
    std::ofstream ScopedNotices(InStoreDirectory / "THIRD_PARTY_NOTICES.md", std::ios::binary | std::ios::trunc);
    ScopedNotices.write(ScopedNoticesText.data(), static_cast<std::streamsize>(ScopedNoticesText.size()));
    std::ofstream ScopedOffer(InStoreDirectory / "SOURCE_OFFER.txt", std::ios::binary | std::ios::trunc);
    ScopedOffer << "FemboyEngine2D is distributed under the Mozilla Public License 2.0.\n"
                   "The complete source of every MPL-covered file accompanies each release.\n"
                   "See the project repository published with this build.\n";
    if (!ScopedNotices || !ScopedOffer)
    {
        if (InError != nullptr)
        {
            *InError = "store notices: cannot write the attribution files";
        }
        return false;
    }
    return true;
}

bool WriteStoreManifest(const std::filesystem::path& InStoreDirectory, std::string_view InStoreName,
                        const std::filesystem::path& InArchivePath, const std::filesystem::path& InPluginPath,
                        std::string_view InProjectName, std::uint32_t InCipher, bool InKeyBaked,
                        std::string* InError)
{
    if (InArchivePath.empty())
    {
        if (InError != nullptr)
        {
            *InError = "store manifest: archive path is empty";
        }
        return false;
    }
    std::error_code ScopedError;
    std::filesystem::create_directories(InStoreDirectory, ScopedError);
    if (ScopedError)
    {
        if (InError != nullptr)
        {
            *InError = "store manifest: cannot create the store directory";
        }
        return false;
    }
    const std::string ScopedArchiveName = InArchivePath.filename().generic_string();
    const std::string ScopedPluginName = InPluginPath.empty() ? std::string{} : InPluginPath.filename().generic_string();

    std::ofstream ScopedManifest(InStoreDirectory / "store.txt", std::ios::binary | std::ios::trunc);
    if (!ScopedManifest)
    {
        if (InError != nullptr)
        {
            *InError = "store manifest: cannot write store.txt";
        }
        return false;
    }
    ScopedManifest << "store: " << InStoreName << '\n'
                   << "project: " << InProjectName << '\n'
                   << "archive: " << ScopedArchiveName << '\n'
                   << "plugin: " << (ScopedPluginName.empty() ? "(none)" : ScopedPluginName) << '\n'
                   << "cipher: " << InCipher << '\n'
                   << "key_baked: " << (InKeyBaked ? "true" : "false") << '\n';
    if (!ScopedManifest)
    {
        if (InError != nullptr)
        {
            *InError = "store manifest: store.txt write failed";
        }
        return false;
    }
    ScopedManifest.close();

    // Per-store manifest with the exact artifact hashes. It is the record an
    // upload script verifies before accepting a build; signing stays explicit,
    // so an unsigned folder can never look signed.
    std::uint64_t ScopedArchiveHash = 0;
    std::uint64_t ScopedArchiveBytes = 0;
    std::uint64_t ScopedPluginHash = 0;
    std::uint64_t ScopedPluginBytes = 0;
    if (!HashFile(InArchivePath, &ScopedArchiveHash, &ScopedArchiveBytes))
    {
        if (InError != nullptr)
        {
            *InError = "store manifest: archive is missing or unreadable";
        }
        return false;
    }
    if (!InPluginPath.empty() && !HashFile(InPluginPath, &ScopedPluginHash, &ScopedPluginBytes))
    {
        if (InError != nullptr)
        {
            *InError = "store manifest: plugin is missing or unreadable";
        }
        return false;
    }
    std::ofstream ScopedStoreManifest(InStoreDirectory / "store_manifest.json", std::ios::binary | std::ios::trunc);
    if (!ScopedStoreManifest)
    {
        if (InError != nullptr)
        {
            *InError = "store manifest: cannot write store_manifest.json";
        }
        return false;
    }
    ScopedStoreManifest << "{\n"
                        << "  \"schema\": 1,\n"
                        << "  \"store\": \"" << InStoreName << "\",\n"
                        << "  \"project\": \"" << InProjectName << "\",\n"
                        << "  \"archive\": {\"file\": \"" << ScopedArchiveName << "\", \"bytes\": "
                        << ScopedArchiveBytes << ", \"fnv1a64\": \"" << HashHex(ScopedArchiveHash) << "\"},\n"
                        << "  \"plugin\": {\"file\": \"" << ScopedPluginName << "\", \"bytes\": "
                        << ScopedPluginBytes << ", \"fnv1a64\": \"" << HashHex(ScopedPluginHash) << "\"},\n"
                        << "  \"cipher\": " << InCipher << ",\n"
                        << "  \"key_baked\": " << (InKeyBaked ? "true" : "false") << ",\n"
                        << "  \"signing\": \"unsigned\"\n"
                        << "}\n";
    if (!ScopedStoreManifest)
    {
        if (InError != nullptr)
        {
            *InError = "store manifest: store_manifest.json write failed";
        }
        return false;
    }
    ScopedStoreManifest.close();

    // Steam takes a depot build script rather than a JSON descriptor.
    if (InStoreName == "steam")
    {
        std::ofstream ScopedDepot(InStoreDirectory / "depot_build.vdf", std::ios::binary | std::ios::trunc);
        if (!ScopedDepot)
        {
            if (InError != nullptr)
            {
                *InError = "store manifest: cannot write depot_build.vdf";
            }
            return false;
        }
        ScopedDepot << "\"DepotBuildConfig\"\n"
                       "{\n"
                       "    \"DepotID\" \"0\"\n"
                       "    \"ContentRoot\" \".\"\n"
                       "    \"FileMapping\"\n"
                       "    {\n"
                       "        \"LocalPath\" \"*\"\n"
                       "        \"DepotPath\" \".\"\n"
                       "        \"recursive\" \"1\"\n"
                       "    }\n"
                       "    \"FileExclusion\" \"depot_build.vdf\"\n"
                       "}\n";
        if (!ScopedDepot)
        {
            if (InError != nullptr)
            {
                *InError = "store manifest: depot_build.vdf write failed";
            }
            return false;
        }
    }
    return true;
}

void ReleaseCooker::Run(std::stop_token InStop) noexcept
{
    auto ScopedCancelled = [this, &InStop]() noexcept
    {
        if (InStop.stop_requested() || MStopRequested.load(std::memory_order_acquire))
        {
            SetStage(ReleaseCookStage::Cancelled, "cancelled");
            return true;
        }
        return false;
    };

    try
    {
        if (MConfig.Encrypts() || MConfig.compileAot)
        {
            SetStage(ReleaseCookStage::PreparingKey, "generating project-local release key");
            if (!PrepareKey())
            {
                Fail("cannot prepare the project-local release key");
                MRunning.store(false, std::memory_order_release);
                WipeKey();
                return;
            }
        }
        else
        {
            SetStage(ReleaseCookStage::PreparingKey, "key baking skipped: unencrypted build");
        }
        if (ScopedCancelled())
        {
            MRunning.store(false, std::memory_order_release);
            WipeKey();
            return;
        }

        char ScopedPluginPath[kReleasePathBytes] = {};
        if (MConfig.compileAot && MHasGraph)
        {
            SetStage(ReleaseCookStage::Aot, "transpiling LogicGraph and compiling native plugin");
            Script::AotBuildOptions ScopedOptions;
            ScopedOptions.outputDirectory = MConfig.aotOutputPath;
            ScopedOptions.includeDirectory = MConfig.engineSourcePath;
            ScopedOptions.compiler = MConfig.compiler;
            ScopedOptions.moduleName = MConfig.moduleName;
            ScopedOptions.keepSource = MConfig.keepAotSource;
            ScopedOptions.forceIncludeHeader = MKeyHeader.data();
            const std::string_view ScopedCompiler(MConfig.compiler);
            const bool ScopedMsvcStyle = ScopedCompiler.find("clang-cl") != std::string_view::npos ||
                                         ScopedCompiler.find("cl.exe") != std::string_view::npos;
            std::string ScopedExtraFlags;
            if (MConfig.profile.linkTimeOptimization)
            {
                ScopedExtraFlags += ScopedMsvcStyle ? " /GL" : " -flto";
            }
            if (MConfig.profile.nativeArch)
            {
                ScopedExtraFlags += ScopedMsvcStyle ? " /arch:AVX2" : " -march=native";
            }
            if (Fby::Build::RelaxesFloatingPoint(MConfig.profile.optimization))
            {
                ScopedExtraFlags += ScopedMsvcStyle ? " /fp:fast" : " -ffast-math";
            }
            ScopedOptions.extraFlags = ScopedExtraFlags;
            std::string ScopedError;
            if (!MAot.Start(MGraph, ScopedOptions, &ScopedError))
            {
                Fail(ScopedError.empty() ? "AOT build rejected" : ScopedError.c_str());
                MRunning.store(false, std::memory_order_release);
                WipeKey();
                return;
            }
            while (MAot.Busy())
            {
                if (ScopedCancelled())
                {
                    MAot.Cancel();
                    MRunning.store(false, std::memory_order_release);
                    WipeKey();
                    return;
                }
                Core::SleepMilliseconds(20.0);
            }
            const Script::AotBuildResult ScopedResult = MAot.Snapshot();
            if (ScopedResult.state != Script::AotBuildState::Succeeded)
            {
                Fail(ScopedResult.error.empty() ? "AOT compilation failed" : ScopedResult.error.c_str());
                MRunning.store(false, std::memory_order_release);
                WipeKey();
                return;
            }
            CopyText(ScopedPluginPath, sizeof(ScopedPluginPath), ScopedResult.libraryPath.generic_string());
            MKeyBaked.store(true, std::memory_order_release);
        }
        else
        {
            SetStage(ReleaseCookStage::Aot, "AOT skipped: no LogicGraph bound");
        }

        if (ScopedCancelled())
        {
            MRunning.store(false, std::memory_order_release);
            WipeKey();
            return;
        }

        char ScopedCommand[kCommandBytes] = {};
        std::snprintf(ScopedCommand, sizeof(ScopedCommand), "\"%s\" cook --root \"%s\" --out \"%s\" --progress \"%s\"",
                      MConfig.packerPath, MConfig.assetsRoot, MConfig.packPath, MConfig.progressPath);
        std::uint32_t ScopedExitCode = 1;
        if (!RunChild(ScopedCommand, ReleaseCookStage::Cooking, MConfig.assetsRoot, &ScopedExitCode))
        {
            if (ScopedCancelled())
            {
                MRunning.store(false, std::memory_order_release);
                WipeKey();
                return;
            }
            Fail("fby_fempak cook failed (see release.log)");
            MRunning.store(false, std::memory_order_release);
            WipeKey();
            return;
        }

        if (ScopedCancelled())
        {
            MRunning.store(false, std::memory_order_release);
            WipeKey();
            return;
        }

        if (MConfig.Encrypts())
        {
            const char* ScopedCipher = MConfig.cipher == 2 ? "aes" : "chacha";
            std::snprintf(ScopedCommand, sizeof(ScopedCommand),
                          "\"%s\" secure --in \"%s\" --out \"%s\" --entry game/assets.fempak --codec auto "
                          "--cipher %s --key-file \"%s\" --progress \"%s\"",
                          MConfig.packerPath, MConfig.packPath, MConfig.archivePath, ScopedCipher, MKeyFile.data(),
                          MConfig.progressPath);
            if (!RunChild(ScopedCommand, ReleaseCookStage::Compressing, MConfig.archivePath, &ScopedExitCode))
            {
                if (ScopedCancelled())
                {
                    MRunning.store(false, std::memory_order_release);
                    WipeKey();
                    return;
                }
                Fail("fby_fempak secure failed (see release.log)");
                MRunning.store(false, std::memory_order_release);
                WipeKey();
                return;
            }
        }
        else
        {
            std::error_code ScopedError;
            std::filesystem::copy_file(MConfig.packPath, MConfig.archivePath,
                                       std::filesystem::copy_options::overwrite_existing, ScopedError);
            if (ScopedError)
            {
                Fail("cannot copy the unencrypted archive");
                MRunning.store(false, std::memory_order_release);
                WipeKey();
                return;
            }
        }

        // A release is not complete until every store directory contains the
        if (!StageStoreArchives(ScopedPluginPath))
        {
            Fail("cannot stage the per-store archives");
            MRunning.store(false, std::memory_order_release);
            WipeKey();
            return;
        }

        std::error_code ScopedSizeError;
        const auto ScopedArchiveSize = std::filesystem::file_size(MConfig.archivePath, ScopedSizeError);
        const auto ScopedPackSize = std::filesystem::file_size(MConfig.packPath, ScopedSizeError);
        {
            std::lock_guard ScopedLock(MProgressMutex);
            MProgress.stage = ReleaseCookStage::Succeeded;
            MProgress.running = false;
            MProgress.failed = false;
            MProgress.encrypted = MConfig.Encrypts();
            MProgress.cipher = MConfig.cipher;
            CopyText(MProgress.asset, sizeof(MProgress.asset), MConfig.archivePath);
            CopyText(MProgress.message, sizeof(MProgress.message), "Build & Pack Game complete");
            MResult.success = true;
            MResult.keyBaked = MKeyBaked.load(std::memory_order_acquire);
            MResult.encrypted = MConfig.Encrypts();
            MResult.packedBytes = ScopedSizeError ? 0 : static_cast<std::uint64_t>(ScopedPackSize);
            MResult.archiveBytes = ScopedSizeError ? 0 : static_cast<std::uint64_t>(ScopedArchiveSize);
            CopyText(MResult.archivePath, sizeof(MResult.archivePath), MConfig.archivePath);
            CopyText(MResult.pluginPath, sizeof(MResult.pluginPath), ScopedPluginPath);
            CopyText(MResult.message, sizeof(MResult.message), "Build & Pack Game complete");
        }
        WipeKey();
    }
    catch (...)
    {
        Fail("release pipeline raised a build-time exception");
        WipeKey();
    }
    MRunning.store(false, std::memory_order_release);
}
}
