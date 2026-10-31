// FemboyEngine2D - shaping and bidi boundary types.
//
// The current renderer still maps codepoints directly. These types are the
// stable boundary for the HarfBuzz/cluster pipeline so layout, hit-testing and
// the atlas can move to glyph ids without changing their public callers again.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Text
{

inline constexpr std::uint32_t kMaxShapedGlyphs = 2048;
inline constexpr std::uint32_t kMaxTextRuns = 512;

enum class TextDirection : std::uint8_t
{
    Auto = 0,
    LeftToRight,
    RightToLeft,
    TopToBottom,
    BottomToTop,
};

enum class TextScript : std::uint32_t
{
    Unknown = 0,
    Latin,
    Cyrillic,
    Greek,
    Arabic,
    Hebrew,
    Devanagari,
    Bengali,
    Thai,
    Han,
    Hiragana,
    Katakana,
    Hangul,
};

struct TextRun
{
    std::uint32_t Begin = 0;
    std::uint32_t End = 0;
    TextScript Script = TextScript::Unknown;
    TextDirection Direction = TextDirection::LeftToRight;
    std::uint8_t BidiLevel = 0;
};

struct ShapedGlyph
{
    // OpenType glyph id once HarfBuzz is active; codepoint in the compatibility
    // shaper. Cluster is the original UTF-8 byte offset.
    std::uint32_t GlyphId = 0;
    std::uint32_t Cluster = 0;
    // Atlas/font id that produced the glyph; 0 means the caller's base font.
    std::uint32_t FontId = 0;
    float AdvanceX = 0.0f;
    float AdvanceY = 0.0f;
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
    std::uint32_t Flags = 0;
};

struct TextShapeParams
{
    std::uint32_t FontId = 0;
    float PixelSize = 16.0f;
    TextDirection Direction = TextDirection::Auto;
    TextScript Script = TextScript::Unknown;
    bool Vertical = false;
    std::string_view Language{};
    std::span<const TextRun> Runs{};
};

struct TextShapeResult
{
    std::array<ShapedGlyph, kMaxShapedGlyphs> Glyphs{};
    std::uint32_t Count = 0;
    bool Overflow = false;
};

class ITextShaper
{
public:
    virtual ~ITextShaper() = default;
    [[nodiscard]] virtual bool Shape(std::string_view InText, const TextShapeParams& InParams,
                                     TextShapeResult* OutResult) noexcept = 0;
};

// Compatibility shaper used until a glyph-id atlas is mounted. It emits one
// glyph per UTF-8 scalar so layout and tests can already consume ShapedGlyph.
class CodepointShaper final : public ITextShaper
{
public:
    [[nodiscard]] bool Shape(std::string_view InText, const TextShapeParams& InParams,
                             TextShapeResult* OutResult) noexcept override;

    std::uint32_t ShapeCalls = 0;
};

// OpenType shaping backend. The font bytes are copied so the caller may release
// its VFS mapping; the shaped glyph ids are intended for a glyph-id atlas.
class HarfBuzzShaper final : public ITextShaper
{
public:
    HarfBuzzShaper();
    ~HarfBuzzShaper() override;
    HarfBuzzShaper(const HarfBuzzShaper&) = delete;
    HarfBuzzShaper& operator=(const HarfBuzzShaper&) = delete;

    [[nodiscard]] bool SetFont(std::span<const std::byte> InFontBytes, float InPixelSize,
                               std::uint32_t InFontId = 0) noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool IsReady() const noexcept;
    // Live hb_font_t handles owned by the engine; useful for leak/resource tests.
    [[nodiscard]] static std::uint32_t ActiveFonts() noexcept;
    [[nodiscard]] bool Shape(std::string_view InText, const TextShapeParams& InParams,
                             TextShapeResult* OutResult) noexcept override;

private:
    struct Impl;
    Impl* MImpl = nullptr;
};

// Ordered backend chain: the first result without a .notdef glyph wins.
class ChainedShaper final : public ITextShaper
{
public:
    [[nodiscard]] bool SetShapers(std::span<ITextShaper* const> InShapers) noexcept
    {
        if (InShapers.size() > MShapers.size())
        {
            return false;
        }
        MShapers = {};
        for (std::size_t ScopedIndex = 0; ScopedIndex < InShapers.size(); ++ScopedIndex)
        {
            MShapers[ScopedIndex] = InShapers[ScopedIndex];
        }
        MCount = static_cast<std::uint32_t>(InShapers.size());
        return true;
    }
    [[nodiscard]] bool Add(ITextShaper* InShaper) noexcept
    {
        if (InShaper == nullptr || MCount >= MShapers.size())
        {
            return false;
        }
        MShapers[MCount++] = InShaper;
        return true;
    }
    void Clear() noexcept
    {
        MShapers = {};
        MCount = 0;
    }
    [[nodiscard]] const TextShapeResult& Scratch() const noexcept
    {
        return MScratch;
    }

    [[nodiscard]] bool Shape(std::string_view InText, const TextShapeParams& InParams,
                             TextShapeResult* OutResult) noexcept override
    {
        if (OutResult == nullptr)
        {
            return false;
        }
        // The chain owns the candidate buffer: a miss must not build a 73 KB
        // temporary on the stack for every backend it tries.
        TextShapeResult& ScopedCandidate = MScratch;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            if (MShapers[ScopedIndex] == nullptr ||
                !MShapers[ScopedIndex]->Shape(InText, InParams, &ScopedCandidate))
            {
                continue;
            }
            bool ScopedHasNotdef = false;
            for (std::uint32_t ScopedGlyph = 0; ScopedGlyph < ScopedCandidate.Count; ++ScopedGlyph)
            {
                ScopedHasNotdef = ScopedHasNotdef || ScopedCandidate.Glyphs[ScopedGlyph].GlyphId == 0;
            }
            if (!ScopedHasNotdef || ScopedIndex + 1u == MCount)
            {
                *OutResult = ScopedCandidate;
                return true;
            }
        }
        return false;
    }

private:
    std::array<ITextShaper*, 4> MShapers{};
    TextShapeResult MScratch;
    std::uint32_t MCount = 0;
};

