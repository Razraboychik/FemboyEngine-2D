#include "Engine/RHI/Vulkan/VulkanRHI.hpp"
#include "Engine/UI/Accessibility.hpp"

#include "Engine/Core/Assert.hpp"
#include "Engine/RHI/VirtualTextureFormat.hpp"
#include "Engine/Core/Log.hpp"

#if !defined(FBY_HAS_VULKAN_HEADERS)

// Built without Vulkan headers (an Apple build, or a configuration that turned
namespace Fby::Rhi
{
std::unique_ptr<RHI> CreateVulkanRHI()
{
    LogError("vulkan", "this build has no Vulkan headers");
    return nullptr;
}
}

#else

#include "Engine/RHI/Vulkan/VulkanLoader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "ShaderBlobs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace Fby::Rhi
{
namespace
{
constexpr std::uint32_t kMaxFramesInFlight = 4;
constexpr std::uint32_t kQuadIndexCount = 6;
constexpr std::uint32_t kAtlasSize = 128;
constexpr std::uint32_t kAtlasLayers = 4;
constexpr std::uint32_t kMaxMeshItemsPerFrame = 4096;
// R8 distance encoding: world units per normalized texel value.
constexpr float kSdfEncodeScale = 1024.0f;

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

// Matches the SdfBlock UBO declared by quad.frag. `occluders` holds pairs:
// shape (x, y, radius, 0) and extents (halfX, halfY, box, 0).
struct SdfGpuBlock
{
    Vector4D fieldBounds{};
    Vector4D sun{};
    Vector4D params{};
    Vector4D flags{};
    Vector4D occluders[kMaxSdfGpuOccluders * 2]{};
};

static_assert(sizeof(SdfGpuBlock) == 4 * 16 + kMaxSdfGpuOccluders * 2 * 16);

// Matches mesh.vert (first 64 bytes) and the material extension consumed by
struct MeshGraphicsPush
{
    Mat4 viewProjection = Mat4::Identity();
    // Time, viewportWidth, viewportHeight, unused
    Vector4D materialParams{0.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(MeshGraphicsPush) == 80);

// Matches mesh_skinning.comp (std430, 16 bytes): one dispatch covers the
struct MeshComputePush
{
    std::uint32_t vertexCount = 0;
    // Several viewports share one frame; each pass skins its own range.
    std::uint32_t vertexOffset = 0;
    std::uint32_t pad1 = 0;
    std::uint32_t pad2 = 0;
};

static_assert(sizeof(MeshComputePush) == 16);

// Matches postfx_composite.frag (std430, 112 bytes).
struct PostFxBlock
{
    Vector4D flagsAndCrt{};
    Vector4D vignetteAndBloom{};
    Vector4D exposureAndChromatic{};
    Vector4D grainAndFlash{};
    Vector4D colorRow0{};
    Vector4D colorRow1{};
    Vector4D colorRow2{};
};

static_assert(sizeof(PostFxBlock) == 112);

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
    VkFormat format = VK_FORMAT_UNDEFINED;
    Extent2D extent{};
    bool srgb = false;
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
    VkDescriptorSet flashSet = VK_NULL_HANDLE;
};

// Push block of accessibility_composite.frag: three matrix columns plus
// (gain, strength, flags, reserved).
struct AccessibilityCompositePush
{
    float matrix[12]{};
    float safety[4]{};
};

static_assert(sizeof(AccessibilityCompositePush) == 64);

[[nodiscard]] std::uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& InMemoryProperties,
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

// Cooked texel format -> Vulkan format. The runtime uploads the cooker's blocks
[[nodiscard]] VkFormat ToVkFormat(Fby::Assets::TextureFormat InFormat, bool InSrgb) noexcept
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

[[nodiscard]] bool IsAstcFormat(Fby::Assets::TextureFormat InFormat) noexcept
{
    return InFormat == Fby::Assets::TextureFormat::Astc4x4Unorm ||
           InFormat == Fby::Assets::TextureFormat::Astc4x4Srgb ||
           InFormat == Fby::Assets::TextureFormat::Astc6x6Unorm || InFormat == Fby::Assets::TextureFormat::Astc6x6Srgb;
}

[[nodiscard]] std::vector<VkExtensionProperties> QueryInstanceExtensions()
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

[[nodiscard]] bool HasExtension(const std::vector<VkExtensionProperties>& InAvailable, const char* InName)
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
[[nodiscard]] std::vector<std::byte> BuildAtlasPixels()
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
    void WaitIdle() override;

    bool BeginFrame() override;
    void Submit(const FramePacket& InPacket) override;
    void EndFrame() override;

    TextureArrayHandle CreateTextureArray(const TextureArrayDesc& InDesc, std::span<const std::byte> InPixels) override;
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
    bool RenderToTarget(RenderTargetHandle InHandle, const FramePacket& InPacket) override;
    bool RecordComputePass(std::string_view InPipeline, const void* InConstants, std::uint32_t InConstantsBytes,
                           Extent2D InExtent) override;
    bool RecordGraphicsPass(std::string_view InPipeline, const void* InConstants, std::uint32_t InConstantsBytes,
                            Extent2D InExtent, std::uint32_t InInstanceCount) override;
    [[nodiscard]] bool RenderTargetTexture(RenderTargetHandle InHandle, NativeTextureView& InView) const override;
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
    bool CreateAccessibilityFlashResources();
    [[nodiscard]] bool FlashGuardWanted(const FrameAccessibility& InAccessibility) const noexcept;
    void RecordFlashGuard(VkCommandBuffer InCommandBuffer, VkImage InColorImage, VkDescriptorSet InSet,
                          Extent2D InExtent, VkImageLayout InFromLayout,
                          const FrameAccessibility& InAccessibility) noexcept;
    bool CreateAccessibilitySwapchainResources();
    bool CreatePostFxResources();
    void RecordPostFxComposite(VkCommandBuffer InCommandBuffer, const PostFxBlock& InBlock,
                               const Extent2D& InExtent);
    void RecordPostFxComposite(VkCommandBuffer InCommandBuffer, const PostFxSettings& InSettings,
                               const Extent2D& InExtent);
    void DestroyAccessibilitySwapchainResources() noexcept;
    [[nodiscard]] bool AccessibilityCompositeWanted(const FramePacket& InPacket) const noexcept;
    void RecordAccessibilityComposite(VkCommandBuffer InCommandBuffer, VkRenderPass InRenderPass,
                                      VkFramebuffer InFramebuffer, VkPipeline InPipeline,
                                      VkDescriptorSet InSet, Extent2D InExtent,
                                      const FrameAccessibility& InAccessibility) noexcept;
    bool CreateFramebuffers();
    bool CreateCommands();
    bool CreateSyncObjects();
    bool CreateDescriptors();
    bool CreatePipeline();
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
    bool EnsureSdfMask(Extent2D InMaskExtent);
    void UpdateSdfComputeSets(VkImageView InMaskView) const;
    void DestroySdfMaskResources() noexcept;
    void DestroySdfShadowResources() noexcept;
    void WriteSdfDescriptors(VkDescriptorSet InSet) const;
    void ApplySdfLighting(const SdfLightingFrame& InFrame);
    void UploadSdfField();
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
                    bool InOffscreen, bool InSrgb);

    [[nodiscard]] bool CreateBuffer(VkDeviceSize InSize, VkBufferUsageFlags InUsage, VkMemoryPropertyFlags InProperties,
                                    VkBuffer* InBuffer, VkDeviceMemory* InMemory) const;
    [[nodiscard]] bool EnsureCaptureStaging(VkDeviceSize InBytes, VkBuffer* OutBuffer, VkDeviceMemory* OutMemory);
    [[nodiscard]] VkCommandBuffer BeginOneShot() const;
    void EndOneShot(VkCommandBuffer InCommand) const;
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

    bool CreatePipelineForPass(VkRenderPass InRenderPass, VkPipeline* OutPipeline);
    [[nodiscard]] RenderTarget* FindRenderTarget(std::uint32_t InId) noexcept;
    void DestroyRenderTargetResources(RenderTarget& InTarget) noexcept;
    void DrawPacket(VkCommandBuffer InCommandBuffer, VkPipeline InPipeline, const FramePacket& InPacket,
                    bool InOffscreen, bool InSrgb);

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

    VkSwapchainKHR MSwapchain = VK_NULL_HANDLE;
    VkFormat MFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D MExtent{};
    std::vector<VkImage> MImages;
    std::vector<VkImageView> MViews;
    VkRenderPass MRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> MFramebuffers;

    VkCommandPool MCommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> MCommandBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> MImageAvailable{};
    std::array<VkFence, kMaxFramesInFlight> MInFlight{};
    std::vector<VkSemaphore> MRenderFinished;

    VkDescriptorSetLayout MDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool MDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet MDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout MPipelineLayout = VK_NULL_HANDLE;
    VkPipeline MPipeline = VK_NULL_HANDLE;
    TextureArray MAtlas{};

    // GPU SDF shadows: static field texture, per-frame parameter block, sampler.
    static constexpr std::uint32_t kSdfFieldSize = 256;
    static constexpr std::uint32_t kSdfMaskFrames = 3;
    static constexpr std::uint32_t kSdfShadowBlockBytes = 608;
    VkImage MSdfFieldImage = VK_NULL_HANDLE;
    VkDeviceMemory MSdfFieldMemory = VK_NULL_HANDLE;
    VkImageView MSdfFieldView = VK_NULL_HANDLE;
    VkSampler MSdfFieldSampler = VK_NULL_HANDLE;
    VkBuffer MSdfBlockBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MSdfBlockMemory = VK_NULL_HANDLE;
    void* MSdfBlockMapped = nullptr;
    std::array<std::uint8_t, kSdfFieldSize * kSdfFieldSize> MSdfFieldTexels{};
    std::uint64_t MSdfFieldVersion = 0;
    bool MSdfFieldUploadPending = false;
    bool MSdfFieldInitialized = false;
    bool MSdfEnabled = false;

    // Screen-space shadow mask + compute pipeline.
    VkImage MSdfMaskImage = VK_NULL_HANDLE;
    VkDeviceMemory MSdfMaskMemory = VK_NULL_HANDLE;
    VkImageView MSdfMaskView = VK_NULL_HANDLE;
    VkSampler MSdfMaskSampler = VK_NULL_HANDLE;
    VkExtent2D MSdfMaskExtent{};
    bool MSdfMaskValid = false;
    VkDescriptorSetLayout MSdfComputeSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool MSdfComputePool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kSdfMaskFrames> MSdfComputeSets{};
    std::array<VkBuffer, kSdfMaskFrames> MSdfComputeBuffers{};
    std::array<VkDeviceMemory, kSdfMaskFrames> MSdfComputeMemories{};
    std::array<void*, kSdfMaskFrames> MSdfComputeMapped{};
    VkPipelineLayout MSdfComputeLayout = VK_NULL_HANDLE;
    VkPipeline MSdfComputePipeline = VK_NULL_HANDLE;

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

    // User-uploaded texture arrays. Slot 0 is a reserved sentinel for the
    static constexpr std::uint32_t kMaxTextureArrays = 64;
    std::vector<std::unique_ptr<TextureArray>> MTextureArrays;
    std::vector<std::uint32_t> MFreeTextureArrayIds;

    // Per-(array, layer) 2D image views used by editor thumbnails. The backend
    // owns them: they are created on first request and destroyed with the device.
    mutable std::vector<std::pair<std::uint64_t, VkImageView>> MLayerViews;

    VkBuffer MIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MIndexMemory = VK_NULL_HANDLE;

    VkBuffer MInstanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory MInstanceMemory = VK_NULL_HANDLE;
    void* MInstanceMapped = nullptr;
    std::uint32_t MInstanceCapacity = 0;
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
    // Set when a fence wait or a submit reports a device error; BeginFrame then
    // refuses to run so a reset-but-unsignalled fence can never hang the loop.
    bool MDeviceLost = false;
    // Set from FramePacket::accessibility in Submit/RenderToTarget and reset in
    bool MBlindFrame = false;

    static constexpr std::uint32_t kMaxRenderTargets = 8;
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

    // One shared offscreen render pass with the same attachment format as every
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
    VkFramebuffer MAccessibilitySwapFramebuffer = VK_NULL_HANDLE;
    VkDescriptorSet MAccessibilitySwapSet = VK_NULL_HANDLE;
    // Per-pixel flash guard: luminance history per pixel + compute dispatch.
    VkPipeline MAccessibilityFlashPipeline = VK_NULL_HANDLE;
    VkPipelineLayout MAccessibilityFlashLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout MAccessibilityFlashSetLayout = VK_NULL_HANDLE;
    bool MAccessibilityFlashReady = false;
    VkImage MAccessibilitySwapFlashImage = VK_NULL_HANDLE;
    VkDeviceMemory MAccessibilitySwapFlashMemory = VK_NULL_HANDLE;
    VkImageView MAccessibilitySwapFlashView = VK_NULL_HANDLE;
    VkDescriptorSet MAccessibilitySwapFlashSet = VK_NULL_HANDLE;
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

bool VulkanRHI::Initialize(const DeviceDesc& InDesc)
{
    MDesc = InDesc;
    MDeviceLost = false;
    MInfo.backend = Backend::Vulkan;
    MFramesInFlight = std::clamp(InDesc.framesInFlight, 1u, kMaxFramesInFlight);
    MInfo.framesInFlight = MFramesInFlight;
    MInfo.maxItemsPerFrame = InDesc.maxItemsPerFrame;
    MInfo.maxTextureLayers = InDesc.maxTextureLayers;
    MInfo.maxMeshVerticesPerFrame = InDesc.maxMeshVerticesPerFrame;
    MInfo.maxBoneMatricesPerFrame = InDesc.maxBoneMatricesPerFrame;
    MInfo.maxStaticMeshVertices = InDesc.maxStaticMeshVertices;
    MInfo.maxMeshDrawsPerFrame = InDesc.maxMeshDrawsPerFrame;
    MTextureArrays.clear();
    // Id 0 = built-in atlas
    MTextureArrays.push_back(nullptr);
    MFreeTextureArrayIds.clear();
    MVirtualTextures.clear();
    // Id 0 = invalid handle
    MVirtualTextures.push_back(nullptr);
    MFreeVirtualTextureIds.clear();

    if (!Vk::GetApi().LoadGlobal())
    {
        LogError("vulkan", "no Vulkan loader on this system");
        return false;
    }
    if (!CreateInstance(InDesc) || !PickPhysicalDevice() || !CreateDevice())
    {
        return false;
    }
    Vk::GetApi().vkGetPhysicalDeviceMemoryProperties(MPhysical, &MMemoryProperties);
    // The requested layer bound is a policy; the device limit is a hard cap.
    VkPhysicalDeviceProperties ScopedDeviceProperties{};
    Vk::GetApi().vkGetPhysicalDeviceProperties(MPhysical, &ScopedDeviceProperties);
    MInfo.maxTextureLayers =
        std::min(InDesc.maxTextureLayers, ScopedDeviceProperties.limits.maxImageArrayLayers);

    if (!CreateSwapchain(InDesc.size) || !CreateRenderPass() || !CreateFramebuffers() || !CreateCommands() ||
        !CreateSyncObjects())
    {
        return false;
    }
    if (!CreateDescriptors() || !CreatePipeline() || !CreateGeometry() || !CreateInstanceRing() ||
        !CreateBuiltInAtlas() || !CreateMeshResources() || !CreateStaticMeshArena() || !CreateMeshDescriptors() ||
        !CreateMeshPipelines() || !CreateOffscreenRenderPass() || !CreateUploadArena(InDesc.maxTextureUploadBytes))
    {
        return false;
    }
    // The offscreen scene image and the swapchain composite pipelines are created
    // once the accessibility resources exist; without this they appeared only
    // after the first resize.
    if (MAccessibilityReady)
    {
        (void)CreateAccessibilitySwapchainResources();
    }

    LogInfo("vulkan", "device: {} (api {}.{}.{}), {} frames in flight", MInfo.name,
            VK_API_VERSION_MAJOR(MInfo.apiVersion), VK_API_VERSION_MINOR(MInfo.apiVersion),
            VK_API_VERSION_PATCH(MInfo.apiVersion), MFramesInFlight);
    return true;
}

bool VulkanRHI::CreateInstance(const DeviceDesc& InDesc)
{
    Vk::Api& ScopedApi = Vk::GetApi();

    std::uint32_t ScopedSdlExtensionCount = 0;
    const char* const* ScopedSdlExtensions = SDL_Vulkan_GetInstanceExtensions(&ScopedSdlExtensionCount);
    if (ScopedSdlExtensions == nullptr)
    {
        LogError("vulkan", "SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        return false;
    }

    std::vector<const char*> ScopedExtensions(ScopedSdlExtensions, ScopedSdlExtensions + ScopedSdlExtensionCount);
    const std::vector<VkExtensionProperties> ScopedAvailable = QueryInstanceExtensions();
    if (InDesc.enableValidation && HasExtension(ScopedAvailable, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        ScopedExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> ScopedLayers;
    if (InDesc.enableValidation)
    {
        const auto ScopedEnumerateLayers = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
            ScopedApi.GetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties"));
        std::uint32_t ScopedLayerCount = 0;
        if (ScopedEnumerateLayers != nullptr && ScopedEnumerateLayers(&ScopedLayerCount, nullptr) == VK_SUCCESS &&
            ScopedLayerCount > 0)
        {
            std::vector<VkLayerProperties> ScopedLayerProperties(ScopedLayerCount);
            if (ScopedEnumerateLayers(&ScopedLayerCount, ScopedLayerProperties.data()) == VK_SUCCESS)
            {
                for (const VkLayerProperties& ScopedProperties : ScopedLayerProperties)
                {
                    if (std::strcmp(ScopedProperties.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                    {
                        ScopedLayers.push_back("VK_LAYER_KHRONOS_validation");
                        break;
                    }
                }
            }
        }
        LogInfo("vulkan", "validation: {}", ScopedLayers.empty() ? "layer not available" : "VK_LAYER_KHRONOS_validation");
    }

    VkApplicationInfo ScopedApplication{};
    ScopedApplication.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ScopedApplication.pApplicationName = "FemboyEngine";
    ScopedApplication.applicationVersion = VK_MAKE_API_VERSION(0, 2, 0, 0);
    ScopedApplication.pEngineName = "FemboyEngine";
    ScopedApplication.engineVersion = VK_MAKE_API_VERSION(0, 2, 0, 0);
    ScopedApplication.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ScopedCreateInfo.pApplicationInfo = &ScopedApplication;
    ScopedCreateInfo.enabledExtensionCount = static_cast<std::uint32_t>(ScopedExtensions.size());
    ScopedCreateInfo.ppEnabledExtensionNames = ScopedExtensions.data();
    ScopedCreateInfo.enabledLayerCount = static_cast<std::uint32_t>(ScopedLayers.size());
    ScopedCreateInfo.ppEnabledLayerNames = ScopedLayers.data();

    if (ScopedApi.vkCreateInstance(&ScopedCreateInfo, nullptr, &MInstance) != VK_SUCCESS ||
        !ScopedApi.LoadInstance(MInstance))
    {
        LogError("vulkan", "instance creation failed");
        return false;
    }
    if (InDesc.enableValidation)
    {
        MCreateDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            ScopedApi.GetInstanceProcAddr(MInstance, "vkCreateDebugUtilsMessengerEXT"));
        MDestroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            ScopedApi.GetInstanceProcAddr(MInstance, "vkDestroyDebugUtilsMessengerEXT"));
        if (MCreateDebugMessenger != nullptr && MDestroyDebugMessenger != nullptr)
        {
            VkDebugUtilsMessengerCreateInfoEXT ScopedMessengerInfo{};
            ScopedMessengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            ScopedMessengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            ScopedMessengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            ScopedMessengerInfo.pfnUserCallback =
                [](VkDebugUtilsMessageSeverityFlagBitsEXT InSeverity, VkDebugUtilsMessageTypeFlagsEXT /*InTypes*/,
                   const VkDebugUtilsMessengerCallbackDataEXT* InData, void* /*InUser*/) -> VkBool32
            {
                const char* ScopedText = InData != nullptr && InData->pMessage != nullptr ? InData->pMessage : "";
                if (InSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                {
                    LogError("vulkan-validation", "{}", ScopedText);
                }
                else
                {
                    LogWarn("vulkan-validation", "{}", ScopedText);
                }
                return VK_FALSE;
            };
            if (MCreateDebugMessenger(MInstance, &ScopedMessengerInfo, nullptr, &MDebugMessenger) == VK_SUCCESS)
            {
                LogInfo("vulkan", "validation messenger active");
            }
            else
            {
                MDebugMessenger = VK_NULL_HANDLE;
            }
        }
    }
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(InDesc.nativeWindow), MInstance, nullptr, &MSurface))
    {
        LogError("vulkan", "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

bool VulkanRHI::PickPhysicalDevice()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    std::uint32_t ScopedCount = 0;
    ScopedApi.vkEnumeratePhysicalDevices(MInstance, &ScopedCount, nullptr);
    if (ScopedCount == 0)
    {
        LogError("vulkan", "no physical devices");
        return false;
    }
    std::vector<VkPhysicalDevice> ScopedDevices(ScopedCount);
    ScopedApi.vkEnumeratePhysicalDevices(MInstance, &ScopedCount, ScopedDevices.data());

    int ScopedBestScore = -1;
    for (VkPhysicalDevice ScopedCandidate : ScopedDevices)
    {
        VkPhysicalDeviceProperties ScopedProperties{};
        ScopedApi.vkGetPhysicalDeviceProperties(ScopedCandidate, &ScopedProperties);

        std::uint32_t ScopedFamilyCount = 0;
        ScopedApi.vkGetPhysicalDeviceQueueFamilyProperties(ScopedCandidate, &ScopedFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> ScopedFamilies(ScopedFamilyCount);
        ScopedApi.vkGetPhysicalDeviceQueueFamilyProperties(ScopedCandidate, &ScopedFamilyCount, ScopedFamilies.data());

        for (std::uint32_t ScopedFamily = 0; ScopedFamily < ScopedFamilyCount; ++ScopedFamily)
        {
            if ((ScopedFamilies[ScopedFamily].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
            {
                continue;
            }
            VkBool32 ScopedPresentSupport = VK_FALSE;
            ScopedApi.vkGetPhysicalDeviceSurfaceSupportKHR(ScopedCandidate, ScopedFamily, MSurface,
                                                           &ScopedPresentSupport);
            if (ScopedPresentSupport == VK_FALSE)
            {
                continue;
            }

            int ScopedScore = ScopedProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
            if (ScopedScore > ScopedBestScore)
            {
                ScopedBestScore = ScopedScore;
                MPhysical = ScopedCandidate;
                MQueueFamily = ScopedFamily;
                MInfo.apiVersion = ScopedProperties.apiVersion;
                MInfo.discrete = ScopedProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
                std::snprintf(MInfo.name, sizeof(MInfo.name), "%s", ScopedProperties.deviceName);
            }
            break;
        }
    }

    if (MPhysical == VK_NULL_HANDLE)
    {
        LogError("vulkan", "no device with a graphics queue that can present to this surface");
        return false;
    }
    return true;
}

bool VulkanRHI::CreateDevice()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    const float ScopedPriority = 1.0f;
    VkDeviceQueueCreateInfo ScopedQueueInfo{};
    ScopedQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    ScopedQueueInfo.queueFamilyIndex = MQueueFamily;
    ScopedQueueInfo.queueCount = 1;
    ScopedQueueInfo.pQueuePriorities = &ScopedPriority;

    const char* ScopedDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // The indirect mesh path uses firstInstance as the draw-record index, which
    VkPhysicalDeviceVulkan11Features ScopedVulkan11{};
    ScopedVulkan11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceFeatures2 ScopedAvailable{};
    ScopedAvailable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    ScopedAvailable.pNext = &ScopedVulkan11;
    ScopedApi.vkGetPhysicalDeviceFeatures2(MPhysical, &ScopedAvailable);
    if (ScopedVulkan11.shaderDrawParameters == VK_FALSE)
    {
        LogError("vulkan", "device does not support shaderDrawParameters; the indirect mesh path cannot select "
                           "per-draw model matrices");
        return false;
    }
    ScopedVulkan11.shaderDrawParameters = VK_TRUE;

    // Texture compression: BC7 is core, ASTC LDR is optional. Enable the ASTC
    MDeviceFeatures = ScopedAvailable.features;
    MAstcLdrSupported = ScopedAvailable.features.textureCompressionASTC_LDR == VK_TRUE;
    MDeviceFeatures.textureCompressionASTC_LDR = MAstcLdrSupported ? VK_TRUE : VK_FALSE;
    MDeviceFeatures.samplerAnisotropy = ScopedAvailable.features.samplerAnisotropy;

    VkDeviceCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ScopedCreateInfo.pNext = &ScopedVulkan11;
    ScopedCreateInfo.queueCreateInfoCount = 1;
    ScopedCreateInfo.pQueueCreateInfos = &ScopedQueueInfo;
    ScopedCreateInfo.enabledExtensionCount = 1;
    ScopedCreateInfo.ppEnabledExtensionNames = ScopedDeviceExtensions;
    ScopedCreateInfo.pEnabledFeatures = &MDeviceFeatures;

    if (ScopedApi.vkCreateDevice(MPhysical, &ScopedCreateInfo, nullptr, &MDevice) != VK_SUCCESS ||
        !ScopedApi.LoadDevice(MDevice))
    {
        LogError("vulkan", "vkCreateDevice failed");
        return false;
    }
    ScopedApi.vkGetDeviceQueue(MDevice, MQueueFamily, 0, &MQueue);
    return true;
}

VkSurfaceFormatKHR VulkanRHI::ChooseSurfaceFormat() const
{
    Vk::Api& ScopedApi = Vk::GetApi();
    std::uint32_t ScopedCount = 0;
    ScopedApi.vkGetPhysicalDeviceSurfaceFormatsKHR(MPhysical, MSurface, &ScopedCount, nullptr);
    std::vector<VkSurfaceFormatKHR> ScopedFormats(ScopedCount);
    if (ScopedCount > 0)
    {
        ScopedApi.vkGetPhysicalDeviceSurfaceFormatsKHR(MPhysical, MSurface, &ScopedCount, ScopedFormats.data());
    }
    for (const VkSurfaceFormatKHR& ScopedFormat : ScopedFormats)
    {
        if (ScopedFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            ScopedFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return ScopedFormat;
        }
    }
    if (!ScopedFormats.empty())
    {
        return ScopedFormats.front();
    }
    return VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
}

VkPresentModeKHR VulkanRHI::ChoosePresentMode(bool InVsync) const
{
    Vk::Api& ScopedApi = Vk::GetApi();
    std::uint32_t ScopedCount = 0;
    ScopedApi.vkGetPhysicalDeviceSurfacePresentModesKHR(MPhysical, MSurface, &ScopedCount, nullptr);
    std::vector<VkPresentModeKHR> ScopedModes(ScopedCount);
    if (ScopedCount > 0)
    {
        ScopedApi.vkGetPhysicalDeviceSurfacePresentModesKHR(MPhysical, MSurface, &ScopedCount, ScopedModes.data());
    }

    if (InVsync)
    {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    for (VkPresentModeKHR ScopedMode : ScopedModes)
    {
        if (ScopedMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return ScopedMode;
        }
    }
    for (VkPresentModeKHR ScopedMode : ScopedModes)
    {
        if (ScopedMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            return ScopedMode;
        }
    }
    // FIFO is the only mode the specification requires.
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRHI::ChooseExtent(Extent2D InRequested) const
{
    VkSurfaceCapabilitiesKHR ScopedCapabilities{};
    Vk::GetApi().vkGetPhysicalDeviceSurfaceCapabilitiesKHR(MPhysical, MSurface, &ScopedCapabilities);

    if (ScopedCapabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
    {
        return ScopedCapabilities.currentExtent;
    }
    VkExtent2D ScopedExtent{InRequested.width, InRequested.height};
    ScopedExtent.width = std::clamp(ScopedExtent.width, ScopedCapabilities.minImageExtent.width,
                                    ScopedCapabilities.maxImageExtent.width);
    ScopedExtent.height = std::clamp(ScopedExtent.height, ScopedCapabilities.minImageExtent.height,
                                     ScopedCapabilities.maxImageExtent.height);
    return ScopedExtent;
}

bool VulkanRHI::CreateBuffer(VkDeviceSize InSize, VkBufferUsageFlags InUsage, VkMemoryPropertyFlags InProperties,
                             VkBuffer* InBuffer, VkDeviceMemory* InMemory) const
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkBufferCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ScopedCreateInfo.size = InSize;
    ScopedCreateInfo.usage = InUsage;
    ScopedCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (ScopedApi.vkCreateBuffer(MDevice, &ScopedCreateInfo, nullptr, InBuffer) != VK_SUCCESS)
    {
        return false;
    }

    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetBufferMemoryRequirements(MDevice, *InBuffer, &ScopedRequirements);

    VkMemoryAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocateInfo.allocationSize = ScopedRequirements.size;
    ScopedAllocateInfo.memoryTypeIndex =
        FindMemoryType(MMemoryProperties, ScopedRequirements.memoryTypeBits, InProperties);
    if (ScopedAllocateInfo.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocateInfo, nullptr, InMemory) != VK_SUCCESS)
    {
        LogError("vulkan", "buffer memory allocation failed ({} bytes)", InSize);
        return false;
    }
    return ScopedApi.vkBindBufferMemory(MDevice, *InBuffer, *InMemory, 0) == VK_SUCCESS;
}

bool VulkanRHI::EnsureCaptureStaging(VkDeviceSize InBytes, VkBuffer* OutBuffer, VkDeviceMemory* OutMemory)
{
    if (MCaptureStaging != VK_NULL_HANDLE && MCaptureStagingBytes >= InBytes)
    {
        *OutBuffer = MCaptureStaging;
        *OutMemory = MCaptureStagingMemory;
        return true;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    // Grow to the next power of two so a slightly larger capture does not
    // reallocate again. The old buffer is idle: the queue waits before returning.
    VkDeviceSize ScopedCapacity = 4096;
    while (ScopedCapacity < InBytes)
    {
        ScopedCapacity *= 2;
    }
    VkBuffer ScopedBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ScopedMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(ScopedCapacity, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ScopedBuffer,
                      &ScopedMemory))
    {
        return false;
    }
    void* ScopedMapped = nullptr;
    if (ScopedApi.vkMapMemory(MDevice, ScopedMemory, 0, ScopedCapacity, 0, &ScopedMapped) != VK_SUCCESS)
    {
        ScopedApi.vkDestroyBuffer(MDevice, ScopedBuffer, nullptr);
        ScopedApi.vkFreeMemory(MDevice, ScopedMemory, nullptr);
        return false;
    }
    if (MCaptureStaging != VK_NULL_HANDLE)
    {
        ScopedApi.vkUnmapMemory(MDevice, MCaptureStagingMemory);
        ScopedApi.vkDestroyBuffer(MDevice, MCaptureStaging, nullptr);
        ScopedApi.vkFreeMemory(MDevice, MCaptureStagingMemory, nullptr);
    }
    MCaptureStaging = ScopedBuffer;
    MCaptureStagingMemory = ScopedMemory;
    MCaptureStagingMapped = ScopedMapped;
    MCaptureStagingBytes = ScopedCapacity;
    *OutBuffer = MCaptureStaging;
    *OutMemory = MCaptureStagingMemory;
    return true;
}

VkCommandBuffer VulkanRHI::BeginOneShot() const
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkCommandBufferAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ScopedAllocateInfo.commandPool = MCommandPool;
    ScopedAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ScopedAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer ScopedCommand = VK_NULL_HANDLE;
    if (ScopedApi.vkAllocateCommandBuffers(MDevice, &ScopedAllocateInfo, &ScopedCommand) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ScopedBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ScopedApi.vkBeginCommandBuffer(ScopedCommand, &ScopedBeginInfo);
    return ScopedCommand;
}

void VulkanRHI::EndOneShot(VkCommandBuffer InCommand) const
{
    Vk::Api& ScopedApi = Vk::GetApi();
    (void)ScopedApi.vkEndCommandBuffer(InCommand);

    VkSubmitInfo ScopedSubmitInfo{};
    ScopedSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    ScopedSubmitInfo.commandBufferCount = 1;
    ScopedSubmitInfo.pCommandBuffers = &InCommand;
    ScopedApi.vkQueueSubmit(MQueue, 1, &ScopedSubmitInfo, VK_NULL_HANDLE);
    ScopedApi.vkQueueWaitIdle(MQueue);
    ScopedApi.vkFreeCommandBuffers(MDevice, MCommandPool, 1, &InCommand);
}

void VulkanRHI::TransitionImage(VkCommandBuffer InCommand, VkImage InImage, VkImageLayout InFrom, VkImageLayout InTo,
                                std::uint32_t InLayers, std::uint32_t InMipLevels) const
{
    VkImageMemoryBarrier ScopedBarrier{};
    ScopedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ScopedBarrier.oldLayout = InFrom;
    ScopedBarrier.newLayout = InTo;
    ScopedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ScopedBarrier.image = InImage;
    ScopedBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ScopedBarrier.subresourceRange.levelCount = InMipLevels;
    ScopedBarrier.subresourceRange.layerCount = InLayers;

    VkPipelineStageFlags ScopedSourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags ScopedDestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (InFrom == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && InTo == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        ScopedSourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        ScopedDestinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        ScopedBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else
    {
        ScopedBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }

    Vk::GetApi().vkCmdPipelineBarrier(InCommand, ScopedSourceStage, ScopedDestinationStage, 0, 0, nullptr, 0, nullptr,
                                      1, &ScopedBarrier);
}

bool VulkanRHI::CreateSwapchain(Extent2D InSize)
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkSurfaceCapabilitiesKHR ScopedCapabilities{};
    ScopedApi.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(MPhysical, MSurface, &ScopedCapabilities);

    const VkSurfaceFormatKHR ScopedFormat = ChooseSurfaceFormat();
    const VkPresentModeKHR ScopedPresentMode = ChoosePresentMode(MDesc.vsync);
    const VkExtent2D ScopedExtent = ChooseExtent(InSize);

    std::uint32_t ScopedImageCount = ScopedCapabilities.minImageCount + 1;
    if (ScopedCapabilities.maxImageCount > 0 && ScopedImageCount > ScopedCapabilities.maxImageCount)
    {
        ScopedImageCount = ScopedCapabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ScopedCreateInfo.surface = MSurface;
    ScopedCreateInfo.minImageCount = ScopedImageCount;
    ScopedCreateInfo.imageFormat = ScopedFormat.format;
    ScopedCreateInfo.imageColorSpace = ScopedFormat.colorSpace;
    ScopedCreateInfo.imageExtent = ScopedExtent;
    ScopedCreateInfo.imageArrayLayers = 1;
    ScopedCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ScopedCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ScopedCreateInfo.preTransform = ScopedCapabilities.currentTransform;
    ScopedCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ScopedCreateInfo.presentMode = ScopedPresentMode;
    ScopedCreateInfo.clipped = VK_TRUE;

    if (ScopedApi.vkCreateSwapchainKHR(MDevice, &ScopedCreateInfo, nullptr, &MSwapchain) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateSwapchainKHR failed");
        return false;
    }

    MFormat = ScopedFormat.format;
    MExtent = ScopedExtent;

    std::uint32_t ScopedSwapImageCount = 0;
    ScopedApi.vkGetSwapchainImagesKHR(MDevice, MSwapchain, &ScopedSwapImageCount, nullptr);
    MImages.resize(ScopedSwapImageCount);
    ScopedApi.vkGetSwapchainImagesKHR(MDevice, MSwapchain, &ScopedSwapImageCount, MImages.data());

    MViews.resize(MImages.size());
    for (std::size_t ScopedI = 0; ScopedI < MImages.size(); ++ScopedI)
    {
        VkImageViewCreateInfo ScopedViewInfo{};
        ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ScopedViewInfo.image = MImages[ScopedI];
        ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ScopedViewInfo.format = MFormat;
        ScopedViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ScopedViewInfo.subresourceRange.levelCount = 1;
        ScopedViewInfo.subresourceRange.layerCount = 1;
        if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &MViews[ScopedI]) != VK_SUCCESS)
        {
            LogError("vulkan", "vkCreateImageView failed");
            return false;
        }
    }

    LogInfo("vulkan", "swapchain {}x{}, {} images", MExtent.width, MExtent.height, MImages.size());
    return true;
}

bool VulkanRHI::CreateRenderPass()
{
    VkAttachmentDescription ScopedColor{};
    ScopedColor.format = MFormat;
    ScopedColor.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ScopedColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ScopedColor.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ScopedColor.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ScopedColorRef{};
    ScopedColorRef.attachment = 0;
    ScopedColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription ScopedSubpass{};
    ScopedSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    ScopedSubpass.colorAttachmentCount = 1;
    ScopedSubpass.pColorAttachments = &ScopedColorRef;

    VkSubpassDependency ScopedDependency{};
    ScopedDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    ScopedDependency.dstSubpass = 0;
    ScopedDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    ScopedDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    ScopedDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ScopedCreateInfo.attachmentCount = 1;
    ScopedCreateInfo.pAttachments = &ScopedColor;
    ScopedCreateInfo.subpassCount = 1;
    ScopedCreateInfo.pSubpasses = &ScopedSubpass;
    ScopedCreateInfo.dependencyCount = 1;
    ScopedCreateInfo.pDependencies = &ScopedDependency;

    if (Vk::GetApi().vkCreateRenderPass(MDevice, &ScopedCreateInfo, nullptr, &MRenderPass) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateRenderPass failed");
        return false;
    }
    return true;
}

bool VulkanRHI::CreateFramebuffers()
{
    MFramebuffers.resize(MViews.size());
    for (std::size_t ScopedI = 0; ScopedI < MViews.size(); ++ScopedI)
    {
        VkFramebufferCreateInfo ScopedCreateInfo{};
        ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ScopedCreateInfo.renderPass = MRenderPass;
        ScopedCreateInfo.attachmentCount = 1;
        ScopedCreateInfo.pAttachments = &MViews[ScopedI];
        ScopedCreateInfo.width = MExtent.width;
        ScopedCreateInfo.height = MExtent.height;
        ScopedCreateInfo.layers = 1;
        if (Vk::GetApi().vkCreateFramebuffer(MDevice, &ScopedCreateInfo, nullptr, &MFramebuffers[ScopedI]) !=
            VK_SUCCESS)
        {
            LogError("vulkan", "vkCreateFramebuffer failed");
            return false;
        }
    }
    return true;
}

bool VulkanRHI::CreateCommands()
{
    VkCommandPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ScopedPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ScopedPoolInfo.queueFamilyIndex = MQueueFamily;
    if (Vk::GetApi().vkCreateCommandPool(MDevice, &ScopedPoolInfo, nullptr, &MCommandPool) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateCommandPool failed");
        return false;
    }

    VkCommandBufferAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ScopedAllocateInfo.commandPool = MCommandPool;
    ScopedAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ScopedAllocateInfo.commandBufferCount = kMaxFramesInFlight;
    if (Vk::GetApi().vkAllocateCommandBuffers(MDevice, &ScopedAllocateInfo, MCommandBuffers.data()) != VK_SUCCESS)
    {
        LogError("vulkan", "vkAllocateCommandBuffers failed");
        return false;
    }
    return true;
}

bool VulkanRHI::CreateSyncObjects()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // RecreateSwapchain calls this again: destroy the previous handles first,
    // otherwise every resize leaks 2x framesInFlight driver objects.
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxFramesInFlight; ++ScopedI)
    {
        if (MImageAvailable[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySemaphore(MDevice, MImageAvailable[ScopedI], nullptr);
            MImageAvailable[ScopedI] = VK_NULL_HANDLE;
        }
        if (MInFlight[ScopedI] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyFence(MDevice, MInFlight[ScopedI], nullptr);
            MInFlight[ScopedI] = VK_NULL_HANDLE;
        }
    }
    for (VkSemaphore& ScopedSemaphore : MRenderFinished)
    {
        if (ScopedSemaphore != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroySemaphore(MDevice, ScopedSemaphore, nullptr);
            ScopedSemaphore = VK_NULL_HANDLE;
        }
    }
    MRenderFinished.clear();

    VkSemaphoreCreateInfo ScopedSemaphoreInfo{};
    ScopedSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo ScopedFenceInfo{};
    ScopedFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    ScopedFenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::uint32_t ScopedI = 0; ScopedI < kMaxFramesInFlight; ++ScopedI)
    {
        if (ScopedApi.vkCreateSemaphore(MDevice, &ScopedSemaphoreInfo, nullptr, &MImageAvailable[ScopedI]) !=
                VK_SUCCESS ||
            ScopedApi.vkCreateFence(MDevice, &ScopedFenceInfo, nullptr, &MInFlight[ScopedI]) != VK_SUCCESS)
        {
            LogError("vulkan", "failed to create frame synchronisation objects");
            return false;
        }
    }

    MRenderFinished.resize(MImages.size());
    for (VkSemaphore& ScopedSemaphore : MRenderFinished)
    {
        if (ScopedApi.vkCreateSemaphore(MDevice, &ScopedSemaphoreInfo, nullptr, &ScopedSemaphore) != VK_SUCCESS)
        {
            LogError("vulkan", "failed to create present semaphores");
            return false;
        }
    }
    return true;
}

bool VulkanRHI::CreateDescriptors()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // Binding 0 atlas, 1 SDF field, 2 per-frame SDF block. Every descriptor set
    // built from this layout (the built-in atlas and each texture array) gets all
    // three written by WriteSdfDescriptors.
    std::array<VkDescriptorSetLayoutBinding, 7> ScopedBindings{};
    ScopedBindings[0].binding = 0;
    ScopedBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[0].descriptorCount = 1;
    ScopedBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[1].binding = 1;
    ScopedBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[1].descriptorCount = 1;
    ScopedBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[2].binding = 2;
    ScopedBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ScopedBindings[2].descriptorCount = 1;
    ScopedBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[3].binding = 3;
    ScopedBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[3].descriptorCount = 1;
    ScopedBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[4].binding = 4;
    ScopedBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[4].descriptorCount = 1;
    ScopedBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[5].binding = 5;
    ScopedBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[5].descriptorCount = 1;
    ScopedBindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedBindings[6].binding = 6;
    ScopedBindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedBindings[6].descriptorCount = kFeedbackSlots;
    ScopedBindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    if (!CreateSdfShadowResources())
    {
        return false;
    }

    VkDescriptorSetLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.bindingCount = static_cast<std::uint32_t>(ScopedBindings.size());
    ScopedLayoutInfo.pBindings = ScopedBindings.data();
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedLayoutInfo, nullptr, &MDescriptorLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateDescriptorSetLayout failed");
        return false;
    }

    std::array<VkDescriptorPoolSize, 3> ScopedPoolSizes{};
    ScopedPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedPoolSizes[0].descriptorCount = (kMaxTextureArrays + 1) * 5;
    ScopedPoolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ScopedPoolSizes[1].descriptorCount = kMaxTextureArrays + 1;
    ScopedPoolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedPoolSizes[2].descriptorCount = (kMaxTextureArrays + 1) * kFeedbackSlots;

    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ScopedPoolInfo.maxSets = kMaxTextureArrays + 1;
    ScopedPoolInfo.poolSizeCount = static_cast<std::uint32_t>(ScopedPoolSizes.size());
    ScopedPoolInfo.pPoolSizes = ScopedPoolSizes.data();
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MDescriptorPool) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreateDescriptorPool failed");
        return false;
    }

    VkDescriptorSetAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocateInfo.descriptorPool = MDescriptorPool;
    ScopedAllocateInfo.descriptorSetCount = 1;
    ScopedAllocateInfo.pSetLayouts = &MDescriptorLayout;
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocateInfo, &MDescriptorSet) != VK_SUCCESS)
    {
        LogError("vulkan", "vkAllocateDescriptorSets failed");
        return false;
    }
    return true;
}

