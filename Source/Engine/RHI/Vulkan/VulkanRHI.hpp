// FemboyEngine2D - Vulkan backend entry point.
#pragma once

#include "Engine/RHI/RHI.hpp"

#include <memory>

namespace Fby::Rhi
{
// Implemented in VulkanRHI.cpp. Returns nullptr (and logs why) when the Vulkan
[[nodiscard]] std::unique_ptr<RHI> CreateVulkanRHI();
}
