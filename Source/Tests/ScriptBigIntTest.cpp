// FemboyEngine2D - script-visible big integer acceptance test.
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Script/AotTranspiler.hpp"
#include "Engine/Script/Bytecode.hpp"
#include "Engine/Script/EcsBridge.hpp"
#include "Engine/Script/GraphCompiler.hpp"
#include "Engine/Script/ScriptSystem.hpp"
#include "Engine/Script/TextScript.hpp"
#include "Engine/Script/Vm.hpp"

#include <bit>
#include <cstdint>
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
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void CheckFloat(float InActual, float InExpected, const char* InMessage)
{
    if (InActual != InExpected)
    {
        std::fprintf(stderr, "FAIL: %s (actual %f, expected %f)\n", InMessage, static_cast<double>(InActual),
                     static_cast<double>(InExpected));
        ++GFailures;
    }
}

struct Harness
{
    Fby::ScriptRuntime Runtime{};
    Fby::Script::EcsBridge Bridge{};
    entt::registry Registry{};
    Fby::Script::VmContext Context{Registry};
    Fby::Script::BoundProgram Bound{};

    Harness()
    {
        Context.bigNumbers = &Runtime.BigNumbers();
    }
};

bool RunProgram(Fby::Script::ProgramBuilder& InBuilder, Harness& InHarness)
{
    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = InBuilder.Build(1, 2, &ScopedError);
    if (ScopedBytes.empty())
    {
        std::fprintf(stderr, "build failed: %s\n", ScopedError.c_str());
        return false;
    }
    Fby::Script::CompiledProgram ScopedParsed;
    if (!Fby::Script::ParseProgram(ScopedBytes, &ScopedParsed, &ScopedError))
    {
        std::fprintf(stderr, "parse failed: %s\n", ScopedError.c_str());
        return false;
    }
    if (!Fby::Script::BindProgram(ScopedParsed, InHarness.Runtime.Bridge(), &InHarness.Bound, &ScopedError))
    {
        std::fprintf(stderr, "bind failed: %s\n", ScopedError.c_str());
        return false;
    }
    return InHarness.Runtime.Vm().Run(InHarness.Bound, Fby::Script::ScriptEvent::Update, InHarness.Registry.create(),
                                     InHarness.Context);
}

void TestAddAndToInt()
{
    using namespace Fby::Script;

    ProgramBuilder ScopedBuilder;
    const auto ScopedFromInt = ScopedBuilder.AddNative(NativeId::BigIntFromInt);
    const auto ScopedAdd = ScopedBuilder.AddNative(NativeId::BigIntAdd);
    const auto ScopedToInt = ScopedBuilder.AddNative(NativeId::BigIntToInt);
    const std::uint16_t ScopedFive = ScopedBuilder.AddConstant(5.0f);
    const std::uint16_t ScopedSeven = ScopedBuilder.AddConstant(7.0f);

    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedFive);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::LoadF32, 1, 0, ScopedSeven);
    ScopedBuilder.Emit(OpCode::CallNative, 1, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedAdd);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedToInt);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);

    Harness ScopedHarness;
    Check(ScopedHarness.Runtime.Initialize(), "script runtime initializes");
    Check(RunProgram(ScopedBuilder, ScopedHarness), "big add program runs");
    CheckFloat(ScopedHarness.Runtime.Vm().F(0), 12.0f, "big add 5 + 7");
}

