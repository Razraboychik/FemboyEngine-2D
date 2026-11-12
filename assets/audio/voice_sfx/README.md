# Voice reaction cues

The six WAV files in this directory are generated deterministically by
`Source/Tools/VoiceSfxGen` from the recipes in
`Source/Engine/Audio/VoiceSfxSynth.cpp`:

```bash
cmake --build build --target fby_voice_sfx_gen
build/bin/tools/fby_voice_sfx_gen assets/audio/voice_sfx
```

They contain no third-party samples. The files are original output of the
repository tool and are distributed under MPL-2.0 together with the engine.

| File | Use |
|---|---|
| `bubble-small.wav` | a single small bubble |
| `bubble-big.wav` | a larger bubble or a water exit |
| `bubble-cluster.wav` | submerged speech, rate-limited |
| `splash.wav` | entering water |
| `explosion-muffled.wav` | a muted participant, played quietly |
| `radio-click.wav` | a mute or unmute transition |

The editor synthesizes the same cues into memory at startup; the WAV files exist
for projects that want to cook them through the normal asset pipeline. If a
project replaces a cue with a CC0 recording, record the source URL and license
here and add the asset to `third_party/notices.toml` before shipping it.
