// FemboyEngine2D - tilemap queries and the tilemap render bridge.
#include "Engine/ECS/Tilemap.hpp"

#include "Engine/Assets/VFS.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace Fby
{
namespace Tilemap
{

bool Contains(const TilemapComponent& InMap, Grid::Cell InCell) noexcept
{
    if (InCell.X < 0 || InCell.Y < 0)
    {
        return false;
    }
    if (static_cast<std::uint32_t>(InCell.X) >= InMap.Width)
    {
        return false;
    }
    return static_cast<std::uint32_t>(InCell.Y) < InMap.Height;
}

std::uint32_t At(const TilemapComponent& InMap, Grid::Cell InCell) noexcept
{
    if (!Contains(InMap, InCell))
    {
        return 0;
    }
    const std::size_t ScopedIndex =
        static_cast<std::size_t>(InCell.Y) * static_cast<std::size_t>(InMap.Width) + static_cast<std::size_t>(InCell.X);
    if (ScopedIndex >= kTilemapMaxTiles)
    {
        return 0;
    }
    return InMap.Tiles[ScopedIndex];
}

bool Set(TilemapComponent& InMap, Grid::Cell InCell, std::uint32_t InTile) noexcept
{
    if (!Contains(InMap, InCell))
    {
        return false;
    }
    const std::size_t ScopedIndex =
        static_cast<std::size_t>(InCell.Y) * static_cast<std::size_t>(InMap.Width) + static_cast<std::size_t>(InCell.X);
    if (ScopedIndex >= kTilemapMaxTiles)
    {
        return false;
    }
    InMap.Tiles[ScopedIndex] = InTile;
    return true;
}

void Clear(TilemapComponent& InMap) noexcept
{
    InMap.Tiles.fill(0);
}

void Resize(TilemapComponent& InMap, std::uint32_t InWidth, std::uint32_t InHeight) noexcept
{
    InMap.Width = std::min(InWidth, kTilemapMaxWidth);
    InMap.Height = std::min(InHeight, kTilemapMaxHeight);
    Clear(InMap);
}

bool IsSolid(const TilemapComponent& InMap, Grid::Cell InCell) noexcept
{
    const std::uint32_t ScopedTile = At(InMap, InCell);
    if (TileEmpty(ScopedTile))
    {
        return false;
    }
    if ((InMap.Flags & kFlagNoCollision) != 0)
    {
        return false;
    }
    return (ScopedTile & kTileFlagSolid) != 0;
}

Vector2D CellToWorld(const TilemapComponent& InMap, Grid::Cell InCell) noexcept
{
    return Grid::CellToWorld(InMap.Grid, InCell);
}

Grid::Cell WorldToCell(const TilemapComponent& InMap, Vector2D InWorld) noexcept
{
    return Grid::WorldToCell(InMap.Grid, InWorld);
}

bool CellBounds(const TilemapComponent& InMap, Grid::Cell InCell, Vector2D* OutMin, Vector2D* OutMax) noexcept
{
    if (OutMin == nullptr || OutMax == nullptr)
    {
        return false;
    }
    const Vector2D ScopedCenter = CellToWorld(InMap, InCell);
    const Vector2D ScopedExtent = Grid::CellExtent(InMap.Grid);
    const std::uint32_t ScopedTile = At(InMap, InCell);
    const float ScopedLift = static_cast<float>(TileHeightOf(ScopedTile)) * InMap.Grid.HeightStep;
    const Vector2D ScopedHalf = ScopedExtent * 0.5f;
    *OutMin = Vector2D{ScopedCenter.X - ScopedHalf.X, ScopedCenter.Y - ScopedHalf.Y - ScopedLift};
    *OutMax = Vector2D{ScopedCenter.X + ScopedHalf.X, ScopedCenter.Y + ScopedHalf.Y - ScopedLift};
    return true;
}

bool TileUv(const TilemapComponent& InMap, std::uint16_t InIndex, Vector2D* OutMin, Vector2D* OutMax) noexcept
{
    if (OutMin == nullptr || OutMax == nullptr)
    {
        return false;
    }
    if (InIndex == 0)
    {
        return false;
    }
    const std::uint32_t ScopedColumns = InMap.AtlasColumns > 0 ? InMap.AtlasColumns : 1;
    const std::uint32_t ScopedRows = InMap.AtlasRows > 0 ? InMap.AtlasRows : 1;
    const std::uint32_t ScopedZeroBased = static_cast<std::uint32_t>(InIndex) - 1u;
    const std::uint32_t ScopedColumn = ScopedZeroBased % ScopedColumns;
    const std::uint32_t ScopedRow = ScopedZeroBased / ScopedColumns;
    if (ScopedRow >= ScopedRows)
    {
        return false;
    }
    const float ScopedCellWidth = 1.0f / static_cast<float>(ScopedColumns);
    const float ScopedCellHeight = 1.0f / static_cast<float>(ScopedRows);
    const float ScopedU = static_cast<float>(ScopedColumn) * ScopedCellWidth;
    const float ScopedV = static_cast<float>(ScopedRow) * ScopedCellHeight;
    *OutMin = Vector2D{ScopedU, ScopedV};
    *OutMax = Vector2D{ScopedU + ScopedCellWidth, ScopedV + ScopedCellHeight};
    return true;
}
}

namespace
{
[[nodiscard]] std::string_view Trim(std::string_view InText) noexcept
{
    while (!InText.empty() && (InText.front() == ' ' || InText.front() == '\t' || InText.front() == '\r'))
    {
        InText.remove_prefix(1);
    }
    while (!InText.empty() && (InText.back() == ' ' || InText.back() == '\t' || InText.back() == '\r'))
    {
        InText.remove_suffix(1);
    }
    return InText;
}

[[nodiscard]] bool ParseInt(std::string_view InText, std::int32_t* Out) noexcept
{
    if (InText.empty() || Out == nullptr)
    {
        return false;
    }
    const char* ScopedBegin = InText.data();
    const char* ScopedEnd = InText.data() + InText.size();
    const auto ScopedResult = std::from_chars(ScopedBegin, ScopedEnd, *Out);
    return ScopedResult.ec == std::errc{} && ScopedResult.ptr == ScopedEnd;
}

[[nodiscard]] bool ParseFloat(std::string_view InText, float* Out) noexcept
{
    if (InText.empty() || Out == nullptr)
    {
        return false;
    }
    std::string ScopedCopy(InText);
    char* ScopedEnd = nullptr;
    const float ScopedValue = std::strtof(ScopedCopy.c_str(), &ScopedEnd);
    if (ScopedEnd == ScopedCopy.c_str() || *ScopedEnd != '\0')
    {
        return false;
    }
    *Out = ScopedValue;
    return true;
}

[[nodiscard]] std::string_view NextToken(std::string_view* Cursor) noexcept
{
    std::string_view ScopedText = Trim(*Cursor);
    const std::size_t ScopedSpace = ScopedText.find_first_of(" \t");
    std::string_view ScopedToken = ScopedText;
    if (ScopedSpace != std::string_view::npos)
    {
        ScopedToken = ScopedText.substr(0, ScopedSpace);
        *Cursor = ScopedText.substr(ScopedSpace + 1);
    }
    else
    {
        *Cursor = {};
    }
    return ScopedToken;
}

void AppendNumber(std::string& OutText, float InValue)
{
    char ScopedBuffer[32] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%.3g", static_cast<double>(InValue));
    OutText += ScopedBuffer;
}

void AppendUnsigned(std::string& OutText, std::uint32_t InValue)
{
    char ScopedBuffer[16] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%u", static_cast<unsigned>(InValue));
    OutText += ScopedBuffer;
}

void AppendHex(std::string& OutText, std::uint32_t InValue)
{
    char ScopedBuffer[16] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%08X", static_cast<unsigned>(InValue));
    OutText += ScopedBuffer;
}

[[nodiscard]] bool ParseHex(std::string_view InText, std::uint32_t* Out) noexcept
{
    if (InText.empty() || Out == nullptr || InText.size() > 8)
    {
        return false;
    }
    std::uint32_t ScopedValue = 0;
    const auto ScopedResult = std::from_chars(InText.data(), InText.data() + InText.size(), ScopedValue, 16);
    if (ScopedResult.ec != std::errc{} || ScopedResult.ptr != InText.data() + InText.size())
    {
        return false;
    }
    *Out = ScopedValue;
    return true;
}
}

bool Tilemap::SaveText(const TilemapComponent& InMap, std::string* OutText)
{
    if (OutText == nullptr)
    {
        return false;
    }
    std::string ScopedText;
    ScopedText.reserve(256 + static_cast<std::size_t>(InMap.Width) * InMap.Height * 9);
    ScopedText += "tilemap 1\n";
    ScopedText += "mode ";
    ScopedText += Grid::ProjectionModeName(InMap.Grid.Mode);
    ScopedText += "\ncell ";
    AppendNumber(ScopedText, InMap.Grid.CellSize.X);
    ScopedText += ' ';
    AppendNumber(ScopedText, InMap.Grid.CellSize.Y);
    ScopedText += "\norigin ";
    AppendNumber(ScopedText, InMap.Grid.Origin.X);
    ScopedText += ' ';
    AppendNumber(ScopedText, InMap.Grid.Origin.Y);
    ScopedText += "\nsize ";
    AppendUnsigned(ScopedText, InMap.Width);
    ScopedText += ' ';
    AppendUnsigned(ScopedText, InMap.Height);
    ScopedText += "\natlas ";
    AppendUnsigned(ScopedText, InMap.AtlasColumns);
    ScopedText += ' ';
    AppendUnsigned(ScopedText, InMap.AtlasRows);
    ScopedText += ' ';
    AppendUnsigned(ScopedText, InMap.TextureLayer);
    ScopedText += "\nsort ";
    AppendUnsigned(ScopedText, InMap.SortLayer);
    ScopedText += "\ntint ";
    AppendHex(ScopedText, InMap.Tint);
    ScopedText += "\ntiles";
    const std::size_t ScopedCount = static_cast<std::size_t>(InMap.Width) * InMap.Height;
    for (std::size_t ScopedI = 0; ScopedI < ScopedCount && ScopedI < kTilemapMaxTiles; ++ScopedI)
    {
        ScopedText += ScopedI == 0 ? ' ' : ',';
        AppendHex(ScopedText, InMap.Tiles[ScopedI]);
    }
    ScopedText += '\n';
    *OutText = std::move(ScopedText);
    return true;
}

bool Tilemap::LoadText(std::string_view InText, TilemapComponent* OutMap, std::string* InError)
{
    if (OutMap == nullptr)
    {
        return false;
    }
    TilemapComponent ScopedMap;
    bool ScopedSawHeader = false;
    bool ScopedSawTiles = false;
    std::uint32_t ScopedExpectedTiles = 0;
    std::uint32_t ScopedLineNumber = 0;
    const auto ScopedFail = [InError](std::uint32_t InLine, const char* InMessage)
    {
        if (InError != nullptr)
        {
            *InError = "tilemap line " + std::to_string(InLine) + ": " + InMessage;
        }
        return false;
    };

    while (!InText.empty())
    {
        const std::size_t ScopedEnd = InText.find('\n');
        const std::string_view ScopedLine =
            Trim(ScopedEnd == std::string_view::npos ? InText : InText.substr(0, ScopedEnd));
        InText = ScopedEnd == std::string_view::npos ? std::string_view{} : InText.substr(ScopedEnd + 1);
        ++ScopedLineNumber;
        if (ScopedLine.empty() || ScopedLine.front() == '#')
        {
            continue;
        }
        std::string_view ScopedRest = ScopedLine;
        const std::string_view ScopedKey = NextToken(&ScopedRest);
        if (ScopedKey == "tilemap")
        {
            std::int32_t ScopedVersion = 0;
            if (!ParseInt(Trim(ScopedRest), &ScopedVersion) || ScopedVersion != 1)
            {
                return ScopedFail(ScopedLineNumber, "unsupported tilemap version");
            }
            ScopedSawHeader = true;
            continue;
        }
        if (ScopedKey == "mode")
        {
            if (!Grid::ParseProjectionMode(NextToken(&ScopedRest), &ScopedMap.Grid.Mode))
            {
                return ScopedFail(ScopedLineNumber, "unknown projection mode");
            }
            continue;
        }
        if (ScopedKey == "cell")
        {
            if (!ParseFloat(NextToken(&ScopedRest), &ScopedMap.Grid.CellSize.X) ||
                !ParseFloat(NextToken(&ScopedRest), &ScopedMap.Grid.CellSize.Y))
            {
                return ScopedFail(ScopedLineNumber, "cell needs two numbers");
            }
            continue;
        }
        if (ScopedKey == "origin")
        {
            if (!ParseFloat(NextToken(&ScopedRest), &ScopedMap.Grid.Origin.X) ||
                !ParseFloat(NextToken(&ScopedRest), &ScopedMap.Grid.Origin.Y))
            {
                return ScopedFail(ScopedLineNumber, "origin needs two numbers");
            }
            continue;
        }
        if (ScopedKey == "size")
        {
            std::int32_t ScopedWidth = 0;
            std::int32_t ScopedHeight = 0;
            if (!ParseInt(NextToken(&ScopedRest), &ScopedWidth) || !ParseInt(NextToken(&ScopedRest), &ScopedHeight) ||
                ScopedWidth <= 0 || ScopedHeight <= 0 || ScopedWidth > static_cast<std::int32_t>(kTilemapMaxWidth) ||
                ScopedHeight > static_cast<std::int32_t>(kTilemapMaxHeight))
            {
                return ScopedFail(ScopedLineNumber, "size is out of range");
            }
            ScopedMap.Width = static_cast<std::uint32_t>(ScopedWidth);
            ScopedMap.Height = static_cast<std::uint32_t>(ScopedHeight);
            ScopedExpectedTiles = ScopedMap.Width * ScopedMap.Height;
            continue;
        }
        if (ScopedKey == "atlas")
        {
            std::int32_t ScopedColumns = 0;
            std::int32_t ScopedRows = 0;
            std::int32_t ScopedLayer = 0;
            if (!ParseInt(NextToken(&ScopedRest), &ScopedColumns) || !ParseInt(NextToken(&ScopedRest), &ScopedRows) ||
                !ParseInt(NextToken(&ScopedRest), &ScopedLayer) || ScopedColumns <= 0 || ScopedRows <= 0 ||
                ScopedLayer < 0)
            {
                return ScopedFail(ScopedLineNumber, "atlas needs columns, rows and layer");
            }
            ScopedMap.AtlasColumns = static_cast<std::uint32_t>(ScopedColumns);
            ScopedMap.AtlasRows = static_cast<std::uint32_t>(ScopedRows);
            ScopedMap.TextureLayer = static_cast<std::uint32_t>(ScopedLayer);
            continue;
        }
        if (ScopedKey == "sort")
        {
            std::int32_t ScopedSort = 0;
            if (!ParseInt(NextToken(&ScopedRest), &ScopedSort) || ScopedSort < 0 || ScopedSort > 65535)
            {
                return ScopedFail(ScopedLineNumber, "sort layer is out of range");
            }
            ScopedMap.SortLayer = static_cast<std::uint16_t>(ScopedSort);
            continue;
        }
        if (ScopedKey == "tint")
        {
            std::uint32_t ScopedTint = 0;
            if (!ParseHex(NextToken(&ScopedRest), &ScopedTint))
            {
                return ScopedFail(ScopedLineNumber, "tint is not a hex word");
            }
            ScopedMap.Tint = ScopedTint;
            continue;
        }
        if (ScopedKey == "tiles")
        {
            if (ScopedExpectedTiles == 0)
            {
                return ScopedFail(ScopedLineNumber, "tiles line must follow the size line");
            }
            std::uint32_t ScopedIndex = 0;
            std::string_view ScopedPayload = Trim(ScopedRest);
            while (!ScopedPayload.empty() && ScopedIndex < ScopedExpectedTiles)
            {
                const std::size_t ScopedComma = ScopedPayload.find(',');
                const std::string_view ScopedWord =
                    Trim(ScopedComma == std::string_view::npos ? ScopedPayload : ScopedPayload.substr(0, ScopedComma));
                std::uint32_t ScopedTile = 0;
                if (!ParseHex(ScopedWord, &ScopedTile))
                {
                    return ScopedFail(ScopedLineNumber, "tile payload is not hex");
                }
                if (ScopedIndex < kTilemapMaxTiles)
                {
                    ScopedMap.Tiles[ScopedIndex] = ScopedTile;
                }
                ++ScopedIndex;
                ScopedPayload = ScopedComma == std::string_view::npos ? std::string_view{}
                                                                     : ScopedPayload.substr(ScopedComma + 1);
            }
            if (ScopedIndex != ScopedExpectedTiles)
            {
                return ScopedFail(ScopedLineNumber, "tile count does not match the map size");
            }
            ScopedSawTiles = true;
            continue;
        }
        return ScopedFail(ScopedLineNumber, "unknown key");
    }

    if (!ScopedSawHeader || ScopedMap.Width == 0 || ScopedMap.Height == 0 || !ScopedSawTiles)
    {
        if (InError != nullptr)
        {
            *InError = "tilemap is missing its header, size or tiles";
        }
        return false;
    }
    if (ScopedMap.Tint == 0)
    {
        ScopedMap.Tint = 0xFFFFFFFFu;
    }
    *OutMap = ScopedMap;
    return true;
}

bool Tilemap::LoadFromVfs(const VirtualFileSystem& InVfs, std::string_view InPath, TilemapComponent* OutMap,
                          std::string* InError)
{
    if (OutMap == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "tilemap: null output";
        }
        return false;
    }
    const std::optional<std::span<const std::byte>> ScopedBytes = InVfs.Read(InPath);
    if (!ScopedBytes.has_value() || ScopedBytes->empty())
    {
        if (InError != nullptr)
        {
            *InError = "tilemap: cannot read '" + std::string(InPath) + "'";
        }
        return false;
    }
    const std::string_view ScopedText(reinterpret_cast<const char*>(ScopedBytes->data()), ScopedBytes->size());
    return LoadText(ScopedText, OutMap, InError);
}

