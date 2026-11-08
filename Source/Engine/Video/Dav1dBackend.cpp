// FemboyEngine2D - vendored dav1d backend for AV1 video.
#include "Engine/Video/Av1Backend.hpp"

#include <cstring>
#include <new>
#include <string>

#if defined(FBY_HAS_DAV1D) && FBY_HAS_DAV1D
#include <dav1d/dav1d.h>

namespace Fby::Video
{
namespace
{
struct Dav1dState
{
    Dav1dContext* Context = nullptr;
    std::string Error;
};

thread_local std::string GLastError;

void SetBackendError(Dav1dState* InState, std::string InMessage)
{
    GLastError = std::move(InMessage);
    if (InState != nullptr)
    {
        InState->Error = GLastError;
    }
}

const char* LastError(void*)
{
    return GLastError.c_str();
}

void* CreateDecoder(void*, std::uint32_t InWidth, std::uint32_t InHeight)
{
    (void)InWidth;
    (void)InHeight;
    Dav1dSettings ScopedSettings;
    dav1d_default_settings(&ScopedSettings);
    ScopedSettings.n_threads = 1;
    ScopedSettings.max_frame_delay = 0;
    ScopedSettings.apply_grain = 0;
    auto* ScopedState = new (std::nothrow) Dav1dState();
    if (ScopedState == nullptr)
    {
        return nullptr;
    }
    if (dav1d_open(&ScopedState->Context, &ScopedSettings) < 0 || ScopedState->Context == nullptr)
    {
        delete ScopedState;
        return nullptr;
    }
    return ScopedState;
}

void DestroyDecoder(void*, void* InDecoder)
{
    auto* ScopedState = static_cast<Dav1dState*>(InDecoder);
    if (ScopedState == nullptr)
    {
        return;
    }
    if (ScopedState->Context != nullptr)
    {
        dav1d_close(&ScopedState->Context);
    }
    delete ScopedState;
}

bool DecodeFrame(void*, void* InDecoder, std::span<const std::byte> InCompressed, const YuvFrameView& InTarget)
{
    auto* ScopedState = static_cast<Dav1dState*>(InDecoder);
    GLastError.clear();
    if (ScopedState == nullptr || ScopedState->Context == nullptr || InCompressed.empty())
    {
        SetBackendError(ScopedState, "dav1d: decoder is not initialized or packet is empty");
        return false;
    }
    Dav1dData ScopedData{};
    std::uint8_t* ScopedBuffer = dav1d_data_create(&ScopedData, InCompressed.size());
    if (ScopedBuffer == nullptr)
    {
        SetBackendError(ScopedState, "dav1d: packet allocation failed");
        return false;
    }
    std::memcpy(ScopedBuffer, InCompressed.data(), InCompressed.size());

    Dav1dPicture ScopedPicture{};
    bool ScopedHavePicture = false;
    for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < 32 && !ScopedHavePicture; ++ScopedAttempt)
    {
        if (ScopedData.sz != 0)
        {
            const int ScopedSend = dav1d_send_data(ScopedState->Context, &ScopedData);
            if (ScopedSend < 0 && ScopedSend != DAV1D_ERR(EAGAIN))
            {
                SetBackendError(ScopedState, "dav1d: send failed (" + std::to_string(ScopedSend) + ")");
                break;
            }
        }
        const int ScopedGet = dav1d_get_picture(ScopedState->Context, &ScopedPicture);
        if (ScopedGet == 0)
        {
            ScopedHavePicture = true;
            break;
        }
        if (ScopedGet != DAV1D_ERR(EAGAIN))
        {
            SetBackendError(ScopedState, "dav1d: decode failed (" + std::to_string(ScopedGet) + ")");
            break;
        }
        if (ScopedData.sz == 0)
        {
            break;
        }
    }
    if (ScopedData.sz != 0)
    {
        dav1d_data_unref(&ScopedData);
    }
    if (!ScopedHavePicture)
    {
        if (ScopedState->Error.empty())
        {
            SetBackendError(ScopedState, "dav1d: no picture produced");
        }
        return false;
    }

