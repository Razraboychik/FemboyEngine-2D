// FemboyEngine2D - Vulkan backend definitions.
#include "VulkanRHIInternal.hpp"

#if defined(FBY_HAS_VULKAN_HEADERS)
namespace Fby::Rhi::Impl
{
bool VulkanRHI::Initialize(const DeviceDesc& InDesc)
{
    MDesc = InDesc;
    MClusterRequested = InDesc.enableClusteredLighting;
    MDepthPrepassRequested = InDesc.enableDepthPrepass;
    MCullRequested = InDesc.enableGpuCulling;
    MDeviceLost = false;
    MInfo.backend = Backend::Vulkan;
    MFramesInFlight = std::clamp(InDesc.framesInFlight, 1u, kMaxFramesInFlight);
    MInfo.framesInFlight = MFramesInFlight;
    MInfo.maxItemsPerFrame = InDesc.maxItemsPerFrame;
    MInfo.maxTextureLayers = InDesc.maxTextureLayers;
    MInfo.maxMeshVerticesPerFrame = InDesc.maxMeshVerticesPerFrame;
    MInfo.maxBoneMatricesPerFrame = InDesc.maxBoneMatricesPerFrame;
    MInfo.maxStaticMeshVertices = InDesc.maxStaticMeshVertices;
    MInfo.maxMeshDrawsPerFrame = InDesc.maxMeshDrawsPerFrame;
    MTextureArrays.clear();
    // Id 0 = built-in atlas
    MTextureArrays.push_back(nullptr);
    MFreeTextureArrayIds.clear();
    MVirtualTextures.clear();
    // Id 0 = invalid handle
    MVirtualTextures.push_back(nullptr);
    MFreeVirtualTextureIds.clear();

    if (!Vk::GetApi().LoadGlobal())
    {
        LogError("vulkan", "no Vulkan loader on this system");
        return false;
    }
    if (!CreateInstance(InDesc) || !PickPhysicalDevice() || !CreateDevice())
    {
        return false;
    }
    Vk::GetApi().vkGetPhysicalDeviceMemoryProperties(MPhysical, &MMemoryProperties);
    // The requested layer bound is a policy; the device limit is a hard cap.
    VkPhysicalDeviceProperties ScopedDeviceProperties{};
    Vk::GetApi().vkGetPhysicalDeviceProperties(MPhysical, &ScopedDeviceProperties);
    MInfo.maxTextureLayers =
        std::min(InDesc.maxTextureLayers, ScopedDeviceProperties.limits.maxImageArrayLayers);

    if (!CreateSwapchain(InDesc.size) || !CreateRenderPass() || !CreateFramebuffers() || !CreateCommands() ||
        !CreateSyncObjects())
    {
        return false;
    }
    if (!CreateDescriptors() || !CreatePipeline() || !CreateDepthPrepassResources() || !CreateGeometry() ||
        !CreateInstanceRing() || !CreateHiZResources() || !CreateBuiltInAtlas() || !CreateMeshResources() ||
        !CreateStaticMeshArena() || !CreateMeshDescriptors() || !CreateMeshPipelines() ||
        !CreateOffscreenRenderPass() || !CreateUploadArena(InDesc.maxTextureUploadBytes))
    {
        return false;
    }
    if (!CreateOitResolveResources())
    {
        LogInfo("vulkan", "OIT resolve unavailable; transparent resolve disabled");
    }
    if (!CreateUpscaleResources())
    {
        LogInfo("vulkan", "spatial upscale unavailable; bilinear fallback");
    }
    else
    {
        MInfo.upscalers.fsr1 = true;
    }
    if (!CreateHudResources())
    {
        LogInfo("vulkan", "HUD composite unavailable");
    }
    if (!CreateMotionResources())
    {
        LogInfo("vulkan", "motion vector pass unavailable");
    }
    else
    {
        MInfo.upscalers.motionVectors = true;
    }
    if (!CreateTemporalResources())
    {
        LogInfo("vulkan", "temporal resolve unavailable");
    }
    else
    {
        MInfo.upscalers.fsr2 = MInfo.upscalers.motionVectors && MInfo.upscalers.fsr1;
        MInfo.upscalers.frameGeneration = MInfo.upscalers.motionVectors;
    }
    MHiZReady = MDesc.enableHiZ;
    if (MCullRequested)
    {
        if (!CreateCullResources())
        {
            LogInfo("vulkan", "GPU culling unavailable; using CPU depth batches");
            DestroyCullResources();
        }
    }
    // The offscreen scene image and the swapchain composite pipelines are created
    // once the accessibility resources exist; without this they appeared only
    // after the first resize.
    if (MAccessibilityReady)
    {
        (void)CreateAccessibilitySwapchainResources();
    }

    LogInfo("vulkan", "device: {} (api {}.{}.{}), {} frames in flight", MInfo.name,
            VK_API_VERSION_MAJOR(MInfo.apiVersion), VK_API_VERSION_MINOR(MInfo.apiVersion),
            VK_API_VERSION_PATCH(MInfo.apiVersion), MFramesInFlight);
    return true;
}

bool VulkanRHI::CreateInstance(const DeviceDesc& InDesc)
{
    Vk::Api& ScopedApi = Vk::GetApi();

    std::uint32_t ScopedSdlExtensionCount = 0;
    const char* const* ScopedSdlExtensions = SDL_Vulkan_GetInstanceExtensions(&ScopedSdlExtensionCount);
    if (ScopedSdlExtensions == nullptr)
    {
        LogError("vulkan", "SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        return false;
    }

    std::vector<const char*> ScopedExtensions(ScopedSdlExtensions, ScopedSdlExtensions + ScopedSdlExtensionCount);
    const std::vector<VkExtensionProperties> ScopedAvailable = QueryInstanceExtensions();
    if (InDesc.enableValidation && HasExtension(ScopedAvailable, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        ScopedExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> ScopedLayers;
    if (InDesc.enableValidation)
    {
        const auto ScopedEnumerateLayers = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
            ScopedApi.GetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties"));
        std::uint32_t ScopedLayerCount = 0;
        if (ScopedEnumerateLayers != nullptr && ScopedEnumerateLayers(&ScopedLayerCount, nullptr) == VK_SUCCESS &&
            ScopedLayerCount > 0)
        {
            std::vector<VkLayerProperties> ScopedLayerProperties(ScopedLayerCount);
            if (ScopedEnumerateLayers(&ScopedLayerCount, ScopedLayerProperties.data()) == VK_SUCCESS)
            {
                for (const VkLayerProperties& ScopedProperties : ScopedLayerProperties)
                {
                    if (std::strcmp(ScopedProperties.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                    {
                        ScopedLayers.push_back("VK_LAYER_KHRONOS_validation");
                        break;
                    }
                }
            }
        }
        LogInfo("vulkan", "validation: {}", ScopedLayers.empty() ? "layer not available" : "VK_LAYER_KHRONOS_validation");
    }

    VkApplicationInfo ScopedApplication{};
    ScopedApplication.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ScopedApplication.pApplicationName = "FemboyEngine";
    ScopedApplication.applicationVersion = VK_MAKE_API_VERSION(0, 0, 4, 0);
    ScopedApplication.pEngineName = "FemboyEngine";
    ScopedApplication.engineVersion = VK_MAKE_API_VERSION(0, 0, 4, 0);
    ScopedApplication.apiVersion = VK_API_VERSION_1_4;
    std::uint32_t ScopedInstanceVersion = VK_API_VERSION_1_4;
    if (ScopedApi.vkEnumerateInstanceVersion != nullptr)
    {
        (void)ScopedApi.vkEnumerateInstanceVersion(&ScopedInstanceVersion);
    }
    // Request the highest version the loader offers, capped at the newest one the
    // headers know: the device feature chain below selects Vulkan12/13 structs
    // when the physical device supports them, and the device version can never
    // exceed the instance one. With the 1.4 SDK and driver installed the instance
    // is created as Vulkan 1.4 and the 1.2/1.3 feature structs stay valid.
#if defined(VK_API_VERSION_1_4)
    constexpr std::uint32_t ScopedMaxApiVersion = VK_API_VERSION_1_4;
#else
    constexpr std::uint32_t ScopedMaxApiVersion = VK_API_VERSION_1_3;
#endif
    ScopedApplication.apiVersion = std::min(ScopedInstanceVersion, ScopedMaxApiVersion);
    MInstanceApiVersion = ScopedApplication.apiVersion;

    VkInstanceCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ScopedCreateInfo.pApplicationInfo = &ScopedApplication;
    ScopedCreateInfo.enabledExtensionCount = static_cast<std::uint32_t>(ScopedExtensions.size());
    ScopedCreateInfo.ppEnabledExtensionNames = ScopedExtensions.data();
    ScopedCreateInfo.enabledLayerCount = static_cast<std::uint32_t>(ScopedLayers.size());
    ScopedCreateInfo.ppEnabledLayerNames = ScopedLayers.data();

    if (ScopedApi.vkCreateInstance(&ScopedCreateInfo, nullptr, &MInstance) != VK_SUCCESS ||
        !ScopedApi.LoadInstance(MInstance))
    {
        LogError("vulkan", "instance creation failed");
        return false;
    }
    if (InDesc.enableValidation)
    {
        MCreateDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            ScopedApi.GetInstanceProcAddr(MInstance, "vkCreateDebugUtilsMessengerEXT"));
        MDestroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            ScopedApi.GetInstanceProcAddr(MInstance, "vkDestroyDebugUtilsMessengerEXT"));
        if (MCreateDebugMessenger != nullptr && MDestroyDebugMessenger != nullptr)
        {
            VkDebugUtilsMessengerCreateInfoEXT ScopedMessengerInfo{};
            ScopedMessengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            ScopedMessengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            ScopedMessengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            ScopedMessengerInfo.pfnUserCallback =
                [](VkDebugUtilsMessageSeverityFlagBitsEXT InSeverity, VkDebugUtilsMessageTypeFlagsEXT /*InTypes*/,
                   const VkDebugUtilsMessengerCallbackDataEXT* InData, void* /*InUser*/) -> VkBool32
            {
                const char* ScopedText = InData != nullptr && InData->pMessage != nullptr ? InData->pMessage : "";
                if (InSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                {
                    LogError("vulkan-validation", "{}", ScopedText);
                }
                else
                {
                    LogWarn("vulkan-validation", "{}", ScopedText);
                }
                return VK_FALSE;
            };
            if (MCreateDebugMessenger(MInstance, &ScopedMessengerInfo, nullptr, &MDebugMessenger) == VK_SUCCESS)
            {
                LogInfo("vulkan", "validation messenger active");
            }
            else
            {
                MDebugMessenger = VK_NULL_HANDLE;
            }
        }
    }
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(InDesc.nativeWindow), MInstance, nullptr, &MSurface))
    {
        LogError("vulkan", "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

bool VulkanRHI::PickPhysicalDevice()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    std::uint32_t ScopedCount = 0;
    ScopedApi.vkEnumeratePhysicalDevices(MInstance, &ScopedCount, nullptr);
    if (ScopedCount == 0)
    {
        LogError("vulkan", "no physical devices");
        return false;
    }
    std::vector<VkPhysicalDevice> ScopedDevices(ScopedCount);
    ScopedApi.vkEnumeratePhysicalDevices(MInstance, &ScopedCount, ScopedDevices.data());

    int ScopedBestScore = -1;
    for (VkPhysicalDevice ScopedCandidate : ScopedDevices)
    {
        VkPhysicalDeviceProperties ScopedProperties{};
        ScopedApi.vkGetPhysicalDeviceProperties(ScopedCandidate, &ScopedProperties);

        std::uint32_t ScopedFamilyCount = 0;
        ScopedApi.vkGetPhysicalDeviceQueueFamilyProperties(ScopedCandidate, &ScopedFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> ScopedFamilies(ScopedFamilyCount);
        ScopedApi.vkGetPhysicalDeviceQueueFamilyProperties(ScopedCandidate, &ScopedFamilyCount, ScopedFamilies.data());

        for (std::uint32_t ScopedFamily = 0; ScopedFamily < ScopedFamilyCount; ++ScopedFamily)
        {
            if ((ScopedFamilies[ScopedFamily].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
            {
                continue;
            }
            VkBool32 ScopedPresentSupport = VK_FALSE;
            ScopedApi.vkGetPhysicalDeviceSurfaceSupportKHR(ScopedCandidate, ScopedFamily, MSurface,
                                                           &ScopedPresentSupport);
            if (ScopedPresentSupport == VK_FALSE)
            {
                continue;
            }
            // Vulkan 1.4 is the minimum validated driver for the 0.4 alpha line.
            if (ScopedProperties.apiVersion < VK_API_VERSION_1_4)
            {
                LogWarn("vulkan", "device '{}' reports Vulkan {}.{}.{}; 1.4 is required",
                        ScopedProperties.deviceName, VK_API_VERSION_MAJOR(ScopedProperties.apiVersion),
                        VK_API_VERSION_MINOR(ScopedProperties.apiVersion),
                        VK_API_VERSION_PATCH(ScopedProperties.apiVersion));
                continue;
            }

            int ScopedScore = ScopedProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
            if (ScopedScore > ScopedBestScore)
            {
                ScopedBestScore = ScopedScore;
                MPhysical = ScopedCandidate;
                MQueueFamily = ScopedFamily;
                MInfo.apiVersion = ScopedProperties.apiVersion;
                MInfo.discrete = ScopedProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
                std::snprintf(MInfo.name, sizeof(MInfo.name), "%s", ScopedProperties.deviceName);
                MInfo.gpuVendor = ScopedProperties.vendorID == 0x10DE ? 1
                                : ScopedProperties.vendorID == 0x1002 || ScopedProperties.vendorID == 0x1022 ? 2
                                : ScopedProperties.vendorID == 0x8086 ? 3
                                : 0;
#if defined(_WIN32)
                MInfo.platform = 0;
#elif defined(__linux__)
                MInfo.platform = 1;
#elif defined(__APPLE__)
                MInfo.platform = 2;
#endif
            }
            break;
        }
    }

    if (MPhysical == VK_NULL_HANDLE)
    {
        LogError("vulkan", "no Vulkan 1.4 device with a graphics queue that can present to this surface");
        return false;
    }
    return true;
}

bool VulkanRHI::CreateDevice()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    const float ScopedPriority = 1.0f;
    VkDeviceQueueCreateInfo ScopedQueueInfo{};
    ScopedQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    ScopedQueueInfo.queueFamilyIndex = MQueueFamily;
    ScopedQueueInfo.queueCount = 1;
    ScopedQueueInfo.pQueuePriorities = &ScopedPriority;

    const char* ScopedDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // The indirect mesh path uses firstInstance as the draw-record index.
    VkPhysicalDeviceVulkan11Features ScopedVulkan11{};
    ScopedVulkan11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceVulkan12Features ScopedVulkan12{};
    ScopedVulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features ScopedVulkan13{};
    ScopedVulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    const std::uint32_t ScopedDeviceApiVersion = std::min(MInfo.apiVersion, MInstanceApiVersion);
    const bool ScopedVulkan12Available = ScopedDeviceApiVersion >= VK_API_VERSION_1_2;
    const bool ScopedVulkan13Available = ScopedDeviceApiVersion >= VK_API_VERSION_1_3;
    ScopedVulkan12.pNext = &ScopedVulkan11;
    ScopedVulkan13.pNext = ScopedVulkan12Available ? static_cast<void*>(&ScopedVulkan12) : &ScopedVulkan11;
    VkPhysicalDeviceFeatures2 ScopedAvailable{};
    ScopedAvailable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    ScopedAvailable.pNext = ScopedVulkan13Available
                                ? static_cast<void*>(&ScopedVulkan13)
                                : (ScopedVulkan12Available ? static_cast<void*>(&ScopedVulkan12)
                                                           : static_cast<void*>(&ScopedVulkan11));
    ScopedApi.vkGetPhysicalDeviceFeatures2(MPhysical, &ScopedAvailable);
    if (ScopedVulkan11.shaderDrawParameters == VK_FALSE)
    {
        LogError("vulkan", "device does not support shaderDrawParameters; the indirect mesh path cannot select "
                           "per-draw model matrices");
        return false;
    }
    ScopedVulkan11.shaderDrawParameters = VK_TRUE;

    MInfo.descriptorIndexing = ScopedVulkan12Available && ScopedVulkan12.descriptorIndexing == VK_TRUE;

    MInfo.drawIndirectCount = ScopedVulkan12Available && ScopedVulkan12.drawIndirectCount == VK_TRUE;
    MInfo.timelineSemaphores = ScopedVulkan12Available && ScopedVulkan12.timelineSemaphore == VK_TRUE;
    MInfo.synchronization2 = ScopedVulkan13Available && ScopedVulkan13.synchronization2 == VK_TRUE;
    ScopedVulkan12.descriptorIndexing = MInfo.descriptorIndexing ? VK_TRUE : VK_FALSE;
    ScopedVulkan12.drawIndirectCount = MInfo.drawIndirectCount ? VK_TRUE : VK_FALSE;
    ScopedVulkan12.timelineSemaphore = MInfo.timelineSemaphores ? VK_TRUE : VK_FALSE;
    ScopedVulkan13.synchronization2 = MInfo.synchronization2 ? VK_TRUE : VK_FALSE;

    // Texture compression: BC7 is core, ASTC LDR is optional. Enable the ASTC
    MDeviceFeatures = ScopedAvailable.features;
    MAstcLdrSupported = ScopedAvailable.features.textureCompressionASTC_LDR == VK_TRUE;
    MDeviceFeatures.textureCompressionASTC_LDR = MAstcLdrSupported ? VK_TRUE : VK_FALSE;
    MDeviceFeatures.samplerAnisotropy = ScopedAvailable.features.samplerAnisotropy;

    VkDeviceCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ScopedCreateInfo.pNext = ScopedVulkan13Available
                                 ? static_cast<void*>(&ScopedVulkan13)
                                 : (ScopedVulkan12Available ? static_cast<void*>(&ScopedVulkan12)
                                                            : static_cast<void*>(&ScopedVulkan11));
    ScopedCreateInfo.queueCreateInfoCount = 1;
    ScopedCreateInfo.pQueueCreateInfos = &ScopedQueueInfo;
    ScopedCreateInfo.enabledExtensionCount = 1;
    ScopedCreateInfo.ppEnabledExtensionNames = ScopedDeviceExtensions;
    ScopedCreateInfo.pEnabledFeatures = &MDeviceFeatures;

    if (ScopedApi.vkCreateDevice(MPhysical, &ScopedCreateInfo, nullptr, &MDevice) != VK_SUCCESS ||
        !ScopedApi.LoadDevice(MDevice))
    {
        LogError("vulkan", "vkCreateDevice failed");
        return false;
    }
    ScopedApi.vkGetDeviceQueue(MDevice, MQueueFamily, 0, &MQueue);
    MInfo.drawIndirectCount = MInfo.drawIndirectCount && ScopedApi.vkCmdDrawIndexedIndirectCount != nullptr;
    return true;
}

VkSurfaceFormatKHR VulkanRHI::ChooseSurfaceFormat() const
{
    Vk::Api& ScopedApi = Vk::GetApi();
    std::uint32_t ScopedCount = 0;
    ScopedApi.vkGetPhysicalDeviceSurfaceFormatsKHR(MPhysical, MSurface, &ScopedCount, nullptr);
    std::vector<VkSurfaceFormatKHR> ScopedFormats(ScopedCount);
    if (ScopedCount > 0)
    {
        ScopedApi.vkGetPhysicalDeviceSurfaceFormatsKHR(MPhysical, MSurface, &ScopedCount, ScopedFormats.data());
    }
    for (const VkSurfaceFormatKHR& ScopedFormat : ScopedFormats)
    {
        if (ScopedFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            ScopedFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return ScopedFormat;
        }
    }
    if (!ScopedFormats.empty())
    {
        return ScopedFormats.front();
    }
    return VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
}

VkPresentModeKHR VulkanRHI::ChoosePresentMode(bool InVsync) const
{
    Vk::Api& ScopedApi = Vk::GetApi();
    std::uint32_t ScopedCount = 0;
    ScopedApi.vkGetPhysicalDeviceSurfacePresentModesKHR(MPhysical, MSurface, &ScopedCount, nullptr);
    std::vector<VkPresentModeKHR> ScopedModes(ScopedCount);
    if (ScopedCount > 0)
    {
        ScopedApi.vkGetPhysicalDeviceSurfacePresentModesKHR(MPhysical, MSurface, &ScopedCount, ScopedModes.data());
    }

    if (InVsync)
    {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    for (VkPresentModeKHR ScopedMode : ScopedModes)
    {
        if (ScopedMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return ScopedMode;
        }
    }
    for (VkPresentModeKHR ScopedMode : ScopedModes)
    {
        if (ScopedMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            return ScopedMode;
        }
    }
    // FIFO is the only mode the specification requires.
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRHI::ChooseExtent(Extent2D InRequested) const
{
    VkSurfaceCapabilitiesKHR ScopedCapabilities{};
    Vk::GetApi().vkGetPhysicalDeviceSurfaceCapabilitiesKHR(MPhysical, MSurface, &ScopedCapabilities);

    if (ScopedCapabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
    {
        return ScopedCapabilities.currentExtent;
    }
    VkExtent2D ScopedExtent{InRequested.width, InRequested.height};
    ScopedExtent.width = std::clamp(ScopedExtent.width, ScopedCapabilities.minImageExtent.width,
                                    ScopedCapabilities.maxImageExtent.width);
    ScopedExtent.height = std::clamp(ScopedExtent.height, ScopedCapabilities.minImageExtent.height,
                                     ScopedCapabilities.maxImageExtent.height);
    return ScopedExtent;
}

bool VulkanRHI::CreateBuffer(VkDeviceSize InSize, VkBufferUsageFlags InUsage, VkMemoryPropertyFlags InProperties,
                             VkBuffer* InBuffer, VkDeviceMemory* InMemory) const
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkBufferCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ScopedCreateInfo.size = InSize;
    ScopedCreateInfo.usage = InUsage;
    ScopedCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (ScopedApi.vkCreateBuffer(MDevice, &ScopedCreateInfo, nullptr, InBuffer) != VK_SUCCESS)
    {
        return false;
    }

    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetBufferMemoryRequirements(MDevice, *InBuffer, &ScopedRequirements);

    VkMemoryAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocateInfo.allocationSize = ScopedRequirements.size;
    ScopedAllocateInfo.memoryTypeIndex =
        FindMemoryType(MMemoryProperties, ScopedRequirements.memoryTypeBits, InProperties);
    if (ScopedAllocateInfo.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocateInfo, nullptr, InMemory) != VK_SUCCESS)
    {
        LogError("vulkan", "buffer memory allocation failed ({} bytes)", InSize);
        return false;
    }
    return ScopedApi.vkBindBufferMemory(MDevice, *InBuffer, *InMemory, 0) == VK_SUCCESS;
}

bool VulkanRHI::EnsureCaptureStaging(VkDeviceSize InBytes, VkBuffer* OutBuffer, VkDeviceMemory* OutMemory)
{
    if (MCaptureStaging != VK_NULL_HANDLE && MCaptureStagingBytes >= InBytes)
    {
        *OutBuffer = MCaptureStaging;
        *OutMemory = MCaptureStagingMemory;
        return true;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    // Grow to the next power of two so a slightly larger capture does not
    // reallocate again. The old buffer is idle: the queue waits before returning.
    VkDeviceSize ScopedCapacity = 4096;
    while (ScopedCapacity < InBytes)
    {
        ScopedCapacity *= 2;
    }
    VkBuffer ScopedBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ScopedMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(ScopedCapacity, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ScopedBuffer,
                      &ScopedMemory))
    {
        return false;
    }
    void* ScopedMapped = nullptr;
    if (ScopedApi.vkMapMemory(MDevice, ScopedMemory, 0, ScopedCapacity, 0, &ScopedMapped) != VK_SUCCESS)
    {
        ScopedApi.vkDestroyBuffer(MDevice, ScopedBuffer, nullptr);
        ScopedApi.vkFreeMemory(MDevice, ScopedMemory, nullptr);
        return false;
    }
    if (MCaptureStaging != VK_NULL_HANDLE)
    {
        ScopedApi.vkUnmapMemory(MDevice, MCaptureStagingMemory);
        ScopedApi.vkDestroyBuffer(MDevice, MCaptureStaging, nullptr);
        ScopedApi.vkFreeMemory(MDevice, MCaptureStagingMemory, nullptr);
    }
    MCaptureStaging = ScopedBuffer;
    MCaptureStagingMemory = ScopedMemory;
    MCaptureStagingMapped = ScopedMapped;
    MCaptureStagingBytes = ScopedCapacity;
    *OutBuffer = MCaptureStaging;
    *OutMemory = MCaptureStagingMemory;
    return true;
}

VkCommandBuffer VulkanRHI::BeginOneShot() const
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkCommandBufferAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ScopedAllocateInfo.commandPool = MCommandPool;
    ScopedAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ScopedAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer ScopedCommand = VK_NULL_HANDLE;
    if (ScopedApi.vkAllocateCommandBuffers(MDevice, &ScopedAllocateInfo, &ScopedCommand) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ScopedBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ScopedApi.vkBeginCommandBuffer(ScopedCommand, &ScopedBeginInfo);
    return ScopedCommand;
}

bool VulkanRHI::WaitUploadFence() const
{
    if (MUploadFence == VK_NULL_HANDLE)
    {
        return false;
    }
    return Vk::GetApi().vkWaitForFences(MDevice, 1, &MUploadFence, VK_TRUE,
                                        std::numeric_limits<std::uint64_t>::max()) == VK_SUCCESS;
}

bool VulkanRHI::SubmitUpload(VkCommandBuffer InCommand)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MUploadFence == VK_NULL_HANDLE || InCommand == VK_NULL_HANDLE)
    {
        return false;
    }
    if (ScopedApi.vkEndCommandBuffer(InCommand) != VK_SUCCESS)
    {
        return false;
    }
    (void)ScopedApi.vkResetFences(MDevice, 1, &MUploadFence);
    VkSubmitInfo ScopedSubmitInfo{};
    ScopedSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    ScopedSubmitInfo.commandBufferCount = 1;
    ScopedSubmitInfo.pCommandBuffers = &InCommand;
    return ScopedApi.vkQueueSubmit(MQueue, 1, &ScopedSubmitInfo, MUploadFence) == VK_SUCCESS;
}

void VulkanRHI::EndOneShot(VkCommandBuffer InCommand) const
{
    Vk::Api& ScopedApi = Vk::GetApi();
    (void)ScopedApi.vkEndCommandBuffer(InCommand);

    VkSubmitInfo ScopedSubmitInfo{};
    ScopedSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    ScopedSubmitInfo.commandBufferCount = 1;
    ScopedSubmitInfo.pCommandBuffers = &InCommand;
    ScopedApi.vkQueueSubmit(MQueue, 1, &ScopedSubmitInfo, VK_NULL_HANDLE);
    ScopedApi.vkQueueWaitIdle(MQueue);
    ScopedApi.vkFreeCommandBuffers(MDevice, MCommandPool, 1, &InCommand);
}

void VulkanRHI::TransitionImage(VkCommandBuffer InCommand, VkImage InImage, VkImageLayout InFrom, VkImageLayout InTo,
                                std::uint32_t InLayers, std::uint32_t InMipLevels) const
{
    VkImageMemoryBarrier ScopedBarrier{};
    ScopedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedBarrier.oldLayout = InFrom;
    ScopedBarrier.newLayout = InTo;
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.image = InImage;
    ScopedBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedBarrier.subresourceRange.levelCount = InMipLevels;
    ScopedBarrier.subresourceRange.layerCount = InLayers;

    VkPipelineStageFlags ScopedSourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags ScopedDestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (InFrom == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && InTo == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        ScopedSourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        ScopedDestinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        ScopedBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (InFrom == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && InTo == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        ScopedSourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        ScopedBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ScopedBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else
    {
        ScopedBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }

    Vk::GetApi().vkCmdPipelineBarrier(InCommand, ScopedSourceStage, ScopedDestinationStage, 0, 0, nullptr, 0, nullptr,
                                      1, &ScopedBarrier);
}

void VulkanRHI::InitImageLayout(VkImage InImage, VkImageLayout InLayout, std::uint32_t InLayers)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    const VkCommandBuffer ScopedCommand = BeginOneShot();
    VkImageMemoryBarrier ScopedBarrier{};
    ScopedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ScopedBarrier.newLayout = InLayout;
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.image = InImage;
    ScopedBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, InLayers};
    ScopedBarrier.srcAccessMask = 0;
    ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    ScopedApi.vkCmdPipelineBarrier(ScopedCommand, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                   0, nullptr, 0, nullptr, 1, &ScopedBarrier);
    EndOneShot(ScopedCommand);
}

bool VulkanRHI::CreateSwapchain(Extent2D InSize)
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkSurfaceCapabilitiesKHR ScopedCapabilities{};
    ScopedApi.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(MPhysical, MSurface, &ScopedCapabilities);

    const VkSurfaceFormatKHR ScopedFormat = ChooseSurfaceFormat();
    const VkPresentModeKHR ScopedPresentMode = ChoosePresentMode(MDesc.vsync);
    const VkExtent2D ScopedExtent = ChooseExtent(InSize);

    std::uint32_t ScopedImageCount = ScopedCapabilities.minImageCount + 1;
    if (ScopedCapabilities.maxImageCount > 0 && ScopedImageCount > ScopedCapabilities.maxImageCount)
    {
        ScopedImageCount = ScopedCapabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ScopedCreateInfo.surface = MSurface;
    ScopedCreateInfo.minImageCount = ScopedImageCount;
    ScopedCreateInfo.imageFormat = ScopedFormat.format;
    ScopedCreateInfo.imageColorSpace = ScopedFormat.colorSpace;
    ScopedCreateInfo.imageExtent = ScopedExtent;
    ScopedCreateInfo.imageArrayLayers = 1;
    ScopedCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ScopedCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ScopedCreateInfo.preTransform = ScopedCapabilities.currentTransform;
    ScopedCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ScopedCreateInfo.presentMode = ScopedPresentMode;
    ScopedCreateInfo.clipped = VK_TRUE;

    if (ScopedApi.vkCreateSwapchainKHR(MDevice, &ScopedCreateInfo, nullptr, &MSwapchain) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateSwapchainKHR failed");
        return false;
    }

    MFormat = ScopedFormat.format;
    MExtent = ScopedExtent;

    std::uint32_t ScopedSwapImageCount = 0;
    ScopedApi.vkGetSwapchainImagesKHR(MDevice, MSwapchain, &ScopedSwapImageCount, nullptr);
    MImages.resize(ScopedSwapImageCount);
    ScopedApi.vkGetSwapchainImagesKHR(MDevice, MSwapchain, &ScopedSwapImageCount, MImages.data());

    MViews.resize(MImages.size());
    for (std::size_t ScopedI = 0; ScopedI < MImages.size(); ++ScopedI)
    {
        VkImageViewCreateInfo ScopedViewInfo{};
        ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ScopedViewInfo.image = MImages[ScopedI];
        ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ScopedViewInfo.format = MFormat;
        ScopedViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ScopedViewInfo.subresourceRange.levelCount = 1;
        ScopedViewInfo.subresourceRange.layerCount = 1;
        if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &MViews[ScopedI]) != VK_SUCCESS)
        {
            LogError("vulkan", "vkCreateImageView failed");
            return false;
        }
    }

    LogInfo("vulkan", "swapchain {}x{}, {} images", MExtent.width, MExtent.height, MImages.size());
    return true;
}

bool VulkanRHI::CreateRenderPass()
{
    VkAttachmentDescription ScopedColor{};
    ScopedColor.format = MFormat;
    ScopedColor.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ScopedColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ScopedColor.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ScopedColor.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ScopedColorRef{};
    ScopedColorRef.attachment = 0;
    ScopedColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription ScopedDepth{};
    ScopedDepth.format = VK_FORMAT_D32_SFLOAT;
    ScopedDepth.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ScopedDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ScopedDepth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ScopedDepth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ScopedDepth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    ScopedDepth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference ScopedDepthRef{};
    ScopedDepthRef.attachment = 1;
    ScopedDepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription ScopedSubpass{};
    ScopedSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    ScopedSubpass.colorAttachmentCount = 1;
    ScopedSubpass.pColorAttachments = &ScopedColorRef;
    ScopedSubpass.pDepthStencilAttachment = &ScopedDepthRef;

    VkSubpassDependency ScopedDependency{};
    ScopedDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    ScopedDependency.dstSubpass = 0;
    ScopedDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    ScopedDependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    ScopedDependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ScopedCreateInfo.attachmentCount = 2;
    const VkAttachmentDescription ScopedAttachments[2] = {ScopedColor, ScopedDepth};
    ScopedCreateInfo.pAttachments = ScopedAttachments;
    ScopedCreateInfo.subpassCount = 1;
    ScopedCreateInfo.pSubpasses = &ScopedSubpass;
    ScopedCreateInfo.dependencyCount = 1;
    ScopedCreateInfo.pDependencies = &ScopedDependency;

    if (Vk::GetApi().vkCreateRenderPass(MDevice, &ScopedCreateInfo, nullptr, &MRenderPass) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateRenderPass failed");
        return false;
    }

    VkAttachmentDescription ScopedOverlayColor = ScopedColor;
    ScopedOverlayColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ScopedOverlayColor.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ScopedOverlayColor.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentDescription ScopedOverlayDepth = ScopedDepth;
    ScopedOverlayDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ScopedOverlayDepth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    ScopedOverlayDepth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    const VkAttachmentDescription ScopedOverlayAttachments[2] = {ScopedOverlayColor, ScopedOverlayDepth};
    VkRenderPassCreateInfo ScopedOverlayInfo = ScopedCreateInfo;
    ScopedOverlayInfo.pAttachments = ScopedOverlayAttachments;
    if (Vk::GetApi().vkCreateRenderPass(MDevice, &ScopedOverlayInfo, nullptr, &MHudOverlayPass) != VK_SUCCESS)
    {
        LogError("vulkan", "HUD overlay render pass failed");
        return false;
    }
    return true;
}

bool VulkanRHI::CreateDepthImage(Extent2D InExtent, VkImage* OutImage, VkDeviceMemory* OutMemory,
                                 VkImageView* OutView)
{
    if (OutImage == nullptr || OutMemory == nullptr || OutView == nullptr || InExtent.width == 0 ||
        InExtent.height == 0)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkImageCreateInfo ScopedInfo{};
    ScopedInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedInfo.format = VK_FORMAT_D32_SFLOAT;
    ScopedInfo.extent = {InExtent.width, InExtent.height, 1};
    ScopedInfo.mipLevels = 1;
    ScopedInfo.arrayLayers = 1;
    ScopedInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ScopedInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedInfo, nullptr, OutImage) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, *OutImage, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex =
        FindMemoryType(MMemoryProperties, ScopedRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ScopedAllocate.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, OutMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, *OutImage, *OutMemory, 0) != VK_SUCCESS)
    {
        ScopedApi.vkDestroyImage(MDevice, *OutImage, nullptr);
        *OutImage = VK_NULL_HANDLE;
        if (*OutMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, *OutMemory, nullptr);
            *OutMemory = VK_NULL_HANDLE;
        }
        return false;
    }
    VkImageViewCreateInfo ScopedView{};
    ScopedView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedView.image = *OutImage;
    ScopedView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedView.format = VK_FORMAT_D32_SFLOAT;
    ScopedView.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedView, nullptr, OutView) != VK_SUCCESS)
    {
        ScopedApi.vkDestroyImage(MDevice, *OutImage, nullptr);
        ScopedApi.vkFreeMemory(MDevice, *OutMemory, nullptr);
        *OutImage = VK_NULL_HANDLE;
        *OutMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanRHI::DestroyDepthImage(VkImage* InImage, VkDeviceMemory* InMemory, VkImageView* InView) noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (InView != nullptr && *InView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, *InView, nullptr);
        *InView = VK_NULL_HANDLE;
    }
    if (InImage != nullptr && *InImage != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, *InImage, nullptr);
        *InImage = VK_NULL_HANDLE;
    }
    if (InMemory != nullptr && *InMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, *InMemory, nullptr);
        *InMemory = VK_NULL_HANDLE;
    }
}

