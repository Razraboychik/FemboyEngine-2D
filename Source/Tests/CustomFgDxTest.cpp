// FemboyEngine2D - portable custom frame-generation smoke on DX.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/CustomFrameGenerator.hpp"
#include "Engine/RHI/RHI.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main()
{
#if defined(_WIN32)
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - custom FG";
    ScopedWindowDesc.width = 320;
    ScopedWindowDesc.height = 240;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }
    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Fby::Rhi::Backend::D3D12;
    if (const char* ScopedBackend = std::getenv("FBY_FG_BACKEND"); ScopedBackend != nullptr &&
        std::strcmp(ScopedBackend, "d3d11") == 0)
    {
        ScopedDesc.backend = Fby::Rhi::Backend::D3D11;
    }
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {320, 240};
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::printf("custom_fg: skipped\n");
        return 0;
    }
    Fby::Rhi::RenderTargetDesc ScopedColorDesc;
    ScopedColorDesc.size = {8, 8};
    ScopedColorDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedColor = ScopedGpu->CreateRenderTarget(ScopedColorDesc);
    const Fby::Rhi::RenderTargetHandle ScopedMotion = ScopedGpu->CreateMotionVectorTarget({8, 8});
    const Fby::Rhi::RenderTargetHandle ScopedOutput = ScopedGpu->CreateRenderTarget(ScopedColorDesc);
    Fby::Rhi::CustomFrameGenerator ScopedGenerator;
    const bool ScopedInitialized = ScopedGenerator.Initialize(*ScopedGpu, {8, 8}, {8, 8});
    if (!ScopedColor.IsValid() || !ScopedMotion.IsValid() || !ScopedOutput.IsValid() || !ScopedInitialized ||
        !ScopedGpu->BeginFrame())
    {
        std::fprintf(stderr, "custom_fg: init failed\n");
        return 1;
    }
    Fby::Rhi::FrameGenerationInput ScopedInput;
    ScopedInput.Color = ScopedColor;
    ScopedInput.Motion = ScopedMotion;
    ScopedInput.Output = ScopedOutput;
    ScopedInput.RenderExtent = {8, 8};
    ScopedInput.OutputExtent = {8, 8};
    ScopedInput.ResetHistory = true;
    ScopedInput.RealFrame = true;
    ScopedInput.Policy.ProjectAllowed = true;
    ScopedInput.Policy.BackendCompiled = true;
    ScopedInput.Policy.DeviceSupported = true;
    ScopedInput.Policy.HudSeparated = true;
    const bool ScopedGenerated = ScopedGenerator.Generate(*ScopedGpu, ScopedInput);
    ScopedGpu->EndFrame();
    std::printf("custom_fg: backend=%s init=%d generate=%d\n",
                ScopedGpu->GetBackend() == Fby::Rhi::Backend::D3D11 ? "d3d11" : "d3d12",
                ScopedInitialized ? 1 : 0, ScopedGenerated ? 1 : 0);
    ScopedGenerator.Shutdown(*ScopedGpu);
    ScopedGpu->DestroyRenderTarget(ScopedColor);
    ScopedGpu->DestroyRenderTarget(ScopedMotion);
    ScopedGpu->DestroyRenderTarget(ScopedOutput);
    ScopedGpu->Shutdown();
    return ScopedGenerated ? 0 : 1;
#else
    std::printf("custom_fg: skipped (non-Windows)\n");
    return 0;
#endif
}
