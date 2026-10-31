// FemboyEngine2D - FemboyScript 2.0 interpreter.
#include "Engine/Script/Vm.hpp"

#include <bit>
#include <cstring>
#include <limits>

namespace Fby::Script
{
namespace
{
[[nodiscard]] constexpr std::uint32_t Bits(Entity InEntity) noexcept
{
    return static_cast<std::uint32_t>(InEntity);
}

template <typename T> [[nodiscard]] T LoadRaw(const std::byte* InAddress) noexcept
{
    T ScopedValue{};
    std::memcpy(&ScopedValue, InAddress, sizeof(T));
    return ScopedValue;
}

void StoreRaw(std::byte* InAddress, const void* InValue, std::uint32_t InSize) noexcept
{
    std::memcpy(InAddress, InValue, InSize);
}
}

bool ScriptVm::Fault(VmFault* InFault, VmFault::Code InCode, std::uint32_t InPc, std::uint32_t InDetail) noexcept
{
    if (InFault != nullptr)
    {
        InFault->code = InCode;
        InFault->pc = InPc;
        InFault->detail = InDetail;
    }
    return false;
}

bool ScriptVm::Run(const BoundProgram& InProgram, ScriptEvent InEvent, Entity InSelf, VmContext& InContext,
                   VmFault* InFault) noexcept
{
    if (InFault != nullptr)
    {
        *InFault = {};
    }
    const VmStopInfo ScopedStop = Execute(InProgram, InEvent, InSelf, InContext, InFault, nullptr);
    return ScopedStop.Halt != VmHalt::Faulted;
}

VmStopInfo ScriptVm::RunDebug(const BoundProgram& InProgram, ScriptEvent InEvent, Entity InSelf,
                              VmContext& InContext, VmDebugState& InDebug) noexcept
{
    VmFault ScopedFault{};
    VmDebugState* ScopedDebug = InDebug.Enabled ? &InDebug : nullptr;
    const VmStopInfo ScopedStop = Execute(InProgram, InEvent, InSelf, InContext, &ScopedFault, ScopedDebug);
    if (InDebug.Enabled && ScopedStop.Halt == VmHalt::Faulted)
    {
        InDebug.Paused = false;
        InDebug.ResumeValid = false;
        InDebug.LastReason = VmPauseReason::None;
    }
    return ScopedStop;
}

void ScriptVm::RequestPauseAtEntry(VmDebugState& InDebug) noexcept
{
    InDebug.Paused = false;
    InDebug.ResumeValid = false;
    InDebug.SkipOnce = false;
    InDebug.PauseRequested = true;
    InDebug.StepRequested = false;
    InDebug.StepBudget = 0;
    InDebug.LastReason = VmPauseReason::None;
}

VmStopInfo ScriptVm::FaultStop(VmFault* InFault, VmFault::Code InCode, std::uint32_t InPc, std::uint32_t InDetail) noexcept
{
    (void)Fault(InFault, InCode, InPc, InDetail);
    VmStopInfo ScopedStop;
    ScopedStop.Halt = VmHalt::Faulted;
    ScopedStop.Pc = InPc;
    if (InFault != nullptr)
    {
        ScopedStop.Fault = *InFault;
    }
    return ScopedStop;
}

VmStopInfo ScriptVm::PauseStop(VmDebugState& InDebug, VmPauseReason InReason) noexcept
{
    InDebug.Paused = true;
    InDebug.ResumeValid = true;
    InDebug.ResumePc = InDebug.CurrentPc;
    InDebug.PausePc = InDebug.CurrentPc;
    InDebug.LastReason = InReason;
    InDebug.SkipOnce = true;
    InDebug.SkipPc = InDebug.CurrentPc;
    ++InDebug.PauseCount;
    VmStopInfo ScopedStop;
    ScopedStop.Halt = VmHalt::Paused;
    ScopedStop.Pc = InDebug.CurrentPc;
    ScopedStop.Reason = InReason;
    return ScopedStop;
}

VmStopInfo ScriptVm::Execute(const BoundProgram& InProgram, ScriptEvent InEvent, Entity InSelf, VmContext& InContext,
                             VmFault* InFault, VmDebugState* InDebug) noexcept
{
    VmStopInfo ScopedStop;
    if (InFault != nullptr)
    {
        *InFault = {};
    }
    if (!InProgram.valid || !InProgram.program.valid)
    {
        return FaultStop(InFault, VmFault::Code::InvalidProgram, 0);
    }

    const FunctionEntry* ScopedEntry = InProgram.program.FindFunction(InEvent);
    if (ScopedEntry == nullptr)
    {
        // A graph that only defines Update is valid; there is nothing to run.
        if (InDebug != nullptr)
        {
            InDebug->Paused = false;
            InDebug->ResumeValid = false;
        }
        return ScopedStop;
    }

    const std::uint32_t ScopedRegisters = InProgram.program.header.registerHighWater;
    const bool ScopedResume = InDebug != nullptr && InDebug->ResumeValid;
    if (!ScopedResume)
    {
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedRegisters; ++ScopedI)
        {
            MRegisters[ScopedI].u = 0;
        }
    }
    if (InDebug != nullptr)
    {
        InDebug->ResumeValid = false;
    }