bool VulkanRHI::CreateFramebuffers()
{
    // A minimized or occluded window reports a zero extent. Creating images of
    // size zero is invalid, so skip the whole pass; swapchain recreation calls
    // this again once the extent returns.
    if (MExtent.width == 0 || MExtent.height == 0 || MViews.empty())
    {
        return true;
    }
    MFramebuffers.resize(MViews.size());
    MDrawDepthImages.assign(MViews.size(), VK_NULL_HANDLE);
    MDrawDepthMemories.assign(MViews.size(), VK_NULL_HANDLE);
    MDrawDepthViews.assign(MViews.size(), VK_NULL_HANDLE);
    for (std::size_t ScopedI = 0; ScopedI < MViews.size(); ++ScopedI)
    {
        if (!CreateDepthImage(Extent2D{MExtent.width, MExtent.height}, &MDrawDepthImages[ScopedI],
                              &MDrawDepthMemories[ScopedI], &MDrawDepthViews[ScopedI]))
        {
            LogError("vulkan", "main depth image failed");
            return false;
        }
        const VkImageView ScopedAttachments[2] = {MViews[ScopedI], MDrawDepthViews[ScopedI]};
        VkFramebufferCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ScopedCreateInfo.renderPass = MRenderPass;
        ScopedCreateInfo.attachmentCount = 2;
        ScopedCreateInfo.pAttachments = ScopedAttachments;
        ScopedCreateInfo.width = MExtent.width;
        ScopedCreateInfo.height = MExtent.height;
        ScopedCreateInfo.layers = 1;
        if (Vk::GetApi().vkCreateFramebuffer(MDevice, &ScopedCreateInfo, nullptr, &MFramebuffers[ScopedI]) !=
            VK_SUCCESS)
        {
            LogError("vulkan", "vkCreateFramebuffer failed");
            return false;
        }
    }
    MHudOverlayFramebuffers.resize(MViews.size());
    for (std::size_t ScopedI = 0; ScopedI < MViews.size(); ++ScopedI)
    {
        const VkImageView ScopedAttachments[2] = {MViews[ScopedI], MDrawDepthViews[ScopedI]};
        VkFramebufferCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ScopedCreateInfo.renderPass = MHudOverlayPass;
        ScopedCreateInfo.attachmentCount = 2;
        ScopedCreateInfo.pAttachments = ScopedAttachments;
        ScopedCreateInfo.width = MExtent.width;
        ScopedCreateInfo.height = MExtent.height;
        ScopedCreateInfo.layers = 1;
        if (Vk::GetApi().vkCreateFramebuffer(MDevice, &ScopedCreateInfo, nullptr,
                                             &MHudOverlayFramebuffers[ScopedI]) != VK_SUCCESS)
        {
            LogError("vulkan", "HUD overlay framebuffer failed");
            return false;
        }
    }
    return true;
}