namespace
{
[[nodiscard]] bool OverlapsView(Vector2D InMin, Vector2D InMax, Vector2D InViewMin, Vector2D InViewMax) noexcept
{
    return InMax.X >= InViewMin.X && InMin.X <= InViewMax.X && InMax.Y >= InViewMin.Y && InMin.Y <= InViewMax.Y;
}

// Cell range of the four view corners plus one cell of margin. Iso projections
// map an axis-aligned view onto a rotated cell range, so the margin is what
// keeps the diamonds next to the border visible.
void ViewCellRange(const TilemapComponent& InMap, Vector2D InViewMin, Vector2D InViewMax, Grid::Cell* OutMin,
                   Grid::Cell* OutMax) noexcept
{
    const Grid::Cell ScopedCorners[4] = {Tilemap::WorldToCell(InMap, InViewMin),
                                         Tilemap::WorldToCell(InMap, Vector2D{InViewMax.X, InViewMin.Y}),
                                         Tilemap::WorldToCell(InMap, Vector2D{InViewMin.X, InViewMax.Y}),
                                         Tilemap::WorldToCell(InMap, InViewMax)};
    Grid::Cell ScopedMin = ScopedCorners[0];
    Grid::Cell ScopedMax = ScopedCorners[0];
    for (std::uint32_t ScopedI = 1; ScopedI < 4; ++ScopedI)
    {
        ScopedMin.X = std::min(ScopedMin.X, ScopedCorners[ScopedI].X);
        ScopedMin.Y = std::min(ScopedMin.Y, ScopedCorners[ScopedI].Y);
        ScopedMax.X = std::max(ScopedMax.X, ScopedCorners[ScopedI].X);
        ScopedMax.Y = std::max(ScopedMax.Y, ScopedCorners[ScopedI].Y);
    }
    *OutMin = Grid::Cell{ScopedMin.X - 1, ScopedMin.Y - 1};
    *OutMax = Grid::Cell{ScopedMax.X + 1, ScopedMax.Y + 1};
}
}

