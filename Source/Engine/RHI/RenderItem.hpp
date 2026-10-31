// FemboyEngine2D - the render item.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/RHI/Mesh.hpp"
#include "Engine/RHI/PostProcess.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Rhi
{

// Per-item switches. Anything that would otherwise need a second pipeline lives
enum class RenderFlags : std::uint32_t
{
    None = 0,
    FlipX = 1u << 0,
    FlipY = 1u << 1,
    // Blending: the default is straight alpha. Additive and premultiplied are
    Additive = 1u << 2,
    Premultiplied = 1u << 3,
    // Snap the quad to whole pixels after the camera transform. Pixel-art
    PixelSnap = 1u << 4,
    // The item samples the bound virtual texture instead of its atlas layer.
    VirtualTexture = 1u << 5,
    // The atlas texel is a signed distance field instead of alpha coverage.
    SdfText = 1u << 6,
    // SDF-only rings. Bits 16..23 carry the normalized width/radius.
    SdfOutline = 1u << 7,
    SdfGlow = 1u << 8,
};

// Shader-facing bit for RenderFlags::VirtualTexture (quad.vert/frag).
inline constexpr std::uint32_t kRenderFlagVirtualTexture = 1u << 5;
// Shader-facing bit for RenderFlags::SdfText (quad.frag/metal sprite path).
inline constexpr std::uint32_t kRenderFlagSdfText = 1u << 6;
inline constexpr std::uint32_t kRenderFlagSdfOutline = 1u << 7;
inline constexpr std::uint32_t kRenderFlagSdfGlow = 1u << 8;
inline constexpr std::uint32_t kSdfEffectWidthShift = 16u;

[[nodiscard]] constexpr std::uint32_t EncodeSdfEffectWidth(float InNormalizedWidth) noexcept
{
    const float ScopedWidth = InNormalizedWidth < 0.0f ? 0.0f : (InNormalizedWidth > 1.0f ? 1.0f : InNormalizedWidth);
    return static_cast<std::uint32_t>(ScopedWidth * 255.0f + 0.5f) << kSdfEffectWidthShift;
}

[[nodiscard]] constexpr std::uint32_t operator|(RenderFlags InA, RenderFlags InB) noexcept
{
    return static_cast<std::uint32_t>(InA) | static_cast<std::uint32_t>(InB);
}
[[nodiscard]] constexpr std::uint32_t operator|(std::uint32_t InA, RenderFlags InB) noexcept
{
    return InA | static_cast<std::uint32_t>(InB);
}

struct RenderItem
{
    // 0  world-space centre
    Vector2D position{};
    // 8  world-space extent
    Vector2D size{};
    // 16 normalized source rect
    Vector2D uvMin{0.0f, 0.0f};
    // 24
    Vector2D uvMax{1.0f, 1.0f};
    // 32 texture-array slice
    std::uint32_t layer = 0;
    // 36 packed RGBA8
    std::uint32_t color = 0xFFFFFFFFu;
    // 40 radians
    float rotation = 0.0f;
    // 44 RenderFlags mask
    std::uint32_t flags = 0;

    [[nodiscard]] static constexpr RenderItem Quad(Vector2D InCenter, Vector2D InSize,
                                                   std::uint32_t InColor = 0xFFFFFFFFu) noexcept
    {
        RenderItem ScopedItem;
        ScopedItem.position = InCenter;
        ScopedItem.size = InSize;
        ScopedItem.color = InColor;
        return ScopedItem;
    }

    [[nodiscard]] constexpr RenderItem WithLayer(std::uint32_t InValue) const noexcept
    {
        RenderItem ScopedCopy = *this;
        ScopedCopy.layer = InValue;
        return ScopedCopy;
    }
    [[nodiscard]] constexpr RenderItem WithColor(Color32 InValue) const noexcept
    {
        RenderItem ScopedCopy = *this;
        ScopedCopy.color = InValue.rgba;
        return ScopedCopy;
    }
    [[nodiscard]] constexpr RenderItem WithRotation(float InRadians) const noexcept
    {
        RenderItem ScopedCopy = *this;
        ScopedCopy.rotation = InRadians;
        return ScopedCopy;
    }
    [[nodiscard]] constexpr RenderItem WithFlip(bool InFlipX, bool InFlipY) const noexcept
    {
        RenderItem ScopedCopy = *this;
        if (InFlipX)
        {
            ScopedCopy.flags |= static_cast<std::uint32_t>(RenderFlags::FlipX);
        }
        if (InFlipY)
        {
            ScopedCopy.flags |= static_cast<std::uint32_t>(RenderFlags::FlipY);
        }
        return ScopedCopy;
    }
};

static_assert(std::is_trivially_copyable_v<RenderItem>, "RenderItem must be memcpy-able");
static_assert(std::is_standard_layout_v<RenderItem>, "RenderItem must have a stable GPU layout");
static_assert(sizeof(RenderItem) == 48, "RenderItem is fixed at 48 bytes so the GPU layout never drifts");
static_assert(alignof(RenderItem) == 4, "RenderItem is tightly packed; the buffer is 16-byte aligned instead");
static_assert(offsetof(RenderItem, position) == 0);
static_assert(offsetof(RenderItem, size) == 8);
static_assert(offsetof(RenderItem, uvMin) == 16);
static_assert(offsetof(RenderItem, uvMax) == 24);
static_assert(offsetof(RenderItem, layer) == 32);
static_assert(offsetof(RenderItem, color) == 36);
static_assert(offsetof(RenderItem, rotation) == 40);
static_assert(offsetof(RenderItem, flags) == 44);

// Depth key: layer | material bucket | depth | sequence, most significant first.
using RenderKey = std::uint64_t;

// The material bucket indexes the frame's material pipeline table, so every item
// with the same material lands in one contiguous run and therefore in one batch.
inline constexpr std::uint32_t kLayerBits = 16;
inline constexpr std::uint32_t kMaterialBits = 8;
inline constexpr std::uint32_t kDepthBits = 32;
inline constexpr std::uint32_t kSequenceBits = 8;
inline constexpr std::uint64_t kDepthScale = (1ull << kDepthBits) - 1ull;
inline constexpr std::uint32_t kMaxMaterialBuckets = 1u << kMaterialBits;
inline constexpr std::uint32_t kMaterialKeyShift = kDepthBits + kSequenceBits;
inline constexpr std::uint32_t kLayerKeyShift = kMaterialKeyShift + kMaterialBits;

[[nodiscard]] constexpr RenderKey MakeMaterialRenderKey(std::uint8_t InMaterialBucket, std::uint16_t InLayer,
                                                        float InDepth, std::uint8_t InSequence = 0) noexcept
{
    // Map [-32768, 32767] world depth to [0, 2^32): a monotonic, branch-free
    const float ScopedClamped = InDepth < -32768.0f ? -32768.0f : (InDepth > 32767.0f ? 32767.0f : InDepth);
    const std::uint64_t ScopedDepthBits = static_cast<std::uint64_t>(ScopedClamped + 32768.0f) << kSequenceBits;
    return (static_cast<std::uint64_t>(InLayer) << kLayerKeyShift) |
           (static_cast<std::uint64_t>(InMaterialBucket) << kMaterialKeyShift) | (ScopedDepthBits & kDepthScale) |
           InSequence;
}

// The engine shader is material bucket 0, so an item without a material keeps
// exactly the order it had before the material bucket existed.
[[nodiscard]] constexpr RenderKey MakeRenderKey(std::uint16_t InLayer, float InDepth,
                                                std::uint8_t InSequence = 0) noexcept
{
    return MakeMaterialRenderKey(0, InLayer, InDepth, InSequence);
}

static_assert(kLayerBits + kMaterialBits + kDepthBits + kSequenceBits == 64);

// A draw call: a contiguous, sorted range of items that share one pipeline.
struct DrawBatch
{
    std::uint32_t firstItem = 0;
    std::uint32_t itemCount = 0;
    // Rhi::ShaderPipelineHandle::id (0 = the quad pipeline)
    std::uint32_t pipeline = 0;
    // Rhi::TextureArrayHandle::id, 0 = inherit FramePacket::textureArray
    std::uint32_t textureArray = 0;
};

static_assert(std::is_trivially_copyable_v<DrawBatch>, "DrawBatch must be memcpy-able into a frame ring");
static_assert(std::is_standard_layout_v<DrawBatch>, "DrawBatch must have a stable layout");
static_assert(sizeof(DrawBatch) == 16, "DrawBatch stays 16 bytes: 4 batches per 64-byte cache line");
static_assert(alignof(DrawBatch) == 4);
static_assert(offsetof(DrawBatch, firstItem) == 0);
static_assert(offsetof(DrawBatch, itemCount) == 4);
static_assert(offsetof(DrawBatch, pipeline) == 8);
static_assert(offsetof(DrawBatch, textureArray) == 12);

// A contiguous run of changed instance data. `first` indexes the sorted item
struct DirtyRange
{
    std::uint32_t first = 0;
    std::uint32_t count = 0;

    [[nodiscard]] constexpr std::uint32_t ByteOffset() const noexcept
    {
        return first * static_cast<std::uint32_t>(sizeof(RenderItem));
    }
    [[nodiscard]] constexpr std::uint32_t ByteCount() const noexcept
    {
        return count * static_cast<std::uint32_t>(sizeof(RenderItem));
    }
};

static_assert(std::is_trivially_copyable_v<DirtyRange>);
static_assert(sizeof(DirtyRange) == 8);

// Camera state for one frame: world-space centre, drawable size in pixels and
struct CameraData
{
    Vector2D center{};
    Vector2D viewport{1920.0f, 1080.0f};
    float zoom = 1.0f;
    float padding = 0.0f;
};

// Accessibility frame policy
enum FrameAccessibilityFlags : std::uint32_t
{
    AccessibilityNone = 0,
    // Skip all game draw + compute work
    AccessibilityBlind = 1u << 0,
    // Audio is muted, subtitles are live
    AccessibilityDeaf = 1u << 1,
    // Clamp luminance delta in composite
    AccessibilityFlashGuard = 1u << 2,
    // Apply a CVD correction matrix
    AccessibilityColorVision = 1u << 3,
};

struct FrameAccessibility
{
    // FrameAccessibilityFlags
    std::uint32_t flags = 0;
    // 0 none, 1 protan, 2 deutan, 3 tritan
    std::uint32_t colorVision = 0;
    // Max relative luminance delta / frame
    float flashThreshold = 0.20f;
    // Residual gain applied above threshold
    float flashGain = 0.85f;
    // 0..1 blend towards the corrected image
    float colorVisionStrength = 1.0f;
    float reserved = 0.0f;
};

// Virtual-texture GPU resource handle. It lives here because FramePacket carries
// it; RHI.hpp owns the resource creation API.
struct VirtualTextureHandle
{
    static constexpr std::uint32_t kInvalid32 = 0xFFFFFFFFu;

    std::uint32_t id = kInvalid32;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return id != kInvalid32;
    }
};

