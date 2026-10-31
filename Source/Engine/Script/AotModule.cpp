// FemboyEngine2D - AOT script plugin loader.
#include "Engine/Script/AotModule.hpp"

#include "Engine/Core/Log.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Fby::Script
{
namespace
{
struct HostUser
{
    VmContext* context = nullptr;
    const EcsBridge* bridge = nullptr;
    // Fault telemetry: the last fault a generated entry point reported.
    std::uint32_t faultCode = 0;
    std::uint32_t faultPc = 0;
    std::uint32_t faultCount = 0;

    [[nodiscard]] bool Faulted() const noexcept
    {
        return faultCount > 0;
    }
};

void SetError(std::string* InError, const std::string& InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}

// Host callbacks. They are free functions so the plugin ABI is plain C; the
void* HostComponent(void* InComponentUser, void* InRegistry, std::uint32_t InEntity,
                    std::uint64_t InLayoutHash) noexcept
{
    const auto* ScopedBridge = static_cast<const EcsBridge*>(InComponentUser);
    if (ScopedBridge == nullptr || InRegistry == nullptr)
    {
        return nullptr;
    }
    const ComponentAccessor* ScopedAccessor = ScopedBridge->Find(InLayoutHash);
    if (ScopedAccessor == nullptr || ScopedAccessor->get == nullptr)
    {
        return nullptr;
    }
    auto& ScopedTyped = *static_cast<Ecs::Registry*>(InRegistry);
    return ScopedAccessor->get(ScopedTyped, static_cast<Entity>(InEntity));
}

std::uint32_t HostDespawn(void* InUser, std::uint32_t InEntity) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->commands == nullptr)
    {
        return 0u;
    }
    const Entity ScopedTyped = static_cast<Entity>(InEntity);
    if (!ScopedHost->context->registry.valid(ScopedTyped))
    {
        return 0u;
    }
    (void)ScopedHost->context->commands->Destroy(ScopedTyped);
    return 1u;
}

std::uint32_t HostSpawn(void* InUser, std::uint32_t InPrefabId) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->spawn == nullptr)
    {
        return static_cast<std::uint32_t>(entt::null);
    }
    return ScopedHost->context->spawn(*ScopedHost->context, InPrefabId);
}

float HostRandomFloat(void* InUser) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    return (ScopedHost != nullptr && ScopedHost->context != nullptr && ScopedHost->context->rng != nullptr)
               ? ScopedHost->context->rng->NextFloat()
               : 0.0f;
}

void HostLogFloat(void*, float InValue) noexcept
{
    LogInfo("aot", "log.f32 = {}", static_cast<double>(InValue));
}

void HostLogU32(void*, std::uint32_t InValue) noexcept
{
    LogInfo("aot", "log.u32 = {}", InValue);
}

void HostLogVec2(void*, float InX, float InY) noexcept
{
    LogInfo("aot", "log.vec2 = ({}, {})", static_cast<double>(InX), static_cast<double>(InY));
}

void HostLogI32(void*, std::int32_t InValue) noexcept
{
    LogInfo("aot", "log.i32 = {}", InValue);
}

enum class HostBigBinaryOp : std::uint8_t
{
    Add = 0,
    Sub,
    Mul,
    Div,
    Mod,
};

std::uint32_t HostBigIntFromInt(void* InUser, std::int32_t InValue) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->bigNumbers == nullptr)
    {
        return 0u;
    }
    const auto ScopedValue = BigNumberPool::Value::FromInt64(InValue);
    const auto ScopedHandle = ScopedHost->context->bigNumbers->Create(ScopedValue);
    return BigNumberPool::PackScript(ScopedHandle);
}

