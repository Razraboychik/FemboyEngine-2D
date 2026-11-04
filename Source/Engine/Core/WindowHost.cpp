// FemboyEngine2D - one host for an arbitrary number of platform windows.
#include "Engine/Core/WindowHost.hpp"

namespace Fby
{

WindowHost::Handle WindowHost::Create(const WindowDesc& InWindow, const Rhi::DeviceDesc& InDevice)
{
    auto ScopedWindow = std::make_unique<WindowManager>();
    if (!ScopedWindow->Initialize() || !ScopedWindow->Create(InWindow))
    {
        return {};
    }
    Rhi::DeviceDesc ScopedDevice = InDevice;
    ScopedDevice.nativeWindow = ScopedWindow->NativeWindow();
    ScopedDevice.size = {ScopedWindow->Size().width, ScopedWindow->Size().height};
    auto ScopedGpu = Rhi::RHI::Create(ScopedDevice);
    if (!ScopedGpu)
    {
        ScopedWindow->Destroy();
        return {};
    }
    WindowRecord ScopedRecord;
    ScopedRecord.Window = std::move(ScopedWindow);
    ScopedRecord.Gpu = std::move(ScopedGpu);
    MWindows.push_back(std::move(ScopedRecord));
    return Handle{static_cast<std::uint32_t>(MWindows.size() - 1)};
}

void WindowHost::Destroy(Handle InHandle)
{
    if (!InHandle.IsValid() || InHandle.Index >= MWindows.size())
    {
        return;
    }
    WindowRecord& ScopedRecord = MWindows[InHandle.Index];
    ScopedRecord.Gpu.reset();
    if (ScopedRecord.Window)
    {
        ScopedRecord.Window->Destroy();
    }
    ScopedRecord.Window.reset();
}

WindowManager* WindowHost::Window(Handle InHandle) noexcept
{
    if (!InHandle.IsValid() || InHandle.Index >= MWindows.size())
    {
        return nullptr;
    }
    return MWindows[InHandle.Index].Window.get();
}

Rhi::RHI* WindowHost::Gpu(Handle InHandle) noexcept
{
    if (!InHandle.IsValid() || InHandle.Index >= MWindows.size())
    {
        return nullptr;
    }
    return MWindows[InHandle.Index].Gpu.get();
}

bool WindowHost::ShouldClose(Handle InHandle) const noexcept
{
    if (!InHandle.IsValid() || InHandle.Index >= MWindows.size() || !MWindows[InHandle.Index].Window)
    {
        return true;
    }
    return MWindows[InHandle.Index].Window->ShouldClose();
}

bool WindowHost::PumpEvents()
{
    bool ScopedOpen = false;
    for (WindowRecord& ScopedRecord : MWindows)
    {
        if (ScopedRecord.Window && ScopedRecord.Window->PumpEvents() && !ScopedRecord.Window->ShouldClose())
        {
            ScopedOpen = true;
        }
    }
    return ScopedOpen;
}

}
