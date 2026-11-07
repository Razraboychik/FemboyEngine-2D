// FemboyEngine2D - one world's frame packet composition.
#pragma once

#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/RenderEcs.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/RHI/Sort.hpp"
#include "Engine/RHI/ViewportPresenter.hpp"

#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace Fby
{
struct SdfField;
class SdfGiVolume;
}

namespace Fby::Rhi
{

// Previous-frame transform of one sprite. This is derived render state that feeds
// motion vectors for temporal upscalers and frame generation; it is never
// serialized and never leaves the registry.
struct MotionHistory2D
{
    Vector2D Position{};
    Vector2D Size{};
    float Rotation = 0.0f;
    // History generation. A record from an older generation is treated as missing.
    std::uint32_t Epoch = 0;
    std::uint32_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<MotionHistory2D>, "MotionHistory2D must stay trivially copyable");
static_assert(sizeof(MotionHistory2D) == 28, "MotionHistory2D must stay 28 bytes");

// CPU-side extract/sort/gather cost of the last composed frame. The timings are
// wall-clock and intended for the editor diagnostics panel, not for budgeting.
struct FrameComposerStats
{
    double ExtractMilliseconds = 0.0;
    double SortMilliseconds = 0.0;
    double GatherMilliseconds = 0.0;
    std::uint32_t CulledSprites = 0;
    std::uint32_t PackedLights = 0;
    std::uint32_t DepthBatches = 0;
    std::uint32_t DepthItems = 0;
    std::uint32_t RenderEcsSprites = 0;
    std::uint32_t RenderEcsChunks = 0;
    std::uint32_t OitItems = 0;
};

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

    // Reserves the compose buffers once so a growing scene does not allocate on
    // the first heavy frame. `InMaxItems` is the frame contract's item cap.
    void Reserve(std::uint32_t InMaxItems);

    // The packed render ECS mirror is a diagnostic view; a shipping viewport can
    // turn it off and skip the per-sprite pack and trigonometry.
    void SetRenderEcsMirror(bool InEnabled) noexcept
    {
        MRenderEcsMirror = InEnabled;
    }

    [[nodiscard]] const Rhi::FramePacket& Packet() const noexcept
    {
        return MPacket;
    }
    [[nodiscard]] const Rhi::FramePacket& OitPacket() const noexcept
    {
        return MOitPacket;
    }
    [[nodiscard]] const FrameComposerStats& Stats() const noexcept
    {
        return MStats;
    }
    [[nodiscard]] const Render::RenderEcs& RenderEcsState() const noexcept
    {
        return MRenderEcs;
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
    // Bucket -> material pipeline id. Bucket 0 is the engine quad pipeline;
    // the table is owned by the caller and must outlive the frame packet.
    void SetMaterialPipelines(std::span<const std::uint32_t> InPipelines) noexcept
    {
        MMaterialPipelines = InPipelines;
        MHasLastFrame = false;
    }

    void SetGpuLighting(bool InEnabled) noexcept
    {
        MGpuLighting = InEnabled;
        MHasLastFrame = false;
    }

    void SetTemporalState(bool InEnabled) noexcept
    {
        if (MTemporalEnabled != InEnabled)
        {
            MTemporalEnabled = InEnabled;
            MResetTemporalHistory = true;
            MHasLastCamera = false;
            ++MMotionEpoch;
        }
    }

    void ResetTemporalHistory() noexcept
    {
        MResetTemporalHistory = true;
        MHasLastCamera = false;
        ++MMotionEpoch;
    }

    void SetOitMode(std::uint8_t InMode) noexcept
    {
        if (MOitMode != InMode)
        {
            MOitMode = InMode;
            MHasLastFrame = false;
        }
    }

    void SetOpaqueFrontToBack(bool InEnabled) noexcept
    {
        if (MOpaqueFrontToBack != InEnabled)
        {
            MOpaqueFrontToBack = InEnabled;
            MHasLastFrame = false;
        }
    }

    void SetOpaqueLayerModes(std::span<const Assets::TextureAlphaMode> InModes)
    {
        MOpaqueLayerModes.assign(InModes.begin(), InModes.end());
        MHasLastFrame = false;
    }

    void SetForceOpaqueCutout(bool InEnabled) noexcept
    {
        if (MForceOpaqueCutout != InEnabled)
        {
            MForceOpaqueCutout = InEnabled;
            MHasLastFrame = false;
        }
    }

    // Packs the current GI probe grid into the frame packet. The volume must
    // outlive BuildFramePacket; the composer owns the GPU-layout copy.
    void SetSdfGiVolume(const SdfGiVolume* InVolume, float InStrength = 0.35f) noexcept
    {
        MSdfGiVolume = InVolume;
        MSdfGiStrength = InStrength;
    }

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
    std::vector<Rhi::DrawBatch> MDepthBatches;
    std::vector<Rhi::MeshRenderItem> MMeshItems;
    std::vector<Rhi::MeshVertex> MMeshVertices;
    std::vector<Mat4> MMeshPalette;
    std::vector<Rhi::RenderItem> MLastItems;
    std::vector<Rhi::RenderKey> MLastKeys;
    std::vector<Rhi::RenderItem> MOitItems;
    std::vector<Rhi::RenderKey> MOitKeys;
    std::vector<Rhi::DrawBatch> MOitBatches;
    std::vector<Rhi::RenderItem> MOpaqueItems;
    std::vector<Rhi::RenderKey> MOpaqueKeys;
    std::vector<Rhi::DrawBatch> MOpaqueBatches;
    Rhi::FramePacket MOitPacket{};
    Render::RenderEcs MRenderEcs;
    std::vector<Render::GpuEntity> MRenderEcsEntities;
    std::vector<Rhi::GpuLight> MLights;
    std::vector<Rhi::GpuLight> MLastLights;
    std::uint32_t MLightVersion = 0;
    std::vector<Rhi::GpuGiProbe> MGiProbes;
    std::vector<Rhi::GpuGiProbe> MLastGiProbes;
    bool MOpaqueFrontToBack = false;
    std::uint8_t MOitMode = 0;
    bool MForceOpaqueCutout = false;
    std::vector<Assets::TextureAlphaMode> MOpaqueLayerModes;
    Rhi::GpuGiVolumeHeader MGiHeader{};
    Rhi::GpuGiVolumeHeader MLastGiHeader{};
    std::uint32_t MGiVersion = 0;
    const SdfGiVolume* MSdfGiVolume = nullptr;
    float MSdfGiStrength = 0.35f;
    std::span<const std::uint32_t> MMaterialPipelines{};
    bool MGpuLighting = false;
    bool MRenderEcsMirror = true;
    Rhi::KeySorter MSorter;
    Rhi::FramePacket MPacket{};
    Rhi::ViewportPresenter MViewportPresenter{};
    FrameComposerStats MStats{};
    const SdfField* MSdfField = nullptr;
    std::uint32_t MCount = 0;
    std::uint32_t MLastCount = 0;
    bool MHasLastFrame = false;
    bool MHasLastCamera = false;
    // History generation. Bumping it invalidates every MotionHistory2D record
    // without touching the registry.
    std::uint32_t MMotionEpoch = 1;
    std::vector<Rhi::MotionVectorItem> MMotionItems;
    bool MTemporalEnabled = false;
    bool MResetTemporalHistory = true;
    std::uint64_t MFrameIndex = 0;
    std::uint64_t MHistoryGeneration = 0;
    float MPreviousJitterX = 0.0f;
    float MPreviousJitterY = 0.0f;
    std::uint32_t MSceneVersion = 0;
    Rhi::CameraData MLastCamera{};
};
}
