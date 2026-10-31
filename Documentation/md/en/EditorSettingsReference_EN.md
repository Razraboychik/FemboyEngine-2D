# Editor Settings Reference

This document is the complete reference for the editor's own settings in FemboyEngine2D 0.1.0-alpha.1: every key, its type, its default, its clamp, what it changes and where it is stored. It covers the `[editor]` table of `project.femproj`, the single `[render]` key that the editor owns, and the `[FemboyEditor]` mirror in `editor.ini`.

The settings are environment preferences, not gameplay. They are stored next to the project so a team reproduces the same authoring environment, they are mirrored to the editor INI for humans and legacy readers, and they never change how a shipped game behaves. No shipped runtime reads them.

## 1. One struct, one file

Every editor setting lives in one struct:

| File | Role |
|---|---|
| `Source/Editor/Infrastructure/EditorSettings.hpp` | The `EditorSettings` struct: the field list, the defaults and the types. This is the single settings file; a new setting starts here. |
| `Source/Editor/Infrastructure/ProjectConfig.cpp` | The `[editor]` TOML codec: `EncodeProjectToml` writes the table, `DecodeProjectToml` reads it and applies the clamps |
| `Source/Editor/Infrastructure/EditorSettings.cpp` | The `AppendEditorSettingsIni` mirror that rewrites the `[FemboyEditor]` section of `editor.ini` |
| `Source/Editor/Panels/SettingsPanels.cpp` | The Editor Settings panel: the widgets, their panel ranges and their tooltips |
| `Source/Engine/UI/Accessibility.hpp` | `AccessibilityEditorSettings`, the eight `ide_*` keys that the editor window also owns |

The struct is trivially copyable and standard layout, and it has a defaulted `operator==`, so the store can detect that something changed without a hand-written comparison per field. That is what makes "save on commit" cheap.

Two persistence surfaces exist and they are not equal:

| Surface | Written by | Read back | Role |
|---|---|---|---|
| `project.femproj`, `[editor]` table | `EncodeProjectToml` | `DecodeProjectToml` | The source of truth |
| `editor.ini`, `[FemboyEditor]` section | `AppendEditorSettingsIni` | Not read by the editor | A human-readable and legacy-reader mirror |

Because the project file is the source of truth, editing `editor.ini` by hand does not change the editor's behaviour on the next start: the next save rewrites the section from the decoded project values. Edit the project file, or use the panel.

Decoding is defensive. Every value is clamped to its documented range, an unreadable value falls back to the default, an out-of-range view mode becomes Standard, and an unknown key is ignored. Encoding writes every known key and nothing else, so an unknown key is dropped on the next save and a save normalises the file.

## 2. The complete key table

Types are the TOML types written by the encoder and accepted by the decoder. "Default" is the value in `EditorSettings.hpp` and therefore the value a new project starts with. "Clamp" is the range that `DecodeProjectToml` enforces on read.