bool VulkanRHI::CreateDepthPrepassFramebuffers()
{
    DestroyDepthPrepassFramebuffers();
    MDepthPrepassFramebuffers.resize(MDrawDepthViews.size());
    for (std::size_t ScopedI = 0; ScopedI < MDrawDepthViews.size(); ++ScopedI)
    {
        VkFramebufferCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ScopedCreateInfo.renderPass = MDepthPrepassRenderPass;
        ScopedCreateInfo.attachmentCount = 1;
        ScopedCreateInfo.pAttachments = &MDrawDepthViews[ScopedI];
        ScopedCreateInfo.width = MExtent.width;
        ScopedCreateInfo.height = MExtent.height;
        ScopedCreateInfo.layers = 1;
        if (Vk::GetApi().vkCreateFramebuffer(MDevice, &ScopedCreateInfo, nullptr, &MDepthPrepassFramebuffers[ScopedI]) !=
            VK_SUCCESS)
        {
            LogError("vulkan", "depth prepass framebuffer failed");
            return false;
        }
    }
    return true;
}

void VulkanRHI::DestroyDepthPrepassFramebuffers() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    for (VkFramebuffer ScopedFramebuffer : MDepthPrepassFramebuffers)
    {
        ScopedApi.vkDestroyFramebuffer(MDevice, ScopedFramebuffer, nullptr);
    }
    MDepthPrepassFramebuffers.clear();
}

bool VulkanRHI::CreateHiZResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    MHiZExtent = {std::max(MExtent.width / 16u, 1u), std::max(MExtent.height / 16u, 1u)};
    MHiZMipCount = 1;
    for (std::uint32_t ScopedDim = std::max(MHiZExtent.width, MHiZExtent.height);
         ScopedDim > 1u && MHiZMipCount < 5u; ScopedDim /= 2u)
    {
        ++MHiZMipCount;
    }
    VkImageCreateInfo ScopedImage{};
    ScopedImage.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImage.imageType = VK_IMAGE_TYPE_2D;
    ScopedImage.format = VK_FORMAT_R32_SFLOAT;
    ScopedImage.extent = {MHiZExtent.width, MHiZExtent.height, 1};
    ScopedImage.mipLevels = MHiZMipCount;
    ScopedImage.arrayLayers = 1;
    ScopedImage.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImage.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedImage.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ScopedImage.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ScopedImage.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedImage, nullptr, &MHiZImage) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MHiZImage, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex =
        FindMemoryType(MMemoryProperties, ScopedRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ScopedAllocate.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MHiZMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MHiZImage, MHiZMemory, 0) != VK_SUCCESS)
    {
        return false;
    }
    VkImageViewCreateInfo ScopedView{};
    ScopedView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedView.image = MHiZImage;
    ScopedView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedView.format = VK_FORMAT_R32_SFLOAT;
    ScopedView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, MHiZMipCount, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedView, nullptr, &MHiZView) != VK_SUCCESS)
    {
        return false;
    }
    MHiZStorageViews.resize(MHiZMipCount, VK_NULL_HANDLE);
    for (std::uint32_t ScopedMip = 0; ScopedMip < MHiZMipCount; ++ScopedMip)
    {
        ScopedView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, ScopedMip, 1, 0, 1};
        if (ScopedApi.vkCreateImageView(MDevice, &ScopedView, nullptr, &MHiZStorageViews[ScopedMip]) !=
            VK_SUCCESS)
        {
            return false;
        }
    }
    VkSamplerCreateInfo ScopedSampler{};
    ScopedSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSampler.magFilter = VK_FILTER_NEAREST;
    ScopedSampler.minFilter = VK_FILTER_NEAREST;
    ScopedSampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ScopedSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.maxLod = static_cast<float>(MHiZMipCount - 1u);
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSampler, nullptr, &MHiZSampler) != VK_SUCCESS)
    {
        return false;
    }
    std::array<VkDescriptorSetLayoutBinding, 2> ScopedBindings{};
    ScopedBindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    ScopedBindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo ScopedSetInfo{};
    ScopedSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetInfo.bindingCount = 2;
    ScopedSetInfo.pBindings = ScopedBindings.data();
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetInfo, nullptr, &MHiZSetLayout) != VK_SUCCESS)
    {
        return false;
    }
    std::array<VkDescriptorPoolSize, 2> ScopedPoolSizes{};
    ScopedPoolSizes[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MHiZMipCount};
    ScopedPoolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MHiZMipCount};
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = MHiZMipCount;
    ScopedPoolInfo.poolSizeCount = 2;
    ScopedPoolInfo.pPoolSizes = ScopedPoolSizes.data();
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MHiZPool) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorSetAllocateInfo ScopedSetAllocate{};
    ScopedSetAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedSetAllocate.descriptorPool = MHiZPool;
    ScopedSetAllocate.descriptorSetCount = 1;
    ScopedSetAllocate.pSetLayouts = &MHiZSetLayout;
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedSetAllocate, &MHiZSet) != VK_SUCCESS)
    {
        return false;
    }
    MHiZMipSets.resize(MHiZMipCount > 0 ? MHiZMipCount - 1u : 0u, VK_NULL_HANDLE);
    for (VkDescriptorSet& ScopedMipSet : MHiZMipSets)
    {
        if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedSetAllocate, &ScopedMipSet) != VK_SUCCESS)
        {
            return false;
        }
    }
    VkPushConstantRange ScopedRange{};
    ScopedRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedRange.size = sizeof(HiZPushConstants);
    VkPipelineLayoutCreateInfo ScopedLayout{};
    ScopedLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayout.setLayoutCount = 1;
    ScopedLayout.pSetLayouts = &MHiZSetLayout;
    ScopedLayout.pushConstantRangeCount = 1;
    ScopedLayout.pPushConstantRanges = &ScopedRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayout, nullptr, &MHiZLayout) != VK_SUCCESS)
    {
        return false;
    }
    VkShaderModuleCreateInfo ScopedModuleInfo{};
    ScopedModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ScopedModuleInfo.codeSize = Shaders::hiz_tile.size() * sizeof(std::uint32_t);
    ScopedModuleInfo.pCode = Shaders::hiz_tile.data();
    VkShaderModule ScopedModule = VK_NULL_HANDLE;
    if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedModuleInfo, nullptr, &ScopedModule) != VK_SUCCESS)
    {
        return false;
    }
    VkComputePipelineCreateInfo ScopedPipeline{};
    ScopedPipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ScopedPipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedPipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedPipeline.stage.module = ScopedModule;
    ScopedPipeline.stage.pName = "main";
    ScopedPipeline.layout = MHiZLayout;
    const VkResult ScopedResult =
        ScopedApi.vkCreateComputePipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipeline, nullptr, &MHiZPipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedModule, nullptr);
    if (ScopedResult != VK_SUCCESS)
    {
        return false;
    }
    MInfo.hiZ = MDesc.enableHiZ;
    if (MHiZMipCount > 1)
    {
        ScopedModuleInfo.codeSize = Shaders::hiz_mip.size() * sizeof(std::uint32_t);
        ScopedModuleInfo.pCode = Shaders::hiz_mip.data();
        if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedModuleInfo, nullptr, &ScopedModule) != VK_SUCCESS)
        {
            return false;
        }
        ScopedPipeline.stage.module = ScopedModule;
        const VkResult ScopedMipResult =
            ScopedApi.vkCreateComputePipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipeline, nullptr, &MHiZMipPipeline);
        ScopedApi.vkDestroyShaderModule(MDevice, ScopedModule, nullptr);
        if (ScopedMipResult != VK_SUCCESS)
        {
            return false;
        }
    }
    return true;
}

void VulkanRHI::DestroyHiZResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MHiZPipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MHiZPipeline, nullptr);
        MHiZPipeline = VK_NULL_HANDLE;
    }
    if (MHiZMipPipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MHiZMipPipeline, nullptr);
        MHiZMipPipeline = VK_NULL_HANDLE;
    }
    if (MHiZLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipelineLayout(MDevice, MHiZLayout, nullptr);
        MHiZLayout = VK_NULL_HANDLE;
    }
    if (MHiZPool != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorPool(MDevice, MHiZPool, nullptr);
        MHiZPool = VK_NULL_HANDLE;
    }
    if (MHiZSetLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MHiZSetLayout, nullptr);
        MHiZSetLayout = VK_NULL_HANDLE;
    }
    if (MHiZSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MHiZSampler, nullptr);
        MHiZSampler = VK_NULL_HANDLE;
    }
    if (MHiZView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MHiZView, nullptr);
        MHiZView = VK_NULL_HANDLE;
    }
    for (VkImageView& ScopedView : MHiZStorageViews)
    {
        if (ScopedView != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImageView(MDevice, ScopedView, nullptr);
            ScopedView = VK_NULL_HANDLE;
        }
    }
    MHiZStorageViews.clear();
    MHiZMipSets.clear();
    if (MHiZImage != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MHiZImage, nullptr);
        MHiZImage = VK_NULL_HANDLE;
    }
    if (MHiZMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MHiZMemory, nullptr);
        MHiZMemory = VK_NULL_HANDLE;
    }
    MHiZSet = VK_NULL_HANDLE;
    MHiZExtent = {};
}

void VulkanRHI::RecordHiZ(VkCommandBuffer InCommandBuffer)
{
    if (!MHiZReady || MHiZPipeline == VK_NULL_HANDLE || MHiZView == VK_NULL_HANDLE ||
        MImageIndex >= MDrawDepthViews.size())
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorImageInfo ScopedDepth{};
    ScopedDepth.sampler = MDepthSampler;
    ScopedDepth.imageView = MDrawDepthViews[MImageIndex];
    ScopedDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo ScopedTile{};
    ScopedTile.imageView = MHiZStorageViews.empty() ? VK_NULL_HANDLE : MHiZStorageViews[0];
    ScopedTile.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    std::array<VkWriteDescriptorSet, 2> ScopedWrites{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        ScopedWrites[ScopedIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[ScopedIndex].dstSet = MHiZSet;
        ScopedWrites[ScopedIndex].dstBinding = ScopedIndex;
        ScopedWrites[ScopedIndex].descriptorCount = 1;
    }
    ScopedWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrites[0].pImageInfo = &ScopedDepth;
    ScopedWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedWrites[1].pImageInfo = &ScopedTile;
    ScopedApi.vkUpdateDescriptorSets(MDevice, 2, ScopedWrites.data(), 0, nullptr);
    TransitionImage(InCommandBuffer, MHiZImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1,
                    MHiZMipCount);
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MHiZPipeline);
    ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MHiZLayout, 0, 1, &MHiZSet,
                                      0, nullptr);
    HiZPushConstants ScopedPush{MExtent.width, MExtent.height, 16, 16};
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MHiZLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 sizeof(ScopedPush), &ScopedPush);
    ScopedApi.vkCmdDispatch(InCommandBuffer, (MHiZExtent.width + 7u) / 8u, (MHiZExtent.height + 7u) / 8u, 1);
    MStats.hizTiles = MHiZExtent.width * MHiZExtent.height;
    MStats.hizMips = MHiZMipCount;
    VkMemoryBarrier ScopedBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_ACCESS_SHADER_READ_BIT};
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ScopedBarrier, 0, nullptr, 0,
                                   nullptr);
    for (std::uint32_t ScopedMip = 1; ScopedMip < MHiZMipCount && ScopedMip - 1 < MHiZMipSets.size(); ++ScopedMip)
    {
        VkDescriptorImageInfo ScopedSource{};
        ScopedSource.sampler = MHiZSampler;
        ScopedSource.imageView = MHiZStorageViews[ScopedMip - 1];
        ScopedSource.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo ScopedDest{};
        ScopedDest.imageView = MHiZStorageViews[ScopedMip];
        ScopedDest.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        std::array<VkWriteDescriptorSet, 2> ScopedMipWrites{};
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
        {
            ScopedMipWrites[ScopedIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ScopedMipWrites[ScopedIndex].dstSet = MHiZMipSets[ScopedMip - 1];
            ScopedMipWrites[ScopedIndex].dstBinding = ScopedIndex;
            ScopedMipWrites[ScopedIndex].descriptorCount = 1;
        }
        ScopedMipWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedMipWrites[0].pImageInfo = &ScopedSource;
        ScopedMipWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        ScopedMipWrites[1].pImageInfo = &ScopedDest;
        ScopedApi.vkUpdateDescriptorSets(MDevice, 2, ScopedMipWrites.data(), 0, nullptr);
        ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MHiZMipPipeline);
        ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MHiZLayout, 0, 1,
                                          &MHiZMipSets[ScopedMip - 1], 0, nullptr);
        const std::uint32_t ScopedSourceWidth = std::max(MHiZExtent.width >> (ScopedMip - 1u), 1u);
        const std::uint32_t ScopedSourceHeight = std::max(MHiZExtent.height >> (ScopedMip - 1u), 1u);
        const std::uint32_t ScopedDestWidth = std::max(MHiZExtent.width >> ScopedMip, 1u);
        const std::uint32_t ScopedDestHeight = std::max(MHiZExtent.height >> ScopedMip, 1u);
        HiZPushConstants ScopedMipPush{ScopedSourceWidth, ScopedSourceHeight, ScopedDestWidth, ScopedDestHeight};
        ScopedApi.vkCmdPushConstants(InCommandBuffer, MHiZLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                     sizeof(ScopedMipPush), &ScopedMipPush);
        ScopedApi.vkCmdDispatch(InCommandBuffer, (ScopedDestWidth + 7u) / 8u, (ScopedDestHeight + 7u) / 8u, 1);
        ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ScopedBarrier, 0, nullptr, 0,
                                       nullptr);
    }
    TransitionImage(InCommandBuffer, MHiZImage, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, MHiZMipCount);
}

