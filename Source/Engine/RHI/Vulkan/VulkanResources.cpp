// FemboyEngine2D - Vulkan backend definitions.
#include "VulkanRHIInternal.hpp"

#if defined(FBY_HAS_VULKAN_HEADERS)
namespace Fby::Rhi::Impl
{
void VulkanRHI::WriteTimestamp(VkCommandBuffer InCommandBuffer, std::uint32_t InIndex, VkPipelineStageFlagBits InStage)
{
    if (MTimestampsReady && MFrameIndex < MTimestampPools.size() && MTimestampPools[MFrameIndex] != VK_NULL_HANDLE &&
        InIndex < TimestampCount)
    {
        Vk::GetApi().vkCmdWriteTimestamp(InCommandBuffer, InStage, MTimestampPools[MFrameIndex], InIndex);
    }
}

void VulkanRHI::RecordOitResolve(VkCommandBuffer InCommandBuffer, VkRenderPass InRenderPass,
                                 const FramePacket& InPacket)
{
    if (MOitResolveSet == VK_NULL_HANDLE || MOitResolveLayout == VK_NULL_HANDLE ||
        InPacket.oitAccumulation == 0xFFFFFFFFu || InPacket.oitRevealage == 0xFFFFFFFFu)
    {
        return;
    }
    const RenderTarget* ScopedAccumulation = FindRenderTarget(InPacket.oitAccumulation);
    const RenderTarget* ScopedRevealage = FindRenderTarget(InPacket.oitRevealage);
    if (ScopedAccumulation == nullptr || ScopedRevealage == nullptr || ScopedAccumulation->view == VK_NULL_HANDLE ||
        ScopedRevealage->view == VK_NULL_HANDLE)
    {
        return;
    }

    VkPipeline ScopedResolvePipeline = VK_NULL_HANDLE;
    if (InRenderPass == MRenderPass)
    {
        ScopedResolvePipeline = MOitResolvePipeline;
    }
    else
    {
        const auto ScopedFound = MOitResolvePipelines.find(InRenderPass);
        if (ScopedFound != MOitResolvePipelines.end())
        {
            ScopedResolvePipeline = ScopedFound->second;
        }
        else if (CreateGraphicsPipelineForPass(
                     InRenderPass, MOitResolveLayout, Shaders::fullscreen_vert.data(),
                     Shaders::fullscreen_vert.size() * sizeof(std::uint32_t), Shaders::oit_resolve.data(),
                     Shaders::oit_resolve.size() * sizeof(std::uint32_t), &ScopedResolvePipeline))
        {
            MOitResolvePipelines.emplace(InRenderPass, ScopedResolvePipeline);
        }
    }
    if (ScopedResolvePipeline == VK_NULL_HANDLE)
    {
        return;
    }

    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorImageInfo ScopedImages[2]{};
    ScopedImages[0].sampler = MOitResolveSampler;
    ScopedImages[0].imageView = ScopedAccumulation->view;
    ScopedImages[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedImages[1].sampler = MOitResolveSampler;
    ScopedImages[1].imageView = ScopedRevealage->view;
    ScopedImages[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet ScopedWrites[2]{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        ScopedWrites[ScopedIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[ScopedIndex].dstSet = MOitResolveSet;
        ScopedWrites[ScopedIndex].dstBinding = ScopedIndex;
        ScopedWrites[ScopedIndex].descriptorCount = 1;
        ScopedWrites[ScopedIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrites[ScopedIndex].pImageInfo = &ScopedImages[ScopedIndex];
    }
    ScopedApi.vkUpdateDescriptorSets(MDevice, 2, ScopedWrites, 0, nullptr);
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ScopedResolvePipeline);
    ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MOitResolveLayout, 0, 1,
                                      &MOitResolveSet, 0, nullptr);
    ScopedApi.vkCmdDraw(InCommandBuffer, 3, 1, 0, 0);
}

void VulkanRHI::RecordClusters(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket)
{
    if (!MClusterRequested || !MClusterReady || MClusterRecordedThisFrame || InPacket.lights.empty() || MBlindFrame)
    {
        return;
    }
    ClusterFrame& ScopedReusableFrame = MClusterFrames[MFrameIndex];
    if (MDesc.enableStaticCaches && InPacket.sceneStatic && InPacket.lightVersion != 0u &&
        ScopedReusableFrame.LightVersion == InPacket.lightVersion)
    {
        MClusterRecordedThisFrame = true;
        ++MStats.clusterCacheHits;
        return;
    }
    const float ScopedWidth = std::max(InPacket.camera.viewport.X, 1.0f);
    const float ScopedHeight = std::max(InPacket.camera.viewport.Y, 1.0f);
    const std::uint32_t ScopedTilesX =
        static_cast<std::uint32_t>(std::ceil(ScopedWidth / static_cast<float>(ClusterTileSize)));
    const std::uint32_t ScopedTilesY =
        static_cast<std::uint32_t>(std::ceil(ScopedHeight / static_cast<float>(ClusterTileSize)));
    if (ScopedTilesX == 0u || ScopedTilesY == 0u || ScopedTilesX * ScopedTilesY > kClusterMaxCount)
    {
        return;
    }
    const std::uint32_t ScopedLightCount =
        std::min(static_cast<std::uint32_t>(InPacket.lights.size()), kClusterMaxLights);
    MStats.clusterTilesX = ScopedTilesX;
    MStats.clusterTilesY = ScopedTilesY;
    MStats.clusterCount = ScopedTilesX * ScopedTilesY;
    MStats.clusterLights = ScopedLightCount;
    MStats.clusterDispatches = 3;
    ClusterFrame& ScopedFrame = MClusterFrames[MFrameIndex];
    if (ScopedFrame.LightMapped == nullptr || ScopedFrame.Set == VK_NULL_HANDLE)
    {
        return;
    }
    std::memcpy(ScopedFrame.LightMapped, InPacket.lights.data(),
                static_cast<std::size_t>(ScopedLightCount) * sizeof(GpuLight));
    Vk::Api& ScopedApi = Vk::GetApi();
    const ClusterGridConstants ScopedConstants{ScopedTilesX, ScopedTilesY, ScopedLightCount, MaxLightsPerCluster};

    const VkMemoryBarrier ScopedComputeBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    const auto ScopedBarrier = [&ScopedApi, InCommandBuffer](const VkMemoryBarrier& InBarrier)
    {
        ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &InBarrier, 0, nullptr, 0, nullptr);
    };
    const std::uint32_t ScopedClusterCount = ScopedTilesX * ScopedTilesY;
    ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MClusterLayout, 0, 1,
                                      &ScopedFrame.Set, 0, nullptr);
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MClusterCountPipeline);
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MClusterLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 sizeof(ClusterGridConstants), &ScopedConstants);
    ScopedApi.vkCmdDispatch(InCommandBuffer, (ScopedClusterCount + 63u) / 64u, 1, 1);
    ScopedBarrier(ScopedComputeBarrier);
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MClusterPrefixPipeline);
    ScopedApi.vkCmdDispatch(InCommandBuffer, 1, 1, 1);
    ScopedBarrier(ScopedComputeBarrier);
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MClusterFillPipeline);
    ScopedApi.vkCmdDispatch(InCommandBuffer, (ScopedClusterCount + 63u) / 64u, 1, 1);
    const VkMemoryBarrier ScopedGraphicsBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                                                VK_ACCESS_SHADER_READ_BIT};
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 1, &ScopedGraphicsBarrier, 0, nullptr, 0,
                                   nullptr);
    ScopedReusableFrame.LightVersion = InPacket.lightVersion;
    MClusterRecordedThisFrame = true;
}

void VulkanRHI::WriteSdfDescriptors(VkDescriptorSet InSet) const
{
    if (InSet == VK_NULL_HANDLE || MSdfFieldView == VK_NULL_HANDLE || MSdfBlockBuffer == VK_NULL_HANDLE ||
        MSdfMaskView == VK_NULL_HANDLE)
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorImageInfo ScopedImageDescriptor{};
    ScopedImageDescriptor.sampler = MSdfFieldSampler;
    ScopedImageDescriptor.imageView = MSdfFieldView;
    ScopedImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo ScopedBufferDescriptor{};
    ScopedBufferDescriptor.buffer = MSdfBlockBuffer;
    ScopedBufferDescriptor.offset = 0;
    ScopedBufferDescriptor.range = sizeof(SdfGpuBlock);

    std::array<VkWriteDescriptorSet, 3> ScopedWrites{};
    ScopedWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrites[0].dstSet = InSet;
    ScopedWrites[0].dstBinding = 1;
    ScopedWrites[0].descriptorCount = 1;
    ScopedWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrites[0].pImageInfo = &ScopedImageDescriptor;
    ScopedWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrites[1].dstSet = InSet;
    ScopedWrites[1].dstBinding = 2;
    ScopedWrites[1].descriptorCount = 1;
    ScopedWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ScopedWrites[1].pBufferInfo = &ScopedBufferDescriptor;
    VkDescriptorImageInfo ScopedMaskDescriptor{};
    ScopedMaskDescriptor.sampler = MSdfMaskSampler;
    ScopedMaskDescriptor.imageView = MSdfMaskView;
    ScopedMaskDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    ScopedWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrites[2].dstSet = InSet;
    ScopedWrites[2].dstBinding = 3;
    ScopedWrites[2].descriptorCount = 1;
    ScopedWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrites[2].pImageInfo = &ScopedMaskDescriptor;

    // Bindings 4/5: the active virtual texture, or the neutral fallback.
    const VirtualTextureResource* ScopedVt = nullptr;
    if (MActiveVirtualTexture.IsValid() && MActiveVirtualTexture.id != 0 &&
        MActiveVirtualTexture.id < MVirtualTextures.size())
    {
        ScopedVt = MVirtualTextures[MActiveVirtualTexture.id].get();
    }
    VkDescriptorImageInfo ScopedTableDescriptor{};
    ScopedTableDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (ScopedVt != nullptr)
    {
        ScopedTableDescriptor.sampler = ScopedVt->tableSampler;
        ScopedTableDescriptor.imageView = ScopedVt->tableView;
    }
    else
    {
        ScopedTableDescriptor.sampler = MVtFallbackTableSampler;
        ScopedTableDescriptor.imageView = MVtFallbackTableView;
    }
    VkDescriptorImageInfo ScopedPagesDescriptor{};
    ScopedPagesDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (ScopedVt != nullptr)
    {
        ScopedPagesDescriptor.sampler = ScopedVt->pagesSampler;
        ScopedPagesDescriptor.imageView = ScopedVt->pagesView;
    }
    else
    {
        ScopedPagesDescriptor.sampler = MVtFallbackPagesSampler;
        ScopedPagesDescriptor.imageView = MVtFallbackPagesView;
    }
    std::array<VkWriteDescriptorSet, 5> ScopedAllWrites{};
    for (std::uint32_t ScopedI = 0; ScopedI < 3; ++ScopedI)
    {
        ScopedAllWrites[ScopedI] = ScopedWrites[ScopedI];
    }
    ScopedAllWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedAllWrites[3].dstSet = InSet;
    ScopedAllWrites[3].dstBinding = 4;
    ScopedAllWrites[3].descriptorCount = 1;
    ScopedAllWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedAllWrites[3].pImageInfo = &ScopedTableDescriptor;
    ScopedAllWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedAllWrites[4].dstSet = InSet;
    ScopedAllWrites[4].dstBinding = 5;
    ScopedAllWrites[4].descriptorCount = 1;
    ScopedAllWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedAllWrites[4].pImageInfo = &ScopedPagesDescriptor;

    std::array<VkDescriptorImageInfo, kFeedbackSlots> ScopedFeedbackInfos{};
    for (std::uint32_t ScopedI = 0; ScopedI < kFeedbackSlots; ++ScopedI)
    {
        ScopedFeedbackInfos[ScopedI].imageView =
            MVtFeedbackViews[ScopedI] != VK_NULL_HANDLE ? MVtFeedbackViews[ScopedI] : MVtFeedbackFallbackView;
        ScopedFeedbackInfos[ScopedI].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    std::array<VkWriteDescriptorSet, 8> ScopedFinalWrites{};
    for (std::uint32_t ScopedI = 0; ScopedI < 5; ++ScopedI)
    {
        ScopedFinalWrites[ScopedI] = ScopedAllWrites[ScopedI];
    }
    ScopedFinalWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedFinalWrites[5].dstSet = InSet;
    ScopedFinalWrites[5].dstBinding = 6;
    ScopedFinalWrites[5].descriptorCount = kFeedbackSlots;
    ScopedFinalWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedFinalWrites[5].pImageInfo = ScopedFeedbackInfos.data();

    // Bindings 7/8: the sparse brick table and the atlas tiles.
    VkDescriptorBufferInfo ScopedSdfTableInfo{};
    ScopedSdfTableInfo.buffer = MSdfTableBuffer;
    ScopedSdfTableInfo.offset = 0;
    ScopedSdfTableInfo.range = VK_WHOLE_SIZE;
    ScopedFinalWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedFinalWrites[6].dstSet = InSet;
    ScopedFinalWrites[6].dstBinding = 7;
    ScopedFinalWrites[6].descriptorCount = 1;
    ScopedFinalWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedFinalWrites[6].pBufferInfo = &ScopedSdfTableInfo;
    VkDescriptorImageInfo ScopedSdfAtlasInfo{};
    ScopedSdfAtlasInfo.sampler = MSdfAtlasSampler;
    ScopedSdfAtlasInfo.imageView = MSdfAtlasView;
    ScopedSdfAtlasInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedFinalWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedFinalWrites[7].dstSet = InSet;
    ScopedFinalWrites[7].dstBinding = 8;
    ScopedFinalWrites[7].descriptorCount = 1;
    ScopedFinalWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedFinalWrites[7].pImageInfo = &ScopedSdfAtlasInfo;
    ScopedApi.vkUpdateDescriptorSets(MDevice, static_cast<std::uint32_t>(ScopedFinalWrites.size()),
                                     ScopedFinalWrites.data(), 0, nullptr);
}

void VulkanRHI::UploadSdfField()
{
    if (!MInFrame || MFrameIndex >= kMaxFramesInFlight || MSdfFieldStagingMapped[MFrameIndex] == nullptr)
    {
        MSdfFieldUploadPending = false;
        return;
    }
    std::memcpy(MSdfFieldStagingMapped[MFrameIndex], MSdfFieldTexels.data(), MSdfFieldTexels.size() * sizeof(float));
    const VkCommandBuffer ScopedCommand = MCommandBuffers[MFrameIndex];
    TransitionImage(ScopedCommand, MSdfFieldImage,
                    MSdfFieldInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy ScopedCopy{};
    ScopedCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ScopedCopy.imageExtent = {kSdfFieldSize, kSdfFieldSize, 1};
    Vk::GetApi().vkCmdCopyBufferToImage(ScopedCommand, MSdfFieldStagingBuffers[MFrameIndex], MSdfFieldImage,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedCopy);
    TransitionImage(ScopedCommand, MSdfFieldImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    MSdfFieldInitialized = true;
    MSdfFieldUploadPending = false;
}

void VulkanRHI::UploadSdfAtlas()
{
    if (!MInFrame || MFrameIndex >= kMaxFramesInFlight || MSdfFieldStagingMapped[MFrameIndex] == nullptr)
    {
        MSdfAtlasUploadPending = false;
        return;
    }
    std::memcpy(MSdfFieldStagingMapped[MFrameIndex], MSdfAtlasTexels.data(), MSdfAtlasTexels.size() * sizeof(float));
    const VkCommandBuffer ScopedCommand = MCommandBuffers[MFrameIndex];
    TransitionImage(ScopedCommand, MSdfAtlasImage,
                    MSdfAtlasInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy ScopedCopy{};
    ScopedCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ScopedCopy.imageExtent = {kSdfAtlasSize, kSdfAtlasSize, 1};
    Vk::GetApi().vkCmdCopyBufferToImage(ScopedCommand, MSdfFieldStagingBuffers[MFrameIndex], MSdfAtlasImage,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedCopy);
    TransitionImage(ScopedCommand, MSdfAtlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    MSdfAtlasInitialized = true;
    MSdfAtlasUploadPending = false;
}

std::uint32_t VulkanRHI::ApplySdfLighting(const SdfLightingFrame& InFrame)
{
    const std::uint32_t ScopedView = std::min(InFrame.viewId, SdfMaxViews - 1u);
    const std::uint32_t ScopedSlot = MFrameIndex * SdfMaxViews + ScopedView;
    if (MSdfBlockMapped == nullptr)
    {
        return ScopedSlot;
    }
    SdfGpuBlock ScopedBlock;
    ScopedBlock.camera = InFrame.camera;
    ScopedBlock.viewport = InFrame.viewport;
    const float ScopedExtentX = static_cast<float>(InFrame.width) * InFrame.cellSize.X;
    const float ScopedExtentY = static_cast<float>(InFrame.height) * InFrame.cellSize.Y;
    ScopedBlock.fieldBounds = Vector4D{InFrame.origin.X, InFrame.origin.Y, ScopedExtentX, ScopedExtentY};
    float ScopedDirectionX = InFrame.sunDirection.X;
    float ScopedDirectionY = InFrame.sunDirection.Y;
    const float ScopedLength = std::sqrt(ScopedDirectionX * ScopedDirectionX + ScopedDirectionY * ScopedDirectionY);
    if (ScopedLength < 1.0e-4f)
    {
        ScopedDirectionX = 0.25f;
        ScopedDirectionY = -1.0f;
    }
    else
    {
        ScopedDirectionX /= ScopedLength;
        ScopedDirectionY /= ScopedLength;
    }
    ScopedBlock.sun = Vector4D{ScopedDirectionX, ScopedDirectionY, std::max(InFrame.softness, 1.0e-3f),
                               std::clamp(InFrame.ambient, 0.0f, 1.0f)};
    const std::uint32_t ScopedOccluderCount = std::min(InFrame.occluderCount, kMaxSdfGpuOccluders);
    ScopedBlock.params =
        Vector4D{static_cast<float>(std::clamp(InFrame.steps, 1u, 64u)), static_cast<float>(ScopedOccluderCount),
                 std::max(InFrame.occlusionRadius, 1.0f), std::clamp(InFrame.occlusionStrength, 0.0f, 1.0f)};
    const bool ScopedEnabled = InFrame.enabled && InFrame.field != nullptr && InFrame.width > 1 && InFrame.height > 1 &&
                               ScopedExtentX > 0.0f && ScopedExtentY > 0.0f;
    const bool ScopedMaskActive =
        ScopedEnabled && ScopedSlot < MSdfMaskPublished.size() && MSdfMaskPublished[ScopedSlot];
    ScopedBlock.flags = Vector4D{kSdfEncodeScale, ScopedEnabled ? 1.0f : 0.0f, ScopedMaskActive ? 1.0f : 0.0f, 0.0f};
    float ScopedHistoryLayer = 0.0f;
    float ScopedHistoryValid = 0.0f;
    if (InFrame.temporal && ScopedEnabled && MFrameCounter > MFramesInFlight)
    {
        const std::uint32_t ScopedHistoryView = std::min(InFrame.viewId, SdfMaxViews - 1u);
        const std::uint32_t ScopedPreviousFrame = (MFrameIndex + MFramesInFlight - 1u) % MFramesInFlight;
        ScopedHistoryLayer = static_cast<float>(ScopedPreviousFrame * SdfMaxViews + ScopedHistoryView);
        ScopedHistoryValid = 1.0f;
    }
    ScopedBlock.history = Vector4D{ScopedHistoryLayer, ScopedHistoryValid, 0.85f, 0.0f};
    ScopedBlock.historyCamera = InFrame.previousCamera;
    // The atlas path needs the grid mapping and the brick layout. A field that is
    // not configured reports zero bricks, which the shader treats as "no atlas".
    ScopedBlock.fieldGrid = Vector4D{InFrame.origin.X, InFrame.origin.Y, InFrame.cellSize.X, InFrame.cellSize.Y};
    const std::uint32_t ScopedBrickSize = 16;
    ScopedBlock.brickMeta = Vector4D{static_cast<float>(ScopedBrickSize),
                                     static_cast<float>((InFrame.width + ScopedBrickSize - 1u) / ScopedBrickSize),
                                     static_cast<float>((InFrame.height + ScopedBrickSize - 1u) / ScopedBrickSize),
                                     static_cast<float>(InFrame.atlasColumns)};
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedOccluderCount; ++ScopedI)
    {
        ScopedBlock.occluders[ScopedI * 2] = InFrame.occluders[ScopedI].Shape;
        ScopedBlock.occluders[ScopedI * 2 + 1] = InFrame.occluders[ScopedI].Extents;
    }
    auto* ScopedBlocks = static_cast<std::byte*>(MSdfBlockMapped);
    *reinterpret_cast<SdfGpuBlock*>(ScopedBlocks + static_cast<std::size_t>(ScopedSlot) * SdfBlockStride) = ScopedBlock;
    MSdfEnabled = ScopedEnabled;

    if (ScopedEnabled && InFrame.fieldVersion != MSdfFieldVersion)
    {
        // Nearest resample into the fixed field texture: the mask is smooth and
        // the source grid is small, so the error stays below one texel.
        for (std::uint32_t ScopedY = 0; ScopedY < kSdfFieldSize; ++ScopedY)
        {
            const std::uint32_t ScopedSourceY = ScopedY * InFrame.height / kSdfFieldSize;
            for (std::uint32_t ScopedX = 0; ScopedX < kSdfFieldSize; ++ScopedX)
            {
                const std::uint32_t ScopedSourceX = ScopedX * InFrame.width / kSdfFieldSize;
                const float ScopedDistance =
                    InFrame.field[static_cast<std::size_t>(ScopedSourceY) * InFrame.width + ScopedSourceX];
                MSdfFieldTexels[static_cast<std::size_t>(ScopedY) * kSdfFieldSize + ScopedX] = ScopedDistance;
            }
        }
        MSdfFieldVersion = InFrame.fieldVersion;
        MSdfFieldUploadPending = true;

        // The sparse atlas shares the field version. The table is host-visible and
        // rewritten in place; the tiles go through the same staging slice as the
        // dense field so a pending frame cannot read overwritten bytes.
        MSdfAtlasUploadPending = false;
        if (InFrame.brickTable != nullptr && InFrame.brickCount > 0 && InFrame.atlasTexels != nullptr &&
            MSdfTableMapped != nullptr && MSdfAtlasImage != VK_NULL_HANDLE)
        {
            const std::uint32_t ScopedBricks = std::min(InFrame.brickCount, kSdfTableCapacity);
            std::memcpy(MSdfTableMapped, InFrame.brickTable,
                        static_cast<std::size_t>(ScopedBricks) * kSdfTableEntryBytes);
            MSdfAtlasTexels.fill(0.0f);
            const std::uint32_t ScopedCopyWidth = std::min(InFrame.atlasWidth, kSdfAtlasSize);
            const std::uint32_t ScopedCopyHeight = std::min(InFrame.atlasHeight, kSdfAtlasSize);
            for (std::uint32_t ScopedY = 0; ScopedY < ScopedCopyHeight; ++ScopedY)
            {
                std::memcpy(MSdfAtlasTexels.data() + static_cast<std::size_t>(ScopedY) * kSdfAtlasSize,
                            InFrame.atlasTexels + static_cast<std::size_t>(ScopedY) * InFrame.atlasWidth,
                            static_cast<std::size_t>(ScopedCopyWidth) * sizeof(float));
            }
            MSdfAtlasUploadPending = true;
        }
    }
    if (MSdfFieldUploadPending)
    {
        UploadSdfField();
    }
    if (MSdfAtlasUploadPending)
    {
        UploadSdfAtlas();
    }
    return ScopedSlot;
}

VirtualTextureResource* VulkanRHI::FindVirtualTexture(std::uint32_t InId) noexcept
{
    if (InId == 0 || InId >= MVirtualTextures.size())
    {
        return nullptr;
    }
    return MVirtualTextures[InId].get();
}

void VulkanRHI::DestroyVirtualTextureResources(VirtualTextureResource& InTexture) noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (InTexture.pagesSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, InTexture.pagesSampler, nullptr);
        InTexture.pagesSampler = VK_NULL_HANDLE;
    }
    if (InTexture.pagesView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, InTexture.pagesView, nullptr);
        InTexture.pagesView = VK_NULL_HANDLE;
    }
    if (InTexture.pages != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, InTexture.pages, nullptr);
        InTexture.pages = VK_NULL_HANDLE;
    }
    if (InTexture.pagesMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, InTexture.pagesMemory, nullptr);
        InTexture.pagesMemory = VK_NULL_HANDLE;
    }
    if (InTexture.tableSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, InTexture.tableSampler, nullptr);
        InTexture.tableSampler = VK_NULL_HANDLE;
    }
    if (InTexture.tableView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, InTexture.tableView, nullptr);
        InTexture.tableView = VK_NULL_HANDLE;
    }
    if (InTexture.table != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, InTexture.table, nullptr);
        InTexture.table = VK_NULL_HANDLE;
    }
    if (InTexture.tableMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, InTexture.tableMemory, nullptr);
        InTexture.tableMemory = VK_NULL_HANDLE;
    }
}

