// FemboyEngine2D - D3D12 vendor FSR3 smoke at a realistic resolution.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/Fsr3FrameGenerator.hpp"
#include "Engine/RHI/RHI.hpp"

#include <cstdio>

int main()
{
#if defined(_WIN32)
    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - FSR3 D3D12";
    ScopedWindowDesc.width = 640;
    ScopedWindowDesc.height = 360;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }
    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Fby::Rhi::Backend::D3D12;
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {640, 360};
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::printf("fsr3_d3d12: skipped\n");
        return 0;
    }
    Fby::Rhi::RenderTargetDesc ScopedColorDesc;
    ScopedColorDesc.size = {320, 180};
    ScopedColorDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedColor = ScopedGpu->CreateRenderTarget(ScopedColorDesc);
    Fby::Rhi::RenderTargetDesc ScopedOutputDesc;
    ScopedOutputDesc.size = {640, 360};
    ScopedOutputDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedOutput = ScopedGpu->CreateRenderTarget(ScopedOutputDesc);
    Fby::Rhi::Fsr3FrameGenerator ScopedFsr3;
    const bool ScopedInitialized = ScopedFsr3.Initialize(*ScopedGpu, {320, 180}, {640, 360});
    std::printf("fsr3_d3d12: init=%d\n", ScopedInitialized ? 1 : 0);
    if (!ScopedInitialized || !ScopedGpu->BeginFrame())
    {
        ScopedFsr3.Shutdown(*ScopedGpu);
        return 1;
    }
    Fby::Rhi::FrameGenerationInput ScopedInput;
    ScopedInput.Color = ScopedColor;
    ScopedInput.Output = ScopedOutput;
    ScopedInput.RenderExtent = {320, 180};
    ScopedInput.OutputExtent = {640, 360};
    ScopedInput.ResetHistory = true;
    ScopedInput.RealFrame = true;
    ScopedInput.Policy.ProjectAllowed = true;
    ScopedInput.Policy.BackendCompiled = true;
    ScopedInput.Policy.DeviceSupported = true;
    ScopedInput.Policy.HudSeparated = true;
    const bool ScopedGenerated = ScopedFsr3.Generate(*ScopedGpu, ScopedInput);
    ScopedGpu->EndFrame();
    std::printf("fsr3_d3d12: generate=%d\n", ScopedGenerated ? 1 : 0);
    ScopedFsr3.Shutdown(*ScopedGpu);
    ScopedGpu->DestroyRenderTarget(ScopedColor);
    ScopedGpu->DestroyRenderTarget(ScopedOutput);
    ScopedGpu->Shutdown();
    return ScopedGenerated ? 0 : 1;
#else
    return 0;
#endif
}
