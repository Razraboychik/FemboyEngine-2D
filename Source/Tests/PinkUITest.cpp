// FemboyEngine2D - Pink UI tests.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/RegistryOps.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/Text/TextAsset.hpp"
#include "Engine/Text/TextRuntime.hpp"
#include "Engine/Text/TextService.hpp"
#include "Engine/Text/TextShapeCache.hpp"
#include "Engine/Text/TextShaper.hpp"
#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
std::atomic<std::size_t> GAllocations{0};
}

// Replaceable global allocation functions.
void* operator new(std::size_t InSize)
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    void* ScopedMemory = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedMemory == nullptr)
    {
        throw std::bad_alloc{};
    }
    return ScopedMemory;
}

void* operator new[](std::size_t InSize)
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    void* ScopedMemory = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedMemory == nullptr)
    {
        throw std::bad_alloc{};
    }
    return ScopedMemory;
}

void operator delete(void* InMemory) noexcept
{
    std::free(InMemory);
}

void operator delete[](void* InMemory) noexcept
{
    std::free(InMemory);
}

void operator delete(void* InMemory, std::size_t) noexcept
{
    std::free(InMemory);
}

void operator delete[](void* InMemory, std::size_t) noexcept
{
    std::free(InMemory);
}

namespace
{

// Test plumbing
using Fby::Color32;
using Fby::Rect;
using Fby::Vector2D;
using Fby::Ecs::SystemContext;
using Fby::Rhi::DrawBatch;
using Fby::Rhi::RenderItem;
using Fby::Rhi::RenderKey;
using Fby::Ui::BuildStatus;
using Fby::Ui::DefaultUiTheme;
using Fby::Ui::FocusPolicy;
using Fby::Ui::FrameOutput;
using Fby::Ui::MakeTextFlags;
using Fby::Ui::TextAlign;
using Fby::Ui::TextVerticalAlign;
using Fby::Ui::UiPaletteSlot;
using Fby::Ui::UiThemeAsset;
using Fby::Ui::WidgetCanvas;
using Fby::Ui::WidgetCanvasSystem;
using Fby::Ui::WidgetComponent;
using Fby::Ui::WidgetComponentFlags;
using Fby::Ui::WidgetDesc;
using Fby::Ui::WidgetFlags;
using Fby::Ui::WidgetKind;

constexpr float kTolerance = 1.0e-4f;
constexpr float kPi = 3.14159265358979323846f;

std::size_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

#define FBY_CHECK_NEAR(actualValue, expectedValue, tolerance)                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        const float MActualFloat = (actualValue);                                                                      \
        const float MExpectedFloat = (expectedValue);                                                                  \
        if (!(std::fabs(MActualFloat - MExpectedFloat) <= (tolerance)))                                                \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %.6f != %.6f\n", __FILE__, __LINE__, MActualFloat, MExpectedFloat);      \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

[[nodiscard]] Vector2D V(float InX, float InY) noexcept
{
    return {InX, InY};
}

[[nodiscard]] Rect R(Vector2D InMinimum, Vector2D InMaximum) noexcept
{
    return {InMinimum, InMaximum};
}

[[nodiscard]] bool Near(float InLeft, float InRight, float InTolerance = kTolerance) noexcept
{
    return std::fabs(InLeft - InRight) <= InTolerance;
}

[[nodiscard]] bool VecNear(Vector2D InLeft, Vector2D InRight, float InTolerance = kTolerance) noexcept
{
    return Near(InLeft.X, InRight.X, InTolerance) && Near(InLeft.Y, InRight.Y, InTolerance);
}

[[nodiscard]] bool RectNear(Rect InLeft, Rect InRight, float InTolerance = kTolerance) noexcept
{
    return VecNear(InLeft.min, InRight.min, InTolerance) && VecNear(InLeft.max, InRight.max, InTolerance);
}

[[nodiscard]] bool FactoryFlag(std::uint32_t InFlags, WidgetFlags InFlag) noexcept
{
    return Fby::Ui::HasFlag(InFlags, InFlag);
}

#define FBY_CHECK_VEC(actualValue, expectedValue)                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        const Vector2D MActualVec = (actualValue);                                                                     \
        const Vector2D MExpectedVec = (expectedValue);                                                                 \
        if (!VecNear(MActualVec, MExpectedVec))                                                                        \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: vec (%.4f, %.4f) != (%.4f, %.4f)\n", __FILE__, __LINE__, MActualVec.X,   \
                         MActualVec.Y, MExpectedVec.X, MExpectedVec.Y);                                                \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

#define FBY_CHECK_RECT(actualValue, expectedValue)                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        const Rect MActualRect = (actualValue);                                                                        \
        const Rect MExpectedRect = (expectedValue);                                                                    \
        if (!RectNear(MActualRect, MExpectedRect))                                                                     \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: rect (%.4f, %.4f)-(%.4f, %.4f) != (%.4f, %.4f)-(%.4f, %.4f)\n",          \
                         __FILE__, __LINE__, MActualRect.min.X, MActualRect.min.Y, MActualRect.max.X,                  \
                         MActualRect.max.Y, MExpectedRect.min.X, MExpectedRect.min.Y, MExpectedRect.max.X,             \
                         MExpectedRect.max.Y);                                                                         \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

// A widget whose anchors collapse to a point: the offsets are absolute
[[nodiscard]] WidgetDesc WidgetAt(Vector2D InMinimum, Vector2D InMaximum, WidgetKind InKind = WidgetKind::Panel,
                                  std::int32_t InZOrder = 0)
{
    WidgetDesc ScopedDesc;
    ScopedDesc.kind = InKind;
    ScopedDesc.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedDesc.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedDesc.offsetMin = InMinimum;
    ScopedDesc.offsetMax = InMaximum;
    ScopedDesc.zOrder = InZOrder;
    return ScopedDesc;
}

// A widget that fills its reference box (default anchors 0..1, zero insets).
[[nodiscard]] WidgetDesc StretchWidget(WidgetKind InKind = WidgetKind::Panel, std::int32_t InZOrder = 0)
{
    WidgetDesc ScopedDesc;
    ScopedDesc.kind = InKind;
    ScopedDesc.zOrder = InZOrder;
    return ScopedDesc;
}

// A focusable button used by the focus-order tests.
[[nodiscard]] std::int32_t AddButton(WidgetCanvas& InCanvas, float InMinX, float InMaxX, std::int32_t InTabIndex,
                                     std::uint16_t InGroup, FocusPolicy InPolicy)
{
    WidgetDesc ScopedDesc = WidgetAt(V(InMinX, 0.0f), V(InMaxX, 40.0f), WidgetKind::Button, 0);
    ScopedDesc.tabIndex = InTabIndex;
    ScopedDesc.focusGroup = InGroup;
    ScopedDesc.focusPolicy = InPolicy;
    return InCanvas.AddWidget(ScopedDesc);
}

// A focusable button with an explicit rect, used by the arrow-navigation test.
[[nodiscard]] std::int32_t AddFocusButton(WidgetCanvas& InCanvas, Vector2D InMinimum, Vector2D InMaximum,
                                          FocusPolicy InPolicy)
{
    WidgetDesc ScopedDesc = WidgetAt(InMinimum, InMaximum, WidgetKind::Button, 0);
    ScopedDesc.focusPolicy = InPolicy;
    return InCanvas.AddWidget(ScopedDesc);
}

// The canvas used by the packing and overflow tests. Five widgets, five kinds,
[[nodiscard]] WidgetCanvas MakeDemoCanvas()
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(320.0f, 200.0f));
    ScopedCanvas.SetSortLayer(3);

    WidgetDesc ScopedPanel = StretchWidget(WidgetKind::Panel, -5);
    ScopedPanel.tint = Color32::FromBytes(0x40, 0x40, 0x48);
    (void)ScopedCanvas.AddWidget(ScopedPanel);

    WidgetDesc ScopedButton = WidgetAt(V(0.0f, 0.0f), V(120.0f, 40.0f), WidgetKind::Button, 2);
    ScopedButton.tint = Fby::Ui::kPinkAccent;
    (void)ScopedCanvas.AddWidget(ScopedButton);

    WidgetDesc ScopedLabel = WidgetAt(V(10.0f, 60.0f), V(150.0f, 80.0f), WidgetKind::Label, 0);
    ScopedLabel.text = "Pink UI";
    ScopedLabel.fontSize = 8;
    ScopedLabel.tint = Color32::White();
    (void)ScopedCanvas.AddWidget(ScopedLabel);

    WidgetDesc ScopedImage = WidgetAt(V(200.0f, 60.0f), V(300.0f, 120.0f), WidgetKind::Image, 1);
    ScopedImage.layer = Fby::Ui::kWhiteLayer;
    ScopedImage.uvMin = V(0.25f, 0.25f);
    ScopedImage.uvMax = V(0.75f, 0.75f);
    (void)ScopedCanvas.AddWidget(ScopedImage);

    WidgetDesc ScopedProgress = WidgetAt(V(10.0f, 120.0f), V(200.0f, 140.0f), WidgetKind::ProgressBar, 1);
    ScopedProgress.tint = Color32::FromBytes(0x30, 0x30, 0x3C);
    ScopedProgress.fillTint = Color32::FromBytes(0xFF, 0x4F, 0xA8);
    ScopedProgress.progress = 0.5f;
    (void)ScopedCanvas.AddWidget(ScopedProgress);
    return ScopedCanvas;
}

// A cooked test atlas that mirrors the 16x6 grid GlyphUv() describes: one
[[nodiscard]] std::vector<std::byte> MakeDemoFontAtlas()
{
    using namespace Fby::Ui;
    constexpr std::uint32_t ScopedCellPx = 8;
    constexpr std::uint32_t ScopedWidth = kFontAtlasColumns * ScopedCellPx;
    constexpr std::uint32_t ScopedHeight = kFontAtlasRows * ScopedCellPx;
    const std::size_t ScopedGlyphBytes = static_cast<std::size_t>(kFontGlyphCount) * sizeof(GlyphRecord);
    const std::size_t ScopedPixelBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u;
    std::vector<std::byte> ScopedBytes(sizeof(FontAtlasHeader) + ScopedGlyphBytes + ScopedPixelBytes, std::byte{0});

    FontAtlasHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic));
    ScopedHeader.version = kFontAtlasVersion;
    ScopedHeader.headerBytes = sizeof(FontAtlasHeader);
    ScopedHeader.glyphCount = kFontGlyphCount;
    ScopedHeader.atlasWidth = ScopedWidth;
    ScopedHeader.atlasHeight = ScopedHeight;
    ScopedHeader.pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::Rgba8);
    ScopedHeader.glyphTableOffset = sizeof(FontAtlasHeader);
    ScopedHeader.pixelOffset = static_cast<std::uint32_t>(sizeof(FontAtlasHeader) + ScopedGlyphBytes);
    ScopedHeader.lineHeight = static_cast<float>(ScopedCellPx);
    ScopedHeader.ascent = static_cast<float>(ScopedCellPx);
    ScopedHeader.descent = 0.0f;
    ScopedHeader.fontId = 0;
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));

    std::array<GlyphRecord, kFontGlyphCount> ScopedRecords{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kFontGlyphCount; ++ScopedIndex)
    {
        GlyphRecord& ScopedRecord = ScopedRecords[ScopedIndex];
        const std::uint32_t ScopedColumn = ScopedIndex % kFontAtlasColumns;
        const std::uint32_t ScopedRow = ScopedIndex / kFontAtlasColumns;
        ScopedRecord.codepoint = kFontFirstChar + ScopedIndex;
        ScopedRecord.u0 = static_cast<float>(ScopedColumn) / static_cast<float>(kFontAtlasColumns);
        ScopedRecord.v0 = static_cast<float>(ScopedRow) / static_cast<float>(kFontAtlasRows);
        ScopedRecord.u1 = static_cast<float>(ScopedColumn + 1u) / static_cast<float>(kFontAtlasColumns);
        ScopedRecord.v1 = static_cast<float>(ScopedRow + 1u) / static_cast<float>(kFontAtlasRows);
        ScopedRecord.advance = static_cast<float>(ScopedCellPx) * kGlyphAdvanceRatio;
        ScopedRecord.bearingX = 0.0f;
        ScopedRecord.bearingY = static_cast<float>(ScopedCellPx);
        ScopedRecord.width = static_cast<float>(ScopedCellPx);
        ScopedRecord.height = static_cast<float>(ScopedCellPx);
        // The shaped path resolves by OpenType id; the demo atlas uses the
        // codepoint as its id so the CodepointShaper can drive it.
        ScopedRecord.glyphId = kFontFirstChar + ScopedIndex;
    }
    std::memcpy(ScopedBytes.data() + sizeof(FontAtlasHeader), ScopedRecords.data(), ScopedGlyphBytes);

    std::byte* ScopedPixels = ScopedBytes.data() + ScopedHeader.pixelOffset;
    for (std::size_t ScopedTexel = 0; ScopedTexel < static_cast<std::size_t>(ScopedWidth) * ScopedHeight; ++ScopedTexel)
    {
        ScopedPixels[ScopedTexel * 4 + 0] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4 + 1] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4 + 2] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4 + 3] = std::byte{0xFF};
    }
    return ScopedBytes;
}

// 1. Data model contract
void TestDataModel()
{
    static_assert(sizeof(RenderItem) == 48, "Pink UI item packing assumes the 48-byte engine item");
    static_assert(offsetof(RenderItem, position) == 0);
    static_assert(offsetof(RenderItem, uvMin) == 16);
    static_assert(offsetof(RenderItem, color) == 36);
    static_assert(offsetof(RenderItem, flags) == 44);
    // DrawBatch is four 32-bit fields and 16 bytes: no uint16 resource ids, no
    static_assert(sizeof(DrawBatch) == 16);
    static_assert(sizeof(DrawBatch::pipeline) == 4);
    static_assert(sizeof(DrawBatch::textureArray) == 4);
    static_assert(sizeof(WidgetCanvas::textureArray) == 4);
    static_assert(sizeof(WidgetCanvas::pipeline) == 4);

    static_assert(std::is_trivially_copyable_v<WidgetComponent>);
    static_assert(std::is_standard_layout_v<WidgetComponent>);
    static_assert(sizeof(WidgetComponent) == 16);
    static_assert(std::is_trivially_copyable_v<WidgetCanvas>);
    static_assert(std::is_standard_layout_v<WidgetCanvas>);
    static_assert(std::is_trivially_copyable_v<WidgetDesc>);
    static_assert(std::is_trivially_copyable_v<Fby::Ui::WidgetHandle>);
    static_assert(sizeof(Fby::Ui::WidgetHandle) == 8);
    static_assert(std::is_trivially_copyable_v<FrameOutput>);
    static_assert(std::is_trivially_copyable_v<Fby::Ui::WidgetCanvas::LayoutCache>);

    // The capacity constants are part of the module contract, not tuning knobs.
    static_assert(Fby::Ui::kMaxWidgets == 512);
    static_assert(Fby::Ui::kMaxGlyphs == 4096);
    static_assert(Fby::Ui::kMaxTextPool == 8192);
    static_assert(Fby::Ui::kMaxChildrenPerWidget == 32);
    static_assert(Fby::Ui::kMaxGridColumns == 16);
    static_assert(Fby::Ui::kMaxGridRows == 32);
    static_assert(Fby::Ui::kMaxCustomQuadsPerWidget == 64);
    static_assert(static_cast<std::uint32_t>(Fby::Ui::WidgetKind::Count) == 20);

    // A default canvas is empty, dirty and root-relative.
    const WidgetCanvas ScopedCanvas;
    FBY_CHECK(ScopedCanvas.widgetCount == 0);
    FBY_CHECK(ScopedCanvas.textUsed == 0);
    FBY_CHECK(!ScopedCanvas.LayoutValid());
    FBY_CHECK(ScopedCanvas.focusIndex == Fby::Ui::kNoWidget);
    FBY_CHECK(ScopedCanvas.worldScale.X == 1.0f && ScopedCanvas.worldScale.Y == 1.0f);

    // WidgetComponent is Mirror-registered with the four primitive fields.
    constexpr auto ScopedDescription = Fby::Reflect::Describe<WidgetComponent>();
    FBY_CHECK(ScopedDescription.name == "WidgetComponent");
    FBY_CHECK(ScopedDescription.size == sizeof(WidgetComponent));
    FBY_CHECK(ScopedDescription.fields.size() == 4);
    FBY_CHECK(ScopedDescription.fields[0].name == "CanvasID");
    FBY_CHECK(ScopedDescription.fields[0].offset == offsetof(WidgetComponent, CanvasID));
    FBY_CHECK(ScopedDescription.fields[1].name == "Layer");
    FBY_CHECK(ScopedDescription.fields[2].name == "Flags");
    FBY_CHECK(ScopedDescription.fields[3].name == "AnimationTime");
    FBY_CHECK(ScopedDescription.fields[3].kind == Fby::Reflect::FieldKind::Float);
    FBY_CHECK(Fby::Reflect::IsRegistered<WidgetComponent>());
    FBY_CHECK(Fby::Reflect::LayoutHash<WidgetComponent>() != 0);
}

// 2. mdspan grid view
void TestGridView()
{
    WidgetCanvas ScopedCanvas;
    const std::array<float, 3> ScopedWidths{100.0f, 50.0f, 150.0f};
    const std::array<float, 2> ScopedHeights{40.0f, -1.0f};
    FBY_CHECK(ScopedCanvas.SetGrid(3, 2, std::span<const float>(ScopedWidths), std::span<const float>(ScopedHeights),
                                   V(10.0f, 5.0f), V(20.0f, 15.0f)));
    FBY_CHECK(ScopedCanvas.gridColumnCount == 3);
    FBY_CHECK(ScopedCanvas.gridRowCount == 2);

    // A real 2D view: row 0 is the column tracks, row 1 the row tracks, and a
    const Fby::Ui::GridView ScopedGrid = ScopedCanvas.Grid();
    FBY_CHECK(ScopedGrid.extent(0) == 2);
    FBY_CHECK(ScopedGrid.extent(1) == WidgetCanvas::kTrackCapacity);
    const float ScopedColumn0 = ScopedGrid[0, 0];
    const float ScopedColumn1 = ScopedGrid[0, 1];
    const float ScopedColumn2 = ScopedGrid[0, 2];
    const float ScopedRow0 = ScopedGrid[1, 0];
    const float ScopedAutoRow = ScopedGrid[1, 1];
    FBY_CHECK_NEAR(ScopedColumn0, 100.0f, 0.0f);
    FBY_CHECK_NEAR(ScopedColumn1, 50.0f, 0.0f);
    FBY_CHECK_NEAR(ScopedColumn2, 150.0f, 0.0f);
    FBY_CHECK_NEAR(ScopedRow0, 40.0f, 0.0f);
    FBY_CHECK_NEAR(ScopedAutoRow, -1.0f, 0.0f);
#if FBY_UI_HAS_MDSPAN
    static_assert(std::is_same_v<Fby::Ui::GridView, std::mdspan<const float, std::dextents<std::size_t, 2>>>,
                  "the grid description must be a real std::mdspan when the toolchain has it");
#endif

    // Invalid grids are rejected without touching the previous configuration.
    FBY_CHECK(!ScopedCanvas.SetGrid(3, 2, std::span<const float>(ScopedWidths).first(2),
                                    std::span<const float>(ScopedHeights), {}, {}));
    FBY_CHECK(!ScopedCanvas.SetGrid(Fby::Ui::kMaxGridColumns + 1, 0, {}, {}, {}, {}));
    FBY_CHECK(ScopedCanvas.gridColumnCount == 3);
    const float ScopedStillColumn2 = ScopedCanvas.Grid()[0, 2];
    FBY_CHECK_NEAR(ScopedStillColumn2, 150.0f, 0.0f);
}

