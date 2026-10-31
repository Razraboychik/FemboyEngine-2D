// FemboyEngine2D - high-level Build & Pack Game orchestrator.
#pragma once

#include "Engine/Assets/CookProgress.hpp"
#include "Engine/Core/BuildProfile.hpp"
#include "Engine/Script/AotBuilder.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace Fby::Editor
{

struct ProjectPaths;
struct ProjectSettings;
inline constexpr std::uint32_t kReleasePathBytes = 512;
inline constexpr std::uint32_t kReleaseNameBytes = 128;
inline constexpr std::uint32_t kReleaseAssetBytes = 192;
inline constexpr std::uint32_t kReleaseMessageBytes = 256;

enum class ReleaseCookStage : std::uint32_t
{
    Idle = 0,
    PreparingKey = 1,
    Aot = 2,
    Cooking = 3,
    Packing = 4,
    Compressing = 5,
    Encrypting = 6,
    Succeeded = 7,
    Failed = 8,
    Cancelled = 9,
};

[[nodiscard]] constexpr const char* ReleaseCookStageName(ReleaseCookStage InStage) noexcept
{
    switch (InStage)
    {
        case ReleaseCookStage::Idle:
            return "idle";
        case ReleaseCookStage::PreparingKey:
            return "preparing key";
        case ReleaseCookStage::Aot:
            return "AOT plugin";
        case ReleaseCookStage::Cooking:
            return "cooking assets";
        case ReleaseCookStage::Packing:
            return "packing";
        case ReleaseCookStage::Compressing:
            return "compressing";
        case ReleaseCookStage::Encrypting:
            return "encrypting";
        case ReleaseCookStage::Succeeded:
            return "succeeded";
        case ReleaseCookStage::Failed:
            return "failed";
        case ReleaseCookStage::Cancelled:
            return "cancelled";
    }
    return "?";
}

struct ReleaseCookConfig
{
    char projectName[kReleaseNameBytes] = "Untitled Project";
    char projectRoot[kReleasePathBytes] = {};
    char assetsRoot[kReleasePathBytes] = {};
    char outputRoot[kReleasePathBytes] = {};
    char archivesRoot[kReleasePathBytes] = {};
    char storesRoot[kReleasePathBytes] = {};
    char aotOutputPath[kReleasePathBytes] = {};
    char packPath[kReleasePathBytes] = {};
    char archivePath[kReleasePathBytes] = {};
    char packerPath[kReleasePathBytes] = {};
    char engineSourcePath[kReleasePathBytes] = {};
    char compiler[kReleaseNameBytes] = "clang-cl";
    char moduleName[kReleaseNameBytes] = "logic_graph";
    char keySeedPath[kReleasePathBytes] = {};
    char keyHeaderPath[kReleasePathBytes] = {};
    char keyFileTempPath[kReleasePathBytes] = {};
    char progressPath[kReleasePathBytes] = {};
    char logPath[kReleasePathBytes] = {};
    Fby::Build::BuildProfile profile{};
    // 0 = no encryption, 1 = ChaCha20-Poly1305, 2 = AES-256-GCM
    std::uint32_t cipher = 1;
    bool compileAot = true;
    bool keepAotSource = true;

    [[nodiscard]] bool Encrypts() const noexcept
    {
        return cipher != 0;
    }
};

// Writes the per-store manifest files for one staged store directory:
// `store.txt`, `store_manifest.json` with FNV-1a artifact hashes and, for Steam,
// a `depot_build.vdf` upload script. The function does not copy files; the
// caller stages the archive and the plugin next to it. Public so the release test
// can verify the artifacts without running a full cook.
[[nodiscard]] bool WriteStoreManifest(const std::filesystem::path& InStoreDirectory, std::string_view InStoreName,
                                      const std::filesystem::path& InArchivePath,
                                      const std::filesystem::path& InPluginPath, std::string_view InProjectName,
                                      std::uint32_t InCipher, bool InKeyBaked, std::string* InError = nullptr);

// Writes the shipped attribution into one staged store directory:
// `THIRD_PARTY_NOTICES.md` (the Fby::Notices document) and `SOURCE_OFFER.txt`
// (the MPL 2.0 source offer). Public so the release test can verify the files.
[[nodiscard]] bool WriteStoreNotices(const std::filesystem::path& InStoreDirectory, std::string* InError = nullptr);

struct ReleaseCookProgress
{
    ReleaseCookStage stage = ReleaseCookStage::Idle;
    std::uint32_t completed = 0;
    std::uint32_t total = 0;
    char asset[kReleaseAssetBytes] = {};
    char message[kReleaseMessageBytes] = {};
    std::uint64_t rawBytes = 0;
    std::uint64_t storedBytes = 0;
    std::uint32_t bytesPerSecond = 0;
    std::uint32_t cipher = 0;
    bool encrypted = false;
    bool running = false;
    bool failed = false;

    [[nodiscard]] float Fraction() const noexcept
    {
        return total == 0 ? 0.0f : static_cast<float>(completed) / static_cast<float>(total);
    }
};

struct ReleaseCookResult
{
    bool success = false;
    bool keyBaked = false;
    bool encrypted = false;
    std::uint64_t packedBytes = 0;
    std::uint64_t archiveBytes = 0;
    char archivePath[kReleasePathBytes] = {};
    char pluginPath[kReleasePathBytes] = {};
    char message[kReleaseMessageBytes] = {};
};

// Generates or reuses a 32-byte project seed, writes the build-scoped binary key
[[nodiscard]] bool BakeReleaseKey(const char* InSeedPath, const char* InKeyPath, const char* InHeaderPath,
                                  char* OutError = nullptr, std::size_t InErrorCapacity = 0) noexcept;

class ReleaseCooker
{
public:
    ReleaseCooker() = default;
    ~ReleaseCooker();

    ReleaseCooker(const ReleaseCooker&) = delete;
    ReleaseCooker& operator=(const ReleaseCooker&) = delete;

    // Builds a config from the open project. `engineRoot` and `packerPath` are
    [[nodiscard]] static ReleaseCookConfig FromProject(const ProjectPaths& InPaths, const ProjectSettings& InSettings,
                                                       std::string_view InEngineRoot, std::string_view InPackerPath);

    // Starts the asynchronous pipeline. `InGraph` may be null or empty: the AOT
    [[nodiscard]] bool Start(const ReleaseCookConfig& InConfig, const Script::LogicGraph* InGraph = nullptr,
                             char* OutError = nullptr, std::size_t InErrorCapacity = 0);

    // Requests cancellation. A child process is terminated on Windows; on other
    void Cancel() noexcept;

    // Editor-frame poll. Joins a finished worker and keeps the result stable.
    void Poll() noexcept;

    [[nodiscard]] bool Running() const noexcept
    {
        return MRunning.load(std::memory_order_acquire);
    }
    [[nodiscard]] ReleaseCookProgress Progress() const noexcept;
    [[nodiscard]] ReleaseCookResult Result() const noexcept;
    [[nodiscard]] const ReleaseCookConfig& Config() const noexcept
    {
        return MConfig;
    }

    // Exposed for the release checklist and tests. No plaintext key is exposed.
    [[nodiscard]] bool KeyBaked() const noexcept
    {
        return MKeyBaked.load(std::memory_order_acquire);
    }

private:
    void Run(std::stop_token InStop) noexcept;
    void SetProgress(const ReleaseCookProgress& InProgress) noexcept;
    void SetStage(ReleaseCookStage InStage, std::string_view InMessage = {}) noexcept;
    void Fail(std::string_view InMessage) noexcept;
    [[nodiscard]] bool RunChild(const char* InCommandLine, ReleaseCookStage InStage, const char* InAsset,
                                std::uint32_t* OutExitCode);
    void PollProgressFile() noexcept;
    [[nodiscard]] bool PrepareKey() noexcept;
    void WipeKey() noexcept;
    // Copies the final archive (and plugin, when AOT ran) into
    [[nodiscard]] bool StageStoreArchives(const char* InPluginPath) noexcept;

    mutable std::mutex MProgressMutex{};
    ReleaseCookProgress MProgress{};
    ReleaseCookResult MResult{};
    ReleaseCookConfig MConfig{};
    Script::LogicGraph MGraph{};
    bool MHasGraph = false;
    std::atomic<bool> MKeyBaked{false};
    Script::AotBuilder MAot{};
    std::jthread MWorker{};
    std::atomic<bool> MRunning{false};
    std::atomic<bool> MStopRequested{false};
    std::array<char, kReleasePathBytes> MKeyFile{};
    std::array<char, kReleasePathBytes> MKeyHeader{};
};
}
