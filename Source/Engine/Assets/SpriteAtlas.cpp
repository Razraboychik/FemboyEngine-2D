// FemboyEngine2D - deterministic sprite atlas packing.
#include "Engine/Assets/SpriteAtlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Fby::Assets
{

AtlasPackError PackSpriteAtlas(std::span<const AtlasSpriteSource> InSources, std::uint32_t InPageWidth,
                               std::uint32_t InPageHeight, SpriteAtlas* OutAtlas) noexcept
{
    if (OutAtlas == nullptr)
    {
        return AtlasPackError::InvalidSprite;
    }
    if (InSources.size() > kMaxAtlasSprites)
    {
        return AtlasPackError::TooManySprites;
    }
    if (InPageWidth == 0 || InPageHeight == 0 || InPageWidth > 16384 || InPageHeight > 16384)
    {
        return AtlasPackError::PageTooLarge;
    }
    for (const AtlasSpriteSource& ScopedSource : InSources)
    {
        if (ScopedSource.Id == 0 || ScopedSource.Width == 0 || ScopedSource.Height == 0)
        {
            return AtlasPackError::InvalidSprite;
        }
    }

    std::array<std::uint32_t, kMaxAtlasSprites> ScopedOrder{};
    for (std::uint32_t ScopedI = 0; ScopedI < InSources.size(); ++ScopedI)
    {
        ScopedOrder[ScopedI] = ScopedI;
    }
    std::sort(ScopedOrder.begin(), ScopedOrder.begin() + InSources.size(),
              [&InSources](std::uint32_t InLeft, std::uint32_t InRight) noexcept
              {
                  const AtlasSpriteSource& ScopedLeft = InSources[InLeft];
                  const AtlasSpriteSource& ScopedRight = InSources[InRight];
                  if (ScopedLeft.Height != ScopedRight.Height)
                  {
                      return ScopedLeft.Height > ScopedRight.Height;
                  }
                  if (ScopedLeft.Width != ScopedRight.Width)
                  {
                      return ScopedLeft.Width > ScopedRight.Width;
                  }
                  return ScopedLeft.Id < ScopedRight.Id;
              });

    *OutAtlas = SpriteAtlas{};
    OutAtlas->PageWidth = InPageWidth;
    OutAtlas->PageHeight = InPageHeight;
    OutAtlas->SpriteCount = static_cast<std::uint32_t>(InSources.size());

    std::uint32_t ScopedCursorX = 0;
    std::uint32_t ScopedCursorY = 0;
    std::uint32_t ScopedRowHeight = 0;
    for (std::uint32_t ScopedOrderIndex = 0; ScopedOrderIndex < InSources.size(); ++ScopedOrderIndex)
    {
        const std::uint32_t ScopedIndex = ScopedOrder[ScopedOrderIndex];
        const AtlasSpriteSource& ScopedSource = InSources[ScopedIndex];
        const std::uint32_t ScopedPadding = ScopedSource.Padding;
        const std::uint64_t ScopedCellWidth = static_cast<std::uint64_t>(ScopedSource.Width) + ScopedPadding * 2ull;
        const std::uint64_t ScopedCellHeight = static_cast<std::uint64_t>(ScopedSource.Height) + ScopedPadding * 2ull;
        if (ScopedCellWidth > InPageWidth || ScopedCellHeight > InPageHeight)
        {
            return AtlasPackError::DoesNotFit;
        }
        if (ScopedCursorX + ScopedCellWidth > InPageWidth)
        {
            ScopedCursorX = 0;
            ScopedCursorY += ScopedRowHeight;
            ScopedRowHeight = 0;
        }
        if (ScopedCursorY + ScopedCellHeight > InPageHeight)
        {
            return AtlasPackError::DoesNotFit;
        }
        AtlasSprite& ScopedSprite = OutAtlas->Sprites[ScopedIndex];
        ScopedSprite.Id = ScopedSource.Id;
        ScopedSprite.X = ScopedCursorX + ScopedPadding;
        ScopedSprite.Y = ScopedCursorY + ScopedPadding;
        ScopedSprite.Width = ScopedSource.Width;
        ScopedSprite.Height = ScopedSource.Height;
        ScopedSprite.PivotX = 0.5f;
        ScopedSprite.PivotY = 0.5f;
        ScopedCursorX += static_cast<std::uint32_t>(ScopedCellWidth);
        ScopedRowHeight = std::max(ScopedRowHeight, static_cast<std::uint32_t>(ScopedCellHeight));
    }
    return AtlasPackError::None;
}

bool LoadSpriteAtlasAsset(std::string_view InText, SpriteAtlasAsset* OutAsset, std::string* InError) noexcept
{
    const auto ScopedFail = [InError](const char* InMessage)
    {
        if (InError != nullptr)
        {
            *InError = InMessage;
        }
        return false;
    };
    if (OutAsset == nullptr)
    {
        return ScopedFail("atlas: null destination");
    }
    *OutAsset = SpriteAtlasAsset{};

    std::size_t ScopedCursor = 0;
    bool ScopedHeader = false;
    bool ScopedPage = false;
    std::uint32_t ScopedSpriteCount = 0;
    while (ScopedCursor < InText.size())
    {
        const std::size_t ScopedLineEnd = InText.find('\n', ScopedCursor);
        const std::size_t ScopedEnd = ScopedLineEnd == std::string_view::npos ? InText.size() : ScopedLineEnd;
        std::string_view ScopedLine = InText.substr(ScopedCursor, ScopedEnd - ScopedCursor);
        ScopedCursor = ScopedEnd + (ScopedLineEnd == std::string_view::npos ? 0 : 1);
        if (!ScopedLine.empty() && ScopedLine.back() == '\r')
        {
            ScopedLine.remove_suffix(1);
        }
        if (ScopedLine.empty())
        {
            continue;
        }
        if (!ScopedHeader)
        {
            if (ScopedLine != "FBYATLAS 1")
            {
                return ScopedFail("atlas: missing FBYATLAS 1 header");
            }
            ScopedHeader = true;
            continue;
        }
        const std::string ScopedText(ScopedLine);
        if (ScopedLine.starts_with("page "))
        {
            if (ScopedPage)
            {
                return ScopedFail("atlas: duplicate page line");
            }
            unsigned int ScopedWidth = 0;
            unsigned int ScopedHeight = 0;
            char ScopedPath[kAtlasPagePathBytes]{};
            if (std::sscanf(ScopedText.c_str(), "page %u %u %259s", &ScopedWidth, &ScopedHeight, ScopedPath) != 3 ||
                ScopedWidth == 0 || ScopedHeight == 0 || ScopedWidth > 16384 || ScopedHeight > 16384)
            {
                return ScopedFail("atlas: invalid page line");
            }
            OutAsset->Atlas.PageWidth = ScopedWidth;
            OutAsset->Atlas.PageHeight = ScopedHeight;
            std::memcpy(OutAsset->PagePath.data(), ScopedPath, std::min<std::size_t>(std::strlen(ScopedPath), kAtlasPagePathBytes - 1));
            ScopedPage = true;
            continue;
        }
        if (ScopedLine.starts_with("sprite "))
        {
            if (!ScopedPage)
            {
                return ScopedFail("atlas: sprite before page");
            }
            if (ScopedSpriteCount >= kMaxAtlasSprites)
            {
                return ScopedFail("atlas: too many sprites");
            }
            unsigned int ScopedId = 0;
            unsigned int ScopedX = 0;
            unsigned int ScopedY = 0;
            unsigned int ScopedWidth = 0;
            unsigned int ScopedHeight = 0;
            float ScopedPivotX = 0.5f;
            float ScopedPivotY = 0.5f;
            if (std::sscanf(ScopedText.c_str(), "sprite %u %u %u %u %u %f %f", &ScopedId, &ScopedX, &ScopedY,
                            &ScopedWidth, &ScopedHeight, &ScopedPivotX, &ScopedPivotY) != 7 ||
                ScopedId == 0 || ScopedWidth == 0 || ScopedHeight == 0 ||
                !std::isfinite(ScopedPivotX) || !std::isfinite(ScopedPivotY))
            {
                return ScopedFail("atlas: invalid sprite line");
            }
            if (ScopedX > OutAsset->Atlas.PageWidth || ScopedWidth > OutAsset->Atlas.PageWidth - ScopedX ||
                ScopedY > OutAsset->Atlas.PageHeight || ScopedHeight > OutAsset->Atlas.PageHeight - ScopedY)
            {
                return ScopedFail("atlas: sprite outside the page");
            }
            AtlasSprite& ScopedSprite = OutAsset->Atlas.Sprites[ScopedSpriteCount];
            ScopedSprite.Id = ScopedId;
            ScopedSprite.X = ScopedX;
            ScopedSprite.Y = ScopedY;
            ScopedSprite.Width = ScopedWidth;
            ScopedSprite.Height = ScopedHeight;
            ScopedSprite.PivotX = ScopedPivotX;
            ScopedSprite.PivotY = ScopedPivotY;
            for (std::uint32_t ScopedI = 0; ScopedI < ScopedSpriteCount; ++ScopedI)
            {
                if (OutAsset->Atlas.Sprites[ScopedI].Id == ScopedId)
                {
                    return ScopedFail("atlas: duplicate sprite id");
                }
            }
            ++ScopedSpriteCount;
            continue;
        }
        return ScopedFail("atlas: unknown line");
    }
    if (!ScopedHeader || !ScopedPage || ScopedSpriteCount == 0)
    {
        return ScopedFail("atlas: incomplete file");
    }
    OutAsset->Atlas.SpriteCount = ScopedSpriteCount;
    OutAsset->Valid = true;
    return true;
}

const AtlasSprite* FindAtlasSprite(const SpriteAtlas& InAtlas, std::uint32_t InId) noexcept
{
    const std::uint32_t ScopedCount = std::min(InAtlas.SpriteCount, kMaxAtlasSprites);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        if (InAtlas.Sprites[ScopedI].Id == InId)
        {
            return &InAtlas.Sprites[ScopedI];
        }
    }
    return nullptr;
}

bool AtlasSpriteUv(const SpriteAtlas& InAtlas, std::uint32_t InId, AtlasUv* OutUv) noexcept
{
    if (OutUv == nullptr || InAtlas.PageWidth == 0 || InAtlas.PageHeight == 0)
    {
        return false;
    }
    const AtlasSprite* ScopedSprite = FindAtlasSprite(InAtlas, InId);
    if (ScopedSprite == nullptr)
    {
        return false;
    }
    const float ScopedWidth = static_cast<float>(InAtlas.PageWidth);
    const float ScopedHeight = static_cast<float>(InAtlas.PageHeight);
    OutUv->Min = Vector2D{static_cast<float>(ScopedSprite->X) / ScopedWidth,
                          static_cast<float>(ScopedSprite->Y) / ScopedHeight};
    OutUv->Max = Vector2D{static_cast<float>(ScopedSprite->X + ScopedSprite->Width) / ScopedWidth,
                          static_cast<float>(ScopedSprite->Y + ScopedSprite->Height) / ScopedHeight};
    return true;
}

}
