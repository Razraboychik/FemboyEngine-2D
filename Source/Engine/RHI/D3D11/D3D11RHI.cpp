// FemboyEngine2D - minimal D3D11 RHI backend (device/swapchain/present).
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/Shaders/DxShaderBlobs.hpp"

#if defined(_WIN32)
#include "Engine/Core/Log.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <SDL3/SDL.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_map>
#include <vector>
#include <memory>

namespace Fby::Rhi
{
namespace
{
using Microsoft::WRL::ComPtr;

class D3D11RHI final : public RHI
{
public:
    bool Initialize(const DeviceDesc& InDesc) override
    {
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
        ComPtr<IDXGIFactory1> ScopedFactory;
        ComPtr<IDXGIAdapter1> ScopedAdapter;
        ComPtr<IDXGIAdapter1> ScopedSoftwareAdapter;
        std::uint64_t ScopedBestVram = 0;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&ScopedFactory))) && ScopedFactory != nullptr)
        {
            for (UINT ScopedIndex = 0;; ++ScopedIndex)
            {
                ComPtr<IDXGIAdapter1> ScopedCandidate;
                if (ScopedFactory->EnumAdapters1(ScopedIndex, &ScopedCandidate) == DXGI_ERROR_NOT_FOUND ||
                    ScopedCandidate == nullptr)
                {
                    break;
                }
                DXGI_ADAPTER_DESC1 ScopedCandidateDesc{};
                if (FAILED(ScopedCandidate->GetDesc1(&ScopedCandidateDesc)))
                {
                    continue;
                }
                if ((ScopedCandidateDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                {
                    ScopedSoftwareAdapter = ScopedCandidate;
                    continue;
                }
                if (ScopedCandidateDesc.DedicatedVideoMemory >= ScopedBestVram)
                {
                    ScopedBestVram = ScopedCandidateDesc.DedicatedVideoMemory;
                    ScopedAdapter = ScopedCandidate;
                    std::snprintf(MInfo.name, sizeof(MInfo.name), "%ls", ScopedCandidateDesc.Description);
                }
            }
        }
        if (ScopedAdapter == nullptr)
        {
            ScopedAdapter = ScopedSoftwareAdapter;
        }
        DXGI_SWAP_CHAIN_DESC ScopedSwapDesc{};
        ScopedSwapDesc.BufferDesc.Width = InDesc.size.width;
        ScopedSwapDesc.BufferDesc.Height = InDesc.size.height;
        ScopedSwapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ScopedSwapDesc.SampleDesc.Count = 1;
        ScopedSwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        ScopedSwapDesc.BufferCount = 2;
        ScopedSwapDesc.OutputWindow = ScopedHwnd;
        ScopedSwapDesc.Windowed = TRUE;
        ScopedSwapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        constexpr D3D_FEATURE_LEVEL ScopedLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        constexpr D3D_FEATURE_LEVEL ScopedLegacyLevels[] = {D3D_FEATURE_LEVEL_10_0};
        const bool ScopedForceLegacy = std::getenv("FBY_D3D11_FORCE_FL10") != nullptr;
        const D3D_FEATURE_LEVEL* ScopedFeatureLevels = ScopedForceLegacy ? ScopedLegacyLevels : ScopedLevels;
        const UINT ScopedFeatureLevelCount = ScopedForceLegacy ? 1u : static_cast<UINT>(std::size(ScopedLevels));
        D3D_FEATURE_LEVEL ScopedLevel = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDeviceAndSwapChain(
                ScopedAdapter.Get(), ScopedAdapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                nullptr, 0, ScopedFeatureLevels,
                ScopedFeatureLevelCount, D3D11_SDK_VERSION, &ScopedSwapDesc, &MSwapchain,
                &MDevice, &ScopedLevel, &MContext)) ||
            MSwapchain == nullptr || MDevice == nullptr)
        {
            return false;
        }
        if (FAILED(MSwapchain->GetBuffer(0, IID_PPV_ARGS(&MBackBuffer))) ||
            FAILED(MDevice->CreateRenderTargetView(MBackBuffer.Get(), nullptr, &MRtv)))
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC ScopedDepthDesc{};
        ScopedDepthDesc.Width = InDesc.size.width;
        ScopedDepthDesc.Height = InDesc.size.height;
        ScopedDepthDesc.MipLevels = 1;
        ScopedDepthDesc.ArraySize = 1;
        ScopedDepthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthDesc.SampleDesc.Count = 1;
        ScopedDepthDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedDepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(MDevice->CreateTexture2D(&ScopedDepthDesc, nullptr, &MDepthTexture)) ||
            FAILED(MDevice->CreateDepthStencilView(MDepthTexture.Get(), nullptr, &MDepthView)))
        {
            return false;
        }
        MInfo.backend = Backend::D3D11;
        MInfo.apiVersion = static_cast<std::uint32_t>(ScopedLevel);
        if (MInfo.name[0] == '\0')
        {
            std::snprintf(MInfo.name, sizeof(MInfo.name), "D3D11 feature level %x",
                          static_cast<unsigned>(ScopedLevel));
        }
        return CreatePipeline();
    }

    void Shutdown() override
    {
        MRtv.Reset();
        MBackBuffer.Reset();
        MContext.Reset();
        MSwapchain.Reset();
        MDevice.Reset();
    }

    void Resize(Extent2D InSize) override
    {
        (void)InSize;
    }
    void WaitIdle() override
    {
        if (MContext != nullptr)
        {
            MContext->Flush();
        }
    }
    bool BeginFrame() override
    {
        if (MContext == nullptr)
        {
            return false;
        }
        const float ScopedClear[4] = {0.08f, 0.09f, 0.12f, 1.0f};
        MContext->ClearRenderTargetView(MRtv.Get(), ScopedClear);
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        MContext->ClearDepthStencilView(MDepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        MContext->IASetInputLayout(MInputLayout.Get());
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MPixelShader.Get(), nullptr, 0);
        MContext->PSSetSamplers(0, 1, MSampler.GetAddressOf());
        MContext->OMSetBlendState(MBlendState.Get(), nullptr, 0xFFFFFFFFu);
        MContext->OMSetDepthStencilState(MDepthState.Get(), 0);
        return true;
    }
    void EndFrame() override
    {
        MSwapchain->Present(0, 0);
    }

    void Submit(const FramePacket& InPacket) override
    {
        if (MContext == nullptr || InPacket.items.empty() || MVertexCapacity < InPacket.items.size() * 6)
        {
            return;
        }
        D3D11_MAPPED_SUBRESOURCE ScopedMapped{};
        if (FAILED(MContext->Map(MVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedMapped)))
        {
            return;
        }
        auto* ScopedVertices = static_cast<SmokeVertex*>(ScopedMapped.pData);
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
        MContext->Unmap(MVertexBuffer.Get(), 0);
        const auto ScopedSrv = MTextureViews.find(InPacket.textureArray);
        ID3D11ShaderResourceView* ScopedView = ScopedSrv != MTextureViews.end() ? ScopedSrv->second.Get()
                                                                              : MWhiteSrv.Get();
        if (ScopedView == nullptr)
        {
            return;
        }
        ID3D11ShaderResourceView* ScopedViews[] = {ScopedView};
        MContext->PSSetShaderResources(0, 1, ScopedViews);
        const UINT ScopedStride = sizeof(SmokeVertex);
        const UINT ScopedOffset = 0;
        MContext->IASetVertexBuffers(0, 1, MVertexBuffer.GetAddressOf(), &ScopedStride, &ScopedOffset);
        MContext->Draw(static_cast<UINT>(ScopedCount), 0);
        if (!InPacket.meshItems.empty())
        {
            D3D11_MAPPED_SUBRESOURCE ScopedMeshMapped{};
            if (FAILED(MContext->Map(MVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedMeshMapped)))
            {
                return;
            }
            auto* ScopedMeshVertices = static_cast<SmokeVertex*>(ScopedMeshMapped.pData);
            std::size_t ScopedMeshCount = 0;
            std::size_t ScopedMeshStart = 0;
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
                if (ScopedIndices.size() < 3 || ScopedVertices.empty())
                {
                    continue;
                }
                if (ScopedMeshCount + ScopedIndices.size() > MVertexCapacity)
                {
                    break;
                }
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
                const auto ScopedMeshSrv = MTextureViews.find(ScopedItem.textureArray);
                ID3D11ShaderResourceView* ScopedMeshView =
                    ScopedMeshSrv != MTextureViews.end() ? ScopedMeshSrv->second.Get() : MWhiteSrv.Get();
                if (ScopedMeshView != nullptr)
                {
                    ID3D11ShaderResourceView* ScopedMeshViews[] = {ScopedMeshView};
                    MContext->PSSetShaderResources(0, 1, ScopedMeshViews);
                }
                const UINT ScopedStride = sizeof(SmokeVertex);
                const UINT ScopedOffset = 0;
                MContext->IASetVertexBuffers(0, 1, MVertexBuffer.GetAddressOf(), &ScopedStride, &ScopedOffset);
                MContext->Draw(static_cast<UINT>(ScopedMeshCount - ScopedMeshStart), ScopedMeshStart);
                ScopedMeshStart = ScopedMeshCount;
            }
            MContext->Unmap(MVertexBuffer.Get(), 0);
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
        const std::size_t ScopedLayerBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u;
        if (ScopedWidth == 0 || ScopedHeight == 0 || InDesc.layers == 0 ||
            InPixels.size() < ScopedLayerBytes * InDesc.layers)
        {
            return {};
        }
        D3D11_TEXTURE2D_DESC ScopedDesc{};
        ScopedDesc.Width = ScopedWidth;
        ScopedDesc.Height = ScopedHeight;
        ScopedDesc.MipLevels = 1;
        ScopedDesc.ArraySize = InDesc.layers;
        ScopedDesc.Format = InDesc.srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        ScopedDesc.SampleDesc.Count = 1;
        ScopedDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ScopedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> ScopedData(InDesc.layers);
        for (std::uint32_t ScopedLayer = 0; ScopedLayer < InDesc.layers; ++ScopedLayer)
        {
            ScopedData[ScopedLayer].pSysMem = InPixels.data() + ScopedLayerBytes * ScopedLayer;
            ScopedData[ScopedLayer].SysMemPitch = ScopedWidth * 4u;
            ScopedData[ScopedLayer].SysMemSlicePitch = static_cast<UINT>(ScopedLayerBytes);
        }
        ComPtr<ID3D11Texture2D> ScopedTexture;
        ComPtr<ID3D11ShaderResourceView> ScopedSrv;
        if (FAILED(MDevice->CreateTexture2D(&ScopedDesc, ScopedData.data(), &ScopedTexture)) ||
            FAILED(MDevice->CreateShaderResourceView(ScopedTexture.Get(), nullptr, &ScopedSrv)))
        {
            return {};
        }
        const std::uint32_t ScopedId = MNextTextureId++;
        MTextures.emplace(ScopedId, ScopedTexture);
        MTextureViews.emplace(ScopedId, ScopedSrv);
        TextureArrayHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }

    void DestroyTextureArray(TextureArrayHandle InHandle) override
    {
        MTextureViews.erase(InHandle.id);
        MTextures.erase(InHandle.id);
    }

    RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& InDesc) override
    {
        if (InDesc.size.width == 0 || InDesc.size.height == 0)
        {
            return {};
        }
        D3D11_TEXTURE2D_DESC ScopedDesc{};
        ScopedDesc.Width = InDesc.size.width;
        ScopedDesc.Height = InDesc.size.height;
        ScopedDesc.MipLevels = 1;
        ScopedDesc.ArraySize = 1;
        ScopedDesc.Format = InDesc.format == RenderTargetFormat::Rg16Float ? DXGI_FORMAT_R16G16_FLOAT
                            : InDesc.format == RenderTargetFormat::Rgba8Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                                             : DXGI_FORMAT_R8G8B8A8_UNORM;
        ScopedDesc.SampleDesc.Count = 1;
        ScopedDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> ScopedTexture;
        ComPtr<ID3D11RenderTargetView> ScopedRtv;
        ComPtr<ID3D11ShaderResourceView> ScopedSrv;
        if (FAILED(MDevice->CreateTexture2D(&ScopedDesc, nullptr, &ScopedTexture)) ||
            FAILED(MDevice->CreateRenderTargetView(ScopedTexture.Get(), nullptr, &ScopedRtv)) ||
            FAILED(MDevice->CreateShaderResourceView(ScopedTexture.Get(), nullptr, &ScopedSrv)))
        {
            return {};
        }
        D3D11_TEXTURE2D_DESC ScopedDepthDesc{};
        ScopedDepthDesc.Width = InDesc.size.width;
        ScopedDepthDesc.Height = InDesc.size.height;
        ScopedDepthDesc.MipLevels = 1;
        ScopedDepthDesc.ArraySize = 1;
        ScopedDepthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthDesc.SampleDesc.Count = 1;
        ScopedDepthDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedDepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> ScopedDepthTexture;
        ComPtr<ID3D11DepthStencilView> ScopedDepthView;
        if (FAILED(MDevice->CreateTexture2D(&ScopedDepthDesc, nullptr, &ScopedDepthTexture)) ||
            FAILED(MDevice->CreateDepthStencilView(ScopedDepthTexture.Get(), nullptr, &ScopedDepthView)))
        {
            return {};
        }
        const std::uint32_t ScopedId = MNextTargetId++;
        MTargets.emplace(ScopedId, ScopedTexture);
        MRtvs.emplace(ScopedId, ScopedRtv);
        MTargetSrvs.emplace(ScopedId, ScopedSrv);
        MTargetDepths.emplace(ScopedId, ScopedDepthTexture);
        MTargetDepthViews.emplace(ScopedId, ScopedDepthView);
        MTargetSizes[ScopedId] = InDesc.size;
        MTargetBlend[ScopedId] = InDesc.blend;
        RenderTargetHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }

    void DestroyRenderTarget(RenderTargetHandle InHandle) override
    {
        MRtvs.erase(InHandle.id);
        MTargetSrvs.erase(InHandle.id);
        MTargetDepths.erase(InHandle.id);
        MTargetDepthViews.erase(InHandle.id);
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
        const auto ScopedView = MRtvs.find(InTarget.id);
        if (MContext == nullptr || ScopedView == MRtvs.end())
        {
            return false;
        }
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        MContext->ClearRenderTargetView(ScopedView->second.Get(), ScopedClear);
        return true;
    }

    bool TemporalResolve(RenderTargetHandle InCurrent, RenderTargetHandle InHistory,
                         RenderTargetHandle InMotion, RenderTargetHandle InDepth,
                         RenderTargetHandle InHistoryDepth, RenderTargetHandle InOutput,
                         float InHistoryWeight, float InMotionScale, bool InResetHistory) override
    {
        (void)InDepth;
        (void)InHistoryDepth;
        (void)InMotionScale;
        const auto ScopedCurrent = MTargetSrvs.find(InCurrent.id);
        const auto ScopedHistory = MTargetSrvs.find(InHistory.id);
        const auto ScopedOutput = MRtvs.find(InOutput.id);
        const auto ScopedSize = MTargetSizes.find(InOutput.id);
        if (MContext == nullptr || ScopedCurrent == MTargetSrvs.end() || ScopedHistory == MTargetSrvs.end() ||
            ScopedOutput == MRtvs.end() || ScopedSize == MTargetSizes.end() || MTemporalConstants == nullptr)
        {
            return false;
        }
        struct TemporalConstants
        {
            float HistoryWeight = 0.0f;
            float ResetHistory = 0.0f;
            float MotionScale = 0.0f;
            float DepthThreshold = 0.0f;
        };
        TemporalConstants ScopedConstants;
        ScopedConstants.HistoryWeight = InHistoryWeight;
        ScopedConstants.ResetHistory = InResetHistory ? 1.0f : 0.0f;
        ScopedConstants.MotionScale = InMotionScale;
        ScopedConstants.DepthThreshold = 0.01f;
        MContext->UpdateSubresource(MTemporalConstants.Get(), 0, nullptr, &ScopedConstants, 0, 0);
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        MContext->ClearRenderTargetView(ScopedOutput->second.Get(), ScopedClear);
        MContext->OMSetRenderTargets(1, ScopedOutput->second.GetAddressOf(), nullptr);
        const D3D11_VIEWPORT ScopedViewport{
            0.0f, 0.0f, static_cast<float>(ScopedSize->second.width),
            static_cast<float>(ScopedSize->second.height), 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        MContext->IASetInputLayout(nullptr);
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MTemporalVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MTemporalPixelShader.Get(), nullptr, 0);
        MContext->PSSetConstantBuffers(0, 1, MTemporalConstants.GetAddressOf());
        MContext->PSSetSamplers(0, 1, MSampler.GetAddressOf());
        const auto ScopedMotionSrv = MTargetSrvs.find(InMotion.id);
        const auto ScopedDepthSrv = MTargetSrvs.find(InDepth.id);
        const auto ScopedHistoryDepthSrv = MTargetSrvs.find(InHistoryDepth.id);
        ID3D11ShaderResourceView* ScopedMotionView =
            ScopedMotionSrv != MTargetSrvs.end() ? ScopedMotionSrv->second.Get() : ScopedCurrent->second.Get();
        ID3D11ShaderResourceView* ScopedDepthView =
            ScopedDepthSrv != MTargetSrvs.end() ? ScopedDepthSrv->second.Get() : ScopedCurrent->second.Get();
        ID3D11ShaderResourceView* ScopedHistoryDepthView =
            ScopedHistoryDepthSrv != MTargetSrvs.end() ? ScopedHistoryDepthSrv->second.Get()
                                                       : ScopedCurrent->second.Get();
        ID3D11ShaderResourceView* ScopedViews[] = {ScopedCurrent->second.Get(), ScopedHistory->second.Get(),
                                                   ScopedMotionView, ScopedDepthView, ScopedHistoryDepthView};
        MContext->PSSetShaderResources(0, 5, ScopedViews);
        MContext->Draw(3, 0);
        ID3D11ShaderResourceView* ScopedEmpty[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        MContext->PSSetShaderResources(0, 5, ScopedEmpty);
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        const D3D11_VIEWPORT ScopedMainViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedMainViewport);
        return true;
    }

    bool UpscaleRenderTarget(RenderTargetHandle InSource, RenderTargetHandle InDestination,
                             SpatialUpscaleMode InMode) override
    {
        (void)InMode;
        return TemporalResolve(InSource, InSource, {}, {}, {}, InDestination, 0.0f, 0.0f, true);
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
        if (!MUIVisible || !MHudTarget.IsValid() || MBackBuffer == nullptr)
        {
            return false;
        }
        const auto ScopedTexture = MTargets.find(MHudTarget.id);
        if (ScopedTexture == MTargets.end())
        {
            return false;
        }
        MContext->CopyResource(MBackBuffer.Get(), ScopedTexture->second.Get());
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
        const auto ScopedTexture = MTargets.find(InHandle.id);
        if (MContext == nullptr || MBackBuffer == nullptr || ScopedTexture == MTargets.end())
        {
            return;
        }
        MContext->CopyResource(MBackBuffer.Get(), ScopedTexture->second.Get());
    }

    bool RcasSharpen(RenderTargetHandle InSource, RenderTargetHandle InDestination, float InSharpness) override
    {
        const auto ScopedSource = MTargetSrvs.find(InSource.id);
        const auto ScopedOutput = MRtvs.find(InDestination.id);
        const auto ScopedSize = MTargetSizes.find(InDestination.id);
        if (MContext == nullptr || ScopedSource == MTargetSrvs.end() || ScopedOutput == MRtvs.end() ||
            ScopedSize == MTargetSizes.end() || MSharpenConstants == nullptr)
        {
            return false;
        }
        struct SharpenConstants
        {
            float Sharpness = 0.0f;
            float TexelX = 0.0f;
            float TexelY = 0.0f;
            float Padding = 0.0f;
        };
        SharpenConstants ScopedConstants;
        ScopedConstants.Sharpness = InSharpness;
        ScopedConstants.TexelX = ScopedSize->second.width > 0 ? 1.0f / static_cast<float>(ScopedSize->second.width) : 0.0f;
        ScopedConstants.TexelY = ScopedSize->second.height > 0 ? 1.0f / static_cast<float>(ScopedSize->second.height) : 0.0f;
        MContext->UpdateSubresource(MSharpenConstants.Get(), 0, nullptr, &ScopedConstants, 0, 0);
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        MContext->ClearRenderTargetView(ScopedOutput->second.Get(), ScopedClear);
        MContext->OMSetRenderTargets(1, ScopedOutput->second.GetAddressOf(), nullptr);
        const D3D11_VIEWPORT ScopedViewport{
            0.0f, 0.0f, static_cast<float>(ScopedSize->second.width),
            static_cast<float>(ScopedSize->second.height), 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        MContext->IASetInputLayout(nullptr);
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MTemporalVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MSharpenPixelShader.Get(), nullptr, 0);
        MContext->PSSetConstantBuffers(0, 1, MSharpenConstants.GetAddressOf());
        MContext->PSSetSamplers(0, 1, MSampler.GetAddressOf());
        ID3D11ShaderResourceView* ScopedViews[] = {ScopedSource->second.Get()};
        MContext->PSSetShaderResources(0, 1, ScopedViews);
        MContext->Draw(3, 0);
        ID3D11ShaderResourceView* ScopedEmpty[] = {nullptr};
        MContext->PSSetShaderResources(0, 1, ScopedEmpty);
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        const D3D11_VIEWPORT ScopedMainViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedMainViewport);
        return true;
    }

    bool RenderToTarget(RenderTargetHandle InHandle, const FramePacket& InPacket) override
    {
        const auto ScopedView = MRtvs.find(InHandle.id);
        const auto ScopedSize = MTargetSizes.find(InHandle.id);
        if (MContext == nullptr || ScopedView == MRtvs.end() || ScopedSize == MTargetSizes.end())
        {
            return false;
        }
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        MContext->ClearRenderTargetView(ScopedView->second.Get(), ScopedClear);
        MContext->OMSetRenderTargets(1, ScopedView->second.GetAddressOf(), nullptr);
        const auto ScopedBlendMode = MTargetBlend.find(InHandle.id);
        ID3D11BlendState* ScopedTargetBlend = MBlendState.Get();
        if (ScopedBlendMode != MTargetBlend.end())
        {
            if (ScopedBlendMode->second == RenderBlendMode::Additive)
            {
                ScopedTargetBlend = MAdditiveBlendState.Get();
            }
            else if (ScopedBlendMode->second == RenderBlendMode::Reveal)
            {
                ScopedTargetBlend = MRevealBlendState.Get();
            }
        }
        MContext->OMSetBlendState(ScopedTargetBlend, nullptr, 0xFFFFFFFFu);
        const D3D11_VIEWPORT ScopedViewport{
            0.0f, 0.0f, static_cast<float>(ScopedSize->second.width),
            static_cast<float>(ScopedSize->second.height), 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        Submit(InPacket);
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        const D3D11_VIEWPORT ScopedMainViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedMainViewport);
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
        if (MContext == nullptr || ScopedSource == MTargets.end() || ScopedDestination == MTargets.end())
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC ScopedSourceDesc{};
        D3D11_TEXTURE2D_DESC ScopedDestinationDesc{};
        ScopedSource->second->GetDesc(&ScopedSourceDesc);
        ScopedDestination->second->GetDesc(&ScopedDestinationDesc);
        if (ScopedSourceDesc.Width != ScopedDestinationDesc.Width ||
            ScopedSourceDesc.Height != ScopedDestinationDesc.Height ||
            ScopedSourceDesc.Format != ScopedDestinationDesc.Format)
        {
            return false;
        }
        MContext->CopyResource(ScopedDestination->second.Get(), ScopedSource->second.Get());
        return true;
    }

    bool CaptureRenderTarget(RenderTargetHandle InHandle, std::span<std::byte> InRgba8,
                             Extent2D* OutSize = nullptr) override
    {
        const auto ScopedTexture = MTargets.find(InHandle.id);
        if (MContext == nullptr || ScopedTexture == MTargets.end())
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC ScopedDesc{};
        ScopedTexture->second->GetDesc(&ScopedDesc);
        if (ScopedDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && ScopedDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            return false;
        }
        const std::size_t ScopedBytes = static_cast<std::size_t>(ScopedDesc.Width) * ScopedDesc.Height * 4u;
        if (InRgba8.size() < ScopedBytes)
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC ScopedStagingDesc = ScopedDesc;
        ScopedStagingDesc.Usage = D3D11_USAGE_STAGING;
        ScopedStagingDesc.BindFlags = 0;
        ScopedStagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ScopedStagingDesc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> ScopedStaging;
        if (FAILED(MDevice->CreateTexture2D(&ScopedStagingDesc, nullptr, &ScopedStaging)))
        {
            return false;
        }
        MContext->CopyResource(ScopedStaging.Get(), ScopedTexture->second.Get());
        D3D11_MAPPED_SUBRESOURCE ScopedMapped{};
        if (FAILED(MContext->Map(ScopedStaging.Get(), 0, D3D11_MAP_READ, 0, &ScopedMapped)))
        {
            return false;
        }
        const bool ScopedSwap = ScopedDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
        for (std::uint32_t ScopedY = 0; ScopedY < ScopedDesc.Height; ++ScopedY)
        {
            const auto* ScopedSource = static_cast<const std::byte*>(ScopedMapped.pData) +
                                       static_cast<std::size_t>(ScopedY) * ScopedMapped.RowPitch;
            std::byte* ScopedDestination = InRgba8.data() + static_cast<std::size_t>(ScopedY) * ScopedDesc.Width * 4u;
            for (std::uint32_t ScopedX = 0; ScopedX < ScopedDesc.Width; ++ScopedX)
            {
                const std::size_t ScopedOffset = static_cast<std::size_t>(ScopedX) * 4u;
                if (ScopedSwap)
                {
                    ScopedDestination[ScopedOffset + 0] = ScopedSource[ScopedOffset + 2];
                    ScopedDestination[ScopedOffset + 1] = ScopedSource[ScopedOffset + 1];
                    ScopedDestination[ScopedOffset + 2] = ScopedSource[ScopedOffset + 0];
                    ScopedDestination[ScopedOffset + 3] = ScopedSource[ScopedOffset + 3];
                }
                else
                {
                    std::memcpy(ScopedDestination + ScopedOffset, ScopedSource + ScopedOffset, 4);
                }
            }
        }
        MContext->Unmap(ScopedStaging.Get(), 0);
        if (OutSize != nullptr)
        {
            *OutSize = {ScopedDesc.Width, ScopedDesc.Height};
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
        const auto ScopedView = MRtvs.find(InTarget.id);
        const auto ScopedSize = MTargetSizes.find(InTarget.id);
        if (MContext == nullptr || MMotionVertexBuffer == nullptr || ScopedView == MRtvs.end() ||
            ScopedSize == MTargetSizes.end() || InCount == 0 || MMotionItems.empty())
        {
            return false;
        }
        const std::uint32_t ScopedCount = std::min(InCount, static_cast<std::uint32_t>(MMotionItems.size()));
        D3D11_MAPPED_SUBRESOURCE ScopedMapped{};
        if (FAILED(MContext->Map(MMotionVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedMapped)))
        {
            return false;
        }
        auto* ScopedVertices = static_cast<MotionVertex*>(ScopedMapped.pData);
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
            const MotionVertex ScopedCorners[6] = {
                {{ScopedCenterX - ScopedHalfX, ScopedCenterY - ScopedHalfY, 0.5f}, {ScopedDeltaX, ScopedDeltaY}},
                {{ScopedCenterX + ScopedHalfX, ScopedCenterY - ScopedHalfY, 0.5f}, {ScopedDeltaX, ScopedDeltaY}},
                {{ScopedCenterX + ScopedHalfX, ScopedCenterY + ScopedHalfY, 0.5f}, {ScopedDeltaX, ScopedDeltaY}},
                {{ScopedCenterX - ScopedHalfX, ScopedCenterY - ScopedHalfY, 0.5f}, {ScopedDeltaX, ScopedDeltaY}},
                {{ScopedCenterX + ScopedHalfX, ScopedCenterY + ScopedHalfY, 0.5f}, {ScopedDeltaX, ScopedDeltaY}},
                {{ScopedCenterX - ScopedHalfX, ScopedCenterY + ScopedHalfY, 0.5f}, {ScopedDeltaX, ScopedDeltaY}},
            };
            std::memcpy(ScopedVertices + ScopedVertexCount, ScopedCorners, sizeof(ScopedCorners));
            ScopedVertexCount += 6;
        }
        MContext->Unmap(MMotionVertexBuffer.Get(), 0);
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        MContext->ClearRenderTargetView(ScopedView->second.Get(), ScopedClear);
        MContext->OMSetRenderTargets(1, ScopedView->second.GetAddressOf(), nullptr);
        MContext->OMSetDepthStencilState(nullptr, 0);
        MContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        MContext->IASetInputLayout(MMotionInputLayout.Get());
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MMotionVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MMotionPixelShader.Get(), nullptr, 0);
        const D3D11_VIEWPORT ScopedMotionViewport{
            0.0f, 0.0f, static_cast<float>(ScopedSize->second.width),
            static_cast<float>(ScopedSize->second.height), 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedMotionViewport);
        const UINT ScopedStride = sizeof(MotionVertex);
        const UINT ScopedOffset = 0;
        MContext->IASetVertexBuffers(0, 1, MMotionVertexBuffer.GetAddressOf(), &ScopedStride, &ScopedOffset);
        MContext->Draw(static_cast<UINT>(ScopedVertexCount), 0);
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        MContext->OMSetDepthStencilState(MDepthState.Get(), 0);
        MContext->OMSetBlendState(MBlendState.Get(), nullptr, 0xFFFFFFFFu);
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        return true;
    }

    [[nodiscard]] bool RenderTargetDepthTexture(RenderTargetHandle InHandle, NativeTextureView& InView) const override
    {
        const auto ScopedDepth = MTargetDepths.find(InHandle.id);
        if (ScopedDepth == MTargetDepths.end())
        {
            return false;
        }
        InView.view = nullptr;
        InView.image = ScopedDepth->second.Get();
        InView.layout = 0;
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
        return Backend::D3D11;
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

    bool CreatePipeline()
    {
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
        ComPtr<ID3DBlob> ScopedVSBlob;
        ComPtr<ID3DBlob> ScopedPSBlob;
        ComPtr<ID3DBlob> ScopedErrors;
        const bool ScopedLegacyShaderModel = MInfo.apiVersion < D3D_FEATURE_LEVEL_11_0;
        const char* ScopedVertexTarget = ScopedLegacyShaderModel ? "vs_4_0" : "vs_5_0";
        const char* ScopedPixelTarget = ScopedLegacyShaderModel ? "ps_4_0" : "ps_5_0";
        const void* ScopedVSData = Shaders::Dx::D3D11SpriteVsSm5.data();
        SIZE_T ScopedVSSize = Shaders::Dx::D3D11SpriteVsSm5.size();
        const void* ScopedPSData = Shaders::Dx::D3D11SpritePsSm5.data();
        SIZE_T ScopedPSSize = Shaders::Dx::D3D11SpritePsSm5.size();
        if (ScopedLegacyShaderModel)
        {
            if (FAILED(D3DCompile(ScopedShader, sizeof(ScopedShader), "sprite11.hlsl", nullptr, nullptr, "VSMain",
                                  ScopedVertexTarget, 0, 0, &ScopedVSBlob, &ScopedErrors)) ||
                FAILED(D3DCompile(ScopedShader, sizeof(ScopedShader), "sprite11.hlsl", nullptr, nullptr, "PSMain",
                                  ScopedPixelTarget, 0, 0, &ScopedPSBlob, &ScopedErrors)))
            {
                return false;
            }
            ScopedVSData = ScopedVSBlob->GetBufferPointer();
            ScopedVSSize = ScopedVSBlob->GetBufferSize();
            ScopedPSData = ScopedPSBlob->GetBufferPointer();
            ScopedPSSize = ScopedPSBlob->GetBufferSize();
        }
        const D3D11_INPUT_ELEMENT_DESC ScopedLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        if (FAILED(MDevice->CreateVertexShader(ScopedVSData, ScopedVSSize, nullptr, &MVertexShader)) ||
            FAILED(MDevice->CreatePixelShader(ScopedPSData, ScopedPSSize, nullptr, &MPixelShader)) ||
            FAILED(MDevice->CreateInputLayout(ScopedLayout, static_cast<UINT>(std::size(ScopedLayout)),
                                              ScopedVSData, ScopedVSSize, &MInputLayout)))
        {
            return false;
        }
        D3D11_BLEND_DESC ScopedBlend{};
        ScopedBlend.RenderTarget[0].BlendEnable = TRUE;
        ScopedBlend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        ScopedBlend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        ScopedBlend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        ScopedBlend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        ScopedBlend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        ScopedBlend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        ScopedBlend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(MDevice->CreateBlendState(&ScopedBlend, &MBlendState)))
        {
            return false;
        }
        D3D11_BLEND_DESC ScopedAdditive = ScopedBlend;
        ScopedAdditive.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        ScopedAdditive.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        if (FAILED(MDevice->CreateBlendState(&ScopedAdditive, &MAdditiveBlendState)))
        {
            return false;
        }
        D3D11_BLEND_DESC ScopedReveal = ScopedBlend;
        ScopedReveal.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        ScopedReveal.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_COLOR;
        ScopedReveal.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        ScopedReveal.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        if (FAILED(MDevice->CreateBlendState(&ScopedReveal, &MRevealBlendState)))
        {
            return false;
        }
        const std::uint32_t ScopedWhitePixel = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC ScopedWhiteDesc{};
        ScopedWhiteDesc.Width = 1;
        ScopedWhiteDesc.Height = 1;
        ScopedWhiteDesc.MipLevels = 1;
        ScopedWhiteDesc.ArraySize = 1;
        ScopedWhiteDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ScopedWhiteDesc.SampleDesc.Count = 1;
        ScopedWhiteDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ScopedWhiteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA ScopedWhiteData{};
        ScopedWhiteData.pSysMem = &ScopedWhitePixel;
        ScopedWhiteData.SysMemPitch = 4;
        ComPtr<ID3D11Texture2D> ScopedWhiteTexture;
        if (FAILED(MDevice->CreateTexture2D(&ScopedWhiteDesc, &ScopedWhiteData, &ScopedWhiteTexture)) ||
            FAILED(MDevice->CreateShaderResourceView(ScopedWhiteTexture.Get(), nullptr, &MWhiteSrv)))
        {
            return false;
        }
        D3D11_DEPTH_STENCIL_DESC ScopedDepthState{};
        ScopedDepthState.DepthEnable = TRUE;
        ScopedDepthState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ScopedDepthState.DepthFunc = D3D11_COMPARISON_LESS;
        if (FAILED(MDevice->CreateDepthStencilState(&ScopedDepthState, &MDepthState)))
        {
            return false;
        }
        D3D11_SAMPLER_DESC ScopedSampler{};
        ScopedSampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        ScopedSampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        ScopedSampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        ScopedSampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        ScopedSampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(MDevice->CreateSamplerState(&ScopedSampler, &MSampler)))
        {
            return false;
        }
        D3D11_BUFFER_DESC ScopedBuffer{};
        ScopedBuffer.ByteWidth = static_cast<UINT>(sizeof(SmokeVertex) * MVertexCapacity);
        ScopedBuffer.Usage = D3D11_USAGE_DYNAMIC;
        ScopedBuffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        ScopedBuffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(MDevice->CreateBuffer(&ScopedBuffer, nullptr, &MVertexBuffer)))
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
        ComPtr<ID3DBlob> ScopedMotionVS;
        ComPtr<ID3DBlob> ScopedMotionPS;
        const void* ScopedMotionVSData = Shaders::Dx::D3D11MotionVsSm5.data();
        SIZE_T ScopedMotionVSSize = Shaders::Dx::D3D11MotionVsSm5.size();
        const void* ScopedMotionPSData = Shaders::Dx::D3D11MotionPsSm5.data();
        SIZE_T ScopedMotionPSSize = Shaders::Dx::D3D11MotionPsSm5.size();
        if (ScopedLegacyShaderModel)
        {
            if (FAILED(D3DCompile(ScopedMotionShader, sizeof(ScopedMotionShader), "motion11.hlsl", nullptr, nullptr,
                                  "VSMain", ScopedVertexTarget, 0, 0, &ScopedMotionVS, &ScopedErrors)) ||
                FAILED(D3DCompile(ScopedMotionShader, sizeof(ScopedMotionShader), "motion11.hlsl", nullptr, nullptr,
                                  "PSMain", ScopedPixelTarget, 0, 0, &ScopedMotionPS, &ScopedErrors)))
            {
                return false;
            }
            ScopedMotionVSData = ScopedMotionVS->GetBufferPointer();
            ScopedMotionVSSize = ScopedMotionVS->GetBufferSize();
            ScopedMotionPSData = ScopedMotionPS->GetBufferPointer();
            ScopedMotionPSSize = ScopedMotionPS->GetBufferSize();
        }
        const D3D11_INPUT_ELEMENT_DESC ScopedMotionLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        if (FAILED(MDevice->CreateVertexShader(ScopedMotionVSData, ScopedMotionVSSize, nullptr,
                                               &MMotionVertexShader)) ||
            FAILED(MDevice->CreatePixelShader(ScopedMotionPSData, ScopedMotionPSSize, nullptr,
                                              &MMotionPixelShader)) ||
            FAILED(MDevice->CreateInputLayout(ScopedMotionLayout, static_cast<UINT>(std::size(ScopedMotionLayout)),
                                              ScopedMotionVSData, ScopedMotionVSSize, &MMotionInputLayout)))
        {
            return false;
        }
        D3D11_BUFFER_DESC ScopedMotionBuffer{};
        ScopedMotionBuffer.ByteWidth = static_cast<UINT>(sizeof(MotionVertex) * MVertexCapacity);
        ScopedMotionBuffer.Usage = D3D11_USAGE_DYNAMIC;
        ScopedMotionBuffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        ScopedMotionBuffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(MDevice->CreateBuffer(&ScopedMotionBuffer, nullptr, &MMotionVertexBuffer)))
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
        ComPtr<ID3DBlob> ScopedTemporalVS;
        ComPtr<ID3DBlob> ScopedTemporalPS;
        const void* ScopedTemporalVSData = Shaders::Dx::D3D11TemporalVsSm5.data();
        SIZE_T ScopedTemporalVSSize = Shaders::Dx::D3D11TemporalVsSm5.size();
        const void* ScopedTemporalPSData = Shaders::Dx::D3D11TemporalPsSm5.data();
        SIZE_T ScopedTemporalPSSize = Shaders::Dx::D3D11TemporalPsSm5.size();
        if (ScopedLegacyShaderModel)
        {
            if (FAILED(D3DCompile(ScopedTemporalShader, sizeof(ScopedTemporalShader), "temporal11.hlsl", nullptr,
                                  nullptr, "VSMain", ScopedVertexTarget, 0, 0, &ScopedTemporalVS, &ScopedErrors)) ||
                FAILED(D3DCompile(ScopedTemporalShader, sizeof(ScopedTemporalShader), "temporal11.hlsl", nullptr,
                                  nullptr, "PSMain", ScopedPixelTarget, 0, 0, &ScopedTemporalPS, &ScopedErrors)))
            {
                return false;
            }
            ScopedTemporalVSData = ScopedTemporalVS->GetBufferPointer();
            ScopedTemporalVSSize = ScopedTemporalVS->GetBufferSize();
            ScopedTemporalPSData = ScopedTemporalPS->GetBufferPointer();
            ScopedTemporalPSSize = ScopedTemporalPS->GetBufferSize();
        }
        if (FAILED(MDevice->CreateVertexShader(ScopedTemporalVSData, ScopedTemporalVSSize, nullptr,
                                               &MTemporalVertexShader)) ||
            FAILED(MDevice->CreatePixelShader(ScopedTemporalPSData, ScopedTemporalPSSize, nullptr,
                                              &MTemporalPixelShader)))
        {
            return false;
        }
        D3D11_BUFFER_DESC ScopedTemporalBuffer{};
        ScopedTemporalBuffer.ByteWidth = 16;
        ScopedTemporalBuffer.Usage = D3D11_USAGE_DEFAULT;
        ScopedTemporalBuffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(MDevice->CreateBuffer(&ScopedTemporalBuffer, nullptr, &MTemporalConstants)))
        {
            return false;
        }
        constexpr char ScopedSharpenShader[] = R"(
