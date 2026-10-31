// FemboyEngine2D - ECS system filling the DOD shape cache from TextComponent.
#pragma once

#include "Engine/Assets/Localization.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextService.hpp"
#include "Engine/Text/TextShapeCache.hpp"

#include <algorithm>
#include <cstdint>

namespace Fby::Text
{

struct TextShapeSystemStats
{
    std::uint32_t Shaped = 0;
    std::uint32_t Reused = 0;
    std::uint32_t Skipped = 0;
    std::uint32_t Overflow = 0;
};

// Runs before text extraction. A cache miss is the only place that calls the
// shaping backend; valid handles are reused without touching HarfBuzz.
[[nodiscard]] inline TextShapeSystemStats UpdateTextShapes(
    entt::registry& InRegistry, const TextService& InText, ITextShaper& InShaper, TextShapeCache& InCache,
    std::uint32_t InFontId, float InFontSize, const TextLayoutParams& InLayoutParams,
    const LocalizationTable* InLocalization = nullptr) noexcept
{
    TextShapeSystemStats ScopedStats;
    auto ScopedView = InRegistry.view<TextComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const TextComponent& ScopedText = ScopedView.get<TextComponent>(ScopedEntity);
        std::string_view ScopedString;
        if (const auto* ScopedLocalized = InRegistry.try_get<LocalizedTextComponent>(ScopedEntity);
            ScopedLocalized != nullptr && InLocalization != nullptr && ScopedLocalized->LocalizationKey != 0)
        {
            ScopedString = InLocalization->FindVariant(
                ScopedLocalized->LocalizationKey, static_cast<std::int64_t>(ScopedLocalized->PluralCount),
                static_cast<LocalizationGender>(std::min(ScopedLocalized->Gender, 2u)));
        }
        else
        {
            ScopedString = InText.Block(ScopedText.Ref());
        }
        if (ScopedString.empty())
        {
            ++ScopedStats.Skipped;
            continue;
        }
        TextShapeKey ScopedKey;
        ScopedKey.TextHash = TextShapeCache::HashText(ScopedString);
        ScopedKey.FontId = InFontId;
        ScopedKey.TextLength = static_cast<std::uint32_t>(ScopedString.size());
        ScopedKey.PixelSize = InFontSize;
        ScopedKey.LocaleHash =
            InLocalization != nullptr ? TextShapeCache::HashText(InLocalization->ActiveLocale()) : 0;
        ScopedKey.Direction = (ScopedText.Flags & kTextFlagVertical) != 0
                                  ? TextDirection::TopToBottom
                                  : ((ScopedText.Flags & kTextFlagRightToLeft) != 0 ? TextDirection::RightToLeft
                                                                                   : TextDirection::Auto);
        ScopedKey.MaxWidth = InLayoutParams.MaxWidth;
        ScopedKey.Vertical = InLayoutParams.Vertical;
        ScopedKey.InlineHash = TextShapeCache::HashInlines(InLayoutParams.Inlines);
        std::uint64_t ScopedKeyHash = ScopedKey.TextHash;
        const auto ScopedMix = [&ScopedKeyHash](std::uint64_t InValue)
        {
            ScopedKeyHash ^= InValue + 0x9E3779B97F4A7C15ull + (ScopedKeyHash << 6u) + (ScopedKeyHash >> 2u);
        };
        ScopedMix(ScopedKey.FontId);
        ScopedMix(ScopedKey.TextLength);
        ScopedMix(static_cast<std::uint64_t>(ScopedKey.PixelSize));
        ScopedMix(static_cast<std::uint64_t>(ScopedKey.Direction));
        ScopedMix(ScopedKey.LocaleHash);
        ScopedMix(static_cast<std::uint64_t>(InLayoutParams.MaxWidth));
        ScopedMix(InLayoutParams.Vertical ? 1u : 0u);
        ScopedMix(ScopedKey.InlineHash);
        TextShapeComponent* ScopedShape = InRegistry.try_get<TextShapeComponent>(ScopedEntity);
        if (ScopedShape != nullptr && ScopedShape->ShapeGeneration != 0 &&
            ScopedShape->ShapeKeyHash == ScopedKeyHash)
        {
            const TextShapeView ScopedExisting =
                InCache.View(TextShapeHandle{ScopedShape->ShapeIndex, ScopedShape->ShapeGeneration});
            if (!ScopedExisting.Glyphs.empty())
            {
                ++ScopedStats.Reused;
                continue;
            }
        }
        const TextShapeHandle ScopedHandle =
            InCache.Insert(InShaper, ScopedString, ScopedKey, InLayoutParams);
        if (!ScopedHandle.IsValid())
        {
            ++ScopedStats.Overflow;
            continue;
        }
        if (ScopedShape == nullptr)
        {
            ScopedShape = &InRegistry.emplace<TextShapeComponent>(ScopedEntity);
        }
        ScopedShape->ShapeIndex = ScopedHandle.Index;
        ScopedShape->ShapeGeneration = ScopedHandle.Generation;
        ScopedShape->ShapeKeyHash = ScopedKeyHash;
        ++ScopedStats.Shaped;
    }
    return ScopedStats;
}

