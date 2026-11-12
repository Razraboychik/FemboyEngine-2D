// FemboyEngine2D - internal Vulkan backend declarations.
#pragma once

#if defined(FBY_HAS_VULKAN_HEADERS)

#include "Engine/RHI/Vulkan/VulkanRHI.hpp"

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/RHI/GpuVisibility.hpp"
#include "Engine/RHI/RenderPasses.hpp"
#include "Engine/RHI/VirtualTextureFormat.hpp"
#include "Engine/UI/Accessibility.hpp"

#include "Engine/RHI/Vulkan/VulkanLoader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "ShaderBlobs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Fby::Rhi::Impl
{
constexpr std::uint32_t kMaxFramesInFlight = 4;
constexpr std::uint32_t kQuadIndexCount = 6;
constexpr std::uint32_t kAtlasSize = 128;
constexpr std::uint32_t kAtlasLayers = 4;
constexpr std::uint32_t kMaxMeshItemsPerFrame = 4096;
// R8 distance encoding: world units per normalized texel value.
// R32F stores world-space distance directly; the shader multiply stays as a
// single neutral scale so the layout contract does not change.
constexpr float kSdfEncodeScale = 1.0f;

// Matches the push constant block in quad.vert/frag (std430: three vec4s plus
// the VT layout, each 16-byte aligned).
struct PushConstants
{
    // viewportSize.xy, zoom, padding
    float viewportAndZoom[4];
    // center.xy, padding, padding
    float centerAndPadding[4];
    // worldTileSize, tableOriginX, tableOriginY, enabled
    float vtWorld[4];
    // tileSize, pageSize, tableSizeX, tableSizeY
    float vtLayout[4];
    // Feedback image array slot (frame index % 3), padding
    float frame[4];
    // Material clock: x = seconds, yz = viewport pixels, w = reserved. The
    // sprite material domain reads this as pc.materialParams.
    float material[4];
};

static_assert(sizeof(PushConstants) == 96);

struct HiZPushConstants
{
    std::uint32_t DepthWidth = 0;
    std::uint32_t DepthHeight = 0;
    std::uint32_t TileWidth = 16;
    std::uint32_t TileHeight = 16;
};

static_assert(sizeof(HiZPushConstants) == 16);

enum FrameTimestampIndex : std::uint32_t
{
    TimestampFrameStart = 0,
    TimestampClusterStart,
    TimestampClusterEnd,
    TimestampDepthStart,
    TimestampDepthEnd,
    TimestampMainStart,
    TimestampMainEnd,
    TimestampPostFxStart,
    TimestampPostFxEnd,
    TimestampFrameEnd,
    TimestampCount
};

[[nodiscard]] constexpr bool IsSrgbFormat(VkFormat InFormat) noexcept
{
    return InFormat == VK_FORMAT_R8G8B8A8_SRGB || InFormat == VK_FORMAT_B8G8R8A8_SRGB;
}

// Matches the SdfBlock UBO declared by quad.frag. `occluders` holds pairs:
// shape (x, y, radius, 0) and extents (halfX, halfY, box, 0).
struct SdfGpuBlock
{
    // Must match SdfShadowBlock in sdf_shadow.comp and SdfBlock in quad.frag
    // (std430, 672 bytes).
    Vector4D camera{};
    Vector4D viewport{};
    Vector4D fieldBounds{};
    Vector4D sun{};
    Vector4D params{};
    Vector4D flags{};
    Vector4D history{};
    Vector4D historyCamera{};
    // World origin.xy and cell size.xy of the field grid, so the atlas path can
    // turn a world position into a cell without a second buffer.
    Vector4D fieldGrid{};
    // Brick size in cells, brick columns, brick rows, atlas columns.
    Vector4D brickMeta{};
    Vector4D occluders[kMaxSdfGpuOccluders * 2]{};
};

static_assert(sizeof(SdfGpuBlock) == 10 * 16 + kMaxSdfGpuOccluders * 2 * 16);
static_assert(sizeof(SdfGpuBlock) == 672);

// Matches mesh.vert (first 64 bytes) and the material extension consumed.
struct MeshGraphicsPush
{
    Mat4 viewProjection = Mat4::Identity();
    // Time, viewportWidth, viewportHeight, unused
    Vector4D materialParams{0.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(MeshGraphicsPush) == 80);

// Matches mesh_skinning.comp (std430, 16 bytes): one dispatch covers.
struct MeshComputePush
{
    std::uint32_t vertexCount = 0;
    // Several viewports share one frame; each pass skins its own range.
    std::uint32_t vertexOffset = 0;
    std::uint32_t pad1 = 0;
    std::uint32_t pad2 = 0;
};

static_assert(sizeof(MeshComputePush) == 16);

// The composite push block and the shared post-process constants are the same
// shader layout; keeping one type prevents the Vulkan range from drifting.
using PostFxBlock = Fby::Rhi::PostFxConstants;

static_assert(sizeof(PostFxBlock) == 304);

// One frame's streaming mesh rings.
struct MeshFrameBuffers
{
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    void* vertexMapped = nullptr;

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    void* indexMapped = nullptr;

    VkBuffer skinnedBuffer = VK_NULL_HANDLE;
    VkDeviceMemory skinnedMemory = VK_NULL_HANDLE;

    VkBuffer boneBuffer = VK_NULL_HANDLE;
    VkDeviceMemory boneMemory = VK_NULL_HANDLE;
    void* boneMapped = nullptr;

    VkBuffer skinRangeBuffer = VK_NULL_HANDLE;
    VkDeviceMemory skinRangeMemory = VK_NULL_HANDLE;
    void* skinRangeMapped = nullptr;

    VkBuffer drawDataBuffer = VK_NULL_HANDLE;
    VkDeviceMemory drawDataMemory = VK_NULL_HANDLE;
    void* drawDataMapped = nullptr;

    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indirectMemory = VK_NULL_HANDLE;
    void* indirectMapped = nullptr;

    VkDescriptorSet computeSet = VK_NULL_HANDLE;
    VkDescriptorSet graphicsSet = VK_NULL_HANDLE;
};

struct MeshDrawRecord
{
    std::uint32_t vertexBase = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t textureArray = 0xFFFFFFFFu;
    std::uint32_t layer = 0;
    std::uint32_t drawDataIndex = 0;
    std::uint32_t materialPipeline = kDefaultMaterialPipeline;
    Mat4 model{};
    bool staticMesh = false;
    bool unskinned = false;
};

// A contiguous indirect range: all commands share one index buffer, one atlas
struct MeshIndirectGroup
{
    std::uint32_t textureArray = 0xFFFFFFFFu;
    std::uint32_t layer = 0;
    std::uint32_t materialPipeline = kDefaultMaterialPipeline;
    std::uint32_t firstCommand = 0;
    std::uint32_t commandCount = 0;
};

struct MeshArenaRange
{
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

struct StaticMeshRecord
{
    std::uint32_t vertexOffset = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    bool alive = false;
};

struct TextureArray
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    std::uint32_t layers = 0;
    std::uint32_t mipLevels = 1;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Fby::Assets::TextureFormat format = Fby::Assets::TextureFormat::Rgba8;
    bool srgb = false;
    bool borrowed = false;
};

// One runtime-compiled material pipeline. A VkPipeline is tied to a render pass,
struct MaterialPipeline
{
    VkPipeline mainPass = VK_NULL_HANDLE;
    VkPipeline offscreenPass = VK_NULL_HANDLE;
    // sRGB render targets use a different attachment format, so a pipeline is
    // only compatible with the pass it was created for.
    VkPipeline srgbPass = VK_NULL_HANDLE;
    bool alive = false;
};

// An offscreen colour image with its own pass and sprite pipeline. The editor
struct RenderTarget
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline meshPipeline = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    Extent2D extent{};
    bool srgb = false;
    Fby::Rhi::RenderBlendMode blend = Fby::Rhi::RenderBlendMode::Opaque;
    bool ownsRenderPass = true;
    // Accessibility composite: the scene renders here, then one fullscreen pass
    // samples this image and writes the corrected result into `image`.
    VkImage sceneImage = VK_NULL_HANDLE;
    VkDeviceMemory sceneMemory = VK_NULL_HANDLE;
    VkImageView sceneView = VK_NULL_HANDLE;
    VkFramebuffer sceneFramebuffer = VK_NULL_HANDLE;
    VkDescriptorSet accessibilitySet = VK_NULL_HANDLE;
    // Per-pixel luminance history for the flash guard (R8, storage image).
    VkImage flashImage = VK_NULL_HANDLE;
    VkDeviceMemory flashMemory = VK_NULL_HANDLE;
    VkImageView flashView = VK_NULL_HANDLE;
};

// Push block of accessibility_composite.frag: three matrix columns plus
// (gain, strength, flags, reserved).
struct AccessibilityCompositePush
{
    float matrix[12]{};
    float safety[4]{};
    // threshold, residual gain, enabled, reserved
    float flashGuard[4]{};
};

static_assert(sizeof(AccessibilityCompositePush) == 80);

[[nodiscard]] inline std::uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& InMemoryProperties,
                                                  std::uint32_t InTypeBits, VkMemoryPropertyFlags InRequired)
{
    for (std::uint32_t ScopedI = 0; ScopedI < InMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((InTypeBits & (1u << ScopedI)) != 0 &&
            (InMemoryProperties.memoryTypes[ScopedI].propertyFlags & InRequired) == InRequired)
        {
            return ScopedI;
        }
    }
    return std::numeric_limits<std::uint32_t>::max();
}

