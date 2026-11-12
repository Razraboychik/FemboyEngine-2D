# Voice presets, moderation policy and reactive SFX

Status: 0.4.0-alpha.1 tree. This document describes the voice preset registry,
the listener-side policy that applies it, the append-only plugin table that lets a
pack register presets, and the reactive SFX layer. The DSP itself is described in
[voice.md](voice.md); the chat contract is in [chat.md](chat.md).

## Scope

The preset layer owns three things:

1. a data format (`.fbyvoice`) and a fixed-capacity registry of immutable
   `VoiceEffectProfile` values;
2. the listener-side policy that decides which preset a participant hears;
3. a small reactive layer that turns voice state into local sound cues.

It does not own the DSP chain, the codec, the transport or the simulation. The
registry hands out values; every rack copies them, so a preset can be replaced or
removed while audio keeps running.

## Runtime flow

```text
microphone -> AEC -> denoise/AGC -> uplink chain -> Opus -> FVOX
FVOX -> jitter -> decode/PLC/FEC -> participant rack -> ring -> Voice bus
                                        ^
                     VoicePresetRegistry (values)
                          ^              ^
              .fbyvoice data pack   VoicePack plugin

participant state -> VoiceReactiveSfx -> Sfx bus (spatial, local only)
```

The uplink chain is optional and only for effects the speaker wants everyone to
hear. The downlink chain is where moderation, local preferences and local
environment live. SFX never enter the `FVOX` packet.

## Registry

`Fby::VoicePresetRegistry` holds up to 64 presets. An entry is:

```cpp
struct VoicePresetDesc
{
    char Id[64];
    char NameKey[96];
    VoicePresetCategory Category;
    std::uint32_t Flags;
    float MinIntensity;
    float MaxIntensity;
    VoiceEffectProfile Profile;
};
```

Rules:

- ids use `A-Z`, `a-z`, `0-9`, `.` and `-`; they are stable across reloads and
  are the only value persisted for a listener choice;
- duplicate ids, an unknown category, an unknown flag or a profile that cannot be
  clamped are rejected with a message;
- `Add(desc, owner)` records the owner so unloading a plugin removes exactly its
  presets; built-ins are added with no owner and stay;
- the registry is not touched on the audio thread. Applying a preset copies the
  profile into the rack and therefore never shares memory with the registry.

## Data format

A `.fbyvoice` file is TOML:

```toml
version = 1
id = "Game.Voice.Cave"
name_key = "voice.preset.cave"
category = "environment"          # environment|moderation|cosmetic|character|scene
flags = ["cosmetic", "uplink", "downlink"]
min_intensity = 0.2
max_intensity = 1.0
enable_conditions = true

[[slot]]
kind = "underwater"
mix = 1.0
low_cut_hz = 140.0
high_cut_hz = 700.0
resonance = 1.35
wow_hz = 0.7
wow_depth = 0.35
```

Slot keys by kind:

| Kind | Keys |
|---|---|
| `mute` | `fade_seconds` |
| `muffle` | `low_cut_hz`, `high_cut_hz`, `resonance`, `drive_db`, `wobble_hz`, `wobble_depth`, `breath_level` |
| `underwater` | `low_cut_hz`, `high_cut_hz`, `resonance`, `wow_hz`, `wow_depth` |
| `radio` | `low_cut_hz`, `high_cut_hz`, `static_level`, `crackle_rate`, `squelch`, `drive_db`, `mute_when_squelched` |
| `pitch` | `semitones`, `window_ms` |
| `formant` | `semitones`, `strength_db` |
| `distortion` | `drive_db`, `low_cut_hz`, `high_cut_hz`, `bit_depth` |
| `ring-modulation` | `frequency_hz`, `depth` |
| `whisper` | `noise_level`, `low_cut_hz`, `high_cut_hz` |

Every slot also takes `mix`. At most eight slots are accepted, matching
`kVoiceEffectMaxSlots`. `flags` and `enable_conditions` are optional: a file that
omits `flags` registers a cosmetic downlink preset, and conditions default to on.
Unknown keys are ignored, so a newer pack still loads on an older engine; an
unknown `kind` is rejected.

`LoadVoicePresetToml()` parses text, `LoadVoicePresetFile()` reads a path. Both
take an owner pointer so the caller can remove the entries on unload.

## Built-in catalog

`VoicePresetRegistry::RegisterBuiltIns()` adds 24 presets:

| Category | Presets |
|---|---|
| Environment | `Fby.Voice.Underwater`, `DeepWater`, `Swamp`, `SubmarineRadio` |
| Moderation | `Fby.Voice.Muted`, `ModerationMumble` |
| Cosmetic | `Fby.Voice.ImitationMumble`, `CursedMumble` |
| Character | `Chipmunk`, `Helium`, `SqueakyToy`, `Deep`, `Giant`, `Satan`, `Robot`, `Alien`, `Ghost`, `OldRadio`, `WalkieTalkie`, `Telephone`, `Megaphone`, `Arcade` |
| Scene | `Drunk`, `TapeWobble` |

All of them compose the existing effect kinds; no new DSP kind is required. Pitch
presets stay within ±12 semitones even though the parameter clamp allows ±24,
because the granular shifter degrades above that range.

## Listener policy

`VoiceEffectPolicy` is owned by the session or the game:

```cpp
struct VoiceEffectPolicy
{
    char ModerationPresetId[64]; // "Fby.Voice.ModerationMumble"
    char HardMutePresetId[64];   // "Fby.Voice.Muted"
    bool MumbleWhenModerated;    // true: a muted player is heard as mumbling
    bool AllowLocalUplink;       // the listener may apply an identity effect
};
```

