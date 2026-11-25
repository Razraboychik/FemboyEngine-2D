# Settings Registry

The settings registry is the constructor a game uses to declare every setting it
exposes. The engine does not own the categories, defaults, colours or audio buses;
those are developer data and callbacks registered at startup.

## 1. Schema

```cpp
Fby::Settings::Registry Registry;
int Applied = 0;

Registry.Float("audio.master")
    .Category("Audio")
    .Label("Master")
    .Unit("dB")
    .Default(Fby::Settings::Value::MakeFloat(-3.0))
    .Range(-60.0, 0.0)
    .Scope(Fby::Settings::Scope::User)
    .Apply([&Applied](const Fby::Settings::Value& Value)
    {
        (void)Value;
        ++Applied;
    });

Registry.Bool("render.world")
    .Category("Rendering")
    .Default(Fby::Settings::Value::MakeBool(true));

Registry.Freeze();
```

`Freeze()` builds the stable id index. No setting may be added after it. The
descriptor span is immutable for the rest of the process.

## 2. Typed values

The core supports boolean, integer, floating point, colour, bounded string and
enum values. A descriptor declares its type, default and optional range. Values
of the wrong type fall back to the default; numeric values are clamped; colours
are clamped per channel; enums must name one of the declared entries.

## 3. Scopes

```text
Project  developer default and policy
User     per-install player override, only when UserEditable is set
Session  non-persistent debug or cheat value
```

The effective value is chosen in this order:

```text
session > user (if editable) > project > descriptor default
```

A project value is authoritative when the setting is not user-editable. A
dangerous setting never applies without an explicit confirmation flag.

## 4. Metadata

Descriptors carry category, label, tooltip, unit, enum labels, range, scope and
flags:

```text
UserEditable
RequiresRestart
Dangerous
RequiresConfirmation
Advanced
Hidden
ServerAuthoritative
```

`Dangerous` descriptors also carry confirmation text and a recovery hotkey. The
registry does not invent a display order or a colour. The game's UI layer decides
how categories are presented.

Every descriptor may carry a `LocalizationKey`; the label is a developer fallback,
not the translated text. `MakeSnapshot()` captures the effective values in
descriptor order for a hot path. A runtime system keeps integer indices and reads
`Snapshot::Float(index)`, `Bool(index)` or `ColorValue(index)` without a string
lookup per frame.

## 5. Apply callbacks

A setter stores the value and, for a non-dangerous setting, invokes its apply
callback immediately. `Apply(id)` re-applies the effective value; `ApplyAll()`
walks the frozen descriptor list. Dangerous settings require a confirmation and
are applied through the same callback after the confirmation step.

## 6. Audio buses

A game registers one setting per audio bus instead of relying on fixed master,
SFX and music fields:

```cpp
for (const BusDef& Bus : GameBuses)
{
    Registry.Float("audio.bus." + Bus.Id)
        .Category("Audio")
        .Default(Fby::Settings::Value::MakeFloat(Bus.DefaultDb))
        .Range(-60.0, 6.0)
        .Scope(Fby::Settings::Scope::User)
        .Apply([Bus](const Fby::Settings::Value& Value)
        {
            Audio.SetBusVolumeDb(Bus.Id, static_cast<float>(Value.FloatValue));
        });
}
```

`ProjectSettings.audioBuses` stores the same topology in `project.femproj`:

```toml
[[audio.buses]]
id = "voice"
name = "Dialogue"
parent = "master"
default_db = -2.0
```

The bus list is developer data. On project open the editor creates the declared
DSP buses, maps each one to a generated dB setting and falls back to the legacy
master/SFX/music values when the list is empty. The engine does not impose a
fixed set of categories.
`Audio::RegisterAudioBusSettings` registers one user-editable dB setting per
declared bus and maps each setting to `AudioSystem::SetBusVolumeDb`.

## 7. Theme colours

Colour roles are ordinary colour settings:

```cpp
Registry.ColorSetting("theme.ui.accent")
    .Category("Theme")
    .Default(Fby::Settings::Value::MakeColor({0.3f, 0.6f, 1.0f, 1.0f}))
    .Scope(Fby::Settings::Scope::User);
```

The game declares the roles and themes. The player changes only the roles the
developer exposes. No engine palette is authoritative.

## 8. Safe mode

`RestoreSafeMode()` resets every dangerous descriptor to its safe value and drops
user and session overrides for those descriptors. It is intended for a
`--safe-mode` launch, a recovery hotkey or a crash-loop fallback. It does not
touch non-dangerous settings.

## 9. Generated panel model

`Editor::SettingsPanelModel` builds rows from the frozen descriptor span. It
filters hidden and advanced entries, applies a search over id, label and category,
exposes `LocalizationKey`, confirmation text and recovery hotkey, and routes edits
through the selected scope. `Reset` removes the override and re-applies the
effective value. `SettingsRegistryPanel` is the ImGui renderer: it groups rows by
category, draws the typed widget, uses a confirmation modal for dangerous values
and never hardcodes a setting id or type. Enum descriptors render as a combo, so
policies are editable; dangerous enum changes use the same confirmation modal.
The Project Settings mixer already uses this generated panel for its
master/SFX/music values and keeps the project fields in sync after undo. The
accessibility and editor sections call a bidirectional sync before drawing. The
legacy controls for UI scale, VSync, panel text, line spacing, log files, undo
memory, frame limit, theme and the migrated accessibility fields are no longer
drawn; the generated section owns those controls.

## 10. Persistence and UI

Project defaults, per-install user overrides, the generated settings panel and
per-project audio-bus data are separate layers. `UserSettings.audioBuses` stores
per-install dB overrides keyed by the same stable bus id; the project file owns
the bus topology and the user file owns the player's values. The optional ImGui interaction test is built with
`-DFBY_BUILD_UI_TESTS=ON` and target `fby_settings_panel_engine_test`. It uses
the official ImGui Test Engine and performs a real combo click that changes the
enum. The Test Engine hooks require `IMGUI_ENABLE_TEST_ENGINE`; the test target
must be built explicitly because normal editor executables do not link the Test
Engine runtime. The optional `fby_settings_fuzz` target is built with
`-DFBY_BUILD_FUZZERS=ON`; its seed corpus lives in
`Source/Tests/FuzzCorpus/Settings`. It exercises project decode, re-encode and
reload without touching the filesystem. Windows uses a static-CRT fuzz build:
`-DFBY_FUZZ_LIBFUZZER=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` links the
real libFuzzer runtime (the COE shim only satisfies the runtime's unused
continue-on-error symbols) and the target has been run for 20,000 executions.
Without that option the same target falls back to a standalone corpus runner.
Linux/macOS Clang links `-fsanitize=fuzzer,address`. The existing accessibility profile is
registered through
`Editor::RegisterAccessibilitySettings`, so its serialized fields become ordinary
registry descriptors without changing their project storage.
`Editor::RegisterEditorSettings` exposes the editor UI scale, body font, text
spacing, log/undo budgets, frame limit, VSync and theme through the same registry;
`EditorSettings` remains the serialized authority. The registry is the single
schema those layers read. `SaveSettingsToml` and `LoadSettingsToml` write only
values for one scope and ignore unknown keys for forward compatibility. The
editor and game UI should build their controls from `Descriptors()`.

## 11. Current limits

- The registry does not create audio buses; the game maps bus ids to its DSP
  graph.
- A live settings panel must be built on top of the descriptor span.
- A setting that needs a restart must say so; the registry rejects late writes
  for such a setting only after an apply has occurred.
- Dangerous values loaded from user files are rejected; safe mode is the only
  path that restores them.