bool VulkanRHI::CreateDepthPrepassResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkSamplerCreateInfo ScopedSampler{};
    ScopedSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSampler.magFilter = VK_FILTER_NEAREST;
    ScopedSampler.minFilter = VK_FILTER_NEAREST;
    ScopedSampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ScopedSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.maxLod = 0.0f;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSampler, nullptr, &MDepthSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "depth sampler failed");
        return false;
    }

    VkAttachmentDescription ScopedDepth{};
    ScopedDepth.format = VK_FORMAT_D32_SFLOAT;
    ScopedDepth.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ScopedDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ScopedDepth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ScopedDepth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ScopedDepth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ScopedDepth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference ScopedDepthRef{};
    ScopedDepthRef.attachment = 0;
    ScopedDepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkSubpassDescription ScopedSubpass{};
    ScopedSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    ScopedSubpass.pDepthStencilAttachment = &ScopedDepthRef;
    VkSubpassDependency ScopedDependency{};
    ScopedDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    ScopedDependency.dstSubpass = 0;
    ScopedDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    ScopedDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    ScopedDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo ScopedPassInfo{};
    ScopedPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ScopedPassInfo.attachmentCount = 1;
    ScopedPassInfo.pAttachments = &ScopedDepth;
    ScopedPassInfo.subpassCount = 1;
    ScopedPassInfo.pSubpasses = &ScopedSubpass;
    ScopedPassInfo.dependencyCount = 1;
    ScopedPassInfo.pDependencies = &ScopedDependency;
    if (ScopedApi.vkCreateRenderPass(MDevice, &ScopedPassInfo, nullptr, &MDepthPrepassRenderPass) != VK_SUCCESS)
    {
        LogError("vulkan", "depth prepass render pass failed");
        return false;
    }
    if (!CreateDepthPrepassFramebuffers())
    {
        return false;
    }

    const auto ScopedMakeModule = [&ScopedApi, this](const auto& InBlob)
    {
        VkShaderModuleCreateInfo ScopedInfo{};
        ScopedInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ScopedInfo.codeSize = InBlob.size() * sizeof(std::uint32_t);
        ScopedInfo.pCode = InBlob.data();
        VkShaderModule ScopedModule = VK_NULL_HANDLE;
        if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedInfo, nullptr, &ScopedModule) != VK_SUCCESS)
        {
            return VkShaderModule{VK_NULL_HANDLE};
        }
        return ScopedModule;
    };
    const VkShaderModule ScopedVertex = ScopedMakeModule(Shaders::quad_vert);
    const VkShaderModule ScopedFragment = ScopedMakeModule(Shaders::depth_only);
    if (ScopedVertex == VK_NULL_HANDLE || ScopedFragment == VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyShaderModule(MDevice, ScopedVertex, nullptr);
        ScopedApi.vkDestroyShaderModule(MDevice, ScopedFragment, nullptr);
        return false;
    }
    VkPipelineShaderStageCreateInfo ScopedStages[2]{};
    ScopedStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    ScopedStages[0].module = ScopedVertex;
    ScopedStages[0].pName = "main";
    ScopedStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedStages[1].module = ScopedFragment;
    ScopedStages[1].pName = "main";

    VkVertexInputBindingDescription ScopedBinding{};
    ScopedBinding.binding = 0;
    ScopedBinding.stride = sizeof(RenderItem);
    ScopedBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    std::array<VkVertexInputAttributeDescription, 8> ScopedAttributes{};
    ScopedAttributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(RenderItem, position))};
    ScopedAttributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(RenderItem, size))};
    ScopedAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(RenderItem, uvMin))};
    ScopedAttributes[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(RenderItem, uvMax))};
    ScopedAttributes[4] = {4, 0, VK_FORMAT_R32_UINT, static_cast<std::uint32_t>(offsetof(RenderItem, layer))};
    ScopedAttributes[5] = {5, 0, VK_FORMAT_R32_UINT, static_cast<std::uint32_t>(offsetof(RenderItem, color))};
    ScopedAttributes[6] = {6, 0, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(RenderItem, rotation))};
    ScopedAttributes[7] = {7, 0, VK_FORMAT_R32_UINT, static_cast<std::uint32_t>(offsetof(RenderItem, flags))};
    VkPipelineVertexInputStateCreateInfo ScopedVertexInput{};
    ScopedVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    ScopedVertexInput.vertexBindingDescriptionCount = 1;
    ScopedVertexInput.pVertexBindingDescriptions = &ScopedBinding;
    ScopedVertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(ScopedAttributes.size());
    ScopedVertexInput.pVertexAttributeDescriptions = ScopedAttributes.data();
    VkPipelineInputAssemblyStateCreateInfo ScopedAssembly{};
    ScopedAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ScopedAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo ScopedViewport{};
    ScopedViewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ScopedViewport.viewportCount = 1;
    ScopedViewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo ScopedRaster{};
    ScopedRaster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    ScopedRaster.polygonMode = VK_POLYGON_MODE_FILL;
    ScopedRaster.cullMode = VK_CULL_MODE_NONE;
    ScopedRaster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    ScopedRaster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ScopedMultisample{};
    ScopedMultisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ScopedMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendStateCreateInfo ScopedBlend{};
    ScopedBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkPipelineDepthStencilStateCreateInfo ScopedDepthState{};
    ScopedDepthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ScopedDepthState.depthTestEnable = VK_TRUE;
    ScopedDepthState.depthWriteEnable = VK_TRUE;
    ScopedDepthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ScopedDepthState.minDepthBounds = 0.0f;
    ScopedDepthState.maxDepthBounds = 1.0f;
    const VkDynamicState ScopedDynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ScopedDynamic{};
    ScopedDynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ScopedDynamic.dynamicStateCount = 2;
    ScopedDynamic.pDynamicStates = ScopedDynamicStates;
    VkGraphicsPipelineCreateInfo ScopedInfo{};
    ScopedInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ScopedInfo.stageCount = 2;
    ScopedInfo.pStages = ScopedStages;
    ScopedInfo.pVertexInputState = &ScopedVertexInput;
    ScopedInfo.pInputAssemblyState = &ScopedAssembly;
    ScopedInfo.pViewportState = &ScopedViewport;
    ScopedInfo.pRasterizationState = &ScopedRaster;
    ScopedInfo.pMultisampleState = &ScopedMultisample;
    ScopedInfo.pColorBlendState = &ScopedBlend;
    ScopedInfo.pDepthStencilState = &ScopedDepthState;
    ScopedInfo.pDynamicState = &ScopedDynamic;
    ScopedInfo.layout = MPipelineLayout;
    ScopedInfo.renderPass = MDepthPrepassRenderPass;
    ScopedInfo.subpass = 0;
    const VkResult ScopedResult =
        ScopedApi.vkCreateGraphicsPipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedInfo, nullptr, &MDepthPrepassPipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedVertex, nullptr);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedFragment, nullptr);
    if (ScopedResult != VK_SUCCESS)
    {
        LogError("vulkan", "depth prepass pipeline failed: {}", static_cast<int>(ScopedResult));
        return false;
    }
    MDepthPrepassReady = true;
    MInfo.depthPrepass = MDepthPrepassRequested;
    return true;
}

bool VulkanRHI::CreateCullResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (ScopedApi.vkCmdDrawIndexedIndirectCount == nullptr)
    {
        return false;
    }
    std::array<VkDescriptorSetLayoutBinding, 6> ScopedBindings{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 4; ++ScopedIndex)
    {
        ScopedBindings[ScopedIndex].binding = ScopedIndex;
        ScopedBindings[ScopedIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ScopedBindings[ScopedIndex].descriptorCount = 1;
        ScopedBindings[ScopedIndex].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    ScopedBindings[4].binding = 4;
    ScopedBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[4].descriptorCount = 1;
    ScopedBindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedBindings[5].binding = 5;
    ScopedBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[5].descriptorCount = 1;
    ScopedBindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo ScopedSetInfo{};
    ScopedSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetInfo.bindingCount = static_cast<std::uint32_t>(ScopedBindings.size());
    ScopedSetInfo.pBindings = ScopedBindings.data();
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetInfo, nullptr, &MCullSetLayout) != VK_SUCCESS)
    {
        return false;
    }
    std::array<VkDescriptorPoolSize, 2> ScopedPoolSizes{};
    ScopedPoolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4u * kMaxFramesInFlight};
    ScopedPoolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2u * kMaxFramesInFlight};
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = kMaxFramesInFlight;
    ScopedPoolInfo.poolSizeCount = static_cast<std::uint32_t>(ScopedPoolSizes.size());
    ScopedPoolInfo.pPoolSizes = ScopedPoolSizes.data();
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MCullPool) != VK_SUCCESS)
    {
        return false;
    }
    VkPushConstantRange ScopedRange{};
    ScopedRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedRange.size = sizeof(GpuCullingConstants);
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MCullSetLayout;
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MCullLayout) != VK_SUCCESS)
    {
        return false;
    }
    const auto ScopedPipeline = [this, &ScopedApi](const auto& InCode, VkPipeline* OutPipeline)
    {
        VkShaderModuleCreateInfo ScopedInfo{};
        ScopedInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ScopedInfo.codeSize = InCode.size() * sizeof(std::uint32_t);
        ScopedInfo.pCode = InCode.data();
        VkShaderModule ScopedModule = VK_NULL_HANDLE;
        if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedInfo, nullptr, &ScopedModule) != VK_SUCCESS)
        {
            return false;
        }
        VkComputePipelineCreateInfo ScopedCreate{};
        ScopedCreate.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ScopedCreate.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ScopedCreate.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ScopedCreate.stage.module = ScopedModule;
        ScopedCreate.stage.pName = "main";
        ScopedCreate.layout = MCullLayout;
        const VkResult ScopedResult =
            ScopedApi.vkCreateComputePipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedCreate, nullptr, OutPipeline);
        ScopedApi.vkDestroyShaderModule(MDevice, ScopedModule, nullptr);
        return ScopedResult == VK_SUCCESS;
    };
    if (!ScopedPipeline(Shaders::sprite_cull, &MCullPipeline) ||
        !ScopedPipeline(Shaders::sprite_cull_finalize, &MCullFinalizePipeline))
    {
        return false;
    }
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < kMaxFramesInFlight; ++ScopedFrame)
    {
        CullFrame& ScopedCull = MCullFrames[ScopedFrame];
        if (!CreateBuffer(static_cast<VkDeviceSize>(kMaxCulledItems) * sizeof(RenderItem),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ScopedCull.VisibleBuffer,
                          &ScopedCull.VisibleMemory) ||
            !CreateBuffer(static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(std::uint32_t),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ScopedCull.CounterBuffer, &ScopedCull.CounterMemory) ||
            !CreateBuffer(static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(VkDrawIndexedIndirectCommand),
                          VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &ScopedCull.CommandBuffer, &ScopedCull.CommandMemory) ||
            !CreateBuffer(static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(std::uint32_t),
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &ScopedCull.ReadbackBuffer, &ScopedCull.ReadbackMemory))
        {
            return false;
        }
        if (ScopedApi.vkMapMemory(MDevice, ScopedCull.CommandMemory, 0, VK_WHOLE_SIZE, 0, &ScopedCull.CommandMapped) !=
                VK_SUCCESS ||
            ScopedApi.vkMapMemory(MDevice, ScopedCull.ReadbackMemory, 0, VK_WHOLE_SIZE, 0,
                                  &ScopedCull.ReadbackMapped) != VK_SUCCESS)
        {
            return false;
        }
        *static_cast<std::uint32_t*>(ScopedCull.ReadbackMapped) = 0;
        VkDrawIndexedIndirectCommand ScopedCommand{};
        ScopedCommand.indexCount = 6;
        ScopedCommand.instanceCount = 0;
        ScopedCommand.firstInstance = 0;
        std::memcpy(ScopedCull.CommandMapped, &ScopedCommand, sizeof(ScopedCommand));

        VkDescriptorSetAllocateInfo ScopedAllocate{};
        ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ScopedAllocate.descriptorPool = MCullPool;
        ScopedAllocate.descriptorSetCount = 1;
        ScopedAllocate.pSetLayouts = &MCullSetLayout;
        if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, &ScopedCull.Set) != VK_SUCCESS)
        {
            return false;
        }
        VkDescriptorBufferInfo ScopedInfo[4]{};
        ScopedInfo[0] = {MInstanceBuffer, 0, VK_WHOLE_SIZE};
        ScopedInfo[1] = {ScopedCull.VisibleBuffer, 0, VK_WHOLE_SIZE};
        ScopedInfo[2] = {ScopedCull.CounterBuffer, 0, VK_WHOLE_SIZE};
        ScopedInfo[3] = {ScopedCull.CommandBuffer, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo ScopedDepthInfo{};
        ScopedDepthInfo.sampler = MDepthSampler;
        ScopedDepthInfo.imageView = MDrawDepthViews.empty() ? VK_NULL_HANDLE : MDrawDepthViews[0];
        ScopedDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo ScopedHiZInfo{};
        ScopedHiZInfo.sampler = MHiZSampler;
        ScopedHiZInfo.imageView = MHiZView;
        ScopedHiZInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet ScopedWrites[6]{};
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 4; ++ScopedIndex)
        {
            ScopedWrites[ScopedIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ScopedWrites[ScopedIndex].dstSet = ScopedCull.Set;
            ScopedWrites[ScopedIndex].dstBinding = ScopedIndex;
            ScopedWrites[ScopedIndex].descriptorCount = 1;
            ScopedWrites[ScopedIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ScopedWrites[ScopedIndex].pBufferInfo = &ScopedInfo[ScopedIndex];
        }
        ScopedWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[4].dstSet = ScopedCull.Set;
        ScopedWrites[4].dstBinding = 4;
        ScopedWrites[4].descriptorCount = 1;
        ScopedWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrites[4].pImageInfo = &ScopedDepthInfo;
        ScopedWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[5].dstSet = ScopedCull.Set;
        ScopedWrites[5].dstBinding = 5;
        ScopedWrites[5].descriptorCount = 1;
        ScopedWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrites[5].pImageInfo = &ScopedHiZInfo;
        ScopedApi.vkUpdateDescriptorSets(MDevice, 6, ScopedWrites, 0, nullptr);
    }
    MCullReady = true;
    MInfo.gpuCulling = true;
    return true;
}

void VulkanRHI::DestroyCullResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    MCullReady = false;
    MInfo.gpuCulling = false;
    for (CullFrame& ScopedFrame : MCullFrames)
    {
        if (ScopedFrame.CommandMapped != nullptr && ScopedFrame.CommandMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkUnmapMemory(MDevice, ScopedFrame.CommandMemory);
            ScopedFrame.CommandMapped = nullptr;
        }
        if (ScopedFrame.ReadbackMapped != nullptr && ScopedFrame.ReadbackMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkUnmapMemory(MDevice, ScopedFrame.ReadbackMemory);
            ScopedFrame.ReadbackMapped = nullptr;
        }
        if (ScopedFrame.ReadbackBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, ScopedFrame.ReadbackBuffer, nullptr);
            ScopedFrame.ReadbackBuffer = VK_NULL_HANDLE;
        }
        if (ScopedFrame.ReadbackMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedFrame.ReadbackMemory, nullptr);
            ScopedFrame.ReadbackMemory = VK_NULL_HANDLE;
        }
        if (ScopedFrame.CommandBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, ScopedFrame.CommandBuffer, nullptr);
            ScopedFrame.CommandBuffer = VK_NULL_HANDLE;
        }
        if (ScopedFrame.CommandMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedFrame.CommandMemory, nullptr);
            ScopedFrame.CommandMemory = VK_NULL_HANDLE;
        }
        if (ScopedFrame.VisibleBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, ScopedFrame.VisibleBuffer, nullptr);
            ScopedFrame.VisibleBuffer = VK_NULL_HANDLE;
        }
        if (ScopedFrame.VisibleMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedFrame.VisibleMemory, nullptr);
            ScopedFrame.VisibleMemory = VK_NULL_HANDLE;
        }
        if (ScopedFrame.CounterBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, ScopedFrame.CounterBuffer, nullptr);
            ScopedFrame.CounterBuffer = VK_NULL_HANDLE;
        }
        if (ScopedFrame.CounterMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedFrame.CounterMemory, nullptr);
            ScopedFrame.CounterMemory = VK_NULL_HANDLE;
        }
        ScopedFrame.Set = VK_NULL_HANDLE;
    }
    if (MCullFinalizePipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MCullFinalizePipeline, nullptr);
        MCullFinalizePipeline = VK_NULL_HANDLE;
    }
    if (MCullPipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MCullPipeline, nullptr);
        MCullPipeline = VK_NULL_HANDLE;
    }
    if (MCullLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipelineLayout(MDevice, MCullLayout, nullptr);
        MCullLayout = VK_NULL_HANDLE;
    }
    if (MCullPool != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorPool(MDevice, MCullPool, nullptr);
        MCullPool = VK_NULL_HANDLE;
    }
    if (MCullSetLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MCullSetLayout, nullptr);
        MCullSetLayout = VK_NULL_HANDLE;
    }
}

