// FemboyEngine2D - Vulkan backend definitions.
#include "VulkanRHIInternal.hpp"

#if defined(FBY_HAS_VULKAN_HEADERS)
namespace Fby::Rhi::Impl
{
bool VulkanRHI::CreateUploadArena(std::uint32_t InBytes)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    MUploadCapacity = InBytes == 0 ? (std::size_t{8} << 20) : InBytes;
    if (!CreateBuffer(static_cast<VkDeviceSize>(MUploadCapacity), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &MUploadBuffer,
                      &MUploadMemory))
    {
        LogError("vulkan", "texture upload arena creation failed ({} bytes)", MUploadCapacity);
        return false;
    }
    if (ScopedApi.vkMapMemory(MDevice, MUploadMemory, 0, MUploadCapacity, 0, &MUploadMapped) != VK_SUCCESS)
    {
        LogError("vulkan", "texture upload arena map failed");
        return false;
    }

    VkFenceCreateInfo ScopedFenceInfo{};
    ScopedFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    ScopedFenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (ScopedApi.vkCreateFence(MDevice, &ScopedFenceInfo, nullptr, &MUploadFence) != VK_SUCCESS)
    {
        LogError("vulkan", "texture upload fence creation failed");
        return false;
    }
    VkCommandBufferAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ScopedAllocateInfo.commandPool = MCommandPool;
    ScopedAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ScopedAllocateInfo.commandBufferCount = 1;
    if (ScopedApi.vkAllocateCommandBuffers(MDevice, &ScopedAllocateInfo, &MUploadCommandBuffer) != VK_SUCCESS)
    {
        LogError("vulkan", "texture upload command buffer creation failed");
        return false;
    }
    return true;
}

VkPipeline VulkanRHI::ResolveMaterialPipeline(std::uint32_t InId, bool InOffscreen, bool InSrgb) const noexcept
{
    if (InId == kDefaultMaterialPipeline || InId >= MMaterialPipelines.size() || MMaterialPipelines[InId] == nullptr ||
        !MMaterialPipelines[InId]->alive)
    {
        return VK_NULL_HANDLE;
    }
    if (InOffscreen && InSrgb && MMaterialPipelines[InId]->srgbPass != VK_NULL_HANDLE)
    {
        return MMaterialPipelines[InId]->srgbPass;
    }
    return InOffscreen ? MMaterialPipelines[InId]->offscreenPass : MMaterialPipelines[InId]->mainPass;
}

ShaderPipelineHandle VulkanRHI::CreateShaderPipeline(const ShaderPipelineDesc& InDesc)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE || MOffscreenRenderPass == VK_NULL_HANDLE)
    {
        LogError("vulkan", "CreateShaderPipeline before device initialisation");
        return {};
    }
    if (InDesc.vertexSpirv.empty() && InDesc.fragmentSpirv.empty())
    {
        LogError("vulkan", "CreateShaderPipeline: both stages are empty");
        return {};
    }
    if ((!InDesc.vertexSpirv.empty() && InDesc.vertexSpirv.size() % sizeof(std::uint32_t) != 0) ||
        (!InDesc.fragmentSpirv.empty() && InDesc.fragmentSpirv.size() % sizeof(std::uint32_t) != 0))
    {
        LogError("vulkan", "CreateShaderPipeline: SPIR-V size is not a multiple of 4");
        return {};
    }

    const bool ScopedSpriteDomain = InDesc.domain == ShaderPipelineDomain::Sprite;
    if (ScopedSpriteDomain && !InDesc.vertexSpirv.empty())
    {
        LogError("vulkan", "CreateShaderPipeline: a sprite material pipeline always uses the engine quad vertex "
                           "shader");
        return {};
    }
    const void* ScopedDefaultVertex = ScopedSpriteDomain ? static_cast<const void*>(Shaders::quad_vert.data())
                                                         : static_cast<const void*>(Shaders::mesh_vert.data());
    const std::size_t ScopedDefaultVertexBytes = ScopedSpriteDomain ? Shaders::quad_vert.size() * sizeof(std::uint32_t)
                                                                    : Shaders::mesh_vert.size() * sizeof(std::uint32_t);

    const void* ScopedVertexData =
        InDesc.vertexSpirv.empty() ? ScopedDefaultVertex : static_cast<const void*>(InDesc.vertexSpirv.data());
    const std::size_t ScopedVertexBytes =
        InDesc.vertexSpirv.empty() ? ScopedDefaultVertexBytes : InDesc.vertexSpirv.size();
    const void* ScopedFragmentData = InDesc.fragmentSpirv.empty()
                                         ? static_cast<const void*>(Shaders::mesh_frag.data())
                                         : static_cast<const void*>(InDesc.fragmentSpirv.data());
    const std::size_t ScopedFragmentBytes =
        InDesc.fragmentSpirv.empty() ? Shaders::mesh_frag.size() * sizeof(std::uint32_t) : InDesc.fragmentSpirv.size();
    // A sprite material pipeline has to share the layout the sprite draw already
    // binds (descriptor set and push constants), not the mesh layout.
    const VkPipelineLayout ScopedLayout = ScopedSpriteDomain ? MPipelineLayout : MMeshGraphicsLayout;

    // Slot 0 stays empty for the lifetime of the device: DrawBatch::pipeline == 0
    // means "the engine quad pipeline", so a material must never own that id.
    if (MMaterialPipelines.empty())
    {
        MMaterialPipelines.push_back(nullptr);
    }

    if (MMaterialPipelines.size() >= kMaxMaterialPipelines && MFreeMaterialPipelineIds.empty())
    {
        LogError("vulkan", "CreateShaderPipeline: all {} material slots are in use", kMaxMaterialPipelines);
        return {};
    }

    auto ScopedPipeline = std::make_unique<MaterialPipeline>();
    const bool ScopedHasSrgbPass = MSrgbOffscreenRenderPass != VK_NULL_HANDLE;
    if (!CreateGraphicsPipelineForPass(MRenderPass, ScopedLayout, ScopedVertexData, ScopedVertexBytes,
                                       ScopedFragmentData, ScopedFragmentBytes, &ScopedPipeline->mainPass) ||
        !CreateGraphicsPipelineForPass(MOffscreenRenderPass, ScopedLayout, ScopedVertexData, ScopedVertexBytes,
                                       ScopedFragmentData, ScopedFragmentBytes, &ScopedPipeline->offscreenPass) ||
        (ScopedHasSrgbPass &&
         !CreateGraphicsPipelineForPass(MSrgbOffscreenRenderPass, ScopedLayout, ScopedVertexData, ScopedVertexBytes,
                                        ScopedFragmentData, ScopedFragmentBytes, &ScopedPipeline->srgbPass)))
    {
        if (ScopedPipeline->mainPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline->mainPass, nullptr);
        }
        if (ScopedPipeline->offscreenPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline->offscreenPass, nullptr);
        }
        if (ScopedPipeline->srgbPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline->srgbPass, nullptr);
        }
        LogError("vulkan", "CreateShaderPipeline: pipeline creation failed for '{}'",
                 InDesc.debugName != nullptr ? InDesc.debugName : "material");
        return {};
    }
    ScopedPipeline->alive = true;

    std::uint32_t ScopedId = 0;
    if (!MFreeMaterialPipelineIds.empty())
    {
        ScopedId = MFreeMaterialPipelineIds.back();
        MFreeMaterialPipelineIds.pop_back();
        MMaterialPipelines[ScopedId] = std::move(ScopedPipeline);
    }
    else
    {
        ScopedId = static_cast<std::uint32_t>(MMaterialPipelines.size());
        MMaterialPipelines.push_back(std::move(ScopedPipeline));
    }
    LogInfo("vulkan", "material pipeline #{} compiled ({})", ScopedId,
            InDesc.debugName != nullptr ? InDesc.debugName : "material");
    return ShaderPipelineHandle{ScopedId};
}

void VulkanRHI::DestroyShaderPipeline(ShaderPipelineHandle InHandle)
{
    if (!InHandle.IsValid() || InHandle.id >= MMaterialPipelines.size() || MMaterialPipelines[InHandle.id] == nullptr)
    {
        return;
    }
    if (MDevice != VK_NULL_HANDLE)
    {
        Vk::Api& ScopedApi = Vk::GetApi();
        ScopedApi.vkDeviceWaitIdle(MDevice);
        MaterialPipeline& ScopedPipeline = *MMaterialPipelines[InHandle.id];
        if (ScopedPipeline.mainPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline.mainPass, nullptr);
        }
        if (ScopedPipeline.offscreenPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline.offscreenPass, nullptr);
        }
        if (ScopedPipeline.srgbPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline.srgbPass, nullptr);
        }
        ScopedPipeline = {};
    }
    MMaterialPipelines[InHandle.id].reset();
    MFreeMaterialPipelineIds.push_back(InHandle.id);
}

void VulkanRHI::DestroyMaterialPipelines() noexcept
{
    if (MDevice == VK_NULL_HANDLE)
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    for (std::unique_ptr<MaterialPipeline>& ScopedPipeline : MMaterialPipelines)
    {
        if (ScopedPipeline != nullptr)
        {
            if (ScopedPipeline->mainPass != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline->mainPass, nullptr);
            }
            if (ScopedPipeline->offscreenPass != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline->offscreenPass, nullptr);
            }
            if (ScopedPipeline->srgbPass != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline->srgbPass, nullptr);
            }
            ScopedPipeline.reset();
        }
    }
    MMaterialPipelines.clear();
    MFreeMaterialPipelineIds.clear();
}

TextureArray* VulkanRHI::FindTextureArray(std::uint32_t InId) noexcept
{
    if (InId == 0)
    {
        return &MAtlas;
    }
    return InId < MTextureArrays.size() ? MTextureArrays[InId].get() : nullptr;
}

VkDescriptorSet VulkanRHI::DescriptorFor(std::uint32_t InId) noexcept
{
    if (InId == 0 || InId == 0xFFFFFFFFu)
    {
        return MDescriptorSet;
    }
    const TextureArray* ScopedArray = FindTextureArray(InId);
    return ScopedArray != nullptr && ScopedArray->descriptor != VK_NULL_HANDLE ? ScopedArray->descriptor
                                                                               : MDescriptorSet;
}

void VulkanRHI::DestroyTextureArrayResources(TextureArray& InArray) noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (InArray.descriptor != VK_NULL_HANDLE && MDescriptorPool != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeDescriptorSets(MDevice, MDescriptorPool, 1, &InArray.descriptor);
        InArray.descriptor = VK_NULL_HANDLE;
    }
    if (InArray.sampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, InArray.sampler, nullptr);
        InArray.sampler = VK_NULL_HANDLE;
    }
    if (!InArray.borrowed)
    {
        if (InArray.view != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImageView(MDevice, InArray.view, nullptr);
            InArray.view = VK_NULL_HANDLE;
        }
        if (InArray.image != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImage(MDevice, InArray.image, nullptr);
            InArray.image = VK_NULL_HANDLE;
        }
        if (InArray.memory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, InArray.memory, nullptr);
            InArray.memory = VK_NULL_HANDLE;
        }
    }
}

TextureArrayHandle VulkanRHI::CreateTextureArrayFromRenderTarget(RenderTargetHandle InHandle)
{
    RenderTarget* ScopedTarget = FindRenderTarget(InHandle.id);
    if (ScopedTarget == nullptr || ScopedTarget->view == VK_NULL_HANDLE || MDescriptorPool == VK_NULL_HANDLE)
    {
        return {};
    }

    std::uint32_t ScopedId = 0;
    if (!MFreeTextureArrayIds.empty())
    {
        ScopedId = MFreeTextureArrayIds.back();
        MFreeTextureArrayIds.pop_back();
    }
    else
    {
        if (MTextureArrays.size() >= kMaxTextureArrays + 1)
        {
            return {};
        }
        ScopedId = static_cast<std::uint32_t>(MTextureArrays.size());
        MTextureArrays.push_back(nullptr);
    }

    auto ScopedArray = std::make_unique<TextureArray>();
    ScopedArray->borrowed = true;
    ScopedArray->image = ScopedTarget->image;
    ScopedArray->view = ScopedTarget->view;
    ScopedArray->layers = 1;
    ScopedArray->mipLevels = 1;
    ScopedArray->width = ScopedTarget->extent.width;
    ScopedArray->height = ScopedTarget->extent.height;
    ScopedArray->format = Fby::Assets::TextureFormat::Rgba8;
    ScopedArray->srgb = ScopedTarget->srgb;

    Vk::Api& ScopedApi = Vk::GetApi();
    VkSamplerCreateInfo ScopedSamplerInfo{};
    ScopedSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSamplerInfo.magFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.minFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &ScopedArray->sampler) != VK_SUCCESS)
    {
        MTextureArrays[ScopedId].reset();
        return {};
    }

    VkDescriptorSetAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocate.descriptorPool = MDescriptorPool;
    ScopedAllocate.descriptorSetCount = 1;
    ScopedAllocate.pSetLayouts = &MDescriptorLayout;
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, &ScopedArray->descriptor) != VK_SUCCESS)
    {
        DestroyTextureArrayResources(*ScopedArray);
        MTextureArrays[ScopedId].reset();
        return {};
    }

    VkDescriptorImageInfo ScopedImageDescriptor{};
    ScopedImageDescriptor.sampler = ScopedArray->sampler;
    ScopedImageDescriptor.imageView = ScopedArray->view;
    ScopedImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet ScopedWrite{};
    ScopedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrite.dstSet = ScopedArray->descriptor;
    ScopedWrite.dstBinding = 0;
    ScopedWrite.descriptorCount = 1;
    ScopedWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrite.pImageInfo = &ScopedImageDescriptor;
    ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedWrite, 0, nullptr);
    WriteSdfDescriptors(ScopedArray->descriptor);

    MTextureArrays[ScopedId] = std::move(ScopedArray);
    TextureArrayHandle ScopedHandle;
    ScopedHandle.id = ScopedId;
    return ScopedHandle;
}

TextureArrayHandle VulkanRHI::CreateTextureArray(const TextureArrayDesc& InDesc, std::span<const std::byte> InPixels)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        LogError("vulkan", "CreateTextureArray called before device creation");
        return {};
    }
    if (InDesc.size.width == 0 || InDesc.size.height == 0 || InDesc.layers == 0)
    {
        LogError("vulkan", "CreateTextureArray: invalid {}x{} x {} descriptor", InDesc.size.width, InDesc.size.height,
                 InDesc.layers);
        return {};
    }
    if (InDesc.mipLevels == 0 || InDesc.mipLevels > 16)
    {
        LogError("vulkan", "CreateTextureArray: invalid mip level count {}", InDesc.mipLevels);
        return {};
    }
    if (InDesc.mipLevels > Fby::Assets::MaxMipLevels(InDesc.size.width, InDesc.size.height, InDesc.format))
    {
        LogError("vulkan", "CreateTextureArray: {} mip levels is more than {}x{} allows", InDesc.mipLevels,
                 InDesc.size.width, InDesc.size.height);
        return {};
    }
    if (InDesc.layers > MInfo.maxTextureLayers)
    {
        LogError("vulkan", "CreateTextureArray: {} layers exceeds DeviceDesc::maxTextureLayers ({})", InDesc.layers,
                 MInfo.maxTextureLayers);
        return {};
    }

    const VkFormat ScopedFormat = ToVkFormat(InDesc.format, InDesc.srgb);
    if (ScopedFormat == VK_FORMAT_UNDEFINED)
    {
        LogError("vulkan", "CreateTextureArray: unknown cooked texture format");
        return {};
    }
    if (IsAstcFormat(InDesc.format) && !MAstcLdrSupported)
    {
        LogError("vulkan", "CreateTextureArray: {} needs ASTC LDR support, which this device does not expose",
                 Fby::Assets::TextureFormatName(InDesc.format));
        return {};
    }
    VkFormatProperties ScopedFormatProperties{};
    ScopedApi.vkGetPhysicalDeviceFormatProperties(MPhysical, ScopedFormat, &ScopedFormatProperties);
    if ((ScopedFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
    {
        LogError("vulkan", "CreateTextureArray: {} is not sampleable on this device",
                 Fby::Assets::TextureFormatName(InDesc.format));
        return {};
    }

    // Cooked payload layout: layer-major, mip-major. The cooker already wrote
    const std::uint64_t ScopedExpected = Fby::Assets::TextureByteSize(
        InDesc.format, InDesc.size.width, InDesc.size.height, InDesc.layers, InDesc.mipLevels);
    if (InPixels.size() != ScopedExpected)
    {
        LogError("vulkan", "CreateTextureArray: pixel buffer is {} bytes, expected {} for {}x{} x {} x {} mips ({})",
                 InPixels.size(), ScopedExpected, InDesc.size.width, InDesc.size.height, InDesc.layers,
                 InDesc.mipLevels, Fby::Assets::TextureFormatName(InDesc.format));
        return {};
    }

    std::uint32_t ScopedId = 0;
    if (!MFreeTextureArrayIds.empty())
    {
        ScopedId = MFreeTextureArrayIds.back();
        MFreeTextureArrayIds.pop_back();
    }
    else
    {
        if (MTextureArrays.size() >= kMaxTextureArrays + 1)
        {
            LogError("vulkan", "CreateTextureArray: all {} user slots are in use", kMaxTextureArrays);
            return {};
        }
        ScopedId = static_cast<std::uint32_t>(MTextureArrays.size());
        MTextureArrays.push_back(nullptr);
    }

    auto ScopedArray = std::make_unique<TextureArray>();
    ScopedArray->layers = InDesc.layers;
    ScopedArray->mipLevels = InDesc.mipLevels;
    ScopedArray->width = InDesc.size.width;
    ScopedArray->height = InDesc.size.height;
    ScopedArray->format = InDesc.format;
    ScopedArray->srgb = InDesc.srgb;

    const auto ScopedCleanup = [&]()
    {
        if (ScopedArray->sampler != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySampler(MDevice, ScopedArray->sampler, nullptr);
        }
        if (ScopedArray->view != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImageView(MDevice, ScopedArray->view, nullptr);
        }
        if (ScopedArray->descriptor != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeDescriptorSets(MDevice, MDescriptorPool, 1, &ScopedArray->descriptor);
        }
        if (ScopedArray->image != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImage(MDevice, ScopedArray->image, nullptr);
        }
        if (ScopedArray->memory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, ScopedArray->memory, nullptr);
        }
    };

    VkImageCreateInfo ScopedImageInfo{};
    ScopedImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedImageInfo.format = ScopedFormat;
    ScopedImageInfo.extent = {InDesc.size.width, InDesc.size.height, 1};
    ScopedImageInfo.mipLevels = InDesc.mipLevels;
    ScopedImageInfo.arrayLayers = InDesc.layers;
    ScopedImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ScopedImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ScopedImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedImageInfo, nullptr, &ScopedArray->image) != VK_SUCCESS)
    {
        LogError("vulkan", "CreateTextureArray: vkCreateImage failed");
        ScopedCleanup();
        return {};
    }

    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, ScopedArray->image, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocateInfo.allocationSize = ScopedRequirements.size;
    ScopedAllocateInfo.memoryTypeIndex =
        FindMemoryType(MMemoryProperties, ScopedRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ScopedAllocateInfo.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocateInfo, nullptr, &ScopedArray->memory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, ScopedArray->image, ScopedArray->memory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "CreateTextureArray: image memory allocation failed ({} bytes)", ScopedRequirements.size);
        ScopedCleanup();
        return {};
    }

    VkBuffer ScopedStaging = VK_NULL_HANDLE;
    VkDeviceMemory ScopedStagingMemory = VK_NULL_HANDLE;
    const bool ScopedStagingCreated =
        CreateBuffer(static_cast<VkDeviceSize>(InPixels.size()), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ScopedStaging,
                     &ScopedStagingMemory);
    if (!ScopedStagingCreated)
    {
        LogError("vulkan", "CreateTextureArray: staging buffer creation failed");
        ScopedCleanup();
        return {};
    }

    void* ScopedMapped = nullptr;
    if (ScopedApi.vkMapMemory(MDevice, ScopedStagingMemory, 0, InPixels.size(), 0, &ScopedMapped) != VK_SUCCESS)
    {
        ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
        ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);
        ScopedCleanup();
        return {};
    }
    std::memcpy(ScopedMapped, InPixels.data(), InPixels.size());
    ScopedApi.vkUnmapMemory(MDevice, ScopedStagingMemory);

    const VkCommandBuffer ScopedCommand = BeginOneShot();
    TransitionImage(ScopedCommand, ScopedArray->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    InDesc.layers, InDesc.mipLevels);
    // One copy per (layer, mip). The buffer offset is the cooked layer-major,
    std::uint64_t ScopedBufferOffset = 0;
    for (std::uint32_t ScopedLayer = 0; ScopedLayer < InDesc.layers; ++ScopedLayer)
    {
        for (std::uint32_t ScopedMip = 0; ScopedMip < InDesc.mipLevels; ++ScopedMip)
        {
            const std::uint32_t ScopedMipWidth = Fby::Assets::MipExtent(InDesc.size.width, ScopedMip);
            const std::uint32_t ScopedMipHeight = Fby::Assets::MipExtent(InDesc.size.height, ScopedMip);
            const std::uint64_t ScopedMipBytes =
                Fby::Assets::MipByteSize(InDesc.format, ScopedMipWidth, ScopedMipHeight);
            VkBufferImageCopy ScopedRegion{};
            ScopedRegion.bufferOffset = static_cast<VkDeviceSize>(ScopedBufferOffset);
            ScopedRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ScopedRegion.imageSubresource.mipLevel = ScopedMip;
            ScopedRegion.imageSubresource.baseArrayLayer = ScopedLayer;
            ScopedRegion.imageSubresource.layerCount = 1;
            ScopedRegion.imageExtent = {Fby::Assets::IsBlockCompressed(InDesc.format)
                                            ? Fby::Assets::BlockAlignedExtent(ScopedMipWidth, InDesc.format)
                                            : ScopedMipWidth,
                                        Fby::Assets::IsBlockCompressed(InDesc.format)
                                            ? Fby::Assets::BlockAlignedExtent(ScopedMipHeight, InDesc.format)
                                            : ScopedMipHeight,
                                        1};
            ScopedApi.vkCmdCopyBufferToImage(ScopedCommand, ScopedStaging, ScopedArray->image,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedRegion);
            ScopedBufferOffset += ScopedMipBytes;
        }
    }
    TransitionImage(ScopedCommand, ScopedArray->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, InDesc.layers, InDesc.mipLevels);
    EndOneShot(ScopedCommand);

    ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
    ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);

    VkImageViewCreateInfo ScopedViewInfo{};
    ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedViewInfo.image = ScopedArray->image;
    ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ScopedViewInfo.format = ScopedFormat;
    ScopedViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedViewInfo.subresourceRange.levelCount = InDesc.mipLevels;
    ScopedViewInfo.subresourceRange.layerCount = InDesc.layers;
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &ScopedArray->view) != VK_SUCCESS)
    {
        LogError("vulkan", "CreateTextureArray: image view creation failed");
        ScopedCleanup();
        return {};
    }

    VkSamplerCreateInfo ScopedSamplerInfo{};
    ScopedSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSamplerInfo.magFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.minFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.mipmapMode =
        InDesc.mipLevels > 1 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ScopedSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.maxLod = static_cast<float>(InDesc.mipLevels);
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &ScopedArray->sampler) != VK_SUCCESS)
    {
        LogError("vulkan", "CreateTextureArray: sampler creation failed");
        ScopedCleanup();
        return {};
    }

    VkDescriptorSetAllocateInfo ScopedDescriptorAllocate{};
    ScopedDescriptorAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedDescriptorAllocate.descriptorPool = MDescriptorPool;
    ScopedDescriptorAllocate.descriptorSetCount = 1;
    ScopedDescriptorAllocate.pSetLayouts = &MDescriptorLayout;
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedDescriptorAllocate, &ScopedArray->descriptor) != VK_SUCCESS)
    {
        LogError("vulkan", "CreateTextureArray: descriptor set allocation failed");
        ScopedArray->descriptor = VK_NULL_HANDLE;
        ScopedCleanup();
        return {};
    }

    VkDescriptorImageInfo ScopedImageDescriptor{};
    ScopedImageDescriptor.sampler = ScopedArray->sampler;
    ScopedImageDescriptor.imageView = ScopedArray->view;
    ScopedImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet ScopedWrite{};
    ScopedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrite.dstSet = ScopedArray->descriptor;
    ScopedWrite.dstBinding = 0;
    ScopedWrite.descriptorCount = 1;
    ScopedWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrite.pImageInfo = &ScopedImageDescriptor;
    ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedWrite, 0, nullptr);
    WriteSdfDescriptors(ScopedArray->descriptor);

    MTextureArrays[ScopedId] = std::move(ScopedArray);
    LogInfo("vulkan", "texture array #{}: {}x{} x {} layers, {}", ScopedId, InDesc.size.width, InDesc.size.height,
            InDesc.layers, InDesc.srgb ? "sRGB" : "linear");
    return TextureArrayHandle{ScopedId};
}

