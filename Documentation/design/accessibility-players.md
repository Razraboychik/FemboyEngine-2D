# Accessibility for players

This page is for the person playing a game built with FemboyEngine2D. It lists
what the engine can speak, how to turn it on and what has to be installed.

## What the game can speak

- **Interface**: the focused widget, its role and state, menus and dialogs.
- **Gameplay actions**: pickups, doors, combat callouts, state changes, enemies
  entering a radius. These lines are not shown on screen; they are spoken.
- **Captions**: the same event text can be shown as subtitles for Deaf mode.
- **Screen readers** (Windows): the interface is published to UI Automation, so
  Narrator, NVDA and JAWS can read it with their own voice and commands.

The narrator and the screen reader are independent. A player can use both, but
then the interface is read twice; most screen-reader users keep the game's focus
narration off and use only action narration.

## Turning it on

A game stores the policy in its project accessibility profile, and a
well-integrated game exposes the same switches in its options screen:

| Setting | Default | What it does |
|---|---|---|
| Narrator | on | master switch for the game's speech |
| Interface narration | on | focus moves, role, state |
| World events | on | gameplay and physics callouts |
| Action narration | on | localized gameplay event templates |
| Action categories | all | interaction, combat, items, navigation, state, system |
| Speech rate | 1.0 | 0.5 through 2.0 |
| Language | content locale | the language the game speaks, not the OS language |
| Voice | auto | a specific installed voice |
| Subtitles | off | visible captions; independent from speech |
| Screen-reader bridge | on (Windows) | publish the interface to UI Automation |

Blind players need a way to reach these switches without sight. The engine
provides the settings and the speech, but the game must expose them: a first-run
spoken prompt, a documented shortcut, or an options screen that a screen reader
can already navigate. If a game ships with speech off and only a checkbox to
enable it, that checkbox is unreachable for the player who needs it.

## Language and voices

The engine documents 42 languages and resolves a voice for the content locale:
exact language first, then the same language without the region, then English,
then any installed voice. The full list and the fallback rules are in
[speech languages](speech-languages.md).

The engine does not download voices. Install them for the platform:

- **Windows**: Settings, Time and language, Language and region. Add the
  language and its speech pack. Desktop SAPI voices and modern one-core voices
  are both used.
- **macOS**: System Settings, Accessibility, Spoken Content, System Voice,
  Manage Voices. Download the language.
- **Linux**: install `speech-dispatcher` and the espeak-ng module for the
  language; `spd-say -L` lists what is available.

If a language has no installed voice, the game still runs: the log reports the
fallback and the voice list shows what exists.

## Voice packs

A game can offer additional voices as packs. A pack is a directory installed
under `Saved/VoicePacks` (or the directory the `FBY_VOICE_PACKS` environment
variable points to). It appears in the voice list as `pack:<id>` with its
language. Packs are user-installed programs: only install packs from a source
you trust, and check the licence shown next to the pack.

## When something is wrong

| Symptom | What to check |
|---|---|
| Silence | narrator switch, speech volume, and that a voice for the language exists |
| Wrong voice | the voice list; an explicit voice wins over the language |
| Interface read twice | turn off the game's interface narration or the screen reader's own review mode |
| Language not spoken | install the language pack or pick a different language/voice |
| Pack missing | `voicepack.ini`, helper and model files must all exist |
| Screen reader sees no window | the bridge exists on Windows; the macOS and Linux providers are added but need platform verification |

## Privacy

Speech is generated locally by the operating system or by a voice pack. The
engine does not send text to a network service.
