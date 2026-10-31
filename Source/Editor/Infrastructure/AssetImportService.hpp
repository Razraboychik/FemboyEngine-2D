// FemboyEngine2D - background import/cook/reload pipeline.
#pragma once

#include "Engine/Assets/AssetDatabase.hpp"
#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace Fby
{
class VirtualFileSystem;
namespace Assets
{
class TextureArrayAsset;
}
}

namespace Fby::Editor
{

class AssetImportService
{
public:
    static constexpr std::uint32_t kSourcePathCapacity = 192;
    static constexpr std::uint32_t kMaxQueuedCooks = 64;
    static constexpr std::uint32_t kMaxCompletedCooks = 256;
    static constexpr std::uint32_t kMaxTextureBindings = 128;
    static constexpr std::uint32_t kMaxCookWorkers = 4;

    struct Config
    {
        std::filesystem::path assetsRoot;
        Assets::TextureFormat textureFormat = Assets::TextureFormat::Bc7Srgb;
        bool srgb = true;
        std::uint32_t maxSize = 2048;
        bool background = true;
        // Parallel cook workers. Each one pops its own request; the result ring
        // stays single-consumer on the editor thread.
        std::uint32_t workerCount = 2;
        // The array a hot-reloaded texture updates in place. An invalid handle
        Rhi::TextureArrayHandle textureArray{};
        // Format the target array was created with. UpdateTextureArrayLayer
        Assets::TextureFormat textureArrayFormat = Assets::TextureFormat::Rgba8;
    };

    struct Stats
    {
        std::uint32_t queued = 0;
        std::uint32_t cooked = 0;
        std::uint32_t committed = 0;
        std::uint32_t textureLayersUpdated = 0;
        std::uint32_t failed = 0;
        std::uint32_t dropped = 0;
        // Times the worker waited for the editor to drain the completed ring.
        std::uint32_t blocked = 0;
    };

    AssetImportService() = default;
    ~AssetImportService();

    AssetImportService(const AssetImportService&) = delete;
    AssetImportService& operator=(const AssetImportService&) = delete;

    [[nodiscard]] bool Initialize(Assets::AssetDatabase& InDatabase, VirtualFileSystem& InVfs, Rhi::RHI* InGpu,
                                  const Config& InConfig, std::string* InError = nullptr);
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const noexcept
    {
        return MInitialized;
    }

    // Writes <source>.femeta when missing and registers it. Idempotent; returns
    [[nodiscard]] bool EnsureMeta(std::string_view InSourcePath, std::string* InError = nullptr);

    // Enqueues a re-cook. Called by AssetDatabaseWatchSystem::Callbacks.
    [[nodiscard]] bool ScheduleCook(std::string_view InSourcePath, Assets::AssetHandle InHandle,
                                    std::string* InError = nullptr) noexcept;

    // Editor-frame entry point. Drains finished cooks, commits the asset
    std::uint32_t Tick(std::string* InError = nullptr);

    // Binds an AssetId to a fixed Vulkan texture-array layer. The editor's atlas
    void BindTextureAsset(AssetId InId, std::uint32_t InLayer) noexcept;
    [[nodiscard]] std::uint32_t LayerFor(AssetId InId) const noexcept;
    // Project Settings can change the cook target without restarting the
    void SetTextureFormat(Assets::TextureFormat InFormat) noexcept
    {
        MConfig.textureFormat = InFormat;
    }
    // Parses atlas.toml and binds each layer's source image to its array index.
    [[nodiscard]] bool BindAtlasManifest(std::string_view InManifestPath, std::string* InError = nullptr);

    [[nodiscard]] const Config& GetConfig() const noexcept
    {
        return MConfig;
    }
    [[nodiscard]] Stats GetStats() const noexcept
    {
        return MStats;
    }
    static bool ScheduleCookThunk(void* InUser, std::string_view InSourcePath, Assets::AssetHandle InHandle,
                                  std::string* InError) noexcept;

private:
    struct CookRequest
    {
        char sourcePath[kSourcePathCapacity]{};
        std::uint32_t sequence = 0;
        Assets::AssetHandle handle{};
        AssetKind kind = AssetKind::Unknown;
        Assets::TextureFormat format = Assets::TextureFormat::Bc7Srgb;
        bool srgb = true;
        std::uint32_t maxSize = 2048;
    };

    struct CookResult
    {
        char sourcePath[kSourcePathCapacity]{};
        char cookedPath[kSourcePathCapacity]{};
        char error[192]{};
        Assets::AssetHandle handle{};
        std::uint64_t sourceHash = 0;
        std::uint64_t cookedHash = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t mipLevels = 1;
        Assets::TextureFormat format = Assets::TextureFormat::Bc7Srgb;
        std::uint32_t flags = 0;
        bool ok = false;
    };

    struct TextureBinding
    {
        AssetId id{};
        std::uint32_t layer = 0;
        bool used = false;
    };

    void WorkerLoop(std::stop_token InStopToken);
    void PushResult(const CookResult& InResult) noexcept;
    [[nodiscard]] bool PopResult(CookResult* InResult) noexcept;
    [[nodiscard]] bool ApplyResult(const CookResult& InResult, std::string* InError);
    void StartWorker();
    void StopWorker();

    Assets::AssetDatabase* MDatabase = nullptr;
    VirtualFileSystem* MVfs = nullptr;
    Rhi::RHI* MGpu = nullptr;
    Config MConfig{};
    Stats MStats{};
    bool MInitialized = false;

    std::array<CookRequest, kMaxQueuedCooks> MRequests{};
    std::uint32_t MRequestHead = 0;
    std::uint32_t MRequestTail = 0;
    std::array<CookResult, kMaxCompletedCooks> MCompleted{};
    std::uint32_t MCompletedHead = 0;
    std::uint32_t MCompletedTail = 0;
    std::array<TextureBinding, kMaxTextureBindings> MBindings{};
    std::uint32_t MNextSequence = 0;

    std::mutex MMutex{};
    std::condition_variable MCondition{};
    // Signalled when the editor pops a completed cook, so a full result ring
    // throttles the worker instead of dropping its result.
    std::condition_variable MCompletedSpace{};
    std::array<std::jthread, kMaxCookWorkers> MWorkers{};
    std::uint32_t MWorkerCount = 0;
    std::atomic<bool> MStopRequested{false};
};
}