bool VulkanRHI::CreateSdfShadowResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // The static field lives in an R8 texture (distance encoded as
    // clamp(distance / encodeScale, 0, 1)); the shader decodes it with the same
    // scale from the block below.
    VkImageCreateInfo ScopedImageInfo{};
    ScopedImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedImageInfo.format = VK_FORMAT_R8_UNORM;
    ScopedImageInfo.extent = {kSdfFieldSize, kSdfFieldSize, 1};
    ScopedImageInfo.mipLevels = 1;
    ScopedImageInfo.arrayLayers = 1;
    ScopedImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedImageInfo, nullptr, &MSdfFieldImage) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF field image creation failed");
        return false;
    }

    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MSdfFieldImage, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocateInfo{};
    ScopedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocateInfo.allocationSize = ScopedRequirements.size;
    ScopedAllocateInfo.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocateInfo.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocateInfo, nullptr, &MSdfFieldMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MSdfFieldImage, MSdfFieldMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF field image memory failed");
        return false;
    }

    VkImageViewCreateInfo ScopedViewInfo{};
    ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedViewInfo.image = MSdfFieldImage;
    ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedViewInfo.format = VK_FORMAT_R8_UNORM;
    ScopedViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &MSdfFieldView) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF field image view failed");
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
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MSdfFieldSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF field sampler failed");
        return false;
    }

    if (!CreateBuffer(sizeof(SdfGpuBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &MSdfBlockBuffer,
                      &MSdfBlockMemory))
    {
        LogError("vulkan", "SDF parameter buffer failed");
        return false;
    }
    if (ScopedApi.vkMapMemory(MDevice, MSdfBlockMemory, 0, sizeof(SdfGpuBlock), 0, &MSdfBlockMapped) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF parameter buffer map failed");
        MSdfBlockMapped = nullptr;
        return false;
    }

    // Half-resolution screen mask: the sprite shader samples it instead of
    // marching per pixel, and the compute pass is scheduled by the render graph.
    const Extent2D ScopedMaskExtent{std::max(MExtent.width / 2u, 1u), std::max(MExtent.height / 2u, 1u)};
    if (!EnsureSdfMask(ScopedMaskExtent))
    {
        return false;
    }
    if (!CreateSdfComputeResources())
    {
        LogWarn("vulkan", "SDF mask compute pass unavailable; shadows run in the sprite shader");
    }
    return CreateVtFallbackResources();
}