void VulkanRHI::DestroyTextureArray(TextureArrayHandle InHandle)
{
    if (!InHandle.IsValid())
    {
        return;
    }
    if (InHandle.id == 0)
    {
        LogWarn("vulkan", "DestroyTextureArray: the built-in atlas is owned by the device");
        return;
    }
    TextureArray* ScopedArray = FindTextureArray(InHandle.id);
    if (ScopedArray == nullptr)
    {
        return;
    }
    WaitIdle();
    DestroyTextureArrayResources(*ScopedArray);
    MTextureArrays[InHandle.id].reset();
    MFreeTextureArrayIds.push_back(InHandle.id);
    LogInfo("vulkan", "texture array #{} released", InHandle.id);
}

bool VulkanRHI::UpdateTextureArrayLayer(TextureArrayHandle InHandle, std::uint32_t InLayer, std::uint32_t InMipLevel,
                                        std::span<const std::byte> InData)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        return false;
    }
    TextureArray* ScopedArray = FindTextureArray(InHandle.id);
    if (ScopedArray == nullptr)
    {
        LogError("vulkan", "UpdateTextureArrayLayer: invalid handle #{}", InHandle.id);
        return false;
    }
    if (InLayer >= ScopedArray->layers || InMipLevel >= ScopedArray->mipLevels)
    {
        LogError("vulkan", "UpdateTextureArrayLayer: layer {} / mip {} out of range ({} / {})", InLayer, InMipLevel,
                 ScopedArray->layers, ScopedArray->mipLevels);
        return false;
    }
    const std::uint32_t ScopedMipWidth = Fby::Assets::MipExtent(ScopedArray->width, InMipLevel);
    const std::uint32_t ScopedMipHeight = Fby::Assets::MipExtent(ScopedArray->height, InMipLevel);
    const std::uint64_t ScopedExpected = Fby::Assets::MipByteSize(ScopedArray->format, ScopedMipWidth, ScopedMipHeight);
    if (InData.size() != ScopedExpected)
    {
        LogError("vulkan", "UpdateTextureArrayLayer: {} bytes supplied, expected {} for {}x{} {} mip {}", InData.size(),
                 ScopedExpected, ScopedMipWidth, ScopedMipHeight, Fby::Assets::TextureFormatName(ScopedArray->format),
                 InMipLevel);
        return false;
    }
    if (MUploadMapped == nullptr || ScopedExpected > MUploadCapacity)
    {
        LogError("vulkan", "UpdateTextureArrayLayer: upload arena is too small ({} needed, {} available)",
                 ScopedExpected, MUploadCapacity);
        return false;
    }

    // Reusing the staging arena waits only for the previous upload, not for the
    // whole device. The frame queue sees this submission before the next draw.
    if (!WaitUploadFence())
    {
        LogError("vulkan", "UpdateTextureArrayLayer: upload fence wait failed");
        return false;
    }
    std::memcpy(MUploadMapped, InData.data(), static_cast<std::size_t>(ScopedExpected));

    (void)ScopedApi.vkResetCommandBuffer(MUploadCommandBuffer, 0);
    VkCommandBufferBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ScopedBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ScopedApi.vkBeginCommandBuffer(MUploadCommandBuffer, &ScopedBeginInfo) != VK_SUCCESS)
    {
        return false;
    }
    TransitionImage(MUploadCommandBuffer, ScopedArray->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ScopedArray->layers, ScopedArray->mipLevels);
    VkBufferImageCopy ScopedRegion{};
    ScopedRegion.bufferOffset = 0;
    ScopedRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedRegion.imageSubresource.mipLevel = InMipLevel;
    ScopedRegion.imageSubresource.baseArrayLayer = InLayer;
    ScopedRegion.imageSubresource.layerCount = 1;
    ScopedRegion.imageExtent = {Fby::Assets::IsBlockCompressed(ScopedArray->format)
                                    ? Fby::Assets::BlockAlignedExtent(ScopedMipWidth, ScopedArray->format)
                                    : ScopedMipWidth,
                                Fby::Assets::IsBlockCompressed(ScopedArray->format)
                                    ? Fby::Assets::BlockAlignedExtent(ScopedMipHeight, ScopedArray->format)
                                    : ScopedMipHeight,
                                1};
    ScopedApi.vkCmdCopyBufferToImage(MUploadCommandBuffer, MUploadBuffer, ScopedArray->image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedRegion);
    TransitionImage(MUploadCommandBuffer, ScopedArray->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ScopedArray->layers, ScopedArray->mipLevels);
    if (!SubmitUpload(MUploadCommandBuffer))
    {
        LogError("vulkan", "UpdateTextureArrayLayer: upload submit failed");
        return false;
    }
    ++MStats.uploadedBytes;
    return true;
}

bool VulkanRHI::CaptureRenderTarget(RenderTargetHandle InHandle, std::span<std::byte> InRgba8, Extent2D* OutSize)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        return false;
    }
    RenderTarget* ScopedTarget = FindRenderTarget(InHandle.id);
    if (ScopedTarget == nullptr || ScopedTarget->image == VK_NULL_HANDLE)
    {
        return false;
    }
    if (ScopedTarget->format != VK_FORMAT_R8G8B8A8_UNORM && !IsSrgbFormat(ScopedTarget->format))
    {
        LogError("vulkan", "CaptureRenderTarget: unsupported target format");
        return false;
    }
    const std::size_t ScopedBytes =
        static_cast<std::size_t>(ScopedTarget->extent.width) * ScopedTarget->extent.height * 4;
    if (InRgba8.size() < ScopedBytes)
    {
        LogError("vulkan", "CaptureRenderTarget: buffer is {} bytes, need {}", InRgba8.size(), ScopedBytes);
        return false;
    }
    if (OutSize != nullptr)
    {
        *OutSize = ScopedTarget->extent;
    }

    VkBuffer ScopedStaging = VK_NULL_HANDLE;
    VkDeviceMemory ScopedStagingMemory = VK_NULL_HANDLE;
    if (!EnsureCaptureStaging(static_cast<VkDeviceSize>(ScopedBytes), &ScopedStaging, &ScopedStagingMemory))
    {
        LogError("vulkan", "CaptureRenderTarget: readback buffer creation failed");
        return false;
    }

    const VkCommandBuffer ScopedCommand = BeginOneShot();
    VkImageMemoryBarrier ScopedToTransfer{};
    ScopedToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedToTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ScopedToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedToTransfer.image = ScopedTarget->image;
    ScopedToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedToTransfer.subresourceRange.levelCount = 1;
    ScopedToTransfer.subresourceRange.layerCount = 1;
    ScopedToTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ScopedToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ScopedApi.vkCmdPipelineBarrier(ScopedCommand, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   0, 0, nullptr, 0, nullptr, 1, &ScopedToTransfer);

    VkBufferImageCopy ScopedRegion{};
    ScopedRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedRegion.imageSubresource.layerCount = 1;
    ScopedRegion.imageExtent = {ScopedTarget->extent.width, ScopedTarget->extent.height, 1};
    ScopedApi.vkCmdCopyImageToBuffer(ScopedCommand, ScopedTarget->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     ScopedStaging, 1, &ScopedRegion);

    ScopedToTransfer.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ScopedToTransfer.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedToTransfer.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ScopedToTransfer.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ScopedApi.vkCmdPipelineBarrier(ScopedCommand, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   0, 0, nullptr, 0, nullptr, 1, &ScopedToTransfer);
    EndOneShot(ScopedCommand);

    void* ScopedMapped = MCaptureStagingMapped;
    if (ScopedMapped != nullptr)
    {
        std::memcpy(InRgba8.data(), ScopedMapped, ScopedBytes);
    }
    return ScopedMapped != nullptr;
}

MeshHandle VulkanRHI::CreateMesh(const MeshDesc& InDesc)
{
    if (InDesc.vertices.empty() || InDesc.indices.empty())
    {
        LogError("vulkan", "CreateMesh '{}': empty geometry", InDesc.debugName != nullptr ? InDesc.debugName : "?");
        return {};
    }
    if (InDesc.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        InDesc.indices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        LogError("vulkan", "CreateMesh '{}': geometry exceeds the arena limit",
                 InDesc.debugName != nullptr ? InDesc.debugName : "?");
        return {};
    }

    const auto ScopedVertexCount = static_cast<std::uint32_t>(InDesc.vertices.size());
    const auto ScopedIndexCount = static_cast<std::uint32_t>(InDesc.indices.size());
    const std::uint32_t ScopedAlignedIndexCount = (ScopedIndexCount + 1u) & ~1u;
    std::uint32_t ScopedVertexOffset = 0;
    std::uint32_t ScopedIndexOffset = 0;
    if (!AllocateStaticRange(MStaticVertexFree, ScopedVertexCount, &ScopedVertexOffset))
    {
        LogError("vulkan", "CreateMesh '{}': static vertex arena exhausted ({} requested, {} capacity)",
                 InDesc.debugName != nullptr ? InDesc.debugName : "?", ScopedVertexCount, MStaticVertexCapacity);
        return {};
    }
    if (!AllocateStaticRange(MStaticIndexFree, ScopedAlignedIndexCount, &ScopedIndexOffset))
    {
        FreeStaticRange(MStaticVertexFree, ScopedVertexOffset, ScopedVertexCount);
        LogError("vulkan", "CreateMesh '{}': static index arena exhausted ({} requested, {} capacity)",
                 InDesc.debugName != nullptr ? InDesc.debugName : "?", ScopedIndexCount, MStaticIndexCapacity);
        return {};
    }

    // Convert once, on the CPU, to the GPU-facing layout. Static geometry has
    const VkDeviceSize ScopedVertexBytes = static_cast<VkDeviceSize>(ScopedVertexCount) * sizeof(SkinnedVertex);
    const VkDeviceSize ScopedStagedIndexBytes =
        static_cast<VkDeviceSize>(ScopedAlignedIndexCount) * sizeof(std::uint16_t);
    const VkDeviceSize ScopedStagingBytes = ScopedVertexBytes + ScopedStagedIndexBytes;

    VkBuffer ScopedStaging = VK_NULL_HANDLE;
    VkDeviceMemory ScopedStagingMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(ScopedStagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ScopedStaging,
                      &ScopedStagingMemory))
    {
        FreeStaticRange(MStaticVertexFree, ScopedVertexOffset, ScopedVertexCount);
        FreeStaticRange(MStaticIndexFree, ScopedIndexOffset, ScopedAlignedIndexCount);
        return {};
    }

    Vk::Api& ScopedApi = Vk::GetApi();
    void* ScopedMapped = nullptr;
    if (ScopedApi.vkMapMemory(MDevice, ScopedStagingMemory, 0, VK_WHOLE_SIZE, 0, &ScopedMapped) != VK_SUCCESS)
    {
        ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
        ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);
        FreeStaticRange(MStaticVertexFree, ScopedVertexOffset, ScopedVertexCount);
        FreeStaticRange(MStaticIndexFree, ScopedIndexOffset, ScopedAlignedIndexCount);
        return {};
    }
    auto* ScopedVertexData = static_cast<SkinnedVertex*>(ScopedMapped);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedVertexCount; ++ScopedI)
    {
        const MeshVertex& ScopedSource = InDesc.vertices[ScopedI];
        SkinnedVertex& ScopedTarget = ScopedVertexData[ScopedI];
        ScopedTarget.position = Vector4D{ScopedSource.position.X, ScopedSource.position.Y, 0.0f, 1.0f};
        ScopedTarget.uv = ScopedSource.uv;
        ScopedTarget.color = ScopedSource.color;
        ScopedTarget.padding = 0;
    }
    auto* ScopedIndexData = reinterpret_cast<std::uint16_t*>(static_cast<std::byte*>(ScopedMapped) + ScopedVertexBytes);
    std::memset(ScopedIndexData, 0, ScopedStagedIndexBytes);
    std::memcpy(ScopedIndexData, InDesc.indices.data(),
                static_cast<std::size_t>(ScopedIndexCount) * sizeof(std::uint16_t));
    ScopedApi.vkUnmapMemory(MDevice, ScopedStagingMemory);

    const VkCommandBuffer ScopedCommand = BeginOneShot();
    VkBufferCopy ScopedVertexCopy{};
    ScopedVertexCopy.dstOffset = static_cast<VkDeviceSize>(ScopedVertexOffset) * sizeof(SkinnedVertex);
    ScopedVertexCopy.size = ScopedVertexBytes;
    ScopedApi.vkCmdCopyBuffer(ScopedCommand, ScopedStaging, MStaticVertexBuffer, 1, &ScopedVertexCopy);
    VkBufferCopy ScopedIndexCopy{};
    ScopedIndexCopy.srcOffset = ScopedVertexBytes;
    ScopedIndexCopy.dstOffset = static_cast<VkDeviceSize>(ScopedIndexOffset) * sizeof(std::uint16_t);
    ScopedIndexCopy.size = ScopedStagedIndexBytes;
    ScopedApi.vkCmdCopyBuffer(ScopedCommand, ScopedStaging, MIndexPool, 1, &ScopedIndexCopy);
    EndOneShot(ScopedCommand);

    ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
    ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);

    std::uint32_t ScopedId = 0;
    if (!MFreeStaticMeshIds.empty())
    {
        ScopedId = MFreeStaticMeshIds.back();
        MFreeStaticMeshIds.pop_back();
    }
    else
    {
        ScopedId = static_cast<std::uint32_t>(MStaticMeshes.size());
        MStaticMeshes.emplace_back();
    }
    StaticMeshRecord& ScopedRecord = MStaticMeshes[ScopedId];
    ScopedRecord.vertexOffset = ScopedVertexOffset;
    ScopedRecord.vertexCount = ScopedVertexCount;
    ScopedRecord.indexOffset = ScopedIndexOffset;
    ScopedRecord.indexCount = ScopedIndexCount;
    ScopedRecord.alive = true;

    LogInfo("vulkan", "static mesh #{} '{}': {} verts @{}, {} indices @{} (device-local)", ScopedId,
            InDesc.debugName != nullptr ? InDesc.debugName : "?", ScopedVertexCount, ScopedVertexOffset,
            ScopedIndexCount, ScopedIndexOffset);
    return MeshHandle{ScopedId};
}

void VulkanRHI::DestroyMesh(MeshHandle InHandle)
{
    if (!InHandle.IsValid() || InHandle.id >= MStaticMeshes.size())
    {
        return;
    }
    StaticMeshRecord& ScopedRecord = MStaticMeshes[InHandle.id];
    if (!ScopedRecord.alive)
    {
        return;
    }
    // The handle may still be referenced by an in-flight command buffer.
    WaitIdle();
    FreeStaticRange(MStaticVertexFree, ScopedRecord.vertexOffset, ScopedRecord.vertexCount);
    FreeStaticRange(MStaticIndexFree, ScopedRecord.indexOffset, (ScopedRecord.indexCount + 1u) & ~1u);
    ScopedRecord = StaticMeshRecord{};
    MFreeStaticMeshIds.push_back(InHandle.id);
}

void VulkanRHI::DestroySwapchain()
{
    DestroyHiZResources();
    DestroyAccessibilitySwapchainResources();
    DestroyDepthPrepassFramebuffers();
    Vk::Api& ScopedApi = Vk::GetApi();
    for (VkFramebuffer ScopedFramebuffer : MFramebuffers)
    {
        ScopedApi.vkDestroyFramebuffer(MDevice, ScopedFramebuffer, nullptr);
    }
    MFramebuffers.clear();
    for (VkFramebuffer ScopedFramebuffer : MHudOverlayFramebuffers)
    {
        ScopedApi.vkDestroyFramebuffer(MDevice, ScopedFramebuffer, nullptr);
    }
    MHudOverlayFramebuffers.clear();
    for (std::size_t ScopedI = 0; ScopedI < MDrawDepthViews.size(); ++ScopedI)
    {
        DestroyDepthImage(&MDrawDepthImages[ScopedI], &MDrawDepthMemories[ScopedI], &MDrawDepthViews[ScopedI]);
    }
    if (MDepthSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MDepthSampler, nullptr);
        MDepthSampler = VK_NULL_HANDLE;
    }
    MDrawDepthImages.clear();
    MDrawDepthMemories.clear();
    MDrawDepthViews.clear();
    for (VkImageView ScopedView : MViews)
    {
        ScopedApi.vkDestroyImageView(MDevice, ScopedView, nullptr);
    }
    MViews.clear();
    for (const auto& [ScopedKey, ScopedLayerView] : MLayerViews)
    {
        (void)ScopedKey;
        ScopedApi.vkDestroyImageView(MDevice, ScopedLayerView, nullptr);
    }
    MLayerViews.clear();
    for (VkSemaphore ScopedSemaphore : MRenderFinished)
    {
        ScopedApi.vkDestroySemaphore(MDevice, ScopedSemaphore, nullptr);
    }
    MRenderFinished.clear();
    MImages.clear();
    if (MSwapchain != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySwapchainKHR(MDevice, MSwapchain, nullptr);
        MSwapchain = VK_NULL_HANDLE;
    }
}

bool VulkanRHI::RecreateSwapchain(Extent2D InSize)
{
    if (MDevice == VK_NULL_HANDLE)
    {
        return false;
    }
    WaitIdle();
    DestroySwapchain();
    if (InSize.width == 0 || InSize.height == 0)
    {
        return true;
    }
    const bool ScopedOk = CreateSwapchain(InSize) && CreateFramebuffers() && CreateDepthPrepassFramebuffers() &&
                          CreateSyncObjects() && CreateHiZResources();
    MHiZReady = ScopedOk && MDesc.enableHiZ;
    if (ScopedOk)
    {
        (void)CreateAccessibilitySwapchainResources();
    }
    return ScopedOk;
}

void VulkanRHI::Resize(Extent2D InSize)
{
    MDesc.size = InSize;
    (void)RecreateSwapchain(InSize);
}

bool VulkanRHI::SetVSync(bool InEnabled)
{
    if (MDesc.vsync == InEnabled)
    {
        return true;
    }
    MDesc.vsync = InEnabled;
    // WaitIdle() inside RecreateSwapchain makes this safe from the editor's
    if (MDevice == VK_NULL_HANDLE)
    {
        return true;
    }
    return RecreateSwapchain(MDesc.size);
}

bool VulkanRHI::VSyncEnabled() const noexcept
{
    return MDesc.vsync;
}

void VulkanRHI::SetOitMode(std::uint8_t InMode)
{
    MInfo.oitMode = InMode;
}

void VulkanRHI::SetUpscaleMode(std::uint8_t InMode)
{
    MInfo.upscaleMode = InMode;
}

void VulkanRHI::WaitIdle()
{
    if (MDevice != VK_NULL_HANDLE)
    {
        Vk::GetApi().vkDeviceWaitIdle(MDevice);
    }
}

bool VulkanRHI::BeginFrame()
{
    if (MSwapchain == VK_NULL_HANDLE || MExtent.width == 0 || MExtent.height == 0 || MDeviceLost)
    {
        return false;
    }

    Vk::Api& ScopedApi = Vk::GetApi();
    MFrameIndex = static_cast<std::uint32_t>(MFrameCounter % MFramesInFlight);
    ++MFrameCounter;
    MClusterRecordedThisFrame = false;
    for (std::uint32_t ScopedView = 0; ScopedView < SdfMaxViews; ++ScopedView)
    {
        MSdfMaskPublished[MFrameIndex * SdfMaxViews + ScopedView] = false;
    }

    if (ScopedApi.vkWaitForFences(MDevice, 1, &MInFlight[MFrameIndex], VK_TRUE,
                                  std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS)
    {
        LogError("vulkan", "vkWaitForFences failed; device lost");
        MDeviceLost = true;
        return false;
    }
    float ScopedGpuFrameMilliseconds = 0.0f;
    float ScopedGpuDepthMilliseconds = 0.0f;
    float ScopedGpuMainMilliseconds = 0.0f;
    float ScopedGpuClusterMilliseconds = 0.0f;
    float ScopedGpuPostFxMilliseconds = 0.0f;
    int ScopedTimestampCode = 0;
    if (MTimestampsReady && MFrameCounter > MFramesInFlight && MFrameIndex < MTimestampPools.size() &&
        MTimestampPools[MFrameIndex] != VK_NULL_HANDLE)
    {
        std::array<std::uint64_t, TimestampCount> ScopedTimestamps{};
        const VkResult ScopedTimestampResult = ScopedApi.vkGetQueryPoolResults(
            MDevice, MTimestampPools[MFrameIndex], 0, TimestampCount, ScopedTimestamps.size() * sizeof(std::uint64_t),
            ScopedTimestamps.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        ScopedTimestampCode = static_cast<int>(ScopedTimestampResult);
        if (ScopedTimestampResult == VK_SUCCESS)
        {
            const auto ScopedMs = [&](std::uint32_t InStart, std::uint32_t InEnd)
            {
                if (ScopedTimestamps[InEnd] < ScopedTimestamps[InStart])
                {
                    return 0.0f;
                }
                return static_cast<float>(ScopedTimestamps[InEnd] - ScopedTimestamps[InStart]) * MTimestampPeriod /
                       1.0e6f;
            };
            ScopedGpuFrameMilliseconds = ScopedMs(TimestampFrameStart, TimestampFrameEnd);
            ScopedGpuDepthMilliseconds = ScopedMs(TimestampDepthStart, TimestampDepthEnd);
            ScopedGpuMainMilliseconds = ScopedMs(TimestampMainStart, TimestampMainEnd);
            ScopedGpuClusterMilliseconds = ScopedMs(TimestampClusterStart, TimestampClusterEnd);
            ScopedGpuPostFxMilliseconds = ScopedMs(TimestampPostFxStart, TimestampPostFxEnd);
        }
    }
    std::uint32_t ScopedCulledItems = 0;
    if (MCullReady && MFrameIndex < MCullFrames.size() && MCullFrames[MFrameIndex].ReadbackMapped != nullptr)
    {
        const auto* ScopedCounts = static_cast<const std::uint32_t*>(MCullFrames[MFrameIndex].ReadbackMapped);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxCullBatches; ++ScopedIndex)
        {
            ScopedCulledItems += ScopedCounts[ScopedIndex];
        }
    }
    ParseVtFeedback(MFrameIndex % kFeedbackSlots);

    const VkResult ScopedAcquire =
        ScopedApi.vkAcquireNextImageKHR(MDevice, MSwapchain, std::numeric_limits<std::uint64_t>::max(),
                                        MImageAvailable[MFrameIndex], VK_NULL_HANDLE, &MImageIndex);
    if (ScopedAcquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        (void)RecreateSwapchain(MDesc.size);
        return false;
    }
    if (ScopedAcquire != VK_SUCCESS && ScopedAcquire != VK_SUBOPTIMAL_KHR)
    {
        LogError("vulkan", "vkAcquireNextImageKHR failed: {}", static_cast<int>(ScopedAcquire));
        return false;
    }

    if (MExtent.width != 0 && MExtent.height != 0)
    {
        // The mask is created or resized before recording starts: creating it from
        // the compute pass rewrites descriptor sets the recording buffer already
        // bound.
        const Extent2D ScopedMaskExtent{std::max(MExtent.width / 2u, 1u), std::max(MExtent.height / 2u, 1u)};
        if (MSdfMaskView == VK_NULL_HANDLE || MSdfMaskExtent.width != ScopedMaskExtent.width ||
            MSdfMaskExtent.height != ScopedMaskExtent.height)
        {
            WaitAllInFlight();
            (void)EnsureSdfMask(ScopedMaskExtent);
        }
    }
    if (MVtFeedbackPendingExtent.width != 0)
    {
        // The feedback views and their descriptor writes must not race a recording
        // or pending command buffer.
        const Extent2D ScopedPending = MVtFeedbackPendingExtent;
        MVtFeedbackPendingExtent = {};
        WaitAllInFlight();
        (void)EnsureVtFeedback(ScopedPending);
    }

    // The fence is reset just before the submit in EndFrame: resetting here and
    // then failing the frame leaves an unsignalled fence that hangs the slot.
    VkCommandBuffer ScopedFrameCommandBuffer = MCommandBuffers[MFrameIndex];
    ScopedApi.vkResetCommandBuffer(ScopedFrameCommandBuffer, 0);

    if (MVirtualTextureDirty)
    {
        // The command buffer holds no recorded commands yet and the in-flight
        // submissions are done, so the shared sprite sets can be rewritten.
        WaitAllInFlight();
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
        MVirtualTextureDirty = false;
    }

    VkCommandBufferBeginInfo ScopedFrameBegin{};
    ScopedFrameBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ScopedFrameBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ScopedApi.vkBeginCommandBuffer(ScopedFrameCommandBuffer, &ScopedFrameBegin) != VK_SUCCESS)
    {
        LogError("vulkan", "vkBeginCommandBuffer failed; stopping the frame loop");
        MDeviceLost = true;
        return false;
    }
    if (MTimestampsReady && MTimestampPools[MFrameIndex] != VK_NULL_HANDLE)
    {
        ScopedApi.vkCmdResetQueryPool(ScopedFrameCommandBuffer, MTimestampPools[MFrameIndex], 0, TimestampCount);
        WriteTimestamp(ScopedFrameCommandBuffer, TimestampFrameStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    }

    MStats = {};
    MStats.frameIndex = MFrameCounter;
    MStats.culledItems = ScopedCulledItems;
    MStats.gpuFrameMilliseconds = ScopedGpuFrameMilliseconds;
    MStats.gpuDepthMilliseconds = ScopedGpuDepthMilliseconds;
    MStats.gpuMainMilliseconds = ScopedGpuMainMilliseconds;
    MStats.gpuClusterMilliseconds = ScopedGpuClusterMilliseconds;
    MStats.gpuPostFxMilliseconds = ScopedGpuPostFxMilliseconds;
    MStats.deviceLost = MDeviceLost;
    MStats.oitItems = 0;
    MStats.gpuTimestampsValid = ScopedGpuFrameMilliseconds > 0.0f;
    MStats.gpuTimestampResult = ScopedTimestampCode;
    MInFrame = true;
    MPassOpen = false;
    MMainPassRecorded = false;
    MPresentTargetId = 0xFFFFFFFFu;
    MBlindFrame = false;
    MVtFeedbackRecordedThisFrame = false;
    MInstanceFrameCursor = 0;
    MPreUploadedPackets.clear();
    MRecordedPassIndex = 0;
    MPostFxThisFrame = false;
    MPostFxRecordPending = false;
    MMeshFrameVertexCursor = 0;
    MMeshFrameIndexCursor = 0;
    MMeshFrameBoneCursor = 0;
    return true;
}

RenderTarget* VulkanRHI::FindRenderTarget(std::uint32_t InId) noexcept
{
    return InId < MRenderTargets.size() ? MRenderTargets[InId].get() : nullptr;
}

void VulkanRHI::DestroyRenderTargetResources(RenderTarget& InTarget) noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (InTarget.sceneFramebuffer != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyFramebuffer(MDevice, InTarget.sceneFramebuffer, nullptr);
        InTarget.sceneFramebuffer = VK_NULL_HANDLE;
    }
    if (InTarget.sceneView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, InTarget.sceneView, nullptr);
        InTarget.sceneView = VK_NULL_HANDLE;
    }
    if (InTarget.sceneImage != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, InTarget.sceneImage, nullptr);
        InTarget.sceneImage = VK_NULL_HANDLE;
    }
    if (InTarget.sceneMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, InTarget.sceneMemory, nullptr);
        InTarget.sceneMemory = VK_NULL_HANDLE;
    }
    if (InTarget.flashView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, InTarget.flashView, nullptr);
        InTarget.flashView = VK_NULL_HANDLE;
    }
    if (InTarget.flashImage != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, InTarget.flashImage, nullptr);
        InTarget.flashImage = VK_NULL_HANDLE;
    }
    if (InTarget.flashMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, InTarget.flashMemory, nullptr);
        InTarget.flashMemory = VK_NULL_HANDLE;
    }
    if (InTarget.pipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, InTarget.pipeline, nullptr);
        InTarget.pipeline = VK_NULL_HANDLE;
    }
    if (InTarget.meshPipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, InTarget.meshPipeline, nullptr);
        InTarget.meshPipeline = VK_NULL_HANDLE;
    }
    if (InTarget.framebuffer != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyFramebuffer(MDevice, InTarget.framebuffer, nullptr);
        InTarget.framebuffer = VK_NULL_HANDLE;
    }
    if (InTarget.renderPass != VK_NULL_HANDLE && InTarget.ownsRenderPass)
    {
        ScopedApi.vkDestroyRenderPass(MDevice, InTarget.renderPass, nullptr);
    }
    InTarget.renderPass = VK_NULL_HANDLE;
    if (InTarget.view != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, InTarget.view, nullptr);
        InTarget.view = VK_NULL_HANDLE;
    }
    if (InTarget.image != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, InTarget.image, nullptr);
        InTarget.image = VK_NULL_HANDLE;
    }
    if (InTarget.memory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, InTarget.memory, nullptr);
        InTarget.memory = VK_NULL_HANDLE;
    }
    DestroyDepthImage(&InTarget.depthImage, &InTarget.depthMemory, &InTarget.depthView);
}

