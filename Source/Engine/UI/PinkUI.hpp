// FemboyEngine2D - Pink UI: the data-oriented widget canvas.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/Text/TextAsset.hpp"
#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/UiTheme.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#if defined(__cpp_lib_mdspan) && __cpp_lib_mdspan >= 202207L
#include <mdspan>
#define FBY_UI_HAS_MDSPAN 1
#else
#define FBY_UI_HAS_MDSPAN 0
#endif

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Text
{
class ITextShaper;
struct TextShapeParams;
}

namespace Fby
{
class LocalizationTable;
}

namespace Fby::Ui
{

// Capacities
inline constexpr std::uint32_t kMaxWidgets = 512;
inline constexpr std::uint32_t kMaxGlyphs = 4096;
// The widget text pool is for short control strings. Long-form, rich, inline and
// shaped text belongs to Fby::Text (TextService/TextLayout/TextRender), never to
// this pool.
inline constexpr std::uint32_t kMaxTextPool = 8192;
inline constexpr std::uint32_t kMaxChildrenPerWidget = 32;
inline constexpr std::uint32_t kMaxGridColumns = 16;
inline constexpr std::uint32_t kMaxGridRows = 32;

// Texture-array slices used by a canvas. Layer 0 is the white texel the cold
inline constexpr std::uint32_t kWhiteLayer = 0;
inline constexpr std::uint32_t kFontAtlasLayer = 1;

// Font atlas geometry: ASCII 32..126 is 95 glyphs, laid out left-to-right in a
inline constexpr std::uint32_t kFontAtlasColumns = 16;
inline constexpr std::uint32_t kFontAtlasRows = 6;
inline constexpr std::uint32_t kFontFirstChar = 32;
inline constexpr std::uint32_t kFontLastChar = 126;
inline constexpr std::uint32_t kFontGlyphCount = kFontLastChar - kFontFirstChar + 1;

// A monospace cell is 8x8 but the glyph itself is ~6 px wide, so the pen
inline constexpr float kGlyphAdvanceRatio = 0.75f;
inline constexpr float kTextLineHeightRatio = 1.0f;
inline constexpr std::uint16_t kDefaultFontSize = 12;

// Sentinels. kNoWidget is "no widget index", kNoCell is "not placed in a grid
inline constexpr std::int32_t kNoWidget = -1;
inline constexpr std::int32_t kNoCell = -1;
inline constexpr std::uint16_t kNoCellTrack = 0xFFFFu;

// A pointer-free widget reference. The slot index is stable for the lifetime of
// a canvas (there is no removal API) and the generation invalidates handles
// across Clear(), so a handle can cross an ECS boundary without a pointer.
struct WidgetHandle
{
    std::int32_t widget = kNoWidget;
    std::uint32_t generation = 0;
};

[[nodiscard]] constexpr bool IsHandleSet(WidgetHandle InHandle) noexcept
{
    return InHandle.widget != kNoWidget && InHandle.generation != 0;
}

// Sorting range for per-widget z-order. The render key stores
inline constexpr std::int32_t kMinZOrder = -32;
inline constexpr std::int32_t kMaxZOrder = 31;

// Accent colour of the module. A default, not a theme: the editor owns the
inline constexpr Color32 kPinkAccent = Color32::FromBytes(0xFF, 0x4F, 0xA8, 0xFF);

// Widget kinds
enum class WidgetKind : std::uint8_t
{
    // One solid quad in the tint colour
    Panel = 0,
    // Panel + pressed/hover tint modulation + focusable by default
    Button,
    // One quad per glyph of the text id
    Label,
    // One quad with the widget's layer + uv rect
    Image,
    // Background quad + fill quad (fill width = progress * width)
    ProgressBar,
    // Proportional/mono label with clickable link ranges
    Hypertext,
    // No quad, participates in layout
    Spacer,
    // Four-quad stroke frame; borderWidth is the thickness of each side
    Border,
    // Track + fill + knob; progress is the value and a drag sets it
    Slider,
    // Box + check mark; the Checked flag carries the state
    Checkbox,
    // Track + thumb; ratio is the visible fraction and progress the offset
    Scrollbar,
    // No quad; only the child selected by a WidgetHandle is shown and hit-tested
    Switcher,
    // Full-rect scrim. With the Modal flag it also traps focus in its subtree
    Overlay,
    // Application-painted quads through a CustomWidgetPaint callback
    Custom,
    // Background plus centred text, shown after the target hover delay
    Tooltip,
    // Editable single-line field with caret and selection
    TextInput,
    // Nine quads from one texture with clamped corner insets
    NineSlice,
    // Pill track plus a left/right knob; the Checked flag is the state
    ToggleSwitch,
    // Box plus dot; selecting one clears the others in its radioGroup
    RadioButton,
    // Long/rich text block rendered through Fby::Text (TextService/TextRender)
    TextBlock,
    Count
};

// Per-widget state bits. Fixed-width bit flags, not booleans: a flag update is
enum class WidgetFlags : std::uint32_t
{
    None = 0,
    Visible = 1u << 0,
    Enabled = 1u << 1,
    Focusable = 1u << 2,
    // PointerDown set it, PointerUp has not cleared it
    Pressed = 1u << 3,
    // Set by PointerMove; the app may also set it directly
    Hovered = 1u << 4,
    IgnoreHitTest = 1u << 5,
    // ProgressBar/Slider/Scrollbar grow bottom-up instead of left-to-right
    Vertical = 1u << 6,
    // Checkbox state, toggled by a completed click
    Checked = 1u << 7,
    // Overlay traps keyboard focus inside its subtree while it is shown
    Modal = 1u << 8,
    // Descendants are clipped to this widget's rect (scroll views, masks)
    ClipChildren = 1u << 9,
    // A Tooltip stays visible without a hover (debug/inspection or a pinned card)
    Pinned = 1u << 10,
    // A TextInput rejects edits while this bit is set
    ReadOnly = 1u << 11,
    // TextBlock layout direction; the glyph order still comes from Fby::Text
    TextRightToLeft = 1u << 12,
    TextVertical = 1u << 13,
};

[[nodiscard]] constexpr std::uint32_t operator|(WidgetFlags InA, WidgetFlags InB) noexcept
{
    return static_cast<std::uint32_t>(InA) | static_cast<std::uint32_t>(InB);
}
[[nodiscard]] constexpr std::uint32_t operator|(std::uint32_t InA, WidgetFlags InB) noexcept
{
    return InA | static_cast<std::uint32_t>(InB);
}
[[nodiscard]] constexpr bool HasFlag(std::uint32_t InFlags, WidgetFlags InValue) noexcept
{
    return (InFlags & static_cast<std::uint32_t>(InValue)) != 0;
}

// Focus is opt-in. None keeps a widget out of the keyboard loop entirely.
enum class FocusPolicy : std::uint8_t
{
    None = 0,
    // Click focuses the widget, but Tab skips it
    Pointer = 1,
    // Tab reaches the widget, but a click does not focus it
    Tab = 2,
    // Both pointer and Tab reach the widget
    TabAndPointer = 3,
};

inline constexpr std::uint16_t kNoFocusGroup = 0xFFFFu;

[[nodiscard]] constexpr bool HasPointerFocus(FocusPolicy InPolicy) noexcept
{
    return InPolicy == FocusPolicy::Pointer || InPolicy == FocusPolicy::TabAndPointer;
}

[[nodiscard]] constexpr bool HasTabFocus(FocusPolicy InPolicy) noexcept
{
    return InPolicy == FocusPolicy::Tab || InPolicy == FocusPolicy::TabAndPointer;
}

// Arrow-key traversal. Left/Right move on X, Up/Down move on Y.
enum class FocusDirection : std::uint8_t
{
    Left = 0,
    Right,
    Up,
    Down,
};

// Text layout flags stored in WidgetDesc::textFlags and the canvas textFlags
// array. Bits 0..1 are horizontal alignment, bits 2..3 vertical alignment and
// bit 4 enables word wrap inside the widget rect.
enum class TextAlign : std::uint8_t
{
    Left = 0,
    Centre = 1,
    Right = 2,
};

enum class TextVerticalAlign : std::uint8_t
{
    Top = 0,
    Middle = 1,
    Bottom = 2,
};

inline constexpr std::uint32_t kTextAlignMask = 0x3u;
inline constexpr std::uint32_t kTextVerticalShift = 2u;
inline constexpr std::uint32_t kTextVerticalMask = 0x3u << kTextVerticalShift;
inline constexpr std::uint32_t kTextWrap = 1u << 4;

[[nodiscard]] constexpr std::uint16_t MakeTextFlags(TextAlign InAlign, TextVerticalAlign InVertical,
                                                     bool InWrap = false) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint32_t>(InAlign) & kTextAlignMask) |
                                      ((static_cast<std::uint32_t>(InVertical) << kTextVerticalShift) &
                                       kTextVerticalMask) |
                                      (InWrap ? kTextWrap : 0u));
}

