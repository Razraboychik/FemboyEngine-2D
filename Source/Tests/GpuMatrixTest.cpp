// FemboyEngine2D - GPU/backend capability matrix smoke.
#include "Engine/RHI/Dx/DxProbe.hpp"
#include "Engine/RHI/RHI.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
int CheckBackendNames()
{
    struct Entry
    {
        const char* Name;
        Fby::Rhi::Backend Value;
    };
    constexpr Entry kEntries[] = {
        {"vulkan", Fby::Rhi::Backend::Vulkan}, {"metal", Fby::Rhi::Backend::Metal},
        {"d3d12", Fby::Rhi::Backend::D3D12},   {"d3d11", Fby::Rhi::Backend::D3D11},
        {"null", Fby::Rhi::Backend::Null},
    };
    for (const Entry& ScopedEntry : kEntries)
    {
        if (Fby::Rhi::BackendFromName(ScopedEntry.Name) != ScopedEntry.Value ||
            std::strcmp(Fby::Rhi::BackendName(ScopedEntry.Value), ScopedEntry.Name) != 0)
        {
            std::fprintf(stderr, "gpu_matrix: backend name mismatch for '%s'\n", ScopedEntry.Name);
            return 1;
        }
    }
    if (Fby::Rhi::BackendFromName("") != Fby::Rhi::Backend::Null ||
        Fby::Rhi::BackendFromName("nope") != Fby::Rhi::Backend::Null)
    {
        std::fprintf(stderr, "gpu_matrix: unknown backend name was accepted\n");
        return 1;
    }
    return 0;
}
}

int main()
{
    if (CheckBackendNames() != 0)
    {
        return 1;
    }
#if defined(_WIN32)
    const std::vector<Fby::Rhi::DxAdapterInfo> ScopedAdapters = Fby::Rhi::ProbeDxAdapters();
    if (ScopedAdapters.empty())
    {
        std::printf("gpu_matrix: no DXGI adapters, backend names verified\n");
        return 0;
    }
    int ScopedFailures = 0;
    for (const Fby::Rhi::DxAdapterInfo& ScopedAdapter : ScopedAdapters)
    {
        if (std::memchr(ScopedAdapter.Name, 0, sizeof(ScopedAdapter.Name)) == nullptr)
        {
            std::fprintf(stderr, "gpu_matrix: adapter name is not NUL-terminated\n");
            ++ScopedFailures;
        }
        if (ScopedAdapter.SupportsD3D11 != (ScopedAdapter.D3D11FeatureLevel != 0) ||
            ScopedAdapter.SupportsD3D12 != (ScopedAdapter.D3D12FeatureLevel >= 0xB000))
        {
            std::fprintf(stderr, "gpu_matrix: inconsistent feature-level flags for '%s'\n", ScopedAdapter.Name);
            ++ScopedFailures;
        }
        std::printf("gpu_matrix: adapter=\"%s\" vram=%llu d3d11=%d(%x) d3d12=%d(%x)\n", ScopedAdapter.Name,
                    static_cast<unsigned long long>(ScopedAdapter.DedicatedVideoMemory),
                    ScopedAdapter.SupportsD3D11 ? 1 : 0, ScopedAdapter.D3D11FeatureLevel,
                    ScopedAdapter.SupportsD3D12 ? 1 : 0, ScopedAdapter.D3D12FeatureLevel);
    }
    if (ScopedFailures != 0)
    {
        return 1;
    }
#endif
    std::printf("gpu_matrix: OK\n");
    return 0;
}