std::uint32_t HostBigIntBinary(void* InUser, std::uint32_t InLeft, std::uint32_t InRight,
                               HostBigBinaryOp InOp) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->bigNumbers == nullptr)
    {
        return 0u;
    }
    const auto ScopedLeft = BigNumberPool::UnpackScript(InLeft);
    const auto ScopedRight = BigNumberPool::UnpackScript(InRight);
    if (!ScopedHost->context->bigNumbers->IsValid(ScopedLeft) ||
        !ScopedHost->context->bigNumbers->IsValid(ScopedRight))
    {
        return 0u;
    }
    BigNum::Handle ScopedResult;
    BigNum::Handle ScopedRemainder;
    BigNum::Status ScopedStatus = BigNum::Status::InvalidArgument;
    switch (InOp)
    {
        case HostBigBinaryOp::Add:
            ScopedStatus = ScopedHost->context->bigNumbers->Add(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case HostBigBinaryOp::Sub:
            ScopedStatus = ScopedHost->context->bigNumbers->Sub(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case HostBigBinaryOp::Mul:
            ScopedStatus = ScopedHost->context->bigNumbers->Mul(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case HostBigBinaryOp::Div:
            ScopedStatus =
                ScopedHost->context->bigNumbers->DivMod(ScopedLeft, ScopedRight, &ScopedResult, &ScopedRemainder);
            break;
        case HostBigBinaryOp::Mod:
            ScopedStatus =
                ScopedHost->context->bigNumbers->DivMod(ScopedLeft, ScopedRight, &ScopedRemainder, &ScopedResult);
            break;
    }
    return ScopedStatus == BigNum::Status::Ok ? BigNumberPool::PackScript(ScopedResult) : 0u;
}

std::uint32_t HostBigIntAdd(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    return HostBigIntBinary(InUser, InLeft, InRight, HostBigBinaryOp::Add);
}

std::uint32_t HostBigIntSub(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    return HostBigIntBinary(InUser, InLeft, InRight, HostBigBinaryOp::Sub);
}

std::uint32_t HostBigIntMul(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    return HostBigIntBinary(InUser, InLeft, InRight, HostBigBinaryOp::Mul);
}

std::uint32_t HostBigIntDiv(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    return HostBigIntBinary(InUser, InLeft, InRight, HostBigBinaryOp::Div);
}

std::uint32_t HostBigIntMod(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    return HostBigIntBinary(InUser, InLeft, InRight, HostBigBinaryOp::Mod);
}

std::int32_t HostBigIntCmp(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->bigNumbers == nullptr)
    {
        return 0;
    }
    int ScopedComparison = 0;
    if (ScopedHost->context->bigNumbers->Compare(BigNumberPool::UnpackScript(InLeft),
                                                 BigNumberPool::UnpackScript(InRight), &ScopedComparison) !=
        BigNum::Status::Ok)
    {
        return 0;
    }
    return ScopedComparison;
}

std::int64_t HostBigIntToInt(void* InUser, std::uint32_t InHandle) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->bigNumbers == nullptr)
    {
        return 0;
    }
    BigNumberPool::Value ScopedValue;
    if (!ScopedHost->context->bigNumbers->Get(BigNumberPool::UnpackScript(InHandle), &ScopedValue))
    {
        return 0;
    }
    std::int64_t ScopedInteger = 0;
    return ScopedValue.ToInt64(&ScopedInteger) == BigNum::Status::Ok ? ScopedInteger : 0;
}

void HostBigIntLog(void* InUser, std::uint32_t InHandle) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->bigNumbers == nullptr)
    {
        return;
    }
    BigNumberPool::Value ScopedValue;
    if (!ScopedHost->context->bigNumbers->Get(BigNumberPool::UnpackScript(InHandle), &ScopedValue))
    {
        LogWarn("aot", "big log received an invalid handle");
        return;
    }
    std::array<char, BigNumberPool::Value::kDecimalCapacity> ScopedBuffer{};
    std::size_t ScopedSize = 0;
    if (ScopedValue.FormatPlain(std::span<char>(ScopedBuffer), &ScopedSize) != BigNum::Status::Ok)
    {
        LogWarn("aot", "big log could not format the value");
        return;
    }
    LogInfo("aot", "big = {}", std::string_view(ScopedBuffer.data(), ScopedSize));
}

void HostBigIntRelease(void* InUser, std::uint32_t InHandle) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->bigNumbers == nullptr)
    {
        return;
    }
    (void)ScopedHost->context->bigNumbers->Release(BigNumberPool::UnpackScript(InHandle));
}

enum class HostDecimalOp : std::uint8_t
{
    Add = 0,
    Sub,
    Mul,
    Div,
};

std::uint32_t HostDecimalFromInt(void* InUser, std::int32_t InValue) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->decimals == nullptr)
    {
        return 0u;
    }
    const auto ScopedValue = DecimalPool::Value::FromInt64(InValue);
    const auto ScopedHandle = ScopedHost->context->decimals->Create(ScopedValue);
    return DecimalPool::PackScript(ScopedHandle);
}

