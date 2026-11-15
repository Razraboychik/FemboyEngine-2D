// FemboyEngine2D - screen, hardware and storage profile.
#pragma once

#include "Engine/Core/Types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Platform
{

struct DisplayInfo
{
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float refreshHz = 0.0f;
    float scale = 1.0f;
    bool primary = false;

    [[nodiscard]] float Aspect() const noexcept
    {
        return height == 0 ? 0.0f : static_cast<float>(width) / static_cast<float>(height);
    }
    // "16:9", "4:3", ...; empty when the resolution is unknown.
    [[nodiscard]] std::string AspectLabel() const;
};

struct HardwareInfo
{
    std::string cpu;
    std::uint32_t logicalCores = 0;
    std::string gpu;
    std::uint64_t videoMemoryBytes = 0;
    std::uint64_t memoryTotalBytes = 0;
    std::uint64_t memoryAvailableBytes = 0;
};

struct StorageInfo
{
    std::string path;
    std::uint64_t totalBytes = 0;
    std::uint64_t freeBytes = 0;

    [[nodiscard]] std::uint64_t UsedBytes() const noexcept
    {
        return totalBytes > freeBytes ? totalBytes - freeBytes : 0;
    }
};

struct SystemProfile
{
    std::vector<DisplayInfo> displays;
    DisplayInfo primaryDisplay;
    HardwareInfo hardware;
    StorageInfo storage;
};

// Queries the current display list, CPU/GPU/memory and the volume that owns
// `InStoragePath`. The window is optional; when passed, its monitor is used for
// the primary display scale. No allocation happens per frame.
class SystemInfo
{
public:
    [[nodiscard]] static SystemProfile Query(std::string_view InStoragePath = ".",
                                             void* InNativeWindow = nullptr);
    // Resolution of the monitor that currently hosts the window.
    [[nodiscard]] static DisplayInfo WindowDisplay(void* InNativeWindow);
};

}
