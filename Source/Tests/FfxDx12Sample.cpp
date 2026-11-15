// FemboyEngine2D - minimal FFX DX12 frame-generation context sample.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32) && defined(FBY_WITH_FIDELITYFX)
#include "ffx_api.h"
#include "ffx_api_loader.h"
#include "ffx_framegeneration.h"
#include "dx12/ffx_api_dx12.h"

#include <d3d12.h>
#include <windows.h>

namespace
{
struct Api
{
    HMODULE Module = nullptr;
    ffxFunctions Functions{};
};

int RunVariant(int InVariant)
{
    Api ScopedApi;
    wchar_t ScopedPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, ScopedPath, MAX_PATH);
    wchar_t* ScopedSlash = wcsrchr(ScopedPath, L'\\');
    if (ScopedSlash != nullptr)
    {
        *(ScopedSlash + 1) = L'\0';
    }
    ScopedApi.Module =
        LoadLibraryW((std::wstring(ScopedPath) + L"amd_fidelityfx_framegeneration_dx12.dll").c_str());
    std::printf("ffx_sample: dll=%d err=%lu\n", ScopedApi.Module != nullptr ? 1 : 0, GetLastError());
    std::fflush(stdout);
    if (ScopedApi.Module == nullptr)
    {
        return 2;
    }
    ScopedApi.Functions.CreateContext =
        reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(ScopedApi.Module, "ffxCreateContext"));
    ScopedApi.Functions.DestroyContext =
        reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(ScopedApi.Module, "ffxDestroyContext"));
    ScopedApi.Functions.Dispatch =
        reinterpret_cast<PfnFfxDispatch>(GetProcAddress(ScopedApi.Module, "ffxDispatch"));
    std::printf("ffx_sample: create=%p\n", reinterpret_cast<void*>(ScopedApi.Functions.CreateContext));
    std::fflush(stdout);
    if (ScopedApi.Functions.CreateContext == nullptr)
    {
        return 2;
    }

    ID3D12Device* ScopedDevice = nullptr;
    const HRESULT ScopedDeviceResult =
        D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&ScopedDevice));
    std::printf("ffx_sample: device=0x%lx\n", static_cast<unsigned long>(ScopedDeviceResult));
    std::fflush(stdout);
    if (FAILED(ScopedDeviceResult))
    {
        return 2;
    }

    ffxCreateBackendDX12Desc ScopedBackend{};
    ScopedBackend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    ScopedBackend.device = ScopedDevice;
    ffxCreateContextDescFrameGenerationVersion ScopedVersion{};
    ScopedVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
    ScopedVersion.version = FFX_FRAMEGENERATION_VERSION;
    ffxCreateContextDescFrameGenerationHudless ScopedHudless{};
    ScopedHudless.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
    ScopedHudless.hudlessBackBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;

    ffxCreateContextDescFrameGeneration ScopedCreate{};
    ScopedCreate.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    ScopedCreate.displaySize = {1280, 720};
    ScopedCreate.maxRenderSize = {640, 360};
    ScopedCreate.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;

    ffxApiHeader* ScopedTail = nullptr;
    if ((InVariant & 1) != 0)
    {
        ScopedTail = &ScopedVersion.header;
    }
    if ((InVariant & 2) != 0)
    {
        ScopedVersion.header.pNext = ScopedTail;
        ScopedTail = &ScopedHudless.header;
    }
    ScopedBackend.header.pNext = ScopedTail;
    ScopedCreate.header.pNext = &ScopedBackend.header;

    std::printf("ffx_sample: variant=%d calling CreateContext\n", InVariant);
    std::fflush(stdout);
    ffxContext ScopedContext = nullptr;
    const ffxReturnCode_t ScopedResult =
        ScopedApi.Functions.CreateContext(&ScopedContext, &ScopedCreate.header, nullptr);
    std::printf("ffx_sample: result=%d context=%p\n", static_cast<int>(ScopedResult), ScopedContext);
    std::fflush(stdout);
    if (ScopedContext != nullptr && ScopedApi.Functions.DestroyContext != nullptr)
    {
        ScopedApi.Functions.DestroyContext(&ScopedContext, nullptr);
    }
    ScopedDevice->Release();
    return ScopedResult == FFX_API_RETURN_OK ? 0 : 1;
}
}
#endif

int main(int InArgc, char** InArgv)
{
#if defined(_WIN32) && defined(FBY_WITH_FIDELITYFX)
    setvbuf(stdout, nullptr, _IONBF, 0);
    const int ScopedVariant = InArgc > 1 ? std::atoi(InArgv[1]) : 0;
    return RunVariant(ScopedVariant);
#else
    (void)InArgc;
    (void)InArgv;
    std::printf("ffx_sample: skipped (FidelityFX off or non-Windows)\n");
    return 0;
#endif
}
