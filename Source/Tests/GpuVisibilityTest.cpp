// FemboyEngine2D - GPU visibility layout tests.
#include "Engine/RHI/GpuVisibility.hpp"

#include <cstddef>
#include <cstdio>
#include <type_traits>

int main()
{
    using namespace Fby::Rhi;

    static_assert(sizeof(DrawIndexedIndirectCommand) == 20);
    static_assert(offsetof(DrawIndexedIndirectCommand, IndexCount) == 0);
    static_assert(offsetof(DrawIndexedIndirectCommand, InstanceCount) == 4);
    static_assert(offsetof(DrawIndexedIndirectCommand, FirstIndex) == 8);
    static_assert(offsetof(DrawIndexedIndirectCommand, VertexOffset) == 12);
    static_assert(offsetof(DrawIndexedIndirectCommand, FirstInstance) == 16);
    static_assert(std::is_same_v<decltype(DrawIndexedIndirectCommand::VertexOffset), std::int32_t>);
    static_assert(sizeof(GpuCullingConstants) == 72);
    static_assert(offsetof(GpuCullingConstants, ViewBounds) == 0);
    static_assert(offsetof(GpuCullingConstants, Viewport) == 16);
    static_assert(offsetof(GpuCullingConstants, ItemCount) == 32);
    static_assert(offsetof(GpuCullingConstants, MaxVisible) == 36);
    static_assert(offsetof(GpuCullingConstants, SourceOffset) == 40);
    static_assert(offsetof(GpuCullingConstants, OutputOffset) == 44);
    static_assert(offsetof(GpuCullingConstants, CounterOffset) == 48);
    static_assert(offsetof(GpuCullingConstants, CommandOffset) == 52);
    static_assert(offsetof(GpuCullingConstants, Reserved0) == 56);
    static_assert(offsetof(GpuCullingConstants, Reserved1) == 60);
    static_assert(offsetof(GpuCullingConstants, UseDepth) == 64);
    static_assert(offsetof(GpuCullingConstants, Reserved2) == 68);

    if (sizeof(DrawIndexedIndirectCommand) != 20u || sizeof(GpuCullingConstants) != 72u)
    {
        std::fprintf(stderr, "gpu_visibility: invalid layout\n");
        return 1;
    }
    std::printf("gpu_visibility: OK\n");
    return 0;
}
