// FemboyEngine2D - PIE client presentation helpers.
#include "Editor/Infrastructure/PiePresentation.hpp"

namespace Fby::Editor
{

bool BuildPieClientSdfFrame(Ecs::Registry& InWorld, const SdfField* InField, const SdfAtlasImage& InAtlas,
                            Vector2D InCenter, float InZoom, Rhi::Extent2D InViewport,
                            Vector4D* InPreviousCamera, Rhi::SdfLightingFrame* OutFrame) noexcept
{
    if (OutFrame == nullptr)
    {
        return false;
    }
    Rhi::SdfLightingFrame ScopedFrame;
    if (!BuildSdfLightingFrame(InWorld, InField, &ScopedFrame))
    {
        return false;
    }
    ScopedFrame.camera = Vector4D{InCenter.X, InCenter.Y, InZoom, 0.0f};
    ScopedFrame.viewport = Vector4D{static_cast<float>(InViewport.width), static_cast<float>(InViewport.height),
                                    0.0f, 0.0f};
    ScopedFrame.temporal = true;
    ScopedFrame.previousCamera = InPreviousCamera != nullptr ? *InPreviousCamera : Vector4D{};
    if (InPreviousCamera != nullptr)
    {
        *InPreviousCamera = ScopedFrame.camera;
    }
    if (!InAtlas.Table.empty() && !InAtlas.Texels.empty())
    {
        ScopedFrame.brickTable = reinterpret_cast<const std::byte*>(InAtlas.Table.data());
        ScopedFrame.brickCount = static_cast<std::uint32_t>(InAtlas.Table.size());
        ScopedFrame.atlasTexels = InAtlas.Texels.data();
        ScopedFrame.atlasWidth = InAtlas.Width;
        ScopedFrame.atlasHeight = InAtlas.Height;
        ScopedFrame.atlasColumns = InAtlas.Columns;
    }
    *OutFrame = ScopedFrame;
    return true;
}

}
