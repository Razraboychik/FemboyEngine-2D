# Pink UI widget and HUD catalog

FemboyEngine2D 0.1.0-alpha.1 has one retained-mode UI module: `Fby::Ui::WidgetCanvas` in
[PinkUI.hpp](../../Source/Engine/UI/PinkUI.hpp). This page is the element catalog,
the interaction and focus contracts, the visibility rules, the custom-widget
extension point and the recommended next additions. The engine version stays
0.1.0-alpha.1: everything listed as shipped is part of the existing 0.1 API.

## Drawing model

- `WidgetCanvas` is one flat, trivially copyable SoA block (no per-widget
  allocation). A widget is an index into parallel arrays; a `WidgetDesc` is the
  insertion record for `AddWidget`.
- Layout is two deterministic passes: the `std::mdspan` grid (tracks, gaps,
  padding, spans, auto rows) and the UGUI anchor/inset convention for children.
- `WidgetCanvasSystem::BuildFrame` writes 48-byte `Rhi::RenderItem`s and
  ascending `RenderKey`s into caller storage, then exactly one `DrawBatch` per
  canvas. `BuildFrame` performs no allocation.
- Text is drawn only through the mounted font atlas. There is no bitmap
  fallback in a shipping build; a canvas with text and no mounted atlas is
  refused with a diagnostic instead of drawing a fallback font.
- Colours come from the widget literal or from a `UiThemeAsset` palette slot
  (16 slots, `UiPaletteSlot`). `tint` is the face or stroke, `fillTint` the
  progress/knob/thumb/mark colour, and `textTint` the button caption colour.
- Units are DPI-independent: the canvas is authored in Units and
  `displayScale`/`followDisplayScale` convert to physical pixels.

## Text scope: the Text module is authoritative

Pink UI is a control layer, not a text engine. The 8 KiB widget text pool, the
atlas codepoint walk and the short-string wrap/alignment above are intended for
**short control strings** (button captions, menu labels, field values). They are
not a replacement for the engine text stack:

- `Text::TextStore` keeps up to 1 MiB of long-form text in an arena and splits it
  into blocks; the frame path never copies it into a widget.
- `Text::TextService` caches layout and markup (spans, styles, actions, inline
  objects).
- `Text::TextLayout` performs bidi runs (SheenBidi), pixel wrapping, vertical
  layout and inline atoms (`U+FFFC`), with up to 4096 lines.
- `Text::TextShaper`/`TextShapeCache` carry HarfBuzz clusters and glyph ids;
  `Text::BuildShapedTextRenderItems` consumes them.
- `Text::TextRenderSystem` is the ECS extract for world text, with spans,
  inline sprites, effects and large blocks.

The UI bridge is `WidgetKind::TextBlock`: it stores a 16-byte
`Text::TextAssetRef`, lays out through `TextService`/`TextLayout` and emits
through `TextRender` or `BuildShapedTextRenderItems`, so large, rich, inline and
shaped text does not pass through the widget pool. With
`SystemContext::textShapes`/`textShaper` set, it uses the HarfBuzz shape cache;
without them it falls back to the codepoint TextRender path. Cluster-based
editing is available through `BuildTextClusterOffsets` plus the cluster
caret/backspace/delete helpers; shaped glyph rendering inside the editable field
uses the shape cache when configured, including right-aligned RTL and mixed-run
caret edges. Host wiring is in place through `Text::TextRuntime` in Sandbox and
Editor, with a real `TextFontStack` when the TTF mounts. Inline `U+FFFC` atoms
are emitted from the shaped path at their placeholder clusters; a host can
supply `InlineSprites` to map atom ids to atlas slices. Long shaped blocks
expose `SetTextLineRange` for a viewport window, and `SetTextBlock` pages between
`TextStore` blocks.

## Elements shipped in 0.1.0-alpha.1

