// FemboyEngine2D - deterministic storefront ECS systems and binary command log.
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Platform/PlatformService.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby::Platform
{

inline constexpr std::uint32_t kPlatformLogVersion = 1;
inline constexpr std::uint32_t kPlatformLogMaxBytes = 128u * 1024u;
inline constexpr std::uint32_t kPlatformLogMaxRecords = 2048;
inline constexpr char kPlatformLogMagic[8] = {'F', 'B', 'P', 'L', 'O', 'G', '0', '1'};
inline constexpr std::uint32_t kAchievementNameBytes = 48;
inline constexpr std::uint32_t kCloudNameBytes = 48;

enum class PlatformLogType : std::uint8_t
{
    Achievement = 1,
    CloudWrite = 2,
    CloudRead = 3,
    Stat = 4,
    StoreStats = 5,
};

struct PlatformLogEvent
{
    PlatformLogType type = PlatformLogType::Achievement;
    std::uint64_t tick = 0;
    std::uint64_t id = 0;
    std::string_view name;
    std::span<const std::byte> data;
};

using PlatformLogSink = void (*)(void* InUser, const PlatformLogEvent& InEvent);

class PlatformCommandLog
{
public:
    PlatformCommandLog() noexcept
    {
        Clear();
    }

    void Clear() noexcept;
    [[nodiscard]] bool Append(PlatformLogType InType, std::uint64_t InTick, std::uint64_t InId, std::string_view InName,
                              std::span<const std::byte> InData = {}) noexcept;
    void Seal() noexcept;
    [[nodiscard]] bool Replay(PlatformLogSink InSink, void* InUser) const noexcept;

    [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] std::uint64_t Checksum() const noexcept
    {
        return MChecksum;
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return MCount == 0;
    }
    [[nodiscard]] std::uint64_t FirstTick() const noexcept
    {
        return MFirstTick;
    }
    [[nodiscard]] std::uint64_t LastTick() const noexcept
    {
        return MLastTick;
    }

    static constexpr std::uint32_t kHeaderBytes = 48;
    static constexpr std::uint32_t kRecordBytes = 32;

private:
    std::array<std::byte, kPlatformLogMaxBytes> MStorage{};
    std::uint32_t MWriteOffset = kHeaderBytes;
    std::uint32_t MCount = 0;
    std::uint64_t MFirstTick = 0;
    std::uint64_t MLastTick = 0;
    std::uint64_t MChecksum = 0;
};

// Components
enum AchievementFlags : std::uint32_t
{
    AchievementNone = 0,
    AchievementPending = 1u << 0,
    AchievementCommitted = 1u << 1,
    AchievementFailed = 1u << 2,
    AchievementHidden = 1u << 3,
};

struct AchievementRequest
{
    std::uint64_t achievementId = 0;
    std::uint32_t points = 0;
    std::uint32_t flags = 0;
    char apiName[kAchievementNameBytes] = {};
};

static_assert(std::is_trivially_copyable_v<AchievementRequest>);
static_assert(std::is_standard_layout_v<AchievementRequest>);

enum CloudSaveFlags : std::uint32_t
{
    CloudSaveNone = 0,
    CloudSaveDirty = 1u << 0,
    CloudSaveReadRequested = 1u << 1,
    CloudSaveFailed = 1u << 2,
    CloudSaveEncrypted = 1u << 3,
};

struct CloudSaveComponent
{
    std::uint64_t saveId = 0;
    std::uint32_t version = 1;
    std::uint32_t bytes = 0;
    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;
    char fileName[kCloudNameBytes] = {};
};

static_assert(std::is_trivially_copyable_v<CloudSaveComponent>);
static_assert(std::is_standard_layout_v<CloudSaveComponent>);

struct PlatformAccountComponent
{
    std::uint64_t userId = 0;
    std::uint32_t capabilities = 0;
    std::uint32_t state = 0;
    std::uint64_t lastSyncTick = 0;
};

static_assert(std::is_trivially_copyable_v<PlatformAccountComponent>);

// SteamAchievementSystem
class SteamAchievementSystem
{
public:
    SteamAchievementSystem() = default;

    void Initialize(PlatformService& InService, PlatformCommandLog& InLog, std::uint64_t InStartTick = 1) noexcept;
    // FixedUpdate. Collects up to 64 pending requests, sorts them by achievement
    void Update(Ecs::SystemContext& InContext);
    [[nodiscard]] std::uint32_t Committed() const noexcept
    {
        return MCommitted;
    }
    [[nodiscard]] std::uint32_t Failed() const noexcept
    {
        return MFailed;
    }

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, SteamAchievementSystem& InSystem);

private:
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    PlatformService* MService = nullptr;
    PlatformCommandLog* MLog = nullptr;
    std::uint64_t MNextTick = 1;
    std::uint32_t MCommitted = 0;
    std::uint32_t MFailed = 0;
};

