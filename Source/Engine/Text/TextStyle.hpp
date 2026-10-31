// FemboyEngine2D - rich text style spans.
#pragma once

#include "Engine/Core/Types.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby::Text
{

enum class TextStyleFlags : std::uint16_t
{
    None = 0,
    Bold = 1u << 0,
    Italic = 1u << 1,
    Underline = 1u << 2,
    Strike = 1u << 3,
    Monospace = 1u << 4,
};

[[nodiscard]] constexpr std::uint16_t operator|(TextStyleFlags InLeft, TextStyleFlags InRight) noexcept
{
    return static_cast<std::uint16_t>(InLeft) | static_cast<std::uint16_t>(InRight);
}

struct TextStyle
{
    // Zero means "use the renderer's default size".
    Color32 Color = Color32::White();
    std::uint16_t FontSize = 0;
    std::uint16_t Flags = 0;
};

static_assert(sizeof(TextStyle) == 8);
static_assert(std::is_trivially_copyable_v<TextStyle>);

// Byte range [Begin, End) in a block. StyleIndex addresses the span's style
// table; Flags is reserved for span-local overrides.
struct TextSpan
{
    std::uint32_t Begin = 0;
    std::uint32_t End = 0;
    std::uint16_t StyleIndex = 0;
    std::uint16_t Flags = 0;
};

static_assert(sizeof(TextSpan) == 12);
static_assert(std::is_trivially_copyable_v<TextSpan>);

enum class TextActionKind : std::uint16_t
{
    None = 0,
    Link,
    Command,
    Tooltip,
};

// A clickable or script-driven byte range. ActionId is interpreted by the caller
// (link target, command id, tooltip id), so the text layer stays data-driven.
struct TextAction
{
    std::uint32_t Begin = 0;
    std::uint32_t End = 0;
    std::uint32_t ActionId = 0;
    TextActionKind Kind = TextActionKind::None;
    std::uint16_t Flags = 0;
};

static_assert(sizeof(TextAction) == 16);
static_assert(std::is_trivially_copyable_v<TextAction>);

enum class TextInlineKind : std::uint32_t
{
    Sprite = 0,
    Border,
    Progress,
    Separator,
    Button,
};

// An inline atom occupies U+FFFC in the text and reserves Width x Height pixels.
struct TextInlineObject
{
    std::uint32_t ByteOffset = 0;
    float Width = 0.0f;
    float Height = 0.0f;
    std::uint32_t Id = 0;
    TextInlineKind Kind = TextInlineKind::Sprite;
    // Progress uses 0..1; other kinds ignore it.
    float Progress = 0.0f;
    std::uint32_t Flags = 0;
};

static_assert(sizeof(TextInlineObject) == 28);
static_assert(std::is_trivially_copyable_v<TextInlineObject>);

// Runtime mapping from an inline object id to a sprite-array slice and UV rect.
struct TextInlineSprite
{
    std::uint32_t Id = 0;
    std::uint32_t Layer = 0;
    float U0 = 0.0f;
    float V0 = 0.0f;
    float U1 = 1.0f;
    float V1 = 1.0f;
};

static_assert(sizeof(TextInlineSprite) == 24);
static_assert(std::is_trivially_copyable_v<TextInlineSprite>);
}
