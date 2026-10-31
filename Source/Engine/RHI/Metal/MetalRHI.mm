// FemboyEngine2D - Metal backend.
//
// This is the real Apple path, not a factory stub:
//   * CAMetalLayer presentation through SDL3;
//   * a device-local static mesh cache addressed by MeshHandle;
//   * the SAME one-dispatch LBS contract as Vulkan, implemented in MSL;
//   * sprite and mesh render pipelines compiled from the embedded MSL library.
//
// The math is deliberately identical to the Vulkan shaders: full 4x4 palette,
// full 4x4 model, homogeneous SkinnedVertex. Only the API calls differ.
//
// NOTE: editor ImGui on Metal is not wired yet; QueryUiBackend() returns false.
// The mesh/sprite RHI itself is complete and is what the LBS path needs.

#include "Engine/RHI/Metal/MetalRHI.hpp"

#include "MetalShaderSource.hpp"
#include "Engine/Core/Log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace Fby::Rhi
{
namespace
{
constexpr std::uint32_t kMaxFrames = 4;
constexpr std::uint32_t kAtlasSize = 128;
constexpr std::uint32_t kAtlasLayers = 4;

struct MetalTextureArray
{
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> sampler = nil;
    std::uint32_t layers = 0;
};

struct MetalFrame
{
    // MeshVertex, storageModeShared
    id<MTLBuffer> bindVertices = nil;
    id<MTLBuffer> dynamicIndices = nil;
    // Device-local compute output
    id<MTLBuffer> skinnedVertices = nil;
    id<MTLBuffer> bones = nil;
    id<MTLBuffer> ranges = nil;
    // RenderItem[]
    id<MTLBuffer> instances = nil;
};

struct MetalStaticMesh
{
    // SkinnedVertex, storageModePrivate
    id<MTLBuffer> vertices = nil;
    id<MTLBuffer> indices = nil;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    bool alive = false;
};

struct MetalRenderTarget
{
    id<MTLTexture> texture = nil;
    Extent2D extent{};
    bool srgb = false;
};

[[nodiscard]] Mat4 MakeOrthoProjection(const CameraData& camera) noexcept
{
    Mat4 projection;
    const float scaleX = camera.viewport.X > 0.0f ? (2.0f * camera.zoom) / camera.viewport.X : 0.0f;
    const float scaleY = camera.viewport.Y > 0.0f ? (-2.0f * camera.zoom) / camera.viewport.Y : 0.0f;
    projection.m[0] = scaleX;
    projection.m[5] = scaleY;
    projection.m[12] = -camera.center.X * scaleX;
    projection.m[13] = -camera.center.Y * scaleY;
    return projection;
}

[[nodiscard]] std::vector<std::byte> BuildAtlasPixels()
{
    constexpr std::uint32_t size = kAtlasSize;
    std::vector<std::byte> pixels(static_cast<std::size_t>(size) * size * kAtlasLayers * 4);
    const auto write = [&pixels](std::uint32_t layer, std::uint32_t x, std::uint32_t y, std::uint8_t r, std::uint8_t g,
                                 std::uint8_t b, std::uint8_t a)
    {
        const std::size_t index = ((static_cast<std::size_t>(layer) * kAtlasSize + y) * kAtlasSize + x) * 4;
        pixels[index + 0] = static_cast<std::byte>(r);
        pixels[index + 1] = static_cast<std::byte>(g);
        pixels[index + 2] = static_cast<std::byte>(b);
        pixels[index + 3] = static_cast<std::byte>(a);
    };
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            write(0, x, y, 255, 255, 255, 255);
            const bool even = ((x / 16) + (y / 16)) % 2 == 0;
            write(1, x, y, even ? 255 : 40, even ? 80 : 20, even ? 140 : 60, 255);
            const float dx = static_cast<float>(x) - static_cast<float>(size) * 0.5f;
            const float dy = static_cast<float>(y) - static_cast<float>(size) * 0.5f;
            const float radius = std::sqrt(dx * dx + dy * dy) / (static_cast<float>(size) * 0.5f);
            const auto shade = static_cast<std::uint8_t>(std::clamp(1.0f - radius, 0.0f, 1.0f) * 255.0f);
            write(2, x, y, 40, shade, 90, 255);
            write(3, x, y, radius <= 0.85f ? 90 : 0, radius <= 0.85f ? 160 : 0, radius <= 0.85f ? 255 : 0,
                  radius <= 0.85f ? 255 : 0);
        }
    }
    return pixels;
}

class MetalRHI final : public RHI
{
public:
    MetalRHI() = default;
    ~MetalRHI() override
    {
        Shutdown();
    }

    MetalRHI(const MetalRHI&) = delete;
    MetalRHI& operator=(const MetalRHI&) = delete;

    bool Initialize(const DeviceDesc& desc) override;
    void Shutdown() override;
    void Resize(Extent2D size) override;
    void WaitIdle() override;

    bool BeginFrame() override;
    void Submit(const FramePacket& packet) override;
    void EndFrame() override;