| Kind | Draws | Quads | State and interaction | Main colours |
| --- | --- | --- | --- | --- |
| `Panel` | Solid rectangle | 1 | Decorative; focus is opt-in like every kind | `tint` |
| `Button` | Face plus an optional centred caption | 1 + glyphs | Press/hover tint, click on release; focus opt-in | `tint`, `textTint` |
| `Label` | One quad per glyph, top-left aligned | glyphs | Static text, explicit `\n` line breaks | `tint` |
| `Image` | One quad with source UV rect | 1 | Static sprite, texture-array layer + UV | `tint` alpha modulates |
| `ProgressBar` | Track plus fill, left-to-right or bottom-up | 1 or 2 | App drives `progress`; the `Vertical` flag flips the axis | `tint`, `fillTint` |
| `Hypertext` | Proportional text with clickable byte ranges | glyphs | Link hit test via `HitTestHyperlink` | `tint` |
| `Spacer` | Nothing, layout only | 0 | Never hit-tested | none |
| `Border` | Four-sided stroke frame | 4 or 2 | Decorative; `borderWidth` in Units | `tint` |
| `Slider` | Track, fill to the knob, knob | 2 or 3 | Press jumps, `PointerMove` drags, release commits; focus opt-in | `tint`, `fillTint` |
| `Checkbox` | Box plus a two-bar check mark | 1 or 3 | A completed click toggles the `Checked` flag; focus opt-in | `tint`, `fillTint` |
| `Scrollbar` | Track plus thumb | 2 | `ratio` is the visible fraction, `progress` the offset; drag updates it | `tint`, `fillTint` |
| `Switcher` | Nothing; only the child selected by a `WidgetHandle` | 0 | `SetActiveWidget`/`SetActivePage`; inactive children stay laid out but leave draw, hit and focus | none |
| `Overlay` | Full-rect scrim | 1 | Blocks the pointer; `Modal` traps Tab focus in its subtree | `tint` |
| `Custom` | Application-painted quads | paint count | `CustomWidgetPaint` callback, deterministic, capped at 64 quads | `tint`, `fillTint`, `Params` |
| `Tooltip` | Background plus centred caption | 1 + glyphs while active | Appears after its target is hovered for `tooltipDelay`; `Pinned` bypasses the timer; hit testing is disabled | `tint`, `textTint` |
| `TextInput` | Background, selection, glyphs, caret | 1 + selection + glyphs + caret | Single-line editing with caret and selection; focus opt-in; `ReadOnly` flag | `tint`, `textTint`, `fillTint` |
| `TextBlock` | Long/rich text from `Fby::Text` | TextRender output, viewport-capped at 1024 items | Stores a 16-byte `TextAssetRef`; bidi, wrapping, spans/styles and inline objects; needs `SystemContext::text` | `textTint`, spans/styles |
| `NineSlice` | Nine texture quads with clamped corner insets | 9 | Textured frame/panel; `sliceInsets` in Units | `tint`, layer + UV |
| `ToggleSwitch` | Pill track plus a left/right knob | 2 | A completed click toggles `Checked`; focus opt-in | `tint`, `fillTint` |
| `RadioButton` | Box plus dot | 1 or 2 | Selecting one clears the other `radioGroup` members; focus opt-in | `tint`, `fillTint` |

Quads per element follow a fixed rule, so a UI budget can be computed before
layout:

| Kind | Quads |
| --- | --- |
| `Panel`, `Image`, `Overlay` | 1 |
| `Button` | 1 + `CountGlyphs(text)` |
| `Label`, `Hypertext` | `CountGlyphs(text)` |
| `ProgressBar` | `progress > 0 ? 2 : 1` |
| `Border` | 0 when the stroke is zero, 2 when the stroke fills the box, otherwise 4 |
| `Slider` | `progress > 0 ? 3 : 2` |
| `Checkbox` | `Checked ? 3 : 1` |
| `Scrollbar` | 2 |
| `Switcher`, `Spacer` | 0 |
| `Custom` | the painter's output, capped at `kMaxCustomQuadsPerWidget` (64) |
| `Tooltip` | `active ? 1 + CountGlyphs(text) : 0` |
| `TextInput` | `1 + (selection ? 1 : 0) + CountGlyphs(text) + (focused and caret visible ? 1 : 0)` |
| `TextBlock` | `TextRender` item count, viewport-capped at 1024; decorations/effects included |
| `NineSlice` | 9 |
| `ToggleSwitch` | 2 |
| `RadioButton` | `Checked ? 2 : 1` |

`kMaxWidgets` is 512, `kMaxGlyphs` is 4096, the text pool is 8192 bytes and a
canvas holds 128 hyperlink ranges. `BuildFrame` reports `requiredItems`,
`itemsWritten`, `glyphQuads` and `overflow`; an overflow writes nothing and
returns `BuildStatus::Overflow`.

## Interaction contract

- `HitTest` walks the draw order back to front. `Spacer`, `Switcher` and widgets
  with `IgnoreHitTest` are transparent to the pointer. Hidden widgets and
  inactive switcher pages never hit.
- `PointerDown` clears the previous press and captures `Button`, `Checkbox`,
  `Slider` or `Scrollbar`. It focuses the hit widget only when the focus policy
  includes the pointer. A Slider/Scrollbar press jumps to the pressed value and
  sets `dragIndex`.
- `PointerMove` refreshes the `Hovered` bits and carries an active drag. It
  returns the dragged widget whose `progress` changed.
- `PointerUp` clears the press, ends the capture and fires only when the release
  is inside the pressed widget. A `Checkbox` toggles on that completed click; a
  `Slider`/`Scrollbar` commits the release position.
- Disabled widgets remain hit-testable so a tooltip can still describe them,
  but they never take focus, press or drag.