// 3. Grid layout: cells, spans, gaps, padding, auto rows
void TestGridLayout()
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(400.0f, 300.0f));
    const std::array<float, 3> ScopedWidths{100.0f, 50.0f, 150.0f};
    // Row 1 has an auto track: its height comes from the tallest explicit child.
    const std::array<float, 3> ScopedHeights{40.0f, -1.0f, 60.0f};
    FBY_CHECK(ScopedCanvas.SetGrid(3, 3, std::span<const float>(ScopedWidths), std::span<const float>(ScopedHeights),
                                   V(10.0f, 5.0f), V(20.0f, 15.0f)));

    // Column offsets: 0, 110, 170, 330. Row offsets after the auto pass:
    WidgetDesc ScopedCellA = StretchWidget(WidgetKind::Panel);
    ScopedCellA.gridRow = 0;
    ScopedCellA.gridColumn = 0;
    const std::int32_t ScopedIndexA = ScopedCanvas.AddWidget(ScopedCellA);
    FBY_CHECK(ScopedIndexA == 0);

    WidgetDesc ScopedCellB = StretchWidget(WidgetKind::Panel);
    ScopedCellB.gridRow = 0;
    ScopedCellB.gridColumn = 2;
    const std::int32_t ScopedIndexB = ScopedCanvas.AddWidget(ScopedCellB);

    // The only explicit-height child of row 1: stretched across the cell's
    WidgetDesc ScopedCellTall = WidgetAt(V(0.0f, 0.0f), V(0.0f, 30.0f), WidgetKind::Panel);
    ScopedCellTall.anchorMax = V(1.0f, 0.0f);
    ScopedCellTall.gridRow = 1;
    ScopedCellTall.gridColumn = 2;
    const std::int32_t ScopedIndexTall = ScopedCanvas.AddWidget(ScopedCellTall);

    WidgetDesc ScopedCellC = StretchWidget(WidgetKind::Panel);
    ScopedCellC.gridRow = 2;
    ScopedCellC.gridColumn = 1;
    const std::int32_t ScopedIndexC = ScopedCanvas.AddWidget(ScopedCellC);

    WidgetDesc ScopedCellSpan = StretchWidget(WidgetKind::Panel);
    ScopedCellSpan.gridRow = 1;
    ScopedCellSpan.gridColumn = 0;
    ScopedCellSpan.rowSpan = 2;
    ScopedCellSpan.columnSpan = 2;
    const std::int32_t ScopedIndexSpan = ScopedCanvas.AddWidget(ScopedCellSpan);

    FBY_CHECK(ScopedIndexB >= 0);
    FBY_CHECK(ScopedIndexTall >= 0);
    FBY_CHECK(ScopedIndexC >= 0);
    FBY_CHECK(ScopedIndexSpan >= 0);

    // Padding 20/15, gap 10/5, one-cell placement.
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedIndexA), R(V(20.0f, 15.0f), V(120.0f, 55.0f)));
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedIndexB), R(V(190.0f, 15.0f), V(340.0f, 55.0f)));
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedIndexTall), R(V(190.0f, 60.0f), V(340.0f, 90.0f)));
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedIndexC), R(V(130.0f, 95.0f), V(180.0f, 155.0f)));
    // 2x2 span: both tracks plus one gap on each axis.
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedIndexSpan), R(V(20.0f, 60.0f), V(180.0f, 155.0f)));

    // The authored grid is untouched by the auto resolution.
    const float ScopedAuthoredAutoRow = ScopedCanvas.Grid()[1, 1];
    FBY_CHECK_NEAR(ScopedAuthoredAutoRow, -1.0f, 0.0f);

    // Cells are validated at insertion: out-of-grid, oversized span, a cell on
    WidgetDesc ScopedOutOfGrid = StretchWidget();
    ScopedOutOfGrid.gridRow = 0;
    ScopedOutOfGrid.gridColumn = 3;
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedOutOfGrid) == Fby::Ui::kNoWidget);

    WidgetDesc ScopedOversized = StretchWidget();
    ScopedOversized.gridRow = 0;
    ScopedOversized.gridColumn = 2;
    ScopedOversized.columnSpan = 2;
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedOversized) == Fby::Ui::kNoWidget);

    WidgetDesc ScopedParentedCell = StretchWidget();
    ScopedParentedCell.parent = ScopedIndexA;
    ScopedParentedCell.gridRow = 0;
    ScopedParentedCell.gridColumn = 0;
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedParentedCell) == Fby::Ui::kNoWidget);

    WidgetDesc ScopedHalfCell = StretchWidget();
    ScopedHalfCell.gridRow = 0;
    ScopedHalfCell.gridColumn = Fby::Ui::kNoCell;
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedHalfCell) == Fby::Ui::kNoWidget);

    WidgetDesc ScopedZeroSpan = StretchWidget();
    ScopedZeroSpan.gridRow = 0;
    ScopedZeroSpan.gridColumn = 0;
    ScopedZeroSpan.rowSpan = 0;
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedZeroSpan) == Fby::Ui::kNoWidget);

    // A cell placed on a canvas without a configured grid is rejected too.
    WidgetCanvas ScopedNoGrid;
    WidgetDesc ScopedCellWithoutGrid = StretchWidget();
    ScopedCellWithoutGrid.gridRow = 0;
    ScopedCellWithoutGrid.gridColumn = 0;
    FBY_CHECK(ScopedNoGrid.AddWidget(ScopedCellWithoutGrid) == Fby::Ui::kNoWidget);
}

// 4. Anchor resolution inside parents and nested parent rects
void TestAnchorsAndNesting()
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(400.0f, 300.0f));

    const std::int32_t ScopedPanel = ScopedCanvas.AddWidget(StretchWidget(WidgetKind::Panel));
    FBY_CHECK(ScopedPanel == 0);
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedPanel), R(V(0.0f, 0.0f), V(400.0f, 300.0f)));

    // Point anchors: offsets are an absolute rect inside the parent.
    WidgetDesc ScopedFixed = WidgetAt(V(10.0f, 10.0f), V(60.0f, 30.0f));
    ScopedFixed.parent = ScopedPanel;
    const std::int32_t ScopedFixedWidget = ScopedCanvas.AddWidget(ScopedFixed);
    FBY_CHECK(ScopedFixedWidget == 1);
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedFixedWidget), R(V(10.0f, 10.0f), V(60.0f, 30.0f)));

    // Stretched anchors: the right half of the parent.
    WidgetDesc ScopedHalfRight = StretchWidget(WidgetKind::Panel);
    ScopedHalfRight.parent = ScopedPanel;
    ScopedHalfRight.anchorMin = V(0.5f, 0.0f);
    ScopedHalfRight.anchorMax = V(1.0f, 1.0f);
    const std::int32_t ScopedHalfRightWidget = ScopedCanvas.AddWidget(ScopedHalfRight);
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedHalfRightWidget), R(V(200.0f, 0.0f), V(400.0f, 300.0f)));

    // Nested: a grandchild resolves against its parent's resolved rect, not
    WidgetDesc ScopedGrandchild = WidgetAt(V(5.0f, 5.0f), V(15.0f, 10.0f));
    ScopedGrandchild.parent = ScopedFixedWidget;
    const std::int32_t ScopedGrandchildWidget = ScopedCanvas.AddWidget(ScopedGrandchild);
    FBY_CHECK(ScopedGrandchildWidget == 3);
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedGrandchildWidget), R(V(15.0f, 15.0f), V(25.0f, 20.0f)));

    // UGUI-style insets on a stretched anchor: negative offsetMax pulls the
    WidgetDesc ScopedInset = StretchWidget(WidgetKind::Panel);
    ScopedInset.parent = ScopedPanel;
    ScopedInset.offsetMin = V(5.0f, 5.0f);
    ScopedInset.offsetMax = V(-5.0f, -5.0f);
    const std::int32_t ScopedInsetWidget = ScopedCanvas.AddWidget(ScopedInset);
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedInsetWidget), R(V(5.0f, 5.0f), V(395.0f, 295.0f)));

    // Hierarchy links.
    FBY_CHECK(ScopedCanvas.Parent(ScopedFixedWidget) == ScopedPanel);
    FBY_CHECK(ScopedCanvas.ChildCount(ScopedPanel) == 3);
    FBY_CHECK(ScopedCanvas.Parent(ScopedGrandchildWidget) == ScopedFixedWidget);
    FBY_CHECK(ScopedCanvas.ChildCount(ScopedFixedWidget) == 1);
    FBY_CHECK(ScopedCanvas.Parent(ScopedPanel) == Fby::Ui::kNoWidget);

    // A parent must already exist; future/huge indices are rejected.
    WidgetDesc ScopedOrphan = StretchWidget();
    ScopedOrphan.parent = 99;
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedOrphan) == Fby::Ui::kNoWidget);

    // Moving the parent's anchors re-resolves the whole subtree on the next
    ScopedCanvas.SetAnchors(ScopedPanel, V(0.0f, 0.0f), V(0.5f, 0.5f));
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedPanel), R(V(0.0f, 0.0f), V(200.0f, 150.0f)));
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedFixedWidget), R(V(10.0f, 10.0f), V(60.0f, 30.0f)));
    FBY_CHECK_RECT(ScopedCanvas.LocalRect(ScopedGrandchildWidget), R(V(15.0f, 15.0f), V(25.0f, 20.0f)));
}

// 5. Hit test order and occlusion
void TestHitTestOrder()
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(200.0f, 200.0f));

    WidgetDesc ScopedBase = StretchWidget(WidgetKind::Panel, -1);
    const std::int32_t ScopedBaseWidget = ScopedCanvas.AddWidget(ScopedBase);

    WidgetDesc ScopedLower = WidgetAt(V(0.0f, 0.0f), V(100.0f, 100.0f), WidgetKind::Button, 0);
    const std::int32_t ScopedLowerWidget = ScopedCanvas.AddWidget(ScopedLower);

    WidgetDesc ScopedUpper = WidgetAt(V(0.0f, 0.0f), V(100.0f, 100.0f), WidgetKind::Panel, 2);
    const std::int32_t ScopedUpperWidget = ScopedCanvas.AddWidget(ScopedUpper);

    WidgetDesc ScopedSpacer = WidgetAt(V(0.0f, 0.0f), V(100.0f, 100.0f), WidgetKind::Spacer, 5);
    const std::int32_t ScopedSpacerWidget = ScopedCanvas.AddWidget(ScopedSpacer);
    FBY_CHECK(ScopedSpacerWidget >= 0);

    // Topmost hit wins, and the Spacer above everything is skipped.
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == ScopedUpperWidget);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(150.0f, 150.0f)) == ScopedBaseWidget);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(-1.0f, 50.0f)) == Fby::Ui::kNoWidget);

    // IgnoreHitTest drops the widget out of the hit chain.
    ScopedCanvas.SetFlag(ScopedUpperWidget, WidgetFlags::IgnoreHitTest, true);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == ScopedLowerWidget);
    ScopedCanvas.SetFlag(ScopedUpperWidget, WidgetFlags::IgnoreHitTest, false);

    // Invisible widgets are not hit either.
    ScopedCanvas.SetFlag(ScopedUpperWidget, WidgetFlags::Visible, false);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == ScopedLowerWidget);
    ScopedCanvas.SetFlag(ScopedUpperWidget, WidgetFlags::Visible, true);

    // A Spacer never steals a click from a panel behind it.
    WidgetCanvas ScopedSpacerOnly;
    ScopedSpacerOnly.SetCanvasSize(V(100.0f, 100.0f));
    const std::int32_t ScopedBehind = ScopedSpacerOnly.AddWidget(StretchWidget(WidgetKind::Panel, -1));
    const std::int32_t ScopedBlank = ScopedSpacerOnly.AddWidget(StretchWidget(WidgetKind::Spacer, 1));
    FBY_CHECK(ScopedBlank >= 0);
    FBY_CHECK(Fby::Ui::HitTest(ScopedSpacerOnly, V(50.0f, 50.0f)) == ScopedBehind);

    // Equal z-order: the later inserted widget is on top (stable draw order).
    WidgetCanvas ScopedTied;
    ScopedTied.SetCanvasSize(V(100.0f, 100.0f));
    const std::int32_t ScopedFirst = ScopedTied.AddWidget(StretchWidget(WidgetKind::Panel, 0));
    const std::int32_t ScopedSecond = ScopedTied.AddWidget(StretchWidget(WidgetKind::Panel, 0));
    FBY_CHECK(Fby::Ui::HitTest(ScopedTied, V(10.0f, 10.0f)) == ScopedSecond);
    FBY_CHECK(ScopedTied.SetZOrder(ScopedFirst, 1));
    FBY_CHECK(Fby::Ui::HitTest(ScopedTied, V(10.0f, 10.0f)) == ScopedFirst);
}

// 6. Pointer press and the opt-in focus contract
void TestPointerAndFocus()
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(200.0f, 100.0f));

    const std::int32_t ScopedLeft =
        ScopedCanvas.AddWidget(WidgetAt(V(0.0f, 0.0f), V(100.0f, 50.0f), WidgetKind::Button, 0));
    const std::int32_t ScopedRight =
        ScopedCanvas.AddWidget(WidgetAt(V(100.0f, 0.0f), V(200.0f, 50.0f), WidgetKind::Button, 0));

    // A Button is pointer-interactive but keyboard-inert until its policy opts in.
    FBY_CHECK(!FactoryFlag(ScopedCanvas.Flags(ScopedLeft), WidgetFlags::Focusable));
    FBY_CHECK(ScopedCanvas.FocusPolicyOf(ScopedLeft) == FocusPolicy::None);
    FBY_CHECK(!ScopedCanvas.CanFocus(ScopedLeft));
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedLeft), WidgetFlags::Enabled));
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == Fby::Ui::kNoWidget);

    // Press: hit + pressed bit, but no focus for a None widget.
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(50.0f, 25.0f)) == ScopedLeft);
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedLeft), WidgetFlags::Pressed));
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(50.0f, 25.0f)) == ScopedLeft);

    // Pointer policy: a click focuses.
    ScopedCanvas.SetFocusPolicy(ScopedLeft, FocusPolicy::Pointer);
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedLeft), WidgetFlags::Focusable));
    FBY_CHECK(ScopedCanvas.CanFocus(ScopedLeft));
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(50.0f, 25.0f)) == ScopedLeft);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == ScopedLeft);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(50.0f, 25.0f)) == ScopedLeft);
    Fby::Ui::SetFocus(ScopedCanvas, Fby::Ui::kNoWidget);

    // Tab policy: a click does not focus, but Tab traversal does.
    ScopedCanvas.SetFocusPolicy(ScopedRight, FocusPolicy::Tab);
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(150.0f, 25.0f)) == ScopedRight);
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedRight), WidgetFlags::Pressed));
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(150.0f, 25.0f)) == ScopedRight);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedRight);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == ScopedRight);

    // Release away from the pressed button: no click, press bit still clears.
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(50.0f, 25.0f)) == ScopedLeft);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(150.0f, 25.0f)) == Fby::Ui::kNoWidget);
    FBY_CHECK(!FactoryFlag(ScopedCanvas.Flags(ScopedLeft), WidgetFlags::Pressed));

    // A disabled button is still hit-testable but never focuses or presses.
    ScopedCanvas.SetFocusPolicy(ScopedLeft, FocusPolicy::TabAndPointer);
    ScopedCanvas.SetFlag(ScopedLeft, WidgetFlags::Enabled, false);
    Fby::Ui::SetFocus(ScopedCanvas, Fby::Ui::kNoWidget);
    FBY_CHECK(!ScopedCanvas.CanFocus(ScopedLeft));
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(50.0f, 25.0f)) == ScopedLeft);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == Fby::Ui::kNoWidget);
    FBY_CHECK(!FactoryFlag(ScopedCanvas.Flags(ScopedLeft), WidgetFlags::Pressed));
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(50.0f, 25.0f)) == Fby::Ui::kNoWidget);
    // A disabled widget is skipped by Tab traversal.
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedRight);

    // Any kind can opt in, including a Panel; the default stays inert.
    WidgetCanvas ScopedPanels;
    ScopedPanels.SetCanvasSize(V(100.0f, 100.0f));
    const std::int32_t ScopedPanel = ScopedPanels.AddWidget(StretchWidget(WidgetKind::Panel));
    FBY_CHECK(Fby::Ui::PointerDown(ScopedPanels, V(50.0f, 50.0f)) == ScopedPanel);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedPanels) == Fby::Ui::kNoWidget);
    ScopedPanels.SetFocusPolicy(ScopedPanel, FocusPolicy::TabAndPointer);
    FBY_CHECK(ScopedPanels.CanFocus(ScopedPanel));
    FBY_CHECK(Fby::Ui::PointerDown(ScopedPanels, V(50.0f, 50.0f)) == ScopedPanel);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedPanels) == ScopedPanel);

    // SetFocus validates the index: a stale/out-of-range id clears to nothing.
    Fby::Ui::SetFocus(ScopedCanvas, ScopedRight);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == ScopedRight);
    Fby::Ui::SetFocus(ScopedCanvas, 999);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == ScopedRight);
    Fby::Ui::SetFocus(ScopedCanvas, Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == Fby::Ui::kNoWidget);
}

// 7. Embedded font and text measurement
void TestFontAndText()
{
    // The space glyph is blank; 'A' has ink; anything outside 32..126 maps to
    const std::array<std::uint8_t, 8> ScopedSpace = Fby::Ui::GlyphBitmap(' ');
    std::uint32_t ScopedSpaceInk = 0;
    for (const std::uint8_t ScopedRow : ScopedSpace)
    {
        ScopedSpaceInk += ScopedRow;
    }
    FBY_CHECK(ScopedSpaceInk == 0);

    const std::array<std::uint8_t, 8> ScopedLetterA = Fby::Ui::GlyphBitmap('A');
    std::uint32_t ScopedLetterAInk = 0;
    for (const std::uint8_t ScopedRow : ScopedLetterA)
    {
        ScopedLetterAInk += ScopedRow;
    }
    FBY_CHECK(ScopedLetterAInk != 0);

    const std::array<std::uint8_t, 8> ScopedFallback = Fby::Ui::GlyphBitmap(static_cast<char>(1));
    const std::array<std::uint8_t, 8> ScopedQuestion = Fby::Ui::GlyphBitmap('?');
    FBY_CHECK(ScopedFallback == ScopedQuestion);

    // UV mapping: glyph index 33 ('A') is column 1, row 2 of 16x6 cells.
    const Rect ScopedUvA = Fby::Ui::GlyphUv('A');
    FBY_CHECK_NEAR(ScopedUvA.min.X, 1.0f / 16.0f, 1.0e-6f);
    FBY_CHECK_NEAR(ScopedUvA.min.Y, 2.0f / 6.0f, 1.0e-6f);
    FBY_CHECK_NEAR(ScopedUvA.max.X, 2.0f / 16.0f, 1.0e-6f);
    FBY_CHECK_NEAR(ScopedUvA.max.Y, 3.0f / 6.0f, 1.0e-6f);

    // Every printable ASCII glyph maps into the atlas.
    for (std::uint32_t ScopedCode = Fby::Ui::kFontFirstChar; ScopedCode <= Fby::Ui::kFontLastChar; ++ScopedCode)
    {
        const Rect ScopedUv = Fby::Ui::GlyphUv(static_cast<char>(ScopedCode));
        FBY_CHECK(ScopedUv.min.X >= 0.0f && ScopedUv.max.X <= 1.0f && ScopedUv.min.Y >= 0.0f && ScopedUv.max.Y <= 1.0f);
        FBY_CHECK(ScopedUv.min.X < ScopedUv.max.X && ScopedUv.min.Y < ScopedUv.max.Y);
    }

    // Measurement: 0.75 advance per glyph, fontSize per line.
    FBY_CHECK_VEC(Fby::Ui::MeasureText("Hi", 8), V(12.0f, 8.0f));
    FBY_CHECK_VEC(Fby::Ui::MeasureText("", 8), V(0.0f, 0.0f));
    FBY_CHECK_VEC(Fby::Ui::MeasureText("A\nBC", 8), V(12.0f, 16.0f));
    // 0 => kDefaultFontSize 12
    FBY_CHECK_VEC(Fby::Ui::MeasureText("A", 0), V(9.0f, 12.0f));
    FBY_CHECK(Fby::Ui::CountGlyphs("A\nBC") == 3);
    FBY_CHECK(Fby::Ui::CountGlyphs("") == 0);
}

