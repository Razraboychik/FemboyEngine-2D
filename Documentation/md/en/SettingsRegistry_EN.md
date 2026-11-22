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

The bus list is developer data. The engine provides the registry and the DSP
graph; it does not impose a fixed set of categories.
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
and never hardcodes a setting id or type. The Project Settings mixer already uses
this generated panel for its master/SFX/music values and keeps the project fields
in sync after undo.

## 10. Persistence and UI

Project defaults, per-install user overrides, the generated settings panel and
per-project audio-bus data are separate layers. The registry is the single
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
