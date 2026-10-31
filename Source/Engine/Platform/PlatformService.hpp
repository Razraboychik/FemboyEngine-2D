// FemboyEngine2D - storefront bridge.
#pragma once

#include "Engine/Core/BuildProfile.hpp"
#include "../../Plugins/PlatformService/PlatformAPI.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace Fby::Platform
{

inline constexpr std::uint32_t kPlatformCommandCapacity = 256;
inline constexpr std::uint32_t kPlatformInlineCloudBytes = 512;
inline constexpr std::uint32_t kPlatformMaxNameBytes = 63;
inline constexpr std::uint32_t kPlatformMaxCommandsPerPump = 32;

enum class PlatformCommandType : std::uint8_t
{
    UnlockAchievement = 1,
    SetStatInt = 2,
    SetStatFloat = 3,
    StoreStats = 4,
    CloudWrite = 5,
    CloudRead = 6,
    ShowOverlay = 7,
};

struct PlatformCommand
{
    PlatformCommandType type = PlatformCommandType::UnlockAchievement;
    std::uint8_t flags = 0;
    std::uint16_t reserved = 0;
    std::uint32_t sequence = 0;
    std::uint64_t id = 0;
    std::int64_t integer = 0;
    double number = 0.0;
    char name[kPlatformMaxNameBytes + 1] = {};
    std::uint32_t dataSize = 0;
    std::array<std::byte, kPlatformInlineCloudBytes> data{};
};

static_assert(std::is_trivially_copyable_v<PlatformCommand>);

struct PlatformServiceCallbacks
{
    void* user = nullptr;
    void (*onAchievement)(void* InUser, std::uint64_t InId, std::string_view InApiName, bool InSuccess) = nullptr;
    void (*onCloud)(void* InUser, std::string_view InName, std::span<const std::byte> InData, bool InSuccess) = nullptr;
    void (*onAuth)(void* InUser, std::uint64_t InUserId, bool InSuccess) = nullptr;
};

struct PlatformServiceStats
{
    std::uint64_t queued = 0;
    std::uint64_t executed = 0;
    std::uint64_t dropped = 0;
    std::uint64_t achievements = 0;
    std::uint64_t cloudWrites = 0;
    std::uint64_t cloudReads = 0;
    std::uint64_t pumps = 0;
    std::uint64_t lastError = 0;
};

struct PlatformServiceDesc
{
    FbyPlatformKind kind = FbyPlatformKind_None;
};

class PlatformService
{
public:
    PlatformService() = default;
    ~PlatformService();

    PlatformService(const PlatformService&) = delete;
    PlatformService& operator=(const PlatformService&) = delete;

    // Loads one provider shared library and resolves fby_platform_get_api.
    [[nodiscard]] bool Load(FbyPlatformKind InKind, std::string_view InLibraryPath, std::string* InError = nullptr);
    // Test/mock hook: adopts an already resolved table. The table must outlive
    [[nodiscard]] bool Attach(const FbyPlatformApi* InApi, std::string* InError = nullptr);

    [[nodiscard]] bool Initialize(const FbyPlatformInitDesc& InDesc, std::string* InError = nullptr);
    void Shutdown() noexcept;
    void Unload() noexcept;

    // Drains the command ring and pumps the provider. Safe to call once per
    std::uint32_t Pump();

    [[nodiscard]] bool QueueAchievement(std::uint64_t InId, std::string_view InApiName) noexcept;
    [[nodiscard]] bool QueueStat(std::string_view InApiName, std::int64_t InValue) noexcept;
    [[nodiscard]] bool QueueStat(std::string_view InApiName, double InValue) noexcept;
    [[nodiscard]] bool QueueStoreStats() noexcept;
    [[nodiscard]] bool QueueCloudWrite(std::uint64_t InId, std::string_view InName,
                                       std::span<const std::byte> InData) noexcept;
    [[nodiscard]] bool QueueCloudRead(std::uint64_t InId, std::string_view InName) noexcept;
    [[nodiscard]] bool QueueOverlay(std::string_view InDialog) noexcept;

    void SetGameCallbacks(const PlatformServiceCallbacks& InCallbacks) noexcept
    {
        MGameCallbacks = InCallbacks;
    }

    [[nodiscard]] const FbyPlatformApi* Api() const noexcept
    {
        return MApi;
    }
    [[nodiscard]] bool Ready() const noexcept
    {
        return MState == PlatformState::Ready;
    }
    [[nodiscard]] FbyPlatformKind Kind() const noexcept
    {
        return MApi == nullptr ? FbyPlatformKind_None : MApi->kind;
    }
    [[nodiscard]] std::string_view ProviderName() const noexcept;
    [[nodiscard]] std::uint32_t Pending() const noexcept;
    [[nodiscard]] const PlatformServiceStats& Stats() const noexcept
    {
        return MStats;
    }
    [[nodiscard]] std::uint64_t UserId() const noexcept;

private:
    enum class PlatformState : std::uint8_t
    {
        Unloaded = 0,
        Loaded,
        Ready,
        Failed,
    };

    [[nodiscard]] bool Push(const PlatformCommand& InCommand) noexcept;
    [[nodiscard]] bool Pop(PlatformCommand& OutCommand) noexcept;
    void Execute(const PlatformCommand& InCommand) noexcept;

    static void FBY_PLATFORM_CALL OnAchievement(void* InUser, const char* InApiName, int InSuccess);
    static void FBY_PLATFORM_CALL OnCloud(void* InUser, const char* InName, int InSuccess, const void* InData,
                                          std::uint32_t InSize);
    static void FBY_PLATFORM_CALL OnAuth(void* InUser, std::uint64_t InUserId, int InSuccess);
    static void FBY_PLATFORM_CALL OnLog(void* InUser, int InLevel, const char* InMessage);

    const FbyPlatformApi* MApi = nullptr;
    void* MLibrary = nullptr;
    PlatformState MState = PlatformState::Unloaded;
    PlatformServiceCallbacks MGameCallbacks{};
    PlatformServiceStats MStats{};

    std::array<PlatformCommand, kPlatformCommandCapacity> MCommands{};
    std::atomic<std::uint32_t> MHead{0};
    std::atomic<std::uint32_t> MTail{0};
    std::uint32_t MSequence = 0;
};
}