- A `Border` used as decoration over other widgets should set `IgnoreHitTest`;
  otherwise its hollow interior still hits as the border widget.
- A `Vertical` Slider/Scrollbar/ProgressBar grows bottom-up, which is the shape
  a HUD health or stamina bar expects.

## Focus model

Focus is opt-in. A widget is keyboard-inert until its policy says otherwise, so
a gamepad or keyboard handler can decide exactly which controls take part.

| Policy | Pointer press focuses | Tab traversal reaches |
| --- | --- | --- |
| `FocusPolicy::None` (default) | no | no |
| `FocusPolicy::Pointer` | yes | no |
| `FocusPolicy::Tab` | no | yes |
| `FocusPolicy::TabAndPointer` | yes | yes |

`WidgetDesc::focusPolicy`, `tabIndex` and `focusGroup` configure a widget at
insertion; `SetFocusPolicy`, `SetTabIndex`, `SetFocusGroup` change it later.
`SetFocusable(true)` is the shorthand for `TabAndPointer`. The legacy
`WidgetFlags::Focusable` bit mirrors the policy and is never a second source of
truth.

- `FocusNext(canvas, backwards)` walks the Tab candidates ordered by
  `(tabIndex, insertion index)`. It skips hidden, disabled and out-of-scope
  widgets and wraps when `focusWrap` is on.
- `FocusMove(canvas, FocusDirection::Left/Right/Up/Down)` is the arrow-key path.
  It needs an existing focus and moves to the nearest focusable widget in that
  half-plane, with a perpendicular penalty. Any opt-in policy counts, so a
  pointer-only control is still reachable once it is focused.
- `focusGroupFilter` restricts traversal to one `focusGroup`; the default
  `kNoFocusGroup` means every group.
- `SetFocus(widget)` is the programmatic path for a modal or a cutscene.
- `EffectiveFocusScope` is the explicit `SetFocusScope` widget when it is
  visible, otherwise the topmost shown `Overlay` with the `Modal` flag. Tab focus
  stays inside that subtree, which is what a modal menu needs.
- `ActivateFocused(canvas)` is the app's Enter/Space hook: a Checkbox toggles
  and every other kind is returned so the app can run its click action. The
  engine ships no key mapping; the app owns the keyboard and gamepad buttons.
- `CanFocus(widget)` reports the effective state (policy, visibility, enabled)
  and `FocusedWidget` returns the current index.
- `SetDrawFocusRing(true)` draws four quads just outside the focused widget in
  the theme `Focus` colour, appended with the highest depth so they stay on top.
  It is off by default, so a game can use its own indicator or none at all;
  `SetFocusRingWidth` clamps the ring to a visible thickness.

## Visibility

Every kind is hideable, including custom painters and switcher pages:

```cpp
Hud.SetVisible(ScopedWidget, false);   // hidden: no draw, hit or focus
const bool Shown = Hud.IsVisible(ScopedWidget);
```

- Visibility is inherited: hiding a parent hides its whole subtree without
  touching the children's own `Visible` bit.
- A `Switcher` hides every child except the one selected by its `WidgetHandle`,
  and the same rule removes them from draw, hit testing, focus and the glyph
  budget.
- Hiding never changes layout. A hidden widget keeps its anchors and grid cell,
  so a menu does not reflow when a row is toggled. Use anchors or a second
  canvas if a reflow is wanted.
- Hidden text is not part of the font contract: a canvas whose only label is
  hidden does not need a mounted atlas.

## Transitions, state colours and short text

Visual transitions are presentation-only. `opacity` and `visualOffset` carry
targets set by `FadeTo`/`SlideTo`, `AdvanceTransitions(dt)` integrates them once
per frame (`WidgetCanvasSystem::Update` does it for ECS canvases), and both
values are inherited through the subtree. Layout and hit testing stay on the
anchor rect, so a fade or slide never moves a hit target. The speed is per second
(alpha or Units); a negative speed keeps the canvas default.

The palette has two state slots beyond the base colours: `Hover` and `Pressed`
(18 slots total). An unset state slot (alpha zero) falls back to multiplying the
authored tint, so existing themes are unchanged; `Disabled` also resolves from
the theme.

`WidgetDesc::textFlags` and the canvas `textFlags` carry the control-string layout
bits: horizontal and vertical alignment plus `kTextWrap` for short captions.
Long, rich and shaped text still belongs to `TextBlock` and the `Fby::Text` stack.

## Clipping

`SetClipChildren(widget, true)` composes a clip rect from that widget's layout
rect for every descendant. Nested clippers intersect, so a viewport inside a
window inside a menu clips to the smallest box. The clipping widget itself is
not clipped.