// WidgetComponent - the ECS attachment.
struct WidgetComponent
{
    // Index into the app's WidgetCanvas pool
    std::uint32_t CanvasID = 0;
    // RenderKey layer bucket for the whole canvas
    std::uint32_t Layer = 0;
    // WidgetComponentFlags
    std::uint32_t Flags = 0;
    // App-fed clock; a system may animate progress
    float AnimationTime = 0.0f;
};

enum class WidgetComponentFlags : std::uint32_t
{
    None = 0,
    // Skip the canvas in WidgetCanvasSystem::Update
    Visible = 1u << 0,
    // Hint: the app's camera should not transform it
    ScreenSpace = 1u << 1,
};

static_assert(std::is_trivially_copyable_v<WidgetComponent>, "WidgetComponent must be memcpy-able");
static_assert(std::is_standard_layout_v<WidgetComponent>, "WidgetComponent must have a stable layout");
static_assert(sizeof(WidgetComponent) == 16, "WidgetComponent is two ids and two floats");

// The UI works in abstract Units, never in monitor pixels. One Unit is one
using Units = float;

[[nodiscard]] constexpr Units PixelsToUnits(float InPixels, float InDisplayScale) noexcept
{
    return InDisplayScale > 0.0f ? InPixels / InDisplayScale : InPixels;
}

[[nodiscard]] constexpr float UnitsToPixels(Units InUnits, float InDisplayScale) noexcept
{
    return InUnits * (InDisplayScale > 0.0f ? InDisplayScale : 1.0f);
}

// WidgetDesc - the insertion record for WidgetCanvas::AddWidget().
struct WidgetCanvas;
struct WidgetPaintContext;

// A custom painter receives the resolved rect and a quad emitter. BuildFrame
// runs it twice (budget, then emit), so it must be deterministic and must not
// call the mutating canvas APIs.
using CustomWidgetPaint = void (*)(WidgetPaintContext& InContext) noexcept;

inline constexpr std::uint32_t kMaxCustomQuadsPerWidget = 64;

struct WidgetHyperlink
{
    std::uint16_t begin = 0;
    std::uint16_t end = 0;
    std::uint32_t target = 0;
    std::uint16_t flags = 0;
    std::uint16_t reserved = 0;
};

struct WidgetDesc
{
    WidgetKind kind = WidgetKind::Panel;
    // KNoWidget = canvas root
    std::int32_t parent = kNoWidget;
    // Normalized inside the reference box
    Vector2D anchorMin{0.0f, 0.0f};
    // Default: stretch to fill
    Vector2D anchorMax{1.0f, 1.0f};
    // Min-edge inset in canvas pixels
    Vector2D offsetMin{0.0f, 0.0f};
    // Max-edge inset (negative grows inward)
    Vector2D offsetMax{0.0f, 0.0f};
    // Panel/button/image/label colour. A palette index overrides this literal
    Color32 tint = Color32::White();
    std::uint16_t tintPalette = kPaletteInherit;
    // Button caption colour. A palette index overrides this literal
    Color32 textTint = Color32::White();
    std::uint16_t textPalette = kPaletteInherit;
    // Progress-bar fill colour
    Color32 fillTint = kPinkAccent;
    std::uint16_t fillPalette = kPaletteInherit;
    // Texture-array slice; Labels force the font layer
    std::uint32_t layer = kWhiteLayer;
    // Image source rect
    Vector2D uvMin{0.0f, 0.0f};
    Vector2D uvMax{1.0f, 1.0f};
    // Labels only; copied into the char pool
    std::string_view text{};
    std::uint32_t fontSize = kDefaultFontSize;
    // Reserved for alignment/shadows
    std::uint32_t textFlags = 0;
    // ProgressBar fill in [0, 1]
    float progress = 0.0f;
    // Border stroke thickness in Units
    float borderWidth = 1.0f;
    // Slider knob length in Units; 0 selects an automatic knob
    float knobSize = 0.0f;
    // Scrollbar thumb fraction of the track, in [0, 1]
    float ratio = 0.25f;
    // Keyboard focus policy. None (the default) means the widget cannot focus
    FocusPolicy focusPolicy = FocusPolicy::None;
    // Tab order key; ties break by insertion index
    std::int32_t tabIndex = 0;
    // Focus group filter key; kNoFocusGroup keeps the widget ungrouped
    std::uint16_t focusGroup = kNoFocusGroup;
    // Custom only: painter, opaque state and four float parameters
    CustomWidgetPaint customPaint = nullptr;
    void* customUser = nullptr;
    Vector4D customParams{};
    // Tooltip only: the hover source and the visible delay in seconds
    WidgetHandle tooltipTarget{};
    float tooltipDelay = 0.5f;
    // TextBlock only: a 16-byte stable reference into the Fby::Text store
    Text::TextAssetRef textRef{};
    // NineSlice corner insets in Units: X left, Y top, Z right, W bottom
    Vector4D sliceInsets{10.0f, 10.0f, 10.0f, 10.0f};
    // RadioButton group; kNoFocusGroup disables mutual exclusion
    std::uint16_t radioGroup = kNoFocusGroup;
    // Visual-only transition state. Layout stays where the anchors put it.
    float opacity = 1.0f;
    Vector2D visualOffset{};
    // Opacity per second and Units per second for FadeTo/SlideTo
    float transitionSpeed = 8.0f;
    // Draw/hit-test stacking, -32..31
    std::int32_t zOrder = 0;
    // Cell placement; both row and column
    std::int32_t gridRow = kNoCell;
    // Must be set to use the grid
    std::int32_t gridColumn = kNoCell;
    std::uint32_t rowSpan = 1;
    std::uint32_t columnSpan = 1;
    // Hypertext only. Ranges index the UTF-8 bytes copied into the pool; target
    std::span<const WidgetHyperlink> hyperlinks{};
    // FontAtlasSet index; 0 = primary font
    std::uint16_t fontAtlas = 0;
    std::uint32_t flags =
        static_cast<std::uint32_t>(WidgetFlags::Visible) | static_cast<std::uint32_t>(WidgetFlags::Enabled);
};

// The paint surface handed to a CustomWidgetPaint callback. The emitter is
// valid only for the duration of the call; the callback must not store it.
struct WidgetPaintContext
{
    const WidgetCanvas& Canvas;
    std::int32_t Widget = kNoWidget;
    Rect LocalRect{};
    // Four floats the widget author owns; no engine meaning
    Vector4D Params{};
    void* User = nullptr;
    Color32 Tint = Color32::White();
    Color32 Fill = Color32::White();
    std::uint32_t Layer = 0;
    bool (*Emit)(void* InState, Rect InLocal, std::uint32_t InLayer, std::uint32_t InColor, Vector2D InUvMin,
                 Vector2D InUvMax, float InRotation) noexcept = nullptr;
    void* EmitState = nullptr;

