# Accessibility contract

This document is the implementation contract: the profile, the semantic tree, the
GPU composite path and the platform bridge. The user-facing guide — how to turn
the narrator on, what a screen reader sees, the OLED orbit and the editor themes
— is [Accessibility guide](accessibility-guide.md).

Accessibility is one profile applied to the frame, not a set of scattered
switches. The runtime section drives the game, the presentation section drives
the HUD, and the editor section drives the ImGui window. Complete Reference
chapter 13 documents the primitives.

## One profile

`Source/Engine/UI/Accessibility.hpp` defines `AccessibilityProfile` with three
POD sections:

- `runtime` (`AccessibilitySettings`): blind, deaf, flash guard, colour vision
  and its strength;
- `presentation`: OLED orbit, motion scale, HUD idle fade, minimum contrast;
- `narration` and `bridge`: what the game speaks and what it publishes to the OS;
- `editor`: blind/deaf/high-contrast/reduce-motion for the editor window itself;
  the editor theme and symbol colours are separate authoring settings, and the
  editor's narrator and bridge policy live in `EditorSettings`.

`DefaultAccessibilityProfile()` is the default policy: flash guard and OLED orbit
on, no colour correction, full motion, narration and the OS bridge enabled.

`AccessibilityKernel::ApplyProfile()` is the single apply entry point. It copies
the profile, clamps hostile values (strength 0..1, flash threshold 0.02..0.45,
motion 0..1, contrast 1..21), stores the runtime section and reconfigures the
flash guard. `Profile()` returns the live profile, so individual setters and a
saved profile cannot disagree.

`ProjectSettings` carries one `AccessibilityProfile accessibility` with the
runtime, presentation, narration and bridge policy of the game. `EditorSettings`
keeps the editor's own narrator and bridge policy (`narration`, `bridge`) next to
the `[editor]` keys, so the editor never shares a policy with a build. The
`editor` section of the profile still holds the `ide_*` switches. `project.femproj`
keeps the old keys (`[accessibility]`, `[editor] ide_*`) and adds
`profile_version`, `color_vision`, `color_vision_strength`, `motion_scale`,
`hud_fade_after_idle_seconds` and `minimum_contrast_ratio`, so existing projects
load unchanged.

`ProjectSettingsPanel::Apply()` calls `ApplyProfile()` once and derives the Master
mute from the same flags; `EditorSettingsPanel` edits the `editor` section, the
editor narrator and the editor bridge, and applies the palette to the ImGui
window.

## Editor themes and symbol colours

The editor does not apply a colour-vision matrix to the whole window. A full
matrix changes every panel, screenshot and asset preview at once and makes the
authoring colours lie. The editor separates three independent controls:

- **Panel theme** — surface tints only. `graphite`, `very-dark` (a near-black
  gray, not pure black), `midnight`, `forest`, `plum`, `amber` and `crimson`;
  the RGB variants are dark tinted workspaces, not full-editor colour masks.
  A low-strength `panelGlow` adds a soft accent edge around panels; zero
  disables it.
- **Text** — sRGB colour, opacity and contrast multiplier. Text defaults to a
  shade of white and is enforced against the selected panel background with a
  WCAG ratio; changing the theme never silently changes the authored text.
- **Symbol colours** — `AxisX`, `AxisY`, `Grid`, `Play`, `Pause`, `Step`, `Stop`,
  `Rotate`, `Multiplayer` and four plot colours. This is the correct place to
  solve a colour-vision problem: it preserves the meaning of each control while
  leaving panel backgrounds and asset previews untouched.

The palette is consumed by the viewport axes, the world grid, playback icons,
the rotate gizmo control and the ImGui/ImPlot line colours. The settings panel
edits every value directly and persists it in `project.femproj` and `editor.ini`.
Colour inversion is not part of this contract. Text outline is a separate
checkbox with colour and pixel size. It is implemented in the vendored ImGui
text renderer (``ImGuiStyle::TextBorderSize`` / ``TextBorderColor``), so it draws
real glyph outlines instead of a fake background strip; the patch is tracked
under ``third_party/patches/imgui/text-outline.patch``.

## Frame path

- `ApplyPostFx()` translates the profile into `Rhi::PostFxSettings`: the
  `PostFxFlashGuard` and `PostFxColorVision` bits, the flash residual gain and
  the colour-vision matrix selector. `PostFxPass` bakes the matrix rows into the
  composite constants and `postfx_composite.frag` applies both terms, so the
  shader and the kernel read one profile.
- The flash residual is the guard's live gain (`CurrentGain()`), not the
  configured floor, so a calm frame is never dimmed by default.
- `fby_sandbox` owns an `AccessibilityKernel`, applies the default profile once,
  toggles blind/deaf/flash/colour vision with F2-F5 and calls `ApplyToFrame()` on
  every packet. The Clean Slate `GameMain.cpp` does the same around `Submit()`,
  with a comment where `BuildSubtitleCanvas()` composes the caption overlay.
- The editor composes captions and applies the project profile to the PIE
  viewport packet only, so a game blind mode blanks the play view, never the
  authoring window.

