#include "Engine/RHI/RHI.hpp"

#include <cstdlib>

#include <SDL3/SDL.h>

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/RHI/RingBuffer.hpp"

#include <algorithm>
#include <cmath>
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
        MStats.items += static_cast<std::uint32_t>(InPacket.items.size());
        MStats.batches += static_cast<std::uint32_t>(InPacket.batches.size());
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

std::vector<std::byte> BuildBuiltInAtlasPixels(std::uint32_t InSize, std::uint32_t InLayers)
{
    const std::uint32_t ScopedSize = InSize == 0 ? 128u : InSize;
    const std::uint32_t ScopedLayers = InLayers == 0 ? 4u : InLayers;
    std::vector<std::byte> ScopedPixels(static_cast<std::size_t>(ScopedSize) * ScopedSize * ScopedLayers * 4u);
    const auto ScopedWrite = [&ScopedPixels, ScopedSize](std::uint32_t InLayer, std::uint32_t InX, std::uint32_t InY,
                                                         std::uint8_t InR, std::uint8_t InG, std::uint8_t InB,
                                                         std::uint8_t InA)
    {
        const std::size_t ScopedIndex =
            ((static_cast<std::size_t>(InLayer) * ScopedSize + InY) * ScopedSize + InX) * 4u;
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
            if (ScopedLayers > 1)
            {
                const bool ScopedEven = ((ScopedX / 16u) + (ScopedY / 16u)) % 2u == 0u;
                ScopedWrite(1, ScopedX, ScopedY, ScopedEven ? 255 : 40, ScopedEven ? 80 : 20,
                            ScopedEven ? 140 : 60, 255);
            }
            if (ScopedLayers > 2)
            {
                const float ScopedDx = static_cast<float>(ScopedX) - static_cast<float>(ScopedSize) * 0.5f;
                const float ScopedDy = static_cast<float>(ScopedY) - static_cast<float>(ScopedSize) * 0.5f;
                const float ScopedRadius =
                    std::sqrt(ScopedDx * ScopedDx + ScopedDy * ScopedDy) / (static_cast<float>(ScopedSize) * 0.5f);
                const auto ScopedShade =
                    static_cast<std::uint8_t>(std::clamp(1.0f - ScopedRadius, 0.0f, 1.0f) * 255.0f);
                ScopedWrite(2, ScopedX, ScopedY, 40, ScopedShade, 90, 255);
            }
            if (ScopedLayers > 3)
            {
                const float ScopedDx = static_cast<float>(ScopedX) - static_cast<float>(ScopedSize) * 0.5f;
                const float ScopedDy = static_cast<float>(ScopedY) - static_cast<float>(ScopedSize) * 0.5f;
                const float ScopedRadius =
                    std::sqrt(ScopedDx * ScopedDx + ScopedDy * ScopedDy) / (static_cast<float>(ScopedSize) * 0.5f);
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
    }
    return ScopedPixels;
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
    bool ScopedForceNull = false;
    // Manual/diagnostic override: FBY_RHI_BACKEND=vulkan, d3d12, d3d11, metal,
    // null or auto. Unset/auto prefers Vulkan when it is available and falls back
    // to the platform backend, so games do not have to probe APIs themselves.
    if (const char* ScopedOverride = std::getenv("FBY_RHI_BACKEND"); ScopedOverride != nullptr)
    {
        if (std::strcmp(ScopedOverride, "auto") != 0 && ScopedOverride[0] != '\0')
        {
            ScopedBackend = BackendFromName(ScopedOverride);
            if (ScopedBackend == Backend::Null)
            {
                if (std::strcmp(ScopedOverride, "null") != 0)
                {
                    LogError("RHI",
                             "unknown FBY_RHI_BACKEND='{}'; expected vulkan, d3d12, d3d11, metal, null or auto",
                             ScopedOverride);
                    return nullptr;
                }
                // An explicit "null" must not fall into the auto probe.
                ScopedForceNull = true;
            }
        }
    }
    const bool ScopedAuto =
        ScopedBackend == Backend::Null && !ScopedForceNull && ScopedDesc.nativeWindow != nullptr;

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