bool VulkanRHI::CreateVtFallbackResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // 1x1 R32_UINT table with the missing sentinel and a 1x1 white page. They are
    // bound when no virtual texture is active so the descriptor is always valid;
    // the shader's enabled flag is zero in that case.
    VkImageCreateInfo ScopedTableInfo{};
    ScopedTableInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedTableInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedTableInfo.format = VK_FORMAT_R32_UINT;
    ScopedTableInfo.extent = {1, 1, 1};
    ScopedTableInfo.mipLevels = 1;
    ScopedTableInfo.arrayLayers = 1;
    ScopedTableInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedTableInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedTableInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedTableInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedTableInfo, nullptr, &MVtFallbackTable) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback table image failed");
        return false;
    }
    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MVtFallbackTable, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MVtFallbackTableMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MVtFallbackTable, MVtFallbackTableMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback table memory failed");
        return false;
    }
    VkImageViewCreateInfo ScopedTableView{};
    ScopedTableView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedTableView.image = MVtFallbackTable;
    ScopedTableView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedTableView.format = VK_FORMAT_R32_UINT;
    ScopedTableView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedTableView, nullptr, &MVtFallbackTableView) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback table view failed");
        return false;
    }
    VkSamplerCreateInfo ScopedSampler{};
    ScopedSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ScopedSampler.magFilter = VK_FILTER_NEAREST;
    ScopedSampler.minFilter = VK_FILTER_NEAREST;
    ScopedSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ScopedSampler.maxLod = 0.0f;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSampler, nullptr, &MVtFallbackTableSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback table sampler failed");
        return false;
    }

    VkImageCreateInfo ScopedPagesInfo{};
    ScopedPagesInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedPagesInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedPagesInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ScopedPagesInfo.extent = {1, 1, 1};
    ScopedPagesInfo.mipLevels = 1;
    ScopedPagesInfo.arrayLayers = 1;
    ScopedPagesInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedPagesInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedPagesInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedPagesInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedPagesInfo, nullptr, &MVtFallbackPages) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback page image failed");
        return false;
    }
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MVtFallbackPages, &ScopedRequirements);
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MVtFallbackPagesMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MVtFallbackPages, MVtFallbackPagesMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback page memory failed");
        return false;
    }
    VkImageViewCreateInfo ScopedPagesView{};
    ScopedPagesView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedPagesView.image = MVtFallbackPages;
    ScopedPagesView.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ScopedPagesView.format = VK_FORMAT_R8G8B8A8_UNORM;
    ScopedPagesView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedPagesView, nullptr, &MVtFallbackPagesView) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback page view failed");
        return false;
    }
    ScopedSampler.magFilter = VK_FILTER_LINEAR;
    ScopedSampler.minFilter = VK_FILTER_LINEAR;
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSampler, nullptr, &MVtFallbackPagesSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "VT fallback page sampler failed");
        return false;
    }

    // 1x1 r32ui storage image for the feedback descriptor array before the real
    // feedback images exist.
    VkImageCreateInfo ScopedFeedbackInfo{};
    ScopedFeedbackInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedFeedbackInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedFeedbackInfo.format = VK_FORMAT_R32_UINT;
    ScopedFeedbackInfo.extent = {1, 1, 1};
    ScopedFeedbackInfo.mipLevels = 1;
    ScopedFeedbackInfo.arrayLayers = 1;
    ScopedFeedbackInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedFeedbackInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedFeedbackInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ScopedFeedbackInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedFeedbackInfo, nullptr, &MVtFeedbackFallback) != VK_SUCCESS)
    {
        LogError("vulkan", "VT feedback fallback image failed");
        return false;
    }
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MVtFeedbackFallback, &ScopedRequirements);
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MVtFeedbackFallbackMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MVtFeedbackFallback, MVtFeedbackFallbackMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "VT feedback fallback memory failed");
        return false;
    }
    VkImageViewCreateInfo ScopedFeedbackView{};
    ScopedFeedbackView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedFeedbackView.image = MVtFeedbackFallback;
    ScopedFeedbackView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedFeedbackView.format = VK_FORMAT_R32_UINT;
    ScopedFeedbackView.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedFeedbackView, nullptr, &MVtFeedbackFallbackView) != VK_SUCCESS)
    {
        LogError("vulkan", "VT feedback fallback view failed");
        return false;
    }
    return true;
}

