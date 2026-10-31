// FemboyEngine2D - FemboyScript 2.0 VM / ECS bridge / graph compiler tests.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/Script/Bytecode.hpp"
#include "Engine/Script/EcsBridge.hpp"
#include "Engine/Script/GraphCompiler.hpp"
#include "Engine/Script/ScriptSystem.hpp"
#include "Engine/Script/TextScript.hpp"
#include "Engine/Script/Vm.hpp"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace
{
std::uint64_t GAllocationCount = 0;

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

struct CountingRng
{
    Fby::DeterministicRng rng = Fby::DeterministicRng::FromSeed(1234);
};

// Hand-built bytecode: constants, ALU, comparison, branches.
bool TestArithmetic()
{
    using namespace Fby::Script;
    ProgramBuilder ScopedBuilder;
    const std::uint16_t ScopedTwo = ScopedBuilder.AddConstant(2.0f);
    const std::uint16_t ScopedThree = ScopedBuilder.AddConstant(3.0f);
    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedTwo);
    ScopedBuilder.Emit(OpCode::LoadF32, 1, 0, ScopedThree);
    ScopedBuilder.Emit(OpCode::AddF, 2, 0, 1);
    ScopedBuilder.Emit(OpCode::MulF, 3, 2, 1);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);

    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(1, 2, &ScopedError);
    Check(!ScopedBytes.empty(), "arithmetic program builds");

    CompiledProgram ScopedParsed;
    Check(ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError), "arithmetic program parses");

    Fby::Script::EcsBridge ScopedBridge;
    BoundProgram ScopedBound;
    Check(BindProgram(ScopedParsed, ScopedBridge, &ScopedBound, &ScopedError), "program with no components binds");
    Check(ScopedBound.componentCount == 0, "program declares no components");

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    Fby::Script::ScriptVm ScopedVm;
    Check(ScopedVm.Run(ScopedBound, ScriptEvent::Update, ScopedEntity, ScopedContext), "arithmetic program runs");
    CheckFloat(ScopedVm.F(2), 5.0f, "2 + 3 == 5");
    CheckFloat(ScopedVm.F(3), 15.0f, "5 * 3 == 15");
    return true;
}

bool TestBranch()
{
    using namespace Fby::Script;
    ProgramBuilder ScopedBuilder;
    const std::uint16_t ScopedFive = ScopedBuilder.AddConstant(5.0f);
    // 5
    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedFive);
    // 5
    ScopedBuilder.Emit(OpCode::LoadF32, 1, 0, ScopedFive);
    // 5.0f > 5.0f -> false (0)
    ScopedBuilder.Emit(OpCode::GtF, 2, 0, 1);
    ScopedBuilder.Emit(OpCode::JumpIfZero, 2, 0, 6);
    // Skipped
    ScopedBuilder.Emit(OpCode::LoadI32, 3, 0, 111);
    ScopedBuilder.Emit(OpCode::Jump, 0, 0, 7);
    // Taken
    ScopedBuilder.Emit(OpCode::LoadI32, 3, 0, 222);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);

    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
    Check(!ScopedBytes.empty(), "branch program builds");
    CompiledProgram ScopedParsed;
    Check(ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError), "branch program parses");
    Fby::Script::EcsBridge ScopedBridge;
    BoundProgram ScopedBound;
    Check(BindProgram(ScopedParsed, ScopedBridge, &ScopedBound, &ScopedError), "branch program binds");
    entt::registry ScopedRegistry;
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    Fby::Script::ScriptVm ScopedVm;
    Check(ScopedVm.Run(ScopedBound, ScriptEvent::Update, ScopedRegistry.create(), ScopedContext),
          "branch program runs");
    Check(ScopedVm.I(3) == 222, "JumpIfZero takes the documented branch");
    return true;
}

