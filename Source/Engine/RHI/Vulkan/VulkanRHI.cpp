#include "Engine/RHI/Vulkan/VulkanRHI.hpp"

#include "Engine/RHI/GpuVisibility.hpp"
#include "Engine/UI/Accessibility.hpp"

#include "Engine/Core/Assert.hpp"
#include "Engine/RHI/RenderPasses.hpp"
#include "Engine/RHI/VirtualTextureFormat.hpp"
#include "Engine/Core/Log.hpp"

#if !defined(FBY_HAS_VULKAN_HEADERS)

// Built without Vulkan headers (an Apple build, or a configuration that turned
namespace Fby::Rhi
{
std::unique_ptr<RHI> CreateVulkanRHI()
{
    LogError("vulkan", "this build has no Vulkan headers");
    return nullptr;
}
}

#else

#include "Engine/RHI/Vulkan/VulkanLoader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "ShaderBlobs.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "VulkanRHIInternal.hpp"

#if defined(FBY_HAS_VULKAN_HEADERS)
namespace Fby::Rhi
{
std::unique_ptr<RHI> CreateVulkanRHI()
{
    return std::make_unique<Impl::VulkanRHI>();
}
}
#endif

// Loader
namespace Fby::Rhi::Vk
{
bool Api::LoadGlobal()
{
    SDL_FunctionPointer ScopedPointer = SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (ScopedPointer == nullptr)
    {
        return false;
    }
    GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(ScopedPointer);

#define FBY_VK_LOAD_GLOBAL(name)                                                                                       \
    name = reinterpret_cast<PFN_##name>(GetInstanceProcAddr(nullptr, #name));                                          \
    if (name == nullptr)                                                                                               \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    FBY_VK_GLOBAL_FUNCTIONS(FBY_VK_LOAD_GLOBAL)
#undef FBY_VK_LOAD_GLOBAL
    return true;
}

bool Api::LoadInstance(VkInstance instance)
{
#define FBY_VK_LOAD_INSTANCE(name)                                                                                     \
    name = reinterpret_cast<PFN_##name>(GetInstanceProcAddr(instance, #name));                                         \
    if (name == nullptr)                                                                                               \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    FBY_VK_INSTANCE_FUNCTIONS(FBY_VK_LOAD_INSTANCE)
#undef FBY_VK_LOAD_INSTANCE
    return true;
}

bool Api::LoadDevice(VkDevice device)
{
#define FBY_VK_LOAD_DEVICE(name)                                                                                       \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));                                           \
    if (name == nullptr)                                                                                               \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    FBY_VK_DEVICE_FUNCTIONS(FBY_VK_LOAD_DEVICE)
#undef FBY_VK_LOAD_DEVICE
    return true;
}

Api& GetApi()
{
    static Api ScopedApi;
    return ScopedApi;
}
}

// FBY_HAS_VULKAN_HEADERS
#endif