    TextureArrayHandle CreateTextureArray(const TextureArrayDesc& desc, std::span<const std::byte> pixels) override;
    void DestroyTextureArray(TextureArrayHandle handle) override;
    MeshHandle CreateMesh(const MeshDesc& desc) override;
    void DestroyMesh(MeshHandle handle) override;

    RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc) override;
    void DestroyRenderTarget(RenderTargetHandle handle) override;
    bool RenderToTarget(RenderTargetHandle handle, const FramePacket& packet) override;
    bool RenderTargetTexture(RenderTargetHandle handle, NativeTextureView& view) const override;

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
        return Backend::Metal;
    }
    [[nodiscard]] std::size_t TransientCapacity() const noexcept override
    {
        return MTransientCapacity;
    }

private:
    bool CreateLibraryAndPipelines();
    void CreateBuiltInAtlas();
    id<MTLTexture> TextureFor(std::uint32_t id) const;
    id<MTLSamplerState> SamplerFor(std::uint32_t id) const;
    void RecordMeshCompute(id<MTLCommandBuffer> commandBuffer, const FramePacket& packet);
    void RecordPacket(id<MTLCommandBuffer> commandBuffer, id<MTLTexture> color, const FramePacket& packet);
    [[nodiscard]] id<MTLBuffer> CreateBuffer(std::size_t bytes, MTLResourceOptions options) const;

    DeviceDesc MDesc{};
    DeviceInfo MInfo{};
    FrameStats MStats{};

    SDL_MetalView MMetalView = nullptr;
    __strong CAMetalLayer* MLayer = nil;
    id<MTLDevice> MDevice = nil;
    id<MTLCommandQueue> MQueue = nil;

    id<MTLLibrary> MLibrary = nil;
    id<MTLComputePipelineState> MSkinPipeline = nil;
    id<MTLRenderPipelineState> MMeshPipeline = nil;
    id<MTLRenderPipelineState> MSpritePipeline = nil;
    id<MTLSamplerState> MDefaultSampler = nil;

    std::array<MetalFrame, kMaxFrames> MFrames{};
    std::vector<std::unique_ptr<MetalTextureArray>> MTextureArrays;
    std::vector<std::unique_ptr<MetalStaticMesh>> MStaticMeshes;
    std::vector<std::uint32_t> MFreeStaticMeshIds;
    std::vector<std::unique_ptr<MetalRenderTarget>> MRenderTargets;
    std::vector<std::uint32_t> MFreeRenderTargetIds;

    id<MTLCommandBuffer> MCommandBuffer = nil;
    id<CAMetalDrawable> MDrawable = nil;
    dispatch_semaphore_t MFrameSemaphore = nullptr;
    std::uint32_t MFrameSlot = 0;
    std::uint32_t MFramesInFlight = 3;
    std::uint32_t MMeshVertexCapacity = 0;
    std::uint32_t MMeshIndexCapacity = 0;
    std::uint32_t MBoneCapacity = 0;
    std::uint32_t MMeshDrawCapacity = 0;
    std::uint32_t MInstanceCapacity = 0;
    std::size_t MTransientCapacity = 0;
    std::uint64_t MFrameIndex = 0;
    bool MInFrame = false;
    // DrawBatch::pipeline is a runtime material-pipeline id. The Metal backend
    // has no material pipeline cache yet, so the id cannot be bound; warn once
    // instead of printing a line per batch per frame.
    bool MWarnedBatchPipeline = false;
};

id<MTLBuffer> MetalRHI::CreateBuffer(std::size_t bytes, MTLResourceOptions options) const
{
    return [MDevice newBufferWithLength:std::max<std::size_t>(bytes, 4) options:options];
}

