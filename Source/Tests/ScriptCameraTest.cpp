// FemboyEngine2D - camera effects from FemboyScript and the visual graph.
#include "Engine/ECS/Schedule.hpp"
#include "Engine/RHI/CameraStack.hpp"
#include "Engine/Script/AotTranspiler.hpp"
#include "Engine/Script/GraphCompiler.hpp"
#include "Engine/Script/ScriptSystem.hpp"
#include "Engine/Script/TextScript.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using namespace Fby;

namespace
{
void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        std::exit(1);
    }
}
}

int main()
{
    Scene::ComponentRegistry ScopedComponents;
    Scene::RegisterBuiltInComponents(ScopedComponents);
    constexpr std::string_view ScopedSource = R"FBY(
event update {
    camera_add_trauma(0.6);
    camera_distort(1.0, 0.5, 0.0, 0.0, 2.0);
    camera_offset(8.0, -4.0, 0.5);
    camera_zoom(1.1, 0.5);
    camera_roll(0.08, 0.5);
    camera_set_motion_scale(1.0);
}
)FBY";
    std::string ScopedError;
    std::vector<std::byte> ScopedProgram;
    Check(Script::CompileTextScript(ScopedSource, ScopedComponents, {}, &ScopedProgram, &ScopedError),
          "camera text script compiles");

    Ecs::Registry ScopedRegistry;
    JobSystem ScopedJobs(1);
    const Entity ScopedCamera = ScopedRegistry.create();
    ScopedRegistry.emplace<Camera2D>(ScopedCamera);
    Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.dt = 1.0f / 60.0f;
    Ecs::RenderSystemView ScopedRender{ScopedRegistry, ScopedJobs, ScopedContext, ScopedContext, ScopedContext.dt};
    CameraStackSystem ScopedCameras;
    CameraStackDesc ScopedDesc;
    ScopedDesc.cameraEntity = ScopedCamera;
    Check(ScopedCameras.Push(ScopedRegistry, ScopedDesc), "camera pushes");

    ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "script runtime initializes");
    ScopedRuntime.SetCameraStack(&ScopedCameras);
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedProgram, &ScopedError);
    Check(ScopedHandle != kScriptNoProgram, "camera program loads");

    const Entity ScopedEntity = ScopedRegistry.create();
    Script::VmContext ScopedVm{ScopedRegistry};
    ScopedVm.camera = &ScopedCameras;
    Check(RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, Script::ScriptEvent::Update, ScopedVm),
          "camera script runs");
    Check(ScopedCameras.Trauma() > 0.0f, "script adds trauma");
    Check((ScopedCameras.ActiveDistortion().flags & CameraDistortionDoubleVision) != 0,
          "script activates double vision");
    ScopedCameras.Update(ScopedRender);
    Check(ScopedCameras.ActiveCamera().Center.X != ScopedCameras.BaseCamera().Center.X,
          "script offset changes the resolved camera");

    // The visual language compiles the same native through the graph front-end.
    {
        Script::LogicGraph ScopedGraph;
        const std::uint16_t ScopedEvent = ScopedGraph.AddNode(Script::LogicNodeKind::EventUpdate);
        const std::uint16_t ScopedShake = ScopedGraph.AddNode(Script::LogicNodeKind::CameraAddTrauma);
        const std::uint16_t ScopedAmount = ScopedGraph.AddNode(Script::LogicNodeKind::ConstFloat,
                                                               std::bit_cast<std::uint32_t>(0.5f));
        Check(ScopedEvent != 0xFFFF && ScopedShake != 0xFFFF && ScopedAmount != 0xFFFF, "camera nodes allocate");
        Check(ScopedGraph.AddLink(ScopedEvent, 0, ScopedShake, 0), "event -> camera shake");
        Check(ScopedGraph.AddLink(ScopedAmount, 0, ScopedShake, 1), "const -> camera shake amount");
        Script::LogicCompileResult ScopedCompiled;
        if (!Script::CompileLogicGraph(ScopedGraph, &ScopedCompiled))
        {
            std::fprintf(stderr, "graph error: %s high=%u nativeArgs=%u\n", ScopedCompiled.error.c_str(), ScopedCompiled.registerHighWater, Script::NativeArgumentCount(Script::NativeId::CameraAddTrauma));
        }
        Check(Script::CompileLogicGraph(ScopedGraph, &ScopedCompiled), "camera graph compiles");
        Script::CompiledProgram ScopedParsed;
        Check(Script::ParseProgram(ScopedCompiled.program, &ScopedParsed, &ScopedError), "camera graph parses");
        std::string ScopedCpp;
        Check(Script::TranspileProgramToCpp(ScopedParsed, &ScopedCpp, &ScopedError), "camera graph nativizes");
        Check(ScopedCpp.find("CameraAddTrauma") != std::string::npos, "camera node reaches AOT C++");
    }

    ScopedCameras.ClearEffects();
    Check(ScopedCameras.Pop(), "camera pops");
    std::printf("script_camera: ok\n");
    return 0;
}
