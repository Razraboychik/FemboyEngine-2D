// FemboyEngine2D - DXGI adapter capability probe.
#include "Engine/RHI/Dx/DxProbe.hpp"

#include <cstdio>

int main()
{
    const auto ScopedAdapters = Fby::Rhi::ProbeDxAdapters();
    int ScopedD3D12 = 0;
    int ScopedD3D11 = 0;
    for (const Fby::Rhi::DxAdapterInfo& ScopedAdapter : ScopedAdapters)
    {
        std::printf("dx_adapter: %s d3d12=%d d3d11=%d vram=%llu\n", ScopedAdapter.Name,
                    ScopedAdapter.SupportsD3D12 ? 1 : 0, ScopedAdapter.SupportsD3D11 ? 1 : 0,
                    static_cast<unsigned long long>(ScopedAdapter.DedicatedVideoMemory));
        ScopedD3D12 += ScopedAdapter.SupportsD3D12 ? 1 : 0;
        ScopedD3D11 += ScopedAdapter.SupportsD3D11 ? 1 : 0;
    }
    std::printf("dx_probe: adapters=%zu d3d12=%d d3d11=%d\n", ScopedAdapters.size(), ScopedD3D12, ScopedD3D11);
    // Headless CI and non-Windows builds legitimately have no DXGI adapter.
    return 0;
}