// Pipelines that share a render pass with a depth attachment still need a
// valid depth-stencil state. Sprites keep the test disabled.
[[nodiscard]] inline VkPipelineDepthStencilStateCreateInfo MakeDepthStencilState() noexcept
{
    VkPipelineDepthStencilStateCreateInfo ScopedState{};
    ScopedState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ScopedState.depthTestEnable = VK_FALSE;
    ScopedState.depthWriteEnable = VK_FALSE;
    ScopedState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    ScopedState.depthBoundsTestEnable = VK_FALSE;
    ScopedState.stencilTestEnable = VK_FALSE;
    ScopedState.minDepthBounds = 0.0f;
    ScopedState.maxDepthBounds = 1.0f;
    return ScopedState;
}

// Cooked texel format -> Vulkan format. The runtime uploads the cooker's blocks
[[nodiscard]] inline VkFormat ToVkFormat(Fby::Assets::TextureFormat InFormat, bool InSrgb) noexcept
{
    switch (InFormat)
    {
        case Fby::Assets::TextureFormat::Rgba8:
            return InSrgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        case Fby::Assets::TextureFormat::Bc7Unorm:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case Fby::Assets::TextureFormat::Bc7Srgb:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        case Fby::Assets::TextureFormat::Astc4x4Unorm:
            return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case Fby::Assets::TextureFormat::Astc4x4Srgb:
            return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case Fby::Assets::TextureFormat::Astc6x6Unorm:
            return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case Fby::Assets::TextureFormat::Astc6x6Srgb:
            return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] inline bool IsAstcFormat(Fby::Assets::TextureFormat InFormat) noexcept
{
    return InFormat == Fby::Assets::TextureFormat::Astc4x4Unorm ||
           InFormat == Fby::Assets::TextureFormat::Astc4x4Srgb ||
           InFormat == Fby::Assets::TextureFormat::Astc6x6Unorm || InFormat == Fby::Assets::TextureFormat::Astc6x6Srgb;
}

[[nodiscard]] inline std::vector<VkExtensionProperties> QueryInstanceExtensions()
{
    const Vk::Api& ScopedApi = Vk::GetApi();
    std::uint32_t ScopedCount = 0;
    ScopedApi.vkEnumerateInstanceExtensionProperties(nullptr, &ScopedCount, nullptr);
    std::vector<VkExtensionProperties> ScopedExtensions(ScopedCount);
    if (ScopedCount > 0)
    {
        ScopedApi.vkEnumerateInstanceExtensionProperties(nullptr, &ScopedCount, ScopedExtensions.data());
    }
    return ScopedExtensions;
}

[[nodiscard]] inline bool HasExtension(const std::vector<VkExtensionProperties>& InAvailable, const char* InName)
{
    for (const VkExtensionProperties& ScopedExtension : InAvailable)
    {
        if (std::strcmp(ScopedExtension.extensionName, InName) == 0)
        {
            return true;
        }
    }
    return false;
}

// CPU atlas
[[nodiscard]] inline std::vector<std::byte> BuildAtlasPixels()
{
    constexpr std::uint32_t ScopedSize = kAtlasSize;
    std::vector<std::byte> ScopedPixels(static_cast<std::size_t>(ScopedSize) * ScopedSize * kAtlasLayers * 4);

    const auto ScopedWrite = [&ScopedPixels](std::uint32_t InLayer, std::uint32_t InX, std::uint32_t InY,
                                             std::uint8_t InR, std::uint8_t InG, std::uint8_t InB, std::uint8_t InA)
    {
        const std::size_t ScopedIndex = ((static_cast<std::size_t>(InLayer) * kAtlasSize + InY) * kAtlasSize + InX) * 4;
        ScopedPixels[ScopedIndex + 0] = static_cast<std::byte>(InR);
        ScopedPixels[ScopedIndex + 1] = static_cast<std::byte>(InG);
        ScopedPixels[ScopedIndex + 2] = static_cast<std::byte>(InB);
        ScopedPixels[ScopedIndex + 3] = static_cast<std::byte>(InA);
    };

    for (std::uint32_t ScopedY = 0; ScopedY < ScopedSize; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < ScopedSize; ++ScopedX)
        {
            ScopedWrite(0, ScopedX, ScopedY, 255, 255, 255, 255);

            const bool ScopedEven = ((ScopedX / 16) + (ScopedY / 16)) % 2 == 0;
            ScopedWrite(1, ScopedX, ScopedY, ScopedEven ? 255 : 40, ScopedEven ? 80 : 20, ScopedEven ? 140 : 60, 255);

            const float ScopedDx = static_cast<float>(ScopedX) - static_cast<float>(ScopedSize) * 0.5f;
            const float ScopedDy = static_cast<float>(ScopedY) - static_cast<float>(ScopedSize) * 0.5f;
            const float ScopedRadius =
                std::sqrt(ScopedDx * ScopedDx + ScopedDy * ScopedDy) / (static_cast<float>(ScopedSize) * 0.5f);
            const auto ScopedShade = static_cast<std::uint8_t>(std::clamp(1.0f - ScopedRadius, 0.0f, 1.0f) * 255.0f);
            ScopedWrite(2, ScopedX, ScopedY, 40, ScopedShade, 90, 255);

            if (ScopedRadius <= 0.85f)
            {
                ScopedWrite(3, ScopedX, ScopedY, 90, 160, 255, 255);
            }
            else
            {
                ScopedWrite(3, ScopedX, ScopedY, 0, 0, 0, 0);
            }
        }
    }
    return ScopedPixels;
}