    bool ScopedCopied = false;
    const bool ScopedSupportedDepth = ScopedPicture.p.bpc == 8 || ScopedPicture.p.bpc == 10 ||
                                      ScopedPicture.p.bpc == 12;
    if (ScopedPicture.p.layout == DAV1D_PIXEL_LAYOUT_I420 && ScopedSupportedDepth &&
        ScopedPicture.p.w == static_cast<int>(InTarget.width) && ScopedPicture.p.h == static_cast<int>(InTarget.height))
    {
        const auto* ScopedLuma = static_cast<const std::uint8_t*>(ScopedPicture.data[0]);
        const auto* ScopedU = static_cast<const std::uint8_t*>(ScopedPicture.data[1]);
        const auto* ScopedV = static_cast<const std::uint8_t*>(ScopedPicture.data[2]);
        const std::uint32_t ScopedShift = ScopedPicture.p.bpc > 8 ? static_cast<std::uint32_t>(ScopedPicture.p.bpc - 8) : 0;
        const std::size_t ScopedChromaWidth = (InTarget.width + 1) / 2;
        const std::size_t ScopedChromaHeight = (InTarget.height + 1) / 2;
        const auto ScopedCopyPlane = [&](const std::uint8_t* InSource, std::ptrdiff_t InStride, std::uint8_t* OutTarget,
                                         std::uint32_t InTargetStride, std::uint32_t InWidth, std::uint32_t InHeight)
        {
            if (ScopedShift == 0)
            {
                for (std::uint32_t ScopedY = 0; ScopedY < InHeight; ++ScopedY)
                {
                    std::memcpy(OutTarget + static_cast<std::size_t>(ScopedY) * InTargetStride,
                                InSource + static_cast<std::ptrdiff_t>(ScopedY) * InStride, InWidth);
                }
                return;
            }
            for (std::uint32_t ScopedY = 0; ScopedY < InHeight; ++ScopedY)
            {
                const auto* ScopedRow = reinterpret_cast<const std::uint16_t*>(
                    InSource + static_cast<std::ptrdiff_t>(ScopedY) * InStride);
                std::uint8_t* ScopedOut = OutTarget + static_cast<std::size_t>(ScopedY) * InTargetStride;
                for (std::uint32_t ScopedX = 0; ScopedX < InWidth; ++ScopedX)
                {
                    ScopedOut[ScopedX] = static_cast<std::uint8_t>(ScopedRow[ScopedX] >> ScopedShift);
                }
            }
        };
        ScopedCopyPlane(ScopedLuma, ScopedPicture.stride[0], InTarget.luma, InTarget.lumaStride, InTarget.width,
                        InTarget.height);
        ScopedCopyPlane(ScopedU, ScopedPicture.stride[1], InTarget.chromaU, InTarget.chromaStride,
                        static_cast<std::uint32_t>(ScopedChromaWidth), static_cast<std::uint32_t>(ScopedChromaHeight));
        ScopedCopyPlane(ScopedV, ScopedPicture.stride[1], InTarget.chromaV, InTarget.chromaStride,
                        static_cast<std::uint32_t>(ScopedChromaWidth), static_cast<std::uint32_t>(ScopedChromaHeight));
        ScopedCopied = true;
    }
    else
    {
        SetBackendError(ScopedState, "dav1d: unsupported pixel layout or dimensions");
    }
    dav1d_picture_unref(&ScopedPicture);
    return ScopedCopied;
}

const Vp9Backend* MakeTable()
{
    static Vp9Backend ScopedTable{};
    ScopedTable.name = "dav1d";
    ScopedTable.createDecoder = &CreateDecoder;
    ScopedTable.destroyDecoder = &DestroyDecoder;
    ScopedTable.decodeFrame = &DecodeFrame;
    ScopedTable.lastError = &LastError;
    return &ScopedTable;
}
}

const Av1Backend* Libdav1dBackend() noexcept
{
    return MakeTable();
}
}

#else

namespace Fby::Video
{
const Av1Backend* Libdav1dBackend() noexcept
{
    return nullptr;
}
}

#endif
