// FemboyEngine2D - display/hardware/storage profile smoke.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/Platform/SystemInfo.hpp"

#include <cstdio>

int main()
{
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - system info";
    ScopedWindowDesc.width = 640;
    ScopedWindowDesc.height = 360;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }
    const Fby::Platform::SystemProfile ScopedProfile =
        Fby::Platform::SystemInfo::Query(".", ScopedWindow.NativeWindow());
    const Fby::Platform::DisplayInfo ScopedDisplay = Fby::Platform::SystemInfo::WindowDisplay(ScopedWindow.NativeWindow());
    std::printf("system_info: displays=%zu primary=%ux%u aspect=%.3f label=%s refresh=%.1f "
                "cpu_cores=%u ram=%llu free_ram=%llu vram=%llu disk=%llu free_disk=%llu\n",
                ScopedProfile.displays.size(), ScopedDisplay.width, ScopedDisplay.height, ScopedDisplay.Aspect(),
                ScopedDisplay.AspectLabel().c_str(), ScopedDisplay.refreshHz, ScopedProfile.hardware.logicalCores,
                static_cast<unsigned long long>(ScopedProfile.hardware.memoryTotalBytes),
                static_cast<unsigned long long>(ScopedProfile.hardware.memoryAvailableBytes),
                static_cast<unsigned long long>(ScopedProfile.hardware.videoMemoryBytes),
                static_cast<unsigned long long>(ScopedProfile.storage.totalBytes),
                static_cast<unsigned long long>(ScopedProfile.storage.freeBytes));
    if (ScopedDisplay.width == 0 || ScopedDisplay.height == 0 || ScopedDisplay.Aspect() <= 0.0f ||
        ScopedProfile.hardware.logicalCores == 0 || ScopedProfile.hardware.memoryTotalBytes == 0 ||
        ScopedProfile.storage.totalBytes == 0)
    {
        std::fprintf(stderr, "system_info: incomplete profile\n");
        return 1;
    }
    return 0;
}
