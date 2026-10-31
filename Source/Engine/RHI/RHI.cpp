#include "Engine/RHI/RHI.hpp"

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/RHI/RingBuffer.hpp"

#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#if defined(FBY_HAS_VULKAN_HEADERS) && !defined(__APPLE__)
#include "Engine/RHI/Vulkan/VulkanRHI.hpp"
#define FBY_VULKAN_AVAILABLE 1
#else
#define FBY_VULKAN_AVAILABLE 0
#endif

#if defined(__APPLE__)
#include "Engine/RHI/Metal/MetalRHI.hpp"
#define FBY_METAL_AVAILABLE 1
#else
#define FBY_METAL_AVAILABLE 0
#endif

namespace Fby::Rhi
{
namespace
{
// Null backend
class NullRHI final : public RHI
{
public:
    bool Initialize(const DeviceDesc& InDesc) override
    {
        MDesc = InDesc;
        MInfo.backend = Backend::Null;
        std::strncpy(MInfo.name, "Null RHI", sizeof(MInfo.name) - 1);
        MInfo.apiVersion = 0;
        MInfo.framesInFlight = InDesc.framesInFlight;
        MInfo.maxItemsPerFrame = InDesc.maxItemsPerFrame;
        MInfo.maxTextureLayers = InDesc.maxTextureLayers;
        MInfo.discrete = false;

        MTransientBytes = static_cast<std::size_t>(InDesc.maxItemsPerFrame) * sizeof(RenderItem) +
                          static_cast<std::size_t>(InDesc.maxItemsPerFrame) * sizeof(RenderKey);
        MTransientStorage.resize(MTransientBytes * InDesc.framesInFlight);
        // TransientRing is not assignable (it owns a mapping), so it is built in
        MRing.emplace(MTransientStorage.data(), MTransientBytes, InDesc.framesInFlight);
        LogInfo("RHI", "null backend initialised ({} frames in flight)", InDesc.framesInFlight);
        return true;
    }

    void Shutdown() override
    {
        MTransientStorage.clear();
        MTransientStorage.shrink_to_fit();
    }

    void Resize(Extent2D InSize) override
    {
        MExtent = InSize;
    }

    void WaitIdle() override
    {
    }

    bool BeginFrame() override
    {
        ++MFrameIndex;
        MRing->BeginFrame(MFrameIndex);
        MStats = {};
        MStats.frameIndex = MFrameIndex;
        MInFrame = true;
        return true;
    }

    void Submit(const FramePacket& InPacket) override
    {
        FBY_ASSERT(MInFrame);
        const auto ScopedItems = MRing->Upload(InPacket.items);
        const auto ScopedKeys = MRing->Upload(InPacket.keys);
        if (ScopedItems == TransientRing::kInvalidOffset || ScopedKeys == TransientRing::kInvalidOffset)
        {
            LogError("RHI", "null backend: transient ring exhausted ({} items)", InPacket.items.size());
            return;
        }
        MStats.items = static_cast<std::uint32_t>(InPacket.items.size());
        MStats.batches = static_cast<std::uint32_t>(InPacket.batches.size());
        MStats.uploadedBytes += InPacket.items.size_bytes() + InPacket.keys.size_bytes();
    }

    void EndFrame() override
    {
        MInFrame = false;
    }

    TextureArrayHandle CreateTextureArray(const TextureArrayDesc&, std::span<const std::byte>) override
    {
        return TextureArrayHandle{MNextTexture++};
    }

    void DestroyTextureArray(TextureArrayHandle) override
    {
    }

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
        return Backend::Null;
    }
    [[nodiscard]] std::size_t TransientCapacity() const noexcept override
    {
        return MTransientBytes;
    }

private:
    DeviceDesc MDesc{};
    DeviceInfo MInfo{};
    std::optional<TransientRing> MRing;
    std::vector<std::byte> MTransientStorage;
    std::size_t MTransientBytes = 0;
    FrameStats MStats{};
    Extent2D MExtent{};
    std::uint64_t MFrameIndex = 0;
    std::uint32_t MNextTexture = 1;
    bool MInFrame = false;
};
}

Backend SelectDefaultBackend() noexcept
{
#if FBY_METAL_AVAILABLE
    return Backend::Metal;
#elif FBY_VULKAN_AVAILABLE
    return Backend::Vulkan;
#else
    return Backend::Null;
#endif
}

std::unique_ptr<RHI> RHI::Create(const DeviceDesc& InDesc)
{
    Backend ScopedBackend = InDesc.backend;
    if (ScopedBackend == Backend::Null && InDesc.nativeWindow != nullptr)
    {
        // "Null" means headless; a caller that passed a window expected a real
        ScopedBackend = SelectDefaultBackend();
    }

    switch (ScopedBackend)
    {
        case Backend::Vulkan:
#if FBY_VULKAN_AVAILABLE
        {
            auto ScopedDevice = CreateVulkanRHI();
            if (!ScopedDevice || !ScopedDevice->Initialize(InDesc))
            {
                return nullptr;
            }
            return ScopedDevice;
        }
#else
            LogError("RHI", "Vulkan backend requested but this build has no Vulkan headers");
            return nullptr;
#endif
        case Backend::Metal:
#if FBY_METAL_AVAILABLE
        {
            auto device = CreateMetalRHI();
            if (!device || !device->Initialize(desc))
            {
                return nullptr;
            }
            return device;
        }
#else
            LogError("RHI", "Metal backend requested on a platform without Metal");
            return nullptr;
#endif
        case Backend::Null:
            break;
    }

    auto ScopedNullDevice = std::make_unique<NullRHI>();
    if (!ScopedNullDevice->Initialize(InDesc))
    {
        return nullptr;
    }
    return ScopedNullDevice;
}
}