    const auto& ScopedCode = InProgram.program.code;
    const auto& ScopedConstants = InProgram.program.constants;
    std::uint32_t ScopedPc = ScopedResume ? InDebug->ResumePc : ScopedEntry->codeOffset;
    std::uint32_t ScopedSteps = 0;

    // One lambda, not std::function: the compiler inlines both field helpers
    const auto ScopedFieldAddress = [&InProgram, &InContext](const FieldBinding& InField, Entity InEntity,
                                                             void** Out) noexcept -> bool
    {
        if (InField.component >= InProgram.componentCount)
        {
            return false;
        }
        const ResolvedComponent& ScopedResolved = InProgram.components[InField.component];
        if (!ScopedResolved.valid || ScopedResolved.get == nullptr)
        {
            return false;
        }
        void* ScopedBase = ScopedResolved.get(InContext.registry, InEntity);
        if (ScopedBase == nullptr)
        {
            return false;
        }
        *Out = static_cast<std::byte*>(ScopedBase) + InField.offset;
        return true;
    };

    while (ScopedPc < ScopedCode.size())
    {
        if (InDebug != nullptr)
        {
            InDebug->CurrentPc = ScopedPc;
            const bool ScopedSkip = InDebug->SkipOnce && InDebug->SkipPc == ScopedPc;
            if (ScopedSkip)
            {
                InDebug->SkipOnce = false;
            }
            if (InDebug->PauseRequested)
            {
                InDebug->PauseRequested = false;
                return PauseStop(*InDebug, VmPauseReason::Request);
            }
            if (InDebug->StepRequested)
            {
                InDebug->StepRequested = false;
                return PauseStop(*InDebug, VmPauseReason::Step);
            }
            if (!ScopedSkip && InDebug->HasBreakpoint(ScopedPc))
            {
                return PauseStop(*InDebug, VmPauseReason::Breakpoint);
            }
        }
        if (++ScopedSteps > kMaxStepsPerRun)
        {
            return FaultStop(InFault, VmFault::Code::StepLimit, ScopedPc);
        }
        const Instruction ScopedInstruction = ScopedCode[ScopedPc];
        ++MExecuted;
        std::uint32_t ScopedNext = ScopedPc + 1;

        switch (ScopedInstruction.Code())
        {
            case OpCode::Nop:
                break;
            case OpCode::Halt:
            {
                ScopedStop.Halt = VmHalt::Completed;
                ScopedStop.Pc = ScopedPc;
                if (InDebug != nullptr)
                {
                    InDebug->Paused = false;
                    InDebug->ResumeValid = false;
                }
                return ScopedStop;
            }

            case OpCode::LoadF32:
                MRegisters[ScopedInstruction.dst].f = ScopedConstants[ScopedInstruction.operand];
                break;
            case OpCode::LoadI32:
                // Signed 16-bit immediate: the bytecode can therefore encode a
                // negative constant, and the AOT backend emits the same value.
                MRegisters[ScopedInstruction.dst].i =
                    static_cast<std::int32_t>(static_cast<std::int16_t>(ScopedInstruction.operand));
                break;
            case OpCode::MoveF:
                MRegisters[ScopedInstruction.dst].f = MRegisters[ScopedInstruction.src].f;
                break;
            case OpCode::MoveI:
                MRegisters[ScopedInstruction.dst].u = MRegisters[ScopedInstruction.src].u;
                break;

            case OpCode::AddF:
                MRegisters[ScopedInstruction.dst].f =
                    MRegisters[ScopedInstruction.src].f + MRegisters[ScopedInstruction.operand].f;
                break;
            case OpCode::SubF:
                MRegisters[ScopedInstruction.dst].f =
                    MRegisters[ScopedInstruction.src].f - MRegisters[ScopedInstruction.operand].f;
                break;
            case OpCode::MulF:
                MRegisters[ScopedInstruction.dst].f =
                    MRegisters[ScopedInstruction.src].f * MRegisters[ScopedInstruction.operand].f;
                break;
            case OpCode::DivF:
                MRegisters[ScopedInstruction.dst].f =
                    MRegisters[ScopedInstruction.src].f / MRegisters[ScopedInstruction.operand].f;
                break;
            case OpCode::AddI:
                MRegisters[ScopedInstruction.dst].i = std::bit_cast<std::int32_t>(
                    MRegisters[ScopedInstruction.src].u + MRegisters[ScopedInstruction.operand].u);
                break;
            case OpCode::SubI:
                MRegisters[ScopedInstruction.dst].i = std::bit_cast<std::int32_t>(
                    MRegisters[ScopedInstruction.src].u - MRegisters[ScopedInstruction.operand].u);
                break;
            case OpCode::MulI:
                MRegisters[ScopedInstruction.dst].i = std::bit_cast<std::int32_t>(
                    MRegisters[ScopedInstruction.src].u * MRegisters[ScopedInstruction.operand].u);
                break;
            case OpCode::DivI:
            {
                const std::int32_t ScopedDivisor = MRegisters[ScopedInstruction.operand].i;
                if (ScopedDivisor == 0)
                {
                    return FaultStop(InFault, VmFault::Code::DivisionByZero, ScopedPc);
                }
                const std::int32_t ScopedDividend = MRegisters[ScopedInstruction.src].i;
                MRegisters[ScopedInstruction.dst].i =
                    (ScopedDividend == std::numeric_limits<std::int32_t>::min() && ScopedDivisor == -1)
                        ? std::numeric_limits<std::int32_t>::min()
                        : ScopedDividend / ScopedDivisor;
                break;
            }
            case OpCode::ModI:
            {
                const std::int32_t ScopedDivisor = MRegisters[ScopedInstruction.operand].i;
                if (ScopedDivisor == 0)
                {
                    return FaultStop(InFault, VmFault::Code::DivisionByZero, ScopedPc);
                }
                const std::int32_t ScopedDividend = MRegisters[ScopedInstruction.src].i;
                MRegisters[ScopedInstruction.dst].i =
                    (ScopedDividend == std::numeric_limits<std::int32_t>::min() && ScopedDivisor == -1)
                        ? 0
                        : ScopedDividend % ScopedDivisor;
                break;
            }
            case OpCode::NegF:
                MRegisters[ScopedInstruction.dst].f = -MRegisters[ScopedInstruction.src].f;
                break;
            case OpCode::NegI:
                MRegisters[ScopedInstruction.dst].i =
                    std::bit_cast<std::int32_t>(0u - MRegisters[ScopedInstruction.src].u);
                break;

            case OpCode::AndI:
                MRegisters[ScopedInstruction.dst].u =
                    MRegisters[ScopedInstruction.src].u & MRegisters[ScopedInstruction.operand].u;
                break;
            case OpCode::OrI:
                MRegisters[ScopedInstruction.dst].u =
                    MRegisters[ScopedInstruction.src].u | MRegisters[ScopedInstruction.operand].u;
                break;
            case OpCode::NotI:
                MRegisters[ScopedInstruction.dst].u = ~MRegisters[ScopedInstruction.src].u;
                break;

            case OpCode::LtF:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].f < MRegisters[ScopedInstruction.operand].f ? 1 : 0;
                break;
            case OpCode::LeF:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].f <= MRegisters[ScopedInstruction.operand].f ? 1 : 0;
                break;
            case OpCode::GtF:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].f > MRegisters[ScopedInstruction.operand].f ? 1 : 0;
                break;
            case OpCode::GeF:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].f >= MRegisters[ScopedInstruction.operand].f ? 1 : 0;
                break;
            case OpCode::EqF:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].f == MRegisters[ScopedInstruction.operand].f ? 1 : 0;
                break;
            case OpCode::NeF:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].f != MRegisters[ScopedInstruction.operand].f ? 1 : 0;
                break;
            case OpCode::LtI:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].i < MRegisters[ScopedInstruction.operand].i ? 1 : 0;
                break;
            case OpCode::LeI:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].i <= MRegisters[ScopedInstruction.operand].i ? 1 : 0;
                break;
            case OpCode::GtI:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].i > MRegisters[ScopedInstruction.operand].i ? 1 : 0;
                break;
            case OpCode::GeI:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].i >= MRegisters[ScopedInstruction.operand].i ? 1 : 0;
                break;
            case OpCode::EqI:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].u == MRegisters[ScopedInstruction.operand].u ? 1 : 0;
                break;
            case OpCode::NeI:
                MRegisters[ScopedInstruction.dst].i =
                    MRegisters[ScopedInstruction.src].u != MRegisters[ScopedInstruction.operand].u ? 1 : 0;
                break;

            case OpCode::Jump:
                ScopedNext = ScopedInstruction.operand;
                break;
            case OpCode::JumpIfZero:
                if (MRegisters[ScopedInstruction.dst].u == 0)
                {
                    ScopedNext = ScopedInstruction.operand;
                }
                break;
            case OpCode::JumpIfNonZero:
                if (MRegisters[ScopedInstruction.dst].u != 0)
                {
                    ScopedNext = ScopedInstruction.operand;
                }
                break;

            case OpCode::LoadSelf:
                MRegisters[ScopedInstruction.dst].u = Bits(InSelf);
                break;

            case OpCode::LoadFieldF:
            case OpCode::LoadFieldI:
            case OpCode::LoadFieldVec2:
            {
                const FieldBinding& ScopedField = InProgram.program.fields[ScopedInstruction.operand];
                const Entity ScopedEntity = static_cast<Entity>(MRegisters[ScopedInstruction.src].u);
                if (!InContext.registry.valid(ScopedEntity))
                {
                    return FaultStop(InFault, VmFault::Code::BadEntity, ScopedPc, MRegisters[ScopedInstruction.src].u);
                }
                void* ScopedAddress = nullptr;
                if (!ScopedFieldAddress(ScopedField, ScopedEntity, &ScopedAddress))
                {
                    return FaultStop(InFault, VmFault::Code::MissingComponent, ScopedPc, ScopedField.component);
                }
                if (ScopedInstruction.Code() == OpCode::LoadFieldF)
                {
                    MRegisters[ScopedInstruction.dst].f = LoadRaw<float>(static_cast<const std::byte*>(ScopedAddress));
                }
                else if (ScopedInstruction.Code() == OpCode::LoadFieldI)
                {
                    std::uint32_t ScopedValue = 0;
                    StoreRaw(reinterpret_cast<std::byte*>(&ScopedValue), ScopedAddress, ScopedField.size);
                    MRegisters[ScopedInstruction.dst].u = ScopedValue;
                }
                else
                {
                    const auto* ScopedFloats = static_cast<const float*>(ScopedAddress);
                    MRegisters[ScopedInstruction.dst].f =
                        LoadRaw<float>(reinterpret_cast<const std::byte*>(ScopedFloats));
                    MRegisters[ScopedInstruction.dst + 1].f =
                        LoadRaw<float>(reinterpret_cast<const std::byte*>(ScopedFloats + 1));
                }
                break;
            }

            case OpCode::StoreFieldF:
            case OpCode::StoreFieldI:
            case OpCode::StoreFieldVec2:
            {
                const FieldBinding& ScopedField = InProgram.program.fields[ScopedInstruction.operand];
                const Entity ScopedEntity = static_cast<Entity>(MRegisters[ScopedInstruction.dst].u);
                if (!InContext.registry.valid(ScopedEntity))
                {
                    return FaultStop(InFault, VmFault::Code::BadEntity, ScopedPc, MRegisters[ScopedInstruction.dst].u);
                }
                void* ScopedAddress = nullptr;
                if (!ScopedFieldAddress(ScopedField, ScopedEntity, &ScopedAddress))
                {
                    return FaultStop(InFault, VmFault::Code::MissingComponent, ScopedPc, ScopedField.component);
                }
                if (ScopedInstruction.Code() == OpCode::StoreFieldF)
                {
                    StoreRaw(static_cast<std::byte*>(ScopedAddress), &MRegisters[ScopedInstruction.src].f, 4);
                }
                else if (ScopedInstruction.Code() == OpCode::StoreFieldI)
                {
                    StoreRaw(static_cast<std::byte*>(ScopedAddress), &MRegisters[ScopedInstruction.src].u,
                             ScopedField.size);
                }
                else
                {
                    auto* ScopedFloats = static_cast<float*>(ScopedAddress);
                    const float ScopedX = MRegisters[ScopedInstruction.src].f;
                    const float ScopedY = MRegisters[ScopedInstruction.src + 1].f;
                    StoreRaw(reinterpret_cast<std::byte*>(ScopedFloats), &ScopedX, 4);
                    StoreRaw(reinterpret_cast<std::byte*>(ScopedFloats + 1), &ScopedY, 4);
                }
                break;
            }

            case OpCode::CallNative:
            {
                if (ScopedInstruction.operand >= InProgram.program.natives.size())
                {
                    return FaultStop(InFault, VmFault::Code::BadNative, ScopedPc, ScopedInstruction.operand);
                }
                const std::uint16_t ScopedId = InProgram.program.natives[ScopedInstruction.operand];
                if (ScopedId >= kMaxNatives || MNatives[ScopedId] == nullptr)
                {
                    return FaultStop(InFault, VmFault::Code::BadNative, ScopedPc, ScopedId);
                }
                MNatives[ScopedId](*this, InContext, ScopedInstruction.dst);
                break;
            }

            case OpCode::SpawnPrefab:
            {
                if (InContext.spawn == nullptr)
                {
                    return FaultStop(InFault, VmFault::Code::SpawnUnavailable, ScopedPc, ScopedInstruction.operand);
                }
                const std::uint32_t ScopedSpawned = InContext.spawn(InContext, ScopedInstruction.operand);
                if (ScopedSpawned == static_cast<std::uint32_t>(entt::null))
                {
                    return FaultStop(InFault, VmFault::Code::BadEntity, ScopedPc, ScopedInstruction.operand);
                }
                MRegisters[ScopedInstruction.dst].u = ScopedSpawned;
                break;
            }
            case OpCode::DespawnEntity:
            {
                if (InContext.commands == nullptr)
                {
                    return FaultStop(InFault, VmFault::Code::NoCommandBuffer, ScopedPc);
                }
                const Entity ScopedEntity = static_cast<Entity>(MRegisters[ScopedInstruction.src].u);
                if (!InContext.registry.valid(ScopedEntity))
                {
                    return FaultStop(InFault, VmFault::Code::BadEntity, ScopedPc, MRegisters[ScopedInstruction.src].u);
                }
                (void)InContext.commands->Destroy(ScopedEntity);
                break;
            }

            case OpCode::RandomF:
            case OpCode::RandomRangeF:
                if (InContext.rng == nullptr)
                {
                    return FaultStop(InFault, VmFault::Code::NoRandomSource, ScopedPc);
                }
                if (ScopedInstruction.Code() == OpCode::RandomF)
                {
                    MRegisters[ScopedInstruction.dst].f = InContext.rng->NextFloat();
                }
                else
                {
                    float ScopedMinimum = MRegisters[ScopedInstruction.src].f;
                    float ScopedMaximum = ScopedConstants[ScopedInstruction.operand];
                    if (ScopedMaximum < ScopedMinimum)
                    {
                        const float ScopedSwap = ScopedMinimum;
                        ScopedMinimum = ScopedMaximum;
                        ScopedMaximum = ScopedSwap;
                    }
                    MRegisters[ScopedInstruction.dst].f =
                        ScopedMinimum + (ScopedMaximum - ScopedMinimum) * InContext.rng->NextFloat();
                }
                break;

            case OpCode::Count:
            default:
                return FaultStop(InFault, VmFault::Code::UnknownOpcode, ScopedPc, ScopedInstruction.op);
        }

        if (InDebug != nullptr && InDebug->StepBudget > 0)
        {
            --InDebug->StepBudget;
            if (InDebug->StepBudget == 0)
            {
                InDebug->StepRequested = true;
            }
        }
        ScopedPc = ScopedNext;
    }
    ScopedStop.Halt = VmHalt::Completed;
    ScopedStop.Pc = ScopedPc;
    if (InDebug != nullptr)
    {
        InDebug->Paused = false;
        InDebug->ResumeValid = false;
    }
    return ScopedStop;
}
}
