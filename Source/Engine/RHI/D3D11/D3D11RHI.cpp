// FemboyEngine2D - minimal D3D11 RHI backend (device/swapchain/present).
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/RenderPasses.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/RHI/Shaders/DxShaderBlobs.hpp"

#if defined(_WIN32)
#include "Engine/Core/Log.hpp"

#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <SDL3/SDL.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
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
        constexpr std::uint32_t kMaxBackendItems = 1u << 16;
        const std::uint32_t ScopedRequestedItems = std::max(1u, InDesc.maxItemsPerFrame);
        const std::uint32_t ScopedItems = std::min(ScopedRequestedItems, kMaxBackendItems);
        MVertexCapacity = ScopedItems * 6u;
        if (ScopedItems < ScopedRequestedItems)
        {
            LogWarn("d3d11", "sprite item capacity clamped from {} to {}", ScopedRequestedItems, ScopedItems);
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
        ComPtr<IDXGIFactory1> ScopedFactory;
        ComPtr<IDXGIAdapter1> ScopedAdapter;
        ComPtr<IDXGIAdapter1> ScopedSoftwareAdapter;
        std::uint64_t ScopedBestVram = 0;
        DXGI_ADAPTER_DESC1 ScopedChosenDesc{};
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
                    ScopedChosenDesc = ScopedCandidateDesc;
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
        UINT ScopedCreateFlags = 0;
        const bool ScopedDebugRequested = std::getenv("FBY_D3D11_DEBUG") != nullptr;
        if (ScopedDebugRequested)
        {
            ScopedCreateFlags |= D3D11_CREATE_DEVICE_DEBUG;
        }
        HRESULT ScopedCreateResult = D3D11CreateDeviceAndSwapChain(
            ScopedAdapter.Get(), ScopedAdapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, ScopedCreateFlags, ScopedFeatureLevels, ScopedFeatureLevelCount, D3D11_SDK_VERSION,
            &ScopedSwapDesc, &MSwapchain, &MDevice, &ScopedLevel, &MContext);
        if (FAILED(ScopedCreateResult) && ScopedDebugRequested)
        {
            LogWarn("d3d11", "debug device unavailable; retrying without the debug layer");
            ScopedCreateFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
            ScopedCreateResult = D3D11CreateDeviceAndSwapChain(
                ScopedAdapter.Get(), ScopedAdapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                nullptr, ScopedCreateFlags, ScopedFeatureLevels, ScopedFeatureLevelCount, D3D11_SDK_VERSION,
                &ScopedSwapDesc, &MSwapchain, &MDevice, &ScopedLevel, &MContext);
        }
        if (FAILED(ScopedCreateResult) || MSwapchain == nullptr || MDevice == nullptr)
        {
            return false;
        }
        if ((ScopedCreateFlags & D3D11_CREATE_DEVICE_DEBUG) != 0)
        {
            MDebugLayer = true;
            MDevice.As(&MInfoQueue);
            LogInfo("d3d11", "debug layer enabled");
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
        MInfo.framesInFlight = 1;
        MInfo.maxItemsPerFrame = ScopedItems;
        MInfo.maxTextureLayers = InDesc.maxTextureLayers;
        MInfo.maxMeshVerticesPerFrame = InDesc.maxMeshVerticesPerFrame;
        MInfo.maxBoneMatricesPerFrame = InDesc.maxBoneMatricesPerFrame;
        MInfo.maxStaticMeshVertices = InDesc.maxStaticMeshVertices;
        MInfo.maxMeshDrawsPerFrame = InDesc.maxMeshDrawsPerFrame;
        MInfo.tier = ScopedLevel >= D3D_FEATURE_LEVEL_11_0 ? GpuTier::Baseline : GpuTier::Legacy;
        MInfo.platform = 0;
        MInfo.gpuVendor = ScopedChosenDesc.VendorId == 0x10DE ? 1
                        : (ScopedChosenDesc.VendorId == 0x1002 || ScopedChosenDesc.VendorId == 0x1022) ? 2
                        : ScopedChosenDesc.VendorId == 0x8086 ? 3
                        : 0;
        MInfo.discrete = (ScopedChosenDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;
        // The engine-side temporal/upscale/sharpen chain and custom frame
        // generation both run on D3D11, so report them as available.
        MInfo.upscalers.motionVectors = true;
        MInfo.upscalers.fsr1 = true;
        MInfo.upscalers.fsr2 = true;
        MInfo.upscalers.bicubic = true;
        MInfo.upscalers.frameGeneration = true;
        MExtent = InDesc.size;
        MDesc = InDesc;
        if (MInfo.name[0] == '\0')
        {
            std::snprintf(MInfo.name, sizeof(MInfo.name), "D3D11 feature level %x",
                          static_cast<unsigned>(ScopedLevel));
        }
        std::snprintf(MInfo.driver, sizeof(MInfo.driver), "D3D11 feature level %x",
                      static_cast<unsigned>(ScopedLevel));
        const bool ScopedPipeline = CreatePipeline() && CreateBuiltInAtlas();
        if (!CreatePostFxResources())
        {
            LogWarn("d3d11", "post-FX composite unavailable");
        }
        return ScopedPipeline;
    }

    [[nodiscard]] static bool AccessibilityWanted(const FramePacket& InPacket) noexcept
    {
        const std::uint32_t ScopedFlags = InPacket.accessibility.flags;
        return ((ScopedFlags & AccessibilityColorVision) != 0 && InPacket.accessibility.colorVision != 0) ||
               ((ScopedFlags & AccessibilityFlashGuard) != 0 && InPacket.accessibility.flashGain < 0.999f);
    }

    void ApplyAccessibility(RenderTargetHandle InHandle, const FramePacket& InPacket, Extent2D InSize)
    {
        const auto ScopedTarget = MTargets.find(InHandle.id);
        const auto ScopedRtv = MRtvs.find(InHandle.id);
        if (MContext == nullptr || ScopedTarget == MTargets.end() || ScopedRtv == MRtvs.end() ||
            MAccessibilityPixelShader == nullptr || MAccessibilityConstants == nullptr)
        {
            return;
        }
        AccessibilityResources& ScopedResources = MAccessibilityResources[InHandle.id];
        if (ScopedResources.Scene == nullptr)
        {
            D3D11_TEXTURE2D_DESC ScopedDesc{};
            ScopedTarget->second->GetDesc(&ScopedDesc);
            ScopedDesc.Usage = D3D11_USAGE_DEFAULT;
            ScopedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            ScopedDesc.CPUAccessFlags = 0;
            ScopedDesc.ArraySize = 1;
            ScopedDesc.MipLevels = 1;
            if (FAILED(MDevice->CreateTexture2D(&ScopedDesc, nullptr, &ScopedResources.Scene)) ||
                FAILED(MDevice->CreateShaderResourceView(ScopedResources.Scene.Get(), nullptr,
                                                        &ScopedResources.SceneSrv)))
            {
                return;
            }
            D3D11_TEXTURE2D_DESC ScopedHistoryDesc = ScopedDesc;
            ScopedHistoryDesc.Format = DXGI_FORMAT_R8_UNORM;
            ScopedHistoryDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
            {
                if (FAILED(MDevice->CreateTexture2D(&ScopedHistoryDesc, nullptr,
                                                    &ScopedResources.History[ScopedIndex])) ||
                    FAILED(MDevice->CreateShaderResourceView(ScopedResources.History[ScopedIndex].Get(), nullptr,
                                                             &ScopedResources.HistorySrv[ScopedIndex])) ||
                    FAILED(MDevice->CreateUnorderedAccessView(ScopedResources.History[ScopedIndex].Get(), nullptr,
                                                              &ScopedResources.HistoryUav[ScopedIndex])))
                {
                    return;
                }
                const float ScopedZero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                MContext->ClearUnorderedAccessViewFloat(ScopedResources.HistoryUav[ScopedIndex].Get(), ScopedZero);
            }
        }
        // The scene is copied so the pass can sample it while writing the target.
        MContext->OMSetRenderTargets(0, nullptr, nullptr);
        MContext->CopyResource(ScopedResources.Scene.Get(), ScopedTarget->second.Get());
        float ScopedBlock[20] = {};
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
        ScopedBlock[15] = 0.0f;
        ScopedBlock[16] = InPacket.accessibility.flashThreshold;
        ScopedBlock[17] = InPacket.accessibility.flashGain;
        ScopedBlock[18] = (ScopedFlags & AccessibilityFlashGuard) != 0 ? 1.0f : 0.0f;
        ScopedBlock[19] = 0.0f;
        D3D11_MAPPED_SUBRESOURCE ScopedMapped{};
        if (FAILED(MContext->Map(MAccessibilityConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedMapped)))
        {
            return;
        }
        std::memcpy(ScopedMapped.pData, ScopedBlock, sizeof(ScopedBlock));
        MContext->Unmap(MAccessibilityConstants.Get(), 0);
        // Ping-pong: this frame writes the texture the next frame will read.
        const std::uint32_t ScopedRead = ScopedResources.ReadIndex;
        const std::uint32_t ScopedWrite = 1u - ScopedRead;
        ID3D11RenderTargetView* ScopedRtvArray[] = {ScopedRtv->second.Get()};
        MContext->OMSetRenderTargets(1, ScopedRtvArray, nullptr);
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(std::max(InSize.width, 1u)),
                                            static_cast<float>(std::max(InSize.height, 1u)), 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        MContext->IASetInputLayout(nullptr);
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MPostFxVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MAccessibilityPixelShader.Get(), nullptr, 0);
        MContext->PSSetSamplers(0, 1, MSampler.GetAddressOf());
        MContext->PSSetConstantBuffers(0, 1, MAccessibilityConstants.GetAddressOf());
        ID3D11ShaderResourceView* ScopedViews[] = {ScopedResources.SceneSrv.Get(),
                                                   ScopedResources.HistorySrv[ScopedRead].Get()};
        MContext->PSSetShaderResources(0, 2, ScopedViews);
        MContext->Draw(3, 0);
        ID3D11ShaderResourceView* ScopedNoViews[] = {nullptr, nullptr};
        MContext->PSSetShaderResources(0, 2, ScopedNoViews);
        // The luminance history is written by a compute pass, so the pixel pass
        // never binds the same resource as input and output.
        UnbindShaderResources();
        if (MAccessibilityFlashShader != nullptr)
        {
            ID3D11ShaderResourceView* ScopedSceneView[] = {ScopedResources.SceneSrv.Get()};
            ID3D11UnorderedAccessView* ScopedHistoryUav[] = {ScopedResources.HistoryUav[ScopedWrite].Get()};
            MContext->CSSetShader(MAccessibilityFlashShader.Get(), nullptr, 0);
            MContext->CSSetShaderResources(0, 1, ScopedSceneView);
            MContext->CSSetUnorderedAccessViews(0, 1, ScopedHistoryUav, nullptr);
            MContext->CSSetSamplers(0, 1, MSampler.GetAddressOf());
            MContext->Dispatch((std::max(InSize.width, 1u) + 7u) / 8u, (std::max(InSize.height, 1u) + 7u) / 8u, 1);
            ID3D11UnorderedAccessView* ScopedNoUav[] = {nullptr};
            MContext->CSSetUnorderedAccessViews(0, 1, ScopedNoUav, nullptr);
            ID3D11ShaderResourceView* ScopedNoInput[] = {nullptr};
            MContext->CSSetShaderResources(0, 1, ScopedNoInput);
            MContext->CSSetShader(nullptr, nullptr, 0);
        }
        ScopedResources.ReadIndex = ScopedWrite;
        MContext->OMSetBlendState(MBlendState.Get(), nullptr, 0xFFFFFFFFu);
        MContext->OMSetDepthStencilState(MDepthState.Get(), 0);
        MContext->IASetInputLayout(MInputLayout.Get());
        MContext->VSSetShader(MVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MPixelShader.Get(), nullptr, 0);
        ++MStats.drawCalls;
    }

    void UnbindShaderResources()
    {
        ID3D11ShaderResourceView* ScopedSrvs[3] = {nullptr, nullptr, nullptr};
        MContext->PSSetShaderResources(0, 3, ScopedSrvs);
        MContext->CSSetShaderResources(0, 2, ScopedSrvs);
        ID3D11UnorderedAccessView* ScopedUavs[2] = {nullptr, nullptr};
        MContext->CSSetUnorderedAccessViews(0, 2, ScopedUavs, nullptr);
    }

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
            if (FAILED(MInfoQueue->GetMessage(ScopedIndex, nullptr, &ScopedSize)))
            {
                continue;
            }
            std::vector<char> ScopedStorage(ScopedSize);
            auto* ScopedMessage = reinterpret_cast<D3D11_MESSAGE*>(ScopedStorage.data());
            if (FAILED(MInfoQueue->GetMessage(ScopedIndex, ScopedMessage, &ScopedSize)))
            {
                continue;
            }
            if (ScopedMessage->Severity == D3D11_MESSAGE_SEVERITY_ERROR ||
                ScopedMessage->Severity == D3D11_MESSAGE_SEVERITY_WARNING)
            {
                LogWarn("d3d11-validation", "{}", ScopedMessage->pDescription);
            }
        }
        MInfoQueue->ClearStoredMessages();
    }

    void Shutdown() override
    {
        DumpValidation();
        MRtv.Reset();
        MBackBuffer.Reset();
        MContext.Reset();
        MSwapchain.Reset();
        MDevice.Reset();
    }

    void Resize(Extent2D InSize) override
    {
        if (MSwapchain == nullptr || MDevice == nullptr || InSize.width == 0 || InSize.height == 0)
        {
            return;
        }
        WaitIdle();
        // ResizeBuffers fails while the context still references the back buffer.
        UnbindShaderResources();
        MContext->OMSetRenderTargets(0, nullptr, nullptr);
        MContext->ClearState();
        MContext->Flush();
        MRtv.Reset();
        MBackBuffer.Reset();
        MDepthView.Reset();
        MDepthTexture.Reset();
        const HRESULT ScopedResize = MSwapchain->ResizeBuffers(2, InSize.width, InSize.height,
                                                                 DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        if (FAILED(ScopedResize))
        {
            if (ScopedResize == DXGI_ERROR_DEVICE_REMOVED || ScopedResize == DXGI_ERROR_DEVICE_RESET)
            {
                MInfo.deviceLost = true;
            }
            LogError("d3d11", "ResizeBuffers failed (0x{:08X})", static_cast<unsigned>(ScopedResize));
            return;
        }
        if (FAILED(MSwapchain->GetBuffer(0, IID_PPV_ARGS(&MBackBuffer))) ||
            FAILED(MDevice->CreateRenderTargetView(MBackBuffer.Get(), nullptr, &MRtv)))
        {
            LogError("d3d11", "back buffer recreation failed");
            return;
        }
        D3D11_TEXTURE2D_DESC ScopedDepthDesc{};
        ScopedDepthDesc.Width = InSize.width;
        ScopedDepthDesc.Height = InSize.height;
        ScopedDepthDesc.MipLevels = 1;
        ScopedDepthDesc.ArraySize = 1;
        ScopedDepthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthDesc.SampleDesc.Count = 1;
        ScopedDepthDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedDepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(MDevice->CreateTexture2D(&ScopedDepthDesc, nullptr, &MDepthTexture)) ||
            FAILED(MDevice->CreateDepthStencilView(MDepthTexture.Get(), nullptr, &MDepthView)))
        {
            LogError("d3d11", "depth target recreation failed");
            return;
        }
        MExtent = InSize;
        MDesc.size = InSize;
        if (!CreatePostFxResources())
        {
            LogWarn("d3d11", "post-FX composite unavailable after resize");
        }
    }

    [[nodiscard]] bool SetVSync(bool InEnabled) override
    {
        MDesc.vsync = InEnabled;
        return true;
    }
    [[nodiscard]] bool VSyncEnabled() const noexcept override
    {
        return MDesc.vsync;
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
        MStats = {};
        ++MStats.frameIndex;
        MMainClearPending = true;
        MPostFxPending = false;
        MPostFxGraphPending = false;
        MBloomRanThisFrame = false;
        UnbindShaderResources();
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        MContext->ClearDepthStencilView(MDepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(std::max(MExtent.width, 1u)),
                                            static_cast<float>(std::max(MExtent.height, 1u)), 0.0f, 1.0f};
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
    [[nodiscard]] bool RecordGraphicsPass(std::string_view InPipeline, const void* InConstants,
                                          std::uint32_t InConstantsBytes, Extent2D InExtent,
                                          std::uint32_t InInstanceCount) override
    {
        (void)InExtent;
        if (MContext == nullptr || InConstants == nullptr || InInstanceCount != 1)
        {
            return false;
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
        return false;
    }

    void EndFrame() override
    {
        RecordPostFxComposite();
        RecordColorGrade();
        if (MMainClearPending)
        {
            MMainClearPending = false;
            const float ScopedClear[4] = {0.08f, 0.09f, 0.12f, 1.0f};
            MContext->ClearRenderTargetView(MRtv.Get(), ScopedClear);
        }
        // The back buffer must not stay bound as an RTV while Present reads it.
        MContext->OMSetRenderTargets(0, nullptr, nullptr);
        const HRESULT ScopedPresent = MSwapchain->Present(MDesc.vsync ? 1u : 0u, 0);
        if (ScopedPresent == DXGI_ERROR_DEVICE_REMOVED || ScopedPresent == DXGI_ERROR_DEVICE_RESET)
        {
            MInfo.deviceLost = true;
            LogError("d3d11", "Present failed with device loss (0x{:08X})",
                     static_cast<unsigned>(ScopedPresent));
        }
        else if (FAILED(ScopedPresent))
        {
            LogWarn("d3d11", "Present failed (0x{:08X})", static_cast<unsigned>(ScopedPresent));
        }
    }

    void Submit(const FramePacket& InPacket) override
    {
        if (MContext == nullptr)
        {
            return;
        }
        MStats.uploadedBytes += InPacket.items.size_bytes() + InPacket.boneMatrices.size_bytes();
        for (const MeshRenderItem& ScopedUploadItem : InPacket.meshItems)
        {
            MStats.uploadedBytes += ScopedUploadItem.vertices.size_bytes() + ScopedUploadItem.indices.size_bytes();
        }
        const bool ScopedPostFx =
            (InPacket.postFxEnabled || MPostFxGraphPending) && MPostFxReady && MSceneRtv != nullptr;
        if (ScopedPostFx)
        {
            MPostFxPending = true;
            MPostFxPendingSettings = InPacket.postFx;
        }
        if (MMainClearPending)
        {
            MMainClearPending = false;
            const float ScopedClearColor[4] = {static_cast<float>(InPacket.clear.R()) / 255.0f,
                                               static_cast<float>(InPacket.clear.G()) / 255.0f,
                                               static_cast<float>(InPacket.clear.B()) / 255.0f,
                                               static_cast<float>(InPacket.clear.A()) / 255.0f};
            ID3D11RenderTargetView* ScopedClearTarget = ScopedPostFx ? MSceneRtv.Get() : MRtv.Get();
            MContext->ClearRenderTargetView(ScopedClearTarget, ScopedClearColor);
        }
        if (ScopedPostFx)
        {
            ID3D11RenderTargetView* ScopedSceneRtvs[] = {MSceneRtv.Get()};
            UnbindShaderResources();
            MContext->OMSetRenderTargets(1, ScopedSceneRtvs, MSceneDepthView.Get());
            const D3D11_VIEWPORT ScopedSceneViewport{0.0f, 0.0f, static_cast<float>(std::max(MExtent.width, 1u)),
                                                     static_cast<float>(std::max(MExtent.height, 1u)), 0.0f, 1.0f};
            MContext->RSSetViewports(1, &ScopedSceneViewport);
        }
        const CameraData ScopedCamera = InPacket.camera;
        const float ScopedHalfWidth = std::max(ScopedCamera.viewport.X * 0.5f, 0.0001f);
        const float ScopedHalfHeight = std::max(ScopedCamera.viewport.Y * 0.5f, 0.0001f);
        const auto ScopedProject = [&](float InWorldX, float InWorldY, float* OutClipX, float* OutClipY)
        {
            *OutClipX = (InWorldX - ScopedCamera.center.X) * ScopedCamera.zoom / ScopedHalfWidth;
            *OutClipY = -((InWorldY - ScopedCamera.center.Y) * ScopedCamera.zoom) / ScopedHalfHeight;
        };
        MStats.items += static_cast<std::uint32_t>(InPacket.items.size());
        MStats.batches += static_cast<std::uint32_t>(InPacket.batches.size());

        if (!InPacket.items.empty())
        {
            std::array<GpuLight, 64> ScopedLights{};
            const std::size_t ScopedLightCount = InPacket.lights.size() < ScopedLights.size() - 1u
                                                     ? InPacket.lights.size()
                                                     : ScopedLights.size() - 1u;
            for (std::size_t ScopedI = 0; ScopedI < ScopedLightCount; ++ScopedI)
            {
                ScopedLights[ScopedI] = InPacket.lights[ScopedI];
            }
            ScopedLights.back().PositionAndRadius.W = static_cast<float>(ScopedLightCount);
            D3D11_MAPPED_SUBRESOURCE ScopedLightMapped{};
            if (SUCCEEDED(MContext->Map(MSpriteLightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedLightMapped)))
            {
                std::memcpy(ScopedLightMapped.pData, ScopedLights.data(), sizeof(ScopedLights));
                MContext->Unmap(MSpriteLightBuffer.Get(), 0);
            }
            const std::size_t ScopedSpriteVertices = InPacket.items.size() * 6u;
            if (ScopedSpriteVertices > MVertexCapacity)
            {
                LogError("d3d11", "vertex buffer holds {} vertices, packet needs {}", MVertexCapacity,
                         ScopedSpriteVertices);
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
                const float ScopedR = static_cast<float>((ScopedItem.color >> 0) & 0xFFu) / 255.0f;
                const float ScopedG = static_cast<float>((ScopedItem.color >> 8) & 0xFFu) / 255.0f;
                const float ScopedB = static_cast<float>((ScopedItem.color >> 16) & 0xFFu) / 255.0f;
                const float ScopedA = static_cast<float>((ScopedItem.color >> 24) & 0xFFu) / 255.0f;
                const float ScopedCos = std::cos(ScopedItem.rotation);
                const float ScopedSin = std::sin(ScopedItem.rotation);
                const bool ScopedFlipX = (ScopedItem.flags & static_cast<std::uint32_t>(RenderFlags::FlipX)) != 0u;
                const bool ScopedFlipY = (ScopedItem.flags & static_cast<std::uint32_t>(RenderFlags::FlipY)) != 0u;
                constexpr float ScopedCorners[6][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f},
                                                       {-0.5f, -0.5f}, {0.5f, 0.5f},  {-0.5f, 0.5f}};
                for (std::uint32_t ScopedCorner = 0; ScopedCorner < 6u; ++ScopedCorner)
                {
                    const float ScopedLocalX = ScopedCorners[ScopedCorner][0] * ScopedItem.size.X;
                    const float ScopedLocalY = ScopedCorners[ScopedCorner][1] * ScopedItem.size.Y;
                    const float ScopedRotatedX = ScopedLocalX * ScopedCos - ScopedLocalY * ScopedSin;
                    const float ScopedRotatedY = ScopedLocalX * ScopedSin + ScopedLocalY * ScopedCos;
                    float ScopedClipX = 0.0f;
                    float ScopedClipY = 0.0f;
                    ScopedProject(ScopedItem.position.X + ScopedRotatedX, ScopedItem.position.Y + ScopedRotatedY,
                                  &ScopedClipX, &ScopedClipY);
                    float ScopedU = ScopedCorners[ScopedCorner][0] + 0.5f;
                    float ScopedV = ScopedCorners[ScopedCorner][1] + 0.5f;
                    if (ScopedFlipX)
                    {
                        ScopedU = 1.0f - ScopedU;
                    }
                    if (ScopedFlipY)
                    {
                        ScopedV = 1.0f - ScopedV;
                    }
                    SmokeVertex& ScopedVertex = ScopedVertices[ScopedCount++];
                    ScopedVertex.Position[0] = ScopedClipX;
                    ScopedVertex.Position[1] = ScopedClipY;
                    ScopedVertex.Position[2] = 0.5f;
                    ScopedVertex.Uv[0] = ScopedItem.uvMin.X + (ScopedItem.uvMax.X - ScopedItem.uvMin.X) * ScopedU;
                    ScopedVertex.Uv[1] = ScopedItem.uvMin.Y + (ScopedItem.uvMax.Y - ScopedItem.uvMin.Y) * ScopedV;
                    ScopedVertex.Color[0] = ScopedR;
                    ScopedVertex.Color[1] = ScopedG;
                    ScopedVertex.Color[2] = ScopedB;
                    ScopedVertex.Color[3] = ScopedA;
                    ScopedVertex.Layer = static_cast<float>(ScopedItem.layer);
                    ScopedVertex.NormalMeta =
                        EncodeNormalVertexMeta(NormalMapLayer(ScopedItem.flags), NormalMapStrength(ScopedItem.flags));
                }
            }
            MContext->Unmap(MVertexBuffer.Get(), 0);
            const UINT ScopedStride = sizeof(SmokeVertex);
            const UINT ScopedOffset = 0;
            MContext->IASetVertexBuffers(0, 1, MVertexBuffer.GetAddressOf(), &ScopedStride, &ScopedOffset);
            if (InPacket.batches.empty())
            {
                ID3D11ShaderResourceView* ScopedViews[] = {TextureViewFor(InPacket.textureArray),
                                                           MSpriteLightSrv.Get()};
                MContext->PSSetShaderResources(0, 2, ScopedViews);
                const Rhi::RenderItem& ScopedFirst = InPacket.items.front();
                const bool ScopedSdf =
                    (ScopedFirst.flags & (Rhi::kRenderFlagSdfText | Rhi::kRenderFlagSdfOutline |
                                          Rhi::kRenderFlagSdfGlow)) != 0u;
                const bool ScopedNormal = ScopedSdf || (NormalMapLayer(ScopedFirst.flags) >= 1u &&
                                                        NormalMapStrength(ScopedFirst.flags) > 0.0f);
                MContext->PSSetShader(ScopedNormal ? MPixelShader.Get() : MBasePixelShader.Get(), nullptr, 0);
                MContext->Draw(static_cast<UINT>(ScopedCount), 0);
                ++MStats.drawCalls;
            }
            else
            {
                for (const DrawBatch& ScopedBatch : InPacket.batches)
                {
                    if (ScopedBatch.itemCount == 0 || ScopedBatch.firstItem >= InPacket.items.size() ||
                        ScopedBatch.itemCount > InPacket.items.size() - ScopedBatch.firstItem)
                    {
                        continue;
                    }
                    const std::uint32_t ScopedTexture =
                        ScopedBatch.textureArray != 0 ? ScopedBatch.textureArray : InPacket.textureArray;
                    ID3D11ShaderResourceView* ScopedViews[] = {TextureViewFor(ScopedTexture),
                                                               MSpriteLightSrv.Get()};
                    MContext->PSSetShaderResources(0, 2, ScopedViews);
                    const Rhi::RenderItem& ScopedBatchFirst =
                        InPacket.items[std::min<std::size_t>(ScopedBatch.firstItem, InPacket.items.size() - 1u)];
                    const bool ScopedBatchSdf =
                        (ScopedBatchFirst.flags & (Rhi::kRenderFlagSdfText | Rhi::kRenderFlagSdfOutline |
                                                   Rhi::kRenderFlagSdfGlow)) != 0u;
                    const bool ScopedBatchNormal =
                        ScopedBatchSdf || (NormalMapLayer(ScopedBatchFirst.flags) >= 1u &&
                                          NormalMapStrength(ScopedBatchFirst.flags) > 0.0f);
                    MContext->PSSetShader(ScopedBatchNormal ? MPixelShader.Get() : MBasePixelShader.Get(), nullptr, 0);
                    MContext->Draw(static_cast<UINT>(ScopedBatch.itemCount) * 6u,
                                   static_cast<UINT>(ScopedBatch.firstItem) * 6u);
                    ++MStats.drawCalls;
                }
            }
        }

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
                    float ScopedClipX = 0.0f;
                    float ScopedClipY = 0.0f;
                    ScopedProject(ScopedWorldX, ScopedWorldY, &ScopedClipX, &ScopedClipY);
                    ScopedMeshVertices[ScopedMeshCount++] = {
                        {ScopedClipX, ScopedClipY, 0.5f},
                        {ScopedVertex.uv.X, ScopedVertex.uv.Y},
                        {ScopedR, ScopedG, ScopedB, ScopedA},
                        static_cast<float>(ScopedItem.layer),
                        0.0f};
                }
                ID3D11ShaderResourceView* ScopedMeshViews[] = {TextureViewFor(ScopedItem.textureArray),
                                                               MSpriteLightSrv.Get()};
                MContext->PSSetShaderResources(0, 2, ScopedMeshViews);
                MContext->PSSetShader(MBasePixelShader.Get(), nullptr, 0);
                const UINT ScopedStride = sizeof(SmokeVertex);
                const UINT ScopedOffset = 0;
                MContext->IASetVertexBuffers(0, 1, MVertexBuffer.GetAddressOf(), &ScopedStride, &ScopedOffset);
                MContext->Draw(static_cast<UINT>(ScopedMeshCount - ScopedMeshStart), static_cast<UINT>(ScopedMeshStart));
                ++MStats.drawCalls;
                ++MStats.meshDraws;
                ++MStats.meshItems;
                if (ScopedItem.mesh.IsValid())
                {
                    ++MStats.staticMeshDraws;
                }
                ScopedMeshStart = ScopedMeshCount;
            }
            MContext->Unmap(MVertexBuffer.Get(), 0);
        }
    }

    [[nodiscard]] ID3D11ShaderResourceView* TextureViewFor(std::uint32_t InId) const noexcept
    {
        const auto ScopedFound = MTextureViews.find(InId);
        if (ScopedFound != MTextureViews.end())
        {
            return ScopedFound->second.Get();
        }
        // 0xFFFFFFFF and 0 both select the built-in atlas on this backend.
        const auto ScopedAtlas = MTextureViews.find(MAtlasId);
        if (ScopedAtlas != MTextureViews.end())
        {
            return ScopedAtlas->second.Get();
        }
        return MWhiteSrv.Get();
    }

    [[nodiscard]] static constexpr DXGI_FORMAT ToDxgiFormat(Fby::Assets::TextureFormat InFormat) noexcept
    {
        switch (InFormat)
        {
            case Fby::Assets::TextureFormat::Rgba8:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case Fby::Assets::TextureFormat::Bc7Unorm:
                return DXGI_FORMAT_BC7_UNORM;
            case Fby::Assets::TextureFormat::Bc7Srgb:
                return DXGI_FORMAT_BC7_UNORM_SRGB;
            case Fby::Assets::TextureFormat::Astc4x4Unorm:
            case Fby::Assets::TextureFormat::Astc4x4Srgb:
            case Fby::Assets::TextureFormat::Astc6x6Unorm:
            case Fby::Assets::TextureFormat::Astc6x6Srgb:
                return DXGI_FORMAT_UNKNOWN;
        }
        return DXGI_FORMAT_UNKNOWN;
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
        const std::uint32_t ScopedMips = std::max(InDesc.mipLevels, 1u);
        const DXGI_FORMAT ScopedFormat = ToDxgiFormat(InDesc.format);
        if (ScopedWidth == 0 || ScopedHeight == 0 || InDesc.layers == 0 || ScopedFormat == DXGI_FORMAT_UNKNOWN)
        {
            if (ScopedFormat == DXGI_FORMAT_UNKNOWN)
            {
                LogError("d3d11", "CreateTextureArray: {} is not supported on Direct3D",
                         Fby::Assets::TextureFormatName(InDesc.format));
            }
            return {};
        }
        const std::uint64_t ScopedExpected = Fby::Assets::TextureByteSize(
            InDesc.format, ScopedWidth, ScopedHeight, InDesc.layers, ScopedMips);
        if (InPixels.size() < ScopedExpected)
        {
            LogError("d3d11", "CreateTextureArray: payload is {} bytes, expected {} ({}x{} x {} x {} mips, {})",
                     InPixels.size(), ScopedExpected, ScopedWidth, ScopedHeight, InDesc.layers, ScopedMips,
                     Fby::Assets::TextureFormatName(InDesc.format));
            return {};
        }
        D3D11_TEXTURE2D_DESC ScopedDesc{};
        ScopedDesc.Width = ScopedWidth;
        ScopedDesc.Height = ScopedHeight;
        ScopedDesc.MipLevels = ScopedMips;
        ScopedDesc.ArraySize = InDesc.layers;
        ScopedDesc.Format = ScopedFormat;
        ScopedDesc.SampleDesc.Count = 1;
        // DEFAULT (not IMMUTABLE) so UpdateTextureArrayLayer can stream layers.
        ScopedDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> ScopedData(static_cast<std::size_t>(InDesc.layers) * ScopedMips);
        std::uint64_t ScopedSourceOffset = 0;
        for (std::uint32_t ScopedLayer = 0; ScopedLayer < InDesc.layers; ++ScopedLayer)
        {
            for (std::uint32_t ScopedMip = 0; ScopedMip < ScopedMips; ++ScopedMip)
            {
                const std::uint32_t ScopedMipWidth = Fby::Assets::MipExtent(ScopedWidth, ScopedMip);
                const std::uint32_t ScopedMipHeight = Fby::Assets::MipExtent(ScopedHeight, ScopedMip);
                const std::uint64_t ScopedMipBytes =
                    Fby::Assets::MipByteSize(InDesc.format, ScopedMipWidth, ScopedMipHeight);
                const Fby::Assets::BlockExtent ScopedBlock = Fby::Assets::FormatBlockExtent(InDesc.format);
                D3D11_SUBRESOURCE_DATA& ScopedSubresource = ScopedData[ScopedMip + ScopedLayer * ScopedMips];
                ScopedSubresource.pSysMem = InPixels.data() + ScopedSourceOffset;
                ScopedSubresource.SysMemPitch =
                    static_cast<UINT>(((ScopedMipWidth + ScopedBlock.width - 1u) / ScopedBlock.width) *
                                      Fby::Assets::FormatBlockBytes(InDesc.format));
                ScopedSubresource.SysMemSlicePitch = static_cast<UINT>(ScopedMipBytes);
                ScopedSourceOffset += ScopedMipBytes;
            }
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
        MTextureInfos[ScopedId] = TextureArrayInfo{ScopedWidth, ScopedHeight, ScopedMips, InDesc.layers, InDesc.format};
        TextureArrayHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }

    void DestroyTextureArray(TextureArrayHandle InHandle) override
    {
        MTextureInfos.erase(InHandle.id);
        MTextureViews.erase(InHandle.id);
        MTextures.erase(InHandle.id);
    }

    TextureArrayHandle CreateTextureArrayFromRenderTarget(RenderTargetHandle InHandle) override
    {
        const auto ScopedTarget = MTargets.find(InHandle.id);
        if (MDevice == nullptr || ScopedTarget == MTargets.end())
        {
            return {};
        }
        D3D11_TEXTURE2D_DESC ScopedDesc{};
        ScopedTarget->second->GetDesc(&ScopedDesc);
        ScopedDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ScopedDesc.CPUAccessFlags = 0;
        ScopedDesc.MiscFlags = 0;
        ScopedDesc.ArraySize = 1;
        ScopedDesc.MipLevels = 1;
        ComPtr<ID3D11Texture2D> ScopedTexture;
        if (FAILED(MDevice->CreateTexture2D(&ScopedDesc, nullptr, &ScopedTexture)))
        {
            return {};
        }
        // A render target bound as an output cannot be copied from.
        UnbindShaderResources();
        MContext->OMSetRenderTargets(0, nullptr, nullptr);
        MContext->CopyResource(ScopedTexture.Get(), ScopedTarget->second.Get());
        ComPtr<ID3D11ShaderResourceView> ScopedSrv;
        if (FAILED(MDevice->CreateShaderResourceView(ScopedTexture.Get(), nullptr, &ScopedSrv)))
        {
            return {};
        }
        const std::uint32_t ScopedId = MNextTextureId++;
        MTextures.emplace(ScopedId, ScopedTexture);
        MTextureViews.emplace(ScopedId, ScopedSrv);
        MTextureInfos[ScopedId] = TextureArrayInfo{ScopedDesc.Width, ScopedDesc.Height, 1, 1,
                                                   Fby::Assets::TextureFormat::Rgba8};
        TextureArrayHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }

    bool UpdateTextureArrayLayer(TextureArrayHandle InHandle, std::uint32_t InLayer, std::uint32_t InMipLevel,
                                 std::span<const std::byte> InData) override
    {
        const auto ScopedInfo = MTextureInfos.find(InHandle.id);
        const auto ScopedTexture = MTextures.find(InHandle.id);
        if (MContext == nullptr || ScopedInfo == MTextureInfos.end() || ScopedTexture == MTextures.end())
        {
            return false;
        }
        const TextureArrayInfo& ScopedArray = ScopedInfo->second;
        if (InLayer >= ScopedArray.Layers || InMipLevel >= ScopedArray.Mips)
        {
            return false;
        }
        const std::uint32_t ScopedWidth = Fby::Assets::MipExtent(ScopedArray.Width, InMipLevel);
        const std::uint32_t ScopedHeight = Fby::Assets::MipExtent(ScopedArray.Height, InMipLevel);
        const std::uint64_t ScopedBytes = Fby::Assets::MipByteSize(ScopedArray.Format, ScopedWidth, ScopedHeight);
        if (InData.size() < ScopedBytes)
        {
            return false;
        }
        const Fby::Assets::BlockExtent ScopedBlock = Fby::Assets::FormatBlockExtent(ScopedArray.Format);
        const UINT ScopedRowPitch =
            static_cast<UINT>(((ScopedWidth + ScopedBlock.width - 1u) / ScopedBlock.width) *
                              Fby::Assets::FormatBlockBytes(ScopedArray.Format));
        const D3D11_BOX ScopedBox{0, 0, 0, ScopedWidth, ScopedHeight, 1};
        MContext->UpdateSubresource(ScopedTexture->second.Get(), InMipLevel + InLayer * ScopedArray.Mips, &ScopedBox,
                                    InData.data(), ScopedRowPitch, static_cast<UINT>(ScopedBytes));
        return true;
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
        ScopedDepthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        ScopedDepthDesc.SampleDesc.Count = 1;
        ScopedDepthDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedDepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> ScopedDepthTexture;
        ComPtr<ID3D11DepthStencilView> ScopedDepthView;
        if (FAILED(MDevice->CreateTexture2D(&ScopedDepthDesc, nullptr, &ScopedDepthTexture)))
        {
            LogWarn("d3d11", "render target depth texture creation failed");
            return {};
        }
        D3D11_DEPTH_STENCIL_VIEW_DESC ScopedDepthViewDesc{};
        ScopedDepthViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        if (FAILED(MDevice->CreateDepthStencilView(ScopedDepthTexture.Get(), &ScopedDepthViewDesc, &ScopedDepthView)))
        {
            LogWarn("d3d11", "render target depth DSV creation failed");
            return {};
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC ScopedDepthSrvDesc{};
        ScopedDepthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        ScopedDepthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        ScopedDepthSrvDesc.Texture2D.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> ScopedDepthSrv;
        if (FAILED(MDevice->CreateShaderResourceView(ScopedDepthTexture.Get(), &ScopedDepthSrvDesc, &ScopedDepthSrv)))
        {
            LogWarn("d3d11", "render target depth SRV creation failed");
            return {};
        }
        const std::uint32_t ScopedId = MNextTargetId++;
        MTargets.emplace(ScopedId, ScopedTexture);
        MRtvs.emplace(ScopedId, ScopedRtv);
        MTargetSrvs.emplace(ScopedId, ScopedSrv);
        MTargetDepths.emplace(ScopedId, ScopedDepthTexture);
        MTargetDepthViews.emplace(ScopedId, ScopedDepthView);
        MTargetDepthSrvs.emplace(ScopedId, ScopedDepthSrv);
        MTargetSizes[ScopedId] = InDesc.size;
        MTargetBlend[ScopedId] = InDesc.blend;
        RenderTargetHandle ScopedHandle;
        ScopedHandle.id = ScopedId;
        return ScopedHandle;
    }

    void DestroyRenderTarget(RenderTargetHandle InHandle) override
    {
        MAccessibilityResources.erase(InHandle.id);
        MRtvs.erase(InHandle.id);
        MTargetSrvs.erase(InHandle.id);
        MTargetDepths.erase(InHandle.id);
        MTargetDepthViews.erase(InHandle.id);
        MTargetDepthSrvs.erase(InHandle.id);
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
        UnbindShaderResources();
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
        // The temporal shader compares real depth, not the colour image.
        const auto ScopedDepthSrv = MTargetDepthSrvs.find(InDepth.id);
        const auto ScopedHistoryDepthSrv = MTargetDepthSrvs.find(InHistoryDepth.id);
        ID3D11ShaderResourceView* ScopedMotionView =
            ScopedMotionSrv != MTargetSrvs.end() ? ScopedMotionSrv->second.Get() : ScopedCurrent->second.Get();
        ID3D11ShaderResourceView* ScopedDepthView =
            ScopedDepthSrv != MTargetDepthSrvs.end() ? ScopedDepthSrv->second.Get() : ScopedCurrent->second.Get();
        ID3D11ShaderResourceView* ScopedHistoryDepthView =
            ScopedHistoryDepthSrv != MTargetDepthSrvs.end() ? ScopedHistoryDepthSrv->second.Get()
                                                            : ScopedCurrent->second.Get();
        ID3D11ShaderResourceView* ScopedViews[] = {ScopedCurrent->second.Get(), ScopedHistory->second.Get(),
                                                   ScopedMotionView, ScopedDepthView, ScopedHistoryDepthView};
        MContext->PSSetShaderResources(0, 5, ScopedViews);
        MContext->Draw(3, 0);
        ID3D11ShaderResourceView* ScopedEmpty[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        MContext->PSSetShaderResources(0, 5, ScopedEmpty);
        UnbindShaderResources();
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        const D3D11_VIEWPORT ScopedMainViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedMainViewport);
        return true;
    }

    bool UpscaleRenderTarget(RenderTargetHandle InSource, RenderTargetHandle InDestination,
                             SpatialUpscaleMode InMode) override
    {
        const auto ScopedSource = MTargetSrvs.find(InSource.id);
        const auto ScopedOutput = MRtvs.find(InDestination.id);
        const auto ScopedOutputSize = MTargetSizes.find(InDestination.id);
        const auto ScopedSourceSize = MTargetSizes.find(InSource.id);
        if (MContext == nullptr || ScopedSource == MTargetSrvs.end() || ScopedOutput == MRtvs.end() ||
            ScopedOutputSize == MTargetSizes.end() || ScopedSourceSize == MTargetSizes.end() ||
            MUpscalePixelShader == nullptr || MUpscaleConstants == nullptr || MPostFxVertexShader == nullptr)
        {
            return false;
        }
        D3D11_MAPPED_SUBRESOURCE ScopedMapped{};
        if (FAILED(MContext->Map(MUpscaleConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedMapped)))
        {
            return false;
        }
        // Nearest = 0, bilinear = 1, bicubic = 2; FSR1 has no DX port and maps
        // to the bicubic filter.
        const float ScopedMode =
            InMode == SpatialUpscaleMode::Nearest ? 0.0f : (InMode == SpatialUpscaleMode::Bilinear ? 1.0f : 2.0f);
        const float ScopedConstants[4] = {ScopedMode, static_cast<float>(ScopedSourceSize->second.width),
                                          static_cast<float>(ScopedSourceSize->second.height), 0.0f};
        std::memcpy(ScopedMapped.pData, ScopedConstants, sizeof(ScopedConstants));
        MContext->Unmap(MUpscaleConstants.Get(), 0);
        UnbindShaderResources();
        MContext->OMSetRenderTargets(1, ScopedOutput->second.GetAddressOf(), nullptr);
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f,
                                            static_cast<float>(std::max(ScopedOutputSize->second.width, 1u)),
                                            static_cast<float>(std::max(ScopedOutputSize->second.height, 1u)), 0.0f,
                                            1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        MContext->IASetInputLayout(nullptr);
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MPostFxVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MUpscalePixelShader.Get(), nullptr, 0);
        MContext->PSSetSamplers(0, 1, MSampler.GetAddressOf());
        MContext->PSSetConstantBuffers(0, 1, MUpscaleConstants.GetAddressOf());
        ID3D11ShaderResourceView* ScopedViews[] = {ScopedSource->second.Get()};
        MContext->PSSetShaderResources(0, 1, ScopedViews);
        MContext->Draw(3, 0);
        ID3D11ShaderResourceView* ScopedNone[] = {nullptr};
        MContext->PSSetShaderResources(0, 1, ScopedNone);
        MContext->OMSetDepthStencilState(MDepthState.Get(), 0);
        MContext->OMSetBlendState(MBlendState.Get(), nullptr, 0xFFFFFFFFu);
        MContext->IASetInputLayout(MInputLayout.Get());
        MContext->VSSetShader(MVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MPixelShader.Get(), nullptr, 0);
        ++MStats.drawCalls;
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
        const auto ScopedSrv = MTargetSrvs.find(MHudTarget.id);
        if (!MUIVisible || !MHudTarget.IsValid() || MRtv == nullptr || ScopedSrv == MTargetSrvs.end() ||
            MHudPixelShader == nullptr || MHudConstants == nullptr || MPostFxVertexShader == nullptr)
        {
            return false;
        }
        const auto ScopedSize = MTargetSizes.find(MHudTarget.id);
        const float ScopedWidth =
            ScopedSize != MTargetSizes.end() ? static_cast<float>(ScopedSize->second.width) : 1.0f;
        const float ScopedHeight =
            ScopedSize != MTargetSizes.end() ? static_cast<float>(ScopedSize->second.height) : 1.0f;
        D3D11_MAPPED_SUBRESOURCE ScopedMapped{};
        if (FAILED(MContext->Map(MHudConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedMapped)))
        {
            return false;
        }
        const float ScopedConstants[4] = {MHudUpscaleNearest ? 1.0f : 0.0f, ScopedWidth, ScopedHeight, 0.0f};
        std::memcpy(ScopedMapped.pData, ScopedConstants, sizeof(ScopedConstants));
        MContext->Unmap(MHudConstants.Get(), 0);

        // The HUD target is premultiplied and drawn as a blended overlay; a
        // CopyResource would replace the scene and requires equal sizes.
        UnbindShaderResources();
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(std::max(MExtent.width, 1u)),
                                            static_cast<float>(std::max(MExtent.height, 1u)), 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        MContext->IASetInputLayout(nullptr);
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MPostFxVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MHudPixelShader.Get(), nullptr, 0);
        MContext->PSSetSamplers(0, 1, MSampler.GetAddressOf());
        MContext->PSSetConstantBuffers(0, 1, MHudConstants.GetAddressOf());
        ID3D11ShaderResourceView* ScopedViews[] = {ScopedSrv->second.Get()};
        MContext->PSSetShaderResources(0, 1, ScopedViews);
        MContext->Draw(3, 0);
        ID3D11ShaderResourceView* ScopedNone[] = {nullptr};
        MContext->PSSetShaderResources(0, 1, ScopedNone);
        MContext->OMSetDepthStencilState(MDepthState.Get(), 0);
        MContext->OMSetBlendState(MBlendState.Get(), nullptr, 0xFFFFFFFFu);
        MContext->IASetInputLayout(MInputLayout.Get());
        MContext->VSSetShader(MVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MPixelShader.Get(), nullptr, 0);
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
        const auto ScopedSrv = MTargetSrvs.find(InHandle.id);
        if (MContext == nullptr || MRtv == nullptr || ScopedSrv == MTargetSrvs.end() ||
            MBlitPixelShader == nullptr || MPostFxVertexShader == nullptr)
        {
            return;
        }
        // A target with a different size still presents: a fullscreen sample
        // scales it instead of CopyResource, which requires equal dimensions.
        UnbindShaderResources();
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(std::max(MExtent.width, 1u)),
                                            static_cast<float>(std::max(MExtent.height, 1u)), 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        MContext->IASetInputLayout(nullptr);
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MPostFxVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MBlitPixelShader.Get(), nullptr, 0);
        MContext->PSSetSamplers(0, 1, MSampler.GetAddressOf());
        ID3D11ShaderResourceView* ScopedViews[] = {ScopedSrv->second.Get()};
        MContext->PSSetShaderResources(0, 1, ScopedViews);
        MContext->Draw(3, 0);
        ID3D11ShaderResourceView* ScopedNone[] = {nullptr};
        MContext->PSSetShaderResources(0, 1, ScopedNone);
        MContext->OMSetDepthStencilState(MDepthState.Get(), 0);
        MContext->OMSetBlendState(MBlendState.Get(), nullptr, 0xFFFFFFFFu);
        MContext->IASetInputLayout(MInputLayout.Get());
        MContext->VSSetShader(MVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MPixelShader.Get(), nullptr, 0);
        ++MStats.drawCalls;
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
        UnbindShaderResources();
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
        UnbindShaderResources();
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
        // The offscreen pass clears with the packet colour, not black.
        const float ScopedClear[4] = {static_cast<float>(InPacket.clear.R()) / 255.0f,
                                      static_cast<float>(InPacket.clear.G()) / 255.0f,
                                      static_cast<float>(InPacket.clear.B()) / 255.0f,
                                      static_cast<float>(InPacket.clear.A()) / 255.0f};
        MContext->ClearRenderTargetView(ScopedView->second.Get(), ScopedClear);
        UnbindShaderResources();
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
        if (AccessibilityWanted(InPacket))
        {
            ApplyAccessibility(InHandle, InPacket, ScopedSize->second);
        }
        UnbindShaderResources();
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        const D3D11_VIEWPORT ScopedMainViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedMainViewport);
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
        const auto ScopedSourceDepth = MTargetDepths.find(InSource.id);
        const auto ScopedDestinationDepth = MTargetDepths.find(InDestination.id);
        if (MContext == nullptr || ScopedSourceDepth == MTargetDepths.end() ||
            ScopedDestinationDepth == MTargetDepths.end())
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC ScopedSourceDesc{};
        D3D11_TEXTURE2D_DESC ScopedDestinationDesc{};
        ScopedSourceDepth->second->GetDesc(&ScopedSourceDesc);
        ScopedDestinationDepth->second->GetDesc(&ScopedDestinationDesc);
        if (ScopedSourceDesc.Width != ScopedDestinationDesc.Width ||
            ScopedSourceDesc.Height != ScopedDestinationDesc.Height ||
            ScopedSourceDesc.Format != ScopedDestinationDesc.Format)
        {
            return false;
        }
        MContext->CopyResource(ScopedDestinationDepth->second.Get(), ScopedSourceDepth->second.Get());
        return true;
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
        if (ScopedDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && ScopedDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            ScopedDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
            ScopedDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
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
        const bool ScopedSwap = ScopedDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                                ScopedDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
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
        const auto ScopedView = MRtvs.find(InTarget.id);
        const auto ScopedSize = MTargetSizes.find(InTarget.id);
        if (MContext == nullptr || MMotionVertexBuffer == nullptr || ScopedView == MRtvs.end() ||
            ScopedSize == MTargetSizes.end() || InCount == 0 || MMotionItems.empty())
        {
            return false;
        }
        const std::uint32_t ScopedCount = std::min(InCount, static_cast<std::uint32_t>(MMotionItems.size()));
        if (static_cast<std::size_t>(ScopedCount) * 6u > MVertexCapacity)
        {
            LogError("d3d11", "motion buffer holds {} vertices, packet needs {}", MVertexCapacity,
                     static_cast<std::size_t>(ScopedCount) * 6u);
            return false;
        }
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
        const float ScopedPreviousZoom = InPreviousCameraZoom > 0.0f ? InPreviousCameraZoom : ScopedZoom;
        constexpr float ScopedCorners[6][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
                                               {-1.0f, -1.0f}, {1.0f, 1.0f},  {-1.0f, 1.0f}};
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            const MotionVectorItem& ScopedItem = MMotionItems[ScopedIndex];
            const float ScopedCos = std::cos(ScopedItem.rotation);
            const float ScopedSin = std::sin(ScopedItem.rotation);
            const float ScopedPreviousCos = std::cos(ScopedItem.previousRotation);
            const float ScopedPreviousSin = std::sin(ScopedItem.previousRotation);
            for (std::uint32_t ScopedCorner = 0; ScopedCorner < 6u; ++ScopedCorner)
            {
                const float ScopedLocalX = ScopedCorners[ScopedCorner][0] * ScopedItem.size.X * 0.5f;
                const float ScopedLocalY = ScopedCorners[ScopedCorner][1] * ScopedItem.size.Y * 0.5f;
                const float ScopedPreviousLocalX =
                    ScopedCorners[ScopedCorner][0] * ScopedItem.previousSize.X * 0.5f;
                const float ScopedPreviousLocalY =
                    ScopedCorners[ScopedCorner][1] * ScopedItem.previousSize.Y * 0.5f;
                const float ScopedWorldX =
                    ScopedItem.position.X + ScopedLocalX * ScopedCos - ScopedLocalY * ScopedSin;
                const float ScopedWorldY =
                    ScopedItem.position.Y + ScopedLocalX * ScopedSin + ScopedLocalY * ScopedCos;
                const float ScopedPreviousWorldX = ScopedItem.previousPosition.X +
                                                   ScopedPreviousLocalX * ScopedPreviousCos -
                                                   ScopedPreviousLocalY * ScopedPreviousSin;
                const float ScopedPreviousWorldY = ScopedItem.previousPosition.Y +
                                                   ScopedPreviousLocalX * ScopedPreviousSin +
                                                   ScopedPreviousLocalY * ScopedPreviousCos;
                const float ScopedCurrentScreenX = (ScopedWorldX - InCameraCenter.X) * ScopedZoom + InJitter.X;
                const float ScopedCurrentScreenY = (ScopedWorldY - InCameraCenter.Y) * ScopedZoom + InJitter.Y;
                const float ScopedPreviousScreenX =
                    (ScopedPreviousWorldX - InPreviousCameraCenter.X) * ScopedPreviousZoom + InPreviousJitter.X;
                const float ScopedPreviousScreenY =
                    (ScopedPreviousWorldY - InPreviousCameraCenter.Y) * ScopedPreviousZoom + InPreviousJitter.Y;
                MotionVertex& ScopedVertex = ScopedVertices[ScopedVertexCount++];
                ScopedVertex.Position[0] = ScopedCurrentScreenX / (ScopedViewportX * 0.5f);
                ScopedVertex.Position[1] = -ScopedCurrentScreenY / (ScopedViewportY * 0.5f);
                ScopedVertex.Position[2] = 0.0f;
                // Normalized UV delta current - previous, matching motion.vert.
                ScopedVertex.Delta[0] = (ScopedCurrentScreenX - ScopedPreviousScreenX) / ScopedViewportX;
                ScopedVertex.Delta[1] = (ScopedCurrentScreenY - ScopedPreviousScreenY) / ScopedViewportY;
            }
        }
        MContext->Unmap(MMotionVertexBuffer.Get(), 0);
        const float ScopedClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        MContext->ClearRenderTargetView(ScopedView->second.Get(), ScopedClear);
        UnbindShaderResources();
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
        ++MStats.drawCalls;
        UnbindShaderResources();
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        MContext->OMSetDepthStencilState(MDepthState.Get(), 0);
        MContext->OMSetBlendState(MBlendState.Get(), nullptr, 0xFFFFFFFFu);
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(std::max(MExtent.width, 1u)),
                                            static_cast<float>(std::max(MExtent.height, 1u)), 0.0f, 1.0f};
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
        float NormalMeta = 0.0f;
    };

    struct MotionVertex
    {
        float Position[3];
        float Delta[2];
    };

    bool CreateBuiltInAtlas()
    {
        const std::vector<std::byte> ScopedPixels = BuildBuiltInAtlasPixels(128, 4);
        TextureArrayDesc ScopedDesc;
        ScopedDesc.size = {128, 128};
        ScopedDesc.layers = 4;
        ScopedDesc.srgb = false;
        ScopedDesc.format = Fby::Assets::TextureFormat::Rgba8;
        const TextureArrayHandle ScopedHandle = CreateTextureArray(ScopedDesc, ScopedPixels);
        if (!ScopedHandle.IsValid())
        {
            LogError("d3d11", "built-in atlas creation failed");
            return false;
        }
        MAtlasId = ScopedHandle.id;
        return true;
    }

    bool CreatePostFxResources()
    {
        if (MDevice == nullptr)
        {
            return false;
        }
        MPostFxVertexShader.Reset();
        MPostFxPixelShader.Reset();
        MPostFxConstants.Reset();
        MSceneSrv.Reset();
        MSceneRtv.Reset();
        MSceneTexture.Reset();
        MSceneDepthView.Reset();
        MSceneDepth.Reset();

        D3D11_TEXTURE2D_DESC ScopedSceneDesc{};
        ScopedSceneDesc.Width = std::max(MExtent.width, 1u);
        ScopedSceneDesc.Height = std::max(MExtent.height, 1u);
        ScopedSceneDesc.MipLevels = 1;
        ScopedSceneDesc.ArraySize = 1;
        ScopedSceneDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ScopedSceneDesc.SampleDesc.Count = 1;
        ScopedSceneDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedSceneDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(MDevice->CreateTexture2D(&ScopedSceneDesc, nullptr, &MSceneTexture)) ||
            FAILED(MDevice->CreateRenderTargetView(MSceneTexture.Get(), nullptr, &MSceneRtv)) ||
            FAILED(MDevice->CreateShaderResourceView(MSceneTexture.Get(), nullptr, &MSceneSrv)))
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC ScopedDepthDesc = ScopedSceneDesc;
        ScopedDepthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        ScopedDepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(MDevice->CreateTexture2D(&ScopedDepthDesc, nullptr, &MSceneDepth)) ||
            FAILED(MDevice->CreateDepthStencilView(MSceneDepth.Get(), nullptr, &MSceneDepthView)))
        {
            return false;
        }

        if (FAILED(MDevice->CreateVertexShader(Shaders::Dx::D3D11PostFxVsSm5.data(),
                                               Shaders::Dx::D3D11PostFxVsSm5.size(), nullptr, &MPostFxVertexShader)) ||
            FAILED(MDevice->CreatePixelShader(Shaders::Dx::D3D11PostFxPsSm5.data(),
                                              Shaders::Dx::D3D11PostFxPsSm5.size(), nullptr, &MPostFxPixelShader)))
        {
            return false;
        }
        D3D11_BUFFER_DESC ScopedConstantDesc{};
        ScopedConstantDesc.ByteWidth = 512;
        ScopedConstantDesc.Usage = D3D11_USAGE_DYNAMIC;
        ScopedConstantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ScopedConstantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(MDevice->CreateBuffer(&ScopedConstantDesc, nullptr, &MPostFxConstants)))
        {
            return false;
        }
        MPostFxReady = true;
        LogInfo("d3d11", "post-FX composite ready ({}x{})", MExtent.width, MExtent.height);
        (void)CreateBloomResources();
        (void)CreateColorGradeResources();
        return true;
    }

    bool CreateBloomResources()
    {
        MBloomPrefilterShader.Reset();
        MBloomBlurShader.Reset();
        MBloomConstants.Reset();
        for (ComPtr<ID3D11ShaderResourceView>& ScopedSrv : MBloomSrvs)
        {
            ScopedSrv.Reset();
        }
        for (ComPtr<ID3D11UnorderedAccessView>& ScopedUav : MBloomUavs)
        {
            ScopedUav.Reset();
        }
        for (ComPtr<ID3D11Texture2D>& ScopedTexture : MBloomTextures)
        {
            ScopedTexture.Reset();
        }
        MBloomReady = false;
        if (MDevice == nullptr)
        {
            return false;
        }
        MBloomWidth = std::max(MExtent.width / 2u, 1u);
        MBloomHeight = std::max(MExtent.height / 2u, 1u);
        D3D11_TEXTURE2D_DESC ScopedDesc{};
        ScopedDesc.Width = MBloomWidth;
        ScopedDesc.Height = MBloomHeight;
        ScopedDesc.MipLevels = 1;
        ScopedDesc.ArraySize = 1;
        ScopedDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ScopedDesc.SampleDesc.Count = 1;
        ScopedDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2u; ++ScopedIndex)
        {
            if (FAILED(MDevice->CreateTexture2D(&ScopedDesc, nullptr, &MBloomTextures[ScopedIndex])) ||
                FAILED(MDevice->CreateShaderResourceView(MBloomTextures[ScopedIndex].Get(), nullptr,
                                                        &MBloomSrvs[ScopedIndex])) ||
                FAILED(MDevice->CreateUnorderedAccessView(MBloomTextures[ScopedIndex].Get(), nullptr,
                                                          &MBloomUavs[ScopedIndex])))
            {
                return false;
            }
        }
        if (FAILED(MDevice->CreateComputeShader(Shaders::Dx::D3D11BloomPrefilterCsSm5.data(),
                                                Shaders::Dx::D3D11BloomPrefilterCsSm5.size(), nullptr,
                                                &MBloomPrefilterShader)) ||
            FAILED(MDevice->CreateComputeShader(Shaders::Dx::D3D11BloomBlurCsSm5.data(),
                                                Shaders::Dx::D3D11BloomBlurCsSm5.size(), nullptr,
                                                &MBloomBlurShader)))
        {
            return false;
        }
        D3D11_BUFFER_DESC ScopedConstantDesc{};
        ScopedConstantDesc.ByteWidth = 512;
        ScopedConstantDesc.Usage = D3D11_USAGE_DYNAMIC;
        ScopedConstantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ScopedConstantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(MDevice->CreateBuffer(&ScopedConstantDesc, nullptr, &MBloomConstants)))
        {
            return false;
        }
        MBloomReady = true;
        LogInfo("d3d11", "bloom chain ready ({}x{})", MBloomWidth, MBloomHeight);
        return true;
    }

    void RecordBloomChain(const PostFxConstants& InBlock)
    {
        if (!MBloomReady || MContext == nullptr)
        {
            return;
        }
        if (!MBloomLogged)
        {
            MBloomLogged = true;
            LogInfo("d3d11", "bloom chain executed");
        }
        const auto ScopedDispatch = [this](ID3D11ComputeShader* InShader, ID3D11ShaderResourceView* InSource,
                                           ID3D11UnorderedAccessView* InTarget, const PostFxConstants& InConstants)
        {
            D3D11_MAPPED_SUBRESOURCE ScopedMapped{};
            if (FAILED(MContext->Map(MBloomConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedMapped)))
            {
                return;
            }
            std::memcpy(ScopedMapped.pData, &InConstants, sizeof(InConstants));
            MContext->Unmap(MBloomConstants.Get(), 0);
            // A resource may not be bound as an input and an output at the same
            // time, so the previous pass' pair is cleared before the new one.
            ID3D11UnorderedAccessView* ScopedNoTarget[] = {nullptr};
            MContext->CSSetUnorderedAccessViews(0, 1, ScopedNoTarget, nullptr);
            ID3D11ShaderResourceView* ScopedNoSource[] = {nullptr};
            MContext->CSSetShaderResources(0, 1, ScopedNoSource);
            ID3D11ShaderResourceView* ScopedSrvs[] = {InSource};
            MContext->CSSetShaderResources(0, 1, ScopedSrvs);
            ID3D11UnorderedAccessView* ScopedUavs[] = {InTarget};
            MContext->CSSetUnorderedAccessViews(0, 1, ScopedUavs, nullptr);
            MContext->CSSetShader(InShader, nullptr, 0);
            MContext->CSSetSamplers(0, 1, MSampler.GetAddressOf());
            MContext->CSSetConstantBuffers(0, 1, MBloomConstants.GetAddressOf());
            MContext->Dispatch((MBloomWidth + 15u) / 16u, (MBloomHeight + 15u) / 16u, 1);
            ID3D11UnorderedAccessView* ScopedNone[] = {nullptr};
            MContext->CSSetUnorderedAccessViews(0, 1, ScopedNone, nullptr);
            ID3D11ShaderResourceView* ScopedNoInput[] = {nullptr};
            MContext->CSSetShaderResources(0, 1, ScopedNoInput);
            MContext->CSSetShader(nullptr, nullptr, 0);
        };
        PostFxConstants ScopedPrefilter = InBlock;
        ScopedPrefilter.grainAndFlash.Y = 0.0f;
        PostFxConstants ScopedHorizontal = InBlock;
        ScopedHorizontal.grainAndFlash.Y = 1.0f;
        PostFxConstants ScopedVertical = InBlock;
        ScopedVertical.grainAndFlash.Y = 0.0f;
        // The horizontal and vertical passes both read the copy in text B and
        // write text A, so a resource is never bound as an input and an output
        // at the same time; the two copies only touch unbound resources.
        ScopedDispatch(MBloomPrefilterShader.Get(), MSceneSrv.Get(), MBloomUavs[0].Get(), ScopedPrefilter);
        MContext->CopyResource(MBloomTextures[1].Get(), MBloomTextures[0].Get());
        ScopedDispatch(MBloomBlurShader.Get(), MBloomSrvs[1].Get(), MBloomUavs[0].Get(), ScopedHorizontal);
        MContext->CopyResource(MBloomTextures[1].Get(), MBloomTextures[0].Get());
        ScopedDispatch(MBloomBlurShader.Get(), MBloomSrvs[1].Get(), MBloomUavs[0].Get(), ScopedVertical);
        MBloomRanThisFrame = true;
    }

    bool CreateColorGradeResources()
    {
        MColorGradeVertexShader.Reset();
        MColorGradePixelShader.Reset();
        MColorGradeConstants.Reset();
        MColorGradeSrv.Reset();
        MColorGradeScratch.Reset();
        MColorGradeReady = false;
        if (MDevice == nullptr)
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC ScopedDesc{};
        ScopedDesc.Width = std::max(MExtent.width, 1u);
        ScopedDesc.Height = std::max(MExtent.height, 1u);
        ScopedDesc.MipLevels = 1;
        ScopedDesc.ArraySize = 1;
        ScopedDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ScopedDesc.SampleDesc.Count = 1;
        ScopedDesc.Usage = D3D11_USAGE_DEFAULT;
        ScopedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(MDevice->CreateTexture2D(&ScopedDesc, nullptr, &MColorGradeScratch)) ||
            FAILED(MDevice->CreateShaderResourceView(MColorGradeScratch.Get(), nullptr, &MColorGradeSrv)))
        {
            return false;
        }
        if (FAILED(MDevice->CreatePixelShader(Shaders::Dx::D3D11BlitPsSm5.data(),
                                              Shaders::Dx::D3D11BlitPsSm5.size(), nullptr, &MBlitPixelShader)) ||
            FAILED(MDevice->CreatePixelShader(Shaders::Dx::D3D11HudCompositePsSm5.data(),
                                              Shaders::Dx::D3D11HudCompositePsSm5.size(), nullptr, &MHudPixelShader)) ||
            FAILED(MDevice->CreatePixelShader(Shaders::Dx::D3D11UpscalePsSm5.data(),
                                              Shaders::Dx::D3D11UpscalePsSm5.size(), nullptr, &MUpscalePixelShader)) ||
            FAILED(MDevice->CreatePixelShader(Shaders::Dx::D3D11AccessibilityPsSm5.data(),
                                              Shaders::Dx::D3D11AccessibilityPsSm5.size(), nullptr,
                                              &MAccessibilityPixelShader)) ||
            FAILED(MDevice->CreateComputeShader(Shaders::Dx::D3D11AccessibilityFlashCsSm5.data(),
                                                Shaders::Dx::D3D11AccessibilityFlashCsSm5.size(), nullptr,
                                                &MAccessibilityFlashShader)) ||
            FAILED(MDevice->CreateVertexShader(Shaders::Dx::D3D11ColorGradingVsSm5.data(),
                                               Shaders::Dx::D3D11ColorGradingVsSm5.size(), nullptr,
                                               &MColorGradeVertexShader)) ||
            FAILED(MDevice->CreatePixelShader(Shaders::Dx::D3D11ColorGradingPsSm5.data(),
                                              Shaders::Dx::D3D11ColorGradingPsSm5.size(), nullptr,
                                              &MColorGradePixelShader)))
        {
            return false;
        }
        D3D11_BUFFER_DESC ScopedConstantDesc{};
        ScopedConstantDesc.ByteWidth = 32;
        ScopedConstantDesc.Usage = D3D11_USAGE_DYNAMIC;
        ScopedConstantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ScopedConstantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(MDevice->CreateBuffer(&ScopedConstantDesc, nullptr, &MColorGradeConstants)))
        {
            return false;
        }
        D3D11_BUFFER_DESC ScopedHudDesc{};
        ScopedHudDesc.ByteWidth = 16;
        ScopedHudDesc.Usage = D3D11_USAGE_DYNAMIC;
        ScopedHudDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ScopedHudDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(MDevice->CreateBuffer(&ScopedHudDesc, nullptr, &MHudConstants)))
        {
            return false;
        }
        if (FAILED(MDevice->CreateBuffer(&ScopedHudDesc, nullptr, &MUpscaleConstants)))
        {
            return false;
        }
        D3D11_BUFFER_DESC ScopedAccessibilityDesc = ScopedHudDesc;
        ScopedAccessibilityDesc.ByteWidth = 80;
        if (FAILED(MDevice->CreateBuffer(&ScopedAccessibilityDesc, nullptr, &MAccessibilityConstants)))
        {
            return false;
        }
        MColorGradeReady = true;
        LogInfo("d3d11", "colour grading pass ready");
        return true;
    }

    void RecordColorGrade()
    {
        if (!MColorGradePending)
        {
            return;
        }
        MColorGradePending = false;
        if (!MColorGradeReady || MContext == nullptr || MColorGradeScratch == nullptr)
        {
            return;
        }
        const std::uint32_t ScopedLutId = static_cast<std::uint32_t>(MColorGradeBlock.params1.Y);
        const auto ScopedLut = MTextureViews.find(ScopedLutId);
        if (ScopedLut == MTextureViews.end())
        {
            if (!MColorGradeLogged)
            {
                MColorGradeLogged = true;
                LogWarn("d3d11", "colour grading skipped: LUT texture {} is not resident", ScopedLutId);
            }
            return;
        }
        if (!MColorGradeLogged)
        {
            MColorGradeLogged = true;
            LogInfo("d3d11", "colour grading pass executed");
        }
        // A resource bound as a render target cannot be copied from.
        UnbindShaderResources();
        MContext->OMSetRenderTargets(0, nullptr, nullptr);
        MContext->CopyResource(MColorGradeScratch.Get(), MBackBuffer.Get());
        D3D11_MAPPED_SUBRESOURCE ScopedMapped{};
        if (FAILED(MContext->Map(MColorGradeConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedMapped)))
        {
            return;
        }
        std::memcpy(ScopedMapped.pData, &MColorGradeBlock, sizeof(MColorGradeBlock));
        MContext->Unmap(MColorGradeConstants.Get(), 0);

        UnbindShaderResources();
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(std::max(MExtent.width, 1u)),
                                            static_cast<float>(std::max(MExtent.height, 1u)), 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        MContext->IASetInputLayout(nullptr);
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MColorGradeVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MColorGradePixelShader.Get(), nullptr, 0);
        MContext->PSSetSamplers(0, 1, MSampler.GetAddressOf());
        MContext->PSSetConstantBuffers(0, 1, MColorGradeConstants.GetAddressOf());
        ID3D11ShaderResourceView* ScopedViews[] = {MColorGradeSrv.Get(), ScopedLut->second.Get()};
        MContext->PSSetShaderResources(0, 2, ScopedViews);
        MContext->Draw(3, 0);
        MContext->OMSetDepthStencilState(MDepthState.Get(), 0);
        MContext->OMSetBlendState(MBlendState.Get(), nullptr, 0xFFFFFFFFu);
        MContext->IASetInputLayout(MInputLayout.Get());
        MContext->VSSetShader(MVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MPixelShader.Get(), nullptr, 0);
        ++MStats.drawCalls;
    }

    void RecordPostFxComposite()
    {
        if (!MPostFxPending || !MPostFxReady || MContext == nullptr)
        {
            return;
        }
        MPostFxPending = false;
        if (!MPostFxLogged)
        {
            MPostFxLogged = true;
            LogInfo("d3d11", "post-FX composite executed");
        }
        // Submit leaves the scene bound as the render target; the bloom compute
        // samples that same texture, so the output binding is cleared first.
        MContext->OMSetRenderTargets(0, nullptr, nullptr);
        PostFxConstants ScopedBlock =
            MPostFxGraphPending ? MPostFxGraphBlock : BuildPostFxConstants(MPostFxPendingSettings, MExtent, MStats.frameIndex);
        MPostFxGraphPending = false;
        const std::uint32_t ScopedFlags = static_cast<std::uint32_t>(ScopedBlock.flagsAndCrt.X);
        if ((ScopedFlags & PostFxBloom) != 0u && MBloomReady)
        {
            RecordBloomChain(ScopedBlock);
        }
        if (!MBloomRanThisFrame)
        {
            ScopedBlock.flagsAndCrt.X =
                static_cast<float>(ScopedFlags & ~static_cast<std::uint32_t>(PostFxBloom));
        }
        D3D11_MAPPED_SUBRESOURCE ScopedMapped{};
        if (FAILED(MContext->Map(MPostFxConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ScopedMapped)))
        {
            return;
        }
        std::memcpy(ScopedMapped.pData, &ScopedBlock, sizeof(ScopedBlock));
        MContext->Unmap(MPostFxConstants.Get(), 0);

        UnbindShaderResources();
        MContext->OMSetRenderTargets(1, MRtv.GetAddressOf(), MDepthView.Get());
        const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, static_cast<float>(std::max(MExtent.width, 1u)),
                                            static_cast<float>(std::max(MExtent.height, 1u)), 0.0f, 1.0f};
        MContext->RSSetViewports(1, &ScopedViewport);
        MContext->IASetInputLayout(nullptr);
        MContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        MContext->VSSetShader(MPostFxVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MPostFxPixelShader.Get(), nullptr, 0);
        MContext->PSSetSamplers(0, 1, MSampler.GetAddressOf());
        MContext->PSSetConstantBuffers(0, 1, MPostFxConstants.GetAddressOf());
        ID3D11ShaderResourceView* ScopedViews[] = {
            MSceneSrv.Get(), MBloomRanThisFrame ? MBloomSrvs[0].Get() : MSceneSrv.Get()};
        MContext->PSSetShaderResources(0, 2, ScopedViews);
        MContext->Draw(3, 0);
        MContext->OMSetDepthStencilState(MDepthState.Get(), 0);
        MContext->OMSetBlendState(MBlendState.Get(), nullptr, 0xFFFFFFFFu);
        MContext->IASetInputLayout(MInputLayout.Get());
        MContext->VSSetShader(MVertexShader.Get(), nullptr, 0);
        MContext->PSSetShader(MPixelShader.Get(), nullptr, 0);
        ++MStats.drawCalls;
    }

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
        const void* ScopedBasePSData = Shaders::Dx::D3D11SpriteBasePsSm5.data();
        SIZE_T ScopedBasePSSize = Shaders::Dx::D3D11SpriteBasePsSm5.size();
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
            {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        if (FAILED(MDevice->CreateVertexShader(ScopedVSData, ScopedVSSize, nullptr, &MVertexShader)) ||
            FAILED(MDevice->CreatePixelShader(ScopedPSData, ScopedPSSize, nullptr, &MPixelShader)) ||
            FAILED(MDevice->CreatePixelShader(ScopedBasePSData, ScopedBasePSSize, nullptr, &MBasePixelShader)) ||
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
        // 2D sprites are sorted on the CPU; depth test and writes would reject
        // every quad after the first one at the same z.
        ScopedDepthState.DepthEnable = FALSE;
        ScopedDepthState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ScopedDepthState.DepthFunc = D3D11_COMPARISON_ALWAYS;
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
        D3D11_BUFFER_DESC ScopedLightBuffer{};
        ScopedLightBuffer.ByteWidth = static_cast<UINT>(sizeof(GpuLight) * 64u);
        ScopedLightBuffer.Usage = D3D11_USAGE_DYNAMIC;
        ScopedLightBuffer.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ScopedLightBuffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ScopedLightBuffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        ScopedLightBuffer.StructureByteStride = sizeof(GpuLight);
        if (FAILED(MDevice->CreateBuffer(&ScopedLightBuffer, nullptr, &MSpriteLightBuffer)) ||
            FAILED(MDevice->CreateShaderResourceView(MSpriteLightBuffer.Get(), nullptr, &MSpriteLightSrv)))
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
float4 PSMain(VSOutput input) : SV_Target { return float4(input.delta, 0.0, 1.0); }
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
    float4 history = HistoryColor.Sample(TemporalSampler, input.uv - motion);
    float currentDepth = CurrentDepth.Sample(TemporalSampler, input.uv).r;
    float historyDepth = HistoryDepth.Sample(TemporalSampler, input.uv - motion).r;
    float confidence = 1.0 - saturate(length(motion) * 8.0);
    float historyWeight = ResetHistory > 0.5 ? 0.0 : HistoryWeight * confidence;
    if (abs(currentDepth - historyDepth) > DepthThreshold)
    {
        historyWeight = 0.0;
    }
    return lerp(history, current, 1.0 - historyWeight);
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
    DeviceDesc MDesc{};
    FrameStats MStats{};
    Extent2D MExtent{};
    std::uint32_t MAtlasId = 0xFFFFFFFFu;
    // The packet clear colour is only known at Submit, so the back buffer clear
    // is deferred from BeginFrame to the first Submit of the frame.
    bool MMainClearPending = false;
    // Post-FX composite: the scene renders offscreen and one fullscreen draw
    // applies CRT/vignette/chromatic/accessibility effects.
    ComPtr<ID3D11Texture2D> MSceneTexture;
    ComPtr<ID3D11RenderTargetView> MSceneRtv;
    ComPtr<ID3D11ShaderResourceView> MSceneSrv;
    ComPtr<ID3D11Texture2D> MSceneDepth;
    ComPtr<ID3D11DepthStencilView> MSceneDepthView;
    ComPtr<ID3D11VertexShader> MPostFxVertexShader;
    ComPtr<ID3D11PixelShader> MPostFxPixelShader;
    ComPtr<ID3D11Buffer> MPostFxConstants;
    PostFxSettings MPostFxPendingSettings{};
    bool MPostFxReady = false;
    bool MPostFxPending = false;
    bool MPostFxLogged = false;
    PostFxConstants MPostFxGraphBlock{};
    bool MPostFxGraphPending = false;
    // Colour grading: a copy of the final back buffer re-rendered through the LUT.
    ComPtr<ID3D11PixelShader> MBlitPixelShader;
    ComPtr<ID3D11PixelShader> MHudPixelShader;
    ComPtr<ID3D11PixelShader> MUpscalePixelShader;
    ComPtr<ID3D11PixelShader> MAccessibilityPixelShader;
    ComPtr<ID3D11ComputeShader> MAccessibilityFlashShader;
    ComPtr<ID3D11Buffer> MAccessibilityConstants;
    struct AccessibilityResources
    {
        ComPtr<ID3D11Texture2D> Scene;
        ComPtr<ID3D11ShaderResourceView> SceneSrv;
        std::array<ComPtr<ID3D11Texture2D>, 2> History;
        std::array<ComPtr<ID3D11ShaderResourceView>, 2> HistorySrv;
        std::array<ComPtr<ID3D11UnorderedAccessView>, 2> HistoryUav;
        std::uint32_t ReadIndex = 0;
    };
    std::unordered_map<std::uint32_t, AccessibilityResources> MAccessibilityResources;
    ComPtr<ID3D11Buffer> MUpscaleConstants;
    ComPtr<ID3D11Buffer> MHudConstants;
    ComPtr<ID3D11Texture2D> MColorGradeScratch;
    ComPtr<ID3D11ShaderResourceView> MColorGradeSrv;
    ComPtr<ID3D11VertexShader> MColorGradeVertexShader;
    ComPtr<ID3D11PixelShader> MColorGradePixelShader;
    ComPtr<ID3D11Buffer> MColorGradeConstants;
    ColorGradingConstants MColorGradeBlock{};
    bool MColorGradeReady = false;
    bool MColorGradePending = false;
    bool MColorGradeLogged = false;
    // Bloom chain: scene -> half-res prefilter -> separable blur.
    std::array<ComPtr<ID3D11Texture2D>, 2> MBloomTextures;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> MBloomSrvs;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2> MBloomUavs;
    ComPtr<ID3D11ComputeShader> MBloomPrefilterShader;
    ComPtr<ID3D11ComputeShader> MBloomBlurShader;
    ComPtr<ID3D11Buffer> MBloomConstants;
    std::uint32_t MBloomWidth = 0;
    std::uint32_t MBloomHeight = 0;
    bool MBloomReady = false;
    bool MBloomRanThisFrame = false;
    bool MBloomLogged = false;
    ComPtr<ID3D11Device> MDevice;
    ComPtr<ID3D11InfoQueue> MInfoQueue;
    bool MDebugLayer = false;
    ComPtr<ID3D11DeviceContext> MContext;
    ComPtr<IDXGISwapChain> MSwapchain;
    ComPtr<ID3D11Texture2D> MBackBuffer;
    RenderTargetHandle MHudTarget;
    bool MUIVisible = true;
    bool MHudUpscaleNearest = false;
    ComPtr<ID3D11RenderTargetView> MRtv;
    ComPtr<ID3D11VertexShader> MVertexShader;
    ComPtr<ID3D11PixelShader> MPixelShader;
    ComPtr<ID3D11PixelShader> MBasePixelShader;
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
    ComPtr<ID3D11Buffer> MSpriteLightBuffer;
    ComPtr<ID3D11ShaderResourceView> MSpriteLightSrv;
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
    std::unordered_map<std::uint32_t, ComPtr<ID3D11ShaderResourceView>> MTargetDepthSrvs;
    std::unordered_map<std::uint32_t, Fby::Rhi::Extent2D> MTargetSizes;
    std::unordered_map<std::uint32_t, RenderBlendMode> MTargetBlend;
    std::uint32_t MNextTargetId = 1;
    struct TextureArrayInfo
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t Mips = 1;
        std::uint32_t Layers = 1;
        Fby::Assets::TextureFormat Format = Fby::Assets::TextureFormat::Rgba8;
    };
    std::unordered_map<std::uint32_t, TextureArrayInfo> MTextureInfos;
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