    // Writes one quad with the full source rect in the context layer.
    [[nodiscard]] bool Quad(Rect InLocal, Color32 InColor) const noexcept
    {
        return Emit != nullptr && Emit(EmitState, InLocal, Layer, InColor.rgba, Vector2D{0.0f, 0.0f},
                                       Vector2D{1.0f, 1.0f}, 0.0f);
    }
    // Writes one quad with an explicit source rect and local rotation.
    [[nodiscard]] bool Quad(Rect InLocal, Color32 InColor, Vector2D InUvMin, Vector2D InUvMax,
                            float InRotation = 0.0f) const noexcept
    {
        return Emit != nullptr && Emit(EmitState, InLocal, Layer, InColor.rgba, InUvMin, InUvMax, InRotation);
    }
};

// GridView - the 2D view over the canvas grid track table.
#if FBY_UI_HAS_MDSPAN
using GridView = std::mdspan<const float, std::dextents<std::size_t, 2>>;
#else
class GridView
{
public:
    constexpr GridView() = default;
    constexpr GridView(const float* data, std::size_t rows, std::size_t columns) noexcept
        : MData(data), MRows(rows), MColumns(columns)
    {
    }

    [[nodiscard]] static constexpr std::size_t rank() noexcept
    {
        return 2;
    }
    [[nodiscard]] constexpr std::size_t extent(std::size_t axis) const noexcept
    {
        return axis == 0 ? MRows : MColumns;
    }
    [[nodiscard]] constexpr const float& operator()(std::size_t row, std::size_t column) const noexcept
    {
        return MData[row * MColumns + column];
    }
    // C++26 mdspan exposes multidimensional subscript instead of operator();
    [[nodiscard]] constexpr const float& operator[](std::size_t row, std::size_t column) const noexcept
    {
        return MData[row * MColumns + column];
    }
    [[nodiscard]] constexpr const float* data() const noexcept
    {
        return MData;
    }

private:
    const float* MData = nullptr;
    std::size_t MRows = 0;
    std::size_t MColumns = 0;
};
#endif

// OLED pixel shifter
struct OledShiftState
{
    bool enabled = true;
    // Seconds since the last step
    float accumulator = 0.0f;
    // 0..7
    std::uint32_t step = 0;
    // Current integer pixel offset
    Vector2D offset{};

    static constexpr float kIntervalSeconds = 60.0f;
    // Points on the 2-px orbit
    static constexpr std::uint32_t kSteps = 8;
    // Zero + eight orbit points
    static constexpr std::uint32_t kCycleSteps = kSteps + 1;
};

[[nodiscard]] Vector2D OledOrbitOffset(std::uint32_t InStep) noexcept;

// WidgetCanvas - the flat widget data model.
struct WidgetCanvas;

// Declared ahead of the struct because EnsureLayout() is defined inline and
void ResolveLayout(const WidgetCanvas& InCanvas) noexcept;

// Vertical scroll controller for a clipped Panel + content + optional
// Scrollbar. Bind once, then feed the window's wheel delta every frame; the
// content's top offset and the scrollbar thumb follow the clamped scroll.
struct ScrollView
{
    std::int32_t panel = kNoWidget;
    std::int32_t content = kNoWidget;
    std::int32_t scrollbar = kNoWidget;
    Vector2D contentBaseMin{};
    Vector2D contentBaseMax{};
    float offset = 0.0f;
    bool bound = false;

    void Bind(WidgetCanvas& InCanvas, WidgetHandle InPanel, WidgetHandle InContent,
              WidgetHandle InScrollbar = {});
    // Wheel is the platform's per-frame vertical scroll (positive = up).
    void Update(WidgetCanvas& InCanvas, float InWheelDelta);
    [[nodiscard]] float MaxOffset(const WidgetCanvas& InCanvas) const noexcept;
    [[nodiscard]] float Offset() const noexcept
    {
        return offset;
    }
};

// Tab strip over a Switcher: each button maps to a page index. Bind the
// switcher, add the buttons in page order, and pass the widget returned by
// PointerUp to HandleClick.
struct TabBar
{
    static constexpr std::uint32_t kMaxTabs = 16;
    std::int32_t switcher = kNoWidget;
    std::array<std::int32_t, kMaxTabs> tabs{};
    std::array<Color32, kMaxTabs> baseTints{};
    std::uint32_t tabCount = 0;
    Color32 activeTint = Color32::White();
    bool bound = false;

    void Bind(WidgetCanvas& InCanvas, WidgetHandle InSwitcher);
    bool AddTab(WidgetCanvas& InCanvas, WidgetHandle InButton);
    bool HandleClick(WidgetCanvas& InCanvas, std::int32_t InPressedWidget);
    [[nodiscard]] std::uint32_t ActiveTab(const WidgetCanvas& InCanvas) const noexcept;
};

// Dropdown/ComboBox: a Button trigger plus a popup Panel holding Button rows.
// Clicking the trigger toggles the popup; clicking an item selects it, copies
// its caption into the trigger and closes the popup.
struct Dropdown
{
    static constexpr std::uint32_t kMaxItems = 32;
    std::int32_t trigger = kNoWidget;
    std::int32_t popup = kNoWidget;
    std::array<std::int32_t, kMaxItems> items{};
    std::array<Color32, kMaxItems> baseTints{};
    std::uint32_t itemCount = 0;
    std::uint32_t selected = 0;
    Color32 activeTint = Color32::White();
    bool open = false;
    bool bound = false;

    void Bind(WidgetCanvas& InCanvas, WidgetHandle InTrigger, WidgetHandle InPopup);
    bool AddItem(WidgetCanvas& InCanvas, WidgetHandle InItem);
    bool HandleClick(WidgetCanvas& InCanvas, std::int32_t InPressedWidget);
    void SetOpen(WidgetCanvas& InCanvas, bool InOpen);
    [[nodiscard]] std::uint32_t SelectedItem() const noexcept
    {
        return selected;
    }
};

// Virtualised vertical list: a fixed pool of row widgets inside a clipped
// content panel. Only the visible items are positioned and shown; the caller
// writes the label for each row after Update.
struct ListView
{
    static constexpr std::uint32_t kMaxRows = 64;
    std::int32_t viewport = kNoWidget;
    std::int32_t content = kNoWidget;
    std::array<std::int32_t, kMaxRows> rows{};
    std::uint32_t rowPool = 0;
    std::uint32_t itemCount = 0;
    float rowHeight = 24.0f;
    float offset = 0.0f;
    bool bound = false;

    void Bind(WidgetCanvas& InCanvas, WidgetHandle InViewport, WidgetHandle InContent,
              std::uint32_t InRowCapacity);
    void SetItemCount(WidgetCanvas& InCanvas, std::uint32_t InItemCount);
    void Update(WidgetCanvas& InCanvas, float InWheelDelta);
    [[nodiscard]] std::uint32_t FirstVisible() const noexcept;
    [[nodiscard]] std::uint32_t ItemAtRow(std::uint32_t InRow) const noexcept;
    [[nodiscard]] std::int32_t RowWidget(std::uint32_t InRow) const noexcept;
};

// Radial progress / cooldown ring painted with the custom-quad path. The ring
// is drawn as one rotated quad per segment, so the cost stays inside
// kMaxCustomQuadsPerWidget and no new render pipeline is needed.
struct RadialProgress
{
    static void Paint(WidgetPaintContext& InContext) noexcept;
    static void Configure(WidgetCanvas& InCanvas, WidgetHandle InWidget, float InProgress,
                          float InThicknessRatio = 0.22f, std::uint32_t InSegments = 24,
                          float InStartAngle = -1.57079633f);
    static void SetProgress(WidgetCanvas& InCanvas, WidgetHandle InWidget, float InProgress);
};

// Auto-placement for a Tooltip: positions the tooltip beside its target and
// flips to the other side when the canvas edge would clip it.
struct TooltipPlacer
{
    std::int32_t tooltip = kNoWidget;
    std::int32_t target = kNoWidget;
    Vector2D offset{12.0f, 8.0f};
    Vector2D tooltipSize{};
    bool bound = false;

    void Bind(WidgetCanvas& InCanvas, WidgetHandle InTooltip, WidgetHandle InTarget);
    void Place(WidgetCanvas& InCanvas);
};

