// FemboyEngine2D - Pink UI implementation.
#include "Engine/UI/PinkUI.hpp"

#include "Engine/Assets/Localization.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/CameraStack.hpp"
#include "Engine/Text/TextLayout.hpp"
#include "Engine/Text/TextRender.hpp"
#include "Engine/Text/TextService.hpp"
#include "Engine/Text/TextShapeCache.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace Fby::Ui
{
namespace
{

// Clip helpers are defined with the focus helpers below; the quad emitter and
// the focus predicate use them earlier in the file.
[[nodiscard]] bool RectInsideClip(const WidgetCanvas& InCanvas, std::uint32_t InWidget, Rect InRect) noexcept;
[[nodiscard]] bool TrimToClip(Rect InRect, Vector2D InUvMin, Vector2D InUvMax, Rect InClip, Rect& OutRect,
                              Vector2D& OutUvMin, Vector2D& OutUvMax) noexcept;

// UTF-8 editing helpers. TextInput offsets are byte offsets that the callers keep
// on a codepoint boundary.
[[nodiscard]] constexpr bool IsUtf8Continuation(char InByte) noexcept
{
    return (static_cast<unsigned char>(InByte) & 0xC0u) == 0x80u;
}

[[nodiscard]] std::uint16_t AlignUtf8Offset(std::string_view InText, std::uint16_t InOffset) noexcept
{
    std::uint16_t ScopedOffset =
        InOffset > InText.size() ? static_cast<std::uint16_t>(InText.size()) : InOffset;
    while (ScopedOffset > 0 && ScopedOffset < InText.size() && IsUtf8Continuation(InText[ScopedOffset]))
    {
        --ScopedOffset;
    }
    return ScopedOffset;
}

[[nodiscard]] std::uint16_t PreviousUtf8(std::string_view InText, std::uint16_t InOffset) noexcept
{
    if (InOffset == 0)
    {
        return 0;
    }
    std::uint16_t ScopedOffset = static_cast<std::uint16_t>(InOffset - 1);
    while (ScopedOffset > 0 && IsUtf8Continuation(InText[ScopedOffset]))
    {
        --ScopedOffset;
    }
    return ScopedOffset;
}

[[nodiscard]] std::uint16_t NextUtf8(std::string_view InText, std::uint16_t InOffset) noexcept
{
    if (InOffset >= InText.size())
    {
        return static_cast<std::uint16_t>(InText.size());
    }
    std::uint16_t ScopedOffset = static_cast<std::uint16_t>(InOffset + 1);
    while (ScopedOffset < InText.size() && IsUtf8Continuation(InText[ScopedOffset]))
    {
        ++ScopedOffset;
    }
    return ScopedOffset;
}

[[nodiscard]] std::uint32_t EncodeUtf8(std::uint32_t InCodepoint, char* OutBytes) noexcept
{
    if (InCodepoint < 0x80u)
    {
        OutBytes[0] = static_cast<char>(InCodepoint);
        return 1;
    }
    if (InCodepoint < 0x800u)
    {
        OutBytes[0] = static_cast<char>(0xC0u | (InCodepoint >> 6));
        OutBytes[1] = static_cast<char>(0x80u | (InCodepoint & 0x3Fu));
        return 2;
    }
    if (InCodepoint < 0x10000u)
    {
        OutBytes[0] = static_cast<char>(0xE0u | (InCodepoint >> 12));
        OutBytes[1] = static_cast<char>(0x80u | ((InCodepoint >> 6) & 0x3Fu));
        OutBytes[2] = static_cast<char>(0x80u | (InCodepoint & 0x3Fu));
        return 3;
    }
    if (InCodepoint <= 0x10FFFFu)
    {
        OutBytes[0] = static_cast<char>(0xF0u | (InCodepoint >> 18));
        OutBytes[1] = static_cast<char>(0x80u | ((InCodepoint >> 12) & 0x3Fu));
        OutBytes[2] = static_cast<char>(0x80u | ((InCodepoint >> 6) & 0x3Fu));
        OutBytes[3] = static_cast<char>(0x80u | (InCodepoint & 0x3Fu));
        return 4;
    }
    return 0;
}

// Caret blink: visible for the first half of every second.
[[nodiscard]] bool CaretBlinkOn(float InTime) noexcept
{
    return std::fmod(InTime, 1.0f) < 0.5f;
}

// Legacy 8x8 metric table (tools and text measurement only)
constexpr std::array<std::array<std::uint8_t, 8>, kFontGlyphCount> kGlyphTable = {{
    // 0x20 ' '
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 0x21 '!'
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    // 0x22 '"'
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 0x23 '#'
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    // 0x24 '$'
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},
    // 0x25 '%'
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
    // 0x26 '&'
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},
    // 0x27 '\''
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 0x28 '('
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
    // 0x29 ')'
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
    // 0x2A '*'
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    // 0x2B '+'
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
    // 0x2C ','
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    // 0x2D '-'
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    // 0x2E '.'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    // 0x2F '/'
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
    // 0x30 '0'
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
    // 0x31 '1'
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
    // 0x32 '2'
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
    // 0x33 '3'
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
    // 0x34 '4'
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},
    // 0x35 '5'
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
    // 0x36 '6'
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
    // 0x37 '7'
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
    // 0x38 '8'
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
    // 0x39 '9'
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
    // 0x3A ':'
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    // 0x3B ';'
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    // 0x3C '<'
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},
    // 0x3D '='
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
    // 0x3E '>'
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
    // 0x3F '?'
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
    // 0x40 '@'
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},
    // 0x41 'A'
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
    // 0x42 'B'
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
    // 0x43 'C'
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
    // 0x44 'D'
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
    // 0x45 'E'
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
    // 0x46 'F'
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},
    // 0x47 'G'
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
    // 0x48 'H'
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
    // 0x49 'I'
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    // 0x4A 'J'
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
    // 0x4B 'K'
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
    // 0x4C 'L'
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},
    // 0x4D 'M'
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
    // 0x4E 'N'
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
    // 0x4F 'O'
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
    // 0x50 'P'
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
    // 0x51 'Q'
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
    // 0x52 'R'
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
    // 0x53 'S'
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
    // 0x54 'T'
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    // 0x55 'U'
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
    // 0x56 'V'
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    // 0x57 'W'
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    // 0x58 'X'
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
    // 0x59 'Y'
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
    // 0x5A 'Z'
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
    // 0x5B '['
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
    // 0x5C '\\'
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},
    // 0x5D ']'
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
    // 0x5E '^'
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},
    // 0x5F '_'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    // 0x60 '`'
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 0x61 'a'
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
    // 0x62 'b'
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},
    // 0x63 'c'
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
    // 0x64 'd'
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00},
    // 0x65 'e'
    {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00},
    // 0x66 'f'
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00},
    // 0x67 'g'
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    // 0x68 'h'
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},
    // 0x69 'i'
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    // 0x6A 'j'
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},
    // 0x6B 'k'
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
    // 0x6C 'l'
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    // 0x6D 'm'
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    // 0x6E 'n'
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},
    // 0x6F 'o'
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
    // 0x70 'p'
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},
    // 0x71 'q'
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
    // 0x72 'r'
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},
    // 0x73 's'
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
    // 0x74 't'
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},
    // 0x75 'u'
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
    // 0x76 'v'
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    // 0x77 'w'
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    // 0x78 'x'
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},
    // 0x79 'y'
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    // 0x7A 'z'
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},
    // 0x7B '{'
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
    // 0x7C '|'
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    // 0x7D '}'
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
    // 0x7E '~'
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
}};

// Maps a byte to its glyph index. Everything outside printable ASCII collapses
[[nodiscard]] constexpr std::uint32_t GlyphIndex(char InGlyph) noexcept
{
    const auto ScopedCode = static_cast<std::uint32_t>(static_cast<unsigned char>(InGlyph));
    if (ScopedCode < kFontFirstChar || ScopedCode > kFontLastChar)
    {
        return static_cast<std::uint32_t>('?') - kFontFirstChar;
    }
    return ScopedCode - kFontFirstChar;
}

// Pen advance for one glyph. The cell is square but the pencil moves 0.75 of a
[[nodiscard]] constexpr float Advance(std::uint32_t InFontSize) noexcept
{
    return static_cast<float>(InFontSize) * kGlyphAdvanceRatio;
}

// Multiplies the RGB channels of a colour. Alpha is preserved so a translucent
[[nodiscard]] Color32 ScaleColor(Color32 InColor, float InFactor) noexcept
{
    const auto ScopedScale = [InFactor](std::uint8_t InChannel)
    {
        const float ScopedValue = static_cast<float>(InChannel) * InFactor;
        const float ScopedClamped = ScopedValue < 0.0f ? 0.0f : (ScopedValue > 255.0f ? 255.0f : ScopedValue);
        return static_cast<std::uint8_t>(ScopedClamped + 0.5f);
    };
    return Color32::FromBytes(ScopedScale(InColor.R()), ScopedScale(InColor.G()), ScopedScale(InColor.B()),
                              InColor.A());
}

[[nodiscard]] Color32 WithAlpha(Color32 InColor, std::uint8_t InAlpha) noexcept
{
    return Color32::FromBytes(InColor.R(), InColor.G(), InColor.B(), InAlpha);
}

// Canvas-local -> world. Rotation is around the canvas origin, so one sin/cos
[[nodiscard]] Vector2D LocalToWorld(const WidgetCanvas& InCanvas, Vector2D InLocal, float InScale) noexcept
{
    const Vector2D ScopedShifted{(InLocal.X + InCanvas.OledOffset().X) * InScale,
                                 (InLocal.Y + InCanvas.OledOffset().Y) * InScale};
    const Vector2D ScopedScaled{ScopedShifted.X * InCanvas.worldScale.X, ScopedShifted.Y * InCanvas.worldScale.Y};
    const float ScopedCosine = std::cos(InCanvas.worldRotation);
    const float ScopedSine = std::sin(InCanvas.worldRotation);
    return {InCanvas.worldPosition.X + ScopedScaled.X * ScopedCosine - ScopedScaled.Y * ScopedSine,
            InCanvas.worldPosition.Y + ScopedScaled.X * ScopedSine + ScopedScaled.Y * ScopedCosine};
}

// Turns one local rect + source rect into a RenderItem. This is the only place
struct QuadWriter
{
    std::span<Rhi::RenderItem> items;
    std::uint32_t cursor = 0;

    // Returns false for a degenerate quad so the caller only advances a key
    [[nodiscard]] bool Write(const WidgetCanvas& InCanvas, Rect InLocal, std::uint32_t InLayer, std::uint32_t InColor,
                             Vector2D InUvMin, Vector2D InUvMax, float InScale,
                             float InRotationOffset = 0.0f) noexcept
    {
        const Vector2D ScopedLocalSize = InLocal.Size();
        if (ScopedLocalSize.X <= 0.0f || ScopedLocalSize.Y <= 0.0f)
        {
            return false;
        }
        Rhi::RenderItem& ScopedItem = items[cursor++];
        ScopedItem.position = LocalToWorld(InCanvas, InLocal.Center(), InScale);
        ScopedItem.size = {ScopedLocalSize.X * InCanvas.worldScale.X * InScale,
                           ScopedLocalSize.Y * InCanvas.worldScale.Y * InScale};
        ScopedItem.uvMin = InUvMin;
        ScopedItem.uvMax = InUvMax;
        ScopedItem.layer = InLayer;
        ScopedItem.color = InColor;
        ScopedItem.rotation = InCanvas.worldRotation + InRotationOffset;
        ScopedItem.flags = 0;
        return true;
    }
};

// One emit target for a widget: writes an item and its key. A struct instead
// of a lambda so a custom painter's C-style callback can reach it through a
// void pointer.
struct QuadEmitState
{
    QuadWriter* Writer = nullptr;
    const WidgetCanvas* Canvas = nullptr;
    float DisplayScale = 1.0f;
    Rhi::RenderKey* Keys = nullptr;
    std::uint16_t SortLayer = 0;
    float Depth = 0.0f;
    std::uint8_t* Sequence = nullptr;
    // Composed clip of the widget being emitted; HasClip is false for a free rect.
    Rect Clip{};
    bool HasClip = false;
    // Visual transition state: alpha multiplier and a presentation-only offset.
    float Opacity = 1.0f;
    Vector2D Offset{};

    bool operator()(Rect InLocal, std::uint32_t InLayer, std::uint32_t InColor, Vector2D InUvMin,
                    Vector2D InUvMax, float InRotation = 0.0f) const noexcept
    {
        if (!(Opacity > 0.001f))
        {
            return false;
        }
        Rect ScopedLocal{Vector2D{InLocal.min.X + Offset.X, InLocal.min.Y + Offset.Y},
                         Vector2D{InLocal.max.X + Offset.X, InLocal.max.Y + Offset.Y}};
        Vector2D ScopedUvMin = InUvMin;
        Vector2D ScopedUvMax = InUvMax;
        if (HasClip)
        {
            if (InRotation != 0.0f)
            {
                // A rotated quad cannot be trimmed on its local axes; hide it
                // once its centre leaves the clip so it never bleeds.
                if (!Clip.Contains(ScopedLocal.Center()))
                {
                    return false;
                }
            }
            else if (!TrimToClip(ScopedLocal, InUvMin, InUvMax, Clip, ScopedLocal, ScopedUvMin, ScopedUvMax))
            {
                return false;
            }
        }
        std::uint32_t ScopedColor = InColor;
        if (Opacity < 0.999f)
        {
            const std::uint32_t ScopedAlpha = (ScopedColor >> 24) & 0xFFu;
            const auto ScopedScaled = static_cast<std::uint32_t>(static_cast<float>(ScopedAlpha) * Opacity + 0.5f);
            ScopedColor = (ScopedColor & 0x00FFFFFFu) | (ScopedScaled << 24);
        }
        if (!Writer->Write(*Canvas, ScopedLocal, InLayer, ScopedColor, ScopedUvMin, ScopedUvMax, DisplayScale,
                           InRotation))
        {
            return false;
        }
        Keys[Writer->cursor - 1] = Rhi::MakeRenderKey(SortLayer, Depth, *Sequence);
        ++(*Sequence);
        return true;
    }
};

// Budget pass of a custom painter: count the quads, capped per widget.
struct CustomBudgetState
{
    std::uint32_t Count = 0;
};

bool CountCustomQuad(void* InState, Rect, std::uint32_t, std::uint32_t, Vector2D, Vector2D, float) noexcept
{
    auto* ScopedState = static_cast<CustomBudgetState*>(InState);
    if (ScopedState == nullptr || ScopedState->Count >= kMaxCustomQuadsPerWidget)
    {
        return false;
    }
    ++ScopedState->Count;
    return true;
}

// Emit pass of a custom painter. The quota comes from the budget pass, so a
// painter that changes its output cannot write past the reserved range.
struct CustomEmitState
{
    QuadEmitState* Emit = nullptr;
    std::uint32_t Quota = 0;
    std::uint32_t Emitted = 0;
};

bool EmitCustomQuad(void* InState, Rect InLocal, std::uint32_t InLayer, std::uint32_t InColor, Vector2D InUvMin,
                    Vector2D InUvMax, float InRotation) noexcept
{
    auto* ScopedState = static_cast<CustomEmitState*>(InState);
    if (ScopedState == nullptr || ScopedState->Emit == nullptr || ScopedState->Emitted >= ScopedState->Quota)
    {
        return false;
    }
    ++ScopedState->Emitted;
    return (*ScopedState->Emit)(InLocal, InLayer, InColor, InUvMin, InUvMax, InRotation);
}

// Height an auto row gives to this widget when it is the row's tallest explicit
[[nodiscard]] float IntrinsicHeight(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    const Vector2D ScopedAnchorMin = InCanvas.anchorMins[InWidget];
    const Vector2D ScopedAnchorMax = InCanvas.anchorMaxs[InWidget];
    if (ScopedAnchorMin.Y != ScopedAnchorMax.Y)
    {
        return 0.0f;
    }
    const float ScopedHeight = InCanvas.offsetMaxs[InWidget].Y - InCanvas.offsetMins[InWidget].Y;
    return ScopedHeight > 0.0f ? ScopedHeight : 0.0f;
}

// Reference box an anchor resolves inside: the cached grid cell for a
[[nodiscard]] Rect ReferenceRect(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    if (InCanvas.cellColumns[InWidget] != kNoCellTrack)
    {
        return Rect{InCanvas.layout.localMins[InWidget], InCanvas.layout.localMaxs[InWidget]};
    }
    const std::int32_t ScopedParent = InCanvas.parents[InWidget];
    if (ScopedParent == kNoWidget)
    {
        Vector2D ScopedMin = InCanvas.safeAreaMin;
        Vector2D ScopedMax{InCanvas.size.X - InCanvas.safeAreaMax.X, InCanvas.size.Y - InCanvas.safeAreaMax.Y};
        if (ScopedMax.X < ScopedMin.X)
        {
            ScopedMax.X = ScopedMin.X;
        }
        if (ScopedMax.Y < ScopedMin.Y)
        {
            ScopedMax.Y = ScopedMin.Y;
        }
        return Rect{ScopedMin, ScopedMax};
    }
    const auto ScopedIndex = static_cast<std::uint32_t>(ScopedParent);
    return Rect{InCanvas.layout.localMins[ScopedIndex], InCanvas.layout.localMaxs[ScopedIndex]};
}

// RenderKey depth for a widget: z-order dominates, insertion index breaks ties.
[[nodiscard]] float WidgetDepth(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    return static_cast<float>(static_cast<std::int32_t>(InCanvas.zOrders[InWidget]) *
                                  static_cast<std::int32_t>(kMaxWidgets) +
                              static_cast<std::int32_t>(InWidget));
}

// Effective visibility: the widget and every ancestor must be visible, and a
// widget under a Switcher must sit on the active page.
[[nodiscard]] bool WidgetShown(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    std::uint32_t ScopedSlot = InWidget;
    for (std::uint32_t ScopedDepth = 0; ScopedDepth <= kMaxWidgets; ++ScopedDepth)
    {
        if (!HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Visible))
        {
            return false;
        }
        if (InCanvas.kinds[ScopedSlot] == WidgetKind::Tooltip &&
            !HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Pinned))
        {
            const WidgetHandle ScopedTarget = InCanvas.tooltipTargets[ScopedSlot];
            if (!IsHandleSet(ScopedTarget) || !InCanvas.ResolveHandle(ScopedTarget))
            {
                return false;
            }
            const auto ScopedTargetSlot = static_cast<std::uint32_t>(ScopedTarget.widget);
            if (!HasFlag(InCanvas.flags[ScopedTargetSlot], WidgetFlags::Hovered) ||
                InCanvas.hoverSeconds[ScopedTargetSlot] < InCanvas.tooltipDelays[ScopedSlot])
            {
                return false;
            }
        }
        const std::int32_t ScopedParent = InCanvas.parents[ScopedSlot];
        if (ScopedParent == kNoWidget)
        {
            return true;
        }
        const auto ScopedParentSlot = static_cast<std::uint32_t>(ScopedParent);
        if (InCanvas.kinds[ScopedParentSlot] == WidgetKind::Switcher)
        {
            const WidgetHandle ScopedActive = InCanvas.switcherActive[ScopedParentSlot];
            const bool ScopedActiveSet =
                IsHandleSet(ScopedActive) && ScopedActive.generation == InCanvas.generation &&
                ScopedActive.widget >= 0 && static_cast<std::uint32_t>(ScopedActive.widget) < InCanvas.widgetCount;
            if (ScopedActiveSet)
            {
                if (ScopedActive.widget != static_cast<std::int32_t>(ScopedSlot))
                {
                    return false;
                }
            }
            else if (InCanvas.childOrdinals[ScopedSlot] != 0)
            {
                // An unset switcher shows its first child.
                return false;
            }
        }
        ScopedSlot = ScopedParentSlot;
    }
    return false;
}

// Tab candidate test: policy, visibility, enabled state, group filter and the
// optional focus scope. A scope contains its descendants, not itself.
[[nodiscard]] bool FocusCandidate(const WidgetCanvas& InCanvas, std::uint32_t InWidget, std::uint16_t InGroupFilter,
                                  std::int32_t InScope, bool InTabOnly = true) noexcept
{
    const FocusPolicy ScopedPolicy = InCanvas.focusPolicies[InWidget];
    if (ScopedPolicy == FocusPolicy::None || (InTabOnly && !HasTabFocus(ScopedPolicy)) ||
        !WidgetShown(InCanvas, InWidget) || !HasFlag(InCanvas.flags[InWidget], WidgetFlags::Enabled))
    {
        return false;
    }
    const Rect ScopedRect{InCanvas.layout.localMins[InWidget], InCanvas.layout.localMaxs[InWidget]};
    if (!RectInsideClip(InCanvas, InWidget, ScopedRect))
    {
        return false;
    }
    if (InGroupFilter != kNoFocusGroup && InCanvas.focusGroups[InWidget] != InGroupFilter)
    {
        return false;
    }
    if (InScope != kNoWidget)
    {
        std::int32_t ScopedParent = InCanvas.parents[InWidget];
        while (ScopedParent != kNoWidget && ScopedParent != InScope)
        {
            ScopedParent = InCanvas.parents[static_cast<std::uint32_t>(ScopedParent)];
        }
        if (ScopedParent == kNoWidget)
        {
            return false;
        }
    }
    return true;
}

// Effective opacity: the widget times every ancestor, so fading a panel fades
// its whole subtree.
[[nodiscard]] float EffectiveOpacity(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    float ScopedValue = 1.0f;
    std::uint32_t ScopedSlot = InWidget;
    for (std::uint32_t ScopedDepth = 0; ScopedDepth <= kMaxWidgets; ++ScopedDepth)
    {
        ScopedValue *= InCanvas.opacities[ScopedSlot];
        const std::int32_t ScopedParent = InCanvas.parents[ScopedSlot];
        if (ScopedParent == kNoWidget)
        {
            break;
        }
        ScopedSlot = static_cast<std::uint32_t>(ScopedParent);
    }
    return ScopedValue < 0.0f ? 0.0f : (ScopedValue > 1.0f ? 1.0f : ScopedValue);
}

// Effective visual offset: the widget plus every ancestor, so sliding a panel
// slides its subtree.
[[nodiscard]] Vector2D EffectiveVisualOffset(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    Vector2D ScopedValue{};
    std::uint32_t ScopedSlot = InWidget;
    for (std::uint32_t ScopedDepth = 0; ScopedDepth <= kMaxWidgets; ++ScopedDepth)
    {
        ScopedValue.X += InCanvas.visualOffsets[ScopedSlot].X;
        ScopedValue.Y += InCanvas.visualOffsets[ScopedSlot].Y;
        const std::int32_t ScopedParent = InCanvas.parents[ScopedSlot];
        if (ScopedParent == kNoWidget)
        {
            break;
        }
        ScopedSlot = static_cast<std::uint32_t>(ScopedParent);
    }
    return ScopedValue;
}

// Tab ordering key: tabIndex first, then the insertion index.
[[nodiscard]] constexpr bool FocusKeyLess(std::int32_t InTabA, std::uint32_t InIndexA, std::int32_t InTabB,
                                          std::uint32_t InIndexB) noexcept
{
    return InTabA < InTabB || (InTabA == InTabB && InIndexA < InIndexB);
}

// Composes the clip rect of every ancestor with ClipChildren. The widget's own
// flag clips its descendants only, never itself.
[[nodiscard]] bool ClipForWidget(const WidgetCanvas& InCanvas, std::uint32_t InWidget, Rect& OutClip) noexcept
{
    bool ScopedAny = false;
    Rect ScopedClip{Vector2D{-1.0e9f, -1.0e9f}, Vector2D{1.0e9f, 1.0e9f}};
    std::int32_t ScopedParent = InCanvas.parents[InWidget];
    while (ScopedParent != kNoWidget)
    {
        const auto ScopedParentSlot = static_cast<std::uint32_t>(ScopedParent);
        if (HasFlag(InCanvas.flags[ScopedParentSlot], WidgetFlags::ClipChildren))
        {
            const Rect ScopedParentRect{InCanvas.layout.localMins[ScopedParentSlot],
                                        InCanvas.layout.localMaxs[ScopedParentSlot]};
            ScopedClip.min.X = std::max(ScopedClip.min.X, ScopedParentRect.min.X);
            ScopedClip.min.Y = std::max(ScopedClip.min.Y, ScopedParentRect.min.Y);
            ScopedClip.max.X = std::min(ScopedClip.max.X, ScopedParentRect.max.X);
            ScopedClip.max.Y = std::min(ScopedClip.max.Y, ScopedParentRect.max.Y);
            ScopedAny = true;
        }
        ScopedParent = InCanvas.parents[ScopedParentSlot];
    }
    OutClip = ScopedClip;
    return ScopedAny;
}

// True when the point is inside every clipping ancestor.
[[nodiscard]] bool PointInsideClip(const WidgetCanvas& InCanvas, std::uint32_t InWidget, Vector2D InPoint) noexcept
{
    std::int32_t ScopedParent = InCanvas.parents[InWidget];
    while (ScopedParent != kNoWidget)
    {
        const auto ScopedParentSlot = static_cast<std::uint32_t>(ScopedParent);
        if (HasFlag(InCanvas.flags[ScopedParentSlot], WidgetFlags::ClipChildren))
        {
            const Rect ScopedParentRect{InCanvas.layout.localMins[ScopedParentSlot],
                                        InCanvas.layout.localMaxs[ScopedParentSlot]};
            if (!ScopedParentRect.Contains(InPoint))
            {
                return false;
            }
        }
        ScopedParent = InCanvas.parents[ScopedParentSlot];
    }
    return true;
}