// 8. BuildFrame packing: one batch, ascending keys, exact stats
void TestBuildFramePacking(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas = MakeDemoCanvas();

    std::array<RenderItem, 64> ScopedItems{};
    std::array<RenderKey, 64> ScopedKeys{};
    std::array<DrawBatch, 8> ScopedBatches{};
    // Sentinel everything so "wrote only the requested range" is observable.
    for (RenderItem& ScopedItem : ScopedItems)
    {
        ScopedItem.color = 0xDEADBEEFu;
    }
    ScopedKeys.fill(0u);

    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    // A caller that already packed three items
    ScopedOutput.startCursor = 3;

    const BuildStatus ScopedStatus = WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput);
    FBY_CHECK(ScopedStatus == BuildStatus::Ok);
    FBY_CHECK(!ScopedOutput.overflow);

    // 1 panel + 1 button + 7 glyphs + 1 image + 2 progress quads = 12 items.
    FBY_CHECK(ScopedOutput.requiredItems == 12);
    FBY_CHECK(ScopedOutput.itemsWritten == 12);
    FBY_CHECK(ScopedOutput.glyphQuads == 7);
    FBY_CHECK(ScopedOutput.widgetsDrawn == 5);
    // BuildFrame does not move the caller's cursor
    FBY_CHECK(ScopedOutput.startCursor == 3);

    // Exactly one batch for the whole canvas, covering the written range. Text
    FBY_CHECK(ScopedOutput.batchesWritten == 1);
    FBY_CHECK(ScopedBatches[0].firstItem == 3);
    FBY_CHECK(ScopedBatches[0].itemCount == 12);
    FBY_CHECK(ScopedBatches[0].pipeline == ScopedCanvas.pipeline);
    FBY_CHECK(ScopedBatches[0].textureArray == InContext.fonts->TextureArray().id);

    // The prefix and the tail were not touched.
    FBY_CHECK(ScopedItems[0].color == 0xDEADBEEFu);
    FBY_CHECK(ScopedItems[2].color == 0xDEADBEEFu);
    FBY_CHECK(ScopedItems[15].color == 0xDEADBEEFu);
    FBY_CHECK(ScopedKeys[0] == 0u);
    FBY_CHECK(ScopedKeys[2] == 0u);

    // Draw order is (zOrder, insertion): panel(-5), label(0), image(1),
    const std::span<const std::uint16_t> ScopedOrder = ScopedCanvas.DrawOrder();
    FBY_CHECK(ScopedOrder.size() == 5);
    FBY_CHECK(ScopedOrder[0] == 0);
    FBY_CHECK(ScopedOrder[1] == 2);
    FBY_CHECK(ScopedOrder[2] == 3);
    FBY_CHECK(ScopedOrder[3] == 4);
    FBY_CHECK(ScopedOrder[4] == 1);
    for (std::uint32_t ScopedIndex = 4; ScopedIndex < 15; ++ScopedIndex)
    {
        FBY_CHECK(ScopedKeys[ScopedIndex] > ScopedKeys[ScopedIndex - 1]);
        FBY_CHECK((ScopedKeys[ScopedIndex] >> 48) == ScopedCanvas.sortLayer);
    }

    // Panel: full canvas, white layer, tint colour.
    FBY_CHECK_VEC(ScopedItems[3].position, V(160.0f, 100.0f));
    FBY_CHECK_VEC(ScopedItems[3].size, V(320.0f, 200.0f));
    FBY_CHECK(ScopedItems[3].layer == Fby::Ui::kWhiteLayer);
    FBY_CHECK(ScopedItems[3].color == ScopedCanvas.tints[0].rgba);
    FBY_CHECK_NEAR(ScopedItems[3].rotation, 0.0f, 0.0f);

    // Label: one quad per glyph, in the font atlas layer, laid out from the
    FBY_CHECK(ScopedItems[4].layer == Fby::Ui::kFontAtlasLayer);
    FBY_CHECK_VEC(ScopedItems[4].position, V(14.0f, 64.0f));
    FBY_CHECK_VEC(ScopedItems[4].size, V(8.0f, 8.0f));
    const Rect ScopedUvP = Fby::Ui::GlyphUv('P');
    FBY_CHECK_VEC(ScopedItems[4].uvMin, ScopedUvP.min);
    FBY_CHECK_VEC(ScopedItems[4].uvMax, ScopedUvP.max);
    // Seventh glyph: 'I'
    FBY_CHECK_VEC(ScopedItems[10].position, V(50.0f, 64.0f));
    FBY_CHECK_VEC(ScopedItems[10].uvMin, Fby::Ui::GlyphUv('I').min);
    for (std::uint32_t ScopedIndex = 4; ScopedIndex < 11; ++ScopedIndex)
    {
        FBY_CHECK(ScopedItems[ScopedIndex].layer == Fby::Ui::kFontAtlasLayer);
        FBY_CHECK_NEAR(ScopedItems[ScopedIndex].size.X, 8.0f, 0.0f);
        FBY_CHECK_NEAR(ScopedItems[ScopedIndex].size.Y, 8.0f, 0.0f);
    }

    // Image: the source rect is preserved; the layer is the font array's white
    FBY_CHECK_VEC(ScopedItems[11].position, V(250.0f, 90.0f));
    FBY_CHECK_VEC(ScopedItems[11].size, V(100.0f, 60.0f));
    FBY_CHECK(ScopedItems[11].layer == Fby::Ui::kWhiteLayer);
    FBY_CHECK_VEC(ScopedItems[11].uvMin, V(0.25f, 0.25f));
    FBY_CHECK_VEC(ScopedItems[11].uvMax, V(0.75f, 0.75f));

    // Progress bar: track quad then half-width fill quad.
    FBY_CHECK_VEC(ScopedItems[12].position, V(105.0f, 130.0f));
    FBY_CHECK_VEC(ScopedItems[12].size, V(190.0f, 20.0f));
    FBY_CHECK(ScopedItems[12].color == ScopedCanvas.tints[4].rgba);
    FBY_CHECK_VEC(ScopedItems[13].position, V(57.5f, 130.0f));
    FBY_CHECK_VEC(ScopedItems[13].size, V(95.0f, 20.0f));
    FBY_CHECK(ScopedItems[13].color == ScopedCanvas.fillTints[4].rgba);

    // Button: last because of zOrder 2.
    FBY_CHECK_VEC(ScopedItems[14].position, V(60.0f, 20.0f));
    FBY_CHECK_VEC(ScopedItems[14].size, V(120.0f, 40.0f));
    FBY_CHECK(ScopedItems[14].color == ScopedCanvas.tints[1].rgba);

    // Canvas world placement: position/scale/rotation are baked into every
    WidgetCanvas ScopedWorldCanvas;
    ScopedWorldCanvas.SetCanvasSize(V(100.0f, 50.0f));
    (void)ScopedWorldCanvas.AddWidget(StretchWidget(WidgetKind::Panel));
    ScopedWorldCanvas.SetWorldTransform(V(100.0f, 200.0f), V(2.0f, 2.0f), 0.0f);
    std::array<RenderItem, 4> ScopedWorldItems{};
    std::array<RenderKey, 4> ScopedWorldKeys{};
    std::array<DrawBatch, 2> ScopedWorldBatches{};
    FrameOutput ScopedWorldOutput;
    ScopedWorldOutput.items = std::span<RenderItem>(ScopedWorldItems);
    ScopedWorldOutput.keys = std::span<RenderKey>(ScopedWorldKeys);
    ScopedWorldOutput.batches = std::span<DrawBatch>(ScopedWorldBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedWorldCanvas, ScopedWorldOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedWorldOutput.itemsWritten == 1);
    FBY_CHECK_VEC(ScopedWorldItems[0].position, V(200.0f, 250.0f));
    FBY_CHECK_VEC(ScopedWorldItems[0].size, V(200.0f, 100.0f));

    ScopedWorldCanvas.SetWorldTransform(V(10.0f, 20.0f), V(1.0f, 1.0f), kPi * 0.5f);
    ScopedWorldOutput = FrameOutput{};
    ScopedWorldOutput.items = std::span<RenderItem>(ScopedWorldItems);
    ScopedWorldOutput.keys = std::span<RenderKey>(ScopedWorldKeys);
    ScopedWorldOutput.batches = std::span<DrawBatch>(ScopedWorldBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedWorldCanvas, ScopedWorldOutput) == BuildStatus::Ok);
    // Centre rotated 90 degrees
    FBY_CHECK_VEC(ScopedWorldItems[0].position, V(-15.0f, 70.0f));
    FBY_CHECK_NEAR(ScopedWorldItems[0].rotation, kPi * 0.5f, 1.0e-5f);

    // A canvas without text may target a foreign texture array; the 32-bit id
    WidgetCanvas ScopedImageCanvas;
    ScopedImageCanvas.SetCanvasSize(V(64.0f, 64.0f));
    ScopedImageCanvas.SetTextureArray(0x00010001u);
    WidgetDesc ScopedOnlyImage = WidgetAt(V(0.0f, 0.0f), V(64.0f, 64.0f), WidgetKind::Image, 0);
    ScopedOnlyImage.layer = 5;
    (void)ScopedImageCanvas.AddWidget(ScopedOnlyImage);
    std::array<RenderItem, 2> ScopedImageItems{};
    std::array<RenderKey, 2> ScopedImageKeys{};
    std::array<DrawBatch, 1> ScopedImageBatches{};
    FrameOutput ScopedImageOutput;
    ScopedImageOutput.items = std::span<RenderItem>(ScopedImageItems);
    ScopedImageOutput.keys = std::span<RenderKey>(ScopedImageKeys);
    ScopedImageOutput.batches = std::span<DrawBatch>(ScopedImageBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedImageCanvas, ScopedImageOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedImageOutput.itemsWritten == 1);
    FBY_CHECK(ScopedImageOutput.fontTextureArray == 0xFFFFFFFFu);
    FBY_CHECK(ScopedImageBatches[0].textureArray == 0x00010001u);
    FBY_CHECK(ScopedImageItems[0].layer == 5);
}

// 9. Capacity overflow: clean result, nothing written
void TestCapacityOverflow(SystemContext& InContext)
{
    {
        WidgetCanvas ScopedCanvas = MakeDemoCanvas();
        std::array<RenderItem, 4> ScopedItems{};
        std::array<RenderKey, 4> ScopedKeys{};
        std::array<DrawBatch, 2> ScopedBatches{};
        for (RenderItem& ScopedItem : ScopedItems)
        {
            ScopedItem.color = 0xDEADBEEFu;
        }
        FrameOutput ScopedOutput;
        ScopedOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Overflow);
        FBY_CHECK(ScopedOutput.overflow);
        FBY_CHECK(ScopedOutput.requiredItems == 12);
        FBY_CHECK(ScopedOutput.itemsWritten == 0);
        FBY_CHECK(ScopedOutput.batchesWritten == 0);
        FBY_CHECK(ScopedItems[0].color == 0xDEADBEEFu);
    }

    {
        WidgetCanvas ScopedCanvas = MakeDemoCanvas();
        std::array<RenderItem, 64> ScopedItems{};
        std::array<RenderKey, 4> ScopedKeys{};
        std::array<DrawBatch, 2> ScopedBatches{};
        FrameOutput ScopedOutput;
        ScopedOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Overflow);
        FBY_CHECK(ScopedOutput.requiredItems == 12);
        FBY_CHECK(ScopedOutput.itemsWritten == 0);
    }

    {
        WidgetCanvas ScopedCanvas = MakeDemoCanvas();
        std::array<RenderItem, 64> ScopedItems{};
        std::array<RenderKey, 64> ScopedKeys{};
        FrameOutput ScopedOutput;
        ScopedOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedOutput.batches = {};
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Overflow);
        FBY_CHECK(ScopedOutput.requiredItems == 12);
        FBY_CHECK(ScopedOutput.itemsWritten == 0);
    }

    {
        WidgetCanvas ScopedCanvas = MakeDemoCanvas();
        std::array<RenderItem, 64> ScopedItems{};
        std::array<RenderKey, 64> ScopedKeys{};
        std::array<DrawBatch, 2> ScopedBatches{};
        FrameOutput ScopedOutput;
        ScopedOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
        ScopedOutput.startCursor = 100;
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Overflow);
    }

    {
        WidgetCanvas ScopedCanvas;
        ScopedCanvas.SetCanvasSize(V(100.0f, 100.0f));
        std::array<char, Fby::Ui::kMaxGlyphs + 1> ScopedText{};
        ScopedText.fill('A');
        WidgetDesc ScopedLabel = StretchWidget(WidgetKind::Label);
        ScopedLabel.text = std::string_view(ScopedText.data(), ScopedText.size());
        FBY_CHECK(ScopedCanvas.AddWidget(ScopedLabel) == 0);

        std::array<RenderItem, 1> ScopedItems{};
        std::array<RenderKey, 1> ScopedKeys{};
        std::array<DrawBatch, 1> ScopedBatches{};
        FrameOutput ScopedOutput;
        ScopedOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Overflow);
        FBY_CHECK(ScopedOutput.overflow);
        FBY_CHECK(ScopedOutput.glyphQuads == Fby::Ui::kMaxGlyphs + 1);
        FBY_CHECK(ScopedOutput.requiredItems == Fby::Ui::kMaxGlyphs + 1);
        FBY_CHECK(ScopedOutput.itemsWritten == 0);
    }

    {
        WidgetCanvas ScopedFull;
        ScopedFull.SetCanvasSize(V(100.0f, 100.0f));
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < Fby::Ui::kMaxWidgets; ++ScopedIndex)
        {
            const std::int32_t ScopedAdded = ScopedFull.AddWidget(StretchWidget(WidgetKind::Panel));
            if (ScopedAdded != static_cast<std::int32_t>(ScopedIndex))
            {
                FBY_CHECK(false);
                break;
            }
        }
        FBY_CHECK(ScopedFull.widgetCount == Fby::Ui::kMaxWidgets);
        FBY_CHECK(ScopedFull.AddWidget(StretchWidget(WidgetKind::Panel)) == Fby::Ui::kNoWidget);

        // 32 children fit, the 33rd is refused.
        WidgetCanvas ScopedTree;
        ScopedTree.SetCanvasSize(V(100.0f, 100.0f));
        const std::int32_t ScopedParent = ScopedTree.AddWidget(StretchWidget(WidgetKind::Panel));
        std::uint32_t ScopedAddedChildren = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < Fby::Ui::kMaxChildrenPerWidget; ++ScopedIndex)
        {
            WidgetDesc ScopedChild = StretchWidget(WidgetKind::Panel);
            ScopedChild.parent = ScopedParent;
            if (ScopedTree.AddWidget(ScopedChild) < 0)
            {
                FBY_CHECK(false);
                break;
            }
            ++ScopedAddedChildren;
        }
        FBY_CHECK(ScopedAddedChildren == Fby::Ui::kMaxChildrenPerWidget);
        FBY_CHECK(ScopedTree.ChildCount(ScopedParent) == Fby::Ui::kMaxChildrenPerWidget);
        WidgetDesc ScopedExtraChild = StretchWidget(WidgetKind::Panel);
        ScopedExtraChild.parent = ScopedParent;
        FBY_CHECK(ScopedTree.AddWidget(ScopedExtraChild) == Fby::Ui::kNoWidget);

        // z-order outside the documented range is refused, not clamped.
        WidgetCanvas ScopedZCanvas;
        ScopedZCanvas.SetCanvasSize(V(10.0f, 10.0f));
        WidgetDesc ScopedTooHigh = StretchWidget();
        ScopedTooHigh.zOrder = Fby::Ui::kMaxZOrder + 1;
        FBY_CHECK(ScopedZCanvas.AddWidget(ScopedTooHigh) == Fby::Ui::kNoWidget);
        WidgetDesc ScopedTooLow = StretchWidget();
        ScopedTooLow.zOrder = Fby::Ui::kMinZOrder - 1;
        FBY_CHECK(ScopedZCanvas.AddWidget(ScopedTooLow) == Fby::Ui::kNoWidget);
        WidgetDesc ScopedEdge = StretchWidget();
        ScopedEdge.zOrder = Fby::Ui::kMaxZOrder;
        FBY_CHECK(ScopedZCanvas.AddWidget(ScopedEdge) == 0);
        FBY_CHECK(!ScopedZCanvas.SetZOrder(0, Fby::Ui::kMinZOrder - 1));

        // Text pool overflow is refused at insertion and at SetText.
        WidgetCanvas ScopedPool;
        ScopedPool.SetCanvasSize(V(10.0f, 10.0f));
        WidgetDesc ScopedOkText = StretchWidget(WidgetKind::Label);
        ScopedOkText.text = "ok";
        const std::int32_t ScopedLabel = ScopedPool.AddWidget(ScopedOkText);
        FBY_CHECK(ScopedLabel == 0);
        std::array<char, Fby::Ui::kMaxTextPool> ScopedHuge{};
        ScopedHuge.fill('x');
        FBY_CHECK(!ScopedPool.SetText(ScopedLabel, std::string_view(ScopedHuge.data(), ScopedHuge.size())));
        FBY_CHECK(ScopedPool.Text(ScopedLabel) == "ok");
        FBY_CHECK(ScopedPool.AddWidget(WidgetDesc{}) >= 0);
    }

    {
        WidgetCanvas ScopedCanvas = MakeDemoCanvas();
        std::array<RenderItem, 64> ScopedItems{};
        std::array<RenderKey, 64> ScopedKeys{};
        std::array<DrawBatch, 4> ScopedBatches{};
        FrameOutput ScopedOutput;
        ScopedOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
        // Only four slots left for a twelve-item canvas
        ScopedOutput.startCursor = 60;
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Overflow);
        FBY_CHECK(ScopedOutput.requiredItems == 12);
        ScopedOutput = FrameOutput{};
        ScopedOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
        ScopedOutput.startCursor = 0;
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
        FBY_CHECK(ScopedOutput.itemsWritten == 12);
    }
}

// 10. No allocation on the frame path
struct AllocationScope
{
    AllocationScope() noexcept : start(GAllocations.load(std::memory_order_relaxed))
    {
    }

    [[nodiscard]] std::size_t Count() const noexcept
    {
        return GAllocations.load(std::memory_order_relaxed) - start;
    }

    std::size_t start = 0;
};

void TestNoAllocation(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas = MakeDemoCanvas();
    ScopedCanvas.SetWorldTransform(V(10.0f, 20.0f), V(1.5f, 1.5f), 0.25f);

    std::array<RenderItem, 64> ScopedItems{};
    std::array<RenderKey, 64> ScopedKeys{};
    std::array<DrawBatch, 4> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);

    // First call: layout cache is cold, so the layout pass and the draw-order
    {
        const AllocationScope ScopedScope;
        const BuildStatus ScopedStatus = WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput);
        const std::size_t ScopedAllocations = ScopedScope.Count();
        FBY_CHECK(ScopedStatus == BuildStatus::Ok);
        FBY_CHECK(ScopedAllocations == 0);
    }

    // Second call: warm cache, pure packing.
    {
        const AllocationScope ScopedScope;
        (void)WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput);
        const std::size_t ScopedAllocations = ScopedScope.Count();
        FBY_CHECK(ScopedAllocations == 0);
    }

    // The new element kinds take the same allocation-free path.
    WidgetCanvas ScopedElements;
    ScopedElements.SetCanvasSize(V(100.0f, 100.0f));
    WidgetDesc ScopedSlider = WidgetAt(V(0.0f, 0.0f), V(100.0f, 20.0f), WidgetKind::Slider);
    ScopedSlider.progress = 0.5f;
    (void)ScopedElements.AddWidget(ScopedSlider);
    (void)ScopedElements.AddWidget(WidgetAt(V(0.0f, 30.0f), V(20.0f, 50.0f), WidgetKind::Checkbox));
    (void)ScopedElements.AddWidget(WidgetAt(V(30.0f, 30.0f), V(70.0f, 70.0f), WidgetKind::Border));
    (void)ScopedElements.AddWidget(WidgetAt(V(80.0f, 30.0f), V(100.0f, 100.0f), WidgetKind::Scrollbar));
    WidgetDesc ScopedCaption = WidgetAt(V(0.0f, 80.0f), V(60.0f, 100.0f), WidgetKind::Button);
    ScopedCaption.text = "Go";
    (void)ScopedElements.AddWidget(ScopedCaption);
    for (std::uint32_t ScopedPass = 0; ScopedPass < 2; ++ScopedPass)
    {
        const AllocationScope ScopedScope;
        FrameOutput ScopedElementOutput;
        ScopedElementOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedElementOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedElementOutput.batches = std::span<DrawBatch>(ScopedBatches);
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedElements, ScopedElementOutput) == BuildStatus::Ok);
        FBY_CHECK(ScopedScope.Count() == 0);
    }

    // Hit testing, focus changes, text measurement and grid reads are all
    {
        const AllocationScope ScopedScope;
        const std::int32_t ScopedHit = Fby::Ui::HitTest(ScopedCanvas, V(60.0f, 20.0f));
        Fby::Ui::SetFocus(ScopedCanvas, ScopedHit);
        (void)Fby::Ui::FocusedWidget(ScopedCanvas);
        const Fby::Ui::GridView ScopedGrid = ScopedCanvas.Grid();
        const float ScopedFirstTrack = ScopedGrid[0, 0];
        (void)ScopedFirstTrack;
        const Vector2D ScopedMeasured = Fby::Ui::MeasureText("Pink UI text measurement", 12);
        const std::size_t ScopedAllocations = ScopedScope.Count();
        FBY_CHECK(ScopedAllocations == 0);
        FBY_CHECK(ScopedHit >= 0);
        FBY_CHECK(ScopedMeasured.X > 0.0f);
    }
}