bool MetalRHI::Initialize(const DeviceDesc& desc)
{
    MDesc = desc;
    MInfo.backend = Backend::Metal;
    MInfo.framesInFlight = std::clamp(desc.framesInFlight, 1u, kMaxFrames);
    MInfo.maxItemsPerFrame = desc.maxItemsPerFrame;
    MInfo.maxTextureLayers = desc.maxTextureLayers;
    MInfo.maxMeshVerticesPerFrame = desc.maxMeshVerticesPerFrame;
    MInfo.maxBoneMatricesPerFrame = desc.maxBoneMatricesPerFrame;
    MInfo.maxStaticMeshVertices = desc.maxStaticMeshVertices;
    MInfo.maxMeshDrawsPerFrame = desc.maxMeshDrawsPerFrame;
    MFramesInFlight = MInfo.framesInFlight;
    MMeshVertexCapacity = std::max(1u, desc.maxMeshVerticesPerFrame);
    MMeshIndexCapacity = std::max(1u, desc.maxMeshIndicesPerFrame);
    MBoneCapacity = std::max(1u, desc.maxBoneMatricesPerFrame);
    MMeshDrawCapacity = std::max(1u, desc.maxMeshDrawsPerFrame);
    MInstanceCapacity = std::max(1u, desc.maxItemsPerFrame);

    MDevice = MTLCreateSystemDefaultDevice();
    if (MDevice == nil)
    {
        LogError("metal", "no Metal device on this system");
        return false;
    }
    MQueue = [MDevice newCommandQueue];
    if (MQueue == nil)
    {
        LogError("metal", "MTLCommandQueue creation failed");
        return false;
    }

    MMetalView = SDL_Metal_CreateView(static_cast<SDL_Window*>(desc.nativeWindow));
    if (MMetalView == nullptr)
    {
        LogError("metal", "SDL_Metal_CreateView failed: {}", SDL_GetError());
        return false;
    }
    MLayer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(MMetalView);
    if (MLayer == nil)
    {
        LogError("metal", "SDL_Metal_GetLayer returned null");
        return false;
    }
    MLayer.device = MDevice;
    MLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    MLayer.framebufferOnly = YES;
    MLayer.maximumDrawableCount = static_cast<NSUInteger>(std::max(2u, MFramesInFlight));
    const float density = SDL_GetWindowPixelDensity(static_cast<SDL_Window*>(desc.nativeWindow));
    MLayer.drawableSize =
        CGSizeMake(static_cast<CGFloat>(desc.size.width) * density, static_cast<CGFloat>(desc.size.height) * density);

    if (!CreateLibraryAndPipelines())
    {
        return false;
    }
    CreateBuiltInAtlas();

    MFrameSemaphore = dispatch_semaphore_create(static_cast<long>(MFramesInFlight));
    for (std::uint32_t i = 0; i < MFramesInFlight; ++i)
    {
        MetalFrame& frame = MFrames[i];
        frame.bindVertices = CreateBuffer(static_cast<std::size_t>(MMeshVertexCapacity) * sizeof(MeshVertex),
                                          MTLResourceStorageModeShared);
        frame.dynamicIndices = CreateBuffer(static_cast<std::size_t>(MMeshIndexCapacity) * sizeof(std::uint16_t),
                                            MTLResourceStorageModeShared);
        frame.skinnedVertices = CreateBuffer(static_cast<std::size_t>(MMeshVertexCapacity) * sizeof(SkinnedVertex),
                                             MTLResourceStorageModePrivate);
        frame.bones =
            CreateBuffer(static_cast<std::size_t>(MBoneCapacity) * sizeof(Mat4), MTLResourceStorageModeShared);
        frame.ranges = CreateBuffer(static_cast<std::size_t>(MMeshDrawCapacity) * sizeof(MeshSkinRange),
                                    MTLResourceStorageModeShared);
        frame.instances = CreateBuffer(static_cast<std::size_t>(MInstanceCapacity) * sizeof(RenderItem),
                                       MTLResourceStorageModeShared);
        MTransientCapacity += static_cast<std::size_t>(MMeshVertexCapacity) * sizeof(MeshVertex) +
                              static_cast<std::size_t>(MMeshIndexCapacity) * sizeof(std::uint16_t) +
                              static_cast<std::size_t>(MInstanceCapacity) * sizeof(RenderItem);
    }

    const char* deviceName = MDevice.name.UTF8String;
    std::snprintf(MInfo.name, sizeof(MInfo.name), "%s", deviceName != nullptr ? deviceName : "Metal device");
    std::snprintf(MInfo.driver, sizeof(MInfo.driver), "Metal %s", "3.0");
    MInfo.discrete = MDevice.hasUnifiedMemory ? false : true;

    LogInfo("metal", "device: {} ({} frames in flight, {} static mesh verts)", MInfo.name, MFramesInFlight,
            desc.maxStaticMeshVertices);
    return true;
}

