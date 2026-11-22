# Accessibility Output Policies

A game can expose independent render and audio policies for players who need one
sensory channel without the other. The engine provides the policy boundary and
the safe-mode recovery; the developer decides whether to expose it and what the
default is.

## 1. Policies

```cpp
Fby::Output::RenderPolicy
    Full
    NoWorld
    Disabled
    Headless

Fby::Output::AudioPolicy
    Full
    CaptionsOnly
    Disabled
```

`NoWorld` keeps the interface, input and accessibility layer alive while the
world pass is skipped. `Disabled` skips the channel entirely. `Headless` also
removes the window and swapchain.

## 1a. Registering the settings

`Settings::RegisterOutputPolicySettings` registers `render.policy` and
`audio.policy` as dangerous user settings with safe values `full`, recovery text
and a recovery hotkey. A game can hide them, expose them in its accessibility
profile, or restrict them to a developer menu.

## 1b. Editor exposure

Project Settings renders the generated accessibility section and an
`Output policies (safe mode)` section. Both use the same registry panel: dangerous
policy values open a confirmation modal with the recovery hotkey, while ordinary
accessibility values apply immediately. Editor Settings renders its generated
subset in the same way.

## 2. Render-disabled mode

When the render policy is `Disabled`, the game should skip world composition,
shadows, post-processing, upscaling, frame generation and GPU readback. It must
keep:

- window and input;
- simulation and audio;
- accessibility narration;
- the recovery hotkey and safe-mode path.

The current engine exposes the policy and documents the required call sites. A
game or editor decides where its world pass is skipped; no hidden engine pass may
depend on a rendered frame.

## 3. Audio-disabled mode

`AudioSystem::SetProcessingEnabled(false)` is the audio calculation switch. While
disabled:

- the mixer and limiter are not run;
- the device callback receives silence;
- reverb, ducking, voice processing and decoding workloads are avoided;
- sound events, captions and gameplay code continue to run.

`SetProcessingEnabled(true)` restores processing without recreating the game
world. A game may use `CaptionsOnly` to keep sound events and subtitles while
skipping audible output.

## 4. Safe mode

The dangerous combination is both channels disabled. The game must refuse to
persist it, force session scope, or start in safe mode. `Policy::IsPlayerVisible()`
reports whether at least one output is available.

Recovery path:

```text
--safe-mode
FBY_SAFE_MODE=1
recovery hotkey
crash-loop fallback
```

`Settings::Registry::RestoreSafeMode()` resets dangerous descriptors to their
safe values and drops user overrides. `Output::Policy::RestoreSafeMode()` restores
both channels to `Full`.

## 5. Developer responsibilities

- Provide an audio-only menu path before offering render-disabled mode.
- Provide captions and visual cues before offering audio-disabled mode.
- Never make gameplay depend on a rendered frame or an audible callback.
- Mark the settings dangerous, confirm them and document the recovery hotkey.
- Test re-enable after device loss, resize and a long idle period.
- Do not promise GPU-class performance if CPU simulation remains the bottleneck.

## 6. Tests

The automated test covers independent policy transitions, both-disabled
detection, safe-mode restore and the audio processing switch with a silent mix.
A game should add a manual pass for menus, captions, TTS, hotplug and re-enable.
