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
    ScopedResult.Present = std::filesystem::is_regular_file(ScopedDll, ScopedError);
    if (ScopedError)
    {
        ScopedResult.Error = "nvngx_dlss.dll probe failed: " + ScopedError.message();
        return ScopedResult;
    }
    if (!ScopedResult.Present)
    {
        ScopedResult.Error = "nvngx_dlss.dll not found";
        return ScopedResult;
    }
#if defined(_WIN32)
    const std::filesystem::path ScopedAbsolute = std::filesystem::absolute(ScopedDll, ScopedError);
    if (ScopedError)
    {
        ScopedResult.Error = "cannot resolve nvngx_dlss.dll: " + ScopedError.message();
        return ScopedResult;
    }
    HMODULE ScopedModule = LoadLibraryExW(ScopedAbsolute.c_str(), nullptr,
                                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (ScopedModule == nullptr)
    {
        ScopedResult.Error = "LoadLibrary failed";
        return ScopedResult;
    }
    const bool ScopedHasInit = GetProcAddress(ScopedModule, "NVSDK_NGX_VULKAN_Init") != nullptr ||
                               GetProcAddress(ScopedModule, "NVSDK_NGX_VULKAN_Init_ProjectID") != nullptr;
    const bool ScopedHasRelease =
        GetProcAddress(ScopedModule, "NVSDK_NGX_VULKAN_ReleaseFeature") != nullptr;
    FreeLibrary(ScopedModule);
    ScopedResult.Loadable = ScopedHasInit && ScopedHasRelease;
    if (!ScopedResult.Loadable)
    {
        ScopedResult.Error = "nvngx_dlss.dll is missing required NGX exports";
    }
#else
    ScopedResult.Error = "DLSS runtime probe is Windows-only";
#endif
    return ScopedResult;
}

}