// Vulkan device
// One virtual-texture resource: the page array plus the world-tile page table.
struct VirtualTextureResource
{
    VkImage pages = VK_NULL_HANDLE;
    VkDeviceMemory pagesMemory = VK_NULL_HANDLE;
    VkImageView pagesView = VK_NULL_HANDLE;
    VkSampler pagesSampler = VK_NULL_HANDLE;
    VkImage table = VK_NULL_HANDLE;
    VkDeviceMemory tableMemory = VK_NULL_HANDLE;
    VkImageView tableView = VK_NULL_HANDLE;
    VkSampler tableSampler = VK_NULL_HANDLE;
    std::uint32_t layerCount = 0;
    std::uint32_t pageSize = 0;
    std::uint32_t tileSize = 0;
    std::uint32_t pageTableSize = 0;
};

class VulkanRHI final : public RHI
{
public:
    VulkanRHI() = default;
    ~VulkanRHI() override
    {
        Shutdown();
    }

    VulkanRHI(const VulkanRHI&) = delete;
    VulkanRHI& operator=(const VulkanRHI&) = delete;

    bool Initialize(const DeviceDesc& InDesc) override;
    void Shutdown() override;
    void Resize(Extent2D InSize) override;
    [[nodiscard]] bool SetVSync(bool InEnabled) override;
    [[nodiscard]] bool VSyncEnabled() const noexcept override;
    void SetOitMode(std::uint8_t InMode) override;
    void SetUpscaleMode(std::uint8_t InMode) override;
    void WaitIdle() override;

    bool BeginFrame() override;
    void Submit(const FramePacket& InPacket) override;
    void EndFrame() override;

    TextureArrayHandle CreateTextureArray(const TextureArrayDesc& InDesc, std::span<const std::byte> InPixels) override;
    TextureArrayHandle CreateTextureArrayFromRenderTarget(RenderTargetHandle InHandle) override;
    void DestroyTextureArray(TextureArrayHandle InHandle) override;
    VirtualTextureHandle CreateVirtualTexture(const VirtualTextureResourceDesc& InDesc) override;
    std::uint32_t ReadVirtualTextureFeedback(std::span<std::uint32_t> OutTiles) override;
    void DestroyVirtualTexture(VirtualTextureHandle InHandle) override;
    bool UpdateVirtualTexturePageTable(VirtualTextureHandle InHandle, std::uint32_t InWidth, std::uint32_t InHeight,
                                       std::span<const std::uint32_t> InEntries) override;
    bool UploadVirtualTexturePage(VirtualTextureHandle InHandle, std::uint32_t InLayer, std::uint32_t InSlotX,
                                  std::uint32_t InSlotY, std::uint32_t InWidth, std::uint32_t InHeight,
                                  std::uint32_t InRowPitch, std::span<const std::byte> InPixels) override;
    bool UpdateTextureArrayLayer(TextureArrayHandle InHandle, std::uint32_t InLayer, std::uint32_t InMipLevel,
                                 std::span<const std::byte> InData) override;
    [[nodiscard]] MeshHandle CreateMesh(const MeshDesc& InDesc) override;
    void DestroyMesh(MeshHandle InHandle) override;

    [[nodiscard]] ShaderPipelineHandle CreateShaderPipeline(const ShaderPipelineDesc& InDesc) override;
    void DestroyShaderPipeline(ShaderPipelineHandle InHandle) override;
    [[nodiscard]] bool SupportsRuntimeShaderPipelines() const noexcept override
    {
        return true;
    }
    bool CaptureRenderTarget(RenderTargetHandle InHandle, std::span<std::byte> InRgba8, Extent2D* OutSize) override;

    [[nodiscard]] bool QueryUiBackend(UiBackendInfo& InInfo) const override;
    [[nodiscard]] void* GetNativeFunction(const char* InName) const override;
    [[nodiscard]] RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& InDesc) override;
    void DestroyRenderTarget(RenderTargetHandle InHandle) override;
    void PresentRenderTarget(RenderTargetHandle InHandle) override;
    [[nodiscard]] bool TransitionRenderTargetLayout(RenderTargetHandle InHandle, int InOldLayout,
                                                    int InNewLayout) override;
    [[nodiscard]] bool PresentGeneratedFrame(RenderTargetHandle InHandle) override;
    bool RenderToTarget(RenderTargetHandle InHandle, const FramePacket& InPacket) override;
    bool RecordComputePass(std::string_view InPipeline, const void* InConstants, std::uint32_t InConstantsBytes,
                           Extent2D InExtent) override;
    bool RecordGraphicsPass(std::string_view InPipeline, const void* InConstants, std::uint32_t InConstantsBytes,
                            Extent2D InExtent, std::uint32_t InInstanceCount) override;
    [[nodiscard]] bool RenderTargetTexture(RenderTargetHandle InHandle, NativeTextureView& InView) const override;
    [[nodiscard]] void* CurrentCommandBuffer() const noexcept override
    {
        return MInFrame ? MCommandBuffers[MFrameIndex] : nullptr;
    }
    [[nodiscard]] bool GetNativeDeviceHandles(NativeDeviceHandles& OutHandles) const override
    {
        OutHandles.Instance = MInstance;
        OutHandles.PhysicalDevice = MPhysical;
        OutHandles.Device = MDevice;
        OutHandles.DeviceProcAddr = reinterpret_cast<void*>(Vk::GetApi().vkGetDeviceProcAddr);
        return MDevice != VK_NULL_HANDLE;
    }
    [[nodiscard]] bool RenderTargetDepthTexture(RenderTargetHandle InHandle, NativeTextureView& InView) const override;
    [[nodiscard]] bool MainDepthTexture(NativeTextureView& InView) const override;
    [[nodiscard]] bool TextureArrayLayerView(TextureArrayHandle InHandle, std::uint32_t InLayer,
                                             NativeTextureView& InView) const override;
    void SetUiRenderer(UiRenderCallback InCallback, void* InUser) override;

    [[nodiscard]] const DeviceInfo& Info() const noexcept override
    {
        return MInfo;
    }
    [[nodiscard]] FrameStats Stats() const noexcept override
    {
        return MStats;
    }
    [[nodiscard]] Backend GetBackend() const noexcept override
    {
        return Backend::Vulkan;
    }
    [[nodiscard]] std::size_t TransientCapacity() const noexcept override
    {
        return MTransientCapacity;
    }

