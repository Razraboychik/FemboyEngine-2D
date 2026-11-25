#include "Engine/RHI/Vulkan/VulkanCapacity.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>

int main()
{
    using Fby::Rhi::VulkanCapacity::ClampInstanceCapacity;
    constexpr std::uint32_t kItemBytes = 48;
    constexpr std::uint32_t kMax = 1u << 20;
    int ScopedFailures = 0;
    const auto Check = [&ScopedFailures](bool InCondition, const char* InMessage)
    {
        if (!InCondition)
        {
            std::fprintf(stderr, "FAIL: %s\n", InMessage);
            ++ScopedFailures;
        }
    };
    Check(ClampInstanceCapacity(0, 256ull << 20, 3, kItemBytes) == 1, "zero request clamps to one");
    Check(ClampInstanceCapacity(std::numeric_limits<std::uint32_t>::max(), 512ull << 20, 3, kItemBytes) == kMax,
          "huge request clamps to the hard cap");
    Check(ClampInstanceCapacity(kMax, 64ull << 20, 3, kItemBytes) < kMax, "device range lowers the cap");
    Check(ClampInstanceCapacity(100, 0, 3, kItemBytes) == 1, "zero range is safe");
    Check(ClampInstanceCapacity(100, 256ull << 20, 0, kItemBytes) == 1, "zero frames is safe");
    Check(ClampInstanceCapacity(1024, 256ull << 20, 3, kItemBytes) == 1024, "ordinary request is unchanged");
    if (ScopedFailures == 0)
    {
        std::printf("vulkan_instance_capacity_test: all checks passed\n");
        return 0;
    }
    return 1;
}
