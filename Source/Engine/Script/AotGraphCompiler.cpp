// FemboyEngine2D - LogicGraph -> canonical bytecode -> C++26.
#include "Engine/Script/AotGraphCompiler.hpp"

#include <utility>

namespace Fby::Script
{

bool TranspileLogicGraphToCpp(const LogicGraph& InGraph, const AotGraphTranspileOptions& InOptions,
                              std::string* InSource, LogicCompileResult* InProgramOut, std::string* InError)
{
    if (InSource == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "TranspileLogicGraphToCpp: null source output";
        }
        return false;
    }

    LogicCompileResult ScopedCompiled;
    if (!CompileLogicGraph(InGraph, InOptions.compile, &ScopedCompiled))
    {
        if (InError != nullptr)
        {
            *InError = ScopedCompiled.error;
        }
        return false;
    }

    CompiledProgram ScopedParsed;
    std::string ScopedParseError;
    if (!ParseProgram(ScopedCompiled.program, &ScopedParsed, &ScopedParseError))
    {
        if (InError != nullptr)
        {
            *InError = ScopedParseError;
        }
        return false;
    }

    std::string ScopedGenerated;
    std::string ScopedTranspileError;
    if (!TranspileProgramToCpp(ScopedParsed, InOptions.transpile, &ScopedGenerated, &ScopedTranspileError))
    {
        if (InError != nullptr)
        {
            *InError = ScopedTranspileError;
        }
        return false;
    }

    *InSource = std::move(ScopedGenerated);
    if (InProgramOut != nullptr)
    {
        *InProgramOut = std::move(ScopedCompiled);
    }
    if (InError != nullptr)
    {
        InError->clear();
    }
    return true;
}
}