std::uint32_t HostDecimalBinary(void* InUser, std::uint32_t InLeft, std::uint32_t InRight,
                                HostDecimalOp InOp) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->decimals == nullptr)
    {
        return 0u;
    }
    const auto ScopedLeft = DecimalPool::UnpackScript(InLeft);
    const auto ScopedRight = DecimalPool::UnpackScript(InRight);
    if (!ScopedHost->context->decimals->IsValid(ScopedLeft) ||
        !ScopedHost->context->decimals->IsValid(ScopedRight))
    {
        return 0u;
    }
    BigNum::Handle ScopedResult;
    BigNum::Status ScopedStatus = BigNum::Status::InvalidArgument;
    switch (InOp)
    {
        case HostDecimalOp::Add:
            ScopedStatus = ScopedHost->context->decimals->Add(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case HostDecimalOp::Sub:
            ScopedStatus = ScopedHost->context->decimals->Sub(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case HostDecimalOp::Mul:
            ScopedStatus = ScopedHost->context->decimals->Mul(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case HostDecimalOp::Div:
            ScopedStatus = ScopedHost->context->decimals->Div(ScopedLeft, ScopedRight, &ScopedResult);
            break;
    }
    return ScopedStatus == BigNum::Status::Ok || ScopedStatus == BigNum::Status::Inexact
               ? DecimalPool::PackScript(ScopedResult)
               : 0u;
}

std::uint32_t HostDecimalAdd(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    return HostDecimalBinary(InUser, InLeft, InRight, HostDecimalOp::Add);
}

std::uint32_t HostDecimalSub(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    return HostDecimalBinary(InUser, InLeft, InRight, HostDecimalOp::Sub);
}

std::uint32_t HostDecimalMul(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    return HostDecimalBinary(InUser, InLeft, InRight, HostDecimalOp::Mul);
}

std::uint32_t HostDecimalDiv(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    return HostDecimalBinary(InUser, InLeft, InRight, HostDecimalOp::Div);
}

std::uint32_t HostDecimalDivRounded(void* InUser, std::uint32_t InLeft, std::uint32_t InRight,
                                    std::int32_t InRounding) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->decimals == nullptr)
    {
        return 0u;
    }
    const auto ScopedLeft = DecimalPool::UnpackScript(InLeft);
    const auto ScopedRight = DecimalPool::UnpackScript(InRight);
    if (!ScopedHost->context->decimals->IsValid(ScopedLeft) ||
        !ScopedHost->context->decimals->IsValid(ScopedRight))
    {
        return 0u;
    }
    const auto ScopedRounding =
        InRounding >= 0 && InRounding <= static_cast<std::int32_t>(BigNum::RoundingMode::Ceil)
            ? static_cast<BigNum::RoundingMode>(InRounding)
            : BigNum::RoundingMode::HalfEven;
    BigNum::Handle ScopedResult;
    const BigNum::Status ScopedStatus = ScopedHost->context->decimals->Div(
        ScopedLeft, ScopedRight, &ScopedResult, BigNum::kDefaultDecimalPrecision, ScopedRounding);
    return ScopedStatus == BigNum::Status::Ok || ScopedStatus == BigNum::Status::Inexact
               ? DecimalPool::PackScript(ScopedResult)
               : 0u;
}

std::int32_t HostDecimalCmp(void* InUser, std::uint32_t InLeft, std::uint32_t InRight) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->decimals == nullptr)
    {
        return 0;
    }
    int ScopedComparison = 0;
    if (ScopedHost->context->decimals->Compare(DecimalPool::UnpackScript(InLeft),
                                               DecimalPool::UnpackScript(InRight), &ScopedComparison) !=
        BigNum::Status::Ok)
    {
        return 0;
    }
    return ScopedComparison;
}

std::int64_t HostDecimalToInt(void* InUser, std::uint32_t InHandle) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->decimals == nullptr)
    {
        return 0;
    }
    DecimalPool::Value ScopedValue;
    if (!ScopedHost->context->decimals->Get(DecimalPool::UnpackScript(InHandle), &ScopedValue))
    {
        return 0;
    }
    std::int64_t ScopedInteger = 0;
    return ScopedValue.ToInt64(&ScopedInteger) == BigNum::Status::Ok ? ScopedInteger : 0;
}

void HostDecimalLog(void* InUser, std::uint32_t InHandle) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->decimals == nullptr)
    {
        return;
    }
    DecimalPool::Value ScopedValue;
    if (!ScopedHost->context->decimals->Get(DecimalPool::UnpackScript(InHandle), &ScopedValue))
    {
        LogWarn("aot", "decimal log received an invalid handle");
        return;
    }
    LogInfo("aot", "decimal = {}", ScopedValue.ToString());
}

void HostDecimalRelease(void* InUser, std::uint32_t InHandle) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->context == nullptr || ScopedHost->context->decimals == nullptr)
    {
        return;
    }
    (void)ScopedHost->context->decimals->Release(DecimalPool::UnpackScript(InHandle));
}