void VulkanRHI::DestroyVtFallbackResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        return;
    }
    DestroyVtFeedbackResources();
    if (MVtFeedbackFallbackView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MVtFeedbackFallbackView, nullptr);
        MVtFeedbackFallbackView = VK_NULL_HANDLE;
    }
    if (MVtFeedbackFallback != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MVtFeedbackFallback, nullptr);
        MVtFeedbackFallback = VK_NULL_HANDLE;
    }
    if (MVtFeedbackFallbackMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MVtFeedbackFallbackMemory, nullptr);
        MVtFeedbackFallbackMemory = VK_NULL_HANDLE;
    }
    if (MVtFallbackPagesSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MVtFallbackPagesSampler, nullptr);
        MVtFallbackPagesSampler = VK_NULL_HANDLE;
    }
    if (MVtFallbackPagesView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MVtFallbackPagesView, nullptr);
        MVtFallbackPagesView = VK_NULL_HANDLE;
    }
    if (MVtFallbackPages != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MVtFallbackPages, nullptr);
        MVtFallbackPages = VK_NULL_HANDLE;
    }
    if (MVtFallbackPagesMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MVtFallbackPagesMemory, nullptr);
        MVtFallbackPagesMemory = VK_NULL_HANDLE;
    }
    if (MVtFallbackTableSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MVtFallbackTableSampler, nullptr);
        MVtFallbackTableSampler = VK_NULL_HANDLE;
    }
    if (MVtFallbackTableView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MVtFallbackTableView, nullptr);
        MVtFallbackTableView = VK_NULL_HANDLE;
    }
    if (MVtFallbackTable != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MVtFallbackTable, nullptr);
        MVtFallbackTable = VK_NULL_HANDLE;
    }
    if (MVtFallbackTableMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MVtFallbackTableMemory, nullptr);
        MVtFallbackTableMemory = VK_NULL_HANDLE;
    }
}

void VulkanRHI::ApplyVirtualTexture(const FramePacket& InPacket)
{
    if (!InPacket.virtualTexture.IsValid())
    {
        return;
    }
    if (InPacket.virtualTexture.id == MActiveVirtualTexture.id)
    {
        return;
    }
    if (InPacket.virtualTexture.id == 0 || InPacket.virtualTexture.id >= MVirtualTextures.size() ||
        MVirtualTextures[InPacket.virtualTexture.id] == nullptr)
    {
        LogError("vulkan", "frame references an unknown virtual texture");
        return;
    }
    MActiveVirtualTexture = InPacket.virtualTexture;
    // The VT is bound once at startup in the current editor flow, before any VT
    // draw is recorded, so rewriting the shared sprite sets is safe here.
    WriteSdfDescriptors(MDescriptorSet);
    for (const std::unique_ptr<TextureArray>& ScopedArray : MTextureArrays)
    {
        if (ScopedArray != nullptr && ScopedArray->descriptor != VK_NULL_HANDLE)
        {
            WriteSdfDescriptors(ScopedArray->descriptor);
        }
    }
    LogInfo("vulkan", "virtual texture #{} bound to the sprite sets", InPacket.virtualTexture.id);
}

bool VulkanRHI::EnsureSdfMask(Extent2D InMaskExtent)
{
    if (InMaskExtent.width == 0 || InMaskExtent.height == 0)
    {
        return false;
    }
    if (MSdfMaskImage != VK_NULL_HANDLE && MSdfMaskExtent.width == InMaskExtent.width &&
        MSdfMaskExtent.height == InMaskExtent.height)
    {
        return true;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    DestroySdfMaskResources();

    VkImageCreateInfo ScopedImageInfo{};
    ScopedImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedImageInfo.format = VK_FORMAT_R16_SFLOAT;
    ScopedImageInfo.extent = {InMaskExtent.width, InMaskExtent.height, 1};
    ScopedImageInfo.mipLevels = 1;
    ScopedImageInfo.arrayLayers = 1;
    ScopedImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ScopedImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ScopedApi.vkCreateImage(MDevice, &ScopedImageInfo, nullptr, &MSdfMaskImage) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF mask image creation failed");
        return false;
    }
    VkMemoryRequirements ScopedRequirements{};
    ScopedApi.vkGetImageMemoryRequirements(MDevice, MSdfMaskImage, &ScopedRequirements);
    VkMemoryAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ScopedAllocate.allocationSize = ScopedRequirements.size;
    ScopedAllocate.memoryTypeIndex = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MMemoryProperties.memoryTypeCount; ++ScopedI)
    {
        if ((ScopedRequirements.memoryTypeBits & (1u << ScopedI)) != 0 &&
            (MMemoryProperties.memoryTypes[ScopedI].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            ScopedAllocate.memoryTypeIndex = ScopedI;
            break;
        }
    }
    if (ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MSdfMaskMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MSdfMaskImage, MSdfMaskMemory, 0) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF mask memory failed");
        return false;
    }
    VkImageViewCreateInfo ScopedViewInfo{};
    ScopedViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ScopedViewInfo.image = MSdfMaskImage;
    ScopedViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ScopedViewInfo.format = VK_FORMAT_R16_SFLOAT;
    ScopedViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (ScopedApi.vkCreateImageView(MDevice, &ScopedViewInfo, nullptr, &MSdfMaskView) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF mask view failed");
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
    if (ScopedApi.vkCreateSampler(MDevice, &ScopedSamplerInfo, nullptr, &MSdfMaskSampler) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF mask sampler failed");
        return false;
    }

    // The mask lives in GENERAL: the compute pass writes it as a storage image
    // and the sprite pass samples it, both without layout transitions.
    const VkCommandBuffer ScopedCommand = BeginOneShot();
    if (ScopedCommand != VK_NULL_HANDLE)
    {
        TransitionImage(ScopedCommand, MSdfMaskImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        EndOneShot(ScopedCommand);
    }
    MSdfMaskExtent = VkExtent2D{InMaskExtent.width, InMaskExtent.height};
    MSdfMaskValid = true;
    LogInfo("vulkan", "SDF shadow mask: {}x{} (r16f, compute + sampled)", InMaskExtent.width, InMaskExtent.height);

    // Descriptor sets that sample the mask must point at the new view.
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
    UpdateSdfComputeSets(MSdfMaskView);
    return true;
}

void VulkanRHI::DestroySdfMaskResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE)
    {
        return;
    }
    if (MSdfMaskSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MSdfMaskSampler, nullptr);
        MSdfMaskSampler = VK_NULL_HANDLE;
    }
    if (MSdfMaskView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MSdfMaskView, nullptr);
        MSdfMaskView = VK_NULL_HANDLE;
    }
    if (MSdfMaskImage != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MSdfMaskImage, nullptr);
        MSdfMaskImage = VK_NULL_HANDLE;
    }
    if (MSdfMaskMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MSdfMaskMemory, nullptr);
        MSdfMaskMemory = VK_NULL_HANDLE;
    }
    MSdfMaskExtent = {};
    MSdfMaskValid = false;
}