```cpp
ScopedViewport.kind = Fby::Ui::WidgetKind::Panel;
ScopedViewport.flags |= static_cast<std::uint32_t>(Fby::Ui::WidgetFlags::ClipChildren);
const std::int32_t ViewportId = Hud.AddWidget(ScopedViewport);
```

- Rendering trims each axis-aligned quad to the clip and interpolates the UV
  rect, so images and glyphs keep their correct mapping instead of stretching.
  A quad with no remaining area writes nothing.
- Rotated quads (the checkbox mark and the custom rotation overload) cannot be
  trimmed on their local axes; they disappear once their centre leaves the
  clip. This is exact for the common case and conservative for the rare one.
- Hit testing ignores the clipped-out part of a child, and `FocusNext`,
  `FocusMove` and the arrow path skip a target that is entirely outside.
- `requiredItems` remains the unclipped upper bound; clipping only lowers
  `itemsWritten` and `glyphQuads`.
- The clip is computed in canvas Units and follows the canvas world transform,
  so it stays correct under DPI scaling, canvas translation and rotation.
- A scroll view is now expressible: a clipping `Panel`, a content child moved by
  the scroll offset, and a `Scrollbar` whose `progress` drives that offset. The
  wheel/keyboard handling and the offset itself stay application code.

## Tooltips

`WidgetKind::Tooltip` is a background plus a centred caption that appears only
after its target has rested under the pointer for `tooltipDelay` seconds:

```cpp
Fby::Ui::WidgetDesc Tip;
Tip.kind = Fby::Ui::WidgetKind::Tooltip;
Tip.tooltipTarget = Hud.Handle(ScopedInventorySlot);
Tip.tooltipDelay = 0.5f;
Tip.text = "Iron Sword";
Tip.fontSize = 16;
Tip.tint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Overlay);
Tip.textTint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Text);
(void)Hud.AddWidget(Tip);
```

- `PointerMove` refreshes `Hovered`; `AdvanceHover(dt)` turns it into
  `HoverSeconds`. `WidgetCanvasSystem::Update` already calls `AdvanceHover` with
  the frame delta for canvases attached through `WidgetComponent`.
- The tooltip is effectively hidden (`IsVisible` false) until the target is
  hovered for the delay, so it costs no quads and no glyphs while idle.
- `WidgetFlags::Pinned` (or `SetPinned`) shows it without hover, which is useful
  for inspection overlays.
- `AddWidget` forces `IgnoreHitTest` on a Tooltip: a tooltip under the cursor
  must never steal the hover from its target.
- The engine does not auto-position a tooltip. Place it with anchors/offsets near
  the target, and move it when the target moves. A stale target handle disables
  the tooltip instead of showing it at a wrong place.

## Text input

`WidgetKind::TextInput` is a single-line editable field. Like every control it
is keyboard-inert until a focus policy opts in, so the app decides when the
field takes keys:

```cpp
Fby::Ui::WidgetDesc Field;
Field.kind = Fby::Ui::WidgetKind::TextInput;
Field.text = "Player";
Field.fontSize = 16;
Field.focusPolicy = Fby::Ui::FocusPolicy::TabAndPointer;
Field.tint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Surface);
Field.textTint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Text);
Field.fillTint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Accent);
const std::int32_t ScopedField = Hud.AddWidget(Field);
```

Editing API (all offsets are UTF-8 byte offsets kept on codepoint boundaries):

- `InsertText(widget, utf8)` and `InsertCodepoint(widget, codepoint)` replace the
  selection and move the caret. Newlines are ignored because the field is
  single-line.
- `Backspace`, `DeleteForward`, `MoveCaret(widget, deltaCodepoints, select)`.
- `SetCaret(widget, offset, select)`, `SelectAll`, `ClearSelection`,
  `SelectedText`, `HasSelection`, `Caret`.
- `SetCaretFromPoint(canvas, widget, point, atlas)` maps a click to a caret for
  the app's pointer handler.
- Cluster editing: `BuildTextClusterOffsets(text, shaper, params, offsets)`
  shapes the current string and returns sorted cluster boundaries;
  `MoveCaretClusters`, `BackspaceCluster` and `DeleteForwardCluster` consume
  that map, so a combining mark or a shaped grapheme is deleted/moved as one
  unit. The app supplies the `Fby::Text::ITextShaper` (HarfBuzz in production);
  without a map the codepoint methods remain available.
- `WidgetFlags::ReadOnly` rejects every mutation while a view-only field is
  shown.
- The app owns the platform side: keyboard repeat, IME composition and the
  clipboard. Feed composed text through `InsertText`/`InsertCodepoint`; copy the
  result of `SelectedText` and paste with `InsertText`.