// EgsStoreSystem
class EgsStoreSystem
{
public:
    static constexpr std::uint32_t kMaxStagedSaves = 16;
    static constexpr std::uint32_t kStagedSaveBytes = 1024;

    EgsStoreSystem() = default;

    void Initialize(PlatformService& InService, PlatformCommandLog& InLog, std::uint64_t InStartTick = 1) noexcept;
    // Copies a save payload into the fixed staging arena. Returns false when the
    [[nodiscard]] bool StageWrite(std::uint64_t InSaveId, std::string_view InFileName,
                                  std::span<const std::byte> InData) noexcept;
    // PostUpdate. Scans CloudSaveComponent, queues a bounded number of writes and
    void Update(Ecs::SystemContext& InContext);
    [[nodiscard]] std::uint32_t Uploaded() const noexcept
    {
        return MUploaded;
    }
    [[nodiscard]] std::uint32_t Failed() const noexcept
    {
        return MFailed;
    }

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, EgsStoreSystem& InSystem);

private:
    struct StagedSave
    {
        std::uint64_t saveId = 0;
        std::uint32_t size = 0;
        bool used = false;
        std::array<std::byte, kStagedSaveBytes> data{};
    };

    [[nodiscard]] StagedSave* Find(std::uint64_t InSaveId) noexcept;
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    PlatformService* MService = nullptr;
    PlatformCommandLog* MLog = nullptr;
    std::array<StagedSave, kMaxStagedSaves> MStaged{};
    std::uint64_t MNextTick = 1;
    std::uint32_t MUploaded = 0;
    std::uint32_t MFailed = 0;
};

// GogAchievementSystem
class GogAchievementSystem
{
public:
    GogAchievementSystem() = default;

    void Initialize(PlatformService& InService, PlatformCommandLog& InLog, std::uint64_t InStartTick = 1) noexcept;
    // PostUpdate. Bounded batch (64), deterministic (achievement id, then entity).
    void Update(Ecs::SystemContext& InContext);
    [[nodiscard]] std::uint32_t Committed() const noexcept
    {
        return MCommitted;
    }
    [[nodiscard]] std::uint32_t Failed() const noexcept
    {
        return MFailed;
    }
    [[nodiscard]] std::uint32_t DuplicatesSkipped() const noexcept
    {
        return MDuplicates;
    }
    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, GogAchievementSystem& InSystem);

private:
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    PlatformService* MService = nullptr;
    PlatformCommandLog* MLog = nullptr;
    std::uint64_t MNextTick = 1;
    std::uint32_t MCommitted = 0;
    std::uint32_t MFailed = 0;
    std::uint32_t MDuplicates = 0;
};

// GogStorageSystem
class GogStorageSystem
{
public:
    static constexpr std::uint32_t kMaxStagedSaves = 16;
    static constexpr std::uint32_t kStagedSaveBytes = 1024;

    GogStorageSystem() = default;

    void Initialize(PlatformService& InService, PlatformCommandLog& InLog, std::uint64_t InStartTick = 1) noexcept;
    [[nodiscard]] bool StageWrite(std::uint64_t InSaveId, std::string_view InFileName,
                                  std::span<const std::byte> InData) noexcept;
    // PostUpdate. Handles CloudSaveDirty and CloudSaveReadRequested, clears the
    void Update(Ecs::SystemContext& InContext);
    [[nodiscard]] std::uint32_t Uploaded() const noexcept
    {
        return MUploaded;
    }
    [[nodiscard]] std::uint32_t Downloaded() const noexcept
    {
        return MDownloaded;
    }
    [[nodiscard]] std::uint32_t Failed() const noexcept
    {
        return MFailed;
    }
    // A staged save that overwrote a different payload with the same id. The
    [[nodiscard]] std::uint32_t Conflicts() const noexcept
    {
        return MConflicts;
    }
    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, GogStorageSystem& InSystem);

private:
    struct StagedSave
    {
        std::uint64_t saveId = 0;
        std::uint32_t size = 0;
        bool used = false;
        std::array<std::byte, kStagedSaveBytes> data{};
    };

    [[nodiscard]] StagedSave* Find(std::uint64_t InSaveId) noexcept;
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    PlatformService* MService = nullptr;
    PlatformCommandLog* MLog = nullptr;
    std::array<StagedSave, kMaxStagedSaves> MStaged{};
    std::uint64_t MNextTick = 1;
    std::uint32_t MUploaded = 0;
    std::uint32_t MDownloaded = 0;
    std::uint32_t MFailed = 0;
    std::uint32_t MConflicts = 0;
};
}