void TilemapRenderSystem::Collect(Ecs::Registry& InRegistry, Vector2D InViewMin, Vector2D InViewMax) noexcept
{
    MCount = 0;
    MCellCount = 0;
    MTruncated = false;

    auto ScopedView = InRegistry.view<TilemapComponent>();
    for (auto [ScopedEntity, ScopedMap] : ScopedView.each())
    {
        (void)ScopedEntity;
        if (ScopedMap.Width == 0 || ScopedMap.Height == 0)
        {
            continue;
        }
        Grid::Cell ScopedMin{};
        Grid::Cell ScopedMax{};
        ViewCellRange(ScopedMap, InViewMin, InViewMax, &ScopedMin, &ScopedMax);
        ScopedMin = Grid::Cell{std::max(ScopedMin.X, 0), std::max(ScopedMin.Y, 0)};
        ScopedMax = Grid::Cell{std::min(ScopedMax.X, static_cast<std::int32_t>(ScopedMap.Width) - 1),
                               std::min(ScopedMax.Y, static_cast<std::int32_t>(ScopedMap.Height) - 1)};
        if (ScopedMin.X > ScopedMax.X || ScopedMin.Y > ScopedMax.Y)
        {
            continue;
        }

        const Vector2D ScopedExtent = Grid::CellExtent(ScopedMap.Grid);
        for (std::int32_t ScopedY = ScopedMin.Y; ScopedY <= ScopedMax.Y; ++ScopedY)
        {
            for (std::int32_t ScopedX = ScopedMin.X; ScopedX <= ScopedMax.X; ++ScopedX)
            {
                const Grid::Cell ScopedCell{ScopedX, ScopedY};
                const std::uint32_t ScopedTile = Tilemap::At(ScopedMap, ScopedCell);
                if (TileEmpty(ScopedTile))
                {
                    continue;
                }
                ++MCellCount;
                if (MCount >= kMaxQuads)
                {
                    MTruncated = true;
                    continue;
                }
                Vector2D ScopedUvMin{};
                Vector2D ScopedUvMax{};
                if (!Tilemap::TileUv(ScopedMap, TileIndexOf(ScopedTile), &ScopedUvMin, &ScopedUvMax))
                {
                    continue;
                }

                const Vector2D ScopedCenter = Tilemap::CellToWorld(ScopedMap, ScopedCell);
                const float ScopedLift = static_cast<float>(TileHeightOf(ScopedTile)) * ScopedMap.Grid.HeightStep;
                const Vector2D ScopedPosition{ScopedCenter.X, ScopedCenter.Y - ScopedLift};

                Vector2D ScopedBoundsMin{};
                Vector2D ScopedBoundsMax{};
                (void)Tilemap::CellBounds(ScopedMap, ScopedCell, &ScopedBoundsMin, &ScopedBoundsMax);
                if (!OverlapsView(ScopedBoundsMin, ScopedBoundsMax, InViewMin, InViewMax))
                {
                    --MCellCount;
                    continue;
                }

                TileQuad ScopedQuad;
                ScopedQuad.Item.position = ScopedPosition;
                ScopedQuad.Item.size = ScopedExtent;
                ScopedQuad.Item.uvMin = ScopedUvMin;
                ScopedQuad.Item.uvMax = ScopedUvMax;
                ScopedQuad.Item.layer = ScopedMap.TextureLayer;
                ScopedQuad.Item.color = ScopedMap.Tint;
                ScopedQuad.Item.rotation = 0.0f;
                ScopedQuad.Item.flags = 0;
                const float ScopedDepth = Grid::SortDepth(ScopedMap.Grid, ScopedCell) + ScopedLift;
                ScopedQuad.Key = Rhi::MakeRenderKey(ScopedMap.SortLayer, ScopedDepth,
                                                    static_cast<std::uint8_t>((MCount + 1u) & 0xFFu));
                MQuads[MCount++] = ScopedQuad;
            }
        }
    }
}
}
