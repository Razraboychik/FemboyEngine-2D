// FemboyEngine2D - OS entropy backend for SecureRng.
#include "Engine/Core/SecureRng.hpp"

#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#elif defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Fby
{
namespace
{
#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
[[nodiscard]] int OpenUrandom() noexcept
{
    int flags = O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
    int descriptor = -1;
    do
    {
        descriptor = ::open("/dev/urandom", flags);
    } while (descriptor < 0 && errno == EINTR);
    return descriptor;
}
#endif
}

bool SecureRng::Fill(std::span<std::byte> InDestination) noexcept
{
    return Fill(InDestination.data(), InDestination.size());
}

bool SecureRng::Fill(void* InDestination, std::size_t InSize) noexcept
{
    if (InSize == 0)
    {
        return true;
    }
    if (InDestination == nullptr)
    {
        return false;
    }

#if defined(_WIN32)
    auto* ScopedCursor = static_cast<unsigned char*>(InDestination);
    std::size_t ScopedRemaining = InSize;
    while (ScopedRemaining > 0)
    {
        const ULONG ScopedChunk = ScopedRemaining > 0x40000000u ? 0x40000000u : static_cast<ULONG>(ScopedRemaining);
        const NTSTATUS ScopedStatus = ::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(ScopedCursor), ScopedChunk,
                                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (ScopedStatus < 0)
        {
            return false;
        }
        ScopedCursor += ScopedChunk;
        ScopedRemaining -= ScopedChunk;
    }
    return true;
#elif defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
    const int ScopedDescriptor = OpenUrandom();
    if (ScopedDescriptor < 0)
    {
        return false;
    }
    auto* ScopedCursor = static_cast<unsigned char*>(InDestination);
    std::size_t ScopedRemaining = InSize;
    bool ScopedOk = true;
    while (ScopedRemaining > 0)
    {
        const ssize_t ScopedGot = ::read(ScopedDescriptor, ScopedCursor, ScopedRemaining);
        if (ScopedGot < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ScopedOk = false;
            break;
        }
        if (ScopedGot == 0)
        {
            ScopedOk = false;
            break;
        }
        ScopedCursor += static_cast<std::size_t>(ScopedGot);
        ScopedRemaining -= static_cast<std::size_t>(ScopedGot);
    }
    (void)::close(ScopedDescriptor);
    return ScopedOk;
#else
    (void)InDestination;
    (void)InSize;
    return false;
#endif
}

bool SecureRng::NextU32(std::uint32_t& InValue) noexcept
{
    return Fill(&InValue, sizeof(InValue));
}

bool SecureRng::NextU64(std::uint64_t& InValue) noexcept
{
    return Fill(&InValue, sizeof(InValue));
}

bool SecureRng::TokenHex(std::span<char> Out) noexcept
{
    static constexpr char ScopedKHex[] = "0123456789abcdef";
    if (Out.empty())
    {
        return true;
    }

    std::uint8_t ScopedBlock[32]{};
    std::size_t ScopedWritten = 0;
    while (ScopedWritten < Out.size())
    {
        const std::size_t ScopedRemainingChars = Out.size() - ScopedWritten;
        const std::size_t ScopedNeededBytes = (ScopedRemainingChars + 1u) / 2u;
        const std::size_t ScopedChunk =
            ScopedNeededBytes < sizeof(ScopedBlock) ? ScopedNeededBytes : sizeof(ScopedBlock);
        if (!Fill(ScopedBlock, ScopedChunk))
        {
            std::memset(ScopedBlock, 0, sizeof(ScopedBlock));
            return false;
        }
        for (std::size_t ScopedI = 0; ScopedI < ScopedChunk && ScopedWritten < Out.size(); ++ScopedI)
        {
            Out[ScopedWritten++] = ScopedKHex[ScopedBlock[ScopedI] >> 4u];
            if (ScopedWritten < Out.size())
            {
                Out[ScopedWritten++] = ScopedKHex[ScopedBlock[ScopedI] & 0x0Fu];
            }
        }
        std::memset(ScopedBlock, 0, sizeof(ScopedBlock));
    }
    return true;
}
}
