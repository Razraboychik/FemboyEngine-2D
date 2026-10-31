// FemboyEngine2D - editor preview for bidi/shaping/vertical text.
#include "Editor/Panels/TextPreviewPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextBidi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include <imgui.h>

namespace Fby::Editor
{
namespace
{
// Script faces mounted by the preview. The Han face is rebuilt for the
// codepoints of the current text; the other faces ship their full glyph set
// because Arabic and Indic shaping needs the contextual alternates.
struct ScriptFace
{
    const char* Path;
    std::uint32_t FontId;
    Text::TextScript Script;
    const char* Label;
};

constexpr ScriptFace kScriptFaces[] = {
    {"fonts/IBMPlex.family.toml", 10, Text::TextScript::Latin, "IBMPlex"},
    {"fonts/tests/NotoSansArabic-Regular.ttf", 1, Text::TextScript::Arabic, "NotoSansArabic"},
    {"fonts/tests/NotoSansDevanagari-Regular.ttf", 2, Text::TextScript::Devanagari, "NotoSansDevanagari"},
    {"fonts/tests/NotoSansThai-Regular.ttf", 3, Text::TextScript::Thai, "NotoSansThai"},
    {"fonts/tests/NotoSansSC-Regular.ttf", 4, Text::TextScript::Han, "NotoSansSC"},
};

// One texture array carries every face, so the whole stack is pinned to a
// single atlas edge. The CJK face is paged inside that edge.
constexpr std::uint32_t kPreviewAtlasSize = 2048;

// Sample text per script, selected through the script combo. The escapes keep
// the source file ASCII while the compiler emits UTF-8.
constexpr const char* kScriptSamples[] = {
    "Hello, world!",
    "Hello, world!",
    "\u0645\u0631\u062d\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645",
    "\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd",
    "\u0928\u092e\u0938\u094d\u0924\u0947 \u0926\u0941\u0928\u093f\u092f\u093e",
    "\u0e2a\u0e27\u0e31\u0e2a\u0e14\u0e35\u0e0a\u0e32\u0e27\u0e42\u0e25\u0e01",
    "\u4f60\u597d\uff0c\u4e16\u754c",
};

[[nodiscard]] std::uint64_t Mix64(std::uint64_t InValue, std::uint64_t InMix) noexcept
{
    return InValue ^ (InMix + 0x9E3779B97F4A7C15ull + (InValue << 6u) + (InValue >> 2u));
}
}

Text::TextScript TextPreviewPanel::SelectedScript() const noexcept
{
    switch (MScript)
    {
        case 1:
            return Text::TextScript::Latin;
        case 2:
            return Text::TextScript::Arabic;
        case 3:
            return Text::TextScript::Hebrew;
        case 4:
            return Text::TextScript::Devanagari;
        case 5:
            return Text::TextScript::Thai;
        case 6:
            return Text::TextScript::Han;
        default:
            return Text::TextScript::Unknown;
    }
}

std::uint64_t TextPreviewPanel::TextKey() const noexcept
{
    std::uint64_t ScopedKey = Mix64(0xCBF29CE484222325ull, static_cast<std::uint64_t>(MFontSize * 64.0f));
    std::size_t ScopedCursor = 0;
    const std::string_view ScopedText(MText);
    while (ScopedCursor < ScopedText.size())
    {
        ScopedKey = Mix64(ScopedKey, Text::DecodeTextUtf8(ScopedText, ScopedCursor));
    }
    return ScopedKey;
}

void TextPreviewPanel::RebuildStack(VirtualFileSystem* InVfs)
{
    MStackReady = false;
    MStack.Reset();
    MBuiltKey = TextKey();
    if (InVfs == nullptr)
    {
        std::snprintf(MStatus, sizeof(MStatus), "no virtual file system");
        return;
    }
    const double ScopedStart = ImGui::GetTime();
    std::array<Text::FontCandidate, 5> ScopedCandidates{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < std::size(kScriptFaces); ++ScopedIndex)
    {
        ScopedCandidates[ScopedIndex] = Text::FontCandidate{kScriptFaces[ScopedIndex].Path,
                                                            kScriptFaces[ScopedIndex].FontId,
                                                            kScriptFaces[ScopedIndex].Script};
    }
    std::string ScopedFamilyError;
    std::uint32_t ScopedFaces = 0;
    // The Latin slot is the full family manifest: one visual language for
    // regular, bold, italic, bold italic and mono, plus the decorations.
    if (MStack.AddFamily(*InVfs, kScriptFaces[0].Path, kScriptFaces[0].FontId, Text::TextScript::Latin, MFontSize,
                         kPreviewAtlasSize, &ScopedFamilyError))
    {
        ++ScopedFaces;
    }
    std::string ScopedScriptError;
    const std::uint32_t ScopedScripts =
        MStack.AddScriptFamily(*InVfs, std::span<const Text::FontCandidate>(ScopedCandidates).subspan(1),
                               MFontSize, kPreviewAtlasSize, &ScopedScriptError);
    ScopedFaces += ScopedScripts;
    if (ScopedFaces == 0)
    {
        std::snprintf(MStatus, sizeof(MStatus), "%s",
                      ScopedFamilyError.empty() ? "no script face mounted" : ScopedFamilyError.c_str());
    }
    MStack.SetDefaultScript(Text::TextScript::Latin);
    // Rank every script chain against the current text, so the face with the
    // best coverage leads and the rest stay as ordered fallbacks.
    for (std::size_t ScopedFaceIndex = 1; ScopedFaceIndex < std::size(kScriptFaces); ++ScopedFaceIndex)
    {
        const ScriptFace& ScopedFace = kScriptFaces[ScopedFaceIndex];
        std::array<std::uint32_t, 4> ScopedOrder{};
        const std::uint32_t ScopedRanked =
            MStack.RankFaces(ScopedFace.Script, MText, std::span<std::uint32_t>(ScopedOrder));
        if (ScopedRanked > 1)
        {
            (void)MStack.OrderScriptChain(ScopedFace.Script,
                                          std::span<const std::uint32_t>(ScopedOrder.data(), ScopedRanked));
        }
    }
    MAtlasBytes = MStack.AtlasBytes();
    MStackReady = ScopedFaces > 0;
    MBuiltSeconds = ImGui::GetTime();
    MBuildMilliseconds = static_cast<float>((MBuiltSeconds - ScopedStart) * 1000.0);
    if (!MStackReady)
    {
        if (MStatus[0] == 0)
        {
            std::snprintf(MStatus, sizeof(MStatus), "no script face mounted");
        }
        return;
    }
    std::snprintf(MStatus, sizeof(MStatus), "%u script face(s), %u atlas bytes, %.1f ms", ScopedFaces, MAtlasBytes,
                  MBuildMilliseconds);
}

void TextPreviewPanel::EnsureLocalization(VirtualFileSystem* InVfs)
{
    if (MLocalizationReady || InVfs == nullptr)
    {
        return;
    }
    const auto ScopedBytes = InVfs->Read("loc/ui.fbin");
    if (!ScopedBytes)
    {
        return;
    }
    MLocalization = LocalizationTable::Open(*ScopedBytes, nullptr);
    MLocalizationReady = MLocalization.IsValid();
}

void TextPreviewPanel::DrawShapedPreview(std::span<const Rhi::RenderItem> InItems)
{
    if (!ImGui::BeginChild("##sdf_preview", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders))
    {
        ImGui::EndChild();
        return;
    }
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    const ImVec2 ScopedOrigin = ImGui::GetCursorScreenPos();
    for (const Rhi::RenderItem& ScopedItem : InItems)
    {
        const Ui::FontAtlas* ScopedAtlas = MStack.Fonts().AtlasForLayer(ScopedItem.layer);
        if (ScopedAtlas == nullptr)
        {
            continue;
        }
        const std::span<const std::byte> ScopedPixels = ScopedAtlas->Pixels();
        const std::uint32_t ScopedWidth = ScopedAtlas->AtlasWidth();
        const std::uint32_t ScopedHeight = ScopedAtlas->AtlasHeight();
        if (ScopedWidth == 0 || ScopedHeight == 0 || ScopedPixels.empty())
        {
            continue;
        }
        const float ScopedStep = std::max(1.0f, std::min(ScopedItem.size.X, ScopedItem.size.Y) / 10.0f);
        for (float ScopedY = 0.0f; ScopedY < ScopedItem.size.Y; ScopedY += ScopedStep)
        {
            for (float ScopedX = 0.0f; ScopedX < ScopedItem.size.X; ScopedX += ScopedStep)
            {
                const float ScopedT = ScopedItem.size.X > 0.0f ? ScopedX / ScopedItem.size.X : 0.0f;
                const float ScopedV = ScopedItem.size.Y > 0.0f ? ScopedY / ScopedItem.size.Y : 0.0f;
                const float ScopedU = ScopedItem.uvMin.X + ScopedT * (ScopedItem.uvMax.X - ScopedItem.uvMin.X);
                const float ScopedW = ScopedItem.uvMin.Y + ScopedV * (ScopedItem.uvMax.Y - ScopedItem.uvMin.Y);
                const auto ScopedPixelX = static_cast<std::uint32_t>(std::clamp(
                    ScopedU * static_cast<float>(ScopedWidth), 0.0f, static_cast<float>(ScopedWidth - 1u)));
                const auto ScopedPixelY = static_cast<std::uint32_t>(std::clamp(
                    ScopedW * static_cast<float>(ScopedHeight), 0.0f, static_cast<float>(ScopedHeight - 1u)));
                const std::size_t ScopedOffset =
                    static_cast<std::size_t>(ScopedPixelY) * ScopedWidth + ScopedPixelX;
                if (ScopedOffset >= ScopedPixels.size())
                {
                    continue;
                }
                const float ScopedDistance = static_cast<float>(ScopedPixels[ScopedOffset]) / 255.0f;
                const float ScopedAlpha = std::clamp((ScopedDistance - 0.42f) / 0.16f, 0.0f, 1.0f);
                if (ScopedAlpha <= 0.0f)
                {
                    continue;
                }
                const float ScopedLeft = ScopedOrigin.x + ScopedItem.position.X - ScopedItem.size.X * 0.5f + ScopedX;
                const float ScopedTop = ScopedOrigin.y + ScopedItem.position.Y - ScopedItem.size.Y * 0.5f + ScopedY;
                ScopedDraw->AddRectFilled(ImVec2(ScopedLeft, ScopedTop),
                                          ImVec2(ScopedLeft + ScopedStep, ScopedTop + ScopedStep),
                                          IM_COL32(235, 235, 245, static_cast<int>(ScopedAlpha * 255.0f)));
            }
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 216.0f));
    ImGui::EndChild();
}

void TextPreviewPanel::Draw(VirtualFileSystem* InVfs, bool* InOpen)
{
    if (!BeginEditorPanel("Text Preview", InOpen))
    {
        EndEditorPanel();
        return;
    }
    EnsureLocalization(InVfs);
    if (MLocalizationReady)
    {
        const std::string ScopedLocalePreview(MLocalization.LocaleCode(static_cast<std::size_t>(MLocaleIndex)));
        if (ImGui::BeginCombo("Locale", ScopedLocalePreview.c_str()))
        {
            for (std::size_t ScopedIndex = 0; ScopedIndex < MLocalization.LocaleCount(); ++ScopedIndex)
            {
                const std::string ScopedLocale(MLocalization.LocaleCode(ScopedIndex));
                const bool ScopedSelected = static_cast<int>(ScopedIndex) == MLocaleIndex;
                if (ImGui::Selectable(ScopedLocale.c_str(), ScopedSelected))
                {
                    MLocaleIndex = static_cast<int>(ScopedIndex);
                    (void)MLocalization.SelectLocale(ScopedLocale);
                }
                if (ScopedSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputInt("Localization key", &MLocalizationKey);
        ImGui::InputInt("Plural count", &MPluralCount);
        const char* const ScopedGenders[] = {"Neutral", "Feminine", "Masculine"};
        (void)ImGui::Combo("Gender", &MGender, ScopedGenders, 3);
        if (ImGui::Button("Load localized string"))
        {
            const auto ScopedSelectedGender =
                static_cast<LocalizationGender>(std::clamp(MGender, 0, 2));
            const std::string_view ScopedLocalized = MLocalization.FindVariant(
                static_cast<std::uint32_t>(MLocalizationKey), MPluralCount, ScopedSelectedGender);
            if (!ScopedLocalized.empty())
            {
                std::snprintf(MText, sizeof(MText), "%.*s", static_cast<int>(ScopedLocalized.size()),
                              ScopedLocalized.data());
            }
        }
    }
    ImGui::InputTextMultiline("##text_preview", MText, sizeof(MText), ImVec2(-1.0f, 90.0f));
    const char* const ScopedDirections[] = {"Auto", "LTR", "RTL", "Vertical"};
    (void)ImGui::Combo("Direction", &MDirection, ScopedDirections, 4);
    const char* const ScopedScripts[] = {"Auto", "Latin", "Arabic", "Hebrew", "Devanagari", "Thai", "Han"};
    if (ImGui::Combo("Script", &MScript, ScopedScripts, 7) && MScript != 0 && MScript != 3)
    {
        const Text::TextScript ScopedWanted = SelectedScript();
        bool ScopedHasScript = false;
        std::size_t ScopedCursor = 0;
        const std::string_view ScopedText(MText);
        while (ScopedCursor < ScopedText.size() && !ScopedHasScript)
        {
            const std::uint32_t ScopedCodepoint = Text::DecodeTextUtf8(ScopedText, ScopedCursor);
            ScopedHasScript = Text::ScriptForCodepoint(ScopedCodepoint) == ScopedWanted;
        }
        if (!ScopedHasScript)
        {
            std::snprintf(MText, sizeof(MText), "%s", kScriptSamples[MScript]);
        }
    }
    (void)ImGui::Checkbox("Bold", &MBold);
    ImGui::SameLine();
    (void)ImGui::Checkbox("Italic", &MItalic);
    ImGui::SameLine();
    (void)ImGui::Checkbox("Mono", &MMono);
    ImGui::SameLine();
    (void)ImGui::Checkbox("Underline", &MUnderline);
    ImGui::SameLine();
    (void)ImGui::Checkbox("Strike", &MStrike);
    if (ImGui::SliderFloat("Font size", &MFontSize, 8.0f, 96.0f, "%.0f px"))
    {
        MBuiltKey = 0;
    }
    if (ImGui::Button("Rebuild atlases"))
    {
        MBuiltKey = 0;
    }
    if (!MStackReady || TextKey() != MBuiltKey)
    {
        const double ScopedNow = ImGui::GetTime();
        if (!MStackReady || ScopedNow - MBuiltSeconds > 0.25)
        {
            RebuildStack(InVfs);
        }
    }
    if (!MStackReady)
    {
        ImGui::TextDisabled("%s", MStatus[0] != 0 ? MStatus : "font is not ready");
        EndEditorPanel();
        return;
    }

    Text::TextShapeParams ScopedParams;
    ScopedParams.PixelSize = MFontSize;
    ScopedParams.Vertical = MDirection == 3;
    ScopedParams.Script = SelectedScript();
    ScopedParams.Direction = MDirection == 2 ? Text::TextDirection::RightToLeft
                                             : (MDirection == 3 ? Text::TextDirection::TopToBottom
                                                                : Text::TextDirection::Auto);
    const bool ScopedShaped = Text::ShapeBidiText(MStack.Shaper(), MText, ScopedParams, &MShaped);
    if (ScopedShaped)
    {
        // Paged faces start with page 0: warm the pages this run needs once.
        MPageBuilds = Text::EnsureShapedGlyphPages(MStack.Fonts(), MShaped);
    }
    std::array<Text::TextRun, Text::kMaxTextRuns> ScopedRuns{};
    (void)Text::SplitRtlRuns(MText, ScopedParams.Direction, std::span<Text::TextRun>(ScopedRuns), &MRunCount);
    Text::TextLayoutParams ScopedLayoutParams;
    ScopedLayoutParams.Vertical = ScopedParams.Vertical;
    ScopedLayoutParams.FontSize = MFontSize;
    ScopedLayoutParams.MaxWidth = MDirection == 3 ? 220.0f : 320.0f;
    Text::ShapedLayoutResult ScopedLayout;
    Text::TextRenderStats ScopedStats;
    MGlyphCount = MShaped.Count;
    MLineCount = 0;
    MBuiltItems = 0;
    MMissingGlyphs = 0;
    if (ScopedShaped && Text::LayoutShapedGlyphs(MText, MShaped, ScopedLayoutParams, &ScopedLayout))
    {
        MLineCount = ScopedLayout.Count;
        std::uint32_t ScopedFormat = 0;
        ScopedFormat |= MBold ? static_cast<std::uint32_t>(Text::TextStyleFlags::Bold) : 0u;
        ScopedFormat |= MItalic ? static_cast<std::uint32_t>(Text::TextStyleFlags::Italic) : 0u;
        ScopedFormat |= MMono ? static_cast<std::uint32_t>(Text::TextStyleFlags::Monospace) : 0u;
        ScopedFormat |= MUnderline ? static_cast<std::uint32_t>(Text::TextStyleFlags::Underline) : 0u;
        ScopedFormat |= MStrike ? static_cast<std::uint32_t>(Text::TextStyleFlags::Strike) : 0u;
        Text::TextStyle ScopedStyle;
        ScopedStyle.Flags = static_cast<std::uint16_t>(ScopedFormat);
        Text::TextSpan ScopedSpan;
        ScopedSpan.Begin = 0;
        ScopedSpan.End = static_cast<std::uint32_t>(std::strlen(MText));
        ScopedSpan.StyleIndex = 0;
        std::array<Text::TextSpan, 1> ScopedSpans{ScopedSpan};
        std::array<Text::TextStyle, 1> ScopedStyles{ScopedStyle};
        Text::TextRenderParams ScopedRenderParams;
        ScopedRenderParams.Fonts = &MStack.Fonts();
        ScopedRenderParams.FontAtlasIndex = 0;
        ScopedRenderParams.FontSize = MFontSize;
        ScopedRenderParams.Vertical = ScopedParams.Vertical;
        ScopedRenderParams.RightToLeft = ScopedParams.Direction == Text::TextDirection::RightToLeft;
        if (ScopedFormat != 0)
        {
            ScopedRenderParams.Spans = std::span<const Text::TextSpan>(ScopedSpans);
            ScopedRenderParams.Styles = std::span<const Text::TextStyle>(ScopedStyles);
        }
        MBuiltItems = Text::BuildShapedTextRenderItems(ScopedLayout, MShaped, Vector2D{0.0f, 0.0f},
                                                       ScopedRenderParams, std::span<Rhi::RenderItem>(MItems),
                                                       &ScopedStats);
        MMissingGlyphs = ScopedStats.MissingGlyphs;
    }
    ImGui::Separator();
    ImGui::Text("Glyphs: %u  runs: %u  lines: %u", MGlyphCount, MRunCount, MLineCount);
    ImGui::Text("SDF items: %u  missing glyphs: %u", MBuiltItems, MMissingGlyphs);
    MAtlasBytes = MStack.AtlasBytes();
    ImGui::Text("Atlas bytes: %u  pages: %u/%u  build: %.1f ms", MAtlasBytes, MPageBuilds, MStack.Fonts().PageLoads(),
                MBuildMilliseconds);
    for (const ScriptFace& ScopedFace : kScriptFaces)
    {
        const Ui::FontAtlas* ScopedAtlas = MStack.Fonts().FindByFontId(ScopedFace.FontId);
        ImGui::TextDisabled("%s id %u: %u glyphs, %u bytes", ScopedFace.Label, ScopedFace.FontId,
                            ScopedAtlas != nullptr ? ScopedAtlas->GlyphCount() : 0u,
                            ScopedAtlas != nullptr ? static_cast<std::uint32_t>(ScopedAtlas->Pixels().size()) : 0u);
    }
    ImGui::TextDisabled("%s", MStatus);
    DrawShapedPreview(std::span<const Rhi::RenderItem>(MItems.data(), MBuiltItems));
    if (ImGui::BeginChild("##runs", ImVec2(0.0f, 120.0f), ImGuiChildFlags_Borders))
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MRunCount; ++ScopedIndex)
        {
            const Text::TextRun& ScopedRun = ScopedRuns[ScopedIndex];
            ImGui::Text("run %u: bytes %u..%u level %u %s", ScopedIndex, ScopedRun.Begin, ScopedRun.End,
                        ScopedRun.BidiLevel,
                        ScopedRun.Direction == Text::TextDirection::RightToLeft ? "RTL" : "LTR");
        }
    }
    ImGui::EndChild();
    EndEditorPanel();
}
}
