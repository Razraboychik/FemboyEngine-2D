// FemboyEngine2D - fixed-capacity shape cache for the ECS/DOD text path.
#pragma once

#include "Engine/Text/TextShapedLayout.hpp"
#include "Engine/Text/TextShaper.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Text
{

inline constexpr std::uint32_t kMaxShapeEntries = 256;
inline constexpr std::uint32_t kMaxShapeGlyphs = 16384;
inline constexpr std::uint32_t kMaxShapeLines = 4096;

struct TextShapeHandle
{
    std::uint32_t Index = 0xFFFFFFFFu;
    std::uint32_t Generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Index != 0xFFFFFFFFu && Generation != 0;
    }

    [[nodiscard]] friend constexpr bool operator==(const TextShapeHandle&, const TextShapeHandle&) noexcept = default;
};

struct TextShapeKey
{
    std::uint64_t TextHash = 0;
    std::uint32_t FontId = 0;
    std::uint32_t TextLength = 0;
    float PixelSize = 0.0f;
    TextDirection Direction = TextDirection::Auto;
    std::uint32_t LocaleHash = 0;
    // Inline atoms change wrapping because their width replaces the placeholder
    // advance; the layout identity must include them.
    std::uint64_t InlineHash = 0;
    // Layout identity: wrapping width and vertical mode change shaped lines.
    float MaxWidth = 0.0f;
    bool Vertical = false;

    [[nodiscard]] friend constexpr bool operator==(const TextShapeKey&, const TextShapeKey&) noexcept = default;
};

struct TextShapeView
{
    std::span<const ShapedGlyph> Glyphs{};
    std::span<const ShapedLine> Lines{};
    float Width = 0.0f;
};

// SoA metadata + flat glyph/line pools. Insert() is the only allocating step
// for HarfBuzz and is expected to run on a cache miss, never per frame.
class TextShapeCache
{
public:
    [[nodiscard]] static std::uint64_t HashText(std::string_view InText) noexcept;
    [[nodiscard]] static std::uint64_t HashInlines(std::span<const TextInlineObject> InInlines) noexcept;

    [[nodiscard]] TextShapeHandle Find(const TextShapeKey& InKey) const noexcept;
    [[nodiscard]] TextShapeView View(TextShapeHandle InHandle) const noexcept;

    [[nodiscard]] TextShapeHandle Insert(ITextShaper& InShaper, std::string_view InText,
                                         const TextShapeKey& InKey, const TextLayoutParams& InLayoutParams,
                                         TextShapeResult* InScratch = nullptr) noexcept;

    void Clear() noexcept;

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MLiveCount;
    }
    [[nodiscard]] std::uint32_t Evictions() const noexcept
    {
        return MEvictions;
    }
    // Lookup metrics. A hit is one Find call that matched a live key and must
    // not touch the shaper; a miss is one Insert that had to shape.
    [[nodiscard]] std::uint32_t Lookups() const noexcept
    {
        return MLookups;
    }
    [[nodiscard]] std::uint32_t Hits() const noexcept
    {
        return MHits;
    }
    [[nodiscard]] std::uint32_t Misses() const noexcept
    {
        return MMisses;
    }

private:
    [[nodiscard]] std::uint32_t AcquireSlot() noexcept;
    [[nodiscard]] std::uint32_t FindOldest() const noexcept;
    [[nodiscard]] bool EvictOldest() noexcept;
    void Compact() noexcept;
    void Touch(std::uint32_t InIndex) const noexcept;

    std::array<TextShapeKey, kMaxShapeEntries> MKeys{};
    mutable std::array<std::uint64_t, kMaxShapeEntries> MLastUse{};
    std::array<std::uint32_t, kMaxShapeEntries> MGenerations{};
    std::array<std::uint32_t, kMaxShapeEntries> MGlyphBegin{};
    std::array<std::uint32_t, kMaxShapeEntries> MGlyphCount{};
    std::array<std::uint32_t, kMaxShapeEntries> MLineBegin{};
    std::array<std::uint32_t, kMaxShapeEntries> MLineCount{};
    std::array<float, kMaxShapeEntries> MWidths{};
    std::array<ShapedGlyph, kMaxShapeGlyphs> MGlyphs{};
    std::array<ShapedLine, kMaxShapeLines> MLines{};
    // Shared shaping buffer for misses, so Insert never builds a 73 KB array on
    // the stack.
    TextShapeResult MScratch{};
    mutable std::uint32_t MLookups = 0;
    mutable std::uint32_t MHits = 0;
    std::uint32_t MMisses = 0;
    std::uint32_t MGlyphUsed = 0;
    std::uint32_t MLineUsed = 0;
    std::uint32_t MLiveCount = 0;
    std::uint32_t MEvictions = 0;
    std::uint32_t MNextGeneration = 1;
    mutable std::uint64_t MClock = 1;
};
}