RenderTargetHandle VulkanRHI::CreateRenderTarget(const RenderTargetDesc& InDesc)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE || InDesc.size.width == 0 || InDesc.size.height == 0)
    {
        return {};
    }

    std::uint32_t ScopedId = 0;
    if (!MFreeRenderTargetIds.empty())
    {
        ScopedId = MFreeRenderTargetIds.back();
        MFreeRenderTargetIds.pop_back();
    }
    else
    {
        if (MRenderTargets.size() >= kMaxRenderTargets)
        {
            LogError("vulkan", "CreateRenderTarget: all {} slots are in use", kMaxRenderTargets);
            return {};
        }
        ScopedId = static_cast<std::uint32_t>(MRenderTargets.size());
        MRenderTargets.push_back(nullptr);
    }

    auto ScopedTarget = std::make_unique<RenderTarget>();
    ScopedTarget->extent = InDesc.size;
    ScopedTarget->srgb = InDesc.srgb;
    ScopedTarget->blend = InDesc.blend;
    ScopedTarget->format =
        InDesc.matchSwapchainFormat ? MFormat : (InDesc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM);

    const auto ScopedCleanup = [&]()
    {
        DestroyRenderTargetResources(*ScopedTarget);
    };

    VkImageCreateInfo ScopedImageInfo{};
    ScopedImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedImageInfo.format = ScopedTarget->format;
    ScopedImageInfo.extent = {InDesc.size.width, InDesc.size.height, 1};
    ScopedImageInfo.mipLevels = 1;
    ScopedImageInfo.arrayLayers = 1;
    ScopedImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ScopedImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedImageInfo, nullptr, &ScopedTarget->image) != VK_SUCCESS)
    {
        LogError("vulkan", "CreateRenderTarget: vkCreateImage failed");
        ScopedCleanup();
        return {};
    }

    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, ScopedTarget->image, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocateInfo.allocationSize = ScopedRequirements.size;
    ScopedAllocateInfo.memoryTypeIndex =
        FindMemoryType(MMemoryProperties, ScopedRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ScopedAllocateInfo.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocateInfo, nullptr, &ScopedTarget->memory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, ScopedTarget->image, ScopedTarget->memory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "CreateRenderTarget: memory allocation failed");
        ScopedCleanup();
        return {};
    }

    if (!IsSrgbFormat(ScopedTarget->format) && MOffscreenRenderPass != VK_NULL_HANDLE)
    {
        // Shared offscreen pass: identical attachment format and structure, so
        ScopedTarget->renderPass = MOffscreenRenderPass;
        ScopedTarget->ownsRenderPass = false;
    }
    else
    {
        VkAttachmentDescription ScopedColor{};
        ScopedColor.format = ScopedTarget->format;
        ScopedColor.samples = VK_SAMPLE_COUNT_1_BIT;
        ScopedColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ScopedColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ScopedColor.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ScopedColor.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ScopedColorRef{};
        ScopedColorRef.attachment = 0;
        ScopedColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
        ScopedDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        ScopedDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        ScopedDependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        if (InDesc.matchSwapchainFormat)
        {
            // The UI pipeline is created against MRenderPass, so this pass has to
            // describe the same stage dependencies to stay compatible with it.
            ScopedDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            ScopedDependency.dstStageMask =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            ScopedDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }

        const VkAttachmentDescription ScopedAttachments[] = {ScopedColor, ScopedDepth};
        VkRenderPassCreateInfo ScopedPassInfo{};
        ScopedPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ScopedPassInfo.attachmentCount = 2;
        ScopedPassInfo.pAttachments = ScopedAttachments;
        ScopedPassInfo.subpassCount = 1;
        ScopedPassInfo.pSubpasses = &ScopedSubpass;
        ScopedPassInfo.dependencyCount = 1;
        ScopedPassInfo.pDependencies = &ScopedDependency;
        if (ScopedApi.vkCreateRenderPass(MDevice, &ScopedPassInfo, nullptr, &ScopedTarget->renderPass) != VK_SUCCESS)
        {
            LogError("vulkan", "CreateRenderTarget: vkCreateRenderPass failed");
            ScopedCleanup();
            return {};
        }
        ScopedTarget->ownsRenderPass = true;
    }

    VkImageViewCreateInfo ScopedViewInfo{};
    ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedViewInfo.image = ScopedTarget->image;
    ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedViewInfo.format = ScopedTarget->format;
    ScopedViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedViewInfo.subresourceRange.levelCount = 1;
    ScopedViewInfo.subresourceRange.layerCount = 1;
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &ScopedTarget->view) != VK_SUCCESS)
    {
        LogError("vulkan", "CreateRenderTarget: vkCreateImageView failed");
        ScopedCleanup();
        return {};
    }
    if (!CreateDepthImage(InDesc.size, &ScopedTarget->depthImage, &ScopedTarget->depthMemory, &ScopedTarget->depthView))
    {
        LogError("vulkan", "CreateRenderTarget: depth image failed");
        ScopedCleanup();
        return {};
    }

    const VkImageView ScopedAttachments[] = {ScopedTarget->view, ScopedTarget->depthView};
    VkFramebufferCreateInfo ScopedFramebufferInfo{};
    ScopedFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ScopedFramebufferInfo.renderPass = ScopedTarget->renderPass;
    ScopedFramebufferInfo.attachmentCount = 2;
    ScopedFramebufferInfo.pAttachments = ScopedAttachments;
    ScopedFramebufferInfo.width = InDesc.size.width;
    ScopedFramebufferInfo.height = InDesc.size.height;
    ScopedFramebufferInfo.layers = 1;
    if (ScopedApi.vkCreateFramebuffer(MDevice, &ScopedFramebufferInfo, nullptr, &ScopedTarget->framebuffer) !=
        VK_SUCCESS)
    {
        LogError("vulkan", "CreateRenderTarget: vkCreateFramebuffer failed");
        ScopedCleanup();
        return {};
    }

    // Accessibility composite companion: the scene renders into this image and a
    // fullscreen pass samples it into the target. Only the shared offscreen pass
    // is supported, and only when the composite pipeline exists.
    if (MAccessibilityReady && ScopedTarget->renderPass == MOffscreenRenderPass)
    {
        VkImageCreateInfo ScopedSceneInfo{};
        ScopedSceneInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ScopedSceneInfo.imageType = VK_IMAGE_TYPE_2D;
        ScopedSceneInfo.format = ScopedTarget->format;
        ScopedSceneInfo.extent = {InDesc.size.width, InDesc.size.height, 1};
        ScopedSceneInfo.mipLevels = 1;
        ScopedSceneInfo.arrayLayers = 1;
        ScopedSceneInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        ScopedSceneInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        ScopedSceneInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ScopedSceneInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ScopedSceneInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (ScopedApi.vkCreateImage(MDevice, &ScopedSceneInfo, nullptr, &ScopedTarget->sceneImage) == VK_SUCCESS)
        {
            VkMemoryRequirements ScopedSceneRequirements{};
            ScopedApi.vkGetImageMemoryRequirements(MDevice, ScopedTarget->sceneImage, &ScopedSceneRequirements);
            VkMemoryAllocateInfo ScopedSceneAllocate{};
            ScopedSceneAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ScopedSceneAllocate.allocationSize = ScopedSceneRequirements.size;
            ScopedSceneAllocate.memoryTypeIndex = FindMemoryType(
                MMemoryProperties, ScopedSceneRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VkImageViewCreateInfo ScopedSceneView{};
            ScopedSceneView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ScopedSceneView.image = ScopedTarget->sceneImage;
            ScopedSceneView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ScopedSceneView.format = ScopedTarget->format;
            ScopedSceneView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ScopedSceneView.subresourceRange.levelCount = 1;
            ScopedSceneView.subresourceRange.layerCount = 1;
            // The scene view must exist before the framebuffer names it, so the
            // attachment array is built after the image view is created. The
            // same ordering bug in the accessibility framebuffer faulted on the
            // first vkCmdBeginRenderPass that used it.
            const bool ScopedSceneResourcesOk =
                ScopedSceneAllocate.memoryTypeIndex != std::numeric_limits<std::uint32_t>::max() &&
                ScopedApi.vkAllocateMemory(MDevice, &ScopedSceneAllocate, nullptr, &ScopedTarget->sceneMemory) ==
                    VK_SUCCESS &&
                ScopedApi.vkBindImageMemory(MDevice, ScopedTarget->sceneImage, ScopedTarget->sceneMemory, 0) ==
                    VK_SUCCESS &&
                ScopedApi.vkCreateImageView(MDevice, &ScopedSceneView, nullptr, &ScopedTarget->sceneView) == VK_SUCCESS;
            bool ScopedSceneOk = false;
            if (ScopedSceneResourcesOk)
            {
                const VkImageView ScopedSceneAttachments[] = {ScopedTarget->sceneView, ScopedTarget->depthView};
                VkFramebufferCreateInfo ScopedSceneFramebuffer{};
                ScopedSceneFramebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                ScopedSceneFramebuffer.renderPass = ScopedTarget->renderPass;
                ScopedSceneFramebuffer.attachmentCount = 2;
                ScopedSceneFramebuffer.pAttachments = ScopedSceneAttachments;
                ScopedSceneFramebuffer.width = InDesc.size.width;
                ScopedSceneFramebuffer.height = InDesc.size.height;
                ScopedSceneFramebuffer.layers = 1;
                ScopedSceneOk = ScopedApi.vkCreateFramebuffer(MDevice, &ScopedSceneFramebuffer, nullptr,
                                                              &ScopedTarget->sceneFramebuffer) == VK_SUCCESS;
            }
            if (ScopedSceneOk)
            {
                VkDescriptorSetAllocateInfo ScopedSetAllocate{};
                ScopedSetAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                ScopedSetAllocate.descriptorPool = MAccessibilityPool;
                ScopedSetAllocate.descriptorSetCount = 1;
                ScopedSetAllocate.pSetLayouts = &MAccessibilitySetLayout;
                if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedSetAllocate, &ScopedTarget->accessibilitySet) ==
                    VK_SUCCESS)
                {
                    VkDescriptorImageInfo ScopedImage{};
                    ScopedImage.sampler = MAccessibilitySampler;
                    ScopedImage.imageView = ScopedTarget->sceneView;
                    ScopedImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    VkWriteDescriptorSet ScopedWrite{};
                    ScopedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    ScopedWrite.dstSet = ScopedTarget->accessibilitySet;
                    ScopedWrite.dstBinding = 0;
                    ScopedWrite.descriptorCount = 1;
                    ScopedWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    ScopedWrite.pImageInfo = &ScopedImage;
                    ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedWrite, 0, nullptr);
                }
            }
            if (ScopedSceneOk)
            {
                VkImageCreateInfo ScopedFlashInfo{};
                ScopedFlashInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                ScopedFlashInfo.imageType = VK_IMAGE_TYPE_2D;
                ScopedFlashInfo.format = VK_FORMAT_R8_UNORM;
                ScopedFlashInfo.extent = {InDesc.size.width, InDesc.size.height, 1};
                ScopedFlashInfo.mipLevels = 1;
                ScopedFlashInfo.arrayLayers = 1;
                ScopedFlashInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                ScopedFlashInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                ScopedFlashInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT;
                ScopedFlashInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                ScopedFlashInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                VkMemoryAllocateInfo ScopedFlashAllocate{};
                ScopedFlashAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                VkImageViewCreateInfo ScopedFlashView{};
                ScopedFlashView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                ScopedFlashView.viewType = VK_IMAGE_VIEW_TYPE_2D;
                ScopedFlashView.format = VK_FORMAT_R8_UNORM;
                ScopedFlashView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                ScopedFlashView.subresourceRange.levelCount = 1;
                ScopedFlashView.subresourceRange.layerCount = 1;
                const bool ScopedImageOk = ScopedApi.vkCreateImage(MDevice, &ScopedFlashInfo, nullptr,
                                                                   &ScopedTarget->flashImage) == VK_SUCCESS;
                VkMemoryRequirements ScopedFlashRequirements{};
                if (ScopedImageOk)
                {
                    ScopedApi.vkGetImageMemoryRequirements(MDevice, ScopedTarget->flashImage, &ScopedFlashRequirements);
                    ScopedFlashAllocate.allocationSize = ScopedFlashRequirements.size;
                    ScopedFlashAllocate.memoryTypeIndex = FindMemoryType(
                        MMemoryProperties, ScopedFlashRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    ScopedFlashView.image = ScopedTarget->flashImage;
                }
                const bool ScopedFlashOk =
                    ScopedImageOk && ScopedFlashAllocate.memoryTypeIndex != std::numeric_limits<std::uint32_t>::max() &&
                    ScopedApi.vkAllocateMemory(MDevice, &ScopedFlashAllocate, nullptr, &ScopedTarget->flashMemory) ==
                        VK_SUCCESS &&
                    ScopedApi.vkBindImageMemory(MDevice, ScopedTarget->flashImage, ScopedTarget->flashMemory, 0) ==
                        VK_SUCCESS &&
                    ScopedApi.vkCreateImageView(MDevice, &ScopedFlashView, nullptr, &ScopedTarget->flashView) ==
                        VK_SUCCESS;
                if (ScopedFlashOk)
                {
                    InitImageLayout(ScopedTarget->flashImage, VK_IMAGE_LAYOUT_GENERAL);
                    if (ScopedTarget->accessibilitySet != VK_NULL_HANDLE)
                    {
                        VkDescriptorImageInfo ScopedHistory{};
                        ScopedHistory.imageView = ScopedTarget->flashView;
                        ScopedHistory.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                        VkWriteDescriptorSet ScopedHistoryWrite{};
                        ScopedHistoryWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        ScopedHistoryWrite.dstSet = ScopedTarget->accessibilitySet;
                        ScopedHistoryWrite.dstBinding = 1;
                        ScopedHistoryWrite.descriptorCount = 1;
                        ScopedHistoryWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                        ScopedHistoryWrite.pImageInfo = &ScopedHistory;
                        ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedHistoryWrite, 0, nullptr);
                    }
                }
            }
        }
    }

    // The sprite pipeline is tied to this pass, so create a second pipeline.
    if (!CreatePipelineForPass(ScopedTarget->renderPass, &ScopedTarget->pipeline, false, false, InDesc.blend) ||
        !CreateMeshPipelineForPass(ScopedTarget->renderPass, &ScopedTarget->meshPipeline))
    {
        ScopedCleanup();
        return {};
    }

    MRenderTargets[ScopedId] = std::move(ScopedTarget);
    LogInfo("vulkan", "render target #{}: {}x{} {}", ScopedId, InDesc.size.width, InDesc.size.height,
            InDesc.srgb ? "sRGB" : "linear");
    return RenderTargetHandle{ScopedId};
}

void VulkanRHI::DestroyRenderTarget(RenderTargetHandle InHandle)
{
    if (!InHandle.IsValid())
    {
        return;
    }
    RenderTarget* ScopedTarget = FindRenderTarget(InHandle.id);
    if (ScopedTarget == nullptr)
    {
        return;
    }
    WaitIdle();
    DestroyRenderTargetResources(*ScopedTarget);
    MRenderTargets[InHandle.id].reset();
    MFreeRenderTargetIds.push_back(InHandle.id);
}