struct WidgetCanvas
{
    // Canvas state
    Vector2D size{1920.0f, 1080.0f};
    // Safe-area insets applied to the root reference box (notches, rounded
    // corners, TV overscan). Child widgets keep their normal anchors inside it.
    Vector2D safeAreaMin{};
    Vector2D safeAreaMax{};
    // DPI/display scale applied by WidgetCanvasSystem. 1.0 on a 96 DPI display.
    float displayScale = 1.0f;
    // When true, Update() derives the Unit reference size from the window's
    bool followDisplayScale = true;
    // Global palette used by every palette-indexed widget in this canvas.
    UiThemeAsset theme = DefaultUiTheme();
    // RenderKey layer bucket. 16-bit by design: MakeRenderKey reserves 16 bits
    std::uint16_t sortLayer = 0;
    // Explicit pad: keeps the following 32-bit ids naturally aligned without
    std::uint16_t reserved0 = 0;
    // DrawBatch texture-array binding: a full 32-bit Rhi::TextureArrayHandle::id.
    std::uint32_t textureArray = 0;
    // DrawBatch pipeline id: a full 32-bit Rhi::ShaderPipelineHandle::id.
    std::uint32_t pipeline = 0;
    // Reserved
    std::uint32_t canvasFlags = 0;
    std::uint32_t widgetCount = 0;
    // Incremented by Clear(); WidgetHandle generations are stamped with it
    std::uint32_t generation = 1;
    std::uint32_t textUsed = 0;
    std::int32_t focusIndex = kNoWidget;
    // Widget that owns the pointer capture for a Slider/Scrollbar drag
    std::int32_t dragIndex = kNoWidget;
    // Programmatic focus trap; kNoWidget keeps the modal-overlay rule active
    std::int32_t focusScope = kNoWidget;
    // Tab from the last candidate wraps to the first
    bool focusWrap = true;
    // Only widgets in this group take Tab focus; kNoFocusGroup disables it
    std::uint16_t focusGroupFilter = kNoFocusGroup;
    std::uint16_t reserved1 = 0;
    // Optional focus ring drawn around the focused widget, outside its rect
    bool drawFocusRing = false;
    float focusRingWidth = 2.0f;
    // Canvas -> world, baked from Transform2D
    Vector2D worldPosition{};
    Vector2D worldScale{1.0f, 1.0f};
    float worldRotation = 0.0f;
    // Integer orbit, advanced by the system
    OledShiftState oled{};
    // Seconds since the canvas started; drives the TextInput caret blink
    float uiTime = 0.0f;

    // Grid description
    static constexpr std::uint32_t kTrackCapacity = kMaxGridColumns > kMaxGridRows ? kMaxGridColumns : kMaxGridRows;
    std::array<std::array<float, kTrackCapacity>, 2> tracks{};
    std::uint32_t gridColumnCount = 0;
    std::uint32_t gridRowCount = 0;
    Vector2D gridGap{0.0f, 0.0f};
    Vector2D gridPadding{0.0f, 0.0f};

    // SoA widget attributes, index = widget id
    std::array<WidgetKind, kMaxWidgets> kinds{};
    std::array<std::uint32_t, kMaxWidgets> flags{};
    std::array<std::int32_t, kMaxWidgets> parents{};
    std::array<std::int32_t, kMaxWidgets> firstChild{};
    std::array<std::int32_t, kMaxWidgets> nextSibling{};
    std::array<std::uint16_t, kMaxWidgets> childCounts{};
    std::array<Vector2D, kMaxWidgets> anchorMins{};
    std::array<Vector2D, kMaxWidgets> anchorMaxs{};
    std::array<Vector2D, kMaxWidgets> offsetMins{};
    std::array<Vector2D, kMaxWidgets> offsetMaxs{};
    std::array<Color32, kMaxWidgets> tints{};
    std::array<Color32, kMaxWidgets> fillTints{};
    std::array<std::uint16_t, kMaxWidgets> tintPalettes{};
    std::array<std::uint16_t, kMaxWidgets> fillPalettes{};
    std::array<Color32, kMaxWidgets> textTints{};
    std::array<std::uint16_t, kMaxWidgets> textPalettes{};
    std::array<std::uint32_t, kMaxWidgets> layers{};
    std::array<Vector2D, kMaxWidgets> uvMins{};
    std::array<Vector2D, kMaxWidgets> uvMaxs{};
    std::array<FocusPolicy, kMaxWidgets> focusPolicies{};
    std::array<std::int32_t, kMaxWidgets> tabIndices{};
    std::array<std::uint16_t, kMaxWidgets> focusGroups{};
    std::array<std::uint16_t, kMaxWidgets> childOrdinals{};
    // Active child of every Switcher, stored as a pointer-free handle
    std::array<WidgetHandle, kMaxWidgets> switcherActive{};
    std::array<CustomWidgetPaint, kMaxWidgets> customPaints{};
    std::array<void*, kMaxWidgets> customUsers{};
    std::array<Vector4D, kMaxWidgets> customParams{};
    std::array<WidgetHandle, kMaxWidgets> tooltipTargets{};
    std::array<float, kMaxWidgets> tooltipDelays{};
    std::array<Text::TextAssetRef, kMaxWidgets> textRefs{};
    std::array<std::uint16_t, kMaxWidgets> textFirstLines{};
    std::array<std::uint16_t, kMaxWidgets> textLineCounts{};
    std::array<Vector4D, kMaxWidgets> slices{};
    std::array<std::uint16_t, kMaxWidgets> radioGroups{};
    std::array<float, kMaxWidgets> opacities{};
    std::array<float, kMaxWidgets> opacityTargets{};
    std::array<Vector2D, kMaxWidgets> visualOffsets{};
    std::array<Vector2D, kMaxWidgets> visualOffsetTargets{};
    std::array<float, kMaxWidgets> transitionSpeeds{};
    // Seconds the pointer has rested on the widget, advanced by AdvanceHover
    std::array<float, kMaxWidgets> hoverSeconds{};
    std::array<std::uint32_t, kMaxWidgets> textOffsets{};
    std::array<std::uint16_t, kMaxWidgets> textLengths{};
    std::array<std::uint16_t, kMaxWidgets> caretOffsets{};
    std::array<std::uint16_t, kMaxWidgets> selectionAnchors{};
    std::array<std::uint16_t, kMaxWidgets> fontSizes{};
    std::array<std::uint16_t, kMaxWidgets> textFlags{};
    std::array<float, kMaxWidgets> progresses{};
    std::array<float, kMaxWidgets> borderWidths{};
    std::array<float, kMaxWidgets> knobSizes{};
    std::array<float, kMaxWidgets> ratios{};
    std::array<std::int8_t, kMaxWidgets> zOrders{};
    std::array<std::uint16_t, kMaxWidgets> cellRows{};
    std::array<std::uint16_t, kMaxWidgets> cellColumns{};
    std::array<std::uint8_t, kMaxWidgets> rowSpans{};
    std::array<std::uint8_t, kMaxWidgets> columnSpans{};
    std::array<std::uint16_t, kMaxWidgets> fontAtlasIndices{};
    std::array<std::uint16_t, kMaxWidgets> firstHyperlink{};
    std::array<std::uint16_t, kMaxWidgets> hyperlinkCounts{};
    std::array<WidgetHyperlink, 128> hyperlinks{};
    std::uint32_t hyperlinkUsed = 0;

    // Character pool for labels. Text is an (offset, length) range into this
    std::array<char, kMaxTextPool> textPool{};
    // Localization key + 1 per widget (0 = no key). The key is resolved lazily
    // by ApplyLocalization(), so switching locale does not touch the layout.
    std::array<std::uint32_t, kMaxWidgets> textKeys{};

    // Layout cache
    struct LayoutCache
    {
        std::array<Vector2D, kMaxWidgets> localMins{};
        std::array<Vector2D, kMaxWidgets> localMaxs{};
        std::array<std::uint16_t, kMaxWidgets> drawOrder{};
        bool valid = false;
    };
    mutable LayoutCache layout;

    // Lifetime / configuration
    void Clear() noexcept;

