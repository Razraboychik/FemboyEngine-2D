// FemboyEngine2D - minimal D3D12 RHI backend (device/swapchain/present).
#include "Engine/RHI/RHI.hpp"
#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/RHI/RenderPasses.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/RHI/Shaders/DxShaderBlobs.hpp"

#if defined(_WIN32)
#include "Engine/Core/Log.hpp"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <SDL3/SDL.h>
#include <windows.h>

#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <string>

namespace Fby::Rhi
{
namespace
{
using Microsoft::WRL::ComPtr;
[[nodiscard]] bool IsDxilBytecode(std::span<const std::byte> InCode) noexcept
{
    // dxc wraps the bitcode in a DXBC container, so the "DXIL" part name lives
    // somewhere inside the blob rather than at offset zero.
    constexpr char ScopedMagic[4] = {'D', 'X', 'I', 'L'};
    for (std::size_t Index = 0; Index + 4 <= InCode.size(); ++Index)
    {
        if (std::memcmp(InCode.data() + Index, ScopedMagic, 4) == 0)
        {
            return true;
        }
    }
    return false;
}


[[nodiscard]] DXGI_FORMAT TextureFormatToDxgi(Fby::Assets::TextureFormat InFormat, bool InSrgb) noexcept
{
    switch (InFormat)
    {
    case Fby::Assets::TextureFormat::Rgba8:
        return InSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    case Fby::Assets::TextureFormat::Bc7Unorm:
        return InSrgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
    case Fby::Assets::TextureFormat::Bc7Srgb:
        return DXGI_FORMAT_BC7_UNORM_SRGB;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}


class D3D12RHI final : public RHI
{
public:
    bool Initialize(const DeviceDesc& InDesc) override
    {
        constexpr std::uint32_t kMaxBackendItems = 1u << 16;
        const std::uint32_t ScopedRequestedItems = std::max(1u, InDesc.maxItemsPerFrame);
        const std::uint32_t ScopedItems = std::min(ScopedRequestedItems, kMaxBackendItems);
        MVertexCapacity = ScopedItems * 6u;
        if (ScopedItems < ScopedRequestedItems)
        {
            LogWarn("d3d12", "sprite item capacity clamped from {} to {}", ScopedRequestedItems, ScopedItems);
        }
        if (!CreateFactoryAndDevice())
        {
            return false;
        }
        if (!CreateSwapchain(InDesc))
        {
            return false;
        }
        MExtent = InDesc.size;
        MInfo.maxTextureLayers = InDesc.maxTextureLayers;
        MInfo.framesInFlight = 1;
        MInfo.maxItemsPerFrame = MVertexCapacity / 6u;
        MInfo.tier = GpuTier::Baseline;
        MInfo.platform = 0;
        // The engine-side temporal/upscale/sharpen chain and custom frame
        // generation both run on D3D12, so report them as available.
        MInfo.upscalers.motionVectors = true;
        MInfo.upscalers.fsr1 = true;
        MInfo.upscalers.fsr2 = true;
        MInfo.upscalers.bicubic = true;
        MInfo.upscalers.frameGeneration = true;
        MVsync = InDesc.vsync;
        if (!CreateCommands())
        {
            return false;
        }
        if (!CreatePostFxResources())
        {
            LogWarn("d3d12", "post-FX resources unavailable");
        }
        return true;
    }

    void Shutdown() override
    {
        WaitIdle();
        SavePipelineLibrary();
        MPipelineLibrary.Reset();
        for (ComPtr<ID3D12Resource>& ScopedBuffer : MBackBuffers)
        {
            ScopedBuffer.Reset();
        }
        MVertexBuffer.Reset();
        MRootSignature.Reset();
        MMaterialRootSignature.Reset();
        MPipeline.Reset();
        MCommandList.Reset();
        MAllocator.Reset();
        MQueue.Reset();
        MSwapchain.Reset();
        MDevice.Reset();
        MFactory.Reset();
        if (MFenceEvent != nullptr)
        {
            CloseHandle(MFenceEvent);
            MFenceEvent = nullptr;
        }
    }

    [[nodiscard]] bool SetVSync(bool InEnabled) override
    {
        MVsync = InEnabled;
        return true;
    }

    [[nodiscard]] bool VSyncEnabled() const noexcept override
    {
        return MVsync;
    }

    void Resize(Extent2D InSize) override
    {
        if (MDevice == nullptr || MSwapchain == nullptr || MCommandList == nullptr || InSize.width == 0 ||
            InSize.height == 0)
        {
            return;
        }
        WaitIdle();
        // Drop the command-list and RTV references before ResizeBuffers.
        MAllocator->Reset();
        MCommandList->Reset(MAllocator.Get(), nullptr);
        MCommandList->Close();
        for (ComPtr<ID3D12Resource>& ScopedBuffer : MBackBuffers)
        {
            ScopedBuffer.Reset();
        }
        const HRESULT ScopedResize = MSwapchain->ResizeBuffers(2, InSize.width, InSize.height,
                                                                 DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        if (FAILED(ScopedResize))
        {
            if (ScopedResize == DXGI_ERROR_DEVICE_REMOVED || ScopedResize == DXGI_ERROR_DEVICE_RESET)
            {
                MInfo.deviceLost = true;
            }
            LogWarn("d3d12", "swapchain resize failed (0x{:08X})", static_cast<unsigned>(ScopedResize));
            return;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
        {
            if (FAILED(MSwapchain->GetBuffer(ScopedIndex, IID_PPV_ARGS(&MBackBuffers[ScopedIndex]))))
            {
                LogWarn("d3d12", "back buffer recreation failed");
                return;
            }
            MDevice->CreateRenderTargetView(MBackBuffers[ScopedIndex].Get(), nullptr, ScopedRtv);
            ScopedRtv.ptr += MRtvSize;
        }
        MExtent = InSize;
        // Size-dependent effect targets are recreated; accessibility keeps its
        // resources per render target and rebuilds them on the next use.
        if (!CreateDepthTarget())
        {
            LogWarn("d3d12", "depth target recreation failed");
        }
        MAccessibilityResources.clear();
        if (!CreatePostFxResources())
        {
            LogWarn("d3d12", "post-FX resources unavailable after resize");
        }
    }

    void WaitIdle() override
    {
        if (MQueue != nullptr && MFence != nullptr && MFenceEvent != nullptr)
        {
            ++MFenceValue;
            MQueue->Signal(MFence.Get(), MFenceValue);
            MFence->SetEventOnCompletion(MFenceValue, MFenceEvent);
            WaitForSingleObject(MFenceEvent, 5000);
        }
    }

    bool BeginFrame() override
    {
        if (MCommandList == nullptr || MAllocator == nullptr || MPipeline == nullptr)
        {
            return false;
        }
        WaitIdle();
        MAllocator->Reset();
        MCommandList->Reset(MAllocator.Get(), nullptr);
        const std::uint32_t ScopedIndex = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = MBackBuffers[ScopedIndex].Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedRtv.ptr += ScopedIndex * MRtvSize;
        const float ScopedClear[4] = {0.08f, 0.09f, 0.12f, 1.0f};
        MCommandList->ClearRenderTargetView(ScopedRtv, ScopedClear, 0, nullptr);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedDsv = MDepthHeap->GetCPUDescriptorHandleForHeapStart();
        MCommandList->ClearDepthStencilView(ScopedDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        MCommandList->OMSetRenderTargets(1, &ScopedRtv, FALSE, &ScopedDsv);
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(MExtent.width), static_cast<float>(MExtent.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, static_cast<LONG>(MExtent.width), static_cast<LONG>(MExtent.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        MCommandList->SetPipelineState(MPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        MInFrame = true;
        return true;
    }

    [[nodiscard]] std::array<float, 28> BuildMaterialConstants(const FramePacket& InPacket,
                                                               bool InSpriteDomain) const
    {
        std::array<float, 28> ScopedBlock{};
        if (InSpriteDomain)
        {
            ScopedBlock[0] = InPacket.camera.viewport.X;
            ScopedBlock[1] = InPacket.camera.viewport.Y;
            ScopedBlock[2] = InPacket.camera.zoom;
            ScopedBlock[4] = InPacket.camera.center.X;
            ScopedBlock[5] = InPacket.camera.center.Y;
            ScopedBlock[16] = static_cast<float>(MStats.frameIndex % 4u);
            ScopedBlock[20] = InPacket.timeSeconds;
            ScopedBlock[21] = InPacket.camera.viewport.X;
            ScopedBlock[22] = InPacket.camera.viewport.Y;
            return ScopedBlock;
        }
        Mat4 ScopedViewProjection;
        if (InPacket.meshUseViewProjection)
        {
            ScopedViewProjection = InPacket.meshViewProjection;
        }
        else
        {
            const float ScopedScaleX =
                InPacket.camera.viewport.X > 0.0f ? (2.0f * InPacket.camera.zoom) / InPacket.camera.viewport.X : 0.0f;
            const float ScopedScaleY =
                InPacket.camera.viewport.Y > 0.0f ? (-2.0f * InPacket.camera.zoom) / InPacket.camera.viewport.Y : 0.0f;
            ScopedViewProjection.m[0] = ScopedScaleX;
            ScopedViewProjection.m[5] = ScopedScaleY;
            ScopedViewProjection.m[12] = -InPacket.camera.center.X * ScopedScaleX;
            ScopedViewProjection.m[13] = -InPacket.camera.center.Y * ScopedScaleY;
        }
        std::memcpy(ScopedBlock.data(), &ScopedViewProjection, sizeof(Mat4));
        ScopedBlock[16] = InPacket.timeSeconds;
        ScopedBlock[17] = InPacket.camera.viewport.X;
        ScopedBlock[18] = InPacket.camera.viewport.Y;
        return ScopedBlock;
    }

    void Submit(const FramePacket& InPacket) override
    {
        if (MCommandList == nullptr || InPacket.items.empty() || MVertexCapacity < InPacket.items.size() * 6)
        {
            return;
        }
        const bool ScopedPostFx = (InPacket.postFxEnabled || MPostFxGraphPending) && MPostFxReady;
        if (ScopedPostFx)
        {
            MPostFxPending = true;
            MPostFxPendingSettings = InPacket.postFx;
            D3D12_CPU_DESCRIPTOR_HANDLE ScopedSceneRtv = MSceneRtvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_CPU_DESCRIPTOR_HANDLE ScopedSceneDsv = MSceneDsvHeap->GetCPUDescriptorHandleForHeapStart();
            const float ScopedSceneClear[4] = {static_cast<float>(InPacket.clear.R()) / 255.0f,
                                               static_cast<float>(InPacket.clear.G()) / 255.0f,
                                               static_cast<float>(InPacket.clear.B()) / 255.0f,
                                               static_cast<float>(InPacket.clear.A()) / 255.0f};
            MCommandList->ClearRenderTargetView(ScopedSceneRtv, ScopedSceneClear, 0, nullptr);
            MCommandList->ClearDepthStencilView(ScopedSceneDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            MCommandList->OMSetRenderTargets(1, &ScopedSceneRtv, FALSE, &ScopedSceneDsv);
            const D3D12_VIEWPORT ScopedSceneViewport{0.0f, 0.0f, static_cast<float>(MExtent.width),
                                                     static_cast<float>(MExtent.height), 0.0f, 1.0f};
            const D3D12_RECT ScopedSceneScissor{0, 0, static_cast<LONG>(MExtent.width),
                                                static_cast<LONG>(MExtent.height)};
            MCommandList->RSSetViewports(1, &ScopedSceneViewport);
            MCommandList->RSSetScissorRects(1, &ScopedSceneScissor);
        }
        void* ScopedMapped = nullptr;
        if (FAILED(MVertexBuffer->Map(0, nullptr, &ScopedMapped)))
        {
            return;
        }
        auto* ScopedVertices = static_cast<SmokeVertex*>(ScopedMapped);
        std::size_t ScopedCount = 0;
        for (const RenderItem& ScopedItem : InPacket.items)
        {
            const float ScopedHalfX = ScopedItem.size.X * 0.5f;
            const float ScopedHalfY = ScopedItem.size.Y * 0.5f;
            const float ScopedX = ScopedItem.position.X;
            const float ScopedY = ScopedItem.position.Y;
            const float ScopedR = static_cast<float>((ScopedItem.color >> 0) & 0xFFu) / 255.0f;
            const float ScopedG = static_cast<float>((ScopedItem.color >> 8) & 0xFFu) / 255.0f;
            const float ScopedB = static_cast<float>((ScopedItem.color >> 16) & 0xFFu) / 255.0f;
            const float ScopedA = static_cast<float>((ScopedItem.color >> 24) & 0xFFu) / 255.0f;
            const float ScopedNormalMeta =
                EncodeNormalVertexMeta(NormalMapLayer(ScopedItem.flags), NormalMapStrength(ScopedItem.flags));
            const SmokeVertex ScopedCorners[6] = {
                {{ScopedX - ScopedHalfX, ScopedY - ScopedHalfY, 0.5f}, {ScopedItem.uvMin.X, ScopedItem.uvMax.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer), ScopedNormalMeta},
                {{ScopedX + ScopedHalfX, ScopedY - ScopedHalfY, 0.5f}, {ScopedItem.uvMax.X, ScopedItem.uvMax.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer), ScopedNormalMeta},
                {{ScopedX + ScopedHalfX, ScopedY + ScopedHalfY, 0.5f}, {ScopedItem.uvMax.X, ScopedItem.uvMin.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer), ScopedNormalMeta},
                {{ScopedX - ScopedHalfX, ScopedY - ScopedHalfY, 0.5f}, {ScopedItem.uvMin.X, ScopedItem.uvMax.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer), ScopedNormalMeta},
                {{ScopedX + ScopedHalfX, ScopedY + ScopedHalfY, 0.5f}, {ScopedItem.uvMax.X, ScopedItem.uvMin.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer), ScopedNormalMeta},
                {{ScopedX - ScopedHalfX, ScopedY + ScopedHalfY, 0.5f}, {ScopedItem.uvMin.X, ScopedItem.uvMin.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer), ScopedNormalMeta},
            };
            std::memcpy(ScopedVertices + ScopedCount, ScopedCorners, sizeof(ScopedCorners));
            ScopedCount += 6;
        }
        ID3D12DescriptorHeap* ScopedHeaps[] = {MTextureHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        const D3D12_VERTEX_BUFFER_VIEW ScopedVbv{MVertexBuffer->GetGPUVirtualAddress(),
                                                 static_cast<UINT>(sizeof(SmokeVertex) * ScopedCount),
                                                 sizeof(SmokeVertex)};
        MCommandList->IASetVertexBuffers(0, 1, &ScopedVbv);
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        std::array<float, 32> ScopedLightConstants{};
        const std::size_t ScopedLightCount = InPacket.lights.size() < 4u ? InPacket.lights.size() : 4u;
        for (std::size_t ScopedI = 0; ScopedI < ScopedLightCount; ++ScopedI)
        {
            const GpuLight& ScopedLight = InPacket.lights[ScopedI];
            ScopedLightConstants[ScopedI * 4u + 0u] = ScopedLight.PositionAndRadius.X;
            ScopedLightConstants[ScopedI * 4u + 1u] = ScopedLight.PositionAndRadius.Y;
            ScopedLightConstants[ScopedI * 4u + 2u] = ScopedLight.PositionAndRadius.Z;
            ScopedLightConstants[16u + ScopedI * 4u + 0u] = ScopedLight.ColorAndIntensity.X;
            ScopedLightConstants[16u + ScopedI * 4u + 1u] = ScopedLight.ColorAndIntensity.Y;
            ScopedLightConstants[16u + ScopedI * 4u + 2u] = ScopedLight.ColorAndIntensity.Z;
            ScopedLightConstants[16u + ScopedI * 4u + 3u] = ScopedLight.ColorAndIntensity.W;
        }
        const auto ScopedDraw = [this](std::uint32_t InFirstVertex, std::uint32_t InVertexCount,
                                       std::uint32_t InTexture)
        {
            const auto ScopedSrv = MTextureSrvIndex.find(InTexture);
            if (ScopedSrv == MTextureSrvIndex.end())
            {
                return;
            }
            D3D12_GPU_DESCRIPTOR_HANDLE ScopedSrvHandle = MTextureHeap->GetGPUDescriptorHandleForHeapStart();
            ScopedSrvHandle.ptr += ScopedSrv->second * MSrvSize;
            MCommandList->SetGraphicsRootDescriptorTable(0, ScopedSrvHandle);
            MCommandList->DrawInstanced(InVertexCount, 1, InFirstVertex, 0);
        };
        if (InPacket.batches.empty())
        {
            MCommandList->SetGraphicsRootSignature(MSpriteLightRootSignature.Get());
            const Rhi::RenderItem& ScopedOnly = InPacket.items.front();
            const bool ScopedOnlyNormal =
                NormalMapLayer(ScopedOnly.flags) >= 1u && NormalMapStrength(ScopedOnly.flags) > 0.0f;
            MCommandList->SetPipelineState(ScopedOnlyNormal ? MPipeline.Get() : MBasePipeline.Get());
            MCommandList->SetGraphicsRoot32BitConstants(1, 32, ScopedLightConstants.data(), 0);
            ScopedDraw(0, static_cast<std::uint32_t>(ScopedCount), InPacket.textureArray);
        }
        else
        {
            for (const DrawBatch& ScopedBatch : InPacket.batches)
            {
                const std::uint32_t ScopedTexture =
                    ScopedBatch.textureArray != 0 ? ScopedBatch.textureArray : InPacket.textureArray;
                const auto ScopedMaterial = MMaterialPipelines.find(ScopedBatch.pipeline);
                if (ScopedBatch.pipeline != 0 && ScopedMaterial != MMaterialPipelines.end())
                {
                    const auto ScopedDomain = MMaterialPipelineDomains.find(ScopedBatch.pipeline);
                    const bool ScopedSpriteDomain =
                        ScopedDomain == MMaterialPipelineDomains.end() ||
                        ScopedDomain->second == ShaderPipelineDomain::Sprite;
                    const std::array<float, 28> ScopedMaterialConstants =
                        BuildMaterialConstants(InPacket, ScopedSpriteDomain);
                    MCommandList->SetGraphicsRootSignature(MMaterialRootSignature.Get());
                    MCommandList->SetGraphicsRoot32BitConstants(1, 28, ScopedMaterialConstants.data(), 0);
                    MCommandList->SetPipelineState(ScopedMaterial->second.Get());
                }
                else
                {
                    MCommandList->SetGraphicsRootSignature(MSpriteLightRootSignature.Get());
                    const Rhi::RenderItem& ScopedBatchFirst =
                        InPacket.items[std::min<std::size_t>(ScopedBatch.firstItem, InPacket.items.size() - 1u)];
                    const bool ScopedBatchNormal = NormalMapLayer(ScopedBatchFirst.flags) >= 1u &&
                                                   NormalMapStrength(ScopedBatchFirst.flags) > 0.0f;
                    MCommandList->SetPipelineState(ScopedBatchNormal ? MPipeline.Get() : MBasePipeline.Get());
                    MCommandList->SetGraphicsRoot32BitConstants(1, 32, ScopedLightConstants.data(), 0);
                }
                ScopedDraw(ScopedBatch.firstItem * 6, ScopedBatch.itemCount * 6, ScopedTexture);
            }
            MCommandList->SetPipelineState(MPipeline.Get());
            MCommandList->SetGraphicsRootSignature(MSpriteLightRootSignature.Get());
        }
        MVertexBuffer->Unmap(0, nullptr);
        if (!InPacket.meshItems.empty())
        {
            void* ScopedMeshMapped = nullptr;
            if (FAILED(MVertexBuffer->Map(0, nullptr, &ScopedMeshMapped)))
            {
                return;
            }
            auto* ScopedMeshVertices = static_cast<SmokeVertex*>(ScopedMeshMapped);
            std::size_t ScopedMeshCount = 0;
            for (const MeshRenderItem& ScopedItem : InPacket.meshItems)
            {
                std::span<const MeshVertex> ScopedVertices = ScopedItem.vertices;
                std::span<const std::uint16_t> ScopedIndices = ScopedItem.indices;
                const auto ScopedMesh = MMeshes.find(ScopedItem.mesh.id);
                if (ScopedMesh != MMeshes.end())
                {
                    ScopedVertices = ScopedMesh->second.Vertices;
                    ScopedIndices = ScopedMesh->second.Indices;
                }
                if (ScopedIndices.size() < 3 || ScopedVertices.empty() ||
                    ScopedMeshCount + ScopedIndices.size() > MVertexCapacity)
                {
                    continue;
                }
                const std::size_t ScopedStart = ScopedMeshCount;
                const float ScopedR = static_cast<float>((ScopedItem.color.rgba >> 0) & 0xFFu) / 255.0f;
                const float ScopedG = static_cast<float>((ScopedItem.color.rgba >> 8) & 0xFFu) / 255.0f;
                const float ScopedB = static_cast<float>((ScopedItem.color.rgba >> 16) & 0xFFu) / 255.0f;
                const float ScopedA = static_cast<float>((ScopedItem.color.rgba >> 24) & 0xFFu) / 255.0f;
                for (std::uint16_t ScopedIndex : ScopedIndices)
                {
                    if (ScopedIndex >= ScopedVertices.size())
                    {
                        continue;
                    }
                    const MeshVertex& ScopedVertex = ScopedVertices[ScopedIndex];
                    float ScopedLocalX = ScopedVertex.position.X;
                    float ScopedLocalY = ScopedVertex.position.Y;
                    if (ScopedItem.boneCount > 0 && !InPacket.boneMatrices.empty())
                    {
                        float ScopedSkinX = 0.0f;
                        float ScopedSkinY = 0.0f;
                        float ScopedWeightSum = 0.0f;
                        for (std::uint32_t ScopedBone = 0; ScopedBone < 2; ++ScopedBone)
                        {
                            const std::uint32_t ScopedBoneIndex =
                                ScopedItem.boneOffset + ScopedVertex.boneIndices[ScopedBone];
                            const float ScopedWeight = ScopedVertex.boneWeights[ScopedBone];
                            if (ScopedBoneIndex >= InPacket.boneMatrices.size() || ScopedWeight <= 0.0f)
                            {
                                continue;
                            }
                            const Mat4& ScopedBoneMatrix = InPacket.boneMatrices[ScopedBoneIndex];
                            ScopedSkinX += (ScopedBoneMatrix.m[0] * ScopedVertex.position.X +
                                            ScopedBoneMatrix.m[4] * ScopedVertex.position.Y +
                                            ScopedBoneMatrix.m[12]) * ScopedWeight;
                            ScopedSkinY += (ScopedBoneMatrix.m[1] * ScopedVertex.position.X +
                                            ScopedBoneMatrix.m[5] * ScopedVertex.position.Y +
                                            ScopedBoneMatrix.m[13]) * ScopedWeight;
                            ScopedWeightSum += ScopedWeight;
                        }
                        if (ScopedWeightSum > 0.0f)
                        {
                            ScopedLocalX = ScopedSkinX / ScopedWeightSum;
                            ScopedLocalY = ScopedSkinY / ScopedWeightSum;
                        }
                    }
                    const float ScopedWorldX = ScopedItem.model.m[0] * ScopedLocalX +
                                               ScopedItem.model.m[4] * ScopedLocalY + ScopedItem.model.m[12];
                    const float ScopedWorldY = ScopedItem.model.m[1] * ScopedLocalX +
                                               ScopedItem.model.m[5] * ScopedLocalY + ScopedItem.model.m[13];
                    ScopedMeshVertices[ScopedMeshCount++] = {
                        {ScopedWorldX, ScopedWorldY, 0.5f},
                        {ScopedVertex.uv.X, ScopedVertex.uv.Y},
                        {ScopedR, ScopedG, ScopedB, ScopedA},
                        static_cast<float>(ScopedItem.layer)};
                }
                const auto ScopedMeshSrv = MTextureSrvIndex.find(ScopedItem.textureArray);
                if (ScopedMeshSrv == MTextureSrvIndex.end())
                {
                    continue;
                }
                D3D12_GPU_DESCRIPTOR_HANDLE ScopedMeshSrvHandle = MTextureHeap->GetGPUDescriptorHandleForHeapStart();
                ScopedMeshSrvHandle.ptr += ScopedMeshSrv->second * MSrvSize;
                MCommandList->SetGraphicsRootDescriptorTable(0, ScopedMeshSrvHandle);
                const D3D12_VERTEX_BUFFER_VIEW ScopedMeshVbv{MVertexBuffer->GetGPUVirtualAddress(),
                                                             static_cast<UINT>(sizeof(SmokeVertex) * ScopedMeshCount),
                                                             sizeof(SmokeVertex)};
                MCommandList->IASetVertexBuffers(0, 1, &ScopedMeshVbv);
                MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                const auto ScopedMeshMaterial = MMaterialPipelines.find(ScopedItem.materialPipeline);
                if (ScopedItem.materialPipeline != 0xFFFFFFFFu && ScopedMeshMaterial != MMaterialPipelines.end())
                {
                    const auto ScopedDomain = MMaterialPipelineDomains.find(ScopedItem.materialPipeline);
                    const bool ScopedSpriteDomain =
                        ScopedDomain == MMaterialPipelineDomains.end() ||
                        ScopedDomain->second == ShaderPipelineDomain::Sprite;
                    const std::array<float, 28> ScopedMaterialConstants =
                        BuildMaterialConstants(InPacket, ScopedSpriteDomain);
                    MCommandList->SetGraphicsRootSignature(MMaterialRootSignature.Get());
                    MCommandList->SetGraphicsRoot32BitConstants(1, 28, ScopedMaterialConstants.data(), 0);
                    MCommandList->SetPipelineState(ScopedMeshMaterial->second.Get());
                }
                else
                {
                    MCommandList->SetGraphicsRootSignature(MSpriteLightRootSignature.Get());
                    MCommandList->SetPipelineState(MBasePipeline.Get());
                    MCommandList->SetGraphicsRoot32BitConstants(1, 32, ScopedLightConstants.data(), 0);
                }
                MCommandList->DrawInstanced(static_cast<UINT>(ScopedMeshCount - ScopedStart), 1,
                                            static_cast<UINT>(ScopedStart), 0);
            }
            MVertexBuffer->Unmap(0, nullptr);
            MCommandList->SetPipelineState(MPipeline.Get());
            MCommandList->SetGraphicsRootSignature(MSpriteLightRootSignature.Get());
        }
    }

    [[nodiscard]] bool RecordGraphicsPass(std::string_view InPipeline, const void* InConstants,
                                          std::uint32_t InConstantsBytes, Extent2D InExtent,
                                          std::uint32_t InInstanceCount) override
    {
        (void)InExtent;
        if (!MInFrame || MCommandList == nullptr || InConstants == nullptr || InInstanceCount != 1)
        {
            return false;
        }
        if (InPipeline == "postfx_composite.frag" || InPipeline == "postfx_composite")
        {
            if (!MPostFxReady || InConstantsBytes < sizeof(PostFxConstants))
            {
                return false;
            }
            std::memcpy(&MPostFxGraphBlock, InConstants, sizeof(PostFxConstants));
            MPostFxGraphPending = true;
            return true;
        }
        if (InPipeline == "color_grading.frag" || InPipeline == "color_grading")
        {
            if (!MColorGradeReady || InConstantsBytes < sizeof(ColorGradingConstants))
            {
                return false;
            }
            std::memcpy(&MColorGradeBlock, InConstants, sizeof(ColorGradingConstants));
            MColorGradePending = true;
            return true;
        }
        return false;
    }

    void EndFrame() override
    {
        MInFrame = false;
        RecordPostFxComposite();
        RecordColorGrade();
        // The back buffer must not stay bound as an RTV while it transitions to
        // the present state.
        MCommandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        const std::uint32_t ScopedIndex = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = MBackBuffers[ScopedIndex].Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        MCommandList->Close();
        ID3D12CommandList* ScopedLists[] = {MCommandList.Get()};
        MQueue->ExecuteCommandLists(1, ScopedLists);
        const HRESULT ScopedPresent = MSwapchain->Present(MVsync ? 1u : 0u, 0);
        if (ScopedPresent == DXGI_ERROR_DEVICE_REMOVED || ScopedPresent == DXGI_ERROR_DEVICE_RESET)
        {
            MInfo.deviceLost = true;
            LogError("d3d12", "Present failed with device loss (0x{:08X})",
                     static_cast<unsigned>(ScopedPresent));
        }
        else if (FAILED(ScopedPresent))
        {
            LogWarn("d3d12", "Present failed (0x{:08X})", static_cast<unsigned>(ScopedPresent));
        }
        ++MFenceValue;
        MQueue->Signal(MFence.Get(), MFenceValue);
        MStats.frameIndex = MFenceValue;
        if (MDebugLayer && FAILED(MDevice->GetDeviceRemovedReason()))
        {
            DumpValidation();
        }
    }

    MeshHandle CreateMesh(const MeshDesc& InDesc) override
    {
        if (InDesc.vertices.empty() || InDesc.indices.empty())
        {
            return {};
        }
        MeshRecord ScopedRecord;
        ScopedRecord.Vertices.assign(InDesc.vertices.begin(), InDesc.vertices.end());
        ScopedRecord.Indices.assign(InDesc.indices.begin(), InDesc.indices.end());
        const std::uint32_t ScopedId = MNextMeshId++;
        MMeshes.emplace(ScopedId, std::move(ScopedRecord));
        MeshHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }

    void DestroyMesh(MeshHandle InHandle) override
    {
        MMeshes.erase(InHandle.id);
    }

    [[nodiscard]] std::uint32_t AcquireSrvSlot() noexcept
    {
        if (!MFreeSrvIndices.empty())
        {
            const std::uint32_t ScopedIndex = MFreeSrvIndices.back();
            MFreeSrvIndices.pop_back();
            return ScopedIndex;
        }
        if (MSrvNext >= kMaxTextures)
        {
            return kInvalidSrvSlot;
        }
        return MSrvNext++;
    }

    void ReleaseSrvSlot(std::uint32_t InIndex) noexcept
    {
        if (InIndex < kMaxTextures)
        {
            MFreeSrvIndices.push_back(InIndex);
        }
    }

    TextureArrayHandle CreateTextureArray(const TextureArrayDesc& InDesc,
                                          std::span<const std::byte> InPixels) override
    {
        if (MDevice == nullptr || MQueue == nullptr || InDesc.size.width == 0 || InDesc.size.height == 0 ||
            InDesc.layers == 0 || InDesc.mipLevels == 0 || (MFreeSrvIndices.empty() && MSrvNext >= kMaxTextures))
        {
            return {};
        }
        const DXGI_FORMAT ScopedFormat = TextureFormatToDxgi(InDesc.format, InDesc.EffectiveSrgb());
        if (ScopedFormat == DXGI_FORMAT_UNKNOWN)
        {
            LogError("d3d12", "CreateTextureArray: {} is not supported on Direct3D",
                     Fby::Assets::TextureFormatName(InDesc.format));
            return {};
        }
        const std::uint64_t ScopedExpected = Fby::Assets::TextureByteSize(
            InDesc.format, InDesc.size.width, InDesc.size.height, InDesc.layers, InDesc.mipLevels);
        if (InPixels.size() < ScopedExpected)
        {
            LogError("d3d12", "CreateTextureArray: payload is {} bytes, expected {} ({}x{} x {} x {} mips, {})",
                     InPixels.size(), ScopedExpected, InDesc.size.width, InDesc.size.height, InDesc.layers,
                     InDesc.mipLevels, Fby::Assets::TextureFormatName(InDesc.format));
            return {};
        }

        D3D12_RESOURCE_DESC ScopedDesc{};
        ScopedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ScopedDesc.Width = InDesc.size.width;
        ScopedDesc.Height = InDesc.size.height;
        ScopedDesc.DepthOrArraySize = static_cast<UINT16>(InDesc.layers);
        ScopedDesc.MipLevels = static_cast<UINT16>(InDesc.mipLevels);
        ScopedDesc.Format = ScopedFormat;
        ScopedDesc.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES ScopedDefaultHeap{};
        ScopedDefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> ScopedTexture;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedDefaultHeap, D3D12_HEAP_FLAG_NONE, &ScopedDesc,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&ScopedTexture))))
        {
            return {};
        }

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> ScopedFootprints(InDesc.mipLevels);
        std::vector<UINT> ScopedRowCounts(InDesc.mipLevels);
        std::vector<UINT64> ScopedRowSizes(InDesc.mipLevels);
        std::uint64_t ScopedLayerBytes = 0;
        MDevice->GetCopyableFootprints(&ScopedDesc, 0, InDesc.mipLevels, 0, ScopedFootprints.data(),
                                       ScopedRowCounts.data(), ScopedRowSizes.data(), &ScopedLayerBytes);

        D3D12_RESOURCE_DESC ScopedUploadDesc{};
        ScopedUploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ScopedUploadDesc.Width = ScopedLayerBytes * InDesc.layers;
        ScopedUploadDesc.Height = 1;
        ScopedUploadDesc.DepthOrArraySize = 1;
        ScopedUploadDesc.MipLevels = 1;
        ScopedUploadDesc.SampleDesc.Count = 1;
        ScopedUploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES ScopedUploadHeap{};
        ScopedUploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        ComPtr<ID3D12Resource> ScopedUpload;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedUploadHeap, D3D12_HEAP_FLAG_NONE, &ScopedUploadDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&ScopedUpload))))
        {
            return {};
        }
        void* ScopedMapped = nullptr;
        if (FAILED(ScopedUpload->Map(0, nullptr, &ScopedMapped)))
        {
            return {};
        }
        auto* ScopedUploadBytes = static_cast<std::byte*>(ScopedMapped);
        std::uint64_t ScopedSourceOffset = 0;
        for (std::uint32_t ScopedLayer = 0; ScopedLayer < InDesc.layers; ++ScopedLayer)
        {
            for (std::uint32_t ScopedMip = 0; ScopedMip < InDesc.mipLevels; ++ScopedMip)
            {
                const std::uint64_t ScopedMipBytes = Fby::Assets::MipByteSize(
                    InDesc.format, Fby::Assets::MipExtent(InDesc.size.width, ScopedMip),
                    Fby::Assets::MipExtent(InDesc.size.height, ScopedMip));
                const UINT ScopedRows = ScopedRowCounts[ScopedMip];
                const UINT ScopedRowBytes =
                    ScopedRows != 0 ? static_cast<UINT>(ScopedRowSizes[ScopedMip] / ScopedRows) : 0;
                std::byte* ScopedDestination =
                    ScopedUploadBytes + ScopedLayer * ScopedLayerBytes + ScopedFootprints[ScopedMip].Offset;
                for (UINT ScopedRow = 0; ScopedRow < ScopedRows; ++ScopedRow)
                {
                    std::memcpy(ScopedDestination + static_cast<std::uint64_t>(ScopedRow) *
                                                      ScopedFootprints[ScopedMip].Footprint.RowPitch,
                                InPixels.data() + ScopedSourceOffset + static_cast<std::uint64_t>(ScopedRow) *
                                                                         ScopedRowBytes,
                                ScopedRowBytes);
                }
                ScopedSourceOffset += ScopedMipBytes;
            }
        }
        ScopedUpload->Unmap(0, nullptr);

        ComPtr<ID3D12CommandAllocator> ScopedAllocator;
        ComPtr<ID3D12GraphicsCommandList> ScopedCommands;
        if (FAILED(MDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ScopedAllocator))) ||
            FAILED(MDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ScopedAllocator.Get(), nullptr,
                                              IID_PPV_ARGS(&ScopedCommands))))
        {
            return {};
        }
        for (std::uint32_t ScopedLayer = 0; ScopedLayer < InDesc.layers; ++ScopedLayer)
        {
            for (std::uint32_t ScopedMip = 0; ScopedMip < InDesc.mipLevels; ++ScopedMip)
            {
                D3D12_TEXTURE_COPY_LOCATION ScopedDestination{};
                ScopedDestination.pResource = ScopedTexture.Get();
                ScopedDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                ScopedDestination.SubresourceIndex = ScopedMip + ScopedLayer * InDesc.mipLevels;
                D3D12_TEXTURE_COPY_LOCATION ScopedSource{};
                ScopedSource.pResource = ScopedUpload.Get();
                ScopedSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                ScopedSource.PlacedFootprint = ScopedFootprints[ScopedMip];
                ScopedSource.PlacedFootprint.Offset =
                    ScopedLayer * ScopedLayerBytes + ScopedFootprints[ScopedMip].Offset;
                ScopedCommands->CopyTextureRegion(&ScopedDestination, 0, 0, 0, &ScopedSource, nullptr);
            }
        }
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = ScopedTexture.Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedCommands->ResourceBarrier(1, &ScopedBarrier);
        ScopedCommands->Close();
        ID3D12CommandList* ScopedLists[] = {ScopedCommands.Get()};
        MQueue->ExecuteCommandLists(1, ScopedLists);
        WaitIdle();

        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrv{};
        ScopedSrv.Format = ScopedDesc.Format;
        ScopedSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        ScopedSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrv.Texture2DArray.MostDetailedMip = 0;
        ScopedSrv.Texture2DArray.MipLevels = InDesc.mipLevels;
        ScopedSrv.Texture2DArray.FirstArraySlice = 0;
        ScopedSrv.Texture2DArray.ArraySize = InDesc.layers;
        const std::uint32_t ScopedSrvIndex = AcquireSrvSlot();
        if (ScopedSrvIndex == kInvalidSrvSlot)
        {
            LogError("d3d12", "CreateRenderTarget: all {} SRV slots are in use", kMaxTextures);
            return {};
        }
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSrvHandle = MTextureHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSrvHandle.ptr += ScopedSrvIndex * MSrvSize;
        MDevice->CreateShaderResourceView(ScopedTexture.Get(), &ScopedSrv, ScopedSrvHandle);
        const std::uint32_t ScopedId = MNextTextureId++;
        MTextures.emplace(ScopedId, ScopedTexture);
        MTextureSrvIndex[ScopedId] = ScopedSrvIndex;
        TextureArrayHandle ScopedTextureHandle;
        ScopedTextureHandle.id = ScopedId;
        return ScopedTextureHandle;
    }