// 11. ECS-facing Update: Transform2D placement and batch accumulation
void TestEcsUpdate(SystemContext& InContext)
{
    Fby::Scene::DestroyAllEntities(InContext.registry);

    std::array<WidgetCanvas, 2> ScopedCanvases{};
    for (WidgetCanvas& ScopedCanvas : ScopedCanvases)
    {
        ScopedCanvas.SetCanvasSize(V(100.0f, 50.0f));
        (void)ScopedCanvas.AddWidget(StretchWidget(WidgetKind::Panel));
    }
    (void)ScopedCanvases[1].AddWidget(StretchWidget(WidgetKind::Panel, 1));

    // Entity 0: canvas 0, rotated 90 degrees, scaled 1, moved to (100, 50).
    const Fby::Entity ScopedEntity0 = InContext.registry.create();
    InContext.registry.emplace<Fby::Transform2D>(ScopedEntity0, V(100.0f, 50.0f), V(1.0f, 1.0f), kPi * 0.5f);
    WidgetComponent ScopedComponent0;
    ScopedComponent0.CanvasID = 0;
    ScopedComponent0.Layer = 7;
    ScopedComponent0.Flags = static_cast<std::uint32_t>(WidgetComponentFlags::Visible);
    InContext.registry.emplace<WidgetComponent>(ScopedEntity0, ScopedComponent0);

    // Entity 1: canvas 1 (two quads), identity orientation at (10, 20).
    const Fby::Entity ScopedEntity1 = InContext.registry.create();
    InContext.registry.emplace<Fby::Transform2D>(ScopedEntity1, V(10.0f, 20.0f), V(1.0f, 1.0f), 0.0f);
    WidgetComponent ScopedComponent1;
    ScopedComponent1.CanvasID = 1;
    ScopedComponent1.Layer = 7;
    ScopedComponent1.Flags = static_cast<std::uint32_t>(WidgetComponentFlags::Visible);
    InContext.registry.emplace<WidgetComponent>(ScopedEntity1, ScopedComponent1);

    // Entity 2: invisible, must not contribute.
    const Fby::Entity ScopedEntity2 = InContext.registry.create();
    InContext.registry.emplace<Fby::Transform2D>(ScopedEntity2, V(999.0f, 999.0f), V(1.0f, 1.0f), 0.0f);
    WidgetComponent ScopedComponent2;
    ScopedComponent2.CanvasID = 0;
    ScopedComponent2.Flags = 0;
    InContext.registry.emplace<WidgetComponent>(ScopedEntity2, ScopedComponent2);

    // Entity 3: dangling canvas id, must be skipped instead of reading memory.
    const Fby::Entity ScopedEntity3 = InContext.registry.create();
    InContext.registry.emplace<Fby::Transform2D>(ScopedEntity3, V(0.0f, 0.0f), V(1.0f, 1.0f), 0.0f);
    WidgetComponent ScopedComponent3;
    ScopedComponent3.CanvasID = 99;
    ScopedComponent3.Flags = static_cast<std::uint32_t>(WidgetComponentFlags::Visible);
    InContext.registry.emplace<WidgetComponent>(ScopedEntity3, ScopedComponent3);

    std::array<RenderItem, 32> ScopedItems{};
    std::array<RenderKey, 32> ScopedKeys{};
    std::array<DrawBatch, 8> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);

    WidgetCanvasSystem::Update(InContext, std::span<WidgetCanvas>(ScopedCanvases), ScopedOutput);
    FBY_CHECK(!ScopedOutput.overflow);
    // 1 from canvas 0 + 2 from canvas 1
    FBY_CHECK(ScopedOutput.itemsWritten == 3);
    FBY_CHECK(ScopedOutput.batchesWritten == 2);
    FBY_CHECK(ScopedOutput.widgetsDrawn == 3);

    // The two batches partition the written range contiguously, whatever order
    FBY_CHECK(ScopedBatches[0].firstItem == 0);
    FBY_CHECK(ScopedBatches[1].firstItem == ScopedBatches[0].itemCount);
    FBY_CHECK(ScopedBatches[0].itemCount + ScopedBatches[1].itemCount == 3);
    FBY_CHECK(ScopedOutput.startCursor == 3);

    // Every item carries the component layer in the key's top 16 bits.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedOutput.itemsWritten; ++ScopedIndex)
    {
        FBY_CHECK((ScopedKeys[ScopedIndex] >> 48) == 7);
    }

    // Rotated canvas: the local centre (50, 25) rotates to (-25, 50) around the
    bool ScopedFoundRotated = false;
    bool ScopedFoundPlaced = false;
    bool ScopedFoundGhost = false;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedOutput.itemsWritten; ++ScopedIndex)
    {
        if (VecNear(ScopedItems[ScopedIndex].position, V(75.0f, 100.0f), 1.0e-3f))
        {
            ScopedFoundRotated = true;
            FBY_CHECK_VEC(ScopedItems[ScopedIndex].size, V(100.0f, 50.0f));
            FBY_CHECK_NEAR(ScopedItems[ScopedIndex].rotation, kPi * 0.5f, 1.0e-5f);
        }
        if (VecNear(ScopedItems[ScopedIndex].position, V(60.0f, 45.0f), 1.0e-3f))
        {
            ScopedFoundPlaced = true;
        }
        if (VecNear(ScopedItems[ScopedIndex].position, V(999.0f, 999.0f), 1.0f))
        {
            ScopedFoundGhost = true;
        }
    }
    FBY_CHECK(ScopedFoundRotated);
    FBY_CHECK(ScopedFoundPlaced);
    FBY_CHECK(!ScopedFoundGhost);
}

// 12. Trivially copyable canvas: a memcpy copy keeps its resolved layout
void TestCanvasCopy()
{
    WidgetCanvas ScopedOriginal = MakeDemoCanvas();
    // Force the layout cache to be filled, then copy through memcpy (the test
    (void)ScopedOriginal.LocalRect(0);
    FBY_CHECK(ScopedOriginal.LayoutValid());

    WidgetCanvas ScopedCopy;
    std::memcpy(&ScopedCopy, &ScopedOriginal, sizeof(WidgetCanvas));
    FBY_CHECK(ScopedCopy.LayoutValid());
    FBY_CHECK(ScopedCopy.widgetCount == ScopedOriginal.widgetCount);
    FBY_CHECK_RECT(ScopedCopy.LocalRect(0), ScopedOriginal.LocalRect(0));
    FBY_CHECK_RECT(ScopedCopy.LocalRect(2), ScopedOriginal.LocalRect(2));
    FBY_CHECK(ScopedCopy.Text(2) == "Pink UI");

    // The copy shares nothing with the original.
    (void)ScopedOriginal.SetText(2, "Changed");
    ScopedOriginal.SetProgress(4, 1.0f);
    FBY_CHECK(ScopedCopy.Text(2) == "Pink UI");
    FBY_CHECK_NEAR(ScopedCopy.progresses[4], 0.5f, 0.0f);
}

// 13. Border, Slider, Checkbox, Scrollbar and a centred Button caption
void TestElementKinds(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(200.0f, 100.0f));

    WidgetDesc ScopedBorder = WidgetAt(V(0.0f, 0.0f), V(40.0f, 40.0f), WidgetKind::Border, 0);
    ScopedBorder.borderWidth = 2.0f;
    ScopedBorder.tint = Color32::FromBytes(0x10, 0x20, 0x30);
    const std::int32_t ScopedBorderId = ScopedCanvas.AddWidget(ScopedBorder);

    WidgetDesc ScopedSlider = WidgetAt(V(0.0f, 50.0f), V(100.0f, 70.0f), WidgetKind::Slider, 0);
    ScopedSlider.progress = 0.5f;
    ScopedSlider.knobSize = 10.0f;
    ScopedSlider.tint = Color32::FromBytes(0x20, 0x20, 0x20);
    ScopedSlider.fillTint = Color32::FromBytes(0xF0, 0x40, 0x80);
    const std::int32_t ScopedSliderId = ScopedCanvas.AddWidget(ScopedSlider);

    WidgetDesc ScopedCheckbox = WidgetAt(V(120.0f, 50.0f), V(140.0f, 70.0f), WidgetKind::Checkbox, 0);
    ScopedCheckbox.flags |= static_cast<std::uint32_t>(WidgetFlags::Checked);
    ScopedCheckbox.tint = Color32::FromBytes(0x30, 0x30, 0x30);
    ScopedCheckbox.fillTint = Color32::FromBytes(0x40, 0xF0, 0x80);
    const std::int32_t ScopedCheckboxId = ScopedCanvas.AddWidget(ScopedCheckbox);

    WidgetDesc ScopedScrollbar = WidgetAt(V(160.0f, 0.0f), V(180.0f, 100.0f), WidgetKind::Scrollbar, 0);
    ScopedScrollbar.ratio = 0.5f;
    ScopedScrollbar.progress = 0.25f;
    ScopedScrollbar.flags |= static_cast<std::uint32_t>(WidgetFlags::Vertical);
    ScopedScrollbar.tint = Color32::FromBytes(0x50, 0x50, 0x50);
    ScopedScrollbar.fillTint = Color32::FromBytes(0x80, 0xF0, 0x40);
    const std::int32_t ScopedScrollbarId = ScopedCanvas.AddWidget(ScopedScrollbar);

    WidgetDesc ScopedButton = WidgetAt(V(0.0f, 80.0f), V(60.0f, 100.0f), WidgetKind::Button, 0);
    ScopedButton.text = "OK";
    ScopedButton.fontSize = 8;
    ScopedButton.textTint = Color32::FromBytes(0x12, 0x34, 0x56);
    const std::int32_t ScopedButtonId = ScopedCanvas.AddWidget(ScopedButton);

    FBY_CHECK(ScopedBorderId == 0);
    FBY_CHECK(ScopedSliderId == 1);
    FBY_CHECK(ScopedCheckboxId == 2);
    FBY_CHECK(ScopedScrollbarId == 3);
    FBY_CHECK(ScopedButtonId == 4);
    FBY_CHECK(ScopedCanvas.Checked(ScopedCheckboxId));
    FBY_CHECK(!ScopedCanvas.Checked(ScopedButtonId));
    // Interactive kinds are keyboard-inert until a policy opts in.
    FBY_CHECK(!FactoryFlag(ScopedCanvas.Flags(ScopedSliderId), WidgetFlags::Focusable));
    FBY_CHECK(!FactoryFlag(ScopedCanvas.Flags(ScopedCheckboxId), WidgetFlags::Focusable));
    FBY_CHECK(!FactoryFlag(ScopedCanvas.Flags(ScopedScrollbarId), WidgetFlags::Focusable));
    FBY_CHECK(ScopedCanvas.FocusPolicyOf(ScopedSliderId) == FocusPolicy::None);
    FBY_CHECK(!ScopedCanvas.CanFocus(ScopedCheckboxId));
    ScopedCanvas.SetFocusPolicy(ScopedCheckboxId, FocusPolicy::TabAndPointer);
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedCheckboxId), WidgetFlags::Focusable));
    FBY_CHECK(ScopedCanvas.CanFocus(ScopedCheckboxId));

    std::array<RenderItem, 32> ScopedItems{};
    std::array<RenderKey, 32> ScopedKeys{};
    std::array<DrawBatch, 2> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);

    // 4 border + 3 slider + 3 checked checkbox + 2 scrollbar + 1 button + 2 glyphs
    FBY_CHECK(ScopedOutput.requiredItems == 15);
    FBY_CHECK(ScopedOutput.itemsWritten == 15);
    FBY_CHECK(ScopedOutput.glyphQuads == 2);
    FBY_CHECK(ScopedOutput.widgetsDrawn == 5);
    for (std::uint32_t ScopedIndex = 1; ScopedIndex < ScopedOutput.itemsWritten; ++ScopedIndex)
    {
        FBY_CHECK(ScopedKeys[ScopedIndex] > ScopedKeys[ScopedIndex - 1]);
    }

    // Border: top, bottom, left, right. The stroke is 2 Units on every side.
    FBY_CHECK_VEC(ScopedItems[0].position, V(20.0f, 1.0f));
    FBY_CHECK_VEC(ScopedItems[0].size, V(40.0f, 2.0f));
    FBY_CHECK(ScopedItems[0].color == ScopedBorder.tint.rgba);
    FBY_CHECK_VEC(ScopedItems[1].position, V(20.0f, 39.0f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(40.0f, 2.0f));
    FBY_CHECK_VEC(ScopedItems[2].position, V(1.0f, 20.0f));
    FBY_CHECK_VEC(ScopedItems[2].size, V(2.0f, 36.0f));
    FBY_CHECK_VEC(ScopedItems[3].position, V(39.0f, 20.0f));
    FBY_CHECK_VEC(ScopedItems[3].size, V(2.0f, 36.0f));

    // Slider: track, then a fill that stops at the knob centre, then the knob.
    FBY_CHECK_VEC(ScopedItems[4].position, V(50.0f, 60.0f));
    FBY_CHECK_VEC(ScopedItems[4].size, V(100.0f, 20.0f));
    FBY_CHECK(ScopedItems[4].color == ScopedSlider.tint.rgba);
    FBY_CHECK_VEC(ScopedItems[5].position, V(25.0f, 60.0f));
    FBY_CHECK_VEC(ScopedItems[5].size, V(50.0f, 20.0f));
    FBY_CHECK(ScopedItems[5].color == ScopedSlider.fillTint.rgba);
    FBY_CHECK_VEC(ScopedItems[6].position, V(50.0f, 60.0f));
    FBY_CHECK_VEC(ScopedItems[6].size, V(10.0f, 20.0f));

    // Checkbox: face plus two rotated mark bars.
    FBY_CHECK_VEC(ScopedItems[7].position, V(130.0f, 60.0f));
    FBY_CHECK_VEC(ScopedItems[7].size, V(20.0f, 20.0f));
    FBY_CHECK_NEAR(ScopedItems[8].size.Y, 2.0f, kTolerance);
    FBY_CHECK_NEAR(ScopedItems[9].size.Y, 2.0f, kTolerance);
    FBY_CHECK(std::abs(ScopedItems[8].rotation) > 0.1f);
    FBY_CHECK(std::abs(ScopedItems[9].rotation) > 0.1f);
    FBY_CHECK(ScopedItems[8].color == ScopedCheckbox.fillTint.rgba);
    FBY_CHECK(ScopedItems[9].color == ScopedCheckbox.fillTint.rgba);

    // Scrollbar: a 50-pixel thumb at 25 percent of the 50-pixel travel.
    FBY_CHECK_VEC(ScopedItems[10].position, V(170.0f, 50.0f));
    FBY_CHECK_VEC(ScopedItems[10].size, V(20.0f, 100.0f));
    FBY_CHECK_VEC(ScopedItems[11].position, V(170.0f, 62.5f));
    FBY_CHECK_VEC(ScopedItems[11].size, V(20.0f, 50.0f));
    FBY_CHECK(ScopedItems[11].color == ScopedScrollbar.fillTint.rgba);

    // Button: the face, then two glyphs centred inside it.
    FBY_CHECK_VEC(ScopedItems[12].position, V(30.0f, 90.0f));
    FBY_CHECK_VEC(ScopedItems[12].size, V(60.0f, 20.0f));
    FBY_CHECK_VEC(ScopedItems[13].position, V(28.0f, 90.0f));
    FBY_CHECK_VEC(ScopedItems[14].position, V(34.0f, 90.0f));
    FBY_CHECK(ScopedItems[13].layer == Fby::Ui::kFontAtlasLayer);
    FBY_CHECK(ScopedItems[14].layer == Fby::Ui::kFontAtlasLayer);
    FBY_CHECK(ScopedItems[13].color == ScopedButton.textTint.rgba);
    FBY_CHECK(ScopedItems[14].color == ScopedButton.textTint.rgba);
}