    void SetCanvasSize(Vector2D InValue) noexcept
    {
        size = InValue;
        InvalidateLayout();
    }
    // Insets from the canvas edges; the root reference box is clamped so the
    // inset can never invert the available area.
    void SetSafeArea(Vector2D InMin, Vector2D InMax) noexcept
    {
        safeAreaMin = InMin;
        safeAreaMax = InMax;
        InvalidateLayout();
    }
    [[nodiscard]] Rect RectOf(WidgetHandle InHandle) const noexcept
    {
        EnsureLayout();
        if (InHandle.widget < 0 || static_cast<std::uint32_t>(InHandle.widget) >= widgetCount)
        {
            return {};
        }
        const auto ScopedIndex = static_cast<std::uint32_t>(InHandle.widget);
        return Rect{layout.localMins[ScopedIndex], layout.localMaxs[ScopedIndex]};
    }
    // Physical window extent / display scale. The canvas stays in Units, so the
    void SetPhysicalCanvasSize(Vector2D InPhysicalPixels, float InScale) noexcept
    {
        const float ScopedScale = InScale > 0.0f ? InScale : 1.0f;
        size = InPhysicalPixels / ScopedScale;
        displayScale = ScopedScale;
        InvalidateLayout();
    }
    void SetDisplayScale(float InScale) noexcept
    {
        displayScale = std::clamp(InScale, 0.5f, 4.0f);
    }
    [[nodiscard]] float DisplayScale() const noexcept
    {
        return displayScale;
    }
    void SetFollowDisplayScale(bool InFollow) noexcept
    {
        followDisplayScale = InFollow;
    }
    void SetTheme(const UiThemeAsset& InTheme) noexcept
    {
        theme = InTheme;
    }
    [[nodiscard]] const UiThemeAsset& Theme() const noexcept
    {
        return theme;
    }
    [[nodiscard]] Vector2D CanvasToScreen(Vector2D InLocal) const noexcept;
    [[nodiscard]] Vector2D ScreenToCanvas(Vector2D InScreen) const noexcept;
    void SetSortLayer(std::uint16_t InValue) noexcept
    {
        sortLayer = InValue;
    }
    void SetTextureArray(std::uint32_t InValue) noexcept
    {
        textureArray = InValue;
    }
    void SetPipeline(std::uint32_t InValue) noexcept
    {
        pipeline = InValue;
    }
    void SetWorldTransform(Vector2D InPosition, Vector2D InScale, float InRotation) noexcept
    {
        worldPosition = InPosition;
        worldScale = InScale;
        worldRotation = InRotation;
    // Only item positions change, not the local layout: no invalidation.
    }

    // Advances the burn-in orbit by dt. Cheap: one float add and, at most once
    void AdvanceOledShift(float InDt) noexcept;
    void SetOledShiftEnabled(bool InEnabled) noexcept
    {
        if (!InEnabled)
        {
            oled = OledShiftState{};
            oled.enabled = false;
        }
        else if (!oled.enabled)
        {
            oled = OledShiftState{};
        }
    }
    [[nodiscard]] Vector2D OledOffset() const noexcept
    {
        return oled.enabled ? oled.offset : Vector2D{};
    }

    // Configures the grid. `columnWidths` must hold exactly `columns` values and
    [[nodiscard]] bool SetGrid(std::uint32_t InColumns, std::uint32_t InRows, std::span<const float> InColumnWidths,
                               std::span<const float> InRowHeights, Vector2D InGap, Vector2D InPadding) noexcept;

    [[nodiscard]] GridView Grid() const noexcept
    {
        return GridView(tracks[0].data(), 2, kTrackCapacity);
    }
    // Physical extent of the mdspan grid at a display scale. Layout stays in
    [[nodiscard]] Vector2D GridPixelExtent(float InScale) const noexcept;

    // Insertion
    [[nodiscard]] std::int32_t AddWidget(const WidgetDesc& InDesc) noexcept;

    // Replaces the text range of a label. The new string is appended to the
    [[nodiscard]] bool SetText(std::int32_t InWidget, std::string_view InText) noexcept;

    // Rewrites a widget text in place when it fits, otherwise appends and
    // compacts the shared pool. Returns false only when the text cannot fit.
    [[nodiscard]] bool ReplaceText(std::int32_t InWidget, std::string_view InText) noexcept;

    // Localization key for a widget caption. ApplyLocalization() replaces every
    // keyed caption with the active locale's string (falling back to the default
    // locale) and returns the number of captions it wrote.
    void SetTextKey(std::int32_t InWidget, std::uint32_t InKey) noexcept;
    [[nodiscard]] std::uint32_t TextKey(std::int32_t InWidget) const noexcept;
    std::uint32_t ApplyLocalization(const LocalizationTable& InTable) noexcept;

    // TextInput editing. Offsets are UTF-8 byte offsets; the editing methods
    // keep the caret on a codepoint boundary and replace the selection.
    [[nodiscard]] std::uint16_t Caret(std::int32_t InWidget) const noexcept;
    [[nodiscard]] bool SetCaret(std::int32_t InWidget, std::uint16_t InOffset, bool InSelect = false) noexcept;
    [[nodiscard]] bool HasSelection(std::int32_t InWidget) const noexcept;
    [[nodiscard]] std::string_view SelectedText(std::int32_t InWidget) const noexcept;
    void SelectAll(std::int32_t InWidget) noexcept;
    void ClearSelection(std::int32_t InWidget) noexcept;
    [[nodiscard]] bool InsertText(std::int32_t InWidget, std::string_view InText) noexcept;
    [[nodiscard]] bool InsertCodepoint(std::int32_t InWidget, std::uint32_t InCodepoint) noexcept;
    [[nodiscard]] bool Backspace(std::int32_t InWidget) noexcept;
    [[nodiscard]] bool DeleteForward(std::int32_t InWidget) noexcept;
    [[nodiscard]] bool MoveCaret(std::int32_t InWidget, std::int32_t InDeltaCodepoints,
                                 bool InSelect = false) noexcept;

    // Seconds since the canvas started; drives the caret blink. The system
    // advances it every frame, and a direct-BuildFrame caller may do it too.
    void AdvanceTime(float InDt) noexcept;

    // Replaces the clickable ranges of a Hypertext widget. Ranges are UTF-8 byte
    [[nodiscard]] bool SetHyperlinks(std::int32_t InWidget, std::span<const WidgetHyperlink> InLinks) noexcept;
    void SetFontAtlas(std::int32_t InWidget, std::uint16_t InAtlasIndex) noexcept
    {
        if (ValidWidget(InWidget))
        {
            fontAtlasIndices[static_cast<std::uint32_t>(InWidget)] = InAtlasIndex;
        }
    }