- This field edits short control strings. The default caret stops are codepoint
  boundaries; the cluster API above makes combining marks and shaped graphemes
  edit as one unit. With `SystemContext::textShapes`/`textShaper` configured the
  glyphs are drawn through the shaped path and caret/selection use shaped
  advances; `WidgetFlags::TextRightToLeft` right-aligns the field and mirrors the
  caret. Mixed runs use the cluster order around each glyph to pick the LTR/RTL
  caret edge; the field edges follow the base direction.

Rendering details:

- The field is its own viewport. Background, selection highlight, glyphs and
  caret are clipped to the widget rect (and to any clipping ancestor), so a long
  value never paints outside the box.
- The caret blinks from `WidgetCanvas::uiTime`, which
  `WidgetCanvasSystem::Update` advances. A direct `BuildFrame` caller can call
  `AdvanceTime(dt)` itself.
- `requiredItems` counts the selection and caret quads; the actual caret quad is
  emitted only while the blink phase is visible.
- `SetText` resets the caret to the end. Editing uses the shared text pool and
  compacts it when a growing replacement would exhaust the 8 KiB budget, so
  typing does not leak pool bytes.

## Host wiring

`Text::TextRuntime` owns the two large stores once per host: the 1 MiB
`TextService` and the fixed-capacity `TextShapeCache`. Both are heap-owned
because their arrays do not belong on a frame stack. The shaper is borrowed, so
a host can point it at a `TextFontStack` face (HarfBuzz) or at the compatibility
`CodepointShaper`:

```cpp
Fby::Text::TextRuntime ScopedTextRuntime;
Fby::Text::CodepointShaper ScopedUiShaper;   // or a TextFontStack HarfBuzz face
ScopedTextRuntime.SetShaper(&ScopedUiShaper);

ScopedContext.text = &ScopedTextRuntime.Service();
ScopedContext.textShapes = &ScopedTextRuntime.Shapes();
ScopedContext.textShaper = ScopedTextRuntime.Shaper();
```

The Sandbox and Editor apps now try to mount `fonts/IBMPlexSans-Regular.ttf`
through a `TextFontStack`; on success they use `Stack.Shaper()` and
`Stack.Fonts()`, otherwise they log and keep the compatibility shaper. This
gives the apps a real HarfBuzz path, not only the test path. A host that mounts
a different `TextFontStack` should call `SetShaper(&Stack.Shaper())`, bind
`SystemContext::fonts = &Stack.Fonts()` and re-bind `textShaper`. The runtime
pointers live in `SystemContext`; no ECS component stores an address.

Both apps resolve the asset mount instead of assuming `./assets`: `FBY_ASSET_DIR`
wins, then `cwd/assets`, then `assets` beside the executable and up to three
parent directories. The resolved absolute path is logged. This means an IDE
launch from `build/bin` or `build/bin/tests` still finds the repository assets;
`FBY_ASSET_DIR` can point either at the repository root or directly at the
`assets` directory.

## Text blocks

`WidgetKind::TextBlock` is the UI end of the engine text stack. It stores a
16-byte `Text::TextAssetRef` (no pointer, no copy of the string) and renders the
`TextService` block with `TextLayout` + `TextRender`:

```cpp
Fby::Ui::WidgetDesc Block;
Block.kind = Fby::Ui::WidgetKind::TextBlock;
Block.textRef = ScopedTextService.MakeRef(ScopedDialogueAsset);
Block.fontSize = 20;
Block.textTint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Text);
(void)Hud.AddWidget(Block);
```

- Layout comes from `TextService::CachedLayout`, so bidi (SheenBidi), pixel
  wrapping, spans/styles, vertical text and inline `U+FFFC` atoms are handled by
  the text module, not by the widget.
- `WidgetFlags::TextRightToLeft` and `WidgetFlags::TextVertical` select the
  layout direction for the block.
- The widget is viewport-sized: at most 1024 render items (including
  decorations/effects) are emitted per block, clipped by `ClipChildren`. A whole
  book or a multi-block asset belongs to `TextRenderSystem` or to separate
  `TextBlock` refs per visible page. For paging, `SetTextBlock(widget, index)`
  selects another `TextStore` block of the same asset;
  `TextService::BlockCount(asset)` gives the page count.
- `SetTextLineRange(widget, firstLine, lineCount)` is the viewport hook: shaped
  blocks pass only that window of `ShapedLine` to the renderer, and the codepoint
  fallback copies the same line window before `BuildTextRenderItems`.
  `lineCount == 0` means the whole layout.
- If the host sets `SystemContext::textShapes` and `SystemContext::textShaper`,
  the block is shaped through `TextShapeCache`/HarfBuzz (clusters and glyph ids)
  and drawn with `BuildShapedTextRenderItems`; otherwise it falls back to the
  codepoint `TextRender` path. The shape cache key includes the wrap width and
  vertical mode, so two layouts of the same text do not collide. Inline
  `U+FFFC` atoms are emitted from the shaped path at their placeholder clusters
  (sprite/border/progress/button/separator); `TextService::AddInlineSprite`
  supplies the layer/UV slice for a Sprite atom per asset, so an icon is a real
  atlas quad and two assets may reuse the same id. Shaped wrapping uses the atom width instead of the placeholder advance,
  and the shape cache key includes an inline hash, so a width change cannot
  reuse a stale line layout. Vertical shaped blocks position atoms by their
  height as well.
