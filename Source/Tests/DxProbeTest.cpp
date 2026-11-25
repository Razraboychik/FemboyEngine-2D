// FemboyEngine2D - DXGI adapter capability probe.
#include "Engine/RHI/Dx/DxProbe.hpp"

#include <cstdio>
#include <cstring>

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
    if (ScopedAdapters.empty())
    {
        return 0;
    }
    int ScopedFailures = 0;
    for (const Fby::Rhi::DxAdapterInfo& ScopedAdapter : ScopedAdapters)
    {
        if (std::memchr(ScopedAdapter.Name, '\0', sizeof(ScopedAdapter.Name)) == nullptr)
        {
            std::fprintf(stderr, "adapter name is not NUL-terminated\n");
            ++ScopedFailures;
        }
    }
    if (ScopedD3D11 == 0 && ScopedD3D12 == 0)
    {
        std::fprintf(stderr, "adapters were enumerated but none supports D3D11 or D3D12\n");
        ++ScopedFailures;
    }
    return ScopedFailures == 0 ? 0 : 1;
}
