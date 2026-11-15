// FemboyEngine2D - D3D12 FSR2-compatible chain smoke.
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/Fsr2Pass.hpp"
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
    ScopedWindowDesc.title = "FemboyEngine - FSR2 D3D12";
    ScopedWindowDesc.width = 320;
    ScopedWindowDesc.height = 240;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }
    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Fby::Rhi::Backend::D3D12;
    if (const char* ScopedBackend = std::getenv("FBY_FSR2_BACKEND"); ScopedBackend != nullptr &&
        std::strcmp(ScopedBackend, "d3d11") == 0)
    {
        ScopedDesc.backend = Fby::Rhi::Backend::D3D11;
    }
    ScopedDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDesc.size = {320, 240};
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    if (!ScopedGpu)
    {
        std::printf("fsr2_dx: skipped\n");
        return 0;
    }
    Fby::Rhi::RenderTargetDesc ScopedSourceDesc;
    ScopedSourceDesc.size = {8, 8};
    ScopedSourceDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedSource = ScopedGpu->CreateRenderTarget(ScopedSourceDesc);
    Fby::Rhi::RenderTargetDesc ScopedOutputDesc;
    ScopedOutputDesc.size = {16, 16};
    ScopedOutputDesc.format = Fby::Rhi::RenderTargetFormat::Rgba8Unorm;
    const Fby::Rhi::RenderTargetHandle ScopedOutput = ScopedGpu->CreateRenderTarget(ScopedOutputDesc);
    const Fby::Rhi::RenderTargetHandle ScopedMotion = ScopedGpu->CreateMotionVectorTarget({8, 8});
    Fby::Rhi::Fsr2Pass ScopedFsr2;
    const bool ScopedEnsured = ScopedFsr2.Ensure(*ScopedGpu, {8, 8}, {16, 16});
    std::printf("fsr2_dx: source=%d output=%d motion=%d ensure=%d\n", ScopedSource.IsValid() ? 1 : 0,
                ScopedOutput.IsValid() ? 1 : 0, ScopedMotion.IsValid() ? 1 : 0, ScopedEnsured ? 1 : 0);
    if (!ScopedEnsured || !ScopedGpu->BeginFrame() ||
        !ScopedFsr2.Resolve(*ScopedGpu, ScopedSource, ScopedMotion, ScopedOutput, true, 0.25f))
    {
        std::fprintf(stderr, "fsr2_dx: resolve failed\n");
        return 1;
    }
    ScopedGpu->EndFrame();
    std::vector<std::byte> ScopedPixels(16 * 16 * 4);
    if (!ScopedGpu->CaptureRenderTarget(ScopedOutput, ScopedPixels))
    {
        std::fprintf(stderr, "fsr2_dx: capture failed\n");
        return 1;
    }
    std::printf("fsr2_dx: resolve=1\n");
    ScopedFsr2.Release(*ScopedGpu);
    ScopedGpu->DestroyRenderTarget(ScopedSource);
    ScopedGpu->DestroyRenderTarget(ScopedOutput);
    ScopedGpu->DestroyRenderTarget(ScopedMotion);
    ScopedGpu->Shutdown();
    return ScopedEnsured ? 0 : 1;
#else
    std::printf("fsr2_dx: skipped (non-Windows)\n");
    return 0;
#endif
}