void VulkanRHI::UpdateSdfComputeSets(VkImageView InMaskView) const
{
    if (MSdfComputeSetLayout == VK_NULL_HANDLE || InMaskView == VK_NULL_HANDLE)
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < kSdfMaskFrames; ++ScopedFrame)
    {
        if (MSdfComputeSets[ScopedFrame] == VK_NULL_HANDLE || MSdfComputeBuffers[ScopedFrame] == VK_NULL_HANDLE)
        {
            continue;
        }
        VkDescriptorImageInfo ScopedFieldInfo{};
        ScopedFieldInfo.sampler = MSdfFieldSampler;
        ScopedFieldInfo.imageView = MSdfFieldView;
        ScopedFieldInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo ScopedMaskInfo{};
        ScopedMaskInfo.imageView = InMaskView;
        ScopedMaskInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo ScopedBufferInfo{};
        ScopedBufferInfo.buffer = MSdfComputeBuffers[ScopedFrame];
        ScopedBufferInfo.offset = 0;
        ScopedBufferInfo.range = kSdfShadowBlockBytes;
        std::array<VkWriteDescriptorSet, 3> ScopedWrites{};
        ScopedWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[0].dstSet = MSdfComputeSets[ScopedFrame];
        ScopedWrites[0].dstBinding = 0;
        ScopedWrites[0].descriptorCount = 1;
        ScopedWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ScopedWrites[0].pImageInfo = &ScopedFieldInfo;
        ScopedWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[1].dstSet = MSdfComputeSets[ScopedFrame];
        ScopedWrites[1].dstBinding = 1;
        ScopedWrites[1].descriptorCount = 1;
        ScopedWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        ScopedWrites[1].pImageInfo = &ScopedMaskInfo;
        ScopedWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ScopedWrites[2].dstSet = MSdfComputeSets[ScopedFrame];
        ScopedWrites[2].dstBinding = 2;
        ScopedWrites[2].descriptorCount = 1;
        ScopedWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ScopedWrites[2].pBufferInfo = &ScopedBufferInfo;
        ScopedApi.vkUpdateDescriptorSets(MDevice, static_cast<std::uint32_t>(ScopedWrites.size()), ScopedWrites.data(), 0,
                                        nullptr);
    }
}

bool VulkanRHI::CreateSdfComputeResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    // r16f is the mask format; when the device cannot use it as a storage image,
    // the sprite shader keeps its per-pixel fallback instead of failing startup.
    VkFormatProperties ScopedFormat{};
    ScopedApi.vkGetPhysicalDeviceFormatProperties(MPhysical, VK_FORMAT_R16_SFLOAT, &ScopedFormat);
    if ((ScopedFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
    {
        LogWarn("vulkan", "r16f storage images are unsupported; SDF shadows use the per-pixel fallback");
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 3> ScopedBindings{};
    ScopedBindings[0].binding = 0;
    ScopedBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBindings[0].descriptorCount = 1;
    ScopedBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedBindings[1].binding = 1;
    ScopedBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedBindings[1].descriptorCount = 1;
    ScopedBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedBindings[2].binding = 2;
    ScopedBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ScopedBindings[2].descriptorCount = 1;
    ScopedBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.bindingCount = static_cast<std::uint32_t>(ScopedBindings.size());
    ScopedLayoutInfo.pBindings = ScopedBindings.data();
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedLayoutInfo, nullptr, &MSdfComputeSetLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute set layout failed");
        return false;
    }

    std::array<VkDescriptorPoolSize, 3> ScopedPoolSizes{};
    ScopedPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedPoolSizes[0].descriptorCount = kSdfMaskFrames;
    ScopedPoolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedPoolSizes[1].descriptorCount = kSdfMaskFrames;
    ScopedPoolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ScopedPoolSizes[2].descriptorCount = kSdfMaskFrames;
    VkDescriptorPoolCreateInfo ScopedPoolInfo{};
    ScopedPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ScopedPoolInfo.maxSets = kSdfMaskFrames;
    ScopedPoolInfo.poolSizeCount = static_cast<std::uint32_t>(ScopedPoolSizes.size());
    ScopedPoolInfo.pPoolSizes = ScopedPoolSizes.data();
    if (ScopedApi.vkCreateDescriptorPool(MDevice, &ScopedPoolInfo, nullptr, &MSdfComputePool) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute pool failed");
        return false;
    }
    std::array<VkDescriptorSetLayout, kSdfMaskFrames> ScopedLayouts{};
    ScopedLayouts.fill(MSdfComputeSetLayout);
    VkDescriptorSetAllocateInfo ScopedAllocate{};
    ScopedAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ScopedAllocate.descriptorPool = MSdfComputePool;
    ScopedAllocate.descriptorSetCount = kSdfMaskFrames;
    ScopedAllocate.pSetLayouts = ScopedLayouts.data();
    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedAllocate, MSdfComputeSets.data()) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute sets failed");
        return false;
    }
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < kSdfMaskFrames; ++ScopedFrame)
    {
        if (!CreateBuffer(kSdfShadowBlockBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &MSdfComputeBuffers[ScopedFrame], &MSdfComputeMemories[ScopedFrame]))
        {
            LogError("vulkan", "SDF compute block buffer failed");
            return false;
        }
        if (ScopedApi.vkMapMemory(MDevice, MSdfComputeMemories[ScopedFrame], 0, kSdfShadowBlockBytes, 0,
                                  &MSdfComputeMapped[ScopedFrame]) != VK_SUCCESS)
        {
            MSdfComputeMapped[ScopedFrame] = nullptr;
            return false;
        }
    }
    UpdateSdfComputeSets(MSdfMaskView);

    VkPipelineLayoutCreateInfo ScopedPipelineLayout{};
    ScopedPipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedPipelineLayout.setLayoutCount = 1;
    ScopedPipelineLayout.pSetLayouts = &MSdfComputeSetLayout;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedPipelineLayout, nullptr, &MSdfComputeLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute pipeline layout failed");
        return false;
    }
    VkShaderModule ScopedModule = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ScopedModuleInfo{};
    ScopedModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ScopedModuleInfo.codeSize = Shaders::sdf_shadow.size() * sizeof(std::uint32_t);
    ScopedModuleInfo.pCode = Shaders::sdf_shadow.data();
    if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedModuleInfo, nullptr, &ScopedModule) != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute shader module failed");
        return false;
    }
    VkComputePipelineCreateInfo ScopedPipelineInfo{};
    ScopedPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ScopedPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedPipelineInfo.stage.module = ScopedModule;
    ScopedPipelineInfo.stage.pName = "main";
    ScopedPipelineInfo.layout = MSdfComputeLayout;
    const VkResult ScopedResult = ScopedApi.vkCreateComputePipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo,
                                                                     nullptr, &MSdfComputePipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedModule, nullptr);
    if (ScopedResult != VK_SUCCESS)
    {
        LogError("vulkan", "SDF compute pipeline failed: {}", static_cast<int>(ScopedResult));
        return false;
    }
    return true;
}

void VulkanRHI::DestroySdfShadowResources() noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    LogInfo("vulkan", "EnsureSdfMask enter: tableView={} pagesView={}",
            static_cast<const void*>(MVtFallbackTableView), static_cast<const void*>(MVtFallbackPagesView));
    DestroySdfMaskResources();
    if (MSdfComputePipeline != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipeline(MDevice, MSdfComputePipeline, nullptr);
        MSdfComputePipeline = VK_NULL_HANDLE;
    }
    if (MSdfComputeLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyPipelineLayout(MDevice, MSdfComputeLayout, nullptr);
        MSdfComputeLayout = VK_NULL_HANDLE;
    }
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < kSdfMaskFrames; ++ScopedFrame)
    {
        if (MSdfComputeMapped[ScopedFrame] != nullptr && MSdfComputeMemories[ScopedFrame] != VK_NULL_HANDLE)
        {
            ScopedApi.vkUnmapMemory(MDevice, MSdfComputeMemories[ScopedFrame]);
            MSdfComputeMapped[ScopedFrame] = nullptr;
        }
        if (MSdfComputeBuffers[ScopedFrame] != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyBuffer(MDevice, MSdfComputeBuffers[ScopedFrame], nullptr);
            MSdfComputeBuffers[ScopedFrame] = VK_NULL_HANDLE;
        }
        if (MSdfComputeMemories[ScopedFrame] != VK_NULL_HANDLE)
        {
            ScopedApi.vkFreeMemory(MDevice, MSdfComputeMemories[ScopedFrame], nullptr);
            MSdfComputeMemories[ScopedFrame] = VK_NULL_HANDLE;
        }
    }
    if (MSdfComputePool != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorPool(MDevice, MSdfComputePool, nullptr);
        MSdfComputePool = VK_NULL_HANDLE;
    }
    if (MSdfComputeSetLayout != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyDescriptorSetLayout(MDevice, MSdfComputeSetLayout, nullptr);
        MSdfComputeSetLayout = VK_NULL_HANDLE;
    }
    if (MSdfBlockMapped != nullptr && MSdfBlockMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkUnmapMemory(MDevice, MSdfBlockMemory);
        MSdfBlockMapped = nullptr;
    }
    if (MSdfBlockBuffer != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyBuffer(MDevice, MSdfBlockBuffer, nullptr);
        MSdfBlockBuffer = VK_NULL_HANDLE;
    }
    if (MSdfBlockMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MSdfBlockMemory, nullptr);
        MSdfBlockMemory = VK_NULL_HANDLE;
    }
    if (MSdfFieldSampler != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroySampler(MDevice, MSdfFieldSampler, nullptr);
        MSdfFieldSampler = VK_NULL_HANDLE;
    }
    if (MSdfFieldView != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImageView(MDevice, MSdfFieldView, nullptr);
        MSdfFieldView = VK_NULL_HANDLE;
    }
    if (MSdfFieldImage != VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyImage(MDevice, MSdfFieldImage, nullptr);
        MSdfFieldImage = VK_NULL_HANDLE;
    }
    if (MSdfFieldMemory != VK_NULL_HANDLE)
    {
        ScopedApi.vkFreeMemory(MDevice, MSdfFieldMemory, nullptr);
        MSdfFieldMemory = VK_NULL_HANDLE;
    }
    MSdfFieldInitialized = false;
    MSdfFieldUploadPending = false;
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
    ScopedWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
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
    std::array<VkWriteDescriptorSet, 6> ScopedFinalWrites{};
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
    ScopedApi.vkUpdateDescriptorSets(MDevice, static_cast<std::uint32_t>(ScopedFinalWrites.size()),
                                    ScopedFinalWrites.data(), 0, nullptr);
}

void VulkanRHI::UploadSdfField()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    VkBuffer ScopedStaging = VK_NULL_HANDLE;
    VkDeviceMemory ScopedStagingMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(sizeof(MSdfFieldTexels), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ScopedStaging,
                      &ScopedStagingMemory))
    {
        MSdfFieldUploadPending = false;
        return;
    }
    void* ScopedMapped = nullptr;
    if (ScopedApi.vkMapMemory(MDevice, ScopedStagingMemory, 0, sizeof(MSdfFieldTexels), 0, &ScopedMapped) ==
        VK_SUCCESS)
    {
        std::memcpy(ScopedMapped, MSdfFieldTexels.data(), MSdfFieldTexels.size());
        ScopedApi.vkUnmapMemory(MDevice, ScopedStagingMemory);
    }
    const VkCommandBuffer ScopedCommand = BeginOneShot();
    if (ScopedCommand != VK_NULL_HANDLE)
    {
        TransitionImage(ScopedCommand, MSdfFieldImage,
                        MSdfFieldInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy ScopedCopy{};
        ScopedCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        ScopedCopy.imageExtent = {kSdfFieldSize, kSdfFieldSize, 1};
        ScopedApi.vkCmdCopyBufferToImage(ScopedCommand, ScopedStaging, MSdfFieldImage,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedCopy);
        TransitionImage(ScopedCommand, MSdfFieldImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        EndOneShot(ScopedCommand);
        MSdfFieldInitialized = true;
    }
    ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
    ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);
    MSdfFieldUploadPending = false;
}

void VulkanRHI::ApplySdfLighting(const SdfLightingFrame& InFrame)
{
    if (MSdfBlockMapped == nullptr)
    {
        return;
    }
    SdfGpuBlock ScopedBlock;
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
    ScopedBlock.params = Vector4D{static_cast<float>(std::clamp(InFrame.steps, 1u, 64u)),
                                  static_cast<float>(ScopedOccluderCount), std::max(InFrame.occlusionRadius, 1.0f),
                                  std::clamp(InFrame.occlusionStrength, 0.0f, 1.0f)};
    const bool ScopedEnabled = InFrame.enabled && InFrame.field != nullptr && InFrame.width > 1 &&
                               InFrame.height > 1 && ScopedExtentX > 0.0f && ScopedExtentY > 0.0f;
    const bool ScopedMaskActive = ScopedEnabled && MSdfMaskValid;
    ScopedBlock.flags = Vector4D{kSdfEncodeScale, ScopedEnabled ? 1.0f : 0.0f, ScopedMaskActive ? 1.0f : 0.0f, 0.0f};
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedOccluderCount; ++ScopedI)
    {
        ScopedBlock.occluders[ScopedI * 2] = InFrame.occluders[ScopedI].Shape;
        ScopedBlock.occluders[ScopedI * 2 + 1] = InFrame.occluders[ScopedI].Extents;
    }
    *static_cast<SdfGpuBlock*>(MSdfBlockMapped) = ScopedBlock;
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
                const float ScopedDistance = InFrame.field[static_cast<std::size_t>(ScopedSourceY) * InFrame.width +
                                                          ScopedSourceX];
                const float ScopedNormalized = std::clamp(ScopedDistance / kSdfEncodeScale, 0.0f, 1.0f);
                MSdfFieldTexels[static_cast<std::size_t>(ScopedY) * kSdfFieldSize + ScopedX] =
                    static_cast<std::uint8_t>(std::lround(ScopedNormalized * 255.0f));
            }
        }
        MSdfFieldVersion = InFrame.fieldVersion;
        MSdfFieldUploadPending = true;
    }
    if (MSdfFieldUploadPending)
    {
        UploadSdfField();
    }
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
    VkBuffer ScopedStaging = VK_NULL_HANDLE;
    VkDeviceMemory ScopedStagingMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(ScopedBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ScopedStaging,
                      &ScopedStagingMemory))
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    void* ScopedMapped = nullptr;
    if (ScopedApi.vkMapMemory(MDevice, ScopedStagingMemory, 0, ScopedBytes, 0, &ScopedMapped) != VK_SUCCESS)
    {
        ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
        ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);
        return false;
    }
    std::memcpy(ScopedMapped, InEntries.data(), static_cast<std::size_t>(ScopedBytes));
    ScopedApi.vkUnmapMemory(MDevice, ScopedStagingMemory);

    const VkCommandBuffer ScopedCommand = BeginOneShot();
    if (ScopedCommand == VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
        ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);
        return false;
    }
    TransitionImage(ScopedCommand, ScopedTexture->table, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy ScopedCopy{};
    ScopedCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ScopedCopy.imageExtent = {InWidth, InHeight, 1};
    ScopedApi.vkCmdCopyBufferToImage(ScopedCommand, ScopedStaging, ScopedTexture->table,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedCopy);
    TransitionImage(ScopedCommand, ScopedTexture->table, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EndOneShot(ScopedCommand);
    ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
    ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);
    return true;
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
    const bool ScopedValid = InLayer < ScopedTexture->layerCount && InSlotX < ScopedTilesPerAxis &&
                             InSlotY < ScopedTilesPerAxis && InWidth > 0 && InHeight > 0 &&
                             InWidth <= ScopedTexture->tileSize && InHeight <= ScopedTexture->tileSize &&
                             InRowPitch >= InWidth * 4u && InPixels.size() >= static_cast<std::size_t>(InRowPitch) * InHeight;
    if (!ScopedValid)
    {
        return false;
    }
    VkBuffer ScopedStaging = VK_NULL_HANDLE;
    VkDeviceMemory ScopedStagingMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(InPixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ScopedStaging,
                      &ScopedStagingMemory))
    {
        return false;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    void* ScopedMapped = nullptr;
    if (ScopedApi.vkMapMemory(MDevice, ScopedStagingMemory, 0, InPixels.size(), 0, &ScopedMapped) != VK_SUCCESS)
    {
        ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
        ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);
        return false;
    }
    std::memcpy(ScopedMapped, InPixels.data(), InPixels.size());
    ScopedApi.vkUnmapMemory(MDevice, ScopedStagingMemory);

    const VkCommandBuffer ScopedCommand = BeginOneShot();
    if (ScopedCommand == VK_NULL_HANDLE)
    {
        ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
        ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);
        return false;
    }
    TransitionImage(ScopedCommand, ScopedTexture->pages, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ScopedTexture->layerCount);
    VkBufferImageCopy ScopedCopy{};
    ScopedCopy.bufferRowLength = InRowPitch / 4u;
    ScopedCopy.bufferImageHeight = InHeight;
    ScopedCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, InLayer, 1};
    ScopedCopy.imageOffset = {static_cast<std::int32_t>(InSlotX * ScopedTexture->tileSize),
                              static_cast<std::int32_t>(InSlotY * ScopedTexture->tileSize), 0};
    ScopedCopy.imageExtent = {InWidth, InHeight, 1};
    ScopedApi.vkCmdCopyBufferToImage(ScopedCommand, ScopedStaging, ScopedTexture->pages,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedCopy);
    TransitionImage(ScopedCommand, ScopedTexture->pages, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ScopedTexture->layerCount);
    EndOneShot(ScopedCommand);
    ScopedApi.vkDestroyBuffer(MDevice, ScopedStaging, nullptr);
    ScopedApi.vkFreeMemory(MDevice, ScopedStagingMemory, nullptr);
    return true;
}

