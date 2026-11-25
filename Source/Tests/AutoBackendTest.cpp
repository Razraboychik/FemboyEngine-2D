// FemboyEngine2D - automatic backend selection smoke.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/RHI.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - auto backend";
    ScopedWindowDesc.width = 320;
    ScopedWindowDesc.height = 240;
    ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }
#if defined(_WIN32)
    _putenv_s("FBY_RHI_BACKEND", "auto");
#endif
    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {320, 240};
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::fprintf(stderr, "auto_backend: no backend\n");
        return 1;
    }
    std::printf("auto_backend: %s\n", Fby::Rhi::BackendName(ScopedGpu->GetBackend()));
    ScopedGpu->Shutdown();
    return 0;
}