// True when the rect still intersects the composed clip rect.
[[nodiscard]] bool RectInsideClip(const WidgetCanvas& InCanvas, std::uint32_t InWidget, Rect InRect) noexcept
{
    Rect ScopedClip{};
    if (!ClipForWidget(InCanvas, InWidget, ScopedClip))
    {
        return true;
    }
    return InRect.min.X < ScopedClip.max.X && InRect.max.X > ScopedClip.min.X && InRect.min.Y < ScopedClip.max.Y &&
           InRect.max.Y > ScopedClip.min.Y;
}

// Trims an axis-aligned quad and its UVs to the clip rect. Returns false when
// nothing remains. The UV interpolation keeps images and glyphs correct.
[[nodiscard]] bool TrimToClip(Rect InRect, Vector2D InUvMin, Vector2D InUvMax, Rect InClip, Rect& OutRect,
                              Vector2D& OutUvMin, Vector2D& OutUvMax) noexcept
{
    const float ScopedWidth = InRect.max.X - InRect.min.X;
    const float ScopedHeight = InRect.max.Y - InRect.min.Y;
    if (!(ScopedWidth > 0.0f) || !(ScopedHeight > 0.0f))
    {
        return false;
    }
    const float ScopedMinX = std::max(InRect.min.X, InClip.min.X);
    const float ScopedMinY = std::max(InRect.min.Y, InClip.min.Y);
    const float ScopedMaxX = std::min(InRect.max.X, InClip.max.X);
    const float ScopedMaxY = std::min(InRect.max.Y, InClip.max.Y);
    if (!(ScopedMaxX > ScopedMinX) || !(ScopedMaxY > ScopedMinY))
    {
        return false;
    }
    const float ScopedU0 = (ScopedMinX - InRect.min.X) / ScopedWidth;
    const float ScopedU1 = (ScopedMaxX - InRect.min.X) / ScopedWidth;
    const float ScopedV0 = (ScopedMinY - InRect.min.Y) / ScopedHeight;
    const float ScopedV1 = (ScopedMaxY - InRect.min.Y) / ScopedHeight;
    OutRect = Rect{Vector2D{ScopedMinX, ScopedMinY}, Vector2D{ScopedMaxX, ScopedMaxY}};
    OutUvMin = Vector2D{InUvMin.X + (InUvMax.X - InUvMin.X) * ScopedU0,
                        InUvMin.Y + (InUvMax.Y - InUvMin.Y) * ScopedV0};
    OutUvMax = Vector2D{InUvMin.X + (InUvMax.X - InUvMin.X) * ScopedU1,
                        InUvMin.Y + (InUvMax.Y - InUvMin.Y) * ScopedV1};
    return true;
}

// Resolves a palette-indexed slot, falling back to the literal authoring colour.
[[nodiscard]] Color32 WidgetTint(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    const std::uint16_t ScopedIndex = InCanvas.tintPalettes[InWidget];
    return ScopedIndex == kPaletteInherit ? InCanvas.tints[InWidget] : InCanvas.theme.Resolve(ScopedIndex);
}

[[nodiscard]] Color32 WidgetFill(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    const std::uint16_t ScopedIndex = InCanvas.fillPalettes[InWidget];
    return ScopedIndex == kPaletteInherit ? InCanvas.fillTints[InWidget] : InCanvas.theme.Resolve(ScopedIndex);
}

[[nodiscard]] Color32 WidgetTextColor(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    const std::uint16_t ScopedIndex = InCanvas.textPalettes[InWidget];
    return ScopedIndex == kPaletteInherit ? InCanvas.textTints[InWidget] : InCanvas.theme.Resolve(ScopedIndex);
}

// The draw colour of a solid widget. The theme can override each interaction
// state; an unset slot (alpha zero) falls back to scaling the authored tint.
[[nodiscard]] std::uint32_t SolidColor(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    const Color32 ScopedTint = WidgetTint(InCanvas, InWidget);
    const std::uint32_t ScopedFlags = InCanvas.flags[InWidget];
    if (!HasFlag(ScopedFlags, WidgetFlags::Enabled))
    {
        return InCanvas.theme.Resolve(UiPaletteSlot::Disabled, ScaleColor(ScopedTint, 0.60f)).rgba;
    }
    if (HasFlag(ScopedFlags, WidgetFlags::Pressed))
    {
        return InCanvas.theme.Resolve(UiPaletteSlot::Pressed, ScaleColor(ScopedTint, 0.82f)).rgba;
    }
    if (HasFlag(ScopedFlags, WidgetFlags::Hovered))
    {
        return InCanvas.theme.Resolve(UiPaletteSlot::Hover, ScaleColor(ScopedTint, 1.15f)).rgba;
    }
    return ScopedTint.rgba;
}

// The fill/knob/thumb/mark colour. Only the disabled state modulates it: a
// pressed track must not change the value indicator it is showing.
[[nodiscard]] std::uint32_t FillColor(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    Color32 ScopedColor = WidgetFill(InCanvas, InWidget);
    if (!HasFlag(InCanvas.flags[InWidget], WidgetFlags::Enabled))
    {
        ScopedColor = ScaleColor(ScopedColor, 0.60f);
    }
    return ScopedColor.rgba;
}

// Stroke thickness of a Border, clamped so the four sides always fit.
[[nodiscard]] float BorderStroke(const WidgetCanvas& InCanvas, std::uint32_t InWidget, Vector2D InExtent) noexcept
{
    const float ScopedLimit = std::min(InExtent.X, InExtent.Y) * 0.5f;
    const float ScopedWidth = InCanvas.borderWidths[InWidget];
    return ScopedWidth > ScopedLimit ? ScopedLimit : (ScopedWidth > 0.0f ? ScopedWidth : 0.0f);
}

// Quads a Border emits. The left and right sides collapse when the stroke fills
// the height, so the packing budget and the emit pass must agree on this count.
[[nodiscard]] std::uint32_t BorderQuadCount(const WidgetCanvas& InCanvas, std::uint32_t InWidget,
                                            Vector2D InExtent) noexcept
{
    const float ScopedStroke = BorderStroke(InCanvas, InWidget, InExtent);
    if (!(ScopedStroke > 0.0f))
    {
        return 0;
    }
    return InExtent.Y - ScopedStroke * 2.0f > 0.0f ? 4u : 2u;
}

// Travel-axis length of a Slider knob. An authored size wins; otherwise the
// knob is as thick as the track and never longer than a 16-Unit segment.
[[nodiscard]] float SliderKnobLength(const WidgetCanvas& InCanvas, std::uint32_t InWidget, Vector2D InExtent,
                                     bool InVertical) noexcept
{
    const float ScopedAxis = InVertical ? InExtent.Y : InExtent.X;
    const float ScopedCross = InVertical ? InExtent.X : InExtent.Y;
    float ScopedKnob = InCanvas.knobSizes[InWidget];
    if (!(ScopedKnob > 0.0f))
    {
        ScopedKnob = std::min(ScopedCross, 16.0f);
    }
    if (!(ScopedKnob > 0.0f))
    {
        ScopedKnob = 1.0f;
    }
    return std::min(ScopedKnob, ScopedAxis);
}

// Travel-relative handle start for a value in [0, 1].
[[nodiscard]] float TrackStart(float InAxis, float InHandle, float InProgress) noexcept
{
    const float ScopedTravel = InAxis - InHandle;
    return ScopedTravel > 0.0f ? ScopedTravel * InProgress : 0.0f;
}

// Thumb length of a Scrollbar from the visible-fraction ratio.
[[nodiscard]] float ScrollThumbLength(const WidgetCanvas& InCanvas, std::uint32_t InWidget, float InAxis) noexcept
{
    const float ScopedAuthored = InCanvas.ratios[InWidget];
    const float ScopedRatio = ScopedAuthored < 0.05f ? 0.05f : (ScopedAuthored > 1.0f ? 1.0f : ScopedAuthored);
    return InAxis * ScopedRatio;
}
}

// OLED pixel shifter
Vector2D OledOrbitOffset(std::uint32_t InStep) noexcept
{
    // Eight integer points on a 2-px circle. Step 0 is the neutral position so
    static constexpr std::array<Vector2D, OledShiftState::kSteps> ScopedKOrbit = {
        Vector2D{2.0f, 0.0f},  Vector2D{1.0f, 1.0f},   Vector2D{0.0f, 2.0f},  Vector2D{-1.0f, 1.0f},
        Vector2D{-2.0f, 0.0f}, Vector2D{-1.0f, -1.0f}, Vector2D{0.0f, -2.0f}, Vector2D{1.0f, -1.0f},
    };
    if (InStep == 0)
    {
        return {};
    }
    return ScopedKOrbit[(InStep - 1u) % OledShiftState::kSteps];
}

void WidgetCanvas::AdvanceOledShift(float InDt) noexcept
{
    if (!oled.enabled || !(InDt > 0.0f))
    {
        return;
    }
    oled.accumulator += InDt;
    // A hitch or a long editor pause must not spin the orbit hundreds of times;
    std::uint32_t ScopedAdvances = 0;
    while (oled.accumulator >= OledShiftState::kIntervalSeconds && ScopedAdvances < OledShiftState::kCycleSteps)
    {
        oled.accumulator -= OledShiftState::kIntervalSeconds;
        oled.step = (oled.step + 1u) % OledShiftState::kCycleSteps;
        ++ScopedAdvances;
    }
    if (oled.accumulator >= OledShiftState::kIntervalSeconds)
    {
        // Clamp pathological dt
        oled.accumulator = 0.0f;
    }
    // Only change the visual offset when a step actually elapsed; otherwise a
    if (ScopedAdvances > 0)
    {
        oled.offset = OledOrbitOffset(oled.step);
    }
}

// WidgetCanvas - configuration and insertion
void WidgetCanvas::Clear() noexcept
{
    // Whole-object reset: canvas size, layer, grid, all widget arrays and the
    // layout cache. The generation advances so every outstanding handle goes
    // stale instead of silently pointing at a new widget.
    const std::uint32_t ScopedGeneration = generation + 1;
    *this = WidgetCanvas{};
    generation = ScopedGeneration;
    tintPalettes.fill(kPaletteInherit);
    fillPalettes.fill(kPaletteInherit);
    textPalettes.fill(kPaletteInherit);
}

bool WidgetCanvas::CanFocus(std::int32_t InWidget) const noexcept
{
    if (!ValidWidget(InWidget))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    return focusPolicies[ScopedSlot] != FocusPolicy::None && WidgetShown(*this, ScopedSlot) &&
           HasFlag(flags[ScopedSlot], WidgetFlags::Enabled);
}

bool WidgetCanvas::IsVisible(std::int32_t InWidget) const noexcept
{
    return ValidWidget(InWidget) && WidgetShown(*this, static_cast<std::uint32_t>(InWidget));
}

void WidgetCanvas::AdvanceHover(float InDt) noexcept
{
    if (!(InDt > 0.0f))
    {
        return;
    }
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < widgetCount; ++ScopedWidget)
    {
        if (HasFlag(flags[ScopedWidget], WidgetFlags::Hovered))
        {
            const float ScopedNext = hoverSeconds[ScopedWidget] + InDt;
            hoverSeconds[ScopedWidget] = ScopedNext > 3600.0f ? 3600.0f : ScopedNext;
        }
        else
        {
            hoverSeconds[ScopedWidget] = 0.0f;
        }
    }
}

bool WidgetCanvas::SetActiveWidget(std::int32_t InSwitcher, WidgetHandle InWidget) noexcept
{
    if (!ValidWidget(InSwitcher))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InSwitcher);
    if (kinds[ScopedSlot] != WidgetKind::Switcher)
    {
        return false;
    }
    if (!IsHandleSet(InWidget))
    {
        // An empty handle is the explicit "first child" reset.
        switcherActive[ScopedSlot] = WidgetHandle{};
        return true;
    }
    if (!ResolveHandle(InWidget) || parents[static_cast<std::uint32_t>(InWidget.widget)] != InSwitcher)
    {
        return false;
    }
    switcherActive[ScopedSlot] = InWidget;
    return true;
}

WidgetHandle WidgetCanvas::ActiveWidget(std::int32_t InSwitcher) const noexcept
{
    if (!ValidWidget(InSwitcher))
    {
        return {};
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InSwitcher);
    if (kinds[ScopedSlot] != WidgetKind::Switcher)
    {
        return {};
    }
    const WidgetHandle ScopedStored = switcherActive[ScopedSlot];
    if (IsHandleSet(ScopedStored) && ScopedStored.generation == generation && ValidWidget(ScopedStored.widget))
    {
        return ScopedStored;
    }
    // The accessor is const, so the first-child fallback is reported without
    // writing the cache; WidgetShown applies the same rule.
    const std::int32_t ScopedFirst = firstChild[ScopedSlot];
    return ValidWidget(ScopedFirst) ? WidgetHandle{ScopedFirst, generation} : WidgetHandle{};
}

void WidgetCanvas::SetActivePage(std::int32_t InSwitcher, std::uint32_t InPage) noexcept
{
    if (!ValidWidget(InSwitcher))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InSwitcher);
    if (kinds[ScopedSlot] != WidgetKind::Switcher)
    {
        return;
    }
    std::int32_t ScopedChild = firstChild[ScopedSlot];
    if (ScopedChild == kNoWidget)
    {
        switcherActive[ScopedSlot] = WidgetHandle{};
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InPage; ++ScopedIndex)
    {
        const std::int32_t ScopedNext = nextSibling[static_cast<std::uint32_t>(ScopedChild)];
        if (ScopedNext == kNoWidget)
        {
            break;
        }
        ScopedChild = ScopedNext;
    }
    switcherActive[ScopedSlot] = Handle(ScopedChild);
}

std::uint32_t WidgetCanvas::ActivePage(std::int32_t InSwitcher) const noexcept
{
    if (!ValidWidget(InSwitcher))
    {
        return 0;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InSwitcher);
    if (kinds[ScopedSlot] != WidgetKind::Switcher)
    {
        return 0;
    }
    const WidgetHandle ScopedActive = ActiveWidget(InSwitcher);
    if (!IsHandleSet(ScopedActive))
    {
        return 0;
    }
    std::uint32_t ScopedPage = 0;
    for (std::int32_t ScopedChild = firstChild[ScopedSlot]; ScopedChild != kNoWidget;
         ScopedChild = nextSibling[static_cast<std::uint32_t>(ScopedChild)])
    {
        if (ScopedChild == ScopedActive.widget)
        {
            return ScopedPage;
        }
        ++ScopedPage;
    }
    return 0;
}

Vector2D WidgetCanvas::CanvasToScreen(Vector2D InLocal) const noexcept
{
    return LocalToWorld(*this, InLocal, displayScale);
}

Vector2D WidgetCanvas::ScreenToCanvas(Vector2D InScreen) const noexcept
{
    const Vector2D ScopedRelative{InScreen.X - worldPosition.X, InScreen.Y - worldPosition.Y};
    const float ScopedCosine = std::cos(worldRotation);
    const float ScopedSine = std::sin(worldRotation);
    const Vector2D ScopedUnrotated{ScopedRelative.X * ScopedCosine + ScopedRelative.Y * ScopedSine,
                                   -ScopedRelative.X * ScopedSine + ScopedRelative.Y * ScopedCosine};
    const float ScopedScale = displayScale > 0.0f ? displayScale : 1.0f;
    const Vector2D ScopedLocal{ScopedUnrotated.X / std::max(worldScale.X * ScopedScale, 1.0e-6f),
                               ScopedUnrotated.Y / std::max(worldScale.Y * ScopedScale, 1.0e-6f)};
    return ScopedLocal - OledOffset();
}

bool WidgetCanvas::SetGrid(std::uint32_t InColumns, std::uint32_t InRows, std::span<const float> InColumnWidths,
                           std::span<const float> InRowHeights, Vector2D InGap, Vector2D InPadding) noexcept
{
    if (InColumns > kMaxGridColumns || InRows > kMaxGridRows)
    {
        return false;
    }
    if (InColumnWidths.size() != InColumns || InRowHeights.size() != InRows)
    {
        return false;
    }
    gridColumnCount = InColumns;
    gridRowCount = InRows;
    gridGap = InGap;
    gridPadding = InPadding;
    tracks[0].fill(0.0f);
    tracks[1].fill(0.0f);
    for (std::uint32_t ScopedI = 0; ScopedI < InColumns; ++ScopedI)
    {
        tracks[0][ScopedI] = InColumnWidths[ScopedI];
    }
    for (std::uint32_t ScopedI = 0; ScopedI < InRows; ++ScopedI)
    {
        tracks[1][ScopedI] = InRowHeights[ScopedI];
    }
    InvalidateLayout();
    return true;
}

Vector2D WidgetCanvas::GridPixelExtent(float InScale) const noexcept
{
    const GridView ScopedGrid = Grid();
    float ScopedWidth = 0.0f;
    float ScopedHeight = 0.0f;
    for (std::uint32_t ScopedColumn = 0; ScopedColumn < gridColumnCount; ++ScopedColumn)
    {
        ScopedWidth += ScopedGrid[0, ScopedColumn];
    }
    for (std::uint32_t ScopedRow = 0; ScopedRow < gridRowCount; ++ScopedRow)
    {
        ScopedHeight += ScopedGrid[1, ScopedRow];
    }
    if (gridColumnCount > 1)
    {
        ScopedWidth += gridGap.X * static_cast<float>(gridColumnCount - 1);
    }
    if (gridRowCount > 1)
    {
        ScopedHeight += gridGap.Y * static_cast<float>(gridRowCount - 1);
    }
    return Vector2D{ScopedWidth * InScale, ScopedHeight * InScale};
}

std::int32_t WidgetCanvas::AddWidget(const WidgetDesc& InDesc) noexcept
{
    if (widgetCount >= kMaxWidgets || InDesc.kind >= WidgetKind::Count)
    {
        return kNoWidget;
    }
    const auto ScopedIndex = static_cast<std::int32_t>(widgetCount);
    const auto ScopedSlot = static_cast<std::uint32_t>(ScopedIndex);

    if (InDesc.parent != kNoWidget)
    {
        if (!ValidWidget(InDesc.parent))
        {
            return kNoWidget;
        }
        if (childCounts[static_cast<std::uint32_t>(InDesc.parent)] >= kMaxChildrenPerWidget)
        {
            return kNoWidget;
        }
    }

    // The grid is canvas-level: only root widgets may be placed in a cell.
    const bool ScopedWantsCell = InDesc.gridRow != kNoCell || InDesc.gridColumn != kNoCell;
    std::uint32_t ScopedCellRow = 0;
    std::uint32_t ScopedCellColumn = 0;
    if (ScopedWantsCell)
    {
        if (InDesc.gridRow < 0 || InDesc.gridColumn < 0 || InDesc.parent != kNoWidget)
        {
            return kNoWidget;
        }
        if (InDesc.rowSpan == 0 || InDesc.columnSpan == 0)
        {
            return kNoWidget;
        }
        ScopedCellRow = static_cast<std::uint32_t>(InDesc.gridRow);
        ScopedCellColumn = static_cast<std::uint32_t>(InDesc.gridColumn);
        if (ScopedCellRow >= gridRowCount || ScopedCellColumn >= gridColumnCount)
        {
            return kNoWidget;
        }
        if (ScopedCellRow + InDesc.rowSpan > gridRowCount || ScopedCellColumn + InDesc.columnSpan > gridColumnCount)
        {
            return kNoWidget;
        }
    }

    if (InDesc.zOrder < kMinZOrder || InDesc.zOrder > kMaxZOrder)
    {
        return kNoWidget;
    }
    if (InDesc.text.size() > static_cast<std::size_t>(kMaxTextPool - textUsed))
    {
        return kNoWidget;
    }

    kinds[ScopedSlot] = InDesc.kind;
    flags[ScopedSlot] = InDesc.flags;
    // Focus is opt-in. No kind is keyboard-inert or focusable by construction:
    // the descriptor decides, and the Focusable bit only mirrors the policy.
    FocusPolicy ScopedPolicy = InDesc.focusPolicy;
    if (ScopedPolicy == FocusPolicy::None && HasFlag(InDesc.flags, WidgetFlags::Focusable))
    {
        ScopedPolicy = FocusPolicy::TabAndPointer;
    }
    focusPolicies[ScopedSlot] = ScopedPolicy;
    tabIndices[ScopedSlot] = InDesc.tabIndex;
    focusGroups[ScopedSlot] = InDesc.focusGroup;
    const std::uint32_t ScopedFocusMask = static_cast<std::uint32_t>(WidgetFlags::Focusable);
    flags[ScopedSlot] =
        ScopedPolicy == FocusPolicy::None ? (flags[ScopedSlot] & ~ScopedFocusMask) : (flags[ScopedSlot] | ScopedFocusMask);
    childOrdinals[ScopedSlot] = InDesc.parent == kNoWidget
                                    ? 0
                                    : static_cast<std::uint16_t>(
                                          childCounts[static_cast<std::uint32_t>(InDesc.parent)]);
    switcherActive[ScopedSlot] = WidgetHandle{};
    customPaints[ScopedSlot] = InDesc.customPaint;
    customUsers[ScopedSlot] = InDesc.customUser;
    customParams[ScopedSlot] = InDesc.customParams;
    tooltipTargets[ScopedSlot] = InDesc.tooltipTarget;
    tooltipDelays[ScopedSlot] = InDesc.tooltipDelay > 0.0f ? InDesc.tooltipDelay : 0.0f;
    textRefs[ScopedSlot] = InDesc.textRef;
    slices[ScopedSlot] = InDesc.sliceInsets;
    radioGroups[ScopedSlot] = InDesc.radioGroup;
    const float ScopedOpacity = InDesc.opacity < 0.0f ? 0.0f : (InDesc.opacity > 1.0f ? 1.0f : InDesc.opacity);
    opacities[ScopedSlot] = ScopedOpacity;
    opacityTargets[ScopedSlot] = ScopedOpacity;
    visualOffsets[ScopedSlot] = InDesc.visualOffset;
    visualOffsetTargets[ScopedSlot] = InDesc.visualOffset;
    transitionSpeeds[ScopedSlot] = InDesc.transitionSpeed > 0.0f ? InDesc.transitionSpeed : 8.0f;
    if (InDesc.kind == WidgetKind::Tooltip)
    {
        // A tooltip under the cursor must never steal the hover from its target.
        flags[ScopedSlot] |= static_cast<std::uint32_t>(WidgetFlags::IgnoreHitTest);
    }
    parents[ScopedSlot] = InDesc.parent;
    firstChild[ScopedSlot] = kNoWidget;
    nextSibling[ScopedSlot] = kNoWidget;
    childCounts[ScopedSlot] = 0;
    anchorMins[ScopedSlot] = InDesc.anchorMin;
    anchorMaxs[ScopedSlot] = InDesc.anchorMax;
    offsetMins[ScopedSlot] = InDesc.offsetMin;
    offsetMaxs[ScopedSlot] = InDesc.offsetMax;
    tints[ScopedSlot] = InDesc.tint;
    fillTints[ScopedSlot] = InDesc.fillTint;
    textTints[ScopedSlot] = InDesc.textTint;
    tintPalettes[ScopedSlot] = InDesc.tintPalette;
    fillPalettes[ScopedSlot] = InDesc.fillPalette;
    textPalettes[ScopedSlot] = InDesc.textPalette;
    // A label samples the font atlas by definition; every other kind samples
    layers[ScopedSlot] =
        (InDesc.kind == WidgetKind::Label || InDesc.kind == WidgetKind::Hypertext) ? kFontAtlasLayer : InDesc.layer;
    uvMins[ScopedSlot] = InDesc.uvMin;
    uvMaxs[ScopedSlot] = InDesc.uvMax;
    fontSizes[ScopedSlot] = static_cast<std::uint16_t>(
        InDesc.fontSize == 0 ? kDefaultFontSize : (InDesc.fontSize > 512 ? 512 : InDesc.fontSize));
    textFlags[ScopedSlot] = static_cast<std::uint16_t>(InDesc.textFlags);
    fontAtlasIndices[ScopedSlot] = InDesc.fontAtlas;
    firstHyperlink[ScopedSlot] = static_cast<std::uint16_t>(hyperlinkUsed);
    hyperlinkCounts[ScopedSlot] = 0;
    if (!InDesc.hyperlinks.empty())
    {
        for (const WidgetHyperlink& ScopedLink : InDesc.hyperlinks)
        {
            if (hyperlinkUsed >= hyperlinks.size())
            {
                break;
            }
            hyperlinks[hyperlinkUsed++] = ScopedLink;
            ++hyperlinkCounts[ScopedSlot];
        }
    }
    progresses[ScopedSlot] = InDesc.progress < 0.0f ? 0.0f : (InDesc.progress > 1.0f ? 1.0f : InDesc.progress);
    borderWidths[ScopedSlot] = InDesc.borderWidth > 0.0f ? InDesc.borderWidth : 0.0f;
    knobSizes[ScopedSlot] = InDesc.knobSize > 0.0f ? InDesc.knobSize : 0.0f;
    ratios[ScopedSlot] = InDesc.ratio < 0.0f ? 0.0f : (InDesc.ratio > 1.0f ? 1.0f : InDesc.ratio);
    zOrders[ScopedSlot] = static_cast<std::int8_t>(InDesc.zOrder);
    cellRows[ScopedSlot] = ScopedWantsCell ? static_cast<std::uint16_t>(ScopedCellRow) : kNoCellTrack;
    cellColumns[ScopedSlot] = ScopedWantsCell ? static_cast<std::uint16_t>(ScopedCellColumn) : kNoCellTrack;
    rowSpans[ScopedSlot] = static_cast<std::uint8_t>(ScopedWantsCell ? InDesc.rowSpan : 1u);
    columnSpans[ScopedSlot] = static_cast<std::uint8_t>(ScopedWantsCell ? InDesc.columnSpan : 1u);

    if (!InDesc.text.empty())
    {
        textOffsets[ScopedSlot] = textUsed;
        textLengths[ScopedSlot] = static_cast<std::uint16_t>(InDesc.text.size());
        for (std::size_t ScopedI = 0; ScopedI < InDesc.text.size(); ++ScopedI)
        {
            textPool[textUsed + ScopedI] = InDesc.text[ScopedI];
        }
        textUsed += static_cast<std::uint32_t>(InDesc.text.size());
    }
    else
    {
        textOffsets[ScopedSlot] = 0;
        textLengths[ScopedSlot] = 0;
    }
    if (InDesc.kind == WidgetKind::TextInput)
    {
        caretOffsets[ScopedSlot] = textLengths[ScopedSlot];
        selectionAnchors[ScopedSlot] = textLengths[ScopedSlot];
    }

    if (InDesc.parent != kNoWidget)
    {
        const auto ScopedParentSlot = static_cast<std::uint32_t>(InDesc.parent);
        if (firstChild[ScopedParentSlot] == kNoWidget)
        {
            firstChild[ScopedParentSlot] = ScopedIndex;
        }
        else
        {
            std::int32_t ScopedSibling = firstChild[ScopedParentSlot];
            while (nextSibling[static_cast<std::uint32_t>(ScopedSibling)] != kNoWidget)
            {
                ScopedSibling = nextSibling[static_cast<std::uint32_t>(ScopedSibling)];
            }
            nextSibling[static_cast<std::uint32_t>(ScopedSibling)] = ScopedIndex;
        }
        ++childCounts[ScopedParentSlot];
    }

    ++widgetCount;
    InvalidateLayout();
    return ScopedIndex;
}