| TOML key | Field | Editor INI key | Type | Default | Clamp | Meaning |
|---|---|---|---|---|---|---|
| `[editor] ui_scale` | `uiScale` | `ui_scale` | float | 1.0 | 0.5 through 3.0 | Scale of the whole editor UI, including panels, toolbars and tooltips |
| `[editor] font_panel_title` | `fontPanelTitle` | `font_panel_title` | float | 24.0 | 16 through 48 px | Font size of panel and window titles |
| `[editor] font_panel_body` | `fontPanelBody` | `font_panel_body` | float | 18.0 | 16 through 40 px | Font size of ordinary panel text; 18 px is the readable floor on a 1080p panel |
| `[editor] font_tooltip` | `fontTooltip` | `font_tooltip` | float | 18.0 | 16 through 40 px | Font size of tooltips and the viewport help card |
| `[editor] font_script_node_title` | `fontScriptNodeTitle` | `font_script_node_title` | float | 21.0 | 16 through 40 px | Font size of a script graph node title |
| `[editor] font_script_pin_input` | `fontScriptPinInput` | `font_script_pin_input` | float | 18.0 | 16 through 40 px | Font size of an input pin label |
| `[editor] font_script_pin_output` | `fontScriptPinOutput` | `font_script_pin_output` | float | 18.0 | 16 through 40 px | Font size of an output pin label |
| `[editor] font_scene_text` | `fontSceneText` | `font_scene_text` | float | 18.0 | 16 through 64 px | Font size of text drawn in the scene view |
| `[editor] show_demo` | `showDemoWindow` | `show_demo` | bool | false | true or false | Shows the Dear ImGui demo window; a development aid, not a shipping feature |
| `[editor] auto_refresh_browser` | `autoRefreshBrowser` | `auto_refresh_browser` | bool | true | true or false | Refreshes the Content Browser when the asset database changes |
| `[editor] watch_assets` | `watchAssets` | `watch_assets` | bool | true | true or false | Runs the OS file watcher and the hot-reload path |
| `[editor] show_grid` | `showGrid` | `show_grid` | bool | true | true or false | Draws the scene grid |
| `[editor] viewport_view_mode` | `viewportViewMode` | `viewport_view_mode` | int | 0 | 0 through 5; invalid values become 0 | The active viewport diagnostic mode; see section 4 |
| `[editor] viewport_complexity_budget` | `viewportComplexityBudget` | `viewport_complexity_budget` | int | 8 | 1 through 64 | Normalisation reference of the Shader Complexity heat ramp; see section 4 |
| `[editor] window_mode` | `windowMode` | `window_mode` | int | 1 | 0 through 2 | 0 windowed, 1 borderless fullscreen, 2 exclusive fullscreen |
| `[editor] window_width` | `windowWidth` | `window_width` | int | 1600 | 320 through 16384 | Windowed width in pixels, used by modes 0 and 1 |
| `[editor] window_height` | `windowHeight` | `window_height` | int | 900 | 240 through 16384 | Windowed height in pixels, used by modes 0 and 1 |
| `[editor] save_ini_on_commit` | `saveEditorIniOnCommit` | `save_ini_on_commit` | bool | true | true or false | Rewrites the `[FemboyEditor]` section of `editor.ini` after a settings commit |
| `[editor] theme` | `theme` | `theme` | string | "graphite" | Up to 31 bytes | Preset name of the editor palette |
| `[editor] ide_blind_mode` | - | `ide_blind_mode` | bool | false | true or false | Keyboard navigation, always-visible focus and the narration sink for the editor window |
| `[editor] ide_deaf_mode` | - | `ide_deaf_mode` | bool | false | true or false | Visual status cues only; no editor audio alerts |
| `[editor] ide_flash_guard` | - | `ide_flash_guard` | bool | true | true or false | FlashGuard on the editor background |
| `[editor] ide_high_contrast` | - | `ide_high_contrast` | bool | false | true or false | High-contrast palette at a 4.5:1 minimum contrast ratio |
| `[editor] ide_reduce_motion` | - | `ide_reduce_motion` | bool | false | true or false | Removes blinking and other repeated motion |
| `[editor] ide_color_vision` | - | `ide_color_vision` | int | 0 | 0 through 3 | 0 none, 1 protanopia, 2 deuteranopia, 3 tritanopia |
| `[editor] ide_color_vision_strength` | - | `ide_color_vision_strength` | float | 1.0 | 0.0 through 1.0 | Strength of the colour-vision correction |
| `[editor] ide_flash_threshold` | - | `ide_flash_threshold` | float | 0.20 | 0.02 through 1.0 | Largest luminance change per frame that FlashGuard allows |
| `[render] vsync` | `vsync` | `vsync` | bool | true | true or false | Vertical sync of the editor window; the only editor key that lives outside `[editor]` |

Three details that are easy to miss:

- `vsync` is encoded into the `[render]` table, next to `texture_format` and `max_texture_size`. It is still an editor setting, it still has a struct field, and it is still mirrored into `[FemboyEditor]`; only its TOML home is different.
- The eight `ide_*` keys belong to `AccessibilityEditorSettings`, not to `EditorSettings`, because the accessibility profile is shared with the game. The editor owns only the `editor` sub-profile of that structure.
- `theme` is a fixed 32-byte buffer, so a decoded value is truncated to 31 bytes and a longer string never overflows. The 1.0 editor paints one built-in dark palette and persists the name for compatibility; other values are carried forward but do not repaint the editor yet.

## 3. Groups in reading order

### 3.1 Display

| Key | Notes |
|---|---|
| `ui_scale` | Applies to the whole editor UI. The panel slider covers 0.75 through 2.0, while the file clamp allows 0.5 through 3.0, so a hand-edited extreme value is accepted and a normal value is easy to reach with the slider |
| `window_mode` | 1 is the default because borderless fullscreen gives stable frame pacing without a display mode switch; 2 is for latency and performance measurements |
| `window_width`, `window_height` | The windowed size. They are ignored by exclusive fullscreen, which uses the display mode |
| `vsync` | On by default. Turn it off only while measuring latency or tearing, because an unlocked editor frame rate makes profiling noisy |

### 3.2 Typography

The typography block is a readability contract: 18 px body text is the floor on a 1080p panel, titles and node captions default larger, and the clamps refuse anything below 16 px.

| Key | Default | Panel slider | Why |
|---|---|---|---|
| `font_panel_title` | 24 | 16 through 36 | A title must separate from body text at a glance |
| `font_panel_body` | 18 | 16 through 30 | The readable floor; raising it is the first accessibility fix |
| `font_tooltip` | 18 | 16 through 30 | Tooltips carry the same load as body text |
| `font_script_node_title` | 21 | 16 through 32 | Node titles sit on a busy canvas |
| `font_script_pin_input` | 18 | 16 through 28 | Pin labels are dense; keep them at or above the floor |
| `font_script_pin_output` | 18 | 16 through 28 | Same as input pins |
| `font_scene_text` | 18 | 16 through 40 | Scene text is read against art, so it needs more headroom |

The panel ranges are deliberately narrower than the file clamps: the panel keeps a safe band, the file accepts an extreme for a specific display.

A legacy migration is part of the decoder. A project that still carries exactly the old combination (20 px title, 16 px body, 16 px tooltip, 18 px node title, 16 px pins, 16 px scene text) is upgraded once to the 24/18/18/21/18/18/18 defaults and the migration is logged. A project that changed any single value keeps its authored sizes, so the migration can never overwrite a deliberate choice.

Font size changes take effect when the font atlas is reloaded, which is the next launch or a hot reload.

### 3.3 Viewport diagnostics

These are the two keys introduced with the diagnostic viewport modes. Both are described in full in section 4.

### 3.4 Authoring

| Key | Default | Notes |
|---|---|---|
| `watch_assets` | true | The OS watcher plus hot reload. Keep it on in a team so an import by a colleague reaches you without a restart |
| `auto_refresh_browser` | true | Refreshes the Content Browser on a database change. A very large project can turn it off and refresh by hand |
| `show_grid` | true | The scene grid. Turn it off for screenshot passes |
| `save_ini_on_commit` | true | Rewrites the INI mirror after a commit. Turning it off leaves the mirror stale while the project file stays current |
| `show_demo` | false | The ImGui demo window. Useful when building editor UI, noise otherwise |

### 3.5 Theme

`theme` stores the palette preset name. The 1.0 editor ships the built-in `graphite` palette; the value is persisted and mirrored so a future palette set can read it without a format change.

### 3.6 Editor accessibility

The eight `ide_*` keys configure the editor window only; the game's own profile is a separate part of the project file.

| Key | Default | Effect |
|---|---|---|
| `ide_blind_mode` | false | Keyboard navigation with an always-visible focus ring and a narration sink that a screen reader can drain |
| `ide_deaf_mode` | false | Visual status cues replace editor audio alerts |
| `ide_flash_guard` | true | Limits the luminance change of the editor background per frame |
| `ide_high_contrast` | false | High-contrast palette at a 4.5:1 minimum |
| `ide_reduce_motion` | false | Removes the blinking caret and similar repeated motion |
| `ide_color_vision` | 0 | 0 none, 1 protanopia, 2 deuteranopia, 3 tritanopia |
| `ide_color_vision_strength` | 1.0 | Blend of the correction; the panel slider is disabled when the deficiency is none |
| `ide_flash_threshold` | 0.20 | Largest allowed luminance change per frame; the panel slider covers 0.02 through 0.45 and the file clamp allows up to 1.0 |