VirtualTextureHandle VulkanRHI::CreateVirtualTexture(const VirtualTextureResourceDesc& InDesc)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    const bool ScopedValid = InDesc.layerCount >= 1 && InDesc.layerCount <= 64 && InDesc.tileSize >= 1 &&
                             InDesc.pageSize >= InDesc.tileSize && InDesc.pageSize <= 16384 &&
                             InDesc.pageTableSize >= 1 && InDesc.pageTableSize <= 16384;
    if (!ScopedValid || MDevice == VK_NULL_HANDLE)
    {
        LogError("vulkan", "invalid virtual texture description");
        return {};
    }

    // Graceful degradation: virtual texturing needs sampled RGBA8 pages and a
    // sampled R32_UINT table + storage feedback. A driver without those features
    // simply gets an invalid handle and the caller keeps the atlas path.
    VkFormatProperties ScopedPagesFormat{};
    ScopedApi.vkGetPhysicalDeviceFormatProperties(MPhysical, VK_FORMAT_R8G8B8A8_UNORM, &ScopedPagesFormat);
    const VkFormatFeatureFlags ScopedPagesNeeded =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    VkFormatProperties ScopedTableFormat{};
    ScopedApi.vkGetPhysicalDeviceFormatProperties(MPhysical, VK_FORMAT_R32_UINT, &ScopedTableFormat);
    const VkFormatFeatureFlags ScopedTableNeeded =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if ((ScopedPagesFormat.optimalTilingFeatures & ScopedPagesNeeded) != ScopedPagesNeeded ||
        (ScopedTableFormat.optimalTilingFeatures & ScopedTableNeeded) != ScopedTableNeeded)
    {
        LogWarn("vulkan", "virtual texturing unsupported by this format/driver; staying on the atlas path");
        return {};
    }

    std::uint32_t ScopedId = VirtualTextureHandle::kInvalid32;
    if (!MFreeVirtualTextureIds.empty())
    {
        ScopedId = MFreeVirtualTextureIds.back();
        MFreeVirtualTextureIds.pop_back();
    }
    else
    {
        ScopedId = static_cast<std::uint32_t>(MVirtualTextures.size());
        MVirtualTextures.push_back(nullptr);
    }
    auto ScopedTexture = std::make_unique<VirtualTextureResource>();
    ScopedTexture->layerCount = InDesc.layerCount;
    ScopedTexture->pageSize = InDesc.pageSize;
    ScopedTexture->tileSize = InDesc.tileSize;
    ScopedTexture->pageTableSize = InDesc.pageTableSize;

    // Page array: RGBA8 pages, one per VRAM layer, sampled by the sprite shader.
    VkImageCreateInfo ScopedPagesInfo{};
    ScopedPagesInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedPagesInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedPagesInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ScopedPagesInfo.extent = {InDesc.pageSize, InDesc.pageSize, 1};
    ScopedPagesInfo.mipLevels = 1;
    ScopedPagesInfo.arrayLayers = InDesc.layerCount;
    ScopedPagesInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedPagesInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedPagesInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedPagesInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedPagesInfo, nullptr, &ScopedTexture->pages) != VK_SUCCESS)
    {
        LogError("vulkan", "VT page array creation failed");
        MVirtualTextures[ScopedId] = std::move(ScopedTexture);
        DestroyVirtualTexture({ScopedId});
        return {};
    }
    VkMemoryRequirements ScopedPagesRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, ScopedTexture->pages, &ScopedPagesRequirements);
    VkMemoryAllocateInfo ScopedPagesAllocate{};
    ScopedPagesAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedPagesAllocate.allocationSize = ScopedPagesRequirements.size;
    ScopedPagesAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedPagesRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedPagesAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedPagesAllocate, nullptr, &ScopedTexture->pagesMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, ScopedTexture->pages, ScopedTexture->pagesMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "VT page array memory failed");
        MVirtualTextures[ScopedId] = std::move(ScopedTexture);
        DestroyVirtualTexture({ScopedId});
        return {};
    }
    VkImageViewCreateInfo ScopedPagesView{};
    ScopedPagesView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedPagesView.image = ScopedTexture->pages;
    ScopedPagesView.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ScopedPagesView.format = VK_FORMAT_R8G8B8A8_UNORM;
    ScopedPagesView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, InDesc.layerCount};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedPagesView, nullptr, &ScopedTexture->pagesView) != VK_SUCCESS)
    {
        LogError("vulkan", "VT page array view failed");
        MVirtualTextures[ScopedId] = std::move(ScopedTexture);
        DestroyVirtualTexture({ScopedId});
        return {};
    }
    VkSamplerCreateInfo ScopedPagesSampler{};
    ScopedPagesSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedPagesSampler.magFilter = VK_FILTER_LINEAR;
    ScopedPagesSampler.minFilter = VK_FILTER_LINEAR;
    ScopedPagesSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedPagesSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedPagesSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedPagesSampler.maxLod = 0.0f;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedPagesSampler, nullptr, &ScopedTexture->pagesSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "VT page sampler failed");
        MVirtualTextures[ScopedId] = std::move(ScopedTexture);
        DestroyVirtualTexture({ScopedId});
        return {};
    }

    // Page table: R32_UINT world-tile -> (layer, slot) grid, nearest sampling.
    VkImageCreateInfo ScopedTableInfo{};
    ScopedTableInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedTableInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedTableInfo.format = VK_FORMAT_R32_UINT;
    ScopedTableInfo.extent = {InDesc.pageTableSize, InDesc.pageTableSize, 1};
    ScopedTableInfo.mipLevels = 1;
    ScopedTableInfo.arrayLayers = 1;
    ScopedTableInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedTableInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedTableInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedTableInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedTableInfo, nullptr, &ScopedTexture->table) != VK_SUCCESS)
    {
        LogError("vulkan", "VT page table creation failed");
        MVirtualTextures[ScopedId] = std::move(ScopedTexture);
        DestroyVirtualTexture({ScopedId});
        return {};
    }
    VkMemoryRequirements ScopedTableRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, ScopedTexture->table, &ScopedTableRequirements);
    VkMemoryAllocateInfo ScopedTableAllocate{};
    ScopedTableAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedTableAllocate.allocationSize = ScopedTableRequirements.size;
    ScopedTableAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedTableRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedTableAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedTableAllocate, nullptr, &ScopedTexture->tableMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, ScopedTexture->table, ScopedTexture->tableMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "VT page table memory failed");
        MVirtualTextures[ScopedId] = std::move(ScopedTexture);
        DestroyVirtualTexture({ScopedId});
        return {};
    }
    VkImageViewCreateInfo ScopedTableView{};
    ScopedTableView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedTableView.image = ScopedTexture->table;
    ScopedTableView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedTableView.format = VK_FORMAT_R32_UINT;
    ScopedTableView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedTableView, nullptr, &ScopedTexture->tableView) != VK_SUCCESS)
    {
        LogError("vulkan", "VT page table view failed");
        MVirtualTextures[ScopedId] = std::move(ScopedTexture);
        DestroyVirtualTexture({ScopedId});
        return {};
    }
    VkSamplerCreateInfo ScopedTableSampler{};
    ScopedTableSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedTableSampler.magFilter = VK_FILTER_NEAREST;
    ScopedTableSampler.minFilter = VK_FILTER_NEAREST;
    ScopedTableSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedTableSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedTableSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedTableSampler.maxLod = 0.0f;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedTableSampler, nullptr, &ScopedTexture->tableSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "VT page table sampler failed");
        MVirtualTextures[ScopedId] = std::move(ScopedTexture);
        DestroyVirtualTexture({ScopedId});
        return {};
    }

    // Both images start in the layout the shader path samples from.
    const VkCommandBuffer ScopedCommand = BeginOneShot();
    if (ScopedCommand == VK_NULL_HANDLE)
    {
        MVirtualTextures[ScopedId] = std::move(ScopedTexture);
        DestroyVirtualTexture({ScopedId});
        return {};
    }
    TransitionImage(ScopedCommand, ScopedTexture->pages, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, InDesc.layerCount);
    TransitionImage(ScopedCommand, ScopedTexture->table, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EndOneShot(ScopedCommand);

    MVirtualTextures[ScopedId] = std::move(ScopedTexture);
    LogInfo("vulkan", "virtual texture #{}: {}x{} x {} pages, {}x{} table", ScopedId, InDesc.pageSize, InDesc.pageSize,
            InDesc.layerCount, InDesc.pageTableSize, InDesc.pageTableSize);
    return VirtualTextureHandle{ScopedId};
}

