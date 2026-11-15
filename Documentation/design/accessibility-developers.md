# Accessibility for developers

This page is the integration checklist for a game and for the editor. Player
documentation lives in [accessibility for players](accessibility-players.md);
the platform and contract details are in [accessibility](accessibility.md).

## Editor workflow

Open **Project Settings → Accessibility**. The game section has three parts:

1. **Narrator (game)**: enable speech, focus/role/state/scene switches, world
   events, rate, queue depth, language and voice.
2. **Action narration (game)**: enable gameplay actions, per-category mask,
   direction and distance words, minimum priority, near/far distances and the
   repeat-suppression window.
3. **Screen reader bridge (game)**: publish the interface to the OS tree and
   choose focus, structure and state events.

The **Voice** combo lists installed voices with their language and any packs
(`pack:<id>`), plus "Auto" and a refresh entry. It is the quickest way to see
whether a language has a voice on this machine.

The editor's own narrator is configured in **Editor Settings**: it has its own
voice, rate and focus policy so a game's profile never changes the editor.

## Code checklist

```cpp
Fby::AccessibilityKernel Kernel;
Kernel.ApplyProfile(ScopedProject.accessibility);      // settings, voice, bridge
Kernel.SetActionLocalization(&ScopedLocTable);         // templates and names
Kernel.SetActionVocabulary(ScopedVocabulary);          // eight directions + distances
Kernel.SetTtsBackend(Fby::NativeTtsBackend());         // OS voices and packs
Kernel.SetBlindMode(true);
Kernel.StartBridge(ScopedWindow);                      // Windows; no-op elsewhere
```

Per frame, on the main thread:

```cpp
Kernel.ProjectCanvas(ScopedCanvas);   // tree, focus narration, OS bridge
Kernel.FlushSpeech();                 // drains narrative events into the narrator
Kernel.Actions().Advance(DeltaTime);  // advances the action deduplication clock
```

Gameplay systems publish a `Fby::ActionEvent` instead of a formatted string; the
layer resolves the template, plural rules and spatial words. The template ids
and the plural convention are described in
[action narration](action-narration.md).

`FlushSpeech()` is a hard main-thread contract. SAPI is apartment-threaded and
AVSpeech owns a queue; never call the narrator from a worker.

## Settings and persistence

The game stores everything in its project accessibility profile. The editor
writes the same keys to the project file and mirrors its own settings to
`editor.ini`. The full key table is in
[editor settings reference](../md/en/EditorSettingsReference_EN.md); the
narration keys are `narration_*` and `narration_action_*`.

## Languages

The engine documents 42 languages and resolves the best installed voice for the
content locale: exact tag, primary subtag, English, first installed voice. The
catalog and the rules are in [speech languages](speech-languages.md). To add a
language, extend `SpeechLanguages.cpp`, the table in that document, and
`fby_speech_languages_test`.

## Verifying an integration

- `fby_accessibility_narrator_test`: tree, projection, narrator policy, voice
  resolution, bridge stub policy.
- `fby_speech_languages_test`: catalog, normalization, fallback and voice
  selection for all 42 languages.
- `fby_action_narration_test`: templates, plurals, spatial words, categories,
  deduplication.
- `fby_tts_voicepack_test`: manifest, `pack:` routing and the helper protocol.
- `fby_tts_thread_test` and `fby_ui_accessibility_test`: main-thread contract,
  subtitles and blind/deaf policy.

Run them through `ctest -R "accessibility|narration|speech|tts"` after a build.

## Platform notes

| Platform | Speech | Screen reader bridge |
|---|---|---|
| Windows | SAPI + one-core (WinRT) | UI Automation, implemented |
| Linux | speech-dispatcher, fail-closed | AT-SPI2 provider implemented (libdbus); compile/runtime verification in Linux CI |
| macOS | AVSpeech | NSAccessibility provider implemented; macOS compile/runtime verification pending |

The engine never links a voice pack helper: packs are separate processes and
their licences stay outside the engine's MPL 2.0 terms. See
[voice packs](voice-packs.md).

## Player-facing integration

A game should expose:

- a first-run path that can be completed without sight;
- an options screen with narrator, language, voice, rate, action categories and
  subtitles;
- a documented shortcut to mute or repeat speech;
- a way to install or point at voice packs, with the pack licence visible.

The engine provides the switches and the speech; the game owns discovery.
