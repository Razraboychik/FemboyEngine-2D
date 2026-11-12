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
#include <chrono>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>

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

std::size_t NormalizeInto(char* OutPath, std::size_t InCapacity, std::string_view InPath) noexcept
{
    if (OutPath == nullptr || InCapacity == 0)
    {
        return 0;
    }
    std::size_t ScopedRead = 0;
    while (ScopedRead + 1 < InPath.size() && InPath[ScopedRead] == '.' && InPath[ScopedRead + 1] == '/')
    {
        ScopedRead += 2;
    }
    while (ScopedRead < InPath.size() && InPath[ScopedRead] == '/')
    {
        ++ScopedRead;
    }
    std::size_t ScopedWritten = 0;
    for (; ScopedRead < InPath.size() && ScopedWritten + 1 < InCapacity; ++ScopedRead)
    {
        char ScopedCharacter = InPath[ScopedRead];
        if (ScopedCharacter == '\\')
        {
            ScopedCharacter = '/';
        }
        if (ScopedCharacter == '\0')
        {
            continue;
        }
        if (ScopedCharacter == '/' && ScopedWritten > 0 && OutPath[ScopedWritten - 1] == '/')
        {
            continue;
        }
        OutPath[ScopedWritten++] = ScopedCharacter;
    }
    OutPath[ScopedWritten] = '\0';
    return ScopedWritten;
}

void SetError(std::string* InError, std::string_view InText)
{
    if (InError != nullptr)
    {
        *InError = InText;
    }
}

constexpr char LowerAscii(char InCharacter) noexcept
{
    return InCharacter >= 'A' && InCharacter <= 'Z' ? static_cast<char>(InCharacter + ('a' - 'A')) : InCharacter;
}

