// FemboyEngine2D - toolchain and build-option metadata.
#include "Engine/Core/BuildInfo.hpp"

#include "BuildInfo.generated.hpp"

namespace Fby::Core
{

const BuildInfo& GetBuildInfo() noexcept
{
    static const BuildInfo ScopedInfo{Detail::kBuildCompiler, Detail::kBuildType, Detail::kBuildPlatform,
                                      Detail::kBuildFlags, Detail::kBuildDependencies};
    return ScopedInfo;
}

std::string FormatBuildInfo()
{
    const BuildInfo& ScopedInfo = GetBuildInfo();
    std::string ScopedText;
    ScopedText.reserve(64 + ScopedInfo.flags.size() + ScopedInfo.dependencies.size());
    ScopedText.append("compiler=").append(ScopedInfo.compiler);
    ScopedText.append(" type=").append(ScopedInfo.buildType);
    ScopedText.append(" platform=").append(ScopedInfo.platform);
    ScopedText.append(" flags=").append(ScopedInfo.flags);
    ScopedText.append(" deps=").append(ScopedInfo.dependencies);
    return ScopedText;
}
}
