// FemboyEngine2D - AssetDatabaseWatchSystem implementation.
#include "Editor/Infrastructure/AssetDatabaseWatchSystem.hpp"

#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Assets/PakFormat.hpp"
#include "Engine/Core/Log.hpp"
#include "Editor/Infrastructure/AssetImport.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Fby::Editor
{
namespace
{
constexpr std::uint32_t kChangeBufferBytes = 8192;

[[nodiscard]] std::string Normalize(std::string_view InPath)
{
    return Assets::NormalizePakPath(InPath);
}

void SetError(std::string* InError, std::string_view InText)
{
    if (InError != nullptr)
    {
        *InError = InText;
    }
}

// Recursively schedules the assets that recorded a dependency on InGuid. The
// depth cap stops A<->B cycles; ScheduleCook de-duplicates a pending path, so a
// diamond graph costs one cook per asset.
void ScheduleDependentCooks(Assets::AssetDatabase& InDatabase,
                            const AssetDatabaseWatchSystem::Callbacks& InCallbacks,
                            AssetDatabaseWatchSystem::Stats& InStats, Assets::AssetGuid InGuid,
                            std::uint32_t InDepth)
{
    if (InDepth >= 8 || InCallbacks.scheduleCook == nullptr)
    {
        return;
    }
    std::array<Assets::AssetHandle, 64> ScopedDependents{};
    const std::uint32_t ScopedCount = InDatabase.CollectDependents(InGuid, ScopedDependents);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount && ScopedI < ScopedDependents.size(); ++ScopedI)
    {
        const Assets::AssetMeta* ScopedMeta = InDatabase.Meta(ScopedDependents[ScopedI]);
        if (ScopedMeta == nullptr)
        {
            continue;
        }
        ++InStats.cookRequests;
        std::string ScopedError;
        if (!InCallbacks.scheduleCook(InCallbacks.user, ScopedMeta->sourcePath, ScopedDependents[ScopedI],
                                      &ScopedError))
        {
            ++InStats.failures;
            LogWarn("watch", "cannot enqueue dependent cook for '{}': {}", ScopedMeta->sourcePath, ScopedError);
            continue;
        }
        ScheduleDependentCooks(InDatabase, InCallbacks, InStats, ScopedMeta->guid, InDepth + 1u);
    }
}
}

AssetDatabaseWatchSystem::~AssetDatabaseWatchSystem()
{
    Shutdown();
}

bool AssetDatabaseWatchSystem::IsRelevantFile(std::string_view InPath) noexcept
{
    return IsRelevantSourceFile(InPath) || Assets::IsAssetMetaPath(InPath);
}

bool AssetDatabaseWatchSystem::IsRelevantSourceFile(std::string_view InPath) noexcept
{
    const auto ScopedEndsWith = [InPath](std::string_view InSuffix)
    { return InPath.size() >= InSuffix.size() && InPath.substr(InPath.size() - InSuffix.size()) == InSuffix; };
    // Cooked payloads are outputs, never sources. Excluding them here is what
    const bool ScopedToml = ScopedEndsWith(".toml");
    const bool ScopedSceneToml =
        ScopedToml && (InPath.starts_with("scenes/") || InPath.find("/scenes/") != std::string_view::npos ||
                       InPath.starts_with("prefabs/") || InPath.find("/prefabs/") != std::string_view::npos);
    const bool ScopedAtlasToml =
        ScopedToml && (InPath.starts_with("atlas/") || InPath.find("/atlas/") != std::string_view::npos);
    return ScopedEndsWith(".png") || ScopedEndsWith(".tga") || ScopedEndsWith(".wav") || ScopedEndsWith(".ogg") ||
           ScopedEndsWith(".opus") || ScopedEndsWith(".webm") || ScopedSceneToml || ScopedAtlasToml ||
           ScopedEndsWith(".feskl") || ScopedEndsWith(".fanim") || ScopedEndsWith(".fmat") ||
           ScopedEndsWith(".fscript");
}

bool AssetDatabaseWatchSystem::Initialize(Assets::AssetDatabase& InDatabase, const Config& InConfig,
                                          Callbacks InCallbacks, std::string* InError)
{
    Shutdown();
    if (InConfig.projectRoot.empty())
    {
        SetError(InError, "watch system needs a project root");
        return false;
    }
    MDatabase = &InDatabase;
    MConfig = InConfig;
    MCallbacks = InCallbacks;
    MPendingHead = MPendingTail = 0;
    MWatchedCount = 0;
    MStats = {};
    MActive = true;
#ifndef _WIN32
    MOsWatcherActive = false;
    return true;
#else
    return StartOsWatcher(InError);
#endif
}

