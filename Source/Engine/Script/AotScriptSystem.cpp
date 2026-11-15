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
    InHost.PruneCoroutines(InContext.registry);

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
        const auto ScopedRunEvent = [&](Script::ScriptEvent InEvent, bool& OutSuspended)
        {
            OutSuspended = false;
            Script::Aot::CoroutineState* ScopedState =
                InHost.CoroutineFor(ScopedEntity, ScopedComponent.Program, InEvent);
            if (ScopedState != nullptr && ScopedState->Active != 0 &&
                ScopedState->WakeFrame > ScopedContext.frameIndex)
            {
                OutSuspended = true;
                return;
            }
            std::string ScopedError;
            ScopedContext.aotCoroutine = ScopedState;
            const bool ScopedOk = InHost.Run(ScopedHash, InEvent, ScopedEntity, ScopedContext, &ScopedError);
            ScopedContext.aotCoroutine = nullptr;
            OutSuspended = ScopedState != nullptr && ScopedState->Active != 0;
            if (!ScopedOk)
            {
                LogWarn("script", "AOT {} failed for {:016x}: {}", InEvent == Script::ScriptEvent::Start ? "start" : "update",
                        ScopedHash, ScopedError);
            }
            if (ScopedState != nullptr && !OutSuspended)
            {
                InHost.ReleaseCoroutine(ScopedEntity, ScopedComponent.Program, InEvent);
            }
        };

        if ((ScopedComponent.Flags & kScriptFlagStarted) == 0)
        {
            bool ScopedStartSuspended = false;
            ScopedRunEvent(Script::ScriptEvent::Start, ScopedStartSuspended);
            if (!ScopedStartSuspended)
            {
                ScopedComponent.Flags |= kScriptFlagStarted;
            }
        }
        bool ScopedUpdateSuspended = false;
        ScopedRunEvent(Script::ScriptEvent::Update, ScopedUpdateSuspended);
        (void)ScopedUpdateSuspended;
    }
}
}