bool VulkanRHI::RcasSharpen(RenderTargetHandle InSource, RenderTargetHandle InDestination, float InSharpness)
{
    if (!MInFrame || MPassOpen || MRcasLayout == VK_NULL_HANDLE || MUpscaleSets[MFrameIndex] == VK_NULL_HANDLE)
    {
        return false;
    }
    const RenderTarget* ScopedSource = FindRenderTarget(InSource.id);
    RenderTarget* ScopedDestination = FindRenderTarget(InDestination.id);
    if (ScopedSource == nullptr || ScopedDestination == nullptr)
    {
        return false;
    }
    VkPipeline ScopedPipeline = VK_NULL_HANDLE;
    const auto ScopedFound = MRcasPipelines.find(ScopedDestination->renderPass);
    if (ScopedFound != MRcasPipelines.end())
    {
        ScopedPipeline = ScopedFound->second;
    }
    else if (CreateGraphicsPipelineForPass(ScopedDestination->renderPass, MRcasLayout, Shaders::fullscreen_vert.data(),
                                           Shaders::fullscreen_vert.size() * sizeof(std::uint32_t),
                                           Shaders::fsr1_rcas.data(), Shaders::fsr1_rcas.size() * sizeof(std::uint32_t),
                                           &ScopedPipeline))
    {
        MRcasPipelines.emplace(ScopedDestination->renderPass, ScopedPipeline);
    }
    if (ScopedPipeline == VK_NULL_HANDLE)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorImageInfo ScopedImage{MUpscaleSampler, ScopedSource->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet ScopedWrite{};
    ScopedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrite.dstSet = MRcasSets[MFrameIndex];
    ScopedWrite.dstBinding = 0;
    ScopedWrite.descriptorCount = 1;
    ScopedWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrite.pImageInfo = &ScopedImage;
    ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedWrite, 0, nullptr);

    VkClearValue ScopedClear[2]{};
    ScopedClear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    ScopedClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo ScopedBegin{};
    ScopedBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBegin.renderPass = ScopedDestination->renderPass;
    ScopedBegin.framebuffer = ScopedDestination->framebuffer;
    ScopedBegin.renderArea.extent = {ScopedDestination->extent.width, ScopedDestination->extent.height};
    ScopedBegin.clearValueCount = 2;
    ScopedBegin.pClearValues = ScopedClear;
    ScopedApi.vkCmdBeginRenderPass(MCommandBuffers[MFrameIndex], &ScopedBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(ScopedDestination->extent.width);
    ScopedViewport.height = static_cast<float>(ScopedDestination->extent.height);
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(MCommandBuffers[MFrameIndex], 0, 1, &ScopedViewport);
    VkRect2D ScopedScissor{};
    ScopedScissor.extent = {ScopedDestination->extent.width, ScopedDestination->extent.height};
    ScopedApi.vkCmdSetScissor(MCommandBuffers[MFrameIndex], 0, 1, &ScopedScissor);
    ScopedApi.vkCmdBindPipeline(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, ScopedPipeline);
    ScopedApi.vkCmdBindDescriptorSets(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, MRcasLayout, 0, 1,
                                      &MRcasSets[MFrameIndex], 0, nullptr);
    struct RcasPush
    {
        float texel[2];
        float sharpness;
        float padding;
    } ScopedPush{};
    ScopedPush.texel[0] = 1.0f / static_cast<float>(ScopedSource->extent.width);
    ScopedPush.texel[1] = 1.0f / static_cast<float>(ScopedSource->extent.height);
    ScopedPush.sharpness = InSharpness;
    ScopedApi.vkCmdPushConstants(MCommandBuffers[MFrameIndex], MRcasLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                 sizeof(ScopedPush), &ScopedPush);
    ScopedApi.vkCmdDraw(MCommandBuffers[MFrameIndex], 3, 1, 0, 0);
    ScopedApi.vkCmdEndRenderPass(MCommandBuffers[MFrameIndex]);
    return true;
}

bool VulkanRHI::UpscaleRenderTarget(RenderTargetHandle InSource, RenderTargetHandle InDestination,
                                    SpatialUpscaleMode InMode)
{
    if (!MInFrame || MPassOpen || MUpscaleLayout == VK_NULL_HANDLE || MUpscaleSets[MFrameIndex] == VK_NULL_HANDLE)
    {
        return false;
    }
    const RenderTarget* ScopedSource = FindRenderTarget(InSource.id);
    RenderTarget* ScopedDestination = FindRenderTarget(InDestination.id);
    if (ScopedSource == nullptr || ScopedDestination == nullptr || ScopedSource->view == VK_NULL_HANDLE ||
        ScopedDestination->renderPass == VK_NULL_HANDLE)
    {
        return false;
    }

    VkPipeline ScopedPipeline = VK_NULL_HANDLE;
    const auto ScopedFound = MUpscalePipelines.find(ScopedDestination->renderPass);
    if (ScopedFound != MUpscalePipelines.end())
    {
        ScopedPipeline = ScopedFound->second;
    }
    else if (CreateGraphicsPipelineForPass(
                 ScopedDestination->renderPass, MUpscaleLayout, Shaders::fullscreen_vert.data(),
                 Shaders::fullscreen_vert.size() * sizeof(std::uint32_t), Shaders::fsr1_upscale.data(),
                 Shaders::fsr1_upscale.size() * sizeof(std::uint32_t), &ScopedPipeline))
    {
        MUpscalePipelines.emplace(ScopedDestination->renderPass, ScopedPipeline);
    }
    if (ScopedPipeline == VK_NULL_HANDLE)
    {
        return false;
    }

    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorImageInfo ScopedImage{};
    ScopedImage.sampler = MUpscaleSampler;
    ScopedImage.imageView = ScopedSource->view;
    ScopedImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet ScopedWrite{};
    ScopedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrite.dstSet = MUpscaleSets[MFrameIndex];
    ScopedWrite.dstBinding = 0;
    ScopedWrite.descriptorCount = 1;
    ScopedWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrite.pImageInfo = &ScopedImage;
    ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedWrite, 0, nullptr);

    VkClearValue ScopedClear[2]{};
    ScopedClear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    ScopedClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo ScopedBegin{};
    ScopedBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBegin.renderPass = ScopedDestination->renderPass;
    ScopedBegin.framebuffer = ScopedDestination->framebuffer;
    ScopedBegin.renderArea.extent = {ScopedDestination->extent.width, ScopedDestination->extent.height};
    ScopedBegin.clearValueCount = 2;
    ScopedBegin.pClearValues = ScopedClear;
    ScopedApi.vkCmdBeginRenderPass(MCommandBuffers[MFrameIndex], &ScopedBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(ScopedDestination->extent.width);
    ScopedViewport.height = static_cast<float>(ScopedDestination->extent.height);
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(MCommandBuffers[MFrameIndex], 0, 1, &ScopedViewport);
    VkRect2D ScopedScissor{};
    ScopedScissor.extent = {ScopedDestination->extent.width, ScopedDestination->extent.height};
    ScopedApi.vkCmdSetScissor(MCommandBuffers[MFrameIndex], 0, 1, &ScopedScissor);
    ScopedApi.vkCmdBindPipeline(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, ScopedPipeline);
    ScopedApi.vkCmdBindDescriptorSets(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, MUpscaleLayout, 0,
                                      1, &MUpscaleSets[MFrameIndex], 0, nullptr);
    struct UpscalePush
    {
        float source[4];
        float params[4];
    } ScopedPush{};
    ScopedPush.source[0] = static_cast<float>(ScopedSource->extent.width);
    ScopedPush.source[1] = static_cast<float>(ScopedSource->extent.height);
    ScopedPush.source[2] = 1.0f / static_cast<float>(ScopedSource->extent.width);
    ScopedPush.source[3] = 1.0f / static_cast<float>(ScopedSource->extent.height);
    ScopedPush.params[0] = InMode == SpatialUpscaleMode::Fsr1 ? 0.0f : 0.12f;
    ScopedPush.params[1] = static_cast<float>(InMode);
    ScopedApi.vkCmdPushConstants(MCommandBuffers[MFrameIndex], MUpscaleLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                 sizeof(ScopedPush), &ScopedPush);
    ScopedApi.vkCmdDraw(MCommandBuffers[MFrameIndex], 3, 1, 0, 0);
    ScopedApi.vkCmdEndRenderPass(MCommandBuffers[MFrameIndex]);
    return true;
}

void VulkanRHI::PresentRenderTarget(RenderTargetHandle InHandle)
{
    const RenderTarget* ScopedTarget = FindRenderTarget(InHandle.id);
    MPresentTargetId = ScopedTarget != nullptr && ScopedTarget->image != VK_NULL_HANDLE ? InHandle.id : 0xFFFFFFFFu;
}

bool VulkanRHI::TransitionRenderTargetLayout(RenderTargetHandle InHandle, int InOldLayout, int InNewLayout)
{
    if (!MInFrame)
    {
        return false;
    }
    const RenderTarget* ScopedTarget = FindRenderTarget(InHandle.id);
    if (ScopedTarget == nullptr || ScopedTarget->image == VK_NULL_HANDLE)
    {
        return false;
    }
    VkImageMemoryBarrier ScopedBarrier{};
    ScopedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    ScopedBarrier.oldLayout = static_cast<VkImageLayout>(InOldLayout);
    ScopedBarrier.newLayout = static_cast<VkImageLayout>(InNewLayout);
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.image = ScopedTarget->image;
    ScopedBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Vk::GetApi().vkCmdPipelineBarrier(MCommandBuffers[MFrameIndex], VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                      &ScopedBarrier);
    return true;
}

bool VulkanRHI::PresentGeneratedFrame(RenderTargetHandle InHandle)
{
    if (MInFrame || MDeviceLost || MSwapchain == VK_NULL_HANDLE)
    {
        return false;
    }
    const RenderTarget* ScopedTarget = FindRenderTarget(InHandle.id);
    if (ScopedTarget == nullptr || ScopedTarget->image == VK_NULL_HANDLE)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    if (ScopedApi.vkWaitForFences(MDevice, 1, &MInFlight[MFrameIndex], VK_TRUE,
                                  std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS)
    {
        return false;
    }
    const VkResult ScopedAcquire =
        ScopedApi.vkAcquireNextImageKHR(MDevice, MSwapchain, std::numeric_limits<std::uint64_t>::max(),
                                        MImageAvailable[MFrameIndex], VK_NULL_HANDLE, &MImageIndex);
    if (ScopedAcquire != VK_SUCCESS && ScopedAcquire != VK_SUBOPTIMAL_KHR)
    {
        return false;
    }
    // The fence is reset just before the submit in EndFrame: resetting here and
    // then failing the frame leaves an unsignalled fence that hangs the slot.
    VkCommandBuffer ScopedCommandBuffer = MCommandBuffers[MFrameIndex];
    ScopedApi.vkResetCommandBuffer(ScopedCommandBuffer, 0);

    if (MVirtualTextureDirty)
    {
        // The command buffer holds no recorded commands now, and the in-flight
        // submissions are done, so the shared sprite sets can be rewritten safely.
        WaitAllInFlight();
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
        MVirtualTextureDirty = false;
    }

    VkCommandBufferBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ScopedBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ScopedApi.vkBeginCommandBuffer(ScopedCommandBuffer, &ScopedBeginInfo) != VK_SUCCESS)
    {
        LogError("vulkan", "vkBeginCommandBuffer failed; stopping the frame loop");
        MDeviceLost = true;
        return false;
    }
    if (MTimestampsReady && MTimestampPools[MFrameIndex] != VK_NULL_HANDLE)
    {
        ScopedApi.vkCmdResetQueryPool(ScopedCommandBuffer, MTimestampPools[MFrameIndex], 0, TimestampCount);
        WriteTimestamp(ScopedCommandBuffer, TimestampFrameStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    }

    VkImageMemoryBarrier ScopedSourceBarrier{};
    ScopedSourceBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedSourceBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ScopedSourceBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ScopedSourceBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedSourceBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ScopedSourceBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedSourceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedSourceBarrier.image = ScopedTarget->image;
    ScopedSourceBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier ScopedDestinationBarrier{};
    ScopedDestinationBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedDestinationBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ScopedDestinationBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ScopedDestinationBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ScopedDestinationBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedDestinationBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedDestinationBarrier.image = MImages[MImageIndex];
    ScopedDestinationBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkImageMemoryBarrier ScopedBarriers[] = {ScopedSourceBarrier, ScopedDestinationBarrier};
    ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, ScopedBarriers);
    VkImageBlit ScopedBlit{};
    ScopedBlit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ScopedBlit.srcOffsets[1] = {static_cast<std::int32_t>(ScopedTarget->extent.width),
                                static_cast<std::int32_t>(ScopedTarget->extent.height), 1};
    ScopedBlit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ScopedBlit.dstOffsets[1] = {static_cast<std::int32_t>(MExtent.width), static_cast<std::int32_t>(MExtent.height), 1};
    ScopedApi.vkCmdBlitImage(ScopedCommandBuffer, ScopedTarget->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             MImages[MImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedBlit,
                             VK_FILTER_LINEAR);
    ScopedDestinationBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ScopedDestinationBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ScopedDestinationBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ScopedDestinationBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &ScopedDestinationBarrier);
    if (MUiVisible && MHudTargetId != 0xFFFFFFFFu && MHudPipeline != VK_NULL_HANDLE &&
        MImageIndex < MHudOverlayFramebuffers.size())
    {
        VkRenderPassBeginInfo ScopedHudBegin{};
        ScopedHudBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        ScopedHudBegin.renderPass = MHudOverlayPass;
        ScopedHudBegin.framebuffer = MHudOverlayFramebuffers[MImageIndex];
        ScopedHudBegin.renderArea.extent = MExtent;
        ScopedApi.vkCmdBeginRenderPass(ScopedCommandBuffer, &ScopedHudBegin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport ScopedHudViewport{};
        ScopedHudViewport.width = static_cast<float>(MExtent.width);
        ScopedHudViewport.height = static_cast<float>(MExtent.height);
        ScopedHudViewport.maxDepth = 1.0f;
        ScopedApi.vkCmdSetViewport(ScopedCommandBuffer, 0, 1, &ScopedHudViewport);
        VkRect2D ScopedHudScissor{};
        ScopedHudScissor.extent = MExtent;
        ScopedApi.vkCmdSetScissor(ScopedCommandBuffer, 0, 1, &ScopedHudScissor);
        ScopedApi.vkCmdBindPipeline(ScopedCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MHudPipeline);
        ScopedApi.vkCmdBindDescriptorSets(ScopedCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MHudLayout, 0, 1,
                                          &HudSetRef(), 0, nullptr);
        ScopedApi.vkCmdDraw(ScopedCommandBuffer, 3, 1, 0, 0);
        ScopedApi.vkCmdEndRenderPass(ScopedCommandBuffer);
    }
    VkImageMemoryBarrier ScopedPresentBarrier{};
    ScopedPresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedPresentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ScopedPresentBarrier.dstAccessMask = 0;
    ScopedPresentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ScopedPresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    ScopedPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedPresentBarrier.image = MImages[MImageIndex];
    ScopedPresentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &ScopedPresentBarrier);
    if (ScopedApi.vkEndCommandBuffer(ScopedCommandBuffer) != VK_SUCCESS)
    {
        return false;
    }
    VkSemaphore ScopedWaitSemaphores[] = {MImageAvailable[MFrameIndex]};
    VkPipelineStageFlags ScopedWaitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSemaphore ScopedSignalSemaphores[] = {MRenderFinished[MImageIndex]};
    VkSubmitInfo ScopedSubmitInfo{};
    ScopedSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    ScopedSubmitInfo.waitSemaphoreCount = 1;
    ScopedSubmitInfo.pWaitSemaphores = ScopedWaitSemaphores;
    ScopedSubmitInfo.pWaitDstStageMask = ScopedWaitStages;
    ScopedSubmitInfo.commandBufferCount = 1;
    ScopedSubmitInfo.pCommandBuffers = &ScopedCommandBuffer;
    ScopedSubmitInfo.signalSemaphoreCount = 1;
    ScopedSubmitInfo.pSignalSemaphores = ScopedSignalSemaphores;
    (void)ScopedApi.vkResetFences(MDevice, 1, &MInFlight[MFrameIndex]);
    if (ScopedApi.vkQueueSubmit(MQueue, 1, &ScopedSubmitInfo, MInFlight[MFrameIndex]) != VK_SUCCESS)
    {
        return false;
    }
    VkPresentInfoKHR ScopedPresentInfo{};
    ScopedPresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    ScopedPresentInfo.waitSemaphoreCount = 1;
    ScopedPresentInfo.pWaitSemaphores = ScopedSignalSemaphores;
    ScopedPresentInfo.swapchainCount = 1;
    ScopedPresentInfo.pSwapchains = &MSwapchain;
    ScopedPresentInfo.pImageIndices = &MImageIndex;
    const VkResult ScopedPresent = ScopedApi.vkQueuePresentKHR(MQueue, &ScopedPresentInfo);
    if (ScopedPresent == VK_ERROR_OUT_OF_DATE_KHR || ScopedPresent == VK_SUBOPTIMAL_KHR)
    {
        (void)RecreateSwapchain(MDesc.size);
        return false;
    }
    return ScopedPresent == VK_SUCCESS;
}

bool VulkanRHI::RenderToTarget(RenderTargetHandle InHandle, const FramePacket& InPacket)
{
    if (!MInFrame)
    {
        LogError("vulkan", "RenderToTarget must be called between BeginFrame() and EndFrame()");
        return false;
    }
    RenderTarget* ScopedTarget = FindRenderTarget(InHandle.id);
    if (ScopedTarget == nullptr || ScopedTarget->pipeline == VK_NULL_HANDLE)
    {
        return false;
    }
    if (MPassOpen)
    {
        LogError("vulkan", "RenderToTarget must be recorded before the main Submit()");
        return false;
    }
    const std::uint32_t ScopedSdfSlot = ApplySdfLighting(InPacket.sdf);
    ApplyVirtualTexture(InPacket);

    Vk::Api& ScopedApi = Vk::GetApi();
    const VkCommandBuffer ScopedCommandBuffer = MCommandBuffers[MFrameIndex];

    // Compute skinning must run outside the render pass.
    MBlindFrame = (InPacket.accessibility.flags & AccessibilityBlind) != 0;
    if (!MBlindFrame)
    {
        RecordMeshSkinning(ScopedCommandBuffer, InPacket);
        RecordClusters(ScopedCommandBuffer, InPacket);
    }

    const std::uint32_t ScopedFeedbackSlot = MFrameIndex % kFeedbackSlots;
    const bool ScopedFeedback = EnsureVtFeedback(ScopedTarget->extent);
    if (ScopedFeedback)
    {
        MVtFeedbackRecordedThisFrame = true;
        VkClearColorValue ScopedFeedbackClear{};
        const VkImageSubresourceRange ScopedRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ScopedApi.vkCmdClearColorImage(ScopedCommandBuffer, MVtFeedbackImages[ScopedFeedbackSlot],
                                       VK_IMAGE_LAYOUT_GENERAL, &ScopedFeedbackClear, 1, &ScopedRange);
        VkImageMemoryBarrier ScopedFeedbackBarrier{};
        ScopedFeedbackBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ScopedFeedbackBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ScopedFeedbackBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ScopedFeedbackBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        ScopedFeedbackBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        ScopedFeedbackBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedFeedbackBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedFeedbackBarrier.image = MVtFeedbackImages[ScopedFeedbackSlot];
        ScopedFeedbackBarrier.subresourceRange = ScopedRange;
        ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                       &ScopedFeedbackBarrier);
    }

    VkClearValue ScopedClearValues[2]{};
    ScopedClearValues[0].color = {
        {static_cast<float>(InPacket.clear.R()) / 255.0f, static_cast<float>(InPacket.clear.G()) / 255.0f,
         static_cast<float>(InPacket.clear.B()) / 255.0f, static_cast<float>(InPacket.clear.A()) / 255.0f}};
    ScopedClearValues[1].depthStencil.depth = 1.0f;
    ScopedClearValues[1].depthStencil.stencil = 0;

    const bool ScopedComposite =
        ScopedTarget->sceneFramebuffer != VK_NULL_HANDLE && AccessibilityCompositeWanted(InPacket);
    VkRenderPassBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBeginInfo.renderPass = ScopedTarget->renderPass;
    ScopedBeginInfo.framebuffer = ScopedComposite ? ScopedTarget->sceneFramebuffer : ScopedTarget->framebuffer;
    ScopedBeginInfo.renderArea.extent = {ScopedTarget->extent.width, ScopedTarget->extent.height};
    ScopedBeginInfo.clearValueCount = 2;
    ScopedBeginInfo.pClearValues = ScopedClearValues;
    PreUploadInstances(ScopedCommandBuffer, InPacket);
    ScopedApi.vkCmdBeginRenderPass(ScopedCommandBuffer, &ScopedBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(ScopedTarget->extent.width);
    ScopedViewport.height = static_cast<float>(ScopedTarget->extent.height);
    ScopedViewport.minDepth = 0.0f;
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(ScopedCommandBuffer, 0, 1, &ScopedViewport);

    VkRect2D ScopedScissor{};
    ScopedScissor.extent = {ScopedTarget->extent.width, ScopedTarget->extent.height};
    ScopedApi.vkCmdSetScissor(ScopedCommandBuffer, 0, 1, &ScopedScissor);

    if (!MBlindFrame)
    {
        const bool ScopedTargetSrgb = IsSrgbFormat(ScopedTarget->format);
        DrawPacket(ScopedCommandBuffer, ScopedTarget->pipeline, InPacket, true, ScopedTargetSrgb, ScopedSdfSlot);
        DrawMeshes(ScopedCommandBuffer, ScopedTarget->meshPipeline, InPacket, true, ScopedTargetSrgb, ScopedSdfSlot);
        RecordOitResolve(ScopedCommandBuffer, ScopedTarget->renderPass, InPacket);
    }
    ScopedApi.vkCmdEndRenderPass(ScopedCommandBuffer);

    if (ScopedComposite)
    {
        // The scene pass ended with the shared final layout (shader read), so the
        // composite can sample it immediately.
        VkImageMemoryBarrier ScopedCompositeBarrier{};
        ScopedCompositeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ScopedCompositeBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        ScopedCompositeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ScopedCompositeBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ScopedCompositeBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ScopedCompositeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedCompositeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedCompositeBarrier.image = ScopedTarget->sceneImage;
        ScopedCompositeBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                       &ScopedCompositeBarrier);
        RecordAccessibilityComposite(ScopedCommandBuffer, ScopedTarget->renderPass, ScopedTarget->framebuffer,
                                     MAccessibilityPipeline, ScopedTarget->accessibilitySet, ScopedTarget->extent,
                                     InPacket.accessibility, true);
    }

    if (ScopedFeedback)
    {
        VkImageMemoryBarrier ScopedCopyBarrier{};
        ScopedCopyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ScopedCopyBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ScopedCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        ScopedCopyBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        ScopedCopyBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        ScopedCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedCopyBarrier.image = MVtFeedbackImages[ScopedFeedbackSlot];
        ScopedCopyBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                       &ScopedCopyBarrier);
        VkBufferImageCopy ScopedFeedbackCopy{};
        ScopedFeedbackCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        ScopedFeedbackCopy.imageExtent = {MVtFeedbackExtent.width, MVtFeedbackExtent.height, 1};
        ScopedApi.vkCmdCopyImageToBuffer(ScopedCommandBuffer, MVtFeedbackImages[ScopedFeedbackSlot],
                                         VK_IMAGE_LAYOUT_GENERAL, MVtFeedbackBuffers[ScopedFeedbackSlot], 1,
                                         &ScopedFeedbackCopy);
    }

    // The render pass already moved the image to SHADER_READ_ONLY. This barrier
    VkImageMemoryBarrier ScopedBarrier{};
    ScopedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.image = ScopedTarget->image;
    ScopedBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedBarrier.subresourceRange.levelCount = 1;
    ScopedBarrier.subresourceRange.layerCount = 1;
    ScopedBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ScopedBarrier);
    return true;
}

bool VulkanRHI::RenderTargetTexture(RenderTargetHandle InHandle, NativeTextureView& InView) const
{
    if (InHandle.id >= MRenderTargets.size() || MRenderTargets[InHandle.id] == nullptr)
    {
        return false;
    }
    const RenderTarget& ScopedTarget = *MRenderTargets[InHandle.id];
    InView.view = ScopedTarget.view;
    InView.image = ScopedTarget.image;
    InView.layout = static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return ScopedTarget.view != VK_NULL_HANDLE;
}

