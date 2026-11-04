// FemboyEngine2D - one host for an arbitrary number of platform windows.
#pragma once

#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/RHI.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace Fby
{

// Each window owns its own WindowManager and RHI device. The simulation can be
// shared across them, which keeps online games and PIE on the same model:
// one world, many presenters.
class WindowHost
{
public:
    struct Handle
    {
        std::uint32_t Index = 0xFFFFFFFFu;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Index != 0xFFFFFFFFu;
        }
    };

    struct WindowRecord
    {
        std::unique_ptr<WindowManager> Window;
        std::unique_ptr<Rhi::RHI> Gpu;
    };

    [[nodiscard]] Handle Create(const WindowDesc& InWindow, const Rhi::DeviceDesc& InDevice);
    void Destroy(Handle InHandle);

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return static_cast<std::uint32_t>(MWindows.size());
    }

    [[nodiscard]] WindowManager* Window(Handle InHandle) noexcept;
    [[nodiscard]] Rhi::RHI* Gpu(Handle InHandle) noexcept;
    [[nodiscard]] bool ShouldClose(Handle InHandle) const noexcept;

    // Pumps every window. Returns false when any window requested close.
    [[nodiscard]] bool PumpEvents();

private:
    std::vector<WindowRecord> MWindows;
};

}
