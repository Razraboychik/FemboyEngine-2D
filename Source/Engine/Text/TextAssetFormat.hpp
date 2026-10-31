// FemboyEngine2D - cooked long-text asset format.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Text
{

inline constexpr char kTextAssetMagic[8] = {'F', 'B', 'Y', 'T', 'X', 'T', '0', '1'};
inline constexpr std::uint32_t kTextAssetVersion = 1;

struct TextAssetHeader
{
    char Magic[8] = {};
    std::uint32_t Version = kTextAssetVersion;
    std::uint32_t Flags = 0;
    std::uint64_t TextBytes = 0;
    std::uint32_t BlockCount = 0;
    std::uint32_t Reserved = 0;
    std::uint64_t BlockTableOffset = 0;
    std::uint64_t TextOffset = 0;
};

static_assert(sizeof(TextAssetHeader) == 48);

struct TextAssetBlock
{
    std::uint32_t Offset = 0;
    std::uint32_t Length = 0;
};

static_assert(sizeof(TextAssetBlock) == 8);

struct TextAssetView
{
    TextAssetHeader Header{};
    std::string_view Text{};
    std::span<const TextAssetBlock> Blocks{};

    [[nodiscard]] std::string_view Block(std::uint32_t InIndex) const noexcept
    {
        if (InIndex >= Blocks.size())
        {
            return {};
        }
        const TextAssetBlock& ScopedBlock = Blocks[InIndex];
        if (ScopedBlock.Offset > Text.size() || ScopedBlock.Length > Text.size() - ScopedBlock.Offset)
        {
            return {};
        }
        return Text.substr(ScopedBlock.Offset, ScopedBlock.Length);
    }
};

enum class TextAssetStatus : std::uint8_t
{
    Ok = 0,
    InvalidHeader,
    Truncated,
    InvalidBlockTable,
    InvalidBlockRange,
    InvalidInput,
};

[[nodiscard]] inline TextAssetStatus OpenTextAsset(std::span<const std::byte> InBytes, TextAssetView* OutView,
                                                   std::string* InError = nullptr) noexcept
{
    const auto ScopedFail = [InError](TextAssetStatus InStatus, const char* InMessage) noexcept
    {
        if (InError != nullptr)
        {
            *InError = InMessage;
        }
        return InStatus;
    };
    if (OutView == nullptr)
    {
        return TextAssetStatus::InvalidInput;
    }
    if (InBytes.size() < sizeof(TextAssetHeader))
    {
        return ScopedFail(TextAssetStatus::Truncated, "text asset is smaller than its header");
    }
    TextAssetHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.Magic, kTextAssetMagic, sizeof(kTextAssetMagic)) != 0)
    {
        return ScopedFail(TextAssetStatus::InvalidHeader, "text asset magic mismatch");
    }
    if (ScopedHeader.Version != kTextAssetVersion)
    {
        return ScopedFail(TextAssetStatus::InvalidHeader, "text asset version mismatch");
    }
    if (ScopedHeader.BlockTableOffset < sizeof(TextAssetHeader) ||
        ScopedHeader.TextOffset < ScopedHeader.BlockTableOffset ||
        ScopedHeader.TextOffset > InBytes.size() || ScopedHeader.TextBytes > InBytes.size() - ScopedHeader.TextOffset)
    {
        return ScopedFail(TextAssetStatus::InvalidHeader, "text asset offsets are out of range");
    }
    const std::uint64_t ScopedTableBytes =
        static_cast<std::uint64_t>(ScopedHeader.BlockCount) * sizeof(TextAssetBlock);
    if (ScopedHeader.BlockTableOffset > InBytes.size() ||
        ScopedTableBytes > InBytes.size() - ScopedHeader.BlockTableOffset ||
        ScopedHeader.BlockTableOffset + ScopedTableBytes > ScopedHeader.TextOffset)
    {
        return ScopedFail(TextAssetStatus::InvalidBlockTable, "text asset block table is out of range");
    }
    const auto ScopedBlockAddress =
        reinterpret_cast<std::uintptr_t>(InBytes.data() + ScopedHeader.BlockTableOffset);
    if (ScopedBlockAddress % alignof(TextAssetBlock) != 0)
    {
        return ScopedFail(TextAssetStatus::InvalidBlockTable, "text asset block table is misaligned");
    }
    const auto* ScopedBlocks = reinterpret_cast<const TextAssetBlock*>(InBytes.data() + ScopedHeader.BlockTableOffset);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedHeader.BlockCount; ++ScopedIndex)
    {
        if (ScopedBlocks[ScopedIndex].Offset > ScopedHeader.TextBytes ||
            ScopedBlocks[ScopedIndex].Length > ScopedHeader.TextBytes - ScopedBlocks[ScopedIndex].Offset)
        {
            return ScopedFail(TextAssetStatus::InvalidBlockRange, "text asset block is out of range");
        }
    }
    OutView->Header = ScopedHeader;
    OutView->Text = std::string_view(reinterpret_cast<const char*>(InBytes.data() + ScopedHeader.TextOffset),
                                     static_cast<std::size_t>(ScopedHeader.TextBytes));
    OutView->Blocks = std::span<const TextAssetBlock>(ScopedBlocks, ScopedHeader.BlockCount);
    return TextAssetStatus::Ok;
}

