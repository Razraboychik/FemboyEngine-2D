// FemboyEngine2D - editor texture thumbnails from texture-array layers.
#pragma once

#include "Engine/RHI/RHI.hpp"

#include <imgui.h>

#include <array>
#include <cstdint>

namespace Fby::Editor
{

// Caches one ImGui texture descriptor per (texture array, layer). The actual
// 2D image view is owned by the RHI backend; this class only wraps it in an
// ImGui descriptor so panels can draw real asset previews.
class TextureThumbnailCache
{
public:
    static constexpr std::uint32_t kMaxEntries = 256;

    TextureThumbnailCache() = default;
    TextureThumbnailCache(const TextureThumbnailCache&) = delete;
    TextureThumbnailCache& operator=(const TextureThumbnailCache&) = delete;

    void Initialize(Rhi::RHI* InRenderer) noexcept
    {
        MRenderer = InRenderer;
    }

    // Drops the cache without touching ImGui: call it before ImGui shuts down.
    void Clear() noexcept
    {
        MEntries = {};
        MCount = 0;
    }

    // Returns ImTextureID_Invalid when the backend has no layer view (Metal,
    // null RHI) or the descriptor could not be created.
    [[nodiscard]] ImTextureID Get(Rhi::TextureArrayHandle InArray, std::uint32_t InLayer);

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return MCount == 0;
    }

private:
    struct Entry
    {
        std::uint32_t ArrayId = 0xFFFFFFFFu;
        std::uint32_t Layer = 0;
        ImTextureID Texture = ImTextureID_Invalid;
    };

    std::array<Entry, kMaxEntries> MEntries{};
    std::uint32_t MCount = 0;
    Rhi::RHI* MRenderer = nullptr;
};
}