bool VulkanRHI::MainDepthTexture(NativeTextureView& InView) const
{
    if (MImageIndex >= MDrawDepthViews.size() || MDrawDepthViews[MImageIndex] == VK_NULL_HANDLE)
    {
        return false;
    }
    InView.view = MDrawDepthViews[MImageIndex];
    InView.image = MDrawDepthImages[MImageIndex];
    InView.layout = static_cast<int>(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    return true;
}

bool VulkanRHI::RenderTargetDepthTexture(RenderTargetHandle InHandle, NativeTextureView& InView) const
{
    if (InHandle.id >= MRenderTargets.size() || MRenderTargets[InHandle.id] == nullptr)
    {
        return false;
    }
    const RenderTarget& ScopedTarget = *MRenderTargets[InHandle.id];
    InView.view = ScopedTarget.depthView;
    InView.image = ScopedTarget.depthImage;
    InView.layout = static_cast<int>(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    return ScopedTarget.depthView != VK_NULL_HANDLE;
}

bool VulkanRHI::TextureArrayLayerView(TextureArrayHandle InHandle, std::uint32_t InLayer,
                                      NativeTextureView& InView) const
{
    const Vk::Api& ScopedApi = Vk::GetApi();
    TextureArray* ScopedArray = const_cast<VulkanRHI*>(this)->FindTextureArray(InHandle.id);
    if (ScopedArray == nullptr || ScopedArray->image == VK_NULL_HANDLE || InLayer >= ScopedArray->layers)
    {
        return false;
    }
    const std::uint64_t ScopedKey = (static_cast<std::uint64_t>(InHandle.id) << 32) | InLayer;
    for (const auto& [Key, View] : MLayerViews)
    {
        if (Key == ScopedKey)
        {
            InView.view = View;
            InView.image = ScopedArray->image;
            InView.layout = static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            return View != VK_NULL_HANDLE;
        }
    }

    VkImageViewCreateInfo ScopedViewInfo{};
    ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedViewInfo.image = ScopedArray->image;
    ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedViewInfo.format = ToVkFormat(ScopedArray->format, ScopedArray->srgb);
    ScopedViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedViewInfo.subresourceRange.baseMipLevel = 0;
    ScopedViewInfo.subresourceRange.levelCount = 1;
    ScopedViewInfo.subresourceRange.baseArrayLayer = InLayer;
    ScopedViewInfo.subresourceRange.layerCount = 1;
    VkImageView ScopedView = VK_NULL_HANDLE;
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &ScopedView) != VK_SUCCESS)
    {
        LogError("vulkan", "TextureArrayLayerView: view creation failed for layer {}", InLayer);
        return false;
    }
    MLayerViews.emplace_back(ScopedKey, ScopedView);
    InView.view = ScopedView;
    InView.layout = static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

void VulkanRHI::SetUiRenderer(UiRenderCallback InCallback, void* InUser)
{
    MUiRenderer = InCallback;
    MUiUser = InUser;
}

bool VulkanRHI::QueryUiBackend(UiBackendInfo& InInfo) const
{
    if (MDevice == VK_NULL_HANDLE)
    {
        return false;
    }
    InInfo.instance = MInstance;
    InInfo.physicalDevice = MPhysical;
    InInfo.device = MDevice;
    InInfo.queue = MQueue;
    InInfo.renderPass = MRenderPass;
    InInfo.pipelineCache = nullptr;
    // The ImGui backend owns its pool
    InInfo.descriptorPool = nullptr;
    InInfo.queueFamily = MQueueFamily;
    InInfo.apiVersion = MInfo.apiVersion != 0 ? MInfo.apiVersion : VK_API_VERSION_1_4;
    InInfo.minImageCount = std::max<std::uint32_t>(2, static_cast<std::uint32_t>(MImages.size()));
    InInfo.imageCount = InInfo.minImageCount;
    InInfo.colorFormat = MFormat;
    InInfo.descriptorPoolSize = 256;
    InInfo.dynamicRendering = false;
    return true;
}

void* VulkanRHI::GetNativeFunction(const char* InName) const
{
    if (InName == nullptr)
    {
        return nullptr;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    // vkGetInstanceProcAddr resolves every command the instance knows, including
    // device-level ones, so it is asked first: that keeps instance-level names away
    // from vkGetDeviceProcAddr, which must return NULL for them and makes the
    // validation layer warn when it does not. Device extensions the instance does
    // not expose still fall through to the device resolver.
    if (MInstance != VK_NULL_HANDLE)
    {
        const auto ScopedFunction = ScopedApi.GetInstanceProcAddr(MInstance, InName);
        if (ScopedFunction != nullptr)
        {
            return reinterpret_cast<void*>(ScopedFunction);
        }
    }
    if (MDevice != VK_NULL_HANDLE)
    {
        return reinterpret_cast<void*>(ScopedApi.vkGetDeviceProcAddr(MDevice, InName));
    }
    return nullptr;
}

void VulkanRHI::RecordDepthPrepass(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket, bool InAccessibility)
{
    if (MDepthPrepassPipeline == VK_NULL_HANDLE || MDepthPrepassRenderPass == VK_NULL_HANDLE)
    {
        return;
    }
    VkFramebuffer ScopedFramebuffer = VK_NULL_HANDLE;
    if (InAccessibility)
    {
        ScopedFramebuffer = MAccessibilityDepthPrepassFramebuffer;
    }
    else if (MImageIndex < MDepthPrepassFramebuffers.size())
    {
        ScopedFramebuffer = MDepthPrepassFramebuffers[MImageIndex];
    }
    if (ScopedFramebuffer == VK_NULL_HANDLE)
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    WriteTimestamp(InCommandBuffer, TimestampDepthStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    const bool ScopedUseCull = MDepthPrepassRequested && MCullReady && !InPacket.items.empty() &&
                               InPacket.items.size() <= kMaxCulledItems &&
                               ScopedApi.vkCmdDrawIndexedIndirectCount != nullptr;
    CullFrame& ScopedCullFrame = MCullFrames[MFrameIndex];
    const bool ScopedReuseCull = ScopedUseCull && MDesc.enableStaticCaches && InPacket.sceneStatic &&
                                 InPacket.gpuOpaqueMain && MHiZReady &&
                                 ScopedCullFrame.SceneVersion == InPacket.sceneVersion;
    VkDeviceSize ScopedSourceBase = 0;
    if (ScopedUseCull && ScopedReuseCull)
    {
        ++MStats.cullCacheHits;
    }
    if (ScopedUseCull && !ScopedReuseCull)
    {
        ScopedSourceBase = UploadPrepassInstances(InCommandBuffer, InPacket);
        RecordOpaqueCull(InCommandBuffer, InPacket, ScopedSourceBase);
    }

    VkClearValue ScopedClear{};
    ScopedClear.depthStencil.depth = 1.0f;
    ScopedClear.depthStencil.stencil = 0;
    VkRenderPassBeginInfo ScopedBegin{};
    ScopedBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBegin.renderPass = MDepthPrepassRenderPass;
    ScopedBegin.framebuffer = ScopedFramebuffer;
    ScopedBegin.renderArea.extent = MExtent;
    ScopedBegin.clearValueCount = 1;
    ScopedBegin.pClearValues = &ScopedClear;
    ScopedApi.vkCmdBeginRenderPass(InCommandBuffer, &ScopedBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(MExtent.width);
    ScopedViewport.height = static_cast<float>(MExtent.height);
    ScopedViewport.minDepth = 0.0f;
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(InCommandBuffer, 0, 1, &ScopedViewport);
    VkRect2D ScopedScissor{};
    ScopedScissor.extent = MExtent;
    ScopedApi.vkCmdSetScissor(InCommandBuffer, 0, 1, &ScopedScissor);
    if (ScopedUseCull)
    {
        CullFrame& ScopedCull = MCullFrames[MFrameIndex];
        ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MDepthPrepassPipeline);
        const VkDeviceSize ScopedZeroOffset = 0;
        ScopedApi.vkCmdBindVertexBuffers(InCommandBuffer, 0, 1, &ScopedCull.VisibleBuffer, &ScopedZeroOffset);
        ScopedApi.vkCmdBindIndexBuffer(InCommandBuffer, MIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
        PushConstants ScopedConstants{};
        ScopedConstants.viewportAndZoom[0] = InPacket.camera.viewport.X;
        ScopedConstants.viewportAndZoom[1] = InPacket.camera.viewport.Y;
        ScopedConstants.viewportAndZoom[2] = InPacket.camera.zoom;
        ScopedConstants.centerAndPadding[0] = InPacket.camera.center.X;
        ScopedConstants.centerAndPadding[1] = InPacket.camera.center.Y;
        ScopedConstants.frame[0] = static_cast<float>(MFrameIndex % kFeedbackSlots);
        ScopedConstants.frame[2] = 0.0f;
        ScopedConstants.material[1] = InPacket.camera.viewport.X;
        ScopedConstants.material[2] = InPacket.camera.viewport.Y;
        ScopedApi.vkCmdPushConstants(InCommandBuffer, MPipelineLayout,
                                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                     sizeof(ScopedConstants), &ScopedConstants);
        if (ScopedReuseCull)
        {
            MStats.depthDrawCalls += static_cast<std::uint32_t>(InPacket.batches.size());
            for (std::uint32_t ScopedBatchIndex = 0; ScopedBatchIndex < InPacket.batches.size(); ++ScopedBatchIndex)
            {
                const VkDeviceSize ScopedCommandOffset =
                    static_cast<VkDeviceSize>(ScopedBatchIndex) * sizeof(VkDrawIndexedIndirectCommand);
                ScopedApi.vkCmdDrawIndexedIndirect(InCommandBuffer, ScopedCull.CommandBuffer, ScopedCommandOffset, 1,
                                                   sizeof(VkDrawIndexedIndirectCommand));
            }
        }
        else
        {
            ScopedApi.vkCmdDrawIndexedIndirectCount(InCommandBuffer, ScopedCull.CommandBuffer, 0,
                                                    ScopedCull.CounterBuffer, 0, 1,
                                                    sizeof(VkDrawIndexedIndirectCommand));
        }
    }
    else if (MDepthPrepassRequested)
    {
        DrawPacket(InCommandBuffer, MDepthPrepassPipeline, InPacket, false, false, 0, true);
    }
    ScopedApi.vkCmdEndRenderPass(InCommandBuffer);
    if (ScopedUseCull && InPacket.gpuOpaqueMain && !ScopedReuseCull)
    {
        RecordHiZ(InCommandBuffer);
        RecordMainCull(InCommandBuffer, InPacket, ScopedSourceBase);
        ScopedCullFrame.SceneVersion = InPacket.sceneVersion;
    }
    WriteTimestamp(InCommandBuffer, TimestampDepthEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

void VulkanRHI::WaitAllInFlight()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    for (const VkFence ScopedFence : MInFlight)
    {
        if (ScopedFence != VK_NULL_HANDLE)
        {
            (void)ScopedApi.vkWaitForFences(MDevice, 1, &ScopedFence, VK_TRUE,
                                            std::numeric_limits<std::uint64_t>::max());
        }
    }
    (void)ScopedApi.vkDeviceWaitIdle(MDevice);
}

void VulkanRHI::PreUploadInstances(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket)
{
    if (InPacket.items.empty() || MInstanceMapped == nullptr || MFrameIndex >= MInstanceSliceValid.size() ||
        MFrameIndex >= MInstanceSliceFrameCounter.size())
    {
        return;
    }
    for (const PreUploadedPacket& ScopedPacket : MPreUploadedPackets)
    {
        if (ScopedPacket.packet == &InPacket)
        {
            return;
        }
    }
    const std::uint32_t ScopedCount = static_cast<std::uint32_t>(InPacket.items.size());
    if (ScopedCount > MInstanceCapacity)
    {
        return;
    }

    bool ScopedOtherStaticPacket = false;
    for (const PreUploadedPacket& ScopedPacket : MPreUploadedPackets)
    {
        if (ScopedPacket.packet != &InPacket && ScopedPacket.packet != nullptr && ScopedPacket.packet->sceneStatic)
        {
            ScopedOtherStaticPacket = true;
            break;
        }
    }
    const VkDeviceSize ScopedStaticBase = static_cast<VkDeviceSize>(MFramesInFlight + MFrameIndex) * MInstanceCapacity;
    const bool ScopedStaticSlot = MFrameIndex < MStaticInstanceSliceValid.size();
    const bool ScopedStaticReuse = InPacket.sceneStatic && ScopedStaticSlot && MStaticInstanceSliceValid[MFrameIndex] &&
                                   MStaticSceneId[MFrameIndex] == InPacket.sceneId &&
                                   MStaticSceneVersion[MFrameIndex] == InPacket.sceneVersion &&
                                   MStaticItemCount[MFrameIndex] == ScopedCount;
    if (InPacket.sceneStatic && !ScopedOtherStaticPacket && ScopedStaticSlot)
    {
        if (ScopedStaticReuse)
        {
            MPreUploadedPackets.push_back(PreUploadedPacket{&InPacket, ScopedStaticBase, ScopedCount});
            return;
        }
        auto* ScopedStaticSlice =
            static_cast<std::byte*>(MInstanceMapped) + ScopedStaticBase * static_cast<VkDeviceSize>(sizeof(RenderItem));
        std::memcpy(ScopedStaticSlice, InPacket.items.data(), InPacket.items.size_bytes());
        VkBufferCopy ScopedCopy{};
        ScopedCopy.srcOffset = ScopedStaticBase * static_cast<VkDeviceSize>(sizeof(RenderItem));
        ScopedCopy.dstOffset = ScopedCopy.srcOffset;
        ScopedCopy.size = static_cast<VkDeviceSize>(InPacket.items.size_bytes());
        Vk::GetApi().vkCmdCopyBuffer(InCommandBuffer, MInstanceStagingBuffer, MInstanceBuffer, 1, &ScopedCopy);
        VkBufferMemoryBarrier ScopedStaticBarrier{};
        ScopedStaticBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        ScopedStaticBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ScopedStaticBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        ScopedStaticBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedStaticBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedStaticBarrier.buffer = MInstanceBuffer;
        ScopedStaticBarrier.offset = 0;
        ScopedStaticBarrier.size = VK_WHOLE_SIZE;
        Vk::GetApi().vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &ScopedStaticBarrier, 0,
                                          nullptr);
        MStats.uploadedBytes += InPacket.items.size_bytes();
        MStaticSceneId[MFrameIndex] = InPacket.sceneId;
        MStaticSceneVersion[MFrameIndex] = InPacket.sceneVersion;
        MStaticItemCount[MFrameIndex] = ScopedCount;
        MStaticInstanceSliceValid[MFrameIndex] = true;
        MPreUploadedPackets.push_back(PreUploadedPacket{&InPacket, ScopedStaticBase, ScopedCount});
        return;
    }
    const VkDeviceSize ScopedSliceBytes = static_cast<VkDeviceSize>(MInstanceCapacity) * sizeof(RenderItem);
    const VkDeviceSize ScopedBase = static_cast<VkDeviceSize>(MFrameIndex) * MInstanceCapacity + MInstanceFrameCursor;
    auto* ScopedSlice = static_cast<std::byte*>(MInstanceMapped) + ScopedSliceBytes * MFrameIndex +
                        static_cast<std::size_t>(MInstanceFrameCursor) * sizeof(RenderItem);
    bool ScopedIncremental = MFramesInFlight == 1 && MPreUploadedPackets.empty() && InPacket.instanceRangesAreDeltas &&
                             MInstanceSliceValid[MFrameIndex] &&
                             MInstanceSliceFrameCounter[MFrameIndex] + 1u == MFrameCounter;
    if (ScopedIncremental)
    {
        for (const DirtyRange& ScopedRange : InPacket.instanceRanges)
        {
            if (ScopedRange.count == 0)
            {
                continue;
            }
            if (static_cast<std::size_t>(ScopedRange.first) + ScopedRange.count > InPacket.items.size())
            {
                ScopedIncremental = false;
                break;
            }
            std::memcpy(ScopedSlice + static_cast<std::size_t>(ScopedRange.first) * sizeof(RenderItem),
                        InPacket.items.data() + ScopedRange.first,
                        static_cast<std::size_t>(ScopedRange.count) * sizeof(RenderItem));
            MStats.uploadedBytes += static_cast<std::uint64_t>(ScopedRange.count) * sizeof(RenderItem);
        }
    }
    if (!ScopedIncremental)
    {
        std::memcpy(ScopedSlice, InPacket.items.data(), InPacket.items.size_bytes());
        MStats.uploadedBytes += InPacket.items.size_bytes();
    }
    MInstanceSliceValid[MFrameIndex] = true;
    MInstanceSliceFrameCounter[MFrameIndex] = MFrameCounter;

    if (ScopedIncremental)
    {
        for (const DirtyRange& ScopedRange : InPacket.instanceRanges)
        {
            if (ScopedRange.count == 0)
            {
                continue;
            }
            VkBufferCopy ScopedRangeCopy{};
            ScopedRangeCopy.srcOffset = (ScopedBase + static_cast<VkDeviceSize>(ScopedRange.first)) *
                                        static_cast<VkDeviceSize>(sizeof(RenderItem));
            ScopedRangeCopy.dstOffset = ScopedRangeCopy.srcOffset;
            ScopedRangeCopy.size = static_cast<VkDeviceSize>(ScopedRange.count) * sizeof(RenderItem);
            Vk::GetApi().vkCmdCopyBuffer(InCommandBuffer, MInstanceStagingBuffer, MInstanceBuffer, 1, &ScopedRangeCopy);
        }
    }
    else
    {
        VkBufferCopy ScopedCopy{};
        ScopedCopy.srcOffset = ScopedBase * static_cast<VkDeviceSize>(sizeof(RenderItem));
        ScopedCopy.dstOffset = ScopedCopy.srcOffset;
        ScopedCopy.size = static_cast<VkDeviceSize>(ScopedCount) * sizeof(RenderItem);
        Vk::GetApi().vkCmdCopyBuffer(InCommandBuffer, MInstanceStagingBuffer, MInstanceBuffer, 1, &ScopedCopy);
    }
    VkBufferMemoryBarrier ScopedBarrier{};
    ScopedBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    ScopedBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ScopedBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.buffer = MInstanceBuffer;
    ScopedBarrier.offset = 0;
    ScopedBarrier.size = VK_WHOLE_SIZE;
    Vk::GetApi().vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &ScopedBarrier, 0, nullptr);

    MPreUploadedPackets.push_back(PreUploadedPacket{&InPacket, ScopedBase, ScopedCount});
    MInstanceFrameCursor += ScopedCount;
}

void VulkanRHI::DrawPacket(VkCommandBuffer InCommandBuffer, VkPipeline InPipeline, const FramePacket& InPacket,
                           bool InOffscreen, bool InSrgb, std::uint32_t InSdfSlot, bool InDepthOnly)
{
    if (InPacket.items.empty())
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    const std::uint32_t ScopedItemCount = static_cast<std::uint32_t>(InPacket.items.size());
    MStats.items += ScopedItemCount;
    MStats.batches += static_cast<std::uint32_t>(InPacket.batches.size());
    // Submit copies the whole packet before the first render pass opens, because
    // buffer copies and barriers are invalid inside a pass. Both the depth pass
    // and the main pass then read the same uploaded slice.
    const PreUploadedPacket* ScopedPreUploaded = nullptr;
    for (const PreUploadedPacket& ScopedPacket : MPreUploadedPackets)
    {
        if (ScopedPacket.packet == &InPacket && ScopedPacket.count == ScopedItemCount)
        {
            ScopedPreUploaded = &ScopedPacket;
            break;
        }
    }
    VkDeviceSize ScopedFirstInstanceBase = 0;
    if (ScopedPreUploaded != nullptr)
    {
        ScopedFirstInstanceBase = ScopedPreUploaded->base;
    }
    else
    {
        if (ScopedItemCount > MInstanceCapacity || MInstanceFrameCursor > MInstanceCapacity - ScopedItemCount)
        {
            LogError("vulkan",
                     "frame passes upload {} items in total, ring holds {} - raise DeviceDesc::maxItemsPerFrame",
                     static_cast<std::uint64_t>(MInstanceFrameCursor) + ScopedItemCount, MInstanceCapacity);
            return;
        }
        // Copy the sorted items into this pass's range of this frame's slice. PIE,
        // the editor viewport and the UI share one frame, so the base advances per
        // pass instead of every pass overwriting the previous upload.
        const VkDeviceSize ScopedSliceBytes = static_cast<VkDeviceSize>(MInstanceCapacity) * sizeof(RenderItem);
        auto* ScopedSlice = static_cast<std::byte*>(MInstanceMapped) + ScopedSliceBytes * MFrameIndex +
                            static_cast<std::size_t>(MInstanceFrameCursor) * sizeof(RenderItem);
        const bool ScopedFirstPassThisFrame = MRecordedPassIndex == 0;
        bool ScopedIncremental = InPacket.instanceRangesAreDeltas && ScopedFirstPassThisFrame &&
                                 MFrameIndex < MInstanceSliceValid.size() && MInstanceSliceValid[MFrameIndex] &&
                                 MInstanceSliceFrameCounter[MFrameIndex] + 1u == MFrameCounter;
        if (ScopedIncremental)
        {
            for (const DirtyRange& ScopedRange : InPacket.instanceRanges)
            {
                if (ScopedRange.count == 0)
                {
                    continue;
                }
                if (static_cast<std::size_t>(ScopedRange.first) + ScopedRange.count > InPacket.items.size())
                {
                    ScopedIncremental = false;
                    break;
                }
                std::memcpy(ScopedSlice + static_cast<std::size_t>(ScopedRange.first) * sizeof(RenderItem),
                            InPacket.items.data() + ScopedRange.first,
                            static_cast<std::size_t>(ScopedRange.count) * sizeof(RenderItem));
                MStats.uploadedBytes += static_cast<std::uint64_t>(ScopedRange.count) * sizeof(RenderItem);
            }
        }
        if (!ScopedIncremental)
        {
            std::memcpy(ScopedSlice, InPacket.items.data(), InPacket.items.size_bytes());
            MStats.uploadedBytes += InPacket.items.size_bytes();
        }
        if (MFrameIndex < MInstanceSliceValid.size())
        {
            MInstanceSliceValid[MFrameIndex] = true;
            if (ScopedFirstPassThisFrame)
            {
                MInstanceSliceFrameCounter[MFrameIndex] = MFrameCounter;
            }
        }
        ScopedFirstInstanceBase = static_cast<VkDeviceSize>(MFrameIndex) * MInstanceCapacity + MInstanceFrameCursor;
        MInstanceFrameCursor += ScopedItemCount;

        // The staging copy is the only host write. From here the vertex stage reads
        // device-local memory, so a discrete GPU never fetches instances over PCIe.
        if (ScopedIncremental)
        {
            for (const DirtyRange& ScopedRange : InPacket.instanceRanges)
            {
                if (ScopedRange.count == 0)
                {
                    continue;
                }
                VkBufferCopy ScopedCopy{};
                ScopedCopy.srcOffset =
                    (ScopedFirstInstanceBase + ScopedRange.first) * static_cast<VkDeviceSize>(sizeof(RenderItem));
                ScopedCopy.dstOffset = ScopedCopy.srcOffset;
                ScopedCopy.size = static_cast<VkDeviceSize>(ScopedRange.count) * sizeof(RenderItem);
                ScopedApi.vkCmdCopyBuffer(InCommandBuffer, MInstanceStagingBuffer, MInstanceBuffer, 1, &ScopedCopy);
            }
        }
        else
        {
            VkBufferCopy ScopedCopy{};
            ScopedCopy.srcOffset = ScopedFirstInstanceBase * static_cast<VkDeviceSize>(sizeof(RenderItem));
            ScopedCopy.dstOffset = ScopedCopy.srcOffset;
            ScopedCopy.size = static_cast<VkDeviceSize>(InPacket.items.size_bytes());
            ScopedApi.vkCmdCopyBuffer(InCommandBuffer, MInstanceStagingBuffer, MInstanceBuffer, 1, &ScopedCopy);
        }
        VkBufferMemoryBarrier ScopedInstanceBarrier{};
        ScopedInstanceBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        ScopedInstanceBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ScopedInstanceBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        ScopedInstanceBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedInstanceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedInstanceBarrier.buffer = MInstanceBuffer;
        ScopedInstanceBarrier.offset = 0;
        ScopedInstanceBarrier.size = VK_WHOLE_SIZE;
        ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &ScopedInstanceBarrier, 0,
                                       nullptr);
    }
    ++MRecordedPassIndex;

    const std::span<const DrawBatch> ScopedBatches = InDepthOnly ? InPacket.depthBatches : InPacket.batches;
    if (InDepthOnly && ScopedBatches.empty())
    {
        return;
    }
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, InPipeline);
    // A sprite batch may carry a material pipeline; the engine quad pipeline is
    // restored whenever the next batch does not ask for one.
    VkPipeline ScopedBoundPipeline = InPipeline;
    // Packet-level array is the default. A batch with a non-zero textureArray
    std::uint32_t ScopedBoundArray = InPacket.textureArray;
    VkDescriptorSet ScopedActiveTexture = DescriptorFor(ScopedBoundArray);
    const std::uint32_t ScopedSdfSlot = std::min(InSdfSlot, SdfViewSlots - 1u);
    const std::uint32_t ScopedSdfOffset = ScopedSdfSlot * SdfBlockStride;
    ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MPipelineLayout, 0, 1,
                                      &ScopedActiveTexture, 1, &ScopedSdfOffset);
    if (MGiReady && MClusterReady && MFrameIndex < MGiFrames.size())
    {
        GiFrame& ScopedGi = MGiFrames[MFrameIndex];
        const std::size_t ScopedProbeCount = std::min<std::size_t>(InPacket.giProbes.size(), kMaxGiProbes);
        MStats.giProbes = static_cast<std::uint32_t>(ScopedProbeCount);
        if (!MDesc.enableStaticCaches || ScopedGi.Version != InPacket.giVersion)
        {
            if (ScopedProbeCount > 0)
            {
                std::memcpy(ScopedGi.ProbeMapped, InPacket.giProbes.data(), ScopedProbeCount * sizeof(GpuGiProbe));
            }
            std::memcpy(ScopedGi.HeaderMapped, &InPacket.giHeader, sizeof(GpuGiVolumeHeader));
            ScopedGi.Version = InPacket.giVersion;
            VkMemoryBarrier ScopedGiBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_HOST_WRITE_BIT,
                                            VK_ACCESS_SHADER_READ_BIT};
            ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_HOST_BIT,
                                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &ScopedGiBarrier, 0, nullptr, 0,
                                           nullptr);
        }
    }
    if (MClusterSetLayout != VK_NULL_HANDLE)
    {
        VkDescriptorSet ScopedClusterSet = MClusterReady && MFrameIndex < MClusterFrames.size()
                                               ? MClusterFrames[MFrameIndex].Set
                                               : MClusterFallbackSet;
        if (ScopedClusterSet != VK_NULL_HANDLE)
        {
            ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MPipelineLayout, 1, 1,
                                              &ScopedClusterSet, 0, nullptr);
        }
    }
    ++MStats.textureBinds;

    const VkDeviceSize ScopedZeroOffset = 0;
    ScopedApi.vkCmdBindVertexBuffers(InCommandBuffer, 0, 1, &MInstanceBuffer, &ScopedZeroOffset);
    ScopedApi.vkCmdBindIndexBuffer(InCommandBuffer, MIndexBuffer, 0, VK_INDEX_TYPE_UINT16);

    PushConstants ScopedConstants{};
    ScopedConstants.viewportAndZoom[0] = InPacket.camera.viewport.X;
    ScopedConstants.viewportAndZoom[1] = InPacket.camera.viewport.Y;
    ScopedConstants.viewportAndZoom[2] = InPacket.camera.zoom;
    ScopedConstants.centerAndPadding[0] = InPacket.camera.center.X;
    ScopedConstants.centerAndPadding[1] = InPacket.camera.center.Y;
    ScopedConstants.vtWorld[0] = InPacket.virtualTextureParams.X;
    ScopedConstants.vtWorld[1] = InPacket.virtualTextureParams.Y;
    ScopedConstants.vtWorld[2] = InPacket.virtualTextureParams.Z;
    ScopedConstants.vtWorld[3] = InPacket.virtualTextureParams.W;
    ScopedConstants.vtLayout[0] = InPacket.virtualTextureLayout.X;
    ScopedConstants.vtLayout[1] = InPacket.virtualTextureLayout.Y;
    ScopedConstants.vtLayout[2] = InPacket.virtualTextureLayout.Z;
    ScopedConstants.vtLayout[3] = InPacket.virtualTextureLayout.W;
    ScopedConstants.frame[0] = static_cast<float>(MFrameIndex % kFeedbackSlots);
    ScopedConstants.frame[1] = static_cast<float>(InPacket.virtualTextureDebugMode);
    ScopedConstants.frame[2] = static_cast<float>(ScopedSdfSlot);
    ScopedConstants.frame[3] = (InPacket.gpuLighting && MClusterReady && !InPacket.lights.empty()) ? 1.0f : 0.0f;
    ScopedConstants.material[0] = InPacket.timeSeconds;
    ScopedConstants.material[1] = InPacket.camera.viewport.X;
    ScopedConstants.material[2] = InPacket.camera.viewport.Y;
    ScopedConstants.material[3] = InPacket.opaqueFrontToBack ? 1.0f : 0.0f;
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MPipelineLayout,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ScopedConstants),
                                 &ScopedConstants);

    const bool ScopedUseGpuCommands = !InDepthOnly && !InOffscreen && InPacket.gpuOpaqueMain &&
                                      InPacket.gpuCulledBatchCount > 0u && InPacket.items.size() <= kMaxCulledItems &&
                                      MDepthPrepassReady && MCullReady && MFrameIndex < MCullFrames.size() &&
                                      ScopedBatches.size() <= kMaxCullBatches;
    bool ScopedNormalVertexBound = false;
    if (ScopedUseGpuCommands)
    {
        CullFrame& ScopedCull = MCullFrames[MFrameIndex];
        const VkDeviceSize ScopedBatchZero = 0;
        ScopedApi.vkCmdBindVertexBuffers(InCommandBuffer, 0, 1, &ScopedCull.VisibleBuffer, &ScopedBatchZero);
        ScopedApi.vkCmdBindIndexBuffer(InCommandBuffer, MIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
        MStats.gpuMainBatches += InPacket.gpuCulledBatchCount;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < InPacket.gpuCulledBatchCount; ++ScopedIndex)
        {
            MStats.gpuMainItems += InPacket.batches[ScopedIndex].itemCount;
        }
    }

    // Fully opaque frames with one pipeline and one atlas need one indirect
    // call, not one per material batch.
    if (ScopedUseGpuCommands && !InDepthOnly && ScopedBatches.size() == InPacket.gpuCulledBatchCount)
    {
        bool ScopedSinglePipeline = true;
        for (const DrawBatch& ScopedBatch : ScopedBatches)
        {
            if (ScopedBatch.pipeline != 0 || ScopedBatch.textureArray != 0)
            {
                ScopedSinglePipeline = false;
                break;
            }
        }
        if (ScopedSinglePipeline && MPipelineOpaque != VK_NULL_HANDLE)
        {
            ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MPipelineOpaque);
            ScopedApi.vkCmdDrawIndexedIndirect(InCommandBuffer, MCullFrames[MFrameIndex].CommandBuffer, 0,
                                               InPacket.gpuCulledBatchCount, sizeof(VkDrawIndexedIndirectCommand));
            ++MStats.drawCalls;
            return;
        }
    }

    // One draw per batch. With a texture array the only batch boundary is the
    if (ScopedBatches.empty())
    {
        ScopedApi.vkCmdDrawIndexed(InCommandBuffer, kQuadIndexCount, static_cast<std::uint32_t>(InPacket.items.size()),
                                   0, 0, static_cast<std::uint32_t>(ScopedFirstInstanceBase));
        ++MStats.drawCalls;
        if (InDepthOnly)
        {
            MStats.depthItems += static_cast<std::uint32_t>(InPacket.items.size());
            ++MStats.depthDrawCalls;
        }
        return;
    }

    for (std::uint32_t ScopedBatchIndex = 0; ScopedBatchIndex < ScopedBatches.size(); ++ScopedBatchIndex)
    {
        const DrawBatch& ScopedBatch = ScopedBatches[ScopedBatchIndex];
        if (ScopedBatch.itemCount == 0)
        {
            continue;
        }
        VkPipeline ScopedBatchPipeline = InPipeline;
        if (!InDepthOnly && !InOffscreen && (MDepthPrepassReady || InPacket.opaqueFrontToBack) &&
            InPacket.gpuOpaqueMain && MPipelineOpaque != VK_NULL_HANDLE &&
            ScopedBatchIndex < InPacket.gpuCulledBatchCount)
        {
            ScopedBatchPipeline = MPipelineOpaque;
        }
        if (!InDepthOnly && ScopedBatch.pipeline != 0)
        {
            const VkPipeline ScopedMaterial = ResolveMaterialPipeline(ScopedBatch.pipeline, InOffscreen, InSrgb);
            if (ScopedMaterial != VK_NULL_HANDLE)
            {
                ScopedBatchPipeline = ScopedMaterial;
            }
        }
        if (ScopedBatchPipeline != ScopedBoundPipeline)
        {
            ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ScopedBatchPipeline);
            ScopedBoundPipeline = ScopedBatchPipeline;
        }
        // Full 32-bit array id: no narrowing cast. Id 0 inherits the packet
        const std::uint32_t ScopedRequestedArray =
            ScopedBatch.textureArray != 0 ? ScopedBatch.textureArray : InPacket.textureArray;
        if (ScopedRequestedArray != ScopedBoundArray)
        {
            // Honest native atlas switch: font array, sprite array or a custom
            ScopedActiveTexture = DescriptorFor(ScopedRequestedArray);
            ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MPipelineLayout, 0, 1,
                                              &ScopedActiveTexture, 1, &ScopedSdfOffset);
            ScopedBoundArray = ScopedRequestedArray;
            ++MStats.textureBinds;
        }
        if (ScopedUseGpuCommands && ScopedBatchIndex < InPacket.gpuCulledBatchCount)
        {
            const VkDeviceSize ScopedCommandOffset =
                static_cast<VkDeviceSize>(ScopedBatchIndex) * sizeof(VkDrawIndexedIndirectCommand);
            ScopedApi.vkCmdDrawIndexedIndirect(InCommandBuffer, MCullFrames[MFrameIndex].CommandBuffer,
                                               ScopedCommandOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
        }
        else
        {
            if (ScopedUseGpuCommands && !ScopedNormalVertexBound)
            {
                const VkDeviceSize ScopedInstanceZero = 0;
                ScopedApi.vkCmdBindVertexBuffers(InCommandBuffer, 0, 1, &MInstanceBuffer, &ScopedInstanceZero);
                ScopedNormalVertexBound = true;
            }
            ScopedApi.vkCmdDrawIndexed(InCommandBuffer, kQuadIndexCount, ScopedBatch.itemCount, 0, 0,
                                       static_cast<std::uint32_t>(ScopedFirstInstanceBase + ScopedBatch.firstItem));
        }
        ++MStats.drawCalls;
        if (InDepthOnly)
        {
            MStats.depthItems += ScopedBatch.itemCount;
            ++MStats.depthDrawCalls;
        }
    }
}