void AssetDatabaseWatchSystem::Shutdown()
{
    StopOsWatcher();
    MDatabase = nullptr;
    MActive = false;
    MPendingHead = MPendingTail = 0;
    MWatchedCount = 0;
}

bool AssetDatabaseWatchSystem::Watch(std::string_view InRelativeDirectory, std::string* InError)
{
    if (MWatchedCount >= kMaxWatchedPaths)
    {
        SetError(InError, "watch path table is full");
        return false;
    }
    const std::string ScopedNormalized = Normalize(InRelativeDirectory);
    for (std::uint32_t ScopedI = 0; ScopedI < MWatchedCount; ++ScopedI)
    {
        if (ScopedNormalized == MWatched[ScopedI].data())
        {
            return true;
        }
    }
    if (ScopedNormalized.size() >= kWatchPathCapacity)
    {
        SetError(InError, "watch path is too long");
        return false;
    }
    std::memcpy(MWatched[MWatchedCount].data(), ScopedNormalized.data(), ScopedNormalized.size());
    MWatched[MWatchedCount][ScopedNormalized.size()] = '\0';
    ++MWatchedCount;
    return true;
}

std::uint32_t AssetDatabaseWatchSystem::ScanForNewSources(std::string* InError)
{
    if (MDatabase == nullptr || !MActive)
    {
        SetError(InError, "watch system is not initialized");
        return 0;
    }
    std::error_code ScopedCode;
    if (!std::filesystem::is_directory(MConfig.projectRoot, ScopedCode))
    {
        SetError(InError, "asset root does not exist: " + MConfig.projectRoot.string());
        return 0;
    }
    std::uint32_t ScopedImported = 0;
    for (const std::filesystem::directory_entry& ScopedEntry : std::filesystem::recursive_directory_iterator(
             MConfig.projectRoot, std::filesystem::directory_options::skip_permission_denied, ScopedCode))
    {
        if (ScopedCode || !ScopedEntry.is_regular_file(ScopedCode))
        {
            continue;
        }
        const std::string ScopedRelative =
            Normalize(ScopedEntry.path().lexically_relative(MConfig.projectRoot).generic_string());
        if (!IsRelevantSourceFile(ScopedRelative) || MDatabase->FindHandleBySourcePath(ScopedRelative).IsValid())
        {
            continue;
        }
        std::string ScopedImportError;
        const Assets::AssetHandle ScopedHandle = EnsureImported(ScopedRelative, &ScopedImportError);
        if (!ScopedHandle.IsValid())
        {
            if (!ScopedImportError.empty())
            {
                LogWarn("watch", "startup import failed for '{}': {}", ScopedRelative, ScopedImportError);
            }
            continue;
        }
        ++ScopedImported;
        if (MCallbacks.scheduleCook != nullptr)
        {
            std::string ScopedScheduleError;
            if (!MCallbacks.scheduleCook(MCallbacks.user, ScopedRelative, ScopedHandle, &ScopedScheduleError))
            {
                LogWarn("watch", "startup cook enqueue failed for '{}': {}", ScopedRelative, ScopedScheduleError);
            }
        }
    }
    if (ScopedImported > 0)
    {
        LogInfo("watch", "startup scan imported {} source asset(s)", ScopedImported);
    }
    return ScopedImported;
}

bool AssetDatabaseWatchSystem::PushChange(std::string_view InRelativePath) noexcept
{
    const std::scoped_lock ScopedLock(MPendingMutex);
    return PushChangeLocked(InRelativePath);
}

bool AssetDatabaseWatchSystem::PushChangeLocked(std::string_view InRelativePath) noexcept
{
    const std::uint32_t ScopedNext = (MPendingHead + 1u) % kMaxPendingChanges;
    if (ScopedNext == MPendingTail)
    {
        // Ring full: a hot-reload burst drops the newest change and reports it.
        return false;
    }
    Change& ScopedChange = MPending[MPendingHead];
    const std::string ScopedNormalized = Normalize(InRelativePath);
    const std::size_t ScopedLength = std::min<std::size_t>(ScopedNormalized.size(), kWatchPathCapacity - 1);
    std::memset(ScopedChange.path, 0, sizeof(ScopedChange.path));
    std::memcpy(ScopedChange.path, ScopedNormalized.data(), ScopedLength);
    ScopedChange.sourceHash = 0;
    ScopedChange.sequence = MStats.changesSeen++;
    MPendingHead = ScopedNext;
    return true;
}

