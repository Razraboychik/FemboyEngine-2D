// FemboyEngine2D - Direct3D adapter capability probe (D3D11/D3D12).
#pragma once

#include <cstdint>
#include <vector>

namespace Fby::Rhi
{

struct DxAdapterInfo
{
    char Name[128] = {};
    std::uint64_t DedicatedVideoMemory = 0;
    std::uint32_t VendorId = 0;
    std::uint32_t D3D12FeatureLevel = 0;
    std::uint32_t D3D11FeatureLevel = 0;
    bool SupportsD3D12 = false;
    bool SupportsD3D11 = false;
};

// Enumerates DXGI adapters and probes D3D11/D3D12 device creation. Empty on
// non-Windows builds or when DXGI cannot be initialised.
[[nodiscard]] std::vector<DxAdapterInfo> ProbeDxAdapters();

}
