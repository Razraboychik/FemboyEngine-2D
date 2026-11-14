// FemboyEngine2D - D3D12 FSR3 frame-generation bridge implementation.
#include "Engine/RHI/Fsr3Dx12.hpp"

#if defined(FBY_WITH_FIDELITYFX) && defined(_WIN32)
#include "Engine/Core/Log.hpp"
#include "ffx_api.h"
#include "ffx_api_loader.h"
#include "ffx_framegeneration.h"
#include "dx12/ffx_api_dx12.h"
#include <d3d12.h>
#include <string>
#include <windows.h>

namespace
{
struct Dx12Api
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
        Module = LoadLibraryW((std::wstring(ScopedPath) + L"amd_fidelityfx_framegeneration_dx12.dll").c_str());
        if (Module == nullptr)
        {
            return false;
        }
        Functions.CreateContext = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(Module, "ffxCreateContext"));
        Functions.DestroyContext =
            reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(Module, "ffxDestroyContext"));
        Functions.Dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(Module, "ffxDispatch"));
        return Functions.CreateContext != nullptr && Functions.DestroyContext != nullptr &&
               Functions.Dispatch != nullptr;
    }
};

Dx12Api GDx12Api;
}

namespace Fby::Rhi::Fsr3Dx12
{

bool Available() noexcept
{
    return GDx12Api.Load();
}

bool CreateContext(void* InDevice, std::uint32_t InRenderWidth, std::uint32_t InRenderHeight,
                   std::uint32_t InOutputWidth, std::uint32_t InOutputHeight, void** OutContext) noexcept
{
    std::fprintf(stderr, "fsr3dx: create enter device=%p\n", InDevice);
    if (InDevice == nullptr || OutContext == nullptr || InRenderWidth == 0 || InRenderHeight == 0 ||
        InOutputWidth == 0 || InOutputHeight == 0 || !GDx12Api.Load())
    {
        return false;
    }
    ffxCreateBackendDX12Desc ScopedBackend{};
    ScopedBackend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    ScopedBackend.device = static_cast<ID3D12Device*>(InDevice);
    ffxCreateContextDescFrameGeneration ScopedCreate{};
    ScopedCreate.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    ScopedCreate.displaySize = {InOutputWidth, InOutputHeight};
    ScopedCreate.maxRenderSize = {InRenderWidth, InRenderHeight};
    ScopedCreate.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    ScopedCreate.header.pNext = &ScopedBackend.header;
    ffxContext ScopedContext = nullptr;
    const ffxReturnCode_t ScopedResult =
        GDx12Api.Functions.CreateContext(&ScopedContext, &ScopedCreate.header, nullptr);
    if (ScopedResult != FFX_API_RETURN_OK || ScopedContext == nullptr)
    {
        std::fprintf(stderr, "fsr3dx: context failed %d\n", static_cast<int>(ScopedResult));
        return false;
    }
    *OutContext = ScopedContext;
    return true;
}

bool Dispatch(void* InContext, void* InCommandList, void* InColor, void* InOutput,
              std::uint32_t InOutputWidth, std::uint32_t InOutputHeight, bool InReset,
              std::uint64_t InFrameId) noexcept
{
    (void)InOutputWidth;
    (void)InOutputHeight;
    if (InContext == nullptr || InCommandList == nullptr || InColor == nullptr || InOutput == nullptr)
    {
        return false;
    }
    ffxDispatchDescFrameGeneration ScopedDispatch{};
    ScopedDispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    ScopedDispatch.commandList = static_cast<ID3D12GraphicsCommandList*>(InCommandList);
    ScopedDispatch.presentColor =
        ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(InColor), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    ScopedDispatch.outputs[0] =
        ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(InOutput), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    ScopedDispatch.numGeneratedFrames = 1;
    ScopedDispatch.reset = InReset;
    ScopedDispatch.frameID = InFrameId;
    ffxContext ScopedContext = static_cast<ffxContext>(InContext);
    return GDx12Api.Functions.Dispatch(&ScopedContext, &ScopedDispatch.header) == FFX_API_RETURN_OK;
}

void DestroyContext(void* InContext) noexcept
{
    if (InContext != nullptr && GDx12Api.Functions.DestroyContext != nullptr)
    {
        ffxContext ScopedContext = static_cast<ffxContext>(InContext);
        GDx12Api.Functions.DestroyContext(&ScopedContext, nullptr);
    }
}

}

#else

namespace Fby::Rhi::Fsr3Dx12
{
bool Available() noexcept
{
    return false;
}

bool CreateContext(void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, void**)
{
    return false;
}

bool Dispatch(void*, void*, void*, void*, std::uint32_t, std::uint32_t, bool, std::uint64_t)
{
    return false;
}

void DestroyContext(void*) noexcept
{
}
}

#endif