bool MetalRHI::CreateLibraryAndPipelines()
{
    NSError* error = nil;
    const auto errorText = [](NSError* value) -> const char*
    { return value != nil ? value.localizedDescription.UTF8String : "unknown"; };
    NSString* source = [NSString stringWithUTF8String:Fby::Rhi::metal_shaders::kMetalLibrarySource];
    MLibrary = [MDevice newLibraryWithSource:source options:nil error:&error];
    if (MLibrary == nil)
    {
        LogError("metal", "MSL compilation failed: {}", errorText(error));
        return false;
    }

    id<MTLFunction> skinFunction = [MLibrary newFunctionWithName:@"mesh_skinning"];
    id<MTLFunction> meshVertex = [MLibrary newFunctionWithName:@"mesh_vertex"];
    id<MTLFunction> meshFragment = [MLibrary newFunctionWithName:@"mesh_fragment"];
    id<MTLFunction> spriteVertex = [MLibrary newFunctionWithName:@"sprite_vertex"];
    id<MTLFunction> spriteFragment = [MLibrary newFunctionWithName:@"sprite_fragment"];
    if (skinFunction == nil || meshVertex == nil || meshFragment == nil || spriteVertex == nil || spriteFragment == nil)
    {
        LogError("metal", "MSL library is missing one of the mesh/sprite entry points");
        return false;
    }

    MSkinPipeline = [MDevice newComputePipelineStateWithFunction:skinFunction error:&error];
    if (MSkinPipeline == nil)
    {
        LogError("metal", "compute pipeline failed: {}", errorText(error));
        return false;
    }

    const auto makeRenderPipeline = [&](id<MTLFunction> vertexFunction, id<MTLFunction> fragmentFunction,
                                        id<MTLRenderPipelineState>* out) -> bool
    {
        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = vertexFunction;
        descriptor.fragmentFunction = fragmentFunction;
        descriptor.colorAttachments[0].pixelFormat = MLayer.pixelFormat;
        descriptor.colorAttachments[0].blendingEnabled = YES;
        descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        *out = [MDevice newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (*out == nil)
        {
            LogError("metal", "render pipeline failed: {}", errorText(error));
            return false;
        }
        return true;
    };

    if (!makeRenderPipeline(meshVertex, meshFragment, &MMeshPipeline) ||
        !makeRenderPipeline(spriteVertex, spriteFragment, &MSpritePipeline))
    {
        return false;
    }

    MTLSamplerDescriptor* samplerDescriptor = [MTLSamplerDescriptor new];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    MDefaultSampler = [MDevice newSamplerStateWithDescriptor:samplerDescriptor];
    return MDefaultSampler != nil;
}

void MetalRHI::CreateBuiltInAtlas()
{
    const std::vector<std::byte> pixels = BuildAtlasPixels();
    TextureArrayDesc desc;
    desc.size = {kAtlasSize, kAtlasSize};
    desc.layers = kAtlasLayers;
    desc.srgb = false;
    const TextureArrayHandle handle = CreateTextureArray(desc, pixels);
    if (!handle.IsValid())
    {
        LogError("metal", "built-in atlas creation failed");
    }
}

id<MTLTexture> MetalRHI::TextureFor(std::uint32_t id) const
{
    if (id == 0 || id == 0xFFFFFFFFu)
    {
        return id < MTextureArrays.size() && MTextureArrays[id] != nullptr ? MTextureArrays[id]->texture : nil;
    }
    return id < MTextureArrays.size() && MTextureArrays[id] != nullptr ? MTextureArrays[id]->texture : nil;
}

id<MTLSamplerState> MetalRHI::SamplerFor(std::uint32_t id) const
{
    if (id < MTextureArrays.size() && MTextureArrays[id] != nullptr && MTextureArrays[id]->sampler != nil)
    {
        return MTextureArrays[id]->sampler;
    }
    return MDefaultSampler;
}

TextureArrayHandle MetalRHI::CreateTextureArray(const TextureArrayDesc& desc, std::span<const std::byte> pixels)
{
    if (desc.size.width == 0 || desc.size.height == 0 || desc.layers == 0)
    {
        return {};
    }
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:(desc.srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm)
                                     width:desc.size.width
                                    height:desc.size.height
                                 mipmapped:NO];
    descriptor.textureType = MTLTextureType2DArray;
    descriptor.arrayLength = desc.layers;
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [MDevice newTextureWithDescriptor:descriptor];
    if (texture == nil)
    {
        LogError("metal", "texture array allocation failed");
        return {};
    }

    const std::size_t layerBytes = static_cast<std::size_t>(desc.size.width) * desc.size.height * 4;
    if (pixels.size() >= layerBytes * desc.layers)
    {
        for (std::uint32_t layer = 0; layer < desc.layers; ++layer)
        {
            [texture replaceRegion:MTLRegionMake2D(0, 0, desc.size.width, desc.size.height)
                       mipmapLevel:0
                             slice:layer
                         withBytes:pixels.data() + layerBytes * layer
                       bytesPerRow:static_cast<NSUInteger>(desc.size.width) * 4];
        }
    }

    MTLSamplerDescriptor* samplerDescriptor = [MTLSamplerDescriptor new];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    id<MTLSamplerState> sampler = [MDevice newSamplerStateWithDescriptor:samplerDescriptor];

    auto array = std::make_unique<MetalTextureArray>();
    array->texture = texture;
    array->sampler = sampler;
    array->layers = desc.layers;

    std::uint32_t id = 0;
    if (MTextureArrays.empty())
    {
        // Id 0 is the built-in atlas
        MTextureArrays.push_back(nullptr);
        id = 0;
        MTextureArrays[0] = std::move(array);
    }
    else
    {
        id = static_cast<std::uint32_t>(MTextureArrays.size());
        MTextureArrays.push_back(std::move(array));
    }
    LogInfo("metal", "texture array #{}: {}x{} x {} layers", id, desc.size.width, desc.size.height, desc.layers);
    return TextureArrayHandle{id};
}

void MetalRHI::DestroyTextureArray(TextureArrayHandle handle)
{
    if (!handle.IsValid() || handle.id == 0 || handle.id >= MTextureArrays.size())
    {
        return;
    }
    WaitIdle();
    MTextureArrays[handle.id].reset();
}