// 14. Slider and Scrollbar dragging, hover bits and the disabled contract
void TestSliderDrag()
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(100.0f, 20.0f));
    WidgetDesc ScopedDesc = StretchWidget(WidgetKind::Slider);
    ScopedDesc.knobSize = 10.0f;
    ScopedDesc.focusPolicy = FocusPolicy::Pointer;
    const std::int32_t ScopedSlider = ScopedCanvas.AddWidget(ScopedDesc);

    // Click on the middle of the track jumps the knob there and captures it.
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(50.0f, 10.0f)) == ScopedSlider);
    FBY_CHECK_NEAR(ScopedCanvas.progresses[ScopedSlider], 0.5f, kTolerance);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == ScopedSlider);
    FBY_CHECK(ScopedCanvas.dragIndex == ScopedSlider);
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedSlider), WidgetFlags::Pressed));

    // The pointer move keeps dragging even after leaving the knob, and the
    // hover bit follows the hit test.
    FBY_CHECK(Fby::Ui::PointerMove(ScopedCanvas, V(95.0f, 10.0f)) == ScopedSlider);
    FBY_CHECK_NEAR(ScopedCanvas.progresses[ScopedSlider], 1.0f, kTolerance);
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedSlider), WidgetFlags::Hovered));

    // Release commits the value and ends the capture.
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(95.0f, 10.0f)) == ScopedSlider);
    FBY_CHECK(ScopedCanvas.dragIndex == Fby::Ui::kNoWidget);
    FBY_CHECK(!FactoryFlag(ScopedCanvas.Flags(ScopedSlider), WidgetFlags::Pressed));
    FBY_CHECK_NEAR(ScopedCanvas.progresses[ScopedSlider], 1.0f, 0.0f);

    // Pressing at the far left clamps to zero instead of going negative.
    FBY_CHECK_NEAR(ScopedCanvas.SliderValueAt(ScopedSlider, V(0.0f, 10.0f)), 0.0f, 0.0f);
    FBY_CHECK_NEAR(ScopedCanvas.SliderValueAt(ScopedSlider, V(100.0f, 10.0f)), 1.0f, 0.0f);

    // A release outside the track is not a click, but it still ends the drag.
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(50.0f, 10.0f)) == ScopedSlider);
    FBY_CHECK(Fby::Ui::PointerMove(ScopedCanvas, V(-40.0f, 10.0f)) == ScopedSlider);
    FBY_CHECK_NEAR(ScopedCanvas.progresses[ScopedSlider], 0.0f, 0.0f);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(-40.0f, 10.0f)) == Fby::Ui::kNoWidget);
    FBY_CHECK(ScopedCanvas.dragIndex == Fby::Ui::kNoWidget);

    // A disabled slider is still hit-testable but never captures the pointer.
    ScopedCanvas.SetProgress(ScopedSlider, 0.5f);
    ScopedCanvas.SetFlag(ScopedSlider, WidgetFlags::Enabled, false);
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(20.0f, 10.0f)) == ScopedSlider);
    FBY_CHECK_NEAR(ScopedCanvas.progresses[ScopedSlider], 0.5f, 0.0f);
    FBY_CHECK(ScopedCanvas.dragIndex == Fby::Ui::kNoWidget);
    FBY_CHECK(!FactoryFlag(ScopedCanvas.Flags(ScopedSlider), WidgetFlags::Pressed));
    FBY_CHECK(Fby::Ui::PointerMove(ScopedCanvas, V(80.0f, 10.0f)) == Fby::Ui::kNoWidget);
    FBY_CHECK_NEAR(ScopedCanvas.progresses[ScopedSlider], 0.5f, 0.0f);

    // Vertical sliders travel bottom-up, so the top half is the high value.
    WidgetCanvas ScopedVerticalCanvas;
    ScopedVerticalCanvas.SetCanvasSize(V(20.0f, 100.0f));
    WidgetDesc ScopedVerticalDesc = StretchWidget(WidgetKind::Slider);
    ScopedVerticalDesc.knobSize = 10.0f;
    ScopedVerticalDesc.flags |= static_cast<std::uint32_t>(WidgetFlags::Vertical);
    const std::int32_t ScopedVertical = ScopedVerticalCanvas.AddWidget(ScopedVerticalDesc);
    FBY_CHECK_NEAR(ScopedVerticalCanvas.SliderValueAt(ScopedVertical, V(10.0f, 50.0f)), 0.5f, kTolerance);
    FBY_CHECK_NEAR(ScopedVerticalCanvas.SliderValueAt(ScopedVertical, V(10.0f, 0.0f)), 1.0f, 0.0f);
    FBY_CHECK_NEAR(ScopedVerticalCanvas.SliderValueAt(ScopedVertical, V(10.0f, 99.0f)), 0.0f, 0.0f);

    // Scrollbar: the thumb occupies half the track, so the travel is the rest.
    WidgetCanvas ScopedScrollCanvas;
    ScopedScrollCanvas.SetCanvasSize(V(100.0f, 20.0f));
    WidgetDesc ScopedScrollDesc = StretchWidget(WidgetKind::Scrollbar);
    ScopedScrollDesc.ratio = 0.5f;
    const std::int32_t ScopedScrollbar = ScopedScrollCanvas.AddWidget(ScopedScrollDesc);
    FBY_CHECK_NEAR(ScopedScrollCanvas.SliderValueAt(ScopedScrollbar, V(50.0f, 10.0f)), 0.5f, kTolerance);
    FBY_CHECK(Fby::Ui::PointerDown(ScopedScrollCanvas, V(0.0f, 10.0f)) == ScopedScrollbar);
    FBY_CHECK_NEAR(ScopedScrollCanvas.progresses[ScopedScrollbar], 0.0f, 0.0f);
    FBY_CHECK(Fby::Ui::PointerMove(ScopedScrollCanvas, V(99.0f, 10.0f)) == ScopedScrollbar);
    FBY_CHECK_NEAR(ScopedScrollCanvas.progresses[ScopedScrollbar], 1.0f, 0.01f);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedScrollCanvas, V(99.0f, 10.0f)) == ScopedScrollbar);
    // The visible fraction is clamped to a minimum so the thumb stays visible.
    ScopedScrollCanvas.SetRatio(ScopedScrollbar, 0.0f);
    FBY_CHECK_NEAR(ScopedScrollCanvas.ratios[ScopedScrollbar], 0.0f, 0.0f);
    FBY_CHECK_NEAR(ScopedScrollCanvas.SliderValueAt(ScopedScrollbar, V(50.0f, 10.0f)), 0.5f, 0.01f);
}

// 15. Checkbox toggles only on a completed click
void TestCheckboxToggle()
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(20.0f, 20.0f));
    const std::int32_t ScopedCheckbox = ScopedCanvas.AddWidget(StretchWidget(WidgetKind::Checkbox));
    FBY_CHECK(!ScopedCanvas.Checked(ScopedCheckbox));

    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(10.0f, 10.0f)) == ScopedCheckbox);
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedCheckbox), WidgetFlags::Pressed));
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(10.0f, 10.0f)) == ScopedCheckbox);
    FBY_CHECK(ScopedCanvas.Checked(ScopedCheckbox));

    // A second completed click toggles it back.
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(10.0f, 10.0f)) == ScopedCheckbox);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(10.0f, 10.0f)) == ScopedCheckbox);
    FBY_CHECK(!ScopedCanvas.Checked(ScopedCheckbox));

    // Press inside and release outside: no toggle and no click.
    FBY_CHECK(Fby::Ui::PointerDown(ScopedCanvas, V(10.0f, 10.0f)) == ScopedCheckbox);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedCanvas, V(50.0f, 50.0f)) == Fby::Ui::kNoWidget);
    FBY_CHECK(!ScopedCanvas.Checked(ScopedCheckbox));

    ScopedCanvas.SetChecked(ScopedCheckbox, true);
    FBY_CHECK(ScopedCanvas.Checked(ScopedCheckbox));
    ScopedCanvas.SetChecked(ScopedCheckbox, false);
    FBY_CHECK(!ScopedCanvas.Checked(ScopedCheckbox));
}

// 16. Vertical progress bars fill from the bottom edge
void TestVerticalProgressBar(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(20.0f, 100.0f));
    WidgetDesc ScopedDesc = StretchWidget(WidgetKind::ProgressBar);
    ScopedDesc.flags |= static_cast<std::uint32_t>(WidgetFlags::Vertical);
    ScopedDesc.progress = 0.25f;
    (void)ScopedCanvas.AddWidget(ScopedDesc);

    std::array<RenderItem, 4> ScopedItems{};
    std::array<RenderKey, 4> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.requiredItems == 2);
    FBY_CHECK(ScopedOutput.itemsWritten == 2);
    FBY_CHECK_VEC(ScopedItems[0].position, V(10.0f, 50.0f));
    FBY_CHECK_VEC(ScopedItems[0].size, V(20.0f, 100.0f));
    FBY_CHECK_VEC(ScopedItems[1].position, V(10.0f, 87.5f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(20.0f, 25.0f));
}

// 17. A Border whose stroke fills the box collapses to the two cross bars
void TestBorderStrokeClamp(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(20.0f, 20.0f));
    WidgetDesc ScopedDesc = StretchWidget(WidgetKind::Border);
    ScopedDesc.borderWidth = 100.0f;
    (void)ScopedCanvas.AddWidget(ScopedDesc);

    std::array<RenderItem, 4> ScopedItems{};
    std::array<RenderKey, 4> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.requiredItems == 2);
    FBY_CHECK(ScopedOutput.itemsWritten == 2);
    FBY_CHECK_VEC(ScopedItems[0].position, V(10.0f, 5.0f));
    FBY_CHECK_VEC(ScopedItems[0].size, V(20.0f, 10.0f));
    FBY_CHECK_VEC(ScopedItems[1].position, V(10.0f, 15.0f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(20.0f, 10.0f));
}

// 18. Focus order, tab index, groups, wrap, scope and activation
void TestFocusTraversal()
{
    // Interactive kinds are keyboard-inert by default.
    const WidgetKind ScopedInteractive[] = {WidgetKind::Button, WidgetKind::Slider, WidgetKind::Checkbox,
                                            WidgetKind::Scrollbar};
    for (const WidgetKind ScopedKind : ScopedInteractive)
    {
        WidgetCanvas ScopedInert;
        ScopedInert.SetCanvasSize(V(10.0f, 10.0f));
        const std::int32_t ScopedId = ScopedInert.AddWidget(StretchWidget(ScopedKind));
        FBY_CHECK(ScopedInert.FocusPolicyOf(ScopedId) == FocusPolicy::None);
        FBY_CHECK(!ScopedInert.CanFocus(ScopedId));
        FBY_CHECK(Fby::Ui::FocusNext(ScopedInert) == Fby::Ui::kNoWidget);
    }

    // The legacy Focusable bit is a bridge to TabAndPointer, not a second source.
    WidgetCanvas ScopedLegacy;
    ScopedLegacy.SetCanvasSize(V(10.0f, 10.0f));
    WidgetDesc ScopedLegacyDesc = StretchWidget(WidgetKind::Panel);
    ScopedLegacyDesc.flags |= static_cast<std::uint32_t>(WidgetFlags::Focusable);
    const std::int32_t ScopedLegacyId = ScopedLegacy.AddWidget(ScopedLegacyDesc);
    FBY_CHECK(ScopedLegacy.FocusPolicyOf(ScopedLegacyId) == FocusPolicy::TabAndPointer);
    FBY_CHECK(ScopedLegacy.CanFocus(ScopedLegacyId));
    ScopedLegacy.SetFocusable(ScopedLegacyId, false);
    FBY_CHECK(ScopedLegacy.FocusPolicyOf(ScopedLegacyId) == FocusPolicy::None);
    FBY_CHECK(!FactoryFlag(ScopedLegacy.Flags(ScopedLegacyId), WidgetFlags::Focusable));

    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(300.0f, 100.0f));
    const std::int32_t ScopedA = AddButton(ScopedCanvas, 0.0f, 50.0f, 20, 1, FocusPolicy::Tab);
    const std::int32_t ScopedB = AddButton(ScopedCanvas, 50.0f, 100.0f, 10, 1, FocusPolicy::TabAndPointer);
    const std::int32_t ScopedC = AddButton(ScopedCanvas, 100.0f, 150.0f, 30, 2, FocusPolicy::Tab);
    FBY_CHECK(ScopedA >= 0 && ScopedB >= 0 && ScopedC >= 0);

    // Order is (tabIndex, insertion index): B(10), A(20), C(30).
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedB);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedA);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedC);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedB);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas, true) == ScopedC);

    // An equal tabIndex falls back to the insertion index.
    const std::int32_t ScopedD = AddButton(ScopedCanvas, 150.0f, 200.0f, 10, 1, FocusPolicy::Tab);
    FBY_CHECK(ScopedD == 3);
    Fby::Ui::SetFocus(ScopedCanvas, Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedB);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedD);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedA);

    // Without wrap the traversal stops at the last candidate and keeps focus.
    ScopedCanvas.SetFocusWrap(false);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedC);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == ScopedC);
    ScopedCanvas.SetFocusWrap(true);

    // Group filter restricts the candidate set.
    ScopedCanvas.SetFocusGroupFilter(1);
    Fby::Ui::SetFocus(ScopedCanvas, Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedB);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedD);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedA);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedB);
    ScopedCanvas.SetFocusGroupFilter(2);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedC);
    ScopedCanvas.SetFocusGroupFilter(Fby::Ui::kNoFocusGroup);

    // Disabled and hidden widgets leave the order.
    ScopedCanvas.SetFlag(ScopedA, WidgetFlags::Enabled, false);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedD);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedC);
    ScopedCanvas.SetFlag(ScopedA, WidgetFlags::Enabled, true);
    ScopedCanvas.SetVisible(ScopedB, false);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedA);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedC);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas, true) == ScopedA);
    ScopedCanvas.SetVisible(ScopedB, true);

    // Activation toggles a focused Checkbox and returns every other kind.
    WidgetCanvas ScopedActivate;
    ScopedActivate.SetCanvasSize(V(100.0f, 100.0f));
    WidgetDesc ScopedCheckbox = StretchWidget(WidgetKind::Checkbox);
    ScopedCheckbox.focusPolicy = FocusPolicy::Tab;
    const std::int32_t ScopedCheckboxId = ScopedActivate.AddWidget(ScopedCheckbox);
    Fby::Ui::SetFocus(ScopedActivate, ScopedCheckboxId);
    FBY_CHECK(Fby::Ui::ActivateFocused(ScopedActivate) == ScopedCheckboxId);
    FBY_CHECK(ScopedActivate.Checked(ScopedCheckboxId));
    FBY_CHECK(Fby::Ui::ActivateFocused(ScopedActivate) == ScopedCheckboxId);
    FBY_CHECK(!ScopedActivate.Checked(ScopedCheckboxId));
    ScopedActivate.SetFlag(ScopedCheckboxId, WidgetFlags::Enabled, false);
    FBY_CHECK(Fby::Ui::ActivateFocused(ScopedActivate) == Fby::Ui::kNoWidget);

    // A Modal Overlay confines Tab focus to its subtree, including programmatic
    // focus outside the scope.
    WidgetCanvas ScopedModal;
    ScopedModal.SetCanvasSize(V(200.0f, 100.0f));
    WidgetDesc ScopedOverlay = StretchWidget(WidgetKind::Overlay, 10);
    ScopedOverlay.flags |= static_cast<std::uint32_t>(WidgetFlags::Modal);
    const std::int32_t ScopedOverlayId = ScopedModal.AddWidget(ScopedOverlay);
    const std::int32_t ScopedOutside = AddButton(ScopedModal, 0.0f, 50.0f, 0, Fby::Ui::kNoFocusGroup, FocusPolicy::Tab);
    WidgetDesc ScopedInside = WidgetAt(V(10.0f, 10.0f), V(90.0f, 30.0f), WidgetKind::Button, 0);
    ScopedInside.parent = ScopedOverlayId;
    ScopedInside.focusPolicy = FocusPolicy::Tab;
    const std::int32_t ScopedInsideId = ScopedModal.AddWidget(ScopedInside);
    FBY_CHECK(Fby::Ui::EffectiveFocusScope(ScopedModal) == ScopedOverlayId);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedModal) == ScopedInsideId);
    Fby::Ui::SetFocus(ScopedModal, ScopedOutside);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedModal) == ScopedInsideId);

    // Clearing Modal removes the trap; the explicit scope takes precedence.
    ScopedModal.SetFlag(ScopedOverlayId, WidgetFlags::Modal, false);
    FBY_CHECK(Fby::Ui::EffectiveFocusScope(ScopedModal) == Fby::Ui::kNoWidget);
    ScopedModal.SetFocusScope(ScopedOutside);
    FBY_CHECK(Fby::Ui::EffectiveFocusScope(ScopedModal) == ScopedOutside);
    Fby::Ui::SetFocus(ScopedModal, Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedModal) == Fby::Ui::kNoWidget);
    ScopedModal.SetFocusScope(Fby::Ui::kNoWidget);
}

// 19. Every kind can be hidden; a hidden parent hides its subtree
void TestVisibility(SystemContext& InContext)
{
    const WidgetKind ScopedKinds[] = {
        WidgetKind::Panel,    WidgetKind::Button,     WidgetKind::Label,  WidgetKind::Image,
        WidgetKind::ProgressBar, WidgetKind::Hypertext, WidgetKind::Spacer, WidgetKind::Border,
        WidgetKind::Slider,   WidgetKind::Checkbox,   WidgetKind::Scrollbar, WidgetKind::Switcher,
        WidgetKind::Overlay,  WidgetKind::Custom,     WidgetKind::Tooltip, WidgetKind::TextInput,
        WidgetKind::NineSlice, WidgetKind::ToggleSwitch, WidgetKind::RadioButton, WidgetKind::TextBlock,
    };
    for (const WidgetKind ScopedKind : ScopedKinds)
    {
        WidgetCanvas ScopedCanvas;
        ScopedCanvas.SetCanvasSize(V(100.0f, 100.0f));
        WidgetDesc ScopedDesc = StretchWidget(ScopedKind);
        ScopedDesc.progress = 0.5f;
        ScopedDesc.focusPolicy = FocusPolicy::TabAndPointer;
        if (ScopedKind == WidgetKind::Tooltip)
        {
            // An unpinned tooltip is hidden without hover; pin it for the sweep.
            ScopedDesc.flags |= static_cast<std::uint32_t>(WidgetFlags::Pinned);
        }
        const std::int32_t ScopedId = ScopedCanvas.AddWidget(ScopedDesc);
        FBY_CHECK(ScopedId == 0);

        std::array<RenderItem, 16> ScopedItems{};
        std::array<RenderKey, 16> ScopedKeys{};
        std::array<DrawBatch, 1> ScopedBatches{};
        FrameOutput ScopedOutput;
        ScopedOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
        FBY_CHECK(ScopedOutput.widgetsDrawn == 1);
        FBY_CHECK(ScopedCanvas.IsVisible(ScopedId));

        ScopedCanvas.SetVisible(ScopedId, false);
        FBY_CHECK(!ScopedCanvas.IsVisible(ScopedId));
        ScopedOutput = FrameOutput{};
        ScopedOutput.items = std::span<RenderItem>(ScopedItems);
        ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
        ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
        FBY_CHECK(ScopedOutput.widgetsDrawn == 0);
        FBY_CHECK(ScopedOutput.itemsWritten == 0);
        FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == Fby::Ui::kNoWidget);
        FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == Fby::Ui::kNoWidget);

        ScopedCanvas.SetVisible(ScopedId, true);
        FBY_CHECK(ScopedCanvas.IsVisible(ScopedId));
        FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedId);
    }

    // Hiding a parent hides the descendants without touching their own bit.
    WidgetCanvas ScopedTree;
    ScopedTree.SetCanvasSize(V(100.0f, 100.0f));
    const std::int32_t ScopedParent = ScopedTree.AddWidget(StretchWidget(WidgetKind::Panel));
    WidgetDesc ScopedChild = WidgetAt(V(10.0f, 10.0f), V(50.0f, 50.0f), WidgetKind::Button, 0);
    ScopedChild.parent = ScopedParent;
    ScopedChild.focusPolicy = FocusPolicy::Tab;
    const std::int32_t ScopedChildId = ScopedTree.AddWidget(ScopedChild);
    FBY_CHECK(ScopedChildId == 1);
    ScopedTree.SetVisible(ScopedParent, false);
    FBY_CHECK(FactoryFlag(ScopedTree.Flags(ScopedChildId), WidgetFlags::Visible));
    FBY_CHECK(!ScopedTree.IsVisible(ScopedChildId));
    FBY_CHECK(Fby::Ui::FocusNext(ScopedTree) == Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::HitTest(ScopedTree, V(20.0f, 20.0f)) == Fby::Ui::kNoWidget);
    ScopedTree.SetVisible(ScopedParent, true);
    FBY_CHECK(ScopedTree.IsVisible(ScopedChildId));
    FBY_CHECK(Fby::Ui::HitTest(ScopedTree, V(20.0f, 20.0f)) == ScopedChildId);
}