`ResolveVoicePreset()` is pure: it takes the registry, policy, session and the
listener's choice, and returns a preset plus a clamped intensity. A participant
with `LocalMuted` resolves to the moderation preset even when the listener picked
a cosmetic one. `BuildVoicePresetProfile()` scales every slot mix by the
intensity; `VoicePresetApplier` pushes the result into `VoiceSystem` only when the
resolved id, intensity or moderation flag changed, because
`VoiceEffectRack::Configure()` resets filter history.

Two mumble presets are deliberate:

- `ModerationMumble` is forced by policy and carries a "muted" badge; it is not
  in the user picker;
- `ImitationMumble` is a voluntary cosmetic effect the speaker may enable for
  everyone; the UI shows it as a normal preset.

A hard mute stays available as `Muted`; a game that prefers silence sets
`MumbleWhenModerated = false`.

## Plugin contract

The service is append-only and does not change `FBY_PLUGIN_ABI_VERSION`:

```c
const FbyVoiceApi* voice; /* last field of FbyHostApi, nullable */
```

- a plugin detects the service with `FbyHostHasVoice(host)`, which checks both the
  host structure size and the null pointer;
- `register_preset` receives a flattened POD descriptor; the host copies every
  field before returning, so plugin memory never outlives the call;
- the host validates and clamps the descriptor; a rejected preset returns 0 and
  logs a warning;
- unloading the plugin removes its presets through the owner pointer;
- a plugin that requires voice and finds no service returns 0 from
  `fby_plugin_load` and stays inactive instead of crashing.

`Source/Plugins/VoicePack` is the reference pack: it registers `Pack.Voice.Helium`,
`Pack.Voice.Swamp` and `Pack.Voice.CursedMumble` through this table.

## Reactive SFX

`VoiceReactiveSfx` is listener-side. It reads only local state:

- `Talking` and `LocallyMuted` from `VoiceSession`;
- `Submerged` from `VoiceSystem::ParticipantCondition()`;
- the participant's position and pan.

It emits at most eight events per update:

| Event | Trigger | Limits |
|---|---|---|
| `Splash` | a participant enters water | global cooldown |
| `BubbleCluster` | a submerged participant starts talking | per-speaker cooldown (2.5 s) |
| `BubbleCluster` | a participant leaves water | global cooldown |
| `ExplosionMuffled` | moderation mute is applied | global cooldown |
| `RadioClick` | moderation mute is removed | global cooldown |

Settings: `MasterGain`, per-kind gain, `SpeakerCooldownSeconds`,
`GlobalCooldownSeconds`, and individual toggles. Every gain is clamped to 0..1 and
the caller applies its own voice/SFX limiter. Nothing is triggered by a remote
peer, and nothing is sent over the network.

## Procedural cues and assets

`SynthesizeVoiceSfx()` renders the built-in cues deterministically at 48 kHz:

| Cue | Recipe | Length |
|---|---|---|
| `BubbleSmall` | sine glide 300 -> 900 Hz, 6 ms attack, noise bed | 0.18 s |
| `BubbleBig` | sine glide 120 -> 400 Hz | 0.30 s |
| `BubbleCluster` | four overlapping bubbles | 0.70 s |
| `Splash` | band-pass noise plus a 170 Hz thump | 0.35 s |
| `ExplosionMuffled` | low-passed noise plus a 45 Hz thump | 0.80 s |
| `RadioClick` | click burst plus static tail | 0.12 s |

`samples` are normalized to a 0.8 peak. `Source/Tools/VoiceSfxGen` writes the
same recipes to `assets/audio/voice_sfx/*.wav`; the editor synthesizes them into
memory at startup and registers them as `AudioSystem` clips, so no file access
happens on the frame path.

The cues are generated by this repository, not sampled from a third party. No
CC0 download is required; if a project replaces them with CC0 assets, keep the
source URL and license in the asset directory and in `third_party/notices.toml`
when the files ship.

## Why SFX are not mixed into voice

`FVOX` carries Opus frames with DTX, FEC and a jitter buffer tuned for speech.
Injecting a sound there would:

- break the codec assumptions and the PLC/FEC recovery path;
- give a remote peer a direct way to trigger loud audio on another machine;
- make the SFX follow voice volume, mute and deaf settings incorrectly;
- prevent spatial placement at the speaker's world position.

The reactive layer keeps the two paths separate: voice stays voice, SFX stay on
the Sfx bus and are placed with `EvaluateVoiceSpatial()` / `SetVoiceSpatial()`.

## Tests

- `voice_presets`: registry add/find/remove, duplicate and invalid ids, built-in
  catalog, TOML loading (valid file, version check, unknown flag/kind, duplicate
  id), intensity scaling, policy resolution for moderation and cosmetics, and
  the `FbyHostHasVoice()` capability probe;
- `voice_reactive_sfx`: deterministic synthesis, peak and finiteness, event
  emission, cooldowns, disabled switch, bounded pan/gain;
- `plugin_reload`: the VoicePack loads, registers three presets, unload removes
  them and a host without a voice registry keeps running;
- `settings`: the listener choice persists in `Saved/Config/UserSettings.toml`
  under `[voice]` (`preset`, `intensity`, `moderation_mumble`, `reactive_sfx`,
  `reactive_sfx_gain`).

## Non-goals

- arbitrary per-sample DSP callbacks from plugins;
- SFX inside `FVOX`;
- automatic voice-to-text or moderation decisions;
- long-tail reverb in a per-slot delay line (a shared send is the future path);
- shipping a foreign plugin in a game build without an explicit opt-in.
