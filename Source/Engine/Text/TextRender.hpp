// FemboyEngine2D - glyph extraction for the 2D text pipeline.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/Text/TextLayout.hpp"
#include "Engine/Text/TextMarkup.hpp"
#include "Engine/Text/TextService.hpp"
#include "Engine/Text/TextShapedLayout.hpp"
#include "Engine/Text/TextShaper.hpp"
#include "Engine/Text/TextStyle.hpp"
#include "Engine/UI/FontAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace Fby::Text
{

struct TextGradientParams
{
    bool Enabled = false;
    Color32 Start = Color32::White();
    Color32 End = Color32::White();
};

struct TextAnimationParams
{
    bool Typewriter = false;
    std::uint32_t VisibleGlyphs = 0;
    bool Wave = false;
    float WaveAmplitude = 2.0f;
    float WaveFrequency = 6.0f;
    bool Shake = false;
    float ShakeAmplitude = 1.0f;
    std::uint32_t ShakeSeed = 1;
    bool Rainbow = false;
    float RainbowSpeed = 1.0f;
    float Time = 0.0f;
};

struct TextRenderEffects
{
    bool Shadow = false;
    Vector2D ShadowOffset{1.0f, 1.0f};
    Color32 ShadowColor = Color32::FromBytes(0, 0, 0, 160);
    bool Outline = false;
    float OutlineWidth = 1.0f;
    Color32 OutlineColor = Color32::FromBytes(0, 0, 0, 255);
    bool Glow = false;
    float GlowRadius = 3.0f;
    Color32 GlowColor = Color32::FromBytes(255, 120, 220, 140);
};

struct TextRenderParams
{
    const Ui::FontAtlasSet* Fonts = nullptr;
    std::uint32_t FontAtlasIndex = 0;
    float FontSize = 16.0f;
    Color32 Color = Color32::White();
    std::uint32_t Flags = 0;
    // Optional rich-text spans. An empty span table falls back to Color.
    std::span<const TextSpan> Spans{};
    std::span<const TextStyle> Styles{};
    std::span<const TextInlineObject> Inlines{};
    std::span<const TextInlineSprite> InlineSprites{};
    TextRenderEffects Effects{};
    TextGradientParams Gradient{};
    TextAnimationParams Animation{};
    bool Vertical = false;
    bool RightToLeft = false;
};

struct TextRenderStats
{
    std::uint32_t Items = 0;
    std::uint32_t MissingGlyphs = 0;
    std::uint32_t Lines = 0;
    bool LayoutOverflow = false;
};

namespace Detail
{

// Resolves a shaped glyph to its atlas. A glyph carries the id of the face that
// produced it, and 0 is a real face id (the first mounted face), so the base
// atlas is only a fallback when no atlas owns that id.
[[nodiscard]] inline const Ui::GlyphRecord* ResolveShapedGlyph(const Ui::FontAtlasSet& InFonts,
                                                              const Ui::FontAtlas* InBase,
                                                              const ShapedGlyph& InGlyph,
                                                              const Ui::FontAtlas** OutAtlas,
                                                              Ui::FontStyle InStyle =
                                                                  Ui::FontStyle::Regular) noexcept
{
    const Ui::GlyphRecord* ScopedRecord =
        InFonts.ResolveGlyphById(InGlyph.FontId, InStyle, InGlyph.GlyphId, OutAtlas);
    if (ScopedRecord == nullptr && InStyle != Ui::FontStyle::Regular)
    {
        ScopedRecord = InFonts.ResolveGlyphById(InGlyph.FontId, Ui::FontStyle::Regular, InGlyph.GlyphId, OutAtlas);
    }
    if (ScopedRecord == nullptr && InBase != nullptr && InBase->FontId() != InGlyph.FontId)
    {
        ScopedRecord = InFonts.ResolveGlyphById(InBase->FontId(), InStyle, InGlyph.GlyphId, OutAtlas);
    }
    return ScopedRecord;
}

// Emits the decoration quad of one styled glyph. The offsets follow the legacy
// styled path so underline and strike sit on the same line in both renderers.
[[nodiscard]] inline bool EmitDecoration(std::span<Rhi::RenderItem> OutItems, std::uint32_t& InWritten,
                                         const TextStyle& InStyle, Vector2D InCenter, Vector2D InSize,
                                         TextRenderStats* OutStats) noexcept
{
    if (InWritten >= OutItems.size())
    {
        if (OutStats != nullptr)
        {
            OutStats->LayoutOverflow = true;
        }
        return false;
    }
    OutItems[InWritten++] = Rhi::RenderItem::Quad(InCenter, InSize, InStyle.Color.rgba).WithLayer(0);
    return true;
}

// Whitespace and other inkless glyphs keep their advance but produce no quad.
// They are resolved glyphs, not missing ones, so the missing counter stays 0.
[[nodiscard]] inline bool IsInklessGlyph(const Ui::GlyphRecord& InGlyph) noexcept
{
    return InGlyph.width <= 0.0f || InGlyph.height <= 0.0f;
}

// Style that covers one shaped glyph. Cluster offsets are byte offsets into the
// block, which is exactly the span space, so a shaped glyph keeps the style of
// the characters it was shaped from.
[[nodiscard]] inline const TextStyle* StyleAt(std::span<const TextSpan> InSpans, std::span<const TextStyle> InStyles,
                                              std::uint32_t InCluster) noexcept
{
    for (const TextSpan& ScopedSpan : InSpans)
    {
        if (InCluster >= ScopedSpan.Begin && InCluster < ScopedSpan.End && ScopedSpan.StyleIndex < InStyles.size())
        {
            return &InStyles[ScopedSpan.StyleIndex];
        }
    }
    return nullptr;
}

[[nodiscard]] inline std::uint32_t LerpColor(std::uint32_t InStart, std::uint32_t InEnd, float InT) noexcept
{
    const float ScopedT = std::clamp(InT, 0.0f, 1.0f);
    const auto ScopedChannel = [ScopedT](std::uint32_t InChannelStart, std::uint32_t InChannelEnd)
    {
        return static_cast<std::uint32_t>(static_cast<float>(InChannelStart) +
                                          (static_cast<float>(InChannelEnd) - static_cast<float>(InChannelStart)) *
                                              ScopedT);
    };
    const std::uint32_t ScopedRed = ScopedChannel(InStart & 0xFFu, InEnd & 0xFFu);
    const std::uint32_t ScopedGreen = ScopedChannel((InStart >> 8u) & 0xFFu, (InEnd >> 8u) & 0xFFu);
    const std::uint32_t ScopedBlue = ScopedChannel((InStart >> 16u) & 0xFFu, (InEnd >> 16u) & 0xFFu);
    const std::uint32_t ScopedAlpha = ScopedChannel((InStart >> 24u) & 0xFFu, (InEnd >> 24u) & 0xFFu);
    return ScopedRed | (ScopedGreen << 8u) | (ScopedBlue << 16u) | (ScopedAlpha << 24u);
}

[[nodiscard]] inline std::uint32_t RainbowColor(float InHue, float InAlpha = 1.0f) noexcept
{
    const float ScopedHue = InHue - std::floor(InHue);
    const float ScopedSector = ScopedHue * 6.0f;
    const float ScopedFraction = ScopedSector - std::floor(ScopedSector);
    const float ScopedValue = 1.0f;
    const float ScopedP = 0.0f;
    const float ScopedQ = 1.0f - ScopedFraction;
    const float ScopedT = ScopedFraction;
    float ScopedRed = 0.0f;
    float ScopedGreen = 0.0f;
    float ScopedBlue = 0.0f;
    switch (static_cast<int>(ScopedSector) % 6)
    {
        case 0:
            ScopedRed = ScopedValue;
            ScopedGreen = ScopedT;
            ScopedBlue = ScopedP;
            break;
        case 1:
            ScopedRed = ScopedQ;
            ScopedGreen = ScopedValue;
            ScopedBlue = ScopedP;
            break;
        case 2:
            ScopedRed = ScopedP;
            ScopedGreen = ScopedValue;
            ScopedBlue = ScopedT;
            break;
        case 3:
            ScopedRed = ScopedP;
            ScopedGreen = ScopedQ;
            ScopedBlue = ScopedValue;
            break;
        case 4:
            ScopedRed = ScopedT;
            ScopedGreen = ScopedP;
            ScopedBlue = ScopedValue;
            break;
        default:
            ScopedRed = ScopedValue;
            ScopedGreen = ScopedP;
            ScopedBlue = ScopedQ;
            break;
    }
    return Color32::FromBytes(static_cast<std::uint8_t>(ScopedRed * 255.0f),
                              static_cast<std::uint8_t>(ScopedGreen * 255.0f),
                              static_cast<std::uint8_t>(ScopedBlue * 255.0f),
                              static_cast<std::uint8_t>(std::clamp(InAlpha, 0.0f, 1.0f) * 255.0f))
        .rgba;
}

inline std::uint32_t EmitGlyphItems(std::string_view InBlock, const TextLayoutResult& InLayout,
                                    std::span<const TextSpan> InSpans, std::span<const TextStyle> InStyles,
                                    Vector2D InOrigin, const TextRenderParams& InRenderParams,
                                    std::span<Rhi::RenderItem> OutItems, TextRenderStats* OutStats) noexcept
{
    if (InRenderParams.Fonts == nullptr || InRenderParams.FontSize <= 0.0f)
    {
        return 0;
    }
    const Ui::FontAtlas* ScopedAtlas = InRenderParams.Fonts->At(InRenderParams.FontAtlasIndex);
    if (ScopedAtlas == nullptr || !ScopedAtlas->IsOpen() || ScopedAtlas->LineHeight() <= 0.0f)
    {
        return 0;
    }
    const std::uint32_t ScopedLayer = InRenderParams.Fonts->LayerForFontId(ScopedAtlas->FontId());
    if (ScopedLayer == 0xFFFFFFFFu)
    {
        return 0;
    }

    std::uint32_t ScopedWritten = 0;
    std::uint32_t ScopedGlyphIndex = 0;
    float ScopedPenY = InOrigin.Y;
    for (std::uint32_t ScopedLine = 0; ScopedLine < InLayout.Count; ++ScopedLine)
    {
        const TextLine& ScopedRange = InLayout.Lines[ScopedLine];
        if (ScopedRange.End > InBlock.size() || ScopedRange.Begin > ScopedRange.End)
        {
            continue;
        }
        const float ScopedLineHeight =
            ScopedRange.Height > 0.0f ? ScopedRange.Height : InRenderParams.FontSize;
        const std::string_view ScopedLineText = InBlock.substr(ScopedRange.Begin, ScopedRange.End - ScopedRange.Begin);
        float ScopedPenX = InOrigin.X;
        std::size_t ScopedCursor = 0;
        while (ScopedCursor < ScopedLineText.size())
        {
            const std::size_t ScopedByteOffset = static_cast<std::size_t>(ScopedRange.Begin) + ScopedCursor;
            const std::uint32_t ScopedCodepoint = DecodeTextUtf8(ScopedLineText, ScopedCursor);
            if (ScopedCodepoint == '\n')
            {
                continue;
            }
            std::uint32_t ScopedColor = InRenderParams.Color.rgba;
            const TextStyle* ScopedStyle = nullptr;
            for (const TextSpan& ScopedSpan : InSpans)
            {
                if (ScopedByteOffset >= ScopedSpan.Begin && ScopedByteOffset < ScopedSpan.End &&
                    ScopedSpan.StyleIndex < InStyles.size())
                {
                    ScopedStyle = &InStyles[ScopedSpan.StyleIndex];
                    ScopedColor = ScopedStyle->Color.rgba;
                    break;
                }
            }
            if (InRenderParams.Gradient.Enabled)
            {
                const float ScopedLineWidth = std::max(ScopedRange.Width, 1.0f);
                const float ScopedT = std::clamp((ScopedPenX - InOrigin.X) / ScopedLineWidth, 0.0f, 1.0f);
                ScopedColor = LerpColor(InRenderParams.Gradient.Start.rgba, InRenderParams.Gradient.End.rgba, ScopedT);
            }
            if (ScopedCodepoint == kTextInlinePlaceholder)
            {
                const TextInlineObject* ScopedInline = nullptr;
                for (const TextInlineObject& ScopedCandidate : InRenderParams.Inlines)
                {
                    if (ScopedCandidate.ByteOffset == ScopedByteOffset)
                    {
                        ScopedInline = &ScopedCandidate;
                        break;
                    }
                }
                if (ScopedInline != nullptr && ScopedInline->Width > 0.0f && ScopedInline->Height > 0.0f)
                {
                    if (ScopedWritten >= OutItems.size())
                    {
                        if (OutStats != nullptr)
                        {
                            OutStats->Items = ScopedWritten;
                        }
                        return ScopedWritten;
                    }
                    const auto ScopedInlineVisualX = [&](float InOffset)
                    {
                        return InRenderParams.RightToLeft
                                   ? InOrigin.X + ScopedRange.Width - (ScopedPenX - InOrigin.X) - InOffset
                                   : ScopedPenX + InOffset;
                    };
                    const Vector2D ScopedInlineCenter{
                        ScopedInlineVisualX(ScopedInline->Width * 0.5f),
                        ScopedPenY + (ScopedLineHeight - ScopedInline->Height) * 0.5f};
                    const auto ScopedEmitInline = [&](const Vector2D& InCenter, const Vector2D& InSize,
                                                      std::uint32_t InColor) noexcept
                    {
                        if (ScopedWritten >= OutItems.size())
                        {
                            if (OutStats != nullptr)
                            {
                                OutStats->Items = ScopedWritten;
                            }
                            return false;
                        }
                        OutItems[ScopedWritten++] = Rhi::RenderItem::Quad(InCenter, InSize, InColor).WithLayer(0);
                        return true;
                    };
                    const Vector2D ScopedInlineSize{ScopedInline->Width, ScopedInline->Height};
                    if (ScopedInline->Kind == TextInlineKind::Sprite)
                    {
                        const TextInlineSprite* ScopedSprite = nullptr;
                        for (const TextInlineSprite& ScopedCandidate : InRenderParams.InlineSprites)
                        {
                            if (ScopedCandidate.Id == ScopedInline->Id)
                            {
                                ScopedSprite = &ScopedCandidate;
                                break;
                            }
                        }
                        if (ScopedWritten >= OutItems.size())
                        {
                            if (OutStats != nullptr)
                            {
                                OutStats->Items = ScopedWritten;
                            }
                            return ScopedWritten;
                        }
                        Rhi::RenderItem& ScopedInlineItem = OutItems[ScopedWritten++];
                        ScopedInlineItem =
                            Rhi::RenderItem::Quad(ScopedInlineCenter, ScopedInlineSize, ScopedColor)
                                .WithLayer(ScopedSprite != nullptr ? ScopedSprite->Layer : 0);
                        if (ScopedSprite != nullptr)
                        {
                            ScopedInlineItem.uvMin = Vector2D{ScopedSprite->U0, ScopedSprite->V0};
                            ScopedInlineItem.uvMax = Vector2D{ScopedSprite->U1, ScopedSprite->V1};
                        }
                    }
                    else if (ScopedInline->Kind == TextInlineKind::Progress)
                    {
                        const std::uint32_t ScopedTrackColor = LerpColor(0xFF101018u, ScopedColor, 0.25f);
                        if (!ScopedEmitInline(ScopedInlineCenter, ScopedInlineSize, ScopedTrackColor))
                        {
                            return ScopedWritten;
                        }
                        const float ScopedFillWidth = ScopedInline->Width * std::clamp(ScopedInline->Progress, 0.0f, 1.0f);
                        if (ScopedFillWidth > 0.0f)
                        {
                            const Vector2D ScopedFillCenter{ScopedInlineVisualX(ScopedFillWidth * 0.5f),
                                                            ScopedInlineCenter.Y};
                            if (!ScopedEmitInline(ScopedFillCenter, Vector2D{ScopedFillWidth, ScopedInline->Height},
                                                  ScopedColor))
                            {
                                return ScopedWritten;
                            }
                        }
                    }
                    else if (ScopedInline->Kind == TextInlineKind::Separator)
                    {
                        if (!ScopedEmitInline(ScopedInlineCenter, ScopedInlineSize, ScopedColor))
                        {
                            return ScopedWritten;
                        }
                    }
                    else
                    {
                        const bool ScopedButton = ScopedInline->Kind == TextInlineKind::Button;
                        if (ScopedButton && !ScopedEmitInline(ScopedInlineCenter, ScopedInlineSize,
                                                              LerpColor(0xFF202028u, ScopedColor, 0.35f)))
                        {
                            return ScopedWritten;
                        }
                        const float ScopedBorder = std::min(1.5f, std::min(ScopedInline->Width, ScopedInline->Height));
                        const Vector2D ScopedEdges[4] = {
                            Vector2D{ScopedInlineCenter.X, ScopedPenY + ScopedBorder * 0.5f},
                            Vector2D{ScopedInlineCenter.X, ScopedPenY + ScopedInline->Height - ScopedBorder * 0.5f},
                            Vector2D{ScopedInlineVisualX(ScopedBorder * 0.5f), ScopedInlineCenter.Y},
                            Vector2D{ScopedInlineVisualX(ScopedInline->Width - ScopedBorder * 0.5f),
                                     ScopedInlineCenter.Y}};
                        const Vector2D ScopedEdgeSizes[4] = {
                            Vector2D{ScopedInline->Width, ScopedBorder}, Vector2D{ScopedInline->Width, ScopedBorder},
                            Vector2D{ScopedBorder, ScopedInline->Height}, Vector2D{ScopedBorder, ScopedInline->Height}};
                        for (std::uint32_t ScopedEdge = 0; ScopedEdge < 4; ++ScopedEdge)
                        {
                            if (!ScopedEmitInline(ScopedEdges[ScopedEdge], ScopedEdgeSizes[ScopedEdge], ScopedColor))
                            {
                                return ScopedWritten;
                            }
                        }
                    }
                    ScopedPenX += ScopedInline->Width;
                }
                continue;
            }
            const Ui::FontAtlas* ScopedGlyphAtlas = ScopedAtlas;
            std::uint32_t ScopedGlyphLayer = ScopedLayer;
            const Ui::FontStyle ScopedFontStyle =
                ScopedStyle != nullptr ? FontStyleForTextFlags(ScopedStyle->Flags) : Ui::FontStyle::Regular;
            const Ui::GlyphRecord* ScopedGlyph = nullptr;
            if (InRenderParams.Fonts != nullptr)
            {
                ScopedGlyph = InRenderParams.Fonts->ResolveGlyph(ScopedAtlas->FontId(), ScopedFontStyle,
                                                                 ScopedCodepoint, &ScopedGlyphAtlas);
                if (ScopedGlyphAtlas != nullptr)
                {
                    const std::uint32_t ScopedResolvedLayer =
                        InRenderParams.Fonts->LayerForAtlas(ScopedGlyphAtlas);
                    if (ScopedResolvedLayer != 0xFFFFFFFFu)
                    {
                        ScopedGlyphLayer = ScopedResolvedLayer;
                    }
                }
            }
            else
            {
                ScopedGlyph = ScopedGlyphAtlas->Glyph(ScopedCodepoint);
            }
            const float ScopedGlyphFontSize =
                ScopedStyle != nullptr && ScopedStyle->FontSize > 0 ? static_cast<float>(ScopedStyle->FontSize)
                                                                  : InRenderParams.FontSize;
            const float ScopedGlyphScale = ScopedGlyphFontSize / ScopedGlyphAtlas->LineHeight();
            if (ScopedGlyph == nullptr || ScopedGlyph->width <= 0.0f || ScopedGlyph->height <= 0.0f)
            {
                if (OutStats != nullptr)
                {
                    ++OutStats->MissingGlyphs;
                }
                continue;
            }
            const std::uint32_t ScopedGlyphOrdinal = ScopedGlyphIndex++;
            if (InRenderParams.Animation.Typewriter &&
                ScopedGlyphOrdinal >= InRenderParams.Animation.VisibleGlyphs)
            {
                ScopedPenX += ScopedGlyph->advance * ScopedGlyphScale;
                continue;
            }
            const Vector2D ScopedGlyphMin{
                ScopedPenX + ScopedGlyph->bearingX * ScopedGlyphScale,
                ScopedPenY + (ScopedGlyphAtlas->Ascent() - ScopedGlyph->bearingY) * ScopedGlyphScale};
            const Vector2D ScopedGlyphSize{ScopedGlyph->width * ScopedGlyphScale,
                                           ScopedGlyph->height * ScopedGlyphScale};
            Vector2D ScopedCenter{ScopedGlyphMin.X + ScopedGlyphSize.X * 0.5f,
                                  ScopedGlyphMin.Y + ScopedGlyphSize.Y * 0.5f};
            if (InRenderParams.Animation.Wave)
            {
                ScopedCenter.Y +=
                    std::sin(InRenderParams.Animation.Time * InRenderParams.Animation.WaveFrequency +
                             static_cast<float>(ScopedGlyphOrdinal) * 0.7f) *
                    InRenderParams.Animation.WaveAmplitude;
            }
            if (InRenderParams.Animation.Shake)
            {
                std::uint32_t ScopedHash = (ScopedGlyphOrdinal + InRenderParams.Animation.ShakeSeed) * 747796405u;
                ScopedHash = (ScopedHash ^ (ScopedHash >> 15u)) * 2246822519u;
                const float ScopedJitterX =
                    static_cast<float>((ScopedHash & 0xFFu) - 128u) / 128.0f * InRenderParams.Animation.ShakeAmplitude;
                ScopedHash = ScopedHash * 1664525u + 1013904223u;
                const float ScopedJitterY =
                    static_cast<float>((ScopedHash & 0xFFu) - 128u) / 128.0f * InRenderParams.Animation.ShakeAmplitude;
                ScopedCenter.X += ScopedJitterX;
                ScopedCenter.Y += ScopedJitterY;
            }
            if (InRenderParams.Animation.Rainbow)
            {
                ScopedColor = RainbowColor(InRenderParams.Animation.Time * InRenderParams.Animation.RainbowSpeed +
                                           static_cast<float>(ScopedGlyphOrdinal) * 0.05f);
            }
            const bool ScopedIsSdf = ScopedGlyphAtlas->Format() == Ui::FontPixelFormat::SdfR8;
            const std::uint32_t ScopedItemFlags =
                InRenderParams.Flags | (ScopedIsSdf ? Rhi::kRenderFlagSdfText : 0u);
            const auto ScopedEmitGlyph = [&](const Vector2D& InCenter, std::uint32_t InColor,
                                             std::uint32_t InEffectFlags = 0) noexcept
            {
                if (ScopedWritten >= OutItems.size())
                {
                    if (OutStats != nullptr)
                    {
                        OutStats->Items = ScopedWritten;
                    }
                    return false;
                }
                Rhi::RenderItem& ScopedCopy = OutItems[ScopedWritten++];
                ScopedCopy =
                    Rhi::RenderItem::Quad(InCenter, ScopedGlyphSize, InColor).WithLayer(ScopedGlyphLayer);
                ScopedCopy.uvMin = Vector2D{ScopedGlyph->u0, ScopedGlyph->v0};
                ScopedCopy.uvMax = Vector2D{ScopedGlyph->u1, ScopedGlyph->v1};
                ScopedCopy.flags = ScopedItemFlags | InEffectFlags;
                return true;
            };
            const float ScopedGlyphExtent = std::max(ScopedGlyph->height, 1.0f);
            if (InRenderParams.Effects.Glow && InRenderParams.Effects.GlowRadius > 0.0f)
            {
                const float ScopedGlow =
                    std::clamp(InRenderParams.Effects.GlowRadius / ScopedGlyphExtent, 0.0f, 1.0f);
                if (!ScopedEmitGlyph(ScopedCenter, InRenderParams.Effects.GlowColor.rgba,
                                     Rhi::kRenderFlagSdfGlow | Rhi::EncodeSdfEffectWidth(ScopedGlow)))
                {
                    return ScopedWritten;
                }
            }
            if (InRenderParams.Effects.Shadow)
            {
                if (!ScopedEmitGlyph(
                        ScopedCenter + InRenderParams.Effects.ShadowOffset,
                        InRenderParams.Effects.ShadowColor.rgba))
                {
                    return ScopedWritten;
                }
            }
            if (InRenderParams.Effects.Outline && InRenderParams.Effects.OutlineWidth > 0.0f)
            {
                const float ScopedOutline =
                    std::clamp(InRenderParams.Effects.OutlineWidth / ScopedGlyphExtent, 0.0f, 1.0f);
                if (!ScopedEmitGlyph(ScopedCenter, InRenderParams.Effects.OutlineColor.rgba,
                                     Rhi::kRenderFlagSdfOutline | Rhi::EncodeSdfEffectWidth(ScopedOutline)))
                {
                    return ScopedWritten;
                }
            }
            if (!ScopedEmitGlyph(ScopedCenter, ScopedColor))
            {
                return ScopedWritten;
            }

            if (ScopedStyle != nullptr &&
                (ScopedStyle->Flags & static_cast<std::uint16_t>(TextStyleFlags::Underline |
                                                                TextStyleFlags::Strike)) != 0)
            {
                const float ScopedDecorationWidth = ScopedGlyph->advance * ScopedGlyphScale;
                const float ScopedBaseline = ScopedPenY + ScopedGlyphAtlas->Ascent() * ScopedGlyphScale;
                const bool ScopedUnderline =
                    (ScopedStyle->Flags & static_cast<std::uint16_t>(TextStyleFlags::Underline)) != 0;
                const float ScopedDecorationY = ScopedUnderline ? ScopedBaseline + 2.0f
                                                                : ScopedBaseline - ScopedLineHeight * 0.3f;
                const Vector2D ScopedDecorationCenter{ScopedPenX + ScopedDecorationWidth * 0.5f, ScopedDecorationY};
                if (ScopedWritten >= OutItems.size())
                {
                    if (OutStats != nullptr)
                    {
                        OutStats->Items = ScopedWritten;
                    }
                    return ScopedWritten;
                }
                OutItems[ScopedWritten++] =
                    Rhi::RenderItem::Quad(ScopedDecorationCenter,
                                          Vector2D{ScopedDecorationWidth, std::max(1.0f, ScopedGlyphScale)},
                                          ScopedColor)
                        .WithLayer(0);
            }
            ScopedPenX += ScopedGlyph->advance * ScopedGlyphScale;
        }
        ScopedPenY += ScopedLineHeight;
    }
    if (OutStats != nullptr)
    {
        OutStats->Items = ScopedWritten;
    }
    return ScopedWritten;
}
}

// Emits one quad per glyph. The caller owns the destination array, so the text
// path does not allocate and can be extracted into the normal render item list.
[[nodiscard]] inline std::uint32_t BuildTextRenderItems(const TextService& InText, TextAssetRef InRef,
                                                        Vector2D InOrigin, const TextLayoutParams& InLayoutParams,
                                                        const TextRenderParams& InRenderParams,
                                                        std::span<Rhi::RenderItem> OutItems,
                                                        TextRenderStats* OutStats = nullptr) noexcept
{
    if (OutStats != nullptr)
    {
        *OutStats = {};
    }
    const std::string_view ScopedBlock = InText.Block(InRef);
    const TextLayoutResult* ScopedLayout = InText.CachedLayout(InRef, InLayoutParams);
    if (ScopedLayout == nullptr)
    {
        return 0;
    }
    if (OutStats != nullptr)
    {
        OutStats->LayoutOverflow = ScopedLayout->Overflow;
        OutStats->Lines = ScopedLayout->Count;
    }
    return Detail::EmitGlyphItems(ScopedBlock, *ScopedLayout, InRenderParams.Spans, InRenderParams.Styles, InOrigin,
                                  InRenderParams, OutItems, OutStats);
}

// Renders already-resolved text with a caller-owned layout.
[[nodiscard]] inline std::uint32_t BuildTextRenderItems(std::string_view InText, const TextLayoutResult& InLayout,
                                                        Vector2D InOrigin,
                                                        const TextRenderParams& InRenderParams,
                                                        std::span<Rhi::RenderItem> OutItems,
                                                        TextRenderStats* OutStats = nullptr) noexcept
{
    if (OutStats != nullptr)
    {
        *OutStats = {};
        OutStats->LayoutOverflow = InLayout.Overflow;
        OutStats->Lines = InLayout.Count;
    }
    return Detail::EmitGlyphItems(InText, InLayout, InRenderParams.Spans, InRenderParams.Styles, InOrigin,
                                  InRenderParams, OutItems, OutStats);
}

// Renders already-resolved text, for formatted placeholders and tool previews.
[[nodiscard]] inline std::uint32_t BuildTextRenderItems(std::string_view InText, Vector2D InOrigin,
                                                        const TextLayoutParams& InLayoutParams,
                                                        const TextRenderParams& InRenderParams,
                                                        std::span<Rhi::RenderItem> OutItems,
                                                        TextRenderStats* OutStats = nullptr) noexcept
{
    if (OutStats != nullptr)
    {
        *OutStats = {};
    }
    TextLayoutResult ScopedLayout;
    const bool ScopedOk = LayoutText(InText, InLayoutParams, &ScopedLayout);
    if (OutStats != nullptr)
    {
        OutStats->LayoutOverflow = !ScopedOk || ScopedLayout.Overflow;
        OutStats->Lines = ScopedLayout.Count;
    }
    return Detail::EmitGlyphItems(InText, ScopedLayout, InRenderParams.Spans, InRenderParams.Styles, InOrigin,
                                  InRenderParams, OutItems, OutStats);
}

// Renders a compiled markup block. The block owns its spans and styles, so the
// caller does not need to keep a separate style table alive.
[[nodiscard]] inline std::uint32_t BuildStyledTextRenderItems(const TextStyledBlock& InBlock, Vector2D InOrigin,
                                                              const TextLayoutParams& InLayoutParams,
                                                              const TextRenderParams& InRenderParams,
                                                              std::span<Rhi::RenderItem> OutItems,
                                                              TextRenderStats* OutStats = nullptr) noexcept
{
    if (OutStats != nullptr)
    {
        *OutStats = {};
    }
    TextLayoutParams ScopedLayoutParams = InLayoutParams;
    if (ScopedLayoutParams.Inlines.empty())
    {
        ScopedLayoutParams.Inlines =
            std::span<const TextInlineObject>(InBlock.Inlines.data(), InBlock.InlineCount);
    }
    TextLayoutResult ScopedLayout;
    const bool ScopedOk = LayoutText(InBlock.View(), ScopedLayoutParams, &ScopedLayout);
    if (OutStats != nullptr)
    {
        OutStats->LayoutOverflow = !ScopedOk || ScopedLayout.Overflow;
        OutStats->Lines = ScopedLayout.Count;
    }
    TextRenderParams ScopedRenderParams = InRenderParams;
    if (ScopedRenderParams.Inlines.empty())
    {
        ScopedRenderParams.Inlines =
            std::span<const TextInlineObject>(InBlock.Inlines.data(), InBlock.InlineCount);
    }
    return Detail::EmitGlyphItems(
        InBlock.View(), ScopedLayout, std::span<const TextSpan>(InBlock.Spans.data(), InBlock.SpanCount),
        std::span<const TextStyle>(InBlock.Styles.data(), InBlock.StyleCount), InOrigin, ScopedRenderParams,
        OutItems, OutStats);
}

// Renders HarfBuzz-shaped glyph ids. Positions are laid out in the order
// returned by the shaper (HarfBuzz already emits RTL runs in visual order).
[[nodiscard]] inline std::uint32_t BuildShapedTextRenderItems(const TextShapeResult& InShaped, Vector2D InOrigin,
                                                              const TextRenderParams& InRenderParams,
                                                              std::span<Rhi::RenderItem> OutItems,
                                                              TextRenderStats* OutStats = nullptr) noexcept
{
    if (OutStats != nullptr)
    {
        *OutStats = {};
    }
    if (InRenderParams.Fonts == nullptr || InShaped.Count == 0)
    {
        return 0;
    }
    const Ui::FontAtlas* ScopedBase = InRenderParams.Fonts->At(InRenderParams.FontAtlasIndex);
    if (ScopedBase == nullptr)
    {
        return 0;
    }
    std::uint32_t ScopedWritten = 0;
    Vector2D ScopedPen = InOrigin;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InShaped.Count; ++ScopedIndex)
    {
        const ShapedGlyph& ScopedShaped = InShaped.Glyphs[ScopedIndex];
        const TextStyle* ScopedStyle =
            Detail::StyleAt(InRenderParams.Spans, InRenderParams.Styles, ScopedShaped.Cluster);
        const Ui::FontStyle ScopedSlot =
            ScopedStyle != nullptr ? FontStyleForTextFlags(ScopedStyle->Flags) : Ui::FontStyle::Regular;
        const Ui::FontAtlas* ScopedAtlas = ScopedBase;
        const Ui::GlyphRecord* ScopedGlyph =
            Detail::ResolveShapedGlyph(*InRenderParams.Fonts, ScopedBase, ScopedShaped, &ScopedAtlas, ScopedSlot);
        if (ScopedGlyph == nullptr)
        {
            if (OutStats != nullptr)
            {
                ++OutStats->MissingGlyphs;
            }
            continue;
        }
        if (Detail::IsInklessGlyph(*ScopedGlyph))
        {
            ScopedPen.X += ScopedShaped.AdvanceX;
            ScopedPen.Y += ScopedShaped.AdvanceY;
            continue;
        }
        if (ScopedWritten >= OutItems.size())
        {
            if (OutStats != nullptr)
            {
                OutStats->LayoutOverflow = true;
            }
            break;
        }
        const float ScopedScale = InRenderParams.FontSize > 0.0f
                                      ? InRenderParams.FontSize / std::max(ScopedAtlas->LineHeight(), 1.0f)
                                      : 1.0f;
        const Vector2D ScopedGlyphSize{ScopedGlyph->width * ScopedScale, ScopedGlyph->height * ScopedScale};
        const Vector2D ScopedCenter{
            ScopedPen.X + ScopedShaped.OffsetX +
                (ScopedGlyph->bearingX + ScopedGlyph->width * 0.5f) * ScopedScale,
            ScopedPen.Y + ScopedShaped.OffsetY +
                (ScopedAtlas->Ascent() - ScopedGlyph->bearingY + ScopedGlyph->height * 0.5f) * ScopedScale};
        const std::uint32_t ScopedColor =
            ScopedStyle != nullptr ? ScopedStyle->Color.rgba : InRenderParams.Color.rgba;
        Rhi::RenderItem& ScopedItem = OutItems[ScopedWritten++];
        ScopedItem = Rhi::RenderItem::Quad(ScopedCenter, ScopedGlyphSize, ScopedColor)
                         .WithLayer(InRenderParams.Fonts->LayerForAtlas(ScopedAtlas));
        ScopedItem.uvMin = Vector2D{ScopedGlyph->u0, ScopedGlyph->v0};
        ScopedItem.uvMax = Vector2D{ScopedGlyph->u1, ScopedGlyph->v1};
        ScopedItem.flags = InRenderParams.Flags |
                           (ScopedAtlas->Format() == Ui::FontPixelFormat::SdfR8 ? Rhi::kRenderFlagSdfText : 0u);
        if (ScopedStyle != nullptr &&
            (ScopedStyle->Flags & static_cast<std::uint16_t>(TextStyleFlags::Underline | TextStyleFlags::Strike)) != 0)
        {
            const float ScopedBaseline = ScopedPen.Y + ScopedAtlas->Ascent() * ScopedScale;
            const float ScopedLineHeight = ScopedAtlas->LineHeight() * ScopedScale;
            const bool ScopedUnderline =
                (ScopedStyle->Flags & static_cast<std::uint16_t>(TextStyleFlags::Underline)) != 0;
            const float ScopedDecorationY =
                ScopedUnderline ? ScopedBaseline + 2.0f : ScopedBaseline - ScopedLineHeight * 0.3f;
            const float ScopedDecorationWidth = std::max(std::abs(ScopedShaped.AdvanceX), 1.0f);
            (void)Detail::EmitDecoration(
                OutItems, ScopedWritten, *ScopedStyle,
                Vector2D{ScopedPen.X + ScopedDecorationWidth * 0.5f, ScopedDecorationY},
                Vector2D{ScopedDecorationWidth, std::max(1.0f, ScopedScale)}, OutStats);
        }
        ScopedPen.X += ScopedShaped.AdvanceX;
        ScopedPen.Y += ScopedShaped.AdvanceY;
    }
    if (OutStats != nullptr)
    {
        OutStats->Items = ScopedWritten;
    }
    return ScopedWritten;
}