bool WidgetCanvas::SetText(std::int32_t InWidget, std::string_view InText) noexcept
{
    if (!ReplaceText(InWidget, InText))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    if (kinds[ScopedSlot] == WidgetKind::TextInput)
    {
        caretOffsets[ScopedSlot] = textLengths[ScopedSlot];
        selectionAnchors[ScopedSlot] = textLengths[ScopedSlot];
    }
    return true;
}

bool WidgetCanvas::ReplaceText(std::int32_t InWidget, std::string_view InText) noexcept
{
    if (!ValidWidget(InWidget) || InText.size() > static_cast<std::size_t>(kMaxTextPool))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    const auto ScopedNewLength = static_cast<std::uint16_t>(InText.size());
    const std::uint16_t ScopedOldLength = textLengths[ScopedSlot];
    if (ScopedNewLength <= ScopedOldLength && ScopedOldLength > 0)
    {
        const std::uint32_t ScopedOffset = textOffsets[ScopedSlot];
        for (std::size_t ScopedI = 0; ScopedI < InText.size(); ++ScopedI)
        {
            textPool[ScopedOffset + ScopedI] = InText[ScopedI];
        }
        textLengths[ScopedSlot] = ScopedNewLength;
        caretOffsets[ScopedSlot] = std::min(caretOffsets[ScopedSlot], ScopedNewLength);
        selectionAnchors[ScopedSlot] = std::min(selectionAnchors[ScopedSlot], ScopedNewLength);
        InvalidateLayout();
        return true;
    }
    if (InText.size() <= static_cast<std::size_t>(kMaxTextPool - textUsed))
    {
        const std::uint32_t ScopedOffset = textUsed;
        for (std::size_t ScopedI = 0; ScopedI < InText.size(); ++ScopedI)
        {
            textPool[ScopedOffset + ScopedI] = InText[ScopedI];
        }
        textOffsets[ScopedSlot] = ScopedOffset;
        textLengths[ScopedSlot] = ScopedNewLength;
        textUsed += static_cast<std::uint32_t>(InText.size());
        InvalidateLayout();
        return true;
    }

    // The pool has no room: copy every live range aside, then append the new
    // text at the end. Dead ranges from earlier replacements are dropped.
    std::array<char, kMaxTextPool> ScopedCopy{};
    std::uint32_t ScopedUsed = 0;
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < widgetCount; ++ScopedWidget)
    {
        const std::uint16_t ScopedLength = textLengths[ScopedWidget];
        if (ScopedLength == 0)
        {
            textOffsets[ScopedWidget] = 0;
            continue;
        }
        const std::uint32_t ScopedFrom = textOffsets[ScopedWidget];
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedLength; ++ScopedI)
        {
            ScopedCopy[ScopedUsed + ScopedI] = textPool[ScopedFrom + ScopedI];
        }
        textOffsets[ScopedWidget] = ScopedUsed;
        ScopedUsed += ScopedLength;
    }
    if (static_cast<std::size_t>(ScopedUsed) + InText.size() > kMaxTextPool)
    {
        return false;
    }
    for (std::size_t ScopedI = 0; ScopedI < InText.size(); ++ScopedI)
    {
        ScopedCopy[ScopedUsed + ScopedI] = InText[ScopedI];
    }
    textOffsets[ScopedSlot] = ScopedUsed;
    textLengths[ScopedSlot] = ScopedNewLength;
    ScopedUsed += static_cast<std::uint32_t>(InText.size());
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedUsed; ++ScopedI)
    {
        textPool[ScopedI] = ScopedCopy[ScopedI];
    }
    textUsed = ScopedUsed;
    caretOffsets[ScopedSlot] = std::min(caretOffsets[ScopedSlot], ScopedNewLength);
    selectionAnchors[ScopedSlot] = std::min(selectionAnchors[ScopedSlot], ScopedNewLength);
    InvalidateLayout();
    return true;
}

void WidgetCanvas::SetTextKey(std::int32_t InWidget, std::uint32_t InKey) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    // 0 means "no key"; store +1 so key 0 is representable.
    textKeys[static_cast<std::uint32_t>(InWidget)] = InKey + 1u;
}

std::uint32_t WidgetCanvas::TextKey(std::int32_t InWidget) const noexcept
{
    if (!ValidWidget(InWidget))
    {
        return 0xFFFFFFFFu;
    }
    const std::uint32_t ScopedStored = textKeys[static_cast<std::uint32_t>(InWidget)];
    return ScopedStored == 0 ? 0xFFFFFFFFu : ScopedStored - 1u;
}

std::uint32_t WidgetCanvas::ApplyLocalization(const LocalizationTable& InTable) noexcept
{
    if (!InTable.IsValid())
    {
        return 0;
    }
    std::uint32_t ScopedApplied = 0;
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < widgetCount; ++ScopedWidget)
    {
        const std::uint32_t ScopedStored = textKeys[ScopedWidget];
        if (ScopedStored == 0)
        {
            continue;
        }
        const std::string_view ScopedText = InTable.FindWithFallback(ScopedStored - 1u);
        if (ScopedText.empty())
        {
            continue;
        }
        if (ReplaceText(static_cast<std::int32_t>(ScopedWidget), ScopedText))
        {
            ++ScopedApplied;
        }
    }
    return ScopedApplied;
}

std::uint16_t WidgetCanvas::Caret(std::int32_t InWidget) const noexcept
{
    return ValidWidget(InWidget) ? caretOffsets[static_cast<std::uint32_t>(InWidget)] : 0;
}

bool WidgetCanvas::SetCaret(std::int32_t InWidget, std::uint16_t InOffset, bool InSelect) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    if (kinds[ScopedSlot] != WidgetKind::TextInput)
    {
        return false;
    }
    const std::string_view ScopedText = Text(InWidget);
    const std::uint16_t ScopedOffset = AlignUtf8Offset(ScopedText, InOffset);
    if (!InSelect)
    {
        selectionAnchors[ScopedSlot] = ScopedOffset;
    }
    caretOffsets[ScopedSlot] = ScopedOffset;
    return true;
}

bool WidgetCanvas::HasSelection(std::int32_t InWidget) const noexcept
{
    if (!ValidWidget(InWidget))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    return caretOffsets[ScopedSlot] != selectionAnchors[ScopedSlot];
}

std::string_view WidgetCanvas::SelectedText(std::int32_t InWidget) const noexcept
{
    if (!ValidWidget(InWidget))
    {
        return {};
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    const std::uint16_t ScopedCaret = caretOffsets[ScopedSlot];
    const std::uint16_t ScopedAnchor = selectionAnchors[ScopedSlot];
    if (ScopedCaret == ScopedAnchor)
    {
        return {};
    }
    const std::uint16_t ScopedBegin = std::min(ScopedCaret, ScopedAnchor);
    const std::uint16_t ScopedEnd = std::max(ScopedCaret, ScopedAnchor);
    const std::string_view ScopedText = Text(InWidget);
    return ScopedText.substr(ScopedBegin, ScopedEnd - ScopedBegin);
}

void WidgetCanvas::SelectAll(std::int32_t InWidget) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    selectionAnchors[ScopedSlot] = 0;
    caretOffsets[ScopedSlot] = textLengths[ScopedSlot];
}

void WidgetCanvas::ClearSelection(std::int32_t InWidget) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    selectionAnchors[ScopedSlot] = caretOffsets[ScopedSlot];
}

namespace
{
// Deletes [InBegin, InEnd) and leaves the caret at InBegin.
[[nodiscard]] bool EraseTextRange(WidgetCanvas& InCanvas, std::int32_t InWidget, std::uint16_t InBegin,
                                  std::uint16_t InEnd) noexcept
{
    if (InBegin >= InEnd)
    {
        return false;
    }
    const std::string_view ScopedText = InCanvas.Text(InWidget);
    if (InEnd > ScopedText.size())
    {
        return false;
    }
    std::array<char, kMaxTextPool> ScopedBuffer{};
    const std::size_t ScopedNewLength = ScopedText.size() - (InEnd - InBegin);
    std::memcpy(ScopedBuffer.data(), ScopedText.data(), InBegin);
    std::memcpy(ScopedBuffer.data() + InBegin, ScopedText.data() + InEnd, ScopedText.size() - InEnd);
    if (!InCanvas.ReplaceText(InWidget, std::string_view{ScopedBuffer.data(), ScopedNewLength}))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    InCanvas.caretOffsets[ScopedSlot] = InBegin;
    InCanvas.selectionAnchors[ScopedSlot] = InBegin;
    return true;
}
}

bool WidgetCanvas::InsertText(std::int32_t InWidget, std::string_view InText) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    if (kinds[ScopedSlot] != WidgetKind::TextInput || HasFlag(flags[ScopedSlot], WidgetFlags::ReadOnly))
    {
        return false;
    }
    // TextInput is single-line: a newline is ignored rather than inserted.
    std::array<char, kMaxTextPool> ScopedInsert{};
    std::size_t ScopedInsertLength = 0;
    for (const char ScopedByte : InText)
    {
        if (ScopedByte == '\n' || ScopedByte == '\r')
        {
            continue;
        }
        if (ScopedInsertLength >= ScopedInsert.size())
        {
            return false;
        }
        ScopedInsert[ScopedInsertLength++] = ScopedByte;
    }
    if (ScopedInsertLength == 0)
    {
        return false;
    }
    const std::string_view ScopedCurrent = Text(InWidget);
    const std::uint16_t ScopedBegin = std::min(caretOffsets[ScopedSlot], selectionAnchors[ScopedSlot]);
    const std::uint16_t ScopedEnd = std::max(caretOffsets[ScopedSlot], selectionAnchors[ScopedSlot]);
    const std::size_t ScopedNewLength = ScopedBegin + ScopedInsertLength + (ScopedCurrent.size() - ScopedEnd);
    if (ScopedNewLength > kMaxTextPool)
    {
        return false;
    }
    std::array<char, kMaxTextPool> ScopedBuffer{};
    std::memcpy(ScopedBuffer.data(), ScopedCurrent.data(), ScopedBegin);
    std::memcpy(ScopedBuffer.data() + ScopedBegin, ScopedInsert.data(), ScopedInsertLength);
    std::memcpy(ScopedBuffer.data() + ScopedBegin + ScopedInsertLength, ScopedCurrent.data() + ScopedEnd,
                ScopedCurrent.size() - ScopedEnd);
    if (!ReplaceText(InWidget, std::string_view{ScopedBuffer.data(), ScopedNewLength}))
    {
        return false;
    }
    const auto ScopedCaret = static_cast<std::uint16_t>(ScopedBegin + ScopedInsertLength);
    caretOffsets[ScopedSlot] = ScopedCaret;
    selectionAnchors[ScopedSlot] = ScopedCaret;
    return true;
}

bool WidgetCanvas::InsertCodepoint(std::int32_t InWidget, std::uint32_t InCodepoint) noexcept
{
    char ScopedBytes[4]{};
    const std::uint32_t ScopedCount = EncodeUtf8(InCodepoint, ScopedBytes);
    if (ScopedCount == 0)
    {
        return false;
    }
    return InsertText(InWidget, std::string_view{ScopedBytes, ScopedCount});
}

bool WidgetCanvas::Backspace(std::int32_t InWidget) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    if (kinds[ScopedSlot] != WidgetKind::TextInput || HasFlag(flags[ScopedSlot], WidgetFlags::ReadOnly))
    {
        return false;
    }
    const std::uint16_t ScopedCaret = caretOffsets[ScopedSlot];
    const std::uint16_t ScopedAnchor = selectionAnchors[ScopedSlot];
    if (ScopedCaret != ScopedAnchor)
    {
        return EraseTextRange(*this, InWidget, std::min(ScopedCaret, ScopedAnchor),
                              std::max(ScopedCaret, ScopedAnchor));
    }
    if (ScopedCaret == 0)
    {
        return false;
    }
    const std::string_view ScopedText = Text(InWidget);
    return EraseTextRange(*this, InWidget, PreviousUtf8(ScopedText, ScopedCaret), ScopedCaret);
}

bool WidgetCanvas::DeleteForward(std::int32_t InWidget) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    if (kinds[ScopedSlot] != WidgetKind::TextInput || HasFlag(flags[ScopedSlot], WidgetFlags::ReadOnly))
    {
        return false;
    }
    const std::uint16_t ScopedCaret = caretOffsets[ScopedSlot];
    const std::uint16_t ScopedAnchor = selectionAnchors[ScopedSlot];
    if (ScopedCaret != ScopedAnchor)
    {
        return EraseTextRange(*this, InWidget, std::min(ScopedCaret, ScopedAnchor),
                              std::max(ScopedCaret, ScopedAnchor));
    }
    const std::string_view ScopedText = Text(InWidget);
    if (ScopedCaret >= ScopedText.size())
    {
        return false;
    }
    return EraseTextRange(*this, InWidget, ScopedCaret, NextUtf8(ScopedText, ScopedCaret));
}

bool WidgetCanvas::MoveCaret(std::int32_t InWidget, std::int32_t InDeltaCodepoints, bool InSelect) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    if (kinds[ScopedSlot] != WidgetKind::TextInput)
    {
        return false;
    }
    const std::string_view ScopedText = Text(InWidget);
    std::uint16_t ScopedCaret = caretOffsets[ScopedSlot];
    if (InDeltaCodepoints > 0)
    {
        for (std::int32_t ScopedStep = 0; ScopedStep < InDeltaCodepoints; ++ScopedStep)
        {
            ScopedCaret = NextUtf8(ScopedText, ScopedCaret);
        }
    }
    else
    {
        for (std::int32_t ScopedStep = 0; ScopedStep < -InDeltaCodepoints; ++ScopedStep)
        {
            ScopedCaret = PreviousUtf8(ScopedText, ScopedCaret);
        }
    }
    if (!InSelect)
    {
        selectionAnchors[ScopedSlot] = ScopedCaret;
    }
    caretOffsets[ScopedSlot] = ScopedCaret;
    return true;
}

void WidgetCanvas::AdvanceTime(float InDt) noexcept
{
    if (!(InDt > 0.0f))
    {
        return;
    }
    uiTime += InDt;
    if (uiTime > 3600.0f)
    {
        uiTime = std::fmod(uiTime, 3600.0f);
    }
}

void WidgetCanvas::SetOpacity(std::int32_t InWidget, float InValue) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    const float ScopedValue = InValue < 0.0f ? 0.0f : (InValue > 1.0f ? 1.0f : InValue);
    opacities[ScopedSlot] = ScopedValue;
    opacityTargets[ScopedSlot] = ScopedValue;
}

void WidgetCanvas::FadeTo(std::int32_t InWidget, float InTarget, float InSpeed) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    opacityTargets[ScopedSlot] = InTarget < 0.0f ? 0.0f : (InTarget > 1.0f ? 1.0f : InTarget);
    if (InSpeed > 0.0f)
    {
        transitionSpeeds[ScopedSlot] = InSpeed;
    }
}

void WidgetCanvas::SetVisualOffset(std::int32_t InWidget, Vector2D InValue) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    visualOffsets[ScopedSlot] = InValue;
    visualOffsetTargets[ScopedSlot] = InValue;
}

void WidgetCanvas::SlideTo(std::int32_t InWidget, Vector2D InTarget, float InSpeed) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    visualOffsetTargets[ScopedSlot] = InTarget;
    if (InSpeed > 0.0f)
    {
        transitionSpeeds[ScopedSlot] = InSpeed;
    }
}

void WidgetCanvas::AdvanceTransitions(float InDt) noexcept
{
    if (!(InDt > 0.0f))
    {
        return;
    }
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < widgetCount; ++ScopedWidget)
    {
        const float ScopedStep = transitionSpeeds[ScopedWidget] * InDt;
        const float ScopedOpacity = opacities[ScopedWidget];
        const float ScopedTarget = opacityTargets[ScopedWidget];
        if (ScopedOpacity < ScopedTarget)
        {
            opacities[ScopedWidget] = std::min(ScopedTarget, ScopedOpacity + ScopedStep);
        }
        else if (ScopedOpacity > ScopedTarget)
        {
            opacities[ScopedWidget] = std::max(ScopedTarget, ScopedOpacity - ScopedStep);
        }
        const Vector2D ScopedOffset = visualOffsets[ScopedWidget];
        const Vector2D ScopedOffsetTarget = visualOffsetTargets[ScopedWidget];
        const Vector2D ScopedDelta{ScopedOffsetTarget.X - ScopedOffset.X, ScopedOffsetTarget.Y - ScopedOffset.Y};
        const float ScopedLength =
            std::sqrt(ScopedDelta.X * ScopedDelta.X + ScopedDelta.Y * ScopedDelta.Y);
        if (ScopedLength <= 1.0e-5f)
        {
            visualOffsets[ScopedWidget] = ScopedOffsetTarget;
        }
        else if (ScopedLength <= ScopedStep)
        {
            visualOffsets[ScopedWidget] = ScopedOffsetTarget;
        }
        else
        {
            const float ScopedRatio = ScopedStep / ScopedLength;
            visualOffsets[ScopedWidget] =
                Vector2D{ScopedOffset.X + ScopedDelta.X * ScopedRatio, ScopedOffset.Y + ScopedDelta.Y * ScopedRatio};
        }
    }
}

void WidgetCanvas::SetChecked(std::int32_t InWidget, bool InChecked) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    const std::uint32_t ScopedMask = static_cast<std::uint32_t>(WidgetFlags::Checked);
    flags[ScopedSlot] = InChecked ? (flags[ScopedSlot] | ScopedMask) : (flags[ScopedSlot] & ~ScopedMask);
    // A RadioButton clears its siblings in the same non-empty group.
    if (InChecked && kinds[ScopedSlot] == WidgetKind::RadioButton &&
        radioGroups[ScopedSlot] != kNoFocusGroup)
    {
        for (std::uint32_t ScopedOther = 0; ScopedOther < widgetCount; ++ScopedOther)
        {
            if (ScopedOther != ScopedSlot && kinds[ScopedOther] == WidgetKind::RadioButton &&
                radioGroups[ScopedOther] == radioGroups[ScopedSlot])
            {
                flags[ScopedOther] &= ~ScopedMask;
            }
        }
    }
}

bool WidgetCanvas::SetHyperlinks(std::int32_t InWidget, std::span<const WidgetHyperlink> InLinks) noexcept
{
    if (!ValidWidget(InWidget) || InLinks.size() > hyperlinks.size() - hyperlinkUsed)
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    firstHyperlink[ScopedSlot] = static_cast<std::uint16_t>(hyperlinkUsed);
    hyperlinkCounts[ScopedSlot] = 0;
    for (const WidgetHyperlink& ScopedLink : InLinks)
    {
        if (ScopedLink.begin >= ScopedLink.end || ScopedLink.end > textLengths[ScopedSlot])
        {
            // Tolerate authoring noise, never index past the text
            continue;
        }
        hyperlinks[hyperlinkUsed++] = ScopedLink;
        ++hyperlinkCounts[ScopedSlot];
    }
    return true;
}

void WidgetCanvas::SetAnchors(std::int32_t InWidget, Vector2D InMin, Vector2D InMax) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    anchorMins[ScopedSlot] = InMin;
    anchorMaxs[ScopedSlot] = InMax;
    InvalidateLayout();
}