    void DestroyTextureArray(TextureArrayHandle InHandle) override
    {
        if (const auto ScopedIndex = MTextureSrvIndex.find(InHandle.id); ScopedIndex != MTextureSrvIndex.end())
        {
            ReleaseSrvSlot(ScopedIndex->second);
            MTextureSrvIndex.erase(ScopedIndex);
        }
        MTextures.erase(InHandle.id);
    }
    bool UpdateTextureArrayLayer(TextureArrayHandle InHandle, std::uint32_t InLayer, std::uint32_t InMipLevel,
                                 std::span<const std::byte> InData) override
    {
        const auto ScopedTexture = MTextures.find(InHandle.id);
        if (MDevice == nullptr || MQueue == nullptr || ScopedTexture == MTextures.end())
        {
            return false;
        }
        const D3D12_RESOURCE_DESC ScopedDesc = ScopedTexture->second->GetDesc();
        if (InLayer >= ScopedDesc.DepthOrArraySize || InMipLevel >= ScopedDesc.MipLevels)
        {
            return false;
        }
        const std::uint32_t ScopedSubresource =
            InMipLevel + InLayer * static_cast<std::uint32_t>(ScopedDesc.MipLevels);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT ScopedFootprint{};
        UINT ScopedRows = 0;
        UINT64 ScopedRowSize = 0;
        UINT64 ScopedTotal = 0;
        MDevice->GetCopyableFootprints(&ScopedDesc, ScopedSubresource, 1, 0, &ScopedFootprint, &ScopedRows,
                                       &ScopedRowSize, &ScopedTotal);
        const UINT64 ScopedRowBytes = ScopedRows != 0 ? ScopedRowSize / ScopedRows : 0;
        if (InData.size() < ScopedRowBytes * ScopedRows)
        {
            return false;
        }
        D3D12_RESOURCE_DESC ScopedUploadDesc{};
        ScopedUploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ScopedUploadDesc.Width = ScopedTotal;
        ScopedUploadDesc.Height = 1;
        ScopedUploadDesc.DepthOrArraySize = 1;
        ScopedUploadDesc.MipLevels = 1;
        ScopedUploadDesc.SampleDesc.Count = 1;
        ScopedUploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES ScopedUploadHeap{};
        ScopedUploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        ComPtr<ID3D12Resource> ScopedUpload;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedUploadHeap, D3D12_HEAP_FLAG_NONE, &ScopedUploadDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&ScopedUpload))))
        {
            return false;
        }
        void* ScopedMapped = nullptr;
        if (FAILED(ScopedUpload->Map(0, nullptr, &ScopedMapped)))
        {
            return false;
        }
        auto* ScopedDestination = static_cast<std::byte*>(ScopedMapped);
        for (UINT ScopedRow = 0; ScopedRow < ScopedRows; ++ScopedRow)
        {
            std::memcpy(ScopedDestination + static_cast<std::uint64_t>(ScopedRow) *
                                              ScopedFootprint.Footprint.RowPitch,
                        InData.data() + static_cast<std::uint64_t>(ScopedRow) * ScopedRowBytes,
                        static_cast<std::size_t>(ScopedRowBytes));
        }
        ScopedUpload->Unmap(0, nullptr);

        WaitIdle();
        ComPtr<ID3D12CommandAllocator> ScopedAllocator;
        ComPtr<ID3D12GraphicsCommandList> ScopedCommands;
        const HRESULT ScopedAllocatorResult =
            MDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ScopedAllocator));
        const HRESULT ScopedListResult =
            SUCCEEDED(ScopedAllocatorResult)
                ? MDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ScopedAllocator.Get(), nullptr,
                                             IID_PPV_ARGS(&ScopedCommands))
                : ScopedAllocatorResult;
        if (FAILED(ScopedAllocatorResult) || FAILED(ScopedListResult))
        {
            LogWarn("d3d12", "CaptureRenderTarget: command objects failed (allocator 0x{:08X}, list 0x{:08X})",
                    static_cast<unsigned>(ScopedAllocatorResult), static_cast<unsigned>(ScopedListResult));
            return false;
        }
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = ScopedTexture->second.Get();
        ScopedBarrier.Transition.Subresource = ScopedSubresource;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedCommands->ResourceBarrier(1, &ScopedBarrier);
        D3D12_TEXTURE_COPY_LOCATION ScopedDestinationLocation{};
        ScopedDestinationLocation.pResource = ScopedTexture->second.Get();
        ScopedDestinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ScopedDestinationLocation.SubresourceIndex = ScopedSubresource;
        D3D12_TEXTURE_COPY_LOCATION ScopedSourceLocation{};
        ScopedSourceLocation.pResource = ScopedUpload.Get();
        ScopedSourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        ScopedSourceLocation.PlacedFootprint = ScopedFootprint;
        ScopedSourceLocation.PlacedFootprint.Offset = 0;
        ScopedCommands->CopyTextureRegion(&ScopedDestinationLocation, 0, 0, 0, &ScopedSourceLocation, nullptr);
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedCommands->ResourceBarrier(1, &ScopedBarrier);
        ScopedCommands->Close();
        ID3D12CommandList* ScopedLists[] = {ScopedCommands.Get()};
        MQueue->ExecuteCommandLists(1, ScopedLists);
        WaitIdle();
        return true;
    }

    TextureArrayHandle CreateTextureArrayFromRenderTarget(RenderTargetHandle InHandle) override
    {
        const auto ScopedTarget = MTargets.find(InHandle.id);
        if (MDevice == nullptr || MQueue == nullptr || ScopedTarget == MTargets.end() ||
            (MFreeSrvIndices.empty() && MSrvNext >= kMaxTextures))
        {
            return {};
        }
        D3D12_RESOURCE_DESC ScopedDesc = ScopedTarget->second->GetDesc();
        ScopedDesc.DepthOrArraySize = 1;
        ScopedDesc.MipLevels = 1;
        ScopedDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES ScopedDefaultHeap{};
        ScopedDefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> ScopedTexture;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedDefaultHeap, D3D12_HEAP_FLAG_NONE, &ScopedDesc,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&ScopedTexture))))
        {
            return {};
        }
        WaitIdle();
        ComPtr<ID3D12CommandAllocator> ScopedAllocator;
        ComPtr<ID3D12GraphicsCommandList> ScopedCommands;
        if (FAILED(MDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ScopedAllocator))) ||
            FAILED(MDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ScopedAllocator.Get(), nullptr,
                                              IID_PPV_ARGS(&ScopedCommands))))
        {
            return {};
        }
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = ScopedTarget->second.Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedCommands->ResourceBarrier(1, &ScopedBarrier);
        ScopedCommands->CopyResource(ScopedTexture.Get(), ScopedTarget->second.Get());
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedCommands->ResourceBarrier(1, &ScopedBarrier);
        ScopedBarrier.Transition.pResource = ScopedTexture.Get();
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedCommands->ResourceBarrier(1, &ScopedBarrier);
        ScopedCommands->Close();
        ID3D12CommandList* ScopedLists[] = {ScopedCommands.Get()};
        MQueue->ExecuteCommandLists(1, ScopedLists);
        WaitIdle();

        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrv{};
        ScopedSrv.Format = ScopedDesc.Format;
        ScopedSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        ScopedSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrv.Texture2DArray.MipLevels = 1;
        ScopedSrv.Texture2DArray.ArraySize = 1;
        const std::uint32_t ScopedSrvIndex = AcquireSrvSlot();
        if (ScopedSrvIndex == kInvalidSrvSlot)
        {
            return {};
        }
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSrvHandle = MTextureHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSrvHandle.ptr += ScopedSrvIndex * MSrvSize;
        MDevice->CreateShaderResourceView(ScopedTexture.Get(), &ScopedSrv, ScopedSrvHandle);
        const std::uint32_t ScopedId = MNextTextureId++;
        MTextures.emplace(ScopedId, ScopedTexture);
        MTextureSrvIndex[ScopedId] = ScopedSrvIndex;
        TextureArrayHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }


    RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& InDesc) override
    {
        if (InDesc.size.width == 0 || InDesc.size.height == 0 || MRtvNext >= kMaxTargets)
        {
            LogError("d3d12", "CreateRenderTarget: invalid size {}x{} or all {} target slots in use ({})",
                     InDesc.size.width, InDesc.size.height, kMaxTargets, MRtvNext);
            return {};
        }
        D3D12_RESOURCE_DESC ScopedDesc{};
        ScopedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ScopedDesc.Width = InDesc.size.width;
        ScopedDesc.Height = InDesc.size.height;
        ScopedDesc.DepthOrArraySize = 1;
        ScopedDesc.MipLevels = 1;
        ScopedDesc.Format = InDesc.format == RenderTargetFormat::Rg16Float ? DXGI_FORMAT_R16G16_FLOAT
                            : InDesc.format == RenderTargetFormat::Rgba8Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                                             : DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedDesc.SampleDesc.Count = 1;
        ScopedDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES ScopedHeap{};
        ScopedHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> ScopedResource;
        const HRESULT ScopedColorResult =
            MDevice->CreateCommittedResource(&ScopedHeap, D3D12_HEAP_FLAG_NONE, &ScopedDesc,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                             IID_PPV_ARGS(&ScopedResource));
        if (FAILED(ScopedColorResult))
        {
            if (ScopedColorResult == DXGI_ERROR_DEVICE_REMOVED)
            {
                MInfo.deviceLost = true;
            }
            LogError("d3d12", "CreateRenderTarget: color resource failed (0x{:08X})",
                     static_cast<unsigned>(ScopedColorResult));
            return {};
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedTargetHeapDesc{};
        ScopedTargetHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ScopedTargetHeapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> ScopedTargetHeap;
        const HRESULT ScopedRtvHeapResult =
            MDevice->CreateDescriptorHeap(&ScopedTargetHeapDesc, IID_PPV_ARGS(&ScopedTargetHeap));
        if (FAILED(ScopedRtvHeapResult))
        {
            if (ScopedRtvHeapResult == DXGI_ERROR_DEVICE_REMOVED)
            {
                MInfo.deviceLost = true;
            }
            LogError("d3d12", "CreateRenderTarget: RTV heap failed (0x{:08X})",
                     static_cast<unsigned>(ScopedRtvHeapResult));
            return {};
        }
        MDevice->CreateRenderTargetView(ScopedResource.Get(), nullptr,
                                        ScopedTargetHeap->GetCPUDescriptorHandleForHeapStart());
        const std::uint32_t ScopedId = MNextTargetId++;
        D3D12_HEAP_PROPERTIES ScopedDepthHeap{};
        ScopedDepthHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC ScopedDepthDesc{};
        ScopedDepthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ScopedDepthDesc.Width = InDesc.size.width;
        ScopedDepthDesc.Height = InDesc.size.height;
        ScopedDepthDesc.DepthOrArraySize = 1;
        ScopedDepthDesc.MipLevels = 1;
        ScopedDepthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthDesc.SampleDesc.Count = 1;
        ScopedDepthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE ScopedDepthClear{};
        ScopedDepthClear.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthClear.DepthStencil.Depth = 1.0f;
        ComPtr<ID3D12Resource> ScopedDepthResource;
        const HRESULT ScopedDepthResult =
            MDevice->CreateCommittedResource(&ScopedDepthHeap, D3D12_HEAP_FLAG_NONE, &ScopedDepthDesc,
                                             D3D12_RESOURCE_STATE_DEPTH_WRITE, &ScopedDepthClear,
                                             IID_PPV_ARGS(&ScopedDepthResource));
        if (FAILED(ScopedDepthResult))
        {
            if (ScopedDepthResult == DXGI_ERROR_DEVICE_REMOVED)
            {
                MInfo.deviceLost = true;
            }
            LogError("d3d12", "CreateRenderTarget: depth resource failed (0x{:08X})",
                     static_cast<unsigned>(ScopedDepthResult));
            return {};
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedDepthHeapDesc{};
        ScopedDepthHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ScopedDepthHeapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> ScopedDepthViewHeap;
        const HRESULT ScopedDsvHeapResult =
            MDevice->CreateDescriptorHeap(&ScopedDepthHeapDesc, IID_PPV_ARGS(&ScopedDepthViewHeap));
        if (FAILED(ScopedDsvHeapResult))
        {
            if (ScopedDsvHeapResult == DXGI_ERROR_DEVICE_REMOVED)
            {
                MInfo.deviceLost = true;
            }
            LogError("d3d12", "CreateRenderTarget: DSV heap failed (0x{:08X})",
                     static_cast<unsigned>(ScopedDsvHeapResult));
            return {};
        }
        MDevice->CreateDepthStencilView(ScopedDepthResource.Get(), nullptr,
                                        ScopedDepthViewHeap->GetCPUDescriptorHandleForHeapStart());
        const std::uint32_t ScopedSrvIndex = AcquireSrvSlot();
        if (ScopedSrvIndex == kInvalidSrvSlot)
        {
            return {};
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrvDesc{};
        ScopedSrvDesc.Format = ScopedDesc.Format;
        ScopedSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrvDesc.Texture2D.MostDetailedMip = 0;
        ScopedSrvDesc.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSrvHandle = MTextureHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSrvHandle.ptr += ScopedSrvIndex * MSrvSize;
        MDevice->CreateShaderResourceView(ScopedResource.Get(), &ScopedSrvDesc, ScopedSrvHandle);
        MTargets.emplace(ScopedId, ScopedResource);
        MTargetHeaps.emplace(ScopedId, ScopedTargetHeap);
        MTargetSrvIndex.emplace(ScopedId, ScopedSrvIndex);
        MTargetDepthResources.emplace(ScopedId, ScopedDepthResource);
        MTargetDepthHeaps.emplace(ScopedId, ScopedDepthViewHeap);
        MTargetSizes[ScopedId] = InDesc.size;
        MTargetBlend[ScopedId] = InDesc.blend;
        ++MRtvNext;
        RenderTargetHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }

    void DestroyRenderTarget(RenderTargetHandle InHandle) override
    {
        const bool ScopedExisted = MTargets.erase(InHandle.id) != 0;
        if (const auto ScopedSrv = MTargetSrvIndex.find(InHandle.id); ScopedSrv != MTargetSrvIndex.end())
        {
            ReleaseSrvSlot(ScopedSrv->second);
            MTargetSrvIndex.erase(ScopedSrv);
        }
        MAccessibilityResources.erase(InHandle.id);
        MTargetHeaps.erase(InHandle.id);
        MTargetDepthResources.erase(InHandle.id);
        MTargetDepthHeaps.erase(InHandle.id);
        MTargetSizes.erase(InHandle.id);
        MTargetBlend.erase(InHandle.id);
        // MRtvNext is the live-target count used against kMaxTargets, not a
        // monotonic id. Destroying a target must release its slot.
        if (ScopedExisted && MRtvNext > 0)
        {
            --MRtvNext;
        }
    }

    bool UploadMotionVectors(std::span<const MotionVectorItem> InItems) override
    {
        MMotionItems.assign(InItems.begin(), InItems.end());
        return true;
    }

    bool ClearMotionVectors(RenderTargetHandle InTarget) override
    {
        if (const char* ScopedNoop = std::getenv("FBY_D3D12_MOTION_NOOP"); ScopedNoop != nullptr && ScopedNoop[0] == '1')
        {
            return true;
        }
        const auto ScopedHeap = MTargetHeaps.find(InTarget.id);
        if (!MInFrame || MCommandList == nullptr || ScopedHeap == MTargetHeaps.end())
        {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = ScopedHeap->second->GetCPUDescriptorHandleForHeapStart();
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        MCommandList->ClearRenderTargetView(ScopedRtv, ScopedClear, 0, nullptr);
        return true;
    }

    bool TemporalResolve(RenderTargetHandle InCurrent, RenderTargetHandle InHistory,
                         RenderTargetHandle InMotion, RenderTargetHandle InDepth,
                         RenderTargetHandle InHistoryDepth, RenderTargetHandle InOutput,
                         float InHistoryWeight, float InMotionScale, bool InResetHistory) override
    {

        (void)InMotionScale;
        const auto ScopedCurrent = MTargets.find(InCurrent.id);
        const auto ScopedHistory = MTargets.find(InHistory.id);
        const auto ScopedOutput = MTargets.find(InOutput.id);
        const auto ScopedOutputHeap = MTargetHeaps.find(InOutput.id);
        const auto ScopedSize = MTargetSizes.find(InOutput.id);
        if (!MInFrame || MCommandList == nullptr || MTemporalPipeline == nullptr || MTemporalRootSignature == nullptr ||
            ScopedCurrent == MTargets.end() || ScopedHistory == MTargets.end() ||
            ScopedOutput == MTargets.end() || ScopedOutputHeap == MTargetHeaps.end() ||
            ScopedSize == MTargetSizes.end())
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedSrvHeapDesc{};
        ScopedSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedSrvHeapDesc.NumDescriptors = 5;
        ScopedSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> ScopedSrvHeap;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedSrvHeapDesc, IID_PPV_ARGS(&ScopedSrvHeap))))
        {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedCpuSrv = ScopedSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrvDesc{};
        ScopedSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrvDesc.Texture2D.MipLevels = 1;
        ScopedSrvDesc.Format = ScopedCurrent->second->GetDesc().Format;
        MDevice->CreateShaderResourceView(ScopedCurrent->second.Get(), &ScopedSrvDesc, ScopedCpuSrv);
        ScopedCpuSrv.ptr += MSrvSize;
        ScopedSrvDesc.Format = ScopedHistory->second->GetDesc().Format;
        MDevice->CreateShaderResourceView(ScopedHistory->second.Get(), &ScopedSrvDesc, ScopedCpuSrv);
        ScopedCpuSrv.ptr += MSrvSize;
        const auto ScopedMotionTarget = MTargets.find(InMotion.id);
        ID3D12Resource* ScopedMotionResource =
            ScopedMotionTarget != MTargets.end() ? ScopedMotionTarget->second.Get() : ScopedCurrent->second.Get();
        ScopedSrvDesc.Format = ScopedMotionResource->GetDesc().Format;
        MDevice->CreateShaderResourceView(ScopedMotionResource, &ScopedSrvDesc, ScopedCpuSrv);
        ScopedCpuSrv.ptr += MSrvSize;
        const auto ScopedDepthTarget = MTargetDepthResources.find(InDepth.id);
        ID3D12Resource* ScopedDepthResource =
            ScopedDepthTarget != MTargetDepthResources.end() ? ScopedDepthTarget->second.Get()
                                                             : ScopedCurrent->second.Get();
        ScopedSrvDesc.Format = ScopedDepthTarget != MTargetDepthResources.end() ? DXGI_FORMAT_R32_FLOAT
                                                                               : ScopedCurrent->second->GetDesc().Format;
        MDevice->CreateShaderResourceView(ScopedDepthResource, &ScopedSrvDesc, ScopedCpuSrv);
        ScopedCpuSrv.ptr += MSrvSize;
        const auto ScopedHistoryDepthTarget = MTargetDepthResources.find(InHistoryDepth.id);
        ID3D12Resource* ScopedHistoryDepthResource =
            ScopedHistoryDepthTarget != MTargetDepthResources.end() ? ScopedHistoryDepthTarget->second.Get()
                                                                   : ScopedCurrent->second.Get();
        ScopedSrvDesc.Format = ScopedHistoryDepthTarget != MTargetDepthResources.end()
                                   ? DXGI_FORMAT_R32_FLOAT
                                   : ScopedCurrent->second->GetDesc().Format;
        MDevice->CreateShaderResourceView(ScopedHistoryDepthResource, &ScopedSrvDesc, ScopedCpuSrv);
        // Real depth attachments need transitions around the pass.
        ID3D12Resource* ScopedDepthResources[2] = {nullptr, nullptr};
        std::uint32_t ScopedDepthCount = 0;
        if (ScopedDepthTarget != MTargetDepthResources.end())
        {
            ScopedDepthResources[ScopedDepthCount++] = ScopedDepthTarget->second.Get();
        }
        if (ScopedHistoryDepthTarget != MTargetDepthResources.end() &&
            (ScopedDepthCount == 0 || ScopedDepthResources[0] != ScopedHistoryDepthTarget->second.Get()))
        {
            ScopedDepthResources[ScopedDepthCount++] = ScopedHistoryDepthTarget->second.Get();
        }
        D3D12_RESOURCE_BARRIER ScopedBarriers[4]{};
        ScopedBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[0].Transition.pResource = ScopedCurrent->second.Get();
        ScopedBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[1].Transition.pResource = ScopedHistory->second.Get();
        ScopedBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedDepthCount; ++ScopedIndex)
        {
            ScopedBarriers[2 + ScopedIndex].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ScopedBarriers[2 + ScopedIndex].Transition.pResource = ScopedDepthResources[ScopedIndex];
            ScopedBarriers[2 + ScopedIndex].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ScopedBarriers[2 + ScopedIndex].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            ScopedBarriers[2 + ScopedIndex].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        MCommandList->ResourceBarrier(2 + ScopedDepthCount, ScopedBarriers);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = ScopedOutputHeap->second->GetCPUDescriptorHandleForHeapStart();
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        MCommandList->ClearRenderTargetView(ScopedRtv, ScopedClear, 0, nullptr);
        MCommandList->OMSetRenderTargets(1, &ScopedRtv, FALSE, nullptr);
        const D3D12_VIEWPORT ScopedViewport{
            0.0f, 0.0f, static_cast<float>(ScopedSize->second.width),
            static_cast<float>(ScopedSize->second.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{
            0, 0, static_cast<LONG>(ScopedSize->second.width), static_cast<LONG>(ScopedSize->second.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        MCommandList->SetPipelineState(MTemporalPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MTemporalRootSignature.Get());
        ID3D12DescriptorHeap* ScopedHeaps[] = {ScopedSrvHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        MCommandList->SetGraphicsRootDescriptorTable(0, ScopedSrvHeap->GetGPUDescriptorHandleForHeapStart());
        const float ScopedConstants[4] = {InHistoryWeight, InResetHistory ? 1.0f : 0.0f, InMotionScale, 0.01f};
        MCommandList->SetGraphicsRoot32BitConstants(1, 4, ScopedConstants, 0);
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MCommandList->DrawInstanced(3, 1, 0, 0);
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedDepthCount; ++ScopedIndex)
        {
            ScopedBarriers[2 + ScopedIndex].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ScopedBarriers[2 + ScopedIndex].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        MCommandList->ResourceBarrier(2 + ScopedDepthCount, ScopedBarriers);
        const std::uint32_t ScopedBackBuffer = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSwapRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSwapRtv.ptr += ScopedBackBuffer * MRtvSize;
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedDsv = MDepthHeap->GetCPUDescriptorHandleForHeapStart();
        MCommandList->OMSetRenderTargets(1, &ScopedSwapRtv, FALSE, &ScopedDsv);
        MCommandList->SetPipelineState(MPipeline.Get());
        const D3D12_VIEWPORT ScopedMainViewport{0.0f, 0.0f, static_cast<float>(MExtent.width), static_cast<float>(MExtent.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedMainScissor{0, 0, static_cast<LONG>(MExtent.width), static_cast<LONG>(MExtent.height)};
        MCommandList->RSSetViewports(1, &ScopedMainViewport);
        MCommandList->RSSetScissorRects(1, &ScopedMainScissor);
        return true;
    }

    bool UpscaleRenderTarget(RenderTargetHandle InSource, RenderTargetHandle InDestination,
                             SpatialUpscaleMode InMode) override
    {
        const auto ScopedSource = MTargets.find(InSource.id);
        const auto ScopedOutputHeap = MTargetHeaps.find(InDestination.id);
        const auto ScopedOutputSize = MTargetSizes.find(InDestination.id);
        const auto ScopedSourceSize = MTargetSizes.find(InSource.id);
        if (!MInFrame || MCommandList == nullptr || ScopedSource == MTargets.end() ||
            ScopedOutputHeap == MTargetHeaps.end() || ScopedOutputSize == MTargetSizes.end() ||
            ScopedSourceSize == MTargetSizes.end() || MUpscalePipeline == nullptr || MUpscaleSrvHeap == nullptr)
        {
            return false;
        }
        MCommandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = ScopedSource->second.Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedCpu = MUpscaleSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrv{};
        ScopedSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrv.Format = ScopedSource->second->GetDesc().Format;
        ScopedSrv.Texture2D.MipLevels = 1;
        MDevice->CreateShaderResourceView(ScopedSource->second.Get(), &ScopedSrv, ScopedCpu);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = ScopedOutputHeap->second->GetCPUDescriptorHandleForHeapStart();
        MCommandList->OMSetRenderTargets(1, &ScopedRtv, FALSE, nullptr);
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f,
                                            static_cast<float>(std::max(ScopedOutputSize->second.width, 1u)),
                                            static_cast<float>(std::max(ScopedOutputSize->second.height, 1u)), 0.0f,
                                            1.0f};
        const D3D12_RECT ScopedScissor{0, 0, static_cast<LONG>(ScopedOutputSize->second.width),
                                       static_cast<LONG>(ScopedOutputSize->second.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        // Nearest = 0, bilinear = 1, bicubic = 2; FSR1 maps to bicubic.
        const float ScopedMode =
            InMode == SpatialUpscaleMode::Nearest ? 0.0f : (InMode == SpatialUpscaleMode::Bilinear ? 1.0f : 2.0f);
        const std::array<float, 4> ScopedConstants = {ScopedMode,
                                                      static_cast<float>(ScopedSourceSize->second.width),
                                                      static_cast<float>(ScopedSourceSize->second.height), 0.0f};
        MCommandList->SetPipelineState(MUpscalePipeline.Get());
        MCommandList->SetGraphicsRootSignature(MUpscaleRootSignature.Get());
        ID3D12DescriptorHeap* ScopedHeaps[] = {MUpscaleSrvHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        MCommandList->SetGraphicsRootDescriptorTable(0, MUpscaleSrvHeap->GetGPUDescriptorHandleForHeapStart());
        MCommandList->SetGraphicsRoot32BitConstants(1, 4, ScopedConstants.data(), 0);
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MCommandList->DrawInstanced(3, 1, 0, 0);
        MCommandList->SetDescriptorHeaps(0, nullptr);
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        MCommandList->SetPipelineState(MPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        ++MStats.drawCalls;
        return true;
    }

    bool RcasSharpen(RenderTargetHandle InSource, RenderTargetHandle InDestination, float InSharpness) override
    {
        const auto ScopedSource = MTargets.find(InSource.id);
        const auto ScopedOutputHeap = MTargetHeaps.find(InDestination.id);
        const auto ScopedSize = MTargetSizes.find(InDestination.id);
        if (!MInFrame || MCommandList == nullptr || MSharpenPipeline == nullptr || MTemporalRootSignature == nullptr ||
            ScopedSource == MTargets.end() || ScopedOutputHeap == MTargetHeaps.end() ||
            ScopedSize == MTargetSizes.end())
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedSrvHeapDesc{};
        ScopedSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedSrvHeapDesc.NumDescriptors = 5;
        ScopedSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> ScopedSrvHeap;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedSrvHeapDesc, IID_PPV_ARGS(&ScopedSrvHeap))))
        {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedCpuSrv = ScopedSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrvDesc{};
        ScopedSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrvDesc.Texture2D.MipLevels = 1;
        ScopedSrvDesc.Format = ScopedSource->second->GetDesc().Format;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 5; ++ScopedIndex)
        {
            MDevice->CreateShaderResourceView(ScopedSource->second.Get(), &ScopedSrvDesc, ScopedCpuSrv);
            ScopedCpuSrv.ptr += MSrvSize;
        }
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = ScopedSource->second.Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = ScopedOutputHeap->second->GetCPUDescriptorHandleForHeapStart();
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        MCommandList->ClearRenderTargetView(ScopedRtv, ScopedClear, 0, nullptr);
        MCommandList->OMSetRenderTargets(1, &ScopedRtv, FALSE, nullptr);
        const D3D12_VIEWPORT ScopedViewport{
            0.0f, 0.0f, static_cast<float>(ScopedSize->second.width),
            static_cast<float>(ScopedSize->second.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{
            0, 0, static_cast<LONG>(ScopedSize->second.width), static_cast<LONG>(ScopedSize->second.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        MCommandList->SetPipelineState(MSharpenPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MTemporalRootSignature.Get());
        ID3D12DescriptorHeap* ScopedHeaps[] = {ScopedSrvHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        MCommandList->SetGraphicsRootDescriptorTable(0, ScopedSrvHeap->GetGPUDescriptorHandleForHeapStart());
        const float ScopedConstants[4] = {
            InSharpness,
            ScopedSize->second.width > 0 ? 1.0f / static_cast<float>(ScopedSize->second.width) : 0.0f,
            ScopedSize->second.height > 0 ? 1.0f / static_cast<float>(ScopedSize->second.height) : 0.0f,
            0.0f};
        MCommandList->SetGraphicsRoot32BitConstants(1, 4, ScopedConstants, 0);
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MCommandList->DrawInstanced(3, 1, 0, 0);
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        const std::uint32_t ScopedBackBuffer = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSwapRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSwapRtv.ptr += ScopedBackBuffer * MRtvSize;
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedDsv = MDepthHeap->GetCPUDescriptorHandleForHeapStart();
        MCommandList->OMSetRenderTargets(1, &ScopedSwapRtv, FALSE, &ScopedDsv);
        MCommandList->SetPipelineState(MPipeline.Get());
        const D3D12_VIEWPORT ScopedMainViewport{0.0f, 0.0f, static_cast<float>(MExtent.width), static_cast<float>(MExtent.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedMainScissor{0, 0, static_cast<LONG>(MExtent.width), static_cast<LONG>(MExtent.height)};
        MCommandList->RSSetViewports(1, &ScopedMainViewport);
        MCommandList->RSSetScissorRects(1, &ScopedMainScissor);
        return true;
    }

    void SetHudTarget(RenderTargetHandle InHandle) override
    {
        MHudTarget = InHandle;
    }

    bool RenderHudTarget() override
    {
        return MHudTarget.IsValid() && MTargets.find(MHudTarget.id) != MTargets.end();
    }

    bool CompositeHud() override
    {
        if (!MUIVisible || !MHudTarget.IsValid() || !MInFrame || MCommandList == nullptr)
        {
            return false;
        }
        const auto ScopedTarget = MTargets.find(MHudTarget.id);
        if (ScopedTarget == MTargets.end() || MHudPipeline == nullptr || MHudSrvHeap == nullptr || !MHudReady)
        {
            return false;
        }
        const D3D12_RESOURCE_DESC ScopedTargetDesc = ScopedTarget->second->GetDesc();
        const std::uint32_t ScopedBackBuffer = MSwapchain->GetCurrentBackBufferIndex();
        MCommandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = ScopedTarget->second.Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedCpu = MHudSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrv{};
        ScopedSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrv.Format = ScopedTargetDesc.Format;
        ScopedSrv.Texture2D.MipLevels = 1;
        MDevice->CreateShaderResourceView(ScopedTarget->second.Get(), &ScopedSrv, ScopedCpu);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedBackRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedBackRtv.ptr += ScopedBackBuffer * MRtvSize;
        MCommandList->OMSetRenderTargets(1, &ScopedBackRtv, FALSE, nullptr);
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(MExtent.width),
                                            static_cast<float>(MExtent.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, static_cast<LONG>(MExtent.width), static_cast<LONG>(MExtent.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        const std::array<float, 4> ScopedConstants = {MHudUpscaleNearest ? 1.0f : 0.0f,
                                                      static_cast<float>(ScopedTargetDesc.Width),
                                                      static_cast<float>(ScopedTargetDesc.Height), 0.0f};
        MCommandList->SetPipelineState(MHudPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MHudRootSignature.Get());
        ID3D12DescriptorHeap* ScopedHeaps[] = {MHudSrvHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        MCommandList->SetGraphicsRootDescriptorTable(0, MHudSrvHeap->GetGPUDescriptorHandleForHeapStart());
        MCommandList->SetGraphicsRoot32BitConstants(1, 4, ScopedConstants.data(), 0);
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MCommandList->DrawInstanced(3, 1, 0, 0);
        MCommandList->SetDescriptorHeaps(0, nullptr);
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        MCommandList->SetPipelineState(MPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        ++MStats.drawCalls;
        return true;
    }

    void SetHudUpscale(bool InNearest) override
    {
        MHudUpscaleNearest = InNearest;
    }

    void SetUiVisible(bool InVisible) override
    {
        MUIVisible = InVisible;
    }

    void PresentRenderTarget(RenderTargetHandle InHandle) override
    {
        const auto ScopedTarget = MTargets.find(InHandle.id);
        if (!MInFrame || MCommandList == nullptr || ScopedTarget == MTargets.end() || MPresentPipeline == nullptr ||
            MPresentSrvHeap == nullptr)
        {
            return;
        }
        const std::uint32_t ScopedIndex = MSwapchain->GetCurrentBackBufferIndex();
        const D3D12_RESOURCE_DESC ScopedDesc = ScopedTarget->second->GetDesc();
        MCommandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = ScopedTarget->second.Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedCpu = MPresentSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrv{};
        ScopedSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrv.Format = ScopedDesc.Format;
        ScopedSrv.Texture2D.MipLevels = 1;
        MDevice->CreateShaderResourceView(ScopedTarget->second.Get(), &ScopedSrv, ScopedCpu);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedBackRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedBackRtv.ptr += ScopedIndex * MRtvSize;
        MCommandList->OMSetRenderTargets(1, &ScopedBackRtv, FALSE, nullptr);
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(MExtent.width),
                                            static_cast<float>(MExtent.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, static_cast<LONG>(MExtent.width), static_cast<LONG>(MExtent.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        MCommandList->SetPipelineState(MPresentPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MPresentRootSignature.Get());
        ID3D12DescriptorHeap* ScopedHeaps[] = {MPresentSrvHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        MCommandList->SetGraphicsRootDescriptorTable(0, MPresentSrvHeap->GetGPUDescriptorHandleForHeapStart());
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MCommandList->DrawInstanced(3, 1, 0, 0);
        MCommandList->SetDescriptorHeaps(0, nullptr);
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        MCommandList->SetPipelineState(MPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        ++MStats.drawCalls;
    }

    bool RenderToTarget(RenderTargetHandle InHandle, const FramePacket& InPacket) override
    {
        const auto ScopedHeap = MTargetHeaps.find(InHandle.id);
        const auto ScopedSize = MTargetSizes.find(InHandle.id);
        if (!MInFrame || MCommandList == nullptr || ScopedHeap == MTargetHeaps.end() ||
            ScopedSize == MTargetSizes.end())
        {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = ScopedHeap->second->GetCPUDescriptorHandleForHeapStart();
        // The offscreen pass clears with the packet colour, not black.
        const float ScopedClear[4] = {static_cast<float>(InPacket.clear.R()) / 255.0f,
                                      static_cast<float>(InPacket.clear.G()) / 255.0f,
                                      static_cast<float>(InPacket.clear.B()) / 255.0f,
                                      static_cast<float>(InPacket.clear.A()) / 255.0f};
        MCommandList->ClearRenderTargetView(ScopedRtv, ScopedClear, 0, nullptr);
        const auto ScopedDepth = MTargetDepthHeaps.find(InHandle.id);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedDepthDsv{};
        D3D12_CPU_DESCRIPTOR_HANDLE* ScopedDepthPointer = nullptr;
        if (ScopedDepth != MTargetDepthHeaps.end())
        {
            ScopedDepthDsv = ScopedDepth->second->GetCPUDescriptorHandleForHeapStart();
            MCommandList->ClearDepthStencilView(ScopedDepthDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            ScopedDepthPointer = &ScopedDepthDsv;
        }
        MCommandList->OMSetRenderTargets(1, &ScopedRtv, FALSE, ScopedDepthPointer);
        const D3D12_VIEWPORT ScopedViewport{
            0.0f, 0.0f, static_cast<float>(ScopedSize->second.width),
            static_cast<float>(ScopedSize->second.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{
            0, 0, static_cast<LONG>(ScopedSize->second.width), static_cast<LONG>(ScopedSize->second.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        const auto ScopedBlendMode = MTargetBlend.find(InHandle.id);
        ID3D12PipelineState* ScopedTargetPipeline = MPipeline.Get();
        if (ScopedBlendMode != MTargetBlend.end())
        {
            if (ScopedBlendMode->second == RenderBlendMode::Additive)
            {
                ScopedTargetPipeline = MAdditivePipeline.Get();
            }
            else if (ScopedBlendMode->second == RenderBlendMode::Reveal)
            {
                ScopedTargetPipeline = MRevealPipeline.Get();
            }
        }
        MCommandList->SetPipelineState(ScopedTargetPipeline);
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        Submit(InPacket);
        if (AccessibilityWanted(InPacket))
        {
            ApplyAccessibility(InHandle, InPacket, ScopedSize->second);
        }
        MCommandList->SetPipelineState(MPipeline.Get());
        const std::uint32_t ScopedBackBuffer = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSwapRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSwapRtv.ptr += ScopedBackBuffer * MRtvSize;
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedDsv = MDepthHeap->GetCPUDescriptorHandleForHeapStart();
        MCommandList->OMSetRenderTargets(1, &ScopedSwapRtv, FALSE, &ScopedDsv);
        const D3D12_VIEWPORT ScopedMainViewport{0.0f, 0.0f, static_cast<float>(MExtent.width), static_cast<float>(MExtent.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedMainScissor{0, 0, static_cast<LONG>(MExtent.width), static_cast<LONG>(MExtent.height)};
        MCommandList->RSSetViewports(1, &ScopedMainViewport);
        MCommandList->RSSetScissorRects(1, &ScopedMainScissor);
        return true;
    }

    bool CopyDepth(RenderTargetHandle InSource, RenderTargetHandle InDestination) override
    {
        // The temporal path reads the colour copy, the depth copy keeps the
        // depth history in sync with Vulkan semantics.
        if (!CopyColorImage(InSource, InDestination))
        {
            return false;
        }
        const auto ScopedSourceDepth = MTargetDepthResources.find(InSource.id);
        const auto ScopedDestinationDepth = MTargetDepthResources.find(InDestination.id);
        if (!MInFrame || MCommandList == nullptr || ScopedSourceDepth == MTargetDepthResources.end() ||
            ScopedDestinationDepth == MTargetDepthResources.end())
        {
            return true;
        }
        const D3D12_RESOURCE_DESC ScopedSourceDesc = ScopedSourceDepth->second->GetDesc();
        const D3D12_RESOURCE_DESC ScopedDestinationDesc = ScopedDestinationDepth->second->GetDesc();
        if (ScopedSourceDesc.Width != ScopedDestinationDesc.Width ||
            ScopedSourceDesc.Height != ScopedDestinationDesc.Height ||
            ScopedSourceDesc.Format != ScopedDestinationDesc.Format)
        {
            return true;
        }
        // Recording on the frame list keeps the copy ordered after the draws;
        // the temporal pass calls this while its own list is still open.
        D3D12_RESOURCE_BARRIER ScopedBarriers[2]{};
        ScopedBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[0].Transition.pResource = ScopedSourceDepth->second.Get();
        ScopedBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[1].Transition.pResource = ScopedDestinationDepth->second.Get();
        ScopedBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        MCommandList->ResourceBarrier(2, ScopedBarriers);
        MCommandList->CopyResource(ScopedDestinationDepth->second.Get(), ScopedSourceDepth->second.Get());
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        MCommandList->ResourceBarrier(2, ScopedBarriers);
        return true;
    }

    bool CopyColorImage(RenderTargetHandle InSource, RenderTargetHandle InDestination) override
    {
        const auto ScopedSource = MTargets.find(InSource.id);
        const auto ScopedDestination = MTargets.find(InDestination.id);
        if (ScopedSource == MTargets.end() || ScopedDestination == MTargets.end() ||
            ScopedSource->second == nullptr || ScopedDestination->second == nullptr)
        {
            return false;
        }
        const D3D12_RESOURCE_DESC ScopedSourceDesc = ScopedSource->second->GetDesc();
        const D3D12_RESOURCE_DESC ScopedDestinationDesc = ScopedDestination->second->GetDesc();
        if (ScopedSourceDesc.Width != ScopedDestinationDesc.Width ||
            ScopedSourceDesc.Height != ScopedDestinationDesc.Height ||
            ScopedSourceDesc.Format != ScopedDestinationDesc.Format)
        {
            return false;
        }
        ComPtr<ID3D12CommandAllocator> ScopedAllocator;
        ComPtr<ID3D12GraphicsCommandList> ScopedCommands;
        if (FAILED(MDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ScopedAllocator))) ||
            FAILED(MDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ScopedAllocator.Get(), nullptr,
                                              IID_PPV_ARGS(&ScopedCommands))))
        {
            return false;
        }
        D3D12_RESOURCE_BARRIER ScopedBarriers[2]{};
        ScopedBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[0].Transition.pResource = ScopedSource->second.Get();
        ScopedBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[1].Transition.pResource = ScopedDestination->second.Get();
        ScopedBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedCommands->ResourceBarrier(2, ScopedBarriers);
        ScopedCommands->CopyResource(ScopedDestination->second.Get(), ScopedSource->second.Get());
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedCommands->ResourceBarrier(2, ScopedBarriers);
        ScopedCommands->Close();
        ID3D12CommandList* ScopedLists[] = {ScopedCommands.Get()};
        MQueue->ExecuteCommandLists(1, ScopedLists);
        WaitIdle();
        return true;
    }

    bool CaptureRenderTarget(RenderTargetHandle InHandle, std::span<std::byte> InRgba8,
                             Extent2D* OutSize = nullptr) override
    {
        const auto ScopedTarget = MTargets.find(InHandle.id);
        if (ScopedTarget == MTargets.end() || InRgba8.empty())
        {
            LogWarn("d3d12", "CaptureRenderTarget: target {} missing or destination empty", InHandle.id);
            return false;
        }
        D3D12_RESOURCE_DESC ScopedTargetDesc = ScopedTarget->second->GetDesc();
        if (ScopedTargetDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
            ScopedTargetDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            ScopedTargetDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
            ScopedTargetDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            LogWarn("d3d12", "CaptureRenderTarget: target {} has unsupported format {}", InHandle.id,
                    static_cast<unsigned>(ScopedTargetDesc.Format));
            return false;
        }
        const std::uint32_t ScopedWidth = static_cast<std::uint32_t>(ScopedTargetDesc.Width);
        const std::uint32_t ScopedHeight = ScopedTargetDesc.Height;
        const std::size_t ScopedBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u;
        if (InRgba8.size() < ScopedBytes)
        {
            LogWarn("d3d12", "CaptureRenderTarget: destination {} is smaller than {} bytes", InRgba8.size(),
                    ScopedBytes);
            return false;
        }
        const UINT ScopedRowPitch = (ScopedWidth * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
                                    ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
        D3D12_HEAP_PROPERTIES ScopedReadbackHeap{};
        ScopedReadbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC ScopedBufferDesc{};
        ScopedBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ScopedBufferDesc.Width = static_cast<UINT64>(ScopedRowPitch) * ScopedHeight;
        ScopedBufferDesc.Height = 1;
        ScopedBufferDesc.DepthOrArraySize = 1;
        ScopedBufferDesc.MipLevels = 1;
        ScopedBufferDesc.SampleDesc.Count = 1;
        ScopedBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> ScopedReadback;
        const HRESULT ScopedReadbackResult =
            MDevice->CreateCommittedResource(&ScopedReadbackHeap, D3D12_HEAP_FLAG_NONE, &ScopedBufferDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&ScopedReadback));
        if (FAILED(ScopedReadbackResult))
        {
            if (ScopedReadbackResult == DXGI_ERROR_DEVICE_REMOVED)
            {
                MInfo.deviceLost = true;
            }
            LogWarn("d3d12", "CaptureRenderTarget: readback buffer creation failed (0x{:08X})",
                    static_cast<unsigned>(ScopedReadbackResult));
            return false;
        }
        ComPtr<ID3D12CommandAllocator> ScopedAllocator;
        ComPtr<ID3D12GraphicsCommandList> ScopedCommands;
        if (FAILED(MDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ScopedAllocator))) ||
            FAILED(MDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ScopedAllocator.Get(), nullptr,
                                              IID_PPV_ARGS(&ScopedCommands))))
        {
            return false;
        }
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = ScopedTarget->second.Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedCommands->ResourceBarrier(1, &ScopedBarrier);
        D3D12_TEXTURE_COPY_LOCATION ScopedDestination{};
        ScopedDestination.pResource = ScopedReadback.Get();
        ScopedDestination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        ScopedDestination.PlacedFootprint.Footprint.Format = ScopedTargetDesc.Format;
        ScopedDestination.PlacedFootprint.Footprint.Width = ScopedWidth;
        ScopedDestination.PlacedFootprint.Footprint.Height = ScopedHeight;
        ScopedDestination.PlacedFootprint.Footprint.Depth = 1;
        ScopedDestination.PlacedFootprint.Footprint.RowPitch = ScopedRowPitch;
        D3D12_TEXTURE_COPY_LOCATION ScopedSource{};
        ScopedSource.pResource = ScopedTarget->second.Get();
        ScopedSource.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ScopedSource.SubresourceIndex = 0;
        ScopedCommands->CopyTextureRegion(&ScopedDestination, 0, 0, 0, &ScopedSource, nullptr);
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedCommands->ResourceBarrier(1, &ScopedBarrier);
        ScopedCommands->Close();
        ID3D12CommandList* ScopedLists[] = {ScopedCommands.Get()};
        MQueue->ExecuteCommandLists(1, ScopedLists);
        WaitIdle();
        void* ScopedMapped = nullptr;
        const HRESULT ScopedMapResult = ScopedReadback->Map(0, nullptr, &ScopedMapped);
        if (FAILED(ScopedMapResult))
        {
            LogWarn("d3d12", "CaptureRenderTarget: readback map failed (0x{:08X})",
                    static_cast<unsigned>(ScopedMapResult));
            return false;
        }
        const bool ScopedSwap = ScopedTargetDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
        for (std::uint32_t ScopedY = 0; ScopedY < ScopedHeight; ++ScopedY)
        {
            const auto* ScopedSourceRow = static_cast<const std::byte*>(ScopedMapped) +
                                          static_cast<std::size_t>(ScopedY) * ScopedRowPitch;
            std::byte* ScopedDestinationRow = InRgba8.data() + static_cast<std::size_t>(ScopedY) * ScopedWidth * 4u;
            for (std::uint32_t ScopedX = 0; ScopedX < ScopedWidth; ++ScopedX)
            {
                const std::size_t ScopedOffset = static_cast<std::size_t>(ScopedX) * 4u;
                if (ScopedSwap)
                {
                    ScopedDestinationRow[ScopedOffset + 0] = ScopedSourceRow[ScopedOffset + 2];
                    ScopedDestinationRow[ScopedOffset + 1] = ScopedSourceRow[ScopedOffset + 1];
                    ScopedDestinationRow[ScopedOffset + 2] = ScopedSourceRow[ScopedOffset + 0];
                    ScopedDestinationRow[ScopedOffset + 3] = ScopedSourceRow[ScopedOffset + 3];
                }
                else
                {
                    std::memcpy(ScopedDestinationRow + ScopedOffset, ScopedSourceRow + ScopedOffset, 4);
                }
            }
        }
        ScopedReadback->Unmap(0, nullptr);
        if (OutSize != nullptr)
        {
            *OutSize = {ScopedWidth, ScopedHeight};
        }
        return true;
    }

    bool RenderMotionVectors(RenderTargetHandle InTarget, std::uint32_t InCount, Vector2D InCameraCenter,
                             Vector2D InPreviousCameraCenter, float InCameraZoom, float InPreviousCameraZoom,
                             Vector2D InJitter, Vector2D InPreviousJitter, Vector2D InViewport) override
    {
        (void)InPreviousCameraCenter;
        (void)InPreviousCameraZoom;
        (void)InPreviousJitter;
        if (const char* ScopedNoop = std::getenv("FBY_D3D12_MOTION_NOOP"); ScopedNoop != nullptr && ScopedNoop[0] == '1')
        {
            return true;
        }
        const auto ScopedHeap = MTargetHeaps.find(InTarget.id);
        const auto ScopedSize = MTargetSizes.find(InTarget.id);
        if (!MInFrame || MCommandList == nullptr || MPipeline == nullptr || MRootSignature == nullptr ||
            MVertexBuffer == nullptr ||
            ScopedHeap == MTargetHeaps.end() || ScopedSize == MTargetSizes.end() || InCount == 0 ||
            MMotionItems.empty())
        {
            std::fprintf(stderr, "d3d12 motion: frame=%d cmd=%p pso=%p vb=%p srv=%zu target=%d size=%d count=%u items=%zu\n",
                         MInFrame ? 1 : 0, static_cast<void*>(MCommandList.Get()),
                         static_cast<void*>(MPipeline.Get()), static_cast<void*>(MVertexBuffer.Get()),
                         MTextureSrvIndex.size(), ScopedHeap != MTargetHeaps.end() ? 1 : 0,
                         ScopedSize != MTargetSizes.end() ? 1 : 0, InCount, MMotionItems.size());
            return false;
        }
        const std::uint32_t ScopedCount = std::min(InCount, static_cast<std::uint32_t>(MMotionItems.size()));
        void* ScopedMapped = nullptr;
        const HRESULT ScopedMotionMap = MVertexBuffer->Map(0, nullptr, &ScopedMapped);
        if (FAILED(ScopedMotionMap))
        {
            std::fprintf(stderr, "d3d12 motion: map failed hr=%lx\n", static_cast<unsigned long>(ScopedMotionMap));
            return false;
        }
        auto* ScopedVertices = static_cast<SmokeVertex*>(ScopedMapped);
        std::size_t ScopedVertexCount = 0;
        const float ScopedViewportX = InViewport.X > 0.0f ? InViewport.X : static_cast<float>(ScopedSize->second.width);
        const float ScopedViewportY = InViewport.Y > 0.0f ? InViewport.Y : static_cast<float>(ScopedSize->second.height);
        const float ScopedZoom = InCameraZoom > 0.0f ? InCameraZoom : 1.0f;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            const MotionVectorItem& ScopedItem = MMotionItems[ScopedIndex];
            const float ScopedHalfX = ScopedItem.size.X * 0.5f * ScopedZoom;
            const float ScopedHalfY = ScopedItem.size.Y * 0.5f * ScopedZoom;
            const float ScopedCenterX =
                (ScopedItem.position.X - InCameraCenter.X) * ScopedZoom / (ScopedViewportX * 0.5f);
            const float ScopedCenterY =
                (ScopedItem.position.Y - InCameraCenter.Y) * ScopedZoom / (ScopedViewportY * 0.5f);
            const float ScopedDeltaX = (ScopedItem.screenDelta.X + InJitter.X) / ScopedViewportX;
            const float ScopedDeltaY = (ScopedItem.screenDelta.Y + InJitter.Y) / ScopedViewportY;
            const SmokeVertex ScopedCorners[6] = {
                {{ScopedCenterX - ScopedHalfX, ScopedCenterY - ScopedHalfY, 0.5f}, {0.0f, 0.0f},
                 {ScopedDeltaX, ScopedDeltaY, 0.0f, 1.0f}, 0.0f},
                {{ScopedCenterX + ScopedHalfX, ScopedCenterY - ScopedHalfY, 0.5f}, {1.0f, 0.0f},
                 {ScopedDeltaX, ScopedDeltaY, 0.0f, 1.0f}, 0.0f},
                {{ScopedCenterX + ScopedHalfX, ScopedCenterY + ScopedHalfY, 0.5f}, {1.0f, 1.0f},
                 {ScopedDeltaX, ScopedDeltaY, 0.0f, 1.0f}, 0.0f},
                {{ScopedCenterX - ScopedHalfX, ScopedCenterY - ScopedHalfY, 0.5f}, {0.0f, 0.0f},
                 {ScopedDeltaX, ScopedDeltaY, 0.0f, 1.0f}, 0.0f},
                {{ScopedCenterX + ScopedHalfX, ScopedCenterY + ScopedHalfY, 0.5f}, {1.0f, 1.0f},
                 {ScopedDeltaX, ScopedDeltaY, 0.0f, 1.0f}, 0.0f},
                {{ScopedCenterX - ScopedHalfX, ScopedCenterY + ScopedHalfY, 0.5f}, {0.0f, 1.0f},
                 {ScopedDeltaX, ScopedDeltaY, 0.0f, 1.0f}, 0.0f},
            };
            std::memcpy(ScopedVertices + ScopedVertexCount, ScopedCorners, sizeof(ScopedCorners));
            ScopedVertexCount += 6;
        }
        MVertexBuffer->Unmap(0, nullptr);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = ScopedHeap->second->GetCPUDescriptorHandleForHeapStart();
        MCommandList->OMSetRenderTargets(1, &ScopedRtv, FALSE, nullptr);
        if (const char* ScopedOnly = std::getenv("FBY_D3D12_MOTION_OMSET_ONLY"); ScopedOnly != nullptr && ScopedOnly[0] == '1')
        {
            return true;
        }
        const D3D12_VIEWPORT ScopedMotionViewport{
            0.0f, 0.0f, static_cast<float>(ScopedSize->second.width),
            static_cast<float>(ScopedSize->second.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedMotionScissor{
            0, 0, static_cast<LONG>(ScopedSize->second.width), static_cast<LONG>(ScopedSize->second.height)};
        MCommandList->RSSetViewports(1, &ScopedMotionViewport);
        MCommandList->RSSetScissorRects(1, &ScopedMotionScissor);
        MCommandList->SetPipelineState(MMotionPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        const D3D12_VERTEX_BUFFER_VIEW ScopedVbv{MVertexBuffer->GetGPUVirtualAddress(),
                                                 static_cast<UINT>(sizeof(MotionVertex) * ScopedVertexCount),
                                                 sizeof(MotionVertex)};
        MCommandList->IASetVertexBuffers(0, 1, &ScopedVbv);
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (const char* ScopedNoDraw = std::getenv("FBY_D3D12_MOTION_NO_DRAW");
            ScopedNoDraw != nullptr && ScopedNoDraw[0] == '1')
        {
            return true;
        }
        const UINT ScopedDrawCount = (std::getenv("FBY_D3D12_MOTION_ZERO_DRAW") != nullptr) ? 0u
                                                                                          : static_cast<UINT>(ScopedVertexCount);
        MCommandList->DrawInstanced(ScopedDrawCount, 1, 0, 0);
        const std::uint32_t ScopedBackBuffer = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSwapRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSwapRtv.ptr += ScopedBackBuffer * MRtvSize;
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedDsv = MDepthHeap->GetCPUDescriptorHandleForHeapStart();
        MCommandList->OMSetRenderTargets(1, &ScopedSwapRtv, FALSE, &ScopedDsv);
        MCommandList->SetPipelineState(MMotionPipeline.Get());
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(MExtent.width), static_cast<float>(MExtent.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, static_cast<LONG>(MExtent.width), static_cast<LONG>(MExtent.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        return true;
    }

    [[nodiscard]] bool RenderTargetDepthTexture(RenderTargetHandle InHandle, NativeTextureView& InView) const override
    {
        const auto ScopedDepth = MTargetDepthResources.find(InHandle.id);
        if (ScopedDepth == MTargetDepthResources.end())
        {
            return false;
        }
        InView.view = nullptr;
        InView.image = ScopedDepth->second.Get();
        InView.layout = 0;
        return true;
    }

    [[nodiscard]] bool GetNativeDeviceHandles(NativeDeviceHandles& OutHandles) const override
    {
        OutHandles.Device = MDevice.Get();
        OutHandles.PhysicalDevice = MPhysicalAdapter.Get();
        OutHandles.Instance = MFactory.Get();
        OutHandles.DeviceProcAddr = nullptr;
        return OutHandles.Device != nullptr;
    }

    [[nodiscard]] bool RenderTargetTexture(RenderTargetHandle InHandle, NativeTextureView& InView) const override
    {
        const auto ScopedTarget = MTargets.find(InHandle.id);
        if (ScopedTarget == MTargets.end())
        {
            return false;
        }
        InView.view = nullptr;
        InView.image = ScopedTarget->second.Get();
        InView.layout = 0;
        return true;
    }

    [[nodiscard]] void* CurrentCommandBuffer() const noexcept override
    {
        return MCommandList.Get();
    }

        [[nodiscard]] std::filesystem::path PsoCachePath() const
    {
        const char* ScopedEnv = std::getenv("FBY_D3D12_PSO_CACHE");
        std::filesystem::path ScopedPath;
        if (ScopedEnv != nullptr)
        {
            ScopedPath = std::filesystem::path(ScopedEnv);
        }
        else
        {
            // Per-executable cache: parallel test binaries would otherwise share
            // one file and lose saved pipelines at random.
            wchar_t ScopedExecutable[MAX_PATH]{};
            const DWORD ScopedLength = GetModuleFileNameW(nullptr, ScopedExecutable, MAX_PATH);
            if (ScopedLength > 0 && ScopedLength < MAX_PATH)
            {
                ScopedPath = std::filesystem::path("Saved") /
                             (std::filesystem::path(ScopedExecutable).stem().wstring() + L"_pso_cache.bin");
            }
            else
            {
                ScopedPath = std::filesystem::path("Saved/d3d12_pso_cache.bin");
            }
        }
        std::error_code ScopedError;
        if (!ScopedPath.parent_path().empty())
        {
            std::filesystem::create_directories(ScopedPath.parent_path(), ScopedError);
        }
        return ScopedPath;
    }

    void EnsurePipelineLibrary() noexcept
    {
        try
        {
        if (MPipelineLibraryLoaded)
        {
            return;
        }
        MPipelineLibraryLoaded = true;
        ComPtr<ID3D12Device1> ScopedDevice;
        if (FAILED(MDevice.As(&ScopedDevice)) || ScopedDevice == nullptr)
        {
            return;
        }
        std::vector<std::byte> ScopedBlob;
        const std::filesystem::path ScopedPath = PsoCachePath();
        std::ifstream ScopedStream(ScopedPath, std::ios::binary | std::ios::ate);
        if (ScopedStream)
        {
            const std::streamsize ScopedSize = ScopedStream.tellg();
            if (ScopedSize > 0)
            {
                ScopedBlob.resize(static_cast<std::size_t>(ScopedSize));
                ScopedStream.seekg(0, std::ios::beg);
                ScopedStream.read(reinterpret_cast<char*>(ScopedBlob.data()), ScopedSize);
            }
        }
        if (!ScopedBlob.empty() &&
            SUCCEEDED(ScopedDevice->CreatePipelineLibrary(ScopedBlob.data(), ScopedBlob.size(),
                                                          IID_PPV_ARGS(&MPipelineLibrary))))
        {
            LogInfo("d3d12", "PSO cache loaded: {}", ScopedPath.string());
            return;
        }
        if (SUCCEEDED(ScopedDevice->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&MPipelineLibrary))))
        {
            LogInfo("d3d12", "PSO cache created: {}", ScopedPath.string());
        }
        }
        catch (...)
        {
            return;
        }
    }

    void SavePipelineLibrary() noexcept
    {
        try
        {
        if (MPipelineLibrary == nullptr)
        {
            return;
        }
        const SIZE_T ScopedSize = MPipelineLibrary->GetSerializedSize();
        if (ScopedSize == 0)
        {
            return;
        }
        std::vector<std::byte> ScopedBlob(ScopedSize);
        if (FAILED(MPipelineLibrary->Serialize(ScopedBlob.data(), ScopedSize)))
        {
            return;
        }
        const std::filesystem::path ScopedPath = PsoCachePath();
        std::ofstream ScopedStream(ScopedPath, std::ios::binary | std::ios::trunc);
        if (ScopedStream)
        {
            ScopedStream.write(reinterpret_cast<const char*>(ScopedBlob.data()),
                               static_cast<std::streamsize>(ScopedSize));
        }
        }
        catch (...)
        {
            return;
        }
    }

[[nodiscard]] ShaderPipelineHandle CreateShaderPipeline(const ShaderPipelineDesc& InDesc) override
    {
        if ((InDesc.domain != ShaderPipelineDomain::Sprite && InDesc.domain != ShaderPipelineDomain::Mesh) ||
            InDesc.fragmentDxil.empty())
        {
            return {};
        }
        // The fragment model chooses the vertex model: DXIL pairs with the SM6
        // material VS, DXBC with the SM5 one.
        const bool ScopedDxil = IsDxilBytecode(InDesc.fragmentDxil);
        D3D12_SHADER_BYTECODE ScopedVS{};
        if (!InDesc.vertexDxil.empty())
        {
            ScopedVS = {InDesc.vertexDxil.data(), InDesc.vertexDxil.size()};
        }
        else if (ScopedDxil)
        {
            ScopedVS = {Shaders::Dx::D3D12MaterialSpriteVsDxil.data(), Shaders::Dx::D3D12MaterialSpriteVsDxil.size()};
        }
        else
        {
            ScopedVS = {Shaders::Dx::D3D12MaterialSpriteVsSm5.data(), Shaders::Dx::D3D12MaterialSpriteVsSm5.size()};
        }
        const D3D12_SHADER_BYTECODE ScopedPS{InDesc.fragmentDxil.data(), InDesc.fragmentDxil.size()};
        const D3D12_INPUT_ELEMENT_DESC ScopedLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
        ScopedPso.pRootSignature = MMaterialRootSignature.Get();
        ScopedPso.VS = ScopedVS;
        ScopedPso.PS = ScopedPS;
        ScopedPso.InputLayout = {ScopedLayout, static_cast<UINT>(std::size(ScopedLayout))};
        ScopedPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedPso.NumRenderTargets = 1;
        ScopedPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedPso.SampleDesc.Count = 1;
        ScopedPso.SampleMask = UINT_MAX;
        ScopedPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedPso.RasterizerState.DepthClipEnable = TRUE;
        ScopedPso.BlendState.RenderTarget[0].BlendEnable = TRUE;
        ScopedPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        ScopedPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        ScopedPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        ScopedPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        ScopedPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        ScopedPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        ScopedPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        ScopedPso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        ScopedPso.DepthStencilState.DepthEnable = TRUE;
        ScopedPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        ScopedPso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        ComPtr<ID3D12PipelineState> ScopedPipeline;
        EnsurePipelineLibrary();
        const std::wstring ScopedCacheName =
            InDesc.debugName != nullptr ? std::wstring(InDesc.debugName, InDesc.debugName + std::strlen(InDesc.debugName))
                                        : std::wstring{};
        if (!ScopedCacheName.empty() && MPipelineLibrary != nullptr &&
            SUCCEEDED(MPipelineLibrary->LoadGraphicsPipeline(ScopedCacheName.c_str(), &ScopedPso,
                                                             IID_PPV_ARGS(&ScopedPipeline))))
        {
            LogInfo("d3d12", "material PSO loaded from cache");
        }
        else if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedPso, IID_PPV_ARGS(&ScopedPipeline))))
        {
            ComPtr<ID3D12InfoQueue> ScopedInfo;
            if (SUCCEEDED(MDevice.As(&ScopedInfo)) && ScopedInfo != nullptr && ScopedInfo->GetNumStoredMessages() > 0)
            {
                SIZE_T ScopedMessageSize = 0;
                ScopedInfo->GetMessage(ScopedInfo->GetNumStoredMessages() - 1, nullptr, &ScopedMessageSize);
                std::vector<char> ScopedMessage(ScopedMessageSize);
                auto* ScopedEntry = reinterpret_cast<D3D12_MESSAGE*>(ScopedMessage.data());
                if (SUCCEEDED(ScopedInfo->GetMessage(ScopedInfo->GetNumStoredMessages() - 1, ScopedEntry,
                                                      &ScopedMessageSize)))
                {
                    std::fprintf(stderr, "material PSO: %s\n", ScopedEntry->pDescription);
                }
            }
            return {};
        }
        if (!ScopedCacheName.empty() && MPipelineLibrary != nullptr)
        {
            (void)MPipelineLibrary->StorePipeline(ScopedCacheName.c_str(), ScopedPipeline.Get());
        }
        const std::uint32_t ScopedId = MNextMaterialPipeline++;
        MMaterialPipelines.emplace(ScopedId, std::move(ScopedPipeline));
        MMaterialPipelineDomains[ScopedId] = InDesc.domain;
        ShaderPipelineHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }

    void DestroyShaderPipeline(ShaderPipelineHandle InHandle) override
    {
        MMaterialPipelineDomains.erase(InHandle.id);
        MMaterialPipelines.erase(InHandle.id);
    }

    [[nodiscard]] bool SupportsRuntimeShaderPipelines() const noexcept override
    {
        return true;
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
        return Backend::D3D12;
    }
    [[nodiscard]] std::size_t TransientCapacity() const noexcept override
    {
        return 0;
    }

private:
    struct SmokeVertex
    {
        float Position[3];
        float Uv[2];
        float Color[4];
        float Layer;
        float NormalMeta = 0.0f;
    };

    struct MotionVertex
    {
        float Position[3];
        float Delta[2];
    };

    void DumpValidation()
    {
        if (MInfoQueue == nullptr)
        {
            return;
        }
        const UINT64 ScopedCount = MInfoQueue->GetNumStoredMessages();
        for (UINT64 ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            SIZE_T ScopedSize = 0;
            if (FAILED(MInfoQueue->GetMessage(ScopedIndex, nullptr, &ScopedSize)) || ScopedSize == 0)
            {
                continue;
            }
            std::vector<std::byte> ScopedStorage(ScopedSize);
            auto* ScopedMessage = reinterpret_cast<D3D12_MESSAGE*>(ScopedStorage.data());
            if (SUCCEEDED(MInfoQueue->GetMessage(ScopedIndex, ScopedMessage, &ScopedSize)))
            {
                std::fprintf(stderr, "d3d12 validation [%d]: %s\n",
                             static_cast<int>(ScopedMessage->Severity), ScopedMessage->pDescription);
            }
        }
        MInfoQueue->ClearStoredMessages();
    }

    bool CreateFactoryAndDevice()
    {
        if (const char* ScopedDebug = std::getenv("FBY_D3D12_DEBUG"); ScopedDebug != nullptr && ScopedDebug[0] == '1')
        {
            ComPtr<ID3D12Debug> ScopedDebugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&ScopedDebugController))) &&
                ScopedDebugController != nullptr)
            {
                ScopedDebugController->EnableDebugLayer();
                MDebugLayer = true;
                std::fprintf(stderr, "d3d12: debug layer enabled\n");
            }
        }
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&MFactory))) || MFactory == nullptr)
        {
            return false;
        }
        ComPtr<IDXGIAdapter1> ScopedAdapter;
        for (UINT ScopedIndex = 0;
             MFactory->EnumAdapters1(ScopedIndex, &ScopedAdapter) != DXGI_ERROR_NOT_FOUND; ++ScopedIndex)
        {
            if (FAILED(D3D12CreateDevice(ScopedAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&MDevice))))
            {
                continue;
            }
            DXGI_ADAPTER_DESC1 ScopedDesc{};
            ScopedAdapter->GetDesc1(&ScopedDesc);
            std::snprintf(MInfo.name, sizeof(MInfo.name), "%ls", ScopedDesc.Description);
            MInfo.discrete = ScopedDesc.Flags == 0;
            MPhysicalAdapter = ScopedAdapter;
            MInfo.gpuVendor = ScopedDesc.VendorId == 0x10DE ? 1
                            : (ScopedDesc.VendorId == 0x1002 || ScopedDesc.VendorId == 0x1022) ? 2
                            : ScopedDesc.VendorId == 0x8086 ? 3
                            : 0;
            MInfo.apiVersion = static_cast<std::uint32_t>(D3D_FEATURE_LEVEL_11_0);
            MInfo.backend = Backend::D3D12;
            std::snprintf(MInfo.driver, sizeof(MInfo.driver), "D3D12 feature level %x",
                          static_cast<unsigned>(D3D_FEATURE_LEVEL_11_0));
            if (MDebugLayer)
            {
                MDevice.As(&MInfoQueue);
                std::fprintf(stderr, "d3d12: info queue=%d\n", MInfoQueue != nullptr ? 1 : 0);
            }
            return true;
        }
        return false;
    }

    bool CreateSwapchain(const DeviceDesc& InDesc)
    {
        D3D12_COMMAND_QUEUE_DESC ScopedQueue{};
        ScopedQueue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(MDevice->CreateCommandQueue(&ScopedQueue, IID_PPV_ARGS(&MQueue))))
        {
            return false;
        }
        auto* ScopedWindow = static_cast<SDL_Window*>(InDesc.nativeWindow);
        if (ScopedWindow == nullptr)
        {
            return false;
        }
        SDL_PropertiesID ScopedProperties = SDL_GetWindowProperties(ScopedWindow);
        HWND ScopedHwnd = static_cast<HWND>(
            SDL_GetPointerProperty(ScopedProperties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (ScopedHwnd == nullptr)
        {
            return false;
        }
        DXGI_SWAP_CHAIN_DESC1 ScopedSwapDesc{};
        ScopedSwapDesc.Width = InDesc.size.width;
        ScopedSwapDesc.Height = InDesc.size.height;
        ScopedSwapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedSwapDesc.SampleDesc.Count = 1;
        ScopedSwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        ScopedSwapDesc.BufferCount = 2;
        ScopedSwapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> ScopedSwapchain1;
        if (FAILED(MFactory->CreateSwapChainForHwnd(MQueue.Get(), ScopedHwnd, &ScopedSwapDesc, nullptr, nullptr,
                                                    &ScopedSwapchain1)) ||
            FAILED(ScopedSwapchain1.As(&MSwapchain)))
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedHeap{};
        ScopedHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ScopedHeap.NumDescriptors = 2;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedHeap, IID_PPV_ARGS(&MRtvHeap))))
        {
            return false;
        }
        MRtvSize = MDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
        {
            if (FAILED(MSwapchain->GetBuffer(ScopedIndex, IID_PPV_ARGS(&MBackBuffers[ScopedIndex]))))
            {
                return false;
            }
            MDevice->CreateRenderTargetView(MBackBuffers[ScopedIndex].Get(), nullptr, ScopedRtv);
            ScopedRtv.ptr += MRtvSize;
        }
        return true;
    }

    bool CreatePostFxResources()
    {
        MPostFxPipeline.Reset();
        MPostFxRootSignature.Reset();
        MPostFxConstants.Reset();
        MSceneTexture.Reset();
        MSceneDepth.Reset();
        MSceneRtvHeap.Reset();
        MSceneSrvHeap.Reset();
        MSceneDsvHeap.Reset();
        MPostFxReady = false;
        if (MDevice == nullptr || MExtent.width == 0 || MExtent.height == 0)
        {
            return false;
        }
        D3D12_HEAP_PROPERTIES ScopedDefaultHeap{};
        ScopedDefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC ScopedDesc{};
        ScopedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ScopedDesc.Width = MExtent.width;
        ScopedDesc.Height = MExtent.height;
        ScopedDesc.DepthOrArraySize = 1;
        ScopedDesc.MipLevels = 1;
        ScopedDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedDesc.SampleDesc.Count = 1;
        ScopedDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const HRESULT ScopedSceneResult = MDevice->CreateCommittedResource(
            &ScopedDefaultHeap, D3D12_HEAP_FLAG_NONE, &ScopedDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
            IID_PPV_ARGS(&MSceneTexture));
        if (FAILED(ScopedSceneResult))
        {
            LogWarn("d3d12", "post-FX scene creation failed (hr={}, {}x{})",
                    static_cast<long>(ScopedSceneResult), MExtent.width, MExtent.height);
            DumpValidation();
            return false;
        }
        D3D12_RESOURCE_DESC ScopedDepthDesc = ScopedDesc;
        ScopedDepthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedDefaultHeap, D3D12_HEAP_FLAG_NONE, &ScopedDepthDesc,
                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
                                                    IID_PPV_ARGS(&MSceneDepth))))
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedRtvHeap{};
        ScopedRtvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ScopedRtvHeap.NumDescriptors = 1;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedRtvHeap, IID_PPV_ARGS(&MSceneRtvHeap))))
        {
            return false;
        }
        MDevice->CreateRenderTargetView(MSceneTexture.Get(), nullptr,
                                        MSceneRtvHeap->GetCPUDescriptorHandleForHeapStart());
        D3D12_DESCRIPTOR_HEAP_DESC ScopedDsvHeap{};
        ScopedDsvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ScopedDsvHeap.NumDescriptors = 1;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedDsvHeap, IID_PPV_ARGS(&MSceneDsvHeap))))
        {
            return false;
        }
        MDevice->CreateDepthStencilView(MSceneDepth.Get(), nullptr,
                                        MSceneDsvHeap->GetCPUDescriptorHandleForHeapStart());
        D3D12_DESCRIPTOR_HEAP_DESC ScopedSrvHeap{};
        ScopedSrvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedSrvHeap.NumDescriptors = 3;
        ScopedSrvHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedSrvHeap, IID_PPV_ARGS(&MSceneSrvHeap))))
        {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrv{};
        ScopedSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedCpu = MSceneSrvHeap->GetCPUDescriptorHandleForHeapStart();
        MDevice->CreateShaderResourceView(MSceneTexture.Get(), &ScopedSrv, ScopedCpu);
        ScopedCpu.ptr += MSrvSize;
        MDevice->CreateShaderResourceView(MSceneTexture.Get(), &ScopedSrv, ScopedCpu);

        D3D12_DESCRIPTOR_RANGE ScopedSceneRange{};
        ScopedSceneRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedSceneRange.NumDescriptors = 2;
        ScopedSceneRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedParams[2]{};
        ScopedParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedParams[0].DescriptorTable.NumDescriptorRanges = 1;
        ScopedParams[0].DescriptorTable.pDescriptorRanges = &ScopedSceneRange;
        ScopedParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        ScopedParams[1].Descriptor.ShaderRegister = 0;
        ScopedParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ScopedSampler{};
        ScopedSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.ShaderRegister = 0;
        ScopedSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedSampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC ScopedRootDesc{};
        ScopedRootDesc.NumParameters = 2;
        ScopedRootDesc.pParameters = ScopedParams;
        ScopedRootDesc.NumStaticSamplers = 1;
        ScopedRootDesc.pStaticSamplers = &ScopedSampler;
        ComPtr<ID3DBlob> ScopedRootBlob;
        ComPtr<ID3DBlob> ScopedErrors;
        if (FAILED(D3D12SerializeRootSignature(&ScopedRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ScopedRootBlob,
                                               &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedRootBlob->GetBufferPointer(), ScopedRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MPostFxRootSignature))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
        ScopedPso.pRootSignature = MPostFxRootSignature.Get();
        ScopedPso.VS = {Shaders::Dx::D3D12PostFxVsSm5.data(), Shaders::Dx::D3D12PostFxVsSm5.size()};
        ScopedPso.PS = {Shaders::Dx::D3D12PostFxPsSm5.data(), Shaders::Dx::D3D12PostFxPsSm5.size()};
        ScopedPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedPso.NumRenderTargets = 1;
        ScopedPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedPso.SampleDesc.Count = 1;
        ScopedPso.SampleMask = UINT_MAX;
        ScopedPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedPso.BlendState = {};
        ScopedPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        ScopedPso.DepthStencilState.DepthEnable = FALSE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedPso, IID_PPV_ARGS(&MPostFxPipeline))))
        {
            return false;
        }
        D3D12_HEAP_PROPERTIES ScopedUploadHeap{};
        ScopedUploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ScopedConstantsDesc{};
        ScopedConstantsDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ScopedConstantsDesc.Width = 512;
        ScopedConstantsDesc.Height = 1;
        ScopedConstantsDesc.DepthOrArraySize = 1;
        ScopedConstantsDesc.MipLevels = 1;
        ScopedConstantsDesc.SampleDesc.Count = 1;
        ScopedConstantsDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedUploadHeap, D3D12_HEAP_FLAG_NONE, &ScopedConstantsDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&MPostFxConstants))))
        {
            return false;
        }
        MPostFxReady = true;
        (void)CreateBloomResources();
        (void)CreateColorGradeResources();
        (void)CreateHudResources();
        (void)CreateUpscaleResources();
        (void)CreatePresentResources();
        if (!CreateAccessibilityResources())
        {
            LogWarn("d3d12", "accessibility resources unavailable");
        }
        return true;
    }

    bool CreateBloomResources()
    {
        MBloomPrefilterPipeline.Reset();
        MBloomBlurPipeline.Reset();
        MBloomRootSignature.Reset();
        MBloomConstants.Reset();
        MBloomHeap.Reset();
        for (ComPtr<ID3D12Resource>& ScopedTexture : MBloomTextures)
        {
            ScopedTexture.Reset();
        }
        MBloomReady = false;
        if (MDevice == nullptr || MExtent.width == 0 || MExtent.height == 0)
        {
            return false;
        }
        MBloomWidth = std::max(MExtent.width / 2u, 1u);
        MBloomHeight = std::max(MExtent.height / 2u, 1u);
        D3D12_HEAP_PROPERTIES ScopedDefaultHeap{};
        ScopedDefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC ScopedDesc{};
        ScopedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ScopedDesc.Width = MBloomWidth;
        ScopedDesc.Height = MBloomHeight;
        ScopedDesc.DepthOrArraySize = 1;
        ScopedDesc.MipLevels = 1;
        ScopedDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ScopedDesc.SampleDesc.Count = 1;
        ScopedDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        for (ComPtr<ID3D12Resource>& ScopedTexture : MBloomTextures)
        {
            if (FAILED(MDevice->CreateCommittedResource(&ScopedDefaultHeap, D3D12_HEAP_FLAG_NONE, &ScopedDesc,
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                        IID_PPV_ARGS(&ScopedTexture))))
            {
                return false;
            }
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedHeap{};
        ScopedHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedHeap.NumDescriptors = 5;
        ScopedHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedHeap, IID_PPV_ARGS(&MBloomHeap))))
        {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrv{};
        ScopedSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ScopedSrv.Texture2D.MipLevels = 1;
        D3D12_UNORDERED_ACCESS_VIEW_DESC ScopedUav{};
        ScopedUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        ScopedUav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        // 0 scene, 1 bloom A, 2 bloom B, 3 A UAV, 4 B UAV.
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedCpu = MBloomHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSceneSrv = ScopedSrv;
        ScopedSceneSrv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        MDevice->CreateShaderResourceView(MSceneTexture.Get(), &ScopedSceneSrv, ScopedCpu);
        ScopedCpu.ptr += MSrvSize;
        MDevice->CreateShaderResourceView(MBloomTextures[0].Get(), &ScopedSrv, ScopedCpu);
        ScopedCpu.ptr += MSrvSize;
        MDevice->CreateShaderResourceView(MBloomTextures[1].Get(), &ScopedSrv, ScopedCpu);
        ScopedCpu.ptr += MSrvSize;
        MDevice->CreateUnorderedAccessView(MBloomTextures[0].Get(), nullptr, &ScopedUav, ScopedCpu);
        ScopedCpu.ptr += MSrvSize;
        MDevice->CreateUnorderedAccessView(MBloomTextures[1].Get(), nullptr, &ScopedUav, ScopedCpu);

        D3D12_DESCRIPTOR_RANGE ScopedSrvRange{};
        ScopedSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedSrvRange.NumDescriptors = 1;
        ScopedSrvRange.BaseShaderRegister = 0;
        D3D12_DESCRIPTOR_RANGE ScopedUavRange{};
        ScopedUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ScopedUavRange.NumDescriptors = 1;
        ScopedUavRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedParams[3]{};
        ScopedParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedParams[0].DescriptorTable.NumDescriptorRanges = 1;
        ScopedParams[0].DescriptorTable.pDescriptorRanges = &ScopedSrvRange;
        ScopedParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        ScopedParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedParams[1].DescriptorTable.NumDescriptorRanges = 1;
        ScopedParams[1].DescriptorTable.pDescriptorRanges = &ScopedUavRange;
        ScopedParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        ScopedParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        ScopedParams[2].Descriptor.ShaderRegister = 0;
        ScopedParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_STATIC_SAMPLER_DESC ScopedSampler{};
        ScopedSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.ShaderRegister = 0;
        ScopedSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        ScopedSampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC ScopedRootDesc{};
        ScopedRootDesc.NumParameters = 3;
        ScopedRootDesc.pParameters = ScopedParams;
        ScopedRootDesc.NumStaticSamplers = 1;
        ScopedRootDesc.pStaticSamplers = &ScopedSampler;
        ComPtr<ID3DBlob> ScopedRootBlob;
        ComPtr<ID3DBlob> ScopedErrors;
        if (FAILED(D3D12SerializeRootSignature(&ScopedRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ScopedRootBlob,
                                               &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedRootBlob->GetBufferPointer(), ScopedRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MBloomRootSignature))))
        {
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC ScopedPrefilter{};
        ScopedPrefilter.pRootSignature = MBloomRootSignature.Get();
        ScopedPrefilter.CS = {Shaders::Dx::D3D12BloomPrefilterCsSm5.data(),
                              Shaders::Dx::D3D12BloomPrefilterCsSm5.size()};
        if (FAILED(MDevice->CreateComputePipelineState(&ScopedPrefilter, IID_PPV_ARGS(&MBloomPrefilterPipeline))))
        {
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC ScopedBlur{};
        ScopedBlur.pRootSignature = MBloomRootSignature.Get();
        ScopedBlur.CS = {Shaders::Dx::D3D12BloomBlurCsSm5.data(), Shaders::Dx::D3D12BloomBlurCsSm5.size()};
        if (FAILED(MDevice->CreateComputePipelineState(&ScopedBlur, IID_PPV_ARGS(&MBloomBlurPipeline))))
        {
            return false;
        }
        D3D12_HEAP_PROPERTIES ScopedUploadHeap{};
        ScopedUploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ScopedConstantsDesc{};
        ScopedConstantsDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ScopedConstantsDesc.Width = 3 * 512;
        ScopedConstantsDesc.Height = 1;
        ScopedConstantsDesc.DepthOrArraySize = 1;
        ScopedConstantsDesc.MipLevels = 1;
        ScopedConstantsDesc.SampleDesc.Count = 1;
        ScopedConstantsDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedUploadHeap, D3D12_HEAP_FLAG_NONE, &ScopedConstantsDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&MBloomConstants))))
        {
            return false;
        }
        MBloomReady = true;
        return true;
    }

    void RecordBloomChain(const PostFxConstants& InBlock)
    {
        if (!MBloomReady || MCommandList == nullptr)
        {
            return;
        }
        void* ScopedMapped = nullptr;
        if (FAILED(MBloomConstants->Map(0, nullptr, &ScopedMapped)))
        {
            return;
        }
        auto* ScopedBlocks = static_cast<PostFxConstants*>(ScopedMapped);
        ScopedBlocks[0] = InBlock;
        ScopedBlocks[0].grainAndFlash.Y = 0.0f;
        ScopedBlocks[1] = InBlock;
        ScopedBlocks[1].grainAndFlash.Y = 1.0f;
        ScopedBlocks[2] = InBlock;
        ScopedBlocks[2].grainAndFlash.Y = 0.0f;
        MBloomConstants->Unmap(0, nullptr);

        const auto ScopedTransition = [this](ID3D12Resource* InResource, D3D12_RESOURCE_STATES InBefore,
                                             D3D12_RESOURCE_STATES InAfter)
        {
            if (InResource == nullptr || InBefore == InAfter)
            {
                return;
            }
            D3D12_RESOURCE_BARRIER ScopedBarrier{};
            ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ScopedBarrier.Transition.pResource = InResource;
            ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ScopedBarrier.Transition.StateBefore = InBefore;
            ScopedBarrier.Transition.StateAfter = InAfter;
            MCommandList->ResourceBarrier(1, &ScopedBarrier);
        };
        const auto ScopedUavBarrier = [this](ID3D12Resource* InResource)
        {
            D3D12_RESOURCE_BARRIER ScopedBarrier{};
            ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            ScopedBarrier.UAV.pResource = InResource;
            MCommandList->ResourceBarrier(1, &ScopedBarrier);
        };
        const D3D12_GPU_DESCRIPTOR_HANDLE ScopedStart = MBloomHeap->GetGPUDescriptorHandleForHeapStart();
        const auto ScopedHandle = [&ScopedStart, this](std::uint32_t InIndex)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE ScopedHandle{ScopedStart};
            ScopedHandle.ptr += static_cast<UINT64>(InIndex) * MSrvSize;
            return ScopedHandle;
        };
        const std::uint32_t ScopedGroupsX = (MBloomWidth + 15u) / 16u;
        const std::uint32_t ScopedGroupsY = (MBloomHeight + 15u) / 16u;
        ID3D12DescriptorHeap* ScopedHeaps[] = {MBloomHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        MCommandList->SetComputeRootSignature(MBloomRootSignature.Get());

        // Scene -> bloom A.
        MCommandList->SetPipelineState(MBloomPrefilterPipeline.Get());
        MCommandList->SetComputeRootDescriptorTable(0, ScopedHandle(0));
        MCommandList->SetComputeRootDescriptorTable(1, ScopedHandle(3));
        MCommandList->SetComputeRootConstantBufferView(2, MBloomConstants->GetGPUVirtualAddress());
        MCommandList->Dispatch(ScopedGroupsX, ScopedGroupsY, 1);
        ScopedUavBarrier(MBloomTextures[0].Get());
        ScopedTransition(MBloomTextures[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // A -> B, horizontal.
        MCommandList->SetPipelineState(MBloomBlurPipeline.Get());
        MCommandList->SetComputeRootDescriptorTable(0, ScopedHandle(1));
        MCommandList->SetComputeRootDescriptorTable(1, ScopedHandle(4));
        MCommandList->SetComputeRootConstantBufferView(2, MBloomConstants->GetGPUVirtualAddress() + 512);
        MCommandList->Dispatch(ScopedGroupsX, ScopedGroupsY, 1);
        ScopedUavBarrier(MBloomTextures[1].Get());
        ScopedTransition(MBloomTextures[1].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ScopedTransition(MBloomTextures[0].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // B -> A, vertical.
        ScopedUavBarrier(MBloomTextures[0].Get());
        MCommandList->SetComputeRootDescriptorTable(0, ScopedHandle(2));
        MCommandList->SetComputeRootDescriptorTable(1, ScopedHandle(3));
        MCommandList->SetComputeRootConstantBufferView(2, MBloomConstants->GetGPUVirtualAddress() + 1024);
        MCommandList->Dispatch(ScopedGroupsX, ScopedGroupsY, 1);
        ScopedUavBarrier(MBloomTextures[0].Get());
        ScopedTransition(MBloomTextures[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ScopedTransition(MBloomTextures[1].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        MCommandList->SetDescriptorHeaps(0, nullptr);
    }

    void ReleaseBloomChain()
    {
        if (!MBloomReady || MCommandList == nullptr)
        {
            return;
        }
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = MBloomTextures[0].Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
    }

    bool CreateUpscaleResources()
    {
        MUpscaleRootSignature.Reset();
        MUpscalePipeline.Reset();
        MUpscaleSrvHeap.Reset();
        if (MDevice == nullptr)
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedHeap{};
        ScopedHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedHeap.NumDescriptors = 1;
        ScopedHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedHeap, IID_PPV_ARGS(&MUpscaleSrvHeap))))
        {
            return false;
        }
        D3D12_DESCRIPTOR_RANGE ScopedRange{};
        ScopedRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedRange.NumDescriptors = 1;
        ScopedRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedParams[2]{};
        ScopedParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedParams[0].DescriptorTable.NumDescriptorRanges = 1;
        ScopedParams[0].DescriptorTable.pDescriptorRanges = &ScopedRange;
        ScopedParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ScopedParams[1].Constants.ShaderRegister = 0;
        ScopedParams[1].Constants.Num32BitValues = 4;
        ScopedParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ScopedSampler{};
        ScopedSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.ShaderRegister = 0;
        ScopedSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedSampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC ScopedRootDesc{};
        ScopedRootDesc.NumParameters = 2;
        ScopedRootDesc.pParameters = ScopedParams;
        ScopedRootDesc.NumStaticSamplers = 1;
        ScopedRootDesc.pStaticSamplers = &ScopedSampler;
        ComPtr<ID3DBlob> ScopedRootBlob;
        ComPtr<ID3DBlob> ScopedErrors;
        if (FAILED(D3D12SerializeRootSignature(&ScopedRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ScopedRootBlob,
                                               &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedRootBlob->GetBufferPointer(), ScopedRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MUpscaleRootSignature))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
        ScopedPso.pRootSignature = MUpscaleRootSignature.Get();
        ScopedPso.VS = {Shaders::Dx::D3D12PostFxVsSm5.data(), Shaders::Dx::D3D12PostFxVsSm5.size()};
        ScopedPso.PS = {Shaders::Dx::D3D12UpscalePsSm5.data(), Shaders::Dx::D3D12UpscalePsSm5.size()};
        ScopedPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedPso.NumRenderTargets = 1;
        ScopedPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedPso.SampleDesc.Count = 1;
        ScopedPso.SampleMask = UINT_MAX;
        ScopedPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedPso.BlendState = {};
        ScopedPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        ScopedPso.DepthStencilState.DepthEnable = FALSE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedPso, IID_PPV_ARGS(&MUpscalePipeline))))
        {
            return false;
        }
        return true;
    }

    bool CreatePresentResources()
    {
        MPresentRootSignature.Reset();
        MPresentPipeline.Reset();
        MPresentSrvHeap.Reset();
        if (MDevice == nullptr)
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedHeap{};
        ScopedHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedHeap.NumDescriptors = 1;
        ScopedHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedHeap, IID_PPV_ARGS(&MPresentSrvHeap))))
        {
            return false;
        }
        D3D12_DESCRIPTOR_RANGE ScopedRange{};
        ScopedRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedRange.NumDescriptors = 1;
        ScopedRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedParam{};
        ScopedParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedParam.DescriptorTable.NumDescriptorRanges = 1;
        ScopedParam.DescriptorTable.pDescriptorRanges = &ScopedRange;
        ScopedParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ScopedSampler{};
        ScopedSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.ShaderRegister = 0;
        ScopedSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedSampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC ScopedRootDesc{};
        ScopedRootDesc.NumParameters = 1;
        ScopedRootDesc.pParameters = &ScopedParam;
        ScopedRootDesc.NumStaticSamplers = 1;
        ScopedRootDesc.pStaticSamplers = &ScopedSampler;
        ComPtr<ID3DBlob> ScopedRootBlob;
        ComPtr<ID3DBlob> ScopedErrors;
        if (FAILED(D3D12SerializeRootSignature(&ScopedRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ScopedRootBlob,
                                               &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedRootBlob->GetBufferPointer(), ScopedRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MPresentRootSignature))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
        ScopedPso.pRootSignature = MPresentRootSignature.Get();
        ScopedPso.VS = {Shaders::Dx::D3D12PostFxVsSm5.data(), Shaders::Dx::D3D12PostFxVsSm5.size()};
        ScopedPso.PS = {Shaders::Dx::D3D12BlitPsSm5.data(), Shaders::Dx::D3D12BlitPsSm5.size()};
        ScopedPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedPso.NumRenderTargets = 1;
        ScopedPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedPso.SampleDesc.Count = 1;
        ScopedPso.SampleMask = UINT_MAX;
        ScopedPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedPso.BlendState = {};
        ScopedPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        ScopedPso.DepthStencilState.DepthEnable = FALSE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedPso, IID_PPV_ARGS(&MPresentPipeline))))
        {
            return false;
        }
        return true;
    }

    void RecordPostFxComposite()
    {
        if (!MPostFxPending || !MPostFxReady || MCommandList == nullptr)
        {
            return;
        }
        MPostFxPending = false;
        PostFxConstants ScopedBlock = MPostFxGraphPending
                                           ? MPostFxGraphBlock
                                           : BuildPostFxConstants(MPostFxPendingSettings, MExtent, MStats.frameIndex);
        MPostFxGraphPending = false;
        const std::uint32_t ScopedFlags = static_cast<std::uint32_t>(ScopedBlock.flagsAndCrt.X);
        const bool ScopedBloomWanted = (ScopedFlags & PostFxBloom) != 0u && MBloomReady;
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = MSceneTexture.Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarrier.Transition.StateAfter = ScopedBloomWanted ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                                                                : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        if (ScopedBloomWanted)
        {
            RecordBloomChain(ScopedBlock);
            ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            MCommandList->ResourceBarrier(1, &ScopedBarrier);
        }
        else
        {
            ScopedBlock.flagsAndCrt.X =
                static_cast<float>(ScopedFlags & ~static_cast<std::uint32_t>(PostFxBloom));
        }
        // Binding 2 is the bloom image when the chain ran, otherwise the scene.
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSecond = MSceneSrvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSecond.ptr += MSrvSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSecondSrv{};
        ScopedSecondSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSecondSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSecondSrv.Texture2D.MipLevels = 1;
        if (ScopedBloomWanted)
        {
            ScopedSecondSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            MDevice->CreateShaderResourceView(MBloomTextures[0].Get(), &ScopedSecondSrv, ScopedSecond);
        }
        else
        {
            ScopedSecondSrv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            MDevice->CreateShaderResourceView(MSceneTexture.Get(), &ScopedSecondSrv, ScopedSecond);
        }
        void* ScopedMapped = nullptr;
        if (SUCCEEDED(MPostFxConstants->Map(0, nullptr, &ScopedMapped)))
        {
            std::memcpy(ScopedMapped, &ScopedBlock, sizeof(ScopedBlock));
            MPostFxConstants->Unmap(0, nullptr);
        }
        const std::uint32_t ScopedBackBuffer = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedBackRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedBackRtv.ptr += ScopedBackBuffer * MRtvSize;
        MCommandList->OMSetRenderTargets(1, &ScopedBackRtv, FALSE, nullptr);
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(MExtent.width),
                                            static_cast<float>(MExtent.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, static_cast<LONG>(MExtent.width), static_cast<LONG>(MExtent.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        MCommandList->SetPipelineState(MPostFxPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MPostFxRootSignature.Get());
        ID3D12DescriptorHeap* ScopedHeaps[] = {MSceneSrvHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        MCommandList->SetGraphicsRootDescriptorTable(0, MSceneSrvHeap->GetGPUDescriptorHandleForHeapStart());
        MCommandList->SetGraphicsRootConstantBufferView(1, MPostFxConstants->GetGPUVirtualAddress());
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MCommandList->DrawInstanced(3, 1, 0, 0);
        MCommandList->SetDescriptorHeaps(0, nullptr);
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        if (ScopedBloomWanted)
        {
            ReleaseBloomChain();
        }
        MCommandList->SetPipelineState(MPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        ++MStats.drawCalls;
    }

    bool CreateColorGradeResources()
    {
        MColorGradePipeline.Reset();
        MColorGradeRootSignature.Reset();
        MColorGradeSrvHeap.Reset();
        MColorGradeScratch.Reset();
        MColorGradeReady = false;
        if (MDevice == nullptr || MExtent.width == 0 || MExtent.height == 0)
        {
            return false;
        }
        D3D12_HEAP_PROPERTIES ScopedDefaultHeap{};
        ScopedDefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC ScopedDesc{};
        ScopedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ScopedDesc.Width = MExtent.width;
        ScopedDesc.Height = MExtent.height;
        ScopedDesc.DepthOrArraySize = 1;
        ScopedDesc.MipLevels = 1;
        ScopedDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedDesc.SampleDesc.Count = 1;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedDefaultHeap, D3D12_HEAP_FLAG_NONE, &ScopedDesc,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&MColorGradeScratch))))
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedHeap{};
        ScopedHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedHeap.NumDescriptors = 2;
        ScopedHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedHeap, IID_PPV_ARGS(&MColorGradeSrvHeap))))
        {
            return false;
        }
        D3D12_DESCRIPTOR_RANGE ScopedRange{};
        ScopedRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedRange.NumDescriptors = 2;
        ScopedRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedParams[2]{};
        ScopedParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedParams[0].DescriptorTable.NumDescriptorRanges = 1;
        ScopedParams[0].DescriptorTable.pDescriptorRanges = &ScopedRange;
        ScopedParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ScopedParams[1].Constants.ShaderRegister = 0;
        ScopedParams[1].Constants.Num32BitValues = 8;
        ScopedParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ScopedSampler{};
        ScopedSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.ShaderRegister = 0;
        ScopedSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedSampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC ScopedRootDesc{};
        ScopedRootDesc.NumParameters = 2;
        ScopedRootDesc.pParameters = ScopedParams;
        ScopedRootDesc.NumStaticSamplers = 1;
        ScopedRootDesc.pStaticSamplers = &ScopedSampler;
        ComPtr<ID3DBlob> ScopedRootBlob;
        ComPtr<ID3DBlob> ScopedErrors;
        if (FAILED(D3D12SerializeRootSignature(&ScopedRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ScopedRootBlob,
                                               &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedRootBlob->GetBufferPointer(), ScopedRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MColorGradeRootSignature))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
        ScopedPso.pRootSignature = MColorGradeRootSignature.Get();
        ScopedPso.VS = {Shaders::Dx::D3D12PostFxVsSm5.data(), Shaders::Dx::D3D12PostFxVsSm5.size()};
        ScopedPso.PS = {Shaders::Dx::D3D12ColorGradingPsSm5.data(), Shaders::Dx::D3D12ColorGradingPsSm5.size()};
        ScopedPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedPso.NumRenderTargets = 1;
        ScopedPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedPso.SampleDesc.Count = 1;
        ScopedPso.SampleMask = UINT_MAX;
        ScopedPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedPso.BlendState = {};
        ScopedPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        ScopedPso.DepthStencilState.DepthEnable = FALSE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedPso, IID_PPV_ARGS(&MColorGradePipeline))))
        {
            return false;
        }
        MColorGradeReady = true;
        return true;
    }

    void RecordColorGrade()
    {
        if (!MColorGradePending)
        {
            return;
        }
        MColorGradePending = false;
        if (!MColorGradeReady || MCommandList == nullptr || MColorGradeScratch == nullptr)
        {
            return;
        }
        const std::uint32_t ScopedLutId = static_cast<std::uint32_t>(MColorGradeBlock.params1.Y);
        const auto ScopedLut = MTextures.find(ScopedLutId);
        if (ScopedLut == MTextures.end())
        {
            return;
        }
        const std::uint32_t ScopedBackBuffer = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_RESOURCE_BARRIER ScopedBarrier{};
        ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarrier.Transition.pResource = MBackBuffers[ScopedBackBuffer].Get();
        ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        MCommandList->ResourceBarrier(1, &ScopedBarrier);
        MCommandList->CopyResource(MColorGradeScratch.Get(), MBackBuffers[ScopedBackBuffer].Get());

        D3D12_CPU_DESCRIPTOR_HANDLE ScopedCpu = MColorGradeSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSceneSrv{};
        ScopedSceneSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSceneSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSceneSrv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedSceneSrv.Texture2D.MipLevels = 1;
        MDevice->CreateShaderResourceView(MColorGradeScratch.Get(), &ScopedSceneSrv, ScopedCpu);
        ScopedCpu.ptr += MSrvSize;
        const D3D12_RESOURCE_DESC ScopedLutDesc = ScopedLut->second->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedLutSrv{};
        ScopedLutSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        ScopedLutSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedLutSrv.Format = ScopedLutDesc.Format;
        ScopedLutSrv.Texture2DArray.MipLevels = ScopedLutDesc.MipLevels;
        ScopedLutSrv.Texture2DArray.ArraySize = ScopedLutDesc.DepthOrArraySize;
        MDevice->CreateShaderResourceView(ScopedLut->second.Get(), &ScopedLutSrv, ScopedCpu);

        D3D12_RESOURCE_BARRIER ScopedBarriers[2]{};
        ScopedBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[0].Transition.pResource = MBackBuffers[ScopedBackBuffer].Get();
        ScopedBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[1].Transition.pResource = MColorGradeScratch.Get();
        ScopedBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        MCommandList->ResourceBarrier(2, ScopedBarriers);

        D3D12_CPU_DESCRIPTOR_HANDLE ScopedBackRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedBackRtv.ptr += ScopedBackBuffer * MRtvSize;
        MCommandList->OMSetRenderTargets(1, &ScopedBackRtv, FALSE, nullptr);
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(MExtent.width),
                                            static_cast<float>(MExtent.height), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, static_cast<LONG>(MExtent.width), static_cast<LONG>(MExtent.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        MCommandList->SetPipelineState(MColorGradePipeline.Get());
        MCommandList->SetGraphicsRootSignature(MColorGradeRootSignature.Get());
        ID3D12DescriptorHeap* ScopedHeaps[] = {MColorGradeSrvHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        MCommandList->SetGraphicsRootDescriptorTable(0, MColorGradeSrvHeap->GetGPUDescriptorHandleForHeapStart());
        MCommandList->SetGraphicsRoot32BitConstants(1, 8, &MColorGradeBlock, 0);
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MCommandList->DrawInstanced(3, 1, 0, 0);
        MCommandList->SetDescriptorHeaps(0, nullptr);
        ScopedBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[0].Transition.pResource = MColorGradeScratch.Get();
        ScopedBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        MCommandList->ResourceBarrier(1, &ScopedBarriers[0]);
        MCommandList->SetPipelineState(MPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        ++MStats.drawCalls;
    }

    bool CreateHudResources()
    {
        MHudRootSignature.Reset();
        MHudPipeline.Reset();
        MHudSrvHeap.Reset();
        MHudReady = false;
        if (MDevice == nullptr)
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedHeap{};
        ScopedHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedHeap.NumDescriptors = 1;
        ScopedHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedHeap, IID_PPV_ARGS(&MHudSrvHeap))))
        {
            return false;
        }
        D3D12_DESCRIPTOR_RANGE ScopedRange{};
        ScopedRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedRange.NumDescriptors = 1;
        ScopedRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedParams[2]{};
        ScopedParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedParams[0].DescriptorTable.NumDescriptorRanges = 1;
        ScopedParams[0].DescriptorTable.pDescriptorRanges = &ScopedRange;
        ScopedParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ScopedParams[1].Constants.ShaderRegister = 0;
        ScopedParams[1].Constants.Num32BitValues = 4;
        ScopedParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ScopedSampler{};
        ScopedSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.ShaderRegister = 0;
        ScopedSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedSampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC ScopedRootDesc{};
        ScopedRootDesc.NumParameters = 2;
        ScopedRootDesc.pParameters = ScopedParams;
        ScopedRootDesc.NumStaticSamplers = 1;
        ScopedRootDesc.pStaticSamplers = &ScopedSampler;
        ComPtr<ID3DBlob> ScopedRootBlob;
        ComPtr<ID3DBlob> ScopedErrors;
        if (FAILED(D3D12SerializeRootSignature(&ScopedRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ScopedRootBlob,
                                               &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedRootBlob->GetBufferPointer(), ScopedRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MHudRootSignature))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
        ScopedPso.pRootSignature = MHudRootSignature.Get();
        ScopedPso.VS = {Shaders::Dx::D3D12PostFxVsSm5.data(), Shaders::Dx::D3D12PostFxVsSm5.size()};
        ScopedPso.PS = {Shaders::Dx::D3D12HudCompositePsSm5.data(), Shaders::Dx::D3D12HudCompositePsSm5.size()};
        ScopedPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedPso.NumRenderTargets = 1;
        ScopedPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedPso.SampleDesc.Count = 1;
        ScopedPso.SampleMask = UINT_MAX;
        ScopedPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedPso.BlendState.RenderTarget[0].BlendEnable = TRUE;
        ScopedPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        ScopedPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        ScopedPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        ScopedPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        ScopedPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        ScopedPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        ScopedPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        ScopedPso.DepthStencilState.DepthEnable = FALSE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedPso, IID_PPV_ARGS(&MHudPipeline))))
        {
            return false;
        }
        MHudReady = true;
        return true;
    }

    [[nodiscard]] static bool AccessibilityWanted(const FramePacket& InPacket) noexcept
    {
        const std::uint32_t ScopedFlags = InPacket.accessibility.flags;
        return ((ScopedFlags & AccessibilityColorVision) != 0 && InPacket.accessibility.colorVision != 0) ||
               ((ScopedFlags & AccessibilityFlashGuard) != 0 && InPacket.accessibility.flashGain < 0.999f);
    }

    [[nodiscard]] ID3D12PipelineState* EnsureAccessibilityPipeline(DXGI_FORMAT InFormat)
    {
        const auto ScopedFound = MAccessibilityPipelines.find(InFormat);
        if (ScopedFound != MAccessibilityPipelines.end())
        {
            return ScopedFound->second.Get();
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
        ScopedPso.pRootSignature = MAccessibilityRootSignature.Get();
        ScopedPso.VS = {Shaders::Dx::D3D12PostFxVsSm5.data(), Shaders::Dx::D3D12PostFxVsSm5.size()};
        ScopedPso.PS = {Shaders::Dx::D3D12AccessibilityPsSm5.data(), Shaders::Dx::D3D12AccessibilityPsSm5.size()};
        ScopedPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedPso.NumRenderTargets = 1;
        ScopedPso.RTVFormats[0] = InFormat;
        ScopedPso.SampleDesc.Count = 1;
        ScopedPso.SampleMask = UINT_MAX;
        ScopedPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedPso.BlendState = {};
        ScopedPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        ScopedPso.DepthStencilState.DepthEnable = FALSE;
        ComPtr<ID3D12PipelineState> ScopedPipeline;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedPso, IID_PPV_ARGS(&ScopedPipeline))))
        {
            return nullptr;
        }
        return MAccessibilityPipelines.emplace(InFormat, std::move(ScopedPipeline)).first->second.Get();
    }

    void ApplyAccessibility(RenderTargetHandle InHandle, const FramePacket& InPacket, Extent2D InSize)
    {
        const auto ScopedTarget = MTargets.find(InHandle.id);
        const auto ScopedRtvHeap = MTargetHeaps.find(InHandle.id);
        if (MCommandList == nullptr || ScopedTarget == MTargets.end() || ScopedRtvHeap == MTargetHeaps.end() ||
            MAccessibilityHeap == nullptr || MAccessibilityRootSignature == nullptr ||
            MAccessibilityFlashPipeline == nullptr)
        {
            LogWarn("d3d12", "accessibility skipped: resources state");
            return;
        }
        const D3D12_RESOURCE_DESC ScopedTargetDesc = ScopedTarget->second->GetDesc();
        ID3D12PipelineState* ScopedPixelPipeline = EnsureAccessibilityPipeline(ScopedTargetDesc.Format);
        if (ScopedPixelPipeline == nullptr)
        {
            LogWarn("d3d12", "accessibility pipeline missing for format {}", static_cast<long>(ScopedTargetDesc.Format));
            return;
        }
        AccessibilityResources& ScopedResources = MAccessibilityResources[InHandle.id];
        if (!ScopedResources.Initialized)
        {
            D3D12_HEAP_PROPERTIES ScopedDefaultHeap{};
            ScopedDefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC ScopedSceneDesc = ScopedTargetDesc;
            ScopedSceneDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            if (FAILED(MDevice->CreateCommittedResource(&ScopedDefaultHeap, D3D12_HEAP_FLAG_NONE, &ScopedSceneDesc,
                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        IID_PPV_ARGS(&ScopedResources.Scene))))
            {
                return;
            }
            D3D12_RESOURCE_DESC ScopedHistoryDesc{};
            ScopedHistoryDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            ScopedHistoryDesc.Width = ScopedTargetDesc.Width;
            ScopedHistoryDesc.Height = ScopedTargetDesc.Height;
            ScopedHistoryDesc.DepthOrArraySize = 1;
            ScopedHistoryDesc.MipLevels = 1;
            ScopedHistoryDesc.Format = DXGI_FORMAT_R8_UNORM;
            ScopedHistoryDesc.SampleDesc.Count = 1;
            ScopedHistoryDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
            {
                if (FAILED(MDevice->CreateCommittedResource(&ScopedDefaultHeap, D3D12_HEAP_FLAG_NONE,
                                                            &ScopedHistoryDesc,
                                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                            IID_PPV_ARGS(&ScopedResources.History[ScopedIndex]))))
                {
                    return;
                }
                D3D12_CPU_DESCRIPTOR_HANDLE ScopedUavCpu = MAccessibilityHeap->GetCPUDescriptorHandleForHeapStart();
                ScopedUavCpu.ptr += static_cast<SIZE_T>(2 + ScopedIndex) * MSrvSize;
                D3D12_UNORDERED_ACCESS_VIEW_DESC ScopedUav{};
                ScopedUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                ScopedUav.Format = DXGI_FORMAT_R8_UNORM;
                MDevice->CreateUnorderedAccessView(ScopedResources.History[ScopedIndex].Get(), nullptr, &ScopedUav,
                                                   ScopedUavCpu);
                const float ScopedZero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                MCommandList->ClearUnorderedAccessViewFloat(
                    MAccessibilityHeap->GetGPUDescriptorHandleForHeapStart(), ScopedUavCpu,
                    ScopedResources.History[ScopedIndex].Get(), ScopedZero, 0, nullptr);
                ScopedResources.HistoryState[ScopedIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }
            ScopedResources.SceneState = D3D12_RESOURCE_STATE_COPY_DEST;
            ScopedResources.ReadIndex = 0;
            ScopedResources.Initialized = true;
        }
        const std::uint32_t ScopedRead = ScopedResources.ReadIndex;
        const std::uint32_t ScopedWrite = 1u - ScopedRead;

        MCommandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        const auto ScopedTransition = [this](ID3D12Resource* InResource, D3D12_RESOURCE_STATES InBefore,
                                             D3D12_RESOURCE_STATES InAfter)
        {
            if (InResource == nullptr || InBefore == InAfter)
            {
                return;
            }
            D3D12_RESOURCE_BARRIER ScopedBarrier{};
            ScopedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ScopedBarrier.Transition.pResource = InResource;
            ScopedBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ScopedBarrier.Transition.StateBefore = InBefore;
            ScopedBarrier.Transition.StateAfter = InAfter;
            MCommandList->ResourceBarrier(1, &ScopedBarrier);
        };
        // Copy the scene so the pass can sample it while writing the target.
        ScopedTransition(ScopedTarget->second.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
        ScopedTransition(ScopedResources.Scene.Get(), ScopedResources.SceneState, D3D12_RESOURCE_STATE_COPY_DEST);
        MCommandList->CopyResource(ScopedResources.Scene.Get(), ScopedTarget->second.Get());
        ScopedTransition(ScopedTarget->second.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
        ScopedTransition(ScopedResources.Scene.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ScopedTransition(ScopedResources.History[ScopedRead].Get(), ScopedResources.HistoryState[ScopedRead],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ScopedTransition(ScopedResources.History[ScopedWrite].Get(), ScopedResources.HistoryState[ScopedWrite],
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ScopedResources.SceneState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedResources.HistoryState[ScopedRead] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ScopedResources.HistoryState[ScopedWrite] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_CPU_DESCRIPTOR_HANDLE ScopedCpu = MAccessibilityHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSceneSrv{};
        ScopedSceneSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSceneSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSceneSrv.Format = ScopedTargetDesc.Format;
        ScopedSceneSrv.Texture2D.MipLevels = 1;
        MDevice->CreateShaderResourceView(ScopedResources.Scene.Get(), &ScopedSceneSrv, ScopedCpu);
        ScopedCpu.ptr += MSrvSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedHistorySrv{};
        ScopedHistorySrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedHistorySrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedHistorySrv.Format = DXGI_FORMAT_R8_UNORM;
        ScopedHistorySrv.Texture2D.MipLevels = 1;
        MDevice->CreateShaderResourceView(ScopedResources.History[ScopedRead].Get(), &ScopedHistorySrv, ScopedCpu);

        const D3D12_CPU_DESCRIPTOR_HANDLE ScopedTargetRtv =
            ScopedRtvHeap->second->GetCPUDescriptorHandleForHeapStart();
        MCommandList->OMSetRenderTargets(1, &ScopedTargetRtv, FALSE, nullptr);
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(std::max(InSize.width, 1u)),
                                            static_cast<float>(std::max(InSize.height, 1u)), 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, static_cast<LONG>(InSize.width), static_cast<LONG>(InSize.height)};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        std::array<float, 20> ScopedBlock{};
        const ColorMatrix3 ScopedMatrix = ColorVisionMatrix(
            static_cast<ColorVisionDeficiency>(std::min<std::uint32_t>(InPacket.accessibility.colorVision, 3u)));
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 9; ++ScopedIndex)
        {
            const std::uint32_t ScopedColumn = ScopedIndex / 3u;
            const std::uint32_t ScopedRow = ScopedIndex % 3u;
            ScopedBlock[ScopedColumn * 4u + ScopedRow] = ScopedMatrix.m[ScopedIndex];
        }
        const std::uint32_t ScopedFlags = InPacket.accessibility.flags;
        ScopedBlock[12] = InPacket.accessibility.flashGain;
        ScopedBlock[13] = InPacket.accessibility.colorVisionStrength;
        ScopedBlock[14] = (ScopedFlags & AccessibilityColorVision) != 0 ? 1.0f : 0.0f;
        ScopedBlock[16] = InPacket.accessibility.flashThreshold;
        ScopedBlock[17] = InPacket.accessibility.flashGain;
        ScopedBlock[18] = (ScopedFlags & AccessibilityFlashGuard) != 0 ? 1.0f : 0.0f;
        MCommandList->SetPipelineState(ScopedPixelPipeline);
        MCommandList->SetGraphicsRootSignature(MAccessibilityRootSignature.Get());
        ID3D12DescriptorHeap* ScopedHeaps[] = {MAccessibilityHeap.Get()};
        MCommandList->SetDescriptorHeaps(1, ScopedHeaps);
        MCommandList->SetGraphicsRootDescriptorTable(0, MAccessibilityHeap->GetGPUDescriptorHandleForHeapStart());
        MCommandList->SetGraphicsRoot32BitConstants(1, 20, ScopedBlock.data(), 0);
        MCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MCommandList->DrawInstanced(3, 1, 0, 0);

        // The luminance history is written by a compute pass.
        ScopedTransition(ScopedResources.Scene.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        MCommandList->SetPipelineState(MAccessibilityFlashPipeline.Get());
        MCommandList->SetComputeRootSignature(MAccessibilityFlashRootSignature.Get());
        MCommandList->SetComputeRootDescriptorTable(0, MAccessibilityHeap->GetGPUDescriptorHandleForHeapStart());
        D3D12_GPU_DESCRIPTOR_HANDLE ScopedWriteUav = MAccessibilityHeap->GetGPUDescriptorHandleForHeapStart();
        ScopedWriteUav.ptr += static_cast<SIZE_T>(2 + ScopedWrite) * MSrvSize;
        MCommandList->SetComputeRootDescriptorTable(1, ScopedWriteUav);
        MCommandList->Dispatch((std::max(InSize.width, 1u) + 7u) / 8u, (std::max(InSize.height, 1u) + 7u) / 8u, 1);
        MCommandList->SetDescriptorHeaps(0, nullptr);
        ScopedResources.SceneState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        ScopedResources.ReadIndex = ScopedWrite;
        MCommandList->SetPipelineState(MPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        ++MStats.drawCalls;
    }

    bool CreateAccessibilityResources()
    {
        MAccessibilityRootSignature.Reset();
        MAccessibilityFlashRootSignature.Reset();
        MAccessibilityFlashPipeline.Reset();
        MAccessibilityHeap.Reset();
        MAccessibilityPipelines.clear();
        if (MDevice == nullptr)
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedHeapDesc{};
        ScopedHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedHeapDesc.NumDescriptors = 4;
        ScopedHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedHeapDesc, IID_PPV_ARGS(&MAccessibilityHeap))))
        {
            return false;
        }
        D3D12_DESCRIPTOR_RANGE ScopedSceneRange{};
        ScopedSceneRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedSceneRange.NumDescriptors = 2;
        ScopedSceneRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedParams[2]{};
        ScopedParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedParams[0].DescriptorTable.NumDescriptorRanges = 1;
        ScopedParams[0].DescriptorTable.pDescriptorRanges = &ScopedSceneRange;
        ScopedParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ScopedParams[1].Constants.ShaderRegister = 0;
        ScopedParams[1].Constants.Num32BitValues = 20;
        ScopedParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ScopedSampler{};
        ScopedSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.ShaderRegister = 0;
        ScopedSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedSampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC ScopedRootDesc{};
        ScopedRootDesc.NumParameters = 2;
        ScopedRootDesc.pParameters = ScopedParams;
        ScopedRootDesc.NumStaticSamplers = 1;
        ScopedRootDesc.pStaticSamplers = &ScopedSampler;
        ComPtr<ID3DBlob> ScopedRootBlob;
        ComPtr<ID3DBlob> ScopedErrors;
        if (FAILED(D3D12SerializeRootSignature(&ScopedRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ScopedRootBlob,
                                               &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedRootBlob->GetBufferPointer(), ScopedRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MAccessibilityRootSignature))))
        {
            return false;
        }
        D3D12_DESCRIPTOR_RANGE ScopedFlashSrvRange{};
        ScopedFlashSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedFlashSrvRange.NumDescriptors = 1;
        ScopedFlashSrvRange.BaseShaderRegister = 0;
        D3D12_DESCRIPTOR_RANGE ScopedFlashUavRange{};
        ScopedFlashUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ScopedFlashUavRange.NumDescriptors = 1;
        ScopedFlashUavRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedFlashParams[2]{};
        ScopedFlashParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedFlashParams[0].DescriptorTable.NumDescriptorRanges = 1;
        ScopedFlashParams[0].DescriptorTable.pDescriptorRanges = &ScopedFlashSrvRange;
        ScopedFlashParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedFlashParams[1].DescriptorTable.NumDescriptorRanges = 1;
        ScopedFlashParams[1].DescriptorTable.pDescriptorRanges = &ScopedFlashUavRange;
        D3D12_STATIC_SAMPLER_DESC ScopedFlashSampler = ScopedSampler;
        ScopedFlashSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC ScopedFlashRootDesc{};
        ScopedFlashRootDesc.NumParameters = 2;
        ScopedFlashRootDesc.pParameters = ScopedFlashParams;
        ScopedFlashRootDesc.NumStaticSamplers = 1;
        ScopedFlashRootDesc.pStaticSamplers = &ScopedFlashSampler;
        ComPtr<ID3DBlob> ScopedFlashRootBlob;
        if (FAILED(D3D12SerializeRootSignature(&ScopedFlashRootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &ScopedFlashRootBlob, &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedFlashRootBlob->GetBufferPointer(),
                                                ScopedFlashRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MAccessibilityFlashRootSignature))))
        {
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC ScopedFlashPso{};
        ScopedFlashPso.pRootSignature = MAccessibilityFlashRootSignature.Get();
        ScopedFlashPso.CS = {Shaders::Dx::D3D12AccessibilityFlashCsSm5.data(),
                             Shaders::Dx::D3D12AccessibilityFlashCsSm5.size()};
        if (FAILED(MDevice->CreateComputePipelineState(&ScopedFlashPso, IID_PPV_ARGS(&MAccessibilityFlashPipeline))))
        {
            return false;
        }
        return true;
    }

    bool CreateDepthTarget()
    {
        MDepthHeap.Reset();
        MDepthTexture.Reset();
        if (MDevice == nullptr || MExtent.width == 0 || MExtent.height == 0)
        {
            return false;
        }
        D3D12_HEAP_PROPERTIES ScopedDepthHeap{};
        ScopedDepthHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC ScopedDepthDesc{};
        ScopedDepthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ScopedDepthDesc.Width = MExtent.width;
        ScopedDepthDesc.Height = MExtent.height;
        ScopedDepthDesc.DepthOrArraySize = 1;
        ScopedDepthDesc.MipLevels = 1;
        ScopedDepthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthDesc.SampleDesc.Count = 1;
        ScopedDepthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE ScopedDepthClear{};
        ScopedDepthClear.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthClear.DepthStencil.Depth = 1.0f;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedDepthHeap, D3D12_HEAP_FLAG_NONE, &ScopedDepthDesc,
                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, &ScopedDepthClear,
                                                    IID_PPV_ARGS(&MDepthTexture))))
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedDepthHeapDesc{};
        ScopedDepthHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ScopedDepthHeapDesc.NumDescriptors = 1;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedDepthHeapDesc, IID_PPV_ARGS(&MDepthHeap))))
        {
            return false;
        }
        MDevice->CreateDepthStencilView(MDepthTexture.Get(), nullptr,
                                        MDepthHeap->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    bool CreateCommands()
    {
        if (FAILED(MDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&MAllocator))) ||
            FAILED(MDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, MAllocator.Get(), nullptr,
                                              IID_PPV_ARGS(&MCommandList))) ||
            FAILED(MDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&MFence))))
        {
            return false;
        }
        MCommandList->Close();
        MFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        D3D12_DESCRIPTOR_HEAP_DESC ScopedTargetHeap{};
        ScopedTargetHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ScopedTargetHeap.NumDescriptors = kMaxTargets;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedTargetHeap, IID_PPV_ARGS(&MTargetHeap))))
        {
            return false;
        }
        MRtvSize = MDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_DESCRIPTOR_HEAP_DESC ScopedTextureHeap{};
        ScopedTextureHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedTextureHeap.NumDescriptors = kMaxTextures;
        ScopedTextureHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedTextureHeap, IID_PPV_ARGS(&MTextureHeap))))
        {
            return false;
        }
        MSrvSize = MDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        constexpr char ScopedShader[] = R"(
