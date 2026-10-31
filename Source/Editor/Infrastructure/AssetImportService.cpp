// FemboyEngine2D - background asset import/cook/reload implementation.
#include "Editor/Infrastructure/AssetImportService.hpp"

#include "Engine/Assets/PakFormat.hpp"
#include "Engine/Assets/TextureArray.hpp"
#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/Assets/TextureCook.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"
#include "Editor/Infrastructure/AssetImport.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Fby::Editor
{
namespace
{
// Finished cooks committed per Tick. Bounded so a batch of imports cannot
// stall one editor frame; the remaining results drain on later ticks.
constexpr std::uint32_t kMaxCommitsPerTick = 16;

void SetError(std::string* InError, std::string InText)
{
    if (InError != nullptr)
    {
        *InError = std::move(InText);
    }
}

void CopyFixed(char* InDestination, std::size_t InCapacity, std::string_view InValue) noexcept
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min<std::size_t>(InValue.size(), InCapacity - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(InDestination, InValue.data(), ScopedLength);
    }
    InDestination[ScopedLength] = '\0';
}

[[nodiscard]] std::uint64_t HashBytes(std::span<const std::byte> InBytes) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (const std::byte ScopedByte : InBytes)
    {
        ScopedHash ^= static_cast<std::uint8_t>(ScopedByte);
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

[[nodiscard]] std::string ReplaceExtension(std::string_view InPath, std::string_view InExtension)
{
    const std::size_t ScopedSlash = InPath.find_last_of('/');
    const std::size_t ScopedDot = InPath.find_last_of('.');
    if (ScopedDot == std::string_view::npos || (ScopedSlash != std::string_view::npos && ScopedDot < ScopedSlash))
    {
        return std::string(InPath) + std::string(InExtension);
    }
    return std::string(InPath.substr(0, ScopedDot)) + std::string(InExtension);
}

[[nodiscard]] bool IsAstc(Assets::TextureFormat InFormat) noexcept
{
    return InFormat == Assets::TextureFormat::Astc4x4Unorm || InFormat == Assets::TextureFormat::Astc4x4Srgb ||
           InFormat == Assets::TextureFormat::Astc6x6Unorm || InFormat == Assets::TextureFormat::Astc6x6Srgb;
}

[[nodiscard]] bool WriteBytesAtomically(const std::filesystem::path& InPath, std::span<const std::byte> InBytes,
                                        std::string* InError)
{
    std::error_code ScopedCode;
    if (InPath.has_parent_path())
    {
        std::filesystem::create_directories(InPath.parent_path(), ScopedCode);
        if (ScopedCode)
        {
            SetError(InError, "cannot create " + InPath.parent_path().string() + ": " + ScopedCode.message());
            return false;
        }
    }
    std::filesystem::path ScopedTemporary = InPath;
    ScopedTemporary += ".fbtmp";
    {
        std::ofstream ScopedStream(ScopedTemporary, std::ios::binary | std::ios::trunc);
        if (!ScopedStream)
        {
            SetError(InError, "cannot open " + ScopedTemporary.string());
            return false;
        }
        ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
        ScopedStream.flush();
        if (!ScopedStream)
        {
            ScopedStream.close();
            std::filesystem::remove(ScopedTemporary, ScopedCode);
            SetError(InError, "write failed for " + ScopedTemporary.string());
            return false;
        }
    }
#if defined(_WIN32)
    if (::MoveFileExW(ScopedTemporary.wstring().c_str(), InPath.wstring().c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        std::filesystem::remove(ScopedTemporary, ScopedCode);
        SetError(InError, "cannot replace " + InPath.string());
        return false;
    }
#else
    if (::rename(ScopedTemporary.c_str(), InPath.c_str()) != 0)
    {
        std::filesystem::remove(ScopedTemporary, ScopedCode);
        SetError(InError, "cannot replace " + InPath.string());
        return false;
    }
#endif
    return true;
}
}

AssetImportService::~AssetImportService()
{
    Shutdown();
}

bool AssetImportService::Initialize(Assets::AssetDatabase& InDatabase, VirtualFileSystem& InVfs, Rhi::RHI* InGpu,
                                    const Config& InConfig, std::string* InError)
{
    Shutdown();
    if (InConfig.assetsRoot.empty() || !std::filesystem::is_directory(InConfig.assetsRoot))
    {
        SetError(InError, "asset import root is not a directory: " + InConfig.assetsRoot.string());
        return false;
    }
    MDatabase = &InDatabase;
    MVfs = &InVfs;
    MGpu = InGpu;
    MConfig = InConfig;
    MStats = {};
    MRequestHead = MRequestTail = 0;
    MCompletedHead = MCompletedTail = 0;
    MNextSequence = 0;
    MInitialized = true;
    if (MConfig.background)
    {
        StartWorker();
    }
    LogInfo("import", "importer ready for '{}' ({} / {} mips default)", MConfig.assetsRoot.string(),
            Assets::TextureFormatName(MConfig.textureFormat), MConfig.srgb ? "sRGB" : "linear");
    return true;
}

void AssetImportService::Shutdown()
{
    StopWorker();
    MDatabase = nullptr;
    MVfs = nullptr;
    MGpu = nullptr;
    MInitialized = false;
    MRequestHead = MRequestTail = 0;
    MCompletedHead = MCompletedTail = 0;
}

void AssetImportService::StartWorker()
{
    MStopRequested.store(false, std::memory_order_relaxed);
    MWorkerCount = std::clamp(MConfig.workerCount, 1u, kMaxCookWorkers);
    if (!MConfig.background)
    {
        MWorkerCount = 0;
    }
    for (std::uint32_t ScopedWorker = 0; ScopedWorker < MWorkerCount; ++ScopedWorker)
    {
        MWorkers[ScopedWorker] = std::jthread([this](std::stop_token InStopToken) { WorkerLoop(InStopToken); });
    }
}

void AssetImportService::StopWorker()
{
    MStopRequested.store(true, std::memory_order_relaxed);
    MCondition.notify_all();
    for (std::uint32_t ScopedWorker = 0; ScopedWorker < MWorkerCount; ++ScopedWorker)
    {
        if (MWorkers[ScopedWorker].joinable())
        {
            MWorkers[ScopedWorker].join();
        }
        MWorkers[ScopedWorker] = std::jthread{};
    }
    MWorkerCount = 0;
}

bool AssetImportService::EnsureMeta(std::string_view InSourcePath, std::string* InError)
{
    if (!MInitialized || MDatabase == nullptr)
    {
        SetError(InError, "importer is not initialized");
        return false;
    }
    const Assets::AssetHandle ScopedHandle =
        ImportSourceFile(*MDatabase, MConfig.assetsRoot, InSourcePath, MConfig.textureFormat, InError);
    if (!ScopedHandle.IsValid())
    {
        return false;
    }
    if (const Assets::AssetMeta* ScopedMeta = MDatabase->Meta(ScopedHandle);
        ScopedMeta != nullptr && ScopedMeta->kind == AssetKind::Texture)
    {
        std::string ScopedScheduleError;
        (void)ScheduleCook(ScopedMeta->sourcePath, ScopedHandle, &ScopedScheduleError);
    }
    return true;
}

bool AssetImportService::ScheduleCook(std::string_view InSourcePath, Assets::AssetHandle InHandle,
                                      std::string* InError) noexcept
{
    if (!MInitialized || MDatabase == nullptr)
    {
        SetError(InError, "importer is not initialized");
        return false;
    }
    const Assets::AssetMeta* ScopedMeta = MDatabase->Meta(InHandle);
    if (ScopedMeta == nullptr)
    {
        SetError(InError, "stale asset handle");
        return false;
    }
    // Audio/video are copied or decoded by their own runtime paths; this
    if (ScopedMeta->kind != AssetKind::Texture)
    {
        return true;
    }
    const std::string ScopedNormalized = Assets::NormalizePakPath(InSourcePath);
    if (ScopedNormalized.empty() || ScopedNormalized.size() >= kSourcePathCapacity)
    {
        SetError(InError, "source path is empty or too long");
        return false;
    }
    if (MVfs != nullptr)
    {
        // Windows cannot replace a mapped file. Drop both mappings on the
        (void)MVfs->InvalidateFile(ScopedNormalized);
        const std::string ScopedCooked = CookedPathForSource(ScopedNormalized, ScopedMeta->kind);
        if (!ScopedCooked.empty())
        {
            (void)MVfs->InvalidateFile(ScopedCooked);
        }
    }

    std::lock_guard ScopedLock(MMutex);
    for (std::uint32_t ScopedI = MRequestTail; ScopedI != MRequestHead; ScopedI = (ScopedI + 1u) % kMaxQueuedCooks)
    {
        if (MRequests[ScopedI].handle == InHandle)
        {
            // Already queued; coalesce a burst
            return true;
        }
    }
    const std::uint32_t ScopedNext = (MRequestHead + 1u) % kMaxQueuedCooks;
    if (ScopedNext == MRequestTail)
    {
        ++MStats.dropped;
        SetError(InError, "asset cook queue is full");
        return false;
    }
    CookRequest& ScopedRequest = MRequests[MRequestHead];
    std::memset(&ScopedRequest, 0, sizeof(ScopedRequest));
    CopyFixed(ScopedRequest.sourcePath, sizeof(ScopedRequest.sourcePath), ScopedNormalized);
    ScopedRequest.handle = InHandle;
    ScopedRequest.kind = ScopedMeta->kind;
    ScopedRequest.format = MConfig.textureFormat;
    ScopedRequest.srgb = ScopedMeta->import.srgb;
    ScopedRequest.maxSize = ScopedMeta->import.maxSize;
    ScopedRequest.sequence = MNextSequence++;
    MRequestHead = ScopedNext;
    ++MStats.queued;
    MCondition.notify_one();
    return true;
}

bool AssetImportService::ScheduleCookThunk(void* InUser, std::string_view InSourcePath, Assets::AssetHandle InHandle,
                                           std::string* InError) noexcept
{
    auto* ScopedSelf = static_cast<AssetImportService*>(InUser);
    return ScopedSelf != nullptr && ScopedSelf->ScheduleCook(InSourcePath, InHandle, InError);
}

void AssetImportService::WorkerLoop(std::stop_token InStopToken)
{
    for (;;)
    {
        CookRequest ScopedRequest;
        {
            std::unique_lock ScopedLock(MMutex);
            MCondition.wait(ScopedLock,
                            [this, &InStopToken]
                            {
                                return MStopRequested.load(std::memory_order_relaxed) || InStopToken.stop_requested() ||
                                       MRequestTail != MRequestHead;
                            });
            if ((MStopRequested.load(std::memory_order_relaxed) || InStopToken.stop_requested()) &&
                MRequestTail == MRequestHead)
            {
                break;
            }
            ScopedRequest = MRequests[MRequestTail];
            MRequestTail = (MRequestTail + 1u) % kMaxQueuedCooks;
        }

        CookResult ScopedResult;
        ScopedResult.handle = ScopedRequest.handle;
        CopyFixed(ScopedResult.sourcePath, sizeof(ScopedResult.sourcePath), ScopedRequest.sourcePath);

        const std::filesystem::path ScopedSource =
            MConfig.assetsRoot / std::filesystem::path(std::string(ScopedRequest.sourcePath));
        ScopedResult.sourceHash = HashSourceFile(ScopedSource);
        const std::string ScopedCookedRelative = CookedPathForSource(ScopedRequest.sourcePath, ScopedRequest.kind);
        if (ScopedCookedRelative.empty())
        {
            CopyFixed(ScopedResult.error, sizeof(ScopedResult.error), "no cooked destination for this asset kind");
            ScopedResult.ok = false;
        }
        else
        {
            Fby::Cook::TextureCookOptions ScopedOptions;
            ScopedOptions.target =
                IsAstc(ScopedRequest.format) ? Fby::Cook::CookTarget::Mobile : Fby::Cook::CookTarget::Desktop;
            ScopedOptions.astcBlock = (ScopedRequest.format == Assets::TextureFormat::Astc6x6Srgb ||
                                       ScopedRequest.format == Assets::TextureFormat::Astc6x6Unorm)
                                          ? Fby::Cook::AstcBlock::Block6x6
                                          : Fby::Cook::AstcBlock::Block4x4;
            ScopedOptions.srgb = ScopedRequest.srgb;
            Fby::Cook::CookedTexture ScopedCooked;
            std::string ScopedError;
            if (!Fby::Cook::CookTextureFile(ScopedSource, ScopedOptions, &ScopedCooked, &ScopedError))
            {
                CopyFixed(ScopedResult.error, sizeof(ScopedResult.error), ScopedError);
                ScopedResult.ok = false;
            }
            else
            {
                const std::filesystem::path ScopedCookedPhysical =
                    MConfig.assetsRoot / std::filesystem::path(ScopedCookedRelative);
                if (!WriteBytesAtomically(ScopedCookedPhysical, ScopedCooked.payload, &ScopedError))
                {
                    CopyFixed(ScopedResult.error, sizeof(ScopedResult.error), ScopedError);
                    ScopedResult.ok = false;
                }
                else
                {
                    CopyFixed(ScopedResult.cookedPath, sizeof(ScopedResult.cookedPath), ScopedCookedRelative);
                    ScopedResult.sourceHash = HashSourceFile(ScopedSource);
                    ScopedResult.cookedHash = HashBytes(ScopedCooked.payload);
                    ScopedResult.width = ScopedCooked.width;
                    ScopedResult.height = ScopedCooked.height;
                    ScopedResult.mipLevels = ScopedCooked.mipLevels;
                    ScopedResult.format = ScopedCooked.format;
                    ScopedResult.flags = (ScopedCooked.hasAlpha ? Assets::kTextureFlagAlpha : 0u) |
                                         (Assets::IsSrgb(ScopedCooked.format) ? Assets::kTextureFlagSrgb : 0u);
                    ScopedResult.ok = true;
                }
            }
        }

        {
            std::unique_lock ScopedLock(MMutex);
            const auto ScopedHasSpace = [this]()
            {
                return ((MCompletedHead + 1u) % kMaxCompletedCooks) != MCompletedTail;
            };
            if (!ScopedHasSpace())
            {
                ++MStats.blocked;
                (void)MCompletedSpace.wait_for(ScopedLock, std::chrono::milliseconds(250),
                                               [&InStopToken, &ScopedHasSpace]
                                               {
                                                   return InStopToken.stop_requested() || ScopedHasSpace();
                                               });
            }
            if (!ScopedHasSpace())
            {
                // The editor did not drain in time (paused or shutting down).
                // This is the only path that still drops a cooked result.
                ++MStats.dropped;
                continue;
            }
            MCompleted[MCompletedHead] = ScopedResult;
            MCompletedHead = (MCompletedHead + 1u) % kMaxCompletedCooks;
            if (ScopedResult.ok)
            {
                ++MStats.cooked;
            }
            else
            {
                ++MStats.failed;
            }
        }
    }
}

bool AssetImportService::PopResult(CookResult* InResult) noexcept
{
    if (InResult == nullptr || MCompletedTail == MCompletedHead)
    {
        return false;
    }
    *InResult = MCompleted[MCompletedTail];
    MCompletedTail = (MCompletedTail + 1u) % kMaxCompletedCooks;
    // Wake a worker that is throttled on a full result ring.
    MCompletedSpace.notify_one();
    return true;
}

std::uint32_t AssetImportService::Tick(std::string* InError)
{
    if (!MInitialized)
    {
        return 0;
    }
    std::uint32_t ScopedCommitted = 0;
    CookResult ScopedResult;
    // Bounded commit work per frame: a large batch of finished cooks must not
    // turn one editor frame into a multi-second stall. The rest is drained by
    // the following ticks.
    for (std::uint32_t ScopedBudget = 0; ScopedBudget < kMaxCommitsPerTick; ++ScopedBudget)
    {
        {
            std::lock_guard ScopedLock(MMutex);
            if (!PopResult(&ScopedResult))
            {
                break;
            }
        }
        if (!ScopedResult.ok)
        {
            LogWarn("import", "cook failed for '{}': {}", ScopedResult.sourcePath, ScopedResult.error);
            if (InError != nullptr)
            {
                *InError = ScopedResult.error;
            }
            continue;
        }
        if (ApplyResult(ScopedResult, InError))
        {
            ++ScopedCommitted;
        }
    }
    // Frames that did not drain everything still made ring space, so unblock
    // throttled cooks for the next tick.
    MCompletedSpace.notify_all();
    return ScopedCommitted;
}

bool AssetImportService::ApplyResult(const CookResult& InResult, std::string* InError)
{
    if (MDatabase == nullptr || !InResult.handle.IsValid())
    {
        return false;
    }
    const Assets::AssetMeta* ScopedLive = MDatabase->Meta(InResult.handle);
    if (ScopedLive == nullptr)
    {
        SetError(InError, "asset disappeared before its cook completed");
        ++MStats.failed;
        return false;
    }
    Assets::AssetMeta ScopedUpdated = *ScopedLive;
    CopyFixed(ScopedUpdated.cookedPath, sizeof(ScopedUpdated.cookedPath), InResult.cookedPath);
    ScopedUpdated.sourceHash = InResult.sourceHash;
    ScopedUpdated.cookedHash = InResult.cookedHash;
    ScopedUpdated.flags = InResult.flags;
    std::string ScopedTransactionError;
    if (!MDatabase->BeginTransaction() ||
        !MDatabase->StageMeta(InResult.handle, ScopedUpdated, &ScopedTransactionError) ||
        !MDatabase->CommitTransaction())
    {
        if (MDatabase->InTransaction())
        {
            MDatabase->RollbackTransaction();
        }
        SetError(InError, ScopedTransactionError.empty() ? "asset transaction failed" : ScopedTransactionError);
        ++MStats.failed;
        return false;
    }

    Assets::AssetDatabase::ReloadNotice ScopedNotice{};
    while (MDatabase->PopReload(&ScopedNotice))
    {
    }

    // Keep the authored .femeta in sync with the committed row. The watcher
    if (const Assets::AssetMeta* ScopedCommitted = MDatabase->Meta(InResult.handle); ScopedCommitted != nullptr)
    {
        std::string ScopedMetaError;
        if (!WriteMetaBesideSource(MConfig.assetsRoot, InResult.sourcePath, *ScopedCommitted, &ScopedMetaError))
        {
            LogWarn("import", "could not update '{}': {}", InResult.sourcePath, ScopedMetaError);
        }
    }

    if (MVfs != nullptr)
    {
        std::string ScopedVfsError;
        // Sources stay out of the VFS mapping: the cooker reads them from disk
        (void)MVfs->InvalidateFile(InResult.sourcePath);
        if (!MVfs->ReloadFile(InResult.cookedPath, &ScopedVfsError))
        {
            LogWarn("import", "cooked '{}' is not remappable yet: {}", InResult.cookedPath, ScopedVfsError);
        }
        else if (MGpu != nullptr && MConfig.textureArray.IsValid())
        {
            if (InResult.format != MConfig.textureArrayFormat)
            {
                LogWarn("import", "'{}' cooked as {} but the bound texture array is {}; layer left untouched",
                        InResult.cookedPath, Assets::TextureFormatName(InResult.format),
                        Assets::TextureFormatName(MConfig.textureArrayFormat));
            }
            else if (const auto ScopedBytes = MVfs->Read(InResult.cookedPath); ScopedBytes)
            {
                if (const auto ScopedView = Assets::ParseCookedTexture(*ScopedBytes, &ScopedVfsError); ScopedView)
                {
                    const std::uint32_t ScopedLayer = LayerFor(ScopedLive->id);
                    if (ScopedLayer == Rhi::TextureArrayHandle::kInvalid32)
                    {
                        LogInfo("import", "'{}' has no atlas layer binding; RHI upload skipped", InResult.cookedPath);
                    }
                    else
                    {
                        std::uint64_t ScopedOffset = 0;
                        bool ScopedUploaded = true;
                        for (std::uint32_t ScopedMip = 0; ScopedMip < ScopedView->mipLevels; ++ScopedMip)
                        {
                            const std::uint32_t ScopedWidth = Assets::MipExtent(ScopedView->width, ScopedMip);
                            const std::uint32_t ScopedHeight = Assets::MipExtent(ScopedView->height, ScopedMip);
                            const std::uint64_t ScopedMipBytes =
                                Assets::MipByteSize(ScopedView->format, ScopedWidth, ScopedHeight);
                            if (ScopedOffset + ScopedMipBytes > ScopedView->texels.size())
                            {
                                ScopedUploaded = false;
                                break;
                            }
                            if (!MGpu->UpdateTextureArrayLayer(
                                    MConfig.textureArray, ScopedLayer, ScopedMip,
                                    ScopedView->texels.subspan(static_cast<std::size_t>(ScopedOffset),
                                                               static_cast<std::size_t>(ScopedMipBytes))))
                            {
                                ScopedUploaded = false;
                                break;
                            }
                            ScopedOffset += ScopedMipBytes;
                        }
                        if (ScopedUploaded)
                        {
                            ++MStats.textureLayersUpdated;
                            LogInfo("import", "updated texture array layer {} from '{}' ({} mips)", ScopedLayer,
                                    InResult.cookedPath, ScopedView->mipLevels);
                        }
                        else
                        {
                            LogWarn("import", "RHI rejected the layer update for '{}'", InResult.cookedPath);
                        }
                    }
                }
                else
                {
                    LogWarn("import", "cooked payload '{}' is invalid: {}", InResult.cookedPath, ScopedVfsError);
                }
            }
        }
    }
    ++MStats.committed;
    LogInfo("import", "committed '{}' (asset {}, content gen {})", InResult.sourcePath, ScopedLive->id.value,
            MDatabase->Meta(InResult.handle) != nullptr ? MDatabase->Meta(InResult.handle)->contentGeneration : 0u);
    return true;
}

void AssetImportService::BindTextureAsset(AssetId InId, std::uint32_t InLayer) noexcept
{
    if (!InId.IsValid())
    {
        return;
    }
    for (TextureBinding& ScopedBinding : MBindings)
    {
        if (ScopedBinding.used && ScopedBinding.id == InId)
        {
            ScopedBinding.layer = InLayer;
            return;
        }
    }
    for (TextureBinding& ScopedBinding : MBindings)
    {
        if (!ScopedBinding.used)
        {
            ScopedBinding = TextureBinding{InId, InLayer, true};
            return;
        }
    }
}

std::uint32_t AssetImportService::LayerFor(AssetId InId) const noexcept
{
    for (const TextureBinding& ScopedBinding : MBindings)
    {
        if (ScopedBinding.used && ScopedBinding.id == InId)
        {
            return ScopedBinding.layer;
        }
    }
    return Rhi::TextureArrayHandle::kInvalid32;
}

bool AssetImportService::BindAtlasManifest(std::string_view InManifestPath, std::string* InError)
{
    if (MVfs == nullptr || MDatabase == nullptr)
    {
        SetError(InError, "cannot bind an atlas before the importer is initialized");
        return false;
    }
    const auto ScopedBytes = MVfs->Read(InManifestPath);
    if (!ScopedBytes)
    {
        SetError(InError, "atlas manifest not found in the VFS");
        return false;
    }
    std::string ScopedParseError;
    const auto ScopedManifest = Assets::ParseAtlasManifest(*ScopedBytes, &ScopedParseError);
    if (!ScopedManifest)
    {
        SetError(InError, ScopedParseError);
        return false;
    }
    uint32_t ScopedBound = 0;
    for (std::size_t ScopedI = 0; ScopedI < ScopedManifest->layers.size(); ++ScopedI)
    {
        const std::string& ScopedCookedSource = ScopedManifest->layers[ScopedI].source;
        const std::string ScopedStem = ReplaceExtension(ScopedCookedSource, "");
        for (const char* ScopedExtension : {".png", ".tga"})
        {
            const std::string ScopedSource = ScopedStem + ScopedExtension;
            if (const Assets::AssetHandle ScopedHandle = MDatabase->FindHandleBySourcePath(ScopedSource);
                ScopedHandle.IsValid())
            {
                BindTextureAsset(MDatabase->Id(ScopedHandle), static_cast<std::uint32_t>(ScopedI));
                ++ScopedBound;
                break;
            }
        }
    }
    LogInfo("import", "bound {} of {} atlas layer(s) for hot reload", ScopedBound, ScopedManifest->layers.size());
    return true;
}
}
