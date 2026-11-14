// FemboyEngine2D - GPU/backend capability matrix smoke.
#include "Engine/RHI/Dx/DxProbe.hpp"
#include "Engine/RHI/RHI.hpp"

#include <cstdio>

int main()
{
#if defined(_WIN32)
    const std::vector<Fby::Rhi::DxAdapterInfo> ScopedAdapters = Fby::Rhi::ProbeDxAdapters();
    if (ScopedAdapters.empty())
    {
        std::fprintf(stderr, "gpu_matrix: no DXGI adapters\n");
        return 1;
    }
    std::printf("gpu_matrix: adapters=%zu\n", ScopedAdapters.size());
    for (const Fby::Rhi::DxAdapterInfo& ScopedAdapter : ScopedAdapters)
    {
        std::printf("gpu_matrix: adapter=\"%s\" vram=%llu d3d11=%d(%x) d3d12=%d(%x)\n", ScopedAdapter.Name,
                    static_cast<unsigned long long>(ScopedAdapter.DedicatedVideoMemory),
                    ScopedAdapter.SupportsD3D11 ? 1 : 0, ScopedAdapter.D3D11FeatureLevel,
                    ScopedAdapter.SupportsD3D12 ? 1 : 0, ScopedAdapter.D3D12FeatureLevel);
    }
    if (Fby::Rhi::BackendFromName("vulkan") != Fby::Rhi::Backend::Vulkan ||
        Fby::Rhi::BackendFromName("d3d12") != Fby::Rhi::Backend::D3D12 ||
        Fby::Rhi::BackendFromName("d3d11") != Fby::Rhi::Backend::D3D11 ||
        Fby::Rhi::BackendFromName("nope") != Fby::Rhi::Backend::Null)
    {
        std::fprintf(stderr, "gpu_matrix: backend name parser mismatch\n");
        return 1;
    }
    std::printf("gpu_matrix: OK\n");
    return 0;
#else
    std::printf("gpu_matrix: skipped (non-Windows)\n");
    return 0;
#endif
}
