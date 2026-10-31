// FemboyEngine2D - toolchain and build-option metadata.
#pragma once

#include <string>
#include <string_view>

namespace Fby::Core
{

// Compiled-in description of the toolchain, build type, options and pinned
// dependencies. All strings are static: no allocation on read.
struct BuildInfo
{
    std::string_view compiler;
    std::string_view buildType;
    std::string_view platform;
    std::string_view flags;
    std::string_view dependencies;
};

[[nodiscard]] const BuildInfo& GetBuildInfo() noexcept;

// One line for `--version`, logs and crash reports: compiler, build type,
// platform, flags and the dependency lock.
[[nodiscard]] std::string FormatBuildInfo();
}