    // Attribute setters. Geometry-affecting ones invalidate the layout cache;
    void SetProgress(std::int32_t InWidget, float InValue) noexcept
    {
        if (ValidWidget(InWidget))
        {
            progresses[InWidget] = InValue < 0.0f ? 0.0f : (InValue > 1.0f ? 1.0f : InValue);
        }
    }
    void SetBorderWidth(std::int32_t InWidget, float InValue) noexcept
    {
        if (ValidWidget(InWidget))
        {
            borderWidths[InWidget] = InValue > 0.0f ? InValue : 0.0f;
        }
    }
    void SetKnobSize(std::int32_t InWidget, float InValue) noexcept
    {
        if (ValidWidget(InWidget))
        {
            knobSizes[InWidget] = InValue > 0.0f ? InValue : 0.0f;
        }
    }
    void SetRatio(std::int32_t InWidget, float InValue) noexcept
    {
        if (ValidWidget(InWidget))
        {
            ratios[InWidget] = InValue < 0.0f ? 0.0f : (InValue > 1.0f ? 1.0f : InValue);
        }
    }
    void SetChecked(std::int32_t InWidget, bool InChecked) noexcept;
    [[nodiscard]] bool Checked(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) && HasFlag(flags[InWidget], WidgetFlags::Checked);
    }
    // NineSlice corner insets in Units: X left, Y top, Z right, W bottom.
    void SetSliceInsets(std::int32_t InWidget, Vector4D InInsets) noexcept
    {
        if (ValidWidget(InWidget))
        {
            slices[static_cast<std::uint32_t>(InWidget)] = InInsets;
        }
    }
    [[nodiscard]] Vector4D SliceInsets(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? slices[static_cast<std::uint32_t>(InWidget)] : Vector4D{};
    }
    // RadioButton mutual-exclusion group; kNoFocusGroup means ungrouped.
    void SetRadioGroup(std::int32_t InWidget, std::uint16_t InGroup) noexcept
    {
        if (ValidWidget(InWidget))
        {
            radioGroups[static_cast<std::uint32_t>(InWidget)] = InGroup;
        }
    }
    [[nodiscard]] std::uint16_t RadioGroup(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? radioGroups[static_cast<std::uint32_t>(InWidget)] : kNoFocusGroup;
    }
    void SetTint(std::int32_t InWidget, Color32 InValue) noexcept
    {
        if (ValidWidget(InWidget))
        {
            tints[InWidget] = InValue;
            tintPalettes[InWidget] = kPaletteInherit;
        }
    }
    void SetFillTint(std::int32_t InWidget, Color32 InValue) noexcept
    {
        if (ValidWidget(InWidget))
        {
            fillTints[InWidget] = InValue;
            fillPalettes[InWidget] = kPaletteInherit;
        }
    }
    void SetTintPalette(std::int32_t InWidget, UiPaletteSlot InSlot) noexcept
    {
        if (ValidWidget(InWidget))
        {
            tintPalettes[InWidget] = static_cast<std::uint16_t>(InSlot);
        }
    }
    void SetFillPalette(std::int32_t InWidget, UiPaletteSlot InSlot) noexcept
    {
        if (ValidWidget(InWidget))
        {
            fillPalettes[InWidget] = static_cast<std::uint16_t>(InSlot);
        }
    }
    // Caption colour of a Button; Labels keep using tint for their glyphs.
    void SetTextTint(std::int32_t InWidget, Color32 InValue) noexcept
    {
        if (ValidWidget(InWidget))
        {
            textTints[InWidget] = InValue;
            textPalettes[InWidget] = kPaletteInherit;
        }
    }
    void SetTextPalette(std::int32_t InWidget, UiPaletteSlot InSlot) noexcept
    {
        if (ValidWidget(InWidget))
        {
            textPalettes[InWidget] = static_cast<std::uint16_t>(InSlot);
        }
    }
    void SetAnchors(std::int32_t InWidget, Vector2D InMin, Vector2D InMax) noexcept;
    void SetOffsets(std::int32_t InWidget, Vector2D InMin, Vector2D InMax) noexcept;
    [[nodiscard]] bool SetZOrder(std::int32_t InWidget, std::int32_t InValue) noexcept;
    void SetFlags(std::int32_t InWidget, std::uint32_t InValue) noexcept
    {
        if (!ValidWidget(InWidget))
        {
            return;
        }
        const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
        flags[ScopedSlot] = InValue;
        // The Focusable bit mirrors the focus policy; it is never a second source
        const bool ScopedFocusable = HasFlag(InValue, WidgetFlags::Focusable);
        if (ScopedFocusable)
        {
            if (focusPolicies[ScopedSlot] == FocusPolicy::None)
            {
                focusPolicies[ScopedSlot] = FocusPolicy::TabAndPointer;
            }
        }
        else
        {
            focusPolicies[ScopedSlot] = FocusPolicy::None;
        }
    }
    void SetFlag(std::int32_t InWidget, WidgetFlags InFlag, bool InEnabled) noexcept
    {
        if (!ValidWidget(InWidget))
        {
            return;
        }
        const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
        const std::uint32_t ScopedMask = static_cast<std::uint32_t>(InFlag);
        flags[ScopedSlot] = InEnabled ? (flags[ScopedSlot] | ScopedMask) : (flags[ScopedSlot] & ~ScopedMask);
        if (InFlag == WidgetFlags::Focusable)
        {
            SetFocusable(InWidget, InEnabled);
        }
    }

    // Focus configuration. A widget is keyboard-inert until its policy says
    // otherwise; the pointer press path follows the same policy.
    void SetFocusPolicy(std::int32_t InWidget, FocusPolicy InPolicy) noexcept
    {
        if (!ValidWidget(InWidget))
        {
            return;
        }
        const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
        focusPolicies[ScopedSlot] = InPolicy;
        const std::uint32_t ScopedMask = static_cast<std::uint32_t>(WidgetFlags::Focusable);
        flags[ScopedSlot] =
            InPolicy == FocusPolicy::None ? (flags[ScopedSlot] & ~ScopedMask) : (flags[ScopedSlot] | ScopedMask);
    }
    [[nodiscard]] FocusPolicy FocusPolicyOf(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? focusPolicies[static_cast<std::uint32_t>(InWidget)] : FocusPolicy::None;
    }
    void SetFocusable(std::int32_t InWidget, bool InFocusable) noexcept
    {
        SetFocusPolicy(InWidget, InFocusable ? FocusPolicy::TabAndPointer : FocusPolicy::None);
    }
    // True when the policy allows focus and the widget is visible and enabled.
    [[nodiscard]] bool CanFocus(std::int32_t InWidget) const noexcept;
    void SetTabIndex(std::int32_t InWidget, std::int32_t InIndex) noexcept
    {
        if (ValidWidget(InWidget))
        {
            tabIndices[InWidget] = InIndex;
        }
    }
    [[nodiscard]] std::int32_t TabIndex(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? tabIndices[InWidget] : 0;
    }
    void SetFocusGroup(std::int32_t InWidget, std::uint16_t InGroup) noexcept
    {
        if (ValidWidget(InWidget))
        {
            focusGroups[InWidget] = InGroup;
        }
    }
    [[nodiscard]] std::uint16_t FocusGroup(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? focusGroups[InWidget] : kNoFocusGroup;
    }
    void SetFocusWrap(bool InWrap) noexcept
    {
        focusWrap = InWrap;
    }
    [[nodiscard]] bool FocusWrap() const noexcept
    {
        return focusWrap;
    }
    void SetFocusGroupFilter(std::uint16_t InGroup) noexcept
    {
        focusGroupFilter = InGroup;
    }
    [[nodiscard]] std::uint16_t FocusGroupFilter() const noexcept
    {
        return focusGroupFilter;
    }
    // A scope confines Tab focus to the descendants of this widget.
    void SetFocusScope(std::int32_t InWidget) noexcept
    {
        if (InWidget == kNoWidget || ValidWidget(InWidget))
        {
            focusScope = InWidget;
        }
    }
    [[nodiscard]] std::int32_t FocusScopeIndex() const noexcept
    {
        return focusScope;
    }
    // The optional ring is drawn outside the focused rect in the theme Focus
    // colour; it is four quads appended after every widget so it stays on top.
    void SetDrawFocusRing(bool InEnabled) noexcept
    {
        drawFocusRing = InEnabled;
    }
    [[nodiscard]] bool DrawFocusRing() const noexcept
    {
        return drawFocusRing;
    }
    void SetFocusRingWidth(float InWidth) noexcept
    {
        focusRingWidth = InWidth > 0.0f ? (InWidth > 8.0f ? 8.0f : InWidth) : 0.0f;
    }
    [[nodiscard]] float FocusRingWidth() const noexcept
    {
        return focusRingWidth;
    }

    // Stable, pointer-free reference to a widget.
    [[nodiscard]] WidgetHandle Handle(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? WidgetHandle{InWidget, generation} : WidgetHandle{};
    }
    [[nodiscard]] bool ResolveHandle(WidgetHandle InHandle) const noexcept
    {
        return IsHandleSet(InHandle) && InHandle.generation == generation && ValidWidget(InHandle.widget);
    }

    // Switcher page by stable handle. A stale generation or a widget that is
    // not a direct child is rejected; an empty handle resets to the first child.
    [[nodiscard]] bool SetActiveWidget(std::int32_t InSwitcher, WidgetHandle InWidget) noexcept;
    [[nodiscard]] WidgetHandle ActiveWidget(std::int32_t InSwitcher) const noexcept;
    // Page-index convenience over the handle. A page past the last clamps.
    void SetActivePage(std::int32_t InSwitcher, std::uint32_t InPage) noexcept;
    [[nodiscard]] std::uint32_t ActivePage(std::int32_t InSwitcher) const noexcept;

    // Custom painter; a null painter draws nothing.
    void SetCustomPaint(std::int32_t InWidget, CustomWidgetPaint InPaint, void* InUser = nullptr) noexcept
    {
        if (ValidWidget(InWidget))
        {
            const auto ScopedSlot = static_cast<std::uint32_t>(InWidget);
            customPaints[ScopedSlot] = InPaint;
            customUsers[ScopedSlot] = InUser;
        }
    }

    // Hover timing. The system advances this every frame; PointerMove refreshes
    // the Hovered bits that AdvanceHover turns into seconds.
    void AdvanceHover(float InDt) noexcept;
    [[nodiscard]] float HoverSeconds(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? hoverSeconds[InWidget] : 0.0f;
    }
    void SetTooltipTarget(std::int32_t InTooltip, WidgetHandle InTarget) noexcept
    {
        if (ValidWidget(InTooltip))
        {
            tooltipTargets[InTooltip] = InTarget;
        }
    }
    [[nodiscard]] WidgetHandle TooltipTarget(std::int32_t InTooltip) const noexcept
    {
        return ValidWidget(InTooltip) ? tooltipTargets[InTooltip] : WidgetHandle{};
    }
    void SetTooltipDelay(std::int32_t InTooltip, float InSeconds) noexcept
    {
        if (ValidWidget(InTooltip))
        {
            tooltipDelays[InTooltip] = InSeconds > 0.0f ? InSeconds : 0.0f;
        }
    }
    [[nodiscard]] float TooltipDelay(std::int32_t InTooltip) const noexcept
    {
        return ValidWidget(InTooltip) ? tooltipDelays[InTooltip] : 0.0f;
    }
    // TextBlock content reference. The text stays in Fby::Text; the widget only
    // stores the 16-byte handle and never copies long text into its pool.
    void SetTextRef(std::int32_t InWidget, Text::TextAssetRef InRef) noexcept
    {
        if (ValidWidget(InWidget))
        {
            textRefs[InWidget] = InRef;
        }
    }
    [[nodiscard]] Text::TextAssetRef TextRef(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? textRefs[InWidget] : Text::TextAssetRef{};
    }
    // Selects one TextStore block of the same asset. A long document is paged by
    // moving this block index; the full document stays in Fby::Text.
    void SetTextBlock(std::int32_t InWidget, std::uint32_t InBlock) noexcept
    {
        if (ValidWidget(InWidget))
        {
            textRefs[InWidget].Block = InBlock;
        }
    }
    [[nodiscard]] std::uint32_t TextBlock(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? textRefs[InWidget].Block : 0;
    }
    // Restricts a shaped TextBlock to a line window (first line plus count);
    // count 0 renders the whole layout. The window is clipped to the actual
    // shaped line count and is the viewport hook for very long documents.
    void SetTextLineRange(std::int32_t InWidget, std::uint32_t InFirstLine, std::uint32_t InLineCount) noexcept
    {
        if (ValidWidget(InWidget))
        {
            textFirstLines[InWidget] = static_cast<std::uint16_t>(std::min<std::uint32_t>(InFirstLine, 0xFFFFu));
            textLineCounts[InWidget] = static_cast<std::uint16_t>(std::min<std::uint32_t>(InLineCount, 0xFFFFu));
        }
    }
    [[nodiscard]] std::uint16_t TextFirstLine(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? textFirstLines[InWidget] : 0;
    }
    [[nodiscard]] std::uint16_t TextLineCount(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? textLineCounts[InWidget] : 0;
    }
    void SetPinned(std::int32_t InWidget, bool InPinned) noexcept
    {
        SetFlag(InWidget, WidgetFlags::Pinned, InPinned);
    }
    [[nodiscard]] bool Pinned(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) && HasFlag(flags[static_cast<std::uint32_t>(InWidget)], WidgetFlags::Pinned);
    }

    // Visual transitions. SetOpacity/SetVisualOffset snap; FadeTo/SlideTo set a
    // target that AdvanceTransitions integrates. Layout and hit testing keep
    // the anchor rect, so a transition is presentation-only.
    void SetOpacity(std::int32_t InWidget, float InValue) noexcept;
    [[nodiscard]] float Opacity(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? opacities[static_cast<std::uint32_t>(InWidget)] : 1.0f;
    }
    void FadeTo(std::int32_t InWidget, float InTarget, float InSpeed = -1.0f) noexcept;
    void SetVisualOffset(std::int32_t InWidget, Vector2D InValue) noexcept;
    [[nodiscard]] Vector2D VisualOffset(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? visualOffsets[static_cast<std::uint32_t>(InWidget)] : Vector2D{};
    }
    void SlideTo(std::int32_t InWidget, Vector2D InTarget, float InSpeed = -1.0f) noexcept;
    void AdvanceTransitions(float InDt) noexcept;

    // Visibility. Hiding a widget also hides its subtree and removes it from
    // hit testing, focus traversal and the glyph budget.
    void SetVisible(std::int32_t InWidget, bool InVisible) noexcept
    {
        SetFlag(InWidget, WidgetFlags::Visible, InVisible);
    }
    [[nodiscard]] bool IsVisible(std::int32_t InWidget) const noexcept;

    // Clips every descendant to this widget's rect. The widget itself is not
    // clipped, and the clip composes with every clipping ancestor.
    void SetClipChildren(std::int32_t InWidget, bool InClip) noexcept
    {
        SetFlag(InWidget, WidgetFlags::ClipChildren, InClip);
    }
    [[nodiscard]] bool ClipChildren(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) && HasFlag(flags[static_cast<std::uint32_t>(InWidget)], WidgetFlags::ClipChildren);
    }

    // Layout
    void InvalidateLayout() noexcept
    {
        layout.valid = false;
    }
    [[nodiscard]] bool LayoutValid() const noexcept
    {
        return layout.valid;
    }
    void EnsureLayout() const noexcept
    {
        if (!layout.valid)
        {
            ResolveLayout(*this);
        }
    }

    // Queries. Layout-dependent accessors call EnsureLayout(), so a caller
    [[nodiscard]] bool ValidWidget(std::int32_t InWidget) const noexcept
    {
        return InWidget >= 0 && static_cast<std::uint32_t>(InWidget) < widgetCount;
    }
    [[nodiscard]] WidgetKind Kind(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? kinds[static_cast<std::uint32_t>(InWidget)] : WidgetKind::Panel;
    }
    [[nodiscard]] std::uint32_t Flags(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? flags[static_cast<std::uint32_t>(InWidget)] : 0;
    }
    [[nodiscard]] std::int32_t Parent(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? parents[static_cast<std::uint32_t>(InWidget)] : kNoWidget;
    }
    [[nodiscard]] std::uint16_t ChildCount(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? childCounts[static_cast<std::uint32_t>(InWidget)] : 0;
    }
    [[nodiscard]] std::uint32_t Layer(std::int32_t InWidget) const noexcept
    {
        return ValidWidget(InWidget) ? layers[static_cast<std::uint32_t>(InWidget)] : kWhiteLayer;
    }
    [[nodiscard]] std::string_view Text(std::int32_t InWidget) const noexcept;
    [[nodiscard]] Rect LocalRect(std::int32_t InWidget) const noexcept;
    // Progress a Slider or Scrollbar would take if the pointer landed here
    [[nodiscard]] float SliderValueAt(std::int32_t InWidget, Vector2D InCanvasPoint) const noexcept;
    [[nodiscard]] std::span<const std::uint16_t> DrawOrder() const noexcept
    {
        EnsureLayout();
        return {layout.drawOrder.data(), widgetCount};
    }
};

