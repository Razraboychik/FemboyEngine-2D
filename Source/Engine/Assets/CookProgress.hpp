// FemboyEngine2D - cross-process cook progress record.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby::Assets
{

inline constexpr char kCookProgressMagic[8] = {'F', 'B', 'Y', 'C', 'O', 'O', 'K', '1'};
inline constexpr std::uint32_t kCookProgressVersion = 1;
inline constexpr std::uint32_t kCookProgressAssetBytes = 192;
inline constexpr std::uint32_t kCookProgressMessageBytes = 160;

enum class CookProgressStage : std::uint32_t
{
    Idle = 0,
    Planning = 1,
    Cooking = 2,
    Packing = 3,
    Compressing = 4,
    Encrypting = 5,
    Succeeded = 6,
    Failed = 7,
    Cancelled = 8,
};

enum CookProgressFlags : std::uint32_t
{
    CookProgressActive = 1u << 0,
    CookProgressEncrypted = 1u << 1,
    CookProgressFailed = 1u << 2,
};

struct CookProgressRecord
{
    char magic[8] = {};
    std::uint32_t version = kCookProgressVersion;
    std::uint32_t sequence = 0;
    std::uint32_t stage = static_cast<std::uint32_t>(CookProgressStage::Idle);
    std::uint32_t completed = 0;
    std::uint32_t total = 0;
    std::uint32_t bytesPerSecond = 0;
    // 0 = none, 1 = ChaCha20-Poly1305, 2 = AES-256-GCM
    std::uint32_t cipher = 0;
    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;
    std::uint64_t rawBytes = 0;
    std::uint64_t storedBytes = 0;
    std::uint64_t elapsedMicroseconds = 0;
    char asset[kCookProgressAssetBytes] = {};
    char message[kCookProgressMessageBytes] = {};

    [[nodiscard]] constexpr bool Active() const noexcept
    {
        return (flags & CookProgressActive) != 0;
    }
    [[nodiscard]] constexpr bool Encrypted() const noexcept
    {
        return (flags & CookProgressEncrypted) != 0;
    }
    [[nodiscard]] constexpr bool Failed() const noexcept
    {
        return (flags & CookProgressFailed) != 0;
    }
    [[nodiscard]] constexpr float Fraction() const noexcept
    {
        return total == 0 ? 0.0f : static_cast<float>(completed) / static_cast<float>(total);
    }
};

static_assert(std::is_trivially_copyable_v<CookProgressRecord>);
static_assert(std::is_standard_layout_v<CookProgressRecord>);
static_assert(sizeof(CookProgressRecord) == 424);

[[nodiscard]] constexpr const char* CookProgressStageName(CookProgressStage InStage) noexcept
{
    switch (InStage)
    {
        case CookProgressStage::Idle:
            return "idle";
        case CookProgressStage::Planning:
            return "planning";
        case CookProgressStage::Cooking:
            return "cooking";
        case CookProgressStage::Packing:
            return "packing";
        case CookProgressStage::Compressing:
            return "compressing";
        case CookProgressStage::Encrypting:
            return "encrypting";
        case CookProgressStage::Succeeded:
            return "succeeded";
        case CookProgressStage::Failed:
            return "failed";
        case CookProgressStage::Cancelled:
            return "cancelled";
    }
    return "?";
}

[[nodiscard]] constexpr const char* CookCipherName(std::uint32_t InCipher) noexcept
{
    switch (InCipher)
    {
        case 1:
            return "ChaCha20-Poly1305";
        case 2:
            return "AES-256-GCM";
        default:
            return "none";
    }
}

// Truncating copy into a fixed record field. Always NUL-terminates.
inline void CookProgressSetText(char* InDestination, std::size_t InCapacity, std::string_view InText) noexcept
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedCopy = InText.size() < InCapacity - 1 ? InText.size() : InCapacity - 1;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedCopy; ++ScopedIndex)
    {
        InDestination[ScopedIndex] = InText[ScopedIndex];
    }
    InDestination[ScopedCopy] = '\0';
}

inline void CookProgressSetText(char* InDestination, std::size_t InCapacity, const char* InText) noexcept
{
    CookProgressSetText(InDestination, InCapacity, InText != nullptr ? std::string_view(InText) : std::string_view{});
}
}