- `SystemContext::text` must point at a live `TextService`; a null service draws
  nothing. The canvas must use the font texture array, like every other text
  widget.
- Shaped editing (`TextInput` caret/selection on cluster boundaries) is the
  remaining text-stack step; the block itself already uses the shaped path.

## Choice controls

`ToggleSwitch` and `RadioButton` share the `Checked` flag and the completed-click
rule with `Checkbox`, so `ActivateFocused` works for all three after the app maps
Enter/Space.

- `ToggleSwitch` draws a track and a knob that moves to the right when checked.
  It is the settings-row control that reads better than a checkbox for on/off.
- `RadioButton` draws a box and a dot. `radioGroup` links the siblings:
  selecting one clears every other radio in the same non-empty group, and
  clicking an already selected radio keeps it selected. `SetChecked(widget,
  true)` applies the same exclusion. `kNoFocusGroup` leaves a radio ungrouped.
- Both are keyboard-inert until `FocusPolicy` opts in, and both take a completed
  click through `PointerDown`/`PointerUp`.

## Nine-slice frames

`NineSlice` draws a panel from one atlas region with nine quads. `sliceInsets`
is `Vector4D{left, top, right, bottom}` in Units; each side clamps to 45 percent
of the rect so the centre always has a positive area. The UV insets are the
clamped pixel insets divided by the rect size, which assumes the source region
maps to the rect at a 1:1 scale. That covers UI atlases authored at their display
size; a custom painter is the escape hatch for other mappings.

## Composite recipes

These are compositions of the shipped elements, not new kinds.

- **Dropdown / ComboBox**: a `Button` trigger and a popup `Panel` (or `Overlay`)
  with `ClipChildren`, one `Button` per option and a `Scrollbar` when the list is
  longer than the popup. Toggle the popup with `SetVisible`, and call
  `SetFocusScope(popup)` while it is open so Tab stays in the list; on close,
  restore the previous focus and clear the scope. A transparent `Overlay`
  underneath can close on outside clicks.
- **ListView / inventory**: a viewport `Panel` with `ClipChildren`, a content
  container child, one `Button`/`Panel` per row and a `Scrollbar` whose `ratio`
  is `viewportHeight / contentHeight` and whose `progress` drives the row
  offsets. Scroll by editing the row `offsetMin`/`offsetMax` and letting the
  layout pass repaint, because hit testing always follows the layout rect.
  `visualOffset` is presentation-only and is not a substitute for scrolling.
- **Tabs / Accordion**: a row of `Button`s plus a `Switcher` with one child per
  page; `SetActivePage`/`SetActiveWidget` switches the page and the inactive
  pages keep their layout. The selected button is a normal `Checked` state.
- **Modal dialog**: `Overlay` with the `Modal` flag, a `Panel` centred with point
  anchors, `Button` actions and a focus scope. The overlay blocks the pointer and
  confines Tab to the dialog subtree.
- **Settings row**: `Label` plus a `Checkbox`, `ToggleSwitch`, `RadioButton` or
  `Slider`, with `FocusPolicy::TabAndPointer` on the control. `NineSlice` is the
  textured panel behind the section.

## Custom widgets

`WidgetKind::Custom` is the extension point. The application supplies a
`CustomWidgetPaint` function; the engine calls it twice per frame, once to count
the quads for the budget and once to emit them. The callback must therefore be
deterministic and must not call the mutating canvas APIs.

```cpp
void PaintGauge(Fby::Ui::WidgetPaintContext& InContext) noexcept
{
    const Fby::Rect ScopedRect = InContext.LocalRect;
    const float ScopedValue = InContext.Params.X;
    (void)InContext.Quad(ScopedRect, InContext.Fill);
    const float ScopedHeight = ScopedRect.Size().Y * ScopedValue;
    (void)InContext.Quad(Fby::Rect{ScopedRect.min, Fby::Vector2D{ScopedRect.max.X, ScopedRect.min.Y + ScopedHeight}},
                         InContext.Tint);
}

Fby::Ui::WidgetDesc Gauge;
Gauge.kind = Fby::Ui::WidgetKind::Custom;
Gauge.customPaint = &PaintGauge;
Gauge.customParams = Fby::Vector4D{0.75f, 0.0f, 0.0f, 0.0f};
Gauge.tint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Accent);
Gauge.fillTint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Surface);
(void)Hud.AddWidget(Gauge);
```

