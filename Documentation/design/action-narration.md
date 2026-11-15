# Action narration

Action narration is the speech channel for gameplay events: a pickup, a door, a
hit, a state change, an enemy entering a radius. Unlike the interface, these
lines are **not shown on screen**. They are authored once as localization
entries and spoken from the same table, so a project can reuse them later for
captions without writing the text twice.

## Why it is not the screen reader

The OS screen reader reads the accessibility tree: widgets, focus, names. It has
no idea that a door opened behind the player. The engine's own narrator owns
that channel. When the bridge is active the two must not double-speak: the
project keeps `narration_focus` for the game and lets the OS reader handle the
interface; action narration stays on, because it has no interface node.

## Model

`ActionEvent` is a POD published by gameplay systems. `ActionNarration` resolves
the template, applies plural rules and spatial grammar, filters by policy, and
queues the result in the `Narrator`.

| Field | Meaning |
|---|---|
| `TemplateId` | Localization id of the wording; variant lookup uses `FindVariant` with `Count` |
| `SubjectNameId`, `ObjectNameId` | Localized names injected as `{subject}` and `{object}` |
| `Count` | Positive values enable plural variants and `{count}` |
| `DistanceMeters` | Negative means unknown; otherwise `{distance}` and `{meters}` |
| `AngleRadians`, `HasDirection` | 0 is ahead, positive is clockwise; eight sectors |
| `Category` | `NarrationCategory`: interaction, combat, item, navigation, state, system |
| `Priority` | 0..3, mapped onto `NarratorPriority` |
| `DedupeKey` | Repeated keys inside the window are dropped before composing |

## Templates

Templates are ordinary localization strings:

```toml
[en]
1200 = "{subject} picked up {count} {object}"
1201 = "{subject} picked up one {object}"
1202 = "{subject} is {direction}, {distance}"
```

Placeholders: `{subject}`, `{object}`, `{count}`, `{direction}`, `{distance}`,
`{meters}`. An unknown token is left visible on purpose so an author sees the
typo in the log. An empty fragment collapses its neighbouring spaces and a space
before punctuation, so a missing object name never produces "Item ,".

Plural variants keep the `LocalizationTable` convention: `base + 1` is "one",
`+2` "few", `+3` "many", `+4` "two", `+5` "zero", `base` is "other". Russian and
English therefore work without new schema.

## Spatial words

`{direction}` and `{distance}` are words, not raw numbers, because speech is a
slow channel and "near" is more useful than "2.4 metres" while moving.
`ActionVocabulary` carries the localized ids:

```cpp
Fby::ActionVocabulary ScopedVocabulary;
ScopedVocabulary.Directions[0] = LocIds::Ahead;
ScopedVocabulary.Directions[2] = LocIds::Right;
ScopedVocabulary.Near = LocIds::Near;
ScopedVocabulary.Medium = LocIds::Medium;
ScopedVocabulary.Far = LocIds::Far;
Kernel.SetActionVocabulary(ScopedVocabulary);
```

The eight sectors are clockwise from ahead: ahead, ahead right, right, behind
right, behind, behind left, left, ahead left. A zero id removes the word instead
of leaking an id into speech. Distance thresholds are project settings
(`narration_action_near`, `narration_action_far`); `{meters}` stays available
for a game that prefers numbers.

## Policy

`ActionNarrationSettings` lives in `AccessibilityProfile::actions` and is stored
as `[accessibility] narration_action_*` keys:

| Key | Default | Effect |
|---|---|---|
| `narration_actions_enabled` | true | layer master switch |
| `narration_action_categories` | 63 | bit mask of categories |
| `narration_action_direction` | true | resolve `{direction}` |
| `narration_action_distance` | true | resolve `{distance}` |
| `narration_action_min_priority` | 0 | lowest spoken priority, 3 is critical only |
| `narration_action_near` | 3.0 | metres that still map to "near" |
| `narration_action_far` | 12.0 | metres that separate "medium" from "far" |
| `narration_action_dedup_ms` | 1200 | deduplication window |

`PublishAction()` requires Blind mode and `narration_world`; a project that
silences world events also stops the physics and tag lines that share the log.
The layer is main-thread only, like the narrator.

## Captions

The composed line is returned by `Compose()` before it is queued. A game that
also shows captions can push the same buffer into its subtitle canvas, so Deaf
mode and Blind mode describe the event with one authored string.

## Testing

`fby_action_narration_test` builds a localization pack in memory, checks plain
substitution, direction and distance buckets, English and Russian plurals,
unknown tokens, category and priority gates, deduplication and the kernel
integration. Run it with the rest of the suite.
