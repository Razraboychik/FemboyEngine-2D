#include "Engine/RHI/RHI.hpp"

#include <cstdlib>

#include <SDL3/SDL.h>

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/RHI/RingBuffer.hpp"

#include <algorithm>
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
        // A zero frame count would divide by zero in TransientRing::BeginFrame.
        const std::uint32_t ScopedFramesInFlight = std::clamp(InDesc.framesInFlight, 1u, 8u);
        MInfo.apiVersion = 0;
        MInfo.framesInFlight = ScopedFramesInFlight;
        MInfo.maxItemsPerFrame = InDesc.maxItemsPerFrame;
        MInfo.maxTextureLayers = InDesc.maxTextureLayers;
        MInfo.discrete = false;

        MTransientBytes = static_cast<std::size_t>(InDesc.maxItemsPerFrame) * sizeof(RenderItem) +
                          static_cast<std::size_t>(InDesc.maxItemsPerFrame) * sizeof(RenderKey);
        MTransientStorage.resize(MTransientBytes * ScopedFramesInFlight);
        // TransientRing is not assignable (it owns a mapping), so it is built.
        MRing.emplace(MTransientStorage.data(), MTransientBytes, ScopedFramesInFlight);
        LogInfo("RHI", "null backend initialised ({} frames in flight)", InDesc.framesInFlight);
        return true;
    }

    void Shutdown() override
    {
        MRing.reset();
        MInFrame = false;
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
        if (!MRing.has_value())
        {
            return false;
        }
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
        if (!MRing.has_value())
        {
            return;
        }
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
    // Null with a window means "auto": Vulkan first, then the platform backend.
    return Backend::Null;
}

std::unique_ptr<RHI> CreateD3D12RHI();
std::unique_ptr<RHI> CreateD3D11RHI();

namespace
{

std::unique_ptr<RHI> TryCreateBackend(Backend InBackend, const DeviceDesc& InDesc)
{
    switch (InBackend)
    {
        case Backend::Vulkan:
#if FBY_VULKAN_AVAILABLE
        {
            auto ScopedDevice = CreateVulkanRHI();
            if (!ScopedDevice || !ScopedDevice->Initialize(InDesc))
            {
                LogWarn("RHI", "Vulkan initialization failed; trying the next backend");
                return nullptr;
            }
            return ScopedDevice;
        }
#else
            return nullptr;
#endif
        case Backend::Metal:
#if FBY_METAL_AVAILABLE
        {
            auto ScopedDevice = CreateMetalRHI();
            if (!ScopedDevice || !ScopedDevice->Initialize(InDesc))
            {
                LogWarn("RHI", "Metal initialization failed; trying the next backend");
                return nullptr;
            }
            return ScopedDevice;
        }
#else
            return nullptr;
#endif
        case Backend::D3D12:
#if defined(_WIN32)
        {
            auto ScopedDevice = CreateD3D12RHI();
            if (!ScopedDevice || !ScopedDevice->Initialize(InDesc))
            {
                LogWarn("RHI", "D3D12 initialization failed; trying the next backend");
                return nullptr;
            }
            return ScopedDevice;
        }
#else
            return nullptr;
#endif
        case Backend::D3D11:
#if defined(_WIN32)
        {
            auto ScopedDevice = CreateD3D11RHI();
            if (!ScopedDevice || !ScopedDevice->Initialize(InDesc))
            {
                LogWarn("RHI", "D3D11 initialization failed");
                return nullptr;
            }
            return ScopedDevice;
        }
#else
            return nullptr;
#endif
        case Backend::Null:
            return nullptr;
    }
    return nullptr;
}

}

std::unique_ptr<RHI> RHI::Create(const DeviceDesc& InDesc)
{
    DeviceDesc ScopedDesc = InDesc;
    if (ScopedDesc.size.width == 0 || ScopedDesc.size.height == 0)
    {
        if (ScopedDesc.nativeWindow != nullptr)
        {
            int ScopedWidth = 0;
            int ScopedHeight = 0;
            if (SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(ScopedDesc.nativeWindow), &ScopedWidth,
                                          &ScopedHeight) &&
                ScopedWidth > 0 && ScopedHeight > 0)
            {
                ScopedDesc.size = {static_cast<std::uint32_t>(ScopedWidth),
                                   static_cast<std::uint32_t>(ScopedHeight)};
            }
        }
        if (ScopedDesc.size.width == 0 || ScopedDesc.size.height == 0)
        {
            ScopedDesc.size = {1280, 720};
        }
    }
    Backend ScopedBackend = ScopedDesc.backend;
    // Manual/diagnostic override: FBY_RHI_BACKEND=vulkan, d3d12, d3d11, metal,
    // null or auto. Unset/auto prefers Vulkan when it is available and falls back
    // to the platform backend, so games do not have to probe APIs themselves.
    if (const char* ScopedOverride = std::getenv("FBY_RHI_BACKEND"); ScopedOverride != nullptr)
    {
        ScopedBackend = BackendFromName(ScopedOverride);
    }
    const bool ScopedAuto = ScopedBackend == Backend::Null && ScopedDesc.nativeWindow != nullptr;

    if (ScopedAuto)
    {
#if FBY_METAL_AVAILABLE
        if (auto ScopedDevice = TryCreateBackend(Backend::Metal, ScopedDesc))
        {
            return ScopedDevice;
        }
#endif
#if FBY_VULKAN_AVAILABLE
        if (auto ScopedDevice = TryCreateBackend(Backend::Vulkan, ScopedDesc))
        {
            return ScopedDevice;
        }
#endif
#if defined(_WIN32)
        if (auto ScopedDevice = TryCreateBackend(Backend::D3D12, ScopedDesc))
        {
            return ScopedDevice;
        }
        if (auto ScopedDevice = TryCreateBackend(Backend::D3D11, ScopedDesc))
        {
            return ScopedDevice;
        }
#endif
        LogError("RHI", "No graphics backend could be initialized");
        return nullptr;
    }

    if (ScopedBackend == Backend::Null)
    {
        auto ScopedNullDevice = std::make_unique<NullRHI>();
        if (!ScopedNullDevice->Initialize(ScopedDesc))
        {
            return nullptr;
        }
        return ScopedNullDevice;
    }

    auto ScopedDevice = TryCreateBackend(ScopedBackend, ScopedDesc);
    if (!ScopedDevice)
    {
        LogError("RHI", "Requested backend failed to initialize");
    }
    return ScopedDevice;
}
}