void VulkanRHI::DestroyVirtualTexture(VirtualTextureHandle InHandle)
{
    if (!InHandle.IsValid() || InHandle.id == 0 || InHandle.id >= MVirtualTextures.size())
    {
        return;
    }
    if (MVirtualTextures[InHandle.id] == nullptr)
    {
        return;
    }
    DestroyVirtualTextureResources(*MVirtualTextures[InHandle.id]);
    MVirtualTextures[InHandle.id].reset();
    MFreeVirtualTextureIds.push_back(InHandle.id);
}

bool VulkanRHI::UpdateVirtualTexturePageTable(VirtualTextureHandle InHandle, std::uint32_t InWidth,
                                              std::uint32_t InHeight, std::span<const std::uint32_t> InEntries)
{
    VirtualTextureResource* ScopedTexture = FindVirtualTexture(InHandle.id);
    if (ScopedTexture == nullptr)
    {
        return false;
    }
    if (InWidth == 0 || InHeight == 0 || InWidth > ScopedTexture->pageTableSize ||
        InHeight > ScopedTexture->pageTableSize || InEntries.size() < static_cast<std::size_t>(InWidth) * InHeight)
    {
        return false;
    }
    const VkDeviceSize ScopedBytes = static_cast<VkDeviceSize>(InWidth) * InHeight * sizeof(std::uint32_t);
    if (MUploadMapped == nullptr || ScopedBytes > MUploadCapacity || MUploadCommandBuffer == VK_NULL_HANDLE)
    {
        return false;
    }
    if (!WaitUploadFence())
    {
        return false;
    }
    std::memcpy(MUploadMapped, InEntries.data(), static_cast<std::size_t>(ScopedBytes));

    Vk::Api& ScopedApi = Vk::GetApi();
    (void)ScopedApi.vkResetCommandBuffer(MUploadCommandBuffer, 0);
    VkCommandBufferBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ScopedBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ScopedApi.vkBeginCommandBuffer(MUploadCommandBuffer, &ScopedBeginInfo) != VK_SUCCESS)
    {
        return false;
    }
    TransitionImage(MUploadCommandBuffer, ScopedTexture->table, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy ScopedCopy{};
    ScopedCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ScopedCopy.imageExtent = {InWidth, InHeight, 1};
    ScopedApi.vkCmdCopyBufferToImage(MUploadCommandBuffer, MUploadBuffer, ScopedTexture->table,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedCopy);
    TransitionImage(MUploadCommandBuffer, ScopedTexture->table, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return SubmitUpload(MUploadCommandBuffer);
}

bool VulkanRHI::UploadVirtualTexturePage(VirtualTextureHandle InHandle, std::uint32_t InLayer, std::uint32_t InSlotX,
                                         std::uint32_t InSlotY, std::uint32_t InWidth, std::uint32_t InHeight,
                                         std::uint32_t InRowPitch, std::span<const std::byte> InPixels)
{
    VirtualTextureResource* ScopedTexture = FindVirtualTexture(InHandle.id);
    if (ScopedTexture == nullptr)
    {
        return false;
    }
    const std::uint32_t ScopedTilesPerAxis = ScopedTexture->pageSize / ScopedTexture->tileSize;
    const bool ScopedValid =
        InLayer < ScopedTexture->layerCount && InSlotX < ScopedTilesPerAxis && InSlotY < ScopedTilesPerAxis &&
        InWidth > 0 && InHeight > 0 && InWidth <= ScopedTexture->tileSize && InHeight <= ScopedTexture->tileSize &&
        InRowPitch >= InWidth * 4u && InPixels.size() >= static_cast<std::size_t>(InRowPitch) * InHeight;
    if (!ScopedValid)
    {
        return false;
    }
    if (MUploadMapped == nullptr || InPixels.size() > MUploadCapacity || MUploadCommandBuffer == VK_NULL_HANDLE)
    {
        return false;
    }
    if (!WaitUploadFence())
    {
        return false;
    }
    std::memcpy(MUploadMapped, InPixels.data(), InPixels.size());

    Vk::Api& ScopedApi = Vk::GetApi();
    (void)ScopedApi.vkResetCommandBuffer(MUploadCommandBuffer, 0);
    VkCommandBufferBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ScopedBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ScopedApi.vkBeginCommandBuffer(MUploadCommandBuffer, &ScopedBeginInfo) != VK_SUCCESS)
    {
        return false;
    }
    TransitionImage(MUploadCommandBuffer, ScopedTexture->pages, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ScopedTexture->layerCount);
    VkBufferImageCopy ScopedCopy{};
    ScopedCopy.bufferRowLength = InRowPitch / 4u;
    ScopedCopy.bufferImageHeight = InHeight;
    ScopedCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, InLayer, 1};
    ScopedCopy.imageOffset = {static_cast<std::int32_t>(InSlotX * ScopedTexture->tileSize),
                              static_cast<std::int32_t>(InSlotY * ScopedTexture->tileSize), 0};
    ScopedCopy.imageExtent = {InWidth, InHeight, 1};
    ScopedApi.vkCmdCopyBufferToImage(MUploadCommandBuffer, MUploadBuffer, ScopedTexture->pages,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedCopy);
    TransitionImage(MUploadCommandBuffer, ScopedTexture->pages, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ScopedTexture->layerCount);
    if (!SubmitUpload(MUploadCommandBuffer))
    {
        return false;
    }
    ++MStats.uploadedBytes;
    return true;
}

std::uint32_t VulkanRHI::ReadVirtualTextureFeedback(std::span<std::uint32_t> OutTiles)
{
    const std::uint32_t ScopedCount =
        std::min<std::uint32_t>(MVtFeedbackCount, static_cast<std::uint32_t>(OutTiles.size()));
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        OutTiles[ScopedI] = MVtFeedbackTiles[ScopedI];
    }
    return ScopedCount;
}

void VulkanRHI::ParseVtFeedback(std::uint32_t InSlot) noexcept
{
    if (InSlot >= kFeedbackSlots || MVtFeedbackMapped[InSlot] == nullptr || MVtFeedbackExtent.width == 0 ||
        MVtFeedbackExtent.height == 0)
    {
        return;
    }
    const std::size_t ScopedTexels = static_cast<std::size_t>(MVtFeedbackExtent.width) * MVtFeedbackExtent.height;
    const std::span<const std::uint32_t> ScopedSource(static_cast<const std::uint32_t*>(MVtFeedbackMapped[InSlot]),
                                                      ScopedTexels);
    MVtFeedbackCount = DeduplicateVirtualTextureFeedback(ScopedSource, MVtFeedbackTiles);
    ++MVtFeedbackReads;
}

void VulkanRHI::DestroyVtFeedbackResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        return;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < kFeedbackSlots; ++ScopedI)
    {
        if (MVtFeedbackMapped[ScopedI] != nullptr)
        {
            ScopedApi.vkUnmapMemory(MDevice, MVtFeedbackBufferMemory[ScopedI]);
            MVtFeedbackMapped[ScopedI] = nullptr;
        }
        if (MVtFeedbackBuffers[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MVtFeedbackBuffers[ScopedI], nullptr);
            MVtFeedbackBuffers[ScopedI] = VK_NULL_HANDLE;
        }
        if (MVtFeedbackBufferMemory[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MVtFeedbackBufferMemory[ScopedI], nullptr);
            MVtFeedbackBufferMemory[ScopedI] = VK_NULL_HANDLE;
        }
        if (MVtFeedbackViews[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImageView(MDevice, MVtFeedbackViews[ScopedI], nullptr);
            MVtFeedbackViews[ScopedI] = VK_NULL_HANDLE;
        }
        if (MVtFeedbackImages[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImage(MDevice, MVtFeedbackImages[ScopedI], nullptr);
            MVtFeedbackImages[ScopedI] = VK_NULL_HANDLE;
        }
        if (MVtFeedbackMemory[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MVtFeedbackMemory[ScopedI], nullptr);
            MVtFeedbackMemory[ScopedI] = VK_NULL_HANDLE;
        }
    }
    MVtFeedbackExtent = {};
}

bool VulkanRHI::EnsureVtFeedback(Extent2D InExtent)
{
    const VkExtent2D ScopedExtent{std::max(InExtent.width / 4u, 1u), std::max(InExtent.height / 4u, 1u)};
    if (ScopedExtent.width == MVtFeedbackExtent.width && ScopedExtent.height == MVtFeedbackExtent.height)
    {
        return MVtFeedbackMapped[0] != nullptr;
    }
    if (MInFrame)
    {
        // Recreating the views rewrites the sprite descriptor sets, which would
        // invalidate the command buffer being recorded. Defer the rebuild to
        // BeginFrame, before anything is recorded for that frame.
        MVtFeedbackPendingExtent = InExtent;
        return MVtFeedbackMapped[0] != nullptr;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MInFrame && MVtFeedbackRecordedThisFrame)
    {
        // A pass in this frame already recorded a clear/copy against the old
        // images; recreate on the next frame instead of freeing live objects.
        LogWarn("vulkan", "VT feedback extent change deferred to the next frame");
        return false;
    }
    if (MInFrame)
    {
        // No command in this frame references the old images yet, but a
        // previous frame may still be in flight.
        ScopedApi.vkDeviceWaitIdle(MDevice);
    }
    DestroyVtFeedbackResources();

    const VkDeviceSize ScopedTexels = static_cast<VkDeviceSize>(ScopedExtent.width) * ScopedExtent.height;
    for (std::uint32_t ScopedI = 0; ScopedI < kFeedbackSlots; ++ScopedI)
    {
        VkImageCreateInfo ScopedInfo{};
        ScopedInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ScopedInfo.imageType = VK_IMAGE_TYPE_2D;
        ScopedInfo.format = VK_FORMAT_R32_UINT;
        ScopedInfo.extent = {ScopedExtent.width, ScopedExtent.height, 1};
        ScopedInfo.mipLevels = 1;
        ScopedInfo.arrayLayers = 1;
        ScopedInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        ScopedInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        ScopedInfo.usage =
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ScopedInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (ScopedApi.vkCreateImage(MDevice, &ScopedInfo, nullptr, &MVtFeedbackImages[ScopedI]) != VK_SUCCESS)
        {
            LogError("vulkan", "VT feedback image failed");
            return false;
        }
        if (std::getenv("FBY_VK_DESCRIPTOR_DEBUG") != nullptr)
        {
        }
        VkMemoryRequirements ScopedRequirements{};
        ScopedApi.vkGetImageMemoryRequirements(MDevice, MVtFeedbackImages[ScopedI], &ScopedRequirements);
        VkMemoryAllocateInfo ScopedAllocate{};
        ScopedAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ScopedAllocate.allocationSize = ScopedRequirements.size;
        ScopedAllocate.memoryTypeIndex = 0;
        for (std::uint32_t ScopedType = 0; ScopedType < MMemoryProperties.memoryTypeCount; ++ScopedType)
        {
            if ((ScopedRequirements.memoryTypeBits & (1u << ScopedType)) != 0 &&
                (MMemoryProperties.memoryTypes[ScopedType].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
            {
                ScopedAllocate.memoryTypeIndex = ScopedType;
                break;
            }
        }
        if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MVtFeedbackMemory[ScopedI]) != VK_SUCCESS ||
            ScopedApi.vkBindImageMemory(MDevice, MVtFeedbackImages[ScopedI], MVtFeedbackMemory[ScopedI], 0) !=
                VK_SUCCESS)
        {
            LogError("vulkan", "VT feedback image memory failed");
            return false;
        }
        VkImageViewCreateInfo ScopedView{};
        ScopedView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ScopedView.image = MVtFeedbackImages[ScopedI];
        ScopedView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ScopedView.format = VK_FORMAT_R32_UINT;
        ScopedView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (ScopedApi.vkCreateImageView(MDevice, &ScopedView, nullptr, &MVtFeedbackViews[ScopedI]) != VK_SUCCESS)
        {
            LogError("vulkan", "VT feedback view failed");
            return false;
        }
        if (!CreateBuffer(ScopedTexels * sizeof(std::uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &MVtFeedbackBuffers[ScopedI], &MVtFeedbackBufferMemory[ScopedI]))
        {
            LogError("vulkan", "VT feedback readback buffer failed");
            return false;
        }
        if (ScopedApi.vkMapMemory(MDevice, MVtFeedbackBufferMemory[ScopedI], 0, ScopedTexels * sizeof(std::uint32_t), 0,
                                  &MVtFeedbackMapped[ScopedI]) != VK_SUCCESS)
        {
            MVtFeedbackMapped[ScopedI] = nullptr;
            return false;
        }
        std::memset(MVtFeedbackMapped[ScopedI], 0, static_cast<std::size_t>(ScopedTexels) * sizeof(std::uint32_t));
    }
    MVtFeedbackExtent = ScopedExtent;

    // The feedback views changed: every sprite descriptor set samples them.
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

    const VkCommandBuffer ScopedCommand = BeginOneShot();
    if (ScopedCommand != VK_NULL_HANDLE)
    {
        for (std::uint32_t ScopedI = 0; ScopedI < kFeedbackSlots; ++ScopedI)
        {
            TransitionImage(ScopedCommand, MVtFeedbackImages[ScopedI], VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL);
        }
        EndOneShot(ScopedCommand);
    }
    LogInfo("vulkan", "VT feedback: {}x{} r32ui x {} slots", ScopedExtent.width, ScopedExtent.height, kFeedbackSlots);
    return true;
}

bool VulkanRHI::CreateOitResolveResources()
{
    if (MRenderPass == VK_NULL_HANDLE)
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();

    VkDescriptorSetLayoutBinding ScopedBindings[2]{};
    for (std::uint32_t ScopedBinding = 0; ScopedBinding < 2; ++ScopedBinding)
    {
        ScopedBindings[ScopedBinding].binding = ScopedBinding;
        ScopedBindings[ScopedBinding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedBindings[ScopedBinding].descriptorCount = 1;
        ScopedBindings[ScopedBinding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo ScopedSetLayoutInfo{};
    ScopedSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetLayoutInfo.bindingCount = 2;
    ScopedSetLayoutInfo.pBindings = ScopedBindings;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetLayoutInfo, nullptr, &MOitResolveSetLayout) !=
        VK_SUCCESS)
    {
        return false;
    }

    VkDescriptorPoolSize ScopedPoolSize{};
    ScopedPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedPoolSize.descriptorCount = 2;
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = 1;
    ScopedPoolInfo.poolSizeCount = 1;
    ScopedPoolInfo.pPoolSizes = &ScopedPoolSize;
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MOitResolvePool) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorSetAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocate.descriptorPool = MOitResolvePool;
    ScopedAllocate.descriptorSetCount = 1;
    ScopedAllocate.pSetLayouts = &MOitResolveSetLayout;
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, &MOitResolveSet) != VK_SUCCESS)
    {
        return false;
    }

    VkSamplerCreateInfo ScopedSamplerInfo{};
    ScopedSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSamplerInfo.magFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.minFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MOitResolveSampler) != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MOitResolveSetLayout;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MOitResolveLayout) != VK_SUCCESS)
    {
        return false;
    }

    return CreateGraphicsPipelineForPass(MRenderPass, MOitResolveLayout, Shaders::fullscreen_vert.data(),
                                         Shaders::fullscreen_vert.size() * sizeof(std::uint32_t),
                                         Shaders::oit_resolve.data(),
                                         Shaders::oit_resolve.size() * sizeof(std::uint32_t), &MOitResolvePipeline);
}