void HostFault(void* InUser, std::uint32_t InCode, std::uint32_t InPc) noexcept
{
    auto* ScopedHost = static_cast<HostUser*>(InUser);
    if (ScopedHost != nullptr)
    {
        ScopedHost->faultCode = InCode;
        ScopedHost->faultPc = InPc;
        ++ScopedHost->faultCount;
    }
    LogWarn("aot", "script fault code {} at pc {}", InCode, InPc);
}

// The generated code and the interpreter must agree on the numbers a plugin
// reports, otherwise telemetry cannot merge VM and AOT faults.
static_assert(Aot::FaultCode::MissingComponent == static_cast<std::uint32_t>(VmFault::Code::MissingComponent),
              "AOT fault code must mirror VmFault::Code");
static_assert(Aot::FaultCode::BadField == static_cast<std::uint32_t>(VmFault::Code::BadField),
              "AOT fault code must mirror VmFault::Code");
static_assert(Aot::FaultCode::DivisionByZero == static_cast<std::uint32_t>(VmFault::Code::DivisionByZero),
              "AOT fault code must mirror VmFault::Code");
}

bool ValidateAotModule(const Aot::Module& InModule, const EcsBridge& InBridge, std::string* InError)
{
    if (InModule.magic != Aot::kMagic)
    {
        SetError(InError, "AOT module has a bad magic (not a cooked FemboyScript plugin)");
        return false;
    }
    if (InModule.abiVersion != Aot::kAbiVersion)
    {
        SetError(InError, "AOT module ABI version mismatch");
        return false;
    }
    if (InModule.componentCount > Aot::kMaxModuleComponents)
    {
        SetError(InError, "AOT module declares more components than the ABI allows");
        return false;
    }
    if (InModule.componentCount > 0 && InModule.componentHashes == nullptr)
    {
        SetError(InError, "AOT module declares components but no LayoutHash table");
        return false;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < InModule.componentCount; ++ScopedI)
    {
        if (InBridge.Find(InModule.componentHashes[ScopedI]) == nullptr)
        {
            char ScopedBuffer[192] = {};
            std::snprintf(ScopedBuffer, sizeof(ScopedBuffer),
                          "AOT module expects a component with LayoutHash 0x%016llX that this build does not have",
                          static_cast<unsigned long long>(InModule.componentHashes[ScopedI]));
            SetError(InError, ScopedBuffer);
            return false;
        }
    }
    if (InModule.start == nullptr && InModule.update == nullptr && InModule.fixedUpdate == nullptr)
    {
        SetError(InError, "AOT module exports no event entry point");
        return false;
    }
    return true;
}

bool RunAotModule(const Aot::Module& InModule, ScriptEvent InEvent, Entity InSelf, VmContext& InContext,
                  const EcsBridge& InBridge, std::string* InError)
{
    if (!ValidateAotModule(InModule, InBridge, InError))
    {
        return false;
    }
    void (*ScopedEntry)(Aot::Context&) noexcept = nullptr;
    switch (InEvent)
    {
        case ScriptEvent::Start:
            ScopedEntry = InModule.start;
            break;
        case ScriptEvent::Update:
            ScopedEntry = InModule.update;
            break;
        case ScriptEvent::FixedUpdate:
            ScopedEntry = InModule.fixedUpdate;
            break;
        default:
            break;
    }
    if (ScopedEntry == nullptr)
    {
        SetError(InError, "AOT module does not implement this event");
        return false;
    }

    HostUser ScopedUser;
    ScopedUser.context = &InContext;
    ScopedUser.bridge = &InBridge;

    Aot::HostApi ScopedHost;
    ScopedHost.componentUser = const_cast<EcsBridge*>(&InBridge);
    ScopedHost.component = &HostComponent;
    ScopedHost.despawn = &HostDespawn;
    ScopedHost.spawn = &HostSpawn;
    ScopedHost.randomFloat = &HostRandomFloat;
    ScopedHost.logFloat = &HostLogFloat;
    ScopedHost.logU32 = &HostLogU32;
    ScopedHost.logVec2 = &HostLogVec2;
    ScopedHost.logI32 = &HostLogI32;
    ScopedHost.bigIntFromInt = &HostBigIntFromInt;
    ScopedHost.bigIntAdd = &HostBigIntAdd;
    ScopedHost.bigIntSub = &HostBigIntSub;
    ScopedHost.bigIntMul = &HostBigIntMul;
    ScopedHost.bigIntDiv = &HostBigIntDiv;
    ScopedHost.bigIntMod = &HostBigIntMod;
    ScopedHost.bigIntCmp = &HostBigIntCmp;
    ScopedHost.bigIntToInt = &HostBigIntToInt;
    ScopedHost.bigIntLog = &HostBigIntLog;
    ScopedHost.bigIntRelease = &HostBigIntRelease;
    ScopedHost.decimalFromInt = &HostDecimalFromInt;
    ScopedHost.decimalAdd = &HostDecimalAdd;
    ScopedHost.decimalSub = &HostDecimalSub;
    ScopedHost.decimalMul = &HostDecimalMul;
    ScopedHost.decimalDiv = &HostDecimalDiv;
    ScopedHost.decimalDivRounded = &HostDecimalDivRounded;
    ScopedHost.decimalCmp = &HostDecimalCmp;
    ScopedHost.decimalToInt = &HostDecimalToInt;
    ScopedHost.decimalLog = &HostDecimalLog;
    ScopedHost.decimalRelease = &HostDecimalRelease;
    ScopedHost.fault = &HostFault;

    Aot::Context ScopedAotContext;
    ScopedAotContext.host = &ScopedHost;
    ScopedAotContext.registry = &InContext.registry;
    ScopedAotContext.user = &ScopedUser;
    ScopedAotContext.dt = InContext.dt;
    ScopedAotContext.frameIndex = InContext.frameIndex;
    ScopedAotContext.self = static_cast<std::uint32_t>(InSelf);
    ScopedEntry(ScopedAotContext);
    return true;
}

