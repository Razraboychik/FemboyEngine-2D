// FemboyEngine2D - VP9 decoder backend behind a runtime-loaded function table.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby::Video
{

// One decoded image in planar 8-bit I420 (4:2:0), written into buffers the
struct YuvFrameView
{
    std::uint8_t* luma = nullptr;
    std::uint8_t* chromaU = nullptr;
    std::uint8_t* chromaV = nullptr;
    std::uint32_t lumaStride = 0;
    std::uint32_t chromaStride = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return luma != nullptr && lumaStride >= width && width > 0 && height > 0 &&
               (chromaU == nullptr || (chromaV != nullptr && chromaStride >= (width + 1) / 2));
    }
};

// The function table. `user` is the backend's own state (in the libvpx case the
struct Vp9Backend
{
    const char* name = "none";
    void* user = nullptr;
    // Creates one decoder instance for a width x height VP9 stream. Returns
    void* (*createDecoder)(void* InUser, std::uint32_t InWidth, std::uint32_t InHeight) = nullptr;
    // Tears down an instance created above. Must accept null.
    void (*destroyDecoder)(void* InUser, void* InDecoder) = nullptr;
    // Decodes exactly one compressed VP9 frame into `target`. The decoder does
    bool (*decodeFrame)(void* InUser, void* InDecoder, std::span<const std::byte> InCompressed,
                        const YuvFrameView& InTarget) = nullptr;
    // Last error string, or "" - the pointed-to bytes are static or owned by
    const char* (*lastError)(void* InUser) = nullptr;
};

// The libvpx-backed table, resolved once and cached. Returns nullptr when the
[[nodiscard]] const Vp9Backend* LibvpxBackend() noexcept;

// True when LibvpxBackend() would return a usable table. A file path probe is
[[nodiscard]] bool LibvpxAvailable() noexcept;
}
