// FemboyEngine2D - FidelityFX FSR2 adapter contract.
#include "Engine/RHI/Fsr2Adapter.hpp"

#if defined(FBY_WITH_FIDELITYFX)
#include "ffx_api.h"
#include "ffx_api_loader.h"
#include "vk/ffx_api_vk.h"
#include "ffx_upscale.h"

#include <windows.h>
#include <string>

namespace
{
struct Fsr2ApiState
{
    HMODULE Module = nullptr;
    ffxFunctions Functions{};
};

Fsr2ApiState GFsr2Api;
}
#endif

namespace Fby::Rhi
{

bool Fsr2Adapter::Initialize(void* InDevice, void* InPhysicalDevice, void* InDeviceProcAddr,
                             std::uint32_t InMaxRenderWidth, std::uint32_t InMaxRenderHeight,
                             std::uint32_t InMaxOutputWidth, std::uint32_t InMaxOutputHeight)
{
    if (InMaxRenderWidth == 0 || InMaxRenderHeight == 0 || InMaxOutputWidth == 0 || InMaxOutputHeight == 0 ||
        InMaxRenderWidth > InMaxOutputWidth || InMaxRenderHeight > InMaxOutputHeight ||
        InMaxOutputWidth > 16384 || InMaxOutputHeight > 16384)
    {
        return false;
    }
#if defined(FBY_WITH_FIDELITYFX)
    if (InDevice == nullptr || InPhysicalDevice == nullptr)
    {
        return false;
    }
    if (MAvailable && MMaxRenderWidth == InMaxRenderWidth && MMaxRenderHeight == InMaxRenderHeight &&
        MMaxOutputWidth == InMaxOutputWidth && MMaxOutputHeight == InMaxOutputHeight)
    {
        return true;
    }
    if (MContext != nullptr && GFsr2Api.Functions.DestroyContext != nullptr)
    {
        ffxContext ScopedOldContext = static_cast<ffxContext>(MContext);
        GFsr2Api.Functions.DestroyContext(&ScopedOldContext, nullptr);
        MContext = nullptr;
        MAvailable = false;
    }
    if (GFsr2Api.Module == nullptr)
    {
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
        const std::wstring ScopedBase(ScopedPath);
        GFsr2Api.Module = LoadLibraryW((ScopedBase + L"amd_fidelityfx_upscaler_vk.dll").c_str());
        if (GFsr2Api.Module == nullptr)
        {
            return false;
        }
        ffxLoadFunctions(&GFsr2Api.Functions, GFsr2Api.Module);
    }
    if (GFsr2Api.Functions.CreateContext == nullptr || GFsr2Api.Functions.DestroyContext == nullptr ||
        GFsr2Api.Functions.Dispatch == nullptr)
    {
        return false;
    }

    ffxCreateBackendVKDesc ScopedBackend{};
    ScopedBackend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    ScopedBackend.deviceContext.vkDevice = static_cast<VkDevice>(InDevice);
    ScopedBackend.deviceContext.vkPhysicalDevice = static_cast<VkPhysicalDevice>(InPhysicalDevice);
    ScopedBackend.deviceContext.vkDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(InDeviceProcAddr);

    ffxCreateContextDescUpscale ScopedUpscale{};
    ScopedUpscale.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    ScopedUpscale.maxRenderSize = {InMaxRenderWidth, InMaxRenderHeight};
    ScopedUpscale.maxUpscaleSize = {InMaxOutputWidth, InMaxOutputHeight};
    ScopedUpscale.header.pNext = &ScopedBackend.header;

    ffxContext ScopedContext = nullptr;
    if (GFsr2Api.Functions.CreateContext(&ScopedContext, &ScopedUpscale.header, nullptr) != FFX_API_RETURN_OK)
    {
        return false;
    }
    MContext = ScopedContext;
    MMaxRenderWidth = InMaxRenderWidth;
    MMaxRenderHeight = InMaxRenderHeight;
    MMaxOutputWidth = InMaxOutputWidth;
    MMaxOutputHeight = InMaxOutputHeight;
    MAvailable = true;
    return true;
#else
    (void)InDevice;
    (void)InPhysicalDevice;
    (void)InDeviceProcAddr;
    (void)InMaxRenderWidth;
    (void)InMaxRenderHeight;
    (void)InMaxOutputWidth;
    (void)InMaxOutputHeight;
    MAvailable = false;
    return false;
#endif
}

bool Fsr2Adapter::Dispatch(void* InCommandBuffer, void* InColor, void* InDepth, void* InMotion,
                           void* InReactive, void* InOutput, std::uint32_t InRenderWidth,
                           std::uint32_t InRenderHeight, std::uint32_t InOutputWidth,
                           std::uint32_t InOutputHeight, Fsr2QualityMode InQuality, float InJitterX,
                           float InJitterY, bool InReset)
{
    if (InCommandBuffer == nullptr || InColor == nullptr || InDepth == nullptr || InMotion == nullptr ||
        InOutput == nullptr || InRenderWidth == 0 || InRenderHeight == 0 || InOutputWidth == 0 ||
        InOutputHeight == 0 || InRenderWidth > InOutputWidth || InRenderHeight > InOutputHeight ||
        !Fsr2RenderSizeMatches(InQuality, InRenderWidth, InRenderHeight, InOutputWidth, InOutputHeight))
    {
        return false;
    }
#if defined(FBY_WITH_FIDELITYFX)
    if (MContext == nullptr || GFsr2Api.Functions.Dispatch == nullptr || InCommandBuffer == nullptr)
    {
        return false;
    }
    const auto ScopedTextureDesc = [](std::uint32_t InWidth, std::uint32_t InHeight, uint32_t InFormat)
    {
        FfxApiResourceDescription ScopedDesc{};
        ScopedDesc.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
        ScopedDesc.format = InFormat;
        ScopedDesc.width = InWidth;
        ScopedDesc.height = InHeight;
        ScopedDesc.depth = 1;
        ScopedDesc.mipCount = 1;
        return ScopedDesc;
    };
    ffxDispatchDescUpscale ScopedDispatch{};
    ScopedDispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    ScopedDispatch.commandList = InCommandBuffer;
    ScopedDispatch.color = ffxApiGetResourceVK(InColor, ScopedTextureDesc(InRenderWidth, InRenderHeight,
                                                                        FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM),
                                              FFX_API_RESOURCE_STATE_COMPUTE_READ);
    ScopedDispatch.depth = ffxApiGetResourceVK(InDepth, ScopedTextureDesc(InRenderWidth, InRenderHeight,
                                                                        FFX_API_SURFACE_FORMAT_R32_FLOAT),
                                              FFX_API_RESOURCE_STATE_COMPUTE_READ);
    ScopedDispatch.motionVectors = ffxApiGetResourceVK(InMotion, ScopedTextureDesc(
        InRenderWidth, InRenderHeight, FFX_API_SURFACE_FORMAT_R16G16_FLOAT), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    if (InReactive != nullptr)
    {
        ScopedDispatch.reactive = ffxApiGetResourceVK(
            InReactive, ScopedTextureDesc(InRenderWidth, InRenderHeight, FFX_API_SURFACE_FORMAT_R8_UNORM),
            FFX_API_RESOURCE_STATE_COMPUTE_READ);
    }
    ScopedDispatch.output = ffxApiGetResourceVK(InOutput, ScopedTextureDesc(
        InOutputWidth, InOutputHeight, FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM), FFX_API_RESOURCE_STATE_RENDER_TARGET);
    ScopedDispatch.jitterOffset = {InJitterX, InJitterY};
    // The shader writes normalized UV deltas; FSR2 expects pixels.
    ScopedDispatch.motionVectorScale = {static_cast<float>(InRenderWidth), static_cast<float>(InRenderHeight)};
    ScopedDispatch.renderSize = {InRenderWidth, InRenderHeight};
    ScopedDispatch.upscaleSize = {InOutputWidth, InOutputHeight};
    ScopedDispatch.enableSharpening = true;
    ScopedDispatch.sharpness = 0.35f;
    ScopedDispatch.frameTimeDelta = 16.6667f;
    ScopedDispatch.preExposure = 1.0f;
    ScopedDispatch.reset = InReset;
    ScopedDispatch.cameraNear = 0.1f;
    ScopedDispatch.cameraFar = 1000.0f;
    ScopedDispatch.cameraFovAngleVertical = 1.0f;
    ScopedDispatch.viewSpaceToMetersFactor = 1.0f;
    ffxContext ScopedContext = static_cast<ffxContext>(MContext);
    return GFsr2Api.Functions.Dispatch(&ScopedContext, &ScopedDispatch.header) == FFX_API_RETURN_OK;
#else
    (void)InCommandBuffer;
    (void)InColor;
    (void)InDepth;
    (void)InMotion;
    (void)InReactive;
    (void)InOutput;
    (void)InRenderWidth;
    (void)InRenderHeight;
    (void)InOutputWidth;
    (void)InOutputHeight;
    (void)InJitterX;
    (void)InJitterY;
    (void)InReset;
    return false;
#endif
}

void Fsr2Adapter::Shutdown() noexcept
{
#if defined(FBY_WITH_FIDELITYFX)
    if (MContext != nullptr && GFsr2Api.Functions.DestroyContext != nullptr)
    {
        ffxContext ScopedContext = static_cast<ffxContext>(MContext);
        GFsr2Api.Functions.DestroyContext(&ScopedContext, nullptr);
    }
#endif
    MAvailable = false;
    MContext = nullptr;
    MMaxRenderWidth = 0;
    MMaxRenderHeight = 0;
    MMaxOutputWidth = 0;
    MMaxOutputHeight = 0;
}

}