void VulkanRHI::RecordMeshSkinning(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket)
{
    MMeshDrawCount = 0;
    MMeshStaticDrawCount = 0;
    MMeshDynamicDrawCount = 0;
    MMeshDynamicVertexCount = 0;
    MMeshSkinRangeCount = 0;
    MMeshGroups.clear();
    MMeshDrawOrder.clear();
    if (InPacket.meshItems.empty())
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    MeshFrameBuffers& ScopedFrame = MMeshFrames[MFrameIndex];

    // Static meshes are filled by one-shot transfers in earlier submissions and
    // the streaming rings by host writes; make both visible to the vertex
    // input, the skinning compute and the indirect read before touching them.
    {
        VkMemoryBarrier ScopedUploadBarrier{};
        ScopedUploadBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        ScopedUploadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        ScopedUploadBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
                                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                                       0, 1, &ScopedUploadBarrier, 0, nullptr, 0, nullptr);
    }

    const std::uint32_t ScopedBoneBase = MMeshFrameBoneCursor;
    if (InPacket.boneMatrices.size() > MBoneMatrixCapacity ||
        ScopedBoneBase > MBoneMatrixCapacity - InPacket.boneMatrices.size())
    {
        LogError("vulkan",
                 "mesh frame passes upload {} bone matrices in total, ring holds {} - raise "
                 "maxBoneMatricesPerFrame",
                 static_cast<std::uint64_t>(ScopedBoneBase) + InPacket.boneMatrices.size(), MBoneMatrixCapacity);
        return;
    }
    if (!InPacket.boneMatrices.empty())
    {
        // The whole pass palette is one contiguous block after the previous one.
        auto* ScopedBoneTarget =
            static_cast<std::byte*>(ScopedFrame.boneMapped) + static_cast<std::size_t>(ScopedBoneBase) * sizeof(Mat4);
        std::memcpy(ScopedBoneTarget, InPacket.boneMatrices.data(), InPacket.boneMatrices.size_bytes());
        MStats.uploadedBytes += InPacket.boneMatrices.size_bytes();
    }

    std::uint32_t ScopedVertexCursor = MMeshFrameVertexCursor;
    const std::uint32_t ScopedSkinnedVertexBase = ScopedVertexCursor;
    std::uint32_t ScopedIndexCursor = MMeshFrameIndexCursor;
    const std::uint32_t ScopedPassIndexStart = ScopedIndexCursor;
    const std::uint32_t ScopedDynamicIndexBase = MStaticIndexCapacity + MFrameIndex * MMeshIndexCapacity;

    // Skinned vertices must stay contiguous at the front of the ring: one
    std::uint32_t ScopedSkinnedVertexTotal = 0;
    for (const MeshRenderItem& ScopedItem : InPacket.meshItems)
    {
        if (ScopedItem.mesh.IsValid() || ScopedItem.vertices.empty())
        {
            continue;
        }
        const bool ScopedUnskinned = IsUnskinnedMeshItem(ScopedItem);
        if (!ScopedUnskinned)
        {
            ScopedSkinnedVertexTotal += static_cast<std::uint32_t>(ScopedItem.vertices.size());
        }
    }
    std::uint32_t ScopedUnskinnedCursor = 0;

    for (const MeshRenderItem& ScopedItem : InPacket.meshItems)
    {
        if (MMeshDrawCount >= MMeshDrawCapacity)
        {
            LogError("vulkan", "mesh draw count exceeds {} - raise maxMeshDrawsPerFrame", MMeshDrawCapacity);
            break;
        }

        const bool ScopedIsStatic = ScopedItem.mesh.IsValid();
        const bool ScopedUnskinned = !ScopedIsStatic && IsUnskinnedMeshItem(ScopedItem);
        std::uint32_t ScopedVertexBase = 0;
        std::uint32_t ScopedFirstIndex = 0;
        std::uint32_t ScopedIndexCount = 0;

        if (ScopedIsStatic)
        {
            if (ScopedItem.mesh.id >= MStaticMeshes.size() || !MStaticMeshes[ScopedItem.mesh.id].alive)
            {
                LogError("vulkan", "mesh item references a stale MeshHandle #{}", ScopedItem.mesh.id);
                continue;
            }
            const StaticMeshRecord& ScopedMesh = MStaticMeshes[ScopedItem.mesh.id];
            ScopedVertexBase = ScopedMesh.vertexOffset;
            ScopedFirstIndex = ScopedMesh.indexOffset;
            ScopedIndexCount = ScopedMesh.indexCount;
            ++MMeshStaticDrawCount;
        }
        else
        {
            if (ScopedItem.vertices.empty() || ScopedItem.indices.empty())
            {
                continue;
            }
            const auto ScopedVertexCount = static_cast<std::uint32_t>(ScopedItem.vertices.size());
            const auto ScopedIndexCountForItem = static_cast<std::uint32_t>(ScopedItem.indices.size());
            if (ScopedIndexCursor + ScopedIndexCountForItem > MMeshIndexCapacity)
            {
                LogError("vulkan", "mesh frame exceeds index ring capacity");
                break;
            }

            if (ScopedUnskinned)
            {
                // CPU-deformed geometry: copy the bind-pose vertices into the
                if (ScopedSkinnedVertexBase + ScopedSkinnedVertexTotal + ScopedUnskinnedCursor + ScopedVertexCount >
                    MMeshVertexCapacity)
                {
                    LogError("vulkan", "unskinned mesh frame exceeds vertex ring capacity");
                    break;
                }
                auto* ScopedTargetVertices = static_cast<MeshVertex*>(ScopedFrame.vertexMapped) +
                                             ScopedSkinnedVertexBase + ScopedSkinnedVertexTotal + ScopedUnskinnedCursor;
                std::memcpy(ScopedTargetVertices, ScopedItem.vertices.data(), ScopedItem.vertices.size_bytes());
                ScopedVertexBase = ScopedSkinnedVertexBase + ScopedSkinnedVertexTotal + ScopedUnskinnedCursor;
                ScopedUnskinnedCursor += ScopedVertexCount;
            }
            else
            {
                if (ScopedVertexCursor + ScopedVertexCount > MMeshVertexCapacity ||
                    ScopedVertexCursor + ScopedVertexCount > ScopedSkinnedVertexBase + ScopedSkinnedVertexTotal)
                {
                    LogError("vulkan", "skinned mesh frame exceeds vertex ring capacity");
                    break;
                }
                if (ScopedItem.boneOffset + ScopedItem.boneCount > InPacket.boneMatrices.size())
                {
                    LogError("vulkan", "mesh item bone range [{}, {}) is outside the frame palette ({} bones)",
                             ScopedItem.boneOffset, ScopedItem.boneOffset + ScopedItem.boneCount,
                             InPacket.boneMatrices.size());
                    continue;
                }
                if (MMeshSkinRangeCount >= MMeshDrawCapacity)
                {
                    LogError("vulkan", "mesh skin range table exhausted");
                    break;
                }

                auto* ScopedTargetVertices = static_cast<MeshVertex*>(ScopedFrame.vertexMapped) + ScopedVertexCursor;
                std::memcpy(ScopedTargetVertices, ScopedItem.vertices.data(), ScopedItem.vertices.size_bytes());

                // The only per-vertex indirection in the dispatch: every skinned
                const std::uint32_t ScopedRangeIndex = MMeshSkinRangeCount;
                for (std::uint32_t ScopedVertex = 0; ScopedVertex < ScopedVertexCount; ++ScopedVertex)
                {
                    ScopedTargetVertices[ScopedVertex].padding = ScopedRangeIndex;
                }

                static_cast<MeshSkinRange*>(ScopedFrame.skinRangeMapped)[MMeshSkinRangeCount] =
                    MeshSkinRange{ScopedBoneBase + ScopedItem.boneOffset, ScopedItem.boneCount};
                ++MMeshSkinRangeCount;
                ScopedVertexBase = ScopedVertexCursor;
                ScopedVertexCursor += ScopedVertexCount;
                ++MMeshDynamicDrawCount;
            }

            // Indices are shared by skinned and unskinned draws. The staging
            auto* ScopedTargetIndices = static_cast<std::uint16_t*>(ScopedFrame.indexMapped) + ScopedIndexCursor;
            std::memcpy(ScopedTargetIndices, ScopedItem.indices.data(), ScopedItem.indices.size_bytes());
            ScopedFirstIndex = ScopedDynamicIndexBase + ScopedIndexCursor;
            ScopedIndexCount = ScopedIndexCountForItem;
            ScopedIndexCursor += ScopedIndexCountForItem;
            if ((ScopedIndexCursor & 1u) != 0u && ScopedIndexCursor < MMeshIndexCapacity)
            {
                static_cast<std::uint16_t*>(ScopedFrame.indexMapped)[ScopedIndexCursor] = 0;
                ++ScopedIndexCursor;
            }
            MStats.uploadedBytes += ScopedItem.vertices.size_bytes() + ScopedItem.indices.size_bytes();
        }

        MeshDrawRecord& ScopedRecord = MMeshDraws[MMeshDrawCount++];
        ScopedRecord.vertexBase = ScopedVertexBase;
        ScopedRecord.firstIndex = ScopedFirstIndex;
        ScopedRecord.indexCount = ScopedIndexCount;
        ScopedRecord.textureArray = ScopedItem.textureArray;
        ScopedRecord.layer = ScopedItem.layer;
        ScopedRecord.model = ScopedItem.model;
        ScopedRecord.staticMesh = ScopedIsStatic;
        ScopedRecord.unskinned = ScopedUnskinned;
        ScopedRecord.materialPipeline = ScopedItem.materialPipeline;
        MMeshDrawOrder.push_back(MMeshDrawCount - 1);
    }

    // Commit the ranges so the next recorded pass starts after this one.
    MMeshFrameVertexCursor = ScopedSkinnedVertexBase + ScopedSkinnedVertexTotal + ScopedUnskinnedCursor;
    MMeshFrameIndexCursor = ScopedIndexCursor;
    MMeshFrameBoneCursor = ScopedBoneBase + static_cast<std::uint32_t>(InPacket.boneMatrices.size());

    if (MMeshDrawCount == 0)
    {
        return;
    }

    // Only this pass's contiguous skinned range is dispatched.
    MMeshDynamicVertexCount = ScopedVertexCursor - ScopedSkinnedVertexBase;

    const std::uint32_t ScopedPassIndexCount = ScopedIndexCursor - ScopedPassIndexStart;
    if (ScopedPassIndexCount > 0)
    {
        // Dynamic indices live in the staging ring; copy this pass's range into
        // the frame's index ring at its own offset.
        VkBufferCopy ScopedIndexCopy{};
        ScopedIndexCopy.srcOffset = 0;
        ScopedIndexCopy.dstOffset =
            static_cast<VkDeviceSize>(ScopedDynamicIndexBase + ScopedPassIndexStart) * sizeof(std::uint16_t);
        ScopedIndexCopy.size = static_cast<VkDeviceSize>(ScopedPassIndexCount) * sizeof(std::uint16_t);
        ScopedApi.vkCmdCopyBuffer(InCommandBuffer, ScopedFrame.indexBuffer, MIndexPool, 1, &ScopedIndexCopy);
    }

    // One compute dispatch skins every dynamic range in the frame. Static
    if (MMeshDynamicVertexCount > 0)
    {
        ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MMeshComputePipeline);
        ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MMeshComputeLayout, 0, 1,
                                          &ScopedFrame.computeSet, 0, nullptr);

        MeshComputePush ScopedPush;
        ScopedPush.vertexCount = MMeshDynamicVertexCount;
        ScopedPush.vertexOffset = ScopedSkinnedVertexBase;
        ScopedApi.vkCmdPushConstants(InCommandBuffer, MMeshComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                     sizeof(ScopedPush), &ScopedPush);
        const std::uint32_t ScopedGroups = (MMeshDynamicVertexCount + 63u) / 64u;
        ScopedApi.vkCmdDispatch(InCommandBuffer, ScopedGroups, 1, 1);

        // Compute writes must be visible to the vertex shader. One barrier
        VkMemoryBarrier ScopedBarrier{};
        ScopedBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        ScopedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1,
                                       &ScopedBarrier, 0, nullptr, 0, nullptr);

        MStats.skinDispatches = 1;
        MStats.skinnedVertices += MMeshDynamicVertexCount;
        MStats.skinRanges += MMeshSkinRangeCount;
    }

    if (ScopedPassIndexCount > 0)
    {
        // The dynamic index copy is a transfer, not a shader write. Make it
        VkMemoryBarrier ScopedIndexBarrier{};
        ScopedIndexBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        ScopedIndexBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ScopedIndexBarrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
        ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &ScopedIndexBarrier, 0, nullptr, 0,
                                       nullptr);
    }

    // Group by (geometry class, texture array, layer). Within a group the
    std::sort(MMeshDrawOrder.begin(), MMeshDrawOrder.end(),
              [this](std::uint32_t InLeft, std::uint32_t InRight)
              {
                  const MeshDrawRecord& ScopedA = MMeshDraws[InLeft];
                  const MeshDrawRecord& ScopedB = MMeshDraws[InRight];
                  if (ScopedA.textureArray != ScopedB.textureArray)
                  {
                      return ScopedA.textureArray < ScopedB.textureArray;
                  }
                  if (ScopedA.layer != ScopedB.layer)
                  {
                      return ScopedA.layer < ScopedB.layer;
                  }
                  // Material pipelines split a layer into contiguous indirect
                  if (ScopedA.materialPipeline != ScopedB.materialPipeline)
                  {
                      return ScopedA.materialPipeline < ScopedB.materialPipeline;
                  }
                  // Stable-ish tie-break: static first, then submission order.
                  return ScopedA.staticMesh > ScopedB.staticMesh;
              });

    auto* ScopedCommands = static_cast<VkDrawIndexedIndirectCommand*>(ScopedFrame.indirectMapped);
    auto* ScopedDrawData = static_cast<MeshDrawData*>(ScopedFrame.drawDataMapped);
    std::uint32_t ScopedCommandCursor = 0;
    for (std::uint32_t ScopedSortedIndex = 0; ScopedSortedIndex < MMeshDrawCount; ++ScopedSortedIndex)
    {
        const std::uint32_t ScopedRecordIndex = MMeshDrawOrder[ScopedSortedIndex];
        MeshDrawRecord& ScopedRecord = MMeshDraws[ScopedRecordIndex];
        ScopedRecord.drawDataIndex = ScopedSortedIndex;

        MeshDrawData ScopedData;
        ScopedData.model = ScopedRecord.model;
        ScopedData.vertexBase = ScopedRecord.vertexBase;
        ScopedData.layer = ScopedRecord.layer;
        ScopedData.flags = ScopedRecord.staticMesh ? MeshDrawStatic : 0u;
        if (ScopedRecord.unskinned)
        {
            ScopedData.flags |= MeshDrawUnskinned;
        }
        ScopedData.padding = 0;
        ScopedDrawData[ScopedSortedIndex] = ScopedData;

        const bool ScopedStartsGroup = MMeshGroups.empty() ||
                                       MMeshGroups.back().textureArray != ScopedRecord.textureArray ||
                                       MMeshGroups.back().layer != ScopedRecord.layer ||
                                       MMeshGroups.back().materialPipeline != ScopedRecord.materialPipeline;
        if (ScopedStartsGroup)
        {
            MMeshGroups.push_back(MeshIndirectGroup{ScopedRecord.textureArray, ScopedRecord.layer,
                                                    ScopedRecord.materialPipeline, ScopedCommandCursor, 0});
        }
        MeshIndirectGroup& ScopedGroup = MMeshGroups.back();
        ++ScopedGroup.commandCount;

        VkDrawIndexedIndirectCommand& ScopedCommand = ScopedCommands[ScopedCommandCursor++];
        ScopedCommand.indexCount = ScopedRecord.indexCount;
        ScopedCommand.instanceCount = 1;
        ScopedCommand.firstIndex = ScopedRecord.firstIndex;
        ScopedCommand.vertexOffset = static_cast<std::int32_t>(ScopedRecord.vertexBase);
        // gl_InstanceIndex = firstInstance + 0. This is the draw record index,
        ScopedCommand.firstInstance = ScopedSortedIndex;
    }

    MStats.meshItems += static_cast<std::uint32_t>(InPacket.meshItems.size());
    MStats.meshDraws += MMeshDrawCount;
    MStats.meshIndirectCalls += static_cast<std::uint32_t>(MMeshGroups.size());
    MStats.staticMeshDraws += MMeshStaticDrawCount;

    if (!MMeshPipelineLogged)
    {
        MMeshPipelineLogged = true;
        LogInfo("vulkan",
                "mesh path active: {} draws ({} static, {} skinned), {} indirect calls, {} skin dispatches, {} verts",
                MMeshDrawCount, MMeshStaticDrawCount, MMeshDynamicDrawCount, MMeshGroups.size(), MStats.skinDispatches,
                MMeshDynamicVertexCount);
    }
}

void VulkanRHI::DrawMeshes(VkCommandBuffer InCommandBuffer, VkPipeline InPipeline, const FramePacket& InPacket,
                           bool InOffscreen, bool InSrgb, std::uint32_t InSdfSlot)
{
    if (MMeshDrawCount == 0 || InPipeline == VK_NULL_HANDLE)
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    const MeshFrameBuffers& ScopedFrame = MMeshFrames[MFrameIndex];

    Mat4 ScopedViewProjection;
    if (InPacket.meshUseViewProjection)
    {
        ScopedViewProjection = InPacket.meshViewProjection;
    }
    else
    {
        // Legacy orthographic camera. Column-major: x' = 2*zoom/vw * x + tx,
        const float ScopedScaleX =
            InPacket.camera.viewport.X > 0.0f ? (2.0f * InPacket.camera.zoom) / InPacket.camera.viewport.X : 0.0f;
        const float ScopedScaleY =
            InPacket.camera.viewport.Y > 0.0f ? (-2.0f * InPacket.camera.zoom) / InPacket.camera.viewport.Y : 0.0f;
        ScopedViewProjection.m[0] = ScopedScaleX;
        ScopedViewProjection.m[5] = ScopedScaleY;
        ScopedViewProjection.m[12] = -InPacket.camera.center.X * ScopedScaleX;
        ScopedViewProjection.m[13] = -InPacket.camera.center.Y * ScopedScaleY;
    }

    MeshGraphicsPush ScopedPush;
    ScopedPush.viewProjection = ScopedViewProjection;
    ScopedPush.materialParams =
        Vector4D{InPacket.timeSeconds, InPacket.camera.viewport.X, InPacket.camera.viewport.Y, 0.0f};

    // Static and skinned commands share this pool, so every material/layer is a
    ScopedApi.vkCmdBindIndexBuffer(InCommandBuffer, MIndexPool, 0, VK_INDEX_TYPE_UINT16);
    ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MMeshGraphicsLayout, 1, 1,
                                      &ScopedFrame.graphicsSet, 0, nullptr);
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MMeshGraphicsLayout,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ScopedPush),
                                 &ScopedPush);

    VkPipeline ScopedBoundPipeline = VK_NULL_HANDLE;
    for (const MeshIndirectGroup& ScopedGroup : MMeshGroups)
    {
        if (ScopedGroup.commandCount == 0)
        {
            continue;
        }
        // MaterialGraph pipelines are created for the same pipeline layout and
        VkPipeline ScopedGroupPipeline = InPipeline;
        if (ScopedGroup.materialPipeline != kDefaultMaterialPipeline)
        {
            const VkPipeline ScopedMaterial =
                ResolveMaterialPipeline(ScopedGroup.materialPipeline, InOffscreen, InSrgb);
            if (ScopedMaterial != VK_NULL_HANDLE)
            {
                ScopedGroupPipeline = ScopedMaterial;
            }
        }
        if (ScopedGroupPipeline != ScopedBoundPipeline)
        {
            ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ScopedGroupPipeline);
            ScopedBoundPipeline = ScopedGroupPipeline;
        }
        const VkDescriptorSet ScopedAtlas = DescriptorFor(ScopedGroup.textureArray);
        // Set 0 carries the shared SDF block as a dynamic uniform buffer, so the
        // bind must supply the slot offset even when the mesh does not read it.
        const std::uint32_t ScopedSdfOffset = std::min(InSdfSlot, SdfViewSlots - 1u) * SdfBlockStride;
        ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MMeshGraphicsLayout, 0, 1,
                                          &ScopedAtlas, 1, &ScopedSdfOffset);
        ScopedApi.vkCmdDrawIndexedIndirect(InCommandBuffer, ScopedFrame.indirectBuffer,
                                           static_cast<VkDeviceSize>(ScopedGroup.firstCommand) *
                                               sizeof(VkDrawIndexedIndirectCommand),
                                           ScopedGroup.commandCount, sizeof(VkDrawIndexedIndirectCommand));
        ++MStats.drawCalls;
    }
}

bool VulkanRHI::RecordComputePass(std::string_view InPipeline, const void* InConstants, std::uint32_t InConstantsBytes,
                                  Extent2D InExtent)
{
    if (!MInFrame || MPassOpen || MSdfComputePipeline == VK_NULL_HANDLE || MSdfMaskImage == VK_NULL_HANDLE)
    {
        return false;
    }
    const bool ScopedIsShadow = InPipeline == "sdf_shadow" || InPipeline == "sdf_shadow.comp";
    if (!ScopedIsShadow || InConstants == nullptr || InConstantsBytes < kSdfShadowBlockBytes)
    {
        return false;
    }
    // The mask is created at device init and on swapchain resize, before any
    // command buffer records. Recreating it here would rewrite descriptor sets
    // that the recording command buffer already bound, which invalidates it.
    const Extent2D ScopedMask{std::max(InExtent.width / 2u, 1u), std::max(InExtent.height / 2u, 1u)};
    if (MSdfMaskView == VK_NULL_HANDLE)
    {
        if (!EnsureSdfMask(ScopedMask))
        {
            return false;
        }
    }
    SdfShadowConstants ScopedConstants{};
    std::memcpy(&ScopedConstants, InConstants, kSdfShadowBlockBytes);
    const std::uint32_t ScopedView =
        std::min(static_cast<std::uint32_t>(std::max(ScopedConstants.encode.Z, 0.0f)), SdfMaxViews - 1u);
    const std::uint32_t ScopedSlot = MFrameIndex * SdfMaxViews + ScopedView;
    if (MSdfComputeMapped[ScopedSlot] == nullptr)
    {
        return false;
    }
    ScopedConstants.encode.W = static_cast<float>(ScopedSlot);
    std::memcpy(MSdfComputeMapped[ScopedSlot], &ScopedConstants, kSdfShadowBlockBytes);

    // Recorded into the frame command buffer before the sprite render pass, so
    // the mask writes happen-before the fragment samples.
    const VkCommandBuffer ScopedCommand = MCommandBuffers[MFrameIndex];
    Vk::Api& ScopedApi = Vk::GetApi();
    ScopedApi.vkCmdBindPipeline(ScopedCommand, VK_PIPELINE_BIND_POINT_COMPUTE, MSdfComputePipeline);
    ScopedApi.vkCmdBindDescriptorSets(ScopedCommand, VK_PIPELINE_BIND_POINT_COMPUTE, MSdfComputeLayout, 0, 1,
                                      &MSdfComputeSets[ScopedSlot], 0, nullptr);
    ScopedApi.vkCmdDispatch(ScopedCommand, (ScopedMask.width + 7u) / 8u, (ScopedMask.height + 7u) / 8u, 1);
    // The mask is written by the compute stage and sampled by the sprite pass in
    // the same command buffer: make the write visible to fragment reads.
    VkImageMemoryBarrier ScopedBarrier{};
    ScopedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ScopedBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    ScopedBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.image = MSdfMaskImage;
    ScopedBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, SdfViewSlots};
    ScopedApi.vkCmdPipelineBarrier(ScopedCommand, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ScopedBarrier);
    MSdfMaskPublished[ScopedSlot] = true;
    return true;
}