void WidgetCanvas::SetOffsets(std::int32_t InWidget, Vector2D InMin, Vector2D InMax) noexcept
{
    if (!ValidWidget(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    offsetMins[ScopedSlot] = InMin;
    offsetMaxs[ScopedSlot] = InMax;
    InvalidateLayout();
}

void ScrollView::Bind(WidgetCanvas& InCanvas, WidgetHandle InPanel, WidgetHandle InContent,
                      WidgetHandle InScrollbar)
{
    if (!IsHandleSet(InPanel) || !IsHandleSet(InContent))
    {
        return;
    }
    panel = InPanel.widget;
    content = InContent.widget;
    scrollbar = IsHandleSet(InScrollbar) ? InScrollbar.widget : kNoWidget;
    const auto ScopedContent = static_cast<std::uint32_t>(content);
    contentBaseMin = InCanvas.offsetMins[ScopedContent];
    contentBaseMax = InCanvas.offsetMaxs[ScopedContent];
    offset = 0.0f;
    bound = true;
    Update(InCanvas, 0.0f);
}

float ScrollView::MaxOffset(const WidgetCanvas& InCanvas) const noexcept
{
    if (!bound)
    {
        return 0.0f;
    }
    const Rect ScopedPanel = InCanvas.RectOf(InCanvas.Handle(panel));
    const Rect ScopedContent = InCanvas.RectOf(InCanvas.Handle(content));
    const float ScopedViewport = ScopedPanel.max.Y - ScopedPanel.min.Y;
    const float ScopedContentHeight = ScopedContent.max.Y - ScopedContent.min.Y;
    const float ScopedMax = ScopedContentHeight - ScopedViewport;
    return ScopedMax > 0.0f ? ScopedMax : 0.0f;
}

void ScrollView::Update(WidgetCanvas& InCanvas, float InWheelDelta)
{
    if (!bound)
    {
        return;
    }
    const float ScopedMax = MaxOffset(InCanvas);
    offset -= InWheelDelta * 48.0f;
    if (offset < 0.0f)
    {
        offset = 0.0f;
    }
    if (offset > ScopedMax)
    {
        offset = ScopedMax;
    }
    InCanvas.SetOffsets(content, Vector2D{contentBaseMin.X, contentBaseMin.Y - offset},
                        Vector2D{contentBaseMax.X, contentBaseMax.Y - offset});

    if (scrollbar != kNoWidget)
    {
        const auto ScopedBar = static_cast<std::uint32_t>(scrollbar);
        const Rect ScopedPanel = InCanvas.RectOf(InCanvas.Handle(panel));
        const Rect ScopedContent = InCanvas.RectOf(InCanvas.Handle(content));
        const float ScopedViewport = ScopedPanel.max.Y - ScopedPanel.min.Y;
        const float ScopedContentHeight = ScopedContent.max.Y - ScopedContent.min.Y;
        InCanvas.ratios[ScopedBar] = ScopedContentHeight > 0.0f ? ScopedViewport / ScopedContentHeight : 1.0f;
        InCanvas.progresses[ScopedBar] = ScopedMax > 0.0f ? offset / ScopedMax : 0.0f;
    }
}

void TabBar::Bind(WidgetCanvas& InCanvas, WidgetHandle InSwitcher)
{
    if (!IsHandleSet(InSwitcher))
    {
        return;
    }
    switcher = InSwitcher.widget;
    tabCount = 0;
    bound = true;
    InCanvas.SetActivePage(switcher, 0);
}

bool TabBar::AddTab(WidgetCanvas& InCanvas, WidgetHandle InButton)
{
    if (!bound || tabCount >= kMaxTabs || !IsHandleSet(InButton))
    {
        return false;
    }
    const std::uint32_t ScopedSlot = tabCount;
    tabs[ScopedSlot] = InButton.widget;
    baseTints[ScopedSlot] = InCanvas.tints[static_cast<std::uint32_t>(InButton.widget)];
    ++tabCount;
    if (ScopedSlot == 0)
    {
        InCanvas.SetTint(tabs[ScopedSlot], activeTint);
    }
    return true;
}

bool TabBar::HandleClick(WidgetCanvas& InCanvas, std::int32_t InPressedWidget)
{
    if (!bound)
    {
        return false;
    }
    for (std::uint32_t ScopedTab = 0; ScopedTab < tabCount; ++ScopedTab)
    {
        if (tabs[ScopedTab] != InPressedWidget)
        {
            continue;
        }
        InCanvas.SetActivePage(switcher, ScopedTab);
        for (std::uint32_t ScopedOther = 0; ScopedOther < tabCount; ++ScopedOther)
        {
            InCanvas.SetTint(tabs[ScopedOther], ScopedOther == ScopedTab ? activeTint : baseTints[ScopedOther]);
        }
        return true;
    }
    return false;
}

std::uint32_t TabBar::ActiveTab(const WidgetCanvas& InCanvas) const noexcept
{
    return bound ? InCanvas.ActivePage(switcher) : 0u;
}

void Dropdown::Bind(WidgetCanvas& InCanvas, WidgetHandle InTrigger, WidgetHandle InPopup)
{
    if (!IsHandleSet(InTrigger) || !IsHandleSet(InPopup))
    {
        return;
    }
    trigger = InTrigger.widget;
    popup = InPopup.widget;
    itemCount = 0;
    selected = 0;
    open = false;
    bound = true;
    InCanvas.SetVisible(popup, false);
}

bool Dropdown::AddItem(WidgetCanvas& InCanvas, WidgetHandle InItem)
{
    if (!bound || itemCount >= kMaxItems || !IsHandleSet(InItem))
    {
        return false;
    }
    const std::uint32_t ScopedSlot = itemCount;
    items[ScopedSlot] = InItem.widget;
    baseTints[ScopedSlot] = InCanvas.tints[static_cast<std::uint32_t>(InItem.widget)];
    if (ScopedSlot == 0)
    {
        InCanvas.SetTint(items[ScopedSlot], activeTint);
    }
    ++itemCount;
    return true;
}

void Dropdown::SetOpen(WidgetCanvas& InCanvas, bool InOpen)
{
    if (!bound)
    {
        return;
    }
    open = InOpen;
    InCanvas.SetVisible(popup, open);
}

bool Dropdown::HandleClick(WidgetCanvas& InCanvas, std::int32_t InPressedWidget)
{    if (!bound)
    {
        return false;
    }
    if (InPressedWidget == trigger)
    {
        SetOpen(InCanvas, !open);
        return true;
    }
    for (std::uint32_t ScopedItem = 0; ScopedItem < itemCount; ++ScopedItem)
    {
        if (items[ScopedItem] != InPressedWidget)
        {
            continue;
        }
        selected = ScopedItem;
        for (std::uint32_t ScopedOther = 0; ScopedOther < itemCount; ++ScopedOther)
        {
            InCanvas.SetTint(items[ScopedOther],
                             ScopedOther == ScopedItem ? activeTint : baseTints[ScopedOther]);
        }
        // The trigger caption mirrors the selected row, as a ComboBox does.
        const std::string_view ScopedLabel = InCanvas.Text(items[ScopedItem]);
        if (!ScopedLabel.empty())
        {
            (void)InCanvas.SetText(trigger, ScopedLabel);
        }
        SetOpen(InCanvas, false);
        return true;
    }
    return false;
}

void ListView::Bind(WidgetCanvas& InCanvas, WidgetHandle InViewport, WidgetHandle InContent,
                    std::uint32_t InRowCapacity)
{
    if (!IsHandleSet(InViewport) || !IsHandleSet(InContent) || InRowCapacity == 0)
    {
        return;
    }
    viewport = InViewport.widget;
    content = InContent.widget;
    rowPool = InRowCapacity > kMaxRows ? kMaxRows : InRowCapacity;
    const Rect ScopedViewport = InCanvas.RectOf(InViewport);
    const float ScopedWidth = ScopedViewport.max.X - ScopedViewport.min.X;
    for (std::uint32_t ScopedRow = 0; ScopedRow < rowPool; ++ScopedRow)
    {
        WidgetDesc ScopedRowDesc;
        ScopedRowDesc.kind = WidgetKind::Panel;
        ScopedRowDesc.parent = content;
        ScopedRowDesc.anchorMin = Vector2D{0.0f, 0.0f};
        ScopedRowDesc.anchorMax = Vector2D{0.0f, 0.0f};
        ScopedRowDesc.offsetMin = Vector2D{0.0f, 0.0f};
        ScopedRowDesc.offsetMax = Vector2D{ScopedWidth, rowHeight};
        rows[ScopedRow] = InCanvas.AddWidget(ScopedRowDesc);
    }
    bound = true;
    offset = 0.0f;
    itemCount = 0;
    Update(InCanvas, 0.0f);
}

void ListView::SetItemCount(WidgetCanvas& InCanvas, std::uint32_t InItemCount)
{
    itemCount = InItemCount;
    Update(InCanvas, 0.0f);
}

void ListView::Update(WidgetCanvas& InCanvas, float InWheelDelta)
{
    if (!bound)
    {
        return;
    }
    offset -= InWheelDelta * 48.0f;
    const Rect ScopedViewport = InCanvas.RectOf(InCanvas.Handle(viewport));
    const float ScopedViewportHeight = ScopedViewport.max.Y - ScopedViewport.min.Y;
    const float ScopedWidth = ScopedViewport.max.X - ScopedViewport.min.X;
    const float ScopedContentHeight = static_cast<float>(itemCount) * rowHeight;
    const float ScopedMax = ScopedContentHeight - ScopedViewportHeight;
    if (offset < 0.0f)
    {
        offset = 0.0f;
    }
    if (offset > ScopedMax)
    {
        offset = ScopedMax > 0.0f ? ScopedMax : 0.0f;
    }
    InCanvas.SetOffsets(content, Vector2D{0.0f, 0.0f},
                        Vector2D{ScopedWidth, ScopedContentHeight > ScopedViewportHeight ? ScopedContentHeight
                                                                                        : ScopedViewportHeight});
    const auto ScopedFirst = static_cast<std::uint32_t>(offset / rowHeight);
    for (std::uint32_t ScopedRow = 0; ScopedRow < rowPool; ++ScopedRow)
    {
        const std::uint32_t ScopedItem = ScopedFirst + ScopedRow;
        const bool ScopedShown = ScopedItem < itemCount;
        InCanvas.SetVisible(rows[ScopedRow], ScopedShown);
        if (!ScopedShown)
        {
            continue;
        }
        const float ScopedTop = static_cast<float>(ScopedItem) * rowHeight - offset;
        InCanvas.SetOffsets(rows[ScopedRow], Vector2D{0.0f, ScopedTop},
                            Vector2D{ScopedWidth, ScopedTop + rowHeight});
    }
}

std::uint32_t ListView::FirstVisible() const noexcept
{
    return rowHeight > 0.0f ? static_cast<std::uint32_t>(offset / rowHeight) : 0u;
}

std::uint32_t ListView::ItemAtRow(std::uint32_t InRow) const noexcept
{
    return FirstVisible() + InRow;
}

std::int32_t ListView::RowWidget(std::uint32_t InRow) const noexcept
{
    return InRow < rowPool ? rows[InRow] : kNoWidget;
}

void RadialProgress::Paint(WidgetPaintContext& InContext) noexcept
{
    const float ScopedProgress = InContext.Params.X < 0.0f ? 0.0f : (InContext.Params.X > 1.0f ? 1.0f : InContext.Params.X);
    const float ScopedRatio = InContext.Params.Y > 0.0f && InContext.Params.Y < 1.0f ? InContext.Params.Y : 0.22f;
    std::uint32_t ScopedSegments = static_cast<std::uint32_t>(InContext.Params.Z);
    if (ScopedSegments < 4)
    {
        ScopedSegments = 4;
    }
    if (ScopedSegments > 32)
    {
        ScopedSegments = 32;
    }
    const float ScopedStart = InContext.Params.W;
    const Vector2D ScopedCenter{(InContext.LocalRect.min.X + InContext.LocalRect.max.X) * 0.5f,
                                (InContext.LocalRect.min.Y + InContext.LocalRect.max.Y) * 0.5f};
    const float ScopedRadius =
        std::min(InContext.LocalRect.max.X - InContext.LocalRect.min.X,
                 InContext.LocalRect.max.Y - InContext.LocalRect.min.Y) *
        0.5f;
    if (ScopedRadius <= 0.0f)
    {
        return;
    }
    const float ScopedThickness = std::max(1.0f, ScopedRadius * ScopedRatio);
    const float ScopedStep = 6.28318530718f / static_cast<float>(ScopedSegments);
    const float ScopedChord = 2.0f * ScopedRadius * std::sin(ScopedStep * 0.5f);
    for (std::uint32_t ScopedSegment = 0; ScopedSegment < ScopedSegments; ++ScopedSegment)
    {
        const float ScopedAngle = ScopedStart + (static_cast<float>(ScopedSegment) + 0.5f) * ScopedStep;
        const Vector2D ScopedDirection{std::cos(ScopedAngle), std::sin(ScopedAngle)};
        const Vector2D ScopedMid{ScopedCenter.X + ScopedDirection.X * (ScopedRadius - ScopedThickness * 0.5f),
                                 ScopedCenter.Y + ScopedDirection.Y * (ScopedRadius - ScopedThickness * 0.5f)};
        const Rect ScopedQuad{Vector2D{ScopedMid.X - ScopedThickness * 0.5f, ScopedMid.Y - ScopedChord * 0.5f},
                              Vector2D{ScopedMid.X + ScopedThickness * 0.5f, ScopedMid.Y + ScopedChord * 0.5f}};
        const bool ScopedFilled = (static_cast<float>(ScopedSegment) + 0.5f) / static_cast<float>(ScopedSegments) <=
                                  ScopedProgress;
        (void)InContext.Quad(ScopedQuad, ScopedFilled ? InContext.Fill : InContext.Tint, Vector2D{0.0f, 0.0f},
                             Vector2D{1.0f, 1.0f}, ScopedAngle);
    }
}

void RadialProgress::Configure(WidgetCanvas& InCanvas, WidgetHandle InWidget, float InProgress,
                               float InThicknessRatio, std::uint32_t InSegments, float InStartAngle)
{
    if (!IsHandleSet(InWidget))
    {
        return;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget.widget);
    InCanvas.SetCustomPaint(InWidget.widget, &RadialProgress::Paint);
    InCanvas.customParams[ScopedSlot] =
        Vector4D{InProgress, InThicknessRatio, static_cast<float>(InSegments), InStartAngle};
}

void RadialProgress::SetProgress(WidgetCanvas& InCanvas, WidgetHandle InWidget, float InProgress)
{
    if (!IsHandleSet(InWidget))
    {
        return;
    }
    InCanvas.customParams[static_cast<std::uint32_t>(InWidget.widget)].X = InProgress;
}

void TooltipPlacer::Bind(WidgetCanvas& InCanvas, WidgetHandle InTooltip, WidgetHandle InTarget)
{
    if (!IsHandleSet(InTooltip) || !IsHandleSet(InTarget))
    {
        return;
    }
    tooltip = InTooltip.widget;
    target = InTarget.widget;
    const Rect ScopedRect = InCanvas.RectOf(InTooltip);
    tooltipSize = Vector2D{ScopedRect.max.X - ScopedRect.min.X, ScopedRect.max.Y - ScopedRect.min.Y};
    // The placer owns the tooltip position, so it needs fixed anchors.
    InCanvas.SetAnchors(tooltip, Vector2D{0.0f, 0.0f}, Vector2D{0.0f, 0.0f});
    bound = true;
}

void TooltipPlacer::Place(WidgetCanvas& InCanvas)
{
    if (!bound)
    {
        return;
    }
    const Rect ScopedTarget = InCanvas.RectOf(InCanvas.Handle(target));
    const float ScopedWidth = tooltipSize.X > 0.0f ? tooltipSize.X : 120.0f;
    const float ScopedHeight = tooltipSize.Y > 0.0f ? tooltipSize.Y : 24.0f;
    float ScopedX = ScopedTarget.max.X + offset.X;
    if (ScopedX + ScopedWidth > InCanvas.size.X)
    {
        ScopedX = ScopedTarget.min.X - ScopedWidth - offset.X;
    }
    float ScopedY = ScopedTarget.max.Y + offset.Y;
    if (ScopedY + ScopedHeight > InCanvas.size.Y)
    {
        ScopedY = ScopedTarget.min.Y - ScopedHeight - offset.Y;
    }
    if (ScopedX < 0.0f)
    {
        ScopedX = 0.0f;
    }
    if (ScopedY < 0.0f)
    {
        ScopedY = 0.0f;
    }
    const float ScopedMaxX = InCanvas.size.X - ScopedWidth;
    const float ScopedMaxY = InCanvas.size.Y - ScopedHeight;
    if (ScopedX > ScopedMaxX)
    {
        ScopedX = ScopedMaxX > 0.0f ? ScopedMaxX : 0.0f;
    }
    if (ScopedY > ScopedMaxY)
    {
        ScopedY = ScopedMaxY > 0.0f ? ScopedMaxY : 0.0f;
    }
    InCanvas.SetOffsets(tooltip, Vector2D{ScopedX, ScopedY},
                        Vector2D{ScopedX + ScopedWidth, ScopedY + ScopedHeight});
}

bool WidgetCanvas::SetZOrder(std::int32_t InWidget, std::int32_t InValue) noexcept
{
    if (!ValidWidget(InWidget) || InValue < kMinZOrder || InValue > kMaxZOrder)
    {
        return false;
    }
    zOrders[static_cast<std::uint32_t>(InWidget)] = static_cast<std::int8_t>(InValue);
    // The cached draw order is sorted by z-order
    InvalidateLayout();
    return true;
}

std::string_view WidgetCanvas::Text(std::int32_t InWidget) const noexcept
{
    if (!ValidWidget(InWidget))
    {
        return {};
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    if (textLengths[ScopedSlot] == 0)
    {
        return {};
    }
    return {textPool.data() + textOffsets[ScopedSlot], textLengths[ScopedSlot]};
}

Rect WidgetCanvas::LocalRect(std::int32_t InWidget) const noexcept
{
    if (!ValidWidget(InWidget))
    {
        return {};
    }
    EnsureLayout();
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    return Rect{layout.localMins[ScopedSlot], layout.localMaxs[ScopedSlot]};
}

float WidgetCanvas::SliderValueAt(std::int32_t InWidget, Vector2D InCanvasPoint) const noexcept
{
    if (!ValidWidget(InWidget))
    {
        return 0.0f;
    }
    EnsureLayout();
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    const Rect ScopedRect{layout.localMins[ScopedSlot], layout.localMaxs[ScopedSlot]};
    const Vector2D ScopedExtent = ScopedRect.Size();
    if (!(ScopedExtent.X > 0.0f) || !(ScopedExtent.Y > 0.0f))
    {
        return 0.0f;
    }
    const bool ScopedVertical = HasFlag(flags[ScopedSlot], WidgetFlags::Vertical);
    const float ScopedAxis = ScopedVertical ? ScopedExtent.Y : ScopedExtent.X;
    const float ScopedHandle =
        kinds[ScopedSlot] == WidgetKind::Scrollbar
            ? ScrollThumbLength(*this, ScopedSlot, ScopedAxis)
            : SliderKnobLength(*this, ScopedSlot, ScopedExtent, ScopedVertical);
    const float ScopedTravel = ScopedAxis - ScopedHandle;
    if (!(ScopedTravel > 0.0f))
    {
        return 0.0f;
    }
    // Vertical tracks fill bottom-up, matching ProgressBar with the Vertical bit.
    const float ScopedAlong = ScopedVertical ? ScopedRect.max.Y - InCanvasPoint.Y : InCanvasPoint.X - ScopedRect.min.X;
    const float ScopedValue = (ScopedAlong - ScopedHandle * 0.5f) / ScopedTravel;
    return ScopedValue < 0.0f ? 0.0f : (ScopedValue > 1.0f ? 1.0f : ScopedValue);
}

// Layout
void GridLayoutPass(const WidgetCanvas& InCanvas) noexcept
{
    if (InCanvas.gridColumnCount == 0 || InCanvas.gridRowCount == 0)
    {
        return;
    }

    // A row track <= 0 means "as tall as the tallest explicit-height widget in
    std::array<float, WidgetCanvas::kTrackCapacity> ScopedRowHeights = InCanvas.tracks[1];
    for (std::uint32_t ScopedRow = 0; ScopedRow < InCanvas.gridRowCount; ++ScopedRow)
    {
        if (ScopedRowHeights[ScopedRow] > 0.0f)
        {
            continue;
        }
        float ScopedTallest = 0.0f;
        for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
        {
            if (InCanvas.cellColumns[ScopedWidget] == kNoCellTrack || InCanvas.cellRows[ScopedWidget] != ScopedRow ||
                InCanvas.rowSpans[ScopedWidget] != 1)
            {
                continue;
            }
            ScopedTallest = std::max(ScopedTallest, IntrinsicHeight(InCanvas, ScopedWidget));
        }
        ScopedRowHeights[ScopedRow] = ScopedTallest;
    }

    // offsets[i] is the canvas-local offset of track i's leading edge, so a
    std::array<float, kMaxGridColumns + 1> ScopedColumnOffsets{};
    ScopedColumnOffsets[0] = 0.0f;
    for (std::uint32_t ScopedColumn = 0; ScopedColumn < InCanvas.gridColumnCount; ++ScopedColumn)
    {
        ScopedColumnOffsets[ScopedColumn + 1] =
            ScopedColumnOffsets[ScopedColumn] + InCanvas.tracks[0][ScopedColumn] + InCanvas.gridGap.X;
    }
    std::array<float, kMaxGridRows + 1> ScopedRowOffsets{};
    ScopedRowOffsets[0] = 0.0f;
    for (std::uint32_t ScopedRow = 0; ScopedRow < InCanvas.gridRowCount; ++ScopedRow)
    {
        ScopedRowOffsets[ScopedRow + 1] =
            ScopedRowOffsets[ScopedRow] + ScopedRowHeights[ScopedRow] + InCanvas.gridGap.Y;
    }

    // Cells are recorded in the same cache the anchor phase uses; the anchor
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        if (InCanvas.cellColumns[ScopedWidget] == kNoCellTrack)
        {
            continue;
        }
        const std::uint32_t ScopedRow = InCanvas.cellRows[ScopedWidget];
        const std::uint32_t ScopedColumn = InCanvas.cellColumns[ScopedWidget];
        const std::uint32_t ScopedRowSpan = InCanvas.rowSpans[ScopedWidget];
        const std::uint32_t ScopedColumnSpan = InCanvas.columnSpans[ScopedWidget];
        if (ScopedRow + ScopedRowSpan > InCanvas.gridRowCount ||
            ScopedColumn + ScopedColumnSpan > InCanvas.gridColumnCount)
        {
            // Insertion validates this; defensive against direct struct writes
            continue;
        }
        const float ScopedWidth = ScopedColumnOffsets[ScopedColumn + ScopedColumnSpan] -
                                  ScopedColumnOffsets[ScopedColumn] - InCanvas.gridGap.X;
        const float ScopedHeight =
            ScopedRowOffsets[ScopedRow + ScopedRowSpan] - ScopedRowOffsets[ScopedRow] - InCanvas.gridGap.Y;
        const Vector2D ScopedCellMin{InCanvas.gridPadding.X + ScopedColumnOffsets[ScopedColumn],
                                     InCanvas.gridPadding.Y + ScopedRowOffsets[ScopedRow]};
        InCanvas.layout.localMins[ScopedWidget] = ScopedCellMin;
        InCanvas.layout.localMaxs[ScopedWidget] =
            Vector2D{ScopedCellMin.X + (ScopedWidth > 0.0f ? ScopedWidth : 0.0f),
                     ScopedCellMin.Y + (ScopedHeight > 0.0f ? ScopedHeight : 0.0f)};
    }
}

void ResolveAnchors(const WidgetCanvas& InCanvas) noexcept
{
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        const Rect ScopedReference = ReferenceRect(InCanvas, ScopedWidget);
        const Vector2D ScopedReferenceSize = ScopedReference.Size();
        const Vector2D ScopedAnchorMin = InCanvas.anchorMins[ScopedWidget];
        const Vector2D ScopedAnchorMax = InCanvas.anchorMaxs[ScopedWidget];
        const Vector2D ScopedOffsetMin = InCanvas.offsetMins[ScopedWidget];
        const Vector2D ScopedOffsetMax = InCanvas.offsetMaxs[ScopedWidget];

        Vector2D ScopedMinimum{};
        Vector2D ScopedMaximum{};

        // X: stretched anchors inset both edges; equal anchors make a fixed
        if (ScopedAnchorMax.X != ScopedAnchorMin.X)
        {
            ScopedMinimum.X = ScopedReference.min.X + ScopedAnchorMin.X * ScopedReferenceSize.X + ScopedOffsetMin.X;
            ScopedMaximum.X = ScopedReference.min.X + ScopedAnchorMax.X * ScopedReferenceSize.X + ScopedOffsetMax.X;
        }
        else
        {
            ScopedMinimum.X = ScopedReference.min.X + ScopedAnchorMin.X * ScopedReferenceSize.X + ScopedOffsetMin.X;
            ScopedMaximum.X = ScopedMinimum.X + (ScopedOffsetMax.X - ScopedOffsetMin.X);
        }

        // Y follows the same rule on the vertical axis.
        if (ScopedAnchorMax.Y != ScopedAnchorMin.Y)
        {
            ScopedMinimum.Y = ScopedReference.min.Y + ScopedAnchorMin.Y * ScopedReferenceSize.Y + ScopedOffsetMin.Y;
            ScopedMaximum.Y = ScopedReference.min.Y + ScopedAnchorMax.Y * ScopedReferenceSize.Y + ScopedOffsetMax.Y;
        }
        else
        {
            ScopedMinimum.Y = ScopedReference.min.Y + ScopedAnchorMin.Y * ScopedReferenceSize.Y + ScopedOffsetMin.Y;
            ScopedMaximum.Y = ScopedMinimum.Y + (ScopedOffsetMax.Y - ScopedOffsetMin.Y);
        }

        InCanvas.layout.localMins[ScopedWidget] = ScopedMinimum;
        InCanvas.layout.localMaxs[ScopedWidget] = ScopedMaximum;
    }
}

void ResolveLayout(const WidgetCanvas& InCanvas) noexcept
{
    GridLayoutPass(InCanvas);
    ResolveAnchors(InCanvas);

    // Insertion sort on <= 512 uint16s; run once per invalidation, not per
    auto& ScopedOrder = InCanvas.layout.drawOrder;
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        ScopedOrder[ScopedWidget] = static_cast<std::uint16_t>(ScopedWidget);
    }
    const auto ScopedLess = [&InCanvas](std::uint16_t InLeft, std::uint16_t InRight) noexcept
    {
        if (InCanvas.zOrders[InLeft] != InCanvas.zOrders[InRight])
        {
            return InCanvas.zOrders[InLeft] < InCanvas.zOrders[InRight];
        }
        return InLeft < InRight;
    };
    for (std::uint32_t ScopedI = 1; ScopedI < InCanvas.widgetCount; ++ScopedI)
    {
        const std::uint16_t ScopedKey = ScopedOrder[ScopedI];
        std::uint32_t ScopedJ = ScopedI;
        while (ScopedJ > 0 && ScopedLess(ScopedKey, ScopedOrder[ScopedJ - 1]))
        {
            ScopedOrder[ScopedJ] = ScopedOrder[ScopedJ - 1];
            --ScopedJ;
        }
        ScopedOrder[ScopedJ] = ScopedKey;
    }
    InCanvas.layout.valid = true;
}

// Font queries
std::array<std::uint8_t, 8> GlyphBitmap(char InGlyph) noexcept
{
    return kGlyphTable[GlyphIndex(InGlyph)];
}

Rect GlyphUv(char InGlyph) noexcept
{
    const std::uint32_t ScopedIndex = GlyphIndex(InGlyph);
    const std::uint32_t ScopedColumn = ScopedIndex % kFontAtlasColumns;
    const std::uint32_t ScopedRow = ScopedIndex / kFontAtlasColumns;
    const float ScopedAtlasColumns = static_cast<float>(kFontAtlasColumns);
    const float ScopedAtlasRows = static_cast<float>(kFontAtlasRows);
    const float ScopedU0 = static_cast<float>(ScopedColumn) / ScopedAtlasColumns;
    const float ScopedV0 = static_cast<float>(ScopedRow) / ScopedAtlasRows;
    const float ScopedU1 = static_cast<float>(ScopedColumn + 1) / ScopedAtlasColumns;
    const float ScopedV1 = static_cast<float>(ScopedRow + 1) / ScopedAtlasRows;
    return Rect{Vector2D{ScopedU0, ScopedV0}, Vector2D{ScopedU1, ScopedV1}};
}

