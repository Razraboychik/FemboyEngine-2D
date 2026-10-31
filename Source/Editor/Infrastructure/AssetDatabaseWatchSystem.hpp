// FemboyEngine2D - asset database source watcher.
#pragma once

#include <mutex>

#include "Engine/Assets/AssetDatabase.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace Fby::Editor
{

class AssetDatabaseWatchSystem
{
public:
    static constexpr std::uint32_t kMaxWatchedPaths = 128;
    static constexpr std::uint32_t kMaxPendingChanges = 64;
    static constexpr std::uint32_t kWatchPathCapacity = 192;

    struct Config
    {
        // Assets root; source paths are relative to it
        std::filesystem::path projectRoot;
        std::uint32_t debounceMs = 250;
        bool recursive = true;
        // A loose PNG/WAV/WebM without a .femeta row is imported automatically:
        bool autoImport = true;
        Assets::TextureFormat defaultTextureFormat = Assets::TextureFormat::Bc7Srgb;
    };

    struct Callbacks
    {
        void* user = nullptr;
        // Asynchronous cooker seam. The watcher only enqueues the re-cook; the
        bool (*scheduleCook)(void* InUser, std::string_view InSourcePath, Assets::AssetHandle InHandle,
                             std::string* InError) noexcept = nullptr;
        // Synchronous fallback, retained for tools and tests. The watcher opens
        bool (*cookAndStage)(void* InUser, std::string_view InSourcePath, Assets::AssetMeta* OutMeta,
                             std::string* InError) noexcept = nullptr;
        // Uploads the new cooked bytes into the RHI/audio/animation runtime.
        bool (*reload)(void* InUser, Assets::AssetHandle InHandle, const Assets::AssetMeta& InMeta,
                       std::string* InError) noexcept = nullptr;
    };

    AssetDatabaseWatchSystem() = default;
    ~AssetDatabaseWatchSystem();

    AssetDatabaseWatchSystem(const AssetDatabaseWatchSystem&) = delete;
    AssetDatabaseWatchSystem& operator=(const AssetDatabaseWatchSystem&) = delete;

    [[nodiscard]] bool Initialize(Assets::AssetDatabase& InDatabase, const Config& InConfig, Callbacks InCallbacks,
                                  std::string* InError = nullptr);
    void Shutdown();

    [[nodiscard]] bool IsActive() const noexcept
    {
        return MActive;
    }
    [[nodiscard]] bool IsOsWatcherActive() const noexcept
    {
        return MOsWatcherActive;
    }
    // The Settings panel can change the project cook target while the watcher
    void SetDefaultTextureFormat(Assets::TextureFormat InFormat) noexcept
    {
        MConfig.defaultTextureFormat = InFormat;
    }

    // Adds a folder (relative to projectRoot) to the watched set.
    [[nodiscard]] bool Watch(std::string_view InRelativeDirectory, std::string* InError = nullptr);

    // Startup pass: creates .femeta rows for source files that appeared while
    std::uint32_t ScanForNewSources(std::string* InError = nullptr);

    // Editor-frame entry point: drains OS changes, debounces by source hash,
    std::uint32_t Poll();

    // Test/tool seam: enqueue a change as if the OS had reported it. The OS
    [[nodiscard]] bool InjectChange(std::string_view InRelativePath, std::uint64_t InSourceHash = 0) noexcept;

    struct Stats
    {
        std::uint32_t changesSeen = 0;
        std::uint32_t cookRequests = 0;
        std::uint32_t reloads = 0;
        std::uint32_t failures = 0;
        std::uint32_t ignored = 0;
    };
    [[nodiscard]] const Stats& GetStats() const noexcept
    {
        return MStats;
    }

private:
    struct Change
    {
        char path[kWatchPathCapacity]{};
        std::uint64_t sourceHash = 0;
        std::uint32_t sequence = 0;
    };

    static bool IsRelevantFile(std::string_view InPath) noexcept;
    static bool IsRelevantSourceFile(std::string_view InPath) noexcept;
    [[nodiscard]] bool StartOsWatcher(std::string* InError);
    void StopOsWatcher() noexcept;
    [[nodiscard]] bool PushChange(std::string_view InRelativePath) noexcept;
    [[nodiscard]] bool PushChangeLocked(std::string_view InRelativePath) noexcept;
    [[nodiscard]] bool DrainChange(Change* Out) noexcept;
    void ProcessChange(const Change& InChange);
    void ProcessMetaFile(std::string_view InRelativePath);
    [[nodiscard]] Assets::AssetHandle EnsureImported(std::string_view InRelativePath, std::string* InError);

    Assets::AssetDatabase* MDatabase = nullptr;
    Config MConfig{};
    Callbacks MCallbacks{};

    std::array<Change, kMaxPendingChanges> MPending{};
    std::uint32_t MPendingHead = 0;
    std::uint32_t MPendingTail = 0;
    // Produced by the OS watcher thread and by main-thread injection.
    std::mutex MPendingMutex;

    std::array<std::array<char, kWatchPathCapacity>, kMaxWatchedPaths> MWatched{};
    std::uint32_t MWatchedCount = 0;

    std::jthread MThread{};
    std::atomic<bool> MStopRequested{false};
    std::atomic<bool> MOsWatcherActive{false};
    void* MDirectoryHandle = nullptr;

    Stats MStats{};
    bool MActive = false;
};
}
