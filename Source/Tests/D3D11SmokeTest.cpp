// FemboyEngine2D - D3D11 swapchain clear/present smoke test.
#include "Engine/Core/Log.hpp"

#include <cstdio>

#if defined(_WIN32)
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <windows.h>

#include <cstring>
#include <iterator>

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
}

int main()
{
    Fby::SetLogLevel(Fby::LogLevel::Warn);
    WNDCLASSEXW ScopedClass{};
    ScopedClass.cbSize = sizeof(ScopedClass);
    ScopedClass.lpfnWndProc = SmokeWindowProc;
    ScopedClass.hInstance = GetModuleHandleW(nullptr);
    ScopedClass.lpszClassName = L"FbyD3D11Smoke";
    RegisterClassExW(&ScopedClass);
    HWND ScopedWindow = CreateWindowExW(0, ScopedClass.lpszClassName, L"Fby D3D11 Smoke", WS_OVERLAPPEDWINDOW,
                                        CW_USEDEFAULT, CW_USEDEFAULT, 320, 240, nullptr, nullptr,
                                        ScopedClass.hInstance, nullptr);
    if (ScopedWindow == nullptr)
    {
        std::fprintf(stderr, "d3d11_smoke: window creation failed\n");
        return 1;
    }

    ComPtr<IDXGIFactory1> ScopedFactory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&ScopedFactory))) || ScopedFactory == nullptr)
    {
        std::fprintf(stderr, "d3d11_smoke: DXGI factory unavailable\n");
        DestroyWindow(ScopedWindow);
        return 1;
    }
    ComPtr<IDXGIAdapter1> ScopedAdapter;
    if (ScopedFactory->EnumAdapters1(0, &ScopedAdapter) == DXGI_ERROR_NOT_FOUND)
    {
        std::fprintf(stderr, "d3d11_smoke: no adapter\n");
        DestroyWindow(ScopedWindow);
        return 0;
    }

    DXGI_SWAP_CHAIN_DESC ScopedSwapDesc{};
    ScopedSwapDesc.BufferDesc.Width = 320;
    ScopedSwapDesc.BufferDesc.Height = 240;
    ScopedSwapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ScopedSwapDesc.SampleDesc.Count = 1;
    ScopedSwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    ScopedSwapDesc.BufferCount = 2;
    ScopedSwapDesc.OutputWindow = ScopedWindow;
    ScopedSwapDesc.Windowed = TRUE;
    ScopedSwapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ComPtr<ID3D11Device> ScopedDevice;
    ComPtr<ID3D11DeviceContext> ScopedContextDevice;
    ComPtr<IDXGISwapChain> ScopedSwapchain;
    constexpr D3D_FEATURE_LEVEL ScopedLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL ScopedLevel = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDeviceAndSwapChain(
            ScopedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, ScopedLevels,
            static_cast<UINT>(std::size(ScopedLevels)), D3D11_SDK_VERSION, &ScopedSwapDesc, &ScopedSwapchain,
            &ScopedDevice, &ScopedLevel, &ScopedContextDevice)) ||
        ScopedDevice == nullptr)
    {
        std::fprintf(stderr, "d3d11_smoke: device creation failed\n");
        DestroyWindow(ScopedWindow);
        return 1;
    }

    ComPtr<ID3D11Texture2D> ScopedBackBuffer;
    ComPtr<ID3D11RenderTargetView> ScopedRtv;
    if (FAILED(ScopedSwapchain->GetBuffer(0, IID_PPV_ARGS(&ScopedBackBuffer))) ||
        FAILED(ScopedDevice->CreateRenderTargetView(ScopedBackBuffer.Get(), nullptr, &ScopedRtv)))
    {
        std::fprintf(stderr, "d3d11_smoke: RTV failed\n");
        DestroyWindow(ScopedWindow);
        return 1;
    }
    const float ScopedClear[4] = {0.1f, 0.2f, 0.3f, 1.0f};
    ScopedContextDevice->OMSetRenderTargets(1, ScopedRtv.GetAddressOf(), nullptr);

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
    ComPtr<ID3DBlob> ScopedVSBlob;
    ComPtr<ID3DBlob> ScopedPSBlob;
    ComPtr<ID3DBlob> ScopedErrors;
    if (FAILED(D3DCompile(ScopedShader, sizeof(ScopedShader), "smoke11.hlsl", nullptr, nullptr, "VSMain", "vs_5_0",
                          0, 0, &ScopedVSBlob, &ScopedErrors)) ||
        FAILED(D3DCompile(ScopedShader, sizeof(ScopedShader), "smoke11.hlsl", nullptr, nullptr, "PSMain", "ps_5_0",
                          0, 0, &ScopedPSBlob, &ScopedErrors)))
    {
        std::fprintf(stderr, "d3d11_smoke: shader compile failed\n");
        DestroyWindow(ScopedWindow);
        return 1;
    }
    ComPtr<ID3D11VertexShader> ScopedVS;
    ComPtr<ID3D11PixelShader> ScopedPS;
    ComPtr<ID3D11InputLayout> ScopedLayout;
    const D3D11_INPUT_ELEMENT_DESC ScopedInputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(ScopedDevice->CreateVertexShader(ScopedVSBlob->GetBufferPointer(), ScopedVSBlob->GetBufferSize(),
                                                nullptr, &ScopedVS)) ||
        FAILED(ScopedDevice->CreatePixelShader(ScopedPSBlob->GetBufferPointer(), ScopedPSBlob->GetBufferSize(),
                                               nullptr, &ScopedPS)) ||
        FAILED(ScopedDevice->CreateInputLayout(ScopedInputLayout,
                                               static_cast<UINT>(std::size(ScopedInputLayout)),
                                               ScopedVSBlob->GetBufferPointer(), ScopedVSBlob->GetBufferSize(),
                                               &ScopedLayout)))
    {
        std::fprintf(stderr, "d3d11_smoke: pipeline failed\n");
        DestroyWindow(ScopedWindow);
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
    D3D11_BUFFER_DESC ScopedVertexDesc{};
    ScopedVertexDesc.ByteWidth = sizeof(ScopedVertices);
    ScopedVertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ScopedVertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ScopedVertexData{};
    ScopedVertexData.pSysMem = ScopedVertices;
    ComPtr<ID3D11Buffer> ScopedVertexBuffer;
    if (FAILED(ScopedDevice->CreateBuffer(&ScopedVertexDesc, &ScopedVertexData, &ScopedVertexBuffer)))
    {
        std::fprintf(stderr, "d3d11_smoke: vertex buffer failed\n");
        DestroyWindow(ScopedWindow);
        return 1;
    }
    const UINT ScopedStride = sizeof(SmokeVertex);
    const UINT ScopedOffset = 0;
    const D3D11_VIEWPORT ScopedViewport{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
    ScopedContextDevice->RSSetViewports(1, &ScopedViewport);
    ScopedContextDevice->IASetInputLayout(ScopedLayout.Get());
    ScopedContextDevice->IASetVertexBuffers(0, 1, ScopedVertexBuffer.GetAddressOf(), &ScopedStride, &ScopedOffset);
    ScopedContextDevice->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ScopedContextDevice->VSSetShader(ScopedVS.Get(), nullptr, 0);
    ScopedContextDevice->PSSetShader(ScopedPS.Get(), nullptr, 0);

    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        ScopedContextDevice->ClearRenderTargetView(ScopedRtv.Get(), ScopedClear);
        ScopedContextDevice->Draw(3, 0);
        if (FAILED(ScopedSwapchain->Present(0, 0)))
        {
            std::fprintf(stderr, "d3d11_smoke: present failed\n");
            DestroyWindow(ScopedWindow);
            return 1;
        }
    }
    ScopedContextDevice->Flush();
    DestroyWindow(ScopedWindow);
    std::printf("d3d11_smoke: clear=1 triangle=1 present=1 frames=3 feature_level=%x\n", static_cast<unsigned>(ScopedLevel));
    return 0;
}
#else
int main()
{
    std::printf("d3d11_smoke: skipped (non-Windows)\n");
    return 0;
}
#endif