Texture2DArray SpriteTexture : register(t0);
SamplerState SpriteSampler : register(s0);
struct VSInput { float3 position : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR; float layer : TEXCOORD1; };
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR; float layer : TEXCOORD1; };
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.layer = input.layer;
    return output;
}
float4 PSMain(VSOutput input) : SV_Target
{
    return SpriteTexture.Sample(SpriteSampler, float3(input.uv, input.layer)) * input.color;
}
)";
        const D3D12_SHADER_BYTECODE ScopedVS{Shaders::Dx::D3D12SpriteVsSm5.data(),
                                             Shaders::Dx::D3D12SpriteVsSm5.size()};
        const D3D12_SHADER_BYTECODE ScopedPS{Shaders::Dx::D3D12SpritePsSm5.data(),
                                             Shaders::Dx::D3D12SpritePsSm5.size()};
        ComPtr<ID3DBlob> ScopedErrors;
        D3D12_DESCRIPTOR_RANGE ScopedRange{};
        ScopedRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedRange.NumDescriptors = 1;
        ScopedRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedRootParameter{};
        ScopedRootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedRootParameter.DescriptorTable.NumDescriptorRanges = 1;
        ScopedRootParameter.DescriptorTable.pDescriptorRanges = &ScopedRange;
        ScopedRootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ScopedSampler{};
        ScopedSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        ScopedSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC ScopedRootDesc{};
        ScopedRootDesc.NumParameters = 1;
        ScopedRootDesc.pParameters = &ScopedRootParameter;
        ScopedRootDesc.NumStaticSamplers = 1;
        ScopedRootDesc.pStaticSamplers = &ScopedSampler;
        ScopedRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> ScopedRootBlob;
        if (FAILED(D3D12SerializeRootSignature(&ScopedRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ScopedRootBlob,
                                               &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedRootBlob->GetBufferPointer(),
                                                ScopedRootBlob->GetBufferSize(), IID_PPV_ARGS(&MRootSignature))))
        {
            return false;
        }
        D3D12_ROOT_PARAMETER ScopedLightConstants{};
        ScopedLightConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ScopedLightConstants.Constants.ShaderRegister = 1;
        ScopedLightConstants.Constants.Num32BitValues = 32;
        ScopedLightConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER ScopedSpriteParams[2] = {ScopedRootParameter, ScopedLightConstants};
        D3D12_ROOT_SIGNATURE_DESC ScopedSpriteDesc = ScopedRootDesc;
        ScopedSpriteDesc.NumParameters = 2;
        ScopedSpriteDesc.pParameters = ScopedSpriteParams;
        ComPtr<ID3DBlob> ScopedSpriteBlob;
        if (FAILED(D3D12SerializeRootSignature(&ScopedSpriteDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &ScopedSpriteBlob, &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedSpriteBlob->GetBufferPointer(),
                                                ScopedSpriteBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MSpriteLightRootSignature))))
        {
            return false;
        }
        D3D12_ROOT_PARAMETER ScopedMaterialParams[2]{};
        ScopedMaterialParams[0] = ScopedRootParameter;
        ScopedMaterialParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ScopedMaterialParams[1].Constants.ShaderRegister = 0;
        ScopedMaterialParams[1].Constants.Num32BitValues = 28;
        ScopedMaterialParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC ScopedMaterialRootDesc = ScopedRootDesc;
        ScopedMaterialRootDesc.NumParameters = 2;
        ScopedMaterialRootDesc.pParameters = ScopedMaterialParams;
        ComPtr<ID3DBlob> ScopedMaterialRootBlob;
        if (FAILED(D3D12SerializeRootSignature(&ScopedMaterialRootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &ScopedMaterialRootBlob, &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedMaterialRootBlob->GetBufferPointer(),
                                                ScopedMaterialRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MMaterialRootSignature))))
        {
            return false;
        }
        const D3D12_INPUT_ELEMENT_DESC ScopedLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
        ScopedPso.pRootSignature = MSpriteLightRootSignature.Get();
        ScopedPso.VS = ScopedVS;
        ScopedPso.PS = ScopedPS;
        ScopedPso.InputLayout = {ScopedLayout, static_cast<UINT>(std::size(ScopedLayout))};
        ScopedPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedPso.NumRenderTargets = 1;
        ScopedPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedPso.SampleDesc.Count = 1;
        ScopedPso.SampleMask = UINT_MAX;
        ScopedPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedPso.RasterizerState.DepthClipEnable = TRUE;
        ScopedPso.BlendState.RenderTarget[0].BlendEnable = TRUE;
        ScopedPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        ScopedPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        ScopedPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        ScopedPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        ScopedPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        ScopedPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        ScopedPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        ScopedPso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        ScopedPso.DepthStencilState.DepthEnable = TRUE;
        ScopedPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        ScopedPso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedPso, IID_PPV_ARGS(&MPipeline))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedBasePso = ScopedPso;
        ScopedBasePso.PS = {Shaders::Dx::D3D12SpriteBasePsSm5.data(), Shaders::Dx::D3D12SpriteBasePsSm5.size()};
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedBasePso, IID_PPV_ARGS(&MBasePipeline))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedAdditivePso = ScopedPso;
        ScopedAdditivePso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        ScopedAdditivePso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedAdditivePso, IID_PPV_ARGS(&MAdditivePipeline))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedRevealPso = ScopedPso;
        ScopedRevealPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        ScopedRevealPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        ScopedRevealPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        ScopedRevealPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedRevealPso, IID_PPV_ARGS(&MRevealPipeline))))
        {
            return false;
        }
        constexpr char ScopedMotionShader[] = R"(
