// FemboyEngine2D - editor texture thumbnail cache.
#include "Editor/Infrastructure/TextureThumbnails.hpp"

#include <imgui_impl_vulkan.h>

namespace Fby::Editor
{

ImTextureID TextureThumbnailCache::Get(Rhi::TextureArrayHandle InArray, std::uint32_t InLayer)
{
    if (!InArray.IsValid())
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
    if (MRenderer == nullptr || MCount >= kMaxEntries)
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
}