bool TestSignedIntegerImmediate()
{
    using namespace Fby::Script;
    ProgramBuilder ScopedBuilder;
    // 0xFFFFB is the 16-bit two's complement of -5.
    ScopedBuilder.Emit(OpCode::LoadI32, 0, 0, static_cast<std::uint16_t>(0xFFFBu));
    ScopedBuilder.Emit(OpCode::LoadI32, 1, 0, 7);
    ScopedBuilder.Emit(OpCode::AddI, 2, 0, 1);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);

    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
    Check(!ScopedBytes.empty(), "signed immediate program builds");
    CompiledProgram ScopedParsed;
    Check(ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError), "signed immediate program parses");
    Fby::Script::EcsBridge ScopedBridge;
    BoundProgram ScopedBound;
    Check(BindProgram(ScopedParsed, ScopedBridge, &ScopedBound, &ScopedError), "signed immediate program binds");
    entt::registry ScopedRegistry;
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    Fby::Script::ScriptVm ScopedVm;
    Check(ScopedVm.Run(ScopedBound, ScriptEvent::Update, ScopedRegistry.create(), ScopedContext),
          "signed immediate program runs");
    Check(ScopedVm.I(0) == -5, "LoadI32 decodes a negative immediate");
    Check(ScopedVm.I(2) == 2, "signed immediate arithmetic matches two's complement");
    return true;
}

bool TestCorruptProgramRejected()
{
    using namespace Fby::Script;
    ProgramBuilder ScopedBuilder;
    const std::uint16_t ScopedConstant = ScopedBuilder.AddConstant(1.0f);
    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedConstant);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
    std::string ScopedError;
    std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
    Check(!ScopedBytes.empty(), "corruption test program builds");

    CompiledProgram ScopedParsed;
    std::vector<std::byte> ScopedBadMagic = ScopedBytes;
    ScopedBadMagic[0] = std::byte{0};
    Check(!ParseProgram(ScopedBadMagic, &ScopedParsed, &ScopedError), "bad magic is rejected");
    Check(!ParseProgram(std::span<const std::byte>(ScopedBytes.data(), ScopedBytes.size() / 2), &ScopedParsed,
                        &ScopedError),
          "truncated program is rejected");

    // Out-of-range jump target.
    std::vector<std::byte> ScopedBadJump = ScopedBytes;
    auto* ScopedInstructions = reinterpret_cast<Instruction*>(ScopedBadJump.data() + sizeof(ProgramHeader));
    ScopedInstructions[0].op = static_cast<std::uint8_t>(OpCode::Jump);
    ScopedInstructions[0].operand = 60000;
    Check(!ParseProgram(ScopedBadJump, &ScopedParsed, &ScopedError), "out-of-range jump is rejected");
    return true;
}

// Graph -> bytecode -> VM -> ECS field write.
bool TestGraphHalfVelocity()
{
    using namespace Fby::Script;
    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedSetVelocity = ScopedGraph.AddNode(LogicNodeKind::SetVelocity);
    const std::uint16_t ScopedScale = ScopedGraph.AddNode(LogicNodeKind::Vec2Scale);
    const std::uint16_t ScopedVelocity = ScopedGraph.AddNode(LogicNodeKind::SelfVelocity);
    const std::uint16_t ScopedHalf = ScopedGraph.AddNode(LogicNodeKind::ConstFloat, std::bit_cast<std::uint32_t>(0.5f));

    Check(ScopedEvent != 0xFFFF && ScopedSetVelocity != 0xFFFF && ScopedScale != 0xFFFF, "graph nodes allocate");
    Check(ScopedGraph.AddLink(ScopedEvent, 0, ScopedSetVelocity, 0), "event -> SetVelocity");
    Check(ScopedGraph.AddLink(ScopedScale, 0, ScopedSetVelocity, 1), "Vec2Scale -> SetVelocity.value");
    Check(ScopedGraph.AddLink(ScopedVelocity, 0, ScopedScale, 0), "SelfVelocity -> Vec2Scale.vec");
    Check(ScopedGraph.AddLink(ScopedHalf, 0, ScopedScale, 1), "ConstFloat -> Vec2Scale.scalar");

    LogicCompileResult ScopedCompiled;
    Check(CompileLogicGraph(ScopedGraph, &ScopedCompiled), "half-velocity graph compiles");
    if (!ScopedCompiled.program.empty())
    {
        LogicCompileResult ScopedAgain;
        Check(CompileLogicGraph(ScopedGraph, &ScopedAgain) && ScopedAgain.program == ScopedCompiled.program,
              "graph compile is deterministic");
    }

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "script runtime initializes");
    std::string ScopedError;
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedCompiled.program, &ScopedError);
    Check(ScopedHandle != Fby::kScriptNoProgram, "compiled graph loads into the runtime");

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{Fby::Vector2D{2.0f, 4.0f}});

    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    ScopedContext.rng = &ScopedRuntime.Rng();
    Fby::Script::VmFault ScopedFault;
    Check(Fby::RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, Fby::Script::ScriptEvent::Update, ScopedContext,
                             &ScopedFault),
          "graph program runs against EnTT storage");
    Check(ScopedFault.Ok(), "no VM fault while writing a Velocity field");

    const Fby::Velocity& ScopedResult = ScopedRegistry.get<Fby::Velocity>(ScopedEntity);
    CheckFloat(ScopedResult.Value.X, 1.0f, "velocity.x halved through Mirror offset");
    CheckFloat(ScopedResult.Value.Y, 2.0f, "velocity.y halved through Mirror offset");
    return true;
}

