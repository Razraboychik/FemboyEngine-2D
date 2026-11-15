# Plugin architecture and accessibility packs

This document is the plan for engine plugins. The first concrete plugin is
**Rainbow Palette**, **NVG** and **Thermal**, the runtime accessibility packs. They are not editor plugins:
the editor already owns its own palette, text and symbol settings.

## Decision: core service, plugin presets

Accessibility must not depend on a plugin that can fail to load. The engine keeps
the following in core:

- `AccessibilityProfile` and the runtime theme model;
- the 16-slot semantic palette and role mapping;
- text colour, opacity, outline, size and contrast fields;
- the RHI/UI consumers that apply them.

The accessibility packs are the optional **palette and preset provider**. It can be a data pack
(`.fbytheme` / TOML) for shipped games and a native plugin for development-time
preset logic. A missing pack falls back to the built-in default palette; the
game never loses its text contrast or its safety settings because a DLL failed.

This is deliberate: the current plugin ABI exposes ECS systems and allocation,
not renderer or UI hooks. A plugin cannot safely intercept every draw call,
glyph and panel. The correct injection point is a small registration API, not a
renderer patch.

## Current implementation slice

This branch now has the first slice in the engine:

- `Source/Engine/UI/AccessibilityTheme.hpp/.cpp`:
  - 16 semantic slots and `AccessibilityTextStyle`;
  - `Default`, NVG, thermal, traffic, protan/deutan/tritan and extended traffic
    constructors;
  - a project/game `AccessibilityThemeRegistry` with owner-scoped removal and a
    `UiThemeAsset` bridge.
- `Source/Engine/UI/AccessibilityEffects.hpp/.cpp`:
  - 80-colour palette extensions (64 hue/luminance + 16 neutral);
  - NVG/thermal effect resources with pattern, intensity, blur, sharpness,
    generation and five temperature bands (tag path + colour).
- Plugin ABI **6**:
  - optional `FbyAccessibilityApi` table in `FbyHostApi`;
  - append-only `const FbyVoiceApi* voice` at the end of `FbyHostApi`; the ABI
    version is unchanged, old plugins ignore the field and new ones detect it
    with `FbyHostHasVoice()`; the table has its own `struct_size`/`abi_version`
    and only registers data (`Source/Plugins/VoicePack` is the reference pack);
  - `register_theme`, `set_theme`, `register_palette_extension`,
    `register_effect` and `set_effect`;
  - plugin-owned resources are removed when the plugin is unloaded or reloaded;
  - the phase enum now matches `Fby::Ecs::Phase` exactly, including
    `FBY_PHASE_UI` and `FBY_PHASE_RENDER`.
- Accessibility plugins:
  - `Source/Plugins/RainbowPalette` — 80-colour extension `Rainbow.Palette80` (8x8 hues/shades plus 8 light and 8 dark neutrals);
  - `Source/Plugins/ThemedPalettes` — six 80-colour theme packs: `Palette.Halloween`,
    `Palette.Winter`, `Palette.Autumn`, `Palette.Spring`, `Palette.Terminal` and
    `Palette.Paper`; each is ten curated seed families expanded to eight shades;
  - `Source/Plugins/Nvg` — generation/pattern profiles `Nvg.Gen1.Scanlines`
    through `Nvg.Gen4.Clean`;
  - `Source/Plugins/Thermal` — standard, high-contrast, ironbow, rainbow, white-hot, black-hot and generation-1 thermal
    profiles with five PascalCase tag-mapped temperature bands;
  - `Source/Plugins/Alcohol` — tipsy/drunk/wasted/stalker profiles built from a
    layered effect pool (double vision, wobble, blur, chromatic aberration,
    vignette, desaturation, afterimage, pulse, sway, scale breathing);
  - `Source/Plugins/Radiation` — `Radiation.Low`, `Radiation.High` and
    `Radiation.Critical` screen-noise profiles with `Vision.Radiation`,
    `Accessibility.Vision.Radiation` and `State.Radiation` tags;
- Voice preset packs:
  - `Source/Plugins/VoicePack` — helium, swamp and cursed-mumble presets
    registered through `FbyVoiceApi`; the pack is data-only, unloads cleanly and
    stays inactive on a host without a voice registry;
