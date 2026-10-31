// FemboyEngine2D - FemboyScriptSystem: the ECS face of the script VM.
#pragma once

#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Script/EcsBridge.hpp"
#include "Engine/Script/ScriptComponent.hpp"
#include "Engine/Script/Vm.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace Fby
{

namespace Script
{
class ScriptRuntime;
class AotHost;
}

class ScriptRuntime
{
public:
    ScriptRuntime() = default;
    ~ScriptRuntime() = default;

    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // Registers the built-in components and the default native table. Returns
    [[nodiscard]] bool Initialize();

    // Registers a cooked program. `cooked` must stay alive (a .fempak mapping or
    [[nodiscard]] std::uint16_t LoadProgram(std::span<const std::byte> InCooked, std::string* InError = nullptr);

    // Releases a cooked program slot so the editor can hot-reload a script
    [[nodiscard]] bool UnloadProgram(std::uint32_t InHandle) noexcept;

    [[nodiscard]] const Script::BoundProgram* Program(std::uint32_t InHandle) const noexcept;
    // Stable hash of the cooked bytes for a loaded program (0 for a bad handle).
    // It is the key a cooked build uses to find the program's AOT module.
    [[nodiscard]] std::uint64_t ProgramHash(std::uint32_t InHandle) const noexcept;
    [[nodiscard]] std::uint64_t AotRunCount() const noexcept
    {
        return MAotRunCount;
    }
    [[nodiscard]] std::uint64_t UnnativizedSkipCount() const noexcept
    {
        return MUnnativizedSkipCount;
    }
    [[nodiscard]] std::uint32_t AotRun(std::uint32_t InHandle) noexcept
    {
        (void)InHandle;
        return ++MAotRunCount;
    }
    [[nodiscard]] std::uint32_t ReportUnnativized() noexcept
    {
        return ++MUnnativizedSkipCount;
    }
    [[nodiscard]] Script::EcsBridge& Bridge() noexcept
    {
        return MBridge;
    }
    [[nodiscard]] Script::ScriptVm& Vm() noexcept
    {
        return MVm;
    }
    [[nodiscard]] DeterministicRng& Rng() noexcept
    {
        return MRng;
    }
    [[nodiscard]] Script::BigNumberPool& BigNumbers() noexcept
    {
        return MBigNumbers;
    }
    [[nodiscard]] Script::DecimalPool& Decimals() noexcept
    {
        return MDecimals;
    }
    // Script values are event-local. Clearing at the event boundary keeps the
    // token pools bounded even when a script creates numbers in a loop.
    void ResetTransientNumbers() noexcept
    {
        MBigNumbers.Clear();
        MDecimals.Clear();
    }
    void SetSeed(std::uint64_t InSeed) noexcept
    {
        MRng = DeterministicRng::FromSeed(InSeed);
    }
    void SetNative(Script::NativeId InId, Script::NativeFn InFunction) noexcept
    {
        MVm.SetNative(InId, InFunction);
    }

    // Called by the system when Run() reports a fault. Logs at most once per
    void ReportFault(std::uint32_t InHandle, Entity InEntity, const Script::VmFault& InFault) noexcept;

    [[nodiscard]] std::uint32_t ProgramCount() const noexcept
    {
        return MProgramCount;
    }
    [[nodiscard]] bool Initialized() const noexcept
    {
        return MInitialized;
    }

private:
    struct Slot
    {
        std::span<const std::byte> storage{};
        Script::BoundProgram bound{};
        // Stable identity of the cooked bytes: the AOT manifest is keyed by the
        // same hash, so a program maps to its native module without the VM.
        std::uint64_t hash = 0;
        bool used = false;
    };

    std::array<Slot, kScriptMaxPrograms> MSlots{};
    Script::EcsBridge MBridge{};
    Script::ScriptVm MVm{};
    Script::BigNumberPool MBigNumbers{};
    Script::DecimalPool MDecimals{};
    DeterministicRng MRng{};
    std::uint32_t MProgramCount = 0;
    std::uint64_t MAotRunCount = 0;
    std::uint64_t MUnnativizedSkipCount = 0;
    std::array<bool, kScriptMaxPrograms> MFaultReported{};
    bool MInitialized = false;
};

// Registers the components a script graph can touch today. New components are
void RegisterScriptableComponents(Script::EcsBridge& InBridge);

// Installs LogFloat/LogInt/LogVec2/DespawnSelf/RandomFloat/RandomRange.
void RegisterDefaultScriptNatives(ScriptRuntime& InRuntime);

// The system. Call once per Update (and optionally per FixedUpdate) through
// When `InAot` is supplied, a program that has a nativized module runs as native
// code; `InAllowVmFallback == false` turns a missing module into a reported skip
// instead of an interpreter run (the cooked-build contract: no VM fallback).
void FemboyScriptSystem(Ecs::SystemContext& InContext, ScriptRuntime& InRuntime,
                        const Script::AotHost* InAot = nullptr, bool InAllowVmFallback = true);

// Test/tool helper: run one event for one entity without a schedule.
[[nodiscard]] bool RunScriptOnce(ScriptRuntime& InRuntime, std::uint32_t InProgram, Entity InEntity,
                                 Script::ScriptEvent InEvent, Script::VmContext& InContext,
                                 Script::VmFault* InFault = nullptr);
}