std::uint32_t VulkanRHI::ReadVirtualTextureFeedback(std::span<std::uint32_t> OutTiles)
{
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(MVtFeedbackCount, static_cast<std::uint32_t>(OutTiles.size()));
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
    const std::size_t ScopedTexels =
        static_cast<std::size_t>(MVtFeedbackExtent.width) * MVtFeedbackExtent.height;
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
        ScopedInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ScopedInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (ScopedApi.vkCreateImage(MDevice, &ScopedInfo, nullptr, &MVtFeedbackImages[ScopedI]) != VK_SUCCESS)
        {
            LogError("vulkan", "VT feedback image failed");
            return false;
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

bool VulkanRHI::CreatePipeline()
{
    Vk::Api& ScopedApi = Vk::GetApi();

    VkPushConstantRange ScopedPushRange{};
    ScopedPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    ScopedPushRange.offset = 0;
    ScopedPushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MDescriptorLayout;
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedPushRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MPipelineLayout) != VK_SUCCESS)
    {
        LogError("vulkan", "vkCreatePipelineLayout failed");
        return false;
    }

    return CreatePipelineForPass(MRenderPass, &MPipeline);
}

// One sprite pipeline per render pass. The pass is a parameter because the
bool VulkanRHI::CreatePipelineForPass(VkRenderPass InRenderPass, VkPipeline* OutPipeline)
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
    const VkDeviceSize ScopedTotalBytes = ScopedSliceBytes * MFramesInFlight;

    if (!CreateBuffer(ScopedTotalBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &MInstanceBuffer,
                      &MInstanceMemory) ||
        Vk::GetApi().vkMapMemory(MDevice, MInstanceMemory, 0, VK_WHOLE_SIZE, 0, &MInstanceMapped) != VK_SUCCESS)
    {
        LogError("vulkan", "instance ring creation failed ({} bytes)", ScopedTotalBytes);
        return false;
    }

    MTransientCapacity = static_cast<std::size_t>(ScopedSliceBytes);
    MInstanceSliceValid.fill(false);
    LogInfo("vulkan", "instance ring: {} items/frame x {} frames = {:.1f} MB", MInstanceCapacity, MFramesInFlight,
            static_cast<double>(ScopedTotalBytes) / (1024.0 * 1024.0));
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
                                                 { return InLeft.offset < InRight.offset; });
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

    VkSubpassDescription ScopedSubpass{};
    ScopedSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    ScopedSubpass.colorAttachmentCount = 1;
    ScopedSubpass.pColorAttachments = &ScopedColorRef;

    VkSubpassDependency ScopedDependency{};
    ScopedDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    ScopedDependency.dstSubpass = 0;
    ScopedDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    ScopedDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    ScopedDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ScopedCreateInfo{};
    ScopedCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ScopedCreateInfo.attachmentCount = 1;
    ScopedCreateInfo.pAttachments = &ScopedColor;
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

    VkDescriptorSetLayoutBinding ScopedBinding{};
    ScopedBinding.binding = 0;
    ScopedBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ScopedBinding.descriptorCount = 1;
    ScopedBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo ScopedSetInfo{};
    ScopedSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetInfo.bindingCount = 1;
    ScopedSetInfo.pBindings = &ScopedBinding;
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
    const VkShaderModule ScopedVertex = ScopedMakeModule(Shaders::fullscreen_vert.data(),
                                                         Shaders::fullscreen_vert.size() * sizeof(std::uint32_t));
    const VkShaderModule ScopedFragment =
        ScopedMakeModule(Shaders::accessibility_composite.data(),
                         Shaders::accessibility_composite.size() * sizeof(std::uint32_t));
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
    (void)CreateAccessibilityFlashResources();
    LogInfo("vulkan", "accessibility composite active (colour vision + flash gain per frame)");
    return true;
}