void TestMul()
{
    using namespace Fby::Script;

    ProgramBuilder ScopedBuilder;
    const auto ScopedFromInt = ScopedBuilder.AddNative(NativeId::BigIntFromInt);
    const auto ScopedMul = ScopedBuilder.AddNative(NativeId::BigIntMul);
    const auto ScopedToInt = ScopedBuilder.AddNative(NativeId::BigIntToInt);
    const std::uint16_t ScopedTwenty = ScopedBuilder.AddConstant(20.0f);
    const std::uint16_t ScopedSix = ScopedBuilder.AddConstant(6.0f);

    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedTwenty);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::LoadF32, 1, 0, ScopedSix);
    ScopedBuilder.Emit(OpCode::CallNative, 1, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::MoveF, 2, 0, 0);
    ScopedBuilder.Emit(OpCode::MoveF, 3, 1, 0);
    ScopedBuilder.Emit(OpCode::CallNative, 2, 0, ScopedMul);
    ScopedBuilder.Emit(OpCode::CallNative, 2, 0, ScopedToInt);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);

    Harness ScopedHarness;
    Check(ScopedHarness.Runtime.Initialize(), "script runtime initializes for mul");
    Check(RunProgram(ScopedBuilder, ScopedHarness), "big mul program runs");
    CheckFloat(ScopedHarness.Runtime.Vm().F(2), 120.0f, "big mul 20 * 6");
}

void TestDivMod()
{
    using namespace Fby::Script;

    ProgramBuilder ScopedBuilder;
    const auto ScopedFromInt = ScopedBuilder.AddNative(NativeId::BigIntFromInt);
    const auto ScopedDiv = ScopedBuilder.AddNative(NativeId::BigIntDiv);
    const auto ScopedMod = ScopedBuilder.AddNative(NativeId::BigIntMod);
    const auto ScopedToInt = ScopedBuilder.AddNative(NativeId::BigIntToInt);
    const std::uint16_t ScopedTwenty = ScopedBuilder.AddConstant(20.0f);
    const std::uint16_t ScopedSix = ScopedBuilder.AddConstant(6.0f);

    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedTwenty);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::LoadF32, 1, 0, ScopedSix);
    ScopedBuilder.Emit(OpCode::CallNative, 1, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::MoveF, 2, 0, 0);
    ScopedBuilder.Emit(OpCode::MoveF, 3, 1, 0);
    ScopedBuilder.Emit(OpCode::CallNative, 2, 0, ScopedDiv);
    ScopedBuilder.Emit(OpCode::MoveF, 4, 2, 0);
    ScopedBuilder.Emit(OpCode::CallNative, 4, 0, ScopedToInt);
    ScopedBuilder.Emit(OpCode::MoveF, 2, 0, 0);
    ScopedBuilder.Emit(OpCode::MoveF, 3, 1, 0);
    ScopedBuilder.Emit(OpCode::CallNative, 2, 0, ScopedMod);
    ScopedBuilder.Emit(OpCode::CallNative, 2, 0, ScopedToInt);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);

    Harness ScopedHarness;
    Check(ScopedHarness.Runtime.Initialize(), "script runtime initializes for div");
    Check(RunProgram(ScopedBuilder, ScopedHarness), "big div program runs");
    CheckFloat(ScopedHarness.Runtime.Vm().F(4), 3.0f, "big div 20 / 6");
    CheckFloat(ScopedHarness.Runtime.Vm().F(2), 2.0f, "big mod 20 % 6");
}

void TestCompare()
{
    using namespace Fby::Script;

    ProgramBuilder ScopedBuilder;
    const auto ScopedFromInt = ScopedBuilder.AddNative(NativeId::BigIntFromInt);
    const auto ScopedCmp = ScopedBuilder.AddNative(NativeId::BigIntCmp);
    const std::uint16_t ScopedOne = ScopedBuilder.AddConstant(1.0f);
    const std::uint16_t ScopedTwo = ScopedBuilder.AddConstant(2.0f);

    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedOne);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::LoadF32, 1, 0, ScopedTwo);
    ScopedBuilder.Emit(OpCode::CallNative, 1, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedCmp);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);

    Harness ScopedHarness;
    Check(ScopedHarness.Runtime.Initialize(), "script runtime initializes for compare");
    Check(RunProgram(ScopedBuilder, ScopedHarness), "big compare program runs");
    CheckFloat(ScopedHarness.Runtime.Vm().F(0), -1.0f, "big compare 1 < 2");
}