- Editor:
  - **View > Plugin Manager** lists the plugin files next to the editor
    executable and only the open project's loaded plugins;
  - **Connect & load** copies the selected file into `<project>/plugins/`, loads
    it and records the project-relative path in `project.femproj`;
  - the per-entry checkbox enables or disables a plugin without removing it;
    **Forget** unloads the plugin and deletes the copied file;
  - paths are relative to the project root and validated before loading;
  - `[[plugins]]` entries in `project.femproj` carry `path` and `enabled`;
  a new project starts with no plugin entries (all packs are opt-in);
- Engine:
  - `Fby::Plugin::DiscoverPluginFiles()` lists the loadable libraries in a
    directory and `PluginHost::LoadDirectory()` loads all of them, so a game
    can bring up its packs without an editor or a project descriptor;
  - the editor uses the same discovery result and adds the project binding on
    top of it.
  - opening another project unloads the previous project's plugins.

The visual consumer is now wired:

- `AccessibilityEffectConsumer` maps an active profile onto `PostFxSettings`;
- the composite shader applies NVG tint/patterns, the five-band thermal ramp,
  and the alcohol pool (double vision, wobble, blur, chromatic split, vignette,
  desaturation, afterimage, pulse, sway and scale breathing);
- `AccessibilityKernel::SetEffectRegistry()` lets the editor PIE and the game
  frame read the same active profile;
- the editor stores the selected effect in `project.femproj` as
  `[accessibility] active_effect`, so it is a project binding, not a global
  plugin side effect.

The remaining split is the runtime text stack: it still needs to apply the
`AccessibilityTextStyle` fields to game text. The scene effect path itself is no
longer just metadata.

## Plugin contract today

`Source/Engine/Core/PluginAPI.h` is a plain-C ABI with:

- `fby_plugin_info`, `fby_plugin_load`, `fby_plugin_unload`;
- component layout hashes;
- host allocation, logging and system registration;
- reload/unload through `PluginHost`.

### Phase contract (ABI 6)

`FbyPhase` mirrors `Fby::Ecs::Phase` one-to-one:

| Plugin constant | Engine phase |
| --- | --- |
| `FBY_PHASE_PRE_UPDATE` | `Phase::PreUpdate` |
| `FBY_PHASE_FIXED_UPDATE` | `Phase::FixedUpdate` |
| `FBY_PHASE_POST_UPDATE` | `Phase::PostUpdate` |
| `FBY_PHASE_UI` | `Phase::Ui` |
| `FBY_PHASE_RENDER` | `Phase::Render` |

The host rejects out-of-range phases instead of clamping them. There is no
fallback from the old numeric `Render` value; built-in plugins are rebuilt
against ABI 6. `Phase::Ui` is the HUD phase that keeps running while the
simulation graph is paused.

Render-derived contracts are not plugin extension points:

- `MotionHistory2D` is owned by `FrameComposer`; it is not serialized and a
  plugin must not treat it as a gameplay component.
- `FramePacket` spans point at composer-owned storage for the current frame; a
  plugin must not retain a packet across frames.
- diagnostic `ViewportStats` is only filled in non-Standard viewport modes, so a
  plugin should not read it as a gameplay signal.

That contract is enough for gameplay systems and content packs. It is not enough
for Rainbow, so the plan adds a versioned **accessibility extension table**.

### Accessibility extension (ABI 6)

Append an optional table to `FbyHostApi`; the host checks `struct_size` and
`abi_version` before reading it. ABI 6 is a clean break: plugins are rebuilt
against the matching header, not kept binary-compatible with ABI 5.

```c
typedef struct FbyPalette16 { uint32_t colors[16]; } FbyPalette16;

typedef struct FbyThemeDesc {
    const char* name;
    const char* display_name;
    uint32_t flags;
    FbyPalette16 colors;
    uint32_t text_color;
    uint32_t text_outline_color;
    float text_opacity;
    float text_outline_size;
    float text_contrast;
} FbyThemeDesc;

typedef struct FbyAccessibilityApi {
    uint32_t struct_size;
    uint32_t abi_version;
    int (*register_theme)(void* user, const FbyThemeDesc* theme);
    int (*set_theme)(void* user, const char* name);
    int (*set_runtime_text)(void* user, uint32_t color, float opacity, float contrast,
                            uint32_t outline_color, float outline_size);
} FbyAccessibilityApi;
```