bool VulkanRHI::CreateAccessibilityFlashResources()
{
    Vk::Api& ScopedApi = Vk::GetApi();
    if (MDevice == VK_NULL_HANDLE || !MAccessibilityReady)
    {
        return false;
    }

    VkDescriptorSetLayoutBinding ScopedBindings[2]{};
    ScopedBindings[0].binding = 0;
    ScopedBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedBindings[0].descriptorCount = 1;
    ScopedBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedBindings[1].binding = 1;
    ScopedBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ScopedBindings[1].descriptorCount = 1;
    ScopedBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo ScopedSetInfo{};
    ScopedSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ScopedSetInfo.bindingCount = 2;
    ScopedSetInfo.pBindings = ScopedBindings;
    if (ScopedApi.vkCreateDescriptorSetLayout(MDevice, &ScopedSetInfo, nullptr, &MAccessibilityFlashSetLayout) !=
        VK_SUCCESS)
    {
        MAccessibilityFlashSetLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange ScopedRange{};
    ScopedRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedRange.offset = 0;
    ScopedRange.size = static_cast<std::uint32_t>(sizeof(float) * 4);
    VkPipelineLayoutCreateInfo ScopedLayoutInfo{};
    ScopedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ScopedLayoutInfo.setLayoutCount = 1;
    ScopedLayoutInfo.pSetLayouts = &MAccessibilityFlashSetLayout;
    ScopedLayoutInfo.pushConstantRangeCount = 1;
    ScopedLayoutInfo.pPushConstantRanges = &ScopedRange;
    if (ScopedApi.vkCreatePipelineLayout(MDevice, &ScopedLayoutInfo, nullptr, &MAccessibilityFlashLayout) != VK_SUCCESS)
    {
        MAccessibilityFlashLayout = VK_NULL_HANDLE;
        return false;
    }

    VkShaderModuleCreateInfo ScopedModuleInfo{};
    ScopedModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ScopedModuleInfo.codeSize = Shaders::accessibility_flash.size() * sizeof(std::uint32_t);
    ScopedModuleInfo.pCode = Shaders::accessibility_flash.data();
    VkShaderModule ScopedModule = VK_NULL_HANDLE;
    if (ScopedApi.vkCreateShaderModule(MDevice, &ScopedModuleInfo, nullptr, &ScopedModule) != VK_SUCCESS)
    {
        return false;
    }
    VkComputePipelineCreateInfo ScopedPipelineInfo{};
    ScopedPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ScopedPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ScopedPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ScopedPipelineInfo.stage.module = ScopedModule;
    ScopedPipelineInfo.stage.pName = "main";
    ScopedPipelineInfo.layout = MAccessibilityFlashLayout;
    const VkResult ScopedResult =
        ScopedApi.vkCreateComputePipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo, nullptr,
                                           &MAccessibilityFlashPipeline);
    ScopedApi.vkDestroyShaderModule(MDevice, ScopedModule, nullptr);
    if (ScopedResult != VK_SUCCESS)
    {
        MAccessibilityFlashPipeline = VK_NULL_HANDLE;
        return false;
    }
    MAccessibilityFlashReady = true;
    LogInfo("vulkan", "per-pixel flash guard active");
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
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &ScopedBarrier);

    const float ScopedParams[4] = {InAccessibility.flashThreshold, InAccessibility.flashGain, 1.0f, 0.0f};
    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, MAccessibilityFlashPipeline);
    (void)ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                            MAccessibilityFlashLayout, 0, 1, &InSet, 0, nullptr);
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MAccessibilityFlashLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 sizeof(ScopedParams), ScopedParams);
    ScopedApi.vkCmdDispatch(InCommandBuffer, (InExtent.width + 7u) / 8u, (InExtent.height + 7u) / 8u, 1);

    ScopedBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    ScopedBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ScopedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ScopedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ScopedApi.vkCmdPipelineBarrier(InCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &ScopedBarrier);
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
    ScopedImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_STORAGE_BIT;
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
    VkFramebufferCreateInfo ScopedFramebuffer{};
    ScopedFramebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ScopedFramebuffer.renderPass = MRenderPass;
    ScopedFramebuffer.attachmentCount = 1;
    ScopedFramebuffer.pAttachments = &MAccessibilitySwapView;
    ScopedFramebuffer.width = MExtent.width;
    ScopedFramebuffer.height = MExtent.height;
    ScopedFramebuffer.layers = 1;
    if (ScopedAllocate.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
        ScopedApi.vkAllocateMemory(MDevice, &ScopedAllocate, nullptr, &MAccessibilitySwapMemory) != VK_SUCCESS ||
        ScopedApi.vkBindImageMemory(MDevice, MAccessibilitySwapImage, MAccessibilitySwapMemory, 0) != VK_SUCCESS ||
        ScopedApi.vkCreateImageView(MDevice, &ScopedView, nullptr, &MAccessibilitySwapView) != VK_SUCCESS ||
        ScopedApi.vkCreateFramebuffer(MDevice, &ScopedFramebuffer, nullptr, &MAccessibilitySwapFramebuffer) != VK_SUCCESS)
    {
        LogWarn("vulkan", "accessibility swapchain composite: resources failed; direct path stays");
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
    const VkShaderModule ScopedVertex = ScopedMakeModule(Shaders::fullscreen_vert.data(),
                                                         Shaders::fullscreen_vert.size() * sizeof(std::uint32_t));
    const VkShaderModule ScopedFragment =
        ScopedMakeModule(Shaders::accessibility_composite.data(),
                         Shaders::accessibility_composite.size() * sizeof(std::uint32_t));
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
    if (MAccessibilityFlashReady)
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
            ScopedFlashAllocate.memoryTypeIndex = FindMemoryType(MMemoryProperties,
                                                                 ScopedFlashRequirements.memoryTypeBits,
                                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            ScopedFlashView.image = MAccessibilitySwapFlashImage;
        }
        const bool ScopedFlashOk =
            ScopedFlashImage && ScopedFlashAllocate.memoryTypeIndex != std::numeric_limits<std::uint32_t>::max() &&
            ScopedApi.vkAllocateMemory(MDevice, &ScopedFlashAllocate, nullptr, &MAccessibilitySwapFlashMemory) ==
                VK_SUCCESS &&
            ScopedApi.vkBindImageMemory(MDevice, MAccessibilitySwapFlashImage, MAccessibilitySwapFlashMemory, 0) ==
                VK_SUCCESS &&
            ScopedApi.vkCreateImageView(MDevice, &ScopedFlashView, nullptr,
                                        &MAccessibilitySwapFlashView) == VK_SUCCESS;
        if (ScopedFlashOk)
        {
            VkDescriptorSetAllocateInfo ScopedSetInfo{};
            ScopedSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ScopedSetInfo.descriptorPool = MAccessibilityPool;
            ScopedSetInfo.descriptorSetCount = 1;
            ScopedSetInfo.pSetLayouts = &MAccessibilityFlashSetLayout;
            if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedSetInfo, &MAccessibilitySwapFlashSet) == VK_SUCCESS)
            {
                VkDescriptorImageInfo ScopedColor{};
                ScopedColor.imageView = MAccessibilitySwapView;
                ScopedColor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkDescriptorImageInfo ScopedHistory{};
                ScopedHistory.imageView = MAccessibilitySwapFlashView;
                ScopedHistory.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkWriteDescriptorSet ScopedWrites[2]{};
                for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
                {
                    ScopedWrites[ScopedIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    ScopedWrites[ScopedIndex].dstSet = MAccessibilitySwapFlashSet;
                    ScopedWrites[ScopedIndex].dstBinding = ScopedIndex;
                    ScopedWrites[ScopedIndex].descriptorCount = 1;
                    ScopedWrites[ScopedIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                }
                ScopedWrites[0].pImageInfo = &ScopedColor;
                ScopedWrites[1].pImageInfo = &ScopedHistory;
                ScopedApi.vkUpdateDescriptorSets(MDevice, 2, ScopedWrites, 0, nullptr);
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
    const VkShaderModule ScopedVertex = ScopedMakeModule(Shaders::fullscreen_vert.data(),
                                                         Shaders::fullscreen_vert.size() * sizeof(std::uint32_t));
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
    ScopedPipelineInfo.layout = MPostFxLayout;
    ScopedPipelineInfo.renderPass = MRenderPass;
    ScopedPipelineInfo.subpass = 0;
    const VkResult ScopedResult = ScopedApi.vkCreateGraphicsPipelines(MDevice, VK_NULL_HANDLE, 1, &ScopedPipelineInfo,
                                                                     nullptr, &MPostFxPipeline);
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
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &ScopedBarrier);

    VkClearValue ScopedClear{};
    ScopedClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo ScopedBegin{};
    ScopedBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBegin.renderPass = MRenderPass;
    ScopedBegin.framebuffer = MFramebuffers[MImageIndex];
    ScopedBegin.renderArea.extent = {InExtent.width, InExtent.height};
    ScopedBegin.clearValueCount = 1;
    ScopedBegin.pClearValues = &ScopedClear;
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
    const ColorMatrix3 ScopedMatrix = ColorVisionMatrix(static_cast<ColorVisionDeficiency>(
        std::min<std::uint32_t>(InSettings.colorVision, 3u)));
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
        if (MAccessibilitySwapFramebuffer != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyFramebuffer(MDevice, MAccessibilitySwapFramebuffer, nullptr);
        }
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
    MAccessibilitySwapFlashSet = VK_NULL_HANDLE;
    MAccessibilitySwapFlashView = VK_NULL_HANDLE;
    MAccessibilitySwapFlashImage = VK_NULL_HANDLE;
    MAccessibilitySwapFlashMemory = VK_NULL_HANDLE;
    MAccessibilitySwapPipeline = VK_NULL_HANDLE;
    MAccessibilitySwapFramebuffer = VK_NULL_HANDLE;
    MAccessibilitySwapView = VK_NULL_HANDLE;
    MAccessibilitySwapImage = VK_NULL_HANDLE;
    MAccessibilitySwapMemory = VK_NULL_HANDLE;
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
    const bool ScopedCvd =
        (ScopedFlags & AccessibilityColorVision) != 0 && InPacket.accessibility.colorVision != 0;
    const bool ScopedGain =
        (ScopedFlags & AccessibilityFlashGuard) != 0 && InPacket.accessibility.flashGain < 0.999f;
    return ScopedCvd || ScopedGain;
}

void VulkanRHI::RecordAccessibilityComposite(VkCommandBuffer InCommandBuffer, VkRenderPass InRenderPass,
                                             VkFramebuffer InFramebuffer, VkPipeline InPipeline,
                                             VkDescriptorSet InSet, Extent2D InExtent,
                                             const FrameAccessibility& InAccessibility) noexcept
{
    Vk::Api& ScopedApi = Vk::GetApi();
    const std::uint32_t ScopedFlags = InAccessibility.flags;
    AccessibilityCompositePush ScopedPush{};
    const ColorMatrix3 ScopedMatrix = ColorVisionMatrix(static_cast<ColorVisionDeficiency>(
        std::min<std::uint32_t>(InAccessibility.colorVision, 3u)));
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

    VkRenderPassBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBeginInfo.renderPass = InRenderPass;
    ScopedBeginInfo.framebuffer = InFramebuffer;
    ScopedBeginInfo.renderArea.extent = {InExtent.width, InExtent.height};
    VkClearValue ScopedClear{};
    ScopedBeginInfo.clearValueCount = 1;
    ScopedBeginInfo.pClearValues = &ScopedClear;
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
    const void* ScopedDefaultVertex =
        ScopedSpriteDomain ? static_cast<const void*>(Shaders::quad_vert.data())
                           : static_cast<const void*>(Shaders::mesh_vert.data());
    const std::size_t ScopedDefaultVertexBytes = ScopedSpriteDomain
                                                     ? Shaders::quad_vert.size() * sizeof(std::uint32_t)
                                                     : Shaders::mesh_vert.size() * sizeof(std::uint32_t);

    const void* ScopedVertexData = InDesc.vertexSpirv.empty() ? ScopedDefaultVertex
                                                              : static_cast<const void*>(InDesc.vertexSpirv.data());
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
    if (!CreateGraphicsPipelineForPass(MRenderPass, ScopedLayout, ScopedVertexData, ScopedVertexBytes, ScopedFragmentData,
                                       ScopedFragmentBytes, &ScopedPipeline->mainPass) ||
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

    // The media path calls this at most once per frame. Waiting for the device
    ScopedApi.vkDeviceWaitIdle(MDevice);
    std::memcpy(MUploadMapped, InData.data(), static_cast<std::size_t>(ScopedExpected));

    const VkCommandBuffer ScopedCommand = BeginOneShot();
    TransitionImage(ScopedCommand, ScopedArray->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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
    ScopedApi.vkCmdCopyBufferToImage(ScopedCommand, MUploadBuffer, ScopedArray->image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ScopedRegion);
    TransitionImage(ScopedCommand, ScopedArray->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ScopedArray->layers, ScopedArray->mipLevels);
    EndOneShot(ScopedCommand);
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
    if (ScopedTarget->format != VK_FORMAT_R8G8B8A8_UNORM && ScopedTarget->format != VK_FORMAT_R8G8B8A8_SRGB)
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
    DestroyAccessibilitySwapchainResources();
    Vk::Api& ScopedApi = Vk::GetApi();
    for (VkFramebuffer ScopedFramebuffer : MFramebuffers)
    {
        ScopedApi.vkDestroyFramebuffer(MDevice, ScopedFramebuffer, nullptr);
    }
    MFramebuffers.clear();
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
    const bool ScopedOk = CreateSwapchain(InSize) && CreateFramebuffers() && CreateSyncObjects();
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

    if (ScopedApi.vkWaitForFences(MDevice, 1, &MInFlight[MFrameIndex], VK_TRUE,
                                  std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS)
    {
        LogError("vulkan", "vkWaitForFences failed; device lost");
        MDeviceLost = true;
        return false;
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

    // The fence is reset just before the submit in EndFrame: resetting here and
    // then failing the frame leaves an unsignalled fence that hangs the slot.
    VkCommandBuffer ScopedCommandBuffer = MCommandBuffers[MFrameIndex];
    ScopedApi.vkResetCommandBuffer(ScopedCommandBuffer, 0);

    VkCommandBufferBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ScopedBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ScopedApi.vkBeginCommandBuffer(ScopedCommandBuffer, &ScopedBeginInfo) != VK_SUCCESS)
    {
        LogError("vulkan", "vkBeginCommandBuffer failed; stopping the frame loop");
        MDeviceLost = true;
        return false;
    }

    MStats = {};
    MStats.frameIndex = MFrameCounter;
    MInFrame = true;
    MPassOpen = false;
    MMainPassRecorded = false;
    MBlindFrame = false;
    MVtFeedbackRecordedThisFrame = false;
    MInstanceFrameCursor = 0;
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
    ScopedTarget->format = InDesc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    const auto ScopedCleanup = [&]() { DestroyRenderTargetResources(*ScopedTarget); };

    VkImageCreateInfo ScopedImageInfo{};
    ScopedImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ScopedImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ScopedImageInfo.format = ScopedTarget->format;
    ScopedImageInfo.extent = {InDesc.size.width, InDesc.size.height, 1};
    ScopedImageInfo.mipLevels = 1;
    ScopedImageInfo.arrayLayers = 1;
    ScopedImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ScopedImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ScopedImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

    if (!InDesc.srgb && MOffscreenRenderPass != VK_NULL_HANDLE)
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

        VkSubpassDescription ScopedSubpass{};
        ScopedSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        ScopedSubpass.colorAttachmentCount = 1;
        ScopedSubpass.pColorAttachments = &ScopedColorRef;

        VkSubpassDependency ScopedDependency{};
        ScopedDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        ScopedDependency.dstSubpass = 0;
        ScopedDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        ScopedDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        ScopedDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ScopedPassInfo{};
        ScopedPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ScopedPassInfo.attachmentCount = 1;
        ScopedPassInfo.pAttachments = &ScopedColor;
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

    VkFramebufferCreateInfo ScopedFramebufferInfo{};
    ScopedFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ScopedFramebufferInfo.renderPass = ScopedTarget->renderPass;
    ScopedFramebufferInfo.attachmentCount = 1;
    ScopedFramebufferInfo.pAttachments = &ScopedTarget->view;
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
        ScopedSceneInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_STORAGE_BIT;
        ScopedSceneInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ScopedSceneInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (ScopedApi.vkCreateImage(MDevice, &ScopedSceneInfo, nullptr, &ScopedTarget->sceneImage) == VK_SUCCESS)
        {
            VkMemoryRequirements ScopedSceneRequirements{};
            ScopedApi.vkGetImageMemoryRequirements(MDevice, ScopedTarget->sceneImage, &ScopedSceneRequirements);
            VkMemoryAllocateInfo ScopedSceneAllocate{};
            ScopedSceneAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ScopedSceneAllocate.allocationSize = ScopedSceneRequirements.size;
            ScopedSceneAllocate.memoryTypeIndex = FindMemoryType(MMemoryProperties, ScopedSceneRequirements.memoryTypeBits,
                                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VkImageViewCreateInfo ScopedSceneView{};
            ScopedSceneView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ScopedSceneView.image = ScopedTarget->sceneImage;
            ScopedSceneView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ScopedSceneView.format = ScopedTarget->format;
            ScopedSceneView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ScopedSceneView.subresourceRange.levelCount = 1;
            ScopedSceneView.subresourceRange.layerCount = 1;
            VkFramebufferCreateInfo ScopedSceneFramebuffer{};
            ScopedSceneFramebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ScopedSceneFramebuffer.renderPass = ScopedTarget->renderPass;
            ScopedSceneFramebuffer.attachmentCount = 1;
            ScopedSceneFramebuffer.pAttachments = &ScopedTarget->sceneView;
            ScopedSceneFramebuffer.width = InDesc.size.width;
            ScopedSceneFramebuffer.height = InDesc.size.height;
            ScopedSceneFramebuffer.layers = 1;
            const bool ScopedSceneOk =
                ScopedSceneAllocate.memoryTypeIndex != std::numeric_limits<std::uint32_t>::max() &&
                ScopedApi.vkAllocateMemory(MDevice, &ScopedSceneAllocate, nullptr, &ScopedTarget->sceneMemory) ==
                    VK_SUCCESS &&
                ScopedApi.vkBindImageMemory(MDevice, ScopedTarget->sceneImage, ScopedTarget->sceneMemory, 0) ==
                    VK_SUCCESS &&
                ScopedApi.vkCreateImageView(MDevice, &ScopedSceneView, nullptr, &ScopedTarget->sceneView) == VK_SUCCESS &&
                ScopedApi.vkCreateFramebuffer(MDevice, &ScopedSceneFramebuffer, nullptr, &ScopedTarget->sceneFramebuffer) ==
                    VK_SUCCESS;
            if (ScopedSceneOk)
            {
                VkDescriptorSetAllocateInfo ScopedSetAllocate{};
                ScopedSetAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                ScopedSetAllocate.descriptorPool = MAccessibilityPool;
                ScopedSetAllocate.descriptorSetCount = 1;
                ScopedSetAllocate.pSetLayouts = &MAccessibilitySetLayout;
                if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedSetAllocate,
                                                      &ScopedTarget->accessibilitySet) == VK_SUCCESS)
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
            if (ScopedSceneOk && MAccessibilityFlashReady)
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
                const bool ScopedImageOk =
                    ScopedApi.vkCreateImage(MDevice, &ScopedFlashInfo, nullptr, &ScopedTarget->flashImage) ==
                    VK_SUCCESS;
                VkMemoryRequirements ScopedFlashRequirements{};
                if (ScopedImageOk)
                {
                    ScopedApi.vkGetImageMemoryRequirements(MDevice, ScopedTarget->flashImage,
                                                           &ScopedFlashRequirements);
                    ScopedFlashAllocate.allocationSize = ScopedFlashRequirements.size;
                    ScopedFlashAllocate.memoryTypeIndex = FindMemoryType(MMemoryProperties,
                                                                        ScopedFlashRequirements.memoryTypeBits,
                                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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
                    VkDescriptorSetAllocateInfo ScopedFlashSetInfo{};
                    ScopedFlashSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                    ScopedFlashSetInfo.descriptorPool = MAccessibilityPool;
                    ScopedFlashSetInfo.descriptorSetCount = 1;
                    ScopedFlashSetInfo.pSetLayouts = &MAccessibilityFlashSetLayout;
                    if (ScopedApi.vkAllocateDescriptorSets(MDevice, &ScopedFlashSetInfo,
                                                          &ScopedTarget->flashSet) == VK_SUCCESS)
                    {
                        VkDescriptorImageInfo ScopedColor{};
                        ScopedColor.imageView = ScopedTarget->sceneView;
                        ScopedColor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                        VkDescriptorImageInfo ScopedHistory{};
                        ScopedHistory.imageView = ScopedTarget->flashView;
                        ScopedHistory.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                        VkWriteDescriptorSet ScopedWrites[2]{};
                        ScopedWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        ScopedWrites[0].dstSet = ScopedTarget->flashSet;
                        ScopedWrites[0].dstBinding = 0;
                        ScopedWrites[0].descriptorCount = 1;
                        ScopedWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                        ScopedWrites[0].pImageInfo = &ScopedColor;
                        ScopedWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        ScopedWrites[1].dstSet = ScopedTarget->flashSet;
                        ScopedWrites[1].dstBinding = 1;
                        ScopedWrites[1].descriptorCount = 1;
                        ScopedWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                        ScopedWrites[1].pImageInfo = &ScopedHistory;
                        ScopedApi.vkUpdateDescriptorSets(MDevice, 2, ScopedWrites, 0, nullptr);
                    }
                }
            }
        }
    }

    // The sprite pipeline is tied to this pass, so create a second pipeline.
    if (!CreatePipelineForPass(ScopedTarget->renderPass, &ScopedTarget->pipeline) ||
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
    ApplySdfLighting(InPacket.sdf);
    ApplyVirtualTexture(InPacket);

    Vk::Api& ScopedApi = Vk::GetApi();
    const VkCommandBuffer ScopedCommandBuffer = MCommandBuffers[MFrameIndex];

    // Compute skinning must run outside the render pass.
    MBlindFrame = (InPacket.accessibility.flags & AccessibilityBlind) != 0;
    if (!MBlindFrame)
    {
        RecordMeshSkinning(ScopedCommandBuffer, InPacket);
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

    VkClearValue ScopedClear{};
    ScopedClear.color = {
        {static_cast<float>(InPacket.clear.R()) / 255.0f, static_cast<float>(InPacket.clear.G()) / 255.0f,
         static_cast<float>(InPacket.clear.B()) / 255.0f, static_cast<float>(InPacket.clear.A()) / 255.0f}};

    const bool ScopedComposite =
        ScopedTarget->sceneFramebuffer != VK_NULL_HANDLE && AccessibilityCompositeWanted(InPacket);
    VkRenderPassBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBeginInfo.renderPass = ScopedTarget->renderPass;
    ScopedBeginInfo.framebuffer = ScopedComposite ? ScopedTarget->sceneFramebuffer : ScopedTarget->framebuffer;
    ScopedBeginInfo.renderArea.extent = {ScopedTarget->extent.width, ScopedTarget->extent.height};
    ScopedBeginInfo.clearValueCount = 1;
    ScopedBeginInfo.pClearValues = &ScopedClear;
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
        const bool ScopedTargetSrgb = ScopedTarget->format == VK_FORMAT_R8G8B8A8_SRGB;
        DrawPacket(ScopedCommandBuffer, ScopedTarget->pipeline, InPacket, true, ScopedTargetSrgb);
        DrawMeshes(ScopedCommandBuffer, ScopedTarget->meshPipeline, InPacket, true, ScopedTargetSrgb);
    }
    ScopedApi.vkCmdEndRenderPass(ScopedCommandBuffer);

    if (ScopedTarget->flashSet != VK_NULL_HANDLE && FlashGuardWanted(InPacket.accessibility))
    {
        RecordFlashGuard(ScopedCommandBuffer, ScopedTarget->sceneImage, ScopedTarget->flashSet,
                         ScopedTarget->extent, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         InPacket.accessibility);
    }

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
                                     InPacket.accessibility);
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
    InView.layout = static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return ScopedTarget.view != VK_NULL_HANDLE;
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
    InInfo.apiVersion = MInfo.apiVersion != 0 ? MInfo.apiVersion : VK_API_VERSION_1_1;
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
    if (MDevice != VK_NULL_HANDLE)
    {
        const auto ScopedFunction = ScopedApi.vkGetDeviceProcAddr(MDevice, InName);
        if (ScopedFunction != nullptr)
        {
            return reinterpret_cast<void*>(ScopedFunction);
        }
    }
    if (MInstance != VK_NULL_HANDLE)
    {
        return reinterpret_cast<void*>(ScopedApi.GetInstanceProcAddr(MInstance, InName));
    }
    return nullptr;
}

void VulkanRHI::DrawPacket(VkCommandBuffer InCommandBuffer, VkPipeline InPipeline, const FramePacket& InPacket,
                           bool InOffscreen, bool InSrgb)
{
    if (InPacket.items.empty())
    {
        return;
    }
    Vk::Api& ScopedApi = Vk::GetApi();
    const std::uint32_t ScopedItemCount = static_cast<std::uint32_t>(InPacket.items.size());
    if (ScopedItemCount > MInstanceCapacity || MInstanceFrameCursor > MInstanceCapacity - ScopedItemCount)
    {
        LogError("vulkan", "frame passes upload {} items in total, ring holds {} - raise DeviceDesc::maxItemsPerFrame",
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

    MStats.items += ScopedItemCount;
    MStats.batches += static_cast<std::uint32_t>(InPacket.batches.size());

    const VkDeviceSize ScopedFirstInstanceBase =
        static_cast<VkDeviceSize>(MFrameIndex) * MInstanceCapacity + MInstanceFrameCursor;
    MInstanceFrameCursor += ScopedItemCount;
    ++MRecordedPassIndex;

    ScopedApi.vkCmdBindPipeline(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, InPipeline);
    // A sprite batch may carry a material pipeline; the engine quad pipeline is
    // restored whenever the next batch does not ask for one.
    VkPipeline ScopedBoundPipeline = InPipeline;
    // Packet-level array is the default. A batch with a non-zero textureArray
    std::uint32_t ScopedBoundArray = InPacket.textureArray;
    VkDescriptorSet ScopedActiveTexture = DescriptorFor(ScopedBoundArray);
    ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MPipelineLayout, 0, 1,
                                      &ScopedActiveTexture, 0, nullptr);
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
    ScopedConstants.material[0] = InPacket.timeSeconds;
    ScopedConstants.material[1] = InPacket.camera.viewport.X;
    ScopedConstants.material[2] = InPacket.camera.viewport.Y;
    ScopedConstants.material[3] = 0.0f;
    ScopedApi.vkCmdPushConstants(InCommandBuffer, MPipelineLayout,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                 sizeof(ScopedConstants), &ScopedConstants);

    // One draw per batch. With a texture array the only batch boundary is the
    if (InPacket.batches.empty())
    {
        ScopedApi.vkCmdDrawIndexed(InCommandBuffer, kQuadIndexCount, static_cast<std::uint32_t>(InPacket.items.size()),
                                   0, 0, static_cast<std::uint32_t>(ScopedFirstInstanceBase));
        ++MStats.drawCalls;
        return;
    }

    for (const DrawBatch& ScopedBatch : InPacket.batches)
    {
        if (ScopedBatch.itemCount == 0)
        {
            continue;
        }
        VkPipeline ScopedBatchPipeline = InPipeline;
        if (ScopedBatch.pipeline != 0)
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
                                              &ScopedActiveTexture, 0, nullptr);
            ScopedBoundArray = ScopedRequestedArray;
            ++MStats.textureBinds;
        }
        ScopedApi.vkCmdDrawIndexed(InCommandBuffer, kQuadIndexCount, ScopedBatch.itemCount, 0, 0,
                                   static_cast<std::uint32_t>(ScopedFirstInstanceBase + ScopedBatch.firstItem));
        ++MStats.drawCalls;
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
        ScopedApi.vkCmdPipelineBarrier(
            InCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            0, 1, &ScopedUploadBarrier, 0, nullptr, 0, nullptr);
    }

    const std::uint32_t ScopedBoneBase = MMeshFrameBoneCursor;
    if (InPacket.boneMatrices.size() > MBoneMatrixCapacity ||
        ScopedBoneBase > MBoneMatrixCapacity - InPacket.boneMatrices.size())
    {
        LogError("vulkan", "mesh frame passes upload {} bone matrices in total, ring holds {} - raise "
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
                                             ScopedSkinnedVertexBase + ScopedSkinnedVertexTotal +
                                             ScopedUnskinnedCursor;
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
                           bool InOffscreen, bool InSrgb)
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
            const VkPipeline ScopedMaterial = ResolveMaterialPipeline(ScopedGroup.materialPipeline, InOffscreen, InSrgb);
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
        ScopedApi.vkCmdBindDescriptorSets(InCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, MMeshGraphicsLayout, 0, 1,
                                          &ScopedAtlas, 0, nullptr);
        ScopedApi.vkCmdDrawIndexedIndirect(InCommandBuffer, ScopedFrame.indirectBuffer,
                                           static_cast<VkDeviceSize>(ScopedGroup.firstCommand) *
                                               sizeof(VkDrawIndexedIndirectCommand),
                                           ScopedGroup.commandCount, sizeof(VkDrawIndexedIndirectCommand));
        ++MStats.drawCalls;
    }
}

bool VulkanRHI::RecordComputePass(std::string_view InPipeline, const void* InConstants,
                                    std::uint32_t InConstantsBytes, Extent2D InExtent)
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
    const Extent2D ScopedMask{std::max(InExtent.width / 2u, 1u), std::max(InExtent.height / 2u, 1u)};
    if (!EnsureSdfMask(ScopedMask))
    {
        return false;
    }
    const std::uint32_t ScopedSlot = MFrameIndex % kSdfMaskFrames;
    if (MSdfComputeMapped[ScopedSlot] == nullptr)
    {
        return false;
    }
    std::memcpy(MSdfComputeMapped[ScopedSlot], InConstants, kSdfShadowBlockBytes);

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
    ScopedBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ScopedApi.vkCmdPipelineBarrier(ScopedCommand, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &ScopedBarrier);
    MSdfMaskValid = true;
    return true;
}

bool VulkanRHI::RecordGraphicsPass(std::string_view InPipeline, const void* InConstants,
                                   std::uint32_t InConstantsBytes, Extent2D InExtent,
                                   std::uint32_t InInstanceCount)
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

    MBlindFrame = (InPacket.accessibility.flags & AccessibilityBlind) != 0;
    // Only a scene packet owns the SDF parameters; the UI pass must not overwrite
    // the block before the GPU executes the recorded scene draws.
    if (InPacket.sdf.enabled)
    {
        ApplySdfLighting(InPacket.sdf);
    }
    ApplyVirtualTexture(InPacket);

    // Compute skinning must run outside the render pass. In BlindMode the
    if (!MBlindFrame)
    {
        RecordMeshSkinning(ScopedCommandBuffer, InPacket);
    }

    VkClearValue ScopedClear{};
    ScopedClear.color = {
        {static_cast<float>(InPacket.clear.R()) / 255.0f, static_cast<float>(InPacket.clear.G()) / 255.0f,
         static_cast<float>(InPacket.clear.B()) / 255.0f, static_cast<float>(InPacket.clear.A()) / 255.0f}};

    MAccessibilityFrame = InPacket.accessibility;
    MAccessibilitySwapActive = MAccessibilitySwapReady && AccessibilityCompositeWanted(InPacket);
    // The post-process composite needs the scene in an offscreen image; it is
    // skipped when the accessibility composite owns the swapchain path.
    MPostFxThisFrame = (InPacket.postFxEnabled || MPostFxRecordPending) && MPostFxReady && !MAccessibilitySwapActive;
    MPostFxSettings = InPacket.postFx;
    VkRenderPassBeginInfo ScopedBeginInfo{};
    ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ScopedBeginInfo.renderPass = MRenderPass;
    ScopedBeginInfo.framebuffer =
        (MAccessibilitySwapActive || MPostFxThisFrame) ? MAccessibilitySwapFramebuffer : MFramebuffers[MImageIndex];
    ScopedBeginInfo.renderArea.extent = MExtent;
    ScopedBeginInfo.clearValueCount = 1;
    ScopedBeginInfo.pClearValues = &ScopedClear;

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
        DrawPacket(ScopedCommandBuffer, MPipeline, InPacket, false, false);
        DrawMeshes(ScopedCommandBuffer, MMeshPipeline, InPacket, false, false);
    }
}