static_assert(std::is_trivially_copyable_v<WidgetCanvas>, "WidgetCanvas must be memcpy-able");
static_assert(std::is_standard_layout_v<WidgetCanvas>, "WidgetCanvas must have a stable layout");

// Layout phases
void GridLayoutPass(const WidgetCanvas& InCanvas) noexcept;
void ResolveAnchors(const WidgetCanvas& InCanvas) noexcept;
void ResolveLayout(const WidgetCanvas& InCanvas) noexcept;

// Embedded bitmap font
[[nodiscard]] std::array<std::uint8_t, 8> GlyphBitmap(char InGlyph) noexcept;
[[nodiscard]] Rect GlyphUv(char InGlyph) noexcept;

// Text extents in canvas pixels for one font size, honouring explicit '\n'
[[nodiscard]] Vector2D MeasureText(std::string_view InText, std::uint32_t InFontSize) noexcept;

// Number of quads a label with this text emits: every byte except '\n'.
[[nodiscard]] std::uint32_t CountGlyphs(std::string_view InText) noexcept;

// Pointer interaction
[[nodiscard]] std::int32_t HitTest(const WidgetCanvas& InCanvas, Vector2D InCanvasPoint) noexcept;

// Hypertext hit test: finds the topmost Hypertext widget, maps the local point
[[nodiscard]] const WidgetHyperlink* HitTestHyperlink(const WidgetCanvas& InCanvas, Vector2D InCanvasPoint,
                                                      const FontAtlas* InAtlas,
                                                      std::uint32_t* OutByteOffset = nullptr) noexcept;