`ide_flash_threshold` is the only accessibility key with a conditional widget: the slider is disabled while `ide_flash_guard` is off, because the threshold has no meaning without the guard.

## 4. The two viewport keys

### 4.1 viewport_view_mode

| Property | Value |
|---|---|
| TOML | `[editor] viewport_view_mode` |
| INI | `viewport_view_mode` |
| Field | `viewportViewMode` |
| Type | int, stored as a uint32 in the struct |
| Default | 0 |
| Clamp | 0 through 5, and any invalid value becomes 0 |
| Persisted by | The view mode dropdown in the viewport toolbar and the combo in the Editor Settings panel |

The value is the `Rhi::ViewportMode` enumeration:

| Value | Mode | Short name | What it is for |
|---|---|---|---|
| 0 | Standard (Lit) | Standard | The shipped image |
| 1 | Unlit / Wireframe | Unlit | Art, UV and pivot checks |
| 2 | Collision Overlap | Collision | Collision participation audit |
| 3 | Shader Complexity / Overdraw | Overdraw | Fill and light cost |
| 4 | Light Complexity | Lights | Number of lights per sprite |
| 5 | SDF Visualizer | SDF | Baked distance field agreement |

The clamp exists twice on purpose. `ClampViewportViewMode` in `EditorSettings.cpp` is the one helper that knows the valid range, and both the TOML decoder and the INI writer call it, so the project file and the mirror cannot disagree. The `Rhi` enumeration is append-only, so the number keeps its meaning across engine versions.

### 4.2 viewport_complexity_budget

| Property | Value |
|---|---|
| TOML | `[editor] viewport_complexity_budget` |
| INI | `viewport_complexity_budget` |
| Field | `viewportComplexityBudget` |
| Type | int, stored as a uint32 in the struct |
| Default | 8 |
| Clamp | 1 through 64 |
| Panel widget | Slider 1 through 64 |

The budget is the denominator of the Shader Complexity heat ramp: a sprite whose overlap count times its light count reaches the budget is drawn fully red. It is a diagnostic reference, not a quality setting. Lower it to expose heavy sprites earlier, raise it on a deliberately dense scene, and keep one value per project so two screenshots are comparable.

A zero budget would divide by zero, which is why the clamp starts at one, and the presenter additionally treats a zero as one before dividing.

## 5. The editor.ini mirror

`editor.ini` is written next to the project file. The writer keeps every other section intact, removes the previous `[FemboyEditor]` section, and appends a fresh one, so the section is always complete and never duplicated.

A default project produces this section:

```ini
[FemboyEditor]
ui_scale = 1.000000
font_panel_title = 24.000000
font_panel_body = 18.000000
font_tooltip = 18.000000
font_script_node_title = 21.000000
font_script_pin_input = 18.000000
font_script_pin_output = 18.000000
font_scene_text = 18.000000
vsync = true
show_demo = false
auto_refresh_browser = true
watch_assets = true
show_grid = true
window_mode = 1
window_width = 1600
window_height = 900
viewport_view_mode = 0
viewport_complexity_budget = 8
save_ini_on_commit = true
ide_blind_mode = false
ide_deaf_mode = false
ide_flash_guard = true
ide_high_contrast = false
ide_reduce_motion = false
ide_color_vision = 0
ide_color_vision_strength = 1.000000
ide_flash_threshold = 0.200000
theme = "graphite"
```

Floats are written with six decimal places by the string conversion used for the mirror, booleans as `true` and `false`, and the theme as a quoted string. The mirror reflects the clamped values: `window_mode` is written as at most 2 and `viewport_view_mode` goes through `ClampViewportViewMode`, so the mirror can never advertise a value the editor would refuse.

