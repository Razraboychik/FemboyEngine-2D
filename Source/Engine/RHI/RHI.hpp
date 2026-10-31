// FemboyEngine2D - Render Hardware Interface.
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/RHI/Mesh.hpp"
#include "Engine/RHI/RenderItem.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace Fby::Rhi
{

enum class Backend : std::uint8_t
{
    Null = 0,
    Vulkan,
    Metal,
};

[[nodiscard]] constexpr const char* BackendName(Backend InBackend) noexcept
{
    switch (InBackend)
    {
        case Backend::Null:
            return "null";
        case Backend::Vulkan:
            return "vulkan";
        case Backend::Metal:
            return "metal";
    }
    return "unknown";
}

struct Extent2D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] constexpr bool operator==(const Extent2D&) const = default;
};

// Everything the RHI needs to start. `nativeWindow` is an SDL_Window* kept
struct DeviceDesc
{
    Backend backend = Backend::Null;
    void* nativeWindow = nullptr;
    Extent2D size{1280, 720};
    bool vsync = true;
    bool enableValidation = false;
    // Triple buffering by default: the CPU may be one frame ahead of the GPU
    std::uint32_t framesInFlight = 3;
    // Upper bound for one frame's instance data, in items. 1M items * 48 B is
    std::uint32_t maxItemsPerFrame = 1u << 20;
    std::uint32_t maxTextureLayers = 256;
    // Mesh path ring capacities, per frame slice. The defaults cover ~1000
    std::uint32_t maxMeshVerticesPerFrame = 1u << 16;
    std::uint32_t maxMeshIndicesPerFrame = 1u << 17;
    std::uint32_t maxBoneMatricesPerFrame = 1u << 15;
    // Device-local cache for static geometry. The defaults are 5 MB of vertices
    std::uint32_t maxStaticMeshVertices = 1u << 18;
    std::uint32_t maxStaticMeshIndices = 1u << 19;
    // Lower bound on the per-frame texture-upload staging arena, in bytes. Video
    std::uint32_t maxTextureUploadBytes = 32u << 20;
    // Upper bound on draw records (not draw calls) in one frame.
    std::uint32_t maxMeshDrawsPerFrame = 4096;
};

struct DeviceInfo
{
    Backend backend = Backend::Null;
    char name[256] = {};
    char driver[128] = {};
    std::uint32_t apiVersion = 0;
    std::uint32_t framesInFlight = 0;
    std::uint32_t maxItemsPerFrame = 0;
    std::uint32_t maxTextureLayers = 0;
    std::uint32_t maxMeshVerticesPerFrame = 0;
    std::uint32_t maxBoneMatricesPerFrame = 0;
    std::uint32_t maxStaticMeshVertices = 0;
    std::uint32_t maxMeshDrawsPerFrame = 0;
    bool discrete = false;
};

struct TextureArrayDesc
{
    Extent2D size{};
    std::uint32_t layers = 1;
    std::uint32_t mipLevels = 1;
    bool srgb = true;
    // Cooked texel format. The cooker writes BC7 (desktop) or ASTC (mobile);
    Assets::TextureFormat format = Assets::TextureFormat::Rgba8;
};

// A GPU pipeline compiled from runtime-generated SPIR-V (MaterialGraph).
// Which engine shader interface a material pipeline links against. A mesh
// material pipeline pairs with the mesh vertex stage (or its generated one); a
// sprite material pipeline pairs with the engine quad vertex shader and the
// sprite pipeline layout, so a sprite batch can bind it mid-frame.
enum class ShaderPipelineDomain : std::uint8_t
{
    Mesh = 0,
    Sprite = 1,
};

struct ShaderPipelineDesc
{
    // Empty: engine mesh.vert, or engine quad.vert in the sprite domain
    std::span<const std::byte> vertexSpirv;
    // Empty: engine mesh.frag
    std::span<const std::byte> fragmentSpirv;
    const char* debugName = nullptr;
    // Layout and vertex stage contract the pipeline is built for
    ShaderPipelineDomain domain = ShaderPipelineDomain::Mesh;
};