bool AssetDatabaseWatchSystem::DrainChange(Change* Out) noexcept
{
    const std::scoped_lock ScopedLock(MPendingMutex);
    if (Out == nullptr || MPendingTail == MPendingHead)
    {
        return false;
    }
    *Out = MPending[MPendingTail];
    MPendingTail = (MPendingTail + 1u) % kMaxPendingChanges;
    return true;
}

bool AssetDatabaseWatchSystem::InjectChange(std::string_view InRelativePath, std::uint64_t InSourceHash) noexcept
{
    const std::scoped_lock ScopedLock(MPendingMutex);
    if (!MActive || !IsRelevantFile(InRelativePath) || !PushChangeLocked(InRelativePath))
    {
        return false;
    }
    MPending[(MPendingHead + kMaxPendingChanges - 1u) % kMaxPendingChanges].sourceHash = InSourceHash;
    return true;
}

Assets::AssetHandle AssetDatabaseWatchSystem::EnsureImported(std::string_view InRelativePath, std::string* InError)
{
    if (MDatabase == nullptr)
    {
        return {};
    }
    const std::string ScopedNormalized = Normalize(InRelativePath);
    if (const Assets::AssetHandle ScopedExisting = MDatabase->FindHandleBySourcePath(ScopedNormalized);
        ScopedExisting.IsValid())
    {
        return ScopedExisting;
    }
    if (!MConfig.autoImport)
    {
        return {};
    }
    const Assets::AssetHandle ScopedImported =
        ImportSourceFile(*MDatabase, MConfig.projectRoot, ScopedNormalized, MConfig.defaultTextureFormat, InError);
    if (ScopedImported.IsValid())
    {
        LogInfo("watch", "imported '{}' and generated {}", ScopedNormalized, MetaFilePathForSource(ScopedNormalized));
    }
    return ScopedImported;
}

void AssetDatabaseWatchSystem::ProcessMetaFile(std::string_view InRelativePath)
{
    if (MDatabase == nullptr)
    {
        return;
    }
    const std::filesystem::path ScopedPhysical =
        MConfig.projectRoot / std::filesystem::path(std::string(InRelativePath));
    std::ifstream ScopedStream(ScopedPhysical, std::ios::binary);
    if (!ScopedStream)
    {
        ++MStats.failures;
        LogWarn("watch", "cannot read metadata '{}'", ScopedPhysical.string());
        return;
    }
    const std::string ScopedText((std::istreambuf_iterator<char>(ScopedStream)), std::istreambuf_iterator<char>());
    Assets::AssetMeta ScopedMeta;
    Assets::AssetDependency ScopedDependencies[Assets::kMaxAssetDependencies]{};
    std::uint32_t ScopedDependencyCount = 0;
    std::string ScopedError;
    if (!Assets::ParseAssetMetaFile(ScopedText, &ScopedMeta, ScopedDependencies, Assets::kMaxAssetDependencies,
                                    &ScopedDependencyCount, &ScopedError))
    {
        ++MStats.failures;
        LogWarn("watch", "invalid metadata '{}': {}", InRelativePath, ScopedError);
        return;
    }
    // A .femeta written by this system triggers a change notification for
    if (MDatabase->FindHandleByGuid(ScopedMeta.guid).IsValid())
    {
        return;
    }
    const Assets::AssetHandle ScopedHandle = MDatabase->RegisterMeta(
        ScopedMeta, std::span<const Assets::AssetDependency>(ScopedDependencies, ScopedDependencyCount), &ScopedError);
    if (!ScopedHandle.IsValid())
    {
        ++MStats.failures;
        LogWarn("watch", "cannot register metadata '{}': {}", InRelativePath, ScopedError);
        return;
    }
    LogInfo("watch", "registered metadata '{}'", InRelativePath);
}