struct VSInput { float3 position : POSITION; float2 delta : TEXCOORD0; };
struct VSOutput { float4 position : SV_Position; float2 delta : TEXCOORD0; };
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.delta = input.delta;
    return output;
}
float4 PSMain(VSOutput input) : SV_Target { return float4(input.delta * 0.5, 0.0, 1.0); }
)";
        const D3D12_SHADER_BYTECODE ScopedMotionVS{Shaders::Dx::D3D12MotionVsSm5.data(),
                                                    Shaders::Dx::D3D12MotionVsSm5.size()};
        const D3D12_SHADER_BYTECODE ScopedMotionPS{Shaders::Dx::D3D12MotionPsSm5.data(),
                                                    Shaders::Dx::D3D12MotionPsSm5.size()};
        D3D12_ROOT_SIGNATURE_DESC ScopedMotionRootDesc{};
        ScopedMotionRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> ScopedMotionRootBlob;
        if (FAILED(D3D12SerializeRootSignature(&ScopedMotionRootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &ScopedMotionRootBlob, &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedMotionRootBlob->GetBufferPointer(),
                                                ScopedMotionRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MMotionRootSignature))))
        {
            return false;
        }
        const D3D12_INPUT_ELEMENT_DESC ScopedMotionLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedMotionPso{};
        ScopedMotionPso.pRootSignature = MMotionRootSignature.Get();
        ScopedMotionPso.VS = ScopedMotionVS;
        ScopedMotionPso.PS = ScopedMotionPS;
        ScopedMotionPso.InputLayout = {ScopedMotionLayout, static_cast<UINT>(std::size(ScopedMotionLayout))};
        ScopedMotionPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedMotionPso.NumRenderTargets = 1;
        ScopedMotionPso.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
        ScopedMotionPso.SampleDesc.Count = 1;
        ScopedMotionPso.SampleMask = UINT_MAX;
        ScopedMotionPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedMotionPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedMotionPso.RasterizerState.DepthClipEnable = TRUE;
        ScopedMotionPso.BlendState = {};
        ScopedMotionPso.DepthStencilState.DepthEnable = FALSE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedMotionPso, IID_PPV_ARGS(&MMotionPipeline))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedSpriteMotionPso = ScopedPso;
        ScopedSpriteMotionPso.DSVFormat = DXGI_FORMAT_UNKNOWN;
        ScopedSpriteMotionPso.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
        ScopedSpriteMotionPso.DepthStencilState.DepthEnable = FALSE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedSpriteMotionPso, IID_PPV_ARGS(&MMotionPipeline))))
        {
            return false;
        }
        constexpr char ScopedTemporalShader[] = R"(