AotModule::~AotModule()
{
    Unload();
}

const Aot::Module* AotModule::ValidateLoaded(const EcsBridge& InBridge, std::string* InError) noexcept
{
    if (MHandle == nullptr)
    {
        SetError(InError, "AOT module is not loaded");
        return nullptr;
    }
#if defined(_WIN32)
    using ModuleEntry = const Aot::Module* (*)();
    const ModuleEntry ScopedEntry = reinterpret_cast<ModuleEntry>(
        reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(MHandle), "fby_aot_module")));
#else
    using ModuleEntry = const Aot::Module* (*)();
    const ModuleEntry ScopedEntry = reinterpret_cast<ModuleEntry>(dlsym(MHandle, "fby_aot_module"));
#endif
    if (ScopedEntry == nullptr)
    {
        SetError(InError, "AOT module does not export fby_aot_module");
        return nullptr;
    }
    const Aot::Module* ScopedModule = ScopedEntry();
    if (ScopedModule == nullptr || !ValidateAotModule(*ScopedModule, InBridge, InError))
    {
        return nullptr;
    }
    return ScopedModule;
}

bool AotModule::Load(const char* InPath, const EcsBridge& InBridge, std::string* InError)
{
    Unload();
    if (InPath == nullptr || InPath[0] == '\0')
    {
        SetError(InError, "AOT module path is empty");
        return false;
    }
#if defined(_WIN32)
    MHandle = LoadLibraryA(InPath);
    if (MHandle == nullptr)
    {
        SetError(InError, std::string("cannot load AOT module '") + InPath + "'");
        return false;
    }
#else
    MHandle = dlopen(InPath, RTLD_NOW | RTLD_LOCAL);
    if (MHandle == nullptr)
    {
        SetError(InError, std::string("cannot load AOT module '") + InPath + "': " + dlerror());
        return false;
    }
#endif
    MModule = ValidateLoaded(InBridge, InError);
    if (MModule == nullptr)
    {
        Unload();
        return false;
    }
    MBridge = &InBridge;
    LogInfo("aot", "loaded script plugin '{}' (graph {})", InPath, MModule->graphHash);
    return true;
}

void AotModule::Unload() noexcept
{
    MModule = nullptr;
    MBridge = nullptr;
    if (MHandle == nullptr)
    {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(MHandle));
#else
    dlclose(MHandle);
#endif
    MHandle = nullptr;
}

bool AotModule::HasEvent(ScriptEvent InEvent) const noexcept
{
    if (MModule == nullptr)
    {
        return false;
    }
    switch (InEvent)
    {
        case ScriptEvent::Start:
            return MModule->start != nullptr;
        case ScriptEvent::Update:
            return MModule->update != nullptr;
        case ScriptEvent::FixedUpdate:
            return MModule->fixedUpdate != nullptr;
        default:
            return false;
    }
}

bool AotModule::Run(ScriptEvent InEvent, Entity InSelf, VmContext& InContext, std::string* InError) const
{
    if (MModule == nullptr || MBridge == nullptr)
    {
        SetError(InError, "AOT module is not loaded");
        return false;
    }
    return RunAotModule(*MModule, InEvent, InSelf, InContext, *MBridge, InError);
}
}
