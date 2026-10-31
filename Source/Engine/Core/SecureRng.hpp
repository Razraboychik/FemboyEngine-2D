// FemboyEngine2D - cryptographically secure random source.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby
{

class SecureRng
{
public:
    SecureRng() = delete;

    // Fills the whole span from the OS entropy source. Returns false on any
    [[nodiscard]] static bool Fill(std::span<std::byte> InDestination) noexcept;
    [[nodiscard]] static bool Fill(void* InDestination, std::size_t InSize) noexcept;

    [[nodiscard]] static bool NextU32(std::uint32_t& InValue) noexcept;
    [[nodiscard]] static bool NextU64(std::uint64_t& InValue) noexcept;

    // Lower-case hexadecimal token of exactly `out.size()` characters. Tokens
    [[nodiscard]] static bool TokenHex(std::span<char> Out) noexcept;

    // True on platforms this build knows how to ask for OS entropy. This is a
    [[nodiscard]] static constexpr bool Supported() noexcept
    {
#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
        return true;
#else
        return false;
#endif
    }
};
}
