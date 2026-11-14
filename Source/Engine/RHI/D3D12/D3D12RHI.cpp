// FemboyEngine2D - minimal D3D12 RHI backend (device/swapchain/present).
#include "Engine/RHI/RHI.hpp"
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

class D3D12RHI final : public RHI
{
public:
    bool Initialize(const DeviceDesc& InDesc) override
    {
        (void)InDesc;
        if (!CreateFactoryAndDevice())
        {
            return false;
        }
        if (!CreateSwapchain(InDesc))
        {
            return false;
        }
        return CreateCommands();
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

    void Resize(Extent2D InSize) override
    {
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
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, 320, 240};
        MCommandList->RSSetViewports(1, &ScopedViewport);
        MCommandList->RSSetScissorRects(1, &ScopedScissor);
        MCommandList->SetPipelineState(MPipeline.Get());
        MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        MInFrame = true;
        return true;
    }

    void Submit(const FramePacket& InPacket) override
    {
        if (MCommandList == nullptr || InPacket.items.empty() || MVertexCapacity < InPacket.items.size() * 6)
        {
            return;
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
            const SmokeVertex ScopedCorners[6] = {
                {{ScopedX - ScopedHalfX, ScopedY - ScopedHalfY, 0.5f}, {ScopedItem.uvMin.X, ScopedItem.uvMax.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer)},
                {{ScopedX + ScopedHalfX, ScopedY - ScopedHalfY, 0.5f}, {ScopedItem.uvMax.X, ScopedItem.uvMax.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer)},
                {{ScopedX + ScopedHalfX, ScopedY + ScopedHalfY, 0.5f}, {ScopedItem.uvMax.X, ScopedItem.uvMin.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer)},
                {{ScopedX - ScopedHalfX, ScopedY - ScopedHalfY, 0.5f}, {ScopedItem.uvMin.X, ScopedItem.uvMax.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer)},
                {{ScopedX + ScopedHalfX, ScopedY + ScopedHalfY, 0.5f}, {ScopedItem.uvMax.X, ScopedItem.uvMin.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer)},
                {{ScopedX - ScopedHalfX, ScopedY + ScopedHalfY, 0.5f}, {ScopedItem.uvMin.X, ScopedItem.uvMin.Y},
                 {ScopedR, ScopedG, ScopedB, ScopedA}, static_cast<float>(ScopedItem.layer)},
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
                    constexpr std::array<std::uint32_t, 20> ScopedMaterialConstants{};
                    MCommandList->SetGraphicsRootSignature(MMaterialRootSignature.Get());
                    MCommandList->SetGraphicsRoot32BitConstants(1, 20, ScopedMaterialConstants.data(), 0);
                    MCommandList->SetPipelineState(ScopedMaterial->second.Get());
                }
                else
                {
                    MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
                    MCommandList->SetPipelineState(MPipeline.Get());
                }
                ScopedDraw(ScopedBatch.firstItem * 6, ScopedBatch.itemCount * 6, ScopedTexture);
            }
            MCommandList->SetPipelineState(MPipeline.Get());
            MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
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
                    constexpr std::array<std::uint32_t, 20> ScopedMaterialConstants{};
                    MCommandList->SetGraphicsRootSignature(MMaterialRootSignature.Get());
                    MCommandList->SetGraphicsRoot32BitConstants(1, 20, ScopedMaterialConstants.data(), 0);
                    MCommandList->SetPipelineState(ScopedMeshMaterial->second.Get());
                }
                else
                {
                    MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
                    MCommandList->SetPipelineState(MPipeline.Get());
                }
                MCommandList->DrawInstanced(static_cast<UINT>(ScopedMeshCount - ScopedStart), 1,
                                            static_cast<UINT>(ScopedStart), 0);
            }
            MVertexBuffer->Unmap(0, nullptr);
            MCommandList->SetPipelineState(MPipeline.Get());
            MCommandList->SetGraphicsRootSignature(MRootSignature.Get());
        }
    }

    void EndFrame() override
    {
        MInFrame = false;
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
        MSwapchain->Present(0, 0);
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

    TextureArrayHandle CreateTextureArray(const TextureArrayDesc& InDesc,
                                          std::span<const std::byte> InPixels) override
    {
        const std::uint32_t ScopedWidth = InDesc.size.width;
        const std::uint32_t ScopedHeight = InDesc.size.height;
        if (ScopedWidth == 0 || ScopedHeight == 0 || InDesc.layers == 0 || MSrvNext >= kMaxTextures ||
            InPixels.size() < static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u * InDesc.layers)
        {
            return {};
        }
        D3D12_RESOURCE_DESC ScopedDesc{};
        ScopedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ScopedDesc.Width = ScopedWidth;
        ScopedDesc.Height = ScopedHeight;
        ScopedDesc.DepthOrArraySize = static_cast<UINT16>(InDesc.layers);
        ScopedDesc.MipLevels = 1;
        ScopedDesc.Format = InDesc.srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
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
        const std::uint64_t ScopedLayerBytes = static_cast<std::uint64_t>(ScopedWidth) * ScopedHeight * 4u;
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
        std::memcpy(ScopedMapped, InPixels.data(), static_cast<std::size_t>(ScopedLayerBytes * InDesc.layers));
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
            D3D12_TEXTURE_COPY_LOCATION ScopedDestination{};
            ScopedDestination.pResource = ScopedTexture.Get();
            ScopedDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            ScopedDestination.SubresourceIndex = ScopedLayer;
            D3D12_TEXTURE_COPY_LOCATION ScopedSource{};
            ScopedSource.pResource = ScopedUpload.Get();
            ScopedSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            ScopedSource.PlacedFootprint.Footprint.Format = ScopedDesc.Format;
            ScopedSource.PlacedFootprint.Footprint.Width = ScopedWidth;
            ScopedSource.PlacedFootprint.Footprint.Height = ScopedHeight;
            ScopedSource.PlacedFootprint.Footprint.Depth = 1;
            ScopedSource.PlacedFootprint.Footprint.RowPitch = ScopedWidth * 4u;
            ScopedSource.PlacedFootprint.Offset = ScopedLayerBytes * ScopedLayer;
            ScopedCommands->CopyTextureRegion(&ScopedDestination, 0, 0, 0, &ScopedSource, nullptr);
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
        ScopedSrv.Texture2DArray.MipLevels = 1;
        ScopedSrv.Texture2DArray.FirstArraySlice = 0;
        ScopedSrv.Texture2DArray.ArraySize = InDesc.layers;
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSrvHandle = MTextureHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSrvHandle.ptr += MSrvNext * MSrvSize;
        MDevice->CreateShaderResourceView(ScopedTexture.Get(), &ScopedSrv, ScopedSrvHandle);
        const std::uint32_t ScopedId = MNextTextureId++;
        MTextures.emplace(ScopedId, ScopedTexture);
        MTextureSrvIndex[ScopedId] = MSrvNext;
        ++MSrvNext;
        TextureArrayHandle ScopedTextureHandle;
        ScopedTextureHandle.id = ScopedId;
        return ScopedTextureHandle;
    }

    void DestroyTextureArray(TextureArrayHandle InHandle) override
    {
        MTextureSrvIndex.erase(InHandle.id);
        MTextures.erase(InHandle.id);
    }

    RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& InDesc) override
    {
        if (InDesc.size.width == 0 || InDesc.size.height == 0 || MRtvNext >= kMaxTargets)
        {
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
        if (FAILED(MDevice->CreateCommittedResource(&ScopedHeap, D3D12_HEAP_FLAG_NONE, &ScopedDesc,
                                                    D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                    IID_PPV_ARGS(&ScopedResource))))
        {
            return {};
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedTargetHeapDesc{};
        ScopedTargetHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ScopedTargetHeapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> ScopedTargetHeap;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedTargetHeapDesc, IID_PPV_ARGS(&ScopedTargetHeap))))
        {
            return {};
        }
        MDevice->CreateRenderTargetView(ScopedResource.Get(), nullptr,
                                        ScopedTargetHeap->GetCPUDescriptorHandleForHeapStart());
        const std::uint32_t ScopedId = MNextTargetId++;
        D3D12_DESCRIPTOR_HEAP_DESC ScopedSrvHeapDesc{};
        ScopedSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ScopedSrvHeapDesc.NumDescriptors = 1;
        ScopedSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> ScopedTargetSrvHeap;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedSrvHeapDesc, IID_PPV_ARGS(&ScopedTargetSrvHeap))))
        {
            return {};
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC ScopedSrvDesc{};
        ScopedSrvDesc.Format = ScopedDesc.Format;
        ScopedSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ScopedSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ScopedSrvDesc.Texture2D.MostDetailedMip = 0;
        ScopedSrvDesc.Texture2D.MipLevels = 1;
        MDevice->CreateShaderResourceView(ScopedResource.Get(), &ScopedSrvDesc,
                                          ScopedTargetSrvHeap->GetCPUDescriptorHandleForHeapStart());
        MTargets.emplace(ScopedId, ScopedResource);
        MTargetHeaps.emplace(ScopedId, ScopedTargetHeap);
        MTargetSrvHeaps.emplace(ScopedId, ScopedTargetSrvHeap);
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
        if (FAILED(MDevice->CreateCommittedResource(&ScopedDepthHeap, D3D12_HEAP_FLAG_NONE, &ScopedDepthDesc,
                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, &ScopedDepthClear,
                                                    IID_PPV_ARGS(&ScopedDepthResource))))
        {
            return {};
        }
        D3D12_DESCRIPTOR_HEAP_DESC ScopedDepthHeapDesc{};
        ScopedDepthHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ScopedDepthHeapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> ScopedDepthViewHeap;
        if (FAILED(MDevice->CreateDescriptorHeap(&ScopedDepthHeapDesc, IID_PPV_ARGS(&ScopedDepthViewHeap))))
        {
            return {};
        }
        MDevice->CreateDepthStencilView(ScopedDepthResource.Get(), nullptr,
                                        ScopedDepthViewHeap->GetCPUDescriptorHandleForHeapStart());
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
        MTargetHeaps.erase(InHandle.id);
        MTargetSrvHeaps.erase(InHandle.id);
        MTargetDepthResources.erase(InHandle.id);
        MTargetDepthHeaps.erase(InHandle.id);
        MTargets.erase(InHandle.id);
        MTargetSizes.erase(InHandle.id);
        MTargetBlend.erase(InHandle.id);
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
        const auto ScopedDepthTarget = MTargets.find(InDepth.id);
        ID3D12Resource* ScopedDepthResource =
            ScopedDepthTarget != MTargets.end() ? ScopedDepthTarget->second.Get() : ScopedCurrent->second.Get();
        ScopedSrvDesc.Format = ScopedDepthResource->GetDesc().Format;
        MDevice->CreateShaderResourceView(ScopedDepthResource, &ScopedSrvDesc, ScopedCpuSrv);
        ScopedCpuSrv.ptr += MSrvSize;
        const auto ScopedHistoryDepthTarget = MTargets.find(InHistoryDepth.id);
        ID3D12Resource* ScopedHistoryDepthResource = ScopedHistoryDepthTarget != MTargets.end()
                                                        ? ScopedHistoryDepthTarget->second.Get()
                                                        : ScopedCurrent->second.Get();
        ScopedSrvDesc.Format = ScopedHistoryDepthResource->GetDesc().Format;
        MDevice->CreateShaderResourceView(ScopedHistoryDepthResource, &ScopedSrvDesc, ScopedCpuSrv);
        D3D12_RESOURCE_BARRIER ScopedBarriers[2]{};
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
        MCommandList->ResourceBarrier(2, ScopedBarriers);
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
        MCommandList->ResourceBarrier(2, ScopedBarriers);
        const std::uint32_t ScopedBackBuffer = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSwapRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSwapRtv.ptr += ScopedBackBuffer * MRtvSize;
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedDsv = MDepthHeap->GetCPUDescriptorHandleForHeapStart();
        MCommandList->OMSetRenderTargets(1, &ScopedSwapRtv, FALSE, &ScopedDsv);
        MCommandList->SetPipelineState(MPipeline.Get());
        const D3D12_VIEWPORT ScopedMainViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        const D3D12_RECT ScopedMainScissor{0, 0, 320, 240};
        MCommandList->RSSetViewports(1, &ScopedMainViewport);
        MCommandList->RSSetScissorRects(1, &ScopedMainScissor);
        return true;
    }

    bool UpscaleRenderTarget(RenderTargetHandle InSource, RenderTargetHandle InDestination,
                             SpatialUpscaleMode InMode) override
    {
        (void)InMode;
        return TemporalResolve(InSource, InSource, {}, {}, {}, InDestination, 0.0f, 0.0f, true);
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
        const D3D12_VIEWPORT ScopedMainViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        const D3D12_RECT ScopedMainScissor{0, 0, 320, 240};
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
        if (!MUIVisible || !MHudTarget.IsValid())
        {
            return false;
        }
        const auto ScopedTarget = MTargets.find(MHudTarget.id);
        if (!MInFrame || MCommandList == nullptr || ScopedTarget == MTargets.end())
        {
            return false;
        }
        const std::uint32_t ScopedIndex = MSwapchain->GetCurrentBackBufferIndex();
        const D3D12_RESOURCE_DESC ScopedDesc = ScopedTarget->second->GetDesc();
        const D3D12_RESOURCE_DESC ScopedSwapDesc = MBackBuffers[ScopedIndex]->GetDesc();
        if (ScopedDesc.Width != ScopedSwapDesc.Width || ScopedDesc.Height != ScopedSwapDesc.Height ||
            ScopedDesc.Format != ScopedSwapDesc.Format)
        {
            return false;
        }
        D3D12_RESOURCE_BARRIER ScopedBarriers[2]{};
        ScopedBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[0].Transition.pResource = ScopedTarget->second.Get();
        ScopedBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[1].Transition.pResource = MBackBuffers[ScopedIndex].Get();
        ScopedBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        MCommandList->ResourceBarrier(2, ScopedBarriers);
        MCommandList->CopyResource(MBackBuffers[ScopedIndex].Get(), ScopedTarget->second.Get());
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        MCommandList->ResourceBarrier(2, ScopedBarriers);
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
        if (!MInFrame || MCommandList == nullptr || ScopedTarget == MTargets.end())
        {
            return;
        }
        const std::uint32_t ScopedIndex = MSwapchain->GetCurrentBackBufferIndex();
        const D3D12_RESOURCE_DESC ScopedDesc = ScopedTarget->second->GetDesc();
        const D3D12_RESOURCE_DESC ScopedSwapDesc = MBackBuffers[ScopedIndex]->GetDesc();
        if (ScopedDesc.Width != ScopedSwapDesc.Width || ScopedDesc.Height != ScopedSwapDesc.Height ||
            ScopedDesc.Format != ScopedSwapDesc.Format)
        {
            return;
        }
        D3D12_RESOURCE_BARRIER ScopedBarriers[2]{};
        ScopedBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[0].Transition.pResource = ScopedTarget->second.Get();
        ScopedBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedBarriers[1].Transition.pResource = MBackBuffers[ScopedIndex].Get();
        ScopedBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        MCommandList->ResourceBarrier(2, ScopedBarriers);
        MCommandList->CopyResource(MBackBuffers[ScopedIndex].Get(), ScopedTarget->second.Get());
        ScopedBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ScopedBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ScopedBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        MCommandList->ResourceBarrier(2, ScopedBarriers);
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
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
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
        MCommandList->SetPipelineState(MPipeline.Get());
        const std::uint32_t ScopedBackBuffer = MSwapchain->GetCurrentBackBufferIndex();
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedSwapRtv = MRtvHeap->GetCPUDescriptorHandleForHeapStart();
        ScopedSwapRtv.ptr += ScopedBackBuffer * MRtvSize;
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedDsv = MDepthHeap->GetCPUDescriptorHandleForHeapStart();
        MCommandList->OMSetRenderTargets(1, &ScopedSwapRtv, FALSE, &ScopedDsv);
        const D3D12_VIEWPORT ScopedMainViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        const D3D12_RECT ScopedMainScissor{0, 0, 320, 240};
        MCommandList->RSSetViewports(1, &ScopedMainViewport);
        MCommandList->RSSetScissorRects(1, &ScopedMainScissor);
        return true;
    }

    bool CopyDepth(RenderTargetHandle InSource, RenderTargetHandle InDestination) override
    {
        return CopyColorImage(InSource, InDestination);
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
            return false;
        }
        D3D12_RESOURCE_DESC ScopedTargetDesc = ScopedTarget->second->GetDesc();
        if (ScopedTargetDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
            ScopedTargetDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            return false;
        }
        const std::uint32_t ScopedWidth = static_cast<std::uint32_t>(ScopedTargetDesc.Width);
        const std::uint32_t ScopedHeight = ScopedTargetDesc.Height;
        const std::size_t ScopedBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u;
        if (InRgba8.size() < ScopedBytes)
        {
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
        if (FAILED(MDevice->CreateCommittedResource(&ScopedReadbackHeap, D3D12_HEAP_FLAG_NONE, &ScopedBufferDesc,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&ScopedReadback))))
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
        if (FAILED(ScopedReadback->Map(0, nullptr, &ScopedMapped)))
        {
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
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, 320, 240};
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
        const std::filesystem::path ScopedPath =
            ScopedEnv != nullptr ? std::filesystem::path(ScopedEnv) : std::filesystem::path("Saved/d3d12_pso_cache.bin");
        std::error_code ScopedError;
        if (!ScopedPath.parent_path().empty())
        {
            std::filesystem::create_directories(ScopedPath.parent_path(), ScopedError);
        }
        return ScopedPath;
    }

    void EnsurePipelineLibrary() noexcept
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

    void SavePipelineLibrary() noexcept
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

