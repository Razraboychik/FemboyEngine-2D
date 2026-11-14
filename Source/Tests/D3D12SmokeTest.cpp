// FemboyEngine2D - D3D12 swapchain clear/present smoke test.
#include "Engine/Core/Log.hpp"

#include <cstdio>

#if defined(_WIN32)
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace
{
using Microsoft::WRL::ComPtr;

LRESULT CALLBACK SmokeWindowProc(HWND InWindow, UINT InMessage, WPARAM InWParam, LPARAM InLParam)
{
    if (InMessage == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(InWindow, InMessage, InWParam, InLParam);
}

struct SmokeContext
{
    HWND Window = nullptr;
    ComPtr<IDXGIFactory4> Factory;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<IDXGISwapChain3> Swapchain;
    ComPtr<ID3D12DescriptorHeap> RtvHeap;
    ComPtr<ID3D12Resource> BackBuffers[2];
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> Commands;
    ComPtr<ID3D12Fence> Fence;
    HANDLE FenceEvent = nullptr;
    std::uint64_t FenceValue = 0;
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> Pipeline;
    ComPtr<ID3D12Resource> VertexBuffer;
    std::uint32_t RtvSize = 0;
};
}

int main()
{
    Fby::SetLogLevel(Fby::LogLevel::Warn);
    SmokeContext ScopedContext;
    WNDCLASSEXW ScopedClass{};
    ScopedClass.cbSize = sizeof(ScopedClass);
    ScopedClass.lpfnWndProc = SmokeWindowProc;
    ScopedClass.hInstance = GetModuleHandleW(nullptr);
    ScopedClass.lpszClassName = L"FbyD3D12Smoke";
    RegisterClassExW(&ScopedClass);

    ScopedContext.Window = CreateWindowExW(0, ScopedClass.lpszClassName, L"Fby D3D12 Smoke", WS_OVERLAPPEDWINDOW,
                                           CW_USEDEFAULT, CW_USEDEFAULT, 320, 240, nullptr, nullptr,
                                           ScopedClass.hInstance, nullptr);
    if (ScopedContext.Window == nullptr)
    {
        std::fprintf(stderr, "d3d12_smoke: window creation failed\n");
        return 1;
    }

    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&ScopedContext.Factory))) || ScopedContext.Factory == nullptr)
    {
        std::fprintf(stderr, "d3d12_smoke: DXGI factory unavailable\n");
        DestroyWindow(ScopedContext.Window);
        return 1;
    }

    ComPtr<IDXGIAdapter1> ScopedAdapter;
    for (UINT ScopedIndex = 0; ScopedContext.Factory->EnumAdapters1(ScopedIndex, &ScopedAdapter) != DXGI_ERROR_NOT_FOUND;
         ++ScopedIndex)
    {
        DXGI_ADAPTER_DESC1 ScopedDesc{};
        if (FAILED(ScopedAdapter->GetDesc1(&ScopedDesc)))
        {
            continue;
        }
        if (FAILED(D3D12CreateDevice(ScopedAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(&ScopedContext.Device))))
        {
            continue;
        }
        std::printf("d3d12_smoke: adapter=%ls\n", ScopedDesc.Description);
        break;
    }
    if (ScopedContext.Device == nullptr)
    {
        std::fprintf(stderr, "d3d12_smoke: no D3D12 device\n");
        DestroyWindow(ScopedContext.Window);
        return 0;
    }

    D3D12_COMMAND_QUEUE_DESC ScopedQueueDesc{};
    ScopedQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(ScopedContext.Device->CreateCommandQueue(&ScopedQueueDesc, IID_PPV_ARGS(&ScopedContext.Queue))) ||
        FAILED(ScopedContext.Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                            IID_PPV_ARGS(&ScopedContext.Allocator))) ||
        FAILED(ScopedContext.Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       ScopedContext.Allocator.Get(), nullptr,
                                                       IID_PPV_ARGS(&ScopedContext.Commands))) ||
        FAILED(ScopedContext.Commands->Close()))
    {
        std::fprintf(stderr, "d3d12_smoke: command objects failed\n");
        DestroyWindow(ScopedContext.Window);
        return 1;
    }

    DXGI_SWAP_CHAIN_DESC1 ScopedSwapDesc{};
    ScopedSwapDesc.Width = 320;
    ScopedSwapDesc.Height = 240;
    ScopedSwapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ScopedSwapDesc.SampleDesc.Count = 1;
    ScopedSwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    ScopedSwapDesc.BufferCount = 2;
    ScopedSwapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> ScopedSwapchain1;
    if (FAILED(ScopedContext.Factory->CreateSwapChainForHwnd(ScopedContext.Queue.Get(), ScopedContext.Window,
                                                             &ScopedSwapDesc, nullptr, nullptr,
                                                             &ScopedSwapchain1)) ||
        FAILED(ScopedSwapchain1.As(&ScopedContext.Swapchain)))
    {
        std::fprintf(stderr, "d3d12_smoke: swapchain failed\n");
        DestroyWindow(ScopedContext.Window);
        return 1;
    }

    D3D12_DESCRIPTOR_HEAP_DESC ScopedHeapDesc{};
    ScopedHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ScopedHeapDesc.NumDescriptors = 2;
    if (FAILED(ScopedContext.Device->CreateDescriptorHeap(&ScopedHeapDesc, IID_PPV_ARGS(&ScopedContext.RtvHeap))))
    {
        std::fprintf(stderr, "d3d12_smoke: RTV heap failed\n");
        DestroyWindow(ScopedContext.Window);
        return 1;
    }
    ScopedContext.RtvSize = ScopedContext.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE ScopedRtv = ScopedContext.RtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        if (FAILED(ScopedContext.Swapchain->GetBuffer(ScopedIndex, IID_PPV_ARGS(&ScopedContext.BackBuffers[ScopedIndex]))))
        {
            std::fprintf(stderr, "d3d12_smoke: backbuffer failed\n");
            DestroyWindow(ScopedContext.Window);
            return 1;
        }
        ScopedContext.Device->CreateRenderTargetView(ScopedContext.BackBuffers[ScopedIndex].Get(), nullptr, ScopedRtv);
        ScopedRtv.ptr += ScopedContext.RtvSize;
    }

    if (FAILED(ScopedContext.Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ScopedContext.Fence))))
    {
        std::fprintf(stderr, "d3d12_smoke: fence failed\n");
        DestroyWindow(ScopedContext.Window);
        return 1;
    }
    ScopedContext.FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    constexpr char ScopedShader[] = R"(