## Captions

`AccessibilityOverlay` owns the subtitle canvas and the fixed storage.
`ComposeUiOverlay()` appends the UI items after the game items, offsets the
overlay batches by the game item count and stamps the font texture array.
`fby_sandbox` builds the overlay every frame, so deaf mode shows a real caption
panel; the Clean Slate template carries the same call. The composition is
CPU-only and refuses when storage is short; an empty overlay is a no-op.

## CPU colour fallback

`ApplyCpuColorFallback()` corrects the clear colour and every item colour with the
same matrix and guard gain for a path without a final composite. It is exact for
the clear colour and approximate for alpha-blended sprites, and it is mutually
exclusive with `ApplyPostFx()`.

## GPU composite

- `Shaders/accessibility_composite.frag` samples the scene image and applies the
  colour-vision matrix and the flash residual gain from one push block.
- Every render target keeps a companion scene image, view, framebuffer and
  descriptor set. `RenderToTarget` redirects the scene there when
  `AccessibilityCompositeWanted()` is true, runs one fullscreen pass into the
  target, then hits the capture barrier. Pipeline, layout, sampler and pool are
  created once; without them the old path renders (fail closed).
- The swapchain path keeps a private scene image, view, framebuffer and
  descriptor set sized with the swapchain (recreated with it). `Submit` renders
  into it when the policy asks, and `EndFrame` composites into the acquired
  swapchain image.
- `Shaders/accessibility_flash.comp` runs before the composite with an R8
  luminance history per pixel and clamps a sudden delta to the residual gain. It
  stores raw luminance, so a static bright frame stops being dimmed after the
  first frame; the composite owns colour vision only, so the two terms cannot
  darken a frame twice.
- Both the offscreen and swapchain paths keep their own history image and
  descriptor set, so sandbox and PIE share one behaviour and every resource is
  recreated with the target that owns it.

## Render graph

The render graph executes compute passes, but its graphics and post records are
not the shipping post-FX path. The Vulkan backend consumes `FramePacket.postFx`
in `RecordPostFxComposite`, and that is where the accessibility effect settings
and colour-vision correction are applied. The render-graph `PostFxPass` remains
a separate, non-shipping path.

## Tests

`accessibility_narrator` (semantic tree, PinkUI projection, roles, narrator
priority, deduplication, chunking, language selection, owner arbitration and the
kernel focus announcement), `ui_accessibility` (defaults, clamping, live view,
frame mapping, `ApplyPostFx` flags/matrix/gain, caption append order and batch
offset) and `accessibility_composite` (8x8 offscreen target on a real Vulkan device:
baseline, deuteranopia matrix, zero-correction byte-for-byte baseline, 0.5
residual dimming, lone flash flag, dark-to-bright clamp, unguarded control,
flagged swapchain submit). The composite test skips without a Vulkan device.

## Semantic tree, projection and narrator

The accessibility output is one semantic tree shared by the game and the editor.
Nothing describes the interface twice.

- `Ui::AccessibilityTree` (`Source/Engine/UI/AccessibilityTree.hpp`) is a fixed
  capacity SoA store: ids, parents, roles, states, screen bounds, localization
  ids and a text pool. A rebuild keeps the focused id, so a screen reader never
  receives a spurious focus change. A structure event is raised only when the
  id sequence actually changed.
- `Ui::ProjectWidgetCanvas()` projects a retained `WidgetCanvas`: `WidgetKind`
  becomes an `AccessibilityRole`, `WidgetFlags` become state bits, the text key
  becomes the accessible name, and the canvas focus becomes the tree focus. A
  widget can override its spoken name with
  `WidgetCanvas::SetAccessibleName(id, locId)`.
- `Narrator` (`Source/Engine/UI/Narrator.hpp`) owns the speech queue: priority,
  deduplication by node id inside a 600 ms window, sentence-bounded chunking at
  320 bytes, and one utterance at a time so SAPI and AVSpeech never stack a
  hundred lines. It speaks the content locale, not the OS locale.
- `AccessibilityKernel::ProjectCanvas()` rebuilds the tree, announces a focus
  change in Blind mode and publishes the result to the OS bridge. The game calls
  it once per frame after building its UI canvas.

## Platform bridge

`Source/Engine/UI/AccessibilityBridge.hpp` is the provider seam. On Windows the
engine installs a UI Automation fragment provider on the process window: nodes
answer `Name`, `ControlType`, `BoundingRectangle`, `IsEnabled`,
`IsKeyboardFocusable`, `HasKeyboardFocus`, `IsOffscreen`, `AutomationId` and
`FrameworkId`; buttons and toggles expose `IInvokeProvider` and `IToggleProvider`;
focus, structure and state events are raised from the tree ring. The provider
runs on the UI thread, so no tree lock exists and a query never allocates a
copy of the interface. Other platforms keep the stub: the narrator still works
and the tree stays available for a future AT-SPI2 or NSAccessibility provider.

