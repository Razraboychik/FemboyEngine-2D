// FemboyEngine2D - DLSS runtime presence check. No SDK symbols are linked here.
#pragma once

#include <filesystem>

namespace Fby::Rhi
{

struct DlssRuntimeAvailability
{
    bool Upscaling = false;
    bool FrameGeneration = false;
};

[[nodiscard]] inline DlssRuntimeAvailability DetectDlssRuntime(const std::filesystem::path& InDirectory) noexcept
{
    DlssRuntimeAvailability ScopedResult;
    try
    {
        std::error_code ScopedError;
        ScopedResult.Upscaling =
            !ScopedError && std::filesystem::is_regular_file(InDirectory / "nvngx_dlss.dll", ScopedError);
        ScopedError.clear();
        ScopedResult.FrameGeneration =
            !ScopedError && std::filesystem::is_regular_file(InDirectory / "nvngx_dlssg.dll", ScopedError);
    }
    catch (...)
    {
        return {};
    }
    return ScopedResult;
}

}
