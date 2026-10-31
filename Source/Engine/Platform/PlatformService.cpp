// FemboyEngine2D - storefront bridge implementation.
#include "Engine/Platform/PlatformService.hpp"

#include "Engine/Core/Log.hpp"

#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Fby::Platform
{
namespace
{

void CopyName(char* OutName, std::string_view InName) noexcept
{
    const std::size_t ScopedCount = InName.size() < kPlatformMaxNameBytes ? InName.size() : kPlatformMaxNameBytes;
    std::memcpy(OutName, InName.data(), ScopedCount);
    OutName[ScopedCount] = '\0';
}

[[nodiscard]] bool ValidName(std::string_view InName) noexcept
{
    return !InName.empty() && InName.size() <= kPlatformMaxNameBytes && InName.find('\0') == std::string_view::npos;
}
}

PlatformService::~PlatformService()
{
    Unload();
}

bool PlatformService::Load(FbyPlatformKind InKind, std::string_view InLibraryPath, std::string* InError)
{
    Unload();
    if (InLibraryPath.empty())
    {
        if (InError != nullptr)
        {
            *InError = "platform library path is empty";
        }
        return false;
    }
    std::string ScopedPath(InLibraryPath);
#if defined(_WIN32)
    HMODULE ScopedModule = LoadLibraryA(ScopedPath.c_str());
    if (ScopedModule == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "cannot load platform library: " + ScopedPath;
        }
        return false;
    }
    MLibrary = ScopedModule;
    const auto ScopedGetApi =
        reinterpret_cast<FbyPlatformGetApiFn>(GetProcAddress(ScopedModule, "fby_platform_get_api"));
#else
    void* ScopedModule = dlopen(ScopedPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (ScopedModule == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = std::string("cannot load platform library: ") + dlerror();
        }
        return false;
    }
    MLibrary = ScopedModule;
    const auto ScopedGetApi = reinterpret_cast<FbyPlatformGetApiFn>(dlsym(ScopedModule, "fby_platform_get_api"));
#endif
    if (ScopedGetApi == nullptr)
    {
        Unload();
        if (InError != nullptr)
        {
            *InError = "provider exports no fby_platform_get_api";
        }
        return false;
    }
    const FbyPlatformApi* ScopedApi = ScopedGetApi(FBY_PLATFORM_ABI_VERSION);
    if (ScopedApi == nullptr)
    {
        Unload();
        if (InError != nullptr)
        {
            *InError = "provider rejected the engine ABI version";
        }
        return false;
    }
    if (ScopedApi->abiVersion != FBY_PLATFORM_ABI_VERSION ||
        ScopedApi->structSize < static_cast<std::uint32_t>(sizeof(FbyPlatformApi)) || ScopedApi->initialize == nullptr ||
        ScopedApi->shutdown == nullptr || ScopedApi->pump == nullptr)
    {
        Unload();
        if (InError != nullptr)
        {
            *InError = "provider ABI table is incomplete";
        }
        return false;
    }
    if (InKind != FbyPlatformKind_None && ScopedApi->kind != InKind)
    {
        Unload();
        if (InError != nullptr)
        {
            *InError = "provider kind does not match the requested storefront";
        }
        return false;
    }
    MApi = ScopedApi;
    MState = PlatformState::Loaded;
    return true;
}

bool PlatformService::Attach(const FbyPlatformApi* InApi, std::string* InError)
{
    Unload();
    if (InApi == nullptr || InApi->abiVersion != FBY_PLATFORM_ABI_VERSION ||
        InApi->structSize < static_cast<std::uint32_t>(sizeof(FbyPlatformApi)) || InApi->initialize == nullptr ||
        InApi->shutdown == nullptr || InApi->pump == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "attached platform API is invalid";
        }
        return false;
    }
    MApi = InApi;
    MState = PlatformState::Loaded;
    return true;
}

