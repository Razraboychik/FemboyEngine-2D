// FemboyEngine2D - FemboyScript 2.0 bytecode interpreter.
#pragma once

#include "Engine/Core/BigNumberStore.hpp"
#include "Engine/Core/DecimalStore.hpp"
#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/Script/Bytecode.hpp"
#include "Engine/Script/EcsBridge.hpp"
#include "Engine/Script/ScriptHeap.hpp"

#include <array>
#include <cstdint>

namespace Fby
{
class TransportGroupSystem;
class PhysicsSystem;
class CameraStackSystem;
class AudioSystem;
}

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
        CommandBufferFull,
        SpawnUnavailable,
        UnknownOpcode,
        NoHeap,
        HeapFull,
        BadString,
        BadCollection,
        OutOfRange,
        MissingKey,
        BadCall,
        Recursion,
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
            case Code::CommandBufferFull:
                return "command-buffer-full";
            case Code::SpawnUnavailable:
                return "spawn-unavailable";
            case Code::UnknownOpcode:
                return "unknown-opcode";
            case Code::NoHeap:
                return "no-heap";
            case Code::HeapFull:
                return "heap-full";
            case Code::BadString:
                return "bad-string";
            case Code::BadCollection:
                return "bad-collection";
            case Code::OutOfRange:
                return "out-of-range";
            case Code::MissingKey:
                return "missing-key";
            case Code::BadCall:
                return "bad-call";
            case Code::Recursion:
                return "recursion";
        }
        return "?";
    }
};

// Everything a program may observe about the world.
struct VmContext
{
    Ecs::Registry& registry;
    Entity self = Fby::kNullEntity;
    Ecs::CommandBuffer* commands = nullptr;
    DeterministicRng* rng = nullptr;
    BigNumberPool* bigNumbers = nullptr;
    DecimalPool* decimals = nullptr;
    float dt = 0.0f;
    std::uint64_t frameIndex = 0;
    // Optional host service for transport_attach / transport_detach natives.
    Fby::TransportGroupSystem* transport = nullptr;
    // Optional host service for the physics_* natives.
    Fby::PhysicsSystem* physics = nullptr;
    // Optional host service for camera_* natives.
    Fby::CameraStackSystem* camera = nullptr;
    // Optional host service for play_sound; asset ids are script integers.
    Fby::AudioSystem* audio = nullptr;
    void* user = nullptr;
    // Prefab spawn is a host hook because prefab instantiation lives.
    std::uint32_t (*spawn)(VmContext& InContext, std::uint32_t InPrefabId) noexcept = nullptr;
    // Bounded value heap for strings, arrays and maps. A host that runs a script
    // without collections may leave it null; collection natives then fault.
    ScriptHeap* heap = nullptr;
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

    // Runs one event entry point for `self`. Returns false only on a fault.
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
    // Register access for native functions. The interpreter validates its own
    // instructions, but a native may still pass an arbitrary index, so these
    // accessors clamp to the fixed register file instead of indexing past it.
    [[nodiscard]] float F(std::uint8_t InIndex) const noexcept
    {
        return InIndex < kMaxVmRegisters ? MRegisters[InIndex].f : 0.0f;
    }
    [[nodiscard]] std::int32_t I(std::uint8_t InIndex) const noexcept
    {
        return InIndex < kMaxVmRegisters ? MRegisters[InIndex].i : 0;
    }
    [[nodiscard]] std::uint32_t U(std::uint8_t InIndex) const noexcept
    {
        return InIndex < kMaxVmRegisters ? MRegisters[InIndex].u : 0u;
    }
    void SetF(std::uint8_t InIndex, float InValue) noexcept
    {
        if (InIndex < kMaxVmRegisters)
        {
            MRegisters[InIndex].f = InValue;
        }
    }
    void SetI(std::uint8_t InIndex, std::int32_t InValue) noexcept
    {
        if (InIndex < kMaxVmRegisters)
        {
            MRegisters[InIndex].i = InValue;
        }
    }
    void SetU(std::uint8_t InIndex, std::uint32_t InValue) noexcept
    {
        if (InIndex < kMaxVmRegisters)
        {
            MRegisters[InIndex].u = InValue;
        }
    }
    [[nodiscard]] ValueTag Tag(std::uint8_t InIndex) const noexcept
    {
        return InIndex < kMaxVmRegisters ? MTags[InIndex] : ValueTag::Float;
    }
    void SetTag(std::uint8_t InIndex, ValueTag InTag) noexcept
    {
        if (InIndex < kMaxVmRegisters)
        {
            MTags[InIndex] = InTag;
        }
    }
    // A native reports a recoverable script fault; the VM turns it into the same
    // VmFault stop a bytecode fault produces.
    void RaiseFault(VmFault::Code InCode, std::uint32_t InDetail = 0) noexcept
    {
        MPendingFault = InCode;
        MPendingFaultDetail = InDetail;
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
    std::array<ValueTag, kMaxVmRegisters> MTags{};
    std::array<NativeFn, kMaxNatives> MNatives{};
    struct CallFrame
    {
        std::uint32_t ReturnPc = 0;
        std::uint8_t ResultDst = 0;
        std::uint16_t Function = 0;
        std::uint16_t Base = 0;
    };
    std::array<CallFrame, kMaxCallDepth> MCalls{};
    std::uint32_t MCallDepth = 0;
    VmFault::Code MPendingFault = VmFault::Code::None;
    std::uint32_t MPendingFaultDetail = 0;
    std::uint64_t MExecuted = 0;
};
}