Texture2D CurrentColor : register(t0);
Texture2D HistoryColor : register(t1);
Texture2D MotionVectors : register(t2);
Texture2D CurrentDepth : register(t3);
Texture2D HistoryDepth : register(t4);
SamplerState TemporalSampler : register(s0);
cbuffer TemporalParams : register(b0) { float HistoryWeight; float ResetHistory; float MotionScale; float DepthThreshold; };
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
float4 PSMain(VSOutput input) : SV_Target
{
    float4 current = CurrentColor.Sample(TemporalSampler, input.uv);
    float2 motion = MotionVectors.Sample(TemporalSampler, input.uv).rg * MotionScale;
    float4 history = HistoryColor.Sample(TemporalSampler, input.uv + motion);
    float currentDepth = CurrentDepth.Sample(TemporalSampler, input.uv).r;
    float historyDepth = HistoryDepth.Sample(TemporalSampler, input.uv + motion).r;
    float weight = ResetHistory > 0.5 ? 1.0 : HistoryWeight;
    if (abs(currentDepth - historyDepth) > DepthThreshold)
    {
        weight = 1.0;
    }
    return lerp(history, current, weight);
}
)";
        const D3D12_SHADER_BYTECODE ScopedTemporalVS{Shaders::Dx::D3D12TemporalVsSm5.data(),
                                                      Shaders::Dx::D3D12TemporalVsSm5.size()};
        const D3D12_SHADER_BYTECODE ScopedTemporalPS{Shaders::Dx::D3D12TemporalPsSm5.data(),
                                                      Shaders::Dx::D3D12TemporalPsSm5.size()};
        D3D12_DESCRIPTOR_RANGE ScopedTemporalRange{};
        ScopedTemporalRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ScopedTemporalRange.NumDescriptors = 5;
        ScopedTemporalRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER ScopedTemporalParams[2]{};
        ScopedTemporalParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ScopedTemporalParams[0].DescriptorTable.NumDescriptorRanges = 1;
        ScopedTemporalParams[0].DescriptorTable.pDescriptorRanges = &ScopedTemporalRange;
        ScopedTemporalParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ScopedTemporalParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ScopedTemporalParams[1].Constants.ShaderRegister = 0;
        ScopedTemporalParams[1].Constants.Num32BitValues = 4;
        ScopedTemporalParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ScopedTemporalSampler{};
        ScopedTemporalSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedTemporalSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedTemporalSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedTemporalSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ScopedTemporalSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        ScopedTemporalSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC ScopedTemporalRootDesc{};
        ScopedTemporalRootDesc.NumParameters = 2;
        ScopedTemporalRootDesc.pParameters = ScopedTemporalParams;
        ScopedTemporalRootDesc.NumStaticSamplers = 1;
        ScopedTemporalRootDesc.pStaticSamplers = &ScopedTemporalSampler;
        ScopedTemporalRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> ScopedTemporalRootBlob;
        if (FAILED(D3D12SerializeRootSignature(&ScopedTemporalRootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &ScopedTemporalRootBlob, &ScopedErrors)) ||
            FAILED(MDevice->CreateRootSignature(0, ScopedTemporalRootBlob->GetBufferPointer(),
                                                ScopedTemporalRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&MTemporalRootSignature))))
        {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedTemporalPso{};
        ScopedTemporalPso.pRootSignature = MTemporalRootSignature.Get();
        ScopedTemporalPso.VS = ScopedTemporalVS;
        ScopedTemporalPso.PS = ScopedTemporalPS;
        ScopedTemporalPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedTemporalPso.NumRenderTargets = 1;
        ScopedTemporalPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedTemporalPso.SampleDesc.Count = 1;
        ScopedTemporalPso.SampleMask = UINT_MAX;
        ScopedTemporalPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedTemporalPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedTemporalPso.BlendState = {};
        ScopedTemporalPso.DepthStencilState.DepthEnable = FALSE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedTemporalPso, IID_PPV_ARGS(&MTemporalPipeline))))
        {
            return false;
        }
        constexpr char ScopedSharpenShader[] = R"(