static_assert(std::is_trivially_copyable_v<VirtualTextureHandle>);

static_assert(std::is_trivially_copyable_v<FrameAccessibility>);
static_assert(std::is_standard_layout_v<FrameAccessibility>);
static_assert(sizeof(FrameAccessibility) == 24);

// Per-pixel SDF shadowing on the GPU. The static field travels as a texture
// (uploaded only when `fieldVersion` changes); moving casters stay in the UBO and
// are re-uploaded every frame, so dynamic shadows need no field rebake.
inline constexpr std::uint32_t kMaxSdfGpuOccluders = 16;

struct SdfLightingOccluder
{
    // Circle: x, y, radius; box: x, y, unused.
    Vector4D Shape{};
    // x, y half extents; z = 1 for a box, 0 for a circle.
    Vector4D Extents{};
};

struct SdfLightingFrame
{
    // Non-owning view of a width x height distance field (negative = solid).
    const float* field = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Vector2D origin{};
    Vector2D cellSize{1.0f, 1.0f};
    // Direction towards the sun/light, normalized by the backend.
    Vector2D sunDirection{0.25f, -1.0f};
    float softness = 24.0f;
    float ambient = 0.30f;
    float occlusionRadius = 32.0f;
    float occlusionStrength = 0.30f;
    std::uint32_t steps = 16;
    std::uint32_t occluderCount = 0;
    // Backends re-upload the field texture only when this changes.
    std::uint64_t fieldVersion = 0;
    bool enabled = false;
    std::array<SdfLightingOccluder, kMaxSdfGpuOccluders> occluders{};
};