bool TestGraphCycleRejected()
{
    using namespace Fby::Script;
    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedFirst = ScopedGraph.AddNode(LogicNodeKind::Add);
    const std::uint16_t ScopedSecond = ScopedGraph.AddNode(LogicNodeKind::Add);
    const std::uint16_t ScopedLog = ScopedGraph.AddNode(LogicNodeKind::LogValue);
    Check(ScopedGraph.AddLink(ScopedEvent, 0, ScopedLog, 0), "event -> Log");
    Check(ScopedGraph.AddLink(ScopedFirst, 0, ScopedSecond, 0), "cycle link a->b");
    Check(ScopedGraph.AddLink(ScopedSecond, 0, ScopedFirst, 0), "cycle link b->a");
    Check(ScopedGraph.AddLink(ScopedFirst, 0, ScopedLog, 1), "cycle feed into Log");
    LogicCompileResult ScopedCompiled;
    Check(!CompileLogicGraph(ScopedGraph, &ScopedCompiled), "cyclic graph is rejected");
    Check(ScopedCompiled.error.find("cycle") != std::string::npos, "cycle error names the problem");
    return true;
}

bool TestGraphTypeMismatchRejected()
{
    using namespace Fby::Script;
    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedSetVelocity = ScopedGraph.AddNode(LogicNodeKind::SetVelocity);
    const std::uint16_t ScopedWrong =
        ScopedGraph.AddNode(LogicNodeKind::ConstFloat, std::bit_cast<std::uint32_t>(1.0f));
    Check(ScopedGraph.AddLink(ScopedEvent, 0, ScopedSetVelocity, 0), "event -> SetVelocity");
    Check(ScopedGraph.AddLink(ScopedWrong, 0, ScopedSetVelocity, 1), "float -> Vec2 input");
    LogicCompileResult ScopedCompiled;
    Check(!CompileLogicGraph(ScopedGraph, &ScopedCompiled), "float cannot feed a Vec2 pin");
    Check(!ScopedCompiled.error.empty(), "type mismatch has a readable error");
    return true;
}

bool TestLayoutHashMismatchRejected()
{
    using namespace Fby::Script;
    ProgramBuilder ScopedBuilder;
    const std::uint16_t ScopedComponent = ScopedBuilder.AddComponent(0xDEADBEEFCAFEF00Dull, sizeof(Fby::Transform2D));
    const std::uint16_t ScopedField = ScopedBuilder.AddField(ScopedComponent, Fby::Reflect::FieldKind::Vector2D, 0, 8);
    ScopedBuilder.Emit(OpCode::LoadSelf, 0);
    ScopedBuilder.Emit(OpCode::LoadFieldVec2, 1, 0, ScopedField);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
    Check(!ScopedBytes.empty(), "layout-hash test program builds");

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "runtime for layout test initializes");
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedBytes, &ScopedError);
    Check(ScopedHandle == Fby::kScriptNoProgram, "program with an unknown LayoutHash is rejected");
    Check(ScopedError.find("LayoutHash") != std::string::npos, "layout rejection mentions LayoutHash");
    return true;
}

