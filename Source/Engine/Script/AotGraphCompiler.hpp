// FemboyEngine2D - LogicGraph -> C++26 AOT front end.
#pragma once

#include "Engine/Script/AotTranspiler.hpp"
#include "Engine/Script/GraphCompiler.hpp"

#include <cstdint>
#include <string>

namespace Fby::Script
{

struct AotGraphTranspileOptions
{
    LogicCompileOptions compile{};
    AotTranspileOptions transpile{};
};

// Compiles `graph` to bytecode and then to a complete C++26 translation unit.
[[nodiscard]] bool TranspileLogicGraphToCpp(const LogicGraph& InGraph, const AotGraphTranspileOptions& InOptions,
                                            std::string* InSource, LogicCompileResult* InProgramOut = nullptr,
                                            std::string* InError = nullptr);

[[nodiscard]] inline bool TranspileLogicGraphToCpp(const LogicGraph& InGraph, std::string* InSource,
                                                   LogicCompileResult* InProgramOut = nullptr,
                                                   std::string* InError = nullptr)
{
    return TranspileLogicGraphToCpp(InGraph, AotGraphTranspileOptions{}, InSource, InProgramOut, InError);
}
}
