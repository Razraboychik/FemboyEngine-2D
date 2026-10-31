// FemboyEngine2D - cook-time FemboyScript -> C++26 transpiler.
#pragma once

#include "Engine/Script/Bytecode.hpp"

#include <string>
#include <string_view>

namespace Fby::Script
{

struct AotTranspileOptions
{
    std::string_view programName = "fby_program";
    // Keep the generated code readable: the editor shows it in a read-only pane
    bool emitComments = true;
};

// Produces a complete C++26 translation unit. Returns false when the program
[[nodiscard]] bool TranspileProgramToCpp(const CompiledProgram& InProgram, const AotTranspileOptions& InOptions,
                                         std::string* InSource, std::string* InError = nullptr);

[[nodiscard]] inline bool TranspileProgramToCpp(const CompiledProgram& InProgram, std::string* InSource,
                                                std::string* InError = nullptr)
{
    return TranspileProgramToCpp(InProgram, AotTranspileOptions{}, InSource, InError);
}
}
