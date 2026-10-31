// FemboyEngine2D - storefront bridge and ECS systems tests.
#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Platform/PlatformService.hpp"
#include "Engine/Platform/PlatformSystems.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

struct MockState
{
    int initialized = 0;
    int unlocked = 0;
    int clouds = 0;
    int pumps = 0;
    std::uint64_t user = 4242;
    Fby::Platform::PlatformServiceCallbacks* callbackProbe = nullptr;
};

MockState GMockState{};

FbyPlatformResult FBY_PLATFORM_CALL MockInit(const FbyPlatformInitDesc*, const FbyPlatformCallbacks* InCallbacks)
{
    ++GMockState.initialized;
    if (InCallbacks != nullptr && InCallbacks->onAuth != nullptr)
    {
        InCallbacks->onAuth(InCallbacks->user, GMockState.user, 1);
    }
    return FbyPlatformResult_Ok;
}

void FBY_PLATFORM_CALL MockShutdown()
{
}

void FBY_PLATFORM_CALL MockPump()
{
    ++GMockState.pumps;
}

FbyPlatformResult FBY_PLATFORM_CALL MockUnlock(const char* InName)
{
    (void)InName;
    ++GMockState.unlocked;
    return FbyPlatformResult_Ok;
}

FbyPlatformResult FBY_PLATFORM_CALL MockStatInt(const char*, std::int64_t)
{
    return FbyPlatformResult_Ok;
}

FbyPlatformResult FBY_PLATFORM_CALL MockStatFloat(const char*, double)
{
    return FbyPlatformResult_Ok;
}

FbyPlatformResult FBY_PLATFORM_CALL MockStoreStats()
{
    return FbyPlatformResult_Ok;
}

std::uint64_t FBY_PLATFORM_CALL MockUserId()
{
    return GMockState.user;
}

FbyPlatformResult FBY_PLATFORM_CALL MockRead(const char*, void* OutData, std::uint32_t InCapacity,
                                             std::uint32_t* OutSize)
{
    constexpr char kText[] = "cloud-save";
    *OutSize = sizeof(kText) - 1;
    if (InCapacity < *OutSize || OutData == nullptr)
    {
        return FbyPlatformResult_BufferTooSmall;
    }
    std::memcpy(OutData, kText, *OutSize);
    return FbyPlatformResult_Ok;
}

FbyPlatformResult FBY_PLATFORM_CALL MockWrite(const char*, const void*, std::uint32_t)
{
    ++GMockState.clouds;
    return FbyPlatformResult_Ok;
}

FbyPlatformResult FBY_PLATFORM_CALL MockDelete(const char*)
{
    return FbyPlatformResult_Ok;
}

FbyPlatformResult FBY_PLATFORM_CALL MockOverlay(const char*)
{
    return FbyPlatformResult_Ok;
}

std::uint32_t FBY_PLATFORM_CALL MockPending()
{
    return 0;
}

const FbyPlatformApi GMockApi = {
    FBY_PLATFORM_ABI_VERSION,
    static_cast<std::uint32_t>(sizeof(FbyPlatformApi)),
    FbyPlatformKind_Mock,
    "mock",
    FbyPlatformCapability_Achievements | FbyPlatformCapability_Stats | FbyPlatformCapability_CloudSave,
    &MockInit,
    &MockShutdown,
    &MockPump,
    &MockUnlock,
    &MockStatInt,
    &MockStatFloat,
    &MockStoreStats,
    &MockUserId,
    &MockRead,
    &MockWrite,
    &MockDelete,
    &MockOverlay,
    &MockPending,
};

struct LogProbe
{
    std::uint32_t count = 0;
    std::uint64_t lastId = 0;
};