[[nodiscard]] bool StartsWithNoCase(std::string_view InText, std::string_view InPrefix) noexcept
{
    if (InText.size() < InPrefix.size())
    {
        return false;
    }
    for (std::size_t ScopedI = 0; ScopedI < InPrefix.size(); ++ScopedI)
    {
        if (LowerAscii(InText[ScopedI]) != LowerAscii(InPrefix[ScopedI]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool EndsWithNoCase(std::string_view InText, std::string_view InSuffix) noexcept
{
    if (InText.size() < InSuffix.size())
    {
        return false;
    }
    const std::size_t ScopedOffset = InText.size() - InSuffix.size();
    for (std::size_t ScopedI = 0; ScopedI < InSuffix.size(); ++ScopedI)
    {
        if (LowerAscii(InText[ScopedOffset + ScopedI]) != LowerAscii(InSuffix[ScopedI]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ContainsNoCase(std::string_view InText, std::string_view InNeedle) noexcept
{
    if (InNeedle.empty())
    {
        return true;
    }
    if (InText.size() < InNeedle.size())
    {
        return false;
    }
    for (std::size_t ScopedOffset = 0; ScopedOffset + InNeedle.size() <= InText.size(); ++ScopedOffset)
    {
        bool ScopedMatch = true;
        for (std::size_t ScopedI = 0; ScopedI < InNeedle.size(); ++ScopedI)
        {
            if (LowerAscii(InText[ScopedOffset + ScopedI]) != LowerAscii(InNeedle[ScopedI]))
            {
                ScopedMatch = false;
                break;
            }
        }
        if (ScopedMatch)
        {
            return true;
        }
    }
    return false;
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
    // Cooked payloads are outputs, never sources. Excluding them here is what
    const bool ScopedToml = EndsWithNoCase(InPath, ".toml");
    const bool ScopedSceneToml =
        ScopedToml && (StartsWithNoCase(InPath, "scenes/") || ContainsNoCase(InPath, "/scenes/") ||
                       StartsWithNoCase(InPath, "prefabs/") || ContainsNoCase(InPath, "/prefabs/"));
    const bool ScopedAtlasToml =
        ScopedToml && (StartsWithNoCase(InPath, "atlas/") || ContainsNoCase(InPath, "/atlas/"));
    return EndsWithNoCase(InPath, ".png") || EndsWithNoCase(InPath, ".tga") || EndsWithNoCase(InPath, ".wav") ||
           EndsWithNoCase(InPath, ".ogg") || EndsWithNoCase(InPath, ".opus") || EndsWithNoCase(InPath, ".webm") ||
           ScopedSceneToml || ScopedAtlasToml || EndsWithNoCase(InPath, ".feskl") ||
           EndsWithNoCase(InPath, ".fanim") || EndsWithNoCase(InPath, ".fmat") ||
           EndsWithNoCase(InPath, ".fscript");
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
    return StartOsWatcher(InError);
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
        // Ring full: a hot-reload burst drops the newest change; the counter is
        // part of the public diagnostics so the loss is visible.
        ++MStats.droppedChanges;
        return false;
    }
    Change& ScopedChange = MPending[MPendingHead];
    std::memset(ScopedChange.path, 0, sizeof(ScopedChange.path));
    NormalizeInto(ScopedChange.path, sizeof(ScopedChange.path), InRelativePath);
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
    // A .femeta written by this system triggers a change notification.
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
                    (void)CancelIoEx(ScopedDirectory, &ScopedOverlapped);
                    DWORD ScopedCancelledBytes = 0;
                    if (!GetOverlappedResult(ScopedDirectory, &ScopedOverlapped, &ScopedCancelledBytes, TRUE) &&
                        GetLastError() != ERROR_OPERATION_ABORTED)
                    {
                        break;
                    }
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
bool AssetDatabaseWatchSystem::StartOsWatcher(std::string* InError)
{
    std::error_code ScopedError;
    if (!std::filesystem::is_directory(MConfig.projectRoot, ScopedError))
    {
        SetError(InError, "asset watch root is not a directory");
        return false;
    }
    MOsWatcherActive = true;
    MStopRequested.store(false, std::memory_order_relaxed);
    const auto ScopedDelay =
        std::chrono::milliseconds(std::max<std::uint32_t>(MConfig.debounceMs, 100u));
    MThread = std::jthread(
        [this, ScopedDelay](std::stop_token)
        {
            using FileStamp = std::pair<std::filesystem::file_time_type, std::uintmax_t>;
            std::unordered_map<std::string, FileStamp> ScopedKnown;
            bool ScopedPrimed = false;
            while (!MStopRequested.load(std::memory_order_relaxed))
            {
                std::unordered_map<std::string, FileStamp> ScopedCurrent;
                std::error_code ScopedScanError;
                auto ScopedVisit = [&](const std::filesystem::directory_entry& InEntry)
                {
                    if (ScopedScanError || !InEntry.is_regular_file(ScopedScanError))
                    {
                        return;
                    }
                    const std::string ScopedRelative = Normalize(
                        InEntry.path().lexically_relative(MConfig.projectRoot).generic_string());
                    if (!IsRelevantFile(ScopedRelative))
                    {
                        return;
                    }
                    const auto ScopedWriteTime = InEntry.last_write_time(ScopedScanError);
                    if (ScopedScanError)
                    {
                        return;
                    }
                    ScopedCurrent.emplace(ScopedRelative, FileStamp{ScopedWriteTime, InEntry.file_size(ScopedScanError)});
                };
                if (MConfig.recursive)
                {
                    for (const auto& ScopedEntry : std::filesystem::recursive_directory_iterator(
                             MConfig.projectRoot, std::filesystem::directory_options::skip_permission_denied,
                             ScopedScanError))
                    {
                        if (MStopRequested.load(std::memory_order_relaxed))
                        {
                            break;
                        }
                        ScopedVisit(ScopedEntry);
                    }
                }
                else
                {
                    for (const auto& ScopedEntry : std::filesystem::directory_iterator(
                             MConfig.projectRoot, std::filesystem::directory_options::skip_permission_denied,
                             ScopedScanError))
                    {
                        if (MStopRequested.load(std::memory_order_relaxed))
                        {
                            break;
                        }
                        ScopedVisit(ScopedEntry);
                    }
                }
                if (ScopedPrimed)
                {
                    for (const auto& [ScopedPath, ScopedStamp] : ScopedCurrent)
                    {
                        const auto ScopedPrevious = ScopedKnown.find(ScopedPath);
                        if (ScopedPrevious == ScopedKnown.end() ||
                            ScopedPrevious->second.first != ScopedStamp.first ||
                            ScopedPrevious->second.second != ScopedStamp.second)
                        {
                            (void)PushChange(ScopedPath);
                        }
                    }
                    for (const auto& [ScopedPath, ScopedStamp] : ScopedKnown)
                    {
                        (void)ScopedStamp;
                        if (ScopedCurrent.find(ScopedPath) == ScopedCurrent.end())
                        {
                            (void)PushChange(ScopedPath);
                        }
                    }
                }
                ScopedKnown = std::move(ScopedCurrent);
                ScopedPrimed = true;
                std::this_thread::sleep_for(ScopedDelay);
            }
        });
    LogInfo("watch", "polling OS watcher active for '{}'", MConfig.projectRoot.string());
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
