// FemboyEngine2D - FemboyScript physics backend bridge.
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Script/ScriptSystem.hpp"
#include "Engine/Script/GraphCompiler.hpp"
#include "Engine/Script/TextScript.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using namespace Fby;

int main()
{
    // Visual language: the physics nodes must compile through the same native
    // table as the text front-end.
    {
        Script::LogicGraph ScopedGraph;
        const std::uint16_t ScopedEvent = ScopedGraph.AddNode(Script::LogicNodeKind::EventUpdate);
        const std::uint16_t ScopedBackend = ScopedGraph.AddNode(Script::LogicNodeKind::PhysicsGetBackend);
        const std::uint16_t ScopedLog = ScopedGraph.AddNode(Script::LogicNodeKind::LogValue);
        if (ScopedEvent == 0xFFFF || ScopedBackend == 0xFFFF || ScopedLog == 0xFFFF ||
            !ScopedGraph.AddLink(ScopedEvent, 0, ScopedLog, 0) ||
            !ScopedGraph.AddLink(ScopedBackend, 0, ScopedLog, 1))
        {
            std::fprintf(stderr, "FAIL: graph node setup\n");
            return 1;
        }
        Script::LogicCompileResult ScopedCompiled;
        if (!Script::CompileLogicGraph(ScopedGraph, &ScopedCompiled))
        {
            std::fprintf(stderr, "FAIL: physics graph compile\n");
            return 1;
        }
    }

    Scene::ComponentRegistry ScopedComponents;
    Scene::RegisterBuiltInComponents(ScopedComponents);
    constexpr std::string_view ScopedSource = R"FBY(
event update {
    let backend = physics_get_backend();
    let caps = physics_get_capabilities();
    self.Transform2D.Rotation = backend + caps;
}
)FBY";
    std::string ScopedError;
    std::vector<std::byte> ScopedProgram;
    if (!Script::CompileTextScript(ScopedSource, ScopedComponents, {}, &ScopedProgram, &ScopedError))
    {
        std::fprintf(stderr, "FAIL: compile: %s\n", ScopedError.c_str());
        return 1;
    }

    PhysicsDesc ScopedDesc;
    ScopedDesc.backend = PhysicsBackendKind::Dod;
    PhysicsSystem ScopedPhysics;
    if (!ScopedPhysics.Initialize(ScopedDesc))
    {
        std::fprintf(stderr, "FAIL: physics initialize\n");
        return 1;
    }

    ScriptRuntime ScopedRuntime;
    if (!ScopedRuntime.Initialize())
    {
        std::fprintf(stderr, "FAIL: script runtime initialize\n");
        return 1;
    }
    ScopedRuntime.SetPhysics(&ScopedPhysics);
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedProgram, &ScopedError);
    if (ScopedHandle == kScriptNoProgram)
    {
        std::fprintf(stderr, "FAIL: program load: %s\n", ScopedError.c_str());
        return 1;
    }

    Ecs::Registry ScopedRegistry;
    const Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedEntity);
    Script::VmContext ScopedContext{ScopedRegistry};
    ScopedContext.physics = &ScopedPhysics;
    if (!RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, Script::ScriptEvent::Update, ScopedContext))
    {
        std::fprintf(stderr, "FAIL: script run\n");
        return 1;
    }
    const Transform2D& ScopedTransform = ScopedRegistry.get<Transform2D>(ScopedEntity);
    if (ScopedTransform.Rotation != 26.0f)
    {
        std::fprintf(stderr, "FAIL: backend/capability result %.1f\n", static_cast<double>(ScopedTransform.Rotation));
        return 1;
    }

    ScopedPhysics.Shutdown();
    std::printf("script_physics: ok\n");
    return 0;
}