VkDeviceSize VulkanRHI::UploadPrepassInstances(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket)
{
    const std::uint32_t ScopedCount = static_cast<std::uint32_t>(InPacket.items.size());
    if (ScopedCount > MInstanceCapacity || MInstanceFrameCursor > MInstanceCapacity - ScopedCount)
    {
        return 0;
    }
    const VkDeviceSize ScopedSliceBytes = static_cast<VkDeviceSize>(MInstanceCapacity) * sizeof(RenderItem);
    auto* ScopedSlice = static_cast<std::byte*>(MInstanceMapped) + ScopedSliceBytes * MFrameIndex +
                        static_cast<std::size_t>(MInstanceFrameCursor) * sizeof(RenderItem);
    std::memcpy(ScopedSlice, InPacket.items.data(), InPacket.items.size_bytes());
    const VkDeviceSize ScopedBase =
        static_cast<VkDeviceSize>(MFrameIndex) * MInstanceCapacity + MInstanceFrameCursor;
    VkBufferCopy ScopedCopy{};
    ScopedCopy.srcOffset = ScopedBase * sizeof(RenderItem);
    ScopedCopy.dstOffset = ScopedCopy.srcOffset;
    ScopedCopy.size = static_cast<VkDeviceSize>(InPacket.items.size_bytes());
    Vk::GetApi().vkCmdCopyBuffer(InCommandBuffer, MInstanceStagingBuffer, MInstanceBuffer, 1, &ScopedCopy);
    VkBufferMemoryBarrier ScopedBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    ScopedBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.buffer = MInstanceBuffer;
    ScopedBarrier.offset = 0;
    ScopedBarrier.size = VK_WHOLE_SIZE;
    Vk::GetApi().vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0,
                                      nullptr, 1, &ScopedBarrier, 0, nullptr);
    MInstanceFrameCursor += ScopedCount;
    ++MRecordedPassIndex;
    MStats.uploadedBytes += InPacket.items.size_bytes();
    return ScopedBase;
}

void VulkanRHI::UpdateCullDepth(VkDescriptorSet InSet, VkImageView InView)
{
    if (InSet == VK_NULL_HANDLE || InView == VK_NULL_HANDLE)
    {
        return;
    }
    VkDescriptorImageInfo ScopedInfo{};
    ScopedInfo.sampler = MDepthSampler;
    ScopedInfo.imageView = InView;
    ScopedInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo ScopedHiZ{};
    ScopedHiZ.sampler = MHiZSampler;
    ScopedHiZ.imageView = MHiZView;
    ScopedHiZ.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet ScopedWrites[2]{};
    ScopedWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrites[0].dstSet = InSet;
    ScopedWrites[0].dstBinding = 4;
    ScopedWrites[0].descriptorCount = 1;
    ScopedWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrites[0].pImageInfo = &ScopedInfo;
    ScopedWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrites[1].dstSet = InSet;
    ScopedWrites[1].dstBinding = 5;
    ScopedWrites[1].descriptorCount = 1;
    ScopedWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrites[1].pImageInfo = &ScopedHiZ;
    Vk::GetApi().vkUpdateDescriptorSets(MDevice, 2, ScopedWrites, 0, nullptr);
}

void VulkanRHI::RecordOpaqueCull(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket,
                                VkDeviceSize InSourceBase)
{
    if (!MCullReady || MFrameIndex >= MCullFrames.size())
    {
        return;
    }
    CullFrame& ScopedFrame = MCullFrames[MFrameIndex];
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MImageIndex < MDrawDepthViews.size())
    {
        UpdateCullDepth(ScopedFrame.Set, MDrawDepthViews[MImageIndex]);
    }
    ScopedApi.vkCmdFillBuffer(InCommandBuffer, ScopedFrame.CounterBuffer, 0,
                              static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(std::uint32_t), 0u);
    VkMemoryBarrier ScopedBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ScopedBarrier, 0, nullptr, 0,
                                   nullptr);
    GpuCullingConstants ScopedConstants{};
    const float ScopedHalfX = InPacket.camera.viewport.X * 0.5f / std::max(InPacket.camera.zoom, 0.0001f);
    const float ScopedHalfY = InPacket.camera.viewport.Y * 0.5f / std::max(InPacket.camera.zoom, 0.0001f);
    ScopedConstants.ViewBounds = Vector4D{InPacket.camera.center.X - ScopedHalfX, InPacket.camera.center.Y - ScopedHalfY,
                                          InPacket.camera.center.X + ScopedHalfX, InPacket.camera.center.Y + ScopedHalfY};
    ScopedConstants.Viewport = Vector4D{InPacket.camera.viewport.X, InPacket.camera.viewport.Y, 0.0f,
                                        MDesc.hiZLodScale};
    ScopedConstants.UseDepth = (InPacket.sceneStatic && MHiZReady) ? 1u : 0u;
    ScopedConstants.ItemCount = static_cast<std::uint32_t>(InPacket.items.size());
    ScopedConstants.MaxVisible = kMaxCulledItems;
    ScopedConstants.SourceOffset = static_cast<std::uint32_t>(InSourceBase);
    ScopedConstants.OutputOffset = 0;
    ScopedConstants.CounterOffset = 0;
    ScopedConstants.CommandOffset = 0;
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MCullPipeline);
    ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MCullLayout, 0, 1,
                                      &ScopedFrame.Set, 0, nullptr);
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MCullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 sizeof(ScopedConstants), &ScopedConstants);
    ScopedApi.vkCmdDispatch(InCommandBuffer, (ScopedConstants.ItemCount + 63u) / 64u, 1, 1);
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ScopedBarrier, 0, nullptr, 0, nullptr);
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MCullFinalizePipeline);
    ScopedApi.vkCmdDispatch(InCommandBuffer, 1, 1, 1);
    VkBufferMemoryBarrier ScopedCounterBarrier{};
    ScopedCounterBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    ScopedCounterBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ScopedCounterBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ScopedCounterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedCounterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedCounterBarrier.buffer = ScopedFrame.CounterBuffer;
    ScopedCounterBarrier.offset = 0;
    ScopedCounterBarrier.size = static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(std::uint32_t);
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &ScopedCounterBarrier, 0, nullptr);
    VkBufferMemoryBarrier ScopedCommandBarrier{};
    ScopedCommandBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    ScopedCommandBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ScopedCommandBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    ScopedCommandBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedCommandBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedCommandBarrier.buffer = ScopedFrame.CommandBuffer;
    ScopedCommandBarrier.offset = 0;
    ScopedCommandBarrier.size =
        static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(VkDrawIndexedIndirectCommand);
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 1, &ScopedCommandBarrier, 0,
                                   nullptr);
    VkBufferCopy ScopedReadbackCopy{};
    ScopedReadbackCopy.size = static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(std::uint32_t);
    ScopedApi.vkCmdCopyBuffer(InCommandBuffer, ScopedFrame.CounterBuffer, ScopedFrame.ReadbackBuffer, 1,
                              &ScopedReadbackCopy);
    VkMemoryBarrier ScopedVertexBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                                        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT};
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &ScopedVertexBarrier, 0, nullptr, 0,
                                   nullptr);
}

void VulkanRHI::RecordMainCull(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket,
                               VkDeviceSize InSourceBase)
{
    if (!MCullReady || MFrameIndex >= MCullFrames.size() || InPacket.batches.empty() ||
        InPacket.batches.size() > kMaxCullBatches)
    {
        return;
    }
    CullFrame& ScopedFrame = MCullFrames[MFrameIndex];
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MImageIndex < MDrawDepthViews.size())
    {
        UpdateCullDepth(ScopedFrame.Set, MDrawDepthViews[MImageIndex]);
    }
    VkMemoryBarrier ScopedDepthBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT};
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer,
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ScopedDepthBarrier, 0, nullptr, 0,
                                   nullptr);
    ScopedApi.vkCmdFillBuffer(InCommandBuffer, ScopedFrame.CounterBuffer, 0,
                              static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(std::uint32_t), 0u);
    VkMemoryBarrier ScopedBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ScopedBarrier, 0, nullptr, 0,
                                   nullptr);
    GpuCullingConstants ScopedConstants{};
    const float ScopedHalfX = InPacket.camera.viewport.X * 0.5f / std::max(InPacket.camera.zoom, 0.0001f);
    const float ScopedHalfY = InPacket.camera.viewport.Y * 0.5f / std::max(InPacket.camera.zoom, 0.0001f);
    ScopedConstants.ViewBounds = Vector4D{InPacket.camera.center.X - ScopedHalfX,
                                          InPacket.camera.center.Y - ScopedHalfY,
                                          InPacket.camera.center.X + ScopedHalfX,
                                          InPacket.camera.center.Y + ScopedHalfY};
    ScopedConstants.Viewport = Vector4D{InPacket.camera.viewport.X, InPacket.camera.viewport.Y, 0.0f,
                                        MDesc.hiZLodScale};
    ScopedConstants.UseDepth = MHiZReady ? 1u : 0u;
    ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MCullLayout, 0, 1,
                                      &ScopedFrame.Set, 0, nullptr);
    std::uint32_t ScopedOutput = 0;
    for (std::uint32_t ScopedBatchIndex = 0; ScopedBatchIndex < InPacket.batches.size(); ++ScopedBatchIndex)
    {
        const DrawBatch& ScopedBatch = InPacket.batches[ScopedBatchIndex];
        if (ScopedBatch.itemCount == 0 || ScopedOutput >= kMaxCulledItems)
        {
            continue;
        }
        const std::uint32_t ScopedCount = std::min(ScopedBatch.itemCount, kMaxCulledItems - ScopedOutput);
        ScopedConstants.ItemCount = ScopedCount;
        ScopedConstants.MaxVisible = ScopedCount;
        ScopedConstants.SourceOffset = static_cast<std::uint32_t>(InSourceBase + ScopedBatch.firstItem);
        ScopedConstants.OutputOffset = ScopedOutput;
        ScopedConstants.CounterOffset = ScopedBatchIndex;
        ScopedConstants.CommandOffset = ScopedBatchIndex;
        ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MCullPipeline);
        ScopedApi.vkCmdPushConstants(InCommandBuffer, MCullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                     sizeof(ScopedConstants), &ScopedConstants);
        ScopedApi.vkCmdDispatch(InCommandBuffer, (ScopedCount + 63u) / 64u, 1, 1);
        ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ScopedBarrier, 0, nullptr, 0,
                                       nullptr);
        ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MCullFinalizePipeline);
        ScopedApi.vkCmdPushConstants(InCommandBuffer, MCullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                     sizeof(ScopedConstants), &ScopedConstants);
        ScopedApi.vkCmdDispatch(InCommandBuffer, 1, 1, 1);
        ScopedOutput += ScopedCount;
    }
    VkMemoryBarrier ScopedDrawBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                                      VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT};
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1,
                                   &ScopedDrawBarrier, 0, nullptr, 0, nullptr);
    VkBufferMemoryBarrier ScopedCounterRead{};
    ScopedCounterRead.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    ScopedCounterRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ScopedCounterRead.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ScopedCounterRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedCounterRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedCounterRead.buffer = ScopedFrame.CounterBuffer;
    ScopedCounterRead.offset = 0;
    ScopedCounterRead.size = static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(std::uint32_t);
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &ScopedCounterRead, 0, nullptr);
    VkBufferCopy ScopedCulledCopy{};
    ScopedCulledCopy.size = static_cast<VkDeviceSize>(kMaxCullBatches) * sizeof(std::uint32_t);
    ScopedApi.vkCmdCopyBuffer(InCommandBuffer, ScopedFrame.CounterBuffer, ScopedFrame.ReadbackBuffer, 1,
                              &ScopedCulledCopy);
}

bool VulkanRHI::CreateCommands()
{
    VkCommandPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ScopedPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ScopedPoolInfo.queueFamilyIndex = MQueueFamily;
    if (Vk::GetApi().vkCreateCommandPool(MDevice, &ScopedPoolInfo, nullptr, &MCommandPool) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateCommandPool failed");
        return false;
    }

    VkCommandBufferAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ScopedAllocateInfo.commandPool = MCommandPool;
    ScopedAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ScopedAllocateInfo.commandBufferCount = kMaxFramesInFlight;
    if (Vk::GetApi().vkAllocateCommandBuffers(MDevice, &ScopedAllocateInfo, MCommandBuffers.data()) != VK_SUCCESS)
    {
        LogError("vulkan", "vkAllocateCommandBuffers failed");
        return false;
    }
    return true;
}

bool VulkanRHI::CreateSyncObjects()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // RecreateSwapchain calls this again: destroy the previous handles first,
    // otherwise every resize leaks 2x framesInFlight driver objects.
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxFramesInFlight; ++ScopedI)
    {
        if (MImageAvailable[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySemaphore(MDevice, MImageAvailable[ScopedI], nullptr);
            MImageAvailable[ScopedI] = VK_NULL_HANDLE;
        }
        if (MInFlight[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyFence(MDevice, MInFlight[ScopedI], nullptr);
            MInFlight[ScopedI] = VK_NULL_HANDLE;
        }
        if (MTimestampPools[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyQueryPool(MDevice, MTimestampPools[ScopedI], nullptr);
            MTimestampPools[ScopedI] = VK_NULL_HANDLE;
        }
    }
    for (VkSemaphore& ScopedSemaphore : MRenderFinished)
    {
        if (ScopedSemaphore != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySemaphore(MDevice, ScopedSemaphore, nullptr);
            ScopedSemaphore = VK_NULL_HANDLE;
        }
    }
    MRenderFinished.clear();

    VkSemaphoreCreateInfo ScopedSemaphoreInfo{};
    ScopedSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo ScopedFenceInfo{};
    ScopedFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    ScopedFenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::uint32_t ScopedI = 0; ScopedI < kMaxFramesInFlight; ++ScopedI)
    {
        if (ScopedApi.vkCreateSemaphore(MDevice, &ScopedSemaphoreInfo, nullptr, &MImageAvailable[ScopedI]) !=
                VK_SUCCESS ||
            ScopedApi.vkCreateFence(MDevice, &ScopedFenceInfo, nullptr, &MInFlight[ScopedI]) != VK_SUCCESS)
        {
            LogError("vulkan", "failed to create frame synchronisation objects");
            return false;
        }
    }

    MRenderFinished.resize(MImages.size());
    for (VkSemaphore& ScopedSemaphore : MRenderFinished)
    {
        if (ScopedApi.vkCreateSemaphore(MDevice, &ScopedSemaphoreInfo, nullptr, &ScopedSemaphore) != VK_SUCCESS)
        {
            LogError("vulkan", "failed to create present semaphores");
            return false;
        }
    }

    MTimestampsReady = false;
    MTimestampPeriod = 0.0f;
    if (MPhysical != VK_NULL_HANDLE && ScopedApi.vkGetPhysicalDeviceProperties != nullptr)
    {
        VkPhysicalDeviceProperties ScopedProperties{};
        ScopedApi.vkGetPhysicalDeviceProperties(MPhysical, &ScopedProperties);
        MTimestampPeriod = ScopedProperties.limits.timestampPeriod;
        std::uint32_t ScopedQueueCount = 0;
        ScopedApi.vkGetPhysicalDeviceQueueFamilyProperties(MPhysical, &ScopedQueueCount, nullptr);
        std::vector<VkQueueFamilyProperties> ScopedQueues(ScopedQueueCount);
        if (ScopedQueueCount > 0)
        {
            ScopedApi.vkGetPhysicalDeviceQueueFamilyProperties(MPhysical, &ScopedQueueCount, ScopedQueues.data());
        }
        const bool ScopedTimestampQueue =
            MQueueFamily < ScopedQueues.size() && ScopedQueues[MQueueFamily].timestampValidBits != 0;
        bool ScopedTimestampOk = MTimestampPeriod > 0.0f && ScopedTimestampQueue;
        if (ScopedTimestampOk)
        {
            VkQueryPoolCreateInfo ScopedQueryInfo{};
            ScopedQueryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            ScopedQueryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            ScopedQueryInfo.queryCount = TimestampCount;
            for (std::uint32_t ScopedI = 0; ScopedI < kMaxFramesInFlight; ++ScopedI)
            {
                if (ScopedApi.vkCreateQueryPool(MDevice, &ScopedQueryInfo, nullptr, &MTimestampPools[ScopedI]) !=
                    VK_SUCCESS)
                {
                    ScopedTimestampOk = false;
                    break;
                }
            }
        }
        if (ScopedTimestampOk)
        {
            // A fresh query pool has never been reset, so reading it would violate
            // VUID-vkGetQueryPoolResults-None-09401. Reset every pool once through a
            // one-shot submit before the first frame can read it.
            for (std::uint32_t ScopedI = 0; ScopedI < kMaxFramesInFlight; ++ScopedI)
            {
                const VkCommandBuffer ScopedInit = BeginOneShot();
                ScopedApi.vkCmdResetQueryPool(ScopedInit, MTimestampPools[ScopedI], 0, TimestampCount);
                EndOneShot(ScopedInit);
            }
        }
        if (!ScopedTimestampOk)
        {
            for (VkQueryPool& ScopedPool : MTimestampPools)
            {
                if (ScopedPool != VK_NULL_HANDLE)
                {
                    ScopedApi.vkDestroyQueryPool(MDevice, ScopedPool, nullptr);
                    ScopedPool = VK_NULL_HANDLE;
                }
            }
        }
        MTimestampsReady = ScopedTimestampOk;
    }
    MInfo.timestamps = MTimestampsReady;
    return true;
}

bool VulkanRHI::CreateDescriptors()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    MInfo.staticCaches = MDesc.enableStaticCaches;

    // Binding 0 atlas, 1 SDF field, 2 dynamic per-view SDF block. Every
    // descriptor set built from this layout (the built-in atlas and each texture
    // array) gets all bindings written by WriteSdfDescriptors.
    std::array<VkDescriptorSetLayoutBinding, 9> ScopedBindings{};
    ScopedBindings[0].binding = 0;
    ScopedBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[0].descriptorCount = 1;
    ScopedBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[1].binding = 1;
    ScopedBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[1].descriptorCount = 1;
    ScopedBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[2].binding = 2;
    ScopedBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ScopedBindings[2].descriptorCount = 1;
    ScopedBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[3].binding = 3;
    ScopedBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[3].descriptorCount = 1;
    ScopedBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[4].binding = 4;
    ScopedBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[4].descriptorCount = 1;
    ScopedBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[5].binding = 5;
    ScopedBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[5].descriptorCount = 1;
    ScopedBindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[6].binding = 6;
    ScopedBindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedBindings[6].descriptorCount = kFeedbackSlots;
    ScopedBindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // Sparse brick atlas: the header table and the tile texture. Both stages use
    // them, so the fragment pass and the mask compute pass share the layout.
    ScopedBindings[7].binding = 7;
    ScopedBindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedBindings[7].descriptorCount = 1;
    ScopedBindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedBindings[8].binding = 8;
    ScopedBindings[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[8].descriptorCount = 1;
    ScopedBindings[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    if (!CreateSdfShadowResources())
    {
        return false;
    }
    MGiReady = CreateGiResources();
    if (!MGiReady)
    {
        LogWarn("vulkan", "GI probe buffers unavailable; SDF GI stays on the CPU path");
    }
    // Clustered lighting is optional: if its buffers or pipelines cannot be
    // created the renderer keeps running with the CPU lighting path.
    if (MClusterRequested && MGiReady)
    {
        if (!CreateClusterResources())
        {
            LogInfo("vulkan", "cluster resources unavailable; GPU clustered lighting disabled");
            DestroyClusterResources();
        }
    }

    VkDescriptorSetLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.bindingCount = static_cast<std::uint32_t>(ScopedBindings.size());
    ScopedLayoutInfo.pBindings = ScopedBindings.data();
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedLayoutInfo, nullptr, &MDescriptorLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateDescriptorSetLayout failed");
        return false;
    }

    std::array<VkDescriptorPoolSize, 4> ScopedPoolSizes{};
    ScopedPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedPoolSizes[0].descriptorCount = (kMaxTextureArrays + 1) * 6;
    ScopedPoolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ScopedPoolSizes[1].descriptorCount = kMaxTextureArrays + 1;
    ScopedPoolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedPoolSizes[2].descriptorCount = (kMaxTextureArrays + 1) * kFeedbackSlots;
    ScopedPoolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedPoolSizes[3].descriptorCount = kMaxTextureArrays + 1;

    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ScopedPoolInfo.maxSets = kMaxTextureArrays + 1;
    ScopedPoolInfo.poolSizeCount = static_cast<std::uint32_t>(ScopedPoolSizes.size());
    ScopedPoolInfo.pPoolSizes = ScopedPoolSizes.data();
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MDescriptorPool) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateDescriptorPool failed");
        return false;
    }

    VkDescriptorSetAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocateInfo.descriptorPool = MDescriptorPool;
    ScopedAllocateInfo.descriptorSetCount = 1;
    ScopedAllocateInfo.pSetLayouts = &MDescriptorLayout;
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocateInfo, &MDescriptorSet) != VK_SUCCESS)
    {
        LogError("vulkan", "vkAllocateDescriptorSets failed");
        return false;
    }
    return true;
}