bool PlatformService::Initialize(const FbyPlatformInitDesc& InDesc, std::string* InError)
{
    if (MApi == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "no platform provider loaded";
        }
        return false;
    }
    FbyPlatformInitDesc ScopedDesc = InDesc;
    ScopedDesc.structSize = static_cast<std::uint32_t>(sizeof(FbyPlatformInitDesc));
    ScopedDesc.abiVersion = FBY_PLATFORM_ABI_VERSION;
    if (ScopedDesc.kind == FbyPlatformKind_None)
    {
        ScopedDesc.kind = MApi->kind;
    }
    FbyPlatformCallbacks ScopedCallbacks{};
    ScopedCallbacks.user = this;
    ScopedCallbacks.onAuth = &PlatformService::OnAuth;
    ScopedCallbacks.onAchievement = &PlatformService::OnAchievement;
    ScopedCallbacks.onCloud = &PlatformService::OnCloud;
    ScopedCallbacks.onLog = &PlatformService::OnLog;
    const FbyPlatformResult ScopedResult = MApi->initialize(&ScopedDesc, &ScopedCallbacks);
    if (ScopedResult != FbyPlatformResult_Ok)
    {
        MState = PlatformState::Failed;
        MStats.lastError = ScopedResult;
        if (InError != nullptr)
        {
            *InError = "platform initialize failed: " + std::string(ProviderName());
        }
        return false;
    }
    MState = PlatformState::Ready;
    return true;
}

void PlatformService::Shutdown() noexcept
{
    if (MApi != nullptr && MApi->shutdown != nullptr && MState == PlatformState::Ready)
    {
        MApi->shutdown();
    }
    MState = MApi == nullptr ? PlatformState::Unloaded : PlatformState::Loaded;
}

void PlatformService::Unload() noexcept
{
    Shutdown();
    if (MLibrary != nullptr)
    {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(MLibrary));
#else
        dlclose(MLibrary);
#endif
        MLibrary = nullptr;
    }
    MApi = nullptr;
    MState = PlatformState::Unloaded;
}

std::uint32_t PlatformService::Pump()
{
    if (MApi == nullptr)
    {
        return 0;
    }
    ++MStats.pumps;
    std::uint32_t ScopedExecuted = 0;
    PlatformCommand ScopedCommand{};
    while (ScopedExecuted < kPlatformMaxCommandsPerPump && Pop(ScopedCommand))
    {
        Execute(ScopedCommand);
        ++ScopedExecuted;
        ++MStats.executed;
    }
    if (MApi->pump != nullptr)
    {
        MApi->pump();
    }
    return ScopedExecuted;
}

bool PlatformService::Push(const PlatformCommand& InCommand) noexcept
{
    const std::uint32_t ScopedTail = MTail.load(std::memory_order_relaxed);
    const std::uint32_t ScopedHead = MHead.load(std::memory_order_acquire);
    if (ScopedTail - ScopedHead >= kPlatformCommandCapacity)
    {
        ++MStats.dropped;
        return false;
    }
    MCommands[ScopedTail % kPlatformCommandCapacity] = InCommand;
    MTail.store(ScopedTail + 1, std::memory_order_release);
    ++MStats.queued;
    return true;
}

bool PlatformService::Pop(PlatformCommand& OutCommand) noexcept
{
    const std::uint32_t ScopedHead = MHead.load(std::memory_order_relaxed);
    const std::uint32_t ScopedTail = MTail.load(std::memory_order_acquire);
    if (ScopedHead == ScopedTail)
    {
        return false;
    }
    OutCommand = MCommands[ScopedHead % kPlatformCommandCapacity];
    MHead.store(ScopedHead + 1, std::memory_order_release);
    return true;
}