bool TestMissingComponentFault()
{
    using namespace Fby::Script;
    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedLog = ScopedGraph.AddNode(LogicNodeKind::LogValue);
    const std::uint16_t ScopedRotation = ScopedGraph.AddNode(LogicNodeKind::SelfRotation);
    Check(ScopedGraph.AddLink(ScopedEvent, 0, ScopedLog, 0), "event -> Log");
    Check(ScopedGraph.AddLink(ScopedRotation, 0, ScopedLog, 1), "SelfRotation -> Log");
    LogicCompileResult ScopedCompiled;
    Check(CompileLogicGraph(ScopedGraph, &ScopedCompiled), "rotation graph compiles");

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "runtime for fault test initializes");
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedCompiled.program);
    Check(ScopedHandle != Fby::kScriptNoProgram, "rotation program loads");

    entt::registry ScopedRegistry;
    // Deliberately no Transform2D
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    Fby::Script::VmFault ScopedFault;
    Check(!Fby::RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, Fby::Script::ScriptEvent::Update,
                              ScopedContext, &ScopedFault),
          "missing component faults instead of reading garbage");
    Check(ScopedFault.code == Fby::Script::VmFault::Code::MissingComponent, "fault code names the missing component");
    return true;
}

// The hot path must not allocate.
bool TestNoAllocationInHotPath()
{
    using namespace Fby::Script;
    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedSetVelocity = ScopedGraph.AddNode(LogicNodeKind::SetVelocity);
    const std::uint16_t ScopedVelocity = ScopedGraph.AddNode(LogicNodeKind::SelfVelocity);
    Check(ScopedGraph.AddLink(ScopedEvent, 0, ScopedSetVelocity, 0), "no-alloc event -> SetVelocity");
    Check(ScopedGraph.AddLink(ScopedVelocity, 0, ScopedSetVelocity, 1), "no-alloc SelfVelocity -> SetVelocity");
    LogicCompileResult ScopedCompiled;
    Check(CompileLogicGraph(ScopedGraph, &ScopedCompiled), "no-alloc graph compiles");

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "runtime for allocation test initializes");
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedCompiled.program);
    Check(ScopedHandle != Fby::kScriptNoProgram, "no-alloc program loads");

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{});
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    ScopedContext.rng = &ScopedRuntime.Rng();

    // Warm up EnTT's storage lookups so the counter measures only steady state.
    Check(
        Fby::RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, Fby::Script::ScriptEvent::Update, ScopedContext),
        "warm-up run succeeds");
    const std::uint64_t ScopedBefore = GAllocationCount;
    for (int ScopedI = 0; ScopedI < 1000; ++ScopedI)
    {
        (void)Fby::RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, Fby::Script::ScriptEvent::Update,
                                 ScopedContext);
    }
    const std::uint64_t ScopedAfter = GAllocationCount;
    Check(ScopedBefore == ScopedAfter, "1000 script updates allocate nothing");
    return true;
}

bool TestMalformedBindingsRejected()
{
    using namespace Fby::Script;
    std::string ScopedError;

    // A two-argument native called at the last register must be rejected at load
    {
        ProgramBuilder ScopedBuilder;
        const std::uint16_t ScopedConstant = ScopedBuilder.AddConstant(1.0f);
        const std::uint16_t ScopedNative = ScopedBuilder.AddNative(NativeId::RandomRange);
        ScopedBuilder.Emit(OpCode::LoadF32, 127, 0, ScopedConstant);
        ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedNative);
        ScopedBuilder.Emit(OpCode::Halt);
        (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
        const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
        CompiledProgram ScopedParsed;
        Check(!ScopedBytes.empty() && ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError),
              "native-bound program builds and parses");
        auto* ScopedCode = const_cast<Instruction*>(ScopedParsed.code.data());
        ScopedCode[1].dst = 127;
        Check(!ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError),
              "native call past the register file is rejected");
    }

    // StoreFieldI copies from a 4-byte register; an 8-byte field binding would
    {
        ProgramBuilder ScopedBuilder;
        const std::uint16_t ScopedComponent = ScopedBuilder.AddComponent(0x1234u, 8);
        const std::uint16_t ScopedField =
            ScopedBuilder.AddField(ScopedComponent, Fby::Reflect::FieldKind::UInt32, 0, 4);
        ScopedBuilder.Emit(OpCode::LoadI32, 0, 0, 1);
        ScopedBuilder.Emit(OpCode::StoreFieldI, 0, 0, ScopedField);
        ScopedBuilder.Emit(OpCode::Halt);
        (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
        const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
        CompiledProgram ScopedParsed;
        Check(!ScopedBytes.empty() && ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError),
              "integer field program builds and parses");
        auto* ScopedFields = const_cast<FieldBinding*>(ScopedParsed.fields.data());
        ScopedFields[0].size = 8;
        Check(!ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError),
              "integer store wider than a register is rejected");
    }

    // A zero-sized float field would make the VM read four bytes past the
    {
        ProgramBuilder ScopedBuilder;
        const std::uint16_t ScopedComponent = ScopedBuilder.AddComponent(0x5678u, 4);
        const std::uint16_t ScopedField =
            ScopedBuilder.AddField(ScopedComponent, Fby::Reflect::FieldKind::Float, 0, 4);
        ScopedBuilder.Emit(OpCode::StoreFieldF, 0, 0, ScopedField);
        ScopedBuilder.Emit(OpCode::Halt);
        (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
        const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
        CompiledProgram ScopedParsed;
        Check(!ScopedBytes.empty() && ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError),
              "float field program builds and parses");
        auto* ScopedFields = const_cast<FieldBinding*>(ScopedParsed.fields.data());
        ScopedFields[0].size = 0;
        Check(!ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError), "zero-sized float field is rejected");
    }

    return true;
}

