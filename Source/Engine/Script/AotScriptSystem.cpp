// FemboyEngine2D - cooked-build script system implementation (no interpreter).
#include "Engine/Script/AotScriptSystem.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Script/Vm.hpp"

namespace Fby
{

void FemboyAotScriptSystem(Ecs::SystemContext& InContext, const Script::AotHost& InHost, DeterministicRng* InRng)
{
    Script::VmContext ScopedContext{InContext.registry};
    ScopedContext.commands = InContext.commands;
    ScopedContext.rng = InRng;
    ScopedContext.bigNumbers = &InHost.BigNumbers();
    ScopedContext.decimals = &InHost.Decimals();
    ScopedContext.dt = InContext.dt;
    ScopedContext.frameIndex = InContext.frameIndex;

    auto ScopedView = InContext.registry.view<ScriptComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        ScriptComponent& ScopedComponent = ScopedView.get<ScriptComponent>(ScopedEntity);
        if ((ScopedComponent.Flags & kScriptFlagEnabled) == 0)
        {
            continue;
        }
        const std::uint64_t ScopedHash = InHost.ProgramHashAt(ScopedComponent.Program);
        if (ScopedHash == 0)
        {
            // A cooked build must not silently do nothing: report the program once
            // and keep the entity alive with Start marked as attempted.
            if ((ScopedComponent.Flags & kScriptFlagStarted) == 0)
            {
                LogError("script", "program index {} has no nativized module", ScopedComponent.Program);
                ScopedComponent.Flags |= kScriptFlagStarted;
            }
            continue;
        }
        InHost.ResetTransientNumbers();
        std::string ScopedError;
        if ((ScopedComponent.Flags & kScriptFlagStarted) == 0)
        {
            if (!InHost.Run(ScopedHash, Script::ScriptEvent::Start, ScopedEntity, ScopedContext, &ScopedError))
            {
                LogWarn("script", "AOT start failed for {:016x}: {}", ScopedHash, ScopedError);
            }
            ScopedComponent.Flags |= kScriptFlagStarted;
        }
        if (!InHost.Run(ScopedHash, Script::ScriptEvent::Update, ScopedEntity, ScopedContext, &ScopedError))
        {
            LogWarn("script", "AOT update failed for {:016x}: {}", ScopedHash, ScopedError);
        }
    }
}
}
