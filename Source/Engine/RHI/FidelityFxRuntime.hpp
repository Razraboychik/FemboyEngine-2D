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
    if (GetModuleFileNameW(nullptr, ScopedPath, MAX_PATH) != 0)
    {
        return std::filesystem::path(ScopedPath).parent_path();
    }
#endif
    return std::filesystem::current_path();
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
    ScopedResult.Upscaling = std::filesystem::exists(InDirectory / "amd_fidelityfx_upscaler_vk.dll", ScopedError);
    ScopedError.clear();
    ScopedResult.FrameGeneration =
        std::filesystem::exists(InDirectory / "amd_fidelityfx_framegeneration_vk.dll", ScopedError);
    return ScopedResult;
}

}