Texture2D SourceColor : register(t0);
SamplerState SharpenSampler : register(s0);
cbuffer SharpenParams : register(b0) { float Sharpness; float TexelX; float TexelY; float Padding; };
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
float4 PSMain(VSOutput input) : SV_Target
{
    float4 center = SourceColor.Sample(SharpenSampler, input.uv);
    float4 left = SourceColor.Sample(SharpenSampler, input.uv + float2(-TexelX, 0.0));
    float4 right = SourceColor.Sample(SharpenSampler, input.uv + float2(TexelX, 0.0));
    float4 up = SourceColor.Sample(SharpenSampler, input.uv + float2(0.0, -TexelY));
    float4 down = SourceColor.Sample(SharpenSampler, input.uv + float2(0.0, TexelY));
    float4 sharpened = center + (center * 4.0 - left - right - up - down) * Sharpness;
    return float4(sharpened.rgb, center.a);
}
)";
        const D3D12_SHADER_BYTECODE ScopedSharpenVS{Shaders::Dx::D3D12SharpenVsSm5.data(),
                                                     Shaders::Dx::D3D12SharpenVsSm5.size()};
        const D3D12_SHADER_BYTECODE ScopedSharpenPS{Shaders::Dx::D3D12SharpenPsSm5.data(),
                                                     Shaders::Dx::D3D12SharpenPsSm5.size()};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedSharpenPso{};
        ScopedSharpenPso.pRootSignature = MTemporalRootSignature.Get();
        ScopedSharpenPso.VS = ScopedSharpenVS;
        ScopedSharpenPso.PS = ScopedSharpenPS;
        ScopedSharpenPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ScopedSharpenPso.NumRenderTargets = 1;
        ScopedSharpenPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        ScopedSharpenPso.SampleDesc.Count = 1;
        ScopedSharpenPso.SampleMask = UINT_MAX;
        ScopedSharpenPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ScopedSharpenPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ScopedSharpenPso.BlendState = {};
        ScopedSharpenPso.DepthStencilState.DepthEnable = FALSE;
        if (FAILED(MDevice->CreateGraphicsPipelineState(&ScopedSharpenPso, IID_PPV_ARGS(&MSharpenPipeline))))
        {
            return false;
        }
        D3D12_HEAP_PROPERTIES ScopedUploadHeap{};
        ScopedUploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ScopedVertexDesc{};
        ScopedVertexDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ScopedVertexDesc.Width = static_cast<UINT64>(sizeof(SmokeVertex) * MVertexCapacity);
        ScopedVertexDesc.Height = 1;
        ScopedVertexDesc.DepthOrArraySize = 1;
        ScopedVertexDesc.MipLevels = 1;
        ScopedVertexDesc.SampleDesc.Count = 1;
        ScopedVertexDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(MDevice->CreateCommittedResource(&ScopedUploadHeap, D3D12_HEAP_FLAG_NONE, &ScopedVertexDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&MVertexBuffer))))
        {
            return false;
        }
        if (FAILED(MDevice->CreateCommittedResource(&ScopedUploadHeap, D3D12_HEAP_FLAG_NONE, &ScopedVertexDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&MMotionVertexBuffer))))
        {
            return false;
        }
        if (!CreateDepthTarget())
        {
            return false;
        }
        return MFenceEvent != nullptr;
    }

    DeviceInfo MInfo{};
    FrameStats MStats{};
    ComPtr<IDXGIFactory4> MFactory;
    ComPtr<ID3D12Device> MDevice;
    ComPtr<IDXGIAdapter1> MPhysicalAdapter;
    ComPtr<ID3D12CommandQueue> MQueue;
    ComPtr<IDXGISwapChain3> MSwapchain;
    ComPtr<ID3D12DescriptorHeap> MRtvHeap;
    std::array<ComPtr<ID3D12Resource>, 2> MBackBuffers;
    ComPtr<ID3D12CommandAllocator> MAllocator;
    ComPtr<ID3D12GraphicsCommandList> MCommandList;
    ComPtr<ID3D12Fence> MFence;
    ComPtr<ID3D12RootSignature> MRootSignature;
    ComPtr<ID3D12RootSignature> MSpriteLightRootSignature;
    ComPtr<ID3D12PipelineState> MPipeline;
    ComPtr<ID3D12PipelineState> MBasePipeline;
    ComPtr<ID3D12RootSignature> MMaterialRootSignature;
    Extent2D MExtent{320, 240};
    bool MVsync = true;
    ComPtr<ID3D12RootSignature> MPostFxRootSignature;
    ComPtr<ID3D12PipelineState> MPostFxPipeline;
    ComPtr<ID3D12Resource> MPostFxConstants;
    ComPtr<ID3D12Resource> MSceneTexture;
    ComPtr<ID3D12Resource> MSceneDepth;
    ComPtr<ID3D12DescriptorHeap> MSceneRtvHeap;
    ComPtr<ID3D12DescriptorHeap> MSceneSrvHeap;
    ComPtr<ID3D12DescriptorHeap> MSceneDsvHeap;
    PostFxSettings MPostFxPendingSettings{};
    PostFxConstants MPostFxGraphBlock{};
    bool MPostFxReady = false;
    ComPtr<ID3D12RootSignature> MHudRootSignature;
    ComPtr<ID3D12PipelineState> MHudPipeline;
    ComPtr<ID3D12DescriptorHeap> MHudSrvHeap;
    bool MHudReady = false;
    ComPtr<ID3D12RootSignature> MPresentRootSignature;
    ComPtr<ID3D12PipelineState> MPresentPipeline;
    ComPtr<ID3D12DescriptorHeap> MPresentSrvHeap;
    struct AccessibilityResources
    {
        ComPtr<ID3D12Resource> Scene;
        std::array<ComPtr<ID3D12Resource>, 2> History;
        D3D12_RESOURCE_STATES SceneState = D3D12_RESOURCE_STATE_COPY_DEST;
        std::array<D3D12_RESOURCE_STATES, 2> HistoryState = {D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
        std::uint32_t ReadIndex = 0;
        bool Initialized = false;
    };
    std::unordered_map<std::uint32_t, AccessibilityResources> MAccessibilityResources;
    std::unordered_map<DXGI_FORMAT, ComPtr<ID3D12PipelineState>> MAccessibilityPipelines;
    ComPtr<ID3D12RootSignature> MAccessibilityRootSignature;
    ComPtr<ID3D12RootSignature> MAccessibilityFlashRootSignature;
    ComPtr<ID3D12PipelineState> MAccessibilityFlashPipeline;
    ComPtr<ID3D12DescriptorHeap> MAccessibilityHeap;
    ComPtr<ID3D12RootSignature> MUpscaleRootSignature;
    ComPtr<ID3D12PipelineState> MUpscalePipeline;
    ComPtr<ID3D12DescriptorHeap> MUpscaleSrvHeap;
    bool MPostFxPending = false;
    bool MPostFxGraphPending = false;
    ComPtr<ID3D12Resource> MColorGradeScratch;
    ComPtr<ID3D12DescriptorHeap> MColorGradeSrvHeap;
    ComPtr<ID3D12RootSignature> MColorGradeRootSignature;
    ComPtr<ID3D12PipelineState> MColorGradePipeline;
    ColorGradingConstants MColorGradeBlock{};
    bool MColorGradeReady = false;
    bool MColorGradePending = false;
    std::array<ComPtr<ID3D12Resource>, 2> MBloomTextures;
    ComPtr<ID3D12DescriptorHeap> MBloomHeap;
    ComPtr<ID3D12RootSignature> MBloomRootSignature;
    ComPtr<ID3D12PipelineState> MBloomPrefilterPipeline;
    ComPtr<ID3D12PipelineState> MBloomBlurPipeline;
    ComPtr<ID3D12Resource> MBloomConstants;
    std::uint32_t MBloomWidth = 0;
    std::uint32_t MBloomHeight = 0;
    bool MBloomReady = false;
    ComPtr<ID3D12PipelineLibrary> MPipelineLibrary;
    bool MPipelineLibraryLoaded = false;
    std::unordered_map<std::uint32_t, ComPtr<ID3D12PipelineState>> MMaterialPipelines;
    std::unordered_map<std::uint32_t, ShaderPipelineDomain> MMaterialPipelineDomains;
    std::uint32_t MNextMaterialPipeline = 1;
    ComPtr<ID3D12RootSignature> MMotionRootSignature;
    ComPtr<ID3D12RootSignature> MTemporalRootSignature;
    ComPtr<ID3D12PipelineState> MTemporalPipeline;
    ComPtr<ID3D12PipelineState> MSharpenPipeline;
    ComPtr<ID3D12PipelineState> MAdditivePipeline;
    ComPtr<ID3D12PipelineState> MRevealPipeline;
    ComPtr<ID3D12PipelineState> MMotionPipeline;
    ComPtr<ID3D12Resource> MVertexBuffer;
    ComPtr<ID3D12Resource> MMotionVertexBuffer;
    ComPtr<ID3D12Resource> MDepthTexture;
    ComPtr<ID3D12DescriptorHeap> MDepthHeap;
    std::uint32_t MVertexCapacity = 6 * 4096;
    HANDLE MFenceEvent = nullptr;
    std::uint64_t MFenceValue = 0;
    std::uint32_t MRtvSize = 0;
    static constexpr std::uint32_t kMaxTargets = 16;
    ComPtr<ID3D12DescriptorHeap> MTargetHeap;
    std::unordered_map<std::uint32_t, ComPtr<ID3D12Resource>> MTargets;
    std::unordered_map<std::uint32_t, ComPtr<ID3D12DescriptorHeap>> MTargetHeaps;
    std::unordered_map<std::uint32_t, std::uint32_t> MTargetSrvIndex;
    std::unordered_map<std::uint32_t, ComPtr<ID3D12Resource>> MTargetDepthResources;
    std::unordered_map<std::uint32_t, ComPtr<ID3D12DescriptorHeap>> MTargetDepthHeaps;
    std::unordered_map<std::uint32_t, Fby::Rhi::Extent2D> MTargetSizes;
    std::unordered_map<std::uint32_t, RenderBlendMode> MTargetBlend;
    std::uint32_t MRtvNext = 0;
    std::uint32_t MNextTargetId = 1;
    bool MInFrame = false;
    RenderTargetHandle MHudTarget;
    bool MUIVisible = true;
    bool MHudUpscaleNearest = false;
    std::vector<MotionVectorItem> MMotionItems;
    static constexpr std::uint32_t kMaxTextures = 64;
    ComPtr<ID3D12DescriptorHeap> MTextureHeap;
    struct MeshRecord
    {
        std::vector<MeshVertex> Vertices;
        std::vector<std::uint16_t> Indices;
    };
    std::unordered_map<std::uint32_t, MeshRecord> MMeshes;
    std::uint32_t MNextMeshId = 1;
    std::unordered_map<std::uint32_t, ComPtr<ID3D12Resource>> MTextures;
    std::unordered_map<std::uint32_t, std::uint32_t> MTextureSrvIndex;
    std::uint32_t MSrvSize = 0;
    std::uint32_t MSrvNext = 0;
    std::vector<std::uint32_t> MFreeSrvIndices;
    static constexpr std::uint32_t kInvalidSrvSlot = 0xFFFFFFFFu;
    std::uint32_t MNextTextureId = 1;
    bool MDebugLayer = false;
    ComPtr<ID3D12InfoQueue> MInfoQueue;
};
}

std::unique_ptr<RHI> CreateD3D12RHI()
{
    return std::make_unique<D3D12RHI>();
}
}
#else
namespace Fby::Rhi
{
std::unique_ptr<RHI> CreateD3D12RHI()
{
    return nullptr;
}
}
#endif
