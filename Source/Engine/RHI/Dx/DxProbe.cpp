// FemboyEngine2D - Direct3D adapter capability probe.
#include "Engine/RHI/Dx/DxProbe.hpp"

#if defined(_WIN32)
#include "Engine/Core/Log.hpp"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <iterator>

namespace Fby::Rhi
{
namespace
{
using Microsoft::WRL::ComPtr;

[[nodiscard]] std::uint32_t ProbeD3D12(IDXGIAdapter1* InAdapter)
{
    ComPtr<ID3D12Device> ScopedDevice;
    if (FAILED(D3D12CreateDevice(InAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&ScopedDevice))) ||
        ScopedDevice == nullptr)
    {
        return 0;
    }
    D3D12_FEATURE_DATA_FEATURE_LEVELS ScopedLevels{};
    constexpr D3D_FEATURE_LEVEL ScopedRequested[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,
                                                     D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                                     D3D_FEATURE_LEVEL_11_0};
    ScopedLevels.NumFeatureLevels = static_cast<UINT>(std::size(ScopedRequested));
    ScopedLevels.pFeatureLevelsRequested = ScopedRequested;
    if (FAILED(ScopedDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &ScopedLevels,
                                                 sizeof(ScopedLevels))))
    {
        return static_cast<std::uint32_t>(D3D_FEATURE_LEVEL_11_0);
    }
    return static_cast<std::uint32_t>(ScopedLevels.MaxSupportedFeatureLevel);
}

[[nodiscard]] std::uint32_t ProbeD3D11(IDXGIAdapter1* InAdapter)
{
    ComPtr<ID3D11Device> ScopedDevice;
    D3D_FEATURE_LEVEL ScopedLevel = D3D_FEATURE_LEVEL_11_0;
    constexpr D3D_FEATURE_LEVEL ScopedRequested[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                                     D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const HRESULT ScopedResult = D3D11CreateDevice(
        InAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, ScopedRequested,
        static_cast<UINT>(std::size(ScopedRequested)), D3D11_SDK_VERSION, &ScopedDevice, &ScopedLevel, nullptr);
    return SUCCEEDED(ScopedResult) ? static_cast<std::uint32_t>(ScopedLevel) : 0;
}
}

std::vector<DxAdapterInfo> ProbeDxAdapters()
{
    std::vector<DxAdapterInfo> ScopedAdapters;
    ComPtr<IDXGIFactory1> ScopedFactory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&ScopedFactory))) || ScopedFactory == nullptr)
    {
        LogWarn("dx", "CreateDXGIFactory1 failed");
        return ScopedAdapters;
    }
    for (UINT ScopedIndex = 0;; ++ScopedIndex)
    {
        ComPtr<IDXGIAdapter1> ScopedAdapter;
        if (ScopedFactory->EnumAdapters1(ScopedIndex, &ScopedAdapter) == DXGI_ERROR_NOT_FOUND ||
            ScopedAdapter == nullptr)
        {
            break;
        }
        DXGI_ADAPTER_DESC1 ScopedDesc{};
        if (FAILED(ScopedAdapter->GetDesc1(&ScopedDesc)))
        {
            continue;
        }
        DxAdapterInfo ScopedInfo;
        std::snprintf(ScopedInfo.Name, sizeof(ScopedInfo.Name), "%ls", ScopedDesc.Description);
        ScopedInfo.DedicatedVideoMemory = ScopedDesc.DedicatedVideoMemory;
        ScopedInfo.VendorId = ScopedDesc.VendorId;
        ScopedInfo.D3D12FeatureLevel = ProbeD3D12(ScopedAdapter.Get());
        ScopedInfo.D3D11FeatureLevel = ProbeD3D11(ScopedAdapter.Get());
        ScopedInfo.SupportsD3D12 = ScopedInfo.D3D12FeatureLevel >= D3D_FEATURE_LEVEL_11_0;
        ScopedInfo.SupportsD3D11 = ScopedInfo.D3D11FeatureLevel >= D3D_FEATURE_LEVEL_11_0;
        ScopedAdapters.push_back(ScopedInfo);
    }
    return ScopedAdapters;
}
}
#else
namespace Fby::Rhi
{
std::vector<DxAdapterInfo> ProbeDxAdapters()
{
    return {};
}
}
#endif