void TestTextScript()
{
    using namespace Fby::Script;

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    constexpr std::string_view kSource = R"FBY(
event update {
    let a = BigIntFromInt(20);
    let b = BigIntFromInt(6);
    self.Transform2D.Rotation = BigIntToInt(BigIntMul(a, b));
}
)FBY";

    std::string ScopedError;
    std::vector<std::byte> ScopedProgram;
    Check(CompileTextScript(kSource, ScopedComponents, {}, &ScopedProgram, &ScopedError),
          "big integer text script compiles");

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "big integer text runtime initializes");
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedProgram, &ScopedError);
    Check(ScopedHandle != Fby::kScriptNoProgram, "big integer text program loads");

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    ScopedContext.bigNumbers = &ScopedRuntime.BigNumbers();
    Check(Fby::RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, ScriptEvent::Update, ScopedContext),
          "big integer text program runs");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation == 120.0f,
          "big integer text program writes the product");

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 100; ++ScopedIteration)
    {
        Check(Fby::RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, ScriptEvent::Update, ScopedContext),
              "big integer text program remains runnable");
    }
    Check(ScopedRuntime.BigNumbers().Used() <= 8, "big integer tokens reset at the event boundary");
}

void TestDecimalTextScript()
{
    using namespace Fby::Script;

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    constexpr std::string_view kSource = R"FBY(
event update {
    let a = DecimalFromInt(6);
    let b = DecimalFromInt(2);
    self.Transform2D.Rotation = DecimalToInt(DecimalDivRounded(a, b, 0));
}
)FBY";

    std::string ScopedError;
    std::vector<std::byte> ScopedProgram;
    Check(CompileTextScript(kSource, ScopedComponents, {}, &ScopedProgram, &ScopedError),
          "decimal text script compiles");

    Fby::ScriptRuntime ScopedRuntime;
    Check(ScopedRuntime.Initialize(), "decimal text runtime initializes");
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedProgram, &ScopedError);
    Check(ScopedHandle != Fby::kScriptNoProgram, "decimal text program loads");

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    ScopedContext.bigNumbers = &ScopedRuntime.BigNumbers();
    ScopedContext.decimals = &ScopedRuntime.Decimals();
    Check(Fby::RunScriptOnce(ScopedRuntime, ScopedHandle, ScopedEntity, ScriptEvent::Update, ScopedContext),
          "decimal text program runs");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation == 3.0f,
          "decimal text program writes the quotient");
}

void TestAotTranspile()
{
    using namespace Fby::Script;

    ProgramBuilder ScopedBuilder;
    const auto ScopedFromInt = ScopedBuilder.AddNative(NativeId::BigIntFromInt);
    const auto ScopedAdd = ScopedBuilder.AddNative(NativeId::BigIntAdd);
    const auto ScopedToInt = ScopedBuilder.AddNative(NativeId::BigIntToInt);
    const std::uint16_t ScopedOne = ScopedBuilder.AddConstant(1.0f);
    const std::uint16_t ScopedTwo = ScopedBuilder.AddConstant(2.0f);
    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedOne);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::LoadF32, 1, 0, ScopedTwo);
    ScopedBuilder.Emit(OpCode::CallNative, 1, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedAdd);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedToInt);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);

    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(1, 2, &ScopedError);
    CompiledProgram ScopedProgram;
    Check(ParseProgram(ScopedBytes, &ScopedProgram, &ScopedError), "big AOT program parses");

    std::string ScopedSource;
    Check(TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "big AOT program transpiles");
    Check(ScopedSource.find("ctx.BigIntFromInt") != std::string::npos, "AOT emits BigIntFromInt");
    Check(ScopedSource.find("ctx.BigIntAdd") != std::string::npos, "AOT emits BigIntAdd");
    Check(ScopedSource.find("ctx.BigIntToInt") != std::string::npos, "AOT emits BigIntToInt");
}

