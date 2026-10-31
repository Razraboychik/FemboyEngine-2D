// FemboyEngine2D - FemboyScript 2.0 bytecode interpreter.
#pragma once

#include "Engine/Core/BigNumberStore.hpp"
#include "Engine/Core/DecimalStore.hpp"
#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/Script/Bytecode.hpp"
#include "Engine/Script/EcsBridge.hpp"

#include <array>
#include <cstdint>

namespace Fby::Script
{

// Script visible big numbers are opaque tokens in ordinary float registers. The
// pool is fixed capacity, so the token size is chosen to stay exactly
// representable in a 24-bit float mantissa.
using BigNumberPool = BigNum::BigNumberStore<8, 64>;
using DecimalPool = BigNum::DecimalStore<64>;

struct VmFault
{
    enum class Code : std::uint8_t
    {
        None = 0,
        InvalidProgram,
        MissingComponent,
        BadEntity,
        BadNative,
        BadField,
        BadRegister,
        StepLimit,
        DivisionByZero,
        NoRandomSource,
        NoCommandBuffer,
        SpawnUnavailable,
        UnknownOpcode,
    };

    Code code = Code::None;
    std::uint32_t pc = 0;
    std::uint32_t detail = 0;

    [[nodiscard]] constexpr bool Ok() const noexcept
    {
        return code == Code::None;
    }

    [[nodiscard]] static constexpr const char* Name(Code InValue) noexcept
    {
        switch (InValue)
        {
            case Code::None:
                return "ok";
            case Code::InvalidProgram:
                return "invalid-program";
            case Code::MissingComponent:
                return "missing-component";
            case Code::BadEntity:
                return "bad-entity";
            case Code::BadNative:
                return "bad-native";
            case Code::BadField:
                return "bad-field";
            case Code::BadRegister:
                return "bad-register";
            case Code::StepLimit:
                return "step-limit";
            case Code::DivisionByZero:
                return "division-by-zero";
            case Code::NoRandomSource:
                return "no-random-source";
            case Code::NoCommandBuffer:
                return "no-command-buffer";
            case Code::SpawnUnavailable:
                return "spawn-unavailable";
            case Code::UnknownOpcode:
                return "unknown-opcode";
        }
        return "?";
    }
};

// Everything a program may observe about the world. Pointers are optional so a
struct VmContext
{
    Ecs::Registry& registry;
    Ecs::CommandBuffer* commands = nullptr;
    DeterministicRng* rng = nullptr;
    BigNumberPool* bigNumbers = nullptr;
    DecimalPool* decimals = nullptr;
    float dt = 0.0f;
    std::uint64_t frameIndex = 0;
    void* user = nullptr;
    // Prefab spawn is a host hook because prefab instantiation lives in the
    std::uint32_t (*spawn)(VmContext& InContext, std::uint32_t InPrefabId) noexcept = nullptr;
};

class ScriptVm;

// Why a debug run stopped before Halt.
enum class VmPauseReason : std::uint8_t
{
    None = 0,
    Breakpoint,
    Step,
    Request,
    Entry,
};

enum class VmHalt : std::uint8_t
{
    Completed = 0,
    Faulted,
    Paused,
};

struct VmStopInfo
{
    VmHalt Halt = VmHalt::Completed;
    VmPauseReason Reason = VmPauseReason::None;
    VmFault Fault{};
    std::uint32_t Pc = 0;

    [[nodiscard]] bool Paused() const noexcept
    {
        return Halt == VmHalt::Paused;
    }
    [[nodiscard]] bool Faulted() const noexcept
    {
        return Halt == VmHalt::Faulted;
    }
};

// Debug session state owned by the editor. `RunDebug` reads and writes it, so a
// pause keeps the register file and the program counter until the caller
// resumes; the interpreter itself stays allocation-free.
struct VmDebugState
{
    bool Enabled = false;
    bool Paused = false;
    bool ResumeValid = false;
    std::uint32_t ResumePc = 0;
    std::uint32_t CurrentPc = 0;
    std::uint32_t PausePc = 0;
    bool SkipOnce = false;
    std::uint32_t SkipPc = 0;
    bool PauseRequested = false;
    bool StepRequested = false;
    std::uint32_t StepBudget = 0;
    VmPauseReason LastReason = VmPauseReason::None;
    std::uint32_t PauseCount = 0;
    std::uint32_t BreakpointCount = 0;
    std::array<std::uint64_t, (kMaxInstructions + 63) / 64> Breakpoints{};

