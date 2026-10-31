// FemboyEngine2D - nativized program registry tests: manifest, cook gate and a
// VM-free dispatch path through FemboyScriptSystem.
#include "Engine/Script/AotHost.hpp"

#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Script/AotScriptSystem.hpp"
#include "Engine/Script/Bytecode.hpp"
#include "Engine/Script/GraphCompiler.hpp"
#include "Engine/Script/ScriptSystem.hpp"

#include <bit>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void CheckFloat(float InActual, float InExpected, const char* InMessage)
{
    if (InActual != InExpected)
    {
        std::printf("FAIL: %s (expected %f, got %f)\n", InMessage, static_cast<double>(InExpected),
                    static_cast<double>(InActual));
        ++GFailures;
    }
}

[[nodiscard]] bool Contains(const std::string& InText, const char* InNeedle)
{
    return InText.find(InNeedle) != std::string::npos;
}

// A hand-written module standing in for a cooked library: halves Velocity.
std::uint64_t GVelocityHash = 0;
const std::uint64_t* GHashes = &GVelocityHash;

void AotNoop(Fby::Script::Aot::Context& InContext) noexcept
{
    (void)InContext;
}

void AotHalfVelocity(Fby::Script::Aot::Context& InContext) noexcept
{
    std::byte* ScopedBase = InContext.Component(GVelocityHash);
    if (ScopedBase == nullptr)
    {
        return;
    }
    const float ScopedX = Fby::Script::Aot::Load<float>(ScopedBase, 0);
    const float ScopedY = Fby::Script::Aot::Load<float>(ScopedBase, 4);
    Fby::Script::Aot::Store<float>(ScopedBase, 0, ScopedX * 0.5f);
    Fby::Script::Aot::Store<float>(ScopedBase, 4, ScopedY * 0.5f);
}

// Builds "Update: Velocity = Velocity * scale" as a compiled graph program.
[[nodiscard]] bool BuildScaleVelocityProgram(float InScale, std::vector<std::byte>* OutStorage,
                                             std::uint64_t* OutHash)
{
    using namespace Fby::Script;
    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedSetVelocity = ScopedGraph.AddNode(LogicNodeKind::SetVelocity);
    const std::uint16_t ScopedScale = ScopedGraph.AddNode(LogicNodeKind::Vec2Scale);
    const std::uint16_t ScopedVelocity = ScopedGraph.AddNode(LogicNodeKind::SelfVelocity);
    const std::uint16_t ScopedFactor = ScopedGraph.AddNode(LogicNodeKind::ConstFloat, std::bit_cast<std::uint32_t>(InScale));
    (void)ScopedGraph.AddLink(ScopedEvent, 0, ScopedSetVelocity, 0);
    (void)ScopedGraph.AddLink(ScopedScale, 0, ScopedSetVelocity, 1);
    (void)ScopedGraph.AddLink(ScopedVelocity, 0, ScopedScale, 0);
    (void)ScopedGraph.AddLink(ScopedFactor, 0, ScopedScale, 1);

    LogicCompileResult ScopedCompiled;
    if (!CompileLogicGraph(ScopedGraph, &ScopedCompiled))
    {
        return false;
    }
    *OutStorage = ScopedCompiled.program;
    *OutHash = HashProgramBytes(*OutStorage);
    return !OutStorage->empty();
}

void TestManifest()
{
    using namespace Fby::Script;
    std::array<AotProgramRef, 2> ScopedRefs{};
    ScopedRefs[0].Hash = 0x1234;
    ScopedRefs[0].LibraryPath = "modules/half.fbyaot.dll";
    ScopedRefs[0].Name = "half";
    ScopedRefs[1].Hash = 0xffffffffffffffffull;
    ScopedRefs[1].LibraryPath = "modules/full.dll";
    ScopedRefs[1].Name = "full";

    const std::string ScopedText = AotHost::BuildManifest(ScopedRefs);
    Check(Contains(ScopedText, "FBYAOT1"), "manifest has a magic header");

    std::vector<AotProgramRef> ScopedParsed;
    std::string ScopedError;
    Check(AotHost::ParseManifest(ScopedText, &ScopedParsed, &ScopedError), "manifest round-trips");
    Check(ScopedParsed.size() == 2, "manifest keeps both programs");
    Check(ScopedParsed[0].Hash == 0x1234, "manifest preserves the hash");
    Check(ScopedParsed[0].LibraryPath == "modules/half.fbyaot.dll", "manifest preserves the path");
    Check(ScopedParsed[1].Name == "full", "manifest preserves the name");

    Check(!AotHost::ParseManifest("NOTAOT\n", &ScopedParsed, &ScopedError), "bad magic is rejected");
    Check(!AotHost::ParseManifest("FBYAOT1\nprogram\tzzz\tx.dll\tn\n", &ScopedParsed, &ScopedError),
          "invalid hash is rejected");
    Check(!AotHost::ParseManifest("FBYAOT1\nprogram\t0123\n", &ScopedParsed, &ScopedError),
          "missing columns are rejected");
    Check(!AotHost::ParseManifest("", &ScopedParsed, &ScopedError), "empty manifest is rejected");
}