static_assert(std::is_trivially_copyable_v<SdfLightingFrame>);

// The only frame description the RHI accepts. Every span points into storage
struct FramePacket
{
    std::span<const RenderItem> items;
    // Same length as items, ascending
    std::span<const RenderKey> keys;
    std::span<const DrawBatch> batches;
    CameraData camera{};
    Color32 clear = Color32::Black();
    // Texture-array handle id (Rhi::TextureArrayHandle::id). The sentinel keeps
    std::uint32_t textureArray = 0xFFFFFFFFu;

    // Recorded after sprites. Static items use a MeshHandle and upload only one
    std::span<const MeshRenderItem> meshItems{};
    std::span<const Mat4> boneMatrices{};

    // Optional full 2.5D view-projection for the mesh path. When
    Mat4 meshViewProjection = Mat4::Identity();
    bool meshUseViewProjection = false;
    // Seconds since engine start. Feeds the material push-constant block
    float timeSeconds = 0.0f;
    bool meshPadding[3] = {};
    FrameAccessibility accessibility{};

    // Optional incremental upload contract. A producer that owns a persistent
    std::span<const DirtyRange> instanceRanges{};
    bool instanceRangesAreDeltas = false;

    // GPU SDF shadows. Backends without a shadow path ignore it.
    SdfLightingFrame sdf{};

    // Virtual texture bound for items with RenderFlags::VirtualTexture.
    VirtualTextureHandle virtualTexture{};
    // (worldTileSize, tableOriginX, tableOriginY, enabled)
    Vector4D virtualTextureParams{};
    // (tileSize, pageSize, tableSizeX, tableSizeY)
    Vector4D virtualTextureLayout{};
    // 0 = sample pages, 1 = tint by page id, 2 = tint by slot.
    std::uint32_t virtualTextureDebugMode = 0;

    // Final post-process policy for this viewport. `enabled` is explicit so a
    // backend can skip the composite without inspecting the flag bits.
    PostFxSettings postFx{};
    bool postFxEnabled = false;
};
}
