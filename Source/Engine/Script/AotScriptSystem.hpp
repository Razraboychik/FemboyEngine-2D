// FemboyEngine2D - cooked-build script system: runs nativized AOT modules only.
#pragma once

#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Script/AotHost.hpp"
#include "Engine/Script/ScriptComponent.hpp"

namespace Fby
{

// The VM-free script system. `ScriptComponent::Program` is an index into the AOT
// host's manifest order; a program without a native module is reported and
// skipped, never interpreted. This is what a cooked build schedules.
void FemboyAotScriptSystem(Ecs::SystemContext& InContext, const Script::AotHost& InHost,
                           DeterministicRng* InRng = nullptr);
}