Vector2D MeasureText(std::string_view InText, std::uint32_t InFontSize) noexcept
{
    if (InText.empty())
    {
        return {};
    }
    const std::uint32_t ScopedSize = InFontSize == 0 ? kDefaultFontSize : InFontSize;
    const float ScopedAdvance = Advance(ScopedSize);
    std::uint32_t ScopedLongestLine = 0;
    std::uint32_t ScopedLineGlyphs = 0;
    std::uint32_t ScopedLines = 1;
    for (const char ScopedCharacter : InText)
    {
        if (ScopedCharacter == '\n')
        {
            ScopedLongestLine = std::max(ScopedLongestLine, ScopedLineGlyphs);
            ScopedLineGlyphs = 0;
            ++ScopedLines;
            continue;
        }
        ++ScopedLineGlyphs;
    }
    ScopedLongestLine = std::max(ScopedLongestLine, ScopedLineGlyphs);
    return Vector2D{static_cast<float>(ScopedLongestLine) * ScopedAdvance,
                    static_cast<float>(ScopedLines) * static_cast<float>(ScopedSize) * kTextLineHeightRatio};
}

std::uint32_t CountGlyphs(std::string_view InText) noexcept
{
    return Utf8GlyphCount(InText);
}

// Pointer interaction
std::int32_t HitTest(const WidgetCanvas& InCanvas, Vector2D InCanvasPoint) noexcept
{
    InCanvas.EnsureLayout();
    const auto ScopedOrder = std::span<const std::uint16_t>(InCanvas.layout.drawOrder.data(), InCanvas.widgetCount);
    // Back to front: the last drawn widget is on top and wins the hit.
    for (std::size_t ScopedI = ScopedOrder.size(); ScopedI > 0; --ScopedI)
    {
        const auto ScopedWidget = ScopedOrder[ScopedI - 1];
        const auto ScopedSlot = static_cast<std::uint32_t>(ScopedWidget);
        if (!WidgetShown(InCanvas, ScopedSlot))
        {
            continue;
        }
        if (HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::IgnoreHitTest))
        {
            continue;
        }
        // Layout-only kinds never steal a click from a real widget.
        if (InCanvas.kinds[ScopedSlot] == WidgetKind::Spacer || InCanvas.kinds[ScopedSlot] == WidgetKind::Switcher)
        {
            continue;
        }
        // A clipped-out part of a child is not a hit target.
        if (!PointInsideClip(InCanvas, ScopedSlot, InCanvasPoint))
        {
            continue;
        }
        const Rect ScopedRect{InCanvas.layout.localMins[ScopedSlot], InCanvas.layout.localMaxs[ScopedSlot]};
        if (ScopedRect.Contains(InCanvasPoint))
        {
            return static_cast<std::int32_t>(ScopedWidget);
        }
    }
    return kNoWidget;
}

const WidgetHyperlink* HyperlinkAtByte(const WidgetCanvas& InCanvas, std::int32_t InWidget,
                                       std::uint32_t InByteOffset) noexcept
{
    if (!InCanvas.ValidWidget(InWidget) ||
        InCanvas.kinds[static_cast<std::uint32_t>(InWidget)] != WidgetKind::Hypertext)
    {
        return nullptr;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    const std::uint16_t ScopedFirst = InCanvas.firstHyperlink[ScopedSlot];
    for (std::uint16_t ScopedI = 0; ScopedI < InCanvas.hyperlinkCounts[ScopedSlot]; ++ScopedI)
    {
        const WidgetHyperlink& ScopedLink = InCanvas.hyperlinks[ScopedFirst + ScopedI];
        if (InByteOffset >= ScopedLink.begin && InByteOffset < ScopedLink.end)
        {
            return &ScopedLink;
        }
    }
    return nullptr;
}

const WidgetHyperlink* HitTestHyperlink(const WidgetCanvas& InCanvas, Vector2D InCanvasPoint, const FontAtlas* InAtlas,
                                        std::uint32_t* OutByteOffset) noexcept
{
    const std::int32_t ScopedWidget = HitTest(InCanvas, InCanvasPoint);
    if (ScopedWidget == kNoWidget)
    {
        return nullptr;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(ScopedWidget);
    if (InCanvas.kinds[ScopedSlot] != WidgetKind::Hypertext)
    {
        return nullptr;
    }
    const Rect ScopedRect{InCanvas.layout.localMins[ScopedSlot], InCanvas.layout.localMaxs[ScopedSlot]};
    const float ScopedLocalX = InCanvasPoint.X - ScopedRect.min.X;
    const float ScopedLocalY = InCanvasPoint.Y - ScopedRect.min.Y;
    const float ScopedFontSize = static_cast<float>(InCanvas.fontSizes[ScopedSlot]);
    const float ScopedLineHeight = ScopedFontSize * kTextLineHeightRatio;
    const std::uint32_t ScopedLine =
        ScopedLocalY > 0.0f ? static_cast<std::uint32_t>(ScopedLocalY / std::max(ScopedLineHeight, 1.0f)) : 0u;

    const std::string_view ScopedText = InCanvas.Text(ScopedWidget);
    std::size_t ScopedCursor = 0;
    std::uint32_t ScopedCurrentLine = 0;
    float ScopedPenX = 0.0f;
    std::uint32_t ScopedLastByte = 0;
    float ScopedScale = 1.0f;
    if (InAtlas != nullptr && InAtlas->IsOpen() && InAtlas->LineHeight() > 0.0f)
    {
        ScopedScale = ScopedFontSize / InAtlas->LineHeight();
    }
    while (ScopedCursor < ScopedText.size())
    {
        ScopedLastByte = static_cast<std::uint32_t>(ScopedCursor);
        const std::uint32_t ScopedCodepoint = DecodeUtf8(ScopedText, ScopedCursor);
        if (ScopedCodepoint == '\n')
        {
            if (ScopedCurrentLine == ScopedLine)
            {
                break;
            }
            ++ScopedCurrentLine;
            ScopedPenX = 0.0f;
            continue;
        }
        if (ScopedCurrentLine == ScopedLine && ScopedPenX > ScopedLocalX)
        {
            break;
        }
        if (InAtlas != nullptr && InAtlas->IsOpen())
        {
            const GlyphRecord* ScopedGlyph = InAtlas->Glyph(ScopedCodepoint);
            ScopedPenX +=
                ScopedGlyph != nullptr ? ScopedGlyph->advance * ScopedScale : ScopedFontSize * kGlyphAdvanceRatio;
        }
        else
        {
            ScopedPenX += ScopedFontSize * kGlyphAdvanceRatio;
        }
    }
    if (OutByteOffset != nullptr)
    {
        *OutByteOffset = ScopedLastByte;
    }
    return HyperlinkAtByte(InCanvas, ScopedWidget, ScopedLastByte);
}

std::int32_t PointerDown(WidgetCanvas& InCanvas, Vector2D InCanvasPoint) noexcept
{
    // One press at a time: clear the old one before testing, otherwise a
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        InCanvas.flags[ScopedWidget] &= ~static_cast<std::uint32_t>(WidgetFlags::Pressed);
    }
    InCanvas.dragIndex = kNoWidget;

    const std::int32_t ScopedHit = HitTest(InCanvas, InCanvasPoint);
    if (ScopedHit == kNoWidget)
    {
        return kNoWidget;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(ScopedHit);
    const bool ScopedEnabled = HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Enabled);
    if (ScopedEnabled && HasPointerFocus(InCanvas.focusPolicies[ScopedSlot]) && InCanvas.CanFocus(ScopedHit))
    {
        InCanvas.focusIndex = ScopedHit;
    }
    if (!ScopedEnabled)
    {
        return ScopedHit;
    }
    switch (InCanvas.kinds[ScopedSlot])
    {
        case WidgetKind::Button:
        case WidgetKind::Checkbox:
        case WidgetKind::ToggleSwitch:
        case WidgetKind::RadioButton:
            InCanvas.flags[ScopedSlot] |= static_cast<std::uint32_t>(WidgetFlags::Pressed);
            break;
        case WidgetKind::Slider:
        case WidgetKind::Scrollbar:
            // Press captures the pointer, jumps to the pressed value and starts
            InCanvas.flags[ScopedSlot] |= static_cast<std::uint32_t>(WidgetFlags::Pressed);
            InCanvas.dragIndex = ScopedHit;
            InCanvas.progresses[ScopedSlot] = InCanvas.SliderValueAt(ScopedHit, InCanvasPoint);
            break;
        default:
            break;
    }
    return ScopedHit;
}

std::int32_t PointerUp(WidgetCanvas& InCanvas, Vector2D InCanvasPoint) noexcept
{
    std::int32_t ScopedPressed = kNoWidget;
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        if (HasFlag(InCanvas.flags[ScopedWidget], WidgetFlags::Pressed))
        {
            ScopedPressed = static_cast<std::int32_t>(ScopedWidget);
            break;
        }
    }
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        InCanvas.flags[ScopedWidget] &= ~static_cast<std::uint32_t>(WidgetFlags::Pressed);
    }
    // A release always ends the drag, even when the pointer left the track.
    InCanvas.dragIndex = kNoWidget;
    if (ScopedPressed == kNoWidget)
    {
        return kNoWidget;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(ScopedPressed);
    if (!WidgetShown(InCanvas, ScopedSlot) || !HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Enabled))
    {
        return kNoWidget;
    }
    const Rect ScopedRect{InCanvas.layout.localMins[ScopedSlot], InCanvas.layout.localMaxs[ScopedSlot]};
    if (!ScopedRect.Contains(InCanvasPoint))
    {
        return kNoWidget;
    }
    switch (InCanvas.kinds[ScopedSlot])
    {
        case WidgetKind::Checkbox:
        case WidgetKind::ToggleSwitch:
            InCanvas.SetChecked(ScopedPressed, !InCanvas.Checked(ScopedPressed));
            break;
        case WidgetKind::RadioButton:
            // Clicking an already selected radio keeps it selected.
            InCanvas.SetChecked(ScopedPressed, true);
            break;
        case WidgetKind::Slider:
        case WidgetKind::Scrollbar:
            // Commit the exact release position; a drag may end off the knob.
            InCanvas.progresses[ScopedSlot] = InCanvas.SliderValueAt(ScopedPressed, InCanvasPoint);
            break;
        default:
            break;
    }
    return ScopedPressed;
}

std::int32_t PointerMove(WidgetCanvas& InCanvas, Vector2D InCanvasPoint) noexcept
{
    const std::int32_t ScopedHit = HitTest(InCanvas, InCanvasPoint);
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        InCanvas.flags[ScopedWidget] &= ~static_cast<std::uint32_t>(WidgetFlags::Hovered);
    }
    if (ScopedHit != kNoWidget)
    {
        const auto ScopedHitSlot = static_cast<std::uint32_t>(ScopedHit);
        InCanvas.flags[ScopedHitSlot] |= static_cast<std::uint32_t>(WidgetFlags::Hovered);
    }
    if (!InCanvas.ValidWidget(InCanvas.dragIndex))
    {
        InCanvas.dragIndex = kNoWidget;
        return kNoWidget;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InCanvas.dragIndex);
    const WidgetKind ScopedKind = InCanvas.kinds[ScopedSlot];
    if ((ScopedKind != WidgetKind::Slider && ScopedKind != WidgetKind::Scrollbar) ||
        !WidgetShown(InCanvas, ScopedSlot) || !HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Enabled))
    {
        InCanvas.dragIndex = kNoWidget;
        return kNoWidget;
    }
    InCanvas.progresses[ScopedSlot] = InCanvas.SliderValueAt(InCanvas.dragIndex, InCanvasPoint);
    return InCanvas.dragIndex;
}

bool SetCaretFromPoint(WidgetCanvas& InCanvas, std::int32_t InWidget, Vector2D InCanvasPoint,
                       const FontAtlas* InAtlas, bool InSelect) noexcept
{
    if (!InCanvas.ValidWidget(InWidget))
    {
        return false;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
    if (InCanvas.kinds[ScopedSlot] != WidgetKind::TextInput)
    {
        return false;
    }
    const Rect ScopedRect = InCanvas.LocalRect(InWidget);
    const std::string_view ScopedText = InCanvas.Text(InWidget);
    const float ScopedLocalX = InCanvasPoint.X - ScopedRect.min.X;
    const float ScopedFontSize = static_cast<float>(InCanvas.fontSizes[ScopedSlot]);
    float ScopedScale = 1.0f;
    if (InAtlas != nullptr && InAtlas->IsOpen() && InAtlas->LineHeight() > 0.0f)
    {
        ScopedScale = ScopedFontSize / InAtlas->LineHeight();
    }
    float ScopedPenX = 0.0f;
    std::uint16_t ScopedBest = 0;
    std::size_t ScopedCursor = 0;
    while (ScopedCursor < ScopedText.size())
    {
        const std::uint32_t ScopedCodepoint = DecodeUtf8(ScopedText, ScopedCursor);
        const auto ScopedNext = static_cast<std::uint16_t>(ScopedCursor);
        float ScopedAdvance = ScopedFontSize * kGlyphAdvanceRatio;
        if (InAtlas != nullptr && InAtlas->IsOpen())
        {
            const GlyphRecord* ScopedGlyph = InAtlas->Glyph(ScopedCodepoint);
            if (ScopedGlyph != nullptr)
            {
                ScopedAdvance = ScopedGlyph->advance * ScopedScale;
            }
        }
        if (ScopedLocalX < ScopedPenX + ScopedAdvance * 0.5f)
        {
            break;
        }
        ScopedPenX += ScopedAdvance;
        ScopedBest = ScopedNext;
    }
    return InCanvas.SetCaret(InWidget, ScopedBest, InSelect);
}

std::uint32_t BuildTextClusterOffsets(std::string_view InText, Text::ITextShaper& InShaper,
                                      const Text::TextShapeParams& InParams,
                                      std::span<std::uint16_t> OutOffsets) noexcept
{
    if (OutOffsets.empty())
    {
        return 0;
    }
    Text::TextShapeResult ScopedResult;
    if (!InShaper.Shape(InText, InParams, &ScopedResult))
    {
        return 0;
    }
    std::array<std::uint16_t, Text::kMaxShapedGlyphs + 2> ScopedClusters{};
    std::uint32_t ScopedUsed = 0;
    ScopedClusters[ScopedUsed++] = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedResult.Count; ++ScopedIndex)
    {
        const std::uint32_t ScopedCluster = ScopedResult.Glyphs[ScopedIndex].Cluster;
        if (ScopedCluster <= InText.size() && ScopedUsed < ScopedClusters.size())
        {
            ScopedClusters[ScopedUsed++] = static_cast<std::uint16_t>(ScopedCluster);
        }
    }
    if (ScopedUsed < ScopedClusters.size())
    {
        ScopedClusters[ScopedUsed++] = static_cast<std::uint16_t>(InText.size());
    }
    std::sort(ScopedClusters.begin(), ScopedClusters.begin() + ScopedUsed);
    ScopedUsed = static_cast<std::uint32_t>(std::unique(ScopedClusters.begin(),
                                                        ScopedClusters.begin() + ScopedUsed) -
                                            ScopedClusters.begin());
    const std::uint32_t ScopedWritten =
        std::min<std::uint32_t>(ScopedUsed, static_cast<std::uint32_t>(OutOffsets.size()));
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedWritten; ++ScopedIndex)
    {
        OutOffsets[ScopedIndex] = ScopedClusters[ScopedIndex];
    }
    return ScopedWritten;
}

namespace
{
// Index of the cluster whose start is the greatest value not above the caret.
[[nodiscard]] std::uint32_t FindClusterIndex(std::span<const std::uint16_t> InClusterOffsets,
                                             std::uint16_t InCaret) noexcept
{
    if (InClusterOffsets.empty())
    {
        return 0;
    }
    std::uint32_t ScopedIndex = 0;
    while (ScopedIndex + 1 < InClusterOffsets.size() && InClusterOffsets[ScopedIndex] < InCaret)
    {
        ++ScopedIndex;
    }
    return ScopedIndex;
}
}

bool MoveCaretClusters(WidgetCanvas& InCanvas, std::int32_t InWidget, std::int32_t InDeltaClusters, bool InSelect,
                       std::span<const std::uint16_t> InClusterOffsets) noexcept
{
    if (InClusterOffsets.empty() || !InCanvas.ValidWidget(InWidget) ||
        InCanvas.Kind(InWidget) != WidgetKind::TextInput)
    {
        return false;
    }
    const std::uint32_t ScopedIndex = FindClusterIndex(InClusterOffsets, InCanvas.Caret(InWidget));
    const auto ScopedTarget = static_cast<std::int32_t>(ScopedIndex) + InDeltaClusters;
    const std::int32_t ScopedClamped =
        ScopedTarget < 0 ? 0
                         : (ScopedTarget >= static_cast<std::int32_t>(InClusterOffsets.size())
                                ? static_cast<std::int32_t>(InClusterOffsets.size()) - 1
                                : ScopedTarget);
    return InCanvas.SetCaret(InWidget, InClusterOffsets[static_cast<std::uint32_t>(ScopedClamped)], InSelect);
}

bool BackspaceCluster(WidgetCanvas& InCanvas, std::int32_t InWidget,
                      std::span<const std::uint16_t> InClusterOffsets) noexcept
{
    if (InClusterOffsets.empty() || !InCanvas.ValidWidget(InWidget) ||
        InCanvas.Kind(InWidget) != WidgetKind::TextInput ||
        HasFlag(InCanvas.Flags(InWidget), WidgetFlags::ReadOnly))
    {
        return false;
    }
    const std::uint16_t ScopedCaret = InCanvas.Caret(InWidget);
    if (InCanvas.HasSelection(InWidget))
    {
        const std::uint16_t ScopedAnchor = InCanvas.selectionAnchors[static_cast<std::uint32_t>(InWidget)];
        return EraseTextRange(InCanvas, InWidget, std::min(ScopedCaret, ScopedAnchor),
                              std::max(ScopedCaret, ScopedAnchor));
    }
    const std::uint32_t ScopedIndex = FindClusterIndex(InClusterOffsets, ScopedCaret);
    if (ScopedCaret != InClusterOffsets[ScopedIndex] || ScopedIndex == 0)
    {
        return false;
    }
    return EraseTextRange(InCanvas, InWidget, InClusterOffsets[ScopedIndex - 1], InClusterOffsets[ScopedIndex]);
}

bool DeleteForwardCluster(WidgetCanvas& InCanvas, std::int32_t InWidget,
                          std::span<const std::uint16_t> InClusterOffsets) noexcept
{
    if (InClusterOffsets.empty() || !InCanvas.ValidWidget(InWidget) ||
        InCanvas.Kind(InWidget) != WidgetKind::TextInput ||
        HasFlag(InCanvas.Flags(InWidget), WidgetFlags::ReadOnly))
    {
        return false;
    }
    const std::uint16_t ScopedCaret = InCanvas.Caret(InWidget);
    const std::uint32_t ScopedIndex = FindClusterIndex(InClusterOffsets, ScopedCaret);
    if (ScopedCaret != InClusterOffsets[ScopedIndex] || ScopedIndex + 1 >= InClusterOffsets.size())
    {
        return false;
    }
    return EraseTextRange(InCanvas, InWidget, InClusterOffsets[ScopedIndex], InClusterOffsets[ScopedIndex + 1]);
}

void SetFocus(WidgetCanvas& InCanvas, std::int32_t InWidget) noexcept
{
    if (InWidget == kNoWidget || InCanvas.ValidWidget(InWidget))
    {
        InCanvas.focusIndex = InWidget;
    }
}

std::int32_t FocusedWidget(const WidgetCanvas& InCanvas) noexcept
{
    return InCanvas.ValidWidget(InCanvas.focusIndex) ? InCanvas.focusIndex : kNoWidget;
}

std::int32_t EffectiveFocusScope(const WidgetCanvas& InCanvas) noexcept
{
    if (InCanvas.ValidWidget(InCanvas.focusScope) &&
        WidgetShown(InCanvas, static_cast<std::uint32_t>(InCanvas.focusScope)))
    {
        return InCanvas.focusScope;
    }
    InCanvas.EnsureLayout();
    std::int32_t ScopedScope = kNoWidget;
    const std::span<const std::uint16_t> ScopedOrder(InCanvas.layout.drawOrder.data(), InCanvas.widgetCount);
    for (const std::uint16_t ScopedWidget : ScopedOrder)
    {
        const auto ScopedSlot = static_cast<std::uint32_t>(ScopedWidget);
        if (InCanvas.kinds[ScopedSlot] == WidgetKind::Overlay &&
            HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Modal) && WidgetShown(InCanvas, ScopedSlot))
        {
            // Draw order is back to front, so the last modal overlay is topmost.
            ScopedScope = static_cast<std::int32_t>(ScopedWidget);
        }
    }
    return ScopedScope;
}

std::int32_t FocusNext(WidgetCanvas& InCanvas, bool InBackwards) noexcept
{
    if (InCanvas.widgetCount == 0)
    {
        return kNoWidget;
    }
    const std::int32_t ScopedScope = EffectiveFocusScope(InCanvas);
    const std::uint16_t ScopedFilter = InCanvas.focusGroupFilter;
    const bool ScopedHasCurrent = InCanvas.ValidWidget(InCanvas.focusIndex) &&
                                  FocusCandidate(InCanvas, static_cast<std::uint32_t>(InCanvas.focusIndex), ScopedFilter,
                                                 ScopedScope);
    std::int32_t ScopedNext = kNoWidget;
    std::int32_t ScopedExtreme = kNoWidget;
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        if (!FocusCandidate(InCanvas, ScopedWidget, ScopedFilter, ScopedScope))
        {
            continue;
        }
        if (ScopedExtreme == kNoWidget)
        {
            ScopedExtreme = static_cast<std::int32_t>(ScopedWidget);
        }
        else
        {
            const auto ScopedExtremeSlot = static_cast<std::uint32_t>(ScopedExtreme);
            const bool ScopedBetter =
                InBackwards ? FocusKeyLess(InCanvas.tabIndices[ScopedExtremeSlot], ScopedExtremeSlot,
                                           InCanvas.tabIndices[ScopedWidget], ScopedWidget)
                            : FocusKeyLess(InCanvas.tabIndices[ScopedWidget], ScopedWidget,
                                           InCanvas.tabIndices[ScopedExtremeSlot], ScopedExtremeSlot);
            if (ScopedBetter)
            {
                ScopedExtreme = static_cast<std::int32_t>(ScopedWidget);
            }
        }
        if (!ScopedHasCurrent)
        {
            continue;
        }
        const auto ScopedCurrentSlot = static_cast<std::uint32_t>(InCanvas.focusIndex);
        const bool ScopedAfter =
            InBackwards ? FocusKeyLess(InCanvas.tabIndices[ScopedWidget], ScopedWidget,
                                       InCanvas.tabIndices[ScopedCurrentSlot], ScopedCurrentSlot)
                        : FocusKeyLess(InCanvas.tabIndices[ScopedCurrentSlot], ScopedCurrentSlot,
                                       InCanvas.tabIndices[ScopedWidget], ScopedWidget);
        if (!ScopedAfter)
        {
            continue;
        }
        if (ScopedNext == kNoWidget)
        {
            ScopedNext = static_cast<std::int32_t>(ScopedWidget);
        }
        else
        {
            const auto ScopedNextSlot = static_cast<std::uint32_t>(ScopedNext);
            const bool ScopedBetter =
                InBackwards ? FocusKeyLess(InCanvas.tabIndices[ScopedNextSlot], ScopedNextSlot,
                                           InCanvas.tabIndices[ScopedWidget], ScopedWidget)
                            : FocusKeyLess(InCanvas.tabIndices[ScopedWidget], ScopedWidget,
                                           InCanvas.tabIndices[ScopedNextSlot], ScopedNextSlot);
            if (ScopedBetter)
            {
                ScopedNext = static_cast<std::int32_t>(ScopedWidget);
            }
        }
    }
    if (ScopedNext == kNoWidget && (!ScopedHasCurrent || InCanvas.focusWrap))
    {
        ScopedNext = ScopedExtreme;
    }
    if (ScopedNext != kNoWidget)
    {
        InCanvas.focusIndex = ScopedNext;
    }
    return ScopedNext;
}