void AssetDatabaseWatchSystem::ProcessChange(const Change& InChange)
{
    if (MDatabase == nullptr)
    {
        return;
    }
    if (Assets::IsAssetMetaPath(InChange.path))
    {
        ProcessMetaFile(InChange.path);
        return;
    }
    std::string ScopedError;
    const Assets::AssetHandle ScopedHandle = EnsureImported(InChange.path, &ScopedError);
    if (!ScopedHandle.IsValid())
    {
        ++MStats.ignored;
        if (!ScopedError.empty())
        {
            LogWarn("watch", "import skipped for '{}': {}", InChange.path, ScopedError);
        }
        return;
    }
    if (MCallbacks.scheduleCook != nullptr)
    {
        ++MStats.cookRequests;
        if (!MCallbacks.scheduleCook(MCallbacks.user, InChange.path, ScopedHandle, &ScopedError))
        {
            ++MStats.failures;
            LogWarn("watch", "cannot enqueue cook for '{}': {}", InChange.path, ScopedError);
        }
        else if (const Assets::AssetMeta* ScopedMeta = MDatabase->Meta(ScopedHandle); ScopedMeta != nullptr)
        {
            // Cascade: an asset built from this source is stale now.
            ScheduleDependentCooks(*MDatabase, MCallbacks, MStats, ScopedMeta->guid, 0);
        }
        return;
    }
    if (MCallbacks.cookAndStage == nullptr)
    {
        ++MStats.failures;
        LogWarn("watch", "source '{}' changed but no cooker callback is installed", InChange.path);
        return;
    }
    Assets::AssetMeta ScopedMeta;
    ++MStats.cookRequests;
    if (!MCallbacks.cookAndStage(MCallbacks.user, InChange.path, &ScopedMeta, &ScopedError))
    {
        ++MStats.failures;
        LogWarn("watch", "cook failed for '{}': {}", InChange.path, ScopedError);
        return;
    }
    // Preserve identity fields; the cooker only supplies content/import data.
    const Assets::AssetMeta* ScopedLive = MDatabase->Meta(ScopedHandle);
    if (ScopedLive == nullptr)
    {
        ++MStats.failures;
        return;
    }
    ScopedMeta.guid = ScopedLive->guid;
    ScopedMeta.id = ScopedLive->id;
    ScopedMeta.kind = ScopedLive->kind;
    if (!ScopedMeta.valid)
    {
        std::memcpy(ScopedMeta.sourcePath, ScopedLive->sourcePath, sizeof(ScopedMeta.sourcePath));
        ScopedMeta.valid = true;
    }
    ScopedMeta.sourceHash = InChange.sourceHash != 0 ? InChange.sourceHash : ScopedMeta.sourceHash;

    if (!MDatabase->BeginTransaction())
    {
        ++MStats.failures;
        return;
    }
    if (!MDatabase->StageMeta(ScopedHandle, ScopedMeta, &ScopedError))
    {
        MDatabase->RollbackTransaction();
        ++MStats.failures;
        LogWarn("watch", "staging '{}' failed: {}", InChange.path, ScopedError);
        return;
    }
    if (!MDatabase->CommitTransaction())
    {
        ++MStats.failures;
        return;
    }
    if (MCallbacks.reload != nullptr)
    {
        const Assets::AssetMeta* ScopedCommitted = MDatabase->Meta(ScopedHandle);
        if (ScopedCommitted != nullptr &&
            MCallbacks.reload(MCallbacks.user, ScopedHandle, *ScopedCommitted, &ScopedError))
        {
            ++MStats.reloads;
            LogInfo("watch", "hot reloaded '{}' (content gen {})", InChange.path, ScopedCommitted->contentGeneration);
        }
        else
        {
            ++MStats.failures;
            LogWarn("watch", "reload failed for '{}': {}", InChange.path, ScopedError);
        }
    }
    else
    {
        ++MStats.reloads;
    }
// Every committed change also appears in the database reload ring; the host
}

std::uint32_t AssetDatabaseWatchSystem::Poll()
{
    if (!MActive)
    {
        return 0;
    }
    std::uint32_t ScopedReloads = 0;
    Change ScopedChange;
    while (DrainChange(&ScopedChange))
    {
        ProcessChange(ScopedChange);
        ++ScopedReloads;
    }
    return ScopedReloads;
}

// OS watcher
#if defined(_WIN32)
namespace
{
[[nodiscard]] std::string WideToUtf8(const wchar_t* InText, int InLength)
{
    if (InText == nullptr || InLength <= 0)
    {
        return {};
    }
    const int ScopedBytes = WideCharToMultiByte(CP_UTF8, 0, InText, InLength, nullptr, 0, nullptr, nullptr);
    if (ScopedBytes <= 0)
    {
        return {};
    }
    std::string ScopedResult(static_cast<std::size_t>(ScopedBytes), '\0');
    (void)WideCharToMultiByte(CP_UTF8, 0, InText, InLength, ScopedResult.data(), ScopedBytes, nullptr, nullptr);
    return ScopedResult;
}
}

