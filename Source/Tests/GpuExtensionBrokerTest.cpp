// FemboyEngine2D - extension broker requirements and filtering.
#include "Engine/RHI/GpuExtensionBroker.hpp"
#include "Engine/RHI/RHI.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}
}

int main()
{
    using namespace Fby::Rhi;
    GpuExtensionBroker ScopedBroker;
    ScopedBroker.AddInstance("VK_KHR_surface");
    ScopedBroker.AddInstance("VK_KHR_surface");
    ScopedBroker.AddDevice("VK_KHR_swapchain");
    ScopedBroker.AddDevice("VK_KHR_swapchain");
    Check(ScopedBroker.Instance().size() == 1 && ScopedBroker.Device().size() == 1,
          "the broker deduplicates extension names");

    GpuExtensionBroker ScopedNgx;
    ScopedNgx.AddInstance("VK_KHR_external_memory_capabilities");
    ScopedNgx.AddDevice("VK_KHR_external_memory");
    ScopedBroker.Add(ScopedNgx);
    Check(ScopedBroker.Instance().size() == 2 && ScopedBroker.Device().size() == 2,
          "subsystem requirements merge into the broker");

    const std::vector<std::string> ScopedAvailableInstance{"VK_KHR_surface",
                                                           "VK_KHR_external_memory_capabilities"};
    const std::vector<std::string> ScopedAvailableDevice{"VK_KHR_swapchain"};
    std::vector<std::string> ScopedMissing;
    const GpuExtensionBroker ScopedFiltered =
        ScopedBroker.Filter(ScopedAvailableInstance, ScopedAvailableDevice, &ScopedMissing);
    Check(ScopedFiltered.Instance().size() == 2, "supported instance extensions pass the filter");
    Check(ScopedFiltered.Device().size() == 1, "unsupported device extensions are dropped");
    Check(ScopedMissing.size() == 1 && ScopedMissing.front() == "VK_KHR_external_memory",
          "the missing extension is reported by name");

    Check(Fby::Rhi::VulkanTier((1u << 22) + (2u << 12)) == Fby::Rhi::GpuTier::Baseline,
          "Vulkan 1.2 maps to the baseline tier");
    Check(Fby::Rhi::VulkanTier((1u << 22) + (3u << 12)) == Fby::Rhi::GpuTier::Enhanced,
          "Vulkan 1.3 maps to the enhanced tier");
    Check(Fby::Rhi::VulkanTier((1u << 22) + (4u << 12)) == Fby::Rhi::GpuTier::Temporal,
          "Vulkan 1.4 maps to the temporal tier");
    Check(Fby::Rhi::BackendFromName("d3d12") == Fby::Rhi::Backend::D3D12,
          "the backend name parser accepts d3d12");
    Check(Fby::Rhi::BackendFromName("d3d11") == Fby::Rhi::Backend::D3D11,
          "the backend name parser accepts d3d11");
    Check(Fby::Rhi::BackendFromName("unknown") == Fby::Rhi::Backend::Null,
          "an unknown backend name falls back to Null");

    if (GFailures == 0)
    {
        std::printf("gpu_extension_broker: OK\n");
        return 0;
    }
    std::fprintf(stderr, "gpu_extension_broker: %d failure(s)\n", GFailures);
    return 1;
}