void TestDecimalAotTranspile()
{
    using namespace Fby::Script;

    ProgramBuilder ScopedBuilder;
    const auto ScopedFromInt = ScopedBuilder.AddNative(NativeId::DecimalFromInt);
    const auto ScopedAdd = ScopedBuilder.AddNative(NativeId::DecimalAdd);
    const auto ScopedDivRounded = ScopedBuilder.AddNative(NativeId::DecimalDivRounded);
    const auto ScopedToInt = ScopedBuilder.AddNative(NativeId::DecimalToInt);
    const std::uint16_t ScopedOne = ScopedBuilder.AddConstant(1.0f);
    const std::uint16_t ScopedTwo = ScopedBuilder.AddConstant(2.0f);
    const std::uint16_t ScopedZero = ScopedBuilder.AddConstant(0.0f);
    ScopedBuilder.Emit(OpCode::LoadF32, 0, 0, ScopedOne);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::LoadF32, 1, 0, ScopedTwo);
    ScopedBuilder.Emit(OpCode::CallNative, 1, 0, ScopedFromInt);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedAdd);
    ScopedBuilder.Emit(OpCode::LoadF32, 2, 0, ScopedZero);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedDivRounded);
    ScopedBuilder.Emit(OpCode::CallNative, 0, 0, ScopedToInt);
    ScopedBuilder.Emit(OpCode::Halt);
    (void)ScopedBuilder.AddFunction(ScriptEvent::Update, 0);

    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = ScopedBuilder.Build(1, 2, &ScopedError);
    CompiledProgram ScopedProgram;
    Check(ParseProgram(ScopedBytes, &ScopedProgram, &ScopedError), "decimal AOT program parses");
    std::string ScopedSource;
    Check(TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "decimal AOT program transpiles");
    Check(ScopedSource.find("ctx.DecimalFromInt") != std::string::npos, "AOT emits DecimalFromInt");
    Check(ScopedSource.find("ctx.DecimalAdd") != std::string::npos, "AOT emits DecimalAdd");
    Check(ScopedSource.find("ctx.DecimalDivRounded") != std::string::npos, "AOT emits DecimalDivRounded");
    Check(ScopedSource.find("ctx.DecimalToInt") != std::string::npos, "AOT emits DecimalToInt");
}

void TestDecimalVisualGraph()
{
    using namespace Fby::Script;

    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedTwenty = ScopedGraph.AddNode(LogicNodeKind::ConstFloat,
                                                           std::bit_cast<std::uint32_t>(20.0f));
    const std::uint16_t ScopedTwentyDecimal = ScopedGraph.AddNode(LogicNodeKind::DecimalFromInt);
    const std::uint16_t ScopedSix = ScopedGraph.AddNode(LogicNodeKind::ConstFloat, std::bit_cast<std::uint32_t>(6.0f));
    const std::uint16_t ScopedSixDecimal = ScopedGraph.AddNode(LogicNodeKind::DecimalFromInt);
    const std::uint16_t ScopedDiv = ScopedGraph.AddNode(LogicNodeKind::DecimalDivRounded);
    const std::uint16_t ScopedMode = ScopedGraph.AddNode(LogicNodeKind::ConstFloat, std::bit_cast<std::uint32_t>(0.0f));
    const std::uint16_t ScopedLog = ScopedGraph.AddNode(LogicNodeKind::DecimalLog);
    Check(ScopedGraph.AddLink(ScopedEvent, 0, ScopedLog, 0), "decimal graph event links");
    Check(ScopedGraph.AddLink(ScopedTwenty, 0, ScopedTwentyDecimal, 0), "decimal graph 20 links");
    Check(ScopedGraph.AddLink(ScopedSix, 0, ScopedSixDecimal, 0), "decimal graph 6 links");
    Check(ScopedGraph.AddLink(ScopedTwentyDecimal, 0, ScopedDiv, 0), "decimal graph left links");
    Check(ScopedGraph.AddLink(ScopedSixDecimal, 0, ScopedDiv, 1), "decimal graph right links");
    Check(ScopedGraph.AddLink(ScopedMode, 0, ScopedDiv, 2), "decimal graph mode links");
    Check(ScopedGraph.AddLink(ScopedDiv, 0, ScopedLog, 1), "decimal graph result links");

    LogicCompileResult ScopedCompiled;
    Check(CompileLogicGraph(ScopedGraph, {}, &ScopedCompiled), "decimal logic graph compiles");
    Check(!ScopedCompiled.program.empty(), "decimal logic graph emits bytecode");
    CompiledProgram ScopedProgram;
    std::string ScopedError;
    Check(ParseProgram(ScopedCompiled.program, &ScopedProgram, &ScopedError), "decimal graph parses");
    std::string ScopedSource;
    Check(TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "decimal graph transpiles");
    Check(ScopedSource.find("ctx.DecimalDivRounded") != std::string::npos,
          "visual decimal graph emits DecimalDivRounded");
}