- The callback receives the resolved rect, the widget's tint/fill, its layer,
  four author-owned floats (`Params`) and an opaque `User` pointer
  (`SetCustomPaint(widget, paint, user)` or `WidgetDesc::customUser`).
- `Quad()` writes one quad with the full source rect; the second overload takes
  a UV rect and a local rotation. The output is capped at
  `kMaxCustomQuadsPerWidget` (64); a painter that ignores the return value is
  still capped by the budget quota.
- Custom widgets are rendered from quads only. Text stays a `Label`/`Button`
  child, which keeps the font budget and the atlas contract intact.
- Custom quads pass through the same `ClipChildren` trim and UV adjustment as
  the built-in kinds.
- A null painter draws nothing, so a custom widget can be parked while its
  behaviour is disabled.

## HUD recipes

A HUD is a screen-space `WidgetCanvas` attached with `WidgetComponent`; use
`WidgetComponentFlags::ScreenSpace` so the camera does not transform it. There
is no separate HUD module to learn: the elements above compose it.

- Health or stamina bar: a `Border` for the frame, a `ProgressBar` inside it
  with the `Vertical` flag, and a `Label` for the numeric value.
- Crosshair: two thin `Panel` widgets (one horizontal, one vertical), or a
  `Border` at a small size, centred with point anchors.
- Ammo or coin counter: an `Image` icon layer plus a `Label` beside it.
- Menu list: a `Panel` with `ClipChildren`, `Button` rows and a `Scrollbar`.
  Rows may extend past the panel and are trimmed to the viewport; the offset is
  application state.
- Menu pages: a `Switcher` with one child per page and `SetActivePage`, or a
  `Switcher` child per tab. Inactive pages keep their layout.
- Modal dialog: an `Overlay` with the `Modal` flag covers the menu, blocks the
  pointer and confines Tab focus to the dialog buttons underneath.
- Custom gauge or cooldown: a `Custom` painter with `Params` for the value,
  layered between an `Image` icon and a `Label`.
- Settings row: a `Label`, a `Checkbox` or `Slider` with
  `FocusPolicy::TabAndPointer`, and a `Button` to apply.
- Subtitles: `AccessibilityKernel::BuildSubtitleCanvas` already builds one from
  `Panel` and `Label` widgets and is the reference composition.

A bottom-left health bar is a `Border` frame, a `ProgressBar` and a point anchor
on the canvas bottom-left corner:

```cpp
Fby::Ui::WidgetCanvas Hud;
Hud.SetCanvasSize(WindowSizeInUnits);
Hud.SetSortLayer(10);

Fby::Ui::WidgetDesc Frame;
Frame.kind = Fby::Ui::WidgetKind::Border;
Frame.anchorMin = {0.0f, 1.0f};
Frame.anchorMax = {0.0f, 1.0f};
Frame.offsetMin = {32.0f, -120.0f};
Frame.offsetMax = {332.0f, -92.0f};
Frame.borderWidth = 2.0f;
Frame.tint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Border);
Frame.flags |= static_cast<std::uint32_t>(Fby::Ui::WidgetFlags::IgnoreHitTest);
(void)Hud.AddWidget(Frame);

Fby::Ui::WidgetDesc Bar;
Bar.kind = Fby::Ui::WidgetKind::ProgressBar;
Bar.anchorMin = {0.0f, 1.0f};
Bar.anchorMax = {0.0f, 1.0f};
Bar.offsetMin = {36.0f, -116.0f};
Bar.offsetMax = {328.0f, -96.0f};
Bar.progress = HealthFraction;
Bar.tint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Surface);
Bar.fillTint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Critical);
(void)Hud.AddWidget(Bar);
```

A text button is one widget: the face is `tint` and the centred caption uses
`textTint`, so no child label is needed:

```cpp
Fby::Ui::WidgetDesc Resume;
Resume.kind = Fby::Ui::WidgetKind::Button;
Resume.anchorMin = {0.5f, 0.5f};
Resume.anchorMax = {0.5f, 0.5f};
Resume.offsetMin = {-120.0f, -24.0f};
Resume.offsetMax = {120.0f, 24.0f};
Resume.text = "Resume";
Resume.fontSize = 20;
Resume.tint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Primary);
Resume.textTint = Hud.Theme().Resolve(Fby::Ui::UiPaletteSlot::Text);
(void)Hud.AddWidget(Resume);
```

## Recommended additions

The engine layer is deliberately small. The items below are the gaps that
matter for a production HUD and menu stack, in the order they unblock the most
work.