private:
    bool CreateInstance(const DeviceDesc& InDesc);
    bool PickPhysicalDevice();
    bool CreateDevice();
    bool CreateSwapchain(Extent2D InSize);
    bool CreateRenderPass();
    bool CreateAccessibilityCompositeResources();
    void DestroyAccessibilityCompositeResources() noexcept;
    [[nodiscard]] bool FlashGuardWanted(const FrameAccessibility& InAccessibility) const noexcept;
    void RecordFlashGuard(VkCommandBuffer InCommandBuffer, VkImage InColorImage, VkDescriptorSet InSet,
                          Extent2D InExtent, VkImageLayout InFromLayout,
                          const FrameAccessibility& InAccessibility) noexcept;
    bool CreateAccessibilitySwapchainResources();
    bool CreatePostFxResources();
    void RecordPostFxComposite(VkCommandBuffer InCommandBuffer, const PostFxBlock& InBlock, const Extent2D& InExtent);
    void RecordPostFxComposite(VkCommandBuffer InCommandBuffer, const PostFxSettings& InSettings,
                               const Extent2D& InExtent);
    void DestroyAccessibilitySwapchainResources() noexcept;
    [[nodiscard]] bool AccessibilityCompositeWanted(const FramePacket& InPacket) const noexcept;
    void RecordAccessibilityComposite(VkCommandBuffer InCommandBuffer, VkRenderPass InRenderPass,
                                      VkFramebuffer InFramebuffer, VkPipeline InPipeline, VkDescriptorSet InSet,
                                      Extent2D InExtent, const FrameAccessibility& InAccessibility,
                                      bool InHasDepth) noexcept;
    bool CreateFramebuffers();
    bool CreateDepthPrepassResources();
    bool CreateDepthPrepassFramebuffers();
    void DestroyDepthPrepassFramebuffers() noexcept;
    bool CreateCullResources();
    void DestroyCullResources() noexcept;
    [[nodiscard]] VkDeviceSize UploadPrepassInstances(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket);
    void UpdateCullDepth(VkDescriptorSet InSet, VkImageView InView);
    void RecordOpaqueCull(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket, VkDeviceSize InSourceBase);
    void RecordMainCull(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket, VkDeviceSize InSourceBase);
    bool CreateDepthImage(Extent2D InExtent, VkImage* OutImage, VkDeviceMemory* OutMemory, VkImageView* OutView);
    bool CreateHiZResources();
    void DestroyHiZResources() noexcept;
    void RecordHiZ(VkCommandBuffer InCommandBuffer);
    void DestroyDepthImage(VkImage* InImage, VkDeviceMemory* InMemory, VkImageView* InView) noexcept;
    bool CreateCommands();
    bool CreateSyncObjects();
    bool CreateDescriptors();
    bool CreatePipeline();
    bool CreateOitResolveResources();
    bool CreateUpscaleResources();
    bool CreateHudResources();
    bool CreateMotionResources();
    bool CreateTemporalResources();
    bool UploadMotionVectors(std::span<const MotionVectorItem> InItems) override;
    bool CopyColorImage(RenderTargetHandle InSource, RenderTargetHandle InDestination) override;
    bool CopyDepth(RenderTargetHandle InSource, RenderTargetHandle InDestination) override;
    bool TemporalResolve(RenderTargetHandle InCurrent, RenderTargetHandle InHistory, RenderTargetHandle InMotion,
                         RenderTargetHandle InDepth, RenderTargetHandle InHistoryDepth, RenderTargetHandle InOutput,
                         float InHistoryWeight, float InMotionScale, bool InResetHistory) override;
    bool ClearMotionVectors(RenderTargetHandle InTarget) override;
    bool RenderMotionVectors(RenderTargetHandle InTarget, std::uint32_t InCount, Vector2D InCameraCenter,
                             Vector2D InPreviousCameraCenter, float InCameraZoom, float InPreviousCameraZoom,
                             Vector2D InJitter, Vector2D InPreviousJitter, Vector2D InViewport) override;
    bool RenderHudTarget() override;
    bool CompositeHud() override;
    void SetHudUpscale(bool InNearest) override
    {
        MHudNearest = InNearest;
    }
    void SetUiVisible(bool InVisible) override
    {
        MUiVisible = InVisible;
    }
    bool RcasSharpen(RenderTargetHandle InSource, RenderTargetHandle InDestination, float InSharpness) override;
    bool UpscaleRenderTarget(RenderTargetHandle InSource, RenderTargetHandle InDestination,
                             SpatialUpscaleMode InMode) override;
    bool DrawOverlay(const FramePacket& InPacket) override;
    void SetHudTarget(RenderTargetHandle InHandle) override
    {
        MHudTargetId = InHandle.id;
    }
    bool CreateGeometry();
    bool CreateInstanceRing();
    bool CreateBuiltInAtlas();
    bool CreateMeshResources();
    bool CreateMeshDescriptors();
    bool CreateMeshPipelines();
    bool CreateMeshPipelineForPass(VkRenderPass InRenderPass, VkPipeline* OutPipeline);
    bool CreateGraphicsPipelineForPass(VkRenderPass InRenderPass, VkPipelineLayout InLayout, const void* InVertexSpirv,
                                       std::size_t InVertexBytes, const void* InFragmentSpirv,
                                       std::size_t InFragmentBytes, VkPipeline* OutPipeline);
    bool CreateOffscreenRenderPass();
    bool CreateSdfShadowResources();
    bool CreateSdfComputeResources();
    bool CreateGiResources();
    void DestroyGiResources() noexcept;
    bool CreateClusterResources();
    bool CreateClusterFallback();
    void DestroyClusterFallback() noexcept;
    void DestroyClusterResources() noexcept;
    void WriteTimestamp(VkCommandBuffer InCommandBuffer, std::uint32_t InIndex, VkPipelineStageFlagBits InStage);
    void RecordOitResolve(VkCommandBuffer InCommandBuffer, VkRenderPass InRenderPass, const FramePacket& InPacket);
    void RecordClusters(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket);
    bool EnsureSdfMask(Extent2D InMaskExtent);
    void UpdateSdfComputeSets();
    void DestroySdfMaskResources() noexcept;
    void DestroySdfShadowResources() noexcept;
    void WriteSdfDescriptors(VkDescriptorSet InSet) const;
    [[nodiscard]] std::uint32_t ApplySdfLighting(const SdfLightingFrame& InFrame);
    void UploadSdfField();
    void UploadSdfAtlas();
    bool CreateUploadArena(std::uint32_t InBytes);
    void DestroyMaterialPipelines() noexcept;
    [[nodiscard]] VkPipeline ResolveMaterialPipeline(std::uint32_t InId, bool InOffscreen,
                                                     bool InSrgb = false) const noexcept;
    bool CreateStaticMeshArena();
    void DestroyStaticMeshArena() noexcept;
    bool AllocateStaticRange(std::vector<MeshArenaRange>& InFreeList, std::uint32_t InCount, std::uint32_t* InOffset);
    void FreeStaticRange(std::vector<MeshArenaRange>& InFreeList, std::uint32_t InOffset, std::uint32_t InCount);
    void DestroyMeshResources() noexcept;
    void RecordMeshSkinning(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket);
    void DrawMeshes(VkCommandBuffer InCommandBuffer, VkPipeline InPipeline, const FramePacket& InPacket,
                    bool InOffscreen, bool InSrgb, std::uint32_t InSdfSlot);

    [[nodiscard]] bool CreateBuffer(VkDeviceSize InSize, VkBufferUsageFlags InUsage, VkMemoryPropertyFlags InProperties,
                                    VkBuffer* InBuffer, VkDeviceMemory* InMemory) const;
    [[nodiscard]] bool EnsureCaptureStaging(VkDeviceSize InBytes, VkBuffer* OutBuffer, VkDeviceMemory* OutMemory);
    [[nodiscard]] VkCommandBuffer BeginOneShot() const;
    void EndOneShot(VkCommandBuffer InCommand) const;
    [[nodiscard]] bool WaitUploadFence() const;
    [[nodiscard]] bool SubmitUpload(VkCommandBuffer InCommand);
    // Brings a freshly created image to the layout its descriptors assume, so the
    // first bind never references an image that is still UNDEFINED.
    void InitImageLayout(VkImage InImage, VkImageLayout InLayout, std::uint32_t InLayers = 1);
    void TransitionImage(VkCommandBuffer InCommand, VkImage InImage, VkImageLayout InFrom, VkImageLayout InTo,
                         std::uint32_t InLayers = 1, std::uint32_t InMipLevels = 1) const;
    [[nodiscard]] TextureArray* FindTextureArray(std::uint32_t InId) noexcept;
    [[nodiscard]] VirtualTextureResource* FindVirtualTexture(std::uint32_t InId) noexcept;
    bool EnsureVtFeedback(Extent2D InExtent);
    void DestroyVtFeedbackResources() noexcept;
    void ParseVtFeedback(std::uint32_t InSlot) noexcept;
    void DestroyVirtualTextureResources(VirtualTextureResource& InTexture) noexcept;
    bool CreateVtFallbackResources();
    void DestroyVtFallbackResources() noexcept;
    void ApplyVirtualTexture(const FramePacket& InPacket);
    [[nodiscard]] VkDescriptorSet DescriptorFor(std::uint32_t InId) noexcept;
    void DestroyTextureArrayResources(TextureArray& InArray) noexcept;

    bool CreatePipelineForPass(VkRenderPass InRenderPass, VkPipeline* OutPipeline, bool InDepthTest = false,
                               bool InBlendless = false,
                               Fby::Rhi::RenderBlendMode InBlend = Fby::Rhi::RenderBlendMode::Opaque);
    [[nodiscard]] RenderTarget* FindRenderTarget(std::uint32_t InId) noexcept;
    void DestroyRenderTargetResources(RenderTarget& InTarget) noexcept;
    void RecordDepthPrepass(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket, bool InAccessibility);

    void DrawPacket(VkCommandBuffer InCommandBuffer, VkPipeline InPipeline, const FramePacket& InPacket,
                    bool InOffscreen, bool InSrgb, std::uint32_t InSdfSlot, bool InDepthOnly = false);
    void PreUploadInstances(VkCommandBuffer InCommandBuffer, const FramePacket& InPacket);
    // The shared sprite sets outlive a frame slot, so any rewrite has to wait for
    // every command buffer that may still reference them.
    void WaitAllInFlight();

    void DestroySwapchain();
    [[nodiscard]] bool RecreateSwapchain(Extent2D InSize);
    [[nodiscard]] VkSurfaceFormatKHR ChooseSurfaceFormat() const;
    [[nodiscard]] VkPresentModeKHR ChoosePresentMode(bool InVsync) const;
    [[nodiscard]] VkExtent2D ChooseExtent(Extent2D InRequested) const;

    DeviceDesc MDesc{};
    DeviceInfo MInfo{};
    FrameStats MStats{};
    VkPhysicalDeviceMemoryProperties MMemoryProperties{};

    VkInstance MInstance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT MDebugMessenger = VK_NULL_HANDLE;
    PFN_vkCreateDebugUtilsMessengerEXT MCreateDebugMessenger = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT MDestroyDebugMessenger = nullptr;
    VkSurfaceKHR MSurface = VK_NULL_HANDLE;
    VkPhysicalDevice MPhysical = VK_NULL_HANDLE;
    VkDevice MDevice = VK_NULL_HANDLE;
    VkQueue MQueue = VK_NULL_HANDLE;
    std::uint32_t MQueueFamily = 0;
    // Instance API version negotiated with the loader. Device feature chains must
    // not use a version above it, or a strict layer rejects vkCreateDevice.
    std::uint32_t MInstanceApiVersion = VK_API_VERSION_1_4;

    VkSwapchainKHR MSwapchain = VK_NULL_HANDLE;
    VkFormat MFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D MExtent{};
    std::vector<VkImage> MImages;
    std::vector<VkImageView> MViews;
    std::vector<VkImage> MDrawDepthImages;
    std::vector<VkDeviceMemory> MDrawDepthMemories;
    std::vector<VkImageView> MDrawDepthViews;
    VkSampler MDepthSampler = VK_NULL_HANDLE;
    VkImage MHiZImage = VK_NULL_HANDLE;
    VkDeviceMemory MHiZMemory = VK_NULL_HANDLE;
    VkImageView MHiZView = VK_NULL_HANDLE;
    std::vector<VkImageView> MHiZStorageViews;
    VkExtent2D MHiZExtent{};
    std::uint32_t MHiZMipCount = 1;
    bool MHiZReady = false;
    VkSampler MHiZSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout MHiZSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MHiZPool = VK_NULL_HANDLE;
    VkDescriptorSet MHiZSet = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> MHiZMipSets;
    VkPipelineLayout MHiZLayout = VK_NULL_HANDLE;
    VkPipeline MHiZPipeline = VK_NULL_HANDLE;
    VkPipeline MHiZMipPipeline = VK_NULL_HANDLE;
    VkRenderPass MRenderPass = VK_NULL_HANDLE;
    VkRenderPass MHudOverlayPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> MFramebuffers;
    std::vector<VkFramebuffer> MHudOverlayFramebuffers;
    VkRenderPass MDepthPrepassRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> MDepthPrepassFramebuffers;
    VkPipeline MDepthPrepassPipeline = VK_NULL_HANDLE;

    VkCommandPool MCommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> MCommandBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> MImageAvailable{};
    std::array<VkFence, kMaxFramesInFlight> MInFlight{};
    std::vector<VkSemaphore> MRenderFinished;
    std::array<VkQueryPool, kMaxFramesInFlight> MTimestampPools{};
    bool MTimestampsReady = false;
    float MTimestampPeriod = 0.0f;

    VkDescriptorSetLayout MDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool MDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet MDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout MPipelineLayout = VK_NULL_HANDLE;
    VkPipeline MPipeline = VK_NULL_HANDLE;
    VkPipeline MPipelineOpaque = VK_NULL_HANDLE;
    VkDescriptorSetLayout MOitResolveSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MOitResolvePool = VK_NULL_HANDLE;
    VkDescriptorSet MOitResolveSet = VK_NULL_HANDLE;
    VkPipelineLayout MOitResolveLayout = VK_NULL_HANDLE;
    VkPipeline MOitResolvePipeline = VK_NULL_HANDLE;
    std::unordered_map<VkRenderPass, VkPipeline> MOitResolvePipelines;
    VkSampler MOitResolveSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout MUpscaleSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MUpscalePool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> MUpscaleSets{};
    [[nodiscard]] VkDescriptorSet UpscaleSet() const noexcept
    {
        return MUpscaleSets[MFrameIndex];
    }

    // RCAS runs after the upscale in the same frame; a shared set would be updated
    // after the command buffer already bound it.
    std::array<VkDescriptorSet, kMaxFramesInFlight> MRcasSets{};
    [[nodiscard]] VkDescriptorSet RcasSet() const noexcept
    {
        return MRcasSets[MFrameIndex];
    }

    VkPipelineLayout MUpscaleLayout = VK_NULL_HANDLE;
    VkSampler MUpscaleSampler = VK_NULL_HANDLE;
    std::unordered_map<VkRenderPass, VkPipeline> MUpscalePipelines;
    VkPipelineLayout MRcasLayout = VK_NULL_HANDLE;
    std::unordered_map<VkRenderPass, VkPipeline> MRcasPipelines;
    std::uint32_t MHudTargetId = 0xFFFFFFFFu;
    VkDescriptorSetLayout MHudSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MHudPool = VK_NULL_HANDLE;
    // One HUD set per frame slot: the composite rewrites it every frame, and a
    // single shared set would be updated while earlier submissions still read it.
    std::array<VkDescriptorSet, kMaxFramesInFlight> MHudSets{};
    [[nodiscard]] VkDescriptorSet HudSet() const noexcept
    {
        return MHudSets[MFrameIndex];
    }
    [[nodiscard]] VkDescriptorSet& HudSetRef() noexcept
    {
        return MHudSets[MFrameIndex];
    }
    VkPipelineLayout MHudLayout = VK_NULL_HANDLE;
    VkSampler MHudSampler = VK_NULL_HANDLE;
    VkSampler MHudNearestSampler = VK_NULL_HANDLE;
    bool MHudNearest = false;
    VkPipeline MHudPipeline = VK_NULL_HANDLE;
    bool MSkipMainUi = false;
    bool MInHudPass = false;
    bool MUiVisible = true;
    VkBuffer MMotionBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MMotionMemory = VK_NULL_HANDLE;
    std::uint32_t MMotionCapacity = 0;
    std::uint32_t MMotionCount = 0;
    void* MMotionMapped = nullptr;
    VkDescriptorSetLayout MMotionSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MMotionPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> MMotionSets{};
    [[nodiscard]] VkDescriptorSet MotionSet() const noexcept
    {
        return MMotionSets[MFrameIndex];
    }

    VkPipelineLayout MMotionLayout = VK_NULL_HANDLE;
    std::unordered_map<VkRenderPass, VkPipeline> MMotionPipelines;
    VkDescriptorSetLayout MTemporalSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MTemporalPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> MTemporalSets{};
    [[nodiscard]] VkDescriptorSet TemporalSet() const noexcept
    {
        return MTemporalSets[MFrameIndex];
    }

    VkPipelineLayout MTemporalLayout = VK_NULL_HANDLE;
    std::unordered_map<VkRenderPass, VkPipeline> MTemporalPipelines;
    TextureArray MAtlas{};

    // GPU SDF shadows: static field texture, per-view parameter blocks, sampler.
    // The static field texture is a dense resample of the world field. The
    // brick atlas will replace it; until that path lands, a 1024-side texture
    // keeps large worlds from collapsing into a 256-texel field.
    static constexpr std::uint32_t kSdfFieldSize = 1024;
    // Two viewports are enough for editor and PIE; each frame gets its own slot.
    static constexpr std::uint32_t SdfMaxViews = 2;
    static constexpr std::uint32_t SdfViewSlots = kMaxFramesInFlight * SdfMaxViews;
    static constexpr std::uint32_t kSdfShadowBlockBytes = 672;
    // Dynamic uniform offsets must respect minUniformBufferOffsetAlignment.
    // 256 is the largest value the Vulkan specification permits, so this stride
    // is valid on every conformant device.
    static constexpr std::uint32_t SdfBlockStride = 768;
    static_assert(SdfBlockStride >= kSdfShadowBlockBytes);
    static_assert((SdfBlockStride % 256u) == 0u);
    VkImage MSdfFieldImage = VK_NULL_HANDLE;
    VkDeviceMemory MSdfFieldMemory = VK_NULL_HANDLE;
    VkImageView MSdfFieldView = VK_NULL_HANDLE;
    VkSampler MSdfFieldSampler = VK_NULL_HANDLE;
    // Sparse atlas: a grid of 16x16 brick tiles plus a header table. The sampling
    // switch lands after the descriptors; until then the resources stay inert.
    static constexpr std::uint32_t kSdfAtlasSize = 256;
    static constexpr std::uint32_t kSdfTableCapacity = 256;
    static constexpr std::uint32_t kSdfTableEntryBytes = 32;
    VkBuffer MSdfTableBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MSdfTableMemory = VK_NULL_HANDLE;
    void* MSdfTableMapped = nullptr;
    VkImage MSdfAtlasImage = VK_NULL_HANDLE;
    VkDeviceMemory MSdfAtlasMemory = VK_NULL_HANDLE;
    VkImageView MSdfAtlasView = VK_NULL_HANDLE;
    VkSampler MSdfAtlasSampler = VK_NULL_HANDLE;
    std::array<float, kSdfAtlasSize * kSdfAtlasSize> MSdfAtlasTexels{};
    bool MSdfAtlasInitialized = false;
    bool MSdfAtlasUploadPending = false;
    VkBuffer MSdfBlockBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MSdfBlockMemory = VK_NULL_HANDLE;
    void* MSdfBlockMapped = nullptr;
    // The field is re-uploaded from the frame's own staging slice, so a later
    // frame cannot overwrite bytes a pending command buffer still reads.
    std::array<VkBuffer, kMaxFramesInFlight> MSdfFieldStagingBuffers{};
    std::array<VkDeviceMemory, kMaxFramesInFlight> MSdfFieldStagingMemories{};
    std::array<void*, kMaxFramesInFlight> MSdfFieldStagingMapped{};
    std::array<float, kSdfFieldSize * kSdfFieldSize> MSdfFieldTexels{};
    std::uint64_t MSdfFieldVersion = 0;
    bool MSdfFieldUploadPending = false;
    bool MSdfFieldInitialized = false;
    bool MSdfEnabled = false;

    // Screen-space shadow mask array + compute pipeline. Layer = frame slot.
    VkImage MSdfMaskImage = VK_NULL_HANDLE;
    VkDeviceMemory MSdfMaskMemory = VK_NULL_HANDLE;
    VkImageView MSdfMaskView = VK_NULL_HANDLE;
    VkSampler MSdfMaskSampler = VK_NULL_HANDLE;
    VkExtent2D MSdfMaskExtent{};
    std::array<bool, SdfViewSlots> MSdfMaskPublished{};
    VkDescriptorSetLayout MSdfComputeSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MSdfComputePool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, SdfViewSlots> MSdfComputeSets{};
    std::array<VkBuffer, SdfViewSlots> MSdfComputeBuffers{};
    std::array<VkDeviceMemory, SdfViewSlots> MSdfComputeMemories{};
    std::array<void*, SdfViewSlots> MSdfComputeMapped{};
    VkPipelineLayout MSdfComputeLayout = VK_NULL_HANDLE;
    VkPipeline MSdfComputePipeline = VK_NULL_HANDLE;

    // Clustered 2D lighting: one light list and one cluster table per in-flight
    // frame. The light buffer is host-visible; headers and indices are GPU-only.
    static constexpr std::uint32_t kClusterMaxLights = 1024;
    // 8192 clusters cover 1920x1080 at a 16px tile (120 x 68). Larger views
    // keep CPU lighting and skip the GPU cluster pass instead of growing this
    // table without bound.
    static constexpr std::uint32_t kClusterMaxCount = 8192;
    static constexpr std::uint32_t kClusterMaxIndices = kClusterMaxCount * 64;
    struct ClusterFrame
    {
        VkBuffer LightBuffer = VK_NULL_HANDLE;
        VkDeviceMemory LightMemory = VK_NULL_HANDLE;
        void* LightMapped = nullptr;
        VkBuffer HeaderBuffer = VK_NULL_HANDLE;
        VkDeviceMemory HeaderMemory = VK_NULL_HANDLE;
        VkBuffer IndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory IndexMemory = VK_NULL_HANDLE;
        VkDescriptorSet Set = VK_NULL_HANDLE;
        std::uint32_t LightVersion = 0;
    };
    std::array<ClusterFrame, kMaxFramesInFlight> MClusterFrames{};
    VkDescriptorSetLayout MClusterSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MClusterPool = VK_NULL_HANDLE;
    VkPipelineLayout MClusterLayout = VK_NULL_HANDLE;
    VkPipeline MClusterCountPipeline = VK_NULL_HANDLE;
    VkPipeline MClusterPrefixPipeline = VK_NULL_HANDLE;
    VkPipeline MClusterFillPipeline = VK_NULL_HANDLE;
    bool MClusterRequested = true;
    bool MClusterReady = false;
    bool MClusterFallbackReady = false;
    std::array<VkBuffer, 5> MClusterFallbackBuffers{};
    std::array<VkDeviceMemory, 5> MClusterFallbackMemories{};
    VkDescriptorSet MClusterFallbackSet = VK_NULL_HANDLE;
    bool MDepthPrepassRequested = true;
    bool MDepthPrepassReady = false;

    static constexpr std::uint32_t kMaxCulledItems = 1u << 17;
    static constexpr std::uint32_t kMaxCullBatches = 64;
    struct CullFrame
    {
        VkBuffer VisibleBuffer = VK_NULL_HANDLE;
        VkDeviceMemory VisibleMemory = VK_NULL_HANDLE;
        VkBuffer CounterBuffer = VK_NULL_HANDLE;
        VkDeviceMemory CounterMemory = VK_NULL_HANDLE;
        VkBuffer CommandBuffer = VK_NULL_HANDLE;
        VkDeviceMemory CommandMemory = VK_NULL_HANDLE;
        void* CommandMapped = nullptr;
        VkBuffer ReadbackBuffer = VK_NULL_HANDLE;
        VkDeviceMemory ReadbackMemory = VK_NULL_HANDLE;
        void* ReadbackMapped = nullptr;
        VkDescriptorSet Set = VK_NULL_HANDLE;
        std::uint32_t SceneVersion = 0;
    };
    static constexpr std::uint32_t kMaxGiProbes = 4096;
    struct GiFrame
    {
        VkBuffer ProbeBuffer = VK_NULL_HANDLE;
        VkDeviceMemory ProbeMemory = VK_NULL_HANDLE;
        void* ProbeMapped = nullptr;
        VkBuffer HeaderBuffer = VK_NULL_HANDLE;
        VkDeviceMemory HeaderMemory = VK_NULL_HANDLE;
        void* HeaderMapped = nullptr;
        std::uint32_t Version = 0;
    };
    std::array<GiFrame, kMaxFramesInFlight> MGiFrames{};
    bool MGiReady = false;
    bool MVirtualTextureDirty = false;
    std::array<CullFrame, kMaxFramesInFlight> MCullFrames{};
    VkDescriptorSetLayout MCullSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MCullPool = VK_NULL_HANDLE;
    VkPipelineLayout MCullLayout = VK_NULL_HANDLE;
    VkPipeline MCullPipeline = VK_NULL_HANDLE;
    VkPipeline MCullFinalizePipeline = VK_NULL_HANDLE;
    bool MCullRequested = true;
    bool MCullReady = false;

    bool MClusterRecordedThisFrame = false;

    VkDescriptorSetLayout MMeshComputeSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout MMeshGraphicsSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MMeshDescriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout MMeshComputeLayout = VK_NULL_HANDLE;
    VkPipelineLayout MMeshGraphicsLayout = VK_NULL_HANDLE;
    VkPipeline MMeshComputePipeline = VK_NULL_HANDLE;
    VkPipeline MMeshPipeline = VK_NULL_HANDLE;
    std::array<MeshFrameBuffers, kMaxFramesInFlight> MMeshFrames{};
    std::vector<MeshDrawRecord> MMeshDraws{};
    std::vector<MeshIndirectGroup> MMeshGroups{};
    std::vector<std::uint32_t> MMeshDrawOrder{};
    std::uint32_t MMeshDrawCount = 0;
    std::uint32_t MMeshStaticDrawCount = 0;
    std::uint32_t MMeshDynamicDrawCount = 0;
    std::uint32_t MMeshDynamicVertexCount = 0;
    std::uint32_t MMeshSkinRangeCount = 0;
    bool MMeshPipelineLogged = false;
    std::uint32_t MMeshVertexCapacity = 0;
    std::uint32_t MMeshIndexCapacity = 0;
    std::uint32_t MBoneMatrixCapacity = 0;
    std::uint32_t MMeshDrawCapacity = 0;
    // Ranges already used in this frame. A frame can record several viewports,
    // so every mesh pass appends instead of restarting at zero.
    std::uint32_t MMeshFrameVertexCursor = 0;
    std::uint32_t MMeshFrameIndexCursor = 0;
    std::uint32_t MMeshFrameBoneCursor = 0;

    // Device-local static geometry arena. Both buffers are allocated once;
    VkBuffer MStaticVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MStaticVertexMemory = VK_NULL_HANDLE;
    // One index pool for the whole frame:
    VkBuffer MIndexPool = VK_NULL_HANDLE;
    VkDeviceMemory MIndexPoolMemory = VK_NULL_HANDLE;
    std::uint32_t MStaticVertexCapacity = 0;
    std::uint32_t MStaticIndexCapacity = 0;
    std::vector<MeshArenaRange> MStaticVertexFree;
    std::vector<MeshArenaRange> MStaticIndexFree;
    std::vector<StaticMeshRecord> MStaticMeshes;
    std::vector<std::uint32_t> MFreeStaticMeshIds;

    // User-uploaded texture arrays. Slot 0 is a reserved sentinel.
    static constexpr std::uint32_t kMaxTextureArrays = 64;
    std::vector<std::unique_ptr<TextureArray>> MTextureArrays;
    std::vector<std::uint32_t> MFreeTextureArrayIds;

    // Per-(array, layer) 2D image views used by editor thumbnails. The backend
    // owns them: they are created on first request and destroyed with the device.
    mutable std::vector<std::pair<std::uint64_t, VkImageView>> MLayerViews;

    VkBuffer MIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MIndexMemory = VK_NULL_HANDLE;

    // Device-local vertex source plus one persistently mapped staging slice per
    // frame. The staging buffer is never used as a vertex buffer.
    VkBuffer MInstanceBuffer = VK_NULL_HANDLE;
    // Instance data is uploaded before any render pass opens: buffer copies and
    // barriers are invalid inside a pass. Each packet is uploaded once and every
    // pass that draws it reuses the same slice.
    struct PreUploadedPacket
    {
        const FramePacket* packet = nullptr;
        VkDeviceSize base = 0;
        std::uint32_t count = 0;
    };
    std::vector<PreUploadedPacket> MPreUploadedPackets;
    VkDeviceMemory MInstanceMemory = VK_NULL_HANDLE;
    VkBuffer MInstanceStagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MInstanceStagingMemory = VK_NULL_HANDLE;
    void* MInstanceMapped = nullptr;
    std::uint32_t MInstanceCapacity = 0;
    // Dedicated tail slices, one per frame in flight, for a static packet. They
    // survive frame rotation and are overwritten only when that frame's static
    // scene version changes.
    std::array<bool, 8> MStaticInstanceSliceValid{};
    std::array<std::uint64_t, 8> MStaticSceneId{};
    std::array<std::uint64_t, 8> MStaticSceneVersion{};
    std::array<std::uint32_t, 8> MStaticItemCount{};
    std::uint32_t MFramesInFlight = 1;
    // Items already uploaded in this frame. Several viewports (PIE + editor + UI)
    // share one frame, so each pass takes its own contiguous range instead of
    // overwriting the previous one.
    std::uint32_t MInstanceFrameCursor = 0;
    std::uint32_t MRecordedPassIndex = 0;
    // A slice is valid after its first full upload. Delta range uploads are only
    // applied when the slice was written in the immediately previous frame.
    std::array<bool, 8> MInstanceSliceValid{};
    std::array<std::uint64_t, 8> MInstanceSliceFrameCounter{};

    std::uint32_t MFrameIndex = 0;
    std::uint32_t MImageIndex = 0;
    std::uint64_t MFrameCounter = 0;
    std::size_t MTransientCapacity = 0;
    bool MInFrame = false;
    bool MPassOpen = false;
    // True once Submit opened the swapchain pass; EndFrame uses it to avoid
    // presenting an image that was never written.
    bool MMainPassRecorded = false;
    std::uint32_t MPresentTargetId = 0xFFFFFFFFu;
    // Set when a fence wait or a submit reports a device error; BeginFrame then
    // refuses to run so a reset-but-unsignalled fence can never hang the loop.
    bool MDeviceLost = false;
    // Set from FramePacket::accessibility in Submit/RenderToTarget and reset.
    bool MBlindFrame = false;

    static constexpr std::uint32_t kMaxRenderTargets = 16;
    std::vector<std::unique_ptr<RenderTarget>> MRenderTargets;
    std::vector<std::unique_ptr<VirtualTextureResource>> MVirtualTextures;
    std::vector<std::uint32_t> MFreeVirtualTextureIds;
    // 1x1 neutral VT views bound when no virtual texture is active: the shader
    // never samples them (enabled flag is 0), but the descriptor must be valid.
    VkImage MVtFallbackTable = VK_NULL_HANDLE;
    VkDeviceMemory MVtFallbackTableMemory = VK_NULL_HANDLE;
    VkImageView MVtFallbackTableView = VK_NULL_HANDLE;
    VkSampler MVtFallbackTableSampler = VK_NULL_HANDLE;
    VkImage MVtFallbackPages = VK_NULL_HANDLE;
    VkDeviceMemory MVtFallbackPagesMemory = VK_NULL_HANDLE;
    VkImageView MVtFallbackPagesView = VK_NULL_HANDLE;
    VkSampler MVtFallbackPagesSampler = VK_NULL_HANDLE;
    VirtualTextureHandle MActiveVirtualTexture{};
    // Feedback: one r32ui image and one readback buffer per frame slot. The
    // shader writes the requested tile per VT fragment, the CPU dedups it a few
    // frames later without waiting on the GPU mid-frame.
    static constexpr std::uint32_t kFeedbackSlots = 3;
    std::array<VkImage, kFeedbackSlots> MVtFeedbackImages{};
    std::array<VkDeviceMemory, kFeedbackSlots> MVtFeedbackMemory{};
    std::array<VkImageView, kFeedbackSlots> MVtFeedbackViews{};
    std::array<VkBuffer, kFeedbackSlots> MVtFeedbackBuffers{};
    std::array<VkDeviceMemory, kFeedbackSlots> MVtFeedbackBufferMemory{};
    std::array<void*, kFeedbackSlots> MVtFeedbackMapped{};
    VkExtent2D MVtFeedbackExtent{};
    Extent2D MVtFeedbackPendingExtent{};
    // Feedback images are frame-global; recreating them mid-frame would free
    // images that recorded commands still reference.
    bool MVtFeedbackRecordedThisFrame = false;
    VkImage MVtFeedbackFallback = VK_NULL_HANDLE;
    VkDeviceMemory MVtFeedbackFallbackMemory = VK_NULL_HANDLE;
    VkImageView MVtFeedbackFallbackView = VK_NULL_HANDLE;
    std::array<std::uint32_t, kMaxVirtualTextureFeedbackTiles> MVtFeedbackTiles{};
    std::uint32_t MVtFeedbackCount = 0;
    std::uint64_t MVtFeedbackReads = 0;
    std::vector<std::uint32_t> MFreeRenderTargetIds;
    UiRenderCallback MUiRenderer = nullptr;
    void* MUiUser = nullptr;

    // One shared offscreen render pass with the same attachment format.
    VkRenderPass MOffscreenRenderPass = VK_NULL_HANDLE;
    VkRenderPass MSrgbOffscreenRenderPass = VK_NULL_HANDLE;

    // Accessibility composite resources (created once, optional).
    VkDescriptorSetLayout MAccessibilitySetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MAccessibilityPool = VK_NULL_HANDLE;
    // Post-process composite over the offscreen scene image.
    VkDescriptorSetLayout MPostFxSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MPostFxPool = VK_NULL_HANDLE;
    VkPipelineLayout MPostFxLayout = VK_NULL_HANDLE;
    VkPipeline MPostFxPipeline = VK_NULL_HANDLE;
    VkDescriptorSet MPostFxSet = VK_NULL_HANDLE;
    VkBuffer MPostFxUniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MPostFxUniformMemory = VK_NULL_HANDLE;
    bool MPostFxReady = false;
    bool MPostFxThisFrame = false;
    PostFxSettings MPostFxSettings{};
    // Constants arriving through RecordGraphicsPass (render-graph path).
    PostFxBlock MPostFxRecordBlock{};
    bool MPostFxRecordPending = false;
    VkPipelineLayout MAccessibilityLayout = VK_NULL_HANDLE;
    VkPipeline MAccessibilityPipeline = VK_NULL_HANDLE;
    VkSampler MAccessibilitySampler = VK_NULL_HANDLE;
    bool MAccessibilityReady = false;
    // Swapchain variant: the scene renders into a private image, then one pass
    // composites it into the acquired swapchain image.
    VkPipeline MAccessibilitySwapPipeline = VK_NULL_HANDLE;
    VkImage MAccessibilitySwapImage = VK_NULL_HANDLE;
    VkDeviceMemory MAccessibilitySwapMemory = VK_NULL_HANDLE;
    VkImageView MAccessibilitySwapView = VK_NULL_HANDLE;
    VkImage MAccessibilitySwapDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory MAccessibilitySwapDepthMemory = VK_NULL_HANDLE;
    VkImageView MAccessibilitySwapDepthView = VK_NULL_HANDLE;
    VkFramebuffer MAccessibilitySwapFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer MAccessibilityDepthPrepassFramebuffer = VK_NULL_HANDLE;
    VkDescriptorSet MAccessibilitySwapSet = VK_NULL_HANDLE;
    // Per-pixel flash guard: luminance history per pixel + compute dispatch.
    VkPipeline MAccessibilityFlashPipeline = VK_NULL_HANDLE;
    VkPipelineLayout MAccessibilityFlashLayout = VK_NULL_HANDLE;
    bool MAccessibilityFlashReady = false;
    VkImage MAccessibilitySwapFlashImage = VK_NULL_HANDLE;
    VkDeviceMemory MAccessibilitySwapFlashMemory = VK_NULL_HANDLE;
    VkImageView MAccessibilitySwapFlashView = VK_NULL_HANDLE;
    bool MAccessibilitySwapReady = false;
    bool MAccessibilitySwapActive = false;
    // Accessibility policy of the packet currently recorded into Submit().
    FrameAccessibility MAccessibilityFrame{};
    std::vector<std::unique_ptr<MaterialPipeline>> MMaterialPipelines;
    std::vector<std::uint32_t> MFreeMaterialPipelineIds;
    static constexpr std::uint32_t kMaxMaterialPipelines = 256;

    // Persistent host-visible staging arena for UpdateTextureArrayLayer: video
    VkBuffer MUploadBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MUploadMemory = VK_NULL_HANDLE;
    // One persistent upload command buffer and fence. Reusing the staging arena
    // waits on this fence instead of waiting for the whole device.
    VkCommandBuffer MUploadCommandBuffer = VK_NULL_HANDLE;
    VkFence MUploadFence = VK_NULL_HANDLE;
    // Persistent readback buffer for CaptureRenderTarget; grows on demand.
    VkBuffer MCaptureStaging = VK_NULL_HANDLE;
    VkDeviceMemory MCaptureStagingMemory = VK_NULL_HANDLE;
    void* MCaptureStagingMapped = nullptr;
    VkDeviceSize MCaptureStagingBytes = 0;
    void* MUploadMapped = nullptr;
    std::size_t MUploadCapacity = 0;

    VkPhysicalDeviceFeatures MDeviceFeatures{};
    bool MAstcLdrSupported = false;
};
}

#endif