std::int32_t FocusMove(WidgetCanvas& InCanvas, FocusDirection InDirection) noexcept
{
    const std::int32_t ScopedCurrent = FocusedWidget(InCanvas);
    if (ScopedCurrent == kNoWidget)
    {
        return kNoWidget;
    }
    InCanvas.EnsureLayout();
    const std::int32_t ScopedScope = EffectiveFocusScope(InCanvas);
    const std::uint16_t ScopedFilter = InCanvas.focusGroupFilter;
    const auto ScopedCurrentSlot = static_cast<std::uint32_t>(ScopedCurrent);
    const Rect ScopedCurrentRect{InCanvas.layout.localMins[ScopedCurrentSlot],
                                 InCanvas.layout.localMaxs[ScopedCurrentSlot]};
    const Vector2D ScopedOrigin = ScopedCurrentRect.Center();
    std::int32_t ScopedBest = kNoWidget;
    float ScopedBestScore = 0.0f;
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        if (ScopedWidget == ScopedCurrentSlot || !FocusCandidate(InCanvas, ScopedWidget, ScopedFilter, ScopedScope, false))
        {
            continue;
        }
        const Rect ScopedRect{InCanvas.layout.localMins[ScopedWidget], InCanvas.layout.localMaxs[ScopedWidget]};
        const Vector2D ScopedCenter = ScopedRect.Center();
        const float ScopedDeltaX = ScopedCenter.X - ScopedOrigin.X;
        const float ScopedDeltaY = ScopedCenter.Y - ScopedOrigin.Y;
        float ScopedPrimary = 0.0f;
        float ScopedCross = 0.0f;
        bool ScopedMatches = false;
        switch (InDirection)
        {
            case FocusDirection::Left:
                ScopedMatches = ScopedDeltaX < -1.0e-4f;
                ScopedPrimary = -ScopedDeltaX;
                ScopedCross = std::abs(ScopedDeltaY);
                break;
            case FocusDirection::Right:
                ScopedMatches = ScopedDeltaX > 1.0e-4f;
                ScopedPrimary = ScopedDeltaX;
                ScopedCross = std::abs(ScopedDeltaY);
                break;
            case FocusDirection::Up:
                ScopedMatches = ScopedDeltaY < -1.0e-4f;
                ScopedPrimary = -ScopedDeltaY;
                ScopedCross = std::abs(ScopedDeltaX);
                break;
            case FocusDirection::Down:
                ScopedMatches = ScopedDeltaY > 1.0e-4f;
                ScopedPrimary = ScopedDeltaY;
                ScopedCross = std::abs(ScopedDeltaX);
                break;
        }
        if (!ScopedMatches)
        {
            continue;
        }
        // The perpendicular penalty keeps a straight neighbour ahead of a
        // diagonal one at the same primary distance.
        const float ScopedScore = ScopedPrimary + ScopedCross * 1.5f;
        if (ScopedBest == kNoWidget || ScopedScore < ScopedBestScore)
        {
            ScopedBest = static_cast<std::int32_t>(ScopedWidget);
            ScopedBestScore = ScopedScore;
        }
    }
    if (ScopedBest != kNoWidget)
    {
        InCanvas.focusIndex = ScopedBest;
    }
    return ScopedBest;
}

std::int32_t ActivateFocused(WidgetCanvas& InCanvas) noexcept
{
    const std::int32_t ScopedFocused = FocusedWidget(InCanvas);
    if (ScopedFocused == kNoWidget)
    {
        return kNoWidget;
    }
    const auto ScopedSlot = static_cast<std::uint32_t>(ScopedFocused);
    if (!WidgetShown(InCanvas, ScopedSlot) || !HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Enabled))
    {
        return kNoWidget;
    }
    if (InCanvas.kinds[ScopedSlot] == WidgetKind::Checkbox || InCanvas.kinds[ScopedSlot] == WidgetKind::ToggleSwitch)
    {
        InCanvas.SetChecked(ScopedFocused, !InCanvas.Checked(ScopedFocused));
    }
    else if (InCanvas.kinds[ScopedSlot] == WidgetKind::RadioButton)
    {
        InCanvas.SetChecked(ScopedFocused, true);
    }
    return ScopedFocused;
}

// WidgetCanvasSystem - render extraction
bool WidgetCanvasSystem::MountFonts(Rhi::RHI& InRhi)
{
    // Mandatory cold-start stage. Every failure here is fatal UI-host state: a
    const auto ScopedPanic = [](const char* InReason) noexcept
    {
        LogError("pinkui",
                 "PANIC: WidgetCanvasSystem::MountFonts failed: {}. The VFS .fbyfont set was not mounted into a GPU "
                 "texture array; text rendering is unavailable (no legacy bitmap fallback)",
                 InReason);
        FBY_ASSERT_MSG(false, "Pink UI: MountFonts() must succeed before the first BuildFrame()");
    };

    if (MFonts == nullptr)
    {
        ScopedPanic("the UI host has no FontAtlasSet");
        return false;
    }
    if (MFonts->Count() == 0)
    {
        ScopedPanic("the FontAtlasSet is empty (no .fbyfont was loaded from the VFS)");
        return false;
    }
    if (MFonts->IsCurrent())
    {
        return true;
    }
    if (!MFonts->MountToTextureArray(InRhi))
    {
        ScopedPanic("FontAtlasSet::MountToTextureArray() rejected the set (see the preceding 'fonts' line)");
        return false;
    }
    LogInfo("pinkui", "font array #{} mounted and current (generation {})", MFonts->TextureArray().id,
            MFonts->Generation());
    return true;
}

