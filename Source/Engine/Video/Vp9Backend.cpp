// FemboyEngine2D - runtime-loaded libvpx VP9 backend.
#include "Engine/Video/Vp9Backend.hpp"

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Fby::Video
{
namespace
{

// Dynamic library helpers (mirrors the plugin host's loader)
#if defined(_WIN32)
using LibraryHandle = HMODULE;

[[nodiscard]] LibraryHandle OpenCodecLibrary(const char* InName) noexcept
{
    return ::LoadLibraryExA(InName, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

void CloseCodecLibrary(LibraryHandle InHandle) noexcept
{
    ::FreeLibrary(InHandle);
}

[[nodiscard]] void* FindCodecSymbol(LibraryHandle InHandle, const char* InName) noexcept
{
    return reinterpret_cast<void*>(::GetProcAddress(InHandle, InName));
}
#else
using LibraryHandle = void*;

[[nodiscard]] LibraryHandle OpenCodecLibrary(const char* name) noexcept
{
    return ::dlopen(name, RTLD_NOW | RTLD_LOCAL);
}

void CloseCodecLibrary(LibraryHandle handle) noexcept
{
    ::dlclose(handle);
}

[[nodiscard]] void* FindCodecSymbol(LibraryHandle handle, const char* name) noexcept
{
    return ::dlsym(handle, name);
}
#endif

// The names a libvpx build is normally installed under. Loaded in order; the
constexpr const char* kLibraryNames[] = {
#if defined(_WIN32)
    "vpx.dll",
    "libvpx.dll",
    "vpx64.dll",
#elif defined(__APPLE__)
    "libvpx.dylib",
    "/opt/homebrew/lib/libvpx.dylib",
    "/usr/local/lib/libvpx.dylib",
#else
    "libvpx.so.9", "libvpx.so.8", "libvpx.so.7", "libvpx.so.6", "libvpx.so",
#endif
};

// Mirrored public libvpx ABI
struct VpxImage
{
    // Vpx_img_fmt_t
    int format = 0;
    int colorSpace = 0;
    int colorRange = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int bitDepth = 0;
    unsigned int displayWidth = 0;
    unsigned int displayHeight = 0;
    unsigned int renderWidth = 0;
    unsigned int renderHeight = 0;
    unsigned int xChromaShift = 0;
    unsigned int yChromaShift = 0;
    unsigned char* planes[4] = {};
    int stride[4] = {};
};

// vpx_codec_dec_cfg_t
struct VpxDecodeConfig
{
    unsigned int threads = 1;
    unsigned int width = 0;
    unsigned int height = 0;
};

// vpx_img_fmt_t values from vpx_image.h.
constexpr int kVpxImageFormatPlanar = 0x100;
constexpr int kVpxImageFormatUvFlip = 0x200;
constexpr int kVpxImageFormatHighBitDepth = 0x800;
// 0x102
constexpr int kVpxImageFormatI420 = kVpxImageFormatPlanar | 2;
// 0x301
constexpr int kVpxImageFormatYv12 = kVpxImageFormatPlanar | kVpxImageFormatUvFlip | 1;

// vpx_codec_err_t: VPX_CODEC_OK is 0.
constexpr int kVpxCodecOk = 0;
// VPX_DL_REALTIME: decode for low latency, never wait on a quality target.
constexpr int kVpxDeadlineRealtime = 1;

// One decoder instance. `context` is deliberately opaque storage: the real
struct LibvpxDecoder
{
    alignas(16) unsigned char context[512] = {};
    void* imageIterator = nullptr;
};

struct LibvpxApi
{
    LibraryHandle library = nullptr;
    const void* (*codecVp9Dx)() = nullptr;
    int (*decInitVer)(void* InContext, const void* InIface, const void* InConfig, unsigned int InFlags,
                      int InVersion) = nullptr;
    int (*decode)(void* InContext, const unsigned char* InData, std::size_t InSize, void* InUserPrivate,
                  int InDeadline) = nullptr;
    const VpxImage* (*getFrame)(void* InContext, void** InIterator) = nullptr;
    const char* (*errorDetail)(void* InContext) = nullptr;
    const char* (*errorToString)(int InError) = nullptr;
    void (*destroy)(void* InContext) = nullptr;
};

struct BackendState
{
    LibvpxApi api{};
    bool ready = false;
    bool attempted = false;
    std::once_flag once{};
};

BackendState& State() noexcept
{
    static BackendState ScopedState;
    return ScopedState;
}

// Per-thread because two players can decode on different threads at once.
thread_local char lastErrorText[256] = {};

void SetLastError(const char* InText) noexcept
{
    std::snprintf(lastErrorText, sizeof(lastErrorText), "%s", InText);
}

[[nodiscard]] const char* GetLastError(void*) noexcept
{
    return lastErrorText;
}

bool ResolveApi(LibvpxApi& InApi)
{
    for (const char* ScopedName : kLibraryNames)
    {
        LibraryHandle ScopedLibrary = OpenCodecLibrary(ScopedName);
        if (ScopedLibrary == nullptr)
        {
            continue;
        }
        InApi.library = ScopedLibrary;
        InApi.codecVp9Dx = reinterpret_cast<const void* (*)()>(FindCodecSymbol(ScopedLibrary, "vpx_codec_vp9_dx"));
        InApi.decInitVer = reinterpret_cast<int (*)(void*, const void*, const void*, unsigned int, int)>(
            FindCodecSymbol(ScopedLibrary, "vpx_codec_dec_init_ver"));
        InApi.decode = reinterpret_cast<int (*)(void*, const unsigned char*, std::size_t, void*, int)>(
            FindCodecSymbol(ScopedLibrary, "vpx_codec_decode"));
        InApi.getFrame =
            reinterpret_cast<const VpxImage* (*)(void*, void**)>(FindCodecSymbol(ScopedLibrary, "vpx_codec_get_frame"));
        InApi.errorDetail =
            reinterpret_cast<const char* (*)(void*)>(FindCodecSymbol(ScopedLibrary, "vpx_codec_error_detail"));
        InApi.errorToString =
            reinterpret_cast<const char* (*)(int)>(FindCodecSymbol(ScopedLibrary, "vpx_codec_err_to_string"));
        InApi.destroy = reinterpret_cast<void (*)(void*)>(FindCodecSymbol(ScopedLibrary, "vpx_codec_destroy"));

        if (InApi.codecVp9Dx != nullptr && InApi.decInitVer != nullptr && InApi.decode != nullptr &&
            InApi.getFrame != nullptr && InApi.errorDetail != nullptr && InApi.errorToString != nullptr &&
            InApi.destroy != nullptr)
        {
            return true;
        }
        CloseCodecLibrary(ScopedLibrary);
        InApi = {};
    }
    return false;
}

void* CreateDecoder(void* InUser, std::uint32_t InWidth, std::uint32_t InHeight)
{
    auto& ScopedApi = *static_cast<LibvpxApi*>(InUser);
    auto* ScopedDecoder = new (std::nothrow) LibvpxDecoder();
    if (ScopedDecoder == nullptr)
    {
        SetLastError("libvpx: out of memory allocating a decoder");
        return nullptr;
    }

    const void* ScopedIface = ScopedApi.codecVp9Dx();
    if (ScopedIface == nullptr)
    {
        SetLastError("libvpx: vpx_codec_vp9_dx() returned no interface");
        delete ScopedDecoder;
        return nullptr;
    }

    VpxDecodeConfig ScopedConfig;
    ScopedConfig.threads = 1;
    ScopedConfig.width = InWidth;
    ScopedConfig.height = InHeight;

    // The ABI version moved with libvpx releases. Try the value of a current
    for (int ScopedVersion = 32; ScopedVersion >= 1; --ScopedVersion)
    {
        std::memset(ScopedDecoder->context, 0, sizeof(ScopedDecoder->context));
        if (ScopedApi.decInitVer(ScopedDecoder->context, ScopedIface, &ScopedConfig, 0, ScopedVersion) == kVpxCodecOk)
        {
            ScopedDecoder->imageIterator = nullptr;
            return ScopedDecoder;
        }
    }

    SetLastError("libvpx: vpx_codec_dec_init_ver rejected every known ABI version");
    delete ScopedDecoder;
    return nullptr;
}

void DestroyDecoder(void* InUser, void* InDecoder)
{
    if (InDecoder == nullptr)
    {
        return;
    }
    auto& ScopedApi = *static_cast<LibvpxApi*>(InUser);
    auto* ScopedHandle = static_cast<LibvpxDecoder*>(InDecoder);
    ScopedApi.destroy(ScopedHandle->context);
    delete ScopedHandle;
}

bool CopyImage(const VpxImage& InImage, const YuvFrameView& InTarget)
{
    if ((InImage.format & kVpxImageFormatPlanar) == 0)
    {
        SetLastError("libvpx returned a non-planar image format");
        return false;
    }
    if ((InImage.format & kVpxImageFormatHighBitDepth) != 0)
    {
        SetLastError("libvpx returned a high-bit-depth image; only 8-bit I420 is supported");
        return false;
    }

    const unsigned int ScopedWidth = InImage.displayWidth != 0 ? InImage.displayWidth : InImage.width;
    const unsigned int ScopedHeight = InImage.displayHeight != 0 ? InImage.displayHeight : InImage.height;
    if (ScopedWidth != InTarget.width || ScopedHeight != InTarget.height)
    {
        SetLastError("libvpx decoded a frame whose size does not match the track dimensions");
        return false;
    }

    const bool ScopedYv12 = InImage.format == kVpxImageFormatYv12;
    if (InImage.format != kVpxImageFormatI420 && !ScopedYv12)
    {
        SetLastError("libvpx returned an unsupported I420 variant");
        return false;
    }

    const unsigned int ScopedChromaWidth = (ScopedWidth + 1) / 2;
    const unsigned int ScopedChromaHeight = (ScopedHeight + 1) / 2;
    for (unsigned int ScopedRow = 0; ScopedRow < ScopedHeight; ++ScopedRow)
    {
        std::memcpy(InTarget.luma + static_cast<std::size_t>(ScopedRow) * InTarget.lumaStride,
                    InImage.planes[0] +
                        static_cast<std::size_t>(ScopedRow) * static_cast<std::size_t>(InImage.stride[0]),
                    ScopedWidth);
    }
    if (InTarget.chromaU != nullptr)
    {
        const unsigned char* ScopedSourceU = InImage.planes[ScopedYv12 ? 2 : 1];
        const unsigned char* ScopedSourceV = InImage.planes[ScopedYv12 ? 1 : 2];
        for (unsigned int ScopedRow = 0; ScopedRow < ScopedChromaHeight; ++ScopedRow)
        {
            std::memcpy(InTarget.chromaU + static_cast<std::size_t>(ScopedRow) * InTarget.chromaStride,
                        ScopedSourceU +
                            static_cast<std::size_t>(ScopedRow) * static_cast<std::size_t>(InImage.stride[1]),
                        ScopedChromaWidth);
            std::memcpy(InTarget.chromaV + static_cast<std::size_t>(ScopedRow) * InTarget.chromaStride,
                        ScopedSourceV +
                            static_cast<std::size_t>(ScopedRow) * static_cast<std::size_t>(InImage.stride[2]),
                        ScopedChromaWidth);
        }
    }
    return true;
}

bool DecodeFrame(void* InUser, void* InDecoder, std::span<const std::byte> InCompressed, const YuvFrameView& InTarget)
{
    if (InDecoder == nullptr || !InTarget.IsValid())
    {
        SetLastError("libvpx: decoder or target frame is invalid");
        return false;
    }
    auto& ScopedApi = *static_cast<LibvpxApi*>(InUser);
    auto* ScopedHandle = static_cast<LibvpxDecoder*>(InDecoder);
    if (InCompressed.empty())
    {
        SetLastError("libvpx: empty compressed frame");
        return false;
    }

    const int ScopedResult =
        ScopedApi.decode(ScopedHandle->context, reinterpret_cast<const unsigned char*>(InCompressed.data()),
                         InCompressed.size(), nullptr, kVpxDeadlineRealtime);
    if (ScopedResult != kVpxCodecOk)
    {
        const char* ScopedDetail = ScopedApi.errorDetail(ScopedHandle->context);
        SetLastError(ScopedDetail != nullptr ? ScopedDetail : ScopedApi.errorToString(ScopedResult));
        return false;
    }

    ScopedHandle->imageIterator = nullptr;
    const VpxImage* ScopedImage = ScopedApi.getFrame(ScopedHandle->context, &ScopedHandle->imageIterator);
    if (ScopedImage == nullptr)
    {
        SetLastError("libvpx decoded no frame (truncated VP9 payload?)");
        return false;
    }
    return CopyImage(*ScopedImage, InTarget);
}

const Vp9Backend* MakeBackend(LibvpxApi& InApi) noexcept
{
    static const Vp9Backend ScopedTable = {
        "libvpx", &InApi, &CreateDecoder, &DestroyDecoder, &DecodeFrame, &GetLastError,
    };
    return &ScopedTable;
}
}

const Vp9Backend* LibvpxBackend() noexcept
{
    BackendState& ScopedState = State();
    std::call_once(ScopedState.once,
                   [&ScopedState]
                   {
                       ScopedState.ready = ResolveApi(ScopedState.api);
                       ScopedState.attempted = true;
                   });
    if (!ScopedState.ready)
    {
        return nullptr;
    }
    return MakeBackend(ScopedState.api);
}

bool LibvpxAvailable() noexcept
{
    return LibvpxBackend() != nullptr;
}
}