bool VulkanRHI::CreateUpscaleResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorSetLayoutBinding ScopedBinding{};
    ScopedBinding.binding = 0;
    ScopedBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBinding.descriptorCount = 1;
    ScopedBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo ScopedSetLayoutInfo{};
    ScopedSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetLayoutInfo.bindingCount = 1;
    ScopedSetLayoutInfo.pBindings = &ScopedBinding;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetLayoutInfo, nullptr, &MUpscaleSetLayout) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorPoolSize ScopedPoolSize{};
    ScopedPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedPoolSize.descriptorCount = 2;
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = 2 * kMaxFramesInFlight;
    ScopedPoolInfo.poolSizeCount = 1;
    ScopedPoolInfo.pPoolSizes = &ScopedPoolSize;
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MUpscalePool) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorSetAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocate.descriptorPool = MUpscalePool;
    ScopedAllocate.descriptorSetCount = 1;
    ScopedAllocate.pSetLayouts = &MUpscaleSetLayout;
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < kMaxFramesInFlight; ++ScopedFrame)
    {
        if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, &MUpscaleSets[ScopedFrame]) != VK_SUCCESS ||
            ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, &MRcasSets[ScopedFrame]) != VK_SUCCESS)
        {
            return false;
        }
    }
    VkSamplerCreateInfo ScopedSamplerInfo{};
    ScopedSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSamplerInfo.magFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.minFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MUpscaleSampler) != VK_SUCCESS)
    {
        return false;
    }
    VkPushConstantRange ScopedPush{};
    ScopedPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedPush.offset = 0;
    ScopedPush.size = 32;
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MUpscaleSetLayout;
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedPush;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MUpscaleLayout) != VK_SUCCESS)
    {
        return false;
    }
    VkPushConstantRange ScopedRcasPush{};
    ScopedRcasPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedRcasPush.size = 16;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedRcasPush;
    return ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MRcasLayout) == VK_SUCCESS;
}

bool VulkanRHI::CreateHudResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorSetLayoutBinding ScopedBinding{};
    ScopedBinding.binding = 0;
    ScopedBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBinding.descriptorCount = 1;
    ScopedBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo ScopedSetLayoutInfo{};
    ScopedSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetLayoutInfo.bindingCount = 1;
    ScopedSetLayoutInfo.pBindings = &ScopedBinding;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetLayoutInfo, nullptr, &MHudSetLayout) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorPoolSize ScopedPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                        static_cast<std::uint32_t>(kMaxFramesInFlight)};
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = kMaxFramesInFlight;
    ScopedPoolInfo.poolSizeCount = 1;
    ScopedPoolInfo.pPoolSizes = &ScopedPoolSize;
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MHudPool) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorSetAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocate.descriptorPool = MHudPool;
    ScopedAllocate.descriptorSetCount = 1;
    ScopedAllocate.pSetLayouts = &MHudSetLayout;
    for (VkDescriptorSet& ScopedSet : MHudSets)
    {
        if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, &ScopedSet) != VK_SUCCESS)
        {
            return false;
        }
    }
    VkSamplerCreateInfo ScopedSamplerInfo{};
    ScopedSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSamplerInfo.magFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.minFilter = VK_FILTER_LINEAR;
    ScopedSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MHudSampler) != VK_SUCCESS)
    {
        return false;
    }
    ScopedSamplerInfo.magFilter = VK_FILTER_NEAREST;
    ScopedSamplerInfo.minFilter = VK_FILTER_NEAREST;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MHudNearestSampler) != VK_SUCCESS)
    {
        return false;
    }
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MHudSetLayout;
    return ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MHudLayout) == VK_SUCCESS;
}

bool VulkanRHI::CreateMotionResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorSetLayoutBinding ScopedBinding{};
    ScopedBinding.binding = 0;
    ScopedBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedBinding.descriptorCount = 1;
    ScopedBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo ScopedSetLayoutInfo{};
    ScopedSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetLayoutInfo.bindingCount = 1;
    ScopedSetLayoutInfo.pBindings = &ScopedBinding;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetLayoutInfo, nullptr, &MMotionSetLayout) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorPoolSize ScopedPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                        static_cast<std::uint32_t>(kMaxFramesInFlight)};
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = kMaxFramesInFlight;
    ScopedPoolInfo.poolSizeCount = 1;
    ScopedPoolInfo.pPoolSizes = &ScopedPoolSize;
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MMotionPool) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorSetAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocate.descriptorPool = MMotionPool;
    ScopedAllocate.descriptorSetCount = 1;
    ScopedAllocate.pSetLayouts = &MMotionSetLayout;
    for (VkDescriptorSet& ScopedSet : MMotionSets)
    {
        if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, &ScopedSet) != VK_SUCCESS)
        {
            return false;
        }
    }
    VkPushConstantRange ScopedPush{};
    ScopedPush.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ScopedPush.size = 48;
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MMotionSetLayout;
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedPush;
    return ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MMotionLayout) == VK_SUCCESS;
}

bool VulkanRHI::CreateTemporalResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    VkDescriptorSetLayoutBinding ScopedBindings[5]{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 5; ++ScopedIndex)
    {
        ScopedBindings[ScopedIndex].binding = ScopedIndex;
        ScopedBindings[ScopedIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedBindings[ScopedIndex].descriptorCount = 1;
        ScopedBindings[ScopedIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo ScopedSetLayoutInfo{};
    ScopedSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetLayoutInfo.bindingCount = 5;
    ScopedSetLayoutInfo.pBindings = ScopedBindings;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetLayoutInfo, nullptr, &MTemporalSetLayout) !=
        VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorPoolSize ScopedPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5};
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = 1;
    ScopedPoolInfo.poolSizeCount = 1;
    ScopedPoolInfo.pPoolSizes = &ScopedPoolSize;
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MTemporalPool) != VK_SUCCESS)
    {
        return false;
    }
    VkDescriptorSetAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocate.descriptorPool = MTemporalPool;
    ScopedAllocate.descriptorSetCount = 1;
    ScopedAllocate.pSetLayouts = &MTemporalSetLayout;
    for (VkDescriptorSet& ScopedSet : MTemporalSets)
    {
        if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, &ScopedSet) != VK_SUCCESS)
        {
            return false;
        }
    }
    VkPushConstantRange ScopedPush{};
    ScopedPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedPush.size = 32;
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MTemporalSetLayout;
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedPush;
    return ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MTemporalLayout) == VK_SUCCESS;
}

bool VulkanRHI::CreatePipeline()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (!CreateClusterFallback())
    {
        LogError("vulkan", "cluster fallback set failed");
        return false;
    }

    VkPushConstantRange ScopedPushRange{};
    ScopedPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedPushRange.offset = 0;
    ScopedPushRange.size = sizeof(PushConstants);

    const std::array<VkDescriptorSetLayout, 2> ScopedSetLayouts{MDescriptorLayout, MClusterSetLayout};
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = MClusterSetLayout != VK_NULL_HANDLE ? 2u : 1u;
    ScopedLayoutInfo.pSetLayouts = ScopedSetLayouts.data();
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedPushRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MPipelineLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreatePipelineLayout failed");
        return false;
    }

    if (!CreatePipelineForPass(MRenderPass, &MPipeline))
    {
        return false;
    }
    return CreatePipelineForPass(MRenderPass, &MPipelineOpaque, true, true);
}

// One sprite pipeline per render pass. The pass is a parameter because the
bool VulkanRHI::CreatePipelineForPass(VkRenderPass InRenderPass, VkPipeline* OutPipeline, bool InDepthTest,
                                      bool InBlendless, Fby::Rhi::RenderBlendMode InBlend)
{
    Vk::Api& ScopedApi = Vk::GetApi();

    const auto ScopedMakeModule = [&ScopedApi, this](const auto& InBlob)
    {
        VkShaderModuleCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ScopedCreateInfo.codeSize = InBlob.size() * sizeof(std::uint32_t);
        ScopedCreateInfo.pCode = InBlob.data();

        VkShaderModule ScopedModule = VK_NULL_HANDLE;
        if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedCreateInfo, nullptr, &ScopedModule) != VK_SUCCESS)
        {
            LogError("vulkan", "vkCreateShaderModule failed");
        }
        return ScopedModule;
    };

    VkShaderModule ScopedVertexModule = ScopedMakeModule(Shaders::quad_vert);
    VkShaderModule ScopedFragmentModule = ScopedMakeModule(Shaders::quad_frag);
    if (ScopedVertexModule == VK_NULL_HANDLE || ScopedFragmentModule == VK_NULL_HANDLE)
    {
        return false;
    }

    VkPipelineShaderStageCreateInfo ScopedStages[2]{};
    ScopedStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    ScopedStages[0].module = ScopedVertexModule;
    ScopedStages[0].pName = "main";
    ScopedStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedStages[1].module = ScopedFragmentModule;
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

    VkPipelineInputAssemblyStateCreateInfo ScopedInputAssembly{};
    ScopedInputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ScopedInputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ScopedViewportState{};
    ScopedViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ScopedViewportState.viewportCount = 1;
    ScopedViewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo ScopedRaster{};
    ScopedRaster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    ScopedRaster.polygonMode = VK_POLYGON_MODE_FILL;
    ScopedRaster.cullMode = VK_CULL_MODE_NONE;
    ScopedRaster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    ScopedRaster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ScopedMultisample{};
    ScopedMultisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ScopedMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState ScopedBlendAttachment{};
    ScopedBlendAttachment.blendEnable = InBlendless ? VK_FALSE : VK_TRUE;
    ScopedBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ScopedBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ScopedBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    ScopedBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ScopedBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ScopedBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    if (InBlend == Fby::Rhi::RenderBlendMode::Additive)
    {
        ScopedBlendAttachment.blendEnable = VK_TRUE;
        ScopedBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        ScopedBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        ScopedBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ScopedBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    else if (InBlend == Fby::Rhi::RenderBlendMode::Reveal)
    {
        ScopedBlendAttachment.blendEnable = VK_TRUE;
        ScopedBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        ScopedBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ScopedBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        ScopedBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    ScopedBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo ScopedColorBlend{};
    ScopedColorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ScopedColorBlend.attachmentCount = 1;
    ScopedColorBlend.pAttachments = &ScopedBlendAttachment;

    const VkDynamicState ScopedDynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ScopedDynamicState{};
    ScopedDynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ScopedDynamicState.dynamicStateCount = 2;
    ScopedDynamicState.pDynamicStates = ScopedDynamicStates;

    VkPipelineDepthStencilStateCreateInfo ScopedDepthState = MakeDepthStencilState();
    if (InDepthTest)
    {
        ScopedDepthState.depthTestEnable = VK_TRUE;
        ScopedDepthState.depthWriteEnable = VK_TRUE;
        ScopedDepthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    }
    VkGraphicsPipelineCreateInfo ScopedPipelineInfo{};
    ScopedPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ScopedPipelineInfo.stageCount = 2;
    ScopedPipelineInfo.pStages = ScopedStages;
    ScopedPipelineInfo.pVertexInputState = &ScopedVertexInput;
    ScopedPipelineInfo.pInputAssemblyState = &ScopedInputAssembly;
    ScopedPipelineInfo.pViewportState = &ScopedViewportState;
    ScopedPipelineInfo.pRasterizationState = &ScopedRaster;
    ScopedPipelineInfo.pMultisampleState = &ScopedMultisample;
    ScopedPipelineInfo.pColorBlendState = &ScopedColorBlend;
    ScopedPipelineInfo.pDepthStencilState = &ScopedDepthState;
    ScopedPipelineInfo.pDynamicState = &ScopedDynamicState;
    ScopedPipelineInfo.layout = MPipelineLayout;
    ScopedPipelineInfo.renderPass = InRenderPass;
    ScopedPipelineInfo.subpass = 0;

    const VkResult ScopedResult =
        ScopedApi.vkCreateGraphicsPipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo, nullptr, OutPipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedVertexModule, nullptr);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedFragmentModule, nullptr);

    if (ScopedResult != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateGraphicsPipelines failed: {}", static_cast<int>(ScopedResult));
        return false;
    }
    return true;
}

bool VulkanRHI::CreateGeometry()
{
    // A unit quad as two triangles. The vertex shader reads only gl_VertexIndex,
    constexpr std::uint16_t ScopedIndices[kQuadIndexCount] = {0, 1, 2, 3, 4, 5};

    if (!CreateBuffer(sizeof(ScopedIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &MIndexBuffer,
                      &MIndexMemory))
    {
        LogError("vulkan", "index buffer creation failed");
        return false;
    }

    void* ScopedMapped = nullptr;
    if (Vk::GetApi().vkMapMemory(MDevice, MIndexMemory, 0, sizeof(ScopedIndices), 0, &ScopedMapped) != VK_SUCCESS)
    {
        return false;
    }
    std::memcpy(ScopedMapped, ScopedIndices, sizeof(ScopedIndices));
    Vk::GetApi().vkUnmapMemory(MDevice, MIndexMemory);
    return true;
}

bool VulkanRHI::CreateInstanceRing()
{
    MInstanceCapacity = std::max(1u, MDesc.maxItemsPerFrame);
    const VkDeviceSize ScopedSliceBytes = static_cast<VkDeviceSize>(MInstanceCapacity) * sizeof(RenderItem);
    const VkDeviceSize ScopedTotalBytes = ScopedSliceBytes * (MFramesInFlight * 2u);

    const VkBufferUsageFlags ScopedDeviceUsage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!CreateBuffer(ScopedTotalBytes, ScopedDeviceUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &MInstanceBuffer,
                      &MInstanceMemory) ||
        !CreateBuffer(ScopedTotalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &MInstanceStagingBuffer, &MInstanceStagingMemory) ||
        Vk::GetApi().vkMapMemory(MDevice, MInstanceStagingMemory, 0, VK_WHOLE_SIZE, 0, &MInstanceMapped) != VK_SUCCESS)
    {
        LogError("vulkan", "instance ring creation failed ({} bytes)", ScopedTotalBytes);
        return false;
    }

    MTransientCapacity = static_cast<std::size_t>(ScopedSliceBytes);
    MInstanceSliceValid.fill(false);
    MStaticInstanceSliceValid.fill(false);
    MStaticSceneId.fill(0);
    MStaticSceneVersion.fill(0);
    MStaticItemCount.fill(0);
    LogInfo("vulkan", "instance ring: {} items/frame x {} frames = {:.1f} MB device-local + staging", MInstanceCapacity,
            MFramesInFlight, static_cast<double>(ScopedTotalBytes) / (1024.0 * 1024.0));
    return true;
}

bool VulkanRHI::CreateBuiltInAtlas()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    const std::vector<std::byte> ScopedPixels = BuildAtlasPixels();
    const VkDeviceSize ScopedImageBytes = static_cast<VkDeviceSize>(ScopedPixels.size());

    MAtlas.layers = kAtlasLayers;

    VkImageCreateInfo ScopedImageInfo{};
    ScopedImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ScopedImageInfo.extent = {kAtlasSize, kAtlasSize, 1};
    ScopedImageInfo.mipLevels = 1;
    ScopedImageInfo.arrayLayers = kAtlasLayers;
    ScopedImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ScopedImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ScopedImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (ScopedApi.vkCreateImage(MDevice, &ScopedImageInfo, nullptr, &MAtlas.image) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateImage failed");
        return false;
    }

    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MAtlas.image, &ScopedRequirements);

    VkMemoryAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocateInfo.allocationSize = ScopedRequirements.size;
    ScopedAllocateInfo.memoryTypeIndex =
        FindMemoryType(MMemoryProperties, ScopedRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ScopedAllocateInfo.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocateInfo, nullptr, &MAtlas.memory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MAtlas.image, MAtlas.memory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "atlas image allocation failed");
        return false;
    }

    VkBuffer ScopedStaging = VK_NULL_HANDLE;
    VkDeviceMemory ScopedStagingMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(ScopedImageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ScopedStaging,
                      &ScopedStagingMemory))
    {
        return false;
    }

    void* ScopedMapped = nullptr;
    ScopedApi.vkMapMemory(MDevice, ScopedStagingMemory, 0, ScopedImageBytes, 0, &ScopedMapped);
    std::memcpy(ScopedMapped, ScopedPixels.data(), ScopedPixels.size());
    ScopedApi.vkUnmapMemory(MDevice, ScopedStagingMemory);

    const VkCommandBuffer ScopedCommand = BeginOneShot();
    TransitionImage(ScopedCommand, MAtlas.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    kAtlasLayers);

    VkBufferImageCopy ScopedRegion{};
    ScopedRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedRegion.imageSubresource.layerCount = kAtlasLayers;
    ScopedRegion.imageExtent = {kAtlasSize, kAtlasSize, 1};
    ScopedApi.vkCmdCopyBufferToImage(ScopedCommand, ScopedStaging, MAtlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     1, &ScopedRegion);

    TransitionImage(ScopedCommand, MAtlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kAtlasLayers);
    EndOneShot(ScopedCommand);

    ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
    ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);

    VkImageViewCreateInfo ScopedViewInfo{};
    ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedViewInfo.image = MAtlas.image;
    ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ScopedViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ScopedViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedViewInfo.subresourceRange.levelCount = 1;
    ScopedViewInfo.subresourceRange.layerCount = kAtlasLayers;
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &MAtlas.view) != VK_SUCCESS)
    {
        LogError("vulkan", "atlas image view failed");
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
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MAtlas.sampler) != VK_SUCCESS)
    {
        LogError("vulkan", "sampler creation failed");
        return false;
    }

    VkDescriptorImageInfo ScopedImageDescriptor{};
    ScopedImageDescriptor.sampler = MAtlas.sampler;
    ScopedImageDescriptor.imageView = MAtlas.view;
    ScopedImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet ScopedWrite{};
    ScopedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ScopedWrite.dstSet = MDescriptorSet;
    ScopedWrite.dstBinding = 0;
    ScopedWrite.descriptorCount = 1;
    ScopedWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedWrite.pImageInfo = &ScopedImageDescriptor;
    ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedWrite, 0, nullptr);
    WriteSdfDescriptors(MDescriptorSet);

    LogInfo("vulkan", "atlas: {}x{} x {} layers (layer 0 = white)", kAtlasSize, kAtlasSize, kAtlasLayers);
    return true;
}

