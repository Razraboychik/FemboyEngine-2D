// FemboyEngine2D - asset root discovery for host applications.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace Fby::Core
{

// Absolute directory that contains the executable, or the working directory
// when argv[0] carries no path (a PATH launch).
[[nodiscard]] inline std::filesystem::path ExecutableDirectory(std::string_view InArgv0)
{
    if (InArgv0.empty())
    {
        return std::filesystem::current_path();
    }
    std::filesystem::path ScopedPath(InArgv0);
    if (!ScopedPath.has_parent_path())
    {
        return std::filesystem::current_path();
    }
    std::error_code ScopedError;
    const std::filesystem::path ScopedAbsolute = std::filesystem::absolute(ScopedPath, ScopedError);
    return ScopedError ? ScopedPath.parent_path() : ScopedAbsolute.parent_path();
}

// Finds the repository/install `assets` directory without relying on the
// caller's working directory. Resolution order: FBY_ASSET_DIR, cwd/assets, then
// assets next to the executable and up to three parent directories (the build
// tree puts the binary in build/bin, three levels below the repository root).
[[nodiscard]] inline std::filesystem::path FindAssetRoot(std::string_view InArgv0)
{
    std::error_code ScopedError;
    if (const char* ScopedEnvironment = std::getenv("FBY_ASSET_DIR");
        ScopedEnvironment != nullptr && ScopedEnvironment[0] != '\0')
    {
        const std::filesystem::path ScopedEnvironmentPath(ScopedEnvironment);
        if (std::filesystem::is_directory(ScopedEnvironmentPath / "assets", ScopedError) ||
            std::filesystem::is_directory(ScopedEnvironmentPath, ScopedError))
        {
            return std::filesystem::weakly_canonical(ScopedEnvironmentPath, ScopedError);
        }
    }
    std::filesystem::path ScopedCandidate = std::filesystem::current_path(ScopedError) / "assets";
    if (!ScopedError && std::filesystem::is_directory(ScopedCandidate, ScopedError))
    {
        return std::filesystem::weakly_canonical(ScopedCandidate, ScopedError);
    }
    std::filesystem::path ScopedExecutable = ExecutableDirectory(InArgv0);
    for (std::uint32_t ScopedDepth = 0; ScopedDepth < 4; ++ScopedDepth)
    {
        ScopedCandidate = ScopedExecutable / "assets";
        if (std::filesystem::is_directory(ScopedCandidate, ScopedError))
        {
            return std::filesystem::weakly_canonical(ScopedCandidate, ScopedError);
        }
        ScopedExecutable = ScopedExecutable.parent_path();
        if (ScopedExecutable.empty())
        {
            break;
        }
    }
    return {};
}
}
