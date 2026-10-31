// FemboyEngine2D - FemboyScriptSystem implementation.
#include "Engine/Script/ScriptSystem.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Script/AotHost.hpp"

#include <array>
#include <string_view>

namespace Fby
{
namespace
{
// Default native functions
void NativeLogFloat(Script::ScriptVm& InVm, Script::VmContext&, std::uint8_t InBase)
{
    LogInfo("script", "log.f32 = {}", static_cast<double>(InVm.F(InBase)));
}

void NativeLogInt(Script::ScriptVm& InVm, Script::VmContext&, std::uint8_t InBase)
{
    LogInfo("script", "log.i32 = {}", InVm.I(InBase));
}

void NativeLogVec2(Script::ScriptVm& InVm, Script::VmContext&, std::uint8_t InBase)
{
    LogInfo("script", "log.vec2 = ({}, {})", static_cast<double>(InVm.F(InBase)),
            static_cast<double>(InVm.F(InBase + 1)));
}

void NativeLogMessage(Script::ScriptVm& InVm, Script::VmContext&, std::uint8_t InBase)
{
    // The message id is a compiler-assigned constant; the string table lives in
    LogInfo("script", "log.message #{}", InVm.U(InBase));
}

void NativeDespawnSelf(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    if (InContext.commands == nullptr)
    {
        InVm.SetU(InBase, 0);
        return;
    }
    const auto ScopedEntity = static_cast<Entity>(InVm.U(InBase));
    if (InContext.registry.valid(ScopedEntity))
    {
        (void)InContext.commands->Destroy(ScopedEntity);
        InVm.SetU(InBase, 1);
    }
    else
    {
        InVm.SetU(InBase, 0);
    }
}

void NativeSpawnPrefab(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    if (InContext.spawn == nullptr)
    {
        InVm.SetU(InBase, static_cast<std::uint32_t>(entt::null));
        return;
    }
    const std::uint32_t ScopedSpawned = InContext.spawn(InContext, InVm.U(InBase));
    InVm.SetU(InBase, ScopedSpawned);
}

void NativeRandomFloat(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    InVm.SetF(InBase, InContext.rng != nullptr ? InContext.rng->NextFloat() : 0.0f);
}

void NativeRandomRange(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    const float ScopedMaximum = InVm.F(InBase + 1);
    InVm.SetF(InBase, InContext.rng != nullptr ? InContext.rng->RangeFloat(0.0f, ScopedMaximum) : 0.0f);
}

void NativePlaySound(Script::ScriptVm& InVm, Script::VmContext&, std::uint8_t InBase)
{
    // Audio bus routing is the audio module's business; the script layer only
    (void)InVm;
    (void)InBase;
}

void NativeGetDeltaTime(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    InVm.SetF(InBase, InContext.dt);
}

void NativeGetFrameIndex(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    InVm.SetU(InBase, static_cast<std::uint32_t>(InContext.frameIndex));
}

// Script tokens are integer floats in the 24-bit mantissa range. The store
// owns the bit layout so the interpreter and AOT host cannot drift.
[[nodiscard]] std::uint32_t PackScriptBigNumber(Fby::BigNum::Handle InHandle) noexcept
{
    return Script::BigNumberPool::PackScript(InHandle);
}

[[nodiscard]] Fby::BigNum::Handle UnpackScriptBigNumber(std::uint32_t InToken) noexcept
{
    return Script::BigNumberPool::UnpackScript(InToken);
}

[[nodiscard]] bool ReadScriptBigNumber(Script::VmContext& InContext, std::uint32_t InToken,
                                      Script::BigNumberPool::Value* OutValue) noexcept
{
    if (InContext.bigNumbers == nullptr || OutValue == nullptr)
    {
        return false;
    }
    return InContext.bigNumbers->Get(UnpackScriptBigNumber(InToken), OutValue);
}

void NativeBigIntFromInt(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    if (InContext.bigNumbers == nullptr)
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    const auto ScopedValue =
        Script::BigNumberPool::Value::FromInt64(static_cast<std::int64_t>(InVm.F(InBase)));
    const auto ScopedHandle = InContext.bigNumbers->Create(ScopedValue);
    InVm.SetF(InBase, static_cast<float>(PackScriptBigNumber(ScopedHandle)));
}

enum class ScriptBigBinaryOp : std::uint8_t
{
    Add = 0,
    Sub,
    Mul,
    Div,
    Mod,
};

void NativeBigIntBinary(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase,
                        ScriptBigBinaryOp InOp)
{
    if (InContext.bigNumbers == nullptr)
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    const auto ScopedLeft = UnpackScriptBigNumber(static_cast<std::uint32_t>(InVm.F(InBase)));
    const auto ScopedRight = UnpackScriptBigNumber(static_cast<std::uint32_t>(InVm.F(InBase + 1)));
    if (!InContext.bigNumbers->IsValid(ScopedLeft) || !InContext.bigNumbers->IsValid(ScopedRight))
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    Fby::BigNum::Handle ScopedResult;
    Fby::BigNum::Handle ScopedRemainder;
    Fby::BigNum::Status ScopedStatus = Fby::BigNum::Status::InvalidArgument;
    switch (InOp)
    {
        case ScriptBigBinaryOp::Add:
            ScopedStatus = InContext.bigNumbers->Add(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case ScriptBigBinaryOp::Sub:
            ScopedStatus = InContext.bigNumbers->Sub(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case ScriptBigBinaryOp::Mul:
            ScopedStatus = InContext.bigNumbers->Mul(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case ScriptBigBinaryOp::Div:
            ScopedStatus = InContext.bigNumbers->DivMod(ScopedLeft, ScopedRight, &ScopedResult, &ScopedRemainder);
            break;
        case ScriptBigBinaryOp::Mod:
            ScopedStatus = InContext.bigNumbers->DivMod(ScopedLeft, ScopedRight, &ScopedRemainder, &ScopedResult);
            break;
    }
    InVm.SetF(InBase, ScopedStatus == Fby::BigNum::Status::Ok
                          ? static_cast<float>(PackScriptBigNumber(ScopedResult))
                          : 0.0f);
}

void NativeBigIntAdd(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    NativeBigIntBinary(InVm, InContext, InBase, ScriptBigBinaryOp::Add);
}

void NativeBigIntSub(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    NativeBigIntBinary(InVm, InContext, InBase, ScriptBigBinaryOp::Sub);
}

void NativeBigIntMul(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    NativeBigIntBinary(InVm, InContext, InBase, ScriptBigBinaryOp::Mul);
}

void NativeBigIntDiv(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    NativeBigIntBinary(InVm, InContext, InBase, ScriptBigBinaryOp::Div);
}

void NativeBigIntMod(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    NativeBigIntBinary(InVm, InContext, InBase, ScriptBigBinaryOp::Mod);
}

void NativeBigIntCmp(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    if (InContext.bigNumbers == nullptr)
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    const auto ScopedLeft = UnpackScriptBigNumber(static_cast<std::uint32_t>(InVm.F(InBase)));
    const auto ScopedRight = UnpackScriptBigNumber(static_cast<std::uint32_t>(InVm.F(InBase + 1)));
    int ScopedComparison = 0;
    const auto ScopedStatus = InContext.bigNumbers->Compare(ScopedLeft, ScopedRight, &ScopedComparison);
    InVm.SetF(InBase, ScopedStatus == Fby::BigNum::Status::Ok ? static_cast<float>(ScopedComparison) : 0.0f);
}

void NativeBigIntToInt(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    Script::BigNumberPool::Value ScopedValue;
    if (!ReadScriptBigNumber(InContext, static_cast<std::uint32_t>(InVm.F(InBase)), &ScopedValue))
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    std::int64_t ScopedInteger = 0;
    if (ScopedValue.ToInt64(&ScopedInteger) != Fby::BigNum::Status::Ok)
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    InVm.SetF(InBase, static_cast<float>(ScopedInteger));
}

void NativeBigIntLog(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    Script::BigNumberPool::Value ScopedValue;
    if (!ReadScriptBigNumber(InContext, static_cast<std::uint32_t>(InVm.F(InBase)), &ScopedValue))
    {
        LogWarn("script", "BigIntLog received an invalid handle");
        return;
    }
    std::array<char, Script::BigNumberPool::Value::kDecimalCapacity> ScopedBuffer{};
    std::size_t ScopedSize = 0;
    if (ScopedValue.FormatPlain(std::span<char>(ScopedBuffer), &ScopedSize) != Fby::BigNum::Status::Ok)
    {
        LogWarn("script", "BigIntLog could not format the value");
        return;
    }
    LogInfo("script", "big = {}", std::string_view(ScopedBuffer.data(), ScopedSize));
}

void NativeBigIntRelease(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    if (InContext.bigNumbers == nullptr)
    {
        return;
    }
    (void)InContext.bigNumbers->Release(UnpackScriptBigNumber(static_cast<std::uint32_t>(InVm.F(InBase))));
}

[[nodiscard]] std::uint32_t PackScriptDecimal(Fby::BigNum::Handle InHandle) noexcept
{
    return Script::DecimalPool::PackScript(InHandle);
}

[[nodiscard]] Fby::BigNum::Handle UnpackScriptDecimal(std::uint32_t InToken) noexcept
{
    return Script::DecimalPool::UnpackScript(InToken);
}

[[nodiscard]] bool ReadScriptDecimal(Script::VmContext& InContext, std::uint32_t InToken,
                                     Script::DecimalPool::Value* OutValue) noexcept
{
    if (InContext.decimals == nullptr || OutValue == nullptr)
    {
        return false;
    }
    return InContext.decimals->Get(UnpackScriptDecimal(InToken), OutValue);
}

void NativeDecimalFromInt(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    if (InContext.decimals == nullptr)
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    const auto ScopedValue =
        Script::DecimalPool::Value::FromInt64(static_cast<std::int64_t>(InVm.F(InBase)));
    const auto ScopedHandle = InContext.decimals->Create(ScopedValue);
    InVm.SetF(InBase, static_cast<float>(PackScriptDecimal(ScopedHandle)));
}

enum class ScriptDecimalOp : std::uint8_t
{
    Add = 0,
    Sub,
    Mul,
    Div,
};

void NativeDecimalBinary(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase,
                         ScriptDecimalOp InOp)
{
    if (InContext.decimals == nullptr)
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    const auto ScopedLeft = UnpackScriptDecimal(static_cast<std::uint32_t>(InVm.F(InBase)));
    const auto ScopedRight = UnpackScriptDecimal(static_cast<std::uint32_t>(InVm.F(InBase + 1)));
    if (!InContext.decimals->IsValid(ScopedLeft) || !InContext.decimals->IsValid(ScopedRight))
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    Fby::BigNum::Handle ScopedResult;
    Fby::BigNum::Status ScopedStatus = Fby::BigNum::Status::InvalidArgument;
    switch (InOp)
    {
        case ScriptDecimalOp::Add:
            ScopedStatus = InContext.decimals->Add(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case ScriptDecimalOp::Sub:
            ScopedStatus = InContext.decimals->Sub(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case ScriptDecimalOp::Mul:
            ScopedStatus = InContext.decimals->Mul(ScopedLeft, ScopedRight, &ScopedResult);
            break;
        case ScriptDecimalOp::Div:
            ScopedStatus = InContext.decimals->Div(ScopedLeft, ScopedRight, &ScopedResult);
            break;
    }
    InVm.SetF(InBase, ScopedStatus == Fby::BigNum::Status::Ok || ScopedStatus == Fby::BigNum::Status::Inexact
                          ? static_cast<float>(PackScriptDecimal(ScopedResult))
                          : 0.0f);
}

void NativeDecimalAdd(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    NativeDecimalBinary(InVm, InContext, InBase, ScriptDecimalOp::Add);
}

void NativeDecimalSub(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    NativeDecimalBinary(InVm, InContext, InBase, ScriptDecimalOp::Sub);
}

void NativeDecimalMul(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    NativeDecimalBinary(InVm, InContext, InBase, ScriptDecimalOp::Mul);
}

void NativeDecimalDiv(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    NativeDecimalBinary(InVm, InContext, InBase, ScriptDecimalOp::Div);
}

void NativeDecimalDivRounded(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    if (InContext.decimals == nullptr)
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    const auto ScopedLeft = UnpackScriptDecimal(static_cast<std::uint32_t>(InVm.F(InBase)));
    const auto ScopedRight = UnpackScriptDecimal(static_cast<std::uint32_t>(InVm.F(InBase + 1)));
    if (!InContext.decimals->IsValid(ScopedLeft) || !InContext.decimals->IsValid(ScopedRight))
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    const int ScopedMode = static_cast<int>(InVm.F(InBase + 2));
    const auto ScopedRounding =
        ScopedMode >= 0 && ScopedMode <= static_cast<int>(Fby::BigNum::RoundingMode::Ceil)
            ? static_cast<Fby::BigNum::RoundingMode>(ScopedMode)
            : Fby::BigNum::RoundingMode::HalfEven;
    Fby::BigNum::Handle ScopedResult;
    const Fby::BigNum::Status ScopedStatus =
        InContext.decimals->Div(ScopedLeft, ScopedRight, &ScopedResult, Fby::BigNum::kDefaultDecimalPrecision,
                                ScopedRounding);
    InVm.SetF(InBase, ScopedStatus == Fby::BigNum::Status::Ok || ScopedStatus == Fby::BigNum::Status::Inexact
                          ? static_cast<float>(PackScriptDecimal(ScopedResult))
                          : 0.0f);
}

void NativeDecimalCmp(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    if (InContext.decimals == nullptr)
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    int ScopedComparison = 0;
    const auto ScopedStatus = InContext.decimals->Compare(
        UnpackScriptDecimal(static_cast<std::uint32_t>(InVm.F(InBase))),
        UnpackScriptDecimal(static_cast<std::uint32_t>(InVm.F(InBase + 1))), &ScopedComparison);
    InVm.SetF(InBase, ScopedStatus == Fby::BigNum::Status::Ok ? static_cast<float>(ScopedComparison) : 0.0f);
}

void NativeDecimalToInt(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    Script::DecimalPool::Value ScopedValue;
    if (!ReadScriptDecimal(InContext, static_cast<std::uint32_t>(InVm.F(InBase)), &ScopedValue))
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    std::int64_t ScopedInteger = 0;
    if (ScopedValue.ToInt64(&ScopedInteger) != Fby::BigNum::Status::Ok)
    {
        InVm.SetF(InBase, 0.0f);
        return;
    }
    InVm.SetF(InBase, static_cast<float>(ScopedInteger));
}

void NativeDecimalLog(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    Script::DecimalPool::Value ScopedValue;
    if (!ReadScriptDecimal(InContext, static_cast<std::uint32_t>(InVm.F(InBase)), &ScopedValue))
    {
        LogWarn("script", "DecimalLog received an invalid handle");
        return;
    }
    LogInfo("script", "decimal = {}", ScopedValue.ToString());
}

void NativeDecimalRelease(Script::ScriptVm& InVm, Script::VmContext& InContext, std::uint8_t InBase)
{
    if (InContext.decimals == nullptr)
    {
        return;
    }
    (void)InContext.decimals->Release(UnpackScriptDecimal(static_cast<std::uint32_t>(InVm.F(InBase))));
}
}

void RegisterScriptableComponents(Script::EcsBridge& InBridge)
{
    // Built-ins a visual graph can read/write today. Every registration is a
    (void)InBridge.RegisterComponent<Transform2D>("Transform2D");
    (void)InBridge.RegisterComponent<Velocity>("Velocity");
    (void)InBridge.RegisterComponent<Sprite2D>("Sprite2D");
    (void)InBridge.RegisterComponent<DrawOrder>("DrawOrder");
    (void)InBridge.RegisterComponent<Lifetime>("Lifetime");
    (void)InBridge.RegisterComponent<ScriptComponent>("ScriptComponent");
}

void RegisterDefaultScriptNatives(ScriptRuntime& InRuntime)
{
    InRuntime.SetNative(Script::NativeId::LogFloat, &NativeLogFloat);
    InRuntime.SetNative(Script::NativeId::LogInt, &NativeLogInt);
    InRuntime.SetNative(Script::NativeId::LogVec2, &NativeLogVec2);
    InRuntime.SetNative(Script::NativeId::LogMessage, &NativeLogMessage);
    InRuntime.SetNative(Script::NativeId::DespawnSelf, &NativeDespawnSelf);
    InRuntime.SetNative(Script::NativeId::SpawnPrefab, &NativeSpawnPrefab);
    InRuntime.SetNative(Script::NativeId::RandomFloat, &NativeRandomFloat);
    InRuntime.SetNative(Script::NativeId::RandomRange, &NativeRandomRange);
    InRuntime.SetNative(Script::NativeId::PlaySound, &NativePlaySound);
    InRuntime.SetNative(Script::NativeId::GetDeltaTime, &NativeGetDeltaTime);
    InRuntime.SetNative(Script::NativeId::GetFrameIndex, &NativeGetFrameIndex);
    InRuntime.SetNative(Script::NativeId::BigIntFromInt, &NativeBigIntFromInt);
    InRuntime.SetNative(Script::NativeId::BigIntAdd, &NativeBigIntAdd);
    InRuntime.SetNative(Script::NativeId::BigIntSub, &NativeBigIntSub);
    InRuntime.SetNative(Script::NativeId::BigIntMul, &NativeBigIntMul);
    InRuntime.SetNative(Script::NativeId::BigIntDiv, &NativeBigIntDiv);
    InRuntime.SetNative(Script::NativeId::BigIntMod, &NativeBigIntMod);
    InRuntime.SetNative(Script::NativeId::BigIntCmp, &NativeBigIntCmp);
    InRuntime.SetNative(Script::NativeId::BigIntToInt, &NativeBigIntToInt);
    InRuntime.SetNative(Script::NativeId::BigIntLog, &NativeBigIntLog);
    InRuntime.SetNative(Script::NativeId::BigIntRelease, &NativeBigIntRelease);
    InRuntime.SetNative(Script::NativeId::DecimalFromInt, &NativeDecimalFromInt);
    InRuntime.SetNative(Script::NativeId::DecimalAdd, &NativeDecimalAdd);
    InRuntime.SetNative(Script::NativeId::DecimalSub, &NativeDecimalSub);
    InRuntime.SetNative(Script::NativeId::DecimalMul, &NativeDecimalMul);
    InRuntime.SetNative(Script::NativeId::DecimalDiv, &NativeDecimalDiv);
    InRuntime.SetNative(Script::NativeId::DecimalDivRounded, &NativeDecimalDivRounded);
    InRuntime.SetNative(Script::NativeId::DecimalCmp, &NativeDecimalCmp);
    InRuntime.SetNative(Script::NativeId::DecimalToInt, &NativeDecimalToInt);
    InRuntime.SetNative(Script::NativeId::DecimalLog, &NativeDecimalLog);
    InRuntime.SetNative(Script::NativeId::DecimalRelease, &NativeDecimalRelease);
}

bool ScriptRuntime::Initialize()
{
    if (MInitialized)
    {
        return true;
    }
    MBridge.Clear();
    RegisterScriptableComponents(MBridge);
    if (MBridge.Count() == 0)
    {
        LogError("script", "no scriptable components registered");
        return false;
    }
    RegisterDefaultScriptNatives(*this);
    MRng = DeterministicRng::FromSeed(0x4642334C4F4C49ull);
    MInitialized = true;
    return true;
}

std::uint16_t ScriptRuntime::LoadProgram(std::span<const std::byte> InCooked, std::string* InError)
{
    if (!MInitialized)
    {
        if (InError != nullptr)
        {
            *InError = "ScriptRuntime is not initialized";
        }
        return static_cast<std::uint16_t>(kScriptNoProgram);
    }

    std::uint32_t ScopedHandle = kScriptNoProgram;
    for (std::uint32_t ScopedI = 0; ScopedI < kScriptMaxPrograms; ++ScopedI)
    {
        if (!MSlots[ScopedI].used)
        {
            ScopedHandle = ScopedI;
            break;
        }
    }
    if (ScopedHandle == kScriptNoProgram)
    {
        if (InError != nullptr)
        {
            *InError = "script program table is full";
        }
        return static_cast<std::uint16_t>(kScriptNoProgram);
    }

    Script::CompiledProgram ScopedParsed;
    if (!Script::ParseProgram(InCooked, &ScopedParsed, InError))
    {
        return static_cast<std::uint16_t>(kScriptNoProgram);
    }

    Slot& ScopedSlot = MSlots[ScopedHandle];
    if (!Script::BindProgram(ScopedParsed, MBridge, &ScopedSlot.bound, InError))
    {
        ScopedSlot = {};
        return static_cast<std::uint16_t>(kScriptNoProgram);
    }
    ScopedSlot.storage = ScopedParsed.storage;
    ScopedSlot.hash = Script::HashProgramBytes(InCooked);
    ScopedSlot.used = true;
    ++MProgramCount;
    MFaultReported[ScopedHandle] = false;
    return static_cast<std::uint16_t>(ScopedHandle);
}

bool ScriptRuntime::UnloadProgram(std::uint32_t InHandle) noexcept
{
    if (InHandle >= kScriptMaxPrograms || !MSlots[InHandle].used)
    {
        return false;
    }
    MSlots[InHandle] = {};
    MFaultReported[InHandle] = false;
    if (MProgramCount > 0)
    {
        --MProgramCount;
    }
    return true;
}

const Script::BoundProgram* ScriptRuntime::Program(std::uint32_t InHandle) const noexcept
{
    if (InHandle >= kScriptMaxPrograms || !MSlots[InHandle].used)
    {
        return nullptr;
    }
    return &MSlots[InHandle].bound;
}

std::uint64_t ScriptRuntime::ProgramHash(std::uint32_t InHandle) const noexcept
{
    if (InHandle >= kScriptMaxPrograms || !MSlots[InHandle].used)
    {
        return 0;
    }
    return MSlots[InHandle].hash;
}

void ScriptRuntime::ReportFault(std::uint32_t InHandle, Entity InEntity, const Script::VmFault& InFault) noexcept
{
    if (InHandle >= kScriptMaxPrograms)
    {
        return;
    }
    if (MFaultReported[InHandle])
    {
        return;
    }
    MFaultReported[InHandle] = true;
    LogError("script", "program #{} faulted on entity {} at pc {}: {} (detail {})", InHandle,
             static_cast<std::uint32_t>(InEntity), InFault.pc, Script::VmFault::Name(InFault.code), InFault.detail);
}

bool RunScriptOnce(ScriptRuntime& InRuntime, std::uint32_t InProgram, Entity InEntity, Script::ScriptEvent InEvent,
                   Script::VmContext& InContext, Script::VmFault* InFault)
{
    const Script::BoundProgram* ScopedBound = InRuntime.Program(InProgram);
    if (ScopedBound == nullptr)
    {
        return false;
    }
    InRuntime.ResetTransientNumbers();
    Script::VmFault ScopedLocalFault;
    const bool ScopedResult = InRuntime.Vm().Run(*ScopedBound, InEvent, InEntity, InContext,
                                                 InFault != nullptr ? InFault : &ScopedLocalFault);
    if (!ScopedResult)
    {
        InRuntime.ReportFault(InProgram, InEntity, InFault != nullptr ? *InFault : ScopedLocalFault);
    }
    return ScopedResult;
}

void FemboyScriptSystem(Ecs::SystemContext& InContext, ScriptRuntime& InRuntime, const Script::AotHost* InAot,
                        bool InAllowVmFallback)
{
    if (!InRuntime.Initialized())
    {
        return;
    }

    Script::VmContext ScopedVmContext{InContext.registry};
    ScopedVmContext.commands = InContext.commands;
    ScopedVmContext.rng = &InRuntime.Rng();
    ScopedVmContext.bigNumbers = &InRuntime.BigNumbers();
    ScopedVmContext.decimals = &InRuntime.Decimals();
    ScopedVmContext.dt = InContext.dt;
    ScopedVmContext.frameIndex = InContext.frameIndex;

    auto ScopedView = InContext.registry.view<ScriptComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        ScriptComponent& ScopedComponent = ScopedView.get<ScriptComponent>(ScopedEntity);
        if (ScopedComponent.Program >= kScriptMaxPrograms)
        {
            continue;
        }
        if ((ScopedComponent.Flags & kScriptFlagEnabled) == 0)
        {
            continue;
        }
        const std::uint64_t ScopedHash = InRuntime.ProgramHash(ScopedComponent.Program);
        const bool ScopedNativized = InAot != nullptr && ScopedHash != 0 && InAot->HasProgram(ScopedHash);
        const Script::BoundProgram* ScopedProgram = InRuntime.Program(ScopedComponent.Program);
        if (!ScopedNativized && ScopedProgram == nullptr)
        {
            continue;
        }
        InRuntime.ResetTransientNumbers();

        if (!ScopedNativized && !InAllowVmFallback)
        {
            // Cooked-build contract: a program without a native module is a build
            // error, not a reason to start the interpreter.
            LogError("script", "program {} ({:016x}) has no AOT module and the VM fallback is disabled", 
                     ScopedComponent.Program, ScopedHash);
            (void)InRuntime.ReportUnnativized();
            ScopedComponent.Flags |= kScriptFlagStarted;
            continue;
        }

        if (ScopedNativized)
        {
            std::string ScopedError;
            if ((ScopedComponent.Flags & kScriptFlagStarted) == 0)
            {
                if (!InAot->Run(ScopedHash, Script::ScriptEvent::Start, ScopedEntity, ScopedVmContext, &ScopedError))
                {
                    LogWarn("script", "AOT start failed for {:016x}: {}", ScopedHash, ScopedError);
                }
                ScopedComponent.Flags |= kScriptFlagStarted;
            }
            (void)InRuntime.AotRun(ScopedComponent.Program);
            if (!InAot->Run(ScopedHash, Script::ScriptEvent::Update, ScopedEntity, ScopedVmContext, &ScopedError))
            {
                LogWarn("script", "AOT update failed for {:016x}: {}", ScopedHash, ScopedError);
            }
            continue;
        }

        if ((ScopedComponent.Flags & kScriptFlagStarted) == 0)
        {
            Script::VmFault ScopedFault;
            if (!InRuntime.Vm().Run(*ScopedProgram, Script::ScriptEvent::Start, ScopedEntity, ScopedVmContext,
                                    &ScopedFault))
            {
                InRuntime.ReportFault(ScopedComponent.Program, ScopedEntity, ScopedFault);
            }
            // Mark started even after a fault: retrying a broken Start every
            ScopedComponent.Flags |= kScriptFlagStarted;
        }

        Script::VmFault ScopedFault;
        if (!InRuntime.Vm().Run(*ScopedProgram, Script::ScriptEvent::Update, ScopedEntity, ScopedVmContext,
                                &ScopedFault))
        {
            InRuntime.ReportFault(ScopedComponent.Program, ScopedEntity, ScopedFault);
        }
    }
}
}