void VulkanRHI::EndFrame()
{
    FBY_ASSERT(MInFrame);
    Vk::Api& ScopedApi = Vk::GetApi();
    const VkCommandBuffer ScopedCommandBuffer = MCommandBuffers[MFrameIndex];

    if (!MMainPassRecorded)
    {
        // A frame that only rendered offscreen targets never wrote the acquired
        // swapchain image. Present a defined clear instead of undefined memory.
        VkClearValue ScopedClear{};
        ScopedClear.color.float32[0] = 0.0f;
        ScopedClear.color.float32[1] = 0.0f;
        ScopedClear.color.float32[2] = 0.0f;
        ScopedClear.color.float32[3] = 1.0f;
        VkRenderPassBeginInfo ScopedBeginInfo{};
        ScopedBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        ScopedBeginInfo.renderPass = MRenderPass;
        ScopedBeginInfo.framebuffer = MFramebuffers[MImageIndex];
        ScopedBeginInfo.renderArea.extent = MExtent;
        ScopedBeginInfo.clearValueCount = 1;
        ScopedBeginInfo.pClearValues = &ScopedClear;
        ScopedApi.vkCmdBeginRenderPass(ScopedCommandBuffer, &ScopedBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        ScopedApi.vkCmdEndRenderPass(ScopedCommandBuffer);
    }

    if (MPassOpen)
    {
        // UI last: the game has already been recorded into the main pass and
        if (MUiRenderer != nullptr && !MBlindFrame)
        {
            MUiRenderer(MUiUser, ScopedCommandBuffer);
        }
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
        if (MAccessibilitySwapFlashSet != VK_NULL_HANDLE && FlashGuardWanted(MAccessibilityFrame))
        {
            // Per-pixel guard first: it needs the scene in GENERAL, applies the
            // residual gain and leaves the image readable for the composite.
            RecordFlashGuard(ScopedCommandBuffer, MAccessibilitySwapImage, MAccessibilitySwapFlashSet,
                             Extent2D{MExtent.width, MExtent.height}, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                             MAccessibilityFrame);
            RecordAccessibilityComposite(ScopedCommandBuffer, MRenderPass, MFramebuffers[MImageIndex],
                                         MAccessibilitySwapPipeline, MAccessibilitySwapSet,
                                         Extent2D{MExtent.width, MExtent.height}, MAccessibilityFrame);
            MAccessibilitySwapActive = false;
            MPassOpen = false;
        }
        else
        {
            VkImageMemoryBarrier ScopedSwapBarrier{};
            ScopedSwapBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ScopedSwapBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            ScopedSwapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ScopedSwapBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ScopedSwapBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ScopedSwapBarrier.image = MAccessibilitySwapImage;
            ScopedSwapBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            ScopedSwapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            ScopedApi.vkCmdPipelineBarrier(ScopedCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                           &ScopedSwapBarrier);
            RecordAccessibilityComposite(ScopedCommandBuffer, MRenderPass, MFramebuffers[MImageIndex],
                                         MAccessibilitySwapPipeline, MAccessibilitySwapSet,
                                         Extent2D{MExtent.width, MExtent.height}, MAccessibilityFrame);
            MAccessibilitySwapActive = false;
        }
    }
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
            ScopedApi.vkUnmapMemory(MDevice, MInstanceMemory);
            MInstanceMapped = nullptr;
        }
        DestroySdfShadowResources();
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
        DestroySwapchain();

        if (MCommandPool != VK_NULL_HANDLE)
        {
            ScopedApi.vkDestroyCommandPool(MDevice, MCommandPool, nullptr);
            MCommandPool = VK_NULL_HANDLE;
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

std::unique_ptr<RHI> CreateVulkanRHI()
{
    return std::make_unique<VulkanRHI>();
}
}

// Loader
namespace Fby::Rhi::Vk
{
bool Api::LoadGlobal()
{
    SDL_FunctionPointer ScopedPointer = SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (ScopedPointer == nullptr)
    {
        return false;
    }
    GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(ScopedPointer);

#define FBY_VK_LOAD_GLOBAL(name)                                                                                       \
    name = reinterpret_cast<PFN_##name>(GetInstanceProcAddr(nullptr, #name));                                          \
    if (name == nullptr)                                                                                               \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    FBY_VK_GLOBAL_FUNCTIONS(FBY_VK_LOAD_GLOBAL)
#undef FBY_VK_LOAD_GLOBAL
    return true;
}

bool Api::LoadInstance(VkInstance instance)
{
#define FBY_VK_LOAD_INSTANCE(name)                                                                                     \
    name = reinterpret_cast<PFN_##name>(GetInstanceProcAddr(instance, #name));                                         \
    if (name == nullptr)                                                                                               \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    FBY_VK_INSTANCE_FUNCTIONS(FBY_VK_LOAD_INSTANCE)
#undef FBY_VK_LOAD_INSTANCE
    return true;
}

bool Api::LoadDevice(VkDevice device)
{
#define FBY_VK_LOAD_DEVICE(name)                                                                                       \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));                                           \
    if (name == nullptr)                                                                                               \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    FBY_VK_DEVICE_FUNCTIONS(FBY_VK_LOAD_DEVICE)
#undef FBY_VK_LOAD_DEVICE
    return true;
}

Api& GetApi()
{
    static Api ScopedApi;
    return ScopedApi;
}
}

// FBY_HAS_VULKAN_HEADERS
#endif
