// FemboyEngine2D - editor texture thumbnail cache.
#include "Editor/Infrastructure/TextureThumbnails.hpp"

#include <imgui_impl_vulkan.h>

namespace Fby::Editor
{

ImTextureID TextureThumbnailCache::Get(Rhi::TextureArrayHandle InArray, std::uint32_t InLayer)
{
    if (!MEnabled || !InArray.IsValid())
    {
        return ImTextureID_Invalid;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MEntries[ScopedI].ArrayId == InArray.id && MEntries[ScopedI].Layer == InLayer)
        {
            return MEntries[ScopedI].Texture;
        }
    }
    if (MRenderer == nullptr || MCount >= MLimit)
    {
        return ImTextureID_Invalid;
    }
    Rhi::NativeTextureView ScopedView;
    if (!MRenderer->TextureArrayLayerView(InArray, InLayer, ScopedView) || ScopedView.view == nullptr)
    {
        return ImTextureID_Invalid;
    }
    const VkDescriptorSet ScopedDescriptor =
        ImGui_ImplVulkan_AddTexture(static_cast<VkImageView>(ScopedView.view), static_cast<VkImageLayout>(ScopedView.layout));
    if (ScopedDescriptor == VK_NULL_HANDLE)
    {
        return ImTextureID_Invalid;
    }
    Entry ScopedEntry;
    ScopedEntry.ArrayId = InArray.id;
    ScopedEntry.Layer = InLayer;
    ScopedEntry.Texture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(ScopedDescriptor));
    MEntries[MCount++] = ScopedEntry;
    return ScopedEntry.Texture;
}

void TextureThumbnailCache::Clear() noexcept
{
    // Clear() is reachable at runtime (settings change); make sure no GPU frame
    // still references the descriptor sets before ImGui frees them.
    if (MCount > 0 && MRenderer != nullptr)
    {
        MRenderer->WaitIdle();
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MEntries[ScopedI].Texture != ImTextureID_Invalid)
        {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(MEntries[ScopedI].Texture));
        }
    }
    MEntries = {};
    MCount = 0;
}

void TextureThumbnailCache::SetEnabled(bool InEnabled) noexcept
{
    MEnabled = InEnabled;
    if (!MEnabled)
    {
        Clear();
    }
}

void TextureThumbnailCache::SetLimit(std::uint32_t InLimit) noexcept
{
    const std::uint32_t ScopedLimit = std::min(InLimit, kMaxEntries);
    if (ScopedLimit < MCount)
    {
        Clear();
    }
    MLimit = ScopedLimit;
}
}