    void SetBreakpoint(std::uint32_t InPc) noexcept
    {
        if (InPc >= kMaxInstructions)
        {
            return;
        }
        const std::uint32_t ScopedWord = InPc / 64;
        const std::uint64_t ScopedBit = 1ull << (InPc % 64);
        if ((Breakpoints[ScopedWord] & ScopedBit) == 0)
        {
            Breakpoints[ScopedWord] |= ScopedBit;
            ++BreakpointCount;
        }
    }
    void ClearBreakpoint(std::uint32_t InPc) noexcept
    {
        if (InPc >= kMaxInstructions)
        {
            return;
        }
        const std::uint32_t ScopedWord = InPc / 64;
        const std::uint64_t ScopedBit = 1ull << (InPc % 64);
        if ((Breakpoints[ScopedWord] & ScopedBit) != 0)
        {
            Breakpoints[ScopedWord] &= ~ScopedBit;
            --BreakpointCount;
        }
    }
    [[nodiscard]] bool HasBreakpoint(std::uint32_t InPc) const noexcept
    {
        if (InPc >= kMaxInstructions)
        {
            return false;
        }
        return (Breakpoints[InPc / 64] & (1ull << (InPc % 64))) != 0;
    }
    void ClearBreakpoints() noexcept
    {
        Breakpoints.fill(0);
        BreakpointCount = 0;
    }
    void ClearPause() noexcept
    {
        Paused = false;
        ResumeValid = false;
        ResumePc = 0;
        SkipOnce = false;
        PauseRequested = false;
        StepRequested = false;
        StepBudget = 0;
        LastReason = VmPauseReason::None;
    }
};

// Native functions read their arguments from consecutive VM registers starting
using NativeFn = void (*)(ScriptVm& InVm, VmContext& InContext, std::uint8_t InBase);

class ScriptVm
{
public:
    static constexpr std::uint32_t kMaxNatives = kNativeFunctionCount;

    ScriptVm() = default;

    ScriptVm(const ScriptVm&) = delete;
    ScriptVm& operator=(const ScriptVm&) = delete;

    void SetNative(NativeId InId, NativeFn InFunction) noexcept
    {
        if (static_cast<std::uint32_t>(InId) < kMaxNatives)
        {
            MNatives[static_cast<std::uint32_t>(InId)] = InFunction;
        }
    }
    [[nodiscard]] NativeFn Native(NativeId InId) const noexcept
    {
        return static_cast<std::uint32_t>(InId) < kMaxNatives ? MNatives[static_cast<std::uint32_t>(InId)] : nullptr;
    }

    // Runs one event entry point for `self`. Returns false only on a fault; a
    [[nodiscard]] bool Run(const BoundProgram& InProgram, ScriptEvent InEvent, Entity InSelf, VmContext& InContext,
                           VmFault* InFault = nullptr) noexcept;

    // Debug entry point. With `InDebug.Enabled` false this is equivalent to
    // Run(); with it enabled the loop honours breakpoints, single steps and
    // pause requests, and leaves the register file and pc intact on a pause.
    [[nodiscard]] VmStopInfo RunDebug(const BoundProgram& InProgram, ScriptEvent InEvent, Entity InSelf,
                                      VmContext& InContext, VmDebugState& InDebug) noexcept;

    // Prepares a fresh debug session: clears pause state and requests a pause at
    // the first instruction of the event.
    void RequestPauseAtEntry(VmDebugState& InDebug) noexcept;

    // Register access for native functions. Bounds are the program's declared
    [[nodiscard]] float F(std::uint8_t InIndex) const noexcept
    {
        return MRegisters[InIndex].f;
    }
    [[nodiscard]] std::int32_t I(std::uint8_t InIndex) const noexcept
    {
        return MRegisters[InIndex].i;
    }
    [[nodiscard]] std::uint32_t U(std::uint8_t InIndex) const noexcept
    {
        return MRegisters[InIndex].u;
    }
    void SetF(std::uint8_t InIndex, float InValue) noexcept
    {
        MRegisters[InIndex].f = InValue;
    }
    void SetI(std::uint8_t InIndex, std::int32_t InValue) noexcept
    {
        MRegisters[InIndex].i = InValue;
    }
    void SetU(std::uint8_t InIndex, std::uint32_t InValue) noexcept
    {
        MRegisters[InIndex].u = InValue;
    }

    [[nodiscard]] std::uint64_t InstructionsExecuted() const noexcept
    {
        return MExecuted;
    }

private:
    union Register
    {
        float f;
        std::int32_t i;
        std::uint32_t u;
    };

    [[nodiscard]] bool Fault(VmFault* InFault, VmFault::Code InCode, std::uint32_t InPc,
                             std::uint32_t InDetail = 0) noexcept;
    [[nodiscard]] VmStopInfo FaultStop(VmFault* InFault, VmFault::Code InCode, std::uint32_t InPc,
                                       std::uint32_t InDetail = 0) noexcept;
    [[nodiscard]] VmStopInfo PauseStop(VmDebugState& InDebug, VmPauseReason InReason) noexcept;
    [[nodiscard]] VmStopInfo Execute(const BoundProgram& InProgram, ScriptEvent InEvent, Entity InSelf,
                                     VmContext& InContext, VmFault* InFault, VmDebugState* InDebug) noexcept;

    std::array<Register, kMaxVmRegisters> MRegisters{};
    std::array<NativeFn, kMaxNatives> MNatives{};
    std::uint64_t MExecuted = 0;
};
}