bool VulkanRHI::CreateSdfShadowResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // The static field lives in an R32F texture and stores world-space distance
    // directly. The neutral encode scale keeps the shader contract stable.
    VkImageCreateInfo ScopedImageInfo{};
    ScopedImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedImageInfo.format = VK_FORMAT_R32_SFLOAT;
    ScopedImageInfo.extent = {kSdfFieldSize, kSdfFieldSize, 1};
    ScopedImageInfo.mipLevels = 1;
    ScopedImageInfo.arrayLayers = 1;
    ScopedImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedImageInfo, nullptr, &MSdfFieldImage) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF field image creation failed");
        return false;
    }

    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MSdfFieldImage, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocateInfo.allocationSize = ScopedRequirements.size;
    ScopedAllocateInfo.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocateInfo.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocateInfo, nullptr, &MSdfFieldMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MSdfFieldImage, MSdfFieldMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF field image memory failed");
        return false;
    }
    InitImageLayout(MSdfFieldImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    MSdfFieldInitialized = true;

    VkImageViewCreateInfo ScopedViewInfo{};
    ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedViewInfo.image = MSdfFieldImage;
    ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedViewInfo.format = VK_FORMAT_R32_SFLOAT;
    ScopedViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &MSdfFieldView) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF field image view failed");
        return false;
    }

    VkSamplerCreateInfo ScopedSamplerInfo{};
    ScopedSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSamplerInfo.magFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.minFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.maxLod = 0.0f;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MSdfFieldSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF field sampler failed");
        return false;
    }

    constexpr VkDeviceSize ScopedFieldBytes =
        static_cast<VkDeviceSize>(kSdfFieldSize) * kSdfFieldSize * sizeof(float);
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < kMaxFramesInFlight; ++ScopedFrame)
    {
        if (!CreateBuffer(ScopedFieldBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &MSdfFieldStagingBuffers[ScopedFrame], &MSdfFieldStagingMemories[ScopedFrame]))
        {
            LogError("vulkan", "SDF field staging buffer failed");
            return false;
        }
        if (ScopedApi.vkMapMemory(MDevice, MSdfFieldStagingMemories[ScopedFrame], 0, ScopedFieldBytes, 0,
                                  &MSdfFieldStagingMapped[ScopedFrame]) != VK_SUCCESS)
        {
            MSdfFieldStagingMapped[ScopedFrame] = nullptr;
            return false;
        }
    }

    const VkDeviceSize ScopedBlockBytes = static_cast<VkDeviceSize>(SdfViewSlots) * SdfBlockStride;
    if (!CreateBuffer(ScopedBlockBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &MSdfBlockBuffer,
                      &MSdfBlockMemory))
    {
        LogError("vulkan", "SDF parameter buffer failed");
        return false;
    }
    if (ScopedApi.vkMapMemory(MDevice, MSdfBlockMemory, 0, ScopedBlockBytes, 0, &MSdfBlockMapped) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF parameter buffer map failed");
        MSdfBlockMapped = nullptr;
        return false;
    }

    // Brick table: host-visible, rewritten when the field version changes.
    const VkDeviceSize ScopedTableBytes =
        static_cast<VkDeviceSize>(kSdfTableCapacity) * kSdfTableEntryBytes;
    if (!CreateBuffer(ScopedTableBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &MSdfTableBuffer,
                      &MSdfTableMemory))
    {
        LogError("vulkan", "SDF brick table buffer failed");
        return false;
    }
    if (ScopedApi.vkMapMemory(MDevice, MSdfTableMemory, 0, ScopedTableBytes, 0, &MSdfTableMapped) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF brick table map failed");
        MSdfTableMapped = nullptr;
        return false;
    }
    std::memset(MSdfTableMapped, 0, static_cast<std::size_t>(ScopedTableBytes));

    // Sparse atlas texture: one 16x16 tile per narrow brick, R32F like the field.
    VkImageCreateInfo ScopedAtlasInfo{};
    ScopedAtlasInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedAtlasInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedAtlasInfo.format = VK_FORMAT_R32_SFLOAT;
    ScopedAtlasInfo.extent = {kSdfAtlasSize, kSdfAtlasSize, 1};
    ScopedAtlasInfo.mipLevels = 1;
    ScopedAtlasInfo.arrayLayers = 1;
    ScopedAtlasInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedAtlasInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedAtlasInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedAtlasInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedAtlasInfo, nullptr, &MSdfAtlasImage) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF atlas image creation failed");
        return false;
    }
    VkMemoryRequirements ScopedAtlasRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MSdfAtlasImage, &ScopedAtlasRequirements);
    VkMemoryAllocateInfo ScopedAtlasAllocate{};
    ScopedAtlasAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAtlasAllocate.allocationSize = ScopedAtlasRequirements.size;
    ScopedAtlasAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedAtlasRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAtlasAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAtlasAllocate, nullptr, &MSdfAtlasMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MSdfAtlasImage, MSdfAtlasMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF atlas memory failed");
        return false;
    }
    VkImageViewCreateInfo ScopedAtlasViewInfo{};
    ScopedAtlasViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedAtlasViewInfo.image = MSdfAtlasImage;
    ScopedAtlasViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedAtlasViewInfo.format = VK_FORMAT_R32_SFLOAT;
    ScopedAtlasViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedAtlasViewInfo, nullptr, &MSdfAtlasView) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF atlas view failed");
        return false;
    }
    VkSamplerCreateInfo ScopedAtlasSamplerInfo{};
    ScopedAtlasSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedAtlasSamplerInfo.magFilter = VK_FILTER_LINEAR;
    ScopedAtlasSamplerInfo.minFilter = VK_FILTER_LINEAR;
    ScopedAtlasSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedAtlasSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedAtlasSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedAtlasSamplerInfo.maxLod = 0.0f;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedAtlasSamplerInfo, nullptr, &MSdfAtlasSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF atlas sampler failed");
        return false;
    }

    // The compute set samples the atlas from the first dispatch, so bring the
    // image out of UNDEFINED and zero it before any frame can bind it.
    std::memset(MSdfFieldStagingMapped[0], 0, static_cast<std::size_t>(kSdfAtlasSize) * kSdfAtlasSize * sizeof(float));
    const VkCommandBuffer ScopedAtlasInit = BeginOneShot();
    TransitionImage(ScopedAtlasInit, MSdfAtlasImage, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy ScopedAtlasCopy{};
    ScopedAtlasCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ScopedAtlasCopy.imageExtent = {kSdfAtlasSize, kSdfAtlasSize, 1};
    ScopedApi.vkCmdCopyBufferToImage(ScopedAtlasInit, MSdfFieldStagingBuffers[0], MSdfAtlasImage,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedAtlasCopy);
    TransitionImage(ScopedAtlasInit, MSdfAtlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EndOneShot(ScopedAtlasInit);
    MSdfAtlasInitialized = true;

    // Half-resolution screen mask: the sprite shader samples it instead of
    // marching per pixel, and the compute pass is scheduled by the render graph.
    const Extent2D ScopedMaskExtent{std::max(MExtent.width / 2u, 1u), std::max(MExtent.height / 2u, 1u)};
    if (!EnsureSdfMask(ScopedMaskExtent))
    {
        return false;
    }
    if (!CreateSdfComputeResources())
    {
        LogWarn("vulkan", "SDF mask compute pass unavailable; shadows run in the sprite shader");
    }
    return CreateVtFallbackResources();
}

bool VulkanRHI::CreateVtFallbackResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // 1x1 R32_UINT table with the missing sentinel and a 1x1 white page. They are
    // bound when no virtual texture is active so the descriptor is always valid;
    // the shader's enabled flag is zero in that case.
    VkImageCreateInfo ScopedTableInfo{};
    ScopedTableInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedTableInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedTableInfo.format = VK_FORMAT_R32_UINT;
    ScopedTableInfo.extent = {1, 1, 1};
    ScopedTableInfo.mipLevels = 1;
    ScopedTableInfo.arrayLayers = 1;
    ScopedTableInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedTableInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedTableInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedTableInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedTableInfo, nullptr, &MVtFallbackTable) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback table image failed");
        return false;
    }
    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MVtFallbackTable, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MVtFallbackTableMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MVtFallbackTable, MVtFallbackTableMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback table memory failed");
        return false;
    }
    InitImageLayout(MVtFallbackTable, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkImageViewCreateInfo ScopedTableView{};
    ScopedTableView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedTableView.image = MVtFallbackTable;
    ScopedTableView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedTableView.format = VK_FORMAT_R32_UINT;
    ScopedTableView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedTableView, nullptr, &MVtFallbackTableView) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback table view failed");
        return false;
    }
    VkSamplerCreateInfo ScopedSampler{};
    ScopedSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSampler.magFilter = VK_FILTER_NEAREST;
    ScopedSampler.minFilter = VK_FILTER_NEAREST;
    ScopedSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.maxLod = 0.0f;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSampler, nullptr, &MVtFallbackTableSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback table sampler failed");
        return false;
    }

    VkImageCreateInfo ScopedPagesInfo{};
    ScopedPagesInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedPagesInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedPagesInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ScopedPagesInfo.extent = {1, 1, 1};
    ScopedPagesInfo.mipLevels = 1;
    ScopedPagesInfo.arrayLayers = 1;
    ScopedPagesInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedPagesInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedPagesInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedPagesInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedPagesInfo, nullptr, &MVtFallbackPages) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback page image failed");
        return false;
    }
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MVtFallbackPages, &ScopedRequirements);
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MVtFallbackPagesMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MVtFallbackPages, MVtFallbackPagesMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback page memory failed");
        return false;
    }
    InitImageLayout(MVtFallbackPages, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkImageViewCreateInfo ScopedPagesView{};
    ScopedPagesView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedPagesView.image = MVtFallbackPages;
    ScopedPagesView.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ScopedPagesView.format = VK_FORMAT_R8G8B8A8_UNORM;
    ScopedPagesView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedPagesView, nullptr, &MVtFallbackPagesView) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback page view failed");
        return false;
    }
    ScopedSampler.magFilter = VK_FILTER_LINEAR;
    ScopedSampler.minFilter = VK_FILTER_LINEAR;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSampler, nullptr, &MVtFallbackPagesSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback page sampler failed");
        return false;
    }

    // 1x1 r32ui storage image for the feedback descriptor array before the real
    // feedback images exist.
    VkImageCreateInfo ScopedFeedbackInfo{};
    ScopedFeedbackInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedFeedbackInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedFeedbackInfo.format = VK_FORMAT_R32_UINT;
    ScopedFeedbackInfo.extent = {1, 1, 1};
    ScopedFeedbackInfo.mipLevels = 1;
    ScopedFeedbackInfo.arrayLayers = 1;
    ScopedFeedbackInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedFeedbackInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedFeedbackInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedFeedbackInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedFeedbackInfo, nullptr, &MVtFeedbackFallback) != VK_SUCCESS)
    {
        LogError("vulkan", "VT feedback fallback image failed");
        return false;
    }
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MVtFeedbackFallback, &ScopedRequirements);
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MVtFeedbackFallbackMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MVtFeedbackFallback, MVtFeedbackFallbackMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "VT feedback fallback memory failed");
        return false;
    }
    InitImageLayout(MVtFeedbackFallback, VK_IMAGE_LAYOUT_GENERAL);
    VkImageViewCreateInfo ScopedFeedbackView{};
    ScopedFeedbackView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedFeedbackView.image = MVtFeedbackFallback;
    ScopedFeedbackView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedFeedbackView.format = VK_FORMAT_R32_UINT;
    ScopedFeedbackView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedFeedbackView, nullptr, &MVtFeedbackFallbackView) != VK_SUCCESS)
    {
        LogError("vulkan", "VT feedback fallback view failed");
        return false;
    }
    return true;
}

void VulkanRHI::DestroyVtFallbackResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        return;
    }
    DestroyVtFeedbackResources();
    if (MVtFeedbackFallbackView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MVtFeedbackFallbackView, nullptr);
        MVtFeedbackFallbackView = VK_NULL_HANDLE;
    }
    if (MVtFeedbackFallback != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MVtFeedbackFallback, nullptr);
        MVtFeedbackFallback = VK_NULL_HANDLE;
    }
    if (MVtFeedbackFallbackMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MVtFeedbackFallbackMemory, nullptr);
        MVtFeedbackFallbackMemory = VK_NULL_HANDLE;
    }
    if (MVtFallbackPagesSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MVtFallbackPagesSampler, nullptr);
        MVtFallbackPagesSampler = VK_NULL_HANDLE;
    }
    if (MVtFallbackPagesView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MVtFallbackPagesView, nullptr);
        MVtFallbackPagesView = VK_NULL_HANDLE;
    }
    if (MVtFallbackPages != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MVtFallbackPages, nullptr);
        MVtFallbackPages = VK_NULL_HANDLE;
    }
    if (MVtFallbackPagesMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MVtFallbackPagesMemory, nullptr);
        MVtFallbackPagesMemory = VK_NULL_HANDLE;
    }
    if (MVtFallbackTableSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MVtFallbackTableSampler, nullptr);
        MVtFallbackTableSampler = VK_NULL_HANDLE;
    }
    if (MVtFallbackTableView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MVtFallbackTableView, nullptr);
        MVtFallbackTableView = VK_NULL_HANDLE;
    }
    if (MVtFallbackTable != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MVtFallbackTable, nullptr);
        MVtFallbackTable = VK_NULL_HANDLE;
    }
    if (MVtFallbackTableMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MVtFallbackTableMemory, nullptr);
        MVtFallbackTableMemory = VK_NULL_HANDLE;
    }
}