struct ShaderPipelineHandle
{
    static constexpr std::uint32_t kInvalid32 = 0xFFFFFFFFu;
    std::uint32_t id = kInvalid32;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return id != kInvalid32;
    }
};

// Editor / offscreen presentation
struct RenderTargetDesc
{
    Extent2D size{};
    bool srgb = false;
};

struct RenderTargetHandle
{
    static constexpr std::uint32_t kInvalid32 = 0xFFFFFFFFu;
    std::uint32_t id = kInvalid32;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return id != kInvalid32;
    }
};

struct NativeTextureView
{
    // VkImageView on Vulkan
    void* view = nullptr;
    // VkImageLayout, passed back to the backend verbatim
    int layout = 0;
};

struct UiBackendInfo
{
    void* instance = nullptr;
    void* physicalDevice = nullptr;
    void* device = nullptr;
    void* queue = nullptr;
    void* renderPass = nullptr;
    void* pipelineCache = nullptr;
    // Null: let the backend create its own
    void* descriptorPool = nullptr;
    std::uint32_t queueFamily = 0;
    std::uint32_t apiVersion = 0;
    std::uint32_t minImageCount = 2;
    std::uint32_t imageCount = 0;
    std::uint32_t colorFormat = 0;
    std::uint32_t descriptorPoolSize = 256;
    bool dynamicRendering = false;
};

// `nativeCommandBuffer` is a VkCommandBuffer on Vulkan. The callback runs while
using UiRenderCallback = void (*)(void* InUser, void* InNativeCommandBuffer);

// Per-frame counters. The renderer exposes these instead of leaving the game to
struct FrameStats
{
    std::uint64_t frameIndex = 0;
    std::uint32_t items = 0;
    std::uint32_t batches = 0;
    std::uint32_t drawCalls = 0;
    std::uint32_t textureBinds = 0;
    std::uint32_t meshItems = 0;
    std::uint32_t meshDraws = 0;
    std::uint32_t meshIndirectCalls = 0;
    std::uint32_t staticMeshDraws = 0;
    std::uint32_t skinDispatches = 0;
    std::uint32_t skinnedVertices = 0;
    std::uint32_t skinRanges = 0;
    std::uint64_t uploadedBytes = 0;
};

// Data pushed to a pipeline for the whole dispatch. Constrained so a caller
template <typename T>
concept PodPayload = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> && sizeof(T) <= 128;

// A single texture-array handle owns `layers` slices. Items reference a layer by
struct TextureArrayHandle
{
    static constexpr std::uint32_t kInvalid32 = 0xFFFFFFFFu;

    std::uint32_t id = kInvalid32;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return id != kInvalid32;
    }
};

struct VirtualTextureResourceDesc
{
    std::uint32_t layerCount = 8;
    std::uint32_t pageSize = 2048;
    std::uint32_t tileSize = 64;
    // Page table is pageTableSize x pageTableSize tiles (world extents are the
    // caller's business; the GPU only needs the lookup grid).
    std::uint32_t pageTableSize = 1024;
};

static_assert(std::is_trivially_copyable_v<VirtualTextureResourceDesc>);

class RHI
{
public:
    virtual ~RHI() = default;

    RHI(const RHI&) = delete;
    RHI& operator=(const RHI&) = delete;

    // Creates and initialises the backend selected in `desc`. Returns nullptr
    [[nodiscard]] static std::unique_ptr<RHI> Create(const DeviceDesc& InDesc);

    virtual bool Initialize(const DeviceDesc& InDesc) = 0;
    virtual void Shutdown() = 0;
    // Recreates the swapchain. Called on window resize, never mid-frame.
    virtual void Resize(Extent2D InSize) = 0;
    // Switches the present mode without restarting the editor. Called between
    [[nodiscard]] virtual bool SetVSync(bool InEnabled)
    {
        (void)InEnabled;
        return false;
    }
    [[nodiscard]] virtual bool VSyncEnabled() const noexcept
    {
        return false;
    }
    virtual void WaitIdle() = 0;