// Renders a DOD cache view: flat glyph and line spans, no temporary shaping.
[[nodiscard]] inline std::uint32_t BuildShapedTextRenderItems(std::span<const ShapedGlyph> InGlyphs,
                                                              std::span<const ShapedLine> InLines, Vector2D InOrigin,
                                                              const TextRenderParams& InRenderParams,
                                                              std::span<Rhi::RenderItem> OutItems,
                                                              TextRenderStats* OutStats = nullptr) noexcept
{
    if (OutStats != nullptr)
    {
        *OutStats = {};
    }
    if (InRenderParams.Fonts == nullptr || InGlyphs.empty())
    {
        return 0;
    }
    const Ui::FontAtlas* ScopedBase = InRenderParams.Fonts->At(InRenderParams.FontAtlasIndex);
    if (ScopedBase == nullptr)
    {
        return 0;
    }
    std::uint32_t ScopedWritten = 0;
    float ScopedLineY = InOrigin.Y;
    float ScopedLineX = InOrigin.X;
    for (const ShapedLine& ScopedLine : InLines)
    {
        if (InRenderParams.Vertical)
        {
            float ScopedPenY = InOrigin.Y;
            for (std::uint32_t ScopedIndex = ScopedLine.BeginGlyph;
                 ScopedIndex < ScopedLine.EndGlyph && ScopedIndex < InGlyphs.size(); ++ScopedIndex)
            {
                const ShapedGlyph& ScopedShaped = InGlyphs[ScopedIndex];
                const TextStyle* ScopedStyle =
                    Detail::StyleAt(InRenderParams.Spans, InRenderParams.Styles, ScopedShaped.Cluster);
                const Ui::FontStyle ScopedSlot =
                    ScopedStyle != nullptr ? FontStyleForTextFlags(ScopedStyle->Flags) : Ui::FontStyle::Regular;
                const TextInlineObject* ScopedInline = nullptr;
                for (const TextInlineObject& ScopedCandidate : InRenderParams.Inlines)
                {
                    if (ScopedCandidate.ByteOffset == ScopedShaped.Cluster)
                    {
                        ScopedInline = &ScopedCandidate;
                        break;
                    }
                }
                if (ScopedInline != nullptr && ScopedInline->Width > 0.0f && ScopedInline->Height > 0.0f)
                {
                    if (ScopedWritten >= OutItems.size())
                    {
                        if (OutStats != nullptr)
                        {
                            OutStats->LayoutOverflow = true;
                            OutStats->Items = ScopedWritten;
                        }
                        return ScopedWritten;
                    }
                    const TextInlineSprite* ScopedSprite = nullptr;
                    for (const TextInlineSprite& ScopedCandidate : InRenderParams.InlineSprites)
                    {
                        if (ScopedCandidate.Id == ScopedInline->Id)
                        {
                            ScopedSprite = &ScopedCandidate;
                            break;
                        }
                    }
                    const Vector2D ScopedInlineCenter{ScopedLineX + ScopedInline->Width * 0.5f,
                                                      ScopedPenY + ScopedInline->Height * 0.5f};
                    Rhi::RenderItem& ScopedItem = OutItems[ScopedWritten++];
                    ScopedItem = Rhi::RenderItem::Quad(
                                     ScopedInlineCenter,
                                     Vector2D{ScopedInline->Width, ScopedInline->Height},
                                     ScopedStyle != nullptr ? ScopedStyle->Color.rgba : InRenderParams.Color.rgba)
                                     .WithLayer(ScopedSprite != nullptr ? ScopedSprite->Layer : 0);
                    if (ScopedSprite != nullptr)
                    {
                        ScopedItem.uvMin = Vector2D{ScopedSprite->U0, ScopedSprite->V0};
                        ScopedItem.uvMax = Vector2D{ScopedSprite->U1, ScopedSprite->V1};
                    }
                    ScopedPenY += std::max(ScopedInline->Height, InRenderParams.FontSize * 0.5f);
                    continue;
                }
                const Ui::FontAtlas* ScopedAtlas = ScopedBase;
                const Ui::GlyphRecord* ScopedGlyph = Detail::ResolveShapedGlyph(*InRenderParams.Fonts, ScopedBase,
                                                                               ScopedShaped, &ScopedAtlas, ScopedSlot);
                if (ScopedGlyph == nullptr)
                {
                    if (OutStats != nullptr)
                    {
                        ++OutStats->MissingGlyphs;
                    }
                    continue;
                }
                if (Detail::IsInklessGlyph(*ScopedGlyph))
                {
                    ScopedPenY += std::max(std::abs(ScopedShaped.AdvanceY), InRenderParams.FontSize * 0.5f);
                    continue;
                }
                if (ScopedWritten >= OutItems.size())
                {
                    if (OutStats != nullptr)
                    {
                        OutStats->LayoutOverflow = true;
                    }
                    return ScopedWritten;
                }
                const float ScopedScale =
                    InRenderParams.FontSize > 0.0f
                        ? InRenderParams.FontSize / std::max(ScopedAtlas->LineHeight(), 1.0f)
                        : 1.0f;
                const Vector2D ScopedGlyphSize{ScopedGlyph->width * ScopedScale,
                                               ScopedGlyph->height * ScopedScale};
                const Vector2D ScopedCenter{
                    ScopedLineX + ScopedShaped.OffsetX +
                        (ScopedGlyph->bearingX + ScopedGlyph->width * 0.5f) * ScopedScale,
                    ScopedPenY + ScopedShaped.OffsetY +
                        (ScopedAtlas->Ascent() - ScopedGlyph->bearingY + ScopedGlyph->height * 0.5f) *
                            ScopedScale};
                const std::uint32_t ScopedColor =
                    ScopedStyle != nullptr ? ScopedStyle->Color.rgba : InRenderParams.Color.rgba;
                Rhi::RenderItem& ScopedItem = OutItems[ScopedWritten++];
                ScopedItem = Rhi::RenderItem::Quad(ScopedCenter, ScopedGlyphSize, ScopedColor)
                                 .WithLayer(InRenderParams.Fonts->LayerForAtlas(ScopedAtlas));
                ScopedItem.uvMin = Vector2D{ScopedGlyph->u0, ScopedGlyph->v0};
                ScopedItem.uvMax = Vector2D{ScopedGlyph->u1, ScopedGlyph->v1};
                ScopedItem.flags =
                    InRenderParams.Flags |
                    (ScopedAtlas->Format() == Ui::FontPixelFormat::SdfR8 ? Rhi::kRenderFlagSdfText : 0u);
                ScopedPenY += std::max(std::abs(ScopedShaped.AdvanceY), InRenderParams.FontSize * 0.5f);
            }
            ScopedLineX += ScopedLine.Width + InRenderParams.FontSize;
            continue;
        }
        float ScopedPenX = InOrigin.X;
        for (std::uint32_t ScopedIndex = ScopedLine.BeginGlyph;
             ScopedIndex < ScopedLine.EndGlyph && ScopedIndex < InGlyphs.size(); ++ScopedIndex)
        {
            const ShapedGlyph& ScopedShaped = InGlyphs[ScopedIndex];
            const TextStyle* ScopedStyle =
                Detail::StyleAt(InRenderParams.Spans, InRenderParams.Styles, ScopedShaped.Cluster);
            const Ui::FontStyle ScopedSlot =
                ScopedStyle != nullptr ? FontStyleForTextFlags(ScopedStyle->Flags) : Ui::FontStyle::Regular;
            // A shaped placeholder still carries its cluster, so the inline atom
            // can replace the placeholder glyph and advance by its own width.
            const TextInlineObject* ScopedInline = nullptr;
            for (const TextInlineObject& ScopedCandidate : InRenderParams.Inlines)
            {
                if (ScopedCandidate.ByteOffset == ScopedShaped.Cluster)
                {
                    ScopedInline = &ScopedCandidate;
                    break;
                }
            }
            if (ScopedInline != nullptr && ScopedInline->Width > 0.0f && ScopedInline->Height > 0.0f)
            {
                const std::uint32_t ScopedInlineColor =
                    ScopedStyle != nullptr ? ScopedStyle->Color.rgba : InRenderParams.Color.rgba;
                const auto ScopedEmitInlineQuad = [&](Vector2D InCenter, Vector2D InSize, std::uint32_t InColor,
                                                     std::uint32_t InLayer, Vector2D InUvMin,
                                                     Vector2D InUvMax) noexcept -> bool
                {
                    if (ScopedWritten >= OutItems.size())
                    {
                        if (OutStats != nullptr)
                        {
                            OutStats->LayoutOverflow = true;
                            OutStats->Items = ScopedWritten;
                        }
                        return false;
                    }
                    Rhi::RenderItem& ScopedItem = OutItems[ScopedWritten++];
                    ScopedItem = Rhi::RenderItem::Quad(InCenter, InSize, InColor).WithLayer(InLayer);
                    ScopedItem.uvMin = InUvMin;
                    ScopedItem.uvMax = InUvMax;
                    return true;
                };
                const float ScopedInlineHeight = std::max(InRenderParams.FontSize, 1.0f);
                const Vector2D ScopedInlineCenter{
                    ScopedPenX + ScopedInline->Width * 0.5f,
                    ScopedLineY + (ScopedInlineHeight - ScopedInline->Height) * 0.5f};
                const Vector2D ScopedInlineSize{ScopedInline->Width, ScopedInline->Height};
                if (ScopedInline->Kind == TextInlineKind::Sprite)
                {
                    const TextInlineSprite* ScopedSprite = nullptr;
                    for (const TextInlineSprite& ScopedCandidate : InRenderParams.InlineSprites)
                    {
                        if (ScopedCandidate.Id == ScopedInline->Id)
                        {
                            ScopedSprite = &ScopedCandidate;
                            break;
                        }
                    }
                    const Vector2D ScopedUvMin{ScopedSprite != nullptr ? ScopedSprite->U0 : 0.0f,
                                               ScopedSprite != nullptr ? ScopedSprite->V0 : 0.0f};
                    const Vector2D ScopedUvMax{ScopedSprite != nullptr ? ScopedSprite->U1 : 1.0f,
                                               ScopedSprite != nullptr ? ScopedSprite->V1 : 1.0f};
                    if (!ScopedEmitInlineQuad(ScopedInlineCenter, ScopedInlineSize, ScopedInlineColor,
                                              ScopedSprite != nullptr ? ScopedSprite->Layer : 0, ScopedUvMin,
                                              ScopedUvMax))
                    {
                        return ScopedWritten;
                    }
                }
                else if (ScopedInline->Kind == TextInlineKind::Progress)
                {
                    const std::uint32_t ScopedTrackColor = Detail::LerpColor(0xFF101018u, ScopedInlineColor, 0.25f);
                    if (!ScopedEmitInlineQuad(ScopedInlineCenter, ScopedInlineSize, ScopedTrackColor, 0,
                                              Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}))
                    {
                        return ScopedWritten;
                    }
                    const float ScopedProgress = std::clamp(ScopedInline->Progress, 0.0f, 1.0f);
                    if (ScopedProgress > 0.0f)
                    {
                        const Vector2D ScopedFillSize{ScopedInlineSize.X * ScopedProgress, ScopedInlineSize.Y};
                        const Vector2D ScopedFillCenter{ScopedInlineCenter.X - ScopedInlineSize.X * 0.5f +
                                                            ScopedFillSize.X * 0.5f,
                                                        ScopedInlineCenter.Y};
                        if (!ScopedEmitInlineQuad(ScopedFillCenter, ScopedFillSize, ScopedInlineColor, 0,
                                                  Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}))
                        {
                            return ScopedWritten;
                        }
                    }
                }
                else if (ScopedInline->Kind == TextInlineKind::Border ||
                         ScopedInline->Kind == TextInlineKind::Button)
                {
                    if (!ScopedEmitInlineQuad(ScopedInlineCenter, ScopedInlineSize, ScopedInlineColor, 0,
                                              Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}))
                    {
                        return ScopedWritten;
                    }
                }
                else
                {
                    if (!ScopedEmitInlineQuad(ScopedInlineCenter, ScopedInlineSize, ScopedInlineColor, 0,
                                              Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}))
                    {
                        return ScopedWritten;
                    }
                }
                ScopedPenX += ScopedInline->Width;
                continue;
            }
            const Ui::FontAtlas* ScopedAtlas = ScopedBase;
            const Ui::GlyphRecord* ScopedGlyph = Detail::ResolveShapedGlyph(*InRenderParams.Fonts, ScopedBase,
                                                                           ScopedShaped, &ScopedAtlas, ScopedSlot);
            if (ScopedGlyph == nullptr)
            {
                if (OutStats != nullptr)
                {
                    ++OutStats->MissingGlyphs;
                }
                continue;
            }
            if (Detail::IsInklessGlyph(*ScopedGlyph))
            {
                ScopedPenX += ScopedShaped.AdvanceX;
                continue;
            }
            if (ScopedWritten >= OutItems.size())
            {
                if (OutStats != nullptr)
                {
                    OutStats->LayoutOverflow = true;
                }
                return ScopedWritten;
            }
            const float ScopedScale =
                InRenderParams.FontSize > 0.0f ? InRenderParams.FontSize / std::max(ScopedAtlas->LineHeight(), 1.0f)
                                               : 1.0f;
            const Vector2D ScopedGlyphSize{ScopedGlyph->width * ScopedScale, ScopedGlyph->height * ScopedScale};
            const Vector2D ScopedCenter{
                ScopedPenX + ScopedShaped.OffsetX +
                    (ScopedGlyph->bearingX + ScopedGlyph->width * 0.5f) * ScopedScale,
                ScopedLineY + ScopedShaped.OffsetY +
                    (ScopedAtlas->Ascent() - ScopedGlyph->bearingY + ScopedGlyph->height * 0.5f) * ScopedScale};
            const std::uint32_t ScopedColor =
                ScopedStyle != nullptr ? ScopedStyle->Color.rgba : InRenderParams.Color.rgba;
            Rhi::RenderItem& ScopedItem = OutItems[ScopedWritten++];
            ScopedItem = Rhi::RenderItem::Quad(ScopedCenter, ScopedGlyphSize, ScopedColor)
                             .WithLayer(InRenderParams.Fonts->LayerForAtlas(ScopedAtlas));
            ScopedItem.uvMin = Vector2D{ScopedGlyph->u0, ScopedGlyph->v0};
            ScopedItem.uvMax = Vector2D{ScopedGlyph->u1, ScopedGlyph->v1};
            ScopedItem.flags =
                InRenderParams.Flags |
                (ScopedAtlas->Format() == Ui::FontPixelFormat::SdfR8 ? Rhi::kRenderFlagSdfText : 0u);
            if (ScopedStyle != nullptr &&
                (ScopedStyle->Flags &
                 static_cast<std::uint16_t>(TextStyleFlags::Underline | TextStyleFlags::Strike)) != 0)
            {
                const float ScopedBaseline = ScopedLineY + ScopedAtlas->Ascent() * ScopedScale;
                const float ScopedHeight = ScopedAtlas->LineHeight() * ScopedScale;
                const bool ScopedUnderline =
                    (ScopedStyle->Flags & static_cast<std::uint16_t>(TextStyleFlags::Underline)) != 0;
                const float ScopedDecorationY =
                    ScopedUnderline ? ScopedBaseline + 2.0f : ScopedBaseline - ScopedHeight * 0.3f;
                const float ScopedDecorationWidth = std::max(std::abs(ScopedShaped.AdvanceX), 1.0f);
                (void)Detail::EmitDecoration(
                    OutItems, ScopedWritten, *ScopedStyle,
                    Vector2D{ScopedPenX + ScopedDecorationWidth * 0.5f, ScopedDecorationY},
                    Vector2D{ScopedDecorationWidth, std::max(1.0f, ScopedScale)}, OutStats);
            }
            ScopedPenX += ScopedShaped.AdvanceX;
        }
        ScopedLineY += ScopedLine.Height;
    }
    if (OutStats != nullptr)
    {
        OutStats->Items = ScopedWritten;
    }
    return ScopedWritten;
}

// Renders shaped glyphs through a wrapped line layout. Both the horizontal
// walk and the vertical column walk live in one place, so the preview and the
// runtime cannot drift apart.
[[nodiscard]] inline std::uint32_t BuildShapedTextRenderItems(const ShapedLayoutResult& InLayout,
                                                              const TextShapeResult& InShaped, Vector2D InOrigin,
                                                              const TextRenderParams& InRenderParams,
                                                              std::span<Rhi::RenderItem> OutItems,
                                                              TextRenderStats* OutStats = nullptr) noexcept
{
    return BuildShapedTextRenderItems(std::span<const ShapedGlyph>(InShaped.Glyphs.data(), InShaped.Count),
                                      std::span<const ShapedLine>(InLayout.Lines.data(), InLayout.Count), InOrigin,
                                      InRenderParams, OutItems, OutStats);
}
}