bool VulkanRHI::CreateMeshResources()
{
    MMeshVertexCapacity = std::max(1u, MDesc.maxMeshVerticesPerFrame);
    // Vulkan buffer copies require 4-byte offsets: keep every index range even.
    MMeshIndexCapacity = (std::max(1u, MDesc.maxMeshIndicesPerFrame) + 1u) & ~1u;
    MBoneMatrixCapacity = std::max(1u, MDesc.maxBoneMatricesPerFrame);
    MMeshDrawCapacity = std::max(1u, std::min(MDesc.maxMeshDrawsPerFrame, kMaxMeshItemsPerFrame));
    MStaticIndexCapacity = (std::max(1u, MDesc.maxStaticMeshIndices) + 1u) & ~1u;

    const VkDeviceSize ScopedIndexPoolBytes = (static_cast<VkDeviceSize>(MStaticIndexCapacity) +
                                               static_cast<VkDeviceSize>(MMeshIndexCapacity) * MFramesInFlight) *
                                              sizeof(std::uint16_t);
    if (!CreateBuffer(ScopedIndexPoolBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &MIndexPool, &MIndexPoolMemory))
    {
        LogError("vulkan", "mesh index pool creation failed ({} bytes)", ScopedIndexPoolBytes);
        return false;
    }

    const VkDeviceSize ScopedVertexBytes = static_cast<VkDeviceSize>(MMeshVertexCapacity) * sizeof(MeshVertex);
    const VkDeviceSize ScopedIndexBytes = static_cast<VkDeviceSize>(MMeshIndexCapacity) * sizeof(std::uint16_t);
    const VkDeviceSize ScopedSkinnedBytes = static_cast<VkDeviceSize>(MMeshVertexCapacity) * sizeof(SkinnedVertex);
    const VkDeviceSize ScopedBoneBytes = static_cast<VkDeviceSize>(MBoneMatrixCapacity) * sizeof(Mat4);
    const VkDeviceSize ScopedRangeBytes = static_cast<VkDeviceSize>(MMeshDrawCapacity) * sizeof(MeshSkinRange);
    const VkDeviceSize ScopedDrawBytes = static_cast<VkDeviceSize>(MMeshDrawCapacity) * sizeof(MeshDrawData);
    // Dynamic and static commands share one host-visible array; worst case one
    const VkDeviceSize ScopedIndirectBytes =
        static_cast<VkDeviceSize>(MMeshDrawCapacity) * 2u * sizeof(VkDrawIndexedIndirectCommand);

    Vk::Api& ScopedApi = Vk::GetApi();
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < MFramesInFlight; ++ScopedFrame)
    {
        MeshFrameBuffers& ScopedBuffers = MMeshFrames[ScopedFrame];
        const auto ScopedCreateAndMap = [&](VkDeviceSize InSize, VkBufferUsageFlags InUsage, VkBuffer* InBuffer,
                                            VkDeviceMemory* InMemory, void** InMapped, const char* InLabel) -> bool
        {
            if (!CreateBuffer(InSize, InUsage,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, InBuffer,
                              InMemory))
            {
                LogError("vulkan", "mesh {} buffer creation failed ({} bytes)", InLabel, InSize);
                return false;
            }
            if (ScopedApi.vkMapMemory(MDevice, *InMemory, 0, VK_WHOLE_SIZE, 0, InMapped) != VK_SUCCESS)
            {
                LogError("vulkan", "mesh {} buffer map failed", InLabel);
                return false;
            }
            return true;
        };

        if (!ScopedCreateAndMap(ScopedVertexBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &ScopedBuffers.vertexBuffer,
                                &ScopedBuffers.vertexMemory, &ScopedBuffers.vertexMapped, "bind vertex") ||
            !ScopedCreateAndMap(ScopedIndexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &ScopedBuffers.indexBuffer,
                                &ScopedBuffers.indexMemory, &ScopedBuffers.indexMapped, "dynamic index staging") ||
            !ScopedCreateAndMap(ScopedBoneBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &ScopedBuffers.boneBuffer,
                                &ScopedBuffers.boneMemory, &ScopedBuffers.boneMapped, "bone") ||
            !ScopedCreateAndMap(ScopedRangeBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &ScopedBuffers.skinRangeBuffer,
                                &ScopedBuffers.skinRangeMemory, &ScopedBuffers.skinRangeMapped, "skin range") ||
            !ScopedCreateAndMap(ScopedDrawBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &ScopedBuffers.drawDataBuffer,
                                &ScopedBuffers.drawDataMemory, &ScopedBuffers.drawDataMapped, "draw data") ||
            !ScopedCreateAndMap(ScopedIndirectBytes, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &ScopedBuffers.indirectBuffer,
                                &ScopedBuffers.indirectMemory, &ScopedBuffers.indirectMapped, "indirect"))
        {
            return false;
        }

        // The skinning output is read by the vertex shader on the GPU only: keep
        if (!CreateBuffer(ScopedSkinnedBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ScopedBuffers.skinnedBuffer,
                          &ScopedBuffers.skinnedMemory))
        {
            LogError("vulkan", "mesh skinned device-local buffer creation failed ({} bytes)", ScopedSkinnedBytes);
            return false;
        }
    }

    MMeshDraws.assign(MMeshDrawCapacity, MeshDrawRecord{});
    MMeshGroups.reserve(64);
    MMeshDrawOrder.reserve(MMeshDrawCapacity);

    LogInfo("vulkan", "mesh rings: {} verts / {} indices / {} bones / {} draws per frame x {} frames",
            MMeshVertexCapacity, MMeshIndexCapacity, MBoneMatrixCapacity, MMeshDrawCapacity, MFramesInFlight);
    return true;
}

bool VulkanRHI::AllocateStaticRange(std::vector<MeshArenaRange>& InFreeList, std::uint32_t InCount,
                                    std::uint32_t* InOffset)
{
    if (InOffset == nullptr || InCount == 0)
    {
        return false;
    }
    for (std::size_t ScopedI = 0; ScopedI < InFreeList.size(); ++ScopedI)
    {
        MeshArenaRange& ScopedRange = InFreeList[ScopedI];
        if (ScopedRange.count < InCount)
        {
            continue;
        }
        *InOffset = ScopedRange.offset;
        ScopedRange.offset += InCount;
        ScopedRange.count -= InCount;
        if (ScopedRange.count == 0)
        {
            InFreeList.erase(InFreeList.begin() + static_cast<std::ptrdiff_t>(ScopedI));
        }
        return true;
    }
    return false;
}

void VulkanRHI::FreeStaticRange(std::vector<MeshArenaRange>& InFreeList, std::uint32_t InOffset, std::uint32_t InCount)
{
    if (InCount == 0)
    {
        return;
    }
    MeshArenaRange ScopedReleased{InOffset, InCount};
    const auto ScopedPosition = std::lower_bound(InFreeList.begin(), InFreeList.end(), ScopedReleased,
                                                 [](const MeshArenaRange& InLeft, const MeshArenaRange& InRight)
                                                 {
                                                     return InLeft.offset < InRight.offset;
                                                 });
    InFreeList.insert(ScopedPosition, ScopedReleased);
    // Coalesce neighbours so a load/unload/load cycle does not fragment the
    for (std::size_t ScopedI = 1; ScopedI < InFreeList.size();)
    {
        MeshArenaRange& ScopedPrevious = InFreeList[ScopedI - 1];
        MeshArenaRange& ScopedCurrent = InFreeList[ScopedI];
        if (ScopedPrevious.offset + ScopedPrevious.count == ScopedCurrent.offset)
        {
            ScopedPrevious.count += ScopedCurrent.count;
            InFreeList.erase(InFreeList.begin() + static_cast<std::ptrdiff_t>(ScopedI));
        }
        else
        {
            ++ScopedI;
        }
    }
}

bool VulkanRHI::CreateStaticMeshArena()
{
    MStaticVertexCapacity = std::max(1u, MDesc.maxStaticMeshVertices);
    MStaticIndexCapacity = (std::max(1u, MDesc.maxStaticMeshIndices) + 1u) & ~1u;

    const VkDeviceSize ScopedVertexBytes = static_cast<VkDeviceSize>(MStaticVertexCapacity) * sizeof(SkinnedVertex);

    if (!CreateBuffer(ScopedVertexBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &MStaticVertexBuffer, &MStaticVertexMemory))
    {
        LogError("vulkan", "static mesh vertex arena allocation failed ({} verts)", MStaticVertexCapacity);
        return false;
    }

    MStaticVertexFree.clear();
    MStaticIndexFree.clear();
    MStaticVertexFree.push_back(MeshArenaRange{0, MStaticVertexCapacity});
    MStaticIndexFree.push_back(MeshArenaRange{0, MStaticIndexCapacity});
    MStaticMeshes.clear();
    MFreeStaticMeshIds.clear();

    LogInfo("vulkan", "static mesh arena: {:.1f} MB device-local vertices, {} indices share the index pool",
            static_cast<double>(ScopedVertexBytes) / (1024.0 * 1024.0), MStaticIndexCapacity);
    return true;
}

bool VulkanRHI::CreateMeshDescriptors()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // Compute: bind pose, skinned output, bone palette, skin range table.
    VkDescriptorSetLayoutBinding ScopedComputeBindings[4]{};
    for (std::uint32_t ScopedBinding = 0; ScopedBinding < 4; ++ScopedBinding)
    {
        ScopedComputeBindings[ScopedBinding].binding = ScopedBinding;
        ScopedComputeBindings[ScopedBinding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ScopedComputeBindings[ScopedBinding].descriptorCount = 1;
        ScopedComputeBindings[ScopedBinding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo ScopedComputeLayoutInfo{};
    ScopedComputeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedComputeLayoutInfo.bindingCount = 4;
    ScopedComputeLayoutInfo.pBindings = ScopedComputeBindings;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedComputeLayoutInfo, nullptr, &MMeshComputeSetLayout) !=
        VK_SUCCESS)
    {
        LogError("vulkan", "mesh compute descriptor layout failed");
        return false;
    }

    // Graphics: skinned vertices, static vertices, per-draw data, bind-pose
    VkDescriptorSetLayoutBinding ScopedGraphicsBindings[4]{};
    for (std::uint32_t ScopedBinding = 0; ScopedBinding < 4; ++ScopedBinding)
    {
        ScopedGraphicsBindings[ScopedBinding].binding = ScopedBinding;
        ScopedGraphicsBindings[ScopedBinding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ScopedGraphicsBindings[ScopedBinding].descriptorCount = 1;
        ScopedGraphicsBindings[ScopedBinding].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    }
    VkDescriptorSetLayoutCreateInfo ScopedGraphicsLayoutInfo{};
    ScopedGraphicsLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedGraphicsLayoutInfo.bindingCount = 4;
    ScopedGraphicsLayoutInfo.pBindings = ScopedGraphicsBindings;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedGraphicsLayoutInfo, nullptr, &MMeshGraphicsSetLayout) !=
        VK_SUCCESS)
    {
        LogError("vulkan", "mesh graphics descriptor layout failed");
        return false;
    }

    VkDescriptorPoolSize ScopedPoolSize{};
    ScopedPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ScopedPoolSize.descriptorCount = MFramesInFlight * 8;
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = MFramesInFlight * 2;
    ScopedPoolInfo.poolSizeCount = 1;
    ScopedPoolInfo.pPoolSizes = &ScopedPoolSize;
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MMeshDescriptorPool) != VK_SUCCESS)
    {
        LogError("vulkan", "mesh descriptor pool failed");
        return false;
    }

    for (std::uint32_t ScopedFrame = 0; ScopedFrame < MFramesInFlight; ++ScopedFrame)
    {
        MeshFrameBuffers& ScopedBuffers = MMeshFrames[ScopedFrame];

        VkDescriptorSetAllocateInfo ScopedComputeAllocate{};
        ScopedComputeAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ScopedComputeAllocate.descriptorPool = MMeshDescriptorPool;
        ScopedComputeAllocate.descriptorSetCount = 1;
        ScopedComputeAllocate.pSetLayouts = &MMeshComputeSetLayout;
        if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedComputeAllocate, &ScopedBuffers.computeSet) !=
            VK_SUCCESS)
        {
            LogError("vulkan", "mesh compute descriptor allocation failed");
            return false;
        }

        VkDescriptorBufferInfo ScopedComputeBuffers[4]{};
        ScopedComputeBuffers[0] = {ScopedBuffers.vertexBuffer, 0, VK_WHOLE_SIZE};
        ScopedComputeBuffers[1] = {ScopedBuffers.skinnedBuffer, 0, VK_WHOLE_SIZE};
        ScopedComputeBuffers[2] = {ScopedBuffers.boneBuffer, 0, VK_WHOLE_SIZE};
        ScopedComputeBuffers[3] = {ScopedBuffers.skinRangeBuffer, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet ScopedComputeWrites[4]{};
        for (std::uint32_t ScopedBinding = 0; ScopedBinding < 4; ++ScopedBinding)
        {
            ScopedComputeWrites[ScopedBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ScopedComputeWrites[ScopedBinding].dstSet = ScopedBuffers.computeSet;
            ScopedComputeWrites[ScopedBinding].dstBinding = ScopedBinding;
            ScopedComputeWrites[ScopedBinding].descriptorCount = 1;
            ScopedComputeWrites[ScopedBinding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ScopedComputeWrites[ScopedBinding].pBufferInfo = &ScopedComputeBuffers[ScopedBinding];
        }
        ScopedApi.vkUpdateDescriptorSets(MDevice, 4, ScopedComputeWrites, 0, nullptr);

        VkDescriptorSetAllocateInfo ScopedGraphicsAllocate{};
        ScopedGraphicsAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ScopedGraphicsAllocate.descriptorPool = MMeshDescriptorPool;
        ScopedGraphicsAllocate.descriptorSetCount = 1;
        ScopedGraphicsAllocate.pSetLayouts = &MMeshGraphicsSetLayout;
        if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedGraphicsAllocate, &ScopedBuffers.graphicsSet) !=
            VK_SUCCESS)
        {
            LogError("vulkan", "mesh graphics descriptor allocation failed");
            return false;
        }

        VkDescriptorBufferInfo ScopedGraphicsBuffers[4]{};
        ScopedGraphicsBuffers[0] = {ScopedBuffers.skinnedBuffer, 0, VK_WHOLE_SIZE};
        ScopedGraphicsBuffers[1] = {MStaticVertexBuffer, 0, VK_WHOLE_SIZE};
        ScopedGraphicsBuffers[2] = {ScopedBuffers.drawDataBuffer, 0, VK_WHOLE_SIZE};
        ScopedGraphicsBuffers[3] = {ScopedBuffers.vertexBuffer, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet ScopedGraphicsWrites[4]{};
        for (std::uint32_t ScopedBinding = 0; ScopedBinding < 4; ++ScopedBinding)
        {
            ScopedGraphicsWrites[ScopedBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ScopedGraphicsWrites[ScopedBinding].dstSet = ScopedBuffers.graphicsSet;
            ScopedGraphicsWrites[ScopedBinding].dstBinding = ScopedBinding;
            ScopedGraphicsWrites[ScopedBinding].descriptorCount = 1;
            ScopedGraphicsWrites[ScopedBinding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ScopedGraphicsWrites[ScopedBinding].pBufferInfo = &ScopedGraphicsBuffers[ScopedBinding];
        }
        ScopedApi.vkUpdateDescriptorSets(MDevice, 4, ScopedGraphicsWrites, 0, nullptr);
    }
    return true;
}

bool VulkanRHI::CreateMeshPipelines()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkPushConstantRange ScopedComputeRange{};
    ScopedComputeRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedComputeRange.offset = 0;
    ScopedComputeRange.size = sizeof(MeshComputePush);
    VkPipelineLayoutCreateInfo ScopedComputeLayoutInfo{};
    ScopedComputeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedComputeLayoutInfo.setLayoutCount = 1;
    ScopedComputeLayoutInfo.pSetLayouts = &MMeshComputeSetLayout;
    ScopedComputeLayoutInfo.pushConstantRangeCount = 1;
    ScopedComputeLayoutInfo.pPushConstantRanges = &ScopedComputeRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedComputeLayoutInfo, nullptr, &MMeshComputeLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "mesh compute pipeline layout failed");
        return false;
    }

    const auto ScopedMakeModule = [&ScopedApi, this](const auto& InBlob)
    {
        VkShaderModuleCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ScopedCreateInfo.codeSize = InBlob.size() * sizeof(std::uint32_t);
        ScopedCreateInfo.pCode = InBlob.data();
        VkShaderModule ScopedModule = VK_NULL_HANDLE;
        if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedCreateInfo, nullptr, &ScopedModule) != VK_SUCCESS)
        {
            LogError("vulkan", "mesh shader module creation failed");
        }
        return ScopedModule;
    };

    const VkShaderModule ScopedComputeModule = ScopedMakeModule(Shaders::mesh_skinning);
    if (ScopedComputeModule == VK_NULL_HANDLE)
    {
        return false;
    }
    VkComputePipelineCreateInfo ScopedComputeInfo{};
    ScopedComputeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ScopedComputeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedComputeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedComputeInfo.stage.module = ScopedComputeModule;
    ScopedComputeInfo.stage.pName = "main";
    ScopedComputeInfo.layout = MMeshComputeLayout;
    const VkResult ScopedComputeResult = ScopedApi.vkCreateComputePipelines(
        MDevice, VK_NULL_HANDLE, 1, &ScopedComputeInfo, nullptr, &MMeshComputePipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedComputeModule, nullptr);
    if (ScopedComputeResult != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateComputePipelines failed: {}", static_cast<int>(ScopedComputeResult));
        return false;
    }

    VkPushConstantRange ScopedGraphicsRange{};
    ScopedGraphicsRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedGraphicsRange.offset = 0;
    ScopedGraphicsRange.size = sizeof(MeshGraphicsPush);
    VkDescriptorSetLayout ScopedSetLayouts[2] = {MDescriptorLayout, MMeshGraphicsSetLayout};
    VkPipelineLayoutCreateInfo ScopedGraphicsLayoutInfo{};
    ScopedGraphicsLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedGraphicsLayoutInfo.setLayoutCount = 2;
    ScopedGraphicsLayoutInfo.pSetLayouts = ScopedSetLayouts;
    ScopedGraphicsLayoutInfo.pushConstantRangeCount = 1;
    ScopedGraphicsLayoutInfo.pPushConstantRanges = &ScopedGraphicsRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedGraphicsLayoutInfo, nullptr, &MMeshGraphicsLayout) !=
        VK_SUCCESS)
    {
        LogError("vulkan", "mesh graphics pipeline layout failed");
        return false;
    }

    return CreateMeshPipelineForPass(MRenderPass, &MMeshPipeline);
}