void TestCookGate()
{
    using namespace Fby::Script;
    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "runtime initializes for the gate test");
    GVelocityHash = Fby::Reflect::LayoutHash<Fby::Velocity>();

    Fby::Script::Aot::Module ScopedModule;
    ScopedModule.componentCount = 1;
    ScopedModule.componentHashes = GHashes;
    ScopedModule.start = &AotNoop;
    ScopedModule.update = &AotHalfVelocity;

    AotHost ScopedHost;
    Check(ScopedHost.RegisterModule(0xABC, ScopedModule, ScopedRuntime.Bridge(), "half"), "module registers");

    const std::array<std::uint64_t, 1> ScopedPresent{0xABC};
    std::string ScopedError;
    Check(ScopedHost.ValidateRequired(ScopedPresent, &ScopedError), "cook gate accepts a nativized program");

    const std::array<std::uint64_t, 2> ScopedMissing{0xABC, 0xDEF};
    Check(!ScopedHost.ValidateRequired(ScopedMissing, &ScopedError), "cook gate rejects a missing program");
    Check(Contains(ScopedError, "0000000000000def"), "cook gate names the missing program");
}

void TestNativizedSystem()
{
    using namespace Fby::Script;
    std::vector<std::byte> ScopedStorage;
    std::uint64_t ScopedHash = 0;
    Check(BuildScaleVelocityProgram(0.5f, &ScopedStorage, &ScopedHash), "half-velocity program compiles");

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "runtime initializes for the dispatch test");
    std::string ScopedError;
    const std::uint32_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedStorage, &ScopedError);
    Check(ScopedHandle != Fby::kScriptNoProgram, "program loads");
    Check(ScopedRuntime.ProgramHash(ScopedHandle) == ScopedHash, "program hash matches the cooked bytes");

    GVelocityHash = Fby::Reflect::LayoutHash<Fby::Velocity>();
    Fby::Script::Aot::Module ScopedModule;
    ScopedModule.componentCount = 1;
    ScopedModule.componentHashes = GHashes;
    ScopedModule.start = &AotNoop;
    ScopedModule.update = &AotHalfVelocity;

    AotHost ScopedHost;
    Check(ScopedHost.RegisterModule(ScopedHash, ScopedModule, ScopedRuntime.Bridge(), "half"), "module registers");

    entt::registry ScopedRegistry;
    Fby::JobSystem ScopedJobs;
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{Fby::Vector2D{2.0f, 4.0f}});
    Fby::ScriptComponent ScopedComponent;
    ScopedComponent.Program = ScopedHandle;
    ScopedComponent.Flags = Fby::kScriptFlagEnabled;
    ScopedRegistry.emplace<Fby::ScriptComponent>(ScopedEntity, ScopedComponent);

    // No VM fallback: the native module is the only execution path.
    Fby::FemboyScriptSystem(ScopedContext, ScopedRuntime, &ScopedHost, false);
    CheckFloat(ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value.X, 1.0f, "native module halved velocity.x");
    CheckFloat(ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value.Y, 2.0f, "native module halved velocity.y");
    Check(ScopedRuntime.AotRunCount() == 1, "the AOT path ran once");

    // A program without a module is refused instead of falling back to the VM.
    std::vector<std::byte> ScopedOtherStorage;
    std::uint64_t ScopedOtherHash = 0;
    Check(BuildScaleVelocityProgram(0.25f, &ScopedOtherStorage, &ScopedOtherHash), "second program compiles");
    const std::uint32_t ScopedOtherHandle = ScopedRuntime.LoadProgram(ScopedOtherStorage, &ScopedError);
    Check(ScopedOtherHandle != Fby::kScriptNoProgram, "second program loads");
    const Fby::Entity ScopedOtherEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Velocity>(ScopedOtherEntity, Fby::Velocity{Fby::Vector2D{8.0f, 8.0f}});
    Fby::ScriptComponent ScopedOtherComponent;
    ScopedOtherComponent.Program = ScopedOtherHandle;
    ScopedOtherComponent.Flags = Fby::kScriptFlagEnabled;
    ScopedRegistry.emplace<Fby::ScriptComponent>(ScopedOtherEntity, ScopedOtherComponent);
    Fby::FemboyScriptSystem(ScopedContext, ScopedRuntime, &ScopedHost, false);
    CheckFloat(ScopedRegistry.get<Fby::Velocity>(ScopedOtherEntity).Value.X, 8.0f,
               "an unnativized program does not run on the VM");
    Check(ScopedRuntime.UnnativizedSkipCount() == 1, "the skip is reported");

    // The editor/dev posture keeps the VM fallback for programs without a module.
    Fby::FemboyScriptSystem(ScopedContext, ScopedRuntime, &ScopedHost, true);
    CheckFloat(ScopedRegistry.get<Fby::Velocity>(ScopedOtherEntity).Value.X, 2.0f,
               "the VM fallback still runs an unnativized program");
}

