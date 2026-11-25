// FemboyEngine2D - FidelityFX runtime presence check. No AMD symbols are linked.
#pragma once

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Fby::Rhi
{

[[nodiscard]] inline std::filesystem::path ExecutableDirectory()
{
#if defined(_WIN32)
    wchar_t ScopedPath[MAX_PATH]{};
    const DWORD ScopedLength = GetModuleFileNameW(nullptr, ScopedPath, MAX_PATH);
    if (ScopedLength != 0 && ScopedLength < MAX_PATH)
    {
        return std::filesystem::path(ScopedPath).parent_path();
    }
#endif
    std::error_code ScopedError;
    const std::filesystem::path ScopedCurrent = std::filesystem::current_path(ScopedError);
    return ScopedError ? std::filesystem::path{} : ScopedCurrent;
}

struct FidelityFxRuntimeAvailability
{
    bool Upscaling = false;
    bool FrameGeneration = false;
};

[[nodiscard]] inline FidelityFxRuntimeAvailability DetectFidelityFxRuntime(
    const std::filesystem::path& InDirectory) noexcept
{
    FidelityFxRuntimeAvailability ScopedResult;
    std::error_code ScopedError;
    try
    {
        ScopedResult.Upscaling =
            std::filesystem::is_regular_file(InDirectory / "amd_fidelityfx_upscaler_vk.dll", ScopedError);
        ScopedError.clear();
        ScopedResult.FrameGeneration =
            std::filesystem::is_regular_file(InDirectory / "amd_fidelityfx_framegeneration_vk.dll", ScopedError);
    }
    catch (...)
    {
        return {};
    }
    return ScopedResult;
}

}
