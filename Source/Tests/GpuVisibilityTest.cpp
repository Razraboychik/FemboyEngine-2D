// FemboyEngine2D - GPU visibility layout tests.
#include "Engine/RHI/GpuVisibility.hpp"

#include <cstdio>

int main()
{
    using namespace Fby::Rhi;

    static_assert(sizeof(DrawIndexedIndirectCommand) == 20);
    static_assert(offsetof(DrawIndexedIndirectCommand, InstanceCount) == 4);
    static_assert(sizeof(GpuCullingConstants) == 72);
    static_assert(offsetof(GpuCullingConstants, Viewport) == 16);
    static_assert(offsetof(GpuCullingConstants, ItemCount) == 32);
    static_assert(offsetof(GpuCullingConstants, OutputOffset) == 44);
    static_assert(offsetof(GpuCullingConstants, CounterOffset) == 48);
    static_assert(offsetof(GpuCullingConstants, CommandOffset) == 52);
    static_assert(offsetof(GpuCullingConstants, UseDepth) == 64);

    if (sizeof(DrawIndexedIndirectCommand) != 20u || sizeof(GpuCullingConstants) != 72u)
    {
        std::fprintf(stderr, "gpu_visibility: invalid layout\n");
        return 1;
    }
    std::printf("gpu_visibility: OK\n");
    return 0;
}