void PlatformService::Execute(const PlatformCommand& InCommand) noexcept
{
    if (MApi == nullptr)
    {
        return;
    }
    FbyPlatformResult ScopedResult = FbyPlatformResult_NotAvailable;
    switch (InCommand.type)
    {
        case PlatformCommandType::UnlockAchievement:
            if (MApi->unlockAchievement != nullptr)
            {
                ScopedResult = MApi->unlockAchievement(InCommand.name);
                if (ScopedResult == FbyPlatformResult_Ok || ScopedResult == FbyPlatformResult_Pending)
                {
                    ++MStats.achievements;
                    if (MGameCallbacks.onAchievement != nullptr)
                    {
                        MGameCallbacks.onAchievement(MGameCallbacks.user, InCommand.id, InCommand.name, true);
                    }
                }
            }
            break;
        case PlatformCommandType::SetStatInt:
            if (MApi->setStatInt != nullptr)
            {
                ScopedResult = MApi->setStatInt(InCommand.name, InCommand.integer);
            }
            break;
        case PlatformCommandType::SetStatFloat:
            if (MApi->setStatFloat != nullptr)
            {
                ScopedResult = MApi->setStatFloat(InCommand.name, InCommand.number);
            }
            break;
        case PlatformCommandType::StoreStats:
            if (MApi->storeStats != nullptr)
            {
                ScopedResult = MApi->storeStats();
            }
            break;
        case PlatformCommandType::CloudWrite:
            if (MApi->writeCloud != nullptr)
            {
                ScopedResult = MApi->writeCloud(InCommand.name, InCommand.data.data(), InCommand.dataSize);
                if (ScopedResult == FbyPlatformResult_Ok || ScopedResult == FbyPlatformResult_Pending)
                {
                    ++MStats.cloudWrites;
                }
            }
            break;
        case PlatformCommandType::CloudRead:
            if (MApi->readCloud != nullptr)
            {
                std::array<std::byte, kPlatformInlineCloudBytes> ScopedRead{};
                std::uint32_t ScopedRequired = 0;
                ScopedResult = MApi->readCloud(InCommand.name, ScopedRead.data(),
                                               static_cast<std::uint32_t>(ScopedRead.size()), &ScopedRequired);
                if (ScopedResult == FbyPlatformResult_Ok && MGameCallbacks.onCloud != nullptr)
                {
                    ++MStats.cloudReads;
                    MGameCallbacks.onCloud(MGameCallbacks.user, InCommand.name,
                                           std::span<const std::byte>(ScopedRead.data(), ScopedRequired), true);
                }
            }
            break;
        case PlatformCommandType::ShowOverlay:
            if (MApi->showOverlay != nullptr)
            {
                ScopedResult = MApi->showOverlay(InCommand.name);
            }
            break;
    }
    if (ScopedResult != FbyPlatformResult_Ok && ScopedResult != FbyPlatformResult_Pending)
    {
        MStats.lastError = ScopedResult;
    }
}

bool PlatformService::QueueAchievement(std::uint64_t InId, std::string_view InApiName) noexcept
{
    if (!Ready() || !ValidName(InApiName))
    {
        return false;
    }
    PlatformCommand ScopedCommand{};
    ScopedCommand.type = PlatformCommandType::UnlockAchievement;
    ScopedCommand.id = InId;
    ScopedCommand.sequence = ++MSequence;
    CopyName(ScopedCommand.name, InApiName);
    return Push(ScopedCommand);
}

bool PlatformService::QueueStat(std::string_view InApiName, std::int64_t InValue) noexcept
{
    if (!Ready() || !ValidName(InApiName))
    {
        return false;
    }
    PlatformCommand ScopedCommand{};
    ScopedCommand.type = PlatformCommandType::SetStatInt;
    ScopedCommand.integer = InValue;
    ScopedCommand.sequence = ++MSequence;
    CopyName(ScopedCommand.name, InApiName);
    return Push(ScopedCommand);
}

bool PlatformService::QueueStat(std::string_view InApiName, double InValue) noexcept
{
    if (!Ready() || !ValidName(InApiName))
    {
        return false;
    }
    PlatformCommand ScopedCommand{};
    ScopedCommand.type = PlatformCommandType::SetStatFloat;
    ScopedCommand.number = InValue;
    ScopedCommand.sequence = ++MSequence;
    CopyName(ScopedCommand.name, InApiName);
    return Push(ScopedCommand);
}

bool PlatformService::QueueStoreStats() noexcept
{
    if (!Ready())
    {
        return false;
    }
    PlatformCommand ScopedCommand{};
    ScopedCommand.type = PlatformCommandType::StoreStats;
    ScopedCommand.sequence = ++MSequence;
    return Push(ScopedCommand);
}