[[nodiscard]] const WidgetHyperlink* HyperlinkAtByte(const WidgetCanvas& InCanvas, std::int32_t InWidget,
                                                     std::uint32_t InByteOffset) noexcept;

// Hit test + press: focuses the hit widget when it is focusable/enabled, sets
// the press bit on a Button or Checkbox, and captures a Slider/Scrollbar drag.
[[nodiscard]] std::int32_t PointerDown(WidgetCanvas& InCanvas, Vector2D InCanvasPoint) noexcept;

// Clears press state. Returns the widget that was pressed AND released over,
// or kNoWidget. A completed click toggles a Checkbox.
[[nodiscard]] std::int32_t PointerUp(WidgetCanvas& InCanvas, Vector2D InCanvasPoint) noexcept;

// Pointer move: refreshes the Hovered bit and carries an active Slider/Scrollbar
// drag. Returns the dragged widget whose progress changed, or kNoWidget.
[[nodiscard]] std::int32_t PointerMove(WidgetCanvas& InCanvas, Vector2D InCanvasPoint) noexcept;

// Places the caret of a TextInput at a canvas point, using the glyph advances of
// the supplied atlas. A null atlas falls back to the monospace advance.
[[nodiscard]] bool SetCaretFromPoint(WidgetCanvas& InCanvas, std::int32_t InWidget, Vector2D InCanvasPoint,
                                     const FontAtlas* InAtlas, bool InSelect = false) noexcept;

// Shaped-cluster editing for a TextInput. BuildTextClusterOffsets shapes the
// current string and writes sorted, unique cluster byte offsets (including 0 and
// the string length). The other three functions consume that map so caret moves,
// backspace and delete remove a whole grapheme cluster, not one codepoint.
[[nodiscard]] std::uint32_t BuildTextClusterOffsets(std::string_view InText, Text::ITextShaper& InShaper,
                                                    const Text::TextShapeParams& InParams,
                                                    std::span<std::uint16_t> OutOffsets) noexcept;
[[nodiscard]] bool MoveCaretClusters(WidgetCanvas& InCanvas, std::int32_t InWidget,
                                     std::int32_t InDeltaClusters, bool InSelect,
                                     std::span<const std::uint16_t> InClusterOffsets) noexcept;
[[nodiscard]] bool BackspaceCluster(WidgetCanvas& InCanvas, std::int32_t InWidget,
                                    std::span<const std::uint16_t> InClusterOffsets) noexcept;
[[nodiscard]] bool DeleteForwardCluster(WidgetCanvas& InCanvas, std::int32_t InWidget,
                                        std::span<const std::uint16_t> InClusterOffsets) noexcept;

void SetFocus(WidgetCanvas& InCanvas, std::int32_t InWidget) noexcept;
[[nodiscard]] std::int32_t FocusedWidget(const WidgetCanvas& InCanvas) noexcept;

// The focus scope that applies now: the explicit scope when it is visible,
// otherwise the topmost shown Modal Overlay, otherwise kNoWidget.
[[nodiscard]] std::int32_t EffectiveFocusScope(const WidgetCanvas& InCanvas) noexcept;

// Moves focus to the next/previous Tab-policy candidate ordered by
// (tabIndex, insertion index), skipping hidden, disabled and out-of-scope
// widgets. Returns the new focus index or kNoWidget.
[[nodiscard]] std::int32_t FocusNext(WidgetCanvas& InCanvas, bool InBackwards = false) noexcept;

// Arrow-key traversal among every focusable widget (any policy) when a focus is
// already set. The nearest candidate in the requested half-plane wins, with a
// perpendicular penalty. Respects visibility, enabled state, groups, scope and
// the clip rectangle. Returns the new focus index or kNoWidget.
[[nodiscard]] std::int32_t FocusMove(WidgetCanvas& InCanvas, FocusDirection InDirection) noexcept;

// Keyboard activation for the app's Enter/Space handler. A Checkbox toggles;
// every other kind is returned so the app can run its click action.
[[nodiscard]] std::int32_t ActivateFocused(WidgetCanvas& InCanvas) noexcept;

// Frame output
struct FrameOutput
{
    std::span<Rhi::RenderItem> items{};
    std::span<Rhi::RenderKey> keys{};
    std::span<Rhi::DrawBatch> batches{};
    std::uint32_t startCursor = 0;

    // Results, written by BuildFrame/Update.
    std::uint32_t itemsWritten = 0;
    std::uint32_t batchesWritten = 0;
    std::uint32_t widgetsDrawn = 0;
    std::uint32_t glyphQuads = 0;
    // Conservative unclipped upper bound. ClipChildren can trim the written
    // items below this number, never above it.
    std::uint32_t requiredItems = 0;
    // Texture array that now backs the glyphs (white layer 0 + font layers).
    std::uint32_t fontTextureArray = 0xFFFFFFFFu;
    // Set when BuildFrame refused to pack because the text/font-array contract
    bool fontContractViolated = false;
    bool overflow = false;
};

enum class BuildStatus : std::uint8_t
{
    // The canvas was written
    Ok = 0,
    // Not enough item/key/batch capacity, or the glyph budget was exceeded
    Overflow,
    // Canvas data is inconsistent (defensive; should not happen)
    Invalid,
    // The canvas has visible text but the font atlas set is missing, empty, not
    FontsNotMounted,
};

// WidgetCanvasSystem - the Render-phase extract for Pink UI.
class WidgetCanvasSystem
{
public:
    explicit WidgetCanvasSystem(FontAtlasSet* InFonts = nullptr) noexcept
        : MFonts(InFonts)
    {
    }

    // Cold path only: call from app init / scene load, never from BuildFrame or
    [[nodiscard]] bool MountFonts(Rhi::RHI& InRhi);

    [[nodiscard]] static BuildStatus BuildFrame(Fby::Ecs::SystemContext& InContext, const WidgetCanvas& InCanvas,
                                                FrameOutput& Output) noexcept;
    static void Update(Fby::Ecs::SystemContext& InContext, std::span<WidgetCanvas> InCanvases,
                       FrameOutput& Output) noexcept;

private:
    FontAtlasSet* MFonts = nullptr;
};
}

// Mirror registration
FBY_REFLECT_COMPONENT(Fby::Ui::WidgetComponent, "WidgetComponent",
                      FBY_FIELD(Fby::Ui::WidgetComponent, CanvasID, UInt32),
                      FBY_FIELD(Fby::Ui::WidgetComponent, Layer, UInt32),
                      FBY_FIELD(Fby::Ui::WidgetComponent, Flags, UInt32),
                      FBY_FIELD(Fby::Ui::WidgetComponent, AnimationTime, Float))