| Priority | Element or capability | Why it is needed | Prerequisite |
| --- | --- | --- | --- |
| P0 | Input mapping for focus | `FocusNext`/`FocusMove`/`ActivateFocused` exist, but the app maps keys and gamepad buttons | A named input action map and repeat settings |
| P1 | IME, clipboard and undo for TextInput | The field is UTF-8 and codepoint-aware, but composition, clipboard and history stay app-level | Platform input and an undo stack |
| P1 | `ScrollView` controller | Wheel/keyboard scrolling, rubber-band, child offset and scrollbar sync | Clip exists; needs offset state and an input mapping |
| P1 | Tooltip auto-position | Place a tooltip beside its target and flip at screen edges | Text measurement and a placement pass |
| P1 | `Dropdown`/`ComboBox` | Locale, resolution, quality settings | Composite of Button, Panel and Button rows; clip exists |
| P1 | `ListView` | Content browser and inventory | Virtualisation; clip exists |
| P1 | `Tabs` and `Accordion` | Panels and settings pages | Composite of Button and Switcher, or a small state machine |
| P2 | `RadialProgress`/cooldown ring | Ability HUD | A ring path in the quad shader or the existing `PinkSdf` ring |
| P2 | Segmented progress bar | Ammo pips and shield cells | Segment count in the progress emit |
| P2 | Safe-area insets | Notched and mobile HUDs | Window/platform safe-area query |
| P2 | Ellipsis and full justification | Labels and buttons in tight layouts | Alignment and wrapping exist; needs measurement plus a trim pass |
| P2 | Localization ids | Buttons carry raw text; a data-authored menu should reference locale keys | UI layout asset plus the `LocalizationTable` bridge |
| P2 | UI layout asset and editor | Data-authored menus and localization ids | A serialisable widget record and an editor panel |

Two capabilities are already engine-level and should be used instead of new
widgets: the `PinkSdf` primitive batch for circular and rounded HUD shapes, and
the accessibility kernel for announcements, contrast checks and subtitles.

## Known constraints

- One `DrawBatch` per canvas means every widget shares one pipeline and one
  texture array. A widget cannot switch blend mode or material.
- Clipping is CPU-side trimming: axis-aligned quads trim exactly with UV
  interpolation, rotated quads hide once their centre leaves the clip. There is
  still no per-draw scissor, so arbitrary shader output cannot be clipped.
- `WidgetHandle` is canvas-local: a slot index plus a generation. It carries no
  canvas id, so a reference that crosses canvases must store the canvas
  alongside the handle.
- Text uses the `textFlags` alignment and wrap bits for short control strings;
  long and rich text belongs to `TextBlock`. Ellipsis and full justification are
  not in the widget layer.
- Only the canvas has a world rotation. Per-widget rotation is used internally
  by the checkbox mark, the custom quad overload and nothing else.
- Custom painters emit quads only. Text and child widgets stay separate, and a
  painter runs twice per frame, so it must be deterministic and capped at 64
  quads.
- Hiding is a draw, hit and focus filter, not a layout removal. A hidden widget
  keeps its rect and grid cell.
- Tooltips do not position themselves and need `AdvanceHover` once per frame
  (`WidgetCanvasSystem::Update` does it for ECS canvases).
- `TextInput` is single-line. Composition (IME), system clipboard integration,
  undo/redo and touch selection are application responsibilities; the engine
  provides the UTF-8 buffer, caret, selection and rendering.
- `NineSlice` divides its UV rect by the widget size, so its source region must
  map at 1:1. `ToggleSwitch` and `RadioButton` use axis-aligned quads: no
  rounded pill or circular radio until an SDF path is used.
- Focus has an optional four-quad ring, but the engine still ships no key or
  gamepad mapping. `FocusedWidget` keeps its index when the widget becomes
  hidden; `CanFocus` and `FocusNext` are the authoritative checks.

## Verification

`pink_ui_test` covers the elements and the interaction model: quad counts for a
mixed canvas, Border stroke geometry and clamping, Slider and Scrollbar drag
capture, Checkbox toggling, the vertical progress bar, the hover bits, the
disabled contract, the default-off focus policy, tab order by `tabIndex` and
insertion, groups, wrap, disabled/hidden skips, modal overlay scope, arrow-key
movement, keyboard activation, hiding every kind plus subtree inheritance,
switcher handles and stale-generation rejection, clipped quad/UV/hit/focus
behaviour including nested clips, the tooltip hover delay/pinning/ignore-hit
rule, the TextInput caret/selection/blink and pool compaction, NineSlice
geometry and inset clamping, the ToggleSwitch knob and the RadioButton group
exclusion, the optional focus ring geometry/ordering, the per-state palette
slots, fade/slide transitions with subtree inheritance, short-text alignment
and word wrap, the TextBlock service path with a viewport cap, and the custom
painter budget/quota with an allocation-free paint. The related
`pinkui_font_mount_test` and `ui_accessibility_test` still pass. Strict shaping,
bidi, inline objects and very large text are covered by the `Fby::Text` tests
(`text_render`, `text_golden`, `font_paging`), not by the widget test.