bool VulkanRHI::CreateMeshPipelineForPass(VkRenderPass InRenderPass, VkPipeline* OutPipeline)
{
    return CreateGraphicsPipelineForPass(InRenderPass, MMeshGraphicsLayout, Shaders::mesh_vert.data(),
                                         Shaders::mesh_vert.size() * sizeof(std::uint32_t), Shaders::mesh_frag.data(),
                                         Shaders::mesh_frag.size() * sizeof(std::uint32_t), OutPipeline);
}

bool VulkanRHI::CreateGraphicsPipelineForPass(VkRenderPass InRenderPass, VkPipelineLayout InLayout,
                                              const void* InVertexSpirv, std::size_t InVertexBytes,
                                              const void* InFragmentSpirv, std::size_t InFragmentBytes,
                                              VkPipeline* OutPipeline)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    const auto ScopedMakeModule = [&ScopedApi, this](const void* InData, std::size_t InBytes)
    {
        VkShaderModuleCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ScopedCreateInfo.codeSize = InBytes;
        ScopedCreateInfo.pCode = static_cast<const std::uint32_t*>(InData);
        VkShaderModule ScopedModule = VK_NULL_HANDLE;
        if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedCreateInfo, nullptr, &ScopedModule) != VK_SUCCESS)
        {
            LogError("vulkan", "mesh shader module creation failed");
        }
        return ScopedModule;
    };

    const VkShaderModule ScopedVertexModule = ScopedMakeModule(InVertexSpirv, InVertexBytes);
    const VkShaderModule ScopedFragmentModule = ScopedMakeModule(InFragmentSpirv, InFragmentBytes);
    if (ScopedVertexModule == VK_NULL_HANDLE || ScopedFragmentModule == VK_NULL_HANDLE)
    {
        return false;
    }

    VkPipelineShaderStageCreateInfo ScopedStages[2]{};
    ScopedStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    ScopedStages[0].module = ScopedVertexModule;
    ScopedStages[0].pName = "main";
    ScopedStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedStages[1].module = ScopedFragmentModule;
    ScopedStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo ScopedVertexInput{};
    ScopedVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ScopedInputAssembly{};
    ScopedInputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ScopedInputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ScopedViewportState{};
    ScopedViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ScopedViewportState.viewportCount = 1;
    ScopedViewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo ScopedRaster{};
    ScopedRaster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    ScopedRaster.polygonMode = VK_POLYGON_MODE_FILL;
    ScopedRaster.cullMode = VK_CULL_MODE_NONE;
    ScopedRaster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    ScopedRaster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ScopedMultisample{};
    ScopedMultisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ScopedMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState ScopedBlendAttachment{};
    ScopedBlendAttachment.blendEnable = VK_TRUE;
    ScopedBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ScopedBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ScopedBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    ScopedBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ScopedBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ScopedBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    ScopedBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo ScopedColorBlend{};
    ScopedColorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ScopedColorBlend.attachmentCount = 1;
    ScopedColorBlend.pAttachments = &ScopedBlendAttachment;

    const VkDynamicState ScopedDynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ScopedDynamicState{};
    ScopedDynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ScopedDynamicState.dynamicStateCount = 2;
    ScopedDynamicState.pDynamicStates = ScopedDynamicStates;

    const VkPipelineDepthStencilStateCreateInfo ScopedDepthState = MakeDepthStencilState();
    VkGraphicsPipelineCreateInfo ScopedPipelineInfo{};
    ScopedPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ScopedPipelineInfo.stageCount = 2;
    ScopedPipelineInfo.pStages = ScopedStages;
    ScopedPipelineInfo.pVertexInputState = &ScopedVertexInput;
    ScopedPipelineInfo.pInputAssemblyState = &ScopedInputAssembly;
    ScopedPipelineInfo.pViewportState = &ScopedViewportState;
    ScopedPipelineInfo.pRasterizationState = &ScopedRaster;
    ScopedPipelineInfo.pMultisampleState = &ScopedMultisample;
    ScopedPipelineInfo.pColorBlendState = &ScopedColorBlend;
    ScopedPipelineInfo.pDepthStencilState = &ScopedDepthState;
    ScopedPipelineInfo.pDynamicState = &ScopedDynamicState;
    ScopedPipelineInfo.layout = InLayout;
    ScopedPipelineInfo.renderPass = InRenderPass;
    ScopedPipelineInfo.subpass = 0;

    const VkResult ScopedResult =
        ScopedApi.vkCreateGraphicsPipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo, nullptr, OutPipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedVertexModule, nullptr);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedFragmentModule, nullptr);
    if (ScopedResult != VK_SUCCESS)
    {
        LogError("vulkan", "mesh pipeline creation failed: {}", static_cast<int>(ScopedResult));
        return false;
    }
    return true;
}

bool VulkanRHI::CreateOffscreenRenderPass()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkAttachmentDescription ScopedColor{};
    ScopedColor.format = VK_FORMAT_R8G8B8A8_UNORM;
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
    ScopedDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkAttachmentDescription ScopedAttachments[] = {ScopedColor, ScopedDepth};
    VkRenderPassCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ScopedCreateInfo.attachmentCount = 2;
    ScopedCreateInfo.pAttachments = ScopedAttachments;
    ScopedCreateInfo.subpassCount = 1;
    ScopedCreateInfo.pSubpasses = &ScopedSubpass;
    ScopedCreateInfo.dependencyCount = 1;
    ScopedCreateInfo.pDependencies = &ScopedDependency;
    if (ScopedApi.vkCreateRenderPass(MDevice, &ScopedCreateInfo, nullptr, &MOffscreenRenderPass) != VK_SUCCESS)
    {
        LogError("vulkan", "offscreen render pass creation failed");
        return false;
    }
    // Material pipelines need a variant compiled against the sRGB attachment.
    ScopedColor.format = VK_FORMAT_R8G8B8A8_SRGB;
    ScopedAttachments[0].format = VK_FORMAT_R8G8B8A8_SRGB;
    if (ScopedApi.vkCreateRenderPass(MDevice, &ScopedCreateInfo, nullptr, &MSrgbOffscreenRenderPass) != VK_SUCCESS)
    {
        // A driver without this format can still run every non-sRGB path;
        // CreateShaderPipeline then keeps the offscreen variant only.
        LogWarn("vulkan", "sRGB offscreen render pass creation failed; sRGB targets fall back to offscreen");
        MSrgbOffscreenRenderPass = VK_NULL_HANDLE;
    }
    (void)CreateAccessibilityCompositeResources();
    return true;
}

bool VulkanRHI::CreateAccessibilityCompositeResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE || MOffscreenRenderPass == VK_NULL_HANDLE)
    {
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
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MAccessibilitySampler) != VK_SUCCESS)
    {
        LogWarn("vulkan", "accessibility composite: sampler creation failed; the CPU path stays");
        DestroyAccessibilityCompositeResources();
        return false;
    }

    VkDescriptorSetLayoutBinding ScopedBindings[2]{};
    ScopedBindings[0].binding = 0;
    ScopedBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[0].descriptorCount = 1;
    ScopedBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[1].binding = 1;
    ScopedBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedBindings[1].descriptorCount = 1;
    ScopedBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo ScopedSetInfo{};
    ScopedSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetInfo.bindingCount = 2;
    ScopedSetInfo.pBindings = ScopedBindings;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetInfo, nullptr, &MAccessibilitySetLayout) != VK_SUCCESS)
    {
        LogWarn("vulkan", "accessibility composite: descriptor set layout failed; the CPU path stays");
        DestroyAccessibilityCompositeResources();
        return false;
    }

    VkDescriptorPoolSize ScopedPoolSizes[2]{};
    ScopedPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedPoolSizes[0].descriptorCount = kMaxRenderTargets + 1;
    ScopedPoolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedPoolSizes[1].descriptorCount = 2 * kMaxRenderTargets;
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = kMaxRenderTargets + 1;
    ScopedPoolInfo.poolSizeCount = 2;
    ScopedPoolInfo.pPoolSizes = ScopedPoolSizes;
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MAccessibilityPool) != VK_SUCCESS)
    {
        LogWarn("vulkan", "accessibility composite: descriptor pool failed; the CPU path stays");
        DestroyAccessibilityCompositeResources();
        return false;
    }

    VkPushConstantRange ScopedRange{};
    ScopedRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedRange.offset = 0;
    ScopedRange.size = sizeof(AccessibilityCompositePush);
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MAccessibilitySetLayout;
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MAccessibilityLayout) != VK_SUCCESS)
    {
        LogWarn("vulkan", "accessibility composite: pipeline layout failed; the CPU path stays");
        DestroyAccessibilityCompositeResources();
        return false;
    }

    const auto ScopedMakeModule = [&ScopedApi, this](const void* InData, std::size_t InBytes)
    {
        VkShaderModuleCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ScopedCreateInfo.codeSize = InBytes;
        ScopedCreateInfo.pCode = static_cast<const std::uint32_t*>(InData);
        VkShaderModule ScopedModule = VK_NULL_HANDLE;
        (void)ScopedApi.vkCreateShaderModule(MDevice, &ScopedCreateInfo, nullptr, &ScopedModule);
        return ScopedModule;
    };
    const VkShaderModule ScopedVertex =
        ScopedMakeModule(Shaders::fullscreen_vert.data(), Shaders::fullscreen_vert.size() * sizeof(std::uint32_t));
    const VkShaderModule ScopedFragment = ScopedMakeModule(
        Shaders::accessibility_composite.data(), Shaders::accessibility_composite.size() * sizeof(std::uint32_t));
    if (ScopedVertex == VK_NULL_HANDLE || ScopedFragment == VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyShaderModule(MDevice, ScopedVertex, nullptr);
        ScopedApi.vkDestroyShaderModule(MDevice, ScopedFragment, nullptr);
        DestroyAccessibilityCompositeResources();
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

    VkPipelineVertexInputStateCreateInfo ScopedVertexInput{};
    ScopedVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ScopedInputAssembly{};
    ScopedInputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ScopedInputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo ScopedViewportState{};
    ScopedViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ScopedViewportState.viewportCount = 1;
    ScopedViewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo ScopedRaster{};
    ScopedRaster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    ScopedRaster.polygonMode = VK_POLYGON_MODE_FILL;
    ScopedRaster.cullMode = VK_CULL_MODE_NONE;
    ScopedRaster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    ScopedRaster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ScopedMultisample{};
    ScopedMultisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ScopedMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ScopedBlend{};
    ScopedBlend.blendEnable = VK_FALSE;
    ScopedBlend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo ScopedColorBlend{};
    ScopedColorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ScopedColorBlend.attachmentCount = 1;
    ScopedColorBlend.pAttachments = &ScopedBlend;
    const VkDynamicState ScopedDynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ScopedDynamicState{};
    ScopedDynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ScopedDynamicState.dynamicStateCount = 2;
    ScopedDynamicState.pDynamicStates = ScopedDynamicStates;

    const VkPipelineDepthStencilStateCreateInfo ScopedDepthState = MakeDepthStencilState();
    VkGraphicsPipelineCreateInfo ScopedPipelineInfo{};
    ScopedPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ScopedPipelineInfo.stageCount = 2;
    ScopedPipelineInfo.pStages = ScopedStages;
    ScopedPipelineInfo.pVertexInputState = &ScopedVertexInput;
    ScopedPipelineInfo.pInputAssemblyState = &ScopedInputAssembly;
    ScopedPipelineInfo.pViewportState = &ScopedViewportState;
    ScopedPipelineInfo.pRasterizationState = &ScopedRaster;
    ScopedPipelineInfo.pMultisampleState = &ScopedMultisample;
    ScopedPipelineInfo.pColorBlendState = &ScopedColorBlend;
    ScopedPipelineInfo.pDepthStencilState = &ScopedDepthState;
    ScopedPipelineInfo.pDynamicState = &ScopedDynamicState;
    ScopedPipelineInfo.layout = MAccessibilityLayout;
    ScopedPipelineInfo.renderPass = MOffscreenRenderPass;
    ScopedPipelineInfo.subpass = 0;
    const VkResult ScopedResult = ScopedApi.vkCreateGraphicsPipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo,
                                                                      nullptr, &MAccessibilityPipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedVertex, nullptr);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedFragment, nullptr);
    if (ScopedResult != VK_SUCCESS)
    {
        LogWarn("vulkan", "accessibility composite: pipeline creation failed; the CPU path stays");
        DestroyAccessibilityCompositeResources();
        return false;
    }

    MAccessibilityReady = true;
    LogInfo("vulkan", "accessibility composite active (colour vision + flash gain per frame)");
    return true;
}