struct ScriptBackend
{
    TextScript Script = TextScript::Unknown;
    ITextShaper* Shaper = nullptr;
};

// Script-routed overload: TextShapeCache uses ShapeBidiText internally, so each
// run reaches the backend registered for its script.
[[nodiscard]] inline TextShapeSystemStats UpdateTextShapes(
    entt::registry& InRegistry, const TextService& InText, std::span<const ScriptBackend> InBackends,
    TextShapeCache& InCache, std::uint32_t InFontId, float InFontSize, const TextLayoutParams& InLayoutParams,
    const LocalizationTable* InLocalization = nullptr, ITextShaper* InFallback = nullptr) noexcept
{
    ScriptShaper ScopedRouter;
    for (const ScriptBackend& ScopedBackend : InBackends)
    {
        if (!ScopedRouter.AddScript(ScopedBackend.Script, ScopedBackend.Shaper))
        {
            return TextShapeSystemStats{0, 0, 1, 0};
        }
    }
    ScopedRouter.SetFallback(InFallback);
    return UpdateTextShapes(InRegistry, InText, ScopedRouter, InCache, InFontId, InFontSize, InLayoutParams,
                            InLocalization);
}

// Fallback-chain overload: build one ChainedShaper for the whole ECS pass.
// The caller supplies primary and fallback backends in priority order.
[[nodiscard]] inline TextShapeSystemStats UpdateTextShapes(
    entt::registry& InRegistry, const TextService& InText, std::span<ITextShaper* const> InShapers,
    TextShapeCache& InCache, std::uint32_t InFontId, float InFontSize, const TextLayoutParams& InLayoutParams,
    const LocalizationTable* InLocalization = nullptr) noexcept
{
    ChainedShaper ScopedChain;
    if (!ScopedChain.SetShapers(InShapers))
    {
        return TextShapeSystemStats{0, 0, 1, 0};
    }
    return UpdateTextShapes(InRegistry, InText, ScopedChain, InCache, InFontId, InFontSize, InLayoutParams,
                            InLocalization);
}

// Stack overload: the ECS pass uses the script stack the runtime already owns,
// so shaping, script routing and glyph pages need no per-system wiring. Pages
// are warmed from the cache view, which keeps the render pass page-ready without
// shaping twice.
[[nodiscard]] inline TextShapeSystemStats UpdateTextShapes(
    entt::registry& InRegistry, const TextService& InText, TextFontStack& InStack, TextShapeCache& InCache,
    std::uint32_t InFontId, float InFontSize, const TextLayoutParams& InLayoutParams,
    const LocalizationTable* InLocalization = nullptr) noexcept
{
    const TextShapeSystemStats ScopedStats = UpdateTextShapes(InRegistry, InText, InStack.Shaper(), InCache,
                                                             InFontId, InFontSize, InLayoutParams, InLocalization);
    auto ScopedShapes = InRegistry.view<TextShapeComponent>();
    for (const Entity ScopedEntity : ScopedShapes)
    {
        const TextShapeComponent& ScopedShape = ScopedShapes.get<TextShapeComponent>(ScopedEntity);
        if (ScopedShape.ShapeGeneration == 0)
        {
            continue;
        }
        const TextShapeView ScopedView =
            InCache.View(TextShapeHandle{ScopedShape.ShapeIndex, ScopedShape.ShapeGeneration});
        (void)EnsureGlyphPages(InStack.Fonts(), ScopedView.Glyphs);
    }
    return ScopedStats;
}
}