[[nodiscard]] inline std::vector<std::byte> BuildTextAsset(std::string_view InText,
                                                          std::uint32_t InBlockBytes = 4096,
                                                          std::string* InError = nullptr) noexcept
{
    if (InBlockBytes == 0)
    {
        if (InError != nullptr)
        {
            *InError = "text block size must be positive";
        }
        return {};
    }
    if (InText.size() > std::numeric_limits<std::uint32_t>::max())
    {
        if (InError != nullptr)
        {
            *InError = "text asset exceeds the 32-bit block offset range";
        }
        return {};
    }
    std::vector<TextAssetBlock> ScopedBlocks;
    if (InText.empty())
    {
        ScopedBlocks.push_back(TextAssetBlock{0, 0});
    }
    else
    {
        std::size_t ScopedCursor = 0;
        while (ScopedCursor < InText.size())
        {
            std::size_t ScopedEnd = std::min<std::size_t>(ScopedCursor + InBlockBytes, InText.size());
            if (ScopedEnd < InText.size())
            {
                while (ScopedEnd > ScopedCursor + 1 &&
                       (static_cast<unsigned char>(InText[ScopedEnd]) & 0xC0u) == 0x80u)
                {
                    --ScopedEnd;
                }
                const std::size_t ScopedNewline = InText.rfind('\n', ScopedEnd - 1);
                if (ScopedNewline != std::string_view::npos && ScopedNewline >= ScopedCursor)
                {
                    ScopedEnd = ScopedNewline + 1;
                }
                else
                {
                    const std::size_t ScopedSpace = InText.rfind(' ', ScopedEnd - 1);
                    if (ScopedSpace != std::string_view::npos && ScopedSpace >= ScopedCursor)
                    {
                        ScopedEnd = ScopedSpace + 1;
                    }
                }
            }
            if (ScopedEnd <= ScopedCursor)
            {
                ScopedEnd = ScopedCursor + 1;
            }
            ScopedBlocks.push_back(
                TextAssetBlock{static_cast<std::uint32_t>(ScopedCursor), static_cast<std::uint32_t>(ScopedEnd - ScopedCursor)});
            ScopedCursor = ScopedEnd;
        }
    }

    TextAssetHeader ScopedHeader{};
    std::memcpy(ScopedHeader.Magic, kTextAssetMagic, sizeof(kTextAssetMagic));
    ScopedHeader.Version = kTextAssetVersion;
    ScopedHeader.TextBytes = InText.size();
    ScopedHeader.BlockCount = static_cast<std::uint32_t>(ScopedBlocks.size());
    ScopedHeader.BlockTableOffset = sizeof(TextAssetHeader);
    ScopedHeader.TextOffset =
        (ScopedHeader.BlockTableOffset + ScopedBlocks.size() * sizeof(TextAssetBlock) + 15u) & ~15ull;
    std::vector<std::byte> ScopedBytes(static_cast<std::size_t>(ScopedHeader.TextOffset) + InText.size(), std::byte{0});
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));
    std::memcpy(ScopedBytes.data() + ScopedHeader.BlockTableOffset, ScopedBlocks.data(),
                ScopedBlocks.size() * sizeof(TextAssetBlock));
    if (!InText.empty())
    {
        std::memcpy(ScopedBytes.data() + ScopedHeader.TextOffset, InText.data(), InText.size());
    }
    return ScopedBytes;
}
}