// Script router: picks the matching backend for a run, with one fallback.
class ScriptShaper final : public ITextShaper
{
public:
    [[nodiscard]] bool AddScript(TextScript InScript, ITextShaper* InShaper) noexcept
    {
        if (InShaper == nullptr || MCount >= MEntries.size())
        {
            return false;
        }
        MEntries[MCount++] = Entry{InScript, InShaper};
        return true;
    }
    void SetFallback(ITextShaper* InShaper) noexcept
    {
        MFallback = InShaper;
    }
    void Clear() noexcept
    {
        MEntries = {};
        MCount = 0;
        MFallback = nullptr;
    }
    [[nodiscard]] std::uint32_t ScriptCount() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] bool Shape(std::string_view InText, const TextShapeParams& InParams,
                             TextShapeResult* OutResult) noexcept override
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            if (MEntries[ScopedIndex].Script == InParams.Script)
            {
                return MEntries[ScopedIndex].Shaper->Shape(InText, InParams, OutResult);
            }
        }
        return MFallback != nullptr ? MFallback->Shape(InText, InParams, OutResult) : false;
    }

private:
    struct Entry
    {
        TextScript Script = TextScript::Unknown;
        ITextShaper* Shaper = nullptr;
    };
    std::array<Entry, 16> MEntries{};
    std::uint32_t MCount = 0;
    ITextShaper* MFallback = nullptr;
};

// Splits with bidi, shapes each run in its own direction/script, and appends
// the results in the visual run order supplied by SheenBidi. A non-Unknown
// InParams.Script forces every run onto that script, which is how a caller
// switches the preview face instead of only switching the shaping parameters.
[[nodiscard]] bool ShapeBidiText(ITextShaper& InShaper, std::string_view InText,
                                 const TextShapeParams& InParams, TextShapeResult* OutResult) noexcept;
}