bool VulkanRHI::RecordGraphicsPass(std::string_view InPipeline, const void* InConstants, std::uint32_t InConstantsBytes,
                                   Extent2D InExtent, std::uint32_t InInstanceCount)
{
    (void)InExtent;
    if (!MInFrame || MPassOpen || InConstants == nullptr || InInstanceCount != 1)
    {
        return false;
    }
    if (InPipeline != "postfx_composite.frag" && InPipeline != "postfx_composite")
    {
        return false;
    }
    if (!MPostFxReady || InConstantsBytes < sizeof(PostFxBlock))
    {
        return false;
    }
    std::memcpy(&MPostFxRecordBlock, InConstants, sizeof(PostFxBlock));
    MPostFxRecordPending = true;
    return true;
}

void VulkanRHI::Submit(const FramePacket& InPacket)
{
    FBY_ASSERT(MInFrame);

    Vk::Api& ScopedApi = Vk::GetApi();
    const VkCommandBuffer ScopedCommandBuffer = MCommandBuffers[MFrameIndex];

    if (MPassOpen)
    {
        LogError("vulkan", "Submit called more than once per frame");
        return;
    }
    MStats.oitItems = 0;
    MStats.oitMode = InPacket.oitMode;
    for (const RenderItem& ScopedItem : InPacket.items)
    {
        if ((ScopedItem.flags & static_cast<std::uint32_t>(RenderFlags::Oit)) != 0u)
        {
            ++MStats.oitItems;
        }
    }
    MBlindFrame = (InPacket.accessibility.flags & AccessibilityBlind) != 0;
    // Every scene packet writes its own SDF slot, so editor and PIE no longer
    // overwrite each other before the GPU executes the recorded draws.
    const std::uint32_t ScopedSdfSlot = ApplySdfLighting(InPacket.sdf);
    ApplyVirtualTexture(InPacket);
    PreUploadInstances(ScopedCommandBuffer, InPacket);

    // Compute skinning must run outside the render pass. In BlindMode the
    if (!MBlindFrame)
    {
        RecordMeshSkinning(ScopedCommandBuffer, InPacket);
        WriteTimestamp(ScopedCommandBuffer, TimestampClusterStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        RecordClusters(ScopedCommandBuffer, InPacket);
        WriteTimestamp(ScopedCommandBuffer, TimestampClusterEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }

    VkClearValue ScopedClearValues[2]{};
    ScopedClearValues[0].color = {
        {static_cast<float>(InPacket.clear.R()) / 255.0f, static_cast<float>(InPacket.clear.G()) / 255.0f,
         static_cast<float>(InPacket.clear.B()) / 255.0f, static_cast<float>(InPacket.clear.A()) / 255.0f}};
    ScopedClearValues[1].depthStencil.depth = 1.0f;
    ScopedClearValues[1].depthStencil.stencil = 0;

    MAccessibilityFrame = InPacket.accessibility;
    MAccessibilitySwapActive = MAccessibilitySwapReady && AccessibilityCompositeWanted(InPacket);
    // The post-process composite needs the scene in an offscreen image; it is
    // skipped when the accessibility composite owns the swapchain path.
    MPostFxThisFrame = (InPacket.postFxEnabled || MPostFxRecordPending) && MPostFxReady && !MAccessibilitySwapActive;
    MPostFxSettings = InPacket.postFx;
    RecordDepthPrepass(ScopedCommandBuffer, InPacket, MAccessibilitySwapActive || MPostFxThisFrame);
    WriteTimestamp(ScopedCommandBuffer, TimestampMainStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    VkRenderPassBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBeginInfo.renderPass = MRenderPass;
    ScopedBeginInfo.framebuffer =
        (MAccessibilitySwapActive || MPostFxThisFrame) ? MAccessibilitySwapFramebuffer : MFramebuffers[MImageIndex];
    ScopedBeginInfo.renderArea.extent = MExtent;
    ScopedBeginInfo.clearValueCount = 2;
    ScopedBeginInfo.pClearValues = ScopedClearValues;

    ScopedApi.vkCmdBeginRenderPass(ScopedCommandBuffer, &ScopedBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    MPassOpen = true;
    MMainPassRecorded = true;

    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(MExtent.width);
    ScopedViewport.height = static_cast<float>(MExtent.height);
    ScopedViewport.minDepth = 0.0f;
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(ScopedCommandBuffer, 0, 1, &ScopedViewport);

    VkRect2D ScopedScissor{};
    ScopedScissor.extent = MExtent;
    ScopedApi.vkCmdSetScissor(ScopedCommandBuffer, 0, 1, &ScopedScissor);

    if (!MBlindFrame)
    {
        DrawPacket(ScopedCommandBuffer, MPipeline, InPacket, false, false, ScopedSdfSlot);
        DrawMeshes(ScopedCommandBuffer, MMeshPipeline, InPacket, false, false, ScopedSdfSlot);
        RecordOitResolve(ScopedCommandBuffer, MRenderPass, InPacket);
    }
}

bool VulkanRHI::DrawOverlay(const FramePacket& InPacket)
{
    if (!MInFrame || (!MPassOpen && !MInHudPass) || InPacket.items.empty())
    {
        return false;
    }
    DrawPacket(MCommandBuffers[MFrameIndex], MPipeline, InPacket, false, false, 0);
    return true;
}

bool VulkanRHI::UploadMotionVectors(std::span<const MotionVectorItem> InItems)
{
    MMotionCount = static_cast<std::uint32_t>(InItems.size());
    if (InItems.empty())
    {
        return true;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MMotionCapacity < InItems.size())
    {
        if (MMotionMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, MMotionMemory);
            MMotionMapped = nullptr;
        }
        if (MMotionBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MMotionBuffer, nullptr);
            MMotionBuffer = VK_NULL_HANDLE;
        }
        if (MMotionMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MMotionMemory, nullptr);
            MMotionMemory = VK_NULL_HANDLE;
        }
        const VkDeviceSize ScopedBytes = static_cast<VkDeviceSize>(InItems.size()) * sizeof(MotionVectorItem);
        if (!CreateBuffer(ScopedBytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &MMotionBuffer,
                          &MMotionMemory) ||
            ScopedApi.vkMapMemory(MDevice, MMotionMemory, 0, ScopedBytes, 0, &MMotionMapped) != VK_SUCCESS)
        {
            return false;
        }
        MMotionCapacity = static_cast<std::uint32_t>(InItems.size());
    }
    if (MMotionMapped == nullptr)
    {
        return false;
    }
    std::memcpy(MMotionMapped, InItems.data(), InItems.size() * sizeof(MotionVectorItem));
    VkDescriptorBufferInfo ScopedBufferInfo{};
    ScopedBufferInfo.buffer = MMotionBuffer;
    ScopedBufferInfo.offset = 0;
    ScopedBufferInfo.range = static_cast<VkDeviceSize>(InItems.size()) * sizeof(MotionVectorItem);
    VkWriteDescriptorSet ScopedWrite{};
    ScopedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrite.dstSet = MMotionSets[MFrameIndex];
    ScopedWrite.dstBinding = 0;
    ScopedWrite.descriptorCount = 1;
    ScopedWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedWrite.pBufferInfo = &ScopedBufferInfo;
    ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedWrite, 0, nullptr);
    return true;
}

bool VulkanRHI::CopyColorImage(RenderTargetHandle InSource, RenderTargetHandle InDestination)
{
    if (!MInFrame || MPassOpen)
    {
        return false;
    }
    RenderTarget* ScopedSource = FindRenderTarget(InSource.id);
    RenderTarget* ScopedDestination = FindRenderTarget(InDestination.id);
    if (ScopedSource == nullptr || ScopedDestination == nullptr)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkImageMemoryBarrier ScopedBarriers[2]{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        ScopedBarriers[ScopedIndex].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ScopedBarriers[ScopedIndex].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ScopedBarriers[ScopedIndex].newLayout =
            ScopedIndex == 0 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ScopedBarriers[ScopedIndex].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedBarriers[ScopedIndex].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedBarriers[ScopedIndex].image = ScopedIndex == 0 ? ScopedSource->image : ScopedDestination->image;
        ScopedBarriers[ScopedIndex].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ScopedBarriers[ScopedIndex].dstAccessMask =
            ScopedIndex == 0 ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    ScopedApi.vkCmdPipelineBarrier(MCommandBuffers[MFrameIndex], VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, ScopedBarriers);
    VkImageCopy ScopedCopy{};
    ScopedCopy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ScopedCopy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ScopedCopy.extent = {std::min(ScopedSource->extent.width, ScopedDestination->extent.width),
                         std::min(ScopedSource->extent.height, ScopedDestination->extent.height), 1};
    ScopedApi.vkCmdCopyImage(MCommandBuffers[MFrameIndex], ScopedSource->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             ScopedDestination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedCopy);
    for (VkImageMemoryBarrier& ScopedBarrier : ScopedBarriers)
    {
        ScopedBarrier.oldLayout = ScopedBarrier.newLayout;
        ScopedBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ScopedBarrier.srcAccessMask = ScopedBarrier.dstAccessMask;
        ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    ScopedApi.vkCmdPipelineBarrier(MCommandBuffers[MFrameIndex], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, ScopedBarriers);
    return true;
}

bool VulkanRHI::CopyDepth(RenderTargetHandle InSource, RenderTargetHandle InDestination)
{
    if (!MInFrame || MPassOpen)
    {
        return false;
    }
    RenderTarget* ScopedSource = FindRenderTarget(InSource.id);
    RenderTarget* ScopedDestination = FindRenderTarget(InDestination.id);
    if (ScopedSource == nullptr || ScopedDestination == nullptr || ScopedSource->depthImage == VK_NULL_HANDLE ||
        ScopedDestination->depthImage == VK_NULL_HANDLE)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkImageMemoryBarrier ScopedBarriers[2]{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        ScopedBarriers[ScopedIndex].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ScopedBarriers[ScopedIndex].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        ScopedBarriers[ScopedIndex].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ScopedBarriers[ScopedIndex].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedBarriers[ScopedIndex].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedBarriers[ScopedIndex].image = ScopedIndex == 0 ? ScopedSource->depthImage : ScopedDestination->depthImage;
        ScopedBarriers[ScopedIndex].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        ScopedBarriers[ScopedIndex].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }
    ScopedBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ScopedBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ScopedApi.vkCmdPipelineBarrier(MCommandBuffers[MFrameIndex], VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, ScopedBarriers);
    VkImageCopy ScopedCopy{};
    ScopedCopy.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    ScopedCopy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    ScopedCopy.extent = {std::min(ScopedSource->extent.width, ScopedDestination->extent.width),
                         std::min(ScopedSource->extent.height, ScopedDestination->extent.height), 1};
    ScopedApi.vkCmdCopyImage(MCommandBuffers[MFrameIndex], ScopedSource->depthImage,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ScopedDestination->depthImage,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedCopy);
    for (VkImageMemoryBarrier& ScopedBarrier : ScopedBarriers)
    {
        ScopedBarrier.oldLayout = ScopedBarrier.newLayout;
        ScopedBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        ScopedBarrier.srcAccessMask = ScopedBarrier.dstAccessMask;
        ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    ScopedApi.vkCmdPipelineBarrier(MCommandBuffers[MFrameIndex], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, ScopedBarriers);
    return true;
}

bool VulkanRHI::TemporalResolve(RenderTargetHandle InCurrent, RenderTargetHandle InHistory, RenderTargetHandle InMotion,
                                RenderTargetHandle InDepth, RenderTargetHandle InHistoryDepth,
                                RenderTargetHandle InOutput, float InHistoryWeight, float InMotionScale,
                                bool InResetHistory)
{
    if (!MInFrame || MTemporalLayout == VK_NULL_HANDLE)
    {
        return false;
    }
    RenderTarget* ScopedCurrent = FindRenderTarget(InCurrent.id);
    RenderTarget* ScopedHistory = FindRenderTarget(InHistory.id);
    RenderTarget* ScopedMotion = FindRenderTarget(InMotion.id);
    RenderTarget* ScopedDepth = FindRenderTarget(InDepth.id);
    RenderTarget* ScopedHistoryDepth = FindRenderTarget(InHistoryDepth.id);
    RenderTarget* ScopedOutput = FindRenderTarget(InOutput.id);
    if (ScopedCurrent == nullptr || ScopedHistory == nullptr || ScopedMotion == nullptr || ScopedDepth == nullptr ||
        ScopedHistoryDepth == nullptr || ScopedOutput == nullptr)
    {
        return false;
    }
    VkPipeline ScopedPipeline = VK_NULL_HANDLE;
    const auto ScopedFound = MTemporalPipelines.find(ScopedOutput->renderPass);
    if (ScopedFound != MTemporalPipelines.end())
    {
        ScopedPipeline = ScopedFound->second;
    }
    else if (CreateGraphicsPipelineForPass(ScopedOutput->renderPass, MTemporalLayout, Shaders::fullscreen_vert.data(),
                                           Shaders::fullscreen_vert.size() * sizeof(std::uint32_t),
                                           Shaders::temporal_resolve.data(),
                                           Shaders::temporal_resolve.size() * sizeof(std::uint32_t), &ScopedPipeline))
    {
        MTemporalPipelines.emplace(ScopedOutput->renderPass, ScopedPipeline);
    }
    if (ScopedPipeline == VK_NULL_HANDLE)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorImageInfo ScopedImages[5]{};
    const VkImageView ScopedViews[5] = {ScopedCurrent->view, ScopedHistory->view, ScopedMotion->view,
                                        ScopedDepth->depthView, ScopedHistoryDepth->depthView};
    VkWriteDescriptorSet ScopedWrites[5]{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 5; ++ScopedIndex)
    {
        ScopedImages[ScopedIndex].sampler = MHudSampler;
        ScopedImages[ScopedIndex].imageView = ScopedViews[ScopedIndex];
        ScopedImages[ScopedIndex].imageLayout = ScopedIndex >= 3 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                                 : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ScopedWrites[ScopedIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[ScopedIndex].dstSet = MTemporalSets[MFrameIndex];
        ScopedWrites[ScopedIndex].dstBinding = ScopedIndex;
        ScopedWrites[ScopedIndex].descriptorCount = 1;
        ScopedWrites[ScopedIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrites[ScopedIndex].pImageInfo = &ScopedImages[ScopedIndex];
    }
    ScopedApi.vkUpdateDescriptorSets(MDevice, 5, ScopedWrites, 0, nullptr);
    VkClearValue ScopedClear[2]{};
    ScopedClear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    ScopedClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo ScopedBegin{};
    ScopedBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBegin.renderPass = ScopedOutput->renderPass;
    ScopedBegin.framebuffer = ScopedOutput->framebuffer;
    ScopedBegin.renderArea.extent = {ScopedOutput->extent.width, ScopedOutput->extent.height};
    ScopedBegin.clearValueCount = 2;
    ScopedBegin.pClearValues = ScopedClear;
    ScopedApi.vkCmdBeginRenderPass(MCommandBuffers[MFrameIndex], &ScopedBegin, VK_SUBPASS_CONTENTS_INLINE);
    ScopedApi.vkCmdBindPipeline(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, ScopedPipeline);
    ScopedApi.vkCmdBindDescriptorSets(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, MTemporalLayout, 0,
                                      1, &MTemporalSets[MFrameIndex], 0, nullptr);
    struct TemporalPush
    {
        float texel[2];
        float historyWeight;
        float motionScale;
        float resetHistory;
        float padding[3];
    } ScopedPush{};
    ScopedPush.texel[0] = 1.0f / static_cast<float>(ScopedCurrent->extent.width);
    ScopedPush.texel[1] = 1.0f / static_cast<float>(ScopedCurrent->extent.height);
    ScopedPush.historyWeight = InHistoryWeight;
    ScopedPush.motionScale = InMotionScale;
    ScopedPush.resetHistory = InResetHistory ? 1.0f : 0.0f;
    ScopedApi.vkCmdPushConstants(MCommandBuffers[MFrameIndex], MTemporalLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                 sizeof(ScopedPush), &ScopedPush);
    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(ScopedOutput->extent.width);
    ScopedViewport.height = static_cast<float>(ScopedOutput->extent.height);
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(MCommandBuffers[MFrameIndex], 0, 1, &ScopedViewport);
    VkRect2D ScopedScissor{};
    ScopedScissor.extent = {ScopedOutput->extent.width, ScopedOutput->extent.height};
    ScopedApi.vkCmdSetScissor(MCommandBuffers[MFrameIndex], 0, 1, &ScopedScissor);
    ScopedApi.vkCmdDraw(MCommandBuffers[MFrameIndex], 3, 1, 0, 0);
    ScopedApi.vkCmdEndRenderPass(MCommandBuffers[MFrameIndex]);
    return true;
}

bool VulkanRHI::ClearMotionVectors(RenderTargetHandle InTarget)
{
    if (!MInFrame)
    {
        return false;
    }
    RenderTarget* ScopedTarget = FindRenderTarget(InTarget.id);
    if (ScopedTarget == nullptr || ScopedTarget->framebuffer == VK_NULL_HANDLE)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkClearValue ScopedClear[2]{};
    ScopedClear[0].color = {{0.5f, 0.5f, 0.0f, 1.0f}};
    ScopedClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo ScopedBegin{};
    ScopedBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBegin.renderPass = ScopedTarget->renderPass;
    ScopedBegin.framebuffer = ScopedTarget->framebuffer;
    ScopedBegin.renderArea.extent = {ScopedTarget->extent.width, ScopedTarget->extent.height};
    ScopedBegin.clearValueCount = 2;
    ScopedBegin.pClearValues = ScopedClear;
    ScopedApi.vkCmdBeginRenderPass(MCommandBuffers[MFrameIndex], &ScopedBegin, VK_SUBPASS_CONTENTS_INLINE);
    ScopedApi.vkCmdEndRenderPass(MCommandBuffers[MFrameIndex]);
    return true;
}

bool VulkanRHI::RenderMotionVectors(RenderTargetHandle InTarget, std::uint32_t InCount, Vector2D InCameraCenter,
                                    Vector2D InPreviousCameraCenter, float InCameraZoom, float InPreviousCameraZoom,
                                    Vector2D InJitter, Vector2D InPreviousJitter, Vector2D InViewport)
{
    if (!MInFrame || InCount == 0 || MMotionLayout == VK_NULL_HANDLE || MMotionSets[MFrameIndex] == VK_NULL_HANDLE)
    {
        return false;
    }
    RenderTarget* ScopedTarget = FindRenderTarget(InTarget.id);
    if (ScopedTarget == nullptr || ScopedTarget->framebuffer == VK_NULL_HANDLE)
    {
        return false;
    }
    VkPipeline ScopedPipeline = VK_NULL_HANDLE;
    const auto ScopedFound = MMotionPipelines.find(ScopedTarget->renderPass);
    if (ScopedFound != MMotionPipelines.end())
    {
        ScopedPipeline = ScopedFound->second;
    }
    else if (CreateGraphicsPipelineForPass(ScopedTarget->renderPass, MMotionLayout, Shaders::motion.data(),
                                           Shaders::motion.size() * sizeof(std::uint32_t), Shaders::motion_frag.data(),
                                           Shaders::motion_frag.size() * sizeof(std::uint32_t), &ScopedPipeline))
    {
        MMotionPipelines.emplace(ScopedTarget->renderPass, ScopedPipeline);
    }
    if (ScopedPipeline == VK_NULL_HANDLE)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkClearValue ScopedClear[2]{};
    ScopedClear[0].color = {{0.5f, 0.5f, 0.0f, 1.0f}};
    ScopedClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo ScopedBegin{};
    ScopedBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBegin.renderPass = ScopedTarget->renderPass;
    ScopedBegin.framebuffer = ScopedTarget->framebuffer;
    ScopedBegin.renderArea.extent = {ScopedTarget->extent.width, ScopedTarget->extent.height};
    ScopedBegin.clearValueCount = 2;
    ScopedBegin.pClearValues = ScopedClear;
    ScopedApi.vkCmdBeginRenderPass(MCommandBuffers[MFrameIndex], &ScopedBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(ScopedTarget->extent.width);
    ScopedViewport.height = static_cast<float>(ScopedTarget->extent.height);
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(MCommandBuffers[MFrameIndex], 0, 1, &ScopedViewport);
    VkRect2D ScopedScissor{};
    ScopedScissor.extent = {ScopedTarget->extent.width, ScopedTarget->extent.height};
    ScopedApi.vkCmdSetScissor(MCommandBuffers[MFrameIndex], 0, 1, &ScopedScissor);
    ScopedApi.vkCmdBindPipeline(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, ScopedPipeline);
    ScopedApi.vkCmdBindDescriptorSets(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, MMotionLayout, 0,
                                      1, &MMotionSets[MFrameIndex], 0, nullptr);
    struct MotionPush
    {
        float camera[2];
        float previousCamera[2];
        float viewport[2];
        float cameraZoom;
        float previousCameraZoom;
        float jitter[2];
        float previousJitter[2];
    } ScopedPush{};
    ScopedPush.camera[0] = InCameraCenter.X;
    ScopedPush.camera[1] = InCameraCenter.Y;
    ScopedPush.previousCamera[0] = InPreviousCameraCenter.X;
    ScopedPush.previousCamera[1] = InPreviousCameraCenter.Y;
    ScopedPush.viewport[0] = InViewport.X;
    ScopedPush.viewport[1] = InViewport.Y;
    ScopedPush.cameraZoom = InCameraZoom;
    ScopedPush.previousCameraZoom = InPreviousCameraZoom;
    ScopedPush.jitter[0] = InJitter.X;
    ScopedPush.jitter[1] = InJitter.Y;
    ScopedPush.previousJitter[0] = InPreviousJitter.X;
    ScopedPush.previousJitter[1] = InPreviousJitter.Y;
    ScopedApi.vkCmdPushConstants(MCommandBuffers[MFrameIndex], MMotionLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                 sizeof(ScopedPush), &ScopedPush);
    ScopedApi.vkCmdDraw(MCommandBuffers[MFrameIndex], 6, InCount, 0, 0);
    ScopedApi.vkCmdEndRenderPass(MCommandBuffers[MFrameIndex]);
    return true;
}

bool VulkanRHI::RenderHudTarget()
{
    if (!MInFrame || MHudTargetId == 0xFFFFFFFFu || MHudLayout == VK_NULL_HANDLE)
    {
        return false;
    }
    RenderTarget* ScopedHud = FindRenderTarget(MHudTargetId);
    if (ScopedHud == nullptr || ScopedHud->view == VK_NULL_HANDLE)
    {
        return false;
    }
    if (MHudPipeline == VK_NULL_HANDLE)
    {
        if (!CreateGraphicsPipelineForPass(MRenderPass, MHudLayout, Shaders::fullscreen_vert.data(),
                                           Shaders::fullscreen_vert.size() * sizeof(std::uint32_t),
                                           Shaders::hud_composite.data(),
                                           Shaders::hud_composite.size() * sizeof(std::uint32_t), &MHudPipeline))
        {
            return false;
        }
    }

    Vk::Api& ScopedApi = Vk::GetApi();
    VkClearValue ScopedClear[2]{};
    ScopedClear[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    ScopedClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo ScopedBegin{};
    ScopedBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBegin.renderPass = ScopedHud->renderPass;
    ScopedBegin.framebuffer = ScopedHud->framebuffer;
    ScopedBegin.renderArea.extent = {ScopedHud->extent.width, ScopedHud->extent.height};
    ScopedBegin.clearValueCount = 2;
    ScopedBegin.pClearValues = ScopedClear;
    ScopedApi.vkCmdBeginRenderPass(MCommandBuffers[MFrameIndex], &ScopedBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(ScopedHud->extent.width);
    ScopedViewport.height = static_cast<float>(ScopedHud->extent.height);
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(MCommandBuffers[MFrameIndex], 0, 1, &ScopedViewport);
    VkRect2D ScopedScissor{};
    ScopedScissor.extent = {ScopedHud->extent.width, ScopedHud->extent.height};
    ScopedApi.vkCmdSetScissor(MCommandBuffers[MFrameIndex], 0, 1, &ScopedScissor);
    MInHudPass = true;
    if (MUiRenderer != nullptr)
    {
        MUiRenderer(MUiUser, MCommandBuffers[MFrameIndex]);
    }
    MInHudPass = false;
    ScopedApi.vkCmdEndRenderPass(MCommandBuffers[MFrameIndex]);
    MSkipMainUi = true;
    return true;
}

bool VulkanRHI::CompositeHud()
{
    if (!MInFrame || !MPassOpen || MHudTargetId == 0xFFFFFFFFu || MHudLayout == VK_NULL_HANDLE ||
        MHudPipeline == VK_NULL_HANDLE)
    {
        return false;
    }
    RenderTarget* ScopedHud = FindRenderTarget(MHudTargetId);
    if (ScopedHud == nullptr || ScopedHud->view == VK_NULL_HANDLE)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorImageInfo ScopedImage{};
    ScopedImage.sampler = MHudNearest ? MHudNearestSampler : MHudSampler;
    ScopedImage.imageView = ScopedHud->view;
    ScopedImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet ScopedWrite{};
    ScopedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrite.dstSet = HudSet();
    ScopedWrite.dstBinding = 0;
    ScopedWrite.descriptorCount = 1;
    ScopedWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrite.pImageInfo = &ScopedImage;
    ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedWrite, 0, nullptr);

    VkViewport ScopedMainViewport{};
    ScopedMainViewport.width = static_cast<float>(MExtent.width);
    ScopedMainViewport.height = static_cast<float>(MExtent.height);
    ScopedMainViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(MCommandBuffers[MFrameIndex], 0, 1, &ScopedMainViewport);
    VkRect2D ScopedMainScissor{};
    ScopedMainScissor.extent = MExtent;
    ScopedApi.vkCmdSetScissor(MCommandBuffers[MFrameIndex], 0, 1, &ScopedMainScissor);
    ScopedApi.vkCmdBindPipeline(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, MHudPipeline);
    ScopedApi.vkCmdBindDescriptorSets(MCommandBuffers[MFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, MHudLayout, 0, 1,
                                      &HudSetRef(), 0, nullptr);
    ScopedApi.vkCmdDraw(MCommandBuffers[MFrameIndex], 3, 1, 0, 0);
    return true;
}

void VulkanRHI::EndFrame()
{
    FBY_ASSERT(MInFrame);
    Vk::Api& ScopedApi = Vk::GetApi();
    const VkCommandBuffer ScopedCommandBuffer = MCommandBuffers[MFrameIndex];

    if (!MMainPassRecorded && MPresentTargetId == 0xFFFFFFFFu)
    {
        // A frame that only rendered offscreen targets never wrote the acquired
        // swapchain image. Present a defined clear instead of undefined memory.
        FramePacket ScopedEmpty{};
        RecordDepthPrepass(ScopedCommandBuffer, ScopedEmpty, false);
        VkClearValue ScopedClearValues[2]{};
        ScopedClearValues[0].color.float32[0] = 0.0f;
        ScopedClearValues[0].color.float32[1] = 0.0f;
        ScopedClearValues[0].color.float32[2] = 0.0f;
        ScopedClearValues[0].color.float32[3] = 1.0f;
        ScopedClearValues[1].depthStencil.depth = 1.0f;
        ScopedClearValues[1].depthStencil.stencil = 0;
        VkRenderPassBeginInfo ScopedBeginInfo{};
        ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        ScopedBeginInfo.renderPass = MRenderPass;
        ScopedBeginInfo.framebuffer = MFramebuffers[MImageIndex];
        ScopedBeginInfo.renderArea.extent = MExtent;
        ScopedBeginInfo.clearValueCount = 2;
        ScopedBeginInfo.pClearValues = ScopedClearValues;
        ScopedApi.vkCmdBeginRenderPass(ScopedCommandBuffer, &ScopedBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        ScopedApi.vkCmdEndRenderPass(ScopedCommandBuffer);
    }

    if (MPassOpen)
    {
        // UI last: the game has already been recorded into the main pass and
        if (MUiRenderer != nullptr && !MBlindFrame && !MSkipMainUi && MUiVisible)
        {
            MUiRenderer(MUiUser, ScopedCommandBuffer);
        }
        WriteTimestamp(ScopedCommandBuffer, TimestampMainEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        ScopedApi.vkCmdEndRenderPass(ScopedCommandBuffer);
        MPassOpen = false;
    }
    if (MAccessibilitySwapActive && MAccessibilitySwapReady)
    {
        // The scene pass ended in PRESENT_SRC_KHR because it shares MRenderPass;
        // move it to a samplable layout before the composite reads it.
        VkImageMemoryBarrier ScopedBarrier{};
        ScopedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ScopedBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        ScopedBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ScopedBarrier.image = MAccessibilitySwapImage;
        ScopedBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                       &ScopedBarrier);
        RecordAccessibilityComposite(ScopedCommandBuffer, MRenderPass, MFramebuffers[MImageIndex],
                                     MAccessibilitySwapPipeline, MAccessibilitySwapSet,
                                     Extent2D{MExtent.width, MExtent.height}, MAccessibilityFrame, true);
        MAccessibilitySwapActive = false;
    }
    WriteTimestamp(ScopedCommandBuffer, TimestampPostFxStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    if (MPostFxRecordPending)
    {
        RecordPostFxComposite(ScopedCommandBuffer, MPostFxRecordBlock, Extent2D{MExtent.width, MExtent.height});
        MPostFxRecordPending = false;
        MPostFxThisFrame = false;
    }
    else if (MPostFxThisFrame && MPostFxReady)
    {
        RecordPostFxComposite(ScopedCommandBuffer, MPostFxSettings, Extent2D{MExtent.width, MExtent.height});
        MPostFxThisFrame = false;
    }
    WriteTimestamp(ScopedCommandBuffer, TimestampPostFxEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    if (MPresentTargetId != 0xFFFFFFFFu)
    {
        const RenderTarget* ScopedTarget = FindRenderTarget(MPresentTargetId);
        if (ScopedTarget != nullptr && ScopedTarget->image != VK_NULL_HANDLE && MImageIndex < MImages.size())
        {
            VkImageMemoryBarrier ScopedSourceBarrier{};
            ScopedSourceBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ScopedSourceBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            ScopedSourceBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            ScopedSourceBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ScopedSourceBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            ScopedSourceBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ScopedSourceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ScopedSourceBarrier.image = ScopedTarget->image;
            ScopedSourceBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkImageMemoryBarrier ScopedDestinationBarrier{};
            ScopedDestinationBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ScopedDestinationBarrier.srcAccessMask = 0;
            ScopedDestinationBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            ScopedDestinationBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ScopedDestinationBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            ScopedDestinationBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ScopedDestinationBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ScopedDestinationBarrier.image = MImages[MImageIndex];
            ScopedDestinationBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            const VkImageMemoryBarrier ScopedBarriers[] = {ScopedSourceBarrier, ScopedDestinationBarrier};
            ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                                           ScopedBarriers);

            VkImageBlit ScopedBlit{};
            ScopedBlit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            ScopedBlit.srcOffsets[1] = {static_cast<std::int32_t>(ScopedTarget->extent.width),
                                        static_cast<std::int32_t>(ScopedTarget->extent.height), 1};
            ScopedBlit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            ScopedBlit.dstOffsets[1] = {static_cast<std::int32_t>(MExtent.width),
                                        static_cast<std::int32_t>(MExtent.height), 1};
            ScopedApi.vkCmdBlitImage(ScopedCommandBuffer, ScopedTarget->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     MImages[MImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedBlit,
                                     VK_FILTER_LINEAR);

            ScopedSourceBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            ScopedSourceBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            ScopedSourceBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            ScopedSourceBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ScopedDestinationBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            ScopedDestinationBarrier.dstAccessMask = 0;
            ScopedDestinationBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            ScopedDestinationBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            const VkImageMemoryBarrier ScopedFinalBarriers[] = {ScopedSourceBarrier, ScopedDestinationBarrier};
            ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 2,
                                           ScopedFinalBarriers);
        }
    }
    WriteTimestamp(ScopedCommandBuffer, TimestampFrameEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    if (ScopedApi.vkEndCommandBuffer(ScopedCommandBuffer) != VK_SUCCESS)
    {
        LogError("vulkan", "vkEndCommandBuffer failed; stopping the frame loop");
        MInFrame = false;
        MDeviceLost = true;
        return;
    }

    VkSemaphore ScopedWaitSemaphores[] = {MImageAvailable[MFrameIndex]};
    VkPipelineStageFlags ScopedWaitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore ScopedSignalSemaphores[] = {MRenderFinished[MImageIndex]};

    VkSubmitInfo ScopedSubmitInfo{};
    ScopedSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    ScopedSubmitInfo.waitSemaphoreCount = 1;
    ScopedSubmitInfo.pWaitSemaphores = ScopedWaitSemaphores;
    ScopedSubmitInfo.pWaitDstStageMask = ScopedWaitStages;
    ScopedSubmitInfo.commandBufferCount = 1;
    ScopedSubmitInfo.pCommandBuffers = &ScopedCommandBuffer;
    ScopedSubmitInfo.signalSemaphoreCount = 1;
    ScopedSubmitInfo.pSignalSemaphores = ScopedSignalSemaphores;

    // Reset the fence immediately before the only submit that signals it.
    (void)ScopedApi.vkResetFences(MDevice, 1, &MInFlight[MFrameIndex]);
    if (ScopedApi.vkQueueSubmit(MQueue, 1, &ScopedSubmitInfo, MInFlight[MFrameIndex]) != VK_SUCCESS)
    {
        LogError("vulkan", "vkQueueSubmit failed; stopping the frame loop");
        MInFrame = false;
        MDeviceLost = true;
        return;
    }

    VkPresentInfoKHR ScopedPresentInfo{};
    ScopedPresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    ScopedPresentInfo.waitSemaphoreCount = 1;
    ScopedPresentInfo.pWaitSemaphores = ScopedSignalSemaphores;
    ScopedPresentInfo.swapchainCount = 1;
    ScopedPresentInfo.pSwapchains = &MSwapchain;
    ScopedPresentInfo.pImageIndices = &MImageIndex;

    const VkResult ScopedPresent = ScopedApi.vkQueuePresentKHR(MQueue, &ScopedPresentInfo);
    if (ScopedPresent == VK_ERROR_OUT_OF_DATE_KHR || ScopedPresent == VK_SUBOPTIMAL_KHR)
    {
        (void)RecreateSwapchain(MDesc.size);
    }

    MSkipMainUi = false;
    MInFrame = false;
}

void VulkanRHI::DestroyMeshResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        return;
    }
    if (MMeshComputePipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MMeshComputePipeline, nullptr);
        MMeshComputePipeline = VK_NULL_HANDLE;
    }
    if (MMeshPipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MMeshPipeline, nullptr);
        MMeshPipeline = VK_NULL_HANDLE;
    }
    if (MMeshComputeLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipelineLayout(MDevice, MMeshComputeLayout, nullptr);
        MMeshComputeLayout = VK_NULL_HANDLE;
    }
    if (MMeshGraphicsLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipelineLayout(MDevice, MMeshGraphicsLayout, nullptr);
        MMeshGraphicsLayout = VK_NULL_HANDLE;
    }
    if (MMeshDescriptorPool != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorPool(MDevice, MMeshDescriptorPool, nullptr);
        MMeshDescriptorPool = VK_NULL_HANDLE;
        for (MeshFrameBuffers& ScopedFrame : MMeshFrames)
        {
            ScopedFrame.computeSet = VK_NULL_HANDLE;
            ScopedFrame.graphicsSet = VK_NULL_HANDLE;
        }
    }
    if (MMeshGraphicsSetLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MMeshGraphicsSetLayout, nullptr);
        MMeshGraphicsSetLayout = VK_NULL_HANDLE;
    }
    if (MMeshComputeSetLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MMeshComputeSetLayout, nullptr);
        MMeshComputeSetLayout = VK_NULL_HANDLE;
    }
    for (MeshFrameBuffers& ScopedFrame : MMeshFrames)
    {
        if (ScopedFrame.vertexMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, ScopedFrame.vertexMemory);
            ScopedFrame.vertexMapped = nullptr;
        }
        if (ScopedFrame.indexMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, ScopedFrame.indexMemory);
            ScopedFrame.indexMapped = nullptr;
        }
        if (ScopedFrame.boneMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, ScopedFrame.boneMemory);
            ScopedFrame.boneMapped = nullptr;
        }
        if (ScopedFrame.skinRangeMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, ScopedFrame.skinRangeMemory);
            ScopedFrame.skinRangeMapped = nullptr;
        }
        if (ScopedFrame.drawDataMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, ScopedFrame.drawDataMemory);
            ScopedFrame.drawDataMapped = nullptr;
        }
        if (ScopedFrame.indirectMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, ScopedFrame.indirectMemory);
            ScopedFrame.indirectMapped = nullptr;
        }
        const auto ScopedDestroy = [&ScopedApi, this](VkBuffer& InBuffer, VkDeviceMemory& InMemory)
        {
            if (InBuffer != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyBuffer(MDevice, InBuffer, nullptr);
                InBuffer = VK_NULL_HANDLE;
            }
            if (InMemory != VK_NULL_HANDLE)
            {
                ScopedApi.vkFreeMemory(MDevice, InMemory, nullptr);
                InMemory = VK_NULL_HANDLE;
            }
        };
        ScopedDestroy(ScopedFrame.vertexBuffer, ScopedFrame.vertexMemory);
        ScopedDestroy(ScopedFrame.indexBuffer, ScopedFrame.indexMemory);
        ScopedDestroy(ScopedFrame.skinnedBuffer, ScopedFrame.skinnedMemory);
        ScopedDestroy(ScopedFrame.boneBuffer, ScopedFrame.boneMemory);
        ScopedDestroy(ScopedFrame.skinRangeBuffer, ScopedFrame.skinRangeMemory);
        ScopedDestroy(ScopedFrame.drawDataBuffer, ScopedFrame.drawDataMemory);
        ScopedDestroy(ScopedFrame.indirectBuffer, ScopedFrame.indirectMemory);
    }
    if (MIndexPool != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyBuffer(MDevice, MIndexPool, nullptr);
        MIndexPool = VK_NULL_HANDLE;
    }
    if (MIndexPoolMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MIndexPoolMemory, nullptr);
        MIndexPoolMemory = VK_NULL_HANDLE;
    }
    DestroyStaticMeshArena();
}