bool TestProgramUnload()
{
    using namespace Fby::Script;
    ProgramBuilder ScopedBuilder;
    const std::uint16_t ScopedConstant = ScopedBuilder.AddConstant(1.0f);
    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedConstant);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);
    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(0, 0, &ScopedError);
    Check(!ScopedBytes.empty(), "unload test program builds");

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "unload test runtime initializes");
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedBytes, &ScopedError);
    Check(ScopedHandle != Fby::kScriptNoProgram, "unload test program loads");
    Check(ScopedRuntime.ProgramCount() == 1, "program count increments on load");
    Check(ScopedRuntime.UnloadProgram(ScopedHandle), "program unloads");
    Check(ScopedRuntime.ProgramCount() == 0, "program count decrements on unload");
    Check(!ScopedRuntime.UnloadProgram(ScopedHandle), "double unload is rejected");
    return true;
}

bool TestTextScript()
{
    using namespace Fby::Script;
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    constexpr std::string_view ScopedSource = R"FBY(
event update {
    let half = 0.5;
    self.Transform2D.Rotation = self.Transform2D.Rotation + half;
}
)FBY";
    std::string ScopedError;
    std::vector<std::byte> ScopedProgram;
    Check(CompileTextScript(ScopedSource, ScopedComponents, {}, &ScopedProgram, &ScopedError),
          "text script compiles");
    if (ScopedProgram.empty())
    {
        std::printf("text compile error: %s\n", ScopedError.c_str());
        return false;
    }
    CompiledProgram ScopedParsed;
    Check(ParseProgram(ScopedProgram, &ScopedParsed, &ScopedError), "text script program parses");

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "text runtime initializes");
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedProgram, &ScopedError);
    Check(ScopedHandle != Fby::kScriptNoProgram, "text program loads");
    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    Check(Fby::RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, ScriptEvent::Update, ScopedContext),
          "text program runs");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation == 0.5f,
          "text program writes a component field");
    return true;
}
}

void* operator new(std::size_t InSize)
{
    ++GAllocationCount;
    void* ScopedMemory = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedMemory == nullptr)
    {
        std::abort();
    }
    return ScopedMemory;
}

void operator delete(void* InMemory) noexcept
{
    std::free(InMemory);
}

void operator delete(void* InMemory, std::size_t) noexcept
{
    std::free(InMemory);
}

int main()
{
    TestArithmetic();
    TestBranch();
    TestSignedIntegerImmediate();
    TestCorruptProgramRejected();
    TestMalformedBindingsRejected();
    TestProgramUnload();
    TestTextScript();
    TestGraphHalfVelocity();
    TestGraphCycleRejected();
    TestGraphTypeMismatchRejected();
    TestLayoutHashMismatchRejected();
    TestMissingComponentFault();
    TestNoAllocationInHotPath();

    if (GFailures != 0)
    {
        std::printf("%d script test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("script_vm_test: all tests passed\n");
    return 0;
}