void VulkanRHI::ApplyVirtualTexture(const FramePacket& InPacket)
{
    if (!InPacket.virtualTexture.IsValid())
    {
        return;
    }
    if (InPacket.virtualTexture.id == MActiveVirtualTexture.id)
    {
        return;
    }
    if (InPacket.virtualTexture.id == 0 || InPacket.virtualTexture.id >= MVirtualTextures.size() ||
        MVirtualTextures[InPacket.virtualTexture.id] == nullptr)
    {
        LogError("vulkan", "frame references an unknown virtual texture");
        return;
    }
    MActiveVirtualTexture = InPacket.virtualTexture;
    // The shared sprite sets are rewritten in BeginFrame, before any pass binds
    // them: updating a set that the recording command buffer already bound
    // invalidates that command buffer.
    MVirtualTextureDirty = true;
    LogInfo("vulkan", "virtual texture #{} bound to the sprite sets", InPacket.virtualTexture.id);
}

bool VulkanRHI::EnsureSdfMask(Extent2D InMaskExtent)
{
    if (MInFrame)
    {
        // Recreating the mask rewrites descriptor sets. The mask is created or
        // resized in BeginFrame, before any pass binds those sets.
        return MSdfMaskView != VK_NULL_HANDLE;
    }
    if (InMaskExtent.width == 0 || InMaskExtent.height == 0)
    {
        return false;
    }
    if (MSdfMaskImage != VK_NULL_HANDLE && MSdfMaskExtent.width == InMaskExtent.width &&
        MSdfMaskExtent.height == InMaskExtent.height)
    {
        return true;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    // A viewport resize can reach this point mid-frame. Do not free an image
    // that command buffers from older frames may still reference.
    WaitIdle();
    DestroySdfMaskResources();

    VkImageCreateInfo ScopedImageInfo{};
    ScopedImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedImageInfo.format = VK_FORMAT_R16_SFLOAT;
    ScopedImageInfo.extent = {InMaskExtent.width, InMaskExtent.height, 1};
    ScopedImageInfo.mipLevels = 1;
    ScopedImageInfo.arrayLayers = SdfViewSlots;
    ScopedImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ScopedImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedImageInfo, nullptr, &MSdfMaskImage) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF mask image creation failed");
        return false;
    }
    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MSdfMaskImage, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MSdfMaskMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MSdfMaskImage, MSdfMaskMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF mask memory failed");
        return false;
    }
    VkImageViewCreateInfo ScopedViewInfo{};
    ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedViewInfo.image = MSdfMaskImage;
    ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ScopedViewInfo.format = VK_FORMAT_R16_SFLOAT;
    ScopedViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, SdfViewSlots};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &MSdfMaskView) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF mask view failed");
        return false;
    }
    VkSamplerCreateInfo ScopedSamplerInfo{};
    ScopedSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSamplerInfo.magFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.minFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.maxLod = 0.0f;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MSdfMaskSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF mask sampler failed");
        return false;
    }

    // The mask lives in GENERAL: the compute pass writes it as a storage image
    // and the sprite pass samples it, both without layout transitions.
    const VkCommandBuffer ScopedCommand = BeginOneShot();
    if (ScopedCommand != VK_NULL_HANDLE)
    {
        TransitionImage(ScopedCommand, MSdfMaskImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        SdfViewSlots);
        EndOneShot(ScopedCommand);
    }
    MSdfMaskExtent = VkExtent2D{InMaskExtent.width, InMaskExtent.height};
    MSdfMaskPublished.fill(false);
    LogInfo("vulkan", "SDF shadow mask: {}x{} x {} layers (r16f, compute + sampled)", InMaskExtent.width,
            InMaskExtent.height, SdfViewSlots);

    // Descriptor sets that sample the mask must point at the new view.
    if (MDescriptorSet != VK_NULL_HANDLE)
    {
        WriteSdfDescriptors(MDescriptorSet);
    }
    for (const std::unique_ptr<TextureArray>& ScopedArray : MTextureArrays)
    {
        if (ScopedArray != nullptr && ScopedArray->descriptor != VK_NULL_HANDLE)
        {
            WriteSdfDescriptors(ScopedArray->descriptor);
        }
    }
    UpdateSdfComputeSets();
    return true;
}

void VulkanRHI::DestroySdfMaskResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        return;
    }
    if (MSdfMaskSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MSdfMaskSampler, nullptr);
        MSdfMaskSampler = VK_NULL_HANDLE;
    }
    if (MSdfMaskView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MSdfMaskView, nullptr);
        MSdfMaskView = VK_NULL_HANDLE;
    }
    if (MSdfMaskImage != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MSdfMaskImage, nullptr);
        MSdfMaskImage = VK_NULL_HANDLE;
    }
    if (MSdfMaskMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MSdfMaskMemory, nullptr);
        MSdfMaskMemory = VK_NULL_HANDLE;
    }
    MSdfMaskExtent = {};
    MSdfMaskPublished.fill(false);
}

void VulkanRHI::UpdateSdfComputeSets()
{
    if (MSdfComputeSetLayout == VK_NULL_HANDLE || MSdfMaskView == VK_NULL_HANDLE)
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < SdfViewSlots; ++ScopedSlot)
    {
        if (MSdfComputeSets[ScopedSlot] == VK_NULL_HANDLE || MSdfComputeBuffers[ScopedSlot] == VK_NULL_HANDLE)
        {
            continue;
        }
        VkDescriptorImageInfo ScopedFieldInfo{};
        ScopedFieldInfo.sampler = MSdfFieldSampler;
        ScopedFieldInfo.imageView = MSdfFieldView;
        ScopedFieldInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo ScopedMaskInfo{};
        ScopedMaskInfo.imageView = MSdfMaskView;
        ScopedMaskInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo ScopedHistoryInfo{};
        ScopedHistoryInfo.sampler = MSdfMaskSampler;
        ScopedHistoryInfo.imageView = MSdfMaskView;
        ScopedHistoryInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo ScopedAtlasTableInfo{};
        ScopedAtlasTableInfo.buffer = MSdfTableBuffer;
        ScopedAtlasTableInfo.offset = 0;
        ScopedAtlasTableInfo.range = VK_WHOLE_SIZE;
        VkDescriptorImageInfo ScopedAtlasInfo{};
        ScopedAtlasInfo.sampler = MSdfAtlasSampler;
        ScopedAtlasInfo.imageView = MSdfAtlasView;
        ScopedAtlasInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorBufferInfo ScopedBufferInfo{};
        ScopedBufferInfo.buffer = MSdfComputeBuffers[ScopedSlot];
        ScopedBufferInfo.offset = 0;
        ScopedBufferInfo.range = kSdfShadowBlockBytes;
        std::array<VkWriteDescriptorSet, 6> ScopedWrites{};
        ScopedWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[0].dstSet = MSdfComputeSets[ScopedSlot];
        ScopedWrites[0].dstBinding = 0;
        ScopedWrites[0].descriptorCount = 1;
        ScopedWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrites[0].pImageInfo = &ScopedFieldInfo;
        ScopedWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[1].dstSet = MSdfComputeSets[ScopedSlot];
        ScopedWrites[1].dstBinding = 1;
        ScopedWrites[1].descriptorCount = 1;
        ScopedWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        ScopedWrites[1].pImageInfo = &ScopedMaskInfo;
        ScopedWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[2].dstSet = MSdfComputeSets[ScopedSlot];
        ScopedWrites[2].dstBinding = 2;
        ScopedWrites[2].descriptorCount = 1;
        ScopedWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ScopedWrites[2].pBufferInfo = &ScopedBufferInfo;
        ScopedWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[3].dstSet = MSdfComputeSets[ScopedSlot];
        ScopedWrites[3].dstBinding = 3;
        ScopedWrites[3].descriptorCount = 1;
        ScopedWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrites[3].pImageInfo = &ScopedHistoryInfo;
        ScopedWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[4].dstSet = MSdfComputeSets[ScopedSlot];
        ScopedWrites[4].dstBinding = 7;
        ScopedWrites[4].descriptorCount = 1;
        ScopedWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ScopedWrites[4].pBufferInfo = &ScopedAtlasTableInfo;
        ScopedWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[5].dstSet = MSdfComputeSets[ScopedSlot];
        ScopedWrites[5].dstBinding = 8;
        ScopedWrites[5].descriptorCount = 1;
        ScopedWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrites[5].pImageInfo = &ScopedAtlasInfo;
        ScopedApi.vkUpdateDescriptorSets(MDevice, static_cast<std::uint32_t>(ScopedWrites.size()), ScopedWrites.data(), 0,
                                        nullptr);
    }
}

bool VulkanRHI::CreateSdfComputeResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // r16f is the mask format; when the device cannot use it as a storage image,
    // the sprite shader keeps its per-pixel fallback instead of failing startup.
    VkFormatProperties ScopedFormat{};
    ScopedApi.vkGetPhysicalDeviceFormatProperties(MPhysical, VK_FORMAT_R16_SFLOAT, &ScopedFormat);
    if ((ScopedFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
    {
        LogWarn("vulkan", "r16f storage images are unsupported; SDF shadows use the per-pixel fallback");
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 6> ScopedBindings{};
    ScopedBindings[0].binding = 0;
    ScopedBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[0].descriptorCount = 1;
    ScopedBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedBindings[1].binding = 1;
    ScopedBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedBindings[1].descriptorCount = 1;
    ScopedBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedBindings[2].binding = 2;
    ScopedBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ScopedBindings[2].descriptorCount = 1;
    ScopedBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedBindings[3].binding = 3;
    ScopedBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[3].descriptorCount = 1;
    ScopedBindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    // Sparse atlas: the same table and tiles the fragment path uses.
    ScopedBindings[4].binding = 7;
    ScopedBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedBindings[4].descriptorCount = 1;
    ScopedBindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedBindings[5].binding = 8;
    ScopedBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[5].descriptorCount = 1;
    ScopedBindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.bindingCount = static_cast<std::uint32_t>(ScopedBindings.size());
    ScopedLayoutInfo.pBindings = ScopedBindings.data();
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedLayoutInfo, nullptr, &MSdfComputeSetLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute set layout failed");
        return false;
    }

    std::array<VkDescriptorPoolSize, 4> ScopedPoolSizes{};
    ScopedPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedPoolSizes[0].descriptorCount = SdfViewSlots * 3u;
    ScopedPoolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedPoolSizes[1].descriptorCount = SdfViewSlots;
    ScopedPoolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ScopedPoolSizes[2].descriptorCount = SdfViewSlots;
    ScopedPoolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedPoolSizes[3].descriptorCount = SdfViewSlots;
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = SdfViewSlots;
    ScopedPoolInfo.poolSizeCount = static_cast<std::uint32_t>(ScopedPoolSizes.size());
    ScopedPoolInfo.pPoolSizes = ScopedPoolSizes.data();
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MSdfComputePool) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute pool failed");
        return false;
    }
    std::array<VkDescriptorSetLayout, SdfViewSlots> ScopedLayouts{};
    ScopedLayouts.fill(MSdfComputeSetLayout);
    VkDescriptorSetAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocate.descriptorPool = MSdfComputePool;
    ScopedAllocate.descriptorSetCount = SdfViewSlots;
    ScopedAllocate.pSetLayouts = ScopedLayouts.data();
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, MSdfComputeSets.data()) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute sets failed");
        return false;
    }
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < SdfViewSlots; ++ScopedSlot)
    {
        if (!CreateBuffer(kSdfShadowBlockBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &MSdfComputeBuffers[ScopedSlot], &MSdfComputeMemories[ScopedSlot]))
        {
            LogError("vulkan", "SDF compute block buffer failed");
            return false;
        }
        if (ScopedApi.vkMapMemory(MDevice, MSdfComputeMemories[ScopedSlot], 0, kSdfShadowBlockBytes, 0,
                                  &MSdfComputeMapped[ScopedSlot]) != VK_SUCCESS)
        {
            MSdfComputeMapped[ScopedSlot] = nullptr;
            return false;
        }
    }
    UpdateSdfComputeSets();

    VkPipelineLayoutCreateInfo ScopedPipelineLayout{};
    ScopedPipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedPipelineLayout.setLayoutCount = 1;
    ScopedPipelineLayout.pSetLayouts = &MSdfComputeSetLayout;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedPipelineLayout, nullptr, &MSdfComputeLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute pipeline layout failed");
        return false;
    }
    VkShaderModule ScopedModule = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ScopedModuleInfo{};
    ScopedModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ScopedModuleInfo.codeSize = Shaders::sdf_shadow.size() * sizeof(std::uint32_t);
    ScopedModuleInfo.pCode = Shaders::sdf_shadow.data();
    if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedModuleInfo, nullptr, &ScopedModule) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute shader module failed");
        return false;
    }
    VkComputePipelineCreateInfo ScopedPipelineInfo{};
    ScopedPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ScopedPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedPipelineInfo.stage.module = ScopedModule;
    ScopedPipelineInfo.stage.pName = "main";
    ScopedPipelineInfo.layout = MSdfComputeLayout;
    const VkResult ScopedResult = ScopedApi.vkCreateComputePipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo,
                                                                     nullptr, &MSdfComputePipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedModule, nullptr);
    if (ScopedResult != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute pipeline failed: {}", static_cast<int>(ScopedResult));
        return false;
    }
    return true;
}

void VulkanRHI::DestroySdfShadowResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    LogInfo("vulkan", "EnsureSdfMask enter: tableView={} pagesView={}",
            static_cast<const void*>(MVtFallbackTableView), static_cast<const void*>(MVtFallbackPagesView));
    DestroySdfMaskResources();
    if (MSdfComputePipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MSdfComputePipeline, nullptr);
        MSdfComputePipeline = VK_NULL_HANDLE;
    }
    if (MSdfComputeLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipelineLayout(MDevice, MSdfComputeLayout, nullptr);
        MSdfComputeLayout = VK_NULL_HANDLE;
    }
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < SdfViewSlots; ++ScopedSlot)
    {
        if (MSdfComputeMapped[ScopedSlot] != nullptr && MSdfComputeMemories[ScopedSlot] != VK_NULL_HANDLE)
        {
            ScopedApi.vkUnmapMemory(MDevice, MSdfComputeMemories[ScopedSlot]);
            MSdfComputeMapped[ScopedSlot] = nullptr;
        }
        if (MSdfComputeBuffers[ScopedSlot] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MSdfComputeBuffers[ScopedSlot], nullptr);
            MSdfComputeBuffers[ScopedSlot] = VK_NULL_HANDLE;
        }
        if (MSdfComputeMemories[ScopedSlot] != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MSdfComputeMemories[ScopedSlot], nullptr);
            MSdfComputeMemories[ScopedSlot] = VK_NULL_HANDLE;
        }
    }
    if (MSdfComputePool != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorPool(MDevice, MSdfComputePool, nullptr);
        MSdfComputePool = VK_NULL_HANDLE;
    }
    if (MSdfComputeSetLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MSdfComputeSetLayout, nullptr);
        MSdfComputeSetLayout = VK_NULL_HANDLE;
    }
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < kMaxFramesInFlight; ++ScopedFrame)
    {
        if (MSdfFieldStagingMapped[ScopedFrame] != nullptr && MSdfFieldStagingMemories[ScopedFrame] != VK_NULL_HANDLE)
        {
            ScopedApi.vkUnmapMemory(MDevice, MSdfFieldStagingMemories[ScopedFrame]);
            MSdfFieldStagingMapped[ScopedFrame] = nullptr;
        }
        if (MSdfFieldStagingBuffers[ScopedFrame] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MSdfFieldStagingBuffers[ScopedFrame], nullptr);
            MSdfFieldStagingBuffers[ScopedFrame] = VK_NULL_HANDLE;
        }
        if (MSdfFieldStagingMemories[ScopedFrame] != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MSdfFieldStagingMemories[ScopedFrame], nullptr);
            MSdfFieldStagingMemories[ScopedFrame] = VK_NULL_HANDLE;
        }
    }
    if (MSdfBlockMapped != nullptr && MSdfBlockMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkUnmapMemory(MDevice, MSdfBlockMemory);
        MSdfBlockMapped = nullptr;
    }
    if (MSdfBlockBuffer != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyBuffer(MDevice, MSdfBlockBuffer, nullptr);
        MSdfBlockBuffer = VK_NULL_HANDLE;
    }
    if (MSdfBlockMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MSdfBlockMemory, nullptr);
        MSdfBlockMemory = VK_NULL_HANDLE;
    }
    if (MSdfFieldSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MSdfFieldSampler, nullptr);
        MSdfFieldSampler = VK_NULL_HANDLE;
    }
    if (MSdfFieldView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MSdfFieldView, nullptr);
        MSdfFieldView = VK_NULL_HANDLE;
    }
    if (MSdfFieldImage != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MSdfFieldImage, nullptr);
        MSdfFieldImage = VK_NULL_HANDLE;
    }
    if (MSdfFieldMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MSdfFieldMemory, nullptr);
        MSdfFieldMemory = VK_NULL_HANDLE;
    }
    MSdfFieldInitialized = false;
    MSdfFieldUploadPending = false;
    // Sparse atlas resources.
    if (MSdfTableMapped != nullptr)
    {
        ScopedApi.vkUnmapMemory(MDevice, MSdfTableMemory);
        MSdfTableMapped = nullptr;
    }
    if (MSdfTableBuffer != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyBuffer(MDevice, MSdfTableBuffer, nullptr);
        MSdfTableBuffer = VK_NULL_HANDLE;
    }
    if (MSdfTableMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MSdfTableMemory, nullptr);
        MSdfTableMemory = VK_NULL_HANDLE;
    }
    if (MSdfAtlasSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MSdfAtlasSampler, nullptr);
        MSdfAtlasSampler = VK_NULL_HANDLE;
    }
    if (MSdfAtlasView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MSdfAtlasView, nullptr);
        MSdfAtlasView = VK_NULL_HANDLE;
    }
    if (MSdfAtlasImage != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MSdfAtlasImage, nullptr);
        MSdfAtlasImage = VK_NULL_HANDLE;
    }
    if (MSdfAtlasMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MSdfAtlasMemory, nullptr);
        MSdfAtlasMemory = VK_NULL_HANDLE;
    }
}