bool PlatformService::QueueCloudWrite(std::uint64_t InId, std::string_view InName,
                                      std::span<const std::byte> InData) noexcept
{
    if (!Ready() || !ValidName(InName) || InData.size() > kPlatformInlineCloudBytes)
    {
        return false;
    }
    PlatformCommand ScopedCommand{};
    ScopedCommand.type = PlatformCommandType::CloudWrite;
    ScopedCommand.id = InId;
    ScopedCommand.sequence = ++MSequence;
    ScopedCommand.dataSize = static_cast<std::uint32_t>(InData.size());
    CopyName(ScopedCommand.name, InName);
    if (!InData.empty())
    {
        std::memcpy(ScopedCommand.data.data(), InData.data(), InData.size());
    }
    return Push(ScopedCommand);
}

bool PlatformService::QueueCloudRead(std::uint64_t InId, std::string_view InName) noexcept
{
    if (!Ready() || !ValidName(InName))
    {
        return false;
    }
    PlatformCommand ScopedCommand{};
    ScopedCommand.type = PlatformCommandType::CloudRead;
    ScopedCommand.id = InId;
    ScopedCommand.sequence = ++MSequence;
    CopyName(ScopedCommand.name, InName);
    return Push(ScopedCommand);
}

bool PlatformService::QueueOverlay(std::string_view InDialog) noexcept
{
    if (!Ready() || !ValidName(InDialog))
    {
        return false;
    }
    PlatformCommand ScopedCommand{};
    ScopedCommand.type = PlatformCommandType::ShowOverlay;
    ScopedCommand.sequence = ++MSequence;
    CopyName(ScopedCommand.name, InDialog);
    return Push(ScopedCommand);
}

std::string_view PlatformService::ProviderName() const noexcept
{
    return MApi == nullptr || MApi->providerName == nullptr ? std::string_view{} : std::string_view(MApi->providerName);
}

std::uint32_t PlatformService::Pending() const noexcept
{
    const std::uint32_t ScopedTail = MTail.load(std::memory_order_acquire);
    const std::uint32_t ScopedHead = MHead.load(std::memory_order_acquire);
    return ScopedTail - ScopedHead;
}

std::uint64_t PlatformService::UserId() const noexcept
{
    return MApi != nullptr && MApi->userId != nullptr ? MApi->userId() : 0;
}

void FBY_PLATFORM_CALL PlatformService::OnAchievement(void* InUser, const char* InApiName, int InSuccess)
{
    auto* ScopedSelf = static_cast<PlatformService*>(InUser);
    if (ScopedSelf == nullptr || ScopedSelf->MGameCallbacks.onAchievement == nullptr)
    {
        return;
    }
    ScopedSelf->MGameCallbacks.onAchievement(ScopedSelf->MGameCallbacks.user, 0,
                                             InApiName == nullptr ? std::string_view{} : std::string_view(InApiName),
                                             InSuccess != 0);
}

void FBY_PLATFORM_CALL PlatformService::OnCloud(void* InUser, const char* InName, int InSuccess, const void* InData,
                                                std::uint32_t InSize)
{
    auto* ScopedSelf = static_cast<PlatformService*>(InUser);
    if (ScopedSelf == nullptr || ScopedSelf->MGameCallbacks.onCloud == nullptr)
    {
        return;
    }
    const auto* ScopedBytes = static_cast<const std::byte*>(InData);
    ScopedSelf->MGameCallbacks.onCloud(ScopedSelf->MGameCallbacks.user,
                                       InName == nullptr ? std::string_view{} : std::string_view(InName),
                                       InSuccess != 0 && ScopedBytes != nullptr
                                           ? std::span<const std::byte>(ScopedBytes, InSize)
                                           : std::span<const std::byte>{},
                                       InSuccess != 0);
}

void FBY_PLATFORM_CALL PlatformService::OnAuth(void* InUser, std::uint64_t InUserId, int InSuccess)
{
    auto* ScopedSelf = static_cast<PlatformService*>(InUser);
    if (ScopedSelf != nullptr && ScopedSelf->MGameCallbacks.onAuth != nullptr)
    {
        ScopedSelf->MGameCallbacks.onAuth(ScopedSelf->MGameCallbacks.user, InUserId, InSuccess != 0);
    }
}

void FBY_PLATFORM_CALL PlatformService::OnLog(void* InUser, int InLevel, const char* InMessage)
{
    (void)InUser;
    (void)InLevel;
    (void)InMessage;
}
}