The key names in the mirror match the TOML key names for everything except `vsync`, whose TOML home is `[render]` but whose INI home is `[FemboyEditor]`. That single divergence is deliberate: the INI is a flat editor-environment dump, the project file groups the render keys together.

## 6. Panel ranges versus file clamps

Where the panel is narrower than the file, the file is the authority and the panel is the safe path. This is the complete list of divergences:

| Key | File clamp | Panel widget |
|---|---|---|
| `ui_scale` | 0.5 through 3.0 | Slider 0.75 through 2.0 |
| `font_panel_title` | 16 through 48 | Slider 16 through 36 |
| `font_panel_body` | 16 through 40 | Slider 16 through 30 |
| `font_tooltip` | 16 through 40 | Slider 16 through 30 |
| `font_script_node_title` | 16 through 40 | Slider 16 through 32 |
| `font_script_pin_input` | 16 through 40 | Slider 16 through 28 |
| `font_script_pin_output` | 16 through 40 | Slider 16 through 28 |
| `font_scene_text` | 16 through 64 | Slider 16 through 40 |
| `ide_flash_threshold` | 0.02 through 1.0 | Slider 0.02 through 0.45, disabled when the guard is off |

A hand-edited file value above the panel maximum is honoured on load and shown in the panel at its own maximum, so opening the panel and moving the slider pulls the value back into the panel band. That is intentional: the panel is an authoring tool, not a validator.

## 7. Changing a setting correctly

The struct is the single file, so a new setting has one route. In order:

1. Add the field to `EditorSettings` in `EditorSettings.hpp`, with its default and a short comment that states the unit or the meaning of the value.
2. Add the key to the encoder in `EncodeProjectToml` and the matching read plus clamp to `DecodeProjectToml`, in the same order as the struct.
3. Add the key to `AppendEditorSettingsIni` so the mirror stays complete.
4. Add the widget to the Editor Settings panel and choose its panel range; keep it inside the file clamp.
5. Add the field to the infrastructure test that round-trips the project file and checks the INI mirror.
6. Add a row to this reference, then run the drift check.

The drift check is part of the documentation generator:

```bash
python Documentation/Tools/GenerateReferenceDocs.py --check
```

It reads the `[editor]` table from `ProjectConfig.cpp` and the members of `EditorSettings`, and fails with the exact missing key or field when this reference or the Russian edition falls behind the struct. Run it after every settings change; it is the cheapest way to keep the documentation honest.

## 8. Recommended values

| Situation | Suggested values |
|---|---|
| Default workstation, 1080p | `ui_scale` 1.0, typography defaults, `viewport_view_mode` 0 |
| 1440p or 4K editor, small UI | `ui_scale` 1.25 through 1.5, `font_panel_body` 18 through 20 |
| Accessibility review | `ui_scale` 1.25 or more, `font_panel_body` 20, `font_tooltip` 20, `ide_high_contrast` on, `ide_blind_mode` on |
| Performance profiling | `vsync` off, `window_mode` 2, `viewport_view_mode` 3 or 4, `viewport_complexity_budget` at the per-project reference |
| Screenshot or trailer pass | `show_grid` off, `viewport_view_mode` 0, `ui_scale` back to the team default |
| Shared team project | Keep `watch_assets` and `auto_refresh_browser` on, and commit the project file rather than the INI mirror |

## 9. Related documents

- [Viewport Display Modes](ViewportModes_EN.md): what each view mode shows, the exact colour legend, the collision ladder, render masks, lights and time stop.
- [Project Settings Recommendations](ProjectSettingsRecommendations_EN.md): the project keys that sit beside these editor keys.
- [Application Guide](FemboyEngineApplicationGuide_EN.md): the editor workflow these settings affect.
- [Accessibility](../../design/accessibility.md): the shared profile and the frame path that consume the `ide_*` and accessibility keys.
- [Lighting](../../design/lighting.md): the solver whose cost the viewport modes measure.