Texture2D SourceColor : register(t0);
SamplerState SharpenSampler : register(s0);
cbuffer SharpenParams : register(b0) { float Sharpness; float TexelX; float TexelY; float Padding; };
float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float4 center = SourceColor.Sample(SharpenSampler, uv);
    float4 left = SourceColor.Sample(SharpenSampler, uv + float2(-TexelX, 0.0));
    float4 right = SourceColor.Sample(SharpenSampler, uv + float2(TexelX, 0.0));
    float4 up = SourceColor.Sample(SharpenSampler, uv + float2(0.0, -TexelY));
    float4 down = SourceColor.Sample(SharpenSampler, uv + float2(0.0, TexelY));
    float4 sharpened = center + (center * 4.0 - left - right - up - down) * Sharpness;
    return float4(sharpened.rgb, center.a);
}
)";
        ComPtr<ID3DBlob> ScopedSharpenPS;
        const void* ScopedSharpenPSData = Shaders::Dx::D3D11SharpenPsSm5.data();
        SIZE_T ScopedSharpenPSSize = Shaders::Dx::D3D11SharpenPsSm5.size();
        if (ScopedLegacyShaderModel)
        {
            if (FAILED(D3DCompile(ScopedSharpenShader, sizeof(ScopedSharpenShader), "sharpen11.hlsl", nullptr, nullptr,
                                  "PSMain", ScopedPixelTarget, 0, 0, &ScopedSharpenPS, &ScopedErrors)))
            {
                return false;
            }
            ScopedSharpenPSData = ScopedSharpenPS->GetBufferPointer();
            ScopedSharpenPSSize = ScopedSharpenPS->GetBufferSize();
        }
        if (FAILED(MDevice->CreatePixelShader(ScopedSharpenPSData, ScopedSharpenPSSize, nullptr, &MSharpenPixelShader)))
        {
            return false;
        }
        D3D11_BUFFER_DESC ScopedSharpenBuffer{};
        ScopedSharpenBuffer.ByteWidth = 16;
        ScopedSharpenBuffer.Usage = D3D11_USAGE_DEFAULT;
        ScopedSharpenBuffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(MDevice->CreateBuffer(&ScopedSharpenBuffer, nullptr, &MSharpenConstants));
    }

    DeviceInfo MInfo{};
    FrameStats MStats{};
    ComPtr<ID3D11Device> MDevice;
    ComPtr<ID3D11DeviceContext> MContext;
    ComPtr<IDXGISwapChain> MSwapchain;
    ComPtr<ID3D11Texture2D> MBackBuffer;
    RenderTargetHandle MHudTarget;
    bool MUIVisible = true;
    bool MHudUpscaleNearest = false;
    ComPtr<ID3D11RenderTargetView> MRtv;
    ComPtr<ID3D11VertexShader> MVertexShader;
    ComPtr<ID3D11PixelShader> MPixelShader;
    ComPtr<ID3D11InputLayout> MInputLayout;
    ComPtr<ID3D11BlendState> MBlendState;
    ComPtr<ID3D11BlendState> MAdditiveBlendState;
    ComPtr<ID3D11BlendState> MRevealBlendState;
    ComPtr<ID3D11SamplerState> MSampler;
    ComPtr<ID3D11ShaderResourceView> MWhiteSrv;
    std::vector<MotionVectorItem> MMotionItems;
    ComPtr<ID3D11Texture2D> MDepthTexture;
    ComPtr<ID3D11DepthStencilView> MDepthView;
    ComPtr<ID3D11DepthStencilState> MDepthState;
    ComPtr<ID3D11Buffer> MVertexBuffer;
    ComPtr<ID3D11VertexShader> MMotionVertexShader;
    ComPtr<ID3D11PixelShader> MMotionPixelShader;
    ComPtr<ID3D11InputLayout> MMotionInputLayout;
    ComPtr<ID3D11Buffer> MMotionVertexBuffer;
    ComPtr<ID3D11VertexShader> MTemporalVertexShader;
    ComPtr<ID3D11PixelShader> MTemporalPixelShader;
    ComPtr<ID3D11Buffer> MTemporalConstants;
    ComPtr<ID3D11PixelShader> MSharpenPixelShader;
    ComPtr<ID3D11Buffer> MSharpenConstants;
    std::uint32_t MVertexCapacity = 6 * 4096;
    std::unordered_map<std::uint32_t, ComPtr<ID3D11Texture2D>> MTargets;
    std::unordered_map<std::uint32_t, ComPtr<ID3D11RenderTargetView>> MRtvs;
    std::unordered_map<std::uint32_t, ComPtr<ID3D11ShaderResourceView>> MTargetSrvs;
    std::unordered_map<std::uint32_t, ComPtr<ID3D11Texture2D>> MTargetDepths;
    std::unordered_map<std::uint32_t, ComPtr<ID3D11DepthStencilView>> MTargetDepthViews;
    std::unordered_map<std::uint32_t, Fby::Rhi::Extent2D> MTargetSizes;
    std::unordered_map<std::uint32_t, RenderBlendMode> MTargetBlend;
    std::uint32_t MNextTargetId = 1;
    std::unordered_map<std::uint32_t, ComPtr<ID3D11Texture2D>> MTextures;
    std::unordered_map<std::uint32_t, ComPtr<ID3D11ShaderResourceView>> MTextureViews;
    struct MeshRecord
    {
        std::vector<MeshVertex> Vertices;
        std::vector<std::uint16_t> Indices;
    };
    std::unordered_map<std::uint32_t, MeshRecord> MMeshes;
    std::uint32_t MNextMeshId = 1;
    std::uint32_t MNextTextureId = 1;
};
}

std::unique_ptr<RHI> CreateD3D11RHI()
{
    return std::make_unique<D3D11RHI>();
}
}
#else
namespace Fby::Rhi
{
std::unique_ptr<RHI> CreateD3D11RHI()
{
    return nullptr;
}
}
#endif