// 20. Switcher pages hide inactive children while keeping their layout
void TestSwitcher(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(100.0f, 100.0f));
    const std::int32_t ScopedSwitcher = ScopedCanvas.AddWidget(StretchWidget(WidgetKind::Switcher));
    std::array<std::int32_t, 3> ScopedPages{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedPages.size(); ++ScopedIndex)
    {
        WidgetDesc ScopedPage = StretchWidget(WidgetKind::Panel);
        ScopedPage.parent = ScopedSwitcher;
        ScopedPage.focusPolicy = FocusPolicy::Tab;
        ScopedPages[ScopedIndex] = ScopedCanvas.AddWidget(ScopedPage);
    }
    FBY_CHECK(ScopedCanvas.ActivePage(ScopedSwitcher) == 0);
    FBY_CHECK(ScopedCanvas.IsVisible(ScopedPages[0]));
    FBY_CHECK(!ScopedCanvas.IsVisible(ScopedPages[1]));

    std::array<RenderItem, 8> ScopedItems{};
    std::array<RenderKey, 8> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    // The Switcher itself draws nothing; only the active page emits a quad.
    FBY_CHECK(ScopedOutput.requiredItems == 1);
    FBY_CHECK(ScopedOutput.itemsWritten == 1);
    FBY_CHECK(ScopedOutput.widgetsDrawn == 2);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == ScopedPages[0]);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedPages[0]);

    ScopedCanvas.SetActivePage(ScopedSwitcher, 1);
    FBY_CHECK(ScopedCanvas.ActivePage(ScopedSwitcher) == 1);
    FBY_CHECK(!ScopedCanvas.IsVisible(ScopedPages[0]));
    FBY_CHECK(ScopedCanvas.IsVisible(ScopedPages[1]));
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == ScopedPages[1]);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedPages[1]);

    // Out-of-range pages clamp to the last child.
    ScopedCanvas.SetActivePage(ScopedSwitcher, 99);
    FBY_CHECK(ScopedCanvas.ActivePage(ScopedSwitcher) == 2);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == ScopedPages[2]);

    // A specific child is selected through a pointer-free handle, not a page.
    const Fby::Ui::WidgetHandle ScopedPage1 = ScopedCanvas.Handle(ScopedPages[1]);
    FBY_CHECK(Fby::Ui::IsHandleSet(ScopedPage1));
    FBY_CHECK(ScopedCanvas.ResolveHandle(ScopedPage1));
    FBY_CHECK(ScopedCanvas.SetActiveWidget(ScopedSwitcher, ScopedPage1));
    FBY_CHECK(ScopedCanvas.ActiveWidget(ScopedSwitcher).widget == ScopedPages[1]);
    FBY_CHECK(ScopedCanvas.ActivePage(ScopedSwitcher) == 1);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == ScopedPages[1]);

    // An empty handle resets the switcher to its first child.
    FBY_CHECK(ScopedCanvas.SetActiveWidget(ScopedSwitcher, Fby::Ui::WidgetHandle{}));
    FBY_CHECK(ScopedCanvas.ActivePage(ScopedSwitcher) == 0);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == ScopedPages[0]);

    // A widget that is not a direct child is rejected.
    const std::int32_t ScopedOutside = ScopedCanvas.AddWidget(StretchWidget(WidgetKind::Panel));
    FBY_CHECK(!ScopedCanvas.SetActiveWidget(ScopedSwitcher, ScopedCanvas.Handle(ScopedOutside)));
    FBY_CHECK(ScopedCanvas.ActivePage(ScopedSwitcher) == 0);

    // A handle goes stale when the canvas is cleared and the generation moves.
    const Fby::Ui::WidgetHandle ScopedStale = ScopedPage1;
    ScopedCanvas.Clear();
    FBY_CHECK(!ScopedCanvas.ResolveHandle(ScopedStale));
    const std::int32_t ScopedFresh = ScopedCanvas.AddWidget(StretchWidget(WidgetKind::Panel));
    FBY_CHECK(ScopedCanvas.Handle(ScopedFresh).generation != ScopedStale.generation);
    FBY_CHECK(ScopedCanvas.ResolveHandle(ScopedCanvas.Handle(ScopedFresh)));

    // Hiding the Switcher hides every page.
    ScopedCanvas.Clear();
    const std::int32_t ScopedHiddenSwitcher = ScopedCanvas.AddWidget(StretchWidget(WidgetKind::Switcher));
    WidgetDesc ScopedHiddenPage = StretchWidget(WidgetKind::Panel);
    ScopedHiddenPage.parent = ScopedHiddenSwitcher;
    (void)ScopedCanvas.AddWidget(ScopedHiddenPage);
    ScopedCanvas.SetVisible(ScopedHiddenSwitcher, false);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 0);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(50.0f, 50.0f)) == Fby::Ui::kNoWidget);
}

std::uint32_t GCustomPaintCalls = 0;

// A custom painter with a deterministic two-quad output.
void PaintInsetQuads(Fby::Ui::WidgetPaintContext& InContext) noexcept
{
    ++GCustomPaintCalls;
    (void)InContext.Quad(InContext.LocalRect, InContext.Tint);
    const float ScopedInset = InContext.Params.X;
    const Vector2D ScopedMin{InContext.LocalRect.min.X + ScopedInset, InContext.LocalRect.min.Y + ScopedInset};
    const Vector2D ScopedMax{InContext.LocalRect.max.X - ScopedInset, InContext.LocalRect.max.Y - ScopedInset};
    (void)InContext.Quad(Rect{ScopedMin, ScopedMax}, InContext.Fill);
}

// A painter that intentionally overruns the per-widget quota.
void PaintFlood(Fby::Ui::WidgetPaintContext& InContext) noexcept
{
    ++GCustomPaintCalls;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 100; ++ScopedIndex)
    {
        (void)InContext.Quad(InContext.LocalRect, InContext.Tint);
    }
}

// 21. Custom widgets: painter, params, quota, hiding and allocation-free paint
void TestCustomWidget(SystemContext& InContext)
{
    GCustomPaintCalls = 0;
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(100.0f, 100.0f));
    WidgetDesc ScopedDesc = StretchWidget(WidgetKind::Custom);
    ScopedDesc.customPaint = &PaintInsetQuads;
    ScopedDesc.customParams = Fby::Vector4D{10.0f, 0.0f, 0.0f, 0.0f};
    ScopedDesc.tint = Color32::FromBytes(0x10, 0x20, 0x30);
    ScopedDesc.fillTint = Color32::FromBytes(0x40, 0x50, 0x60);
    const std::int32_t ScopedCustom = ScopedCanvas.AddWidget(ScopedDesc);
    FBY_CHECK(ScopedCustom == 0);

    std::array<RenderItem, 128> ScopedItems{};
    std::array<RenderKey, 128> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    {
        const AllocationScope ScopedScope;
        FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
        FBY_CHECK(ScopedScope.Count() == 0);
    }
    // The painter runs twice: once for the budget and once for the emit.
    FBY_CHECK(GCustomPaintCalls == 2);
    FBY_CHECK(ScopedOutput.requiredItems == 2);
    FBY_CHECK(ScopedOutput.itemsWritten == 2);
    FBY_CHECK_VEC(ScopedItems[0].position, V(50.0f, 50.0f));
    FBY_CHECK_VEC(ScopedItems[0].size, V(100.0f, 100.0f));
    FBY_CHECK(ScopedItems[0].color == ScopedDesc.tint.rgba);
    FBY_CHECK_VEC(ScopedItems[1].position, V(50.0f, 50.0f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(80.0f, 80.0f));
    FBY_CHECK(ScopedItems[1].color == ScopedDesc.fillTint.rgba);

    // A hidden custom widget never runs its painter.
    GCustomPaintCalls = 0;
    ScopedCanvas.SetVisible(ScopedCustom, false);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(GCustomPaintCalls == 0);
    FBY_CHECK(ScopedOutput.itemsWritten == 0);
    ScopedCanvas.SetVisible(ScopedCustom, true);

    // A null painter draws nothing.
    ScopedCanvas.SetCustomPaint(ScopedCustom, nullptr);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 0);

    // A painter that overruns is capped by the quota in both passes.
    GCustomPaintCalls = 0;
    ScopedCanvas.SetCustomPaint(ScopedCustom, &PaintFlood);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(GCustomPaintCalls == 2);
    FBY_CHECK(ScopedOutput.requiredItems == Fby::Ui::kMaxCustomQuadsPerWidget);
    FBY_CHECK(ScopedOutput.itemsWritten == Fby::Ui::kMaxCustomQuadsPerWidget);
}

// 22. Arrow-key focus movement
void TestFocusArrows()
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(200.0f, 200.0f));
    const std::int32_t ScopedA = AddFocusButton(ScopedCanvas, V(0.0f, 0.0f), V(50.0f, 50.0f), FocusPolicy::Tab);
    const std::int32_t ScopedB = AddFocusButton(ScopedCanvas, V(100.0f, 0.0f), V(150.0f, 50.0f), FocusPolicy::Tab);
    const std::int32_t ScopedC = AddFocusButton(ScopedCanvas, V(0.0f, 100.0f), V(50.0f, 150.0f), FocusPolicy::Tab);
    const std::int32_t ScopedD = AddFocusButton(ScopedCanvas, V(100.0f, 100.0f), V(150.0f, 150.0f), FocusPolicy::Tab);

    // Arrows need an existing focus.
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Right) == Fby::Ui::kNoWidget);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedA);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Right) == ScopedB);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Down) == ScopedD);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Left) == ScopedC);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Up) == ScopedA);

    // A disabled or hidden neighbour is skipped.
    ScopedCanvas.SetFlag(ScopedB, WidgetFlags::Enabled, false);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedA);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Right) == ScopedD);
    ScopedCanvas.SetFlag(ScopedB, WidgetFlags::Enabled, true);
    ScopedCanvas.SetVisible(ScopedB, false);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedA);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Right) == ScopedD);
    ScopedCanvas.SetVisible(ScopedB, true);

    // Groups and the scope narrow the arrows exactly like Tab.
    ScopedCanvas.SetFocusGroup(ScopedA, 1);
    ScopedCanvas.SetFocusGroup(ScopedC, 1);
    ScopedCanvas.SetFocusGroup(ScopedB, 2);
    ScopedCanvas.SetFocusGroup(ScopedD, 2);
    ScopedCanvas.SetFocusGroupFilter(1);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedA);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Right) == Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Down) == ScopedC);
    ScopedCanvas.SetFocusGroupFilter(Fby::Ui::kNoFocusGroup);

    // Any opt-in policy counts as focusable for the arrows.
    ScopedCanvas.SetFocusPolicy(ScopedB, FocusPolicy::Pointer);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedA);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Right) == ScopedB);
}

// 23. ClipChildren trims quads, UVs, hit testing and focus
void TestClipRect(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(200.0f, 100.0f));
    WidgetDesc ScopedContainer = WidgetAt(V(0.0f, 0.0f), V(50.0f, 100.0f), WidgetKind::Panel, 0);
    const std::int32_t ScopedParent = ScopedCanvas.AddWidget(ScopedContainer);
    ScopedCanvas.SetClipChildren(ScopedParent, true);
    FBY_CHECK(ScopedCanvas.ClipChildren(ScopedParent));

    WidgetDesc ScopedImage = WidgetAt(V(25.0f, 25.0f), V(100.0f, 75.0f), WidgetKind::Image, 1);
    ScopedImage.parent = ScopedParent;
    ScopedImage.focusPolicy = FocusPolicy::Tab;
    const std::int32_t ScopedClipped = ScopedCanvas.AddWidget(ScopedImage);

    std::array<RenderItem, 8> ScopedItems{};
    std::array<RenderKey, 8> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    // requiredItems stays the unclipped upper bound; the clipped output shrinks.
    FBY_CHECK(ScopedOutput.requiredItems == 2);
    FBY_CHECK(ScopedOutput.itemsWritten == 2);
    FBY_CHECK_VEC(ScopedItems[0].position, V(25.0f, 50.0f));
    FBY_CHECK_VEC(ScopedItems[0].size, V(50.0f, 100.0f));
    FBY_CHECK_VEC(ScopedItems[1].position, V(37.5f, 50.0f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(25.0f, 50.0f));
    FBY_CHECK_NEAR(ScopedItems[1].uvMin.X, 0.0f, kTolerance);
    FBY_CHECK_NEAR(ScopedItems[1].uvMax.X, 1.0f / 3.0f, kTolerance);
    FBY_CHECK_NEAR(ScopedItems[1].uvMin.Y, 0.0f, kTolerance);
    FBY_CHECK_NEAR(ScopedItems[1].uvMax.Y, 1.0f, kTolerance);

    // Clicks and focus respect the composed clip.
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(40.0f, 50.0f)) == ScopedClipped);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(60.0f, 50.0f)) == Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(10.0f, 50.0f)) == ScopedParent);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedClipped);

    // A child entirely outside the clip emits nothing and cannot take focus.
    WidgetDesc ScopedOutside = WidgetAt(V(60.0f, 10.0f), V(90.0f, 40.0f), WidgetKind::Image, 2);
    ScopedOutside.parent = ScopedParent;
    ScopedOutside.focusPolicy = FocusPolicy::Tab;
    const std::int32_t ScopedOutsideId = ScopedCanvas.AddWidget(ScopedOutside);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.requiredItems == 3);
    FBY_CHECK(ScopedOutput.itemsWritten == 2);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(70.0f, 20.0f)) == Fby::Ui::kNoWidget);
    Fby::Ui::SetFocus(ScopedCanvas, Fby::Ui::kNoWidget);
    FBY_CHECK(Fby::Ui::FocusNext(ScopedCanvas) == ScopedClipped);
    FBY_CHECK(Fby::Ui::FocusMove(ScopedCanvas, Fby::Ui::FocusDirection::Right) == Fby::Ui::kNoWidget);
    ScopedCanvas.SetVisible(ScopedOutsideId, false);

    // Nested clips compose to the intersection.
    WidgetCanvas ScopedNested;
    ScopedNested.SetCanvasSize(V(200.0f, 200.0f));
    const std::int32_t ScopedOuter = ScopedNested.AddWidget(
        WidgetAt(V(0.0f, 0.0f), V(100.0f, 100.0f), WidgetKind::Panel, 0));
    ScopedNested.SetClipChildren(ScopedOuter, true);
    WidgetDesc ScopedInnerDesc = WidgetAt(V(50.0f, 50.0f), V(150.0f, 150.0f), WidgetKind::Panel, 1);
    ScopedInnerDesc.parent = ScopedOuter;
    const std::int32_t ScopedInner = ScopedNested.AddWidget(ScopedInnerDesc);
    ScopedNested.SetClipChildren(ScopedInner, true);
    WidgetDesc ScopedLeafDesc = WidgetAt(V(0.0f, 0.0f), V(200.0f, 200.0f), WidgetKind::Image, 2);
    ScopedLeafDesc.parent = ScopedInner;
    const std::int32_t ScopedLeaf = ScopedNested.AddWidget(ScopedLeafDesc);

    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedNested, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.requiredItems == 3);
    FBY_CHECK(ScopedOutput.itemsWritten == 3);
    FBY_CHECK_VEC(ScopedItems[2].position, V(75.0f, 75.0f));
    FBY_CHECK_VEC(ScopedItems[2].size, V(50.0f, 50.0f));
    FBY_CHECK(Fby::Ui::HitTest(ScopedNested, V(60.0f, 60.0f)) == ScopedLeaf);
    FBY_CHECK(Fby::Ui::HitTest(ScopedNested, V(90.0f, 90.0f)) == ScopedLeaf);
    FBY_CHECK(Fby::Ui::HitTest(ScopedNested, V(120.0f, 120.0f)) == Fby::Ui::kNoWidget);
}

// 24. Tooltip hover delay, pinning and the ignore-hit-test contract
void TestTooltip(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(200.0f, 100.0f));
    const std::int32_t ScopedTarget =
        ScopedCanvas.AddWidget(WidgetAt(V(0.0f, 0.0f), V(80.0f, 40.0f), WidgetKind::Button, 0));

    WidgetDesc ScopedTip = WidgetAt(V(90.0f, 50.0f), V(190.0f, 80.0f), WidgetKind::Tooltip, 5);
    ScopedTip.text = "Tip";
    ScopedTip.fontSize = 8;
    ScopedTip.tooltipTarget = ScopedCanvas.Handle(ScopedTarget);
    ScopedTip.tooltipDelay = 0.5f;
    ScopedTip.tint = Color32::FromBytes(0x20, 0x20, 0x20, 0xE0);
    ScopedTip.textTint = Color32::FromBytes(0xF0, 0xF0, 0xF0);
    const std::int32_t ScopedTooltip = ScopedCanvas.AddWidget(ScopedTip);

    FBY_CHECK(ScopedCanvas.TooltipTarget(ScopedTooltip).widget == ScopedTarget);
    FBY_CHECK_NEAR(ScopedCanvas.TooltipDelay(ScopedTooltip), 0.5f, 0.0f);
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedTooltip), WidgetFlags::IgnoreHitTest));
    FBY_CHECK(!ScopedCanvas.IsVisible(ScopedTooltip));

    std::array<RenderItem, 16> ScopedItems{};
    std::array<RenderKey, 16> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.requiredItems == 1);
    FBY_CHECK(ScopedOutput.itemsWritten == 1);
    FBY_CHECK(ScopedOutput.widgetsDrawn == 1);

    // Hovering the target accumulates time, but not enough before the delay.
    (void)Fby::Ui::PointerMove(ScopedCanvas, V(20.0f, 20.0f));
    FBY_CHECK(FactoryFlag(ScopedCanvas.Flags(ScopedTarget), WidgetFlags::Hovered));
    ScopedCanvas.AdvanceHover(0.25f);
    FBY_CHECK_NEAR(ScopedCanvas.HoverSeconds(ScopedTarget), 0.25f, kTolerance);
    FBY_CHECK(!ScopedCanvas.IsVisible(ScopedTooltip));

    // Crossing the delay reveals the tooltip with its centred caption.
    ScopedCanvas.AdvanceHover(0.30f);
    FBY_CHECK_NEAR(ScopedCanvas.HoverSeconds(ScopedTarget), 0.55f, kTolerance);
    FBY_CHECK(ScopedCanvas.IsVisible(ScopedTooltip));
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.requiredItems == 5);
    FBY_CHECK(ScopedOutput.itemsWritten == 5);
    FBY_CHECK(ScopedOutput.glyphQuads == 3);
    FBY_CHECK_VEC(ScopedItems[1].position, V(140.0f, 65.0f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(100.0f, 30.0f));
    FBY_CHECK(ScopedItems[1].color == ScopedTip.tint.rgba);
    FBY_CHECK(ScopedItems[2].color == ScopedTip.textTint.rgba);
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(140.0f, 65.0f)) == Fby::Ui::kNoWidget);

    // Moving away resets the timer and hides the tooltip again.
    (void)Fby::Ui::PointerMove(ScopedCanvas, V(150.0f, 95.0f));
    ScopedCanvas.AdvanceHover(0.1f);
    FBY_CHECK_NEAR(ScopedCanvas.HoverSeconds(ScopedTarget), 0.0f, 0.0f);
    FBY_CHECK(!ScopedCanvas.IsVisible(ScopedTooltip));

    // A pinned tooltip is visible without hover; the hit-test rule still holds.
    ScopedCanvas.SetPinned(ScopedTooltip, true);
    FBY_CHECK(ScopedCanvas.Pinned(ScopedTooltip));
    FBY_CHECK(ScopedCanvas.IsVisible(ScopedTooltip));
    FBY_CHECK(Fby::Ui::HitTest(ScopedCanvas, V(140.0f, 65.0f)) == Fby::Ui::kNoWidget);
    ScopedCanvas.SetPinned(ScopedTooltip, false);
    FBY_CHECK(!ScopedCanvas.IsVisible(ScopedTooltip));

    // A stale target handle (or no target at all) never shows the tooltip.
    const Fby::Ui::WidgetHandle ScopedStaleTarget = ScopedCanvas.Handle(ScopedTarget);
    ScopedCanvas.Clear();
    const std::int32_t ScopedOrphan = ScopedCanvas.AddWidget(StretchWidget(WidgetKind::Tooltip));
    ScopedCanvas.SetTooltipTarget(ScopedOrphan, ScopedStaleTarget);
    FBY_CHECK(!ScopedCanvas.ResolveHandle(ScopedStaleTarget));
    FBY_CHECK(!ScopedCanvas.IsVisible(ScopedOrphan));
}