    // Acquires the next swapchain image and starts uploading into this frame's
    virtual bool BeginFrame() = 0;
    virtual void Submit(const FramePacket& InPacket) = 0;
    virtual void EndFrame() = 0;

    // Records one render-graph compute pass into the current frame command buffer.
    // Backends know the named pipelines they ship; unknown names are ignored so a
    // graph can carry passes a given backend does not implement yet.
    virtual bool RecordComputePass(std::string_view InPipeline, const void* InConstants, std::uint32_t InConstantsBytes,
                                   Extent2D InExtent)
    {
        (void)InPipeline;
        (void)InConstants;
        (void)InConstantsBytes;
        (void)InExtent;
        return false;
    }

    // Records one render-graph graphics pass into the current frame. Backends
    // know the named pipelines they ship (the Vulkan path executes the
    // full-screen postfx composite); unknown names return false so a graph can
    // carry passes a backend does not implement yet.
    virtual bool RecordGraphicsPass(std::string_view InPipeline, const void* InConstants,
                                    std::uint32_t InConstantsBytes, Extent2D InExtent,
                                    std::uint32_t InInstanceCount = 1)
    {
        (void)InPipeline;
        (void)InConstants;
        (void)InConstantsBytes;
        (void)InExtent;
        (void)InInstanceCount;
        return false;
    }

    virtual TextureArrayHandle CreateTextureArray(const TextureArrayDesc& InDesc,
                                                  std::span<const std::byte> InPixels) = 0;    virtual void DestroyTextureArray(TextureArrayHandle InHandle) = 0;

    // Virtual texturing. The page array stores one pageSize x pageSize page per
    // layer; pages are uploaded per tile and the table is uploaded per frame.
    // Backends without VT return an invalid handle and false; the caller falls
    // back to ordinary texture arrays.
    [[nodiscard]] virtual VirtualTextureHandle CreateVirtualTexture(const VirtualTextureResourceDesc& InDesc)
    {
        (void)InDesc;
        return {};
    }
    virtual void DestroyVirtualTexture(VirtualTextureHandle InHandle)
    {
        (void)InHandle;
    }
    [[nodiscard]] virtual bool UpdateVirtualTexturePageTable(VirtualTextureHandle InHandle, std::uint32_t InWidth,
                                                             std::uint32_t InHeight,
                                                             std::span<const std::uint32_t> InEntries)
    {
        (void)InHandle;
        (void)InWidth;
        (void)InHeight;
        (void)InEntries;
        return false;
    }
    // Reads the page requests the VT sprite path wrote this batch of frames.
    // Returns the number of unique packed tile ids written into `OutTiles`.
    virtual std::uint32_t ReadVirtualTextureFeedback(std::span<std::uint32_t> OutTiles)
    {
        (void)OutTiles;
        return 0;
    }
    [[nodiscard]] virtual bool UploadVirtualTexturePage(VirtualTextureHandle InHandle, std::uint32_t InLayer,
                                                        std::uint32_t InSlotX, std::uint32_t InSlotY,
                                                        std::uint32_t InWidth, std::uint32_t InHeight,
                                                        std::uint32_t InRowPitch, std::span<const std::byte> InPixels)
    {
        (void)InHandle;
        (void)InLayer;
        (void)InSlotX;
        (void)InSlotY;
        (void)InWidth;
        (void)InHeight;
        (void)InRowPitch;
        (void)InPixels;
        return false;
    }

    // Streaming path for media and for editor re-cooks. Writes one layer/mip of
    virtual bool UpdateTextureArrayLayer(TextureArrayHandle InHandle, std::uint32_t InLayer, std::uint32_t InMipLevel,
                                         std::span<const std::byte> InData)
    {
        (void)InHandle;
        (void)InLayer;
        (void)InMipLevel;
        (void)InData;
        return false;
    }