bool VulkanRHI::FlashGuardWanted(const FrameAccessibility& InAccessibility) const noexcept
{
    return MAccessibilityFlashReady && (InAccessibility.flags & AccessibilityFlashGuard) != 0 &&
           InAccessibility.flashGain < 0.999f;
}

void VulkanRHI::RecordFlashGuard(VkCommandBuffer InCommandBuffer, VkImage InColorImage, VkDescriptorSet InSet,
                                 Extent2D InExtent, VkImageLayout InFromLayout,
                                 const FrameAccessibility& InAccessibility) noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    VkImageMemoryBarrier ScopedBarrier{};
    ScopedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedBarrier.oldLayout = InFromLayout;
    ScopedBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.image = InColorImage;
    ScopedBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ScopedBarrier);

    const float ScopedParams[4] = {InAccessibility.flashThreshold, InAccessibility.flashGain, 1.0f, 0.0f};
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MAccessibilityFlashPipeline);
    (void)ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MAccessibilityFlashLayout,
                                            0, 1, &InSet, 0, nullptr);
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MAccessibilityFlashLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 sizeof(ScopedParams), ScopedParams);
    ScopedApi.vkCmdDispatch(InCommandBuffer, (InExtent.width + 7u) / 8u, (InExtent.height + 7u) / 8u, 1);

    ScopedBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    ScopedBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ScopedBarrier);
}

bool VulkanRHI::CreateAccessibilitySwapchainResources()
{
    DestroyAccessibilitySwapchainResources();
    Vk::Api& ScopedApi = Vk::GetApi();
    if (!MAccessibilityReady || MDevice == VK_NULL_HANDLE || MExtent.width == 0 || MExtent.height == 0)
    {
        return false;
    }

    VkImageCreateInfo ScopedImageInfo{};
    ScopedImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedImageInfo.format = MFormat;
    ScopedImageInfo.extent = {MExtent.width, MExtent.height, 1};
    ScopedImageInfo.mipLevels = 1;
    ScopedImageInfo.arrayLayers = 1;
    ScopedImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // The accessibility flash guard writes into this image from a compute shader,
    // so it needs STORAGE usage. An sRGB format is not required by the spec to
    // expose STORAGE_IMAGE, but the desktop drivers used by the project accept it;
    // the image creation check below disables the composite on devices that do not.
    ScopedImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ScopedImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedImageInfo, nullptr, &MAccessibilitySwapImage) != VK_SUCCESS)
    {
        DestroyAccessibilitySwapchainResources();
        return false;
    }
    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MAccessibilitySwapImage, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex =
        FindMemoryType(MMemoryProperties, ScopedRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkImageViewCreateInfo ScopedView{};
    ScopedView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedView.image = MAccessibilitySwapImage;
    ScopedView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedView.format = MFormat;
    ScopedView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedView.subresourceRange.levelCount = 1;
    ScopedView.subresourceRange.layerCount = 1;
    if (!CreateDepthImage(Extent2D{MExtent.width, MExtent.height}, &MAccessibilitySwapDepthImage,
                          &MAccessibilitySwapDepthMemory, &MAccessibilitySwapDepthView))
    {
        LogWarn("vulkan", "accessibility swapchain composite: depth failed; direct path stays");
        DestroyAccessibilitySwapchainResources();
        return false;
    }
    VkFramebufferCreateInfo ScopedDepthPrepassFramebuffer{};
    if (MDepthPrepassRenderPass != VK_NULL_HANDLE)
    {
        ScopedDepthPrepassFramebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ScopedDepthPrepassFramebuffer.renderPass = MDepthPrepassRenderPass;
        ScopedDepthPrepassFramebuffer.attachmentCount = 1;
        ScopedDepthPrepassFramebuffer.pAttachments = &MAccessibilitySwapDepthView;
        ScopedDepthPrepassFramebuffer.width = MExtent.width;
        ScopedDepthPrepassFramebuffer.height = MExtent.height;
        ScopedDepthPrepassFramebuffer.layers = 1;
        if (ScopedApi.vkCreateFramebuffer(MDevice, &ScopedDepthPrepassFramebuffer, nullptr,
                                          &MAccessibilityDepthPrepassFramebuffer) != VK_SUCCESS)
        {
            LogWarn("vulkan", "accessibility depth prepass framebuffer failed");
            DestroyAccessibilitySwapchainResources();
            return false;
        }
    }
    // The image view must exist before the framebuffer names it. Capturing the
    // attachment array first handed the driver a null view handle, and the
    // resulting framebuffer faulted on its first vkCmdBeginRenderPass.
    if (ScopedAllocate.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MAccessibilitySwapMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MAccessibilitySwapImage, MAccessibilitySwapMemory, 0) != VK_SUCCESS ||
        ScopedApi.vkCreateImageView(MDevice, &ScopedView, nullptr, &MAccessibilitySwapView) != VK_SUCCESS)
    {
        LogWarn("vulkan", "accessibility swapchain composite: resources failed; direct path stays");
        DestroyAccessibilitySwapchainResources();
        return false;
    }
    const VkImageView ScopedAttachments[2] = {MAccessibilitySwapView, MAccessibilitySwapDepthView};
    VkFramebufferCreateInfo ScopedFramebuffer{};
    ScopedFramebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ScopedFramebuffer.renderPass = MRenderPass;
    ScopedFramebuffer.attachmentCount = 2;
    ScopedFramebuffer.pAttachments = ScopedAttachments;
    ScopedFramebuffer.width = MExtent.width;
    ScopedFramebuffer.height = MExtent.height;
    ScopedFramebuffer.layers = 1;
    if (ScopedApi.vkCreateFramebuffer(MDevice, &ScopedFramebuffer, nullptr, &MAccessibilitySwapFramebuffer) !=
        VK_SUCCESS)
    {
        LogWarn("vulkan", "accessibility swapchain composite: framebuffer failed; direct path stays");
        DestroyAccessibilitySwapchainResources();
        return false;
    }

    // The composite pipeline is tied to the swapchain render pass.
    const auto ScopedMakeModule = [&ScopedApi, this](const void* InData, std::size_t InBytes)
    {
        VkShaderModuleCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ScopedCreateInfo.codeSize = InBytes;
        ScopedCreateInfo.pCode = static_cast<const std::uint32_t*>(InData);
        VkShaderModule ScopedModule = VK_NULL_HANDLE;
        (void)ScopedApi.vkCreateShaderModule(MDevice, &ScopedCreateInfo, nullptr, &ScopedModule);
        return ScopedModule;
    };
    const VkShaderModule ScopedVertex =
        ScopedMakeModule(Shaders::fullscreen_vert.data(), Shaders::fullscreen_vert.size() * sizeof(std::uint32_t));
    const VkShaderModule ScopedFragment = ScopedMakeModule(
        Shaders::accessibility_composite.data(), Shaders::accessibility_composite.size() * sizeof(std::uint32_t));
    if (ScopedVertex == VK_NULL_HANDLE || ScopedFragment == VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyShaderModule(MDevice, ScopedVertex, nullptr);
        ScopedApi.vkDestroyShaderModule(MDevice, ScopedFragment, nullptr);
        DestroyAccessibilitySwapchainResources();
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
    VkPipelineVertexInputStateCreateInfo ScopedVertexInput{};
    ScopedVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ScopedInputAssembly{};
    ScopedInputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ScopedInputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo ScopedViewportState{};
    ScopedViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ScopedViewportState.viewportCount = 1;
    ScopedViewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo ScopedRaster{};
    ScopedRaster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    ScopedRaster.polygonMode = VK_POLYGON_MODE_FILL;
    ScopedRaster.cullMode = VK_CULL_MODE_NONE;
    ScopedRaster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    ScopedRaster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ScopedMultisample{};
    ScopedMultisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ScopedMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ScopedBlend{};
    ScopedBlend.blendEnable = VK_FALSE;
    ScopedBlend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo ScopedColorBlend{};
    ScopedColorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ScopedColorBlend.attachmentCount = 1;
    ScopedColorBlend.pAttachments = &ScopedBlend;
    const VkDynamicState ScopedDynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ScopedDynamicState{};
    ScopedDynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ScopedDynamicState.dynamicStateCount = 2;
    ScopedDynamicState.pDynamicStates = ScopedDynamicStates;
    VkGraphicsPipelineCreateInfo ScopedPipelineInfo{};
    ScopedPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ScopedPipelineInfo.stageCount = 2;
    ScopedPipelineInfo.pStages = ScopedStages;
    ScopedPipelineInfo.pVertexInputState = &ScopedVertexInput;
    ScopedPipelineInfo.pInputAssemblyState = &ScopedInputAssembly;
    ScopedPipelineInfo.pViewportState = &ScopedViewportState;
    ScopedPipelineInfo.pRasterizationState = &ScopedRaster;
    ScopedPipelineInfo.pMultisampleState = &ScopedMultisample;
    ScopedPipelineInfo.pColorBlendState = &ScopedColorBlend;
    ScopedPipelineInfo.pDynamicState = &ScopedDynamicState;
    // A full-screen composite still needs a depth-stencil state: the render pass
    // has a depth attachment, so the pipeline must describe how to treat it.
    VkPipelineDepthStencilStateCreateInfo ScopedDepthState{};
    ScopedDepthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ScopedDepthState.depthTestEnable = VK_FALSE;
    ScopedDepthState.depthWriteEnable = VK_FALSE;
    ScopedDepthState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    ScopedDepthState.stencilTestEnable = VK_FALSE;
    ScopedPipelineInfo.pDepthStencilState = &ScopedDepthState;
    ScopedPipelineInfo.layout = MAccessibilityLayout;
    ScopedPipelineInfo.renderPass = MRenderPass;
    ScopedPipelineInfo.subpass = 0;
    const VkResult ScopedResult = ScopedApi.vkCreateGraphicsPipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo,
                                                                      nullptr, &MAccessibilitySwapPipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedVertex, nullptr);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedFragment, nullptr);
    if (ScopedResult != VK_SUCCESS)
    {
        LogWarn("vulkan", "accessibility swapchain composite: pipeline failed; direct path stays");
        DestroyAccessibilitySwapchainResources();
        return false;
    }

    VkDescriptorSetAllocateInfo ScopedSetAllocate{};
    ScopedSetAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedSetAllocate.descriptorPool = MAccessibilityPool;
    ScopedSetAllocate.descriptorSetCount = 1;
    ScopedSetAllocate.pSetLayouts = &MAccessibilitySetLayout;
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedSetAllocate, &MAccessibilitySwapSet) == VK_SUCCESS)
    {
        VkDescriptorImageInfo ScopedImage{};
        ScopedImage.sampler = MAccessibilitySampler;
        ScopedImage.imageView = MAccessibilitySwapView;
        ScopedImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet ScopedWrite{};
        ScopedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrite.dstSet = MAccessibilitySwapSet;
        ScopedWrite.dstBinding = 0;
        ScopedWrite.descriptorCount = 1;
        ScopedWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrite.pImageInfo = &ScopedImage;
        ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedWrite, 0, nullptr);
    }
    // The flash guard now runs inside the composite fragment shader and only uses
    // this r8 image as luminance history. R8_UNORM supports storage images, so the
    // sRGB scene image no longer needs STORAGE usage.
    {
        VkImageCreateInfo ScopedFlashInfo{};
        ScopedFlashInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ScopedFlashInfo.imageType = VK_IMAGE_TYPE_2D;
        ScopedFlashInfo.format = VK_FORMAT_R8_UNORM;
        ScopedFlashInfo.extent = {MExtent.width, MExtent.height, 1};
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
        const bool ScopedFlashImage =
            ScopedApi.vkCreateImage(MDevice, &ScopedFlashInfo, nullptr, &MAccessibilitySwapFlashImage) == VK_SUCCESS;
        if (ScopedFlashImage)
        {
            VkMemoryRequirements ScopedFlashRequirements{};
            ScopedApi.vkGetImageMemoryRequirements(MDevice, MAccessibilitySwapFlashImage, &ScopedFlashRequirements);
            ScopedFlashAllocate.allocationSize = ScopedFlashRequirements.size;
            ScopedFlashAllocate.memoryTypeIndex = FindMemoryType(
                MMemoryProperties, ScopedFlashRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            ScopedFlashView.image = MAccessibilitySwapFlashImage;
        }
        const bool ScopedFlashOk =
            ScopedFlashImage && ScopedFlashAllocate.memoryTypeIndex != std::numeric_limits<std::uint32_t>::max() &&
            ScopedApi.vkAllocateMemory(MDevice, &ScopedFlashAllocate, nullptr, &MAccessibilitySwapFlashMemory) ==
                VK_SUCCESS &&
            ScopedApi.vkBindImageMemory(MDevice, MAccessibilitySwapFlashImage, MAccessibilitySwapFlashMemory, 0) ==
                VK_SUCCESS &&
            ScopedApi.vkCreateImageView(MDevice, &ScopedFlashView, nullptr, &MAccessibilitySwapFlashView) == VK_SUCCESS;
        if (ScopedFlashOk)
        {
            InitImageLayout(MAccessibilitySwapFlashImage, VK_IMAGE_LAYOUT_GENERAL);
            if (MAccessibilitySwapSet != VK_NULL_HANDLE)
            {
                VkDescriptorImageInfo ScopedHistory{};
                ScopedHistory.imageView = MAccessibilitySwapFlashView;
                ScopedHistory.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkWriteDescriptorSet ScopedHistoryWrite{};
                ScopedHistoryWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                ScopedHistoryWrite.dstSet = MAccessibilitySwapSet;
                ScopedHistoryWrite.dstBinding = 1;
                ScopedHistoryWrite.descriptorCount = 1;
                ScopedHistoryWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                ScopedHistoryWrite.pImageInfo = &ScopedHistory;
                ScopedApi.vkUpdateDescriptorSets(MDevice, 1, &ScopedHistoryWrite, 0, nullptr);
            }
        }
    }

    MAccessibilitySwapReady = true;
    LogInfo("vulkan", "accessibility composite active on the swapchain path");
    (void)CreatePostFxResources();
    return true;
}
bool VulkanRHI::CreatePostFxResources()
{
    if (MDevice == VK_NULL_HANDLE || MAccessibilitySwapView == VK_NULL_HANDLE ||
        MAccessibilitySampler == VK_NULL_HANDLE)
    {
        LogWarn("vulkan", "postfx composite: accessibility offscreen resources are missing");
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();

    VkDescriptorSetLayoutBinding ScopedBindings[2]{};
    for (std::uint32_t ScopedI = 0; ScopedI < 2; ++ScopedI)
    {
        ScopedBindings[ScopedI].binding = ScopedI;
        ScopedBindings[ScopedI].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedBindings[ScopedI].descriptorCount = 1;
        ScopedBindings[ScopedI].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo ScopedSetInfo{};
    ScopedSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetInfo.bindingCount = 2;
    ScopedSetInfo.pBindings = ScopedBindings;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetInfo, nullptr, &MPostFxSetLayout) != VK_SUCCESS)
    {
        LogWarn("vulkan", "postfx composite: descriptor set layout creation failed");
        return false;
    }

    VkPushConstantRange ScopedPushRange{};
    ScopedPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedPushRange.size = sizeof(PostFxBlock);
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MPostFxSetLayout;
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedPushRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MPostFxLayout) != VK_SUCCESS)
    {
        LogWarn("vulkan", "postfx composite: pipeline layout creation failed");
        return false;
    }

    VkDescriptorPoolSize ScopedPoolSize{};
    ScopedPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedPoolSize.descriptorCount = 2;
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = 1;
    ScopedPoolInfo.poolSizeCount = 1;
    ScopedPoolInfo.pPoolSizes = &ScopedPoolSize;
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MPostFxPool) != VK_SUCCESS)
    {
        LogWarn("vulkan", "postfx composite: descriptor pool creation failed");
        return false;
    }

    const auto ScopedMakeModule = [&ScopedApi, this](const void* InData, std::size_t InBytes)
    {
        VkShaderModuleCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ScopedCreateInfo.codeSize = InBytes;
        ScopedCreateInfo.pCode = static_cast<const std::uint32_t*>(InData);
        VkShaderModule ScopedModule = VK_NULL_HANDLE;
        (void)ScopedApi.vkCreateShaderModule(MDevice, &ScopedCreateInfo, nullptr, &ScopedModule);
        return ScopedModule;
    };
    const VkShaderModule ScopedVertex =
        ScopedMakeModule(Shaders::fullscreen_vert.data(), Shaders::fullscreen_vert.size() * sizeof(std::uint32_t));
    const VkShaderModule ScopedFragment =
        ScopedMakeModule(Shaders::postfx_composite.data(), Shaders::postfx_composite.size() * sizeof(std::uint32_t));
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

    VkPipelineVertexInputStateCreateInfo ScopedVertexInput{};
    ScopedVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ScopedInputAssembly{};
    ScopedInputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ScopedInputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo ScopedViewportState{};
    ScopedViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ScopedViewportState.viewportCount = 1;
    ScopedViewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo ScopedRaster{};
    ScopedRaster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    ScopedRaster.polygonMode = VK_POLYGON_MODE_FILL;
    ScopedRaster.cullMode = VK_CULL_MODE_NONE;
    ScopedRaster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    ScopedRaster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ScopedMultisample{};
    ScopedMultisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ScopedMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ScopedBlend{};
    ScopedBlend.blendEnable = VK_FALSE;
    ScopedBlend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo ScopedColorBlend{};
    ScopedColorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ScopedColorBlend.attachmentCount = 1;
    ScopedColorBlend.pAttachments = &ScopedBlend;
    const VkDynamicState ScopedDynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ScopedDynamicState{};
    ScopedDynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ScopedDynamicState.dynamicStateCount = 2;
    ScopedDynamicState.pDynamicStates = ScopedDynamicStates;
    VkGraphicsPipelineCreateInfo ScopedPipelineInfo{};
    ScopedPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ScopedPipelineInfo.stageCount = 2;
    ScopedPipelineInfo.pStages = ScopedStages;
    ScopedPipelineInfo.pVertexInputState = &ScopedVertexInput;
    ScopedPipelineInfo.pInputAssemblyState = &ScopedInputAssembly;
    ScopedPipelineInfo.pViewportState = &ScopedViewportState;
    ScopedPipelineInfo.pRasterizationState = &ScopedRaster;
    ScopedPipelineInfo.pMultisampleState = &ScopedMultisample;
    ScopedPipelineInfo.pColorBlendState = &ScopedColorBlend;
    ScopedPipelineInfo.pDynamicState = &ScopedDynamicState;
    VkPipelineDepthStencilStateCreateInfo ScopedDepthState{};
    ScopedDepthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ScopedDepthState.depthTestEnable = VK_FALSE;
    ScopedDepthState.depthWriteEnable = VK_FALSE;
    ScopedDepthState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    ScopedDepthState.stencilTestEnable = VK_FALSE;
    ScopedPipelineInfo.pDepthStencilState = &ScopedDepthState;
    ScopedPipelineInfo.layout = MPostFxLayout;
    ScopedPipelineInfo.renderPass = MRenderPass;
    ScopedPipelineInfo.subpass = 0;
    const VkResult ScopedResult =
        ScopedApi.vkCreateGraphicsPipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo, nullptr, &MPostFxPipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedVertex, nullptr);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedFragment, nullptr);
    if (ScopedResult != VK_SUCCESS)
    {
        LogWarn("vulkan", "postfx composite: pipeline creation failed; direct path stays");
        return false;
    }

    VkDescriptorSetAllocateInfo ScopedSetAllocate{};
    ScopedSetAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedSetAllocate.descriptorPool = MPostFxPool;
    ScopedSetAllocate.descriptorSetCount = 1;
    ScopedSetAllocate.pSetLayouts = &MPostFxSetLayout;
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedSetAllocate, &MPostFxSet) != VK_SUCCESS)
    {
        LogWarn("vulkan", "postfx composite: descriptor set allocation failed");
        return false;
    }
    VkDescriptorImageInfo ScopedImage{};
    ScopedImage.sampler = MAccessibilitySampler;
    ScopedImage.imageView = MAccessibilitySwapView;
    ScopedImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet ScopedWrites[2]{};
    for (std::uint32_t ScopedI = 0; ScopedI < 2; ++ScopedI)
    {
        ScopedWrites[ScopedI].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[ScopedI].dstSet = MPostFxSet;
        ScopedWrites[ScopedI].dstBinding = ScopedI;
        ScopedWrites[ScopedI].descriptorCount = 1;
        ScopedWrites[ScopedI].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrites[ScopedI].pImageInfo = &ScopedImage;
    }
    ScopedApi.vkUpdateDescriptorSets(MDevice, 2, ScopedWrites, 0, nullptr);
    MPostFxReady = true;
    return true;
}