void VulkanRHI::DestroyStaticMeshArena() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        return;
    }
    if (MStaticVertexBuffer != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyBuffer(MDevice, MStaticVertexBuffer, nullptr);
        MStaticVertexBuffer = VK_NULL_HANDLE;
    }
    if (MStaticVertexMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MStaticVertexMemory, nullptr);
        MStaticVertexMemory = VK_NULL_HANDLE;
    }
    MStaticVertexFree.clear();
    MStaticIndexFree.clear();
    MStaticMeshes.clear();
    MFreeStaticMeshIds.clear();
}

void VulkanRHI::Shutdown()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    MDeviceLost = false;
    if (MDevice != VK_NULL_HANDLE)
    {
        ScopedApi.vkDeviceWaitIdle(MDevice);
    }

    if (MDevice != VK_NULL_HANDLE)
    {
        if (MInstanceMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, MInstanceStagingMemory);
            MInstanceMapped = nullptr;
        }
        DestroySdfShadowResources();
        DestroyGiResources();
        DestroyClusterFallback();
        DestroyClusterResources();
        DestroyCullResources();
        DestroyVtFallbackResources();
        if (MInstanceStagingBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MInstanceStagingBuffer, nullptr);
            ScopedApi.vkFreeMemory(MDevice, MInstanceStagingMemory, nullptr);
            MInstanceStagingBuffer = VK_NULL_HANDLE;
            MInstanceStagingMemory = VK_NULL_HANDLE;
        }
        if (MInstanceBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MInstanceBuffer, nullptr);
            ScopedApi.vkFreeMemory(MDevice, MInstanceMemory, nullptr);
            MInstanceBuffer = VK_NULL_HANDLE;
            MInstanceMemory = VK_NULL_HANDLE;
        }
        if (MIndexBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MIndexBuffer, nullptr);
            ScopedApi.vkFreeMemory(MDevice, MIndexMemory, nullptr);
            MIndexBuffer = VK_NULL_HANDLE;
            MIndexMemory = VK_NULL_HANDLE;
        }
        for (std::unique_ptr<TextureArray>& ScopedArray : MTextureArrays)
        {
            if (ScopedArray != nullptr)
            {
                DestroyTextureArrayResources(*ScopedArray);
                ScopedArray.reset();
            }
        }
        MTextureArrays.clear();
        MFreeTextureArrayIds.clear();
        for (std::unique_ptr<VirtualTextureResource>& ScopedTexture : MVirtualTextures)
        {
            if (ScopedTexture != nullptr)
            {
                DestroyVirtualTextureResources(*ScopedTexture);
                ScopedTexture.reset();
            }
        }
        MVirtualTextures.clear();
        MFreeVirtualTextureIds.clear();
        for (std::unique_ptr<RenderTarget>& ScopedTarget : MRenderTargets)
        {
            if (ScopedTarget != nullptr)
            {
                DestroyRenderTargetResources(*ScopedTarget);
                ScopedTarget.reset();
            }
        }
        MRenderTargets.clear();
        MFreeRenderTargetIds.clear();
        DestroyMaterialPipelines();
        if (MUploadMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, MUploadMemory);
            MUploadMapped = nullptr;
        }
        if (MUploadFence != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyFence(MDevice, MUploadFence, nullptr);
            MUploadFence = VK_NULL_HANDLE;
        }
        if (MUploadCommandBuffer != VK_NULL_HANDLE && MCommandPool != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeCommandBuffers(MDevice, MCommandPool, 1, &MUploadCommandBuffer);
            MUploadCommandBuffer = VK_NULL_HANDLE;
        }
        if (MUploadBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MUploadBuffer, nullptr);
            ScopedApi.vkFreeMemory(MDevice, MUploadMemory, nullptr);
            MUploadBuffer = VK_NULL_HANDLE;
            MUploadMemory = VK_NULL_HANDLE;
        }
        if (MCaptureStagingMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, MCaptureStagingMemory);
            MCaptureStagingMapped = nullptr;
        }
        if (MCaptureStaging != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MCaptureStaging, nullptr);
            ScopedApi.vkFreeMemory(MDevice, MCaptureStagingMemory, nullptr);
            MCaptureStaging = VK_NULL_HANDLE;
            MCaptureStagingMemory = VK_NULL_HANDLE;
            MCaptureStagingBytes = 0;
        }
        if (MOffscreenRenderPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyRenderPass(MDevice, MOffscreenRenderPass, nullptr);
            MOffscreenRenderPass = VK_NULL_HANDLE;
        }
        if (MSrgbOffscreenRenderPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyRenderPass(MDevice, MSrgbOffscreenRenderPass, nullptr);
            MSrgbOffscreenRenderPass = VK_NULL_HANDLE;
        }
        DestroyMeshResources();
        if (MAtlas.sampler != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySampler(MDevice, MAtlas.sampler, nullptr);
        }
        if (MAtlas.view != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImageView(MDevice, MAtlas.view, nullptr);
        }
        if (MAtlas.image != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImage(MDevice, MAtlas.image, nullptr);
        }
        if (MAtlas.memory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MAtlas.memory, nullptr);
        }
        MAtlas = {};

        if (MPipeline != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, MPipeline, nullptr);
        }
        if (MPipelineOpaque != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, MPipelineOpaque, nullptr);
        }
        if (MOitResolvePipeline != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, MOitResolvePipeline, nullptr);
        }
        for (const auto& [ScopedPass, ScopedPipeline] : MOitResolvePipelines)
        {
            (void)ScopedPass;
            if (ScopedPipeline != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline, nullptr);
            }
        }
        MOitResolvePipelines.clear();
        if (MOitResolveLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipelineLayout(MDevice, MOitResolveLayout, nullptr);
        }
        if (MOitResolvePool != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorPool(MDevice, MOitResolvePool, nullptr);
        }
        if (MOitResolveSetLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MOitResolveSetLayout, nullptr);
        }
        if (MOitResolveSampler != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySampler(MDevice, MOitResolveSampler, nullptr);
        }
        for (const auto& [ScopedPass, ScopedPipeline] : MUpscalePipelines)
        {
            (void)ScopedPass;
            if (ScopedPipeline != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline, nullptr);
            }
        }
        MUpscalePipelines.clear();
        if (MUpscaleLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipelineLayout(MDevice, MUpscaleLayout, nullptr);
        }
        if (MUpscalePool != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorPool(MDevice, MUpscalePool, nullptr);
        }
        if (MUpscaleSetLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MUpscaleSetLayout, nullptr);
        }
        if (MUpscaleSampler != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySampler(MDevice, MUpscaleSampler, nullptr);
        }
        for (const auto& [ScopedPass, ScopedPipeline] : MRcasPipelines)
        {
            (void)ScopedPass;
            if (ScopedPipeline != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline, nullptr);
            }
        }
        MRcasPipelines.clear();
        if (MRcasLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipelineLayout(MDevice, MRcasLayout, nullptr);
        }
        if (MMotionMapped != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, MMotionMemory);
            MMotionMapped = nullptr;
        }
        if (MMotionBuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MMotionBuffer, nullptr);
        }
        if (MMotionMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MMotionMemory, nullptr);
        }
        for (const auto& [ScopedPass, ScopedPipeline] : MMotionPipelines)
        {
            (void)ScopedPass;
            if (ScopedPipeline != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline, nullptr);
            }
        }
        MMotionPipelines.clear();
        if (MMotionLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipelineLayout(MDevice, MMotionLayout, nullptr);
        }
        if (MMotionPool != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorPool(MDevice, MMotionPool, nullptr);
        }
        if (MMotionSetLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MMotionSetLayout, nullptr);
        }
        if (MHudPipeline != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, MHudPipeline, nullptr);
        }
        if (MHudLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipelineLayout(MDevice, MHudLayout, nullptr);
        }
        if (MHudPool != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorPool(MDevice, MHudPool, nullptr);
        }
        if (MHudSetLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MHudSetLayout, nullptr);
        }
        if (MHudSampler != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySampler(MDevice, MHudSampler, nullptr);
        }
        if (MHudNearestSampler != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySampler(MDevice, MHudNearestSampler, nullptr);
        }
        if (MPipelineLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipelineLayout(MDevice, MPipelineLayout, nullptr);
        }
        if (MDescriptorPool != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorPool(MDevice, MDescriptorPool, nullptr);
        }
        if (MDescriptorLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MDescriptorLayout, nullptr);
        }

        for (VkQueryPool ScopedPool : MTimestampPools)
        {
            if (ScopedPool != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyQueryPool(MDevice, ScopedPool, nullptr);
            }
        }
        for (VkFence ScopedFence : MInFlight)
        {
            if (ScopedFence != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyFence(MDevice, ScopedFence, nullptr);
            }
        }
        for (VkSemaphore ScopedSemaphore : MImageAvailable)
        {
            if (ScopedSemaphore != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroySemaphore(MDevice, ScopedSemaphore, nullptr);
            }
        }
        for (const auto& [ScopedPass, ScopedPipeline] : MTemporalPipelines)
        {
            (void)ScopedPass;
            if (ScopedPipeline != VK_NULL_HANDLE)
            {
                ScopedApi.vkDestroyPipeline(MDevice, ScopedPipeline, nullptr);
            }
        }
        MTemporalPipelines.clear();
        if (MTemporalLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipelineLayout(MDevice, MTemporalLayout, nullptr);
            MTemporalLayout = VK_NULL_HANDLE;
        }
        if (MTemporalPool != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorPool(MDevice, MTemporalPool, nullptr);
            MTemporalPool = VK_NULL_HANDLE;
        }
        if (MTemporalSetLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MTemporalSetLayout, nullptr);
            MTemporalSetLayout = VK_NULL_HANDLE;
        }
        MTemporalSets.fill(VK_NULL_HANDLE);
        DestroySwapchain();
        DestroyAccessibilityCompositeResources();

        if (MCommandPool != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyCommandPool(MDevice, MCommandPool, nullptr);
            MCommandPool = VK_NULL_HANDLE;
        }
        if (MDepthPrepassPipeline != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, MDepthPrepassPipeline, nullptr);
            MDepthPrepassPipeline = VK_NULL_HANDLE;
        }
        if (MDepthPrepassRenderPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyRenderPass(MDevice, MDepthPrepassRenderPass, nullptr);
            MDepthPrepassRenderPass = VK_NULL_HANDLE;
        }
        if (MHudOverlayPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyRenderPass(MDevice, MHudOverlayPass, nullptr);
            MHudOverlayPass = VK_NULL_HANDLE;
        }
        if (MRenderPass != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyRenderPass(MDevice, MRenderPass, nullptr);
            MRenderPass = VK_NULL_HANDLE;
        }
        ScopedApi.vkDestroyDevice(MDevice, nullptr);
        MDevice = VK_NULL_HANDLE;
    }

    if (MSurface != VK_NULL_HANDLE && MInstance != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySurfaceKHR(MInstance, MSurface, nullptr);
        MSurface = VK_NULL_HANDLE;
    }
    if (MDebugMessenger != VK_NULL_HANDLE && MDestroyDebugMessenger != nullptr && MInstance != VK_NULL_HANDLE)
    {
        MDestroyDebugMessenger(MInstance, MDebugMessenger, nullptr);
        MDebugMessenger = VK_NULL_HANDLE;
    }
    if (MInstance != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyInstance(MInstance, nullptr);
        MInstance = VK_NULL_HANDLE;
    }
}
}
#endif
