// FemboyEngine2D - NGX/DLSS Vulkan extension discovery.
#pragma once

#include <string>
#include <vector>

namespace Fby::Rhi
{

// Returns the instance extensions NGX asks for DLSS SuperSampling. Empty when
// the NGX runtime is absent, the SDK is not compiled in, or the query fails.
[[nodiscard]] std::vector<std::string> DlssRequiredInstanceExtensions();

// Returns the device extensions NGX asks for, given the Vulkan instance and
// physical device. Must be called before vkCreateDevice.
[[nodiscard]] std::vector<std::string> DlssRequiredDeviceExtensions(void* InInstance, void* InPhysicalDevice);

// True when the NGX runtime DLL can be loaded next to the executable.
[[nodiscard]] bool DlssRuntimeFilePresent();

}