MeshHandle MetalRHI::CreateMesh(const MeshDesc& desc)
{
    if (desc.vertices.empty() || desc.indices.empty())
    {
        return {};
    }

    std::vector<SkinnedVertex> vertices(desc.vertices.size());
    for (std::size_t i = 0; i < desc.vertices.size(); ++i)
    {
        const MeshVertex& source = desc.vertices[i];
        SkinnedVertex& target = vertices[i];
        target.position = Vector4D{source.position.X, source.position.Y, 0.0f, 1.0f};
        target.uv = source.uv;
        target.color = source.color;
        target.padding = 0;
    }

    auto mesh = std::make_unique<MetalStaticMesh>();
    mesh->vertices = CreateBuffer(vertices.size() * sizeof(SkinnedVertex), MTLResourceStorageModePrivate);
    mesh->indices = CreateBuffer(desc.indices.size_bytes(), MTLResourceStorageModePrivate);
    mesh->vertexCount = static_cast<std::uint32_t>(vertices.size());
    mesh->indexCount = static_cast<std::uint32_t>(desc.indices.size());
    mesh->alive = true;

    id<MTLBuffer> stagingVertices = [MDevice newBufferWithBytes:vertices.data()
                                                         length:vertices.size() * sizeof(SkinnedVertex)
                                                        options:MTLResourceStorageModeShared];
    id<MTLBuffer> stagingIndices = [MDevice newBufferWithBytes:desc.indices.data()
                                                        length:desc.indices.size_bytes()
                                                       options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> commandBuffer = [MQueue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    [blit copyFromBuffer:stagingVertices
             sourceOffset:0
                 toBuffer:mesh->vertices
        destinationOffset:0
                     size:vertices.size() * sizeof(SkinnedVertex)];
    [blit copyFromBuffer:stagingIndices
             sourceOffset:0
                 toBuffer:mesh->indices
        destinationOffset:0
                     size:desc.indices.size_bytes()];
    [blit endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    std::uint32_t id = 0;
    if (!MFreeStaticMeshIds.empty())
    {
        id = MFreeStaticMeshIds.back();
        MFreeStaticMeshIds.pop_back();
        MStaticMeshes[id] = std::move(mesh);
    }
    else
    {
        id = static_cast<std::uint32_t>(MStaticMeshes.size());
        MStaticMeshes.push_back(std::move(mesh));
    }
    LogInfo("metal", "static mesh #{}: {} verts, {} indices (device-local)", id, desc.vertices.size(),
            desc.indices.size());
    return MeshHandle{id};
}

void MetalRHI::DestroyMesh(MeshHandle handle)
{
    if (!handle.IsValid() || handle.id >= MStaticMeshes.size() || MStaticMeshes[handle.id] == nullptr)
    {
        return;
    }
    WaitIdle();
    MStaticMeshes[handle.id].reset();
    MFreeStaticMeshIds.push_back(handle.id);
}

void MetalRHI::RecordMeshCompute(id<MTLCommandBuffer> commandBuffer, const FramePacket& packet)
{
    if (packet.meshItems.empty())
    {
        return;
    }
    MetalFrame& frame = MFrames[MFrameSlot];
    if (packet.boneMatrices.size() > MBoneCapacity)
    {
        LogError("metal", "bone palette exceeds capacity");
        return;
    }
    if (!packet.boneMatrices.empty())
    {
        std::memcpy(frame.bones.contents, packet.boneMatrices.data(), packet.boneMatrices.size_bytes());
    }

    std::uint32_t vertexCursor = 0;
    std::uint32_t indexCursor = 0;
    std::uint32_t rangeCount = 0;
    auto* bindVertices = static_cast<MeshVertex*>(frame.bindVertices.contents);
    auto* indices = static_cast<std::uint16_t*>(frame.dynamicIndices.contents);
    auto* ranges = static_cast<MeshSkinRange*>(frame.ranges.contents);

    for (const MeshRenderItem& item : packet.meshItems)
    {
        if (item.mesh.IsValid())
        {
            // Static, already device-local
            continue;
        }
        if (item.vertices.empty() || item.indices.empty())
        {
            continue;
        }
        if (item.boneOffset + item.boneCount > packet.boneMatrices.size())
        {
            LogError("metal", "mesh bone range is outside the frame palette");
            continue;
        }
        if (vertexCursor + item.vertices.size() > MMeshVertexCapacity ||
            indexCursor + item.indices.size() > MMeshIndexCapacity || rangeCount >= MMeshDrawCapacity)
        {
            LogError("metal", "dynamic mesh ring exhausted");
            break;
        }
        std::memcpy(bindVertices + vertexCursor, item.vertices.data(), item.vertices.size_bytes());
        for (std::uint32_t v = 0; v < item.vertices.size(); ++v)
        {
            (bindVertices + vertexCursor)[v].padding = rangeCount;
        }
        std::memcpy(indices + indexCursor, item.indices.data(), item.indices.size_bytes());
        ranges[rangeCount] = MeshSkinRange{item.boneOffset, item.boneCount};
        ++rangeCount;
        vertexCursor += static_cast<std::uint32_t>(item.vertices.size());
        indexCursor += static_cast<std::uint32_t>(item.indices.size());
    }

    if (vertexCursor == 0)
    {
        return;
    }

    id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
    [encoder setComputePipelineState:MSkinPipeline];
    [encoder setBuffer:frame.bindVertices offset:0 atIndex:0];
    [encoder setBuffer:frame.skinnedVertices offset:0 atIndex:1];
    [encoder setBuffer:frame.bones offset:0 atIndex:2];
    [encoder setBuffer:frame.ranges offset:0 atIndex:3];
    [encoder setBytes:&vertexCursor length:sizeof(vertexCursor) atIndex:4];
    [encoder dispatchThreads:MTLSizeMake(vertexCursor, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [encoder endEncoding];
    MStats.skinDispatches = 1;
    MStats.skinnedVertices = vertexCursor;
    MStats.skinRanges = rangeCount;
}

void MetalRHI::RecordPacket(id<MTLCommandBuffer> commandBuffer, id<MTLTexture> color, const FramePacket& packet)
{
    if (color == nil)
    {
        return;
    }
    RecordMeshCompute(commandBuffer, packet);

    MetalFrame& frame = MFrames[MFrameSlot];
    if (!packet.items.empty())
    {
        if (packet.items.size() > MInstanceCapacity)
        {
            LogError("metal", "instance ring exhausted");
        }
        else
        {
            std::memcpy(frame.instances.contents, packet.items.data(), packet.items.size_bytes());
        }
    }

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = color;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor =
        MTLClearColorMake(static_cast<double>(packet.clear.R()) / 255.0, static_cast<double>(packet.clear.G()) / 255.0,
                          static_cast<double>(packet.clear.B()) / 255.0, static_cast<double>(packet.clear.A()) / 255.0);
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    [encoder setViewport:(MTLViewport){0.0, 0.0, static_cast<double>(color.width), static_cast<double>(color.height),
                                       0.0, 1.0}];

    if (!packet.items.empty() && packet.items.size() <= MInstanceCapacity)
    {
        CameraData camera = packet.camera;
        [encoder setRenderPipelineState:MSpritePipeline];
        [encoder setVertexBytes:&camera length:sizeof(camera) atIndex:1];

        // Per-batch texture-array binding, mirroring VulkanRHI::DrawPacket.
        //
        // DrawBatch::textureArray is a full 32-bit handle id: 0 inherits
        // FramePacket::textureArray, and 0xFFFFFFFF means "built-in atlas". The
        // vertex buffer is offset by batch.firstItem so instance 0 is the batch's
        // first RenderItem; Metal has no baseInstance for a plain draw call and
        // offsetting the buffer base is the exact equivalent with no shader change.
        const NSUInteger ScopedItemStride = sizeof(RenderItem);
        const auto ScopedBindArray = [&](std::uint32_t InArrayId)
        {
            id<MTLTexture> ScopedTexture = TextureFor(InArrayId);
            if (ScopedTexture == nil)
            {
                ScopedTexture = TextureFor(0);
            }
            [encoder setFragmentTexture:ScopedTexture atIndex:0];
            [encoder setFragmentSamplerState:SamplerFor(InArrayId) atIndex:0];
        };

        if (packet.batches.empty())
        {
            [encoder setVertexBuffer:frame.instances offset:0 atIndex:0];
            ScopedBindArray(packet.textureArray);
            ++MStats.textureBinds;
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:static_cast<NSUInteger>(packet.items.size())];
            ++MStats.drawCalls;
        }
        else
        {
            // The packet binding is the default; a batch with a non-zero 32-bit
            // textureArray rebinds only when the array actually changes, exactly
            // like VulkanRHI's ScopedBoundArray.
            std::uint32_t ScopedBoundArray = packet.textureArray;
            ScopedBindArray(ScopedBoundArray);
            ++MStats.textureBinds;
            for (const DrawBatch& ScopedBatch : packet.batches)
            {
                if (ScopedBatch.itemCount == 0)
                {
                    continue;
                }
                if (ScopedBatch.firstItem >= packet.items.size() ||
                    ScopedBatch.itemCount > packet.items.size() - ScopedBatch.firstItem)
                {
                    LogError("metal", "DrawBatch [{}, +{}) is outside the {} item span",
                             ScopedBatch.firstItem, ScopedBatch.itemCount,
                             static_cast<std::uint32_t>(packet.items.size()));
                    continue;
                }
                if (ScopedBatch.pipeline != 0 && !MWarnedBatchPipeline)
                {
                    MWarnedBatchPipeline = true;
                    LogWarn("metal",
                            "DrawBatch::pipeline {} is not bindable on Metal yet: the runtime material pipeline cache "
                            "is Vulkan-only; the default sprite pipeline is used",
                            ScopedBatch.pipeline);
                }
                const std::uint32_t ScopedArray =
                    ScopedBatch.textureArray != 0 ? ScopedBatch.textureArray : packet.textureArray;
                [encoder setVertexBuffer:frame.instances
                                  offset:static_cast<NSUInteger>(ScopedBatch.firstItem) * ScopedItemStride
                                 atIndex:0];
                if (ScopedArray != ScopedBoundArray)
                {
                    ScopedBindArray(ScopedArray);
                    ScopedBoundArray = ScopedArray;
                    ++MStats.textureBinds;
                }
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:0
                            vertexCount:6
                          instanceCount:static_cast<NSUInteger>(ScopedBatch.itemCount)];
                ++MStats.drawCalls;
            }
        }
    }

    if (!packet.meshItems.empty())
    {
        Mat4 viewProjection;
        if (packet.meshUseViewProjection)
        {
            viewProjection = packet.meshViewProjection;
        }
        else
        {
            viewProjection = MakeOrthoProjection(packet.camera);
        }
        [encoder setRenderPipelineState:MMeshPipeline];
        [encoder setVertexBytes:&viewProjection length:sizeof(viewProjection) atIndex:3];

        std::uint32_t vertexCursor = 0;
        std::uint32_t indexCursor = 0;
        for (const MeshRenderItem& item : packet.meshItems)
        {
            MeshDrawData draw;
            draw.model = item.model;
            draw.layer = item.layer;
            draw.flags = 0;
            id<MTLBuffer> vertexBuffer = frame.skinnedVertices;
            id<MTLBuffer> indexBuffer = frame.dynamicIndices;
            NSUInteger indexCount = 0;
            NSUInteger indexOffset = 0;

            if (item.mesh.IsValid())
            {
                if (item.mesh.id >= MStaticMeshes.size() || MStaticMeshes[item.mesh.id] == nullptr)
                {
                    continue;
                }
                const MetalStaticMesh& mesh = *MStaticMeshes[item.mesh.id];
                draw.vertexBase = 0;
                draw.flags = MeshDrawStatic;
                vertexBuffer = mesh.vertices;
                indexBuffer = mesh.indices;
                indexCount = mesh.indexCount;
            }
            else
            {
                if (item.vertices.empty() || item.indices.empty() ||
                    item.boneOffset + item.boneCount > packet.boneMatrices.size() ||
                    vertexCursor + item.vertices.size() > MMeshVertexCapacity ||
                    indexCursor + item.indices.size() > MMeshIndexCapacity)
                {
                    continue;
                }
                draw.vertexBase = vertexCursor;
                vertexBuffer = frame.skinnedVertices;
                indexBuffer = frame.dynamicIndices;
                indexCount = item.indices.size();
                indexOffset = static_cast<NSUInteger>(indexCursor) * sizeof(std::uint16_t);
                vertexCursor += static_cast<std::uint32_t>(item.vertices.size());
                indexCursor += static_cast<std::uint32_t>(item.indices.size());
            }

            [encoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
            // Static and skinned share binding 1; static draws select via the flag,
            // so binding the same buffer at 1 keeps the shader valid.
            [encoder setVertexBuffer:vertexBuffer offset:0 atIndex:1];
            [encoder setVertexBytes:&draw length:sizeof(draw) atIndex:2];
            const std::uint32_t textureId = item.textureArray;
            id<MTLTexture> atlasTexture = TextureFor(textureId);
            if (atlasTexture == nil)
            {
                atlasTexture = TextureFor(0);
            }
            [encoder setFragmentTexture:atlasTexture atIndex:0];
            [encoder setFragmentSamplerState:SamplerFor(textureId) atIndex:0];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:indexCount
                                 indexType:MTLIndexTypeUInt16
                               indexBuffer:indexBuffer
                         indexBufferOffset:indexOffset];
            ++MStats.drawCalls;
            ++MStats.meshDraws;
            ++MStats.meshIndirectCalls;
            MStats.staticMeshDraws += item.mesh.IsValid() ? 1u : 0u;
        }
    }

    [encoder endEncoding];
}

bool MetalRHI::BeginFrame()
{
    if (MInFrame)
    {
        return false;
    }
    dispatch_semaphore_wait(MFrameSemaphore, DISPATCH_TIME_FOREVER);
    MDrawable = [MLayer nextDrawable];
    if (MDrawable == nil)
    {
        dispatch_semaphore_signal(MFrameSemaphore);
        return false;
    }
    MCommandBuffer = [MQueue commandBuffer];
    if (MCommandBuffer == nil)
    {
        MDrawable = nil;
        dispatch_semaphore_signal(MFrameSemaphore);
        return false;
    }
    MFrameSlot = static_cast<std::uint32_t>(MFrameIndex % MFramesInFlight);
    ++MFrameIndex;
    MStats = {};
    MStats.frameIndex = MFrameIndex;
    MInFrame = true;
    return true;
}

void MetalRHI::Submit(const FramePacket& packet)
{
    if (!MInFrame || MCommandBuffer == nil || MDrawable == nil)
    {
        return;
    }
    if ((packet.accessibility.flags & AccessibilityBlind) != 0)
    {
        // BlindMode: record the clear so the drawable is defined, but no
        // instance upload, no skinning compute dispatch and no draw call.
        FramePacket clearOnly = packet;
        clearOnly.items = {};
        clearOnly.keys = {};
        clearOnly.batches = {};
        clearOnly.meshItems = {};
        clearOnly.boneMatrices = {};
        RecordPacket(MCommandBuffer, MDrawable.texture, clearOnly);
        return;
    }
    MStats.items += static_cast<std::uint32_t>(packet.items.size());
    MStats.batches += static_cast<std::uint32_t>(packet.batches.size());
    MStats.meshItems += static_cast<std::uint32_t>(packet.meshItems.size());
    MStats.uploadedBytes += packet.items.size_bytes() + packet.boneMatrices.size_bytes();
    RecordPacket(MCommandBuffer, MDrawable.texture, packet);
}

void MetalRHI::EndFrame()
{
    if (!MInFrame)
    {
        return;
    }
    if (MCommandBuffer != nil && MDrawable != nil)
    {
        [MCommandBuffer presentDrawable:MDrawable];
        dispatch_semaphore_t semaphore = MFrameSemaphore;
        [MCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
          dispatch_semaphore_signal(semaphore);
        }];
        [MCommandBuffer commit];
    }
    else
    {
        dispatch_semaphore_signal(MFrameSemaphore);
    }
    MCommandBuffer = nil;
    MDrawable = nil;
    MInFrame = false;
}

void MetalRHI::WaitIdle()
{
    id<MTLCommandBuffer> commandBuffer = [MQueue commandBuffer];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

void MetalRHI::Resize(Extent2D size)
{
    if (MLayer == nil)
    {
        return;
    }
    const float density = SDL_GetWindowPixelDensity(static_cast<SDL_Window*>(MDesc.nativeWindow));
    MLayer.drawableSize =
        CGSizeMake(static_cast<CGFloat>(size.width) * density, static_cast<CGFloat>(size.height) * density);
    MDesc.size = size;
}

RenderTargetHandle MetalRHI::CreateRenderTarget(const RenderTargetDesc& desc)
{
    if (desc.size.width == 0 || desc.size.height == 0)
    {
        return {};
    }
    // Mesh/sprite pipelines are built for the drawable format. The editor's
    // offscreen target must use the same format, so sRGB is the only supported
    // render-target format on Metal.
    MTLTextureDescriptor* textureDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MLayer.pixelFormat
                                                           width:desc.size.width
                                                          height:desc.size.height
                                                       mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [MDevice newTextureWithDescriptor:textureDescriptor];
    if (texture == nil)
    {
        return {};
    }

    auto target = std::make_unique<MetalRenderTarget>();
    target->texture = texture;
    target->extent = desc.size;
    target->srgb = desc.srgb;

    std::uint32_t id = 0;
    if (!MFreeRenderTargetIds.empty())
    {
        id = MFreeRenderTargetIds.back();
        MFreeRenderTargetIds.pop_back();
        MRenderTargets[id] = std::move(target);
    }
    else
    {
        id = static_cast<std::uint32_t>(MRenderTargets.size());
        MRenderTargets.push_back(std::move(target));
    }
    return RenderTargetHandle{id};
}

void MetalRHI::DestroyRenderTarget(RenderTargetHandle handle)
{
    if (!handle.IsValid() || handle.id >= MRenderTargets.size() || MRenderTargets[handle.id] == nullptr)
    {
        return;
    }
    WaitIdle();
    MRenderTargets[handle.id].reset();
    MFreeRenderTargetIds.push_back(handle.id);
}

bool MetalRHI::RenderToTarget(RenderTargetHandle handle, const FramePacket& packet)
{
    if (!MInFrame || !handle.IsValid() || handle.id >= MRenderTargets.size() || MRenderTargets[handle.id] == nullptr)
    {
        return false;
    }
    if ((packet.accessibility.flags & AccessibilityBlind) != 0)
    {
        FramePacket clearOnly = packet;
        clearOnly.items = {};
        clearOnly.keys = {};
        clearOnly.batches = {};
        clearOnly.meshItems = {};
        clearOnly.boneMatrices = {};
        RecordPacket(MCommandBuffer, MRenderTargets[handle.id]->texture, clearOnly);
        return true;
    }
    MStats.items += static_cast<std::uint32_t>(packet.items.size());
    MStats.batches += static_cast<std::uint32_t>(packet.batches.size());
    MStats.meshItems += static_cast<std::uint32_t>(packet.meshItems.size());
    RecordPacket(MCommandBuffer, MRenderTargets[handle.id]->texture, packet);
    return true;
}

bool MetalRHI::RenderTargetTexture(RenderTargetHandle handle, NativeTextureView& view) const
{
    if (!handle.IsValid() || handle.id >= MRenderTargets.size() || MRenderTargets[handle.id] == nullptr)
    {
        return false;
    }
    view.view = (__bridge void*)MRenderTargets[handle.id]->texture;
    view.layout = 0;
    return true;
}

void MetalRHI::Shutdown()
{
    if (MDevice != nil)
    {
        WaitIdle();
    }
    for (MetalFrame& frame : MFrames)
    {
        frame.bindVertices = nil;
        frame.dynamicIndices = nil;
        frame.skinnedVertices = nil;
        frame.bones = nil;
        frame.ranges = nil;
        frame.instances = nil;
    }
    MTextureArrays.clear();
    MStaticMeshes.clear();
    MFreeStaticMeshIds.clear();
    MRenderTargets.clear();
    MFreeRenderTargetIds.clear();
    MSkinPipeline = nil;
    MMeshPipeline = nil;
    MSpritePipeline = nil;
    MDefaultSampler = nil;
    MLibrary = nil;
    MQueue = nil;
    MDevice = nil;
    MLayer = nil;
    if (MMetalView != nullptr)
    {
        SDL_Metal_DestroyView(MMetalView);
        MMetalView = nullptr;
    }
    if (MFrameSemaphore != nullptr)
    {
        MFrameSemaphore = nullptr;
    }
    MInFrame = false;
}
}

std::unique_ptr<RHI> CreateMetalRHI()
{
    return std::make_unique<MetalRHI>();
}
}