[[nodiscard]] ShaderPipelineHandle CreateShaderPipeline(const ShaderPipelineDesc& InDesc) override
    {
        if ((InDesc.domain != ShaderPipelineDomain::Sprite && InDesc.domain != ShaderPipelineDomain::Mesh) ||
            InDesc.fragmentDxil.empty())
        {
            return {};
        }
        const D3D12_SHADER_BYTECODE ScopedVS = InDesc.vertexDxil.empty()
                                                   ? D3D12_SHADER_BYTECODE{Shaders::Dx::D3D12MaterialSpriteVsDxil.data(),
                                                                           Shaders::Dx::D3D12MaterialSpriteVsDxil.size()}
                                                   : D3D12_SHADER_BYTECODE{InDesc.vertexDxil.data(),
                                                                           InDesc.vertexDxil.size()};
        const D3D12_SHADER_BYTECODE ScopedPS{InDesc.fragmentDxil.data(), InDesc.fragmentDxil.size()};
        const D3D12_INPUT_ELEMENT_DESC ScopedLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
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
        ShaderPipelineHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }

    void DestroyShaderPipeline(ShaderPipelineHandle InHandle) override
    {
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
            MInfo.apiVersion = 0xD3D12;
            MInfo.backend = Backend::D3D12;
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
        D3D12_ROOT_PARAMETER ScopedMaterialParams[2]{};
        ScopedMaterialParams[0] = ScopedRootParameter;
        ScopedMaterialParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ScopedMaterialParams[1].Constants.ShaderRegister = 0;
        ScopedMaterialParams[1].Constants.Num32BitValues = 20;
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
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
        ScopedPso.pRootSignature = MRootSignature.Get();
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
        D3D12_HEAP_PROPERTIES ScopedDepthHeap{};
        ScopedDepthHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC ScopedDepthDesc{};
        ScopedDepthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ScopedDepthDesc.Width = 320;
        ScopedDepthDesc.Height = 240;
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
    ComPtr<ID3D12PipelineState> MPipeline;
    ComPtr<ID3D12RootSignature> MMaterialRootSignature;
    ComPtr<ID3D12PipelineLibrary> MPipelineLibrary;
    bool MPipelineLibraryLoaded = false;
    std::unordered_map<std::uint32_t, ComPtr<ID3D12PipelineState>> MMaterialPipelines;
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
    std::unordered_map<std::uint32_t, ComPtr<ID3D12DescriptorHeap>> MTargetSrvHeaps;
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
