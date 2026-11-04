// FemboyEngine2D - optional DLSS runtime probe. No NGX symbols are linked.
#pragma once

#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Fby::Rhi
{

struct DlssRuntimeProbe
{
    bool Present = false;
    bool Loadable = false;
    std::string Error;
};

[[nodiscard]] inline DlssRuntimeProbe ProbeDlssRuntime(const std::filesystem::path& InDirectory)
{
    DlssRuntimeProbe ScopedResult;
    const std::filesystem::path ScopedDll = InDirectory / "nvngx_dlss.dll";
    std::error_code ScopedError;
    ScopedResult.Present = std::filesystem::exists(ScopedDll, ScopedError);
    if (!ScopedResult.Present)
    {
        ScopedResult.Error = "nvngx_dlss.dll not found";
        return ScopedResult;
    }
#if defined(_WIN32)
    HMODULE ScopedModule = LoadLibraryW(ScopedDll.c_str());
    if (ScopedModule == nullptr)
    {
        ScopedResult.Error = "LoadLibrary failed";
        return ScopedResult;
    }
    FreeLibrary(ScopedModule);
    ScopedResult.Loadable = true;
#else
    ScopedResult.Error = "DLSS runtime probe is Windows-only";
#endif
    return ScopedResult;
}

}