void LogSink(void* InUser, const Fby::Platform::PlatformLogEvent& InEvent)
{
    auto* ScopedProbe = static_cast<LogProbe*>(InUser);
    ++ScopedProbe->count;
    ScopedProbe->lastId = InEvent.id;
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Platform;

    PlatformService ScopedService;
    std::string ScopedError;
    FBY_CHECK(ScopedService.Attach(&GMockApi, &ScopedError));
    FbyPlatformInitDesc ScopedDesc{};
    ScopedDesc.kind = FbyPlatformKind_Mock;
    FBY_CHECK(ScopedService.Initialize(ScopedDesc, &ScopedError));
    FBY_CHECK(ScopedService.Ready());
    FBY_CHECK(ScopedService.UserId() == 4242);

    FBY_CHECK(ScopedService.QueueAchievement(0xA1, "ACH_FIRST"));
    FBY_CHECK(ScopedService.QueueCloudWrite(0xB2, "slot.sav",
                                            std::span<const std::byte>(reinterpret_cast<const std::byte*>("save"), 4)));
    FBY_CHECK(ScopedService.Pending() == 2);
    FBY_CHECK(ScopedService.Pump() == 2);
    FBY_CHECK(GMockState.unlocked == 1);
    FBY_CHECK(GMockState.clouds == 1);
    FBY_CHECK(GMockState.pumps == 1);
    FBY_CHECK(ScopedService.Stats().achievements == 1);

    PlatformCommandLog ScopedLog;
    FBY_CHECK(ScopedLog.Append(PlatformLogType::Achievement, 10, 0xA1, "ACH_FIRST"));
    FBY_CHECK(ScopedLog.Append(PlatformLogType::CloudWrite, 11, 0xB2, "slot.sav",
                               std::span<const std::byte>(reinterpret_cast<const std::byte*>("save"), 4)));
    ScopedLog.Seal();
    LogProbe ScopedProbe{};
    FBY_CHECK(ScopedLog.Replay(&LogSink, &ScopedProbe));
    FBY_CHECK(ScopedProbe.count == 2);
    FBY_CHECK(ScopedProbe.lastId == 0xB2);

    Ecs::Registry ScopedRegistry;
    JobSystem ScopedJobs(1);
    Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.dt = 1.0f / 120.0f;

    SteamAchievementSystem ScopedAchievements;
    ScopedAchievements.Initialize(ScopedService, ScopedLog, 100);
    Ecs::SystemGraph ScopedGraph;
    FBY_CHECK(ScopedAchievements.Register(ScopedGraph, ScopedAchievements));
    FBY_CHECK(ScopedGraph.Build());
    const Entity ScopedPlayer = ScopedRegistry.create();
    AchievementRequest ScopedRequest{};
    ScopedRequest.achievementId = 0xC3;
    ScopedRequest.flags = AchievementPending;
    std::memcpy(ScopedRequest.apiName, "ACH_PLAYER", 11);
    ScopedRegistry.emplace<AchievementRequest>(ScopedPlayer, ScopedRequest);
    ScopedGraph.Run(Ecs::Phase::FixedUpdate, ScopedContext);
    FBY_CHECK(ScopedAchievements.Committed() == 1);
    FBY_CHECK(ScopedService.Pending() == 1);
    FBY_CHECK(ScopedService.Pump() == 1);
    FBY_CHECK(GMockState.unlocked == 2);

    EgsStoreSystem ScopedStore;
    ScopedStore.Initialize(ScopedService, ScopedLog, 200);
    const Entity ScopedSave = ScopedRegistry.create();
    CloudSaveComponent ScopedCloud{};
    ScopedCloud.saveId = 0xD4;
    ScopedCloud.flags = CloudSaveDirty;
    std::memcpy(ScopedCloud.fileName, "slot1.sav", 10);
    ScopedRegistry.emplace<CloudSaveComponent>(ScopedSave, ScopedCloud);
    FBY_CHECK(ScopedStore.StageWrite(0xD4, "slot1.sav",
                                     std::span<const std::byte>(reinterpret_cast<const std::byte*>("save-data"), 9)));
    ScopedStore.Update(ScopedContext);
    FBY_CHECK(ScopedStore.Uploaded() == 1);
    FBY_CHECK(ScopedService.Pending() == 1);
    FBY_CHECK(ScopedService.Pump() == 1);
    FBY_CHECK(GMockState.clouds == 2);

    // An empty log must not replay as a valid one.
    PlatformCommandLog ScopedCorrupt;
    FBY_CHECK(!ScopedCorrupt.Replay(&LogSink, &ScopedProbe));

    // GOG Galaxy. The systems share the binary PlatformCommandLog and the
    GogAchievementSystem ScopedGogAchievements;
    ScopedGogAchievements.Initialize(ScopedService, ScopedLog, 300);
    Ecs::SystemGraph ScopedGogGraph;
    FBY_CHECK(GogAchievementSystem::Register(ScopedGogGraph, ScopedGogAchievements));
    FBY_CHECK(ScopedGogGraph.Build());

    const Entity ScopedGogPlayer = ScopedRegistry.create();
    AchievementRequest ScopedGogRequest{};
    ScopedGogRequest.achievementId = 0xE5;
    ScopedGogRequest.flags = AchievementPending;
    std::memcpy(ScopedGogRequest.apiName, "ACH_GOG", 8);
    ScopedRegistry.emplace<AchievementRequest>(ScopedGogPlayer, ScopedGogRequest);
    const Entity ScopedGogDuplicate = ScopedRegistry.create();
    AchievementRequest ScopedDuplicate{};
    ScopedDuplicate.achievementId = 0xE5;
    ScopedDuplicate.flags = AchievementPending;
    ScopedRegistry.emplace<AchievementRequest>(ScopedGogDuplicate, ScopedDuplicate);

    ScopedGogGraph.Run(Ecs::Phase::PostUpdate, ScopedContext);
    FBY_CHECK(ScopedGogAchievements.Committed() == 1);
    FBY_CHECK(ScopedGogAchievements.DuplicatesSkipped() == 1);
    FBY_CHECK(ScopedService.Pending() == 1);
    FBY_CHECK(ScopedService.Pump() == 1);
    FBY_CHECK(GMockState.unlocked == 3);

    GogStorageSystem ScopedGogStorage;
    ScopedGogStorage.Initialize(ScopedService, ScopedLog, 400);
    FBY_CHECK(ScopedGogStorage.StageWrite(0xF6, "gog.sav",
                                          std::span<const std::byte>(reinterpret_cast<const std::byte*>("gog-save"), 8)));
    FBY_CHECK(ScopedGogStorage.StageWrite(0xF6, "gog.sav",
                                          std::span<const std::byte>(reinterpret_cast<const std::byte*>("gog-save2"), 9)));
    FBY_CHECK(ScopedGogStorage.Conflicts() == 1);
    const Entity ScopedGogSave = ScopedRegistry.create();
    CloudSaveComponent ScopedGogCloud{};
    ScopedGogCloud.saveId = 0xF6;
    ScopedGogCloud.flags = CloudSaveDirty;
    std::memcpy(ScopedGogCloud.fileName, "gog.sav", 8);
    ScopedRegistry.emplace<CloudSaveComponent>(ScopedGogSave, ScopedGogCloud);
    ScopedGogStorage.Update(ScopedContext);
    FBY_CHECK(ScopedGogStorage.Uploaded() == 1);
    FBY_CHECK(ScopedService.Pending() == 1);
    FBY_CHECK(ScopedService.Pump() == 1);
    FBY_CHECK(GMockState.clouds == 3);

#if defined(FBY_GOG_PROVIDER_PATH)
    // The provider is a shared library loaded exactly like a game would load
    PlatformService ScopedGogService;
    std::string ScopedGogError;
    FBY_CHECK(ScopedGogService.Load(FbyPlatformKind_Gog, FBY_GOG_PROVIDER_PATH, &ScopedGogError));
    FBY_CHECK(ScopedGogService.Kind() == FbyPlatformKind_Gog);
    FBY_CHECK(ScopedGogService.ProviderName() == "gog-galaxy");
    FbyPlatformInitDesc ScopedGogDesc{};
    ScopedGogDesc.structSize = static_cast<std::uint32_t>(sizeof(ScopedGogDesc));
    ScopedGogDesc.abiVersion = FBY_PLATFORM_ABI_VERSION;
    ScopedGogDesc.kind = FbyPlatformKind_Gog;
    ScopedGogDesc.clientId = "client-id";
    ScopedGogDesc.clientSecret = "client-secret";
    const bool ScopedGogInit = ScopedGogService.Initialize(ScopedGogDesc, &ScopedGogError);
    FBY_CHECK(ScopedGogInit == ScopedGogService.Ready());
#endif

    if (GFailures == 0)
    {
        std::printf("platform_bridge_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "platform_bridge_test: %u failure(s)\n", GFailures);
    return 1;
}
