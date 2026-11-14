# Voice packs (own voices) and MPL 2.0

The engine is MPL 2.0. Voice packs let a project speak with its own voices
without linking a phonemizer, a model runtime or an audio codec into the
engine. The pack's **helper is a separate process**: the engine starts it, sends
newline-delimited commands to its stdin and never loads its code. That boundary
keeps third-party licences (including GPL) outside the MPL terms of the engine.

## Pack layout

```text
Saved/VoicePacks/piper-en-us/
  voicepack.ini
  piper_helper.exe        (piper_helper on Linux/macOS)
  en_US-amy.onnx
  LICENSE.txt             (the pack's own licences)
```

`Saved/VoicePacks` is the default root; `FBY_VOICE_PACKS` overrides it. A layout
with one pack per subdirectory is expected; a root that is itself a pack
directory also works.

## voicepack.ini

```ini
id = piper-en-us
name = Piper English (US)
language = en-US
license = MIT (helper), CC-BY-4.0 (model)
helper = piper_helper.exe
model = en_US-amy.onnx
```

| Key | Required | Meaning |
|---|---|---|
| `id` | yes | stable id; exposed as `pack:<id>` |
| `name` | yes | shown in the Voice combo |
| `language` | yes | BCP-47 tag, used for the "auto by language" search |
| `license` | no, recommended | free-form licence tag shown to the user |
| `helper` | yes | executable path, relative to the pack directory |
| `model` | yes | model path, relative to the pack directory; must exist |

A pack with a missing file or field is skipped and counted in the scan log. The
registry is a **scanner**: enumerating voices never executes a helper, so a
broken pack cannot run code just by being listed.

## Helper protocol

The engine starts the helper with:

```text
"<helper>" --voice "<pack id>" --model "<model path>"
```

and then writes to its stdin (UTF-8, `\n` terminates control lines):

```text
VOICE <pack id>
RATE <rate>
SPEAK <byte length>
<utf-8 text>
STOP
QUIT
```

`SPEAK` is length-prefixed, so the text may contain any byte except the
terminating newline. The helper owns playback: it may use any audio stack and
any licence. The engine does not wait for it and does not read its output
(stdout and stderr go to the null device). `STOP` cancels the current utterance;
`QUIT` asks the helper to exit, and the engine closes stdin before terminating
it as a last resort.

The reference helper for tests is `fby_tts_voicepack_helper`; it records the
protocol so `fby_tts_voicepack_test` can assert it without an audio device.

## Selecting a pack

Packs appear in the Voice combo next to OS voices as `pack:<id>` with their
language. Selecting one stores the id in `narration_voice` and routes speech to
the pack; selecting an OS voice clears the pack route. A language request
(`SetVoice`) prefers the OS synthesizer and falls back to the first pack whose
language matches the primary subtag.

## MPL 2.0 rules

1. **Do not link a helper or a model into the engine.** That includes static
   libraries, in-process ONNX runtimes and GPL phonemizers. The pipe is the
   boundary; changing it changes the licence analysis.
2. **Do not add GPL headers or sources to this repository.** A helper under
   GPL-3.0 stays an independent program. If a distribution bundles it, ship its
   source or a written offer as the GPL requires, keep its licence file next to
   it and list it in the third-party inventory.
3. **MPL files stay MPL.** New engine files (including `VoicePack.cpp` and
   `Tts.hpp`) are covered by the repository's MPL 2.0 licence. A modified
   engine file must remain under MPL 2.0 and its source must be available to
   recipients, as the licence already requires for every engine file.
4. **Packs are data, not engine code.** A pack's model and helper may use any
   licence; `license` is stored and shown but never interpreted. The pack's
   `LICENSE.txt` is the authoritative text.
5. **No vendor blobs in the engine tree.** Ship packs as downloadable content or
   as a separate package directory. The engine repository must not start
   containing GPL binaries, model weights or their derivatives.
6. **Trust boundary.** A helper runs with user privileges when speech starts.
   Packs are user-installed programs; the launcher should show the `license` and
   the origin before enabling one.

Example: Piper is MIT and its phonemizer `espeak-ng` is GPL-3.0. A helper that
bundles both can be distributed as a GPL program and run as a separate process;
the engine stays MPL 2.0 because it only writes bytes to the helper's stdin.
Model voices carry their own licences and are listed per pack.

## Troubleshooting

| Symptom | Cause |
|---|---|
| Pack missing from the list | `voicepack.ini` lacks a field, or `helper`/`model` does not exist; the scan log counts it as skipped |
| "helper could not be started" | wrong path, missing execute bit, or a helper built for another architecture |
| Speech silent but no error | the helper started and exited; check its own log next to the model |
| `pack:` id rejected | the pack is not installed, or the id is mistyped |

## Verification

`fby_tts_voicepack_test` covers manifest parsing (including a skipped broken
pack), enumeration, `pack:` selection, rate propagation, the exact pipe payload,
`STOP`/`QUIT`, and the layered backend with no OS synthesizer present.