// 25. TextInput editing, caret, selection, blink and text-pool compaction
void TestTextInput(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(200.0f, 100.0f));
    WidgetDesc ScopedDesc = WidgetAt(V(0.0f, 0.0f), V(160.0f, 30.0f), WidgetKind::TextInput, 0);
    ScopedDesc.text = "AB";
    ScopedDesc.fontSize = 8;
    ScopedDesc.focusPolicy = FocusPolicy::TabAndPointer;
    ScopedDesc.tint = Color32::FromBytes(0x20, 0x20, 0x20);
    ScopedDesc.textTint = Color32::FromBytes(0xF0, 0xF0, 0xF0);
    const std::int32_t ScopedInput = ScopedCanvas.AddWidget(ScopedDesc);
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 2);
    FBY_CHECK(!ScopedCanvas.HasSelection(ScopedInput));
    Fby::Ui::SetFocus(ScopedCanvas, ScopedInput);

    std::array<RenderItem, 16> ScopedItems{};
    std::array<RenderKey, 16> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    // Background, two glyphs and the visible caret.
    FBY_CHECK(ScopedOutput.requiredItems == 4);
    FBY_CHECK(ScopedOutput.itemsWritten == 4);
    FBY_CHECK(ScopedOutput.glyphQuads == 2);
    FBY_CHECK_VEC(ScopedItems[0].position, V(80.0f, 15.0f));
    FBY_CHECK_VEC(ScopedItems[1].position, V(4.0f, 4.0f));
    FBY_CHECK_VEC(ScopedItems[2].position, V(8.0f, 4.0f));
    FBY_CHECK_NEAR(ScopedItems[3].position.X, 8.0f, 0.01f);
    FBY_CHECK_NEAR(ScopedItems[3].size.Y, 8.0f, kTolerance);

    // The caret blinks with the canvas clock.
    ScopedCanvas.AdvanceTime(0.6f);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 3);
    ScopedCanvas.AdvanceTime(0.5f);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 4);

    // A selection adds one highlight quad between the glyphs.
    FBY_CHECK(ScopedCanvas.SetCaret(ScopedInput, 1, true));
    FBY_CHECK(ScopedCanvas.HasSelection(ScopedInput));
    FBY_CHECK(ScopedCanvas.SelectedText(ScopedInput) == "B");
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 5);
    FBY_CHECK_VEC(ScopedItems[1].position, V(6.0f, 4.0f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(4.0f, 8.0f));

    // Editing replaces the selection and keeps the caret on codepoint boundaries.
    FBY_CHECK(ScopedCanvas.SetCaret(ScopedInput, 2, false));
    ScopedCanvas.ClearSelection(ScopedInput);
    FBY_CHECK(ScopedCanvas.InsertText(ScopedInput, "C"));
    FBY_CHECK(ScopedCanvas.Text(ScopedInput) == "ABC");
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 3);
    FBY_CHECK(ScopedCanvas.Backspace(ScopedInput));
    FBY_CHECK(ScopedCanvas.Text(ScopedInput) == "AB");
    FBY_CHECK(ScopedCanvas.SetCaret(ScopedInput, 0, false));
    FBY_CHECK(ScopedCanvas.DeleteForward(ScopedInput));
    FBY_CHECK(ScopedCanvas.Text(ScopedInput) == "B");
    FBY_CHECK(ScopedCanvas.InsertCodepoint(ScopedInput, 0xE9u));
    FBY_CHECK(ScopedCanvas.Text(ScopedInput) == "\xC3\xA9""B");
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 2);
    FBY_CHECK(ScopedCanvas.MoveCaret(ScopedInput, 1));
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 3);
    FBY_CHECK(ScopedCanvas.MoveCaret(ScopedInput, -1));
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 2);
    ScopedCanvas.SelectAll(ScopedInput);
    FBY_CHECK(ScopedCanvas.SelectedText(ScopedInput) == "\xC3\xA9""B");
    FBY_CHECK(ScopedCanvas.InsertText(ScopedInput, "X"));
    FBY_CHECK(ScopedCanvas.Text(ScopedInput) == "X");

    // Read-only rejects every mutation.
    ScopedCanvas.SetFlag(ScopedInput, WidgetFlags::ReadOnly, true);
    FBY_CHECK(!ScopedCanvas.InsertText(ScopedInput, "Y"));
    FBY_CHECK(!ScopedCanvas.Backspace(ScopedInput));
    FBY_CHECK(!ScopedCanvas.DeleteForward(ScopedInput));
    ScopedCanvas.SetFlag(ScopedInput, WidgetFlags::ReadOnly, false);

    // Click placement uses the atlas advances.
    (void)ScopedCanvas.SetText(ScopedInput, "AB");
    FBY_CHECK(Fby::Ui::SetCaretFromPoint(ScopedCanvas, ScopedInput, V(8.0f, 10.0f), InContext.fonts->At(0)));
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 1);
    FBY_CHECK(Fby::Ui::SetCaretFromPoint(ScopedCanvas, ScopedInput, V(20.0f, 10.0f), nullptr));
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 2);

    // The pool compacts instead of dropping a large replacement.
    WidgetCanvas ScopedPool;
    ScopedPool.SetCanvasSize(V(100.0f, 100.0f));
    const std::int32_t ScopedKeep = ScopedPool.AddWidget(WidgetAt(V(0.0f, 0.0f), V(50.0f, 20.0f), WidgetKind::Label));
    FBY_CHECK(ScopedPool.SetText(ScopedKeep, "Keep"));
    WidgetDesc ScopedLong = WidgetAt(V(0.0f, 30.0f), V(100.0f, 50.0f), WidgetKind::TextInput);
    const std::int32_t ScopedLongInput = ScopedPool.AddWidget(ScopedLong);
    std::array<char, 4096> ScopedLarge{};
    for (char& ScopedByte : ScopedLarge)
    {
        ScopedByte = 'x';
    }
    FBY_CHECK(ScopedPool.SetText(ScopedLongInput, std::string_view{ScopedLarge.data(), 3000}));
    FBY_CHECK(ScopedPool.SetText(ScopedLongInput, std::string_view{ScopedLarge.data(), 4000}));
    FBY_CHECK(ScopedPool.SetText(ScopedLongInput, std::string_view{ScopedLarge.data(), 3000}));
    FBY_CHECK(ScopedPool.SetText(ScopedLongInput, std::string_view{ScopedLarge.data(), 3500}));
    FBY_CHECK(ScopedPool.Text(ScopedKeep) == "Keep");
    FBY_CHECK(ScopedPool.Text(ScopedLongInput).size() == 3500);
    FBY_CHECK(ScopedPool.textUsed <= Fby::Ui::kMaxTextPool);
}

// 26. NineSlice geometry, ToggleSwitch and RadioButton groups
void TestElementExtended(SystemContext& InContext)
{
    // NineSlice: nine quads with sliced UVs.
    WidgetCanvas ScopedNine;
    ScopedNine.SetCanvasSize(V(100.0f, 100.0f));
    WidgetDesc ScopedSlice = StretchWidget(WidgetKind::NineSlice);
    ScopedSlice.sliceInsets = Fby::Vector4D{10.0f, 10.0f, 10.0f, 10.0f};
    const std::int32_t ScopedSliceId = ScopedNine.AddWidget(ScopedSlice);
    FBY_CHECK(ScopedNine.SliceInsets(ScopedSliceId).X == 10.0f);
    std::array<RenderItem, 16> ScopedItems{};
    std::array<RenderKey, 16> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedNine, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.requiredItems == 9);
    FBY_CHECK(ScopedOutput.itemsWritten == 9);
    FBY_CHECK_VEC(ScopedItems[0].position, V(5.0f, 5.0f));
    FBY_CHECK_VEC(ScopedItems[0].size, V(10.0f, 10.0f));
    FBY_CHECK_VEC(ScopedItems[0].uvMin, V(0.0f, 0.0f));
    FBY_CHECK_VEC(ScopedItems[0].uvMax, V(0.1f, 0.1f));
    FBY_CHECK_VEC(ScopedItems[4].position, V(50.0f, 50.0f));
    FBY_CHECK_VEC(ScopedItems[4].size, V(80.0f, 80.0f));
    FBY_CHECK_VEC(ScopedItems[4].uvMin, V(0.1f, 0.1f));
    FBY_CHECK_VEC(ScopedItems[4].uvMax, V(0.9f, 0.9f));
    FBY_CHECK_VEC(ScopedItems[8].position, V(95.0f, 95.0f));
    FBY_CHECK_VEC(ScopedItems[8].size, V(10.0f, 10.0f));
    FBY_CHECK_VEC(ScopedItems[8].uvMin, V(0.9f, 0.9f));
    FBY_CHECK_VEC(ScopedItems[8].uvMax, V(1.0f, 1.0f));

    // Oversized insets clamp to 45 percent per side.
    ScopedNine.SetSliceInsets(ScopedSliceId, Fby::Vector4D{100.0f, 100.0f, 100.0f, 100.0f});
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedNine, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 9);
    FBY_CHECK_VEC(ScopedItems[0].size, V(45.0f, 45.0f));
    FBY_CHECK_VEC(ScopedItems[4].size, V(10.0f, 10.0f));
    FBY_CHECK_NEAR(ScopedItems[0].uvMax.X, 0.45f, kTolerance);

    // ToggleSwitch: the knob moves right when checked.
    WidgetCanvas ScopedToggle;
    ScopedToggle.SetCanvasSize(V(100.0f, 40.0f));
    const std::int32_t ScopedToggleId = ScopedToggle.AddWidget(StretchWidget(WidgetKind::ToggleSwitch));
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedToggle, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 2);
    FBY_CHECK_VEC(ScopedItems[1].position, V(20.0f, 20.0f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(36.0f, 36.0f));
    FBY_CHECK(Fby::Ui::PointerDown(ScopedToggle, V(50.0f, 20.0f)) == ScopedToggleId);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedToggle, V(50.0f, 20.0f)) == ScopedToggleId);
    FBY_CHECK(ScopedToggle.Checked(ScopedToggleId));
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedToggle, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK_VEC(ScopedItems[1].position, V(80.0f, 20.0f));
    FBY_CHECK(Fby::Ui::ActivateFocused(ScopedToggle) == Fby::Ui::kNoWidget);
    Fby::Ui::SetFocus(ScopedToggle, ScopedToggleId);
    FBY_CHECK(Fby::Ui::ActivateFocused(ScopedToggle) == ScopedToggleId);
    FBY_CHECK(!ScopedToggle.Checked(ScopedToggleId));

    // RadioButton: one selected per group, clicking the selected one keeps it.
    WidgetCanvas ScopedRadios;
    ScopedRadios.SetCanvasSize(V(200.0f, 40.0f));
    std::array<std::int32_t, 4> ScopedRadioIds{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedRadioIds.size(); ++ScopedIndex)
    {
        WidgetDesc ScopedRadio = WidgetAt(V(static_cast<float>(ScopedIndex) * 50.0f, 0.0f),
                                          V(static_cast<float>(ScopedIndex) * 50.0f + 40.0f, 40.0f),
                                          WidgetKind::RadioButton, 0);
        ScopedRadio.radioGroup = static_cast<std::uint16_t>(ScopedIndex < 3 ? 7 : 8);
        ScopedRadioIds[ScopedIndex] = ScopedRadios.AddWidget(ScopedRadio);
    }
    FBY_CHECK(ScopedRadios.RadioGroup(ScopedRadioIds[0]) == 7);
    FBY_CHECK(Fby::Ui::PointerDown(ScopedRadios, V(20.0f, 20.0f)) == ScopedRadioIds[0]);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedRadios, V(20.0f, 20.0f)) == ScopedRadioIds[0]);
    FBY_CHECK(ScopedRadios.Checked(ScopedRadioIds[0]));
    FBY_CHECK(Fby::Ui::PointerDown(ScopedRadios, V(70.0f, 20.0f)) == ScopedRadioIds[1]);
    FBY_CHECK(Fby::Ui::PointerUp(ScopedRadios, V(70.0f, 20.0f)) == ScopedRadioIds[1]);
    FBY_CHECK(ScopedRadios.Checked(ScopedRadioIds[1]));
    FBY_CHECK(!ScopedRadios.Checked(ScopedRadioIds[0]));
    FBY_CHECK(!ScopedRadios.Checked(ScopedRadioIds[3]));
    FBY_CHECK(Fby::Ui::PointerUp(ScopedRadios, V(70.0f, 20.0f)) == Fby::Ui::kNoWidget);
    Fby::Ui::SetFocus(ScopedRadios, ScopedRadioIds[1]);
    FBY_CHECK(Fby::Ui::ActivateFocused(ScopedRadios) == ScopedRadioIds[1]);
    FBY_CHECK(ScopedRadios.Checked(ScopedRadioIds[1]));
    ScopedRadios.SetChecked(ScopedRadioIds[2], true);
    FBY_CHECK(ScopedRadios.Checked(ScopedRadioIds[2]));
    FBY_CHECK(!ScopedRadios.Checked(ScopedRadioIds[1]));
    ScopedRadios.SetChecked(ScopedRadioIds[3], true);
    FBY_CHECK(ScopedRadios.Checked(ScopedRadioIds[3]));
    FBY_CHECK(ScopedRadios.Checked(ScopedRadioIds[2]));
}

// 27. Optional focus ring on top of the focused widget
void TestFocusRing(SystemContext& InContext)
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(100.0f, 40.0f));
    WidgetDesc ScopedButton = WidgetAt(V(10.0f, 10.0f), V(60.0f, 30.0f), WidgetKind::Button, 0);
    ScopedButton.focusPolicy = FocusPolicy::Tab;
    const std::int32_t ScopedId = ScopedCanvas.AddWidget(ScopedButton);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedId);

    std::array<RenderItem, 8> ScopedItems{};
    std::array<RenderKey, 8> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 1);

    ScopedCanvas.SetDrawFocusRing(true);
    ScopedCanvas.SetFocusRingWidth(2.0f);
    FBY_CHECK(ScopedCanvas.DrawFocusRing());
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.requiredItems == 5);
    FBY_CHECK(ScopedOutput.itemsWritten == 5);
    FBY_CHECK_VEC(ScopedItems[1].position, V(35.0f, 9.0f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(54.0f, 2.0f));
    FBY_CHECK_VEC(ScopedItems[2].position, V(35.0f, 31.0f));
    FBY_CHECK_VEC(ScopedItems[2].size, V(54.0f, 2.0f));
    FBY_CHECK_VEC(ScopedItems[3].position, V(9.0f, 20.0f));
    FBY_CHECK_VEC(ScopedItems[3].size, V(2.0f, 20.0f));
    FBY_CHECK_VEC(ScopedItems[4].position, V(61.0f, 20.0f));
    FBY_CHECK_VEC(ScopedItems[4].size, V(2.0f, 20.0f));
    const std::uint32_t ScopedRingColor =
        ScopedCanvas.theme.Resolve(Fby::Ui::UiPaletteSlot::Focus, Fby::Ui::kPinkAccent).rgba;
    FBY_CHECK(ScopedItems[1].color == ScopedRingColor);
    FBY_CHECK(ScopedKeys[1] > ScopedKeys[0]);
    FBY_CHECK(ScopedKeys[4] > ScopedKeys[3]);

    // A hidden focus target or a zero width suppresses the ring.
    ScopedCanvas.SetVisible(ScopedId, false);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 0);
    ScopedCanvas.SetVisible(ScopedId, true);
    ScopedCanvas.SetFocusRingWidth(0.0f);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 1);
}

// 28. Per-state palette, visual transitions and short-text alignment
void TestStateAndTransitions(SystemContext& InContext)
{
    // Hover, pressed and disabled resolve through their palette slots.
    WidgetCanvas ScopedButtons;
    ScopedButtons.SetCanvasSize(V(100.0f, 40.0f));
    UiThemeAsset ScopedTheme = DefaultUiTheme();
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Hover)] =
        Color32::FromBytes(0x10, 0x80, 0x10);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Pressed)] =
        Color32::FromBytes(0x80, 0x10, 0x10);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Disabled)] =
        Color32::FromBytes(0x40, 0x40, 0x40);
    ScopedButtons.SetTheme(ScopedTheme);
    const std::int32_t ScopedButtonId = ScopedButtons.AddWidget(StretchWidget(WidgetKind::Button));

    std::array<RenderItem, 8> ScopedItems{};
    std::array<RenderKey, 8> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    ScopedButtons.SetFlag(ScopedButtonId, WidgetFlags::Hovered, true);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedButtons, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedItems[0].color == Color32::FromBytes(0x10, 0x80, 0x10).rgba);
    ScopedButtons.SetFlag(ScopedButtonId, WidgetFlags::Hovered, false);
    ScopedButtons.SetFlag(ScopedButtonId, WidgetFlags::Pressed, true);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedButtons, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedItems[0].color == Color32::FromBytes(0x80, 0x10, 0x10).rgba);
    ScopedButtons.SetFlag(ScopedButtonId, WidgetFlags::Pressed, false);
    ScopedButtons.SetFlag(ScopedButtonId, WidgetFlags::Enabled, false);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedButtons, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedItems[0].color == Color32::FromBytes(0x40, 0x40, 0x40).rgba);

    // Fade and slide are presentation-only and inherit through the hierarchy.
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(100.0f, 100.0f));
    const std::int32_t ScopedPanel = ScopedCanvas.AddWidget(StretchWidget(WidgetKind::Panel));
    ScopedCanvas.FadeTo(ScopedPanel, 0.0f, 5.0f);
    ScopedCanvas.AdvanceTransitions(0.1f);
    FBY_CHECK_NEAR(ScopedCanvas.Opacity(ScopedPanel), 0.5f, kTolerance);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 1);
    FBY_CHECK(((ScopedItems[0].color >> 24) & 0xFFu) == 128u);
    ScopedCanvas.AdvanceTransitions(0.2f);
    FBY_CHECK_NEAR(ScopedCanvas.Opacity(ScopedPanel), 0.0f, 0.0f);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 0);

    ScopedCanvas.SetOpacity(ScopedPanel, 1.0f);
    ScopedCanvas.SlideTo(ScopedPanel, V(10.0f, 0.0f), 100.0f);
    ScopedCanvas.AdvanceTransitions(0.05f);
    FBY_CHECK_NEAR(ScopedCanvas.VisualOffset(ScopedPanel).X, 5.0f, kTolerance);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK_NEAR(ScopedItems[0].position.X, 55.0f, kTolerance);

    WidgetCanvas ScopedTree;
    ScopedTree.SetCanvasSize(V(100.0f, 100.0f));
    const std::int32_t ScopedParent = ScopedTree.AddWidget(StretchWidget(WidgetKind::Panel));
    WidgetDesc ScopedChild = WidgetAt(V(10.0f, 10.0f), V(50.0f, 30.0f), WidgetKind::Button, 1);
    ScopedChild.parent = ScopedParent;
    const std::int32_t ScopedChildId = ScopedTree.AddWidget(ScopedChild);
    ScopedTree.SetOpacity(ScopedParent, 0.5f);
    ScopedTree.SetVisualOffset(ScopedParent, V(3.0f, 0.0f));
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedTree, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 2);
    FBY_CHECK(((ScopedItems[1].color >> 24) & 0xFFu) == 128u);
    FBY_CHECK_NEAR(ScopedItems[1].position.X, 33.0f, kTolerance);
    FBY_CHECK(ScopedTree.IsVisible(ScopedChildId));

    // Short control text: right/bottom alignment and word wrap.
    WidgetCanvas ScopedText;
    ScopedText.SetCanvasSize(V(100.0f, 40.0f));
    WidgetDesc ScopedLabel = WidgetAt(V(0.0f, 0.0f), V(100.0f, 40.0f), WidgetKind::Label);
    ScopedLabel.text = "AB";
    ScopedLabel.fontSize = 8;
    ScopedLabel.textFlags = MakeTextFlags(TextAlign::Right, TextVerticalAlign::Bottom);
    (void)ScopedText.AddWidget(ScopedLabel);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedText, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 2);
    FBY_CHECK_VEC(ScopedItems[0].position, V(92.0f, 36.0f));
    FBY_CHECK_VEC(ScopedItems[1].position, V(98.0f, 36.0f));

    WidgetCanvas ScopedWrap;
    ScopedWrap.SetCanvasSize(V(40.0f, 40.0f));
    WidgetDesc ScopedWrapped = WidgetAt(V(0.0f, 0.0f), V(20.0f, 40.0f), WidgetKind::Label);
    ScopedWrapped.text = "AB CD";
    ScopedWrapped.fontSize = 8;
    ScopedWrapped.textFlags = MakeTextFlags(TextAlign::Left, TextVerticalAlign::Top, true);
    (void)ScopedWrap.AddWidget(ScopedWrapped);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedWrap, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 4);
    FBY_CHECK_VEC(ScopedItems[0].position, V(4.0f, 4.0f));
    FBY_CHECK_VEC(ScopedItems[1].position, V(10.0f, 4.0f));
    FBY_CHECK_VEC(ScopedItems[2].position, V(4.0f, 12.0f));
    FBY_CHECK_VEC(ScopedItems[3].position, V(10.0f, 12.0f));
}