bool AssetDatabaseWatchSystem::StartOsWatcher(std::string* InError)
{
    const std::wstring ScopedRoot = MConfig.projectRoot.wstring();
    HANDLE ScopedDirectory =
        CreateFileW(ScopedRoot.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (ScopedDirectory == INVALID_HANDLE_VALUE)
    {
        MOsWatcherActive = false;
        SetError(InError, "ReadDirectoryChangesW: cannot open the project root");
        LogWarn("watch", "OS watcher disabled: cannot open '{}'", MConfig.projectRoot.string());
        return false;
    }
    MDirectoryHandle = ScopedDirectory;
    MOsWatcherActive = true;
    MStopRequested.store(false, std::memory_order_relaxed);
    const bool ScopedRecursive = MConfig.recursive;
    MThread = std::jthread(
        [this, ScopedDirectory, ScopedRecursive](std::stop_token InStopToken)
        {
            (void)InStopToken;
            OVERLAPPED ScopedOverlapped{};
            ScopedOverlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (ScopedOverlapped.hEvent == nullptr)
            {
                MOsWatcherActive.store(false, std::memory_order_relaxed);
                return;
            }
            std::array<std::byte, kChangeBufferBytes> ScopedBuffer{};
            while (!MStopRequested.load(std::memory_order_relaxed))
            {
                ResetEvent(ScopedOverlapped.hEvent);
                // With an overlapped request lpBytesReturned is undefined; the
                // byte count must come from GetOverlappedResult after the wait.
                const BOOL ScopedIssued = ReadDirectoryChangesW(
                    ScopedDirectory, ScopedBuffer.data(), static_cast<DWORD>(ScopedBuffer.size()),
                    ScopedRecursive ? TRUE : FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                    nullptr, &ScopedOverlapped, nullptr);
                if (!ScopedIssued)
                {
                    break;
                }
                const DWORD ScopedWait = WaitForSingleObject(ScopedOverlapped.hEvent, 100);
                if (ScopedWait == WAIT_TIMEOUT)
                {
                    CancelIoEx(ScopedDirectory, &ScopedOverlapped);
                    WaitForSingleObject(ScopedOverlapped.hEvent, 100);
                    continue;
                }
                if (ScopedWait != WAIT_OBJECT_0)
                {
                    break;
                }
                DWORD ScopedBytes = 0;
                if (!GetOverlappedResult(ScopedDirectory, &ScopedOverlapped, &ScopedBytes, FALSE) || ScopedBytes == 0)
                {
                    continue;
                }
                const auto* ScopedInfo = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(ScopedBuffer.data());
                for (;;)
                {
                    const std::string ScopedRelative = WideToUtf8(
                        ScopedInfo->FileName, static_cast<int>(ScopedInfo->FileNameLength / sizeof(wchar_t)));
                    if (!ScopedRelative.empty() && IsRelevantFile(ScopedRelative))
                    {
                        (void)PushChange(ScopedRelative);
                    }
                    if (ScopedInfo->NextEntryOffset == 0)
                    {
                        break;
                    }
                    ScopedInfo = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                        reinterpret_cast<const std::byte*>(ScopedInfo) + ScopedInfo->NextEntryOffset);
                }
            }
            CloseHandle(ScopedOverlapped.hEvent);
        });
    return true;
}
#else
bool AssetDatabaseWatchSystem::StartOsWatcher(std::string*)
{
    MOsWatcherActive = false;
    LogWarn("watch", "OS directory watches are not implemented on this platform; use InjectChange or Poll");
    return true;
}
#endif

void AssetDatabaseWatchSystem::StopOsWatcher() noexcept
{
    if (!MOsWatcherActive && !MThread.joinable())
    {
        return;
    }
    MStopRequested.store(true, std::memory_order_relaxed);
#if defined(_WIN32)
    if (MDirectoryHandle != nullptr)
    {
        CancelIoEx(static_cast<HANDLE>(MDirectoryHandle), nullptr);
    }
#endif
    if (MThread.joinable())
    {
        MThread.join();
    }
#if defined(_WIN32)
    if (MDirectoryHandle != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(MDirectoryHandle));
        MDirectoryHandle = nullptr;
    }
#endif
    MOsWatcherActive = false;
}
}
