# Accessibility guide

[English](accessibility-guide.md) | [Русский перевод](accessibility-guide_RU.md)

This guide is for developers who ship a game or work in the editor. It covers
the three output channels the engine provides — the built-in narrator, the
operating-system screen-reader bridge and captions — plus the visual comfort
settings: flash guard, colour vision, the OLED orbit and the editor themes.

The implementation contract (the profile structure, the GPU composite path and
the semantic tree) lives in [accessibility.md](accessibility.md).
This page is about using it.

## 1. Three ways the engine speaks

| Channel | Who speaks | Use it when |
|---|---|---|
| Narrator | the engine itself, through SAPI, AVSpeech or a custom backend | the player has no screen reader; game content (dialogue, captions, world events) must be spoken; the content language differs from the OS voice |
| Screen-reader bridge | the player's screen reader (Narrator, NVDA, JAWS, VoiceOver, Orca) | the player already runs assistive technology and expects their own voice, rate and commands |
| Captions | text only | deaf and hard-of-hearing players, and as a fallback when no voice is available |

The narrator and the bridge read the same semantic tree, so they can never
describe two different interfaces. Captions are a separate queue and can be on
while the bridge and the narrator are off.

## 2. Quick start in a game

```cpp
Fby::AccessibilityKernel ScopedAccessibility;
ScopedAccessibility.ApplyProfile(ProjectSettings.accessibility); // reads the policy
ScopedAccessibility.SetSpeechOwner(Fby::NarratorOwner::Game);
(void)ScopedAccessibility.StartBridge(ScopedWindow.NativeWindow());
ScopedAccessibility.SetCueLocalization(&ScopedLocalization); // optional: localized names

// Once per frame, after the UI canvas has been built:
ScopedAccessibility.ProjectCanvas(ScopedUiCanvas);

// The usual frame calls stay unchanged:
ScopedAccessibility.ApplyToFrame(ScopedFrame);
ScopedAccessibility.ApplyPostFx(ScopedFrame.postFx);
ScopedAccessibility.FlushSpeech(); // main thread only
```

`ApplyProfile()` is the single switch: it applies the runtime settings, the
narrator policy, the bridge policy and the frame flags at once. Individual
setters exist for tests and for runtime toggles.

`fby_sandbox` wires exactly this path; F2 toggles Blind mode, F3 Deaf mode, F4
the flash guard and F5 colour vision.

## 3. The narrator in a game

The narrator is a fixed-capacity speech queue: priorities, duplicate suppression
for 600 ms per node id, sentence-bounded chunks of at most 320 bytes and one
utterance at a time. It never calls a COM API from a worker thread;
`FlushSpeech()` is the main-thread pump.

| Project key (`[accessibility]`) | Default | Effect |
|---|---|---|
| `narration_enabled` | true | master switch for speech; captions keep working when it is off |
| `narration_focus` | true | announce a focus move |
| `narration_role` | true | include the role ("button", "slider") |
| `narration_state` | true | include checked, disabled and read-only |
| `narration_scene` | false | speak a full scene description on demand |
| `narration_world` | true | Blind-mode world events: contacts, tag changes |
| `narration_interrupt` | true | a focus move cuts the current line |
| `narration_verbosity` | 2 | 0 silent, 1 name only, 2 name and role, 3 name, role and state |
| `narration_rate` | 1.0 | speech rate, clamped to 0.5 through 2.0 |
| `narration_max_queue` | 8 | queued utterances, clamped to 1 through 16 |
| `narration_language` | `en-US` | BCP-47 tag of the voice |
| `narration_voice` | empty | exact installed voice id; empty means "auto by language" |

The voice follows the **content locale**, not the OS locale: a game translated
into Japanese speaks Japanese on an English Windows install. `SetNarrativeLocale()`
normalises the tag (`pt_BR` becomes `pt-BR`), and the narrator resolves it
against the installed voices: exact tag, then language, then the system default,
with a warning in the `tts` log when the fallback happens.

The **Voice** combo in Project Settings (and in the editor narrator section)
lists every installed voice with its language, plus "Auto (by language)". A
chosen voice is stored as `narration_voice` and wins over the language search
while it stays installed; if the user uninstalls it, the narrator falls back to
the language policy and logs the fallback. The list is refreshed by the
"Refresh installed voices" entry, so a voice installed while the editor runs
appears without a restart.

Two honest limits:

- The engine ships no voices. On Windows it asks both speech stacks: desktop
  SAPI voices and the modern one-core voices through WinRT (SAPI does not always
  list the latter). macOS exposes AVSpeech voices; Linux uses speech-dispatcher
  when the client library and the daemon are installed, and stays silent
  otherwise. A language is spoken only when a voice for it is installed; the
  editor panel, the voice list and the log report the mismatch.
- Reliability of a language across platforms is a matrix, not a promise. Treat
  "spoken if a voice exists, otherwise a clear fallback" as the contract.

Narration of long text is chunked, so a dialogue line longer than 320 bytes is
spoken as several utterances in order. A burst of world events is bounded by
`narration_max_queue`; lower-priority lines are dropped first, and the dropped
count is visible on the narrator.

## 4. The narrator in the editor

The editor keeps its own narrator policy in **Editor Settings → Narrator (editor
window)**; a game's project policy never changes the editor, and an editor policy
never ships with a build.

- It is **off by default**. The checkbox speaks panels, focus, errors and Play In
  Editor state changes.
- Turning on **Blind mode** turns the editor narrator on with it.
- Panel focus speaks the panel name. Widget focus speaks the widget label and
  role ("Save, button"). The label and role come from the vendored ImGui patch
  `third_party/patches/imgui/a11y-item-labels.patch`; the button, checkbox, radio
  button, slider, drag, input text and combo widgets carry a role today, and menu
  items and selectables announce their label when ImGui gives them a non-empty id.
- A panel can push a custom line through `EditorSystem::Announce()`, which is how
  commits, build results and console messages reach the voice.

While Play In Editor runs, the game owns the voice and the editor queues nothing;
the editor also keeps publishing its tree to the bridge.

## 5. The screen-reader bridge

On Windows the engine installs a UI Automation fragment provider on the process
window. It is independent from the narrator and can be on while speech is off.

Published per node: `Name`, `ControlType`, `BoundingRectangle`, `IsEnabled`,
`IsKeyboardFocusable`, `HasKeyboardFocus`, `IsOffscreen`, `AutomationId` and
`FrameworkId`. Buttons and toggles expose `IInvokeProvider` and `IToggleProvider`.
Focus, structure and state events are raised from the tree event ring; a rebuild
that produces the same ids stays silent.

In the editor, every visible ImGui window is a pane and the focused widget is a
child of its pane, so a screen reader can answer both "which panel" and "which
control".

Project keys (`[accessibility]`):

| Key | Default | Effect |
|---|---|---|
| `bridge_enabled` | true | publish the tree to the OS |
| `bridge_focus_events` | true | raise focus-changed events |
| `bridge_structure_events` | true | raise structure changes when the id sequence changes |
| `bridge_state_events` | true | raise state property changes |

How to verify:

1. Run the game or the editor and look for `a11y: accessibility bridge is live
   on Windows` in the log.
2. Open Accessibility Insights for Windows or Inspect and attach to the window;
   the tree should show the root, the panes and the focused control.
3. With Narrator or NVDA, Tab through the interface: the reader must follow the
   focus and read the label.

Limits: Linux (AT-SPI2) and macOS (NSAccessibility) are stubs today; the tree and
the narrator still work there. Exclusive fullscreen can hide the window from some
screen readers, so prefer borderless fullscreen for accessibility testing.

## 6. Captions and Deaf mode

`AccessibilityOverlay` owns the caption canvas and the fixed storage;
`ComposeUiOverlay()` appends it after the game items without allocating. Sound
cues with a localization id, timed audio markers, and a voice-caption provider
(VAD label or speech-to-text) all feed `SubtitleQueue`.

- Deaf mode mutes the Master bus and shows the captions.
- `show_subtitles` shows them without muting.
- Blind mode mirrors the same text into the narrative log.

## 7. Visual comfort

**Flash guard** — limits the luminance change per frame. `flash_threshold`
(0.02 through 0.45) is the allowed delta and `flash_gain` (0.05 through 1.0) is
the residual retention of a larger flash. A calm frame is never dimmed: the
guard's live gain is 1.0 until a flash is detected.

**Colour vision** — Machado 2009 matrices with a 0 through 1 strength. The GPU
composite applies the matrix and the flash gain in one pass
(`accessibility_composite.frag`); `ApplyCpuColorFallback()` does the same on the
CPU for a path without a final composite and must not be combined with the GPU
pass.

**OLED orbit** — `oled_pixel_shift` moves the UI by an integer 2 px every
`oled_interval_seconds` (1 through 3600) to avoid static burn-in. It is
presentation-only: layout and hit testing are unaffected.

