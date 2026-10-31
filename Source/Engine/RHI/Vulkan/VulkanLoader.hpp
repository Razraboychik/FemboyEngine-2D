// FemboyEngine2D - Vulkan entry point loader.
#pragma once

#if !defined(FBY_HAS_VULKAN_HEADERS)
#error "VulkanLoader.hpp requires FBY_HAS_VULKAN_HEADERS"
#endif

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

namespace Fby::Rhi::Vk
{

// Every function the backend calls, grouped by the scope it must be loaded in:
#define FBY_VK_GLOBAL_FUNCTIONS(X)                                                                                     \
    X(vkEnumerateInstanceVersion)                                                                                      \
    X(vkCreateInstance)                                                                                                \
    X(vkEnumerateInstanceExtensionProperties)

#define FBY_VK_INSTANCE_FUNCTIONS(X)                                                                                   \
    X(vkDestroyInstance)                                                                                               \
    X(vkEnumeratePhysicalDevices)                                                                                      \
    X(vkGetPhysicalDeviceProperties)                                                                                   \
    X(vkGetPhysicalDeviceFeatures2)                                                                                    \
    X(vkGetPhysicalDeviceMemoryProperties)                                                                             \
    X(vkGetPhysicalDeviceFormatProperties)                                                                             \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                                        \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                                       \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)                                                                       \
    X(vkCreateDevice)                                                                                                  \
    X(vkDestroySurfaceKHR)                                                                                             \
    X(vkGetDeviceProcAddr)

#define FBY_VK_DEVICE_FUNCTIONS(X)                                                                                     \
    X(vkDestroyDevice)                                                                                                 \
    X(vkGetDeviceQueue)                                                                                                \
    X(vkDeviceWaitIdle)                                                                                                \
    X(vkQueueWaitIdle)                                                                                                 \
    X(vkCreateSwapchainKHR)                                                                                            \
    X(vkDestroySwapchainKHR)                                                                                           \
    X(vkGetSwapchainImagesKHR)                                                                                         \
    X(vkCreateImageView)                                                                                               \
    X(vkDestroyImageView)                                                                                              \
    X(vkCreateRenderPass)                                                                                              \
    X(vkDestroyRenderPass)                                                                                             \
    X(vkCreateFramebuffer)                                                                                             \
    X(vkDestroyFramebuffer)                                                                                            \
    X(vkCreateCommandPool)                                                                                             \
    X(vkDestroyCommandPool)                                                                                            \
    X(vkAllocateCommandBuffers)                                                                                        \
    X(vkFreeCommandBuffers)                                                                                            \
    X(vkBeginCommandBuffer)                                                                                            \
    X(vkEndCommandBuffer)                                                                                              \
    X(vkResetCommandBuffer)                                                                                            \
    X(vkCmdBeginRenderPass)                                                                                            \
    X(vkCmdEndRenderPass)                                                                                              \
    X(vkCmdPipelineBarrier)                                                                                            \
    X(vkCmdCopyBuffer)                                                                                                 \
    X(vkCmdCopyBufferToImage)                                                                                          \
    X(vkCmdCopyImageToBuffer)                                                                                          \
    X(vkCmdClearColorImage)                                                                                          \
    X(vkCmdBindPipeline)                                                                                               \
    X(vkCmdBindDescriptorSets)                                                                                         \
    X(vkCmdBindVertexBuffers)                                                                                          \
    X(vkCmdBindIndexBuffer)                                                                                            \
    X(vkCmdDraw)                                                       \
    X(vkCmdDrawIndexed)                                                                                                \
    X(vkCmdDrawIndexedIndirect)                                                                                        \
    X(vkCmdDispatch)                                                                                                   \
    X(vkCmdPushConstants)                                                                                              \
    X(vkCmdSetViewport)                                                                                                \
    X(vkCmdSetScissor)                                                                                                 \
    X(vkCreateSemaphore)                                                                                               \
    X(vkDestroySemaphore)                                                                                              \
    X(vkCreateFence)                                                                                                   \
    X(vkDestroyFence)                                                                                                  \
    X(vkWaitForFences)                                                                                                 \
    X(vkResetFences)                                                                                                   \
    X(vkAcquireNextImageKHR)                                                                                           \
    X(vkQueueSubmit)                                                                                                   \
    X(vkQueuePresentKHR)                                                                                               \
    X(vkCreateShaderModule)                                                                                            \
    X(vkDestroyShaderModule)                                                                                           \
    X(vkCreatePipelineLayout)                                                                                          \
    X(vkDestroyPipelineLayout)                                                                                         \
    X(vkCreateGraphicsPipelines)                                                                                       \
    X(vkCreateComputePipelines)                                                                                        \
    X(vkDestroyPipeline)                                                                                               \
    X(vkCreateDescriptorSetLayout)                                                                                     \
    X(vkDestroyDescriptorSetLayout)                                                                                    \
    X(vkCreateDescriptorPool)                                                                                          \
    X(vkDestroyDescriptorPool)                                                                                         \
    X(vkAllocateDescriptorSets)                                                                                        \
    X(vkFreeDescriptorSets)                                                                                            \
    X(vkUpdateDescriptorSets)                                                                                          \
    X(vkCreateSampler)                                                                                                 \
    X(vkDestroySampler)                                                                                                \
    X(vkCreateImage)                                                                                                   \
    X(vkDestroyImage)                                                                                                  \
    X(vkGetImageMemoryRequirements)                                                                                    \
    X(vkBindImageMemory)                                                                                               \
    X(vkCreateBuffer)                                                                                                  \
    X(vkDestroyBuffer)                                                                                                 \
    X(vkGetBufferMemoryRequirements)                                                                                   \
    X(vkBindBufferMemory)                                                                                              \
    X(vkAllocateMemory)                                                                                                \
    X(vkFreeMemory)                                                                                                    \
    X(vkMapMemory)                                                                                                     \
    X(vkUnmapMemory)

struct Api
{
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;

#define FBY_VK_DECLARE(name) PFN_##name name = nullptr;
    FBY_VK_GLOBAL_FUNCTIONS(FBY_VK_DECLARE)
    FBY_VK_INSTANCE_FUNCTIONS(FBY_VK_DECLARE)
    FBY_VK_DEVICE_FUNCTIONS(FBY_VK_DECLARE)
#undef FBY_VK_DECLARE

    // Resolves vkGetInstanceProcAddr through SDL and then the global entry
    bool LoadGlobal();

    bool LoadInstance(VkInstance InInstance);
    bool LoadDevice(VkDevice InDevice);
};

// One process-wide instance; the loader is thread-safe and stateless.
[[nodiscard]] Api& GetApi();
}
