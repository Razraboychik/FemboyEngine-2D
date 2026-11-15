// FemboyEngine2D - screen, hardware and storage profile implementation.
#include "Engine/Platform/SystemInfo.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <system_error>

#if defined(_WIN32)
#include <dxgi1_6.h>
#include <windows.h>

#include <array>
#else
#include <fstream>
#include <unistd.h>
#endif

namespace Fby::Platform
{
namespace
{

[[nodiscard]] std::string AspectLabelFor(std::uint32_t InWidth, std::uint32_t InHeight)
{
    if (InWidth == 0 || InHeight == 0)
    {
        return {};
    }
    const std::uint32_t ScopedDivisor = std::gcd(InWidth, InHeight);
    return std::to_string(InWidth / ScopedDivisor) + ":" + std::to_string(InHeight / ScopedDivisor);
}

#if defined(_WIN32)
[[nodiscard]] std::string ReadRegistryString(HKEY InRoot, const wchar_t* InPath, const wchar_t* InValue)
{
    HKEY ScopedKey = nullptr;
    if (RegOpenKeyExW(InRoot, InPath, 0, KEY_READ, &ScopedKey) != ERROR_SUCCESS)
    {
        return {};
    }
    std::array<wchar_t, 512> ScopedBuffer{};
    DWORD ScopedBytes = static_cast<DWORD>(ScopedBuffer.size() * sizeof(wchar_t));
    DWORD ScopedType = 0;
    const LONG ScopedResult =
        RegQueryValueExW(ScopedKey, InValue, nullptr, &ScopedType,
                         reinterpret_cast<LPBYTE>(ScopedBuffer.data()), &ScopedBytes);
    RegCloseKey(ScopedKey);
    if (ScopedResult != ERROR_SUCCESS || ScopedType != REG_SZ)
    {
        return {};
    }
    std::wstring ScopedWide(ScopedBuffer.data());
    return std::string(ScopedWide.begin(), ScopedWide.end());
}

[[nodiscard]] std::string CpuName()
{
    const std::string ScopedName =
        ReadRegistryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                           L"ProcessorNameString");
    return ScopedName.empty() ? "Unknown CPU" : ScopedName;
}

void QueryGpu(HardwareInfo* InInfo)
{
    IDXGIFactory1* ScopedFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&ScopedFactory))) || ScopedFactory == nullptr)
    {
        return;
    }
    IDXGIAdapter1* ScopedAdapter = nullptr;
    for (UINT ScopedIndex = 0; ScopedFactory->EnumAdapters1(ScopedIndex, &ScopedAdapter) != DXGI_ERROR_NOT_FOUND;
         ++ScopedIndex)
    {
        DXGI_ADAPTER_DESC1 ScopedDesc{};
        if (ScopedAdapter != nullptr && SUCCEEDED(ScopedAdapter->GetDesc1(&ScopedDesc)) &&
            (ScopedDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
        {
            const std::wstring ScopedWide(ScopedDesc.Description);
            InInfo->gpu = std::string(ScopedWide.begin(), ScopedWide.end());
            InInfo->videoMemoryBytes = ScopedDesc.DedicatedVideoMemory;
            ScopedAdapter->Release();
            break;
        }
        if (ScopedAdapter != nullptr)
        {
            ScopedAdapter->Release();
            ScopedAdapter = nullptr;
        }
    }
    ScopedFactory->Release();
}

void QueryMemory(HardwareInfo* InInfo)
{
    MEMORYSTATUSEX ScopedStatus{};
    ScopedStatus.dwLength = sizeof(ScopedStatus);
    if (GlobalMemoryStatusEx(&ScopedStatus))
    {
        InInfo->memoryTotalBytes = ScopedStatus.ullTotalPhys;
        InInfo->memoryAvailableBytes = ScopedStatus.ullAvailPhys;
    }
}
#else
[[nodiscard]] std::string CpuName()
{
    return "Unknown CPU";
}

void QueryGpu(HardwareInfo*)
{
}

void QueryMemory(HardwareInfo* InInfo)
{
    const long ScopedPages = sysconf(_SC_PHYS_PAGES);
    const long ScopedPageSize = sysconf(_SC_PAGE_SIZE);
    if (ScopedPages > 0 && ScopedPageSize > 0)
    {
        InInfo->memoryTotalBytes = static_cast<std::uint64_t>(ScopedPages) *
                                   static_cast<std::uint64_t>(ScopedPageSize);
    }
}
#endif

void QueryDisplays(SystemProfile* InProfile, void* InNativeWindow)
{
    int ScopedCount = 0;
    SDL_DisplayID* ScopedDisplays = SDL_GetDisplays(&ScopedCount);
    if (ScopedDisplays == nullptr)
    {
        return;
    }
    const SDL_DisplayID ScopedPrimary = SDL_GetPrimaryDisplay();
    const SDL_DisplayID ScopedWindowDisplay =
        InNativeWindow != nullptr ? SDL_GetDisplayForWindow(static_cast<SDL_Window*>(InNativeWindow)) : 0;
    for (int ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        DisplayInfo ScopedInfo;
        const SDL_DisplayID ScopedDisplay = ScopedDisplays[ScopedIndex];
        ScopedInfo.primary = ScopedDisplay == ScopedPrimary;
        if (const char* ScopedName = SDL_GetDisplayName(ScopedDisplay); ScopedName != nullptr)
        {
            ScopedInfo.name = ScopedName;
        }
        if (const SDL_DisplayMode* ScopedMode = SDL_GetDesktopDisplayMode(ScopedDisplay); ScopedMode != nullptr)
        {
            ScopedInfo.width = static_cast<std::uint32_t>(ScopedMode->w);
            ScopedInfo.height = static_cast<std::uint32_t>(ScopedMode->h);
            ScopedInfo.refreshHz = ScopedMode->refresh_rate;
        }
        if (ScopedDisplay == ScopedWindowDisplay && InNativeWindow != nullptr)
        {
            int ScopedWidth = 0;
            int ScopedHeight = 0;
            if (SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(InNativeWindow), &ScopedWidth, &ScopedHeight) &&
                ScopedWidth > 0 && ScopedHeight > 0)
            {
                ScopedInfo.width = static_cast<std::uint32_t>(ScopedWidth);
                ScopedInfo.height = static_cast<std::uint32_t>(ScopedHeight);
            }
            ScopedInfo.scale = SDL_GetWindowDisplayScale(static_cast<SDL_Window*>(InNativeWindow));
        }
        if (ScopedInfo.scale <= 0.0f)
        {
            ScopedInfo.scale = 1.0f;
        }
        if (ScopedInfo.primary)
        {
            InProfile->primaryDisplay = ScopedInfo;
        }
        InProfile->displays.push_back(std::move(ScopedInfo));
    }
    SDL_free(ScopedDisplays);
    if (InProfile->primaryDisplay.width == 0 && !InProfile->displays.empty())
    {
        InProfile->primaryDisplay = InProfile->displays.front();
    }
}

}

