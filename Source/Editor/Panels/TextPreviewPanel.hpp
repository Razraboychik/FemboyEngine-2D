// FemboyEngine2D - editor preview for the shaped Unicode pipeline.
#pragma once

#include "Engine/Assets/Localization.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextRender.hpp"
#include "Engine/Text/TextShaper.hpp"
#include "Engine/Text/TextShapedLayout.hpp"

#include <array>
#include <cstdint>

namespace Fby
{
class VirtualFileSystem;

namespace Editor
{

// Script preview panel. Every script is mounted as its own atlas, the shaper
// routes each bidi run to the face of its script, and the paint pass resolves
// each shaped glyph through ShapedGlyph.FontId. A script switch therefore
// changes the face and the atlas, not only the shaping parameters.
class TextPreviewPanel
{
public:
    TextPreviewPanel() = default;
    TextPreviewPanel(const TextPreviewPanel&) = delete;
    TextPreviewPanel& operator=(const TextPreviewPanel&) = delete;

    void Draw(VirtualFileSystem* InVfs, bool* InOpen);

private:
    // Builds or rebuilds the script stack for the current text and pixel size.
    void RebuildStack(VirtualFileSystem* InVfs);
    void EnsureLocalization(VirtualFileSystem* InVfs);
    [[nodiscard]] Text::TextScript SelectedScript() const noexcept;
    // Identity of the current text and pixel size, used to debounce rebuilds.
    [[nodiscard]] std::uint64_t TextKey() const noexcept;
    void DrawShapedPreview(std::span<const Rhi::RenderItem> InItems);

    Text::TextFontStack MStack;
    Text::TextShapeResult MShaped;
    std::array<Rhi::RenderItem, Text::kMaxShapedGlyphs> MItems{};
    char MText[1024] = "Hello, world! \u0645\u0631\u062d\u0628\u0627 \u4f60\u597d\uff0c\u4e16\u754c";
    int MDirection = 0;
    int MScript = 0;
    float MFontSize = 24.0f;
    LocalizationTable MLocalization;
    int MLocaleIndex = 0;
    int MLocalizationKey = 1001;
    int MPluralCount = 1;
    int MGender = 0;
    // Formatting demo: every flag is applied to the whole preview text as one
    // span, so the panel shows the family style slots and the decorations.
    bool MBold = false;
    bool MItalic = false;
    bool MMono = false;
    bool MUnderline = false;
    bool MStrike = false;
    std::uint64_t MBuiltKey = 0;
    double MBuiltSeconds = -1.0e9;
    std::uint32_t MBuiltItems = 0;
    std::uint32_t MMissingGlyphs = 0;
    std::uint32_t MGlyphCount = 0;
    std::uint32_t MRunCount = 0;
    std::uint32_t MLineCount = 0;
    std::uint32_t MAtlasBytes = 0;
    std::uint32_t MPageBuilds = 0;
    float MBuildMilliseconds = 0.0f;
    bool MLocalizationReady = false;
    bool MStackReady = false;
    char MStatus[192] = {};
};
}
}
