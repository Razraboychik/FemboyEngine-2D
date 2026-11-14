// FemboyEngine2D - FidelityFX FSR3 frame generation backend.
#include "Engine/RHI/Fsr3FrameGenerator.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/RHI/Fsr3Dx12.hpp"

#if defined(FBY_WITH_FIDELITYFX) && defined(_WIN32)
#include "ffx_api.h"
#include "ffx_api_loader.h"
#include "ffx_framegeneration.h"
#include "vk/ffx_api_vk.h"

#include <windows.h>
#include <string>

namespace
{
struct Fsr3FgApi
{
    HMODULE Module = nullptr;
    ffxFunctions Functions{};

    [[nodiscard]] bool Load()
    {
        if (Module != nullptr)
        {
            return true;
        }
        wchar_t ScopedPath[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, ScopedPath, MAX_PATH) == 0)
        {
            return false;
        }
        wchar_t* ScopedSlash = wcsrchr(ScopedPath, L'\\');
        if (ScopedSlash == nullptr)
        {
            return false;
        }
        *(ScopedSlash + 1) = L'\0';
        Module = LoadLibraryW((std::wstring(ScopedPath) + L"amd_fidelityfx_framegeneration_vk.dll").c_str());
        if (Module == nullptr)
        {
            return false;
        }
        Functions.CreateContext =
            reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(Module, "ffxCreateContext"));
        Functions.DestroyContext =
            reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(Module, "ffxDestroyContext"));
        Functions.Configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(Module, "ffxConfigure"));
        Functions.Query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(Module, "ffxQuery"));
        Functions.Dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(Module, "ffxDispatch"));
        return Functions.CreateContext != nullptr && Functions.DestroyContext != nullptr &&
               Functions.Dispatch != nullptr;
    }
};

Fsr3FgApi GFsr3FgApi;

[[nodiscard]] FfxApiResource MakeResource(const Fby::Rhi::NativeTextureView& InView, std::uint32_t InWidth,
                                          std::uint32_t InHeight, uint32_t InState)
{
    FfxApiResourceDescription ScopedDesc{};
    ScopedDesc.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    ScopedDesc.format = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    ScopedDesc.width = InWidth;
    ScopedDesc.height = InHeight;
    ScopedDesc.depth = 1;
    ScopedDesc.mipCount = 1;
    FfxApiResource ScopedResource{};
    ScopedResource.resource = InView.image != nullptr ? InView.image : InView.view;
    ScopedResource.description = ScopedDesc;
    ScopedResource.state = InState;
    return ScopedResource;
}
}
#endif

