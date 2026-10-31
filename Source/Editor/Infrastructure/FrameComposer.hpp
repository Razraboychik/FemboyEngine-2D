// FemboyEngine2D - one world's frame packet composition.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/RHI/Sort.hpp"
#include "Engine/RHI/ViewportPresenter.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace Fby
{
struct SdfField;
}

namespace Fby::Editor
{

// Extracts one ECS world into the single FramePacket shape the RHI accepts:
// sprites and tilemap quads are key-sorted into batches, skinned and cloth
// meshes go through the mesh path. The composer owns the arrays the packet
// points into, so a packet stays valid until the next BuildFramePacket call on
// the same composer. The editor keeps one composer per viewport.
class FrameComposer
{
public:
    // `InExtraMeshItems` (cloth) and `InTileQuads` are copied into the
    // composer's storage and appended after the registry's own sprites.
    void BuildFramePacket(Ecs::Registry& InRegistry, const Rhi::CameraData& InCamera, Color32 InClear,
                          std::uint32_t InTextureArray,
                          std::span<const Rhi::MeshRenderItem> InExtraMeshItems = {},
                          std::span<const TileQuad> InTileQuads = {},
                          const Rhi::PostFxSettings& InPostFx = Rhi::PostFxDisabled());

    [[nodiscard]] const Rhi::FramePacket& Packet() const noexcept
    {
        return MPacket;
    }

    // GPU SDF shadows travel with the scene packet so the swapchain and the
    // offscreen viewport paths share one description.
    void SetSdfLighting(const Rhi::SdfLightingFrame& InFrame) noexcept
    {
        MPacket.sdf = InFrame;
    }

    void SetVirtualTexture(Rhi::VirtualTextureHandle InHandle, const Vector4D& InWorldParams,
                           const Vector4D& InLayout) noexcept
    {
        MPacket.virtualTexture = InHandle;
        MPacket.virtualTextureParams = InWorldParams;
        MPacket.virtualTextureLayout = InLayout;
    }

    void SetVirtualTextureDebugMode(std::uint32_t InMode) noexcept
    {
        MPacket.virtualTextureDebugMode = InMode;
    }

    // Selects the viewport diagnostic. The presenter recomputes its tint table
    // inside BuildFramePacket, so a mode change needs no extra pass.
    void SetViewportMode(Rhi::ViewportMode InMode) noexcept
    {
        MViewportPresenter.SetMode(InMode);
    }
    [[nodiscard]] Rhi::ViewportMode ViewportMode() const noexcept
    {
        return MViewportPresenter.Mode();
    }
    [[nodiscard]] const Rhi::ViewportPresenterStats& ViewportStats() const noexcept
    {
        return MViewportPresenter.Stats();
    }

    // The SDF visualizer samples this field. The pointer is non-owning and must
    // outlive BuildFramePacket; nullptr disables the distance tint.
    void SetSdfField(const SdfField* InField) noexcept
    {
        MSdfField = InField;
    }

private:
    std::vector<Rhi::RenderItem> MItems;
    std::vector<Rhi::RenderItem> MSorted;
    std::vector<Rhi::RenderKey> MKeys;
    std::vector<Rhi::RenderKey> MSortedKeys;
    std::vector<std::uint32_t> MOrder;
    std::vector<Rhi::DrawBatch> MBatches;
    std::vector<Rhi::MeshRenderItem> MMeshItems;
    std::vector<Rhi::MeshVertex> MMeshVertices;
    std::vector<Mat4> MMeshPalette;
    Rhi::KeySorter MSorter;
    Rhi::FramePacket MPacket{};
    Rhi::ViewportPresenter MViewportPresenter{};
    const SdfField* MSdfField = nullptr;
    std::uint32_t MCount = 0;
};
}