struct VSInput { float3 position : POSITION; float4 color : COLOR; };
struct VSOutput { float4 position : SV_Position; float4 color : COLOR; };
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}
float4 PSMain(VSOutput input) : SV_Target { return input.color; }
)";
    ComPtr<ID3DBlob> ScopedVS;
    ComPtr<ID3DBlob> ScopedPS;
    ComPtr<ID3DBlob> ScopedErrors;
    if (FAILED(D3DCompile(ScopedShader, sizeof(ScopedShader), "smoke.hlsl", nullptr, nullptr, "VSMain", "vs_5_0",
                          0, 0, &ScopedVS, &ScopedErrors)) ||
        FAILED(D3DCompile(ScopedShader, sizeof(ScopedShader), "smoke.hlsl", nullptr, nullptr, "PSMain", "ps_5_0",
                          0, 0, &ScopedPS, &ScopedErrors)))
    {
        std::fprintf(stderr, "d3d12_smoke: shader compile failed\n");
        return 1;
    }
    D3D12_ROOT_SIGNATURE_DESC ScopedRootDesc{};
    ScopedRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> ScopedRootBlob;
    if (FAILED(D3D12SerializeRootSignature(&ScopedRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ScopedRootBlob,
                                           &ScopedErrors)) ||
        FAILED(ScopedContext.Device->CreateRootSignature(0, ScopedRootBlob->GetBufferPointer(),
                                                         ScopedRootBlob->GetBufferSize(),
                                                         IID_PPV_ARGS(&ScopedContext.RootSignature))))
    {
        std::fprintf(stderr, "d3d12_smoke: root signature failed\n");
        return 1;
    }
    const D3D12_INPUT_ELEMENT_DESC ScopedInputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_RASTERIZER_DESC ScopedRasterizer{};
    ScopedRasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    ScopedRasterizer.CullMode = D3D12_CULL_MODE_NONE;
    ScopedRasterizer.DepthClipEnable = TRUE;
    D3D12_BLEND_DESC ScopedBlend{};
    ScopedBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    D3D12_DEPTH_STENCIL_DESC ScopedDepth{};
    ScopedDepth.DepthEnable = FALSE;
    ScopedDepth.StencilEnable = FALSE;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ScopedPso{};
    ScopedPso.pRootSignature = ScopedContext.RootSignature.Get();
    ScopedPso.VS = {ScopedVS->GetBufferPointer(), ScopedVS->GetBufferSize()};
    ScopedPso.PS = {ScopedPS->GetBufferPointer(), ScopedPS->GetBufferSize()};
    ScopedPso.InputLayout = {ScopedInputLayout, static_cast<UINT>(std::size(ScopedInputLayout))};
    ScopedPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    ScopedPso.NumRenderTargets = 1;
    ScopedPso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    ScopedPso.SampleDesc.Count = 1;
    ScopedPso.SampleMask = UINT_MAX;
    ScopedPso.RasterizerState = ScopedRasterizer;
    ScopedPso.BlendState = ScopedBlend;
    ScopedPso.DepthStencilState = ScopedDepth;
    if (FAILED(ScopedContext.Device->CreateGraphicsPipelineState(&ScopedPso, IID_PPV_ARGS(&ScopedContext.Pipeline))))
    {
        std::fprintf(stderr, "d3d12_smoke: pipeline failed\n");
        return 1;
    }
    struct SmokeVertex
    {
        float Position[3];
        float Color[4];
    };
    const SmokeVertex ScopedVertices[3] = {
        {{0.0f, 0.6f, 0.0f}, {1.0f, 0.2f, 0.2f, 1.0f}},
        {{0.55f, -0.5f, 0.0f}, {0.2f, 1.0f, 0.2f, 1.0f}},
        {{-0.55f, -0.5f, 0.0f}, {0.2f, 0.2f, 1.0f, 1.0f}},
    };
    D3D12_HEAP_PROPERTIES ScopedUploadHeap{};
    ScopedUploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC ScopedVertexDesc{};
    ScopedVertexDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ScopedVertexDesc.Width = sizeof(ScopedVertices);
    ScopedVertexDesc.Height = 1;
    ScopedVertexDesc.DepthOrArraySize = 1;
    ScopedVertexDesc.MipLevels = 1;
    ScopedVertexDesc.SampleDesc.Count = 1;
    ScopedVertexDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(ScopedContext.Device->CreateCommittedResource(
            &ScopedUploadHeap, D3D12_HEAP_FLAG_NONE, &ScopedVertexDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&ScopedContext.VertexBuffer))))
    {
        std::fprintf(stderr, "d3d12_smoke: vertex buffer failed\n");
        return 1;
    }
    void* ScopedMapped = nullptr;
    ScopedContext.VertexBuffer->Map(0, nullptr, &ScopedMapped);
    std::memcpy(ScopedMapped, ScopedVertices, sizeof(ScopedVertices));
    ScopedContext.VertexBuffer->Unmap(0, nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE ScopedHandle = ScopedContext.RtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        ScopedContext.Allocator->Reset();
        ScopedContext.Commands->Reset(ScopedContext.Allocator.Get(), nullptr);
        const std::uint32_t ScopedIndex = ScopedContext.Swapchain->GetCurrentBackBufferIndex();
        D3D12_RESOURCE_BARRIER ScopedToRender{};
        ScopedToRender.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ScopedToRender.Transition.pResource = ScopedContext.BackBuffers[ScopedIndex].Get();
        ScopedToRender.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        ScopedToRender.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedToRender.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ScopedContext.Commands->ResourceBarrier(1, &ScopedToRender);
        D3D12_CPU_DESCRIPTOR_HANDLE ScopedFrameRtv = ScopedHandle;
        ScopedFrameRtv.ptr += ScopedIndex * ScopedContext.RtvSize;
        const float ScopedClear[4] = {0.1f, 0.2f, 0.3f, 1.0f};
        ScopedContext.Commands->ClearRenderTargetView(ScopedFrameRtv, ScopedClear, 0, nullptr);
        ScopedContext.Commands->OMSetRenderTargets(1, &ScopedFrameRtv, FALSE, nullptr);
        const D3D12_VIEWPORT ScopedViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        const D3D12_RECT ScopedScissor{0, 0, 320, 240};
        ScopedContext.Commands->RSSetViewports(1, &ScopedViewport);
        ScopedContext.Commands->RSSetScissorRects(1, &ScopedScissor);
        ScopedContext.Commands->SetPipelineState(ScopedContext.Pipeline.Get());
        ScopedContext.Commands->SetGraphicsRootSignature(ScopedContext.RootSignature.Get());
        const D3D12_VERTEX_BUFFER_VIEW ScopedVbv{ScopedContext.VertexBuffer->GetGPUVirtualAddress(),
                                                 sizeof(ScopedVertices), sizeof(SmokeVertex)};
        ScopedContext.Commands->IASetVertexBuffers(0, 1, &ScopedVbv);
        ScopedContext.Commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ScopedContext.Commands->DrawInstanced(3, 1, 0, 0);
        D3D12_RESOURCE_BARRIER ScopedToPresent = ScopedToRender;
        ScopedToPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedToPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        ScopedContext.Commands->ResourceBarrier(1, &ScopedToPresent);
        if (FAILED(ScopedContext.Commands->Close()))
        {
            std::fprintf(stderr, "d3d12_smoke: command close failed\n");
            return 1;
        }
        ID3D12CommandList* ScopedLists[] = {ScopedContext.Commands.Get()};
        ScopedContext.Queue->ExecuteCommandLists(1, ScopedLists);
        if (FAILED(ScopedContext.Swapchain->Present(0, 0)))
        {
            std::fprintf(stderr, "d3d12_smoke: present failed\n");
            return 1;
        }
        ++ScopedContext.FenceValue;
        ScopedContext.Queue->Signal(ScopedContext.Fence.Get(), ScopedContext.FenceValue);
        if (ScopedContext.Fence->GetCompletedValue() < ScopedContext.FenceValue &&
            ScopedContext.FenceEvent != nullptr)
        {
            ScopedContext.Fence->SetEventOnCompletion(ScopedContext.FenceValue, ScopedContext.FenceEvent);
            WaitForSingleObject(ScopedContext.FenceEvent, 2000);
        }
    }
    ScopedContext.Queue->Signal(ScopedContext.Fence.Get(), ScopedContext.FenceValue + 1);
    ScopedContext.Fence->SetEventOnCompletion(ScopedContext.FenceValue + 1, ScopedContext.FenceEvent);
    WaitForSingleObject(ScopedContext.FenceEvent, 2000);
    if (ScopedContext.FenceEvent != nullptr)
    {
        CloseHandle(ScopedContext.FenceEvent);
    }
    DestroyWindow(ScopedContext.Window);
    std::printf("d3d12_smoke: clear=1 triangle=1 present=1 frames=3\n");
    return 0;
}
#else
int main()
{
    std::printf("d3d12_smoke: skipped (non-Windows)\n");
    return 0;
}
#endif