The narrator's synthesis layer is separate from the bridge. On Windows it is a
composite of desktop SAPI voices and the one-core voices exposed by the WinRT
speech stack; ids of the latter carry a `winrt:` prefix so a stored
`narration_voice` is unambiguous. The WinRT adapter is the only engine
translation unit compiled with exceptions enabled, because the C++/WinRT
projection requires them; without the SDK projection the engine logs
"One-core speech: ... not found" and keeps SAPI only. On Linux the adapter loads
`libspeechd` with `dlopen` and falls back to silence when the daemon is absent;
speech-dispatcher has no enumerable voice ids, so the language tag is the whole
voice policy there.

Both OS adapters play through their own audio path (the WinRT `MediaPlayer` and
the speech-dispatcher daemon), not through the engine mixer, so music ducking and
spatialisation do not apply to system voices. The narrator still owns the text,
the queue and the language policy in every case.

The language policy is documented separately: the engine ships a 42-language
catalog, normalizes BCP-47 tags and resolves voices through one fallback chain
for every backend. See [speech languages](speech-languages.md).

Gameplay events use a second, data-driven channel. `ActionNarration` resolves
localization templates with plural variants, injects localized names and
direction/distance words from `ActionVocabulary`, applies category and priority
policy, and queues through the same narrator. The composed line is returned
before it is spoken, so captions can reuse it. See
[action narration](action-narration.md).

Projects can also install voice packs. A pack is a directory with a manifest, a
helper executable and a model; the engine starts the helper and writes a
length-prefixed command stream to its stdin. The helper owns playback and is
never linked into the engine, which keeps separately licensed (including GPL)
helpers and models outside the MPL 2.0 terms of the engine. See
[voice packs](voice-packs.md).

The editor publishes one pane per visible ImGui window through the same tree,
plus a child node for the focused widget. The widget node comes from the
vendored ImGui patch `third_party/patches/imgui/a11y-item-labels.patch`:

- `ImGuiWindow::GetID()` records the label that produced the id; `ItemAdd()`
  consumes it only when the ids match, so an unrelated `GetID()` cannot leak a
  label onto the next widget.
- `ItemAdd()` captures the label, the item rect and the role of the item that
  owns `NavId`, and `NewFrame()` clears the capture on the next frame.
- The button, invisible button, checkbox, radio button, slider, drag, input
  text and combo widgets call `SetAccessibilityItemRole()` right after
  `ItemAdd()`; the application reads the result through
  `GetAccessibilityFocusLabel()`, `GetAccessibilityFocusRole()`,
  `GetAccessibilityFocusId()` and `GetAccessibilityFocusBounds()`.

The editor narrator therefore speaks "Save, button" and not only the panel
name, and a panel can still push a custom line through
`EditorSystem::Announce()`. Menu items and selectables have no role of their own
yet: they still announce their label when ImGui gives them a non-empty id.

## Settings

Game (project file, edited in Project Settings):

| Key (`[accessibility]`) | Meaning |
|---|---|
| `narration_enabled` | speak the interface and world events |
| `narration_focus`, `narration_role`, `narration_state` | what a focus announcement contains |
| `narration_scene`, `narration_world` | on-demand scene description and Blind mode world events |
| `narration_interrupt` | a focus move cuts the current line |
| `narration_verbosity` | 0 silent, 1 name, 2 name and role, 3 name, role and state |
| `narration_rate`, `narration_max_queue`, `narration_language` | voice policy (BCP-47) |
| `narration_voice` | exact installed voice id; empty means "auto by language" |
| `bridge_enabled`, `bridge_focus_events`, `bridge_structure_events`, `bridge_state_events` | what the game publishes to the OS |

Editor (project `[editor]` section and `editor.ini`, edited in Editor Settings):
the same narrator and bridge keys, read only by the editor window. The editor
narrator is off by default and Blind mode turns it on; the game and the editor
never share a policy, and a running Play In Editor session owns the voice.


## Project motion comfort and input policy

Motion comfort and input accessibility are project settings, not plugins.

`[comfort]` in `project.femproj`:

| Key | Default | Range | Consumer |
|---|---|---|---|
| `camera_shake_scale` | 1 | 0..1 | `CameraStackSystem::SetComfortShakeScale` |
| `head_bob_scale` | 1 | 0..1 | game camera policy |
| `fov_scale` | 1 | 0.5..1.5 | game camera policy |
| `motion_blur_scale` | 1 | 0..1 | game post-process policy |
| `vignette_scale` | 1 | 0..1.5 | `PostFxSettings.vignetteStrength` in PIE |

Input behaviour is not a project accessibility table. Hold/Tap/Pulse/Repeat,
`Deadzone`, `Scale`, `Invert` and swizzle are already per-mapping triggers and
modifiers in `InputSystem`. One-button mode and aim assist are authored control
schemes or gameplay rules, not engine-wide flags, so they stay with the game.

Gamepad buttons/axes are part of `InputSource` and `GamepadState`; the SDL3
backend opens and closes gamepads. Mobile touch is part of the same input path:
`InputState::touches`, `InputSourceKind::Touch` and `InputSourceKind::TouchAxis`.
`WindowManager` consumes `SDL_EVENT_FINGER_*` events; touch coordinates are
normalised client coordinates.
