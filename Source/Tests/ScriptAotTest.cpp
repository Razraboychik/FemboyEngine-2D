// FemboyEngine2D - AOT transpiler / loader tests.
#include "Engine/ECS/Components.hpp"
#include "Engine/Script/AotAbi.hpp"
#include "Engine/Script/AotModule.hpp"
#include "Engine/Script/AotTranspiler.hpp"
#include "Engine/Script/EcsBridge.hpp"
#include "Engine/Script/GraphCompiler.hpp"
#include "Engine/Script/ScriptSystem.hpp"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
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

[[nodiscard]] Fby::Script::CompiledProgram BuildHalfVelocityProgram(std::vector<std::byte>* InStorage)
{
    using namespace Fby::Script;
    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedSetVelocity = ScopedGraph.AddNode(LogicNodeKind::SetVelocity);
    const std::uint16_t ScopedScale = ScopedGraph.AddNode(LogicNodeKind::Vec2Scale);
    const std::uint16_t ScopedVelocity = ScopedGraph.AddNode(LogicNodeKind::SelfVelocity);
    const std::uint16_t ScopedHalf = ScopedGraph.AddNode(LogicNodeKind::ConstFloat, std::bit_cast<std::uint32_t>(0.5f));
    (void)ScopedGraph.AddLink(ScopedEvent, 0, ScopedSetVelocity, 0);
    (void)ScopedGraph.AddLink(ScopedScale, 0, ScopedSetVelocity, 1);
    (void)ScopedGraph.AddLink(ScopedVelocity, 0, ScopedScale, 0);
    (void)ScopedGraph.AddLink(ScopedHalf, 0, ScopedScale, 1);

    LogicCompileResult ScopedCompiled;
    Fby::Script::LogicCompileOptions ScopedOptions;
    ScopedOptions.graphHash = 0x1234ABCDull;
    ScopedOptions.sourceHash = 0x5678EF01ull;
    if (!CompileLogicGraph(ScopedGraph, ScopedOptions, &ScopedCompiled))
    {
        std::printf("FAIL: graph compile: %s\n", ScopedCompiled.error.c_str());
        ++GFailures;
        return {};
    }
    *InStorage = ScopedCompiled.program;
    CompiledProgram ScopedParsed;
    std::string ScopedError;
    if (!ParseProgram(*InStorage, &ScopedParsed, &ScopedError))
    {
        std::printf("FAIL: parse: %s\n", ScopedError.c_str());
        ++GFailures;
    }
    return ScopedParsed;
}

bool TestTranspilerShape()
{
    std::vector<std::byte> ScopedStorage;
    const Fby::Script::CompiledProgram ScopedProgram = BuildHalfVelocityProgram(&ScopedStorage);
    Check(ScopedProgram.valid, "program valid for transpile");

    std::string ScopedSource;
    std::string ScopedError;
    Check(Fby::Script::TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "program transpiles");
    Check(Contains(ScopedSource, "#include \"Engine/Script/AotAbi.hpp\""), "generated file includes only AotAbi");
    Check(Contains(ScopedSource, "void fby_entry_update("), "generated update entry exists");
    Check(Contains(ScopedSource, "FBY_AOT_EXPORT"), "module export macro present");
    Check(Contains(ScopedSource, "fby_aot_module"), "module accessor present");
    Check(Contains(ScopedSource, "ctx.Component("), "component resolved through the host bridge");
    Check(Contains(ScopedSource, "Store<float>"), "field write is native memcpy");
    Check(Contains(ScopedSource, "ctx.RandomFloat") == false, "no random call in this graph");
    Check(!Contains(ScopedSource, "switch (r.op)"), "generated code has no VM dispatch loop");
    Check(!Contains(ScopedSource, "program.code"), "generated code does not read bytecode");

    // Deterministic output.
    std::string ScopedSecond;
    Check(Fby::Script::TranspileProgramToCpp(ScopedProgram, &ScopedSecond, &ScopedError) &&
              ScopedSecond == ScopedSource,
          "transpiler is deterministic");

    Fby::Script::CompiledProgram ScopedInvalid;
    std::string ScopedUnusedSource;
    Check(!Fby::Script::TranspileProgramToCpp(ScopedInvalid, &ScopedUnusedSource, &ScopedError),
          "invalid program is rejected");
    return true;
}

// A hand-written module standing in for a cooked .dll: it exercises exactly the
std::uint64_t GVelocityHash = 0;
const std::uint64_t* GHashes = &GVelocityHash;

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