void TestAotOnlySystem()
{
    using namespace Fby::Script;
    // No ScriptRuntime at all: this is the cooked-build path.
    Fby::Script::Aot::Module ScopedModule;
    ScopedModule.componentCount = 1;
    ScopedModule.componentHashes = GHashes;
    ScopedModule.start = &AotNoop;
    ScopedModule.update = &AotHalfVelocity;

    Fby::Script::EcsBridge ScopedBridge;
    Check(ScopedBridge.RegisterComponent<Fby::Velocity>("Velocity"), "bridge registers Velocity");
    GHashes = &GVelocityHash;
    GVelocityHash = Fby::Reflect::LayoutHash<Fby::Velocity>();

    AotHost ScopedHost;
    Check(ScopedHost.RegisterModule(0xCAFE, ScopedModule, ScopedBridge, "half"), "module registers for the cooked path");

    entt::registry ScopedRegistry;
    Fby::JobSystem ScopedJobs;
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{Fby::Vector2D{4.0f, 8.0f}});
    Fby::ScriptComponent ScopedComponent;
    ScopedComponent.Program = 0;
    ScopedComponent.Flags = Fby::kScriptFlagEnabled;
    ScopedRegistry.emplace<Fby::ScriptComponent>(ScopedEntity, ScopedComponent);

    Fby::FemboyAotScriptSystem(ScopedContext, ScopedHost);
    CheckFloat(ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value.X, 2.0f, "cooked system halved velocity.x");
    CheckFloat(ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value.Y, 4.0f, "cooked system halved velocity.y");

    // Disabled scripts do not run; an unknown program index is reported and skipped.
    ScopedRegistry.get<Fby::ScriptComponent>(ScopedEntity).Flags = 0;
    Fby::FemboyAotScriptSystem(ScopedContext, ScopedHost);
    CheckFloat(ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value.X, 2.0f, "disabled script is skipped");
    ScopedRegistry.get<Fby::ScriptComponent>(ScopedEntity).Flags = Fby::kScriptFlagEnabled;
    ScopedRegistry.get<Fby::ScriptComponent>(ScopedEntity).Program = 7;
    Fby::FemboyAotScriptSystem(ScopedContext, ScopedHost);
    CheckFloat(ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value.X, 2.0f, "unknown program index is skipped");
}

void TestProgramHash()
{
    std::vector<std::byte> ScopedBytes(16);
    for (std::size_t ScopedI = 0; ScopedI < ScopedBytes.size(); ++ScopedI)
    {
        ScopedBytes[ScopedI] = static_cast<std::byte>(ScopedI);
    }
    const std::uint64_t ScopedFirst = Fby::Script::HashProgramBytes(ScopedBytes);
    Check(ScopedFirst != 0, "program hash is non-zero");
    Check(Fby::Script::HashProgramBytes(ScopedBytes) == ScopedFirst, "program hash is stable");
    ScopedBytes[3] = std::byte{0x7F};
    Check(Fby::Script::HashProgramBytes(ScopedBytes) != ScopedFirst, "program hash covers every byte");
}
}

int main()
{
    TestManifest();
    TestCookGate();
    TestNativizedSystem();
    TestAotOnlySystem();
    TestProgramHash();

    if (GFailures != 0)
    {
        std::printf("%d AOT host test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("AOT host tests passed\n");
    return 0;
}