bool VulkanRHI::CreateGiResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    for (GiFrame& ScopedFrame : MGiFrames)
    {
        if (!CreateBuffer(static_cast<VkDeviceSize>(kMaxGiProbes) * sizeof(GpuGiProbe),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &ScopedFrame.ProbeBuffer, &ScopedFrame.ProbeMemory) ||
            !CreateBuffer(sizeof(GpuGiVolumeHeader), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &ScopedFrame.HeaderBuffer, &ScopedFrame.HeaderMemory))
        {
            return false;
        }
        if (ScopedApi.vkMapMemory(MDevice, ScopedFrame.ProbeMemory, 0, VK_WHOLE_SIZE, 0,
                                  &ScopedFrame.ProbeMapped) != VK_SUCCESS ||
            ScopedApi.vkMapMemory(MDevice, ScopedFrame.HeaderMemory, 0, VK_WHOLE_SIZE, 0,
                                  &ScopedFrame.HeaderMapped) != VK_SUCCESS)
        {
            return false;
        }
        std::memset(ScopedFrame.ProbeMapped, 0, static_cast<std::size_t>(kMaxGiProbes) * sizeof(GpuGiProbe));
        std::memset(ScopedFrame.HeaderMapped, 0, sizeof(GpuGiVolumeHeader));
    }
    return true;
}

void VulkanRHI::DestroyGiResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    for (GiFrame& ScopedFrame : MGiFrames)
    {
        if (ScopedFrame.ProbeBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, ScopedFrame.ProbeBuffer, nullptr);
            ScopedFrame.ProbeBuffer = VK_NULL_HANDLE;
        }
        if (ScopedFrame.ProbeMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedFrame.ProbeMemory, nullptr);
            ScopedFrame.ProbeMemory = VK_NULL_HANDLE;
        }
        if (ScopedFrame.HeaderBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, ScopedFrame.HeaderBuffer, nullptr);
            ScopedFrame.HeaderBuffer = VK_NULL_HANDLE;
        }
        if (ScopedFrame.HeaderMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedFrame.HeaderMemory, nullptr);
            ScopedFrame.HeaderMemory = VK_NULL_HANDLE;
        }
        ScopedFrame.ProbeMapped = nullptr;
        ScopedFrame.HeaderMapped = nullptr;
    }
}

bool VulkanRHI::CreateClusterResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    std::array<VkDescriptorSetLayoutBinding, 5> ScopedBindings{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 3u; ++ScopedIndex)
    {
        ScopedBindings[ScopedIndex].binding = ScopedIndex;
        ScopedBindings[ScopedIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ScopedBindings[ScopedIndex].descriptorCount = 1;
        ScopedBindings[ScopedIndex].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    ScopedBindings[3].binding = 3;
    ScopedBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedBindings[3].descriptorCount = 1;
    ScopedBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[4].binding = 4;
    ScopedBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ScopedBindings[4].descriptorCount = 1;
    ScopedBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo ScopedSetInfo{};
    ScopedSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetInfo.bindingCount = static_cast<std::uint32_t>(ScopedBindings.size());
    ScopedSetInfo.pBindings = ScopedBindings.data();
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetInfo, nullptr, &MClusterSetLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "cluster descriptor set layout failed");
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> ScopedPoolSizes{};
    ScopedPoolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedPoolSizes[0].descriptorCount = 4u * kMaxFramesInFlight;
    ScopedPoolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ScopedPoolSizes[1].descriptorCount = kMaxFramesInFlight;
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = kMaxFramesInFlight;
    ScopedPoolInfo.poolSizeCount = static_cast<std::uint32_t>(ScopedPoolSizes.size());
    ScopedPoolInfo.pPoolSizes = ScopedPoolSizes.data();
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MClusterPool) != VK_SUCCESS)
    {
        LogError("vulkan", "cluster descriptor pool failed");
        return false;
    }

    VkPushConstantRange ScopedRange{};
    ScopedRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedRange.offset = 0;
    ScopedRange.size = sizeof(ClusterGridConstants);
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MClusterSetLayout;
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MClusterLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "cluster pipeline layout failed");
        return false;
    }

    const auto ScopedCreatePipeline = [this, &ScopedApi](const auto& InCode, VkPipeline* OutPipeline)
    {
        VkShaderModule ScopedModule = VK_NULL_HANDLE;
        VkShaderModuleCreateInfo ScopedModuleInfo{};
        ScopedModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ScopedModuleInfo.codeSize = InCode.size() * sizeof(std::uint32_t);
        ScopedModuleInfo.pCode = InCode.data();
        if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedModuleInfo, nullptr, &ScopedModule) != VK_SUCCESS)
        {
            return false;
        }
        VkComputePipelineCreateInfo ScopedPipelineInfo{};
        ScopedPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ScopedPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ScopedPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ScopedPipelineInfo.stage.module = ScopedModule;
        ScopedPipelineInfo.stage.pName = "main";
        ScopedPipelineInfo.layout = MClusterLayout;
        const VkResult ScopedResult =
            ScopedApi.vkCreateComputePipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo, nullptr, OutPipeline);
        ScopedApi.vkDestroyShaderModule(MDevice, ScopedModule, nullptr);
        return ScopedResult == VK_SUCCESS;
    };
    if (!ScopedCreatePipeline(Shaders::cluster_count, &MClusterCountPipeline) ||
        !ScopedCreatePipeline(Shaders::cluster_prefix, &MClusterPrefixPipeline) ||
        !ScopedCreatePipeline(Shaders::cluster_fill, &MClusterFillPipeline))
    {
        LogError("vulkan", "cluster compute pipeline failed");
        return false;
    }

    for (std::uint32_t ScopedFrame = 0; ScopedFrame < kMaxFramesInFlight; ++ScopedFrame)
    {
        ClusterFrame& ScopedTarget = MClusterFrames[ScopedFrame];
        if (!CreateBuffer(static_cast<VkDeviceSize>(kClusterMaxLights) * sizeof(GpuLight),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &ScopedTarget.LightBuffer, &ScopedTarget.LightMemory) ||
            !CreateBuffer(static_cast<VkDeviceSize>(kClusterMaxCount) * sizeof(ClusterHeader),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &ScopedTarget.HeaderBuffer, &ScopedTarget.HeaderMemory) ||
            !CreateBuffer(static_cast<VkDeviceSize>(kClusterMaxIndices) * sizeof(std::uint32_t),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &ScopedTarget.IndexBuffer, &ScopedTarget.IndexMemory))
        {
            LogError("vulkan", "cluster buffers failed");
            return false;
        }
        if (ScopedApi.vkMapMemory(MDevice, ScopedTarget.LightMemory, 0, VK_WHOLE_SIZE, 0,
                                  &ScopedTarget.LightMapped) != VK_SUCCESS)
        {
            LogError("vulkan", "cluster light map failed");
            return false;
        }

        VkDescriptorSetAllocateInfo ScopedAllocateInfo{};
        ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ScopedAllocateInfo.descriptorPool = MClusterPool;
        ScopedAllocateInfo.descriptorSetCount = 1;
        ScopedAllocateInfo.pSetLayouts = &MClusterSetLayout;
        if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocateInfo, &ScopedTarget.Set) != VK_SUCCESS)
        {
            LogError("vulkan", "cluster descriptor set failed");
            return false;
        }
        std::array<VkDescriptorBufferInfo, 5> ScopedBufferInfos{};
        ScopedBufferInfos[0] = VkDescriptorBufferInfo{ScopedTarget.LightBuffer, 0, VK_WHOLE_SIZE};
        ScopedBufferInfos[1] = VkDescriptorBufferInfo{ScopedTarget.HeaderBuffer, 0, VK_WHOLE_SIZE};
        ScopedBufferInfos[2] = VkDescriptorBufferInfo{ScopedTarget.IndexBuffer, 0, VK_WHOLE_SIZE};
        ScopedBufferInfos[3] = VkDescriptorBufferInfo{MGiFrames[ScopedFrame].ProbeBuffer, 0, VK_WHOLE_SIZE};
        ScopedBufferInfos[4] = VkDescriptorBufferInfo{MGiFrames[ScopedFrame].HeaderBuffer, 0, VK_WHOLE_SIZE};
        std::array<VkWriteDescriptorSet, 5> ScopedWrites{};
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedWrites.size(); ++ScopedIndex)
        {
            ScopedWrites[ScopedIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ScopedWrites[ScopedIndex].dstSet = ScopedTarget.Set;
            ScopedWrites[ScopedIndex].dstBinding = ScopedIndex;
            ScopedWrites[ScopedIndex].descriptorCount = 1;
            ScopedWrites[ScopedIndex].descriptorType =
                ScopedIndex == 4u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ScopedWrites[ScopedIndex].pBufferInfo = &ScopedBufferInfos[ScopedIndex];
        }
        ScopedApi.vkUpdateDescriptorSets(MDevice, static_cast<std::uint32_t>(ScopedWrites.size()),
                                         ScopedWrites.data(), 0, nullptr);
    }
    MClusterReady = true;
    MInfo.clusteredLighting = true;
    return true;
}

bool VulkanRHI::CreateClusterFallback()
{
    if (MClusterReady)
    {
        return true;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MClusterSetLayout == VK_NULL_HANDLE)
    {
        std::array<VkDescriptorSetLayoutBinding, 5> ScopedBindings{};
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 3u; ++ScopedIndex)
        {
            ScopedBindings[ScopedIndex].binding = ScopedIndex;
            ScopedBindings[ScopedIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ScopedBindings[ScopedIndex].descriptorCount = 1;
            ScopedBindings[ScopedIndex].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        ScopedBindings[3].binding = 3;
        ScopedBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ScopedBindings[3].descriptorCount = 1;
        ScopedBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        ScopedBindings[4].binding = 4;
        ScopedBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ScopedBindings[4].descriptorCount = 1;
        ScopedBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo ScopedSetInfo{};
        ScopedSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ScopedSetInfo.bindingCount = static_cast<std::uint32_t>(ScopedBindings.size());
        ScopedSetInfo.pBindings = ScopedBindings.data();
        if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetInfo, nullptr, &MClusterSetLayout) != VK_SUCCESS)
        {
            return false;
        }
    }
    if (MClusterPool == VK_NULL_HANDLE)
    {
        std::array<VkDescriptorPoolSize, 2> ScopedPoolSizes{};
        ScopedPoolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
        ScopedPoolSizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        VkDescriptorPoolCreateInfo ScopedPoolInfo{};
        ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ScopedPoolInfo.maxSets = 2;
        ScopedPoolInfo.poolSizeCount = static_cast<std::uint32_t>(ScopedPoolSizes.size());
        ScopedPoolInfo.pPoolSizes = ScopedPoolSizes.data();
        if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MClusterPool) != VK_SUCCESS)
        {
            return false;
        }
    }
    constexpr std::array<VkDeviceSize, 5> ScopedSizes{sizeof(GpuLight), sizeof(ClusterHeader), sizeof(std::uint32_t),
                                                      sizeof(GpuGiProbe), sizeof(GpuGiVolumeHeader)};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSizes.size(); ++ScopedIndex)
    {
        const VkBufferUsageFlags ScopedUsage =
            ScopedIndex == 4u ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (!CreateBuffer(ScopedSizes[ScopedIndex], ScopedUsage,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &MClusterFallbackBuffers[ScopedIndex], &MClusterFallbackMemories[ScopedIndex]))
        {
            return false;
        }
        void* ScopedMapped = nullptr;
        if (ScopedApi.vkMapMemory(MDevice, MClusterFallbackMemories[ScopedIndex], 0, VK_WHOLE_SIZE, 0,
                                  &ScopedMapped) != VK_SUCCESS)
        {
            return false;
        }
        std::memset(ScopedMapped, 0, static_cast<std::size_t>(ScopedSizes[ScopedIndex]));
        ScopedApi.vkUnmapMemory(MDevice, MClusterFallbackMemories[ScopedIndex]);
    }
    VkDescriptorSetAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocate.descriptorPool = MClusterPool;
    ScopedAllocate.descriptorSetCount = 1;
    ScopedAllocate.pSetLayouts = &MClusterSetLayout;
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, &MClusterFallbackSet) != VK_SUCCESS)
    {
        return false;
    }
    std::array<VkDescriptorBufferInfo, 5> ScopedInfo{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 5; ++ScopedIndex)
    {
        ScopedInfo[ScopedIndex] = {MClusterFallbackBuffers[ScopedIndex], 0, VK_WHOLE_SIZE};
    }
    std::array<VkWriteDescriptorSet, 5> ScopedWrites{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 5; ++ScopedIndex)
    {
        ScopedWrites[ScopedIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[ScopedIndex].dstSet = MClusterFallbackSet;
        ScopedWrites[ScopedIndex].dstBinding = ScopedIndex;
        ScopedWrites[ScopedIndex].descriptorCount = 1;
        ScopedWrites[ScopedIndex].descriptorType =
            ScopedIndex == 4u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ScopedWrites[ScopedIndex].pBufferInfo = &ScopedInfo[ScopedIndex];
    }
    ScopedApi.vkUpdateDescriptorSets(MDevice, 5, ScopedWrites.data(), 0, nullptr);
    MClusterFallbackReady = true;
    return true;
}

void VulkanRHI::DestroyClusterFallback() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 5; ++ScopedIndex)
    {
        if (MClusterFallbackBuffers[ScopedIndex] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MClusterFallbackBuffers[ScopedIndex], nullptr);
            MClusterFallbackBuffers[ScopedIndex] = VK_NULL_HANDLE;
        }
        if (MClusterFallbackMemories[ScopedIndex] != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MClusterFallbackMemories[ScopedIndex], nullptr);
            MClusterFallbackMemories[ScopedIndex] = VK_NULL_HANDLE;
        }
    }
    MClusterFallbackSet = VK_NULL_HANDLE;
    MClusterFallbackReady = false;
}

void VulkanRHI::DestroyClusterResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    MClusterReady = false;
    MInfo.clusteredLighting = false;
    for (ClusterFrame& ScopedFrame : MClusterFrames)
    {
        if (ScopedFrame.LightMapped != nullptr && ScopedFrame.LightMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkUnmapMemory(MDevice, ScopedFrame.LightMemory);
            ScopedFrame.LightMapped = nullptr;
        }
        if (ScopedFrame.LightBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, ScopedFrame.LightBuffer, nullptr);
            ScopedFrame.LightBuffer = VK_NULL_HANDLE;
        }
        if (ScopedFrame.LightMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedFrame.LightMemory, nullptr);
            ScopedFrame.LightMemory = VK_NULL_HANDLE;
        }
        if (ScopedFrame.HeaderBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, ScopedFrame.HeaderBuffer, nullptr);
            ScopedFrame.HeaderBuffer = VK_NULL_HANDLE;
        }
        if (ScopedFrame.HeaderMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedFrame.HeaderMemory, nullptr);
            ScopedFrame.HeaderMemory = VK_NULL_HANDLE;
        }
        if (ScopedFrame.IndexBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, ScopedFrame.IndexBuffer, nullptr);
            ScopedFrame.IndexBuffer = VK_NULL_HANDLE;
        }
        if (ScopedFrame.IndexMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedFrame.IndexMemory, nullptr);
            ScopedFrame.IndexMemory = VK_NULL_HANDLE;
        }
        ScopedFrame.Set = VK_NULL_HANDLE;
    }
    if (MClusterFillPipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MClusterFillPipeline, nullptr);
        MClusterFillPipeline = VK_NULL_HANDLE;
    }
    if (MClusterPrefixPipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MClusterPrefixPipeline, nullptr);
        MClusterPrefixPipeline = VK_NULL_HANDLE;
    }
    if (MClusterCountPipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MClusterCountPipeline, nullptr);
        MClusterCountPipeline = VK_NULL_HANDLE;
    }
    if (MClusterLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipelineLayout(MDevice, MClusterLayout, nullptr);
        MClusterLayout = VK_NULL_HANDLE;
    }
    if (MClusterPool != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorPool(MDevice, MClusterPool, nullptr);
        MClusterPool = VK_NULL_HANDLE;
    }
    if (MClusterSetLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MClusterSetLayout, nullptr);
        MClusterSetLayout = VK_NULL_HANDLE;
    }
}

}
#endif