The host copies and validates every string and colour, clamps floats, and rejects
duplicate names. A plugin may call these functions from `fby_plugin_load` or
from a registered system; it never receives a renderer, a font atlas or a panel
pointer. All calls are main-thread unless a future thread-safe registry says
otherwise.

## Identifiers, tags and ABI

`FbyThemeDesc`, `FbyPaletteExtensionDesc` and `FbyEffectDesc` contain two
different kinds of strings:

- **id** — a stable resource key (`Rainbow.Palette80`, `Nvg.Gen1.Scanlines`,
  `Thermal.Standard`);
- **tags** — gameplay/project tags (`Vision.Nvg.Generation1`,
  `Temperature.VeryCold`, `Accessibility.Vision.Thermal`).

Both use dotted PascalCase segments. There are no lowercase resource or tag
names; lowercase strings in a plugin are a bug, not a naming preference.

**ABI** means *Application Binary Interface*: the binary contract between the
engine and a plugin DLL/SO. It is not the C++ source API. It fixes the layout of
`FbyHostApi`, the function-pointer types, the entry-point symbols and the
version number. The plugin and host must be compiled against compatible headers
and the same standard-library ABI. That is why the headers are plain C, the
tables have `struct_size`, and the loader rejects an ABI mismatch instead of
calling a function through a stale pointer.

## Rainbow data model

One 16-slot palette is the global authoring surface. Slots are semantic, not
literal colour names:

| Slot | Meaning |
|---:|---|
| 0 | background |
| 1 | surface |
| 2 | surface raised |
| 3 | border |
| 4 | text |
| 5 | text muted |
| 6 | primary |
| 7 | secondary |
| 8 | accent |
| 9 | success |
| 10 | warning |
| 11 | danger |
| 12 | info |
| 13 | axis X |
| 14 | axis Y |
| 15 | highlight / plot series |

The palettes are:

- **Global 16** — every slot is editable with a colour picker and persisted by
  name; the game can ship several named palettes.
- **NVG** — green monochrome with luminance preserved; it is a world/scene
  palette, not a full-screen green tint over UI text.
- **Thermal** — ironbow/black-body ramp for scene luminance; UI keeps semantic
  contrast so labels remain readable.
- **Traffic light** — gray, green, yellow, orange, red; distinct luminance and
  labels, not colour alone.
- **Traffic-light variants** — protanopia, deuteranopia and tritanopia mappings
  that keep the five meanings but choose separable hues/luminance.
- **Extended traffic light** — gray, green, yellow, orange, red with a second
  neutral ramp for disabled and background states.
- **Condition presets** — astigmatism, myopia, hyperopia, glaucoma and cataract
  adjust text size, contrast, opacity and outline; they do not apply a global
  colour matrix.

Names are stable ids (`Rainbow.Nvg`, `Rainbow.Thermal`, `Rainbow.Traffic.Protan`,
`Rainbow.Extended`), while display names are localisable.

## Themed palettes

The engine already owns the runtime theme model: `AccessibilityTheme` with its
16 semantic slots (including slot 15, the highlight colour), text size, contrast
and outline, and the `AccessibilityThemeRegistry` that plugins can write through
`register_theme`. Material highlighting is a renderer/game concern; a plugin
cannot add a render pass or a material.

`ThemedPalettes` is therefore a palette-content plugin, not a second theme
system. It registers six 80-colour extensions through
`register_palette_extension`:

| Id | Mood |
|---|---|
| `Palette.Halloween` | pumpkin, bone, blood, moss, candle, purple, rust, fog, ember, shadow |
| `Palette.Winter` | ice, snow, pine, twilight, steel, frost, deep night, silver, aurora, black |
| `Palette.Autumn` | amber, rust, gold, moss, chestnut, wine, wheat, olive, fog, charcoal |
| `Palette.Spring` | blossom, leaf, sky, butter, lavender, mint, soil, rain, coral, warm white |
| `Palette.Terminal` | phosphor, dark green, light green, amber, amber dark, cyan, gray, black, acid, red |
| `Palette.Paper` | parchment, ink, cream, sepia, faded blue, brown, gray, gold, rose, black |

Each family expands to eight shades, so every extension fills all 80 slots.
Selecting one changes the palette the game and the particle presets resolve
against; the semantic theme and its highlight slot stay in core. A theme pack is
still possible as a content plugin (`register_theme` plus a matching palette),
but it does not need new engine code and is separate from this palette pack.