**HUD and motion** — `hud_fade_after_idle_seconds` fades static HUD elements
after idle time, `motion_scale` (0 through 1) scales caret blinks, shakes and
panel animation, and `minimum_contrast_ratio` (1 through 21) is the WCAG 2.2
floor a theme must meet.

**Project motion comfort** — `[comfort]` in `project.femproj`:

| Key | Default | Consumer |
|---|---|---|
| `camera_shake_scale` | 1 | camera stack |
| `head_bob_scale` | 1 | game camera policy |
| `motion_blur_scale` | 1 | post-process policy |
| `fov_scale` | 1 | game camera policy |
| `vignette_scale` | 1 | composite pass in Play In Editor |

Input behaviour (hold/tap/pulse/repeat, deadzone, scale, invert, one-button
schemes) stays with the input mapping and the game rules; it is not a global
accessibility switch.

## 8. Editor themes and symbol colours

The editor deliberately does **not** apply a colour-vision matrix to the whole
window: that would change panels, screenshots and asset previews together and
make authoring colours lie. Three independent controls solve the same problem
without that side effect:

- **Panel theme** — surface tints only: `graphite`, `very-dark` (near-black gray,
  not pure black), `midnight`, `forest`, `plum`, `amber`, `crimson`. `panel_glow`
  adds a soft accent edge; zero disables it.
- **Text** — sRGB colour, opacity and a contrast multiplier against the selected
  panel background. The authored text is never silently replaced by a theme.
- **Symbol colours** — axes, grid, playback icons, rotate gizmo and plot lines.
  This is the right place to fix a colour-vision problem: meaning is preserved
  while panels and previews stay truthful.

**Text outline** is a separate checkbox with colour and pixel size, rendered by
the vendored ImGui text renderer (`third_party/patches/imgui/text-outline.patch`)
as real glyph outlines, not a background strip.

The editor's own comfort switches (`ide_blind_mode`, `ide_deaf_mode`,
`ide_flash_guard`, `ide_high_contrast`, `ide_reduce_motion`, `ide_color_vision`,
`ide_color_vision_strength`, `ide_flash_threshold`) live in the project `[editor]`
section; the full table is in
[Editor Settings](../md/en/EditorSettingsReference_EN.md#37-editor-accessibility).

## 9. Settings map

| Where | What lives there |
|---|---|
| Project Settings → Accessibility | game runtime flags, captions, colour vision, flash guard, OLED, motion, contrast |
| Project Settings → Narrator (game) | the `narration_*` keys above |
| Project Settings → Screen reader bridge (game) | the `bridge_*` keys above |
| Editor Settings → Accessibility (editor window) | the `ide_*` keys for the editor UI |
| Editor Settings → Narrator (editor window) | the editor's own `narration_*` keys |
| Editor Settings → Screen reader bridge (editor window) | the editor's own `bridge_*` keys |
| Project Settings → Motion comfort (project) | the `[comfort]` keys |

The game policy is stored in `project.femproj`; the editor policy is stored in
the project `[editor]` section and mirrored to `editor.ini`. A shipped build reads
only the project `[accessibility]` policy.

## 10. Troubleshooting

| Symptom | Check |
|---|---|
| Nothing is spoken | `narration_enabled`, Blind mode (for world events), the `tts` log for a missing voice, `narration_max_queue` drops |
| The narrator is silent in the editor | the editor narrator is off by default; enable it or turn on Blind mode |
| The screen reader sees nothing | `bridge_enabled`, the `a11y` log line, exclusive fullscreen, platform (Linux and macOS are stubs) |
| Speech talks over the game | during Play In Editor the game owns the voice; the editor is muted by design |
| A language speaks with the wrong voice | no installed voice matched `narration_language`; the log names the fallback |
| The OneCore voice list is empty on Windows | the C++/WinRT projection was not found when the engine was configured; the configure log prints "One-core speech: ..." |
| Captions do not appear | Deaf mode or `show_subtitles`, and a cue with a localization id or a caption provider |

Log categories: `a11y` (bridge lifecycle), `tts` (voice selection and backend
errors), `editor` (panel narration).

## 11. Verification

- `fby_accessibility_narrator_test` — tree, projection, roles, deduplication,
  priority, chunking, language selection, owner arbitration and kernel focus
  narration.
- `fby_ui_accessibility_test` — profile defaults, clamping, frame mapping,
  caption order and the overlay composition.
- `fby_accessibility_composite_test` — the GPU composite on a real Vulkan device.
- Manual pass: Narrator or NVDA over the editor and a built game menu, captions in
  Deaf mode, and the OLED orbit on a static bright screen.