// 29. TextBlock draws a TextService block through TextLayout/TextRender
void TestTextBlock(SystemContext& InContext)
{
    FBY_CHECK(InContext.text != nullptr);
    if (InContext.text == nullptr)
    {
        return;
    }
    const Fby::AssetId ScopedTextId = Fby::AssetId::FromPath("ui/test_block.txt", Fby::AssetKind::Text);
    FBY_CHECK(InContext.text->AddAsset(ScopedTextId, "Hello, block!"));
    const Fby::Text::TextAssetRef ScopedRef = InContext.text->MakeRef(ScopedTextId);
    FBY_CHECK(ScopedRef.IsValid());

    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(200.0f, 60.0f));
    WidgetDesc ScopedDesc = StretchWidget(WidgetKind::TextBlock);
    ScopedDesc.textRef = ScopedRef;
    ScopedDesc.fontSize = 8;
    ScopedDesc.textTint = Color32::White();
    const std::int32_t ScopedBlock = ScopedCanvas.AddWidget(ScopedDesc);
    FBY_CHECK(ScopedCanvas.TextRef(ScopedBlock).IsValid());

    std::array<RenderItem, 2048> ScopedItems{};
    std::array<RenderKey, 2048> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten > 5);
    FBY_CHECK(ScopedOutput.itemsWritten == ScopedOutput.requiredItems);
    FBY_CHECK(ScopedOutput.glyphQuads == ScopedOutput.itemsWritten);
    FBY_CHECK(InContext.textShapes != nullptr && InContext.textShapes->Count() > 0);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedOutput.itemsWritten; ++ScopedIndex)
    {
        FBY_CHECK(ScopedItems[ScopedIndex].layer == Fby::Ui::kFontAtlasLayer);
    }

    // Hiding the block removes it; an empty reference draws nothing.
    ScopedCanvas.SetVisible(ScopedBlock, false);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 0);
    ScopedCanvas.SetVisible(ScopedBlock, true);
    ScopedCanvas.SetTextRef(ScopedBlock, Fby::Text::TextAssetRef{});
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 0);

    // A block far beyond the widget scratch is clipped by the viewport cap; the
    // full document path remains TextRenderSystem, not a single UI widget.
    std::array<char, 12000> ScopedLarge{};
    for (char& ScopedByte : ScopedLarge)
    {
        ScopedByte = 'a';
    }
    const Fby::AssetId ScopedLargeId = Fby::AssetId::FromPath("ui/test_large.txt", Fby::AssetKind::Text);
    FBY_CHECK(InContext.text->AddAsset(ScopedLargeId, std::string_view{ScopedLarge.data(), ScopedLarge.size()}));
    ScopedCanvas.SetTextRef(ScopedBlock, InContext.text->MakeRef(ScopedLargeId));
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten > 0);
    FBY_CHECK(ScopedOutput.itemsWritten <= 1024);
    FBY_CHECK(ScopedOutput.requiredItems == ScopedOutput.itemsWritten);

    // Paging a long asset is selecting another TextStore block.
    ScopedCanvas.SetTextBlock(ScopedBlock, 1);
    FBY_CHECK(ScopedCanvas.TextBlock(ScopedBlock) == 1);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten > 0);

    // The shaped line window is the viewport hook for long documents.
    std::array<char, 200> ScopedRows{};
    for (char& ScopedByte : ScopedRows)
    {
        ScopedByte = 'a';
    }
    const Fby::AssetId ScopedRowsId = Fby::AssetId::FromPath("ui/test_rows.txt", Fby::AssetKind::Text);
    FBY_CHECK(InContext.text->AddAsset(ScopedRowsId, std::string_view{ScopedRows.data(), ScopedRows.size()}));
    ScopedCanvas.SetTextRef(ScopedBlock, InContext.text->MakeRef(ScopedRowsId));
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    const std::uint32_t ScopedFullRows = ScopedOutput.itemsWritten;
    FBY_CHECK(ScopedFullRows > 1);
    ScopedCanvas.SetTextLineRange(ScopedBlock, 2, 1);
    FBY_CHECK(ScopedCanvas.TextFirstLine(ScopedBlock) == 2);
    FBY_CHECK(ScopedCanvas.TextLineCount(ScopedBlock) == 1);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten > 0);
    FBY_CHECK(ScopedOutput.itemsWritten < ScopedFullRows);

    // Inline atoms go through the shaped path too: the placeholder cluster is
    // replaced by the atom quad instead of a missing glyph.
    const Fby::AssetId ScopedInlineId = Fby::AssetId::FromPath("ui/test_inline.txt", Fby::AssetKind::Text);
    FBY_CHECK(InContext.text->AddMarkupAsset(ScopedInlineId, "A[icon=7:8:8]B"));
    FBY_CHECK(InContext.text->AddInlineSprite(ScopedInlineId,
                                              Fby::Text::TextInlineSprite{7, 3, 0.25f, 0.5f, 0.75f, 1.0f}));
    FBY_CHECK(InContext.text->InlineSpritesFor(ScopedInlineId).size() == 1);
    FBY_CHECK(InContext.text->InlinesFor(ScopedInlineId).size() == 1);
    InContext.textShapes->Clear();
    ScopedCanvas.SetTextLineRange(ScopedBlock, 0, 0);
    ScopedCanvas.SetTextRef(ScopedBlock, InContext.text->MakeRef(ScopedInlineId));
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten >= 3);
    FBY_CHECK(ScopedItems[1].layer == 3);
    FBY_CHECK_NEAR(ScopedItems[1].uvMin.X, 0.25f, 0.001f);
    FBY_CHECK_NEAR(ScopedItems[1].uvMax.Y, 1.0f, 0.001f);
    FBY_CHECK(InContext.textShapes->Count() > 0);

    // A wide atom participates in shaped wrapping by its own width, not by the
    // placeholder glyph advance.
    const Fby::AssetId ScopedWideInlineId =
        Fby::AssetId::FromPath("ui/test_inline_wide.txt", Fby::AssetKind::Text);
    FBY_CHECK(InContext.text->AddMarkupAsset(ScopedWideInlineId, "A[icon=0:240:8]B"));
    ScopedCanvas.SetTextRef(ScopedBlock, InContext.text->MakeRef(ScopedWideInlineId));
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 3);
    FBY_CHECK(ScopedItems[1].position.Y > ScopedItems[0].position.Y);
    FBY_CHECK(ScopedItems[2].position.Y > ScopedItems[1].position.Y);

    // The codepoint fallback honors the same line window.
    Fby::Text::TextShapeCache* const ScopedSavedShapes = InContext.textShapes;
    Fby::Text::ITextShaper* const ScopedSavedShaper = InContext.textShaper;
    InContext.textShapes = nullptr;
    InContext.textShaper = nullptr;
    ScopedCanvas.SetTextRef(ScopedBlock, InContext.text->MakeRef(ScopedRowsId));
    ScopedCanvas.SetTextLineRange(ScopedBlock, 0, 0);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    const std::uint32_t ScopedFullFallback = ScopedOutput.itemsWritten;
    FBY_CHECK(ScopedFullFallback > 1);
    ScopedCanvas.SetTextLineRange(ScopedBlock, 2, 1);
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten > 0);
    FBY_CHECK(ScopedOutput.itemsWritten < ScopedFullFallback);
    InContext.textShapes = ScopedSavedShapes;
    InContext.textShaper = ScopedSavedShaper;
}

// A test shaper that merges a base codepoint with following combining marks.
class ClusterTestShaper final : public Fby::Text::ITextShaper
{
public:
    [[nodiscard]] bool Shape(std::string_view InText, const Fby::Text::TextShapeParams& InParams,
                             Fby::Text::TextShapeResult* OutResult) noexcept override
    {
        if (OutResult == nullptr)
        {
            return false;
        }
        OutResult->Count = 0;
        std::size_t ScopedCursor = 0;
        while (ScopedCursor < InText.size())
        {
            const std::size_t ScopedCluster = ScopedCursor;
            std::size_t ScopedNext = ScopedCursor;
            const std::uint32_t ScopedCodepoint = Fby::Ui::DecodeUtf8(InText, ScopedNext);
            ScopedCursor = ScopedNext;
            while (ScopedCursor < InText.size())
            {
                std::size_t ScopedLook = ScopedCursor;
                const std::uint32_t ScopedFollowing = Fby::Ui::DecodeUtf8(InText, ScopedLook);
                if (ScopedFollowing < 0x300u || ScopedFollowing > 0x36Fu)
                {
                    break;
                }
                ScopedCursor = ScopedLook;
            }
            if (OutResult->Count >= OutResult->Glyphs.size())
            {
                return false;
            }
            Fby::Text::ShapedGlyph& ScopedGlyph = OutResult->Glyphs[OutResult->Count++];
            ScopedGlyph.GlyphId = ScopedCodepoint;
            ScopedGlyph.Cluster = static_cast<std::uint32_t>(ScopedCluster);
            ScopedGlyph.AdvanceX = InParams.PixelSize * 0.5f;
        }
        return true;
    }
};

// 30. Cluster-aware TextInput editing (grapheme clusters, not codepoints)
void TestClusterEditing()
{
    const std::string_view ScopedText = "a"
                                        "\xCC\x81"
                                        "b";
    ClusterTestShaper ScopedShaper;
    Fby::Text::TextShapeParams ScopedParams;
    ScopedParams.PixelSize = 8.0f;

    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(100.0f, 40.0f));
    WidgetDesc ScopedDesc = StretchWidget(WidgetKind::TextInput);
    ScopedDesc.text = ScopedText;
    ScopedDesc.fontSize = 8;
    const std::int32_t ScopedInput = ScopedCanvas.AddWidget(ScopedDesc);
    FBY_CHECK(ScopedCanvas.Text(ScopedInput).size() == 4);
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 4);

    std::array<std::uint16_t, 16> ScopedOffsets{};
    const auto ScopedRebuild = [&]() noexcept
    {
        return Fby::Ui::BuildTextClusterOffsets(ScopedCanvas.Text(ScopedInput), ScopedShaper, ScopedParams,
                                                ScopedOffsets);
    };
    FBY_CHECK(ScopedRebuild() == 3);
    FBY_CHECK(ScopedOffsets[0] == 0);
    FBY_CHECK(ScopedOffsets[1] == 3);
    FBY_CHECK(ScopedOffsets[2] == 4);

    FBY_CHECK(Fby::Ui::MoveCaretClusters(ScopedCanvas, ScopedInput, -1, false, ScopedOffsets));
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 3);
    FBY_CHECK(Fby::Ui::MoveCaretClusters(ScopedCanvas, ScopedInput, -1, false, ScopedOffsets));
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 0);
    FBY_CHECK(Fby::Ui::MoveCaretClusters(ScopedCanvas, ScopedInput, 1, false, ScopedOffsets));
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 3);

    // Backspace removes the whole a+combining cluster, not just the mark.
    FBY_CHECK(ScopedCanvas.SetCaret(ScopedInput, 4, false));
    FBY_CHECK(ScopedRebuild() == 3);
    FBY_CHECK(Fby::Ui::BackspaceCluster(ScopedCanvas, ScopedInput, ScopedOffsets));
    FBY_CHECK(ScopedCanvas.Text(ScopedInput).size() == 3);
    FBY_CHECK(ScopedCanvas.Caret(ScopedInput) == 3);
    FBY_CHECK(ScopedRebuild() == 2);
    FBY_CHECK(Fby::Ui::BackspaceCluster(ScopedCanvas, ScopedInput, ScopedOffsets));
    FBY_CHECK(ScopedCanvas.Text(ScopedInput).empty());

    // Forward delete removes the cluster after the caret.
    FBY_CHECK(ScopedCanvas.SetText(ScopedInput, ScopedText));
    FBY_CHECK(ScopedCanvas.SetCaret(ScopedInput, 0, false));
    FBY_CHECK(ScopedRebuild() == 3);
    FBY_CHECK(Fby::Ui::DeleteForwardCluster(ScopedCanvas, ScopedInput, ScopedOffsets));
    FBY_CHECK(ScopedCanvas.Text(ScopedInput) == "b");

    // A selection is removed as a whole, and ReadOnly rejects cluster deletes.
    FBY_CHECK(ScopedCanvas.SetText(ScopedInput, ScopedText));
    ScopedCanvas.SelectAll(ScopedInput);
    FBY_CHECK(ScopedRebuild() == 3);
    FBY_CHECK(Fby::Ui::BackspaceCluster(ScopedCanvas, ScopedInput, ScopedOffsets));
    FBY_CHECK(ScopedCanvas.Text(ScopedInput).empty());
    FBY_CHECK(ScopedCanvas.SetText(ScopedInput, "x"));
    ScopedCanvas.SetFlag(ScopedInput, WidgetFlags::ReadOnly, true);
    FBY_CHECK(ScopedRebuild() == 2);
    FBY_CHECK(!Fby::Ui::BackspaceCluster(ScopedCanvas, ScopedInput, ScopedOffsets));
}

// A test shaper that emits RTL glyphs in visual order with one cluster each.
class RtlTestShaper final : public Fby::Text::ITextShaper
{
public:
    [[nodiscard]] bool Shape(std::string_view InText, const Fby::Text::TextShapeParams& InParams,
                             Fby::Text::TextShapeResult* OutResult) noexcept override
    {
        if (OutResult == nullptr)
        {
            return false;
        }
        OutResult->Count = 0;
        std::array<std::uint32_t, 64> ScopedCodepoints{};
        std::array<std::uint32_t, 64> ScopedClusters{};
        std::uint32_t ScopedCount = 0;
        std::size_t ScopedCursor = 0;
        while (ScopedCursor < InText.size() && ScopedCount < ScopedCodepoints.size())
        {
            ScopedClusters[ScopedCount] = static_cast<std::uint32_t>(ScopedCursor);
            ScopedCodepoints[ScopedCount] = Fby::Ui::DecodeUtf8(InText, ScopedCursor);
            ++ScopedCount;
        }
        for (std::uint32_t ScopedIndex = ScopedCount; ScopedIndex > 0; --ScopedIndex)
        {
            Fby::Text::ShapedGlyph& ScopedGlyph = OutResult->Glyphs[OutResult->Count++];
            ScopedGlyph.GlyphId = ScopedCodepoints[ScopedIndex - 1];
            ScopedGlyph.Cluster = ScopedClusters[ScopedIndex - 1];
            ScopedGlyph.AdvanceX = InParams.PixelSize;
        }
        return true;
    }
};

// 31. RTL TextInput: visual-order glyphs, mirrored caret and selection
void TestRtlTextInput(SystemContext& InContext)
{
    Fby::Text::ITextShaper* const ScopedPreviousShaper = InContext.textShaper;
    RtlTestShaper ScopedShaper;
    InContext.textShaper = &ScopedShaper;
    InContext.textShapes->Clear();

    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(V(100.0f, 40.0f));
    WidgetDesc ScopedDesc = StretchWidget(WidgetKind::TextInput);
    ScopedDesc.text = "AB";
    ScopedDesc.fontSize = 8;
    ScopedDesc.flags |= static_cast<std::uint32_t>(WidgetFlags::TextRightToLeft);
    const std::int32_t ScopedInput = ScopedCanvas.AddWidget(ScopedDesc);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedInput);

    std::array<RenderItem, 16> ScopedItems{};
    std::array<RenderKey, 16> ScopedKeys{};
    std::array<DrawBatch, 1> ScopedBatches{};
    FrameOutput ScopedOutput;
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    // Background, B (rightmost), A, caret at the logical end (left edge).
    FBY_CHECK(ScopedOutput.itemsWritten == 4);
    FBY_CHECK_VEC(ScopedItems[1].position, V(88.0f, 4.0f));
    FBY_CHECK_VEC(ScopedItems[2].position, V(96.0f, 4.0f));
    FBY_CHECK_NEAR(ScopedItems[3].position.X, 84.0f, 0.01f);

    // Selection [0, 2) spans the whole string; the caret moves to the right.
    FBY_CHECK(ScopedCanvas.SetCaret(ScopedInput, 0, true));
    ScopedOutput = FrameOutput{};
    ScopedOutput.items = std::span<RenderItem>(ScopedItems);
    ScopedOutput.keys = std::span<RenderKey>(ScopedKeys);
    ScopedOutput.batches = std::span<DrawBatch>(ScopedBatches);
    FBY_CHECK(WidgetCanvasSystem::BuildFrame(InContext, ScopedCanvas, ScopedOutput) == BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 5);
    FBY_CHECK_VEC(ScopedItems[1].position, V(92.0f, 4.0f));
    FBY_CHECK_VEC(ScopedItems[1].size, V(16.0f, 8.0f));

    InContext.textShaper = ScopedPreviousShaper;
}

// 32. TextRuntime owns the two large text stores for a host
void TestTextRuntime()
{
    Fby::Text::TextRuntime ScopedRuntime;
    FBY_CHECK(ScopedRuntime.Shaper() == nullptr);
    Fby::Text::CodepointShaper ScopedShaper;
    ScopedRuntime.SetShaper(&ScopedShaper);
    FBY_CHECK(ScopedRuntime.Shaper() == &ScopedShaper);
    const Fby::AssetId ScopedId = Fby::AssetId::FromPath("ui/runtime.txt", Fby::AssetKind::Text);
    FBY_CHECK(ScopedRuntime.Service().AddAsset(ScopedId, "Hello"));
    FBY_CHECK(ScopedRuntime.Service().MakeRef(ScopedId).IsValid());
    FBY_CHECK(ScopedRuntime.Shapes().Count() == 0);
}
}

int main()
{
    Fby::Ecs::Registry ScopedRegistry;
    // Threads are idle for every test; only Update needs a context
    Fby::JobSystem ScopedJobs;
    SystemContext ScopedContext{ScopedRegistry, ScopedJobs};

    // Strict Pink UI contract: text requires a mounted font texture array. The
    Fby::Rhi::DeviceDesc ScopedGpuDesc;
    ScopedGpuDesc.backend = Fby::Rhi::Backend::Null;
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedGpuDesc);
    if (ScopedGpu == nullptr)
    {
        std::fprintf(stderr, "FAIL: the null RHI could not be created\n");
        return 1;
    }
    const std::vector<std::byte> ScopedFontBytes = MakeDemoFontAtlas();
    Fby::Ui::FontAtlasSet ScopedFonts;
    if (!ScopedFonts.Add(ScopedFontBytes))
    {
        std::fprintf(stderr, "FAIL: the test font atlas was rejected\n");
        return 1;
    }
    Fby::Ui::WidgetCanvasSystem ScopedCanvasSystem{&ScopedFonts};
    if (!ScopedCanvasSystem.MountFonts(*ScopedGpu))
    {
        std::fprintf(stderr, "FAIL: the test font atlas did not mount\n");
        return 1;
    }
    ScopedContext.renderer = ScopedGpu.get();
    ScopedContext.fonts = &ScopedFonts;
    Fby::Text::TextRuntime ScopedTextRuntime;
    Fby::Text::CodepointShaper ScopedShaper;
    ScopedTextRuntime.SetShaper(&ScopedShaper);
    ScopedContext.text = &ScopedTextRuntime.Service();
    ScopedContext.textShapes = &ScopedTextRuntime.Shapes();
    ScopedContext.textShaper = ScopedTextRuntime.Shaper();

    TestDataModel();
    TestGridView();
    TestGridLayout();
    TestAnchorsAndNesting();
    TestHitTestOrder();
    TestPointerAndFocus();
    TestFontAndText();
    TestBuildFramePacking(ScopedContext);
    TestCapacityOverflow(ScopedContext);
    TestNoAllocation(ScopedContext);
    TestEcsUpdate(ScopedContext);
    TestCanvasCopy();
    TestElementKinds(ScopedContext);
    TestSliderDrag();
    TestCheckboxToggle();
    TestVerticalProgressBar(ScopedContext);
    TestBorderStrokeClamp(ScopedContext);
    TestFocusTraversal();
    TestVisibility(ScopedContext);
    TestSwitcher(ScopedContext);
    TestCustomWidget(ScopedContext);
    TestFocusArrows();
    TestClipRect(ScopedContext);
    TestTooltip(ScopedContext);
    TestTextInput(ScopedContext);
    TestElementExtended(ScopedContext);
    TestFocusRing(ScopedContext);
    TestStateAndTransitions(ScopedContext);
    TestTextBlock(ScopedContext);
    TestClusterEditing();
    TestRtlTextInput(ScopedContext);
    TestTextRuntime();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "pink_ui_test: %zu check(s) failed\n", GFailures);
        return 1;
    }
    std::printf("pink_ui_test: all checks passed\n");
    return 0;
}