## Alcohol effect pool

The alcohol pack is a **pool**, not a hardcoded “drunk” shader. Each profile is
a set of independent layers, each with an effect type, intensity and frequency:

| Layer | Typical use |
|---|---|
| `DoubleVision` | horizontal ghost image, the literal “double” effect |
| `Wobble` | low-frequency screen-space distortion |
| `Blur` | loss of fine detail |
| `ChromaticAberration` | colour fringes that increase with intensity |
| `Vignette` | narrowing peripheral vision |
| `Desaturation` | muted colour response |
| `Afterimage` | short motion trails |
| `Pulse` | slow brightness/scale breathing |
| `Sway` | camera/world roll |
| `ScaleBreathing` | subtle zoom/scale oscillation |

Profiles: `Alcohol.Tipsy`, `Alcohol.Drunk`, `Alcohol.Wasted` and
`Alcohol.Stalker`. The game can scale the profile intensity, choose a subset of
layers, or use its own project binding; the plugin only supplies the resource
pool. This avoids the common mistake of making one full-screen screen-warp that
cannot be tuned for different gameplay states.

## Injection points

| Layer | Responsibility |
|---|---|
| Accessibility core | Stores the active theme, validates text fields, exposes `ApplyToFrame` |
| RHI composite | Quantises scene colours through the active 16-slot LUT after lighting, before UI |
| UI/theme service | Resolves semantic tokens for game HUD, dialogs and subtitles |
| Text stack | Applies font size, text colour, opacity, outline and contrast per text style |
| Accessibility plugins | Registers palettes/presets and switches the active theme; no direct GPU calls |
| Editor | Keeps its own built-in theme settings; never loads runtime accessibility packs |

World and UI are separate: a thermal/NVG palette must not turn subtitles into a
low-contrast gradient. Text uses the semantic palette unless the game explicitly
selects the same palette for UI.

## Project-scoped plugin files

Project plugins live under the project root:

```toml
[[plugins]]
path = "plugins/rainbow_palette/fby_rainbow_palette.dll"
enabled = true
```

The editor resolves `path` against the project root, rejects absolute paths and
`..`, and unloads the previous project's plugins before loading the new list.
The same list is what a game build reads; the editor's own test fixtures are not
project plugins.

## Lifecycle

1. Engine starts with `DefaultAccessibilityProfile()` and the built-in Graphite
   palette.
2. `PluginHost` loads the project accessibility packs before the first rendered frame when enabled.
3. Packs register themes/effects and optionally select one; invalid entries are
   rejected without unloading the engine.
4. A theme switch is applied at the next frame boundary, never mid-frame.
5. Reload replaces the plugin's registrations atomically; the previous active
   theme remains until a valid replacement arrives.
6. Unload removes pack-owned themes/effects and falls back to the built-in palette.

Shipping builds should normally use the data pack only; native plugin loading
stays a development/editor feature unless the game opts into foreign code.

## Documentation set

`plugins.md` is the entry point. Each plugin gets a section with:

- purpose and non-goals;
- required ABI version and host services;
- data files and stable ids;
- frame/thread rules;
- failure fallback;
- tests and acceptance criteria.

The accessibility packs are the first entries; future plugins append rather than replace this file.

## Implementation phases

1. **Core model** — palette struct, 16 slots, text style, validation, default
   fallback, unit tests.
2. **Runtime application** — RHI scene LUT, UI semantic tokens, text style
   application, frame-boundary switching.
3. **Plugin ABI 6** — exact phase table (`Ui`/`Render`), optional accessibility
   table, registration/set-theme/extension/effect calls, layout and lifetime
   checks, plugin fixture.
4. **Rainbow Palette / NVG / Thermal packs** — 80-colour extension, NVG patterns, thermal bands,
   condition presets, localisation-ready names.
5. **Documentation and tests** — this document, plugin loading test, palette
   contrast test, missing-plugin fallback, hot-reload test.

## Acceptance criteria

- a game with no accessibility pack still has readable default text and UI;
- every palette has 16 colours and passes a contrast check for text roles;
- NVG/thermal affect scene colours without destroying HUD readability;
- traffic-light variants are distinguishable by hue and luminance;
- text colour, opacity, outline colour/size and contrast are independent;
- loading, reloading and unloading a pack never leaves a dangling pointer;
- the editor's built-in palette is unaffected by the runtime plugin.