BuildStatus WidgetCanvasSystem::BuildFrame(Fby::Ecs::SystemContext& InContext, const WidgetCanvas& InCanvas,
                                           FrameOutput& Output) noexcept
{
    Output.itemsWritten = 0;
    Output.batchesWritten = 0;
    Output.widgetsDrawn = 0;
    Output.glyphQuads = 0;
    Output.requiredItems = 0;
    Output.fontTextureArray = 0xFFFFFFFFu;
    Output.fontContractViolated = false;
    Output.overflow = false;

    if (InCanvas.widgetCount > kMaxWidgets)
    {
        return BuildStatus::Invalid;
    }

    InCanvas.EnsureLayout();
    const float ScopedDisplayScale = InCanvas.displayScale > 0.0f ? InCanvas.displayScale : 1.0f;

    // Nothing is written until the full budget is known, so an overflow can
    std::uint32_t ScopedRequired = 0;
    std::uint32_t ScopedGlyphs = 0;
    std::uint32_t ScopedVisibleWidgets = 0;
    // The native font array can back the whole canvas only when no widget needs
    bool ScopedForeignImage = false;
    bool ScopedMissingFontAtlas = false;
    bool ScopedHasText = false;
    // Quad quota of every custom widget, filled by the budget callback and
    // enforced by the emit callback.
    std::array<std::uint16_t, kMaxWidgets> ScopedCustomQuotas{};
    // Scratch for TextBlock emission. It is intentionally viewport-sized: a
    // whole book belongs to TextRenderSystem, not to one widget.
    std::array<Rhi::RenderItem, 1024> ScopedTextBlockItems{};
    std::array<std::uint16_t, kMaxWidgets> ScopedTextBlockCounts{};
    const auto ScopedRenderTextBlock = [&](std::uint32_t InWidget, Rect InRect,
                                           std::span<Rhi::RenderItem> OutItems) noexcept -> std::uint32_t
    {
        if (InContext.text == nullptr || InContext.fonts == nullptr)
        {
            return 0;
        }
        const Text::TextAssetRef ScopedRef = InCanvas.textRefs[InWidget];
        if (!ScopedRef.IsValid() || !InContext.text->HasAsset(ScopedRef.Asset))
        {
            return 0;
        }
        const FontAtlas* ScopedBase = InContext.fonts->At(InCanvas.fontAtlasIndices[InWidget]);
        if (ScopedBase == nullptr || !ScopedBase->IsOpen())
        {
            return 0;
        }
        Text::TextLayoutParams ScopedLayoutParams;
        ScopedLayoutParams.MaxWidth = InRect.Size().X;
        ScopedLayoutParams.Advance = 1.0f;
        ScopedLayoutParams.Fonts = InContext.fonts;
        ScopedLayoutParams.FontId = ScopedBase->FontId();
        ScopedLayoutParams.FontSize = static_cast<float>(InCanvas.fontSizes[InWidget]);
        ScopedLayoutParams.Spans = InContext.text->SpansFor(ScopedRef.Asset);
        ScopedLayoutParams.Styles = InContext.text->StylesFor(ScopedRef.Asset);
        ScopedLayoutParams.Inlines = InContext.text->InlinesFor(ScopedRef.Asset);
        ScopedLayoutParams.Vertical = HasFlag(InCanvas.flags[InWidget], WidgetFlags::TextVertical);
        ScopedLayoutParams.RightToLeft = HasFlag(InCanvas.flags[InWidget], WidgetFlags::TextRightToLeft);
        Text::TextRenderParams ScopedRenderParams;
        ScopedRenderParams.Fonts = InContext.fonts;
        ScopedRenderParams.FontAtlasIndex = InCanvas.fontAtlasIndices[InWidget];
        ScopedRenderParams.FontSize = static_cast<float>(InCanvas.fontSizes[InWidget]);
        ScopedRenderParams.Color = WidgetTextColor(InCanvas, InWidget);
        ScopedRenderParams.Spans = ScopedLayoutParams.Spans;
        ScopedRenderParams.Styles = ScopedLayoutParams.Styles;
        ScopedRenderParams.Inlines = ScopedLayoutParams.Inlines;
        ScopedRenderParams.InlineSprites = InContext.text->InlineSpritesFor(ScopedRef.Asset);
        ScopedRenderParams.Vertical = ScopedLayoutParams.Vertical;
        ScopedRenderParams.RightToLeft = ScopedLayoutParams.RightToLeft;
        const std::string_view ScopedBlockText = InContext.text->Block(ScopedRef);
        // Prefer the HarfBuzz-shaped path when the host supplied a shaper and a
        // shape cache; it carries clusters and glyph ids for any script. Inline
        // atoms are emitted at their placeholder clusters by the shaped renderer.
        if (InContext.textShapes != nullptr && InContext.textShaper != nullptr)
        {
            Text::TextShapeKey ScopedKey;
            ScopedKey.TextHash = Text::TextShapeCache::HashText(ScopedBlockText);
            ScopedKey.FontId = ScopedBase->FontId();
            ScopedKey.TextLength = static_cast<std::uint32_t>(ScopedBlockText.size());
            ScopedKey.PixelSize = ScopedRenderParams.FontSize;
            ScopedKey.Direction = ScopedLayoutParams.Vertical
                                      ? Text::TextDirection::TopToBottom
                                      : (ScopedLayoutParams.RightToLeft ? Text::TextDirection::RightToLeft
                                                                        : Text::TextDirection::Auto);
            ScopedKey.MaxWidth = ScopedLayoutParams.MaxWidth;
            ScopedKey.Vertical = ScopedLayoutParams.Vertical;
            ScopedKey.InlineHash = Text::TextShapeCache::HashInlines(ScopedLayoutParams.Inlines);
            Text::TextShapeHandle ScopedHandle = InContext.textShapes->Find(ScopedKey);
            if (!ScopedHandle.IsValid())
            {
                ScopedHandle = InContext.textShapes->Insert(*InContext.textShaper, ScopedBlockText, ScopedKey,
                                                            ScopedLayoutParams);
            }
            if (ScopedHandle.IsValid())
            {
                const Text::TextShapeView ScopedShape = InContext.textShapes->View(ScopedHandle);
                if (!ScopedShape.Glyphs.empty())
                {
                    std::span<const Text::ShapedLine> ScopedLines = ScopedShape.Lines;
                    const std::uint16_t ScopedFirstLine = InCanvas.textFirstLines[InWidget];
                    const std::uint16_t ScopedLineCount = InCanvas.textLineCounts[InWidget];
                    if (ScopedLineCount > 0)
                    {
                        if (ScopedFirstLine >= ScopedLines.size())
                        {
                            return 0;
                        }
                        const std::size_t ScopedVisible = std::min<std::size_t>(
                            ScopedLineCount, ScopedLines.size() - ScopedFirstLine);
                        ScopedLines = ScopedLines.subspan(ScopedFirstLine, ScopedVisible);
                    }
                    Text::TextRenderStats ScopedShapedStats;
                    return Text::BuildShapedTextRenderItems(ScopedShape.Glyphs, ScopedLines, InRect.min,
                                                            ScopedRenderParams, OutItems, &ScopedShapedStats);
                }
            }
        }
        const Text::TextLayoutResult* ScopedLayout = InContext.text->CachedLayout(ScopedRef, ScopedLayoutParams);
        if (ScopedLayout == nullptr)
        {
            return 0;
        }
        const std::uint16_t ScopedLineCount = InCanvas.textLineCounts[InWidget];
        if (ScopedLineCount > 0)
        {
            const std::uint16_t ScopedFirstLine = InCanvas.textFirstLines[InWidget];
            if (ScopedFirstLine >= ScopedLayout->Count)
            {
                return 0;
            }
            Text::TextLayoutResult ScopedWindow = *ScopedLayout;
            const std::uint32_t ScopedVisible =
                std::min<std::uint32_t>(ScopedLineCount, ScopedLayout->Count - ScopedFirstLine);
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedVisible; ++ScopedIndex)
            {
                ScopedWindow.Lines[ScopedIndex] = ScopedLayout->Lines[ScopedFirstLine + ScopedIndex];
            }
            ScopedWindow.Count = ScopedVisible;
            Text::TextRenderStats ScopedWindowStats;
            return Text::BuildTextRenderItems(ScopedBlockText, ScopedWindow, InRect.min, ScopedRenderParams,
                                              OutItems, &ScopedWindowStats);
        }
        Text::TextRenderStats ScopedStats;
        return Text::BuildTextRenderItems(ScopedBlockText, *ScopedLayout, InRect.min, ScopedRenderParams, OutItems,
                                          &ScopedStats);
    };
    // Shaped single-line field. RTL is left on the codepoint fallback until the
    // caret mapping has a visual-order cluster map.
    const auto ScopedShapeTextInput = [&](std::uint32_t InWidget, Rect InRect) noexcept -> Text::TextShapeView
    {
        if (InContext.textShapes == nullptr || InContext.textShaper == nullptr || InContext.fonts == nullptr)
        {
            return {};
        }
        const std::string_view ScopedInputText = InCanvas.Text(static_cast<std::int32_t>(InWidget));
        if (ScopedInputText.empty())
        {
            return {};
        }
        const FontAtlas* ScopedBase = InContext.fonts->At(InCanvas.fontAtlasIndices[InWidget]);
        if (ScopedBase == nullptr || !ScopedBase->IsOpen())
        {
            return {};
        }
        const float ScopedGlyphSize =
            std::max(1.0f, std::round(static_cast<float>(InCanvas.fontSizes[InWidget]) * ScopedDisplayScale)) /
            ScopedDisplayScale;
        const bool ScopedRightToLeft = HasFlag(InCanvas.flags[InWidget], WidgetFlags::TextRightToLeft);
        Text::TextShapeKey ScopedKey;
        ScopedKey.TextHash = Text::TextShapeCache::HashText(ScopedInputText);
        ScopedKey.FontId = ScopedBase->FontId();
        ScopedKey.TextLength = static_cast<std::uint32_t>(ScopedInputText.size());
        ScopedKey.PixelSize = ScopedGlyphSize;
        ScopedKey.Direction = ScopedRightToLeft ? Text::TextDirection::RightToLeft : Text::TextDirection::LeftToRight;
        ScopedKey.MaxWidth = InRect.Size().X;
        ScopedKey.Vertical = false;
        Text::TextShapeHandle ScopedHandle = InContext.textShapes->Find(ScopedKey);
        if (!ScopedHandle.IsValid())
        {
            Text::TextLayoutParams ScopedLayoutParams;
            ScopedLayoutParams.MaxWidth = InRect.Size().X;
            ScopedLayoutParams.Advance = 1.0f;
            ScopedLayoutParams.Fonts = InContext.fonts;
            ScopedLayoutParams.FontId = ScopedBase->FontId();
            ScopedLayoutParams.FontSize = ScopedGlyphSize;
            ScopedHandle =
                InContext.textShapes->Insert(*InContext.textShaper, ScopedInputText, ScopedKey, ScopedLayoutParams);
        }
        return ScopedHandle.IsValid() ? InContext.textShapes->View(ScopedHandle) : Text::TextShapeView{};
    };
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < InCanvas.widgetCount; ++ScopedWidget)
    {
        if (!WidgetShown(InCanvas, ScopedWidget))
        {
            continue;
        }
        ++ScopedVisibleWidgets;
        const Rect ScopedRect{InCanvas.layout.localMins[ScopedWidget], InCanvas.layout.localMaxs[ScopedWidget]};
        const Vector2D ScopedExtent = ScopedRect.Size();
        if (ScopedExtent.X <= 0.0f || ScopedExtent.Y <= 0.0f)
        {
            // Nothing draws, no glyphs either
            continue;
        }
        switch (InCanvas.kinds[ScopedWidget])
        {
            case WidgetKind::Panel:
                ScopedRequired += 1;
                break;
            case WidgetKind::Button:
            {
                // The button quad plus the centred caption, if any.
                ScopedRequired += 1;
                const std::uint32_t ScopedCount = CountGlyphs(InCanvas.Text(static_cast<std::int32_t>(ScopedWidget)));
                if (ScopedCount > 0)
                {
                    ScopedHasText = true;
                    const FontAtlas* ScopedAtlas =
                        InContext.fonts != nullptr ? InContext.fonts->At(InCanvas.fontAtlasIndices[ScopedWidget])
                                                   : nullptr;
                    if (ScopedAtlas == nullptr || !ScopedAtlas->IsOpen())
                    {
                        ScopedMissingFontAtlas = true;
                    }
                }
                ScopedGlyphs += ScopedCount;
                ScopedRequired += ScopedCount;
                break;
            }
            case WidgetKind::Image:
                ScopedRequired += 1;
                if (InCanvas.layers[ScopedWidget] != kWhiteLayer)
                {
                    ScopedForeignImage = true;
                }
                break;
            case WidgetKind::Label:
            case WidgetKind::Hypertext:
            {
                const std::uint32_t ScopedCount = CountGlyphs(InCanvas.Text(static_cast<std::int32_t>(ScopedWidget)));
                if (ScopedCount > 0)
                {
                    ScopedHasText = true;
                    // A label is drawable only through the mounted font array. Any
                    const FontAtlas* ScopedAtlas =
                        InContext.fonts != nullptr ? InContext.fonts->At(InCanvas.fontAtlasIndices[ScopedWidget])
                                                   : nullptr;
                    if (ScopedAtlas == nullptr || !ScopedAtlas->IsOpen())
                    {
                        ScopedMissingFontAtlas = true;
                    }
                }
                ScopedGlyphs += ScopedCount;
                ScopedRequired += ScopedCount;
                break;
            }
            case WidgetKind::ProgressBar:
                // Track quad always, fill quad only when there is a fill to show.
                ScopedRequired += InCanvas.progresses[ScopedWidget] > 0.0f ? 2 : 1;
                break;
            case WidgetKind::Border:
                ScopedRequired += BorderQuadCount(InCanvas, ScopedWidget, ScopedExtent);
                break;
            case WidgetKind::Slider:
                // Track and knob always, fill only when the value is above zero.
                ScopedRequired += InCanvas.progresses[ScopedWidget] > 0.0f ? 3 : 2;
                break;
            case WidgetKind::Checkbox:
                ScopedRequired += HasFlag(InCanvas.flags[ScopedWidget], WidgetFlags::Checked) ? 3 : 1;
                break;
            case WidgetKind::Scrollbar:
                ScopedRequired += 2;
                break;
            case WidgetKind::NineSlice:
                ScopedRequired += 9;
                break;
            case WidgetKind::ToggleSwitch:
                ScopedRequired += 2;
                break;
            case WidgetKind::RadioButton:
                ScopedRequired += HasFlag(InCanvas.flags[ScopedWidget], WidgetFlags::Checked) ? 2 : 1;
                break;
            case WidgetKind::TextBlock:
            {
                const Text::TextAssetRef ScopedRef = InCanvas.textRefs[ScopedWidget];
                const std::string_view ScopedBlock =
                    InContext.text != nullptr && ScopedRef.IsValid() ? InContext.text->Block(ScopedRef)
                                                                     : std::string_view{};
                if (!ScopedBlock.empty())
                {
                    ScopedHasText = true;
                    const FontAtlas* ScopedBase =
                        InContext.fonts != nullptr ? InContext.fonts->At(InCanvas.fontAtlasIndices[ScopedWidget])
                                                   : nullptr;
                    if (ScopedBase == nullptr || !ScopedBase->IsOpen())
                    {
                        ScopedMissingFontAtlas = true;
                    }
                }
                const std::uint32_t ScopedCount = ScopedRenderTextBlock(
                    ScopedWidget, ScopedRect, std::span<Rhi::RenderItem>(ScopedTextBlockItems));
                ScopedTextBlockCounts[ScopedWidget] = static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(ScopedCount, static_cast<std::uint32_t>(ScopedTextBlockItems.size())));
                ScopedRequired += ScopedCount;
                ScopedGlyphs += ScopedCount;
                break;
            }
            case WidgetKind::Overlay:
                ScopedRequired += 1;
                break;
            case WidgetKind::Custom:
            {
                const CustomWidgetPaint ScopedPaintFn = InCanvas.customPaints[ScopedWidget];
                if (ScopedPaintFn == nullptr)
                {
                    break;
                }
                CustomBudgetState ScopedBudget{};
                WidgetPaintContext ScopedPaint{InCanvas,
                                               static_cast<std::int32_t>(ScopedWidget),
                                               ScopedRect,
                                               InCanvas.customParams[ScopedWidget],
                                               InCanvas.customUsers[ScopedWidget],
                                               WidgetTint(InCanvas, ScopedWidget),
                                               WidgetFill(InCanvas, ScopedWidget),
                                               InCanvas.layers[ScopedWidget],
                                               &CountCustomQuad,
                                               &ScopedBudget};
                ScopedPaintFn(ScopedPaint);
                ScopedCustomQuotas[ScopedWidget] = static_cast<std::uint16_t>(ScopedBudget.Count);
                ScopedRequired += ScopedBudget.Count;
                break;
            }
            case WidgetKind::Tooltip:
            {
                // Background plus a centred caption, only when the hover timer
                // has passed its delay (WidgetShown already filtered the widget).
                ScopedRequired += 1;
                const std::uint32_t ScopedCount = CountGlyphs(InCanvas.Text(static_cast<std::int32_t>(ScopedWidget)));
                if (ScopedCount > 0)
                {
                    ScopedHasText = true;
                    const FontAtlas* ScopedAtlas =
                        InContext.fonts != nullptr ? InContext.fonts->At(InCanvas.fontAtlasIndices[ScopedWidget])
                                                   : nullptr;
                    if (ScopedAtlas == nullptr || !ScopedAtlas->IsOpen())
                    {
                        ScopedMissingFontAtlas = true;
                    }
                }
                ScopedGlyphs += ScopedCount;
                ScopedRequired += ScopedCount;
                break;
            }
            case WidgetKind::TextInput:
            {
                ScopedRequired += 1;
                const auto ScopedInputSlot = static_cast<std::uint32_t>(ScopedWidget);
                if (InCanvas.caretOffsets[ScopedInputSlot] != InCanvas.selectionAnchors[ScopedInputSlot])
                {
                    ++ScopedRequired;
                }
                if (InCanvas.focusIndex == static_cast<std::int32_t>(ScopedWidget) && CaretBlinkOn(InCanvas.uiTime))
                {
                    ++ScopedRequired;
                }
                const Text::TextShapeView ScopedShape = ScopedShapeTextInput(ScopedWidget, ScopedRect);
                const std::uint32_t ScopedCount =
                    !ScopedShape.Glyphs.empty()
                        ? static_cast<std::uint32_t>(ScopedShape.Glyphs.size())
                        : CountGlyphs(InCanvas.Text(static_cast<std::int32_t>(ScopedWidget)));
                if (ScopedCount > 0)
                {
                    ScopedHasText = true;
                    const FontAtlas* ScopedAtlas =
                        InContext.fonts != nullptr ? InContext.fonts->At(InCanvas.fontAtlasIndices[ScopedWidget])
                                                   : nullptr;
                    if (ScopedAtlas == nullptr || !ScopedAtlas->IsOpen())
                    {
                        ScopedMissingFontAtlas = true;
                    }
                }
                ScopedGlyphs += ScopedCount;
                ScopedRequired += ScopedCount;
                break;
            }
            case WidgetKind::Spacer:
            case WidgetKind::Switcher:
            case WidgetKind::Count:
                break;
        }
    }
    const bool ScopedFocusRing = InCanvas.drawFocusRing && InCanvas.ValidWidget(InCanvas.focusIndex) &&
                                 WidgetShown(InCanvas, static_cast<std::uint32_t>(InCanvas.focusIndex)) &&
                                 InCanvas.focusRingWidth > 0.0f;
    if (ScopedFocusRing)
    {
        ScopedRequired += 4;
    }
    Output.requiredItems = ScopedRequired;
    Output.widgetsDrawn = ScopedVisibleWidgets;
    Output.glyphQuads = ScopedGlyphs;

    if (ScopedGlyphs > kMaxGlyphs)
    {
        Output.overflow = true;
        return BuildStatus::Overflow;
    }

    // Text is atlas-only. There is no 8x8 fallback in any build: a canvas with
    if (ScopedHasText)
    {
        const bool ScopedFontsUsable =
            InContext.fonts != nullptr && InContext.fonts->Count() > 0 && InContext.fonts->IsCurrent();
        if (!ScopedFontsUsable || ScopedMissingFontAtlas || InCanvas.textureArray != 0u || ScopedForeignImage)
        {
            const char* ScopedReason = "the font atlas set is not mounted";
            if (InContext.fonts == nullptr)
            {
                ScopedReason = "SystemContext::fonts is null (the UI host never created a FontAtlasSet)";
            }
            else if (InContext.fonts->Count() == 0)
            {
                ScopedReason = "the FontAtlasSet is empty (no .fbyfont was loaded from the VFS)";
            }
            else if (!InContext.fonts->IsCurrent())
            {
                ScopedReason = "the atlas set changed after the last mount (call MountFonts() again)";
            }
            else if (ScopedMissingFontAtlas)
            {
                ScopedReason = "a visible label references a missing or closed font atlas";
            }
            else if (InCanvas.textureArray != 0u)
            {
                ScopedReason = "the canvas targets its own texture array while containing text";
            }
            else
            {
                ScopedReason = "the canvas mixes text with a foreign image layer";
            }
            LogError("pinkui",
                     "PANIC: WidgetCanvasSystem::BuildFrame refused a canvas with text: {}. "
                     "MountFonts(RHI&) is a mandatory cold-start stage; the legacy bitmap fallback was removed",
                     ScopedReason);
            FBY_ASSERT_MSG(ScopedFontsUsable && !ScopedMissingFontAtlas && InCanvas.textureArray == 0u &&
                               !ScopedForeignImage,
                           "Pink UI: BuildFrame() with text requires a mounted FontAtlasSet and a font-array canvas");
            Output.fontContractViolated = true;
            return BuildStatus::FontsNotMounted;
        }
    }

    const std::uint32_t ScopedStart = Output.startCursor;
    if (ScopedStart > Output.items.size() || ScopedStart > Output.keys.size())
    {
        Output.overflow = true;
        return BuildStatus::Overflow;
    }
    if (Output.items.size() - ScopedStart < ScopedRequired || Output.keys.size() - ScopedStart < ScopedRequired)
    {
        Output.overflow = true;
        return BuildStatus::Overflow;
    }
    if (ScopedRequired > 0 && Output.batches.empty())
    {
        Output.overflow = true;
        return BuildStatus::Overflow;
    }
    if (ScopedRequired == 0)
    {
        // Visible but empty canvas: no items, no batch
        return BuildStatus::Ok;
    }

    // Cold path responsibility: WidgetCanvasSystem::MountFonts() runs at app
    std::uint32_t ScopedCanvasTextureArray = InCanvas.textureArray;
    bool ScopedUseFontArray = false;
    if (InContext.fonts != nullptr && InContext.fonts->IsCurrent() && InCanvas.textureArray == 0u &&
        !ScopedForeignImage && !ScopedMissingFontAtlas && InContext.fonts->Count() > 0)
    {
        ScopedUseFontArray = true;
        ScopedCanvasTextureArray = InContext.fonts->TextureArray().id;
        Output.fontTextureArray = ScopedCanvasTextureArray;
    }

    // One QuadWriter for the whole canvas: one pipeline, one texture array,
    QuadWriter ScopedWriter{Output.items, ScopedStart};
    std::uint32_t ScopedEmittedGlyphs = 0;
    for (std::uint32_t ScopedOrderIndex = 0; ScopedOrderIndex < InCanvas.widgetCount; ++ScopedOrderIndex)
    {
        const auto ScopedWidget = InCanvas.layout.drawOrder[ScopedOrderIndex];
        const auto ScopedSlot = static_cast<std::uint32_t>(ScopedWidget);
        if (!WidgetShown(InCanvas, ScopedSlot))
        {
            continue;
        }
        const Rect ScopedRect{InCanvas.layout.localMins[ScopedSlot], InCanvas.layout.localMaxs[ScopedSlot]};
        const Vector2D ScopedExtent = ScopedRect.Size();
        if (ScopedExtent.X <= 0.0f || ScopedExtent.Y <= 0.0f)
        {
            continue;
        }

        const float ScopedDepth = WidgetDepth(InCanvas, ScopedSlot);
        std::uint8_t ScopedSequence = 0;
        QuadEmitState ScopedEmit{&ScopedWriter, &InCanvas, ScopedDisplayScale, Output.keys.data(), InCanvas.sortLayer,
                                 ScopedDepth, &ScopedSequence, {}, false};
        ScopedEmit.HasClip = ClipForWidget(InCanvas, ScopedSlot, ScopedEmit.Clip);
        ScopedEmit.Opacity = EffectiveOpacity(InCanvas, ScopedSlot);
        ScopedEmit.Offset = EffectiveVisualOffset(InCanvas, ScopedSlot);

        // Writes one string into the widget rect. Label text is top-left aligned;
        // a Button caption is centred on both axes.
        // Lays out a string inside a rect using the textFlags alignment and
        // wrap bits. Two deterministic passes: count lines, then emit glyphs.
        const auto ScopedEmitText = [&](Rect InRect, std::uint32_t InColor, std::uint16_t InFontSize,
                                        std::uint16_t InAtlasIndex, std::string_view InText,
                                        std::uint16_t InTextFlags) noexcept
        {
            if (InText.empty())
            {
                return;
            }
            const float ScopedGlyphSize =
                std::max(1.0f, std::round(static_cast<float>(InFontSize) * ScopedDisplayScale)) / ScopedDisplayScale;
            const FontAtlas* ScopedAtlas = InContext.fonts != nullptr ? InContext.fonts->At(InAtlasIndex) : nullptr;
            if (!ScopedUseFontArray || ScopedAtlas == nullptr || !ScopedAtlas->IsOpen() ||
                !(ScopedAtlas->LineHeight() > 0.0f))
            {
                return;
            }
            const std::uint32_t ScopedGlyphLayer = InContext.fonts->LayerForFontId(ScopedAtlas->FontId());
            if (ScopedGlyphLayer == 0xFFFFFFFFu)
            {
                return;
            }
            const float ScopedScale = ScopedGlyphSize / ScopedAtlas->LineHeight();
            const float ScopedLineHeight = ScopedGlyphSize * kTextLineHeightRatio;
            const float ScopedWidth = InRect.Size().X;
            const std::uint32_t ScopedAlign = InTextFlags & kTextAlignMask;
            const std::uint32_t ScopedVertical = (InTextFlags & kTextVerticalMask) >> kTextVerticalShift;
            const bool ScopedWrap = (InTextFlags & kTextWrap) != 0;

            const auto ScopedAdvance = [&](std::uint32_t InCodepoint) noexcept
            {
                const GlyphRecord* ScopedGlyph = ScopedAtlas->Glyph(InCodepoint);
                return ScopedGlyph != nullptr ? ScopedGlyph->advance * ScopedScale
                                              : ScopedGlyphSize * kGlyphAdvanceRatio;
            };

            const auto ScopedLineEnd = [&](std::uint16_t InBegin, std::uint16_t& OutNext) noexcept
            {
                if (InBegin >= InText.size())
                {
                    OutNext = static_cast<std::uint16_t>(InText.size());
                    return OutNext;
                }
                if (!ScopedWrap)
                {
                    std::size_t ScopedCursor = InBegin;
                    while (ScopedCursor < InText.size() && InText[ScopedCursor] != '\n')
                    {
                        ++ScopedCursor;
                    }
                    OutNext = ScopedCursor < InText.size() ? static_cast<std::uint16_t>(ScopedCursor + 1)
                                                           : static_cast<std::uint16_t>(InText.size());
                    return static_cast<std::uint16_t>(ScopedCursor);
                }
                float ScopedPen = 0.0f;
                std::size_t ScopedCursor = InBegin;
                std::size_t ScopedLastBreak = InBegin;
                while (ScopedCursor < InText.size())
                {
                    if (InText[ScopedCursor] == '\n')
                    {
                        OutNext = static_cast<std::uint16_t>(ScopedCursor + 1);
                        return static_cast<std::uint16_t>(ScopedCursor);
                    }
                    const std::size_t ScopedStart = ScopedCursor;
                    const std::uint32_t ScopedCodepoint = DecodeUtf8(InText, ScopedCursor);
                    const float ScopedAdvanceValue = ScopedAdvance(ScopedCodepoint);
                    if (ScopedPen + ScopedAdvanceValue > ScopedWidth && ScopedStart > InBegin)
                    {
                        if (ScopedLastBreak > InBegin)
                        {
                            OutNext = static_cast<std::uint16_t>(ScopedLastBreak);
                            while (OutNext < InText.size() && InText[OutNext] == ' ')
                            {
                                ++OutNext;
                            }
                            return static_cast<std::uint16_t>(ScopedLastBreak);
                        }
                        OutNext = static_cast<std::uint16_t>(ScopedStart);
                        return static_cast<std::uint16_t>(ScopedStart);
                    }
                    ScopedPen += ScopedAdvanceValue;
                    if (ScopedCodepoint == ' ')
                    {
                        ScopedLastBreak = ScopedStart;
                    }
                    if (ScopedCursor <= ScopedStart)
                    {
                        ++ScopedCursor;
                    }
                }
                OutNext = static_cast<std::uint16_t>(InText.size());
                return OutNext;
            };

            const auto ScopedMeasure = [&](std::uint16_t InBegin, std::uint16_t InEnd) noexcept
            {
                float ScopedValue = 0.0f;
                std::size_t ScopedCursor = InBegin;
                while (ScopedCursor < InEnd)
                {
                    const std::uint32_t ScopedCodepoint = DecodeUtf8(InText, ScopedCursor);
                    if (ScopedCodepoint == '\n')
                    {
                        break;
                    }
                    ScopedValue += ScopedAdvance(ScopedCodepoint);
                }
                return ScopedValue;
            };

            std::uint32_t ScopedLineCount = 0;
            std::uint16_t ScopedCursor = 0;
            while (ScopedLineCount < 256)
            {
                std::uint16_t ScopedNext = ScopedCursor;
                (void)ScopedLineEnd(ScopedCursor, ScopedNext);
                ++ScopedLineCount;
                if (ScopedNext >= InText.size())
                {
                    break;
                }
                ScopedCursor = ScopedNext;
            }
            float ScopedPenY = InRect.min.Y;
            if (ScopedVertical == 1u)
            {
                ScopedPenY += (InRect.Size().Y - ScopedLineHeight * static_cast<float>(ScopedLineCount)) * 0.5f;
            }
            else if (ScopedVertical == 2u)
            {
                ScopedPenY += InRect.Size().Y - ScopedLineHeight * static_cast<float>(ScopedLineCount);
            }
            ScopedCursor = 0;
            for (std::uint32_t ScopedLine = 0; ScopedLine < ScopedLineCount; ++ScopedLine)
            {
                std::uint16_t ScopedNext = ScopedCursor;
                const std::uint16_t ScopedEnd = ScopedLineEnd(ScopedCursor, ScopedNext);
                const float ScopedLineWidth = ScopedMeasure(ScopedCursor, ScopedEnd);
                float ScopedPenX = InRect.min.X;
                if (ScopedAlign == 1u)
                {
                    ScopedPenX += (InRect.Size().X - ScopedLineWidth) * 0.5f;
                }
                else if (ScopedAlign == 2u)
                {
                    ScopedPenX += InRect.Size().X - ScopedLineWidth;
                }
                std::size_t ScopedGlyphCursor = ScopedCursor;
                while (ScopedGlyphCursor < ScopedEnd)
                {
                    const std::uint32_t ScopedCodepoint = DecodeUtf8(InText, ScopedGlyphCursor);
                    if (ScopedCodepoint == '\n')
                    {
                        break;
                    }
                    const GlyphRecord* ScopedGlyph = ScopedAtlas->Glyph(ScopedCodepoint);
                    if (ScopedGlyph == nullptr || ScopedGlyph->width <= 0.0f || ScopedGlyph->height <= 0.0f)
                    {
                        // Honest miss: the cooked atlas has no record for this codepoint
                        continue;
                    }
                    const Vector2D ScopedGlyphMin{
                        ScopedPenX + ScopedGlyph->bearingX * ScopedScale,
                        ScopedPenY + (ScopedAtlas->Ascent() - ScopedGlyph->bearingY) * ScopedScale};
                    const Rect ScopedGlyphRect{
                        ScopedGlyphMin, Vector2D{ScopedGlyphMin.X + ScopedGlyph->width * ScopedScale,
                                                 ScopedGlyphMin.Y + ScopedGlyph->height * ScopedScale}};
                    if (ScopedEmit(ScopedGlyphRect, ScopedGlyphLayer, InColor,
                                   Vector2D{ScopedGlyph->u0, ScopedGlyph->v0},
                                   Vector2D{ScopedGlyph->u1, ScopedGlyph->v1}))
                    {
                        ++ScopedEmittedGlyphs;
                    }
                    ScopedPenX += ScopedGlyph->advance * ScopedScale;
                }
                ScopedPenY += ScopedLineHeight;
                if (ScopedNext >= InText.size())
                {
                    break;
                }
                ScopedCursor = ScopedNext;
            }
        };

        // A rotated bar. The Checkbox mark is two of them.
        const auto ScopedEmitBar = [&](Vector2D InFrom, Vector2D InTo, float InThickness,
                                       std::uint32_t InColor) noexcept
        {
            const Vector2D ScopedDelta{InTo.X - InFrom.X, InTo.Y - InFrom.Y};
            const float ScopedLength = std::sqrt(ScopedDelta.X * ScopedDelta.X + ScopedDelta.Y * ScopedDelta.Y);
            if (!(ScopedLength > 0.0f))
            {
                return;
            }
            const Vector2D ScopedCenter{(InFrom.X + InTo.X) * 0.5f, (InFrom.Y + InTo.Y) * 0.5f};
            const Rect ScopedBar{Vector2D{ScopedCenter.X - ScopedLength * 0.5f, ScopedCenter.Y - InThickness * 0.5f},
                                 Vector2D{ScopedCenter.X + ScopedLength * 0.5f, ScopedCenter.Y + InThickness * 0.5f}};
            ScopedEmit(ScopedBar, kWhiteLayer, InColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f},
                       std::atan2(ScopedDelta.Y, ScopedDelta.X));
        };

        switch (InCanvas.kinds[ScopedSlot])
        {
            case WidgetKind::Panel:
                ScopedEmit(ScopedRect, kWhiteLayer, SolidColor(InCanvas, ScopedSlot), Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                break;
            case WidgetKind::Button:
            {
                // The face is the tint; the caption uses its own colour so a
                // button can stay readable over any theme surface.
                ScopedEmit(ScopedRect, kWhiteLayer, SolidColor(InCanvas, ScopedSlot), Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                ScopedEmitText(ScopedRect, WidgetTextColor(InCanvas, ScopedSlot).rgba, InCanvas.fontSizes[ScopedSlot],
                               InCanvas.fontAtlasIndices[ScopedSlot],
                               InCanvas.Text(static_cast<std::int32_t>(ScopedWidget)),
                               MakeTextFlags(TextAlign::Centre, TextVerticalAlign::Middle));
                break;
            }
            case WidgetKind::Image:
                ScopedEmit(ScopedRect, InCanvas.layers[ScopedSlot], WidgetTint(InCanvas, ScopedSlot).rgba,
                           InCanvas.uvMins[ScopedSlot], InCanvas.uvMaxs[ScopedSlot]);
                break;
            case WidgetKind::ProgressBar:
            {
                // Track first, fill second. Within one widget the sequence bits
                // keep the fill above the track while the depth stays equal.
                ScopedEmit(ScopedRect, kWhiteLayer, WidgetTint(InCanvas, ScopedSlot).rgba, Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                if (InCanvas.progresses[ScopedSlot] > 0.0f)
                {
                    const std::uint32_t ScopedFillColor = FillColor(InCanvas, ScopedSlot);
                    if (HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Vertical))
                    {
                        const float ScopedFillHeight = ScopedExtent.Y * InCanvas.progresses[ScopedSlot];
                        const Rect ScopedFill{Vector2D{ScopedRect.min.X, ScopedRect.max.Y - ScopedFillHeight},
                                              ScopedRect.max};
                        ScopedEmit(ScopedFill, kWhiteLayer, ScopedFillColor, Vector2D{0.0f, 0.0f},
                                   Vector2D{1.0f, 1.0f});
                    }
                    else
                    {
                        const float ScopedFillWidth = ScopedExtent.X * InCanvas.progresses[ScopedSlot];
                        const Rect ScopedFill{ScopedRect.min,
                                              Vector2D{ScopedRect.min.X + ScopedFillWidth, ScopedRect.max.Y}};
                        ScopedEmit(ScopedFill, kWhiteLayer, ScopedFillColor, Vector2D{0.0f, 0.0f},
                                   Vector2D{1.0f, 1.0f});
                    }
                }
                break;
            }
            case WidgetKind::Border:
            {
                const float ScopedStroke = BorderStroke(InCanvas, ScopedSlot, ScopedExtent);
                if (!(ScopedStroke > 0.0f))
                {
                    break;
                }
                const std::uint32_t ScopedBorderColor = SolidColor(InCanvas, ScopedSlot);
                ScopedEmit(Rect{ScopedRect.min, Vector2D{ScopedRect.max.X, ScopedRect.min.Y + ScopedStroke}},
                           kWhiteLayer, ScopedBorderColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                ScopedEmit(Rect{Vector2D{ScopedRect.min.X, ScopedRect.max.Y - ScopedStroke}, ScopedRect.max},
                           kWhiteLayer, ScopedBorderColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                if (BorderQuadCount(InCanvas, ScopedSlot, ScopedExtent) == 4)
                {
                    ScopedEmit(Rect{Vector2D{ScopedRect.min.X, ScopedRect.min.Y + ScopedStroke},
                                    Vector2D{ScopedRect.min.X + ScopedStroke, ScopedRect.max.Y - ScopedStroke}},
                               kWhiteLayer, ScopedBorderColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                    ScopedEmit(Rect{Vector2D{ScopedRect.max.X - ScopedStroke, ScopedRect.min.Y + ScopedStroke},
                                    Vector2D{ScopedRect.max.X, ScopedRect.max.Y - ScopedStroke}},
                               kWhiteLayer, ScopedBorderColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                }
                break;
            }
            case WidgetKind::Slider:
            {
                const bool ScopedVertical = HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Vertical);
                const float ScopedAxis = ScopedVertical ? ScopedExtent.Y : ScopedExtent.X;
                const float ScopedKnob = SliderKnobLength(InCanvas, ScopedSlot, ScopedExtent, ScopedVertical);
                const float ScopedHandleStart = TrackStart(ScopedAxis, ScopedKnob, InCanvas.progresses[ScopedSlot]);
                const std::uint32_t ScopedFillColor = FillColor(InCanvas, ScopedSlot);
                ScopedEmit(ScopedRect, kWhiteLayer, WidgetTint(InCanvas, ScopedSlot).rgba, Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                Rect ScopedKnobRect{};
                if (ScopedVertical)
                {
                    const float ScopedKnobMax = ScopedRect.max.Y - ScopedHandleStart;
                    ScopedKnobRect = Rect{Vector2D{ScopedRect.min.X, ScopedKnobMax - ScopedKnob},
                                          Vector2D{ScopedRect.max.X, ScopedKnobMax}};
                    const float ScopedFillHeight = ScopedHandleStart + ScopedKnob * 0.5f;
                    if (InCanvas.progresses[ScopedSlot] > 0.0f)
                    {
                        ScopedEmit(Rect{Vector2D{ScopedRect.min.X, ScopedRect.max.Y - ScopedFillHeight},
                                        ScopedRect.max},
                                   kWhiteLayer, ScopedFillColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                    }
                }
                else
                {
                    ScopedKnobRect = Rect{Vector2D{ScopedRect.min.X + ScopedHandleStart, ScopedRect.min.Y},
                                          Vector2D{ScopedRect.min.X + ScopedHandleStart + ScopedKnob, ScopedRect.max.Y}};
                    const float ScopedFillWidth = ScopedHandleStart + ScopedKnob * 0.5f;
                    if (InCanvas.progresses[ScopedSlot] > 0.0f)
                    {
                        ScopedEmit(Rect{ScopedRect.min, Vector2D{ScopedRect.min.X + ScopedFillWidth, ScopedRect.max.Y}},
                                   kWhiteLayer, ScopedFillColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                    }
                }
                ScopedEmit(ScopedKnobRect, kWhiteLayer, ScopedFillColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                break;
            }
            case WidgetKind::Checkbox:
            {
                ScopedEmit(ScopedRect, kWhiteLayer, SolidColor(InCanvas, ScopedSlot), Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                if (HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Checked))
                {
                    const float ScopedSize = std::min(ScopedExtent.X, ScopedExtent.Y);
                    const Vector2D ScopedCenter = ScopedRect.Center();
                    const Vector2D ScopedFirst{ScopedCenter.X - ScopedSize * 0.30f, ScopedCenter.Y - ScopedSize * 0.02f};
                    const Vector2D ScopedCorner{ScopedCenter.X - ScopedSize * 0.06f, ScopedCenter.Y + ScopedSize * 0.24f};
                    const Vector2D ScopedLast{ScopedCenter.X + ScopedSize * 0.34f, ScopedCenter.Y - ScopedSize * 0.30f};
                    const float ScopedThickness = std::max(1.0f, ScopedSize * 0.10f);
                    const std::uint32_t ScopedMarkColor = FillColor(InCanvas, ScopedSlot);
                    ScopedEmitBar(ScopedFirst, ScopedCorner, ScopedThickness, ScopedMarkColor);
                    ScopedEmitBar(ScopedCorner, ScopedLast, ScopedThickness, ScopedMarkColor);
                }
                break;
            }
            case WidgetKind::Scrollbar:
            {
                const bool ScopedVertical = HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Vertical);
                const float ScopedAxis = ScopedVertical ? ScopedExtent.Y : ScopedExtent.X;
                const float ScopedThumb = ScrollThumbLength(InCanvas, ScopedSlot, ScopedAxis);
                const float ScopedThumbStart = TrackStart(ScopedAxis, ScopedThumb, InCanvas.progresses[ScopedSlot]);
                const std::uint32_t ScopedThumbColor = FillColor(InCanvas, ScopedSlot);
                ScopedEmit(ScopedRect, kWhiteLayer, WidgetTint(InCanvas, ScopedSlot).rgba, Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                if (ScopedVertical)
                {
                    const float ScopedThumbMax = ScopedRect.max.Y - ScopedThumbStart;
                    ScopedEmit(Rect{Vector2D{ScopedRect.min.X, ScopedThumbMax - ScopedThumb},
                                    Vector2D{ScopedRect.max.X, ScopedThumbMax}},
                               kWhiteLayer, ScopedThumbColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                }
                else
                {
                    ScopedEmit(Rect{Vector2D{ScopedRect.min.X + ScopedThumbStart, ScopedRect.min.Y},
                                    Vector2D{ScopedRect.min.X + ScopedThumbStart + ScopedThumb, ScopedRect.max.Y}},
                               kWhiteLayer, ScopedThumbColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                }
                break;
            }
            case WidgetKind::NineSlice:
            {
                const Vector4D ScopedInsets = InCanvas.slices[ScopedSlot];
                const float ScopedLeft = std::clamp(ScopedInsets.X, 0.0f, ScopedExtent.X * 0.45f);
                const float ScopedRight = std::clamp(ScopedInsets.Z, 0.0f, ScopedExtent.X * 0.45f);
                const float ScopedTop = std::clamp(ScopedInsets.Y, 0.0f, ScopedExtent.Y * 0.45f);
                const float ScopedBottom = std::clamp(ScopedInsets.W, 0.0f, ScopedExtent.Y * 0.45f);
                const std::array<float, 4> ScopedXs{ScopedRect.min.X, ScopedRect.min.X + ScopedLeft,
                                                    ScopedRect.max.X - ScopedRight, ScopedRect.max.X};
                const std::array<float, 4> ScopedYs{ScopedRect.min.Y, ScopedRect.min.Y + ScopedTop,
                                                    ScopedRect.max.Y - ScopedBottom, ScopedRect.max.Y};
                const std::array<float, 4> ScopedUs{0.0f, ScopedLeft / ScopedExtent.X,
                                                    1.0f - ScopedRight / ScopedExtent.X, 1.0f};
                const std::array<float, 4> ScopedVs{0.0f, ScopedTop / ScopedExtent.Y,
                                                    1.0f - ScopedBottom / ScopedExtent.Y, 1.0f};
                const Color32 ScopedTint = WidgetTint(InCanvas, ScopedSlot);
                for (std::uint32_t ScopedRow = 0; ScopedRow < 3; ++ScopedRow)
                {
                    for (std::uint32_t ScopedColumn = 0; ScopedColumn < 3; ++ScopedColumn)
                    {
                        const Rect ScopedQuad{Vector2D{ScopedXs[ScopedColumn], ScopedYs[ScopedRow]},
                                              Vector2D{ScopedXs[ScopedColumn + 1], ScopedYs[ScopedRow + 1]}};
                        const Vector2D ScopedUvMin{
                            InCanvas.uvMins[ScopedSlot].X + (InCanvas.uvMaxs[ScopedSlot].X - InCanvas.uvMins[ScopedSlot].X) *
                                                                ScopedUs[ScopedColumn],
                            InCanvas.uvMins[ScopedSlot].Y + (InCanvas.uvMaxs[ScopedSlot].Y - InCanvas.uvMins[ScopedSlot].Y) *
                                                                ScopedVs[ScopedRow]};
                        const Vector2D ScopedUvMax{
                            InCanvas.uvMins[ScopedSlot].X + (InCanvas.uvMaxs[ScopedSlot].X - InCanvas.uvMins[ScopedSlot].X) *
                                                                ScopedUs[ScopedColumn + 1],
                            InCanvas.uvMins[ScopedSlot].Y + (InCanvas.uvMaxs[ScopedSlot].Y - InCanvas.uvMins[ScopedSlot].Y) *
                                                                ScopedVs[ScopedRow + 1]};
                        ScopedEmit(ScopedQuad, InCanvas.layers[ScopedSlot], ScopedTint.rgba, ScopedUvMin, ScopedUvMax);
                    }
                }
                break;
            }
            case WidgetKind::ToggleSwitch:
            {
                const float ScopedPadding = std::min(2.0f, std::min(ScopedExtent.X, ScopedExtent.Y) * 0.1f);
                const float ScopedKnobSize = std::max(1.0f, ScopedExtent.Y - ScopedPadding * 2.0f);
                const float ScopedTravel = std::max(0.0f, ScopedExtent.X - ScopedKnobSize - ScopedPadding * 2.0f);
                const float ScopedKnobX =
                    ScopedRect.min.X + ScopedPadding +
                    (HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Checked) ? ScopedTravel : 0.0f);
                ScopedEmit(ScopedRect, kWhiteLayer, SolidColor(InCanvas, ScopedSlot), Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                ScopedEmit(Rect{Vector2D{ScopedKnobX, ScopedRect.min.Y + ScopedPadding},
                                Vector2D{ScopedKnobX + ScopedKnobSize, ScopedRect.max.Y - ScopedPadding}},
                           kWhiteLayer, FillColor(InCanvas, ScopedSlot), Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                break;
            }
            case WidgetKind::RadioButton:
            {
                ScopedEmit(ScopedRect, kWhiteLayer, SolidColor(InCanvas, ScopedSlot), Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                if (HasFlag(InCanvas.flags[ScopedSlot], WidgetFlags::Checked))
                {
                    const Vector2D ScopedInset{ScopedExtent.X * 0.25f, ScopedExtent.Y * 0.25f};
                    ScopedEmit(Rect{Vector2D{ScopedRect.min.X + ScopedInset.X, ScopedRect.min.Y + ScopedInset.Y},
                                    Vector2D{ScopedRect.max.X - ScopedInset.X, ScopedRect.max.Y - ScopedInset.Y}},
                               kWhiteLayer, FillColor(InCanvas, ScopedSlot), Vector2D{0.0f, 0.0f},
                               Vector2D{1.0f, 1.0f});
                }
                break;
            }
            case WidgetKind::Label:
            case WidgetKind::Hypertext:
                ScopedEmitText(ScopedRect, WidgetTint(InCanvas, ScopedSlot).rgba, InCanvas.fontSizes[ScopedSlot],
                               InCanvas.fontAtlasIndices[ScopedSlot],
                               InCanvas.Text(static_cast<std::int32_t>(ScopedWidget)),
                               InCanvas.textFlags[ScopedSlot]);
                break;
            case WidgetKind::Overlay:
                ScopedEmit(ScopedRect, kWhiteLayer, WidgetTint(InCanvas, ScopedSlot).rgba, Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                break;
            case WidgetKind::Custom:
            {
                const CustomWidgetPaint ScopedPaintFn = InCanvas.customPaints[ScopedSlot];
                if (ScopedPaintFn == nullptr)
                {
                    break;
                }
                CustomEmitState ScopedCustomState{&ScopedEmit, ScopedCustomQuotas[ScopedSlot], 0};
                WidgetPaintContext ScopedPaint{InCanvas,
                                               static_cast<std::int32_t>(ScopedWidget),
                                               ScopedRect,
                                               InCanvas.customParams[ScopedSlot],
                                               InCanvas.customUsers[ScopedSlot],
                                               WidgetTint(InCanvas, ScopedSlot),
                                               WidgetFill(InCanvas, ScopedSlot),
                                               InCanvas.layers[ScopedSlot],
                                               &EmitCustomQuad,
                                               &ScopedCustomState};
                ScopedPaintFn(ScopedPaint);
                break;
            }
            case WidgetKind::TextBlock:
            {
                const std::uint32_t ScopedQuota = ScopedTextBlockCounts[ScopedSlot];
                if (ScopedQuota == 0)
                {
                    break;
                }
                const std::uint32_t ScopedWritten = ScopedRenderTextBlock(
                    ScopedWidget, ScopedRect,
                    std::span<Rhi::RenderItem>(ScopedTextBlockItems).first(ScopedQuota));
                const std::uint32_t ScopedCount = std::min(ScopedWritten, ScopedQuota);
                const float ScopedOffsetX = ScopedEmit.Offset.X;
                const float ScopedOffsetY = ScopedEmit.Offset.Y;
                for (std::uint32_t ScopedItemIndex = 0; ScopedItemIndex < ScopedCount; ++ScopedItemIndex)
                {
                    const Rhi::RenderItem& ScopedSource = ScopedTextBlockItems[ScopedItemIndex];
                    Rect ScopedLocal = Rect::FromCenter(
                        Vector2D{ScopedSource.position.X + ScopedOffsetX, ScopedSource.position.Y + ScopedOffsetY},
                        ScopedSource.size);
                    Vector2D ScopedUvMin = ScopedSource.uvMin;
                    Vector2D ScopedUvMax = ScopedSource.uvMax;
                    if (ScopedEmit.HasClip)
                    {
                        Rect ScopedTrimmed{};
                        if (!TrimToClip(ScopedLocal, ScopedUvMin, ScopedUvMax, ScopedEmit.Clip, ScopedTrimmed,
                                        ScopedUvMin, ScopedUvMax))
                        {
                            continue;
                        }
                        ScopedLocal = ScopedTrimmed;
                    }
                    Rhi::RenderItem ScopedItem = ScopedSource;
                    ScopedItem.position = LocalToWorld(InCanvas, ScopedLocal.Center(), ScopedDisplayScale);
                    ScopedItem.size = Vector2D{ScopedLocal.Size().X * InCanvas.worldScale.X * ScopedDisplayScale,
                                               ScopedLocal.Size().Y * InCanvas.worldScale.Y * ScopedDisplayScale};
                    ScopedItem.uvMin = ScopedUvMin;
                    ScopedItem.uvMax = ScopedUvMax;
                    ScopedItem.rotation = ScopedSource.rotation + InCanvas.worldRotation;
                    if (ScopedEmit.Opacity < 0.999f)
                    {
                        const std::uint32_t ScopedAlpha = (ScopedItem.color >> 24) & 0xFFu;
                        const auto ScopedScaled =
                            static_cast<std::uint32_t>(static_cast<float>(ScopedAlpha) * ScopedEmit.Opacity + 0.5f);
                        ScopedItem.color = (ScopedItem.color & 0x00FFFFFFu) | (ScopedScaled << 24);
                    }
                    const std::uint32_t ScopedSlotIndex = ScopedWriter.cursor++;
                    Output.items[ScopedSlotIndex] = ScopedItem;
                    Output.keys[ScopedSlotIndex] = Rhi::MakeRenderKey(
                        InCanvas.sortLayer,
                        ScopedDepth + static_cast<float>(ScopedItemIndex >> 8) * (1.0f / 256.0f),
                        static_cast<std::uint8_t>(ScopedItemIndex & 0xFFu));
                    ++ScopedEmittedGlyphs;
                }
                break;
            }
            case WidgetKind::TextInput:
            {
                const auto ScopedInputSlot = static_cast<std::uint32_t>(ScopedWidget);
                // The field is its own viewport: glyphs, selection and caret
                // never leave the rect, and an outer ClipChildren still applies.
                if (ScopedEmit.HasClip)
                {
                    ScopedEmit.Clip.min.X = std::max(ScopedEmit.Clip.min.X, ScopedRect.min.X);
                    ScopedEmit.Clip.min.Y = std::max(ScopedEmit.Clip.min.Y, ScopedRect.min.Y);
                    ScopedEmit.Clip.max.X = std::min(ScopedEmit.Clip.max.X, ScopedRect.max.X);
                    ScopedEmit.Clip.max.Y = std::min(ScopedEmit.Clip.max.Y, ScopedRect.max.Y);
                }
                else
                {
                    ScopedEmit.Clip = ScopedRect;
                    ScopedEmit.HasClip = true;
                }
                ScopedEmit(ScopedRect, kWhiteLayer, SolidColor(InCanvas, ScopedSlot), Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                const std::string_view ScopedText = InCanvas.Text(static_cast<std::int32_t>(ScopedWidget));
                const std::uint16_t ScopedCaret = InCanvas.caretOffsets[ScopedInputSlot];
                const std::uint16_t ScopedAnchor = InCanvas.selectionAnchors[ScopedInputSlot];
                const float ScopedGlyphSize =
                    std::max(1.0f, std::round(static_cast<float>(InCanvas.fontSizes[ScopedInputSlot]) *
                                              ScopedDisplayScale)) /
                    ScopedDisplayScale;
                const float ScopedLineHeight = ScopedGlyphSize * kTextLineHeightRatio;
                const FontAtlas* ScopedAtlas =
                    InContext.fonts != nullptr ? InContext.fonts->At(InCanvas.fontAtlasIndices[ScopedInputSlot])
                                               : nullptr;
                float ScopedGlyphScale = 1.0f;
                if (ScopedAtlas != nullptr && ScopedAtlas->IsOpen() && ScopedAtlas->LineHeight() > 0.0f)
                {
                    ScopedGlyphScale = ScopedGlyphSize / ScopedAtlas->LineHeight();
                }
                const Text::TextShapeView ScopedShape = ScopedShapeTextInput(ScopedWidget, ScopedRect);
                if (!ScopedShape.Glyphs.empty() && ScopedAtlas != nullptr)
                {
                    const bool ScopedRtl = HasFlag(InCanvas.flags[ScopedInputSlot], WidgetFlags::TextRightToLeft);
                    float ScopedTotalAdvance = 0.0f;
                    for (const Text::ShapedGlyph& ScopedShapedGlyph : ScopedShape.Glyphs)
                    {
                        ScopedTotalAdvance += ScopedShapedGlyph.AdvanceX;
                    }
                    const float ScopedOriginX =
                        ScopedRtl ? ScopedRect.max.X - ScopedTotalAdvance : ScopedRect.min.X;
                    const auto ScopedShapedX = [&](std::uint16_t InOffset) noexcept
                    {
                        float ScopedPen = 0.0f;
                        const std::size_t ScopedGlyphCount = ScopedShape.Glyphs.size();
                        for (std::size_t ScopedGlyphIndex = 0; ScopedGlyphIndex < ScopedGlyphCount; ++ScopedGlyphIndex)
                        {
                            const Text::ShapedGlyph& ScopedShapedGlyph = ScopedShape.Glyphs[ScopedGlyphIndex];
                            if (ScopedShapedGlyph.Cluster == InOffset)
                            {
                                // The run direction is the direction of the cluster
                                // order around this glyph: descending is RTL.
                                bool ScopedGlyphRtl = ScopedRtl;
                                if (ScopedGlyphIndex + 1 < ScopedGlyphCount)
                                {
                                    ScopedGlyphRtl =
                                        ScopedShape.Glyphs[ScopedGlyphIndex + 1].Cluster < ScopedShapedGlyph.Cluster;
                                }
                                else if (ScopedGlyphIndex > 0)
                                {
                                    ScopedGlyphRtl =
                                        ScopedShapedGlyph.Cluster < ScopedShape.Glyphs[ScopedGlyphIndex - 1].Cluster;
                                }
                                return ScopedGlyphRtl ? ScopedPen + ScopedShapedGlyph.AdvanceX : ScopedPen;
                            }
                            ScopedPen += ScopedShapedGlyph.AdvanceX;
                        }
                        return ScopedRtl ? 0.0f : ScopedTotalAdvance;
                    };
                    if (ScopedCaret != ScopedAnchor)
                    {
                        const float ScopedFrom = ScopedOriginX + ScopedShapedX(std::min(ScopedCaret, ScopedAnchor));
                        const float ScopedTo = ScopedOriginX + ScopedShapedX(std::max(ScopedCaret, ScopedAnchor));
                        const std::uint32_t ScopedSelection = WithAlpha(WidgetFill(InCanvas, ScopedSlot), 90u).rgba;
                        ScopedEmit(Rect{Vector2D{std::min(ScopedFrom, ScopedTo), ScopedRect.min.Y},
                                        Vector2D{std::max(ScopedFrom, ScopedTo), ScopedRect.min.Y + ScopedLineHeight}},
                                   kWhiteLayer, ScopedSelection, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                    }
                    const std::uint32_t ScopedTextColor = WidgetTextColor(InCanvas, ScopedSlot).rgba;
                    float ScopedPenX = ScopedOriginX;
                    for (const Text::ShapedGlyph& ScopedShapedGlyph : ScopedShape.Glyphs)
                    {
                        const FontAtlas* ScopedGlyphAtlas = ScopedAtlas;
                        const GlyphRecord* ScopedRecord = Text::Detail::ResolveShapedGlyph(
                            *InContext.fonts, ScopedAtlas, ScopedShapedGlyph, &ScopedGlyphAtlas);
                        if (ScopedRecord == nullptr || ScopedGlyphAtlas == nullptr || ScopedRecord->width <= 0.0f ||
                            ScopedRecord->height <= 0.0f)
                        {
                            ScopedPenX += ScopedShapedGlyph.AdvanceX;
                            continue;
                        }
                        const float ScopedRecordScale =
                            ScopedGlyphAtlas->LineHeight() > 0.0f
                                ? ScopedGlyphSize / ScopedGlyphAtlas->LineHeight()
                                : 1.0f;
                        const Vector2D ScopedCenter{
                            ScopedPenX + ScopedShapedGlyph.OffsetX +
                                (ScopedRecord->bearingX + ScopedRecord->width * 0.5f) * ScopedRecordScale,
                            ScopedRect.min.Y + ScopedShapedGlyph.OffsetY +
                                (ScopedGlyphAtlas->Ascent() - ScopedRecord->bearingY +
                                 ScopedRecord->height * 0.5f) *
                                    ScopedRecordScale};
                        ScopedEmit(Rect::FromCenter(
                                       ScopedCenter, Vector2D{ScopedRecord->width * ScopedRecordScale,
                                                              ScopedRecord->height * ScopedRecordScale}),
                                   InContext.fonts->LayerForAtlas(ScopedGlyphAtlas), ScopedTextColor,
                                   Vector2D{ScopedRecord->u0, ScopedRecord->v0},
                                   Vector2D{ScopedRecord->u1, ScopedRecord->v1});
                        ++ScopedEmittedGlyphs;
                        ScopedPenX += ScopedShapedGlyph.AdvanceX;
                    }
                    if (InCanvas.focusIndex == static_cast<std::int32_t>(ScopedWidget) &&
                        CaretBlinkOn(InCanvas.uiTime))
                    {
                        const float ScopedCaretX = ScopedOriginX + ScopedShapedX(ScopedCaret);
                        ScopedEmit(Rect{Vector2D{ScopedCaretX - 0.75f, ScopedRect.min.Y},
                                        Vector2D{ScopedCaretX + 0.75f, ScopedRect.min.Y + ScopedLineHeight}},
                                   kWhiteLayer, WidgetFill(InCanvas, ScopedSlot).rgba, Vector2D{0.0f, 0.0f},
                                   Vector2D{1.0f, 1.0f});
                    }
                    break;
                }
                const auto ScopedMeasureTo = [&](std::uint16_t InOffset) noexcept
                {
                    float ScopedPenX = 0.0f;
                    std::size_t ScopedCursor = 0;
                    while (ScopedCursor < ScopedText.size() && ScopedCursor < InOffset)
                    {
                        const std::uint32_t ScopedCodepoint = DecodeUtf8(ScopedText, ScopedCursor);
                        float ScopedAdvance = ScopedGlyphSize * kGlyphAdvanceRatio;
                        if (ScopedAtlas != nullptr && ScopedAtlas->IsOpen())
                        {
                            const GlyphRecord* ScopedGlyph = ScopedAtlas->Glyph(ScopedCodepoint);
                            if (ScopedGlyph != nullptr)
                            {
                                ScopedAdvance = ScopedGlyph->advance * ScopedGlyphScale;
                            }
                        }
                        ScopedPenX += ScopedAdvance;
                    }
                    return ScopedPenX;
                };
                if (ScopedCaret != ScopedAnchor)
                {
                    const float ScopedFrom = ScopedMeasureTo(std::min(ScopedCaret, ScopedAnchor));
                    const float ScopedTo = ScopedMeasureTo(std::max(ScopedCaret, ScopedAnchor));
                    const std::uint32_t ScopedSelection = WithAlpha(WidgetFill(InCanvas, ScopedSlot), 90u).rgba;
                    ScopedEmit(Rect{Vector2D{ScopedRect.min.X + ScopedFrom, ScopedRect.min.Y},
                                    Vector2D{ScopedRect.min.X + ScopedTo, ScopedRect.min.Y + ScopedLineHeight}},
                               kWhiteLayer, ScopedSelection, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
                }
                ScopedEmitText(ScopedRect, WidgetTextColor(InCanvas, ScopedSlot).rgba,
                               InCanvas.fontSizes[ScopedInputSlot], InCanvas.fontAtlasIndices[ScopedInputSlot],
                               ScopedText, 0);
                if (InCanvas.focusIndex == static_cast<std::int32_t>(ScopedWidget) && CaretBlinkOn(InCanvas.uiTime))
                {
                    const float ScopedCaretX = ScopedRect.min.X + ScopedMeasureTo(ScopedCaret);
                    ScopedEmit(Rect{Vector2D{ScopedCaretX - 0.75f, ScopedRect.min.Y},
                                    Vector2D{ScopedCaretX + 0.75f, ScopedRect.min.Y + ScopedLineHeight}},
                               kWhiteLayer, WidgetFill(InCanvas, ScopedSlot).rgba, Vector2D{0.0f, 0.0f},
                               Vector2D{1.0f, 1.0f});
                }
                break;
            }
            case WidgetKind::Tooltip:
                ScopedEmit(ScopedRect, kWhiteLayer, WidgetTint(InCanvas, ScopedSlot).rgba, Vector2D{0.0f, 0.0f},
                           Vector2D{1.0f, 1.0f});
                ScopedEmitText(ScopedRect, WidgetTextColor(InCanvas, ScopedSlot).rgba, InCanvas.fontSizes[ScopedSlot],
                               InCanvas.fontAtlasIndices[ScopedSlot],
                               InCanvas.Text(static_cast<std::int32_t>(ScopedWidget)),
                               MakeTextFlags(TextAlign::Centre, TextVerticalAlign::Middle));
                break;
            case WidgetKind::Spacer:
            case WidgetKind::Switcher:
            case WidgetKind::Count:
                break;
        }
    }

    if (ScopedFocusRing)
    {
        const auto ScopedRingSlot = static_cast<std::uint32_t>(InCanvas.focusIndex);
        const Rect ScopedRingRect{InCanvas.layout.localMins[ScopedRingSlot],
                                  InCanvas.layout.localMaxs[ScopedRingSlot]};
        const float ScopedRingWidth =
            std::min(InCanvas.focusRingWidth, std::min(ScopedRingRect.Size().X, ScopedRingRect.Size().Y) * 0.45f);
        if (ScopedRingWidth > 0.0f)
        {
            std::uint8_t ScopedRingSequence = 0;
            QuadEmitState ScopedRingEmit{&ScopedWriter, &InCanvas, ScopedDisplayScale, Output.keys.data(),
                                         InCanvas.sortLayer, 32767.0f, &ScopedRingSequence, {}, false};
            ScopedRingEmit.HasClip = ClipForWidget(InCanvas, ScopedRingSlot, ScopedRingEmit.Clip);
            ScopedRingEmit.Opacity = EffectiveOpacity(InCanvas, ScopedRingSlot);
            ScopedRingEmit.Offset = EffectiveVisualOffset(InCanvas, ScopedRingSlot);
            const std::uint32_t ScopedRingColor = InCanvas.theme.Resolve(UiPaletteSlot::Focus, kPinkAccent).rgba;
            const Rect ScopedOuter{
                Vector2D{ScopedRingRect.min.X - ScopedRingWidth, ScopedRingRect.min.Y - ScopedRingWidth},
                Vector2D{ScopedRingRect.max.X + ScopedRingWidth, ScopedRingRect.max.Y + ScopedRingWidth}};
            ScopedRingEmit(Rect{ScopedOuter.min, Vector2D{ScopedOuter.max.X, ScopedRingRect.min.Y}}, kWhiteLayer,
                           ScopedRingColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
            ScopedRingEmit(Rect{Vector2D{ScopedOuter.min.X, ScopedRingRect.max.Y}, ScopedOuter.max}, kWhiteLayer,
                           ScopedRingColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
            ScopedRingEmit(Rect{Vector2D{ScopedOuter.min.X, ScopedRingRect.min.Y},
                                Vector2D{ScopedRingRect.min.X, ScopedRingRect.max.Y}},
                           kWhiteLayer, ScopedRingColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
            ScopedRingEmit(Rect{Vector2D{ScopedRingRect.max.X, ScopedRingRect.min.Y},
                                Vector2D{ScopedOuter.max.X, ScopedRingRect.max.Y}},
                           kWhiteLayer, ScopedRingColor, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f});
        }
    }

    Output.itemsWritten = ScopedWriter.cursor - ScopedStart;
    Output.glyphQuads = ScopedEmittedGlyphs;
    Output.batchesWritten = 1;
    // Four fields, all 32-bit: firstItem, itemCount, pipeline, textureArray.
    Output.batches[0] = Rhi::DrawBatch{ScopedStart, Output.itemsWritten, InCanvas.pipeline, ScopedCanvasTextureArray};
    return BuildStatus::Ok;
}

void WidgetCanvasSystem::Update(Fby::Ecs::SystemContext& InContext, std::span<WidgetCanvas> InCanvases,
                                FrameOutput& Output) noexcept
{
    Output.itemsWritten = 0;
    Output.batchesWritten = 0;
    Output.widgetsDrawn = 0;
    Output.glyphQuads = 0;
    Output.requiredItems = 0;
    Output.fontTextureArray = 0xFFFFFFFFu;
    Output.fontContractViolated = false;
    Output.overflow = false;

    if (InCanvases.empty())
    {
        return;
    }

    // Scene placement is data, not a system service: read the entity transform,
    std::uint32_t ScopedItemCursor = Output.startCursor;
    std::uint32_t ScopedBatchCursor = 0;
    auto ScopedView = InContext.registry.view<WidgetComponent, Transform2D>();
    for (auto [ScopedEntity, ScopedComponent, ScopedTransform] : ScopedView.each())
    {
        (void)ScopedEntity;
        if ((ScopedComponent.Flags & static_cast<std::uint32_t>(WidgetComponentFlags::Visible)) == 0)
        {
            continue;
        }
        if (ScopedComponent.CanvasID >= InCanvases.size())
        {
            // A dangling component must not read past the pool
            continue;
        }
        WidgetCanvas& ScopedCanvas = InCanvases[ScopedComponent.CanvasID];
        ScopedCanvas.AdvanceOledShift(InContext.dt);
        ScopedCanvas.AdvanceHover(InContext.dt);
        ScopedCanvas.AdvanceTime(InContext.dt);
        ScopedCanvas.AdvanceTransitions(InContext.dt);
        float ScopedDisplayScale = ScopedCanvas.displayScale;
        if (InContext.cameraStack != nullptr)
        {
            ScopedDisplayScale = InContext.cameraStack->DisplayScale();
        }
        ScopedCanvas.SetDisplayScale(ScopedDisplayScale);
        if (ScopedCanvas.followDisplayScale && InContext.window != nullptr)
        {
            const WindowExtent ScopedExtent = InContext.window->Size();
            if (ScopedExtent.width > 0 && ScopedExtent.height > 0)
            {
                const Vector2D ScopedDesired{static_cast<float>(ScopedExtent.width) / ScopedDisplayScale,
                                             static_cast<float>(ScopedExtent.height) / ScopedDisplayScale};
                if (!(ScopedCanvas.size == ScopedDesired))
                {
                    ScopedCanvas.SetPhysicalCanvasSize(
                        Vector2D{static_cast<float>(ScopedExtent.width), static_cast<float>(ScopedExtent.height)},
                        ScopedDisplayScale);
                }
            }
        }
        ScopedCanvas.SetWorldTransform(ScopedTransform.Position, ScopedTransform.Scale, ScopedTransform.Rotation);
        ScopedCanvas.SetSortLayer(static_cast<std::uint16_t>(ScopedComponent.Layer));

        FrameOutput ScopedSingle = Output;
        ScopedSingle.startCursor = ScopedItemCursor;
        if (ScopedBatchCursor < Output.batches.size())
        {
            ScopedSingle.batches = Output.batches.subspan(ScopedBatchCursor);
        }
        else
        {
            ScopedSingle.batches = {};
        }
        const BuildStatus ScopedStatus = BuildFrame(InContext, ScopedCanvas, ScopedSingle);
        if (ScopedStatus == BuildStatus::FontsNotMounted)
        {
            // The PANIC line has already been emitted, and every text canvas
            Output.fontContractViolated = true;
            break;
        }
        if (ScopedStatus != BuildStatus::Ok)
        {
            Output.overflow = true;
            continue;
        }
        ScopedItemCursor += ScopedSingle.itemsWritten;
        ScopedBatchCursor += ScopedSingle.batchesWritten;
        Output.itemsWritten += ScopedSingle.itemsWritten;
        Output.batchesWritten += ScopedSingle.batchesWritten;
        Output.widgetsDrawn += ScopedSingle.widgetsDrawn;
        Output.glyphQuads += ScopedSingle.glyphQuads;
        Output.requiredItems += ScopedSingle.requiredItems;
    }
    Output.startCursor = ScopedItemCursor;
}
}
