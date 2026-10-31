// FemboyEngine2D - checks the compiled-in build metadata.
#include "Engine/Core/BuildInfo.hpp"

#include <cstdio>
#include <string>

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
}

int main()
{
    const Fby::Core::BuildInfo& ScopedInfo = Fby::Core::GetBuildInfo();
    Check(!ScopedInfo.compiler.empty(), "compiler is recorded");
    Check(ScopedInfo.compiler.find("Clang") != std::string_view::npos ||
              ScopedInfo.compiler.find("GNU") != std::string_view::npos ||
              ScopedInfo.compiler.find("MSVC") != std::string_view::npos,
          "compiler identifies the toolchain");
    Check(!ScopedInfo.buildType.empty(), "build type is recorded");
    Check(!ScopedInfo.platform.empty(), "platform is recorded");
    Check(ScopedInfo.flags.find("std:c++") != std::string_view::npos ||
              ScopedInfo.flags.find("-std=") != std::string_view::npos,
          "language standard flag is recorded");
    Check(ScopedInfo.dependencies.find("SDL3=") != std::string_view::npos, "SDL3 lock is recorded");
    Check(ScopedInfo.dependencies.find("glslang=") != std::string_view::npos, "glslang lock is recorded");
    Check(ScopedInfo.dependencies.find("Box2D=") != std::string_view::npos, "Box2D tree pin is recorded");
    Check(ScopedInfo.dependencies.find("Opus=") != std::string_view::npos, "Opus tree pin is recorded");

    const std::string ScopedText = Fby::Core::FormatBuildInfo();
    Check(ScopedText.find("compiler=") == 0, "formatted info starts with the compiler");
    Check(ScopedText.find("deps=") != std::string::npos, "formatted info includes the lock");

    if (GFailures == 0)
    {
        std::printf("build_info: OK (%s, %s)\n", std::string(ScopedInfo.compiler).c_str(),
                    std::string(ScopedInfo.buildType).c_str());
        return 0;
    }
    std::fprintf(stderr, "build_info: %d failure(s)\n", GFailures);
    return 1;
}