void VulkanRHI::RecordPostFxComposite(VkCommandBuffer InCommandBuffer, const PostFxBlock& InBlock,
                                      const Extent2D& InExtent)
{
    Vk::Api& ScopedApi = Vk::GetApi();
    VkImageMemoryBarrier ScopedBarrier{};
    ScopedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    ScopedBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.image = MAccessibilitySwapImage;
    ScopedBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ScopedBarrier);

    VkClearValue ScopedClearValues[2]{};
    ScopedClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    ScopedClearValues[1].depthStencil.depth = 1.0f;
    ScopedClearValues[1].depthStencil.stencil = 0;
    VkRenderPassBeginInfo ScopedBegin{};
    ScopedBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBegin.renderPass = MRenderPass;
    ScopedBegin.framebuffer = MFramebuffers[MImageIndex];
    ScopedBegin.renderArea.extent = {InExtent.width, InExtent.height};
    ScopedBegin.clearValueCount = 2;
    ScopedBegin.pClearValues = ScopedClearValues;
    ScopedApi.vkCmdBeginRenderPass(InCommandBuffer, &ScopedBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(InExtent.width);
    ScopedViewport.height = static_cast<float>(InExtent.height);
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(InCommandBuffer, 0, 1, &ScopedViewport);
    VkRect2D ScopedScissor{};
    ScopedScissor.extent = {InExtent.width, InExtent.height};
    ScopedApi.vkCmdSetScissor(InCommandBuffer, 0, 1, &ScopedScissor);
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MPostFxPipeline);
    ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MPostFxLayout, 0, 1,
                                      &MPostFxSet, 0, nullptr);
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MPostFxLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(InBlock),
                                 &InBlock);
    ScopedApi.vkCmdDraw(InCommandBuffer, 3, 1, 0, 0);
    ScopedApi.vkCmdEndRenderPass(InCommandBuffer);
}

void VulkanRHI::RecordPostFxComposite(VkCommandBuffer InCommandBuffer, const PostFxSettings& InSettings,
                                      const Extent2D& InExtent)
{
    // Bloom is not sampled from a real blur chain yet; keep the flag off so the
    // composite does not add the scene to itself.
    const std::uint32_t ScopedFlags = InSettings.flags & ~PostFxBloom;
    PostFxBlock ScopedBlock;
    ScopedBlock.flagsAndCrt = Vector4D{static_cast<float>(ScopedFlags), InSettings.crtCurvature,
                                       InSettings.scanlineStrength, static_cast<float>(MFrameCounter % 1024u)};
    ScopedBlock.vignetteAndBloom = Vector4D{InSettings.vignetteStrength, InSettings.bloomThreshold,
                                            InSettings.bloomIntensity, InSettings.exposure};
    ScopedBlock.exposureAndChromatic = Vector4D{InSettings.chromaticAberration, InSettings.filmGrain,
                                                InSettings.flashGuardGain, InSettings.colorVisionStrength};
    ScopedBlock.grainAndFlash = Vector4D{static_cast<float>(MFrameCounter % 997u), 60.0f, 0.0f, 0.0f};
    const ColorMatrix3 ScopedMatrix =
        ColorVisionMatrix(static_cast<ColorVisionDeficiency>(std::min<std::uint32_t>(InSettings.colorVision, 3u)));
    ScopedBlock.colorRow0 = Vector4D{ScopedMatrix.m[0], ScopedMatrix.m[1], ScopedMatrix.m[2], 0.0f};
    ScopedBlock.colorRow1 = Vector4D{ScopedMatrix.m[3], ScopedMatrix.m[4], ScopedMatrix.m[5], 0.0f};
    ScopedBlock.colorRow2 = Vector4D{ScopedMatrix.m[6], ScopedMatrix.m[7], ScopedMatrix.m[8], 0.0f};
    RecordPostFxComposite(InCommandBuffer, ScopedBlock, InExtent);
}

void VulkanRHI::DestroyAccessibilitySwapchainResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice != VK_NULL_HANDLE)
    {
        if (MAccessibilitySwapPipeline != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, MAccessibilitySwapPipeline, nullptr);
        }
        if (MAccessibilitySwapFlashView != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImageView(MDevice, MAccessibilitySwapFlashView, nullptr);
        }
        if (MAccessibilitySwapFlashImage != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImage(MDevice, MAccessibilitySwapFlashImage, nullptr);
        }
        if (MAccessibilitySwapFlashMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MAccessibilitySwapFlashMemory, nullptr);
        }
        if (MAccessibilityDepthPrepassFramebuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyFramebuffer(MDevice, MAccessibilityDepthPrepassFramebuffer, nullptr);
        }
        if (MAccessibilitySwapFramebuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyFramebuffer(MDevice, MAccessibilitySwapFramebuffer, nullptr);
        }
        DestroyDepthImage(&MAccessibilitySwapDepthImage, &MAccessibilitySwapDepthMemory, &MAccessibilitySwapDepthView);
        if (MAccessibilitySwapView != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImageView(MDevice, MAccessibilitySwapView, nullptr);
        }
        if (MAccessibilitySwapImage != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyImage(MDevice, MAccessibilitySwapImage, nullptr);
        }
        if (MAccessibilitySwapMemory != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MAccessibilitySwapMemory, nullptr);
        }
    }
    MAccessibilitySwapFlashView = VK_NULL_HANDLE;
    MAccessibilitySwapFlashImage = VK_NULL_HANDLE;
    MAccessibilitySwapFlashMemory = VK_NULL_HANDLE;
    MAccessibilitySwapPipeline = VK_NULL_HANDLE;
    MAccessibilitySwapFramebuffer = VK_NULL_HANDLE;
    MAccessibilityDepthPrepassFramebuffer = VK_NULL_HANDLE;
    MAccessibilitySwapView = VK_NULL_HANDLE;
    MAccessibilitySwapImage = VK_NULL_HANDLE;
    MAccessibilitySwapMemory = VK_NULL_HANDLE;
    MAccessibilitySwapDepthImage = VK_NULL_HANDLE;
    MAccessibilitySwapDepthMemory = VK_NULL_HANDLE;
    MAccessibilitySwapDepthView = VK_NULL_HANDLE;
    MAccessibilitySwapSet = VK_NULL_HANDLE;
    if (MPostFxPipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MPostFxPipeline, nullptr);
        MPostFxPipeline = VK_NULL_HANDLE;
    }
    if (MPostFxLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipelineLayout(MDevice, MPostFxLayout, nullptr);
        MPostFxLayout = VK_NULL_HANDLE;
    }
    if (MPostFxPool != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorPool(MDevice, MPostFxPool, nullptr);
        MPostFxPool = VK_NULL_HANDLE;
    }
    if (MPostFxSetLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MPostFxSetLayout, nullptr);
        MPostFxSetLayout = VK_NULL_HANDLE;
    }
    MPostFxSet = VK_NULL_HANDLE;
    MPostFxReady = false;
    MPostFxThisFrame = false;
    MAccessibilitySwapReady = false;
    MAccessibilitySwapActive = false;
}

void VulkanRHI::DestroyAccessibilityCompositeResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice != VK_NULL_HANDLE)
    {
        if (MAccessibilityPipeline != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipeline(MDevice, MAccessibilityPipeline, nullptr);
        }
        if (MAccessibilityLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyPipelineLayout(MDevice, MAccessibilityLayout, nullptr);
        }
        if (MAccessibilityPool != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorPool(MDevice, MAccessibilityPool, nullptr);
        }
        if (MAccessibilitySetLayout != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MAccessibilitySetLayout, nullptr);
        }
        if (MAccessibilitySampler != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySampler(MDevice, MAccessibilitySampler, nullptr);
        }
    }
    MAccessibilityPipeline = VK_NULL_HANDLE;
    MAccessibilityLayout = VK_NULL_HANDLE;
    MAccessibilityPool = VK_NULL_HANDLE;
    MAccessibilitySetLayout = VK_NULL_HANDLE;
    MAccessibilitySampler = VK_NULL_HANDLE;
    MAccessibilityReady = false;
}
bool VulkanRHI::AccessibilityCompositeWanted(const FramePacket& InPacket) const noexcept
{
    if (!MAccessibilityReady)
    {
        return false;
    }
    const std::uint32_t ScopedFlags = InPacket.accessibility.flags;
    const bool ScopedCvd = (ScopedFlags & AccessibilityColorVision) != 0 && InPacket.accessibility.colorVision != 0;
    const bool ScopedGain = (ScopedFlags & AccessibilityFlashGuard) != 0 && InPacket.accessibility.flashGain < 0.999f;
    return ScopedCvd || ScopedGain;
}

void VulkanRHI::RecordAccessibilityComposite(VkCommandBuffer InCommandBuffer, VkRenderPass InRenderPass,
                                             VkFramebuffer InFramebuffer, VkPipeline InPipeline, VkDescriptorSet InSet,
                                             Extent2D InExtent, const FrameAccessibility& InAccessibility,
                                             bool InHasDepth) noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    const std::uint32_t ScopedFlags = InAccessibility.flags;
    AccessibilityCompositePush ScopedPush{};
    const ColorMatrix3 ScopedMatrix =
        ColorVisionMatrix(static_cast<ColorVisionDeficiency>(std::min<std::uint32_t>(InAccessibility.colorVision, 3u)));
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 9; ++ScopedIndex)
    {
        const std::uint32_t ScopedColumn = ScopedIndex / 3u;
        const std::uint32_t ScopedRow = ScopedIndex % 3u;
        ScopedPush.matrix[ScopedColumn * 4u + ScopedRow] = ScopedMatrix.m[ScopedIndex];
    }
    ScopedPush.safety[0] = InAccessibility.flashGain;
    ScopedPush.safety[1] = InAccessibility.colorVisionStrength;
    ScopedPush.safety[2] = (ScopedFlags & AccessibilityColorVision) != 0 ? 1.0f : 0.0f;
    ScopedPush.safety[3] = 0.0f;
    ScopedPush.flashGuard[0] = InAccessibility.flashThreshold;
    ScopedPush.flashGuard[1] = InAccessibility.flashGain;
    ScopedPush.flashGuard[2] = (ScopedFlags & AccessibilityFlashGuard) != 0 ? 1.0f : 0.0f;
    ScopedPush.flashGuard[3] = 0.0f;

    VkRenderPassBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBeginInfo.renderPass = InRenderPass;
    ScopedBeginInfo.framebuffer = InFramebuffer;
    ScopedBeginInfo.renderArea.extent = {InExtent.width, InExtent.height};
    VkClearValue ScopedClearValues[2]{};
    ScopedClearValues[1].depthStencil.depth = 1.0f;
    ScopedClearValues[1].depthStencil.stencil = 0;
    ScopedBeginInfo.clearValueCount = InHasDepth ? 2 : 1;
    ScopedBeginInfo.pClearValues = ScopedClearValues;
    ScopedApi.vkCmdBeginRenderPass(InCommandBuffer, &ScopedBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, InPipeline);
    VkViewport ScopedViewport{};
    ScopedViewport.width = static_cast<float>(InExtent.width);
    ScopedViewport.height = static_cast<float>(InExtent.height);
    ScopedViewport.minDepth = 0.0f;
    ScopedViewport.maxDepth = 1.0f;
    ScopedApi.vkCmdSetViewport(InCommandBuffer, 0, 1, &ScopedViewport);
    VkRect2D ScopedScissor{};
    ScopedScissor.extent = {InExtent.width, InExtent.height};
    ScopedApi.vkCmdSetScissor(InCommandBuffer, 0, 1, &ScopedScissor);
    if (InSet != VK_NULL_HANDLE)
    {
        ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MAccessibilityLayout, 0, 1,
                                          &InSet, 0, nullptr);
    }
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MAccessibilityLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                 sizeof(ScopedPush), &ScopedPush);
    ScopedApi.vkCmdDraw(InCommandBuffer, 3, 1, 0, 0);
    ScopedApi.vkCmdEndRenderPass(InCommandBuffer);
}

}
#endif