bool TestHostBridge()
{
    Fby::Script::EcsBridge ScopedBridge;
    Check(ScopedBridge.RegisterComponent<Fby::Velocity>("Velocity"), "bridge registers Velocity");
    GVelocityHash = Fby::Reflect::LayoutHash<Fby::Velocity>();

    Fby::Script::Aot::Module ScopedModule;
    ScopedModule.graphHash = 0x1234;
    ScopedModule.componentCount = 1;
    ScopedModule.componentHashes = GHashes;
    ScopedModule.update = &AotHalfVelocity;

    std::string ScopedError;
    Check(Fby::Script::ValidateAotModule(ScopedModule, ScopedBridge, &ScopedError), "module validates");

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{Fby::Vector2D{2.0f, 4.0f}});

    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    Check(Fby::Script::RunAotModule(ScopedModule, Fby::Script::ScriptEvent::Update, ScopedEntity, ScopedContext,
                                    ScopedBridge, &ScopedError),
          "AOT module runs against ECS storage");
    const Fby::Velocity& ScopedVelocity = ScopedRegistry.get<Fby::Velocity>(ScopedEntity);
    CheckFloat(ScopedVelocity.Value.X, 1.0f, "native module halved velocity.x");
    CheckFloat(ScopedVelocity.Value.Y, 2.0f, "native module halved velocity.y");

    // A module cooked against a different component layout must be refused.
    std::uint64_t ScopedStaleHash = Fby::Reflect::LayoutHash<Fby::Velocity>() ^ 0x1ull;
    Fby::Script::Aot::Module ScopedStale;
    ScopedStale.componentCount = 1;
    ScopedStale.componentHashes = &ScopedStaleHash;
    ScopedStale.update = &AotHalfVelocity;
    Check(!Fby::Script::ValidateAotModule(ScopedStale, ScopedBridge, &ScopedError), "stale LayoutHash is rejected");
    Check(Contains(ScopedError, "LayoutHash"), "stale hash error names the contract");

    // A missing event is refused instead of crashing on a null function pointer.
    Fby::Script::Aot::Module ScopedStartOnly;
    ScopedStartOnly.componentCount = 0;
    ScopedStartOnly.componentHashes = nullptr;
    ScopedStartOnly.start = &AotHalfVelocity;
    Check(!Fby::Script::RunAotModule(ScopedStartOnly, Fby::Script::ScriptEvent::Update, ScopedEntity, ScopedContext,
                                     ScopedBridge, &ScopedError),
          "module without Update is refused");
    return true;
}

bool TestLoaderFailurePath()
{
    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "runtime initializes for loader test");
    Fby::Script::AotModule ScopedModule;
    std::string ScopedError;
    Check(!ScopedModule.Load("this_file_does_not_exist_fby.dll", ScopedRuntime.Bridge(), &ScopedError),
          "missing plugin fails cleanly");
    Check(!ScopedError.empty(), "missing plugin reports a readable error");
    Check(!ScopedModule.Loaded(), "failed load leaves the module unloaded");
    return true;
}

// Optional end-to-end path used by CI/manual verification:
bool TestOptionalEndToEnd()
{
    const char* ScopedDumpDirectory = std::getenv("FBY_AOT_DUMP_DIR");
    if (ScopedDumpDirectory != nullptr)
    {
        std::vector<std::byte> ScopedStorage;
        const Fby::Script::CompiledProgram ScopedProgram = BuildHalfVelocityProgram(&ScopedStorage);
        std::string ScopedSource;
        std::string ScopedError;
        if (!ScopedProgram.valid || !Fby::Script::TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError))
        {
            std::printf("FAIL: E2E transpile: %s\n", ScopedError.c_str());
            ++GFailures;
            return false;
        }
        const std::string ScopedDirectory = ScopedDumpDirectory;
        {
            std::ofstream ScopedBytes(ScopedDirectory + "/program.fbsc", std::ios::binary | std::ios::trunc);
            ScopedBytes.write(reinterpret_cast<const char*>(ScopedStorage.data()),
                              static_cast<std::streamsize>(ScopedStorage.size()));
        }
        {
            std::ofstream ScopedText(ScopedDirectory + "/program.cpp", std::ios::binary | std::ios::trunc);
            ScopedText << ScopedSource;
        }
        std::printf("script_aot_test: wrote E2E artifacts to %s\n", ScopedDirectory.c_str());
    }

    const char* ScopedDllPath = std::getenv("FBY_AOT_DLL");
    if (ScopedDllPath == nullptr)
    {
        return true;
    }

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "runtime initializes for E2E plugin test");
    Fby::Script::AotModule ScopedModule;
    std::string ScopedError;
    if (!ScopedModule.Load(ScopedDllPath, ScopedRuntime.Bridge(), &ScopedError))
    {
        std::printf("FAIL: E2E plugin load: %s\n", ScopedError.c_str());
        ++GFailures;
        return false;
    }
    Check(ScopedModule.Loaded(), "compiled AOT plugin loads");
    Check(ScopedModule.GraphHash() != 0, "plugin exposes its graph hash");

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{Fby::Vector2D{2.0f, 4.0f}});
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    Check(ScopedModule.Run(Fby::Script::ScriptEvent::Update, ScopedEntity, ScopedContext, &ScopedError),
          "compiled AOT plugin runs");
    const Fby::Velocity& ScopedResult = ScopedRegistry.get<Fby::Velocity>(ScopedEntity);
    CheckFloat(ScopedResult.Value.X, 1.0f, "compiled plugin halved velocity.x");
    CheckFloat(ScopedResult.Value.Y, 2.0f, "compiled plugin halved velocity.y");
    ScopedModule.Unload();
    return true;
}

