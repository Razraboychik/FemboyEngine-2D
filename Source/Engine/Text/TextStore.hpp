// FemboyEngine2D - fixed-capacity long-form text storage.
#pragma once

#include "Engine/Text/TextAsset.hpp"
#include "Engine/Text/TextAssetFormat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fby::Text
{

// Text is copied into a preallocated arena once. The frame path only reads
// string views; no block owns a std::string and no per-frame allocation occurs.
template <std::uint32_t MaxBytes = 1u << 20, std::uint32_t MaxBlocks = 4096, std::uint32_t MaxAssets = 64,
          std::uint32_t BlockBytes = 4096>
class TextStore
{
    static_assert(MaxBytes > 0 && MaxBlocks > 0 && MaxAssets > 0 && BlockBytes > 0, "invalid text store capacity");
    static_assert(BlockBytes <= MaxBytes, "a block cannot exceed the arena");

public:
    [[nodiscard]] bool AddAsset(AssetId InAsset, std::string_view InText) noexcept
    {
        if (!InAsset.IsValid() || FindAsset(InAsset) != nullptr || MAssetCount >= MaxAssets)
        {
            return false;
        }
        if (InText.size() > static_cast<std::size_t>(MaxBytes - MDataUsed))
        {
            return false;
        }
        const std::uint32_t ScopedSavedDataUsed = MDataUsed;
        const std::uint32_t ScopedSavedBlockCount = MBlockCount;

        AssetSlot ScopedSlot;
        ScopedSlot.Asset = InAsset;
        ScopedSlot.Generation = NextGeneration(0);
        ScopedSlot.FirstBlock = MBlockCount;
        ScopedSlot.Used = true;

        if (InText.empty())
        {
            if (MBlockCount >= MaxBlocks)
            {
                return false;
            }
            MBlocks[MBlockCount++] = TextBlock{MDataUsed, 0};
            ScopedSlot.BlockCount = 1;
        }
        else
        {
            std::size_t ScopedCursor = 0;
            while (ScopedCursor < InText.size())
            {
                std::size_t ScopedEnd = ScopedCursor + BlockBytes;
                if (ScopedEnd > InText.size())
                {
                    ScopedEnd = InText.size();
                }
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
                const std::size_t ScopedLength = ScopedEnd - ScopedCursor;
                if (ScopedLength > static_cast<std::size_t>(MaxBytes - MDataUsed) ||
                    MBlockCount >= MaxBlocks)
                {
                    MDataUsed = ScopedSavedDataUsed;
                    MBlockCount = ScopedSavedBlockCount;
                    return false;
                }
                for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedLength; ++ScopedIndex)
                {
                    MData[MDataUsed + ScopedIndex] = InText[ScopedCursor + ScopedIndex];
                }
                MBlocks[MBlockCount++] = TextBlock{MDataUsed, static_cast<std::uint32_t>(ScopedLength)};
                MDataUsed += static_cast<std::uint32_t>(ScopedLength);
                ScopedCursor = ScopedEnd;
                ++ScopedSlot.BlockCount;
            }
        }

        MAssets[MAssetCount++] = ScopedSlot;
        return true;
    }

    [[nodiscard]] bool AddCookedAsset(AssetId InAsset, const TextAssetView& InAssetView) noexcept
    {
        if (!InAsset.IsValid() || FindAsset(InAsset) != nullptr || MAssetCount >= MaxAssets)
        {
            return false;
        }
        if (InAssetView.Text.size() > static_cast<std::size_t>(MaxBytes - MDataUsed) ||
            InAssetView.Blocks.size() > MaxBlocks - MBlockCount)
        {
            return false;
        }
        const std::uint32_t ScopedSavedDataUsed = MDataUsed;
        const std::uint32_t ScopedSavedBlockCount = MBlockCount;
        for (const TextAssetBlock& ScopedBlock : InAssetView.Blocks)
        {
            if (ScopedBlock.Offset > InAssetView.Text.size() ||
                ScopedBlock.Length > InAssetView.Text.size() - ScopedBlock.Offset)
            {
                return false;
            }
        }
        for (std::size_t ScopedIndex = 0; ScopedIndex < InAssetView.Text.size(); ++ScopedIndex)
        {
            MData[MDataUsed + ScopedIndex] = InAssetView.Text[ScopedIndex];
        }
        AssetSlot ScopedSlot;
        ScopedSlot.Asset = InAsset;
        ScopedSlot.Generation = NextGeneration(0);
        ScopedSlot.FirstBlock = MBlockCount;
        ScopedSlot.BlockCount = static_cast<std::uint32_t>(InAssetView.Blocks.size());
        ScopedSlot.Used = true;
        for (const TextAssetBlock& ScopedBlock : InAssetView.Blocks)
        {
            if (MBlockCount >= MaxBlocks)
            {
                MDataUsed = ScopedSavedDataUsed;
                MBlockCount = ScopedSavedBlockCount;
                return false;
            }
            MBlocks[MBlockCount++] =
                TextBlock{MDataUsed + ScopedBlock.Offset, ScopedBlock.Length};
        }
        MDataUsed += static_cast<std::uint32_t>(InAssetView.Text.size());
        MAssets[MAssetCount++] = ScopedSlot;
        return true;
    }

    [[nodiscard]] TextAssetRef MakeRef(AssetId InAsset, std::uint32_t InBlock) const noexcept
    {
        const AssetSlot* ScopedSlot = FindAsset(InAsset);
        if (ScopedSlot == nullptr || InBlock >= ScopedSlot->BlockCount)
        {
            return {};
        }
        return TextAssetRef{InAsset, InBlock, ScopedSlot->Generation};
    }

    [[nodiscard]] std::string_view Block(TextAssetRef InRef) const noexcept
    {
        const AssetSlot* ScopedSlot = FindAsset(InRef.Asset);
        if (ScopedSlot == nullptr || ScopedSlot->Generation != InRef.Generation ||
            InRef.Block >= ScopedSlot->BlockCount)
        {
            return {};
        }
        const TextBlock& ScopedBlock = MBlocks[ScopedSlot->FirstBlock + InRef.Block];
        return std::string_view(MData.data() + ScopedBlock.Offset, ScopedBlock.Length);
    }

    [[nodiscard]] std::uint32_t BlockCount(AssetId InAsset) const noexcept
    {
        const AssetSlot* ScopedSlot = FindAsset(InAsset);
        return ScopedSlot != nullptr ? ScopedSlot->BlockCount : 0;
    }

    [[nodiscard]] bool Contains(AssetId InAsset) const noexcept
    {
        return FindAsset(InAsset) != nullptr;
    }

    [[nodiscard]] std::uint32_t AssetCount() const noexcept
    {
        return MAssetCount;
    }

    [[nodiscard]] std::uint32_t BlockCount() const noexcept
    {
        return MBlockCount;
    }

    [[nodiscard]] std::uint32_t UsedBytes() const noexcept
    {
        return MDataUsed;
    }

    void Clear() noexcept
    {
        for (AssetSlot& ScopedSlot : MAssets)
        {
            ScopedSlot = {};
        }
        MAssetCount = 0;
        MBlockCount = 0;
        MDataUsed = 0;
    }

private:
    struct AssetSlot
    {
        AssetId Asset{};
        std::uint32_t FirstBlock = 0;
        std::uint32_t BlockCount = 0;
        std::uint32_t Generation = 0;
        bool Used = false;
    };

    [[nodiscard]] static constexpr std::uint32_t NextGeneration(std::uint32_t InGeneration) noexcept
    {
        const std::uint32_t ScopedNext = InGeneration + 1;
        return ScopedNext == 0 ? 1u : ScopedNext;
    }

    [[nodiscard]] const AssetSlot* FindAsset(AssetId InAsset) const noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MAssetCount; ++ScopedIndex)
        {
            if (MAssets[ScopedIndex].Used && MAssets[ScopedIndex].Asset == InAsset)
            {
                return &MAssets[ScopedIndex];
            }
        }
        return nullptr;
    }

    [[nodiscard]] AssetSlot* FindAsset(AssetId InAsset) noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MAssetCount; ++ScopedIndex)
        {
            if (MAssets[ScopedIndex].Used && MAssets[ScopedIndex].Asset == InAsset)
            {
                return &MAssets[ScopedIndex];
            }
        }
        return nullptr;
    }

    std::array<char, MaxBytes> MData{};
    std::array<TextBlock, MaxBlocks> MBlocks{};
    std::array<AssetSlot, MaxAssets> MAssets{};
    std::uint32_t MAssetCount = 0;
    std::uint32_t MBlockCount = 0;
    std::uint32_t MDataUsed = 0;
};
}
