// FemboyEngine2D - dav1d backend availability and error path.
#include "Engine/Video/Av1Backend.hpp"

#include <array>
#include <cstdio>
#include <cstring>

int main()
{
    const Fby::Video::Av1Backend* ScopedBackend = Fby::Video::Libdav1dBackend();
    if (ScopedBackend == nullptr || ScopedBackend->createDecoder == nullptr ||
        ScopedBackend->decodeFrame == nullptr)
    {
        std::printf("dav1d_backend: skipped (no dav1d on this platform)\n");
        return 0;
    }
    std::printf("dav1d_backend: backend '%s'\n", ScopedBackend->name);

    void* ScopedDecoder = ScopedBackend->createDecoder(ScopedBackend->user, 4, 4);
    if (ScopedDecoder == nullptr)
    {
        std::fprintf(stderr, "dav1d_backend: decoder creation failed\n");
        return 1;
    }

    std::array<std::uint8_t, 16> ScopedLuma{};
    std::array<std::uint8_t, 4> ScopedChromaU{};
    std::array<std::uint8_t, 4> ScopedChromaV{};
    Fby::Video::YuvFrameView ScopedTarget;
    ScopedTarget.luma = ScopedLuma.data();
    ScopedTarget.chromaU = ScopedChromaU.data();
    ScopedTarget.chromaV = ScopedChromaV.data();
    ScopedTarget.lumaStride = 4;
    ScopedTarget.chromaStride = 2;
    ScopedTarget.width = 4;
    ScopedTarget.height = 4;

    const std::array<std::byte, 3> ScopedGarbage{std::byte{0xFF}, std::byte{0x00}, std::byte{0x11}};
    const bool ScopedDecoded = ScopedBackend->decodeFrame(ScopedBackend->user, ScopedDecoder, ScopedGarbage,
                                                          ScopedTarget);
    if (ScopedDecoded)
    {
        std::fprintf(stderr, "dav1d_backend: garbage packet decoded unexpectedly\n");
        ScopedBackend->destroyDecoder(ScopedBackend->user, ScopedDecoder);
        return 1;
    }
    const char* ScopedError = ScopedBackend->lastError != nullptr ? ScopedBackend->lastError(ScopedBackend->user)
                                                                  : nullptr;
    if (ScopedError == nullptr || ScopedError[0] == '\0')
    {
        std::fprintf(stderr, "dav1d_backend: lastError was not populated\n");
        ScopedBackend->destroyDecoder(ScopedBackend->user, ScopedDecoder);
        return 1;
    }
    std::printf("dav1d_backend: error path: %s\n", ScopedError);
    ScopedBackend->destroyDecoder(ScopedBackend->user, ScopedDecoder);
    ScopedBackend->destroyDecoder(ScopedBackend->user, nullptr);
    std::printf("dav1d_backend: OK\n");
    return 0;
}