namespace Fby::Rhi
{

bool Fsr3FrameGenerator::Initialize(RHI& InGpu, Extent2D InRenderExtent, Extent2D InOutputExtent)
{
#if defined(FBY_WITH_FIDELITYFX) && defined(_WIN32)
    if (MContext != nullptr && MOutputExtent.width == InOutputExtent.width &&
        MOutputExtent.height == InOutputExtent.height)
    {
        return true;
    }
    Shutdown(InGpu);
    RHI::NativeDeviceHandles ScopedHandles;
    if (!InGpu.GetNativeDeviceHandles(ScopedHandles))
    {
        return false;
    }
    if (InGpu.GetBackend() == Backend::D3D12)
    {
        void* ScopedDxContext = nullptr;
        if (!Fsr3Dx12::CreateContext(ScopedHandles.Device, InRenderExtent.width, InRenderExtent.height,
                                     InOutputExtent.width, InOutputExtent.height, &ScopedDxContext))
        {
            return false;
        }
        MContext = ScopedDxContext;
        MDx12 = true;
        MOutputExtent = InOutputExtent;
        return true;
    }
    if (!GFsr3FgApi.Load())
    {
        return false;
    }
    ffxCreateBackendVKDesc ScopedBackend{};
    ScopedBackend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    ScopedBackend.deviceContext.vkDevice = static_cast<VkDevice>(ScopedHandles.Device);
    ScopedBackend.deviceContext.vkPhysicalDevice = static_cast<VkPhysicalDevice>(ScopedHandles.PhysicalDevice);
    ScopedBackend.deviceContext.vkDeviceProcAddr =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(ScopedHandles.DeviceProcAddr);

    ffxCreateContextDescFrameGeneration ScopedCreate{};
    ScopedCreate.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    ScopedCreate.displaySize = {InOutputExtent.width, InOutputExtent.height};
    ScopedCreate.maxRenderSize = {InRenderExtent.width, InRenderExtent.height};
    ScopedCreate.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    ScopedCreate.header.pNext = &ScopedBackend.header;
    ffxContext ScopedContext = nullptr;
    const ffxReturnCode_t ScopedCreateResult =
        GFsr3FgApi.Functions.CreateContext(&ScopedContext, &ScopedCreate.header, nullptr);
    if (ScopedCreateResult != FFX_API_RETURN_OK || ScopedContext == nullptr)
    {
        std::fprintf(stderr, "fsr3: context failed %d\n", static_cast<int>(ScopedCreateResult));
        return false;
    }
    MContext = ScopedContext;
    MOutputExtent = InOutputExtent;
    return true;
#else
    (void)InGpu;
    (void)InRenderExtent;
    (void)InOutputExtent;
    return false;
#endif
}

bool Fsr3FrameGenerator::Generate(RHI& InGpu, const FrameGenerationInput& InInput)
{
#if defined(FBY_WITH_FIDELITYFX) && defined(_WIN32)
    if (MContext == nullptr || !InInput.HudSeparated || !InInput.Color.IsValid() || !InInput.Output.IsValid())
    {
        return false;
    }
    if (MDx12)
    {
        const FrameGenerationStatus ScopedDxStatus =
            EvaluateFrameGeneration(InInput.Policy, InInput.Fps, InInput.ResetHistory);
        if (ScopedDxStatus != FrameGenerationStatus::Ready &&
            ScopedDxStatus != FrameGenerationStatus::ResetHistory)
        {
            return false;
        }
        MPacingState = UpdateFramePacing(MPacingConfig, MPacingState, InInput.DeltaMs, InInput.RealFrame);
        if (!MPacingState.AllowGenerated && !InInput.ResetHistory)
        {
            return false;
        }
        NativeTextureView ScopedColor;
        NativeTextureView ScopedOutput;
        if (!InGpu.RenderTargetTexture(InInput.Color, ScopedColor) ||
            !InGpu.RenderTargetTexture(InInput.Output, ScopedOutput))
        {
            return false;
        }
        const bool ScopedOk = Fsr3Dx12::Dispatch(MContext, InGpu.CurrentCommandBuffer(), ScopedColor.image,
                                                 ScopedOutput.image, InInput.OutputExtent.width,
                                                 InInput.OutputExtent.height, InInput.ResetHistory || MReset,
                                                 MFrameId++);
        if (ScopedOk)
        {
            MReset = false;
        }
        return ScopedOk;
    }
    const FrameGenerationStatus ScopedStatus = EvaluateFrameGeneration(InInput.Policy, InInput.Fps, InInput.ResetHistory);
    if (ScopedStatus != FrameGenerationStatus::Ready && ScopedStatus != FrameGenerationStatus::ResetHistory)
    {
        return false;
    }
    MPacingState = UpdateFramePacing(MPacingConfig, MPacingState, InInput.DeltaMs, InInput.RealFrame);
    if (!MPacingState.AllowGenerated && !InInput.ResetHistory)
    {
        return false;
    }
    NativeTextureView ScopedColor;
    NativeTextureView ScopedOutput;
    if (!InGpu.RenderTargetTexture(InInput.Color, ScopedColor) ||
        !InGpu.RenderTargetTexture(InInput.Output, ScopedOutput))
    {
        return false;
    }
    ffxDispatchDescFrameGeneration ScopedDispatch{};
    ScopedDispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    ScopedDispatch.commandList = InGpu.CurrentCommandBuffer();
    ScopedDispatch.presentColor = MakeResource(ScopedColor, InInput.OutputExtent.width,
                                               InInput.OutputExtent.height, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    ScopedDispatch.outputs[0] = MakeResource(ScopedOutput, InInput.OutputExtent.width,
                                              InInput.OutputExtent.height, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    ScopedDispatch.numGeneratedFrames = 1;
    ScopedDispatch.reset = InInput.ResetHistory || MReset;
    ScopedDispatch.frameID = MFrameId++;
    ffxContext ScopedContext = static_cast<ffxContext>(MContext);
    const bool ScopedOk =
        GFsr3FgApi.Functions.Dispatch(&ScopedContext, &ScopedDispatch.header) == FFX_API_RETURN_OK;
    if (ScopedOk)
    {
        MReset = false;
    }
    return ScopedOk;
#else
    (void)InGpu;
    (void)InInput;
    return false;
#endif
}

void Fsr3FrameGenerator::ResetHistory() noexcept
{
    MReset = true;
}

void Fsr3FrameGenerator::Shutdown(RHI& InGpu) noexcept
{
    (void)InGpu;
    if (MDx12)
    {
        Fsr3Dx12::DestroyContext(MContext);
    }
#if defined(FBY_WITH_FIDELITYFX) && defined(_WIN32)
    else if (MContext != nullptr && GFsr3FgApi.Functions.DestroyContext != nullptr)
    {
        ffxContext ScopedContext = static_cast<ffxContext>(MContext);
        GFsr3FgApi.Functions.DestroyContext(&ScopedContext, nullptr);
    }
#endif
    MContext = nullptr;
    MDx12 = false;
    MOutputExtent = {};
    MFrameId = 0;
    MReset = false;
}

}