void TestVisualGraph()
{
    using namespace Fby::Script;

    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedTwenty = ScopedGraph.AddNode(LogicNodeKind::ConstFloat,
                                                           std::bit_cast<std::uint32_t>(20.0f));
    const std::uint16_t ScopedSix = ScopedGraph.AddNode(LogicNodeKind::ConstFloat, std::bit_cast<std::uint32_t>(6.0f));
    const std::uint16_t ScopedTwentyBig = ScopedGraph.AddNode(LogicNodeKind::BigIntFromInt);
    const std::uint16_t ScopedSixBig = ScopedGraph.AddNode(LogicNodeKind::BigIntFromInt);
    const std::uint16_t ScopedMul = ScopedGraph.AddNode(LogicNodeKind::BigIntMul);
    const std::uint16_t ScopedLog = ScopedGraph.AddNode(LogicNodeKind::BigIntLog);
    Check(ScopedGraph.AddLink(ScopedEvent, 0, ScopedLog, 0), "graph event links to big log");
    Check(ScopedGraph.AddLink(ScopedTwenty, 0, ScopedTwentyBig, 0), "graph 20 links to big 20");
    Check(ScopedGraph.AddLink(ScopedSix, 0, ScopedSixBig, 0), "graph 6 links to big 6");
    Check(ScopedGraph.AddLink(ScopedTwentyBig, 0, ScopedMul, 0), "graph big 20 links to mul");
    Check(ScopedGraph.AddLink(ScopedSixBig, 0, ScopedMul, 1), "graph big 6 links to mul");
    Check(ScopedGraph.AddLink(ScopedMul, 0, ScopedLog, 1), "graph mul links to log");

    LogicCompileResult ScopedCompiled;
    Check(CompileLogicGraph(ScopedGraph, {}, &ScopedCompiled), "big integer logic graph compiles");
    if (!ScopedCompiled.error.empty())
    {
        std::fprintf(stderr, "big graph compile error: %s\n", ScopedCompiled.error.c_str());
    }
    Check(!ScopedCompiled.program.empty(), "big integer logic graph emits bytecode");

    CompiledProgram ScopedProgram;
    std::string ScopedError;
    Check(ParseProgram(ScopedCompiled.program, &ScopedProgram, &ScopedError), "big integer graph parses");
    std::string ScopedSource;
    Check(TranspileProgramToCpp(ScopedProgram, &ScopedSource, &ScopedError), "big integer graph transpiles");
    Check(ScopedSource.find("ctx.BigIntMul") != std::string::npos, "visual graph emits BigIntMul");
}
}

int main()
{
    TestAddAndToInt();
    TestMul();
    TestDivMod();
    TestCompare();
    TestTextScript();
    TestDecimalTextScript();
    TestAotTranspile();
    TestDecimalAotTranspile();
    TestVisualGraph();
    TestDecimalVisualGraph();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d script big integer test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("script big integer tests: ok\n");
    return 0;
}