    // Runtime shader pipeline (MaterialGraph -> GLSL -> SPIR-V). One handle owns
    virtual ShaderPipelineHandle CreateShaderPipeline(const ShaderPipelineDesc& InDesc)
    {
        (void)InDesc;
        return {};
    }
    virtual void DestroyShaderPipeline(ShaderPipelineHandle InHandle)
    {
        (void)InHandle;
    }
    // True when the backend builds a graphics pipeline from cooked SPIR-V.
    // Backends without a runtime shader path keep the engine shaders, so a
    // material pipeline is a hint rather than a requirement on those targets.
    [[nodiscard]] virtual bool SupportsRuntimeShaderPipelines() const noexcept
    {
        return false;
    }

    // Copies an offscreen render target back to host memory as tightly packed
    virtual bool CaptureRenderTarget(RenderTargetHandle InHandle, std::span<std::byte> InRgba8,
                                     Extent2D* OutSize = nullptr)
    {
        (void)InHandle;
        (void)InRgba8;
        (void)OutSize;
        return false;
    }

    // Static geometry cache. Uploads vertices/indices into device-local memory
    [[nodiscard]] virtual MeshHandle CreateMesh(const MeshDesc& InDesc)
    {
        (void)InDesc;
        return {};
    }
    virtual void DestroyMesh(MeshHandle InHandle)
    {
        (void)InHandle;
    }

    // Defaults keep headless and alternative backends buildable; only Vulkan
    [[nodiscard]] virtual bool QueryUiBackend(UiBackendInfo& InInfo) const
    {
        (void)InInfo;
        return false;
    }
    // Resolves a backend entry point by name. Used by ImGui's Vulkan loader.
    [[nodiscard]] virtual void* GetNativeFunction(const char* InName) const
    {
        (void)InName;
        return nullptr;
    }
    [[nodiscard]] virtual RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& InDesc)
    {
        (void)InDesc;
        return {};
    }
    virtual void DestroyRenderTarget(RenderTargetHandle InHandle)
    {
        (void)InHandle;
    }
    // Records a sprite pass into the target and leaves it in a sampleable
    virtual bool RenderToTarget(RenderTargetHandle InHandle, const FramePacket& InPacket)
    {
        (void)InHandle;
        (void)InPacket;
        return false;
    }
    [[nodiscard]] virtual bool RenderTargetTexture(RenderTargetHandle InHandle, NativeTextureView& InView) const
    {
        (void)InHandle;
        (void)InView;
        return false;
    }

    // A 2D view of one texture-array layer, for editor thumbnails and per-asset
    // previews. The view stays owned by the backend and is cached per layer.
    [[nodiscard]] virtual bool TextureArrayLayerView(TextureArrayHandle InHandle, std::uint32_t InLayer,
                                                     NativeTextureView& InView) const
    {
        (void)InHandle;
        (void)InLayer;
        (void)InView;
        return false;
    }
    virtual void SetUiRenderer(UiRenderCallback InCallback, void* InUser)
    {
        (void)InCallback;
        (void)InUser;
    }

    [[nodiscard]] virtual const DeviceInfo& Info() const noexcept = 0;
    [[nodiscard]] virtual FrameStats Stats() const noexcept = 0;
    [[nodiscard]] virtual Backend GetBackend() const noexcept = 0;
    // Bytes the transient ring can accept in the current frame slice.
    [[nodiscard]] virtual std::size_t TransientCapacity() const noexcept = 0;

protected:
    RHI() = default;
};

// Backend factory hooks, implemented by the backend translation units.
[[nodiscard]] std::unique_ptr<RHI> CreateVulkanRHI();
[[nodiscard]] std::unique_ptr<RHI> CreateMetalRHI();

// Selects a backend for the current platform at runtime. On Windows/Linux the
[[nodiscard]] Backend SelectDefaultBackend() noexcept;
}
