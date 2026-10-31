// FemboyEngine2D - FemboyScript textual front-end.
#pragma once

#include "Engine/ECS/ComponentRegistry.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Script
{

struct TextCompileOptions
{
    std::uint64_t sourceHash = 0;
    std::uint64_t graphHash = 0;
    // Name the debugger shows in its source view.
    std::string_view sourceName = "<text>";
};

// Compiles the 1.0 text subset to canonical FemboyScript bytecode.
// Grammar:
//   event update { statement* }
//   let name = expression;
//   name = expression;
//   self.Component.Field = expression;
//   if (expression) { statement* } [else { statement* }]
//   native(args...);
// Expressions are float-valued and support + - * / comparisons and parentheses.
[[nodiscard]] bool CompileTextScript(std::string_view InSource, const Scene::ComponentRegistry& InComponents,
                                     const TextCompileOptions& InOptions, std::vector<std::byte>* OutProgram,
                                     std::string* InError = nullptr);
}