bool TestTranspilerArithmeticParity()
{
    using namespace Fby::Script;
    std::string ScopedError;
    std::string ScopedSource;

    // Non-finite constants must become C++ expressions, not `inff`/`nanf`.
    {
        ProgramBuilder ScopedBuilder;
        const std::uint16_t ScopedConstant = ScopedBuilder.AddConstant(std::numeric_limits<float>::infinity());
        ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedConstant);
        ScopedBuilder.Emit(OpCode::Halt);
        (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
        const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
        CompiledProgram ScopedProgram;
        Check(!ScopedBytes.empty() && ParseProgram(ScopedBytes, &ScopedProgram, &ScopedError),
              "infinity program parses");
        Check(TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "infinity program transpiles");
        Check(Contains(ScopedSource, "std::numeric_limits<float>::infinity()"),
              "infinity is emitted as a C++ expression");
    }

    // Integer arithmetic must wrap like the VM and guard INT_MIN / -1.
    {
        ProgramBuilder ScopedBuilder;
        ScopedBuilder.Emit(OpCode::LoadI32, 0, 0, 7);
        ScopedBuilder.Emit(OpCode::LoadI32, 1, 0, 3);
        ScopedBuilder.Emit(OpCode::AddI, 2, 0, 1);
        ScopedBuilder.Emit(OpCode::DivI, 3, 2, 1);
        ScopedBuilder.Emit(OpCode::Halt);
        (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
        const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
        CompiledProgram ScopedProgram;
        Check(!ScopedBytes.empty() && ParseProgram(ScopedBytes, &ScopedProgram, &ScopedError),
              "integer program parses");
        Check(TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "integer program transpiles");
        Check(Contains(ScopedSource, "r.u[2] = r.u[0] + r.u[1];"),
              "AOT addition uses defined unsigned wrap");
        Check(Contains(ScopedSource, "0x80000000u"), "AOT division guards INT_MIN / -1");
    }

    // VM/AOT parity: signed logging, the despawn result register and faults.
    {
        ProgramBuilder ScopedBuilder;
        const std::uint16_t ScopedLog = ScopedBuilder.AddNative(NativeId::LogInt);
        ScopedBuilder.Emit(OpCode::LoadI32, 0, 0, static_cast<std::uint16_t>(0xFFFBu));
        ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedLog);
        ScopedBuilder.Emit(OpCode::Halt);
        (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
        const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
        CompiledProgram ScopedProgram;
        Check(!ScopedBytes.empty() && ParseProgram(ScopedBytes, &ScopedProgram, &ScopedError),
              "log parity program parses");
        Check(TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "log parity program transpiles");
        Check(Contains(ScopedSource, "r.i[0] = -5;"), "AOT decodes a negative LoadI32 immediate");
        Check(Contains(ScopedSource, "ctx.LogI32(r.i[0]);"), "AOT logs a signed int32");
    }
    {
        ProgramBuilder ScopedBuilder;
        const std::uint16_t ScopedDespawn = ScopedBuilder.AddNative(NativeId::DespawnSelf);
        ScopedBuilder.Emit(OpCode::LoadSelf, 0);
        ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedDespawn);
        ScopedBuilder.Emit(OpCode::Halt);
        (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
        const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
        CompiledProgram ScopedProgram;
        Check(!ScopedBytes.empty() && ParseProgram(ScopedBytes, &ScopedProgram, &ScopedError),
              "despawn parity program parses");
        Check(TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "despawn parity transpiles");
        Check(Contains(ScopedSource, "r.u[0] = ctx.Despawn(r.u[0]);"),
              "AOT writes the DespawnSelf result like the VM");
    }
    {
        ProgramBuilder ScopedBuilder;
        ScopedBuilder.Emit(OpCode::LoadI32, 0, 0, 8);
        ScopedBuilder.Emit(OpCode::LoadI32, 1, 0, 0);
        ScopedBuilder.Emit(OpCode::DivI, 2, 0, 1);
        ScopedBuilder.Emit(OpCode::Halt);
        (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
        const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
        CompiledProgram ScopedProgram;
        Check(!ScopedBytes.empty() && ParseProgram(ScopedBytes, &ScopedProgram, &ScopedError),
              "fault parity program parses");
        Check(TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "fault parity transpiles");
        Check(Contains(ScopedSource, "ctx.Fault(8,"), "AOT reports division by zero through the fault hook");
    }
    return true;
}
}

int main()
{
    TestTranspilerShape();
    TestTranspilerArithmeticParity();
    TestHostBridge();
    TestLoaderFailurePath();
    TestOptionalEndToEnd();

    if (GFailures != 0)
    {
        std::printf("%d AOT test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("script_aot_test: all tests passed\n");
    return 0;
}