std::string DisplayInfo::AspectLabel() const
{
    return AspectLabelFor(width, height);
}

SystemProfile SystemInfo::Query(std::string_view InStoragePath, void* InNativeWindow)
{
    SystemProfile ScopedProfile;
    QueryDisplays(&ScopedProfile, InNativeWindow);
    ScopedProfile.hardware.cpu = CpuName();
    ScopedProfile.hardware.logicalCores = static_cast<std::uint32_t>(SDL_GetNumLogicalCPUCores());
    QueryGpu(&ScopedProfile.hardware);
    QueryMemory(&ScopedProfile.hardware);
    std::error_code ScopedError;
    const std::filesystem::space_info ScopedSpace =
        std::filesystem::space(std::filesystem::path(InStoragePath), ScopedError);
    if (!ScopedError)
    {
        ScopedProfile.storage.path = std::string(InStoragePath);
        ScopedProfile.storage.totalBytes = ScopedSpace.capacity;
        ScopedProfile.storage.freeBytes = ScopedSpace.available;
    }
    return ScopedProfile;
}

DisplayInfo SystemInfo::WindowDisplay(void* InNativeWindow)
{
    if (InNativeWindow == nullptr)
    {
        return {};
    }
    const SDL_DisplayID ScopedDisplay = SDL_GetDisplayForWindow(static_cast<SDL_Window*>(InNativeWindow));
    if (ScopedDisplay == 0)
    {
        return {};
    }
    DisplayInfo ScopedInfo;
    if (const char* ScopedName = SDL_GetDisplayName(ScopedDisplay); ScopedName != nullptr)
    {
        ScopedInfo.name = ScopedName;
    }
    if (const SDL_DisplayMode* ScopedMode = SDL_GetDesktopDisplayMode(ScopedDisplay); ScopedMode != nullptr)
    {
        ScopedInfo.width = static_cast<std::uint32_t>(ScopedMode->w);
        ScopedInfo.height = static_cast<std::uint32_t>(ScopedMode->h);
        ScopedInfo.refreshHz = ScopedMode->refresh_rate;
    }
    int ScopedWidth = 0;
    int ScopedHeight = 0;
    if (SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(InNativeWindow), &ScopedWidth, &ScopedHeight) &&
        ScopedWidth > 0 && ScopedHeight > 0)
    {
        ScopedInfo.width = static_cast<std::uint32_t>(ScopedWidth);
        ScopedInfo.height = static_cast<std::uint32_t>(ScopedHeight);
    }
    ScopedInfo.scale = SDL_GetWindowDisplayScale(static_cast<SDL_Window*>(InNativeWindow));
    if (ScopedInfo.scale <= 0.0f)
    {
        ScopedInfo.scale = 1.0f;
    }
    ScopedInfo.primary = true;
    return ScopedInfo;
}

}
