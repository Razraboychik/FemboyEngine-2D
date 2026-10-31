// FemboyEngine2D - the ECS component that binds an entity to a script program.
// Deliberately free of VM includes so a nativized build can include it without
// pulling in the interpreter.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby
{

// Which program drives this entity and its persistent lifecycle bits. 8 bytes.
struct ScriptComponent
{
    // ScriptRuntime handle (kScriptNoProgram) in a VM build, or an index into
    // the AOT manifest's program order in a nativized build.
    std::uint32_t Program = 0xFFFFFFFFu;
    // KScriptFlag*
    std::uint32_t Flags = 0;
};

inline constexpr std::uint16_t kScriptNoProgram = 0xFFFFu;
inline constexpr std::uint32_t kScriptFlagEnabled = 1u << 0;
inline constexpr std::uint32_t kScriptFlagStarted = 1u << 1;
inline constexpr std::uint32_t kScriptMaxPrograms = 32;

static_assert(std::is_trivially_copyable_v<ScriptComponent>);
static_assert(std::is_standard_layout_v<ScriptComponent>);
static_assert(sizeof(ScriptComponent) == 8);
}

FBY_REFLECT_COMPONENT(Fby::ScriptComponent, "ScriptComponent", FBY_FIELD(Fby::ScriptComponent, Program, UInt32),
                      FBY_FIELD(Fby::ScriptComponent, Flags, UInt32))
