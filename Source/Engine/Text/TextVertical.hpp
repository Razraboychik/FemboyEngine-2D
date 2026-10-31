// FemboyEngine2D - vertical CJK punctuation forms.
//
// A font that implements the OpenType `vert` feature substitutes a horizontal
// punctuation glyph for its vertical presentation form. Shaping already asks for
// `vert`/`vrt2`, but a glyph subset atlas must ship the substituted glyphs too,
// so this table maps the punctuation codepoint to the Unicode presentation form
// that the font is expected to reuse for its vertical glyph.
#pragma once

#include <array>
#include <cstdint>

namespace Fby::Text
{

struct VerticalFormEntry
{
    std::uint32_t Horizontal;
    std::uint32_t Vertical;
};

inline constexpr std::array<VerticalFormEntry, 22> kVerticalFormTable{{
    {0x2013u, 0xFE32u},
    {0x2014u, 0xFE31u},
    {0x3001u, 0xFE11u},
    {0x3002u, 0xFE12u},
    {0x3008u, 0xFE3Fu},
    {0x3009u, 0xFE40u},
    {0x300Au, 0xFE3Du},
    {0x300Bu, 0xFE3Eu},
    {0x300Cu, 0xFE41u},
    {0x300Du, 0xFE42u},
    {0x300Eu, 0xFE43u},
    {0x300Fu, 0xFE44u},
    {0x3010u, 0xFE3Bu},
    {0x3011u, 0xFE3Cu},
    {0x3014u, 0xFE39u},
    {0x3015u, 0xFE3Au},
    {0xFF08u, 0xFE35u},
    {0xFF09u, 0xFE36u},
    {0xFF3Bu, 0xFE47u},
    {0xFF3Du, 0xFE48u},
    {0xFF5Bu, 0xFE37u},
    {0xFF5Du, 0xFE38u},
}};

// Presentation form of a horizontal CJK punctuation codepoint, or 0 when the
// codepoint has no vertical form.
[[nodiscard]] constexpr std::uint32_t VerticalFormForCodepoint(std::uint32_t InCodepoint) noexcept
{
    for (const VerticalFormEntry& ScopedEntry : kVerticalFormTable)
    {
        if (ScopedEntry.Horizontal == InCodepoint)
        {
            return ScopedEntry.Vertical;
        }
    }
    return 0;
}

[[nodiscard]] constexpr bool IsVerticalPunctuation(std::uint32_t InCodepoint) noexcept
{
    return VerticalFormForCodepoint(InCodepoint) != 0;
}
}
